#!/usr/bin/env python3
"""Stretch the native RX's 800x480 picture using the fbcon DRM primary plane."""
import ctypes
import fcntl
import glob
import os
import signal
import sys
import time

U32 = ctypes.c_uint32
U64 = ctypes.c_uint64
S32 = ctypes.c_int32


class ClientCap(ctypes.Structure):
    _fields_ = [("capability", U64), ("value", U64)]


class PlaneResources(ctypes.Structure):
    _fields_ = [("plane_id_ptr", U64), ("count_planes", U32)]


class GetPlane(ctypes.Structure):
    _fields_ = [("plane_id", U32), ("crtc_id", U32), ("fb_id", U32),
                ("possible_crtcs", U32), ("gamma_size", U32),
                ("count_format_types", U32), ("format_type_ptr", U64)]


class ObjectProperties(ctypes.Structure):
    _fields_ = [("props_ptr", U64), ("prop_values_ptr", U64),
                ("count_props", U32), ("obj_id", U32), ("obj_type", U32)]


class Property(ctypes.Structure):
    _fields_ = [("values_ptr", U64), ("enum_blob_ptr", U64), ("prop_id", U32),
                ("flags", U32), ("name", ctypes.c_char * 32),
                ("count_values", U32), ("count_enum_blobs", U32)]


class SetPlane(ctypes.Structure):
    _fields_ = [("plane_id", U32), ("crtc_id", U32), ("fb_id", U32),
                ("flags", U32), ("crtc_x", S32), ("crtc_y", S32),
                ("crtc_w", U32), ("crtc_h", U32), ("src_x", U32),
                ("src_y", U32), ("src_h", U32), ("src_w", U32)]


def iowr(number, typ):
    return (3 << 30) | (ctypes.sizeof(typ) << 16) | (ord("d") << 8) | number


SET_CLIENT_CAP = (1 << 30) | (ctypes.sizeof(ClientCap) << 16) | (ord("d") << 8) | 0x0d
SET_MASTER = (ord("d") << 8) | 0x1e
GET_PLANES = iowr(0xb5, PlaneResources)
GET_PLANE = iowr(0xb6, GetPlane)
GET_PROPERTY = iowr(0xaa, Property)
GET_OBJECT_PROPERTIES = iowr(0xb9, ObjectProperties)
SET_PLANE = iowr(0xb7, SetPlane)
PLANE_OBJECT = 0xeeeeeeee
SRC_KEYS = ("SRC_X", "SRC_Y", "SRC_W", "SRC_H")
DST_KEYS = ("CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H")


def call(fd, request, value):
    fcntl.ioctl(fd, request, value)
    return value


def card_for_fb():
    fb = "/sys/class/graphics/fb0"
    for name, expected in (("virtual_size", "1280,800"),
                           ("bits_per_pixel", "16"), ("stride", "2560")):
        with open(os.path.join(fb, name), encoding="ascii") as f:
            if f.read().strip() != expected:
                raise RuntimeError("fb0 is not the supported 1280x800 RGB565 framebuffer")
    device = os.path.realpath(os.path.join(fb, "device"))
    cards = [p for p in glob.glob("/sys/class/drm/card[0-9]*")
             if os.path.basename(p)[4:].isdigit()
             and os.path.realpath(os.path.join(p, "device")) == device]
    if len(cards) != 1:
        raise RuntimeError("could not uniquely identify the DRM card for fb0")
    return "/dev/dri/" + os.path.basename(cards[0])


def plane_properties(fd, plane_id):
    obj = ObjectProperties(obj_id=plane_id, obj_type=PLANE_OBJECT)
    call(fd, GET_OBJECT_PROPERTIES, obj)
    count = obj.count_props
    ids, values = (U32 * count)(), (U64 * count)()
    obj.props_ptr = ctypes.addressof(ids)
    obj.prop_values_ptr = ctypes.addressof(values)
    call(fd, GET_OBJECT_PROPERTIES, obj)
    if obj.count_props > count:
        raise RuntimeError("DRM plane property list changed")
    result = {}
    for i in range(obj.count_props):
        prop = call(fd, GET_PROPERTY, Property(prop_id=ids[i]))
        result[prop.name.decode("ascii")] = values[i]
    return result


def active_primary(fd):
    res = call(fd, GET_PLANES, PlaneResources())
    count = res.count_planes
    ids = (U32 * count)()
    res.plane_id_ptr = ctypes.addressof(ids)
    call(fd, GET_PLANES, res)
    if res.count_planes > count:
        raise RuntimeError("DRM plane list changed")
    active = []
    for plane_id in ids[:res.count_planes]:
        plane = call(fd, GET_PLANE, GetPlane(plane_id=plane_id))
        if not plane.crtc_id or not plane.fb_id:
            continue
        props = plane_properties(fd, plane_id)
        if props.get("type") == 1:
            active.append((plane, props))
    if len(active) != 1:
        raise RuntimeError("expected exactly one active primary plane on fb0's DRM card")
    return active[0]


def plane_request(plane, props, source):
    return SetPlane(plane_id=plane.plane_id, crtc_id=plane.crtc_id,
                    fb_id=plane.fb_id, crtc_x=props["CRTC_X"],
                    crtc_y=props["CRTC_Y"], crtc_w=props["CRTC_W"],
                    crtc_h=props["CRTC_H"], src_x=source["SRC_X"],
                    src_y=source["SRC_Y"], src_w=source["SRC_W"],
                    src_h=source["SRC_H"])


def rect(props, keys):
    return {key: props[key] for key in keys}


def validate_fullscreen(props):
    expected = {"CRTC_X": 0, "CRTC_Y": 0, "CRTC_W": 1280, "CRTC_H": 800,
                "SRC_X": 0, "SRC_Y": 0, "SRC_W": 1280 << 16, "SRC_H": 800 << 16}
    if any(props.get(key) != value for key, value in expected.items()):
        raise RuntimeError("expected an unscaled 1280x800 primary plane")


def is_current(plane, current, props, source, destination):
    return (current.plane_id == plane.plane_id and current.crtc_id == plane.crtc_id and
            current.fb_id == plane.fb_id and rect(props, SRC_KEYS) == source and
            rect(props, DST_KEYS) == destination)


def run(fd, card, ready):
    plane, props = active_primary(fd)
    validate_fullscreen(props)
    source = {"SRC_X": 0, "SRC_Y": 0, "SRC_W": 800 << 16, "SRC_H": 480 << 16}
    original = rect(props, SRC_KEYS)
    destination = rect(props, DST_KEYS)
    stopped = False
    ready_created = False

    def stop(_signum, _frame):
        nonlocal stopped
        stopped = True

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    call(fd, SET_PLANE, plane_request(plane, props, source))
    try:
        with open(ready, "x", encoding="utf-8") as f:
            ready_created = True
            f.write("%d\n" % os.getpid())
        print("rx-display: stretched 800x480 to 1280x800 on %s plane %d" %
              (card, plane.plane_id), flush=True)
        while not stopped:
            time.sleep(1)
            if not stopped:
                current, current_props = active_primary(fd)
                if not is_current(plane, current, current_props, source, destination):
                    raise RuntimeError("plane changed while RX stretch was active")
    finally:
        try:
            current, current_props = active_primary(fd)
            if not is_current(plane, current, current_props, source, destination):
                raise RuntimeError("plane changed since stretch; refusing to restore it")
            call(fd, SET_PLANE, plane_request(plane, current_props, original))
            print("rx-display: restored primary plane", flush=True)
        finally:
            if ready_created:
                os.unlink(ready)


def main():
    if len(sys.argv) != 3 or sys.argv[1] != "run":
        print("usage: rx-display-stretch.py run READY_FILE", file=sys.stderr)
        return 2
    card = card_for_fb()
    fd = os.open(card, os.O_RDWR | os.O_CLOEXEC)
    try:
        call(fd, SET_CLIENT_CAP, ClientCap(2, 1))
        call(fd, SET_CLIENT_CAP, ClientCap(3, 1))
        fcntl.ioctl(fd, SET_MASTER)
        run(fd, card, sys.argv[2])
    finally:
        os.close(fd)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, KeyError, ValueError) as exc:
        print("rx-display: ERROR: %s" % exc, file=sys.stderr)
        sys.exit(1)
