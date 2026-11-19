import copy
import hashlib
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

repo = Path(sys.argv.pop(1)).resolve()
spec = importlib.util.spec_from_file_location("scope_runner", repo / "tools/run_servo_scope.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class ScopeRunnerTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.chip = self.root / "pwmchip0"
        self.chip.mkdir()
        self.node = self.chip / "pwm3"
        self.saved = {"period": "100000", "duty_cycle": "47500", "polarity": "normal", "enable": "1"}
        self.create_node(self.saved)
        self.state = {"servo_controls": [16] * 4, "servo_pads": [3] * 4,
                      "pwmchips": {"pwmchip0": "6060000.pwm0"}, "bindings": {"i2c": "driver"}}
        self.events = []

    def create_node(self, values):
        self.node.mkdir(exist_ok=True)
        for name, value in values.items():
            (self.node / name).write_text(value)

    def write(self, path, value):
        self.events.append((path.name, str(value)))
        if path.name == "export":
            self.assertFalse(self.node.exists())
            self.create_node({"period": "0", "duty_cycle": "0", "polarity": "normal", "enable": "0"})
        elif path.name == "unexport":
            shutil.rmtree(self.node)
        else:
            if path.name in ("enable", "duty_cycle") and (self.node / "period").read_text() == "0":
                raise OSError("EINVAL: period must be initialized first")
            path.write_text(str(value))

    def patches(self, pause=None, after=None):
        if pause is None:
            pause = lambda paused: paused.append(123)
        return (
            mock.patch.object(runner, "chip_path", return_value=self.chip),
            mock.patch.object(runner, "write_value", side_effect=self.write),
            mock.patch.object(runner, "pause_display", side_effect=pause),
            mock.patch.object(runner, "resume_display"),
            mock.patch.object(runner, "snapshot", side_effect=[copy.deepcopy(self.state),
                                                               copy.deepcopy(after or self.state)]),
        )

    def test_normal_and_exception_paths_restore_display(self):
        for fail in (False, True):
            with self.subTest(fail=fail):
                a, b, c, d, e = self.patches()
                with a, b, c, d as resume, e:
                    if fail:
                        with self.assertRaisesRegex(ValueError, "simulated"):
                            with runner.DisplayLease(self.root):
                                self.assertFalse(self.node.exists())
                                raise ValueError("simulated")
                    else:
                        with runner.DisplayLease(self.root):
                            self.assertFalse(self.node.exists())
                    self.assertEqual(runner.read_backlight(self.node), self.saved)
                    resume.assert_called_once_with([123])

    def test_launcher_unexports_backlight_during_pause(self):
        def pause(paused):
            paused.append(123)
            shutil.rmtree(self.node)
        a, b, c, d, e = self.patches(pause=pause)
        with a, b, c, d as resume, e:
            with runner.DisplayLease(self.root):
                self.assertFalse(self.node.exists())
            self.assertEqual(runner.read_backlight(self.node), self.saved)
            # The new period must precede any attempt to disable a fresh PWM.
            self.assertEqual(self.events[:2], [("export", "3"), ("period", "100000")])
            resume.assert_called_once_with([123])

    def test_restore_validation_failure_still_resumes_ui(self):
        after = copy.deepcopy(self.state)
        after["servo_pads"][2] = 99
        a, b, c, d, e = self.patches(after=after)
        with a, b, c, d as resume, e:
            with self.assertRaisesRegex(RuntimeError, "servo_pads"):
                with runner.DisplayLease(self.root):
                    pass
            self.assertEqual(runner.read_backlight(self.node), self.saved)
            resume.assert_called_once_with([123])

    def test_busy_servo_refused_before_pausing_display(self):
        self.state["servo_controls"][2] |= 1
        a, b, c, d, e = self.patches()
        with a, b, c as pause, d, e:
            with self.assertRaisesRegex(RuntimeError, "already active"):
                with runner.DisplayLease(self.root):
                    self.fail("must not enter")
            pause.assert_not_called()

    def test_checksum_and_cli_do_not_start_hardware(self):
        entries = []
        for name in sorted(runner.FILES):
            value = name.encode()
            (self.root / name).write_bytes(value)
            entries.append(hashlib.sha256(value).hexdigest() + "  " + name)
        (self.root / "SHA256SUMS").write_text("\n".join(entries))
        runner.verify_files(self.root)
        (self.root / "e907_servo.bin").write_bytes(b"changed")
        with self.assertRaisesRegex(RuntimeError, "Checksum mismatch"):
            runner.verify_files(self.root)
        listed = subprocess.run([sys.executable, str(repo / "tools/run_servo_scope.py"), "--list"],
                                capture_output=True, text=True)
        self.assertEqual(listed.returncode, 0)
        self.assertEqual(len(listed.stdout.splitlines()), 3)
        bad = subprocess.run([sys.executable, str(repo / "tools/run_servo_scope.py"), "4"],
                             capture_output=True, text=True)
        self.assertEqual(bad.returncode, 2)
        self.assertEqual(runner.case_command(1, self.root)[2], "3")
        self.assertEqual(runner.case_command(2, self.root)[-1], "333")
        self.assertEqual(runner.case_command(3, self.root)[-1], "50")


if __name__ == "__main__":
    unittest.main()
