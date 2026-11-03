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

# Where the native backend actually writes an executable.
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from native_output_staging import native_output_path


LAUNCHER_SOURCE = Path(__file__).resolve().parent / 'fixtures/windows_launcher.c'


def c_string(value):
    """`value` as a C string literal."""
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'


def windows_launcher(cxx, destination, script, work):
    """Build `destination`: a real .exe that forwards to `script`.

    Windows has no shebang and nilbuild never invokes a shell, so a Python
    stand-in compiler needs an executable in front of it. See the comment in
    fixtures/windows_launcher.c for why this belongs to the test rather than
    to nilbuild.
    """
    # The interpreter and script paths are known now, so the launcher's
    # command-line prefix is built and escaped here, exactly once, and
    # prepended to the fixture as a #define. Passing it through the compiler's
    # command line instead would mean escaping it again for the shell and once
    # more for C, and three levels of quoting over paths that deliberately
    # contain spaces is a bug waiting to happen.
    prefix = '"{}" "{}"'.format(sys.executable, script)
    source = work / 'launcher.c'
    source.write_text('#define FORWARD_TO ' + c_string(prefix) + '\n'
                      + LAUNCHER_SOURCE.read_text(encoding='utf-8'),
                      encoding='utf-8', newline='\n')
    if Path(cxx).name.lower() in ('cl', 'cl.exe'):
        command = [str(cxx), '/nologo', str(source), '/Fe:' + str(destination)]
    else:
        command = [str(cxx), str(source), '-o', str(destination)]
    built = subprocess.run(command, cwd=work, capture_output=True, text=True, timeout=180)
    assert built.returncode == 0 and destination.is_file(), (
        command, built.returncode, built.stdout, built.stderr)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--builder', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--interpreter', type=Path, required=True)
    parser.add_argument('--stdlib', type=Path, required=True)
    parser.add_argument('--llvm', default='OFF')
    parser.add_argument('--cxx', type=Path,
                        help='C compiler, used only on Windows to build the '
                             'launcher that stands in for a shebang')
    args = parser.parse_args()
    builder, compiler, interpreter, stdlib = [p.resolve() for p in (args.builder, args.compiler, args.interpreter, args.stdlib)]
    checks = 0
    with tempfile.TemporaryDirectory(prefix='nil incremental ; ') as temporary:
        root = Path(temporary)
        events = root / 'events'
        events.mkdir()
        # The stand-in compiler declares every '*.input*' sibling of the entry
        # as a dependency. The trailing wildcard matters: the protected-output
        # case below needs a declared input sitting exactly where the native
        # backend writes, and on Windows that name gains '.exe'. On POSIX the
        # output name already matches '*.input', so the wildcard changes
        # nothing there.
        #
        # On POSIX the script is the tool: the kernel honours its shebang. On
        # Windows the script is driven by a compiled launcher, and the shebang
        # line is inert but harmless -- keeping one script keeps the two hosts
        # testing the same stand-in compiler.
        script = root / 'compiler tool'
        fake = script
        script.write_text('#!' + sys.executable + '\n' + '''import hashlib, json, os, sys, tempfile, time
from pathlib import Path
root=Path(__file__).parent
events=root/'events'
entry=Path(sys.argv[-1])
files=sorted({entry.resolve(), *[p.resolve() for p in entry.parent.glob('*.input*')]})
data=''.join(p.read_text() for p in files)
def event(kind):
 # CRT O_APPEND is not a cross-process append lock on Windows. Each event
 # instead owns a unique directory and publishes one complete record by
 # rename, so concurrent writers cannot overwrite or expose partial JSON.
 record=[kind, str(entry), time.monotonic_ns()]
 directory=Path(tempfile.mkdtemp(prefix='event-', dir=events))
 pending=directory/'record.pending'
 pending.write_text(json.dumps(record), encoding='utf-8')
 os.replace(pending, directory/'record.json')
if '--analyze' in sys.argv:
 good='BAD' not in data
 report={'schema':'nil.analysis.v1','success':good,'diagnostics':[],
  'dependenciesComplete':good,'dependencies':[{'path':str(p),'digest':hashlib.sha256(p.read_bytes()).hexdigest()} for p in files]}
 modules=entry.parent/'compiler-modules.json'
 if modules.is_file(): report['compilerFiles']=json.loads(modules.read_text())
 print(json.dumps(report))
 sys.exit(0 if good else 1)
event('start')
time.sleep(0.5)
if 'FAIL' in data: event('fail'); sys.exit(17)
output=Path(sys.argv[sys.argv.index('--output')+1])
output.write_text(data + ('native' if '--native' in sys.argv else 'vm'))
if '--native' in sys.argv: output.chmod(0o755)
next_modules=entry.parent/'compiler-modules-after.json'
if next_modules.is_file(): os.replace(next_modules, entry.parent/'compiler-modules.json')
next_contents=entry.parent/'apphelp-content-after'
if next_contents.is_file():
 module=entry.parent/'apphelp.dll'
 previous=module.stat()
 module.write_bytes(next_contents.read_bytes())
 os.utime(module, ns=(previous.st_atime_ns,previous.st_mtime_ns))
 next_contents.unlink()
event('finish')
''', newline='\n')
        script.chmod(0o755)
        if os.name == 'nt':
            assert args.cxx is not None, '--cxx is required on Windows'
            fake = root / 'compiler tool.exe'
            windows_launcher(args.cxx, fake, script, root)

        def run(*arguments, cwd=root, status=0, tool=fake):
            nonlocal checks
            command = [str(builder), *map(str, arguments)]
            if arguments[0] in ('build', 'run', 'check'):
                command += ['--compiler', str(tool), '--sdk', str(stdlib)]
            result = subprocess.run(command, cwd=cwd, text=True, capture_output=True, timeout=60)
            checks += 1
            assert result.returncode == status, (command, result.returncode, result.stdout, result.stderr)
            return result

        def recorded_events():
            # monotonic_ns uses one clock across processes on the supported
            # hosts. Publication order is not event order: a writer can be
            # descheduled between recording its timestamp and the rename.
            return sorted((json.loads(path.read_text(encoding='utf-8'))
                           for path in events.glob('event-*/record.json')),
                          key=lambda event: (event[2], event[1], event[0]))

        def clear_events():
            # Called only after all fixture children have exited. Restrict
            # cleanup to completed records in this test's private directory.
            for record in events.glob('event-*/record.json'):
                record.unlink()
                record.parent.rmdir()

        def starts():
            return sum(event[0] == 'start' for event in recorded_events())

        def create(directory, name, references=()):
            directory.mkdir(parents=True, exist_ok=True)
            (directory/'main.matrix').write_text('version one\n', newline='\n')
            data = {'schema':'nil.project.v1','name':name,'entry':'main.matrix','outputDirectory':'build'}
            if references: data['projectReferences'] = list(references)
            file = directory/'nil.project.json'
            file.write_text(json.dumps(data), newline='\n')
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
            malformed=root/'bad.nildata';malformed.write_text(raw, newline='\n')
            run('describe','--project',malformed,status=2)
        malformed=root/'bad.yaml'
        for raw in ('name: a\nname: b\n','name: &anchor value\n','name: *anchor\n','name: !tag value\n'):
            malformed.write_text(raw, newline='\n');run('describe','--project',malformed,status=2)
        quoted = root/'quoted.yaml'
        quoted.write_text("schema: nil.project.v1\nname: quoted\nentry: src/it's-main.matrix # comment\n"
                          'includePaths:\n  - lib/a"b # comment\nanalysis: {"warningsAsErrors":false}\n', newline='\n')
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
        before=source.stat();source.write_text('version two\n', newline='\n');os.utime(source,ns=(before.st_atime_ns,before.st_mtime_ns))
        build();assert starts()==2
        included=source.parent/'dependency.input';included.write_text('import content\n', newline='\n')
        build();assert starts()==3
        before=included.stat();included.write_text('edited content\n', newline='\n');os.utime(included,ns=(before.st_atime_ns,before.st_mtime_ns))
        build();assert starts()==4
        included.unlink();build();assert starts()==5
        # The compiler whose change must invalidate the cache is whatever
        # nilbuild was pointed at -- the script on POSIX, the launcher on
        # Windows -- so append bytes rather than text. Trailing bytes are a
        # comment to Python and are ignored by the PE loader, which leaves the
        # tool runnable while giving it a different digest.
        fake.write_bytes(fake.read_bytes()+b'# compiler change\n');build();assert starts()==6
        artifact.write_text('tampered output', newline='\n');build();assert starts()==7
        state.write_text('invalid state', newline='\n');build();assert starts()==8
        build('--rebuild');assert starts()==9
        project.write_text(project.read_text().replace('name is "app"','name is "app" # policy update'), newline='\n')
        build();assert starts()==10
        previous=artifact.read_bytes()
        source.write_text('BAD\n', newline='\n');build(status=1);assert artifact.read_bytes()==previous and starts()==10
        source.write_text('FAIL\n', newline='\n');build(status=17);assert artifact.read_bytes()==previous and starts()==11
        source.write_text('good again\n', newline='\n');build();assert starts()==12
        # VM/native targets overlap, within the requested process budget.
        clear_events();build('--target','all','--rebuild','--jobs','2')
        log=recorded_events()
        assert [item[0] for item in log[:2]]==['start','start'], log
        clear_events();build('--target','all','--rebuild','-j','1')
        assert [event[0] for event in recorded_events()]==['start','finish','start','finish']

        left=create(root/'left','left');right=create(root/'right','right')
        parent=create(root/'parent','parent',['../left/nil.project.nildata','../right/nil.project.nildata'])
        clear_events()
        run('build','--project',parent,'--jobs','2')
        log=recorded_events()
        assert [item[0] for item in log[:2]]==['start','start'], log
        assert log[-2][1]==str(parent.parent/'main.matrix') and log[-2][0]=='start', log
        assert all(item[0]=='finish' for item in log[2:4]), log
        run('build','--project',parent,'--jobs','2');assert starts()==3
        (left.parent/'main.matrix').write_text('new dependency\n', newline='\n')
        run('build','--project',parent,'--jobs','2');assert starts()==5
        # Cycles and shared outputs are rejected before launching any compiler.
        cycle=json.loads((left.parent/'nil.project.json').read_text());cycle['projectReferences']=['../parent/nil.project.nildata']
        (left.parent/'nil.project.json').write_text(json.dumps(cycle), newline='\n');left.unlink();run('convert','--project',left.parent/'nil.project.json')
        run('build','--project',parent,status=2);assert starts()==5
        # Reverting references plus equal output paths exercises preflight.
        cycle.pop('projectReferences');cycle['outputDirectory']='../right/build';cycle['name']='right'
        (left.parent/'nil.project.json').write_text(json.dumps(cycle), newline='\n');left.unlink();run('convert','--project',left.parent/'nil.project.json')
        result=run('build','--project',parent,status=2);assert 'share an output' in result.stderr and starts()==5
        for flags in (('--jobs','0'),('--jobs','65'),('--jobs','x'),('--jobs','2','-j','3')):
            build(*flags,status=2)
        run('run','--project',project,'--target','all',status=2)
        # Metadata paths cannot be manifests, artifacts or consumed inputs.
        reserved=root/'reserved';(reserved/'.nilbuild').mkdir(parents=True)
        (reserved/'main.matrix').write_text('safe input\n', newline='\n')
        state_manifest=reserved/'.nilbuild/app.nil.nildata'
        template=reserved/'source.json'
        template.write_text(json.dumps({'schema':'nil.project.v1','name':'app','entry':'../main.matrix','outputDirectory':'..'}), newline='\n')
        # Conversion itself stays within the directory; relocate this fixture explicitly.
        template.rename(reserved/'.nilbuild/source.json')
        run('convert','--project',reserved/'.nilbuild/source.json','--output',state_manifest)
        original=state_manifest.read_bytes()
        run('build','--project',state_manifest,status=2)
        assert state_manifest.read_bytes()==original
        # The reservation also covers another project's output/lock locations.
        nested=create(root/'metadata-output','metadata')
        nested_data=json.loads((nested.parent/'nil.project.json').read_text())
        nested_data.update(outputDirectory='../application/build/.nilbuild', name='artifact')
        (nested.parent/'nil.project.json').write_text(json.dumps(nested_data), newline='\n');nested.unlink()
        run('convert','--project',nested.parent/'nil.project.json')
        parent_data=json.loads((project.parent/'nil.project.json').read_text())
        parent_data['projectReferences']=['../metadata-output/nil.project.nildata']
        conflict=root/'application/conflict.json';conflict.write_text(json.dumps(parent_data), newline='\n')
        assert 'reserved .nilbuild' in run('build','--project',conflict,status=2).stderr
        # Native toolchain uncertainty disables reuse, never input protection.
        protected=create(root/'uncached-native','consumed.input')
        data=json.loads((protected.parent/'nil.project.json').read_text());data['outputDirectory']='.'
        (protected.parent/'nil.project.json').write_text(json.dumps(data), newline='\n');protected.unlink()
        run('convert','--project',protected.parent/'nil.project.json')
        # The property is that nilbuild refuses to publish over a file it did
        # not create. Planting that file at the *requested* name only collides
        # where the native backend writes the name verbatim; on Windows it
        # appends .exe, so the build would have had a free path and the
        # protection would never have been reached. Plant it where the output
        # actually lands, which is the same path on POSIX.
        consumed=native_output_path(protected.parent/'consumed.input')
        consumed.write_text('must survive\n', newline='\n')
        run('build','--project',protected,'--target','native',status=2)
        assert consumed.read_text()=='must survive\n'
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

        # A changed loaded-module set must still reject publication, with a
        # bounded diagnostic that identifies additions and removals exactly.
        module_project=create(root/'module-graph','module-graph')
        module_dir=module_project.parent
        old_modules=[module_dir/f'old-module-{i:02}.dll' for i in range(10)]
        new_modules=[module_dir/f'new-module-{i:02}.dll' for i in range(10)]
        for module in old_modules+new_modules: module.write_bytes(b'module fixture')
        (module_dir/'compiler-modules.json').write_text(json.dumps(list(map(str,old_modules))))
        run('build','--project',module_project)
        module_artifact=module_dir/'build/module-graph.nil'
        module_state=module_dir/'build/.nilbuild/module-graph.nil.nildata'
        original_module_artifact=module_artifact.read_bytes()
        original_module_state=module_state.read_bytes()
        # Make the staged bytes observably different: otherwise an erroneous
        # replacement with the identical old output would evade this check.
        (module_dir/'main.matrix').write_text('version two\n', newline='\n')
        (module_dir/'compiler-modules-after.json').write_text(json.dumps(list(map(str,new_modules))))
        changed=run('build','--project',module_project,'--rebuild',status=2)
        assert 'compiler module graph changed' in changed.stderr, changed.stderr
        assert '; added: ' in changed.stderr and '; removed: ' in changed.stderr, changed.stderr
        for group in (old_modules,new_modules):
            for module in group[:8]: assert json.dumps(str(module),ensure_ascii=False) in changed.stderr, changed.stderr
            for module in group[8:]: assert json.dumps(str(module),ensure_ascii=False) not in changed.stderr, changed.stderr
        assert changed.stderr.count('(and 2 more)')==2, changed.stderr
        assert module_artifact.read_bytes()==original_module_artifact
        assert module_state.read_bytes()==original_module_state
        run('build','--project',module_project)
        assert module_artifact.read_bytes()!=original_module_artifact
        assert 'up to date:' in run('build','--project',module_project).stderr

        # Stabilizing one optional system module's load presence must not
        # exempt even that basename from content verification. Only private
        # fixture files change; no installed/system DLL is opened for writing.
        content_project=create(root/'module-content','module-content')
        content_dir=content_project.parent
        component=content_dir/'apphelp.dll'
        component.write_bytes(b'module version 1')
        (content_dir/'compiler-modules.json').write_text(json.dumps([str(component)]))
        run('build','--project',content_project)
        content_artifact=content_dir/'build/module-content.nil'
        content_state=content_dir/'build/.nilbuild/module-content.nil.nildata'
        old_artifact,old_state=content_artifact.read_bytes(),content_state.read_bytes()
        old_component=component.stat()
        (content_dir/'main.matrix').write_text('version two\n', newline='\n')
        (content_dir/'apphelp-content-after').write_bytes(b'module version 2')
        changed=run('build','--project',content_project,'--rebuild',status=2)
        assert 'build input changed during the build' in changed.stderr, changed.stderr
        assert str(component) in changed.stderr, changed.stderr
        assert component.stat().st_size==old_component.st_size
        assert component.stat().st_mtime_ns==old_component.st_mtime_ns
        assert component.read_bytes()==b'module version 2'
        assert content_artifact.read_bytes()==old_artifact
        assert content_state.read_bytes()==old_state
        run('build','--project',content_project)
        assert content_artifact.read_bytes()!=old_artifact
        assert 'up to date:' in run('build','--project',content_project).stderr

        # Real compiler smoke: unchanged imports skip compilation for both routes.
        actual=root/'real';actual.mkdir()
        (actual/'main.ny').write_text('import provider\nfn main() -> i32\n println(answer())\n return 0\nend fn\n', newline='\n')
        provider=actual/'provider.ny';provider.write_text('module provider\npub fn answer() -> i32\n return 42\nend fn\n', newline='\n')
        run('init','--name','real','--entry','main.ny',cwd=actual)
        target='all' if args.llvm.lower() in ('on','true','1') else 'vm'
        run('build','--target',target,cwd=actual,tool=compiler)
        reused=run('build','--target',target,cwd=actual,tool=compiler)
        assert reused.stderr.count('up to date:')==(2 if target=='all' else 1),reused.stderr
        output=subprocess.run([str(interpreter),str(actual/'build/real.nil')],text=True,capture_output=True,check=True)
        assert output.stdout=='42\n'
        previous=provider.stat();provider.write_text(provider.read_text().replace('42','43'), newline='\n');os.utime(provider,ns=(previous.st_atime_ns,previous.st_mtime_ns))
        assert 'up to date:' not in run('build','--target',target,cwd=actual,tool=compiler).stderr
        output=subprocess.run([str(interpreter),str(actual/'build/real.nil')],text=True,capture_output=True,check=True)
        assert output.stdout=='43\n'
    print(f'nilbuild: {checks} format, incremental, parallel, dependency and failure checks passed')


if __name__=='__main__':main()
