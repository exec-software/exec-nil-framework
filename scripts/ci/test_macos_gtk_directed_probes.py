#!/usr/bin/env python3
"""Pure identity/oracle controls; these do not claim native GTK event delivery."""
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import time
import unittest

SPEC = importlib.util.spec_from_file_location('gtk_directed', Path(__file__).with_name('macos-gtk-directed-probes.py'))
gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gate)


class Controls(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(); self.addCleanup(temporary.cleanup)
        self.base = Path(temporary.name).resolve(); self.root = self.base/'repo'; self.root.mkdir()
        self.aliases = {name: self.base/name for name in gate.ALIASES.values()}

    def ready(self):
        return dict(pid=123, window_number=17, active=True, key_window=True, gtk_entry_focused=True, main_thread=True,
                    base_seconds=12345, gtk_header='4.18.6', gtk_runtime='4.18.6',
                    app_running=False, activation_policy=1,
                    origins={key:str(self.aliases[name]) for key,name in gate.ALIASES.items()})

    def typed(self):
        return dict(text='Matrix', checked=True, changed=6, clicked=1, toggled=1, events=20)

    def final(self):
        return dict(ready=True, typed=True, native_close=True, cleanup_verified=True, success=True,
                    invalid=False, expired=False, exception=False, changed=6, clicked=1, toggled=1,
                    close_requests=1, gdk_events=gate.expected_events(12345), changed_at=[1,3,5,7,9,11],
                    toggled_at=15, clicked_at=20)

    def focus(self):
        # A provider without AccessKit may not expose a focused AX text element.
        return dict(legacy_window_error=0, legacy_window_boolean=True, legacy_window_value=False,
                    app_window_error=0, app_window_element=True, app_window_pid_matches=True, app_window_matches=True,
                    field_error=-25212, field_element=False, field_pid_matches=False, field_role_error=-1,
                    field_text_role=False, field_role_category='unavailable', field_window_error=-1,
                    field_window_element=False, field_window_pid_matches=False, field_window_matches=False)

    def window(self):
        # One owned, correctly titled window; AccessKit is absent either way.
        return dict(app_role_error=0, app_role_application=True, attribute_names_error=0, attribute_names_count=12,
                    windows_attribute_present=True, windows_error=0, windows_array=True, windows_count=1,
                    owned_candidates=1, window_role_candidates=1, title_candidates=1, first_role_category='window',
                    focused_window_error=0, focused_window_element=True, focused_window_pid_matches=True,
                    focused_window_title_matches=True)

    def driver(self, phase):
        return dict(identity_verified=True, post_event_access=True, ax_trusted=True, own_window_selected=True,
                    focus_verified=True, timestamp_readback=phase=='keys', events_posted=20 if phase=='keys' else 0,
                    close_action=phase=='close', success=True, focus_observation=self.focus(),
                    window_observation=self.window(), base_seconds=12345)

    def test_private_fresh_disjoint_paths(self):
        self.assertEqual(gate.paths(self.root/'build/evidence',self.base/'work',self.root),
                         (self.root/'build/evidence',self.base/'work'))
        for output, work in ((self.root,self.base/'work'),(self.base/'output',self.root/'work'),
                             (self.root/'evidence',self.base/'work'),(self.base/'out',self.base/'out/work')):
            with self.subTest(output=output),self.assertRaises(ValueError):gate.paths(output,work,self.root)

    def test_symlink_output_or_work_rejected(self):
        link=self.base/'link';link.symlink_to(self.root,target_is_directory=True)
        with self.assertRaises(ValueError):gate.paths(self.base/'out',link,self.root)
        with self.assertRaises(ValueError):gate.paths(link,self.base/'work',self.root)

    def test_regular_input_seal_and_deadline(self):
        path=self.base/'file';path.write_bytes(b'private')
        row=gate.record(path,time.monotonic()+1)
        self.assertEqual(row['bytes'],7);self.assertEqual(row['inode'],path.stat().st_ino)
        self.assertEqual(row,gate.record(path,time.monotonic()+1))
        with self.assertRaises(ValueError):gate.record(path,time.monotonic()-1)

    @unittest.skipUnless(hasattr(os,'mkfifo'),'POSIX FIFO control')
    def test_special_input_cannot_block_sealing(self):
        path=self.base/'fifo';os.mkfifo(path)
        with self.assertRaises(ValueError):gate.record(path,time.monotonic()+1)

    def test_native_scope_phase_duplicate_and_bounds(self):
        raw=b'{"schema":1,"phase":"ready","qualification":false}'
        self.assertEqual(gate.decode(raw,'ready')['phase'],'ready')
        for value in (raw.replace(b'false',b'true'),raw.replace(b'1',b'true'),raw.replace(b'ready',b'keys'),
                      b'{"schema":1,"schema":1,"phase":"ready","qualification":false}',b'x'*8193):
            with self.subTest(raw=value[:100]),self.assertRaises(ValueError):gate.decode(value,'ready')

    def test_ready_requires_target_main_thread_real_gtk_focus(self):
        gate.ready_frame(self.ready(),123,self.aliases)
        for key,value in (('pid',124),('pid',True),('window_number',0),('main_thread',False),
                          ('active',False),('key_window',False),('gtk_entry_focused',False)):
            row=self.ready();row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):gate.ready_frame(row,123,self.aliases)

    def test_application_loop_observation_is_bounded_not_required(self):
        for value in (True, False):
            row=self.ready();row['app_running']=value;gate.ready_frame(row,123,self.aliases)
        for key,value in (('app_running',1),('app_running',None),('activation_policy',True),
                          ('activation_policy',3),('activation_policy',-1)):
            row=self.ready();row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):gate.ready_frame(row,123,self.aliases)

    def test_ready_requires_exact_provider_versions_and_origins(self):
        for key in ('gtk','gobject','glib','gio'):
            row=self.ready();row['origins'][key]+='-different'
            with self.subTest(key=key),self.assertRaises(ValueError):gate.ready_frame(row,123,self.aliases)
        for key in ('gtk_header','gtk_runtime'):
            row=self.ready();row[key]='4.18.5'
            with self.subTest(key=key),self.assertRaises(ValueError):gate.ready_frame(row,123,self.aliases)

    def test_timestamp_origin_rejects_bool_zero_negative_and_overflow(self):
        for value in (True,0,-1,2**32+1,float('inf')):
            row=self.ready();row['base_seconds']=value
            with self.subTest(value=value),self.assertRaises(ValueError):gate.ready_frame(row,123,self.aliases)
            with self.assertRaises(ValueError):gate.expected_events(value)

    def test_real_widget_callbacks_and_final_release_required(self):
        gate.typed_frame(self.typed())
        for key,value in (('text','matrix'),('text','Matrix '),('checked',False),('changed',5),('changed',True),
                          ('toggled',0),('clicked',0),('events',19)):
            row=self.typed();row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):gate.typed_frame(row)

    def test_exact_twenty_event_sequence(self):
        gate.final_frame(self.final(),12345)
        events=gate.expected_events(12345)
        self.assertEqual([e['modifiers'] for e in events],[1,1]+[0]*18)
        self.assertEqual([e['keyval'] for e in events[:4]],[77,77,97,97])
        self.assertEqual([e['keycode'] for e in events[12:]],[48,48,49,49,48,48,49,49])
        # GDK publicly narrows milliseconds to guint32; preserve that exact conversion.
        self.assertEqual(gate.expected_events(2**32)[19]['time_ms'],19000)

    def test_each_event_identity_field_is_enforced(self):
        mutations={'index':7,'keyval':109,'keycode':45,'modifiers':0,'time_ms':12345001,
                   'release':True,'event_type':5,'own_surface':False,'matches':False}
        for key,value in mutations.items():
            row=self.final();row['gdk_events'][0][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):gate.final_frame(row,12345)

    def test_event_type_bool_duplicates_extras_reorder_and_truncation_fail(self):
        originals=[]
        row=self.final();row['gdk_events'][0]['index']=False;originals.append(row)
        row=self.final();row['gdk_events'][0]['private_text']='hidden';originals.append(row)
        row=self.final();row['gdk_events'][1]=copy.deepcopy(row['gdk_events'][0]);originals.append(row)
        row=self.final();row['gdk_events']=list(reversed(row['gdk_events']));originals.append(row)
        row=self.final();row['gdk_events'].pop();originals.append(row)
        for row in originals:
            with self.assertRaises(ValueError):gate.final_frame(row,12345)

    def test_widget_callbacks_bound_to_matching_key_events(self):
        for key,value in (('changed_at',[2,4,6,8,10,12]),('changed_at',[True,3,5,7,9,11]),
                          ('toggled_at',14),('toggled_at',17),('clicked_at',18),('clicked_at',True)):
            row=self.final();row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):gate.final_frame(row,12345)
        for toggle,click in ((15,19),(16,20)):
            row=self.final();row.update(toggled_at=toggle,clicked_at=click);gate.final_frame(row,12345)

    def test_normal_native_close_and_cleanup_are_required_separately(self):
        for key in ('ready','typed','native_close','cleanup_verified','success'):
            row=self.final();row[key]=False
            with self.subTest(key=key),self.assertRaises(ValueError):gate.final_frame(row,12345)
        for key in ('invalid','expired','exception'):
            row=self.final();row[key]=True
            with self.subTest(key=key),self.assertRaises(ValueError):gate.final_frame(row,12345)
        for value in (0,2,True):
            row=self.final();row['close_requests']=value
            with self.assertRaises(ValueError):gate.final_frame(row,12345)

    def test_missing_widget_accessibility_is_observed_not_claimed(self):
        self.assertTrue(gate.focus_proof(self.focus()))
        value=self.focus();value['field_role_category']='other';self.assertTrue(gate.focus_proof(value))
        value=self.focus();value['legacy_window_error']=-25205;value['legacy_window_boolean']=False
        self.assertTrue(gate.focus_proof(value))

    def test_actual_app_focused_window_identity_cannot_be_bypassed(self):
        for key in ('app_window_element','app_window_pid_matches','app_window_matches'):
            row=self.focus();row[key]=False
            with self.subTest(key=key):self.assertFalse(gate.focus_proof(row))
        row=self.focus();row['app_window_error']=-25205;self.assertFalse(gate.focus_proof(row))
        self.assertFalse(gate.focus_proof({}))

    def test_focus_observations_bounded_without_unrelated_text(self):
        for key,value in (('field_role_error',True),('field_error',-99999),('field_text_role',True),
                          ('external_title','not retained')):
            row=self.focus();row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):gate.focus_proof(row)

    def test_directed_driver_requires_permissions_identity_and_real_action(self):
        for phase in ('keys','close'):
            self.assertTrue(gate.driver_frame(self.driver(phase),phase,0,12345))
            for key in ('identity_verified','post_event_access','ax_trusted','own_window_selected','focus_verified'):
                row=self.driver(phase);row[key]=False;row['success']=False
                if key=='focus_verified':row['focus_observation']['app_window_matches']=False
                if key=='own_window_selected':
                    row['window_observation']['title_candidates']=0
                    row['focus_verified']=False;row['focus_observation']={}
                self.assertFalse(gate.driver_frame(row,phase,2,12345))
                with self.assertRaises(ValueError):gate.driver_frame(row,phase,0,12345)

    def test_absent_window_selection_is_diagnosed_not_claimed(self):
        self.assertTrue(gate.window_proof(self.window()))
        self.assertFalse(gate.window_proof({}))
        # Each distinct cause the native diagnostic must now separate.
        hierarchy=dict(self.window(),app_role_error=-25204,app_role_application=False,
                       attribute_names_error=-25204,attribute_names_count=0,windows_attribute_present=False,
                       windows_error=-25204,windows_array=False,windows_count=0,owned_candidates=0,
                       window_role_candidates=0,title_candidates=0,first_role_category='unavailable',
                       focused_window_error=-25204,focused_window_element=False,
                       focused_window_pid_matches=False,focused_window_title_matches=False)
        unsupported=dict(hierarchy,app_role_error=0,app_role_application=True,attribute_names_error=0,
                         attribute_names_count=9)
        empty=dict(unsupported,windows_attribute_present=True,windows_error=0,windows_array=True)
        untitled=dict(empty,windows_count=1,owned_candidates=1,window_role_candidates=1)
        other_role=dict(empty,windows_count=1,owned_candidates=1,first_role_category='other')
        for row in (hierarchy,unsupported,empty,untitled,other_role):
            with self.subTest(row=row['first_role_category']):self.assertFalse(gate.window_proof(row))
        # A focused window alone is observed, never selection.
        self.assertFalse(gate.window_proof(dict(empty,focused_window_error=0,focused_window_element=True,
                                                focused_window_pid_matches=True,focused_window_title_matches=True)))

    def test_ambiguous_or_unowned_windows_are_never_selected(self):
        for row in (dict(self.window(),windows_count=2,owned_candidates=2,window_role_candidates=2,title_candidates=2),
                    dict(self.window(),owned_candidates=0,window_role_candidates=0,title_candidates=0,
                         first_role_category='unavailable')):
            self.assertFalse(gate.window_proof(row))

    def test_window_observations_bounded_and_internally_consistent(self):
        for key,value in (('windows_count',True),('windows_error',-99999),('attribute_names_count',65),
                          ('first_role_category','toplevel'),('observed_title','Ny GTK directed'),
                          ('owned_candidates',-1)):
            row=self.window();row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):gate.window_proof(row)
        for row in (dict(self.window(),title_candidates=2),
                    dict(self.window(),windows_count=0,owned_candidates=0,window_role_candidates=0,
                         title_candidates=0,first_role_category='window'),
                    dict(self.window(),windows_error=-25205),
                    dict(self.window(),windows_array=False,windows_error=0,windows_count=1,
                         owned_candidates=0,window_role_candidates=0,title_candidates=0,
                         first_role_category='unavailable')):
            with self.assertRaises(ValueError):gate.window_proof(row)

    def test_driver_summary_cannot_override_actual_window_selection(self):
        row=self.driver('keys');row['window_observation']['title_candidates']=0
        with self.assertRaises(ValueError):gate.driver_frame(row,'keys',0,12345)
        row=self.driver('keys');del row['window_observation']
        with self.assertRaises(ValueError):gate.driver_frame(row,'keys',0,12345)

    def test_timestamp_readback_and_final_driver_counts(self):
        for phase,key in (('keys','timestamp_readback'),('close','close_action')):
            row=self.driver(phase);row[key]=False;row['success']=False
            self.assertFalse(gate.driver_frame(row,phase,2,12345))
        for key,value in (('events_posted',True),('events_posted',21),('base_seconds',12346)):
            row=self.driver('keys');row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):gate.driver_frame(row,'keys',0,12345)

    def test_driver_summary_cannot_override_actual_focus(self):
        row=self.driver('keys');row['focus_observation']['app_window_matches']=False
        with self.assertRaises(ValueError):gate.driver_frame(row,'keys',0,12345)

    def test_distinct_driver_and_target_identity_are_explicit(self):
        driver=self.base/'driver';target=self.base/'target';driver.write_bytes(b'driver');target.write_bytes(b'target')
        ds=gate.record(driver,time.monotonic()+1);ts=gate.record(target,time.monotonic()+1)
        identity=dict(pid=123,created=(2**63+3)<<32|0xffffffff)
        argv=gate.driver_arguments(driver,target,'keys',identity,456,ds,ts,'a'*32,12345)
        self.assertEqual(argv,list(map(str,[driver,'keys',123,2**63+3,0xffffffff,456,ts['device'],ts['inode'],target,'a'*32,12345])))
        with self.assertRaises(ValueError):gate.driver_arguments(driver,target,'keys',identity,456,ts,ds,'a'*32,12345)

    def test_invalid_pid_lifetime_or_timestamp_rejected_before_launch(self):
        binary=self.base/'probe';binary.write_bytes(b'binary');seal=gate.record(binary,time.monotonic()+1)
        for identity in (dict(pid=True,created=1<<32),dict(pid=0,created=1<<32),dict(pid=123,created=1),
                         dict(pid=123,created=1<<96),dict(pid=123,created=-1)):
            with self.subTest(identity=identity),self.assertRaises(ValueError):
                gate.driver_arguments(binary,binary,'keys',identity,456,seal,seal,'a'*32,12345)
        for base in (0,True,2**32+1):
            with self.assertRaises(ValueError):gate.driver_arguments(binary,binary,'keys',dict(pid=123,created=1<<32),456,seal,seal,'a'*32,base)

    def test_split_frames_retain_raw_and_reject_trailing_output(self):
        raw=b'{"schema":1,"phase":"ready","qualification":false}\n'
        class Reader:
            def __init__(self,parts):self.parts=iter(parts)
            def read(self,_timeout):return next(self.parts)
        sink=io.BytesIO();frames=gate.Frames(Reader((raw[:10],raw[10:],b'trailing\n',b'')),sink)
        self.assertEqual(gate.decode(raw,'ready'),frames.next('ready',time.monotonic()+1))
        with self.assertRaises(ValueError):frames.drain(time.monotonic()+1,reject_new=True)
        self.assertEqual(sink.getvalue(),raw+b'trailing\n');self.assertTrue(frames.eof)

    def test_eof_wrong_phase_and_oversize_fail_without_second_eof_wait(self):
        class Reader:
            def __init__(self,raw):self.raw=raw
            def read(self,_timeout):return self.raw
        for raw in (b'',b'x'*16385,b'{"schema":1,"phase":"typed","qualification":false}\n'):
            frames=gate.Frames(Reader(raw),io.BytesIO())
            with self.assertRaises(ValueError):frames.next('ready',time.monotonic()+1)
            if not raw:frames.drain(time.monotonic()-1)


if __name__=='__main__':unittest.main()
