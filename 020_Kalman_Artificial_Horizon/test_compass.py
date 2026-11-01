#!/usr/bin/env python3
"""Real-board BMM350 I3C identification and sustained acquisition tests.

This does not certify compass calibration, mounting axes or north accuracy.
"""
import argparse
import contextlib
import math
import pathlib
import sys
import time
import serial
from test_firmware import request, Tee

def check(s):
    assert s["ready"]=="1" and s["ret"]=="0", f"sensor not ready: {s}"
    assert int(s["chip_id"],16)==0x33, f"wrong BMM350 ID: {s}"
    assert 8<=int(s["address"],16)<0x78, "invalid dynamic address"
    assert int(s["clock_hz"])==100000000, "wrong I3C clock"
    assert int(s["samples"])>0 and 0<=int(s["age_ms"])<250, "stale magnetic data"
    field=math.sqrt(sum((int(s[k])/100)**2 for k in ("x100","y100","z100")))
    assert 0.1<field<3500, f"implausible field: {field} uT"
    assert -4000<int(s["temp100"])<10000, "implausible temperature"

def run(port,args):
    s=request(port,"compass",timeout=15,boot=True)
    deadline=time.monotonic()+15
    while (s["ret"]=="-11" or (s["ready"]=="1" and s["samples"]=="0")) and time.monotonic()<deadline:
        time.sleep(.2); s=request(port,"compass")
    check(s)
    print(f"PASS id: BMM350 0x{s['chip_id']}, I3C dynamic address 0x{s['address']}")
    if s.get("field_high")=="1":
        print("WARNING: field exceeds 100 uT application threshold; heading not validated")
    if args.case=="field":
        assert s["field_high"]=="0", "HIGH FIELD: inspect magnetic environment/calibration before heading"
        print("PASS field: below warning threshold (not a north-accuracy test)"); return
    if args.case=="id": return
    first=s; start=time.monotonic()
    for _ in range(args.seconds*2):
        time.sleep(.5); s=request(port,"compass"); check(s)
        assert int(s["errors"])==int(first["errors"]), "new bus/sensor error"
    elapsed=time.monotonic()-start
    hz=(int(s["samples"])-int(first["samples"]))/elapsed
    assert 20<hz<30, f"magnetometer rate {hz:.2f} Hz"
    assert int(s["transfers"])>int(first["transfers"]), "bus transfer count stalled"
    print(f"PASS data: {hz:.2f} Hz over {elapsed:.1f}s, no new errors, fresh compensated XYZ/temperature")

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--port",default="/dev/ttyACM0")
    p.add_argument("--case",choices=["all","id","data","field"],default="all")
    p.add_argument("--seconds",type=int,default=10)
    args=p.parse_args()
    if args.seconds<2: p.error("--seconds must be at least 2")
    out=pathlib.Path(__file__).with_name("test-results"); out.mkdir(exist_ok=True)
    log=out/f"compass-{args.case}-{time.strftime('%Y%m%d-%H%M%S')}-{time.time_ns()%1000000:06d}.log"
    with log.open("w") as f, contextlib.redirect_stdout(Tee(sys.stdout,f)), contextlib.redirect_stderr(Tee(sys.stderr,f)):
        print(f"LOG: {log}")
        try:
            with serial.Serial(args.port,115200,timeout=.2,exclusive=True) as port:
                port.reset_input_buffer(); run(port,args)
        except (AssertionError,RuntimeError,ValueError,KeyError,serial.SerialException) as exc:
            print(f"FAIL: {exc}"); return 1
    return 0

if __name__=="__main__": raise SystemExit(main())
