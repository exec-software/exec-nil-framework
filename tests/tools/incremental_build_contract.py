#!/usr/bin/env python3
"""NILData generation, content caching, and dependency-aware parallel builds."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--builder', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--interpreter', type=Path, required=True)
    parser.add_argument('--stdlib', type=Path, required=True)
    parser.add_argument('--llvm', default='OFF')
    args = parser.parse_args()
    builder, compiler, interpreter, stdlib = [p.resolve() for p in (args.builder, args.compiler, args.interpreter, args.stdlib)]
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil incremental ; ') as temporary:
        root = Path(temporary)
        events = root / 'events.log'
        fake = root / 'compiler tool'
        fake.write_text('#!' + sys.executable + '\n' + '''import hashlib, json, os, sys, time
from pathlib import Path
root=Path(__file__).parent
events=root/'events.log'
entry=Path(sys.argv[-1])
files=sorted({entry.resolve(), *[p.resolve() for p in entry.parent.glob('*.input')]})
data=''.join(p.read_text() for p in files)
def event(kind):
 fd=os.open(events, os.O_WRONLY|os.O_CREAT|os.O_APPEND, 0o600)
 os.write(fd, (json.dumps([kind, str(entry), time.monotonic()])+'\\n').encode()); os.close(fd)
if '--analyze' in sys.argv:
 good='BAD' not in data
 print(json.dumps({'schema':'nil.analysis.v1','success':good,'diagnostics':[],
  'dependenciesComplete':good,'dependencies':[{'path':str(p),'digest':hashlib.sha256(p.read_bytes()).hexdigest()} for p in files]}))
 sys.exit(0 if good else 1)
event('start')
time.sleep(0.5)
if 'FAIL' in data: event('fail'); sys.exit(17)
output=Path(sys.argv[sys.argv.index('--output')+1])
output.write_text(data + ('native' if '--native' in sys.argv else 'vm'))
if '--native' in sys.argv: output.chmod(0o755)
event('finish')
''')
        fake.chmod(0o755)

        def run(*arguments, cwd=root, status=0, tool=fake):
            nonlocal checks
            command = [str(builder), *map(str, arguments)]
            if arguments[0] in ('build', 'run', 'check'):
                command += ['--compiler', str(tool), '--sdk', str(stdlib)]
            result = subprocess.run(command, cwd=cwd, text=True, capture_output=True, timeout=60)
            checks += 1
            assert result.returncode == status, (command, result.returncode, result.stdout, result.stderr)
            return result

        def starts():
            return sum(json.loads(line)[0] == 'start' for line in events.read_text().splitlines()) if events.exists() else 0

        def create(directory, name, references=()):
            directory.mkdir(parents=True, exist_ok=True)
            (directory/'main.matrix').write_text('version one\n')
            data = {'schema':'nil.project.v1','name':name,'entry':'main.matrix','outputDirectory':'build'}
            if references: data['projectReferences'] = list(references)
            file = directory/'nil.project.json'
            file.write_text(json.dumps(data))
            run('convert','--project',file)
            return directory/'nil.project.nildata'

        # Generated defaults and legacy output creation use the same schema.
        initialized = root/'init'
        initialized.mkdir()
        result = run('init', '--name', 'starter', cwd=initialized)
        generated = initialized/'nil.project.nildata'
        assert generated.read_text().startswith('nildata 1\nobject\n')
        assert not (initialized/'src/main.matrix').exists()
        assert json.loads(run('describe','--format','json',cwd=initialized).stdout)['name']=='starter'
        original = generated.read_bytes()
        run('init', cwd=initialized, status=2)
        assert generated.read_bytes()==original
        for format in ('json','yaml'):
            manifest=initialized/f'nil.project.{format}'
            run('init','--format',format,'--name',format,cwd=initialized)
            report=json.loads(run('describe','--project',manifest,'--format','json').stdout)
            assert report['name']==format
        assert json.loads(run('describe','--format','json',cwd=initialized).stdout)['name']=='starter'
        # Moving configuration would change implicit import roots and cwd.
        relocated=root/'relocated';relocated.mkdir()
        run('convert','--project',generated,'--output',relocated/'nil.project.nildata',status=2)
        assert not (relocated/'nil.project.nildata').exists()
        for raw in ('nildata 1\nobject\n name is "a"\n name is "b"\nend object\n',
                    'nildata 1\nobject\n schema is "nil.project.v1"\n',
                    'nildata 1\nobject\nend object\nnull\n'):
            malformed=root/'bad.nildata';malformed.write_text(raw)
            run('describe','--project',malformed,status=2)
        malformed=root/'bad.yaml'
        for raw in ('name: a\nname: b\n','name: &anchor value\n','name: *anchor\n','name: !tag value\n'):
            malformed.write_text(raw);run('describe','--project',malformed,status=2)
        quoted = root/'quoted.yaml'
        quoted.write_text("schema: nil.project.v1\nname: quoted\nentry: src/it's-main.matrix # comment\n"
                          'includePaths:\n  - lib/a"b # comment\nanalysis: {"warningsAsErrors":false}\n')
        parsed=json.loads(run('describe','--project',quoted,'--format','json').stdout)
        assert parsed['entry']==str(root/"src/it's-main.matrix")
        assert str(root/'lib/a"b') in parsed['includePaths']
        run('init','--project',root/'invalid.nildata','--name','../bad',status=2)
        assert not (root/'invalid.nildata').exists()

        project=create(root/'application','app')
        source=project.parent/'main.matrix'; artifact=project.parent/'build/app.nil'
        build=lambda *flags, status=0: run('build','--project',project,*flags,status=status)
        build(); assert starts()==1
        state=artifact.parent/'.nilbuild/app.nil.nildata'
        assert state.read_text().startswith('nildata 1\n')
        stamp=artifact.stat().st_mtime_ns
        assert 'up to date' in build().stderr and starts()==1 and artifact.stat().st_mtime_ns==stamp
        before=source.stat();source.write_text('version two\n');os.utime(source,ns=(before.st_atime_ns,before.st_mtime_ns))
        build();assert starts()==2
        included=source.parent/'dependency.input';included.write_text('import content\n')
        build();assert starts()==3
        before=included.stat();included.write_text('edited content\n');os.utime(included,ns=(before.st_atime_ns,before.st_mtime_ns))
        build();assert starts()==4
        included.unlink();build();assert starts()==5
        fake.write_text(fake.read_text()+'# compiler change\n');build();assert starts()==6
        artifact.write_text('tampered output');build();assert starts()==7
        state.write_text('invalid state');build();assert starts()==8
        build('--rebuild');assert starts()==9
        project.write_text(project.read_text().replace('name is "app"','name is "app" # policy update'))
        build();assert starts()==10
        previous=artifact.read_bytes()
        source.write_text('BAD\n');build(status=1);assert artifact.read_bytes()==previous and starts()==10
        source.write_text('FAIL\n');build(status=17);assert artifact.read_bytes()==previous and starts()==11
        source.write_text('good again\n');build();assert starts()==12
        # VM/native targets overlap, within the requested process budget.
        events.write_text('');build('--target','all','--rebuild','--jobs','2')
        log=[json.loads(line) for line in events.read_text().splitlines()]
        assert [item[0] for item in log[:2]]==['start','start'], log
        events.write_text('');build('--target','all','--rebuild','-j','1')
        assert [json.loads(line)[0] for line in events.read_text().splitlines()]==['start','finish','start','finish']

        left=create(root/'left','left');right=create(root/'right','right')
        parent=create(root/'parent','parent',['../left/nil.project.nildata','../right/nil.project.nildata'])
        events.write_text('')
        run('build','--project',parent,'--jobs','2')
        log=[json.loads(line) for line in events.read_text().splitlines()]
        assert [item[0] for item in log[:2]]==['start','start'], log
        assert log[-2][1]==str(parent.parent/'main.matrix') and log[-2][0]=='start', log
        assert all(item[0]=='finish' for item in log[2:4]), log
        run('build','--project',parent,'--jobs','2');assert starts()==3
        (left.parent/'main.matrix').write_text('new dependency\n')
        run('build','--project',parent,'--jobs','2');assert starts()==5
        # Cycles and shared outputs are rejected before launching any compiler.
        cycle=json.loads((left.parent/'nil.project.json').read_text());cycle['projectReferences']=['../parent/nil.project.nildata']
        (left.parent/'nil.project.json').write_text(json.dumps(cycle));left.unlink();run('convert','--project',left.parent/'nil.project.json')
        run('build','--project',parent,status=2);assert starts()==5
        # Reverting references plus equal output paths exercises preflight.
        cycle.pop('projectReferences');cycle['outputDirectory']='../right/build';cycle['name']='right'
        (left.parent/'nil.project.json').write_text(json.dumps(cycle));left.unlink();run('convert','--project',left.parent/'nil.project.json')
        result=run('build','--project',parent,status=2);assert 'share an output' in result.stderr and starts()==5
        for flags in (('--jobs','0'),('--jobs','65'),('--jobs','x'),('--jobs','2','-j','3')):
            build(*flags,status=2)
        run('run','--project',project,'--target','all',status=2)
        # Concurrent drivers never publish to the same artifact simultaneously.
        previous_starts=starts()
        process=subprocess.Popen([str(builder),'build','--project',str(project),'--compiler',str(fake),'--rebuild'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            deadline=time.monotonic()+10
            while starts()==previous_starts and process.poll() is None and time.monotonic()<deadline: time.sleep(0.01)
            assert process.poll() is None
            assert 'in use' in build(status=2).stderr
            output,error=process.communicate(timeout=10)
            assert process.returncode==0,(output,error)
        finally:
            if process.poll() is None:process.kill();process.wait()

        # Real compiler smoke: unchanged imports skip compilation for both routes.
        actual=root/'real';actual.mkdir()
        (actual/'main.ny').write_text('import provider\nfn main() -> i32\n println(answer())\n return 0\nend fn\n')
        provider=actual/'provider.ny';provider.write_text('module provider\npub fn answer() -> i32\n return 42\nend fn\n')
        run('init','--name','real','--entry','main.ny',cwd=actual)
        target='all' if args.llvm.lower() in ('on','true','1') else 'vm'
        run('build','--target',target,cwd=actual,tool=compiler)
        reused=run('build','--target',target,cwd=actual,tool=compiler)
        assert reused.stderr.count('up to date:')==(2 if target=='all' else 1),reused.stderr
        output=subprocess.run([str(interpreter),str(actual/'build/real.nil')],text=True,capture_output=True,check=True)
        assert output.stdout=='42\n'
        previous=provider.stat();provider.write_text(provider.read_text().replace('42','43'));os.utime(provider,ns=(previous.st_atime_ns,previous.st_mtime_ns))
        assert 'up to date:' not in run('build','--target',target,cwd=actual,tool=compiler).stderr
        output=subprocess.run([str(interpreter),str(actual/'build/real.nil')],text=True,capture_output=True,check=True)
        assert output.stdout=='43\n'
    print(f'nilbuild: {checks} format, incremental, parallel, dependency and failure checks passed')


if __name__=='__main__':main()
