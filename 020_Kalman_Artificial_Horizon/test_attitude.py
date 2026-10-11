#!/usr/bin/env python3
"""Host checks of the same Kalman estimator and horizon raster as the firmware."""
import ctypes as c
import math
import pathlib
import subprocess
import tempfile

ROOT=pathlib.Path(__file__).resolve().parent
class Kalman(c.Structure):
    _fields_=[(name,c.c_float) for name in ("angle","bias","p00","p01","p10","p11")]
class Attitude(c.Structure):
    _fields_=[("roll",Kalman),("pitch",Kalman),("yaw",c.c_float),("g",c.c_float),
              ("initialized",c.c_bool),("accel_used",c.c_bool)]
Vec=c.c_float*3

def main():
    with tempfile.TemporaryDirectory(prefix="e84-kalman-") as folder:
        path=pathlib.Path(folder)/"horizon.so"
        subprocess.run(["cc","-std=c11","-O2","-Wall","-Wextra","-Werror","-shared","-fPIC",
                        str(ROOT/"src/attitude.c"),str(ROOT/"src/horizon_draw.c"),"-lm","-o",str(path)],check=True)
        lib=c.CDLL(str(path))
        lib.attitude_reset.argtypes=[c.POINTER(Attitude)]
        lib.attitude_update.argtypes=[c.POINTER(Attitude),Vec,Vec,c.c_float]
        lib.attitude_update.restype=c.c_bool
        def update(s,a,w=(0,0,0),dt=.01):
            return lib.attitude_update(c.byref(s),Vec(*a),Vec(*w),dt)
        s=Attitude()
        for _ in range(1000): assert update(s,(0,0,1))
        assert abs(s.roll.angle)<.01 and abs(s.pitch.angle)<.01
        print("PASS stationary: level roll/pitch and 1 g")
        lib.attitude_reset(c.byref(s))
        for i in range(6000):
            noise=.01*math.sin(i*1.7)
            assert update(s,(0,noise,math.sqrt(1-noise*noise)),(math.radians(.6),0,0))
        assert abs(s.roll.angle)<.5 and abs(s.roll.bias-.6)<.15,(s.roll.angle,s.roll.bias)
        assert s.roll.p00>=0 and s.roll.p11>=0
        print(f"PASS Kalman gyro-bias estimation: angle={s.roll.angle:.3f}, bias={s.roll.bias:.3f} deg/s")
        lib.attitude_reset(c.byref(s)); assert update(s,(0,0,1))
        for i in range(1,201):
            phi=math.radians(20*i/200); theta=math.radians(15*i/200)
            a=(-math.sin(theta),math.sin(phi)*math.cos(theta),math.cos(phi)*math.cos(theta))
            w=(math.radians(10),math.radians(7.5)*math.cos(phi),-math.radians(7.5)*math.sin(phi))
            assert update(s,a,w)
        assert abs(s.roll.angle-20)<1 and abs(s.pitch.angle-15)<1,(s.roll.angle,s.pitch.angle)
        print("PASS combined rotation: gyro prediction and accelerometer correction track 20/15 deg")
        # Reject linear acceleration as a gravity correction, keep gyro prediction.
        before=s.roll.angle
        assert update(s,(0,1,1),(0,0,0)) and not s.accel_used
        assert abs(s.roll.angle-before)<.1
        old=bytes(s)
        assert not update(s,(float("nan"),0,1)) and bytes(s)==old
        assert not update(s,(0,0,0))
        assert not update(s,(0,0,1),dt=1)
        print("PASS disturbance gating and invalid sample/timing rejection")
        lib.attitude_reset(c.byref(s)); assert update(s,(0,0,1))
        for _ in range(100): assert update(s,(0,0,1),(0,0,math.pi/2))
        assert abs(s.yaw-90)<1
        print("PASS relative yaw: 90-degree gyro integration (not magnetic heading)")
        lib.attitude_reset(c.byref(s))
        phi=math.radians(179); assert update(s,(0,math.sin(phi),math.cos(phi)))
        for _ in range(100):
            phi=math.radians(-179); assert update(s,(0,math.sin(phi),math.cos(phi)))
        assert abs(s.roll.angle)>170
        print("PASS roll wrap: no jump through zero at +/-180 degrees")
        lib.horizon_draw.argtypes=[c.POINTER(c.c_uint16),c.c_float,c.c_float]
        n=344*344; storage=(c.c_uint16*(n+64))()
        for i in range(32): storage[i]=storage[n+32+i]=0x5aa5
        ptr=c.cast(c.byref(storage,64),c.POINTER(c.c_uint16))
        frames=[]
        for bank,pitch in [(0,0),(30,0),(-30,15),(0,-20),(180,70)]:
            lib.horizon_draw(ptr,bank,pitch)
            assert all(storage[i]==storage[n+32+i]==0x5aa5 for i in range(32))
            raw=c.string_at(ptr,n*2)
            assert all(raw!=other[2] for other in frames)
            frames.append((bank,pitch,raw))
        print("PASS horizon raster: distinct poses, intact memory guards")
        try:
            from PIL import Image,ImageDraw
        except ImportError: return
        preview=Image.new("RGB",(344*3,374),(6,12,20)); draw=ImageDraw.Draw(preview)
        for index,(bank,pitch,raw) in enumerate(frames[:3]):
            values=memoryview(raw).cast("H")
            rgb=bytes(component for v in values for component in
                      (((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31))
            preview.paste(Image.frombytes("RGB",(344,344),rgb),(344*index,30))
            draw.text((344*index+10,10),f"Bank {bank} / Pitch {pitch}",fill="white")
        output=ROOT/"test-results/horizon-render.png"; output.parent.mkdir(exist_ok=True)
        preview.save(output); print(f"RENDER: {output}")

if __name__=="__main__": main()
