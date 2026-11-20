"""Build optional 960x720 movie caches from the owner's PC installation."""
import argparse
import ctypes
import ctypes.util
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

WIDTH, HEIGHT = 960, 720
FRAME_BYTES = WIDTH * HEIGHT * 3 // 2
BOOT = ("na_boot.mad", "PSA.mad", "e3_title.mad")
VIDEO_TAGS = (b"MADk", b"MADm", b"MADe")
FILTER = "hqdn3d=1.2:1.0:2:1.5,scale=960:720:flags=lanczos+accurate_rnd,setsar=1,unsharp=5:5:0.45:5:5:0"


def movie_timing(source):
    size = source.stat().st_size
    timings = []
    native_width = native_height = 0
    with source.open("rb") as stream:
        while stream.tell() < size:
            start = stream.tell()
            header = stream.read(8)
            if len(header) != 8:
                raise ValueError(f"{source.name}: partial header")
            tag, length = struct.unpack("<4sI", header)
            if not 8 <= length <= min(1048576, size - start):
                raise ValueError(f"{source.name}: invalid chunk size")
            if tag in VIDEO_TAGS:
                if length < 26:
                    raise ValueError(f"{source.name}: partial video header")
                timing = stream.read(12)
                duration, width, height = struct.unpack("<IHH", timing[4:])
                if not native_width:
                    native_width, native_height = width, height
                if (width, height) != (native_width, native_height):
                    raise ValueError(f"{source.name}: encoded dimensions change")
                if not 65535 <= duration <= 65535000:
                    raise ValueError(f"{source.name}: invalid frame duration")
                timings.append(duration)
            stream.seek(start + length)
    if not timings:
        raise ValueError(f"{source.name}: no video frames")
    return size, timings, native_width, native_height


def compressor(library, frame_bytes=FRAME_BYTES):
    path = Path(library).resolve() if library else None
    # Windows also needs the directory for the DLL's runtime dependencies.
    dll_dir = os.add_dll_directory(str(path.parent)) if path and os.name == "nt" else None
    try:
        libname = str(path) if path else ctypes.util.find_library("lz4")
        if not libname:
            raise ValueError("liblz4 unavailable; pass --lz4 with the library path")
        library = ctypes.CDLL(libname)
        encode = library.LZ4_compress_default
        encode.argtypes = (ctypes.c_char_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int)
        encode.restype = ctypes.c_int
        upper_bound = library.LZ4_compressBound
        upper_bound.argtypes = (ctypes.c_int,)
        upper_bound.restype = ctypes.c_int
        buffer = ctypes.create_string_buffer(upper_bound(frame_bytes))
    finally:
        if dll_dir:
            dll_dir.close()

    def compress(raw):
        size = encode(raw, buffer, len(raw), len(buffer))
        if size <= 0:
            raise ValueError("LZ4 compression failed")
        return buffer.raw[:size]
    return compress


def convert(source, destination, decoder, ffmpeg, compress):
    size, timings, width, height = movie_timing(source)
    command = [ffmpeg, "-hide_banner", "-loglevel", "error", "-threads", "2",
               "-f", "rawvideo", "-pixel_format", "yuv420p",
               "-video_size", f"{width}x{height}", "-i", "-", "-an", "-filter_threads", "2",
               "-vf", FILTER, "-fps_mode", "passthrough", "-pix_fmt", "yuv420p",
               "-c:v", "rawvideo", "-threads", "2", "-f", "rawvideo", "-"]
    with tempfile.NamedTemporaryFile(dir=destination.parent, prefix="movie-",
                                     suffix=".tmp", delete=False) as output:
        temporary = Path(output.name)
        try:
            output.write(struct.pack("<4sIQ", b"NFM1", len(timings), size))
            with subprocess.Popen([decoder, "--export-yuv", str(source)],
                                  stdout=subprocess.PIPE) as native:
                with subprocess.Popen(command, stdin=native.stdout, stdout=subprocess.PIPE) as process:
                    native.stdout.close()
                    try:
                        for duration in timings:
                            pixels = process.stdout.read(FRAME_BYTES)
                            if len(pixels) != FRAME_BYTES:
                                raise ValueError(f"{source.name}: decoded frame count too short")
                            coded = compress(pixels)
                            output.write(struct.pack("<II", duration, len(coded)))
                            output.write(coded)
                        if process.stdout.read(1):
                            raise ValueError(f"{source.name}: decoded frame count too long")
                        if process.wait() or native.wait():
                            raise ValueError(f"{source.name}: decoder or FFmpeg failed")
                    finally:
                        if process.poll() is None:
                            process.kill()
                        if native.poll() is None:
                            native.kill()
            output.close()
            temporary.replace(destination)
        finally:
            output.close()
            if temporary.exists():
                temporary.unlink()
    return len(timings)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("retail", type=Path)
    parser.add_argument("output", type=Path, help="destination Movies720 folder")
    parser.add_argument("--all", action="store_true", help="also convert career and extra movies")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--decoder", required=True, help="built nfsu_intro_movie_tests executable")
    parser.add_argument("--lz4", help="path to liblz4 or a Windows LZ4 DLL")
    args = parser.parse_args()
    movies = args.retail / "Movies"
    if args.output.resolve().is_relative_to(args.retail.resolve()):
        parser.error("use an output folder outside the retail installation")
    selected = sorted(movies.iterdir()) if args.all else [movies / name for name in BOOT]
    selected = [item for item in selected if item.is_file() and
                re.fullmatch(r"[A-Za-z0-9_]+[.]mad", item.name, re.IGNORECASE)]
    if not selected or (not args.all and len(selected) != len(BOOT)):
        parser.error("required original movies are missing")
    compress = compressor(args.lz4)
    args.output.mkdir(parents=True, exist_ok=True)
    failed = []
    for source in selected:
        destination = args.output / (source.stem + ".nfm")
        try:
            count = convert(source, destination, args.decoder, args.ffmpeg, compress)
        except (OSError, ValueError) as error:
            print(f"FAIL {source.name}: {error}", file=sys.stderr, flush=True)
            failed.append(source.name)
            continue
        print(f"{source.name} frames={count} bytes={destination.stat().st_size}", flush=True)
    if failed:
        raise SystemExit("Could not convert " + ", ".join(failed))


if __name__ == "__main__":
    main()
