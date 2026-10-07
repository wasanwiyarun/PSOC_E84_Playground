# CM55 to CM33-NS PSA mailbox relay

This chapter validates the supported PSoC Edge E84 multicore path. CM33
Non-Secure owns CM55 boot and hosts the TF-M relay; CM55 uses the official
mailbox/SRF client path to make PSA calls to that relay. It is intentionally
not a raw, application-defined bidirectional mailbox protocol.

The CM55 application waits for `ipc start`, then issues 100 ordered
`psa_framework_version()` requests.
With `CONFIG_PSOC_EDGE_M55_SRF_SUPPORT=y`, the CM55 PSA client implementation
forwards each request to the CM33-NS/TF-M relay. The test rejects a zero or
changing version and prints one success line per sequence plus a final pass
marker.

## Build and flash

The images are standalone builds, not a Sysbuild image. Build CM33-NS first:

```sh
./build.sh
```

Then connect the KitProg3 J1 USB-C port and program CM55 before CM33-NS:

```sh
./flash.sh
```

To wait for the final CM55 acceptance marker on the board's serial port. The
script saves a timestamped log under `test-results/`:

```sh
./test_firmware.py --port /dev/ttyACM0
```

The serial console should include these markers (the two cores share the
board console, so line ordering may interleave):

```text
OK CORE=CM33-NS ROLE=RELAY_HOST PSA_VERSION=...
OK CORE=CM55 ROLE=RELAY_CLIENT REQUESTS=100
OK RELAY SEQ=1 PSA_VERSION=...
PASS RELAY COUNT=100 PSA_VERSION=...
```

The CM55 serial commands are `ipc start`, `ipc status`, and `help`. Each
request is synchronous: CM55 sends one PSA request and waits for its CM33-NS
relay response before sending the next sequence number.

## Acceptance and limits

- CM33-NS must build before CM55 because the CM55 build consumes generated
  TF-M PSA headers from the CM33-NS build directory.
- Flash CM55 before CM33-NS; CM33-NS starts CM55 after reset.
- `PASS RELAY COUNT=100` confirms 100 ordered CM55-to-CM33 relay calls with a
  stable nonzero PSA framework version.
- Do not run this test with CM33-NS absent. The installed relay client uses an
  unbounded wait for a reply, so absence is not a safely measurable timeout.
- This chapter does not reserve or expose a raw application mailbox, shared
  memory, or a CM33-to-CM55 request channel. Those require a separate,
  platform-supported ownership design.
