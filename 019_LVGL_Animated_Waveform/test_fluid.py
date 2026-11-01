#!/usr/bin/env python3
"""Compile the firmware's exact fluid engine and check it under changing gravity."""
import ctypes
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent

def main():
    with tempfile.TemporaryDirectory(prefix="e84-fluid-") as folder:
        library = pathlib.Path(folder) / "fluid.so"
        subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-shared", "-fPIC", str(ROOT / "src/fluid.c"), str(ROOT / "src/motion.c"),
                        "-lm", "-o", str(library)], check=True)
        sim = ctypes.CDLL(str(library))
        sim.fluid_step.argtypes = [ctypes.c_float, ctypes.c_float]
        sim.fluid_advance.argtypes = [ctypes.c_float, ctypes.c_float, ctypes.c_uint32]
        sim.fluid_advance.restype = ctypes.c_uint
        sim.motion_screen_gravity.argtypes = [ctypes.c_int32, ctypes.c_int32,
                                              ctypes.POINTER(ctypes.c_float),
                                              ctypes.POINTER(ctypes.c_float)]
        bitmap = (ctypes.c_uint16 * (180 * 78))()
        sim.fluid_render.argtypes = [ctypes.POINTER(ctypes.c_uint16)]
        sim.fluid_reset()
        previous = None
        frames = []
        for name, gx, gy, axis, lo, hi in [
            ("down", 0, 160, "y", 45, 77),
            ("left", -160, 30, "x", 2, 65),
            ("right", 160, 30, "x", 115, 178),
            ("up", 0, -160, "y", 2, 33),
        ]:
            for _ in range(240):
                sim.fluid_step(gx, gy)
                assert sim.fluid_valid(), f"escaped or nonfinite particle under {name} gravity"
            sim.fluid_render(bitmap)
            cx, cy = sim.fluid_center_x(), sim.fluid_center_y()
            value = cx if axis == "x" else cy
            assert lo <= value <= hi, (name, cx, cy)
            assert 1500 < sim.fluid_pixels() < 10000, "fluid vanished or flooded raster"
            frame = bytes(bitmap)
            assert frame != previous, "render froze after gravity changed"
            previous = frame
            frames.append((name, frame))
            print(f"PASS {name}: center={cx},{cy} filled_pixels={sim.fluid_pixels()} bounds=valid")
        # Repeated abrupt direction changes must remain numerically stable.
        for i in range(1200):
            sim.fluid_step(200 if (i // 15) % 2 else -200,
                           200 if (i // 23) % 2 else -200)
            assert sim.fluid_valid(), "unstable during shake test"
        print("PASS shake: 1200 frames, all particles finite and inside walls")
        sim.fluid_reset()
        initial = (sim.fluid_center_x(), sim.fluid_center_y())
        assert initial == (87, 52), initial
        print("PASS reset: original liquid volume and position restored")
        # Exercise the SAME sensor-to-screen mapping as the firmware, not
        # only the direct-gravity test commands that bypass that mapping.
        for name, ax, ay, expected_x, expected_y, axis, lo, hi in [
            ("right edge raised", -500, 0, -135, 0, "x", 2, 65),
            ("left edge raised", 500, 0, 135, 0, "x", 115, 178),
            ("bottom edge raised", 0, 500, 0, -135, "y", 2, 33),
            ("top edge raised", 0, -500, 0, 135, "y", 45, 77),
            ("gentle bottom lift", 0, 100, 0, -27, "y", 2, 33),
        ]:
            gx, gy = ctypes.c_float(), ctypes.c_float()
            sim.motion_screen_gravity(ax, ay, ctypes.byref(gx), ctypes.byref(gy))
            assert abs(gx.value - expected_x) < 0.01, (name, gx.value)
            assert abs(gy.value - expected_y) < 0.01, (name, gy.value)
            sim.fluid_reset()
            for _ in range(300):
                sim.fluid_step(gx.value, gy.value)
                assert sim.fluid_valid()
            center = sim.fluid_center_x() if axis == "x" else sim.fluid_center_y()
            assert lo <= center <= hi, (name, center)
            print(f"PASS mapped pose {name}: gravity={gx.value:.0f},{gy.value:.0f}, center={center}")
        gx, gy = ctypes.c_float(), ctypes.c_float()
        sim.motion_screen_gravity(0, 0, ctypes.byref(gx), ctypes.byref(gy))
        assert gx.value == 0 and gy.value == 24, "flat-desk fallback missing"
        print("PASS flat-desk fallback: weak downward force only near level")
        # Equal elapsed time must produce identical physics regardless of
        # whether the panel renders at 60, 30, 20, or 15 frames per second.
        reference = None
        for schedule in ([16, 17, 17] * 40, [33, 33, 34] * 20,
                         [50] * 40, [66, 67, 67] * 10):
            sim.fluid_reset()
            steps = sum(sim.fluid_advance(-135, 30, ms) for ms in schedule)
            assert steps == 120, (steps, sum(schedule))
            assert sim.fluid_valid()
            sim.fluid_render(bitmap)
            frame = bytes(bitmap)
            if reference is not None:
                assert frame == reference, "physics speed depends on display frame rate"
            reference = frame
        print("PASS real-time physics: identical 2-second motion at 60/30/20/15 display FPS")
        sim.fluid_reset()
        assert sim.fluid_advance(280, -280, 5000) == 6, "unbounded catch-up after stall"
        for i in range(1200):
            sim.fluid_advance(280 if (i // 15) % 2 else -280,
                              280 if (i // 23) % 2 else -280, 67)
            assert sim.fluid_valid(), "unstable at faster response settings"
        print("PASS faster-response shake: 1200 irregular frames, bounded catch-up, no escaped particles")
        # Optional review image from the actual CPU raster, not a design mockup.
        try:
            from PIL import Image, ImageDraw
        except ImportError:
            return
        output = Image.new("RGB", (720, 4 * 342), "#02070d")
        draw = ImageDraw.Draw(output)
        for row, (name, raw) in enumerate(frames):
            values = memoryview(raw).cast("H")
            rgb = bytes(channel for v in values for channel in
                        (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
            panel = Image.frombytes("RGB", (180, 78), rgb).resize((720, 312), Image.Resampling.NEAREST)
            draw.text((8, row * 342 + 7), f"Gravity {name}", fill="white")
            output.paste(panel, (0, row * 342 + 30))
        target = ROOT / "test-results/fluid-physics.png"
        target.parent.mkdir(exist_ok=True)
        output.save(target)
        print(f"RENDER: {target}")

if __name__ == "__main__":
    main()
