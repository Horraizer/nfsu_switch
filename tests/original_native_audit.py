"""Fresh-output wrapper for original executable audit checks in CTest."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool',choices=['race_actor_retirement_native','camera_presentation_native',
        'camera_surge_native','career_unique_reward_native','career_tournament_native','career_magazine_native','career_magazine_tasks_native','frontend_magazine_camera_native','race_effect_audio_native_check','engine_audio_native_check','career_fixed_native'],required=True)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--work',type=Path,required=True)
    parser.add_argument('--probe',type=Path)
    args=parser.parse_args()
    args.work.mkdir(parents=True,exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix=args.tool+'-',dir=args.work))/'artifacts'
    root=Path(__file__).resolve().parents[1]
    command=[sys.executable,'-B',str(root/'tools'/(args.tool+'.py')),'--root',str(args.root),'--out',str(out)]
    if args.tool in ('race_effect_audio_native_check','engine_audio_native_check','career_fixed_native'):
        if args.probe is None:parser.error('effect comparison needs its compiled probe')
        command+=['--probe',str(args.probe)]
        if args.tool=='engine_audio_native_check':command+=['--all','--gains','-63077','50000']
    return subprocess.run(command,cwd=root).returncode


if __name__=='__main__':raise SystemExit(main())
