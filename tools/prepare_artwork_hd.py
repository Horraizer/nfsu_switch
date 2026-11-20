#!/usr/bin/env python3
"""Build ArtworkHD from a local PC 1.4 USA installation."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

from PIL import Image, ImageFilter

from prepare_movies720 import compressor


def prepare(source, output, encode):
    with Image.open(source) as original:
        picture = original.convert("RGBA")
    width, height = picture.size
    # Scale the entire texture, including any padding used by the authored UVs.
    scale = min(2, 1536 / max(width, height))
    size = (round(width * scale), round(height * scale))
    if size[0] <= width or size[1] <= height:
        raise ValueError(f"cannot enlarge {source.name} within the artwork budget")
    scaled = picture.resize(size, Image.Resampling.LANCZOS)
    if picture.getchannel("A").getextrema() == (255, 255):
        scaled = scaled.filter(ImageFilter.UnsharpMask(radius=1, percent=45, threshold=3))
    data = encode(scaled.tobytes())
    header = struct.pack("<4sIHHHHII", b"NFA1", int(source.stem, 16),
                         width, height, *size, len(data), width * height * 4)
    target = output / (source.stem + ".nfa")
    with tempfile.NamedTemporaryFile(dir=output, prefix="art-", suffix=".tmp", delete=False) as stream:
        temp = Path(stream.name)
        try:
            stream.write(header)
            stream.write(data)
            stream.close()
            temp.replace(target)
        finally:
            stream.close()
            temp.unlink(missing_ok=True)
    print(f"{target.name} {width}x{height} -> {size[0]}x{size[1]} {len(data)} bytes")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("retail", type=Path)
    parser.add_argument("output", type=Path, help="destination ArtworkHD folder")
    parser.add_argument("--exporter", type=Path, required=True, help="built nfsu_frontend_art_sources executable")
    parser.add_argument("--lz4", help="path to liblz4 or a Windows LZ4 DLL")
    args = parser.parse_args()
    if args.output.resolve().is_relative_to(args.retail.resolve()):
        parser.error("use an output folder outside the retail installation")
    encode = compressor(args.lz4, 1536 * 1536 * 4)
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="art-sources-", dir=args.output.parent) as temporary:
        subprocess.run([str(args.exporter.resolve()), str(args.retail), temporary], check=True)
        for source in sorted(Path(temporary).glob("*.tga")):
            prepare(source, args.output, encode)


if __name__ == "__main__":
    main()
