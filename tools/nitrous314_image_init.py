"""Read-only Windows image mapping witness, before any game instruction.

SEC_IMAGE_NO_EXECUTE maps authenticated speed.exe without executing its
entrypoint/imports. This measures initial image bytes only, not an event0
runtime value or absence of later aggregate/indirect writers.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import struct

from event0_trace import Image, digest_file, manifest
from nitrous314_recovery import EXE_SHA


def mapped_initial_words(path, image):
    if os.name != 'nt':
        raise RuntimeError('Windows SEC_IMAGE_NO_EXECUTE witness requires Windows')
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                  ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CreateFileMappingW.argtypes = (wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                                         wintypes.DWORD, wintypes.DWORD, wintypes.LPCWSTR)
    kernel.CreateFileMappingW.restype = wintypes.HANDLE
    kernel.MapViewOfFile.argtypes = (wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD,
                                    wintypes.DWORD, ctypes.c_size_t)
    kernel.MapViewOfFile.restype = ctypes.c_void_p
    kernel.UnmapViewOfFile.argtypes = (ctypes.c_void_p,)
    kernel.UnmapViewOfFile.restype = wintypes.BOOL
    kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
    kernel.CloseHandle.restype = wintypes.BOOL
    handle = kernel.CreateFileW(str(path), 0x80000000, 7, None, 3, 0x80, None)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    mapping, view = None, None
    try:
        # PAGE_READONLY | SEC_IMAGE_NO_EXECUTE; no writable/executable request.
        mapping = kernel.CreateFileMappingW(handle, None, 0x11000002, 0, 0, None)
        if not mapping:
            raise ctypes.WinError(ctypes.get_last_error())
        view = kernel.MapViewOfFile(mapping, 4, 0, 0, 0)
        if not view:
            raise ctypes.WinError(ctypes.get_last_error())
        def read(address, count):
            return ctypes.string_at(view + address - 0x400000, count)
        if read(0x465430, 16) != image.read(0x465430, 16):
            raise AssertionError('native image code positive control differs')
        if struct.unpack('<I', read(0x7000E8, 4))[0] != 1:
            raise AssertionError('native raw-backed word positive control differs')
        words = {('%08X' % address): struct.unpack('<I', read(address, 4))[0]
                 for address in (0x7361A0, 0x7361A4, 0x7361A8)}
        if any(words.values()):
            raise AssertionError('initial no-execute image words differ from zero')
        return dict(method='CreateFileMappingW PAGE_READONLY|SEC_IMAGE_NO_EXECUTE; MapViewOfFile FILE_MAP_READ',
                    scope='initial OS image mapping only; no game instructions or event0 writer claim',
                    positive_controls=['465430 original code', '7000E8 raw-backed word1'], words=words)
    finally:
        if view and not kernel.UnmapViewOfFile(view):
            raise ctypes.WinError(ctypes.get_last_error())
        if mapping:
            kernel.CloseHandle(mapping)
        kernel.CloseHandle(handle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    root, out = args.root.resolve(), args.out.resolve()
    if out == root or root in out.parents or out in root.parents:
        parser.error('output must be outside input root')
    out.mkdir(parents=True, exist_ok=False)
    before = manifest(root)
    (out / 'before.json').write_bytes(before)
    try:
        path = root / 'speed.exe'
        if digest_file(path) != EXE_SHA:
            raise ValueError('unsupported executable SHA-256')
        report = mapped_initial_words(path, Image(path.read_bytes()))
        report['executable_sha256'] = EXE_SHA
        (out / 'initial-image.json').write_text(json.dumps(report, indent=2) + '\n', encoding='ascii')
        print('PASS: actual initial no-execute Windows image words ' + json.dumps(report['words'], sort_keys=True))
        print('Scope: before game instructions; ordinary event0 values remain unproved')
    finally:
        after = manifest(root)
        (out / 'after.json').write_bytes(after)
        if before != after:
            raise RuntimeError('retail input tree changed')
        print('PASS: retail manifests identical SHA256 ' + hashlib.sha256(before).hexdigest().upper())


if __name__ == '__main__':
    main()
