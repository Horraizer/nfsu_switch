"""World tape integrity, refusal and raw-to-semantic decoding tests."""
import json,struct,sys,tempfile,unittest,zipfile
from unittest.mock import patch
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from original_world_recorder import BinaryWriter,CALL,SNAP,decode_snapshot,package

def call(seq=1,kind=1,phase=1):
    row=[0]*40;row[0]=seq;row[1]=1;row[2]=kind;row[3]=phase;row[6]=2
    return CALL.pack(*row)
def snapshot():
    row=[0]*1344;row[0]=1;row[1]=1;row[2]=1;row[3]=1;row[4]=0x1100000
    row[6]=2;row[15]=1|2|8|32;row[1183]=0x210;row[1174]=4;row[1175]=0xffffffff
    blob=bytearray(SNAP.pack(*row));struct.pack_into('<3f',blob,64+0x60,1.,2.,3.)
    struct.pack_into('<f',blob,64+0x3d4,22.);struct.pack_into('<i',blob,64+0x3d8,3)
    struct.pack_into('<f',blob,64+0x3e0,5000.);struct.pack_into('<H',blob,1088+0x1d0,60000)
    struct.pack_into('<i',blob,1088+0x1d4,4);struct.pack_into('<f',blob,1088+0x1dc,.75)
    struct.pack_into('<f',blob,1088+0x200,33.);struct.pack_into('<3f',blob,1920+0xa0,4.,5.,6.)
    struct.pack_into('<11I',blob,3644,0,0,1,0,0,0,1,1,0,0,0)
    return bytes(blob)
def batch(stream,number,first=0,count=1):
    return dict(kind='binary',stream=stream,batch=number,first=first,count=count,words=40 if stream=='calls'else 1344)

class TapeTests(unittest.TestCase):
    def test_explicit_byte_ceiling_preserves_accepted_prefix(self):
        with tempfile.TemporaryDirectory(prefix='nfsu-world-')as d:
            folder=Path(d);w=BinaryWriter(folder,CALL.size+SNAP.size)
            try:
                w.accept(batch('calls',0),call());w.accept(batch('snapshots',1),snapshot())
                with self.assertRaisesRegex(ValueError,'byte ceiling'):w.accept(batch('calls',2,1),call(2,phase=2))
                self.assertEqual(w.counts,dict(calls=1,snapshots=1))
            finally:w.close()
            self.assertEqual((folder/'calls.bin').read_bytes(),call())
            self.assertEqual((folder/'snapshots.bin').read_bytes(),snapshot())
    def test_archive_verification_streams_without_full_member_reads(self):
        with tempfile.TemporaryDirectory(prefix='nfsu-world-')as d:
            folder=Path(d)/'session';folder.mkdir();content=b'world-state'*300000
            (folder/'snapshots.bin').write_bytes(content)
            with patch.object(zipfile.ZipFile,'read',side_effect=AssertionError('unbounded member allocation')):
                target=package(folder)
            with zipfile.ZipFile(target)as z:self.assertEqual(z.read('snapshots.bin'),content)
            self.assertEqual((folder/'snapshots.bin').read_bytes(),content)
            with self.assertRaises(FileExistsError):package(folder)
    def test_preserves_binary_and_stream_counts(self):
        with tempfile.TemporaryDirectory(prefix='nfsu-world-')as d:
            p=Path(d);w=BinaryWriter(p)
            try:
                w.accept(batch('calls',0),call());w.accept(batch('snapshots',1),snapshot())
                w.accept(batch('calls',2,1),call(2,phase=2));self.assertEqual(w.counts,dict(calls=2,snapshots=1))
            finally:w.close()
            self.assertEqual((p/'calls.bin').read_bytes(),call()+call(2,phase=2))
            self.assertEqual((p/'snapshots.bin').read_bytes(),snapshot())
    def test_rejects_gaps_duplicates_truncation_and_disabled_inner_hook(self):
        bad=[(batch('calls',1),call()),(batch('calls',0,1),call()),
            (batch('calls',0),call(2)),(batch('calls',0),call()[:-1]),
            (batch('calls',0),call(kind=5)),(batch('calls',0),call(phase=3)),
            (batch('snapshots',0),snapshot())]
        for payload,data in bad:
            with self.subTest(payload=payload),tempfile.TemporaryDirectory(prefix='nfsu-world-')as d:
                w=BinaryWriter(Path(d))
                try:
                    with self.assertRaises(ValueError):w.accept(payload,data)
                    self.assertEqual(w.bytes,0)
                finally:w.close()
    def test_decodes_observed_controls_without_filling_missing_state(self):
        row=decode_snapshot(snapshot());self.assertEqual(row['position'],[1.,2.,3.]);self.assertEqual(row['rpm'],5000.)
        self.assertEqual(row['controls']['gear_command_raw'],4);self.assertEqual(row['controls']['steering_word'],60000)
        self.assertEqual(row['demand']['speed'],33.);self.assertEqual(row['rigid']['velocity'],[4.,5.,6.])
        self.assertEqual(row['ai_context_raw'],-1);self.assertEqual(row['performance_levels'],[0,0,1,0,0,0,1,1,0,0,0])
        blob=bytearray(snapshot());struct.pack_into('<I',blob,15*4,1)
        row=decode_snapshot(bytes(blob));self.assertIsNone(row['path']);self.assertNotIn('demand',row);self.assertNotIn('rigid',row)
    def test_short_snapshot_is_refused(self):
        with self.assertRaises(ValueError):decode_snapshot(snapshot()[:-1])

if __name__=='__main__':unittest.main()
