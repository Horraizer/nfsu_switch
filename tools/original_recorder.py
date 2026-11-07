"""Read-only, user-operated recorder for the supported original PC executable."""
import argparse
import ctypes as C
from ctypes import wintypes as W
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import queue
import shutil
import struct
import subprocess
import sys
import threading
import time
import uuid
import zipfile

VERSION = "1.1"
EXE_SHA256 = "c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7"
IMAGE_BASE = 0x400000
FRAME = 0x7364DC
CLOCK = 0x73AD34
WORLD = 0x7361F8
RACE = 0x73619C
CAREER = 0x75EEF8
MAX_BYTES = 1024 * 1024 * 1024
RAW_HEADER = struct.Struct("<dII")


def utc():
    return datetime.now(timezone.utc).isoformat()


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def clean(value):
    if isinstance(value, float) and not math.isfinite(value):
        return None
    if isinstance(value, dict):
        return {str(k): clean(v) for k, v in value.items()}
    if isinstance(value, (tuple, list)):
        return [clean(v) for v in value]
    return value


def encoded(value):
    return json.dumps(clean(value), ensure_ascii=True, allow_nan=False,
                      separators=(",", ":"))


def write_json(path, value):
    Path(path).write_text(encoded(value) + "\n", encoding="ascii")


def make_directory(path, exclusive=False):
    path = Path(path)
    probe = path
    while not probe.exists():
        if not probe.name.isascii():
            raise RuntimeError("New folder names must use ASCII characters")
        if probe.parent == probe:
            raise FileNotFoundError("Output drive is not available")
        probe = probe.parent
    path.mkdir(parents=True, exist_ok=not exclusive)


def field(blob, offset, fmt):
    size = struct.calcsize("<" + fmt)
    if blob is None or len(blob) < offset + size:
        return None
    result = struct.unpack_from("<" + fmt, blob, offset)
    return clean(result[0] if len(result) == 1 else list(result))


def pointer(value):
    return isinstance(value, int) and 0x10000 <= value <= 0x7FFF0000


def decode_path(blob):
    if blob is None or len(blob) < 0x1C0:
        return None
    count = blob[0]
    if count > 20:
        return {"valid": False, "count": count}
    return {"valid": True, "count": count,
            "points": [field(blob, 0x10 + i * 16, "3f") for i in range(count)],
            "roads": list(blob[0x150:0x150 + count]),
            "road_points": [field(blob, 0x164 + i * 2, "H") for i in range(count)],
            "length": field(blob, 0x1AC, "f")}


def decode_actor(address, blob, config, driver):
    return {"address": address, "id": field(blob, 0xA, "h"),
            "role": field(config, 4, "I"), "car_type": field(config, 0x10, "I"),
            "ai_context_raw": field(config, 0x735, "b"),
            "position": field(blob, 0x60, "3f"),
            "velocity": field(blob, 0x70, "3f"),
            "speed_mps": field(blob, 0x3D4, "f"),
            "gear_slot_raw": field(blob, 0x3D8, "i"),
            "rpm": field(blob, 0x3E0, "f"),
            "torque": field(blob, 0x3EC, "f"),
            "support_count": field(blob, 0x3CC, "I"),
            "config_address": field(blob, 0x14, "I"),
            "driver_address": field(blob, 0x30, "I"),
            "controls": {"steering_word": field(driver, 0x1D0, "H"),
                         "gear_command_raw": field(driver, 0x1D4, "i"),
                         "throttle": field(driver, 0x1DC, "f"),
                         "handbrake": field(driver, 0x1E0, "f"),
                         "service_brake": field(driver, 0x1E4, "f")},
            "path": decode_path(driver[0x10:0x1D0] if driver else None),
            "config_readable": config is not None,
            "driver_readable": driver is not None}


class WindowsAPI:
    """No write, suspend, debugger or input APIs are loaded."""
    def __init__(self):
        if os.name != "nt":
            raise RuntimeError("Windows is required")
        self.k = C.WinDLL("kernel32", use_last_error=True)
        signatures = {
            "OpenProcess": ([W.DWORD, W.BOOL, W.DWORD], W.HANDLE),
            "CloseHandle": ([W.HANDLE], W.BOOL),
            "ReadProcessMemory": ([W.HANDLE, C.c_void_p, C.c_void_p,
                                   C.c_size_t, C.POINTER(C.c_size_t)], W.BOOL),
            "GetProcessTimes": ([W.HANDLE] + [C.POINTER(W.FILETIME)] * 4, W.BOOL),
            "QueryFullProcessImageNameW": ([W.HANDLE, W.DWORD, W.LPWSTR,
                                           C.POINTER(W.DWORD)], W.BOOL),
            "GetExitCodeProcess": ([W.HANDLE, C.POINTER(W.DWORD)], W.BOOL),
            "CreateToolhelp32Snapshot": ([W.DWORD, W.DWORD], W.HANDLE),
        }
        for name, (args, result) in signatures.items():
            fn = getattr(self.k, name)
            fn.argtypes, fn.restype = args, result
        class ProcessEntry(C.Structure):
            _fields_ = [("dwSize", W.DWORD), ("cntUsage", W.DWORD),
                        ("th32ProcessID", W.DWORD), ("th32DefaultHeapID", C.c_size_t),
                        ("th32ModuleID", W.DWORD), ("cntThreads", W.DWORD),
                        ("th32ParentProcessID", W.DWORD), ("pcPriClassBase", W.LONG),
                        ("dwFlags", W.DWORD), ("szExeFile", W.WCHAR * 260)]
        class ModuleEntry(C.Structure):
            _fields_ = [("dwSize", W.DWORD), ("th32ModuleID", W.DWORD),
                        ("th32ProcessID", W.DWORD), ("GlblcntUsage", W.DWORD),
                        ("ProccntUsage", W.DWORD), ("modBaseAddr", C.c_void_p),
                        ("modBaseSize", W.DWORD), ("hModule", W.HMODULE),
                        ("szModule", W.WCHAR * 256), ("szExePath", W.WCHAR * 260)]
        self.ProcessEntry, self.ModuleEntry = ProcessEntry, ModuleEntry
        for name in ("Process32FirstW", "Process32NextW"):
            fn = getattr(self.k, name)
            fn.argtypes, fn.restype = [W.HANDLE, C.POINTER(ProcessEntry)], W.BOOL
        for name in ("Module32FirstW", "Module32NextW"):
            fn = getattr(self.k, name)
            fn.argtypes, fn.restype = [W.HANDLE, C.POINTER(ModuleEntry)], W.BOOL

    def processes(self):
        handle = self.k.CreateToolhelp32Snapshot(2, 0)
        if handle == C.c_void_p(-1).value:
            raise C.WinError(C.get_last_error())
        try:
            entry = self.ProcessEntry()
            entry.dwSize = C.sizeof(entry)
            result = []
            ok = self.k.Process32FirstW(handle, C.byref(entry))
            while ok:
                if entry.szExeFile.lower() == "speed.exe":
                    result.append(entry.th32ProcessID)
                ok = self.k.Process32NextW(handle, C.byref(entry))
            return result
        finally:
            self.k.CloseHandle(handle)

    def image_base(self, pid):
        handle = self.k.CreateToolhelp32Snapshot(0x18, pid)
        if handle == C.c_void_p(-1).value:
            raise C.WinError(C.get_last_error())
        try:
            entry = self.ModuleEntry()
            entry.dwSize = C.sizeof(entry)
            if not self.k.Module32FirstW(handle, C.byref(entry)):
                raise C.WinError(C.get_last_error())
            return entry.modBaseAddr
        finally:
            self.k.CloseHandle(handle)


class Process:
    def __init__(self, api, pid):
        self.api, self.pid = api, pid
        self.handle = api.k.OpenProcess(0x1010, False, pid)
        if not self.handle:
            raise C.WinError(C.get_last_error())
        try:
            times = [W.FILETIME() for _ in range(4)]
            if not api.k.GetProcessTimes(self.handle, *(C.byref(t) for t in times)):
                raise C.WinError(C.get_last_error())
            self.created = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
            name, size = C.create_unicode_buffer(32768), W.DWORD(32768)
            if not api.k.QueryFullProcessImageNameW(self.handle, 0, name, C.byref(size)):
                raise C.WinError(C.get_last_error())
            self.path = Path(name.value)
        except BaseException:
            self.close()
            raise

    def close(self):
        if self.handle:
            self.api.k.CloseHandle(self.handle)
            self.handle = None

    def alive(self):
        code = W.DWORD()
        return bool(self.api.k.GetExitCodeProcess(self.handle, C.byref(code))) and code.value == 259

    def read(self, address, size):
        if not isinstance(address, int) or not 0x10000 <= address <= sys.maxsize or not 0 < size <= 0x60000:
            return None
        buf, got = C.create_string_buffer(size), C.c_size_t()
        ok = self.api.k.ReadProcessMemory(self.handle, address, buf, size, C.byref(got))
        return buf.raw if ok and got.value == size else None

    def authenticate(self):
        exe_hash = digest(self.path)
        if exe_hash != EXE_SHA256:
            raise RuntimeError("Unsupported speed.exe. Requires the recorded PC 1.4 USA executable. SHA-256: " + exe_hash)
        base = self.api.image_base(self.pid)
        if base != IMAGE_BASE:
            raise RuntimeError("Unsupported image base: " + hex(base or 0))
        return {"pid": self.pid, "creation_filetime": self.created,
                "executable": str(self.path), "sha256": exe_hash, "image_base": base}


class Sampler:
    def __init__(self, process):
        self.process = process
        self.calls = self.failed = 0
        self.raw = []

    def read(self, address, size):
        self.calls += 1
        blob = self.process.read(address, size)
        if blob is None:
            self.failed += 1
        else:
            self.raw.append((address, blob))
        return blob

    def u32(self, address):
        return field(self.read(address, 4), 0, "I")

    def sample(self, sequence, elapsed):
        started = time.perf_counter()
        self.calls = self.failed = 0
        self.raw = []
        frame_before, clock_before = self.u32(FRAME), self.u32(CLOCK)
        manager, race = self.u32(WORLD), self.u32(RACE)
        stage = self.u32(race) if pointer(race) else None
        seed = self.u32(manager + 0x2D0) if pointer(manager) else None
        actors, issues, seen = [], [], set()
        address = self.u32(manager) if pointer(manager) else None
        if not pointer(manager):
            issues.append("world_unavailable")
        for _ in range(128):
            if address == manager and pointer(manager):
                break
            if not pointer(address):
                issues.append("invalid_actor_link")
                break
            if address in seen:
                issues.append("actor_link_cycle")
                break
            seen.add(address)
            blob = self.read(address, 0x900)
            if blob is None:
                issues.append("actor_unreadable")
                break
            config_ptr, driver_ptr = field(blob, 0x14, "I"), field(blob, 0x30, "I")
            config = self.read(config_ptr, 0x750) if pointer(config_ptr) else None
            # Human drivers can end near an allocation/page boundary. Read only
            # the common input/path prefix; the AI-only tail is slow evidence.
            driver = self.read(driver_ptr, 0x1E8) if pointer(driver_ptr) else None
            actors.append(decode_actor(address, blob, config, driver))
            address = field(blob, 0, "I")
        else:
            issues.append("actor_limit_reached")
        frame_after, clock_after = self.u32(FRAME), self.u32(CLOCK)
        return {"seq": sequence, "elapsed_s": elapsed,
                "frame_before": frame_before, "frame_after": frame_after,
                "clock_before": clock_before, "clock_after": clock_after,
                "cross_frame": frame_before != frame_after or clock_before != clock_after,
                "stage": stage, "world": manager, "rng_seed": seed,
                "actors": actors, "issues": issues,
                "read_calls": self.calls, "read_failures": self.failed,
                "read_ms": (time.perf_counter() - started) * 1000}

    def context(self):
        for address, size in ((0x730000, 0x60000), (CAREER + 0x2E0, 22 * 4),
                              (CAREER + 0x34C, 256), (0x78A300, 0x80)):
            self.read(address, size)
        manager, race = self.u32(WORLD), self.u32(RACE)
        if pointer(manager):
            self.read(manager, 0x400)
        if pointer(race):
            self.read(race, 0x5A00)

    def related_blocks(self, actors):
        # Slow raw evidence only. No additional field semantics are assumed here.
        for actor in actors:
            blob = self.read(actor["address"], 0x900)
            if blob is None:
                continue
            if actor['role'] in (2, 3, 4) and pointer(actor['driver_address']):
                self.read(actor['driver_address'], 0x800)
            for offset, size in ((0x18, 0x500), (0x1C, 0x500),
                                 (0x34, 0x1000), (0x3C, 0x800)):
                address = field(blob, offset, "I")
                if pointer(address):
                    self.read(address, size)


def special_folder(kind):
    buf = C.create_unicode_buffer(32768)
    shell = C.WinDLL("shell32")
    fn = shell.SHGetFolderPathW
    fn.argtypes = [W.HWND, C.c_int, W.HANDLE, W.DWORD, W.LPWSTR]
    fn.restype = C.c_long
    if fn(None, kind, None, 0, buf) != 0:
        raise RuntimeError("Cannot find Windows user folder")
    return Path(buf.value)


class Session:
    def __init__(self, root, label="", hz=60):
        name = "session-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ-") + uuid.uuid4().hex[:8]
        self.directory = Path(root) / name
        make_directory(self.directory, exclusive=True)
        self.meta = {"schema": 1, "recorder_version": VERSION, "started_utc": utc(),
                     "label": label, "requested_hz": hz,
                     "source_sha256": digest(__file__), "supported_exe_sha256": EXE_SHA256,
                     "access": "ReadProcessMemory; no writes, hooks, debugger or game input",
                     "raw_format": "Repeated little-endian <dII elapsed_s,address,size followed by bytes",
                     "limitations": ["Polling, not an atomic or deterministic replay.",
                                     "Samples can straddle game frames; cross_frame marks them.",
                                     "Raw blocks are sampled at 2 Hz; globals also at stage changes and every 30 s.",
                                     "Motion coverage is not proof of a complete lap.",
                                     "PC timings do not measure Switch performance."]}
        write_json(self.directory / "metadata.json", self.meta)
        self.samples = (self.directory / "samples.jsonl").open("w", encoding="ascii", newline="\n")
        self.events = (self.directory / "events.jsonl").open("w", encoding="ascii", newline="\n")
        self.raw = (self.directory / "memory.bin").open("wb")
        self.stats = {"polls": 0, "saved_samples": 0, "cross_frame_samples": 0,
                      "read_failures": 0, "clock_changes": 0, "human_position_changes": 0,
                      "overruns": 0, "raw_records": 0, "max_read_ms": 0.0}
        self.last_clock = self.last_position = None
        self.first_elapsed = self.last_elapsed = None
        self.last_write, self.last_key = -1.0, None
        self.bytes_written = 0

    def event(self, kind, **values):
        self.events.write(encoded({"utc": utc(), "kind": kind, **values}) + "\n")
        self.events.flush()

    def add(self, sample, raw):
        self.stats["polls"] += 1
        self.stats["cross_frame_samples"] += bool(sample["cross_frame"])
        self.stats["read_failures"] += sample["read_failures"]
        self.stats["max_read_ms"] = max(self.stats["max_read_ms"], sample["read_ms"])
        clock = sample["clock_before"]
        if clock is not None:
            if self.last_clock is not None and clock != self.last_clock:
                self.stats["clock_changes"] += 1
            self.last_clock = clock
        human = next((a for a in sample["actors"] if a["role"] == 1), None)
        position = human["position"] if human else None
        if position is not None:
            if self.last_position is not None and position != self.last_position:
                self.stats["human_position_changes"] += 1
            self.last_position = position
        if self.first_elapsed is None:
            self.first_elapsed = sample["elapsed_s"]
        self.last_elapsed = sample["elapsed_s"]
        key = encoded({k: sample[k] for k in ("clock_before", "clock_after", "stage",
                      "world", "rng_seed", "actors", "issues")})
        if key != self.last_key or sample["elapsed_s"] - self.last_write >= 1:
            line = encoded(sample) + "\n"
            self.samples.write(line)
            self.bytes_written += len(line)
            self.stats["saved_samples"] += 1
            self.last_write, self.last_key = sample["elapsed_s"], key
        for address, blob in raw:
            header = RAW_HEADER.pack(sample["elapsed_s"], address, len(blob))
            self.raw.write(header)
            self.raw.write(blob)
            self.bytes_written += len(header) + len(blob)
            self.stats["raw_records"] += 1

    def finish(self, reason):
        self.event("stopped", reason=reason)
        for handle in (self.samples, self.events, self.raw):
            handle.flush()
            os.fsync(handle.fileno())
            handle.close()
        moved = self.stats["human_position_changes"] > 0
        summary = {**self.stats, "ended_utc": utc(), "reason": reason,
                   "duration_s": (self.last_elapsed or 0) - (self.first_elapsed or 0),
                   "movement_observed": moved, "complete_lap_confirmed": False,
                   "coverage": "Movement observed; inspect clock and stage coverage." if moved else
                   "No human movement recorded. The game may have been closed, in menus or paused."}
        write_json(self.directory / "summary.json", summary)
        write_json(self.directory / "metadata.json", self.meta)
        files = sorted(self.directory.iterdir())
        manifest = {f.name: {"bytes": f.stat().st_size, "sha256": digest(f)} for f in files}
        write_json(self.directory / "manifest.json", manifest)
        archive = self.directory.with_suffix(".zip")
        pending = self.directory.with_suffix(".zip.partial")
        with zipfile.ZipFile(pending, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=3,
                             allowZip64=True) as z:
            for f in sorted(self.directory.iterdir()):
                z.write(f, f.name)
        with zipfile.ZipFile(pending) as z:
            if z.testzip() is not None:
                raise RuntimeError("ZIP integrity check failed; raw recording remains available")
            for name, item in manifest.items():
                h = hashlib.sha256()
                with z.open(name) as f:
                    for block in iter(lambda: f.read(1024 * 1024), b""):
                        h.update(block)
                if h.hexdigest() != item["sha256"]:
                    raise RuntimeError("ZIP hash mismatch: " + name)
        pending.rename(archive)
        return archive, summary


class Recorder:
    def __init__(self, root, label="", hz=60):
        self.root, self.label, self.hz = root, label, hz
        self.stop_event, self.messages = threading.Event(), queue.Queue()
        self.thread = None

    def start(self):
        self.thread = threading.Thread(target=self.run, name="nfsu-recorder", daemon=False)
        self.thread.start()

    def stop(self):
        self.stop_event.set()

    def run(self):
        session = process = None
        reason = "User stopped recording"
        try:
            api = WindowsAPI()
            session = Session(self.root, self.label, self.hz)
            self.messages.put(("status", "Waiting for original speed.exe..."))
            while not self.stop_event.is_set():
                pids = api.processes()
                if len(pids) > 1:
                    raise RuntimeError("Multiple speed.exe processes found. Close all but the original game.")
                if pids:
                    process = Process(api, pids[0])
                    session.meta["process"] = process.authenticate()
                    session.event("attached", **session.meta["process"])
                    break
                self.stop_event.wait(0.5)
            if process and not self.stop_event.is_set():
                sampler = Sampler(process)
                begin = deadline = time.perf_counter()
                raw_at = context_at = status_at = -1.0
                previous_stage = object()
                last_change = begin
                last_clock = None
                seq = 0
                while not self.stop_event.is_set():
                    now = time.perf_counter()
                    if not process.alive():
                        reason = "Game process exited"
                        break
                    sample = sampler.sample(seq, now - begin)
                    if sample["clock_before"] is not None and sample["clock_before"] != last_clock:
                        last_change = now
                        last_clock = sample["clock_before"]
                    raw = list(sampler.raw) if now >= raw_at else []
                    if now >= raw_at:
                        sampler.raw = []
                        sampler.related_blocks(sample["actors"])
                        raw.extend(sampler.raw)
                        raw_at = now + 0.5
                    if now >= context_at or sample["stage"] != previous_stage:
                        sampler.raw = []
                        sampler.context()
                        raw.extend(sampler.raw)
                        session.event("context", seq=seq, stage=sample["stage"],
                                      read_failures=sampler.failed)
                        previous_stage = sample["stage"]
                        context_at = now + 30
                    session.add(sample, raw)
                    seq += 1
                    if session.bytes_written >= MAX_BYTES:
                        reason = "Recording size limit reached (1 GiB); start another session"
                        break
                    if now >= status_at:
                        state = "Game clock moving" if now - last_change < 1 else "Clock not moving (menu / pause / loading)"
                        self.messages.put(("status", "Recording: %s | %d samples | %.1f MiB" %
                                           (state, session.stats["saved_samples"], session.bytes_written / 1048576)))
                        session.samples.flush()
                        session.raw.flush()
                        status_at = now + 1
                    deadline += 1 / self.hz
                    delay = deadline - time.perf_counter()
                    if delay < 0:
                        session.stats["overruns"] += 1
                        deadline = time.perf_counter()
                    else:
                        self.stop_event.wait(delay)
        except Exception as exc:
            reason = "Recording error: " + str(exc)
            self.messages.put(("error", reason))
            if session:
                session.event("error", message=reason)
        finally:
            if process:
                process.close()
            if session:
                self.messages.put(("status", "Saving and verifying ZIP. Please wait..."))
                try:
                    archive, summary = session.finish(reason)
                    self.messages.put(("done", str(archive), summary))
                except Exception as exc:
                    self.messages.put(("save_error", "Cannot finish ZIP: %s\nRaw files: %s" % (exc, session.directory)))
            else:
                self.messages.put(("save_error", reason))


class App:
    def __init__(self, window, output):
        import tkinter as tk
        from tkinter import ttk
        self.window, self.output, self.recorder = window, Path(output), None
        self.closing = False
        window.title("NFSU Original Recorder")
        window.geometry("690x340")
        window.minsize(600, 320)
        panel = ttk.Frame(window, padding=18)
        panel.pack(fill="both", expand=True)
        ttk.Label(panel, text="NFS Underground - Original PC Recorder", font=("Segoe UI", 15)).pack(anchor="w")
        ttk.Label(panel, text="1. Start recording.  2. Play the original game.  3. Stop recording.\n"
                  "Read-only capture. The game can be launched before or after Start.").pack(anchor="w", pady=12)
        row = ttk.Frame(panel)
        row.pack(fill="x")
        ttk.Label(row, text="Session note (optional):").pack(side="left")
        self.label = tk.StringVar()
        ttk.Entry(row, textvariable=self.label).pack(side="left", fill="x", expand=True, padx=8)
        buttons = ttk.Frame(panel)
        buttons.pack(fill="x", pady=16)
        self.start_button = ttk.Button(buttons, text="Start recording", command=self.start)
        self.start_button.pack(side="left")
        self.stop_button = ttk.Button(buttons, text="Stop and save ZIP", command=self.stop, state="disabled")
        self.stop_button.pack(side="left", padx=8)
        ttk.Button(buttons, text="Open recordings", command=self.open_output).pack(side="left")
        self.status = tk.StringVar(value="Ready. Supported: original PC 1.4 USA executable.")
        ttk.Label(panel, textvariable=self.status, wraplength=640).pack(anchor="w")
        ttk.Label(panel, text="Output: " + str(self.output), wraplength=640).pack(anchor="w", pady=12)
        ttk.Label(panel, text="Send the resulting ZIP for analysis. No automatic upload.").pack(anchor="w")
        window.protocol("WM_DELETE_WINDOW", self.close)
        window.after(100, self.poll)

    def start(self):
        if self.recorder and self.recorder.thread.is_alive():
            return
        self.recorder = Recorder(self.output, self.label.get()[:500])
        self.start_button.configure(state="disabled")
        self.stop_button.configure(state="normal")
        self.recorder.start()

    def stop(self):
        if self.recorder:
            self.recorder.stop()
        self.stop_button.configure(state="disabled")
        self.status.set("Stopping; saving ZIP may take a moment...")

    def open_output(self):
        make_directory(self.output)
        os.startfile(self.output)

    def close(self):
        if self.recorder and self.recorder.thread.is_alive():
            self.closing = True
            self.stop()
        else:
            self.window.destroy()

    def poll(self):
        from tkinter import messagebox
        if self.recorder:
            while True:
                try:
                    item = self.recorder.messages.get_nowait()
                except queue.Empty:
                    break
                kind = item[0]
                if kind in ("status", "error"):
                    self.status.set(item[1])
                elif kind == "done":
                    self.status.set("Saved: " + item[1] + "\n" + item[2]["coverage"])
                    self.start_button.configure(state="normal")
                    self.stop_button.configure(state="disabled")
                    if item[2]["reason"].startswith("Recording error:"):
                        messagebox.showerror("Recording stopped", item[2]["reason"], parent=self.window)
                elif kind == "save_error":
                    self.status.set(item[1])
                    self.start_button.configure(state="normal")
                    self.stop_button.configure(state="disabled")
                    messagebox.showerror("Recording error", item[1], parent=self.window)
        if self.closing and self.recorder and not self.recorder.thread.is_alive():
            self.window.destroy()
            return
        self.window.after(100, self.poll)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--install", action="store_true", help="Install a user copy and desktop shortcut")
    args = parser.parse_args()
    if args.install:
        install()
        return
    import tkinter as tk
    output = args.output or special_folder(5) / "NFSU-Recordings"
    window = tk.Tk()
    App(window, output)
    window.mainloop()


def install():
    folder = special_folder(5) / "NFSU-Original-Recorder"
    make_directory(folder)
    make_directory(special_folder(5) / "NFSU-Recordings")
    source, target = Path(__file__).resolve(), folder / "original_recorder.py"
    if source != target:
        shutil.copyfile(source, target)
    documentation = source.parents[1] / "docs" / "ORIGINAL_RECORDER.md"
    if documentation.is_file():
        shutil.copyfile(documentation, folder / "README.md")
    pythonw = Path(sys.executable).with_name("pythonw.exe")
    if not pythonw.is_file():
        raise RuntimeError("pythonw.exe is missing; use a complete Python for Windows installation")
    environment = dict(os.environ, NFSUREC_PYTHON=str(pythonw), NFSUREC_SCRIPT=str(target))
    command = """$ErrorActionPreference='Stop'
$shell=New-Object -ComObject WScript.Shell
$shortcut=$shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) 'NFSU Original Recorder.lnk'))
$shortcut.TargetPath=$env:NFSUREC_PYTHON
$shortcut.Arguments='"'+$env:NFSUREC_SCRIPT+'"'
$shortcut.WorkingDirectory=[IO.Path]::GetDirectoryName($env:NFSUREC_SCRIPT)
$shortcut.Description='Read-only original PC game telemetry recorder'
$shortcut.Save()
$check=$shell.CreateShortcut($shortcut.FullName)
if ($check.TargetPath -ne $env:NFSUREC_PYTHON) { throw 'Shortcut verification failed' }
"""
    subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", command],
                   env=environment, check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    if digest(source) != digest(target):
        raise RuntimeError("Installed source hash mismatch")
    print(encoded({"installed": str(target), "shortcut": str(special_folder(0) / "NFSU Original Recorder.lnk"),
                   "sha256": digest(target)}))


if __name__ == "__main__":
    main()
