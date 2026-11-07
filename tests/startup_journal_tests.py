from pathlib import Path
import subprocess,sys,tempfile

binary=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='nfsu-startup-journal-',dir=Path.cwd()) as directory:
    scratch=Path(directory)
    def run(mode,expected):
        result=subprocess.run([str(binary),mode],cwd=scratch,capture_output=True)
        assert result.returncode==expected,(mode,result.returncode,result.stdout,result.stderr)
        return result
    result=run('--write-and-abort',23)
    retained=(scratch/'startup.log').read_bytes()
    assert retained==b'MENU STARTUP seq=1 stage=allocation.before name=scene requested=2457600\n'
    assert result.stdout.replace(b'\r\n',b'\n')==retained
    run('--exclusive',0)
    assert (scratch/'startup.log').read_bytes()==retained
    failure=run('--sync-failure',0)
    assert failure.stdout==b'', 'failed sync must not announce a successful marker'
    run('--close-failure',0)
    result=run('--sweep-abort',23)
    interrupted=(scratch/'event-0-round-1.log').read_bytes()
    assert result.stdout==b'' and b'stage=race.first_frame ' in interrupted
    assert run('--sweep-resume',0).stdout==b''
    assert (scratch/'event-0-round-1.log').read_bytes()==interrupted
print('PASS journal retained after abrupt exit; exclusive evidence preserved; sync/close failures reported')
print('PASS campaign sweep resume, failed stages and torn terminal markers')
