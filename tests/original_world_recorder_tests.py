"""World tape integrity, refusal and raw-to-semantic decoding tests."""
import json,struct,sys,tempfile,unittest,zipfile
from unittest.mock import patch
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from original_world_recorder import BinaryWriter,CALL,SNAP,decode_snapshot,package,sample_landing_camera
from original_recorder import CLOCK,FRAME,Sampler

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
    def test_landing_camera_same_frame_and_unavailable_view(self):
        view_address=0x800000
        actor,owner,active,saved=0x810000,0x820000,0x830000,0x840000
        matrix=bytearray(0x200)
        mover=bytearray(0x6c)
        struct.pack_into('<I',mover,0,0x6c7fc8)
        struct.pack_into('<I',mover,0xc,1)
        struct.pack_into('<H',mover,0x58,65112)
        struct.pack_into('<f',mover,0x68,.75)
        pulse=bytearray(0x20)
        struct.pack_into('<3f',pulse,0,0.,0.,-.125)
        struct.pack_into('<I3f',pulse,0x10,0x704570,.25,.5,17.)
        roadside=bytearray(0x6c)
        struct.pack_into('<I',roadside,0xc,15)
        struct.pack_into('<3f',matrix,0x40,12.,34.,56.)
        struct.pack_into('<3f',matrix,0x1e0,1.,2.,3.)
        class Memory:
            def __init__(self):
                self.values={FRAME:15,CLOCK:1000,0x740500:view_address,0x78a2f0:1002,actor+0x18:owner}
                self.regions={(owner+0x27c,12):struct.pack('<3I',active,saved,0),
                              (active,0x6c):bytes(roadside),(saved,0x6c):bytes(mover),
                              (0x73b220,0x20):bytes(pulse),
                              (actor+0x80,12):struct.pack('<3f',0.,0.,256.),
                              (0x73457c,4):struct.pack('<f',.125)}
            def read(self,address,size):
                if address==view_address and size==len(matrix):return bytes(matrix)
                if size==4 and address in self.values:return struct.pack('<I',self.values[address])
                return self.regions.get((address,size))
        memory=Memory()
        sampler=Sampler(memory)
        sample=dict(cross_frame=False,frame_after=15,clock_after=1000,actors=[dict(address=actor,role=1)])
        recorded=sample_landing_camera(sampler,sample)
        self.assertFalse(recorded['cross_frame'])
        self.assertEqual(recorded['eye'],[12.,34.,56.])
        self.assertEqual(recorded['eye_velocity'],[1.,2.,3.])
        self.assertEqual(recorded['track'],1002)
        self.assertEqual(recorded['movers']['active']['type'],15)
        self.assertEqual(recorded['movers']['saved']['filtered_pitch'],65112)
        self.assertEqual(recorded['movers']['saved']['support_weight'],.75)
        self.assertEqual(recorded['shake']['direction'],[0.,0.,-.125])
        self.assertEqual(recorded['shake']['phase'],.25)
        self.assertEqual(recorded['world_dt'],.125)
        self.assertEqual(recorded['movers']['acceleration'],[0.,0.,256.])
        memory.values[FRAME]=16
        self.assertTrue(sample_landing_camera(sampler,sample)['cross_frame'])
        memory.values[0x740500]=0
        self.assertIsNone(sample_landing_camera(sampler,sample)['eye'])

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
