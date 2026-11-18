import copy
import sys
import subprocess
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "probes/DirectShowProbe"))
from player_startup import PlayerStartupError, wait_for_player_window


class Clock:
    def __init__(self): self.at = 1000
    def now(self): return self.at
    def sleep(self, seconds): self.at += round(seconds * 1000)


class Process:
    pid = 123
    def __init__(self, clock, exit_at=None, code=0):
        self.clock, self.exit_at, self.code = clock, exit_at, code
    def poll(self):
        return self.code if self.exit_at is not None and self.clock.at >= self.exit_at else None


class PlayerStartupTests(unittest.TestCase):
    def run_startup(self, process, snapshot, clock, evidence, checkpoints):
        return wait_for_player_window(process, snapshot, evidence,
            lambda row: checkpoints.append(copy.deepcopy(row)),
            now=clock.now, sleep=clock.sleep, timeout_ms=100)

    def test_clean_exit_before_window_is_failure_and_preserves_last_snapshot(self):
        clock, evidence, checkpoints = Clock(), {}, []
        process = Process(clock, 1030, 0)
        def snapshot():
            return {"windows": [{"pid": 123, "hwnd": 9, "cls": "#32770"}],
                    "modules": ["PotPlayerMini64.exe"], "children": []}
        with self.assertRaises(PlayerStartupError):
            self.run_startup(process, snapshot, clock, evidence, checkpoints)
        self.assertEqual("exited_before_window", evidence["outcome"])
        self.assertEqual(0, evidence["exit_code"])
        self.assertEqual("0x00000000", evidence["exit_code_hex"])
        self.assertEqual("#32770", evidence["snapshots"][-1]["windows"][0]["cls"])
        self.assertEqual("exited_before_window", checkpoints[-1]["outcome"])
        self.assertNotIn("cleanup_exit_code", evidence)

    def test_exception_exit_retains_unsigned_windows_code(self):
        clock, evidence, checkpoints = Clock(), {}, []
        with self.assertRaises(PlayerStartupError):
            self.run_startup(Process(clock, 1000, -1073741819), lambda: {}, clock, evidence, checkpoints)
        self.assertEqual("0xc0000005", evidence["exit_code_hex"])
        self.assertEqual(-1073741819, evidence["exit_code"])

    def test_exit_during_window_enumeration_never_accepts_stale_window(self):
        clock, evidence, checkpoints = Clock(), {}, []
        def snapshot():
            clock.at = 1040
            return {"windows": [{"pid": 123, "hwnd": 9, "cls": "PotPlayer64"}]}
        with self.assertRaises(PlayerStartupError):
            self.run_startup(Process(clock, 1030, 0), snapshot, clock, evidence, checkpoints)
        self.assertEqual("exited_before_window", evidence["outcome"])

    def test_exit_at_deadline_is_distinguished_from_live_timeout(self):
        clock, evidence, checkpoints = Clock(), {}, []
        with self.assertRaises(PlayerStartupError):
            self.run_startup(Process(clock, 1100, 23), lambda: {"windows": []}, clock, evidence, checkpoints)
        self.assertEqual("exited_before_window", evidence["outcome"])
        self.assertEqual(23, evidence["exit_code"])

    def test_other_pid_window_is_never_adopted(self):
        clock, evidence, checkpoints = Clock(), {}, []
        with self.assertRaises(PlayerStartupError):
            self.run_startup(Process(clock), lambda: {"windows": [{"pid": 456, "hwnd": 9, "cls": "PotPlayer64"}]}, clock, evidence, checkpoints)
        self.assertEqual("window_timeout", evidence["outcome"])
        self.assertIsNone(evidence["exit_code"])

    def test_success_returns_only_live_owned_window(self):
        clock, evidence, checkpoints = Clock(), {}, []
        hwnd = self.run_startup(Process(clock), lambda: {"windows": [{"pid": 123, "hwnd": 9, "cls": "PotPlayer64"}]}, clock, evidence, checkpoints)
        self.assertEqual(9, hwnd)
        self.assertEqual("window_ready", evidence["outcome"])
        self.assertEqual("window_ready", checkpoints[-1]["outcome"])

    def test_real_early_process_exits_are_not_successful_launches(self):
        for code in [0, 23]:
            with self.subTest(code=code):
                process = subprocess.Popen([sys.executable, "-c", f"raise SystemExit({code})"])
                evidence, checkpoints = {}, []
                try:
                    with self.assertRaises(PlayerStartupError):
                        wait_for_player_window(process, lambda: {"windows": []}, evidence,
                            lambda row: checkpoints.append(copy.deepcopy(row)), timeout_ms=3000)
                    self.assertEqual(code, evidence["exit_code"])
                    self.assertEqual("exited_before_window", checkpoints[-1]["outcome"])
                finally:
                    if process.poll() is None: process.terminate()
                    process.wait(timeout=3)

    def test_inspection_error_is_recorded_and_can_recover(self):
        clock, evidence, checkpoints = Clock(), {}, []
        def snapshot():
            if clock.at == 1000: raise PermissionError("inspection unavailable")
            return {"windows": [{"pid": 123, "hwnd": 9, "cls": "PotPlayer64"}]}
        self.assertEqual(9, self.run_startup(Process(clock), snapshot, clock, evidence, checkpoints))
        self.assertEqual("PermissionError", evidence["snapshots"][0]["inspection_error"]["type"])


if __name__ == "__main__": unittest.main()
