"""Physics recording integrity, pairing and explicit limitation tests."""
import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import original_physics_recorder as r
from original_physics_replay import port_input


def record(seq=0):
    body, props = bytearray(0x200), bytearray(0x54)
    struct.pack_into('<I', body, 0x14, 0x900000)
    struct.pack_into('<f', props, 0, 1.2)
    for offset in (0x10, 0x24, 0x38):
        struct.pack_into('<f', props, offset, 2.)
    struct.pack_into('<f', props, 0x50, .4)
    return {'seq': seq, 'thread': 17, 'rigid': 0x800000, 'properties': 0x900000,
            'dt_bits': struct.unpack('<I', struct.pack('<f', 1/120))[0],
            'fpcw': 0x7f, 'mxcsr': 0x1f80, 'pre_hex': body.hex(), 'post_hex': body.hex(),
            'properties_hex': props.hex(), 'globals': [[a, bytes(n).hex()] for a, n in (
                (0x6b5b54, 0x38), (0x6f08c8, 0x40), (0x704478, 0x44),
                (0x6d4a00, 0x100), (0x7000ac, 4), (0x6f0738, 4))]}


class Tests(unittest.TestCase):
    def test_complete_record(self):
        self.assertIs(r.validate_record(item := record(), 0), item)

    def test_missing_duplicate_and_reordered_step(self):
        for seq in (1, -1, 10):
            with self.assertRaises(ValueError):
                r.validate_record(record(seq), 0)

    def test_missing_fields_are_not_zero_filled(self):
        for name in r.REQUIRED:
            item = record(); del item[name]
            with self.assertRaises(ValueError):
                r.validate_record(item, 0)

    def test_lengths_and_hex(self):
        for name in ('pre_hex', 'post_hex', 'properties_hex'):
            for value in ('00', 'zz', record()[name] + '00'):
                item = record(); item[name] = value
                with self.assertRaises(ValueError):
                    r.validate_record(item, 0)

    def test_invalid_intervals(self):
        for value in (0., -1., float('inf'), float('nan'), 2.):
            item = record(); item['dt_bits'] = struct.unpack('<I', struct.pack('<f', value))[0]
            with self.assertRaises(ValueError):
                r.validate_record(item, 0)

    def test_global_closure(self):
        for change in ('missing', 'duplicate', 'short', 'code'):
            item = record()
            if change == 'missing': item['globals'].pop()
            if change == 'duplicate': item['globals'][0] = item['globals'][1]
            if change == 'short': item['globals'][0][1] = '00'
            if change == 'code': item['globals'][0][0] = 0x46aa40
            with self.assertRaises(ValueError):
                r.validate_record(item, 0)

    def test_pointer_identity(self):
        for value in (0, 0x900001, 0xffffffff):
            item = record(); item['properties'] = value
            with self.assertRaises(ValueError):
                r.validate_record(item, 0)

    def test_batch_is_atomic_on_invalid_second_record(self):
        with tempfile.TemporaryDirectory(prefix='nfsu-physics-test-') as directory:
            writer = r.Writer(Path(directory))
            with self.assertRaises(ValueError):
                writer.batch({'batch': 0, 'records': [record(), record(7)]})
            self.assertEqual(writer.steps, 0)
            self.assertEqual(writer.path.stat().st_size, 0)
            writer.batch({'batch': 0, 'records': [record(), record(1)]})
            with self.assertRaises(ValueError):
                writer.batch({'batch': 0, 'records': [record(2)]})
            writer.close()
            self.assertEqual(len(writer.path.read_text('ascii').splitlines()), 2)

    def test_empty_batch_refused(self):
        with tempfile.TemporaryDirectory(prefix='nfsu-physics-test-') as directory:
            writer = r.Writer(Path(directory))
            with self.assertRaises(ValueError):
                writer.batch({'batch': 0, 'records': []})
            writer.close()

    def test_size_ceiling_never_partially_writes_batch(self):
        with tempfile.TemporaryDirectory(prefix='nfsu-physics-test-') as directory:
            writer = r.Writer(Path(directory))
            with mock.patch.object(r, 'MAX_BYTES', 1), self.assertRaises(ValueError):
                writer.batch({'batch': 0, 'records': [record()]})
            self.assertEqual(writer.steps, 0)
            self.assertEqual(writer.path.stat().st_size, 0)
            writer.close()

    def test_full_host_queue_is_explicit_and_nonblocking(self):
        recorder = r.Recorder(Path('.'), 'test')
        for _ in range(33):
            recorder.receive({'type': 'send'}, None)
        self.assertTrue(recorder.overflow.is_set())

    def test_packaging_verified_and_not_overwritten(self):
        with tempfile.TemporaryDirectory(prefix='nfsu-physics-test-') as directory:
            folder = Path(directory) / 'capture'; folder.mkdir()
            (folder / 'steps.jsonl').write_text(json.dumps(record()) + '\n', encoding='ascii')
            archive = r.package(folder)
            with zipfile.ZipFile(archive) as z:
                manifest = json.loads(z.read('manifest.json'))
                self.assertEqual(manifest['steps.jsonl']['sha256'], r.base.digest(folder / 'steps.jsonl'))
            with self.assertRaises(FileExistsError):
                r.package(folder)

    def test_port_input_preserves_momenta_and_axes(self):
        item = record(); body = bytearray.fromhex(item['pre_hex'])
        struct.pack_into('<3f', body, 0x70, 2, 3, 4)
        struct.pack_into('<3f', body, 0x30, .1, .2, .3)
        item['pre_hex'] = body.hex()
        values, reason = port_input(item)
        self.assertIsNone(reason)
        self.assertEqual(values[9:12], [2, 3, 4])
        self.assertAlmostEqual(values[27], .1)

    def test_unsupported_port_paths_stay_explicit(self):
        for condition in ('precision', 'drive', 'tensor', 'sse', 'mass'):
            item = record(); body = bytearray.fromhex(item['pre_hex']); props = bytearray.fromhex(item['properties_hex'])
            if condition == 'precision': item['fpcw'] = 0x27f
            if condition == 'sse': item['mxcsr'] = 0x3f80
            if condition == 'drive': struct.pack_into('<f', body, 0x1e4, .1)
            if condition == 'tensor': struct.pack_into('<f', props, 0x14, .1)
            if condition == 'mass': struct.pack_into('<f', props, 0, 0)
            item['pre_hex'], item['properties_hex'] = body.hex(), props.hex()
            values, reason = port_input(item)
            self.assertIsNone(values)
            self.assertTrue(reason)

    def test_stop_before_attachment_does_not_create_empty_capture(self):
        with tempfile.TemporaryDirectory(prefix='nfsu-physics-test-') as directory:
            recorder = r.Recorder(Path(directory), 'test')
            recorder.stop()
            with mock.patch.object(r.base, 'WindowsAPI'):
                recorder.start(); recorder.thread.join(timeout=3)
            self.assertFalse(recorder.thread.is_alive())
            self.assertEqual(list(Path(directory).iterdir()), [])
            messages = list(recorder.messages.queue)
            self.assertTrue(any(item[0] == 'save_error' for item in messages))

    def test_gui_separate_mode_initializes(self):
        import tkinter as tk
        window = tk.Tk(); window.withdraw()
        try:
            app = r.App(window, Path(tempfile.gettempdir()) / 'nfsu-physics-unused')
            self.assertIn('Physics', window.title())
            self.assertIsNone(app.recorder)
        finally:
            window.destroy()


if __name__ == '__main__':
    unittest.main()
