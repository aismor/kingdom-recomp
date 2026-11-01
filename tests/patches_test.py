"""Version-specific code patch table tests that do not require extracted game data."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'recomp'))
import patches  # noqa: E402


LAYOUT = [('main', 0x7000000000, 0x5000000), ('subsdk0', 0x7006000000, 0x1000000),
          ('sdk', 0x7008000000, 0x1000000)]
COMMON_CHEATS = {'infstamina', 'infhorse', 'infhealth', 'superjump', 'freeshop', 'infarrows', 'infdur', 'infzonai',
                 'infood', 'infoodbuff', 'infbat'}


class VersionCheatsTest(unittest.TestCase):
    def table(self, version):
        return patches.load(ROOT / 'recomp' / f'patches-{version}.txt', LAYOUT)

    def test_common_cheats_exist_in_both_supported_versions(self):
        for version in ('v121', 'v143'):
            names = {name for name, _env, _default in self.table(version).sets}
            self.assertTrue(COMMON_CHEATS <= names, (version, COMMON_CHEATS - names))

    def test_inventory_quantity_cheats_are_version_specific(self):
        names121 = {name for name, _env, _default in self.table('v121').sets}
        names143 = {name for name, _env, _default in self.table('v143').sets}
        self.assertIn('pick8', names121)
        self.assertNotIn('items999', names121)
        self.assertIn('items999', names143)
        self.assertNotIn('pick8', names143)


if __name__ == '__main__':
    unittest.main()
