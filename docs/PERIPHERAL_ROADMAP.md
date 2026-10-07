# PSOC Edge E84 AI Kit Peripheral Roadmap

Every completed chapter is built with the local Zephyr workspace, programmed
through KitProg3, and verified through `/dev/ttyACM0`.  Firmware test scripts
write timestamped serial logs that are intentionally ignored by Git.

| Chapter | Peripheral | State | Result |
| --- | --- | --- | --- |
| 001 | Console UART | Verified | Hello-world output received. |
| 002 | LED0 + console | Verified | Blink control, period changes, and repeated serial commands tested. |
| 003 | I²C SHT40, BMI270, DPS368 | Verified | All expected devices responded at `0x44`, `0x68`, and `0x77`. |
| 004 | SHT40 + DPS368 | Verified | Live temperature, humidity, and pressure measurements received. |
| 005 | BMI270 accelerometer + gyroscope | Verified | Zephyr driver returns live converted motion data; test also verifies non-zero raw data at `0x68`. |
| 006 | Button + LED event | Protocol verified | Firmware booted with GPIO setup; serial LED commands passed. A physical SW1 press is still required to verify the interrupt event end-to-end. |
| 007 | BMM350 magnetometer | Blocked | Zephyr contains a BMM350 driver, but this board DTS has no enabled I3C controller, pinctrl, or BMM350 device node. |
| 008 | BGT60TR13C radar | Blocked | No matching BGT60TR13C driver exists in this Zephyr tree and the board has no radar SPI device node. |
| 009 | PDM microphone | Blocked | An Infineon DMIC driver exists, but all E84 PDM controller channels and board microphone pin configuration are disabled. |
| 010 | CYW55513 Wi-Fi SSID scan | Verified | Board scan completed and printed nearby SSIDs, RSSI, and channel without connecting. |
| 011 | CYW55513 Bluetooth LE | Verified | Controller firmware loaded over H:4 UART; serial test verifies connectable advertising control and the standard Nordic UART Service UUID. nRF Connect remains the over-the-air NUS acceptance check. |
| 012 | CM55→CM33-NS PSA mailbox relay | Verified | CM33-NS and CM55 paired standalone images completed 100 ordered PSA requests through the supported relay on the board; PSA version was stable at `0x00000101`. |
| 013 | External QSPI flash, raw API | Planned | Exercise only a newly reserved storage partition: identify, erase, write a patterned block, read it back, and verify integrity. |
| 014 | LittleFS on external QSPI flash | Planned | Mount a dedicated storage partition, create/read/replace a record, and prove the record persists across a board reset. |
| 015 | Secure Enclave service | Planned | Build a secure/non-secure demonstration in which non-secure code calls one restricted secure operation and validates its result without printing or exporting secret material. |

## Planned platform chapters

### 012: CM55→CM33-NS PSA mailbox relay

This is a multicore firmware chapter, but the installed PSoC E84 support is
not a general-purpose application mailbox. CM33-NS owns CM55 boot and the
TF-M relay; CM55 is the supported client of that relay. The chapter therefore
builds the two standalone images in order, then has CM55 issue 100 ordered
`psa_framework_version()` requests through the mailbox/SRF relay.

Acceptance requires both images to build, both cores to print their identity,
and CM55 to emit `PASS RELAY COUNT=100` with one stable nonzero PSA version.
CM55 must be flashed before CM33-NS, because CM33-NS starts CM55 at reset.
The relay client in the installed SDK waits indefinitely when CM33-NS is
absent, so a missing-core timeout test is explicitly outside this chapter.
The chapter does not expose raw shared-memory pointers or an arbitrary
CM33-to-CM55 message channel.

### 013: external-QSPI flash raw access

The board memory map describes a 64 MB external QSPI flash and an existing
`storage_partition`. Before writing anything, the chapter must inspect the
actual partition map and reserve a region that cannot overlap a boot image,
application image, or future secure asset. The test then uses Zephyr's flash
API to erase one sector, write a known pattern plus CRC, read it back, and
verify it. It restores the erased state at the end of the test.

Acceptance is limited to the reserved storage partition. A successful build or
flash-driver probe alone does not authorize writes to application partitions.

### 014: LittleFS storage

This follows raw-flash verification. The example will mount LittleFS over the
reserved storage partition, write a versioned settings record and a short log,
unmount/remount it, reset the board, then verify both records and checksums.
It will also report free-space and mount errors over the serial console.

The filesystem owner is a single core in the first version. CM55↔CM33 shared
filesystem access is explicitly out of scope; Chapter 012 validates only the
supported PSA relay and does not provide a general filesystem request path.

### 015: Secure Enclave service

The board support already provides secure and non-secure CM33 build paths;
this chapter first documents which PSE84 secure-enclave/secure-request API is
available in the installed SDK and selects one safe operation. The candidate
demonstration is a secure-generated challenge response or secure monotonic
counter read, rather than embedding a private key in application source.

Acceptance requires a non-secure caller to receive a valid result, an invalid
request to be rejected, and a build/flash procedure that preserves the secure
and non-secure image chain. No secret bytes, keys, or credentials will be
printed in serial logs or committed to the repository.

## BMI270 follow-up

The board schematic routes the BMI270 through I2C0 at `0x68`. The rebuilt
motion monitor uses the normal Zephyr driver, checks its configuration return
codes, and validates both realistic converted gravity and non-zero raw data.
The earlier all-zero result was an obsolete application/test issue, not a
hardware or current-driver blocker.

## Capability review: remaining peripherals

The remaining onboard peripherals are not skipped because they are
uninteresting; they need board-support work before an application can safely
claim to use them.

- **BMM350:** The local tree has I3C BMM350 bindings and a driver. The E84
  board must first enable and pinmux `i3c0`, add the sensor node with its
  verified address, and establish any required pull-up/power-control setup.
- **BGT60TR13C:** There is no BGT60 radar driver or board SPI node in this
  Zephyr revision. A dedicated driver and validated reset/interrupt/SPI device
  tree integration are prerequisites.
- **PDM microphone:** The SoC and Infineon DMIC driver are present, but the
  E84 board leaves `dmic0` and every channel disabled. A board overlay needs
  the actual microphone pins, clock, channel choice, and DMA/audio format.
- **Wi-Fi:** The E84 M55 DTS declares the CYW55513 SDIO and control GPIOs.
  Chapter 010 verifies credential-free scanning. Connecting to an access point
  remains deferred until an approved SSID and credential are supplied; the
  project will not embed or guess them.
- **Bluetooth LE:** Chapter 011 verifies controller startup, advertising, and the standard Nordic UART Service.
  Future work can add a custom GATT service, pairing, or a phone-based
  end-to-end acceptance test.
- **CM55→CM33-NS relay:** Chapter 012 builds a supported PSA mailbox relay
  test before any feature needs one core to request a service owned by another.
- **External flash and LittleFS:** Chapters 013 and 014 use only a deliberately
  reserved storage partition. Raw-flash verification comes before filesystem
  formatting and persistence tests.
- **Secure Enclave:** Chapter 015 is planned as a constrained secure-service
  demonstration using the board's secure/non-secure CM33 support. The exact
  service API will be selected only after validating the installed SDK's
  secure-enclave interface.
