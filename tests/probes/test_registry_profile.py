import sys
import unittest
from pathlib import Path

if sys.platform != 'win32':
    raise unittest.SkipTest('Requires Windows profile API')

import winreg

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'probes/DirectShowProbe'))
from registry_profile import trial


class RegistryProfileTrialTests(unittest.TestCase):
    def test_native_roundtrip_tracks_unverified_values_and_removes_ini(self):
        values = {
            ('Settings', 'VideoRen2'): (winreg.REG_DWORD, 15),
            ('ExtensionSection', 'Name'): (winreg.REG_SZ, '插件=示例'),
            ('ExtensionSection', 'Empty'): (winreg.REG_SZ, ''),
            ('Settings', 'PrevSpeed'): (winreg.REG_BINARY, bytes(8)),
            ('', 'Version'): (winreg.REG_DWORD, 1),
            ('Settings', 'MultiLine'): (winreg.REG_SZ, 'a\nb'),
        }
        self.assertEqual(trial(values), {
            'fields': {'win32_equal': 3, 'root_or_unnamed_unverified': 1,
                       'ambiguous_ini_syntax': 1, 'binary_or_other_type_unverified': 1},
            'temporary_ini_removed': True,
        })

    def test_win32_strips_outer_quotes(self):
        result = trial({('Settings', 'String'): (winreg.REG_SZ, '"quoted"')})
        self.assertEqual(result['fields'], {'win32_different': 1})
