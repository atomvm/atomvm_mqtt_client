# Build Instructions

Building the `atomvm_mqtt` port requires first building and flashing and AtomVM image containing the port code in this repository:

1. Build AtomVM, including the Port "component"
1. Build an AtomVM image file
1. Erase the ESP32 device and flash the AtomVM image file to the ESP32 device

Once the AtomVM image is created and flashed, you can then develop your application in an iterative fashion:

1. Integrate the AtomVM MQTT library into your application
1. Flash your application to the ESP32 device
1. Debug/Fix/Flash/Rinse/Repeat


## Build AtomVM, including the `atomvm_mqtt` Port

### Prerequisites

Because the IDF SDK does not support dynamic loading of components, any extensions to the AtomVM virtual machine must be linked into the AtomVM image.  Therefore, building the `atomvm_mqtt` Port requires that you build the AtomVM virtual machine.

Instructions for setting up a build environment for AtomVM are outside of the scope of this document.

> For build instructions of the AtomVM Virtual machine on the ESP32 platform, see [here](https://github.com/bettio/AtomVM/blob/master/README.ESP32.Md#building-atomvm-for-esp32).

The remaining sections assume you have downloaded the AtomVM github repository and have all of the prerequisites neeed to build an AtomVM image targeted for the ESP32 platform.

### Build Steps

Clone this repository in the `src/platforms/esp32/components` directory of the AtomVM source tree.

    shell$ cd .../AtomVM/src/platforms/esp32/components
    shell$ git clone https://github.com/fadushin/atomvm_mqtt.git

Edit the `component_port.txt` file in `src/platforms/esp32/main`.  The contents of this file should contain a line for the `atomvm_mqtt` port driver:

    atomvm_mqtt_driver

Build AtomVM (typically run from `src/platforms/esp32`)

    shell$ cd .../AtomVM/src/platforms/esp32
    shell$ make
    ...
    CC .../AtomVM/src/platforms/esp32/build/atomvm_mqtt/ports/atomvm_mqtt.o
    AR .../AtomVM/src/platforms/esp32/build/atomvm_mqtt/libatomvm_mqtt.a
    ...
    To flash all build output, run 'make flash' or:
    python /opt/esp-idf-v3.3/components/esptool_py/esptool/esptool.py --chip esp32 --port /dev/tty.SLAB_USBtoUART --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect 0x1000 .../AtomVM/src/platforms/esp32/build/bootloader/bootloader.bin 0x10000 .../AtomVM/src/platforms/esp32/build/atomvvm-esp32.bin 0x8000 .../AtomVM/src/platforms/esp32/build/partitions.bin

Once the AtomVM image is flashed to the ESP32 device, it includes the internal interfaces needed for communicating with the ADC on the ESP32 device.

## Build and flash an AtomVM image file

If you have already flashed a full AtomVM image to your ESP32 device, then you can simply issue the `flash` target to your device, in order to upload the AtomVM binary image to your device.

Otherwise, you will need to build a complete AtomVM image, containing the boot partition, AtomVM BEAM libraries, and AtomVM image, and then flash it to your device.

Fortunately, AtomVM contains scripts that make this relatively painless.

From the top level source directory of the AtomVM source tree, issue:

    shell$ ./tools/release/esp32/mkimage.sh
    Writing output to .../AtomVM/src/platforms/esp32/build/atomvm-c734de9.img
    =============================================
    Wrote bootloader at offset 0x1000 (4096)
    Wrote partition-table at offset 0x8000 (32768)
    Wrote AtomvVM Virtual Machine at offset 0x10000 (65536)
    Wrote AtomvVM Core BEAM Library at offset 0x110000 (1114112)

You can now flash this image to your ESP32 device:

    shell$ ./tools/release/esp32/flashimage.sh
    %%
    %% Flashing .../AtomVM/src/platforms/esp32/build/atomvm-c734de9.img (size=2108k)
    %%
    esptool.py v2.8-dev
    Serial port /dev/tty.SLAB_USBtoUART
    Connecting....
    Chip is ESP32D0WDQ6 (revision 1)
    Features: WiFi, BT, Dual Core, 240MHz, VRef calibration in efuse, Coding Scheme None
    Crystal is 40MHz
    MAC: 3c:71:bf:84:d9:08
    Uploading stub...
    Running stub...
    Stub running...
    Changing baud rate to 921600
    Changed.
    Configuring flash size...
    Auto-detected Flash size: 4MB
    Wrote 1163264 bytes at 0x00001000 in 14.2 seconds (656.8 kbit/s)...
    Hash of data verified.
    Leaving...
    Hard resetting via RTS pin...

> Note.  You can set the `FLASH_SERIAL_PORT` and `FLASH_BAUD_RATE` environment variables to suit your environment, if necessary.  E.g., under bourne shell and its derivatives:

    shell$ export FLASH_SERIAL_PORT=/dev/tty.SLAB_USBtoUART
    shell$ export FLASH_BAUD_RATE=921600

Your ESP32 device is now ready for integration with your application.

## Integrate the `atomvm_mqtt` Library

### Prerequisites

Integrating the `atomvm_mqtt` library requires [rebar3](https://www.rebar3.org) and [git](https://git-scm.com)

### Integration Steps

The `rebar3` tool makes it relatively easy to integrate `atomvm_mqtt` into your application (assuming you are using `rebar3` already).

Just add the following dependencies to your `rebar.config` file:

    {deps, [
        {atomvm_mqtt, {git, "https://github.com/fadushin/atomvm_mqtt.git", {branch, "master"}}}
    ]}.
    {plugins, [
        {atomvm_rebar3_plugin, {git, "https://github.com/fadushin/atomvm_rebar3_plugin.git", {branch, "master"}}}
    ]}.

With this configuration, you may now issue the `esp32_flash` (and optionally, `packbeam`) target to the `rebar3` command, e.g.,

    shell$ rebar3 help esp32_flash
    ===> Fetching atomvm_rebar3_plugin (from {git,"https://github.com/fadushin/atomvm_rebar3_plugin.git",
                                    {branch,"master"}})
    ===> Fetching packbeam (from {git,"https://github.com/fadushin/atomvm_packbeam.git",
                        {branch,"master"}})
    ===> Analyzing applications...
    ===> Compiling atomvm_rebar3_plugin
    ===> Compiling packbeam
    A rebar plugin to flash packbeam to ESP32 devices
    Usage: rebar3 esp32_flash [-e] [-p] [-b] [-o]

    -e, --esptool  Path to esptool.py
    -p, --port     Device port (default /dev/ttyUSB0)
    -b, --baud     Baud rate (default 115200)
    -o, --offset   Offset (default 0x110000)

Thus, for example, if you need to specify a non-default port:

    shell$ rebar3 esp32_flash -p /dev/tty.SLAB_USBtoUART
    ===> Verifying dependencies...
    ===> App mqtt is a checkout dependency and cannot be locked.
    ===> Analyzing applications...
    ===> Compiling mqtt
    ===> Analyzing applications...
    ===> Compiling mqtt_example
    ===> AVM file written to : mqtt.avm
    ===> AVM file written to : my_app.avm
    ===> esptool.py --chip esp32 --port /dev/tty.SLAB_USBtoUART --baud 115200 --before default_reset --after hard_reset write_flash -u --flash_mode dio --flash_freq 40m --flash_size detect 0x210000 .../my_app/_build/default/lib/my_app.avm

    esptool.py v2.1
    Connecting........_
    Chip is ESP32D0WDQ6 (revision (unknown 0xa))
    Uploading stub...
    Running stub...
    Stub running...
    Configuring flash size...
    Auto-detected Flash size: 4MB
    Wrote 16384 bytes at 0x00210000 in 1.4 seconds (91.3 kbit/s)...
    Hash of data verified.

    Leaving...
    Hard resetting...

You are now ready to integrate the [`atomvm_mqtt` API](api.md) into your application.
