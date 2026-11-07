"""Focused standalone recorder checks; no retail process is modified."""
import ctypes as C
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock
import zipfile

SOURCE = Path(__file__).resolve().parents[1] / "tools" / "original_recorder.py"
spec = importlib.util.spec_from_file_location("original_recorder", SOURCE)
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)


def fixture():
    actor, config, driver = bytearray(0x900), bytearray(0x750), bytearray(0x800)
    struct.pack_into("<I", actor, 0, 0x20000)
    struct.pack_into("<h", actor, 0xA, 17)
    struct.pack_into("<I", actor, 0x14, 0x40000)
    struct.pack_into("<I", actor, 0x30, 0x50000)
    struct.pack_into("<3f", actor, 0x60, 602, 539, 49)
    struct.pack_into("<3f", actor, 0x70, -44, 23, -1)
    struct.pack_into("<Iif", actor, 0x3D4, 0, 4, 0)
    struct.pack_into("<f", actor, 0x3D4, 50)
    struct.pack_into("<f", actor, 0x3E0, 5329)
    struct.pack_into("<I", config, 4, 1)
    struct.pack_into("<I", config, 0x10, 4)
    struct.pack_into("<b", config, 0x735, -2)
    driver[0x10] = 2
    struct.pack_into("<3f", driver, 0x20, 11, 12, 13)
    struct.pack_into("<3f", driver, 0x30, 21, 22, 23)
    driver[0x160:0x162] = bytes([9, 10])
    struct.pack_into("<2H", driver, 0x174, 30, 12)
    struct.pack_into("<H", driver, 0x1D0, 55000)
    struct.pack_into("<i", driver, 0x1D4, 4)
    struct.pack_into("<3f", driver, 0x1DC, 0.8, 0.25, 0.5)
    return actor, config, driver


class Memory:
    def __init__(self):
        actor, config, driver = fixture()
        self.blocks = {r.FRAME: struct.pack("<I", 37), r.CLOCK: struct.pack("<I", 67),
                       r.WORLD: struct.pack("<I", 0x20000), r.RACE: struct.pack("<I", 0x60000),
                       0x20000: struct.pack("<I", 0x30000),
                       0x202D0: struct.pack("<I", 123), 0x60000: struct.pack("<I", 4),
                       0x30000: actor, 0x40000: config, 0x50000: driver}

    def read(self, address, size):
        for base, blob in self.blocks.items():
            offset = address - base
            if 0 <= offset and offset + size <= len(blob):
                return bytes(blob[offset:offset + size])
        return None


class RecorderTests(unittest.TestCase):
    def test_driver_fields_path_and_velocity_not_heading(self):
        a = r.decode_actor(0x30000, *fixture())
        self.assertEqual(a["position"], [602, 539, 49])
        self.assertEqual(a["velocity"], [-44, 23, -1])
        self.assertEqual(a["gear_slot_raw"], 4)
        self.assertEqual(a["rpm"], 5329)
        self.assertEqual(a["ai_context_raw"], -2)
        self.assertEqual(a["controls"]["handbrake"], 0.25)
        self.assertEqual(a["controls"]["service_brake"], 0.5)
        self.assertEqual(a["path"]["roads"], [9, 10])
        self.assertEqual(a["path"]["road_points"], [30, 12])
        self.assertEqual(a["path"]["points"][1], [21, 22, 23])

    def test_missing_and_invalid_data_are_not_fabricated(self):
        self.assertIsNone(r.field(None, 0, "I"))
        self.assertIsNone(r.field(b"\0", 0, "I"))
        self.assertIsNone(r.field(struct.pack("<f", float("nan")), 0, "f"))
        invalid = bytearray(0x1C0)
        invalid[0] = 255
        self.assertFalse(r.decode_path(invalid)["valid"])
        a = r.decode_actor(0x30000, fixture()[0], None, None)
        self.assertIsNone(a["role"])
        self.assertIsNone(a["controls"]["throttle"])

    def test_reject_new_non_ascii_folder_before_writing(self):
        with tempfile.TemporaryDirectory(prefix="nfsu-recorder-path-") as tmp:
            path = Path(tmp) / "\u0442\u0435\u0441\u0442"
            with self.assertRaisesRegex(RuntimeError, "ASCII"):
                r.Session(path)
            self.assertFalse(path.exists())

    def test_list_sentinel_cycles_and_cross_frame(self):
        memory = Memory()
        sample = r.Sampler(memory).sample(0, 0)
        self.assertEqual(sample["issues"], [])
        self.assertEqual(len(sample["actors"]), 1)
        self.assertFalse(sample["cross_frame"])
        struct.pack_into("<I", memory.blocks[0x30000], 0, 0x30000)
        self.assertIn("actor_link_cycle", r.Sampler(memory).sample(1, 1)["issues"])
        memory.blocks[r.FRAME] = b""
        sample = r.Sampler(memory).sample(2, 2)
        self.assertIsNone(sample["frame_before"])
        self.assertGreater(sample["read_failures"], 0)

    def test_frame_change_is_marked(self):
        class Changing(Memory):
            count = 0

            def read(self, address, size):
                if address == r.FRAME:
                    self.count += 1
                    return struct.pack("<I", self.count)
                return super().read(address, size)
        self.assertTrue(r.Sampler(Changing()).sample(0, 0)["cross_frame"])

    def test_human_input_prefix_does_not_require_ai_allocation_tail(self):
        memory = Memory()
        memory.blocks[0x50000] = memory.blocks[0x50000][:0x1E8]
        self.assertIsNone(memory.read(0x50000, 0x800))
        sample = r.Sampler(memory).sample(0, 0)
        self.assertEqual(sample['read_failures'], 0)
        human = sample['actors'][0]
        self.assertTrue(human['driver_readable'])
        self.assertAlmostEqual(human['controls']['throttle'], 0.8, places=6)
        self.assertEqual(human['controls']['handbrake'], 0.25)
        self.assertEqual(human['controls']['service_brake'], 0.5)

    def test_zip_round_trip_and_paused_coverage(self):
        with tempfile.TemporaryDirectory(prefix="nfsu-recorder-test-") as tmp:
            session = r.Session(tmp, "Unicode note: \u0442\u0435\u0441\u0442")
            sampler = r.Sampler(Memory())
            for i in range(121):
                sample = sampler.sample(i, i / 60)
                # Rendering can keep advancing while the game simulation is paused.
                sample["frame_before"] = sample["frame_after"] = i
                session.add(sample, sampler.raw if i == 0 else [])
            self.assertLess(session.stats["saved_samples"], 5)
            archive, report = session.finish("Test stopped")
            self.assertFalse(report["movement_observed"])
            self.assertFalse(report["complete_lap_confirmed"])
            self.assertEqual(report["clock_changes"], 0)
            with zipfile.ZipFile(archive) as z:
                manifest = json.loads(z.read("manifest.json"))
                for name, item in manifest.items():
                    self.assertEqual(hashlib.sha256(z.read(name)).hexdigest(), item["sha256"])
                raw = z.read("memory.bin")
                elapsed, address, length = r.RAW_HEADER.unpack_from(raw)
                self.assertEqual((elapsed, address, length), (0, r.FRAME, 4))
                self.assertEqual(struct.unpack_from("<I", raw, r.RAW_HEADER.size)[0], 37)
            for path in session.directory.iterdir():
                self.assertTrue(path.name.isascii())
                if path.suffix in (".json", ".jsonl"):
                    self.assertTrue(all(b in (9, 10, 13) or 32 <= b <= 126 for b in path.read_bytes()))

    def test_motion_and_read_error_statistics(self):
        with tempfile.TemporaryDirectory(prefix="nfsu-recorder-motion-") as tmp:
            memory, session = Memory(), r.Session(tmp)
            sampler = r.Sampler(memory)
            session.add(sampler.sample(0, 0), [])
            struct.pack_into("<f", memory.blocks[0x30000], 0x60, 603)
            memory.blocks[r.CLOCK] = struct.pack("<I", 134)
            del memory.blocks[0x50000]
            session.add(sampler.sample(1, 1), [])
            _, report = session.finish("Test stopped")
            self.assertTrue(report["movement_observed"])
            self.assertEqual(report["clock_changes"], 1)
            self.assertEqual(report["read_failures"], 1)

    def test_stop_while_waiting_and_capture_error_preserve_archive(self):
        class WaitingAPI:
            def processes(self):
                return []
        class FailingAPI:
            def processes(self):
                raise OSError("controlled enumeration failure")
        for api, error in ((WaitingAPI, False), (FailingAPI, True)):
            with self.subTest(error=error), tempfile.TemporaryDirectory(prefix="nfsu-recorder-stop-") as tmp:
                with mock.patch.object(r, "WindowsAPI", api):
                    engine = r.Recorder(tmp)
                    engine.start()
                    time.sleep(0.02)
                    if not error:
                        engine.stop()
                    engine.thread.join(5)
                    self.assertFalse(engine.thread.is_alive())
                messages = []
                while not engine.messages.empty():
                    messages.append(engine.messages.get_nowait())
                done = [item for item in messages if item[0] == "done"]
                self.assertEqual(len(done), 1)
                self.assertTrue(Path(done[0][1]).is_file())
                self.assertEqual(done[0][2]["polls"], 0)
                self.assertEqual(done[0][2]["reason"].startswith("Recording error:"), error)

    def test_capture_loop_packages_when_process_exits(self):
        class TestAPI:
            def processes(self):
                return [123]
        class TestProcess(Memory):
            closed = False
            polls = 0

            def __init__(self, api, pid):
                super().__init__()

            def authenticate(self):
                return {"test_fixture": True}

            def alive(self):
                self.polls += 1
                self.blocks[r.CLOCK] = struct.pack("<I", self.polls * 67)
                struct.pack_into("<f", self.blocks[0x30000], 0x60, 600 + self.polls)
                return self.polls <= 4

            def close(self):
                type(self).closed = True
        with tempfile.TemporaryDirectory(prefix="nfsu-recorder-loop-") as tmp:
            with mock.patch.object(r, "WindowsAPI", TestAPI), mock.patch.object(r, "Process", TestProcess):
                engine = r.Recorder(tmp)
                engine.start()
                engine.thread.join(5)
                self.assertFalse(engine.thread.is_alive())
            archives = list(Path(tmp).glob("*.zip"))
            self.assertEqual(len(archives), 1)
            with zipfile.ZipFile(archives[0]) as z:
                summary = json.loads(z.read("summary.json"))
                self.assertEqual(summary["polls"], 4)
                self.assertEqual(summary["reason"], "Game process exited")
                self.assertTrue(summary["movement_observed"])
                self.assertEqual(len(z.read("samples.jsonl").splitlines()), 4)
            self.assertTrue(TestProcess.closed)

    @unittest.skipUnless(os.name == "nt", "Win32 integration")
    def test_read_only_process_access_and_unsupported_exe(self):
        # The child owns its buffer and publishes only the address for this test.
        child_code = ("import ctypes,sys; b=ctypes.create_string_buffer(b'recorder-child-probe'); "
                      "print(ctypes.addressof(b), flush=True); sys.stdin.readline()")
        child = subprocess.Popen([sys.executable, "-u", "-c", child_code],
                                 stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        try:
            address = int(child.stdout.readline())
            process = r.Process(r.WindowsAPI(), child.pid)
            try:
                self.assertTrue(process.alive())
                self.assertGreater(process.created, 0)
                self.assertEqual(process.read(address, 21), b"recorder-child-probe\0")
                self.assertIsNone(process.read(1, 20))
                with self.assertRaisesRegex(RuntimeError, "Unsupported speed.exe"):
                    process.authenticate()
            finally:
                process.close()
        finally:
            child.communicate("\n", timeout=10)
            self.assertEqual(child.returncode, 0)


if __name__ == "__main__":
    unittest.main()
