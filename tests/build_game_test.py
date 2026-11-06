"""Platform-specific build_game flags that must remain valid in both release installers.

Run: python tests/build_game_test.py
"""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import build_game  # noqa: E402


class LinkFlagsTest(unittest.TestCase):
    def test_windows_uses_only_coff_linker_options(self):
        flags = build_game.link_flags(True)
        self.assertIn('-Wl,--stack,16777216', flags)
        self.assertNotIn('-Wl,--call-graph-profile-sort=none', flags)

    def test_linux_disables_elf_call_graph_sorting(self):
        flags = build_game.link_flags(False)
        self.assertNotIn('-Wl,--stack,16777216', flags)
        self.assertIn('-Wl,--call-graph-profile-sort=none', flags)


if __name__ == '__main__':
    unittest.main()
