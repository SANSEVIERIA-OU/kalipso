# SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
import tempfile
from pathlib import Path
import unittest

from check_license import check


class LicensePolicyTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        canonical = Path(__file__).resolve().parents[1] / "LICENSE"
        (self.root / "LICENSE").write_bytes(canonical.read_bytes())
        (self.root / "NOTICE").write_text("SANSEVIERIA License 1.0\nhttps://github.com/SANSEVIERIA-OU/example\n")

    def test_valid(self):
        self.assertEqual(check(self.root, "SANSEVIERIA-OU/example"), [])

    def test_missing_licence(self):
        (self.root / "LICENSE").unlink()
        self.assertTrue(check(self.root))

    def test_modified_terms(self):
        with (self.root / "LICENSE").open("a") as stream:
            stream.write("\nCommercial use is unrestricted.\n")
        self.assertTrue(check(self.root))

    def test_missing_notice(self):
        (self.root / "NOTICE").unlink()
        self.assertTrue(check(self.root))

    def test_other_repository(self):
        self.assertTrue(check(self.root, "SANSEVIERIA-OU/another-project"))

    def test_prefix_is_not_repository(self):
        (self.root / "NOTICE").write_text("SANSEVIERIA License 1.0\nhttps://github.com/SANSEVIERIA-OU/example-other\n")
        self.assertTrue(check(self.root, "SANSEVIERIA-OU/example"))


if __name__ == "__main__":
    unittest.main()
