#!/usr/bin/env python3
"""Pure controls for private GTK inputs, namespace, and evidence admission."""
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import stat
import struct
import tarfile
import tempfile
import time
import unittest
import zipfile

SPEC = importlib.util.spec_from_file_location('gtk_provider', Path(__file__).with_name('macos-gtk-provider.py'))
M = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(M)
UI = M.load('gtk_test_ui', 'scripts/ci/macos-ui-probes.py')


def member(name, kind=tarfile.REGTYPE, data=b'x', link=''):
    result = tarfile.TarInfo(name)
    result.type = kind; result.size = len(data) if kind == tarfile.REGTYPE else 0
    result.mode = 0o755 if kind == tarfile.DIRTYPE else 0o644
    result.mtime = 1700000000; result.linkname = link
    return result


def archive(root, members):
    path = root/'fixture.tar.xz'
    with tarfile.open(path, 'w:xz') as output:
        for item, data in members:
            output.addfile(item, io.BytesIO(data) if item.isfile() else None)
    rows, inventory = M.member_manifest([item for item, _ in members], 'fixture')
    rec = UI.record(path, time.monotonic()+10)
    item = {'root':'fixture', 'bytes':rec['bytes'], 'sha256':rec['sha256'], 'members':inventory, 'required_files':{}}
    return path, item


class ArchiveControls(unittest.TestCase):
    def setUp(self):
        self.members = [member('fixture', tarfile.DIRTYPE), member('fixture/a'),
                        member('fixture/alias', tarfile.SYMTYPE, link='a')]

    def test_regular_sibling_alias_is_explicit(self):
        rows, observed = M.member_manifest(self.members, 'fixture')
        self.assertEqual(rows[-1]['link'], 'a'); self.assertEqual(observed['expanded_bytes'], 1)

    def test_paths_refuse_traversal_and_normalization(self):
        for name in ('/fixture', 'fixture/../a', 'fixture//a', './fixture/a', 'fixture\\a', 'fixture/a\n'):
            with self.subTest(name=name), self.assertRaises(ValueError): M.safe_name(name)

    def test_duplicate_normalized_directory_rejected(self):
        with self.assertRaises(ValueError):
            M.member_manifest(self.members+[member('fixture/', tarfile.DIRTYPE)], 'fixture')

    def test_hardlinks_special_files_and_special_modes_rejected(self):
        for kind in (tarfile.LNKTYPE, tarfile.FIFOTYPE, tarfile.CHRTYPE):
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                M.member_manifest(self.members+[member('fixture/bad',kind)], 'fixture')
        self.members[1].mode |= 0o4000
        with self.assertRaises(ValueError): M.member_manifest(self.members, 'fixture')

    def test_symlinks_cannot_escape_chain_or_become_parents(self):
        for target in ('../a','/a','missing','alias'):
            rows=copy.deepcopy(self.members); rows[-1].linkname=target
            with self.subTest(target=target), self.assertRaises(ValueError): M.member_manifest(rows,'fixture')
        with self.assertRaises(ValueError):
            M.member_manifest(self.members+[member('fixture/alias/child')], 'fixture')

    def test_size_and_time_bounds(self):
        for field, value in (('size',M.MAX_FILE+1),('mtime',float('nan')),('mtime',-1),('mtime',True)):
            rows=copy.deepcopy(self.members); setattr(rows[1],field,value)
            with self.subTest(field=field,value=value), self.assertRaises(ValueError): M.member_manifest(rows,'fixture')

    def test_extract_preserves_generated_freshness_and_alias(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp).resolve(); generated=member('fixture/generated.c'); generated.mtime=1700000900
            grammar=member('fixture/input.y')
            members=[(self.members[0],b''),(generated,b'c'),(grammar,b'y'),
                     (member('fixture/alias',tarfile.SYMTYPE,link='generated.c'),b'')]
            path,item=archive(root,members); dest=root/'out'; dest.mkdir()
            M.extract_source(path,dest,item,time.monotonic()+10,UI.record)
            self.assertGreater((dest/'fixture/generated.c').stat().st_mtime,(dest/'fixture/input.y').stat().st_mtime)
            self.assertTrue((dest/'fixture/alias').is_symlink())
            self.assertEqual((dest/'fixture/alias').read_bytes(),b'c')

    def test_digest_or_member_inventory_mismatch_stops_before_write(self):
        for field in ('sha256','members'):
            with self.subTest(field=field), tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp).resolve(); path,item=archive(root,[(self.members[0],b''),(self.members[1],b'x')])
                if field=='sha256': item[field]='0'*64
                else: item[field]['entries']+=1
                dest=root/'out'; dest.mkdir()
                with self.assertRaises(ValueError): M.extract_source(path,dest,item,time.monotonic()+10,UI.record)
                self.assertEqual(list(dest.iterdir()),[])

    def test_required_css_bytes_cannot_be_substituted(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp).resolve(); path,item=archive(root,[(self.members[0],b''),(self.members[1],b'x')])
            item['required_files']={'fixture/a':{'bytes':1,'sha256':'0'*64}}
            dest=root/'out'; dest.mkdir()
            with self.assertRaisesRegex(ValueError,'Required generated'):
                M.extract_source(path,dest,item,time.monotonic()+10,UI.record)

    def test_wheel_rejects_symlink_before_extraction(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp).resolve(); path=root/'wheel.zip'
            with zipfile.ZipFile(path,'w') as output:
                item=zipfile.ZipInfo('mesonbuild/mesonmain.py'); item.external_attr=(stat.S_IFLNK|0o777)<<16
                output.writestr(item,'/elsewhere')
            rec=UI.record(path,time.monotonic()+10); dest=root/'out';dest.mkdir()
            with self.assertRaisesRegex(ValueError,'wheel member'):
                M.extract_meson(path,dest,{'bytes':rec['bytes'],'sha256':rec['sha256']},time.monotonic()+10,UI.record)
            self.assertEqual(list(dest.iterdir()),[])


class ProfileControls(unittest.TestCase):
    def test_locked_build_order_archives_css_and_options(self):
        lock=json.loads(M.LOCK.read_text())
        self.assertEqual(tuple(x['root'] for x in lock['sources']),M.BUILD_ORDER)
        for item in lock['sources']:
            self.assertEqual(len(item['sha256']),64)
            self.assertGreater(item['bytes'],0)
            self.assertEqual(len(item['options']),len(set(x.split('=')[0] for x in item['options'])))
        gtk=lock['sources'][-1]
        css=[k for k in gtk['required_files'] if k.endswith('.css')]
        self.assertEqual(len(css),10)
        self.assertIn('gtk-4.18.6/gtk/theme/Default/Default-light.css',css)
        for option in ('-Dmacos-backend=true','-Dwayland-backend=false','-Dbuild-tests=false'):
            self.assertIn(option,gtk['options'])
        self.assertEqual(lock['meson']['version'],'1.12.1')

    def test_environment_excludes_loader_tool_and_python_overrides(self):
        dirty={x:'injected' for x in ('PATH','DYLD_LIBRARY_PATH','DYLD_INSERT_LIBRARIES','DYLD_FRAMEWORK_PATH',
             'PYTHONPATH','PYTHONHOME','CC','CXX','CFLAGS','LDFLAGS','DEVELOPER_DIR','TOOLCHAINS',
             'COMPILER_PATH','GCC_EXEC_PREFIX','CCC_OVERRIDE_OPTIONS','PKG_CONFIG_PATH','PKG_CONFIG_SYSROOT_DIR')}
        env=M.environment(Path('/private/work'),dirty)
        for key in dirty:
            self.assertNotEqual(env.get(key),'injected')
        self.assertEqual(env['PATH'],'/private/work/tool-bin:/usr/bin:/bin:/usr/sbin:/sbin')

    def test_setup_is_shared_release_private_no_fallback(self):
        args=M.setup_arguments(['python','meson'],Path('/source'),Path('/build'),Path('/prefix'),Path('/native'),{'options':['-Dtests=false']})
        for expected in ('--prefix=/prefix','--libdir=lib','--buildtype=release','--default-library=shared','--wrap-mode=nofallback','--native-file=/native'):
            self.assertIn(expected,args)
        self.assertEqual(args[-1],'-Dtests=false')

    def test_deadline_cannot_be_extended(self):
        with self.assertRaises(ValueError): M.remaining(time.monotonic()-1,600)
        self.assertLessEqual(M.remaining(time.monotonic()+.1,600),.1)

    def test_private_roots_reject_checkout_and_alias_overlap(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp).resolve(); checkout=root/'checkout';checkout.mkdir(); parent=root/'work';parent.mkdir();alias=root/'alias';alias.symlink_to(parent,target_is_directory=True)
            with self.assertRaises(ValueError): UI.paths(root/'output',checkout/'private',checkout)
            with self.assertRaises(ValueError): UI.paths(parent/'output',alias/'output'/'nested',checkout)

    def test_prefix_seal_preserves_internal_alias_and_resources(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp).resolve(); (root/'real').write_text('bytes');(root/'alias').symlink_to('real')
            rows=M.tree_seal(root,time.monotonic()+10,UI.record)
            self.assertEqual(rows['alias'],{'link':'real','target':'real'})
            self.assertEqual(rows['real']['bytes'],5)

    def test_prefix_seal_refuses_external_directory_and_special_alias(self):
        with tempfile.TemporaryDirectory() as tmp:
            parent=Path(tmp).resolve();root=parent/'prefix';root.mkdir();(parent/'other').write_text('x')
            (root/'alias').symlink_to('../other')
            with self.assertRaises(ValueError): M.tree_seal(root,time.monotonic()+10,UI.record)
            (root/'alias').unlink();(root/'dir').mkdir();(root/'alias').symlink_to('dir',target_is_directory=True)
            with self.assertRaises(ValueError): M.tree_seal(root,time.monotonic()+10,UI.record)
            (root/'alias').unlink();os.mkfifo(root/'pipe')
            with self.assertRaises(ValueError): M.tree_seal(root,time.monotonic()+10,UI.record)

    def test_image_oracle_requires_roundtrip_versions_and_all_origins(self):
        row={'png_header':'10650','png_runtime':'10650','png_roundtrip':'passed','jpeg_header':'80','jpeg_abi':'passed',
             'tiff_header':'20250521','tiff_runtime':'1','png_path':'/png','tiff_path':'/tiff','jpeg_path':'/jpeg'}
        encode=lambda x: ''.join(k+'='+v+'\n' for k,v in x.items()).encode()
        self.assertEqual(M.image_result(encode(row)),row)
        for field,value in (('png_runtime','10649'),('png_roundtrip','failed'),('tiff_runtime','0')):
            changed=dict(row);changed[field]=value
            with self.subTest(field=field),self.assertRaises(ValueError): M.image_result(encode(changed))
        del row['jpeg_path']
        with self.assertRaises(ValueError): M.image_result(encode(row))


def smoke_fixture():
    result = {'schema':'nil.macos-gtk-provider-smoke.v1','scope':'private-provider-own-process','status':'PASS',
        'language_acceptance':False,'main_thread':True,'backend':'GdkMacosDisplay','renderer':'GskCairoRenderer',
        'versions':{kind+'_'+side:value for kind,value in (('gtk','4.18.6'),('pango','1.56.4'),('pixbuf','2.42.12'),('png','1.6.50')) for side in ('header','runtime')},
        'cleanup':{'verified':True,'own_window_hidden':True,'gtk_toplevel_count':0},'unexpected_keys':0,'close_requests':1,'elapsed_seconds':2.0,
        'widgets':{'entry_text':'n','checkbox_active':True,'entry_changed':1,'button_clicked':1,'checkbox_toggled':1},
        'png':{'decoded':True,'width':1,'height':1,'channels':4,'rgba':[17,34,51,255]},
        'font':{'quartz':True,'coretext':True,'width':40,'height':12},
        'events':[],'observed_keys':[{'pressed':1,'released':1} for _ in range(3)],'dispatched_keys':[],
        'origins':{key:{'declared':'/private/'+key,'canonical':'/private/'+key,'file_backed':True}
                   for key in ('gtk','pango','pangocairo','pixbuf','glib','gobject','cairo','png','harfbuzz')}}
    for key in ('coretext','appkit'):
        result['origins'][key]={'declared':'/System/Library/Frameworks/'+key,'canonical':None,'file_backed':False}
    for index in range(3):
        seconds=1000.0+index
        event={'stage':index,'keycode':45 if index==0 else 49,'keyval':110 if index==0 else 32,'modifiers':0,
               'queued_count':2,'timestamp_seconds':seconds,'timestamp_hex':seconds.hex(),'gdk_time_ms':int(seconds*1000),'window_number':17}
        result['events'].append(event)
        for released in (False,True):
            result['dispatched_keys'].append({**{key:event[key] for key in ('stage','keycode','keyval','modifiers','gdk_time_ms')},
                'release':released,'own_surface':True,'matches':True})
    return result


class SmokeControls(unittest.TestCase):
    def test_complete_native_oracle_and_framework_boundary(self):
        row=smoke_fixture();self.assertIs(M.smoke_result(row),row)

    def test_no_success_from_queue_without_dispatch_or_widget_effect(self):
        for path,value in ((('observed_keys',0,'released'),0),(('widgets','entry_text'),''),
                           (('dispatched_keys',0,'own_surface'),False),(('dispatched_keys',1,'release'),False)):
            row=smoke_fixture();target=row
            for name in path[:-1]:target=target[name]
            target[path[-1]]=value
            with self.subTest(path=path),self.assertRaises(ValueError):M.smoke_result(row)

    def test_actual_dispatch_timestamp_key_and_order_bound(self):
        for field,value in (('gdk_time_ms',3),('keycode',49),('stage',1),('keyval',32),('modifiers',1)):
            row=smoke_fixture();row['dispatched_keys'][0][field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):M.smoke_result(row)
        row=smoke_fixture();row['events'][0]['timestamp_hex']='0x1p0'
        with self.assertRaises(ValueError):M.smoke_result(row)

    def test_counts_reject_boolean_alias_and_duplicates(self):
        for path in (('events',0,'stage'),('cleanup','gtk_toplevel_count'),('widgets','entry_changed')):
            row=smoke_fixture();target=row
            for name in path[:-1]:target=target[name]
            target[path[-1]]=True if target[path[-1]] else False
            with self.subTest(path=path),self.assertRaises(ValueError):M.smoke_result(row)
        row=smoke_fixture();row['dispatched_keys'].append(row['dispatched_keys'][-1])
        with self.assertRaises(ValueError):M.smoke_result(row)

    def test_resources_origin_and_real_cleanup_are_mandatory(self):
        for path,value in ((('png','rgba'),[0,0,0,255]),(('font','coretext'),False),
                          (('origins','gtk','file_backed'),False),(('cleanup','own_window_hidden'),False),
                          (('cleanup','verified'),False),(('versions','pango_runtime'),'1.56.3')):
            row=smoke_fixture();target=row
            for name in path[:-1]:target=target[name]
            target[path[-1]]=value
            with self.subTest(path=path),self.assertRaises(ValueError):M.smoke_result(row)

    def test_harfbuzz_font_origin_is_required_and_file_backed(self):
        row=smoke_fixture();del row['origins']['harfbuzz']
        with self.assertRaises(ValueError):M.smoke_result(row)
        row=smoke_fixture();row['origins']['harfbuzz']['file_backed']=False
        with self.assertRaises(ValueError):M.smoke_result(row)

    def test_qualified_or_out_of_bound_reports_cannot_pass(self):
        for field,value in (('language_acceptance',True),('elapsed_seconds',30),('elapsed_seconds',float('nan')),
                            ('elapsed_seconds',True),('status','FAIL'),('renderer','GskGLRenderer')):
            row=smoke_fixture();row[field]=value
            with self.subTest(field=field,value=value),self.assertRaises(ValueError):M.smoke_result(row)


class LoaderControls(unittest.TestCase):
    def test_native_loader_header_rejects_wrong_cpu_kind_and_truncation(self):
        header=struct.pack('<8I',0xfeedfacf,0x100000c,0,6,12,1000,0,0)
        self.assertEqual(M.macho_header(header,'arm64')['file_type'],6)
        for raw,arch in ((header,'x86_64'),(header[:31],'arm64'),
                         (struct.pack('<8I',0xfeedfacf,0x100000c,0,1,12,1000,0,0),'arm64')):
            with self.assertRaises(ValueError):M.macho_header(raw,arch)

    def test_all_relevant_load_commands_are_retained(self):
        raw=b'Load command 0\n cmd LC_ID_DYLIB\n name @rpath/libgtk.dylib (offset 24)\n cmd LC_RPATH\n path @loader_path (offset 12)\n cmd LC_LOAD_WEAK_DYLIB\n name /usr/lib/libc.dylib (offset 24)\n'
        rows=M.loader_commands(raw)
        self.assertEqual([x['command'] for x in rows],['LC_ID_DYLIB','LC_RPATH','LC_LOAD_WEAK_DYLIB'])
        self.assertEqual(rows[1]['path'],'@loader_path')

    def test_missing_loader_name_is_failure(self):
        with self.assertRaises(ValueError): M.loader_commands(b'cmd LC_LOAD_DYLIB\ncmdsize 24\n')

    def test_rpath_preserves_first_existing_choice_and_requires_resolution(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp).resolve();first=root/'first';second=root/'second';first.mkdir();second.mkdir()
            (first/'lib.dylib').write_text('first');(second/'lib.dylib').write_text('second')
            self.assertEqual(M.resolve_load('@rpath/lib.dylib',first/'owner',['@loader_path'],root/'binary'),first/'lib.dylib')
            self.assertEqual(M.resolve_load('@rpath/lib.dylib',root/'binary',[str(first),str(second)],root/'binary'),first/'lib.dylib')
            with self.assertRaises(ValueError): M.resolve_load('@rpath/missing',root/'binary',[str(first)],root/'binary')
            with self.assertRaises(ValueError): M.resolve_load('@unknown/lib',root/'binary',[],root/'binary')

    def test_system_boundary_is_explicit_not_arbitrary_absolute(self):
        self.assertTrue(M.system_library('/usr/lib/libSystem.B.dylib'))
        self.assertTrue(M.system_library('/System/Library/Frameworks/AppKit.framework/AppKit'))
        self.assertFalse(M.system_library('/tmp/usr/lib/libSystem.B.dylib'))
        self.assertFalse(M.system_library('/opt/homebrew/lib/libpng.dylib'))


if __name__ == '__main__':
    unittest.main()
