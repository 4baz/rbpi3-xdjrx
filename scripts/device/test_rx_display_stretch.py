#!/usr/bin/env python3
"""Host tests for the RX primary-plane stretch helper."""
import importlib.util
import os
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    "rx_display_stretch", os.path.join(HERE, "rx-display-stretch.py"))
display = importlib.util.module_from_spec(spec)
spec.loader.exec_module(display)


def full_props():
    return {"type": 1, "CRTC_X": 0, "CRTC_Y": 0, "CRTC_W": 1280,
            "CRTC_H": 800, "SRC_X": 0, "SRC_Y": 0,
            "SRC_W": 1280 << 16, "SRC_H": 800 << 16}


class DisplayTests(unittest.TestCase):
    def test_uapi_and_rectangles(self):
        self.assertEqual(display.ctypes.sizeof(display.SetPlane), 48)
        self.assertEqual(display.SET_PLANE, 0xc03064b7)
        p = display.GetPlane(plane_id=86, crtc_id=97, fb_id=671)
        props = full_props()
        display.validate_fullscreen(props)
        source = {"SRC_X": 0, "SRC_Y": 0, "SRC_W": 800 << 16, "SRC_H": 480 << 16}
        request = display.plane_request(p, props, source)
        self.assertEqual((request.plane_id, request.crtc_id, request.fb_id), (86, 97, 671))
        self.assertEqual((request.crtc_w, request.crtc_h), (1280, 800))
        self.assertEqual((request.src_w, request.src_h), (800 << 16, 480 << 16))
        props["SRC_W"] = 800 << 16
        with self.assertRaises(RuntimeError):
            display.validate_fullscreen(props)

    def test_run_restores_original_and_removes_ready_file(self):
        plane = display.GetPlane(plane_id=86, crtc_id=97, fb_id=671)
        before = full_props()
        after = dict(before, SRC_W=800 << 16, SRC_H=480 << 16)
        handlers, requests = {}, []

        def remember_signal(number, handler):
            handlers[number] = handler

        def stop(_seconds):
            handlers[display.signal.SIGTERM](display.signal.SIGTERM, None)

        def ioctl(_fd, request, value):
            self.assertEqual(request, display.SET_PLANE)
            requests.append((value.src_w, value.src_h))
            return value

        with tempfile.TemporaryDirectory() as directory:
            ready = os.path.join(directory, "ready")
            with mock.patch.object(display, "active_primary", side_effect=[(plane, before), (plane, after)]), \
                 mock.patch.object(display, "call", side_effect=ioctl), \
                 mock.patch.object(display.signal, "signal", side_effect=remember_signal), \
                 mock.patch.object(display.time, "sleep", side_effect=stop):
                display.run(1, "/dev/dri/card0", ready)
            self.assertFalse(os.path.exists(ready))
        self.assertEqual(requests, [(800 << 16, 480 << 16), (1280 << 16, 800 << 16)])

    def test_does_not_restore_an_unrelated_plane(self):
        plane = display.GetPlane(plane_id=86, crtc_id=97, fb_id=671)
        changed = display.GetPlane(plane_id=86, crtc_id=97, fb_id=672)
        handlers, requests = {}, []
        with tempfile.TemporaryDirectory() as directory:
            ready = os.path.join(directory, "ready")
            with mock.patch.object(display, "active_primary", side_effect=[(plane, full_props()), (changed, full_props())]), \
                 mock.patch.object(display, "call", side_effect=lambda fd, request, value: requests.append(value)), \
                 mock.patch.object(display.signal, "signal", side_effect=lambda n, h: handlers.update({n: h})), \
                 mock.patch.object(display.time, "sleep", side_effect=lambda _: handlers[display.signal.SIGTERM](0, None)):
                with self.assertRaisesRegex(RuntimeError, "plane changed"):
                    display.run(1, "/dev/dri/card0", ready)
            self.assertEqual(len(requests), 1)

    def test_existing_ready_file_is_not_removed(self):
        plane = display.GetPlane(plane_id=86, crtc_id=97, fb_id=671)
        before = full_props()
        after = dict(before, SRC_W=800 << 16, SRC_H=480 << 16)
        with tempfile.TemporaryDirectory() as directory:
            ready = os.path.join(directory, "ready")
            with open(ready, "w", encoding="utf-8") as f:
                f.write("owned by someone else")
            with mock.patch.object(display, "active_primary", side_effect=[(plane, before), (plane, after)]), \
                 mock.patch.object(display, "call"), mock.patch.object(display.signal, "signal"):
                with self.assertRaises(FileExistsError):
                    display.run(1, "/dev/dri/card0", ready)
            with open(ready, encoding="utf-8") as f:
                self.assertEqual(f.read(), "owned by someone else")


if __name__ == "__main__":
    unittest.main()
