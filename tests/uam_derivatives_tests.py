"""Fail-closed compiler-data correction, without game assets or a GPU."""

import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[1] / "tools/uam_derivatives.py"
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("uam_derivatives", TOOL)
fix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fix)

TGSI = """FRAG
DCL IN[0].xyz, GENERIC[0], PERSPECTIVE
DCL CONST[0][0]
DCL CONST[1][0..6]
 14: DDX TEMP[10].xyz, IN[0].xyzz
 15: MUL TEMP[11], CONST[0][0].xxxx, IN[0].xyzz
 16: DDY TEMP[12].xyz, TEMP[11]
 17: END
"""


def fixture():
    blob = bytearray(768)
    struct.pack_into("<6I", blob, 0, 0x48534B44, 24, 256, 512, 24, 1)
    struct.pack_into("<6I", blob, 24, 1, 48, 17, 256, 16, 0)
    blob[256:512] = bytes(range(256))  # instruction bytes must never change
    return bytes(blob)


class Derivatives(unittest.TestCase):
    def test_only_constant_changes(self):
        raw = fixture()
        fixed = fix.process(raw, TGSI)
        self.assertEqual(fixed[:512], raw[:512])
        self.assertEqual(fixed[516:], raw[516:])
        self.assertEqual(struct.unpack_from("<f", fixed, 512)[0], 1.0)
        self.assertEqual(fix.process(fixed, TGSI), fixed)
        self.assertEqual(fix.process(fixed, TGSI, check=True), fixed)
        with self.assertRaisesRegex(ValueError, "flat lighting would be skipped"):
            fix.process(raw, TGSI, check=True)

    def test_refuse_drift(self):
        for text in (
                TGSI.replace("FRAG", "VERT"),
                TGSI.replace("CONST[0][0].xxxx", "CONST[0][0].yyyy"),
                TGSI.replace("TEMP[12].xyz, TEMP[11]", "TEMP[12].xyz, TEMP[10]"),
                TGSI + " 18: MOV TEMP[0], CONST[0][0]\n",
                TGSI.replace("DCL CONST[0][0]", "DCL CONST[0][0..1]")):
            with self.subTest(text=text), self.assertRaises(ValueError):
                fix.process(fixture(), text)
        for position, value in ((0, 0), (4, 20), (8, 128), (12, 0),
                                (16, 255), (20, 2), (24, 5), (28, 512),
                                (36, 48), (36, 512), (40, 32),
                                (512, 0x40000000), (516, 1)):
            blob = bytearray(fixture())
            struct.pack_into("<I", blob, position, value)
            with self.subTest(position=position, value=value), self.assertRaises(ValueError):
                fix.process(bytes(blob), TGSI)
        for length in (0, 23, 255, 767):
            with self.subTest(length=length), self.assertRaises(ValueError):
                fix.process(fixture()[:length], TGSI)

    def test_vertex_is_unchanged(self):
        blob = bytearray(fixture())
        struct.pack_into("<I", blob, 24, 0)
        struct.pack_into("<2I", blob, 36, 0, 0)
        self.assertEqual(fix.process(bytes(blob), "VERT\n"), bytes(blob))

    def test_unlit_fragment_is_unchanged(self):
        blob = bytearray(fixture())
        struct.pack_into("<2I", blob, 36, 0, 0)
        self.assertEqual(fix.process(bytes(blob), "FRAG\n0: END\n"), bytes(blob))
        with self.assertRaises(ValueError):
            fix.process(bytes(blob), "FRAG\n0: DDY TEMP[0], IN[0]\n")
        with self.assertRaises(ValueError):
            fix.process(bytes(blob), "FRAG\nDCL CONST[0][0]\n")

    def test_cli(self):
        with tempfile.TemporaryDirectory(prefix="nfsu-uam-test-") as directory:
            root = Path(directory)
            raw, tgsi, output = (root / name for name in ("raw.dksh", "shader.tgsi", "fixed.dksh"))
            raw.write_bytes(fixture())
            tgsi.write_text(TGSI, encoding="ascii")
            def run(*args):
                return subprocess.run([sys.executable, str(TOOL), *map(str, args)],
                                      capture_output=True)
            refused = run("--check", raw, tgsi)
            self.assertNotEqual(refused.returncode, 0)
            self.assertIn(b"FAIL: dFdy multiplier is zero", refused.stderr)
            made = run(raw, tgsi, output)
            self.assertEqual(made.returncode, 0, made.stderr)
            self.assertIn(b"initialized", made.stdout)
            verified = run("--check", output, tgsi)
            self.assertEqual(verified.returncode, 0, verified.stderr)
            self.assertIn(b"verified", verified.stdout)
            self.assertEqual(raw.read_bytes(), fixture())


if __name__ == "__main__":
    unittest.main()
