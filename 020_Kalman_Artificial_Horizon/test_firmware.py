#!/usr/bin/env python3
"""Verify the physical Chapter 020 dashboard through its serial commands."""
import argparse
import contextlib
import pathlib
import sys
import time
import uuid
import serial
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/"tools/python"))
from psoc_e84_tools.serial_console import Tee

def request(port,command="info",timeout=8,boot=False):
    token=uuid.uuid4().hex[:8]
    payload=f"horizon {command} {token}\n".encode()
    if boot: port.write(b"\n")
    port.write(payload); port.flush()
    deadline=time.monotonic()+timeout; retry=time.monotonic()+1
    while time.monotonic()<deadline:
        line=port.readline().decode(errors="replace").strip()
        if line: print(line)
        if line=="FAIL HORIZON COMMAND" and boot:
            continue  # Resynchronize a partial command received during boot.
        if "FAIL " in line or "FATAL" in line or "ASSERTION" in line:
            raise RuntimeError(line)
        prefix="OK COMPASS STATUS" if command=="compass" else "OK HORIZON STATUS"
        if line.startswith(prefix) and f"token={token}" in line:
            return dict(x.split("=",1) for x in line.split() if "=" in x)
        if boot and time.monotonic()>=retry:
            port.write(b"\n"+payload); port.flush(); retry=time.monotonic()+1
    raise RuntimeError(f"no fresh response to {command}")

def healthy(s):
    if s.get("convention")!="pilot_v2": raise RuntimeError("old pitch/heading firmware convention")
    if not 0<=int(s["heading10"])<3600: raise RuntimeError("invalid unsigned display heading")
    if s.get("heading_style")!="rotating_card": raise RuntimeError("old heading dial presentation")
    if not 0<=int(s["card_heading10"])<3600: raise RuntimeError("invalid rendered card heading")
    for name,value in (("ret","0"),("display_ret","0"),("touch","1"),("compass","live")):
        if s.get(name)!=value: raise RuntimeError(f"{name}={s.get(name)}")
    if int(s["samples"])<1 or int(s["renders"])<1 or int(s["age_ms"])>150:
        raise RuntimeError("stale sensor data or renderer not advancing")
    if not 50<int(s["g100"])<150: raise RuntimeError("unexpected acceleration magnitude")

def baro_healthy(s):
    if s.get("baro_ret")!="0" or int(s["baro_samples"])<1 or int(s["baro_age_ms"])>500:
        raise RuntimeError("barometer unavailable or stale")
    if not 30000<=int(s["pressure_pa"])<=120000:
        raise RuntimeError("barometer pressure units/range")
    if not -4000<=int(s["temperature100"])<=8500:
        raise RuntimeError("barometer temperature range")
    if not 30000<=int(s["reference_pa"])<=120000:
        raise RuntimeError("missing ground pressure reference")

def run(port,args):
    s=request(port,timeout=args.timeout,boot=True)
    deadline=time.monotonic()+args.timeout
    while (s["compass"]!="live" or int(s["samples"])<1 or int(s.get("baro_samples",0))<1) and time.monotonic()<deadline:
        time.sleep(.2); s=request(port)
    healthy(s)
    start=time.monotonic(); time.sleep(1)
    s2=request(port); elapsed=time.monotonic()-start; healthy(s2)
    hz=(int(s2["samples"])-int(s["samples"]))/elapsed
    if hz<70 or hz>130: raise RuntimeError(f"fusion frequency {hz:.1f} Hz")
    if int(s2["renders"])<=int(s["renders"]): raise RuntimeError("renderer stalled")
    print(f"PASS health: {hz:.1f} Hz fusion, display scanout, touch ready, honest compass status")
    if args.case in ("all","baro"):
        baro_healthy(s2)
        start=time.monotonic(); time.sleep(3); s3=request(port); elapsed=time.monotonic()-start
        baro_healthy(s3); healthy(s3)
        baro_hz=(int(s3["baro_samples"])-int(s2["baro_samples"]))/elapsed
        if not 7<=baro_hz<=12: raise RuntimeError(f"barometer rate {baro_hz:.1f} Hz")
        if s3["baro_errors"]!=s2["baro_errors"]: raise RuntimeError("new barometer errors")
        print(f"PASS barometer: {baro_hz:.1f} Hz, {int(s3['pressure_pa'])/100:.2f} hPa, no new errors")
    if args.case in ("all","calibrate"):
        initial=request(port,"calibrate")
        deadline=time.monotonic()+args.timeout
        while time.monotonic()<deadline:
            time.sleep(.3); s=request(port); healthy(s)
            if s["calibrating"]=="0" and int(s["calibrations"])>int(initial["calibrations"]): break
        else: raise RuntimeError("calibration incomplete: board must be held still for one second")
        print("PASS calibrate: new stationary gyro-bias calibration completed")
    if args.case in ("all","zero"):
        before=request(port)
        # Ground reference is rejected while stationary gyro calibration is pending.
        deadline=time.monotonic()+args.timeout
        while before["calibrating"]=="1" and time.monotonic()<deadline:
            time.sleep(.3); before=request(port)
        s=request(port,"zero"); healthy(s)
        if any(abs(int(s[x]))>1 for x in ("roll10","pitch10","yaw10")):
            raise RuntimeError("zero-view reference not applied")
        if s["frame"]!="ground" or int(s["zeros"])!=int(before["zeros"])+1:
            raise RuntimeError("ground frame not set")
        if abs(int(s["altitude100"]))>10 or abs(int(s["climb100"]))>10:
            raise RuntimeError("ground pressure reference not set")
        time.sleep(.5); after=request(port); healthy(after)
        if abs(int(after["roll10"]))>20 or abs(int(after["pitch10"]))>20:
            raise RuntimeError("ground zero did not remain level (keep the board still)")
        print("PASS ground zero: transformed pose, relative heading and barometric reference; stable after reset")
    if args.case in ("all","hold"):
        try:
            s=request(port,"hold"); time.sleep(.5); s2=request(port); healthy(s2)
            if s2["held"]!="1" or s2["renders"]!=s["renders"] or s2["crc"]!=s["crc"]:
                raise RuntimeError("held horizon changed")
            if int(s2["samples"])<=int(s["samples"]): raise RuntimeError("hold stopped fusion")
            if s2["card_heading10"]!=s["card_heading10"]: raise RuntimeError("held heading card moved")
            print("PASS hold: frozen horizon and heading card while sensor fusion continues")
        finally: request(port,"live")
        time.sleep(.3); s3=request(port); healthy(s3)
        if s3["held"]!="0" or int(s3["renders"])<=int(s2["renders"]):
            raise RuntimeError("live rendering did not resume")
        print("PASS live: dashboard rendering resumed")
    if args.case in ("all","upright"):
        request(port,"upright"); time.sleep(.3); s=request(port); healthy(s)
        if s["frame"]!="upright": raise RuntimeError("upright default not restored")
        print("PASS upright default: mounting restored; physical upright/sign check requires the user")

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--port",default="/dev/ttyACM0")
    parser.add_argument("--timeout",type=float,default=15)
    parser.add_argument("--case",choices=["all","health","zero","hold","calibrate","baro","upright"],default="all")
    args=parser.parse_args()
    out=pathlib.Path(__file__).with_name("test-results"); out.mkdir(exist_ok=True)
    log=out/f"horizon-{time.strftime('%Y%m%d-%H%M%S')}.log"
    with log.open("w") as file,contextlib.redirect_stdout(Tee(sys.stdout,file)),contextlib.redirect_stderr(Tee(sys.stderr,file)):
        print(f"LOG: {log}")
        try:
            with serial.Serial(args.port,115200,timeout=.2) as port:
                port.reset_input_buffer()
                run(port,args)
        except (RuntimeError,ValueError,KeyError,serial.SerialException) as error:
            print(f"FAIL: {error}"); return 1
    return 0

if __name__=="__main__": raise SystemExit(main())
