# PSOC Edge84 AI Kit Hello World

A minimal Zephyr application for the PSOC Edge84 AI Kit M55 core.

The AI Kit's M55 core is booted by its M33 core, so it must be built with
Sysbuild. From this directory, configure and build it with:

```sh
./build.sh
```

The onboard KitProg3 programmer/debugger is exposed through USB-C connector
J1. Sysbuild creates two images: the CM33 companion that starts the M55, and
the M55 application. Once the build succeeds, program both images with:

```sh
../tools/flash_and_monitor_pse84_ai.sh \
  --build-dir build-pse84-ai \
  --companion-domain enable_cm55 \
  --app-domain 001_Hello_World \
  --expect "Hello from the PSOC Edge84 Zephyr playground!"
```

This replaces the firmware currently on the board and verifies its serial
output. KitProg3's serial console is available at `/dev/ttyACM0` with 115200
8N1 settings. The flashing tool is shared: it receives this example's build
directory, Sysbuild domains, and expected output as explicit arguments.

The program writes this line to the Zephyr console:

```text
Hello from the PSOC Edge84 Zephyr playground!
```

It waits two seconds after reset before printing, allowing a serial monitor to
attach first.
