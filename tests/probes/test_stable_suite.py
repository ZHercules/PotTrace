import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('stable_suite', ROOT/'probes/DirectShowProbe/run_stable_suite.py')

class StableSuiteTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(Path(SPEC.origin).is_file(), 'Stable evidence validator is not implemented')
        self.suite = importlib.util.module_from_spec(SPEC)
        SPEC.loader.exec_module(self.suite)

    def valid(self):
        return dict(suite='smoke', player_sha256='a'*64, expected_player_sha256='a'*64,
                    collector_sha256='b'*64, bridge_sha256='c'*64, config_sha256='d'*64,
                    checks={'online_record':True, 'normal_exit':True}, witness={'uncertainty_ms':40})

    def test_missing_witness_is_failure(self):
        result=self.valid();result.pop('witness')
        self.assertIn('missing witness', self.suite.validate_result(result))

    def test_player_fingerprint_mismatch_is_failure(self):
        result=self.valid();result['player_sha256']='e'*64
        self.assertIn('player fingerprint mismatch', self.suite.validate_result(result))

    def test_failed_attempt_is_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            first=self.suite.create_attempt(Path(tmp), 'smoke');(first/'result.json').write_text('{"passed":false}')
            second=self.suite.create_attempt(Path(tmp), 'smoke')
            self.assertNotEqual(first, second)
            self.assertEqual((first/'result.json').read_text(), '{"passed":false}')

    def test_uncalibrated_or_wide_witness_cannot_pass(self):
        for width in (None, 101, -1):
            result=self.valid();result['witness']['uncertainty_ms']=width
            self.assertIn('witness not calibrated within 100ms', self.suite.validate_result(result))

    def test_good_evidence_is_accepted(self):
        self.assertEqual(self.suite.validate_result(self.valid()), [])

    def test_switch_request_without_observed_new_source_cannot_pass(self):
        row=dict(expected_source='b.mp4',source='a.mp4',duration_ms=61000,expected_duration_ms=61000,
                 health=dict(workers=4,slots=2,failures=0,rejected=0,lost=0,enabled=True))
        self.assertIn('source did not switch',self.suite.validate_switch(row))
        row['source']='b.mp4';row['health']['slots']=0
        self.assertIn('collector inactive or unhealthy',self.suite.validate_switch(row))

if __name__ == '__main__': unittest.main()
