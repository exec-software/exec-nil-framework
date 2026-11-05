#!/usr/bin/env python3
"""Pure admission/namespace/output controls; no toolkit or desktop execution."""
import copy
import importlib.util
import os
from pathlib import Path
import tempfile
import time
import unittest

SPEC=importlib.util.spec_from_file_location('gtk_runtime',Path(__file__).with_name('macos-gtk-runtime.py'))
M=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(M)
REV='a'*40


def fixture():
    return {'schema':'nil.macos.gtk-provider.v1','status':'PASS','phase':'complete','revision':REV,
            'architecture':'arm64','qualification':False,'sdk_replaced':False,'product_runtime_admitted':False,
            'problems':[],'elapsed_seconds':220.0,
            **{key:True for key in ('source_unchanged','inputs_unchanged','release_sources_unchanged','prefix_unchanged','cleanup_verified')},
            'phases':[{'name':'compile','status':'PASS','exit_code':0,'cleanup_verified':True}],
            'smoke':{'origins':{key:{'canonical':'/private/'+key+'.dylib'} for key in ('gtk','gobject','glib')}},
            'prerequisites':{'operations':{'gio_path':'/declared/gio.dylib'}},
            'inputs_before':{'/declared/gio.dylib':{'canonical_path':'/private/gio.dylib'}}}


class AdmissionControls(unittest.TestCase):
    def test_exact_successful_profile(self):
        row=fixture();self.assertIs(M.admit_provider(row,REV,'arm64'),row)

    def test_revision_architecture_or_incomplete_phase_rejected(self):
        for field,value in (('revision','b'*40),('architecture','x86_64'),('status','FAIL'),('phase','smoke-headers')):
            row=fixture();row[field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):M.admit_provider(row,REV,'arm64')

    def test_unqualified_scope_and_every_after_seal_required(self):
        for field in ('qualification','sdk_replaced','product_runtime_admitted','source_unchanged','inputs_unchanged',
                      'release_sources_unchanged','prefix_unchanged','cleanup_verified'):
            row=fixture();row[field]=not row[field]
            with self.subTest(field=field),self.assertRaises(ValueError):M.admit_provider(row,REV,'arm64')
        row=fixture();row['inputs_unchanged']=1
        with self.assertRaises(ValueError):M.admit_provider(row,REV,'arm64')

    def test_real_failed_timedout_or_unowned_phase_rejected(self):
        for field,value in (('exit_code',1),('exit_code',False),('status','FAIL'),('cleanup_verified',False),('error','timeout'),('timed_out',True)):
            row=fixture();row['phases'][0][field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):M.admit_provider(row,REV,'arm64')

    def test_duplicate_empty_and_unbounded_inventory_rejected(self):
        for phases in ([],fixture()['phases']*2,fixture()['phases']*451):
            row=fixture();row['phases']=phases
            with self.assertRaises(ValueError):M.admit_provider(row,REV,'arm64')

    def test_whole_bound_and_errors_remain_failures(self):
        for value in (True,float('nan'),float('inf'),-1,1801):
            row=fixture();row['elapsed_seconds']=value
            with self.subTest(value=value),self.assertRaises(ValueError):M.admit_provider(row,REV,'arm64')
        row=fixture();row['problems']=['source changed']
        with self.assertRaises(ValueError):M.admit_provider(row,REV,'arm64')

    def test_canonical_role_names_and_test_only_gio(self):
        result=M.provider_aliases(fixture())
        self.assertEqual(set(result),set(M.ALIASES.values()))
        self.assertEqual(result['libgio-2.0.0.dylib'],Path('/private/gio.dylib'))

    def test_alias_inventory_cannot_use_relative_or_same_library_for_roles(self):
        for value in ('relative/libgtk.dylib','/private/../gtk.dylib','/private/glib.dylib'):
            row=fixture();row['smoke']['origins']['gtk']['canonical']=value
            with self.subTest(value=value),self.assertRaises(ValueError):M.provider_aliases(row)


class NamespaceControls(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory();self.root=Path(self.temporary.name).resolve()
        self.ui=M.provider_modules()[1];self.inputs=self.root/'inputs';self.inputs.mkdir()
        self.aliases={name:self.inputs/name for name in M.ALIASES.values()}
        for name,path in self.aliases.items():path.write_text(name)
    def tearDown(self):self.temporary.cleanup()

    def test_exact_aliases_sealed_without_copy_or_target_write(self):
        curated=self.root/'provider space λ';M.curate(curated,self.aliases)
        observed=M.curated_seal(curated,time.monotonic()+5,self.ui)
        self.assertEqual(set(observed),set(self.aliases))
        for name,path in self.aliases.items():
            self.assertEqual(observed[name]['link'],str(path));self.assertEqual(path.read_text(),name)

    def test_preexisting_or_role_substitution_rejected(self):
        curated=self.root/'exists';curated.mkdir()
        with self.assertRaises(ValueError):M.curate(curated,self.aliases)
        bad=dict(self.aliases);del bad['libgtk-4.1.dylib']
        with self.assertRaises(ValueError):M.curate(self.root/'new',bad)

    def test_file_alias_target_must_already_be_canonical(self):
        alias=self.root/'indirect';alias.symlink_to(self.aliases['libgtk-4.1.dylib'])
        values=dict(self.aliases);values['libgtk-4.1.dylib']=alias
        with self.assertRaises(ValueError):M.curate(self.root/'provider',values)

    def test_missing_special_and_unexpected_entries_rejected(self):
        curated=self.root/'provider';M.curate(curated,self.aliases)
        unknown=curated/'unexpected';unknown.write_text('x')
        with self.assertRaises(ValueError):M.curated_seal(curated,time.monotonic()+5,self.ui)
        unknown.unlink();target=curated/'libgtk-4.1.dylib';target.unlink();os.mkfifo(target)
        with self.assertRaises(ValueError):M.curated_seal(curated,time.monotonic()+5,self.ui)

    def test_alias_retarget_or_byte_change_changes_manifest(self):
        curated=self.root/'provider';M.curate(curated,self.aliases)
        before=M.curated_seal(curated,time.monotonic()+5,self.ui)
        self.aliases['libgtk-4.1.dylib'].write_text('changed')
        self.assertNotEqual(before,M.curated_seal(curated,time.monotonic()+5,self.ui))
        path=curated/'libgtk-4.1.dylib';path.unlink();path.symlink_to(self.aliases['libglib-2.0.0.dylib'])
        self.assertNotEqual(before,M.curated_seal(curated,time.monotonic()+5,self.ui))

    def test_private_copy_is_recorded_as_distinct_identity(self):
        curated=self.root/'mixed';M.curate(curated,self.aliases)
        path=curated/'libglib-2.0.0.dylib';path.unlink();path.write_bytes(self.aliases[path.name].read_bytes())
        row=M.curated_seal(curated,time.monotonic()+5,self.ui)[path.name]
        self.assertIsNone(row['link']);self.assertEqual(row['file']['canonical_path'],str(path))

    def test_directory_permissions_and_symlink_root_rejected(self):
        curated=self.root/'provider';M.curate(curated,self.aliases);curated.chmod(0o755)
        with self.assertRaises(ValueError):M.curated_seal(curated,time.monotonic()+5,self.ui)
        alias=self.root/'alias';alias.symlink_to(curated,target_is_directory=True)
        with self.assertRaises(ValueError):M.curated_seal(alias,time.monotonic()+5,self.ui)


class RuntimeOutputControls(unittest.TestCase):
    def test_exact_mode_summary_and_display_inventory(self):
        self.assertEqual(M.runtime_output(b'UI runtime: 4459 checks passed (--display)\n','--display'),{'mode':'--display','checks':4459})
        self.assertEqual(M.runtime_output(b'UI runtime: 27 checks passed (--darwin-worker-first)\n','--darwin-worker-first')['checks'],27)

    def test_static_modes_require_complete_exact_assertion_counts(self):
        expected={'--provider-present':19,'--provider-absent':20,'--no-display':20,
                  '--darwin-worker-first':27,'--darwin-mixed-provider':31}
        for mode,count in expected.items():
            with self.subTest(mode=mode):
                self.assertEqual(M.runtime_output(f'UI runtime: {count} checks passed ({mode})\n'.encode(),mode)['checks'],count)
                for wrong in (count-1,count+1):
                    with self.assertRaises(ValueError):
                        M.runtime_output(f'UI runtime: {wrong} checks passed ({mode})\n'.encode(),mode)
        with self.assertRaises(ValueError):M.runtime_output(b'UI runtime: 20 checks passed (--unknown)\n','--unknown')

    def test_wrong_mode_empty_skipped_or_partial_success_rejected(self):
        for raw in (b'',b'SKIP\n',b'UI runtime: 1 checks passed (--display)\n',
                    b'UI runtime: 4459 checks passed (--no-display)\n',b'UI runtime: 4459 checks passed (--display)\nextra\n'):
            with self.subTest(raw=raw),self.assertRaises(ValueError):M.runtime_output(raw,'--display')

    def test_clean_environment_preserves_only_explicit_provider_resources(self):
        env=M.runtime_environment(Path('/private/work'),{'prefix':'/private/provider'},
             {'DYLD_LIBRARY_PATH':'bad','DYLD_INSERT_LIBRARIES':'bad','NIL_UI_GTK_DIRECTORY':'bad','NIL_UI_GTK_TEST_PRELOAD_GTK':'bad',
              'DEVELOPER_DIR':'bad','TOOLCHAINS':'bad','PYTHONPATH':'bad','GSK_RENDERER':'vulkan'})
        for key in ('DYLD_LIBRARY_PATH','DYLD_INSERT_LIBRARIES','NIL_UI_GTK_DIRECTORY','NIL_UI_GTK_TEST_PRELOAD_GTK','DEVELOPER_DIR','TOOLCHAINS','PYTHONPATH'):
            self.assertNotIn(key,env)
        self.assertEqual(env['GSK_RENDERER'],'cairo');self.assertEqual(env['GDK_BACKEND'],'macos')
        self.assertEqual(env['GSETTINGS_SCHEMA_DIR'],'/private/provider/share/glib-2.0/schemas')


if __name__=='__main__':unittest.main()
