#!/usr/bin/env python3
"""Real-board command tests: identity, deterministic FIFO, raw ADC and restart."""
import argparse
import contextlib
import json
import pathlib
import struct
import sys
import time
import uuid
import zlib
import serial

ROOT=pathlib.Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT.parent/"tools/python"))
from psoc_e84_tools.serial_console import Tee

def fields(line):
    return dict(part.split("=",1) for part in line.split() if "=" in part)

def request(port,command,expected_failure=False):
    token=uuid.uuid4().hex[:8]
    port.write(f"radar {command} {token}\n".encode()); port.flush()
    deadline=time.monotonic()+15
    data=[]; started=False
    while time.monotonic()<deadline:
        line=port.readline().decode(errors="replace").strip()
        if not line: continue
        print(line)
        if "FATAL" in line or "ASSERTION" in line: raise RuntimeError(line)
        f=fields(line)
        if f.get("token")!=token: continue
        if line.startswith("FAIL"):
            if expected_failure: return f,data
            raise RuntimeError(line)
        if expected_failure: raise RuntimeError("invalid command was accepted")
        if line.startswith("OK RADAR START"):
            assert not started
            started=True
        elif line.startswith("DATA RADAR"):
            assert started and int(f["frame"])==len(data)
            raw=f["raw"]; assert len(raw)==128*3
            values=[int(raw[i:i+3],16) for i in range(0,len(raw),3)]
            assert min(values)==int(f["min"]) and max(values)==int(f["max"])
            assert sum(values)==int(f["sum"])
            data.append(values)
        elif line.startswith("OK RADAR DONE"):
            assert started and int(f["ret"])==0
            assert len(data)==int(f["frames"]) and len(data)*128==int(f["samples"])
            packed=struct.pack("<"+"H"*int(f["samples"]),*(v for row in data for v in row))
            assert zlib.crc32(packed)==int(f["crc"],16),"UART data CRC mismatch"
            assert int(f["irq_observed"])>0,"data-ready pin never asserted"
            return f,data
        elif line.startswith(("OK RADAR INFO","OK RADAR RESET")):
            assert int(f["ret"])==0
            return f,data
    raise RuntimeError(f"timeout: {command}")

def pattern(rows):
    # Independent polynomial implementation, not the C driver's helper.
    word=1
    for row in rows:
        for sample in row:
            assert sample==word,f"LFSR expected {word:03x}, received {sample:03x}"
            feedback=((word>>0)^(word>>1)^(word>>2)^(word>>8))&1
            word=(word>>1)|(feedback<<11)

def run(port,args,out):
    # Newline resynchronizes an incomplete manual command before our first request.
    port.write(b"\n"); port.flush()
    f,_=request(port,"id")
    assert int(f["chip_id"],16)==0x303,"wrong chip ID"
    assert 0<int(f["spi_hz"])<=25000000
    print(f"PASS id: BGT60TR13C 0x{f['chip_id']}, SPI {f['spi_hz']} Hz")
    if args.case in ("all","selftest"):
        f,rows=request(port,"selftest 64"); pattern(rows)
        assert int(f["test"])==1 and int(f["mismatches"])==0 and len(rows)==64
        hz=len(rows)*1e6/int(f["elapsed_us"])
        assert 150<hz<250,f"unexpected frame rate {hz:.2f}"
        print(f"PASS selftest: 8192 exact LFSR samples, CRC verified, data-ready high, {hz:.2f} frames/s")
        f,rows=request(port,"selftest 1"); pattern(rows)
        assert len(rows)==1
        print("PASS selftest repeat: minimum-size capture restarts at the expected sequence")
    if args.case in ("all","capture"):
        captures=[]
        for _ in range(2):
            f,rows=request(port,"capture 16")
            assert int(f["test"])==0 and len(rows)==16
            flat=[v for row in rows for v in row]
            assert max(flat)-min(flat)>4 and len(set(flat))>4,"flat/stuck ADC data"
            try: pattern(rows)
            except AssertionError: pass
            else: raise RuntimeError("capture still contains self-test data")
            captures.append({"status":f,"samples":rows})
        assert captures[0]["samples"]!=captures[1]["samples"],"repeated stale capture"
        (out/"raw-captures.json").write_text(json.dumps(captures,indent=2))
        print("PASS capture: two fresh 2048-sample ADC captures, not LFSR, CRC verified; saved raw-captures.json")
    if args.case in ("all","reset"):
        request(port,"reset")
        f,rows=request(port,"selftest 4"); pattern(rows)
        assert len(rows)==4
        print("PASS reset: reinitialize and repeat exact FIFO sequence")
    if args.case in ("all","parser"):
        for command in ("capture 0","capture 65","capture -1","capture abc","capture 1junk","unknown"):
            request(port,command,expected_failure=True)
        request(port,"info")
        # Overflow is a parser error, not a silently truncated valid capture.
        port.write(b"x"*160+b"\n"); port.flush()
        deadline=time.monotonic()+3
        while time.monotonic()<deadline:
            line=port.readline().decode(errors="replace").strip()
            if line: print(line)
            if line=="FAIL RADAR COMMAND_TOO_LONG": break
        else: raise RuntimeError("overlong input was not rejected")
        request(port,"info")
        print("PASS parser: invalid counts, unknown and overlong commands rejected; firmware remains responsive")

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--port",default="/dev/ttyACM0")
    p.add_argument("--case",choices=["all","id","selftest","capture","reset","parser"],default="all")
    args=p.parse_args()
    out=ROOT/"test-results"/time.strftime("%Y%m%d-%H%M%S"); out.mkdir(parents=True,exist_ok=True)
    with (out/"radar.log").open("w") as log,contextlib.redirect_stdout(Tee(sys.stdout,log)):
        print(f"LOG: {out/'radar.log'}")
        try:
            with serial.Serial(args.port,115200,timeout=.2) as port:
                port.reset_input_buffer(); run(port,args,out)
        except (AssertionError,RuntimeError,ValueError,KeyError,serial.SerialException) as error:
            print(f"FAIL: {error}"); return 1
    return 0

if __name__=="__main__": raise SystemExit(main())
