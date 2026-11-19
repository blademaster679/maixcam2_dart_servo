import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import types
import unittest
from unittest import mock

REPO = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


runner = load("camera_runner", REPO / "tools/run_camera_v11.py")
capture = load("camera_capture", REPO / "tools/camera_v11_capture.py")


class FakeMap(bytearray):
    def close(self):
        self.closed = True


class CameraRunnerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.registers = FakeMap(4096)
        self.original = {"A8": 0x600a3, "A9": 0x300c3}
        for pin, offset in runner.PAD_OFFSETS.items():
            struct.pack_into("<I", self.registers, offset, self.original[pin])
        self.addCleanup(mock.patch.stopall)
        mock.patch.object(runner.os, "open", return_value=123).start()
        mock.patch.object(runner.os, "close").start()
        mock.patch.object(runner.mmap, "mmap", return_value=self.registers).start()

    def test_full_pad_values_restored_after_multimedia_reset(self):
        pads = runner.CameraPads()
        with pads:
            for offset in runner.PAD_OFFSETS.values():
                struct.pack_into("<I", self.registers, offset, 0x83)
        self.assertTrue(pads.restored)
        self.assertEqual(pads.snapshot(), self.original)
        self.assertTrue(self.registers.closed)

    def test_partial_change_and_exception_restore_both_pads(self):
        pads = runner.CameraPads()
        with self.assertRaisesRegex(ValueError, "initialization failed"):
            with pads:
                struct.pack_into("<I", self.registers, runner.PAD_OFFSETS["A8"], 0x30083)
                raise ValueError("initialization failed")
        self.assertTrue(pads.restored)
        self.assertEqual(pads.snapshot(), self.original)

    def test_failed_child_records_failure_after_restoring_pads(self):
        (self.base / "camera_v11_i2c.so").write_bytes(b"test fixture")
        (self.base / "camera_v11_capture.py").write_text("# test fixture\n")
        child = mock.Mock()
        child.wait.return_value = 1
        child.poll.return_value = 1
        with mock.patch.object(runner, "BASE", self.base), \
             mock.patch.object(runner, "LOCKS", ()), \
             mock.patch.object(runner, "check_board"), \
             mock.patch.object(runner.os, "geteuid", return_value=0), \
             mock.patch.object(runner.platform, "machine", return_value="aarch64"), \
             mock.patch.object(runner.subprocess, "Popen", return_value=child):
            output = self.base / "run"
            with self.assertRaisesRegex(RuntimeError, "Camera capture failed"):
                runner.run(output)
            result = json.loads((output / "result.json").read_text())
        self.assertEqual(result["status"], "FAIL")
        self.assertEqual(result["exit_code"], 1)
        self.assertTrue(result["pins_restored"])
        child.kill.assert_not_called()


class CaptureTests(unittest.TestCase):
    def test_empty_first_frame_closes_camera(self):
        cam = mock.Mock()
        cam.read.return_value = None
        maix = types.SimpleNamespace(
            camera=types.SimpleNamespace(Camera=lambda *args, **kwargs: cam),
            image=types.SimpleNamespace(Format=types.SimpleNamespace(FMT_RGB888=1)))
        with tempfile.TemporaryDirectory() as folder, mock.patch.dict(sys.modules, {"maix": maix}):
            with self.assertRaisesRegex(RuntimeError, "Empty image at frame 0"):
                capture.capture(Path(folder))
            self.assertFalse((Path(folder) / "capture.json").exists())
        cam.close.assert_called_once()


if __name__ == "__main__":
    unittest.main()
