#!/usr/bin/env python3
"""QMP mouse injection helper for KintsugiOS headless GUI tests.

Uses input-send-event types 'rel' and 'btn'. This requires a QEMU input
console; launch QEMU with '-vnc :0' (see Makefile run_gui). Absolute event
types ('abs', 'mouse', 'axis') are not supported by this QEMU build.
"""
import json
import socket
import time


class QmpMouse:
    def __init__(self, socket_path, timeout=15):
        self.sock = socket.socket(socket.AF_UNIX)
        for _ in range(400):
            try:
                self.sock.connect(socket_path)
                break
            except (FileNotFoundError, ConnectionRefusedError):
                time.sleep(0.05)
        self.sock.settimeout(timeout)
        self.stream = self.sock.makefile("rwb", buffering=0)
        json.loads(self.stream.readline())
        self.qmp("qmp_capabilities")
        # The guest PS/2 driver initialises the pointer at screen centre.
        self.x, self.y = 640, 400

    def qmp(self, execute, arguments=None):
        self.stream.write(
            (json.dumps({"execute": execute, "arguments": arguments or {}}) + "\n").encode()
        )
        while True:
            raw = self.stream.readline()
            if not raw:
                raise RuntimeError("QEMU disconnected")
            result = json.loads(raw)
            if "error" in result:
                raise RuntimeError(result["error"])
            if "return" in result:
                return result["return"]

    def event(self, event):
        self.qmp("input-send-event", {"events": [event]})

    def move_relative(self, dx, dy, dz=0):
        events = []
        if dx:
            events.append({"type": "rel", "data": {"axis": "x", "value": int(dx)}})
        if dy:
            events.append({"type": "rel", "data": {"axis": "y", "value": int(dy)}})
        if dz:
            events.append({"type": "btn", "data": {
                "button": "wheel-up" if dz > 0 else "wheel-down", "down": True}})
            events.append({"type": "btn", "data": {
                "button": "wheel-up" if dz > 0 else "wheel-down", "down": False}})
        if events:
            self.qmp("input-send-event", {"events": events})
        self.x += dx
        self.y += dy
        time.sleep(0.05)

    def move_to(self, x, y):
        # Send only the delta from the tracked position. Huge reset deltas are
        # dropped by QEMU and leave the guest clamped at an unknown corner.
        dx, dy = x - self.x, y - self.y
        if dx:
            self.move_relative(dx, 0)
        if dy:
            self.move_relative(0, dy)

    def click(self, x, y, button="left"):
        self.move_to(x, y)
        self.event({"type": "btn", "data": {"button": button, "down": True}})
        time.sleep(0.06)
        self.event({"type": "btn", "data": {"button": button, "down": False}})
        time.sleep(0.1)

    def wheel(self, amount):
        button = "wheel-up" if amount > 0 else "wheel-down"
        for _ in range(abs(amount)):
            self.event({"type": "btn", "data": {"button": button, "down": True}})
            time.sleep(0.03)
            self.event({"type": "btn", "data": {"button": button, "down": False}})
            time.sleep(0.05)

    def close(self):
        self.stream.close()
        self.sock.close()
