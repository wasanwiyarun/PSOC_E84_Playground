# External QSPI raw access

Uses only the board-defined 1 MiB `storage_partition` at physical external-flash offset zero. Images start at 1 MiB or higher. `flash start` erases one 256 KiB storage sector, writes 256 bytes, reads it back, checks CRC-32, then erases that sector again.

Build with `./build.sh`. Flash using the shared helper, then run
`./test_firmware.py --port /dev/ttyACM0`. The verifier first checks the
partition identity, then sends `flash start` and requires both CRC and cleanup
markers. This operation intentionally erases the first storage sector only;
do not use it after placing data or LittleFS in that partition.
