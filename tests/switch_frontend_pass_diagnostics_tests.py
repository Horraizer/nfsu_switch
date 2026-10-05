"""Bounded contract for first-frame GPU pass isolation; not GPU execution."""
from pathlib import Path
import argparse

def check(source):
    start=source.index('const bool diagnose=frontend && frontend_first_frame;')
    end=source.index('if (profile_enabled)',start)
    helper=source[start:end]
    if 'if(!diagnose)return true;' not in helper:raise ValueError('later frames would block')
    wait=helper.index('if(!submit(error))')
    reuse=helper.index('begin(slot)')
    if wait>=reuse:raise ValueError('command memory reused before fence completion')
    submit=source[source.index('bool submit(std::string& error)'):source.index('bool shader(')]
    if 'fence.wait()' not in submit:raise ValueError('segment completion is not fenced')
    for label in ['"cube face%u"','checkpoint("floor reflection")','checkpoint(previous_dynamic->program->name)','checkpoint("UI overlay")']:
        if label not in source:raise ValueError('missing isolated pass: '+label)
    if 'if(diagnose)frontend_first_frame=false;' not in source:raise ValueError('diagnostic never finishes')

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--source',type=Path,required=True)
    source=parser.parse_args().source.read_text();check(source)
    for label,old,new in [('missing wait','fence.wait()','DkResult_Success'),('unbounded diagnostic','if(!diagnose)return true;',''),('missing floor isolation','checkpoint("floor reflection")','checkpoint("other")')]:
        try:check(source.replace(old,new,1))
        except ValueError:print('PASS negative control: '+label)
        else:raise ValueError('negative control accepted: '+label)
    print('PASS first-frame cube/floor/program/UI isolation with fenced command-memory reuse; source contract only')
if __name__=='__main__':main()
