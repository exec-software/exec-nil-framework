#!/usr/bin/env python3
"""Bounded standalone Ny UI runtime checks against one unchanged private GTK provider."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import platform
import re
import stat
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
TOTAL_SECONDS, RESERVE_SECONDS = 180, 20
MiB = 1024**2
INPUTS = ('scripts/ci/macos-gtk-runtime.py', 'scripts/ci/macos-gtk-provider.py',
          'scripts/ci/macos-ui-probes.py', 'tests/tools/ordinary_io_parity_contract.py',
          'validation/ny-3d/scripts/debugger_process.py', 'nilcompiler/src/ui_runtime.cpp',
          'nilcompiler/src/ui_runtime.hpp', 'tests/ui_runtime_tests.cpp', 'tests/ui_registry_observer.hpp',
          'tests/ui_lifecycle_tests.cpp', 'nilcompiler/src/codegen.hpp', 'nilcompiler/src/native_runtime.hpp',
          'nilcompiler/src/nil_loader.hpp', 'nilcompiler/src/vm.hpp', 'nilcompiler/src/ast.hpp',
          'nilcompiler/src/lexer.hpp', 'nilcompiler/src/annotation_arguments.hpp',
          'nilcompiler/src/source_debug.hpp', 'nilcompiler/src/database_runtime.hpp', 'nilcompiler/src/memory_profile.hpp')
ALIASES = {'gtk':'libgtk-4.1.dylib','gobject':'libgobject-2.0.0.dylib',
           'glib':'libglib-2.0.0.dylib','gio':'libgio-2.0.0.dylib'}
MODE_CHECKS = {'--provider-present':19, '--provider-absent':20, '--no-display':20,
               '--darwin-worker-first':27, '--darwin-mixed-provider':31}


def require(value, message):
    if not value:
        raise ValueError(message)


def load(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT/relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def provider_modules():
    return (load('runtime_gtk_provider', 'scripts/ci/macos-gtk-provider.py'),
            load('runtime_gtk_file_identity', 'scripts/ci/macos-ui-probes.py'))


def save(path, value):
    with path.open('x', encoding='utf-8') as stream:
        json.dump(value, stream, indent=2, sort_keys=True); stream.write('\n')


def read_json(path, deadline, ui):
    row = ui.record(path, deadline)
    require(row['bytes'] <= 16*MiB, 'JSON evidence bound exceeded')
    data = path.read_bytes()
    require(len(data) == row['bytes'] and hashlib.sha256(data).hexdigest() == row['sha256'], 'JSON evidence changed')
    return json.loads(data), row


def admit_provider(report, revision, architecture):
    require(report.get('schema') == 'nil.macos.gtk-provider.v1' and report.get('status') == 'PASS' and
            report.get('phase') == 'complete' and report.get('revision') == revision and
            report.get('architecture') == architecture and architecture in ('arm64','x86_64'),
            'Successful exact-source native provider required')
    require(report.get('qualification') is False and report.get('sdk_replaced') is False and
            report.get('product_runtime_admitted') is False and report.get('problems') == [],
            'Provider scope/problems differ')
    for key in ('source_unchanged','inputs_unchanged','release_sources_unchanged','prefix_unchanged','cleanup_verified'):
        require(report.get(key) is True, 'Provider lacks '+key)
    elapsed = report.get('elapsed_seconds')
    require(type(elapsed) in (int,float) and math.isfinite(elapsed) and 0 <= elapsed <= 1800,
            'Provider whole bound differs')
    phases = report.get('phases')
    require(isinstance(phases,list) and 0 < len(phases) <= 450 and
            len({p['name'] for p in phases}) == len(phases), 'Provider phase inventory differs')
    require(all(p.get('status') == 'PASS' and type(p.get('exit_code')) is int and p.get('exit_code') == 0 and
                p.get('cleanup_verified') is True and not p.get('error') and not p.get('timed_out') for p in phases),
            'Provider contains incomplete/failed owned phase')
    return report


def provider_aliases(report):
    origins = report['smoke']['origins']
    result = {ALIASES[key]:Path(origins[key]['canonical']) for key in ('gtk','gobject','glib')}
    declared = report['prerequisites']['operations']['gio_path']
    result[ALIASES['gio']] = Path(report['inputs_before'][declared]['canonical_path'])
    require(all(path.is_absolute() and '..' not in path.parts for path in result.values()) and
            len(set(result.values())) == 4, 'Provider canonical alias inventory differs')
    return result


def validate_provider(path, revision, architecture, deadline):
    """Read-only same-job provider validation; reusable by directed-input probes.

    Rehash the provider's declared files and installed prefix. This intentionally
    admits only the original runner/source/profile; it is not SDK qualification.
    """
    provider, ui = provider_modules()
    path = Path(path)
    require(path.is_absolute() and not path.is_symlink() and path.resolve(strict=True) == path,
            'Canonical regular provider report path required')
    report, anchor = read_json(path, deadline, ui)
    admit_provider(report, revision, architecture)
    work, prefix = Path(report['work_dir']), Path(report['prefix'])
    require(work.is_absolute() and work.resolve(strict=True) == work and prefix == work/'prefix' and
            prefix.resolve(strict=True) == prefix and ROOT not in work.parents and work not in ROOT.parents and work != ROOT,
            'Provider work/prefix identity differs')
    info = work.stat()
    require(stat.S_IMODE(info.st_mode) == 0o700 and info.st_uid == os.getuid(), 'Private provider work ownership differs')
    inputs = report['inputs_before']
    require(isinstance(inputs,dict) and 0 < len(inputs) <= 5000, 'Provider input inventory bound differs')
    after, _ = read_json(path.parent/'inputs-after.json', deadline, ui)
    require(inputs == after, 'Provider original input seal differs')
    for name in provider.INPUTS:
        expected = inputs.get(str(ROOT/name))
        require(expected is not None and ui.record(ROOT/name, deadline) == expected,
                'Provider source helper differs from current checkout: '+name)
    for name, expected in inputs.items():
        require(ui.record(Path(name), deadline) == expected, 'Declared provider input changed: '+name)
    before, _ = read_json(path.parent/'prefix-before.json', deadline, ui)
    after, _ = read_json(path.parent/'prefix-after.json', deadline, ui)
    require(before == after and provider.tree_seal(prefix, deadline, ui.record) == before,
            'Installed private provider changed')
    phases = {p['name']:p for p in report['phases']}
    for name, args, expected in (
        ('revision-before',['rev-parse','HEAD'],revision+'\n'), ('revision-after',['rev-parse','HEAD'],revision+'\n'),
        ('source-before',['status','--porcelain','--untracked-files=no'],''),
        ('source-after',['status','--porcelain','--untracked-files=no'],'')):
        require(phases[name]['argv'] == ['/usr/bin/git','-C',str(ROOT),*args], 'Provider checkout command differs')
        require((path.parent/'logs'/(name+'.stdout')).read_text() == expected, 'Provider checkout observation differs')
    raw = path.parent/'logs/gtk-smoke.stdout'
    require(ui.record(raw,deadline)['sha256'] == phases['gtk-smoke']['stdout_record']['sha256'], 'Provider raw smoke changed')
    observed = json.loads(raw.read_bytes())
    require(observed == report['smoke'], 'Provider smoke report differs from actual output')
    provider.smoke_result(observed)
    closure, _ = read_json(path.parent/'loader-closure.json', deadline, ui)
    require(isinstance(closure,dict) and 0 < len(closure) <= 256, 'Provider loader closure bound differs')
    for name, row in closure.items():
        require(row['file'] == inputs[name] and ui.record(Path(name),deadline) == row['file'],
                'Provider loader closure input changed')
        with Path(name).open('rb') as stream:
            header=provider.macho_header(stream.read(32),architecture)
        require(header==row['header'],'Current provider Mach-O architecture/header differs')
        for item in row['loads']:
            require(provider.system_library(item['name']) if item.get('system_cache_or_framework_boundary') else
                    item['target'] in closure, 'Provider loader closure is incomplete')
    aliases = provider_aliases(report)
    for name, target in aliases.items():
        require(str(target) in closure and ui.record(target,deadline)['sha256'] == closure[str(target)]['file']['sha256'],
                'Curated runtime absent from sealed provider closure: '+name)
    require(prefix in aliases[ALIASES['gtk']].parents, 'GTK runtime escaped private prefix')
    require(ui.record(path,deadline) == anchor, 'Provider report changed during validation')
    return report


def curate(directory, aliases):
    require(not directory.exists() and not directory.is_symlink(), 'Fresh curated directory required')
    require(set(aliases) == set(ALIASES.values()), 'Curated provider role inventory differs')
    directory.mkdir(mode=0o700)
    for name, target in aliases.items():
        require(target.is_absolute() and target.resolve(strict=True) == target and target.is_file(),
                'Curated provider target must be canonical regular file')
        (directory/name).symlink_to(target)


def curated_seal(directory, deadline, ui):
    require(directory.is_dir() and not directory.is_symlink(), 'Curated provider directory changed')
    info=directory.stat()
    require(stat.S_IMODE(info.st_mode)==0o700 and info.st_uid==os.getuid(), 'Curated directory ownership/mode changed')
    result = {}
    for path in sorted(directory.iterdir()):
        require(path.name in ALIASES.values(), 'Unexpected curated provider file')
        mode = path.lstat().st_mode
        require(stat.S_ISLNK(mode) or stat.S_ISREG(mode), 'Curated provider contains special file')
        result[path.name] = {'link':os.readlink(path) if stat.S_ISLNK(mode) else None,
                            'file':ui.record(path,deadline)}
    return result


def runtime_environment(work, provider_report, inherited=None):
    provider, _ = provider_modules()
    env = provider.environment(work,inherited)
    prefix = Path(provider_report['prefix'])
    env.update(GSK_RENDERER='cairo',GDK_BACKEND='macos',GSETTINGS_BACKEND='memory',
               GSETTINGS_SCHEMA_DIR=str(prefix/'share/glib-2.0/schemas'),XDG_DATA_DIRS=str(prefix/'share'),
               XDG_CACHE_HOME=str(work/'cache'),XDG_CONFIG_HOME=str(work/'config'),XDG_DATA_HOME=str(work/'data'))
    return env


def runtime_output(raw, mode):
    require(len(raw) <= 4096, 'Runtime summary exceeds bound')
    match = re.fullmatch(rb'UI runtime: ([0-9]+) checks passed \((--[a-z-]+)\)\n',raw)
    require(match is not None and match[2].decode() == mode, 'Runtime summary/mode differs')
    count = int(match[1])
    require(4000 <= count <= 20000 if mode == '--display' else
            mode in MODE_CHECKS and count == MODE_CHECKS[mode],
            'Runtime assertion inventory is incomplete/unbounded')
    return {'mode':mode,'checks':count}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--provider-report',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--work-dir',required=True,type=Path)
    parser.add_argument('--revision',required=True)
    parser.add_argument('--architecture',required=True,choices=('arm64','x86_64'))
    args = parser.parse_args()
    require(sys.platform == 'darwin' and platform.machine() == args.architecture and
            re.fullmatch('[0-9a-f]{40}',args.revision), 'Exact-source matching native macOS required')
    provider,ui = provider_modules()
    output,work = ui.paths(args.output,args.work_dir)
    output.mkdir(parents=True,mode=0o700);work.mkdir(mode=0o700)
    logs=output/'logs';logs.mkdir()
    for name in ('tmp','tool-bin'): (work/name).mkdir()
    start=time.monotonic();final=start+TOTAL_SECONDS;deadline=final-RESERVE_SECONDS
    report={'schema':'nil.macos.gtk-runtime.v1','status':'FAIL','revision':args.revision,'architecture':args.architecture,
            'scope':'Standalone ui_runtime.cpp execution plus ui_lifecycle_tests.cpp syntax only; excludes VM lifecycle execution, language, external-input, debugger and installed SDK acceptance',
            'qualification':False,'sdk_replaced':False,'language_acceptance':False,'work_dir':str(work),
            'work_retained':True,'phase':'initialize','phases':[],'cases':[],'problems':[],
            'limits':{'whole_seconds':TOTAL_SECONDS,'reserve_seconds':RESERVE_SECONDS,'phase_output_bytes':MiB}}
    support=ownership=None;inputs={};provider_anchor=None;curated_before={};baseline=None
    env=provider.environment(work)
    def run(name,argv,limit=10,*,variables=None,strict=True,final_check=False):
        if not final_check:report['phase']=name
        row=support.run_process(ownership,[str(x) for x in argv],work,variables or env,logs,name,
                                provider.remaining(final-5 if final_check else deadline,limit))
        report['phases'].append(row);save(logs/(name+'.json'),row)
        require(row.get('cleanup_verified') is True and not row.get('error') and not row.get('timed_out'),
                name+': owned process/cleanup failure')
        if strict:require(row.get('exit_code')==0,name+': nonzero exit; raw logs retained')
        return row,(logs/row['stdout']).read_bytes()
    try:
        for name in INPUTS:inputs[str(ROOT/name)]=ui.record(ROOT/name,deadline)
        support=load('runtime_gtk_owner','tests/tools/ordinary_io_parity_contract.py');ownership=support.load_ownership(ROOT)
        require(support.MAX_OUTPUT==MiB,'Capture bound changed')
        _,raw=run('revision-before',['/usr/bin/git','-C',ROOT,'rev-parse','HEAD'])
        require(raw.decode().strip()==args.revision,'Runtime checkout revision differs')
        _,raw=run('source-before',['/usr/bin/git','-C',ROOT,'status','--porcelain','--untracked-files=no'])
        require(not raw,'Clean source required')
        baseline=validate_provider(args.provider_report,args.revision,args.architecture,deadline)
        provider_anchor=ui.record(args.provider_report,deadline);report['provider_report']=provider_anchor
        report['prefix']=baseline['prefix'];report['provider_validated_before']=True
        aliases=provider_aliases(baseline);report['aliases']={name:str(path) for name,path in aliases.items()}
        curated=work/'provider space λ';curate(curated,aliases)
        incomplete=work/'incomplete';curate(incomplete,aliases);(incomplete/ALIASES['gtk']).unlink()
        mixed=work/'mixed';curate(mixed,aliases);(mixed/ALIASES['glib']).unlink()
        original=aliases[ALIASES['glib']];expected=ui.record(original,deadline)
        copied=0;digest=hashlib.sha256()
        with original.open('rb') as source,(mixed/ALIASES['glib']).open('xb') as target:
            while block:=source.read(MiB):
                provider.remaining(deadline,1);copied+=len(block)
                require(copied<=expected['bytes'],'GLib grew while copying private negative fixture')
                digest.update(block);target.write(block)
        require(copied==expected['bytes'] and digest.hexdigest()==expected['sha256'] and ui.record(original,deadline)==expected,
                'GLib changed before mixed-copy fixture')
        (mixed/ALIASES['glib']).chmod(0o600)
        for path in (curated,incomplete,mixed):curated_before[str(path)]=curated_seal(path,deadline,ui)
        empty=work/'empty';empty.mkdir(mode=0o700);curated_before[str(empty)]=curated_seal(empty,deadline,ui)
        selected=work/'selected-file';selected.write_text('not a provider directory\n');inputs[str(selected)]=ui.record(selected,deadline)
        env=runtime_environment(work,baseline)
        compiler=Path(baseline['toolchain']['compiler']);sdk=Path(baseline['toolchain']['sdk'])
        for path in (compiler,sdk/'SDKSettings.json'):
            require(ui.record(path,deadline)==baseline['inputs_before'][str(path)],'Selected compiler/SDK differs from provider')
        executable=work/'runtime space λ'
        run('compile-runtime',[compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-pthread','-isysroot',sdk,
            '-mmacosx-version-min=15.0','-iquote',ROOT/'nilcompiler/src','-iquote',ROOT/'tests',
            ROOT/'nilcompiler/src/ui_runtime.cpp',ROOT/'tests/ui_runtime_tests.cpp','-o',executable],60)
        inputs[str(executable)]=ui.record(executable,deadline);report['binary']=inputs[str(executable)]
        # Match this CMake target's C++17 warning profile; this checks the real
        # newly enabled platform body without linking or executing the VM.
        run('lifecycle-syntax',[compiler,'-std=c++17','-pthread','-isysroot',sdk,
            '-mmacosx-version-min=15.0','-iquote',ROOT/'nilcompiler/src','-iquote',ROOT/'tests',
            '-fsyntax-only',ROOT/'tests/ui_lifecycle_tests.cpp'],30)
        report['lifecycle_syntax']={'status':'PASS','executed':False,'linked':False,
                                  'source':inputs[str(ROOT/'tests/ui_lifecycle_tests.cpp')]}
        cases=[('provider-present','--provider-present',str(curated),10),
               ('provider-unset','--provider-absent',None,10),('provider-empty-setting','--provider-absent','',10),
               ('provider-relative','--provider-absent','provider space λ',10),
               ('provider-missing','--provider-absent',str(work/'does-not-exist'),10),
               ('provider-file','--provider-absent',str(selected),10),
               ('provider-empty-directory','--provider-absent',str(empty),10),
               ('provider-incomplete','--provider-absent',str(incomplete),10),
               ('provider-mixed','--darwin-mixed-provider',str(mixed),15),
               ('invalid-backend','--no-display',str(curated),15),
               ('worker-first','--darwin-worker-first',str(curated),20),
               ('display','--display',str(curated),45)]
        for name,mode,selection,limit in cases:
            variables=dict(env)
            if selection is not None:variables['NIL_UI_GTK_DIRECTORY']=selection
            if mode=='--darwin-mixed-provider':variables['NIL_UI_GTK_TEST_PRELOAD_GTK']=str(aliases[ALIASES['gtk']])
            case={'name':name,'mode':mode,'provider_selection':selection,'status':'FAIL'};report['cases'].append(case)
            try:
                row,raw=run(name,[executable,mode],limit,variables=variables,strict=False)
                require(row['exit_code']==0,name+': runtime rejection/acceptance contract failed')
                case.update(runtime_output(raw,mode),status='PASS')
            except Exception as error:
                case['error']=str(error);report['problems'].append(name+': '+str(error))
                if time.monotonic()>=deadline or report['phases'][-1].get('cleanup_verified') is not True:break
        require(len(report['cases'])==len(cases) and all(x['status']=='PASS' for x in report['cases']),
                'Standalone runtime case inventory incomplete/failed')
        report['status']='PASS';report['phase']='complete'
    except Exception as error:report['problems'].append(type(error).__name__+': '+str(error))
    finally:
        checks={};report['after_checks']=checks
        def final_check(name,action):
            try:action();checks[name]={'status':'PASS'}
            except Exception as error:
                checks[name]={'status':'FAIL','error':str(error)};report['problems'].append(name+': '+str(error))
        def provider_after():
            require(provider_anchor is not None,'Initial provider validation did not complete')
            require(ui.record(args.provider_report,final-5)==provider_anchor,'Provider report changed')
            require(validate_provider(args.provider_report,args.revision,args.architecture,final-5)==baseline,'Provider evidence changed')
        def inputs_after():
            observed={name:ui.record(Path(name),final-5) for name in inputs}
            save(output/'inputs-after.json',observed);require(observed==inputs,'Runtime source/binary inputs changed')
        def aliases_after():
            observed={name:curated_seal(Path(name),final-5,ui) for name in curated_before}
            save(output/'curated-after.json',observed);require(observed==curated_before,'Curated namespace changed')
            require(not (work/'does-not-exist').exists() and not (work/'does-not-exist').is_symlink(),'Missing provider fixture changed')
        def source_after():
            _,raw=run('revision-after',['/usr/bin/git','-C',ROOT,'rev-parse','HEAD'],5,final_check=True)
            require(raw.decode().strip()==args.revision,'Source revision changed')
            _,raw=run('source-after',['/usr/bin/git','-C',ROOT,'status','--porcelain','--untracked-files=no'],5,final_check=True)
            require(not raw,'Tracked source changed')
        for name,action in (('provider',provider_after),('inputs',inputs_after),('curated',aliases_after),('source',source_after)):
            final_check(name,action)
        report['inputs_before']=inputs;save(output/'curated-before.json',curated_before)
        report['cleanup_verified']=bool(report['phases']) and all(x.get('cleanup_verified') is True for x in report['phases'])
        report['elapsed_seconds']=time.monotonic()-start
        if report['problems'] or not report['cleanup_verified'] or report['elapsed_seconds']>TOTAL_SECONDS:report['status']='FAIL'
        save(output/'report.json',report)
    print(json.dumps({k:report[k] for k in ('status','phase','problems','cleanup_verified','elapsed_seconds')}))
    return 0 if report['status']=='PASS' else 1

if __name__=='__main__':
    raise SystemExit(main())
