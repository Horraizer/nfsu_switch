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
print('PASS journal retained after abrupt exit; exclusive evidence preserved; sync/close failures reported')
