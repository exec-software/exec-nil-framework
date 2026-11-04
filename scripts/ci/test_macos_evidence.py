"""Regression controls for macOS CI evidence classification and completeness."""
import importlib.util
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location("macos_evidence", Path(__file__).parent / "macos-evidence.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class InventoryTests(unittest.TestCase):
    def registration(self, *names):
        return {"kind": "ctestInfo", "tests": [{"name": name} for name in names]}

    def test_failure_and_skip_are_not_pass(self):
        suite = ET.fromstring('<testsuite><testcase name="ok" status="run"/>'
                              '<testcase name="bad" status="run"><failure>crash</failure></testcase>'
                              '<testcase name="skip" status="notrun"><skipped message="unsupported"/></testcase></testsuite>')
        result = module.summarize(self.registration("ok", "bad", "skip"), suite)
        self.assertEqual(result["counts"], {"passed": 1, "failed": 1, "skipped": 1})
        self.assertEqual(result["cases"][1]["messages"][0]["text"], "crash")
        self.assertEqual(result["cases"][2]["messages"][0]["attributes"], {"message": "unsupported"})

    def test_missing_and_duplicate_remain_visible(self):
        suite = ET.fromstring('<testsuite><testcase name="a" status="run"/><testcase name="a" status="run"/></testsuite>')
        result = module.summarize(self.registration("a", "b"), suite)
        self.assertEqual(result["missing"], ["b"])
        self.assertEqual(result["duplicates"], ["a"])

    def test_ctest_fail_status_is_a_failure(self):
        # CTest 4 on the hosted macOS image uses status="fail"; retain that
        # failure even if a producer omits the optional diagnostic child.
        for child in ("<failure>nonzero exit</failure>", ""):
            with self.subTest(child=child):
                suite = ET.fromstring('<testsuite><testcase name="a" status="fail">' + child + '</testcase></testsuite>')
                inventory = module.summarize(self.registration("a"), suite)
                self.assertEqual(inventory["counts"], {"failed": 1})
                self.assertEqual(module.inventory_errors(inventory), [])

    def test_partial_build_not_run_and_unknown_remain_visible(self):
        suite = ET.fromstring('<testsuite><testcase name="a" status="notrun"/><testcase name="b"/></testsuite>')
        inventory = module.summarize(self.registration("a", "b"), suite)
        self.assertEqual(inventory["counts"], {"not_run": 1, "unknown": 1})
        self.assertIn("CTest has unexecuted cases without an explicit skip or failure", module.inventory_errors(inventory))
        self.assertTrue(any("unknown_statuses" in item for item in module.inventory_errors(inventory)))

    def test_skip_is_present_but_missing_case_fails_collection(self):
        suite = ET.fromstring('<testsuite><testcase name="skip" status="notrun"><skipped/></testcase></testsuite>')
        inventory = module.summarize(self.registration("skip"), suite)
        self.assertEqual(module.inventory_errors(inventory), [])
        inventory = module.summarize(self.registration("skip", "missing"), suite)
        self.assertIn("CTest JUnit inventory is incomplete", module.inventory_errors(inventory))

    def test_duplicate_registration_fails_even_with_unique_junit(self):
        suite = ET.fromstring('<testsuite><testcase name="a" status="run"/></testsuite>')
        inventory = module.summarize(self.registration("a", "a"), suite)
        self.assertEqual(inventory["registration_duplicates"], ["a"])
        self.assertTrue(any("registration_duplicates" in item for item in module.inventory_errors(inventory)))

    def test_unknown_status_cannot_be_hidden_by_skipped_or_failed_child(self):
        for child in ("<skipped/>", "<failure/>"):
            with self.subTest(child=child):
                suite = ET.fromstring('<testsuite><testcase name="a" status="surprise">' + child + '</testcase></testsuite>')
                inventory = module.summarize(self.registration("a"), suite)
                self.assertTrue(any("unknown_statuses" in item for item in module.inventory_errors(inventory)))

    def test_zero_tests_remains_an_error(self):
        inventory = module.summarize(self.registration(), ET.fromstring('<testsuite/>'))
        self.assertIn("CTest registration contains zero tests", module.inventory_errors(inventory))
        self.assertIn("CTest JUnit contains zero tests", module.inventory_errors(inventory))

    def test_unexpected_result_is_reported(self):
        suite = ET.fromstring('<testsuite><testcase name="extra" status="run"/></testsuite>')
        self.assertEqual(module.summarize(self.registration("a"), suite)["unexpected"], ["extra"])

    def test_no_junit_is_no_qualification(self):
        result = module.summarize(self.registration("a"), None)
        self.assertFalse(result["junit_present"])
        self.assertEqual(result["missing"], ["a"])
        self.assertEqual(result["counts"], {})


if __name__ == "__main__":
    unittest.main()
