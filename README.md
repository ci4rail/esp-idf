# Espressif IoT Development Framework

# Changes in this Fork

This fork contains:
* New [Ethernet-over-USB driver](components/tinyusb/additions/src/esp_cdc_ecm_driver.c), see [this example](examples/peripherals/usb/tusb_cdc_ecm/README.md)

# Original README:

[中文版](./README_CN.md)

ESP-IDF is the development framework for Espressif SoCs supported on Windows, Linux and macOS.

# ESP-IDF Release and SoC Compatibility

The following table shows ESP-IDF support of Espressif SoCs where :yellow_circle: and :green_circle: denote preview status and support, respectively. In preview status the build is not yet enabled and some crucial parts could be missing (like documentation, datasheet). Please use an ESP-IDF release where the desired SoC is already supported.

|Chip         |     v3.3       |      v4.0      |       v4.1     |     v4.2       |     v4.3       |     v4.4       |                                                            |
|:----------- |:-------------: | :-------------:| :-------------:| :-------------:| :-------------:| :-------------:|:---------------------------------------------------------- |
|ESP32        | :green_circle: | :green_circle: | :green_circle: | :green_circle: | :green_circle: | :green_circle: |                                                            |
|ESP32-S2     |                |                |                | :green_circle: | :green_circle: | :green_circle: |                                                            |
|ESP32-C3     |                |                |                |                | :green_circle: | :green_circle: |                                                            |
|ESP32-S3     |                |                |                |                | :yellow_circle:| :green_circle: | [Announcement](https://www.espressif.com/en/news/ESP32_S3) |
|ESP32-H2     |                |                |                |                |                | :yellow_circle:| [Announcement](https://www.espressif.com/en/news/ESP32_H2) |

Espressif SoCs released before 2016 (ESP8266 and ESP8285) are supported by [RTOS SDK](https://github.com/espressif/ESP8266_RTOS_SDK) instead.

# Developing With ESP-IDF

## Setting Up ESP-IDF

See https://idf.espressif.com/ for links to detailed instructions on how to set up the ESP-IDF depending on chip you use.

**Note:** Each SoC series and each ESP-IDF release has its own documentation. Please see Section [Versions](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/versions.html) on how to find documentation and how to checkout specific release of ESP-IDF.

### Non-GitHub forks

ESP-IDF uses relative locations as its submodules URLs ([.gitmodules](.gitmodules)). So they link to GitHub.
If ESP-IDF is forked to a Git repository which is not on GitHub, you will need to run the script
[tools/set-submodules-to-github.sh](tools/set-submodules-to-github.sh) after git clone.
The script sets absolute URLs for all submodules, allowing `git submodule update --init --recursive` to complete.
If cloning ESP-IDF from GitHub, this step is not needed.

## Finding a Project

As well as the [esp-idf-template](https://github.com/espressif/esp-idf-template) project mentioned in Getting Started, ESP-IDF comes with some example projects in the [examples](examples) directory.

Once you've found the project you want to work with, change to its directory and you can configure and build it.

To start your own project based on an example, copy the example project directory outside of the ESP-IDF directory.

# Quick Reference

See the Getting Started guide links above for a detailed setup guide. This is a quick reference for common commands when working with ESP-IDF projects:

## Setup Build Environment

(See the Getting Started guide listed above for a full list of required steps with more details.)

* Install host build dependencies mentioned in the Getting Started guide.
* Run the install script to set up the build environment. The options include `install.bat` or `install.ps1` for Windows, and `install.sh` or `install.fish` for Unix shells.
* Run the export script on Windows (`export.bat`) or source it on Unix (`source export.sh`) in every shell environment before using ESP-IDF.

## Configuring the Project

* `idf.py set-target <chip_name>` sets the target of the project to `<chip_name>`. Run `idf.py set-target` without any arguments to see a list of supported targets.
* `idf.py menuconfig` opens a text-based configuration menu where you can configure the project.

## Compiling the Project

`idf.py build`

... will compile app, bootloader and generate a partition table based on the config.

## Flashing the Project

When the build finishes, it will print a command line to use esptool.py to flash the chip. However you can also do this automatically by running:

`idf.py -p PORT flash`

Replace PORT with the name of your serial port (like `COM3` on Windows, `/dev/ttyUSB0` on Linux, or `/dev/cu.usbserial-X` on MacOS. If the `-p` option is left out, `idf.py flash` will try to flash the first available serial port.

This will flash the entire project (app, bootloader and partition table) to a new chip. The settings for serial port flashing can be configured with `idf.py menuconfig`.

You don't need to run `idf.py build` before running `idf.py flash`, `idf.py flash` will automatically rebuild anything which needs it.

## Viewing Serial Output

The `idf.py monitor` target uses the [idf_monitor tool](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/idf-monitor.html) to display serial output from Espressif SoCs. idf_monitor also has a range of features to decode crash output and interact with the device. [Check the documentation page for details](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/idf-monitor.html).

Exit the monitor by typing Ctrl-].

To build, flash and monitor output in one pass, you can run:

`idf.py flash monitor`

## Compiling & Flashing Only the App

After the initial flash, you may just want to build and flash just your app, not the bootloader and partition table:

* `idf.py app` - build just the app.
* `idf.py app-flash` - flash just the app.

`idf.py app-flash` will automatically rebuild the app if any source files have changed.

(In normal development there's no downside to reflashing the bootloader and partition table each time, if they haven't changed.)

## Erasing Flash

The `idf.py flash` target does not erase the entire flash contents. However it is sometimes useful to set the device back to a totally erased state, particularly when making partition table changes or OTA app updates. To erase the entire flash, run `idf.py erase_flash`.

This can be combined with other targets, ie `idf.py -p PORT erase_flash flash` will erase everything and then re-flash the new app, bootloader and partition table.

# Resources

* Documentation for the latest version: https://docs.espressif.com/projects/esp-idf/. This documentation is built from the [docs directory](docs) of this repository.

* The [esp32.com forum](https://esp32.com/) is a place to ask questions and find community resources.

* [Check the Issues section on github](https://github.com/espressif/esp-idf/issues) if you find a bug or have a feature request. Please check existing Issues before opening a new one.

* If you're interested in contributing to ESP-IDF, please check the [Contributions Guide](https://docs.espressif.com/projects/esp-idf/en/latest/contribute/index.html).
