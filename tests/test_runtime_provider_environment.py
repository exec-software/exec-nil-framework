"""Provider loader declarations affect execution, never compile/link children."""
from functools import partial
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

from runtime_provider_environment import DECLARATION, LOADER_VARIABLE, provider_environments

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('provider_route_matrix', ROOT / 'tests/routes/ny_route_matrix.py')
routes = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = routes
SPEC.loader.exec_module(routes)


class ProviderEnvironmentTests(unittest.TestCase):
    def test_ambient_overrides_are_unchanged_without_explicit_declaration(self):
        original = {'PATH': '/tools', LOADER_VARIABLE: '/ambient', 'DYLD_LIBRARY_PATH': '/other'}
        for host in ('darwin', 'linux', 'win32'):
            build, runtime, declared = provider_environments(original, platform_name=host)
            self.assertEqual(build, original)
            self.assertEqual(runtime, original)
            self.assertIsNone(declared)
            self.assertIsNot(build, runtime)

    def test_declared_path_is_runtime_only_and_preserves_order(self):
        value = '/private/provider with space/lib:/usr/lib'
        original = {'PATH': '/tools', DECLARATION: value, 'NIL_STDLIB_PATH': '/sdk/nilstd'}
        build, runtime, declared = provider_environments(original, platform_name='darwin')
        self.assertEqual(declared, value)
        self.assertNotIn(LOADER_VARIABLE, build)
        self.assertNotIn(DECLARATION, build)
        self.assertEqual(runtime[LOADER_VARIABLE], value)
        self.assertEqual(runtime['NIL_STDLIB_PATH'], '/sdk/nilstd')
        self.assertEqual(original[DECLARATION], value)

    def test_conflicts_and_ambiguous_declarations_fail_before_execution(self):
        for invalid in ('', '/lib:', 'relative:/usr/lib', '/lib\nother', '/lib\0other'):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                provider_environments({DECLARATION: invalid}, platform_name='darwin')
        for name in (LOADER_VARIABLE, 'DYLD_INSERT_LIBRARIES', 'DYLD_LIBRARY_PATH'):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'conflicts'):
                provider_environments({DECLARATION: '/provider:/usr/lib', name: '/ambient'}, platform_name='darwin')
        with self.assertRaisesRegex(ValueError, 'macOS'):
            provider_environments({DECLARATION: '/provider:/usr/lib'}, platform_name='linux')

    def test_each_route_assigns_loader_path_to_actual_execution_stage(self):
        for route in routes.EXECUTION_ROUTES:
            with self.subTest(route=route), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                seen = []
                declared = '/private/qualified/lib:/usr/lib'

                def command(argv, cwd, timeout, env):
                    seen.append((list(argv), dict(env)))
                    return routes.CommandResult(list(map(str, argv)), str(cwd), 0, '', '', 0.0)

                def artifact(*unused):
                    return {'valid': True, 'errors': [], 'metadata': {}}

                with patch.object(routes, 'provider_environments', partial(provider_environments, platform_name='darwin')), \
                     patch.object(routes, 'run_command', side_effect=command), \
                     patch.object(routes, 'validate_text_nil', side_effect=artifact), \
                     patch.object(routes, 'validate_binary_nil', side_effect=artifact), \
                     patch.object(routes, 'validate_bundle', side_effect=artifact), \
                     patch.object(routes, 'validate_native_executable', side_effect=artifact):
                    result = routes.run_route(route, root / route, Path('/compiler'), Path('/interpreter'),
                        Path('/fixture.ny'), '', '', 0, 5, [], [], [], True,
                        {'PATH': '/tools', DECLARATION: declared}, 1024)
                self.assertEqual(result['status'], 'PASS', result)
                if route == 'llvm-jit':
                    self.assertEqual(len(seen), 1)
                    self.assertEqual(seen[0][1][LOADER_VARIABLE], declared)
                    self.assertEqual(result['commands']['compile_and_run']['runtime_loader']['role'],
                                     'combined JIT compilation/execution')
                else:
                    self.assertEqual(len(seen), 2)
                    self.assertNotIn(LOADER_VARIABLE, seen[0][1])
                    self.assertEqual(seen[1][1][LOADER_VARIABLE], declared)
                    self.assertIsNone(result['commands']['compile']['runtime_loader']['dyld_fallback_library_path'])
                    self.assertEqual(result['commands']['run']['runtime_loader']['dyld_fallback_library_path'], declared)
                self.assertTrue(all(DECLARATION not in env for _, env in seen))


if __name__ == '__main__':
    unittest.main()
