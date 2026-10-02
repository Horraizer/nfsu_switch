"""Actual CLI containment refusals using only disposable synthetic directories."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def snapshot(root):
    return {str(path.relative_to(root)): (path.read_bytes() if path.is_file() else None)
            for path in root.rglob("*")}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compare", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve(strict=True)
    fixture = Path(tempfile.mkdtemp(prefix="262-output-guards-", dir=str(work)))
    require(fixture.parent == work, "fixture escaped work directory")
    aliases = []
    try:
        protected = fixture / "protected"
        nested = protected / "nested"
        nested.mkdir(parents=True)
        (nested / "child").mkdir()
        (protected / "sentinel.bin").write_bytes(b"synthetic protected sentinel")
        (nested / "sentinel.bin").write_bytes(b"synthetic nested sentinel")
        outside = fixture / "protected-sibling"
        outside.mkdir()
        before = snapshot(protected)

        def alias(name, target):
            path = fixture / name
            if os.name == "nt":
                result = subprocess.run(["cmd", "/c", "mklink", "/J", str(path), str(target)],
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                require(result.returncode == 0, "cannot create required synthetic junction")
            else:
                path.symlink_to(target, target_is_directory=True)
            aliases.append(path)
            require(path.resolve(strict=True) == target.resolve(strict=True), "alias target differs")
            return path

        root_alias = alias("root-alias", protected)
        nested_alias = alias("nested-alias", nested)
        chained_alias = alias("chained-alias", nested_alias)

        def run(label, root, output, refused):
            result = subprocess.run([str(args.compare.resolve(strict=True)), "--root", str(root),
                                     "--out", str(output)], stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE)
            expected = (b"FAIL: output is inside retail installation" if refused else
                        b"FAIL: event 1 missing or not one race")
            require(result.returncode == 1 and expected in result.stdout and not result.stderr,
                    label + ": wrong refusal: " + json.dumps((result.stdout + result.stderr).decode(
                        "utf-8", errors="replace"), ensure_ascii=True))
            require(not output.exists() and not Path(str(output) + ".partial").exists(),
                    label + ": output or partial created")
            require(snapshot(protected) == before, label + ": protected fixture changed")
            print("PASS:", label)

        run("direct nested", protected, nested / "new.tga", True)
        run("root alias", protected, root_alias / "nested" / "new.tga", True)
        run("nested alias", protected, nested_alias / "new.tga", True)
        run("chained nested alias", protected, chained_alias / "new.tga", True)
        run("missing tail after alias", protected, nested_alias / "absent" / "new.tga", True)
        run("aliased protected root", root_alias, nested_alias / "new.tga", True)
        run("parent within alias", protected, nested_alias / "child" / ".." / "new.tga", True)
        # Win32 normalizes alias/.. lexically before opening it; POSIX walks
        # the symlink target's parent. Test the actual host's write semantics.
        run("parent of alias", protected, nested_alias / ".." / "new.tga", os.name != "nt")
        if os.name == "nt":
            run("case alias", protected, Path(str(nested_alias).upper()) / "new.tga", True)
        run("outside with common prefix", protected, outside / "new.tga", False)
        run("outside missing tail", protected, outside / "absent" / "new.tga", False)
        print("PASS: synthetic output containment guards; protected fixtures unchanged")
    finally:
        # Remove each alias itself before recursive cleanup. Never traverse a junction.
        for path in reversed(aliases):
            if os.name == "nt":
                os.rmdir(str(path))
            else:
                path.unlink()
        require(fixture.parent == work and fixture.name.startswith("262-output-guards-"),
                "refusing cleanup outside owned fixture")
        shutil.rmtree(str(fixture))


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print("FAIL:", str(error).encode("ascii", errors="backslashreplace").decode("ascii"))
        sys.exit(1)