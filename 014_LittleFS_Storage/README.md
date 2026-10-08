# LittleFS storage console

This chapter uses only the dedicated external-flash `storage_partition`. Commands: `fs format`, `fs ls`, `fs mkdir <path>`, `fs rmdir <path>`, `fs create <file>`, `fs write <file> <text>`, `fs read <file>`, and `fs delete <file>`.

Build with `./build.sh`, flash the generated Sysbuild images, then run `./test_firmware.py --port /dev/ttyACM0`.
