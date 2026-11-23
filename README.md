# Modular ESP32 Firmware

Modular PlatformIO firmware for the AI-Thinker ESP32-CAM, ESP32 DevKit/WROOM,
ESP32-S2, ESP32-S3, and ESP32-C3. Select the matching board environment in
PlatformIO to build or flash that board.

## Endpoints

- MJPEG live stream on port 80: `http://<camera-ip>/stream`
- Settings page on port 81: `http://<camera-ip>:81/`
- Opening `http://<camera-ip>/` also opens the stream.

The settings page adjusts resolution, JPEG quality, brightness, contrast, and
saturation. The camera defaults to SVGA with PSRAM and VGA without PSRAM.

## Modular firmware profiles

Select a board profile from PlatformIO's project environment menu, or pass its
name to the CLI:

| Environment | Board | Included profile |
| --- | --- | --- |
| `esp32cam` | AI-Thinker ESP32-CAM | Camera stream, web config, MQTT, OTA, serial |
| `esp32dev` | ESP32 DevKit / WROOM-32 | Web config, GPIO, ADC, MQTT, OTA, serial |
| `esp32s2` | ESP32-S2 Saola-1 | Web config, GPIO, ADC, MQTT, OTA, serial |
| `esp32s3` | ESP32-S3 DevKitC-1 | Web config, GPIO, ADC, MQTT, OTA, serial |
| `esp32c3` | ESP32-C3 DevKitM-1 | Web config, GPIO, ADC, MQTT, OTA, serial |
| `esp32c6` | ESP32-C6 DevKitC-1 | Web config, GPIO, ADC, MQTT, OTA, serial |
| `shelly-plus-1` | Shelly Plus 1 (ESP32) | Relay GPIO26, external switch GPIO4, button GPIO25, status LED GPIO0, raw ADC GPIO32 |
| `shelly-plus-1-mini` | Shelly Plus 1 Mini (ESP32-C3) | Relay GPIO7, switch GPIO10, button GPIO1, status LED GPIO0 |
| `shelly-plus-1pm-mini` | Shelly Plus 1PM Mini (ESP32-C3) | Relay GPIO5, switch GPIO10, raw ADC GPIO3 |
| `sonoff-dual-r3` | Sonoff Dual R3 (ESP32) | Two relays GPIO27/GPIO14, button GPIO0, switches GPIO32/GPIO33, status LED GPIO13 |
| `sonoff-dual-r3-lite` | Sonoff Dual R3 Lite (ESP32) | Two relays GPIO27/GPIO14, button GPIO0, switches GPIO32/GPIO33, status LED GPIO13 |

Build and flash the selected target, for example:

```powershell
pio run -e esp32dev -t upload
pio device monitor -e esp32dev
```

The Shelly Plus 1 build is also copied to
[`firmware/Shelly_Plus_1.bin`](./firmware/Shelly_Plus_1.bin). It is based on
published pin maps, has not been tested on the physical device, and does not
provide calibrated temperature protection for the onboard NTC. Do not use it
to control mains loads until the exact hardware revision, safety behavior,
and installation method have been verified. Some Shelly firmware versions
also restrict OTA flashing.

The Shelly Mini and Sonoff Dual R3 profiles use published community pin maps.
Their relay outputs, power-meter interfaces, and mains safety have not been
hardware-tested. The web interface is German; firmware setup and error
messages are also shown in German where applicable.

In VS Code, choose the matching environment in the PlatformIO project tasks
before building or uploading. The profiles are compiled separately; select the
profile matching the exact board connected to USB.

Shared feature switches and board-specific GPIO/ADC pins are in
[`platformio.ini`](./platformio.ini). Camera is only enabled for the AI-Thinker
profile. The sensor module currently reads one configurable analog input; a
specific sensor model/driver can be added later. Matter is deliberately not
included in this first modular version.

## Wi-Fi, web config, and MQTT

The firmware first uses settings saved on the module. If none are saved, it
uses `WIFI_SSID` and `WIFI_PASSWORD` from `include/secrets.h`. If it cannot
connect, it starts a setup access point named `ESP32-Setup-<MAC suffix>`. The
password is `setup<MAC suffix>`; the serial monitor prints the exact network
name and password. Open `http://192.168.4.1/` for general boards or
`http://192.168.4.1:81/` for the camera profile to configure Wi-Fi.

The web page lets you configure the device name, Wi-Fi, MQTT broker and OTA
password. Leave password fields blank to keep their saved values. MQTT topics
use `<device>/cmnd/GPIO<pin>` (`ON`/`OFF`) for commands and
`<device>/tele/GPIO<pin>` for retained output state; analog readings publish
to `<device>/tele/ADC<pin>`. OTA is enabled only after setting an OTA password.

GPIO outputs start LOW and only the pins listed for the selected profile are
exposed in the web UI/MQTT. Check the actual board pinout before connecting
hardware. The ESP32-CAM profile leaves GPIO outputs disabled because most pins
are shared with the camera, microSD, flash LED, or JTAG.

`include/secrets.h` is ignored by Git so Wi-Fi credentials are not committed.
The local HTTP configuration page is unencrypted; use it only on a trusted
home network.

The Shelly Plus 1 pin map is based on the
[ESPHome device reference](https://devices.esphome.io/devices/shelly-plus-1/)
and the [Tasmota device template](https://templates.blakadder.com/shelly_plus_1.html).

## Build and flash only the ESP32-CAM

Select `esp32cam` in PlatformIO and build/upload as described above. The camera
stream stays at `http://<camera-ip>/stream` on port 80; the settings page is at
`http://<camera-ip>:81/`. Building this environment also copies the firmware to
`firmware/ESP32_CAM.bin` for use with the Android companion app.

## JTAG debugging with ESP-Prog

The regular `esp32cam` environment keeps serial flashing unchanged. The separate
`esp32cam-debug` environment enables source-level debugging through ESP-Prog:
its output is kept separate as `firmware/ESP32_CAM_DEBUG.bin`.

```powershell
pio debug -e esp32cam-debug
```

Connect the ESP-Prog JTAG pins to the AI-Thinker ESP32-CAM:

| ESP-Prog | ESP32-CAM |
| --- | --- |
| TMS | GPIO14 |
| TCK | GPIO13 |
| TDO | GPIO15 |
| TDI | GPIO12 |
| GND | GND |
| VDD reference | 3V3 |

Power the camera separately using its normal 5V input; do not power it from the
ESP-Prog. GPIO12–GPIO15 are also used by the microSD interface, so do not use
the SD card while debugging. Keep the existing serial upload method unless
JTAG firmware uploading is specifically needed.

## Home Assistant

The AI-Thinker ESP32-CAM firmware exposes an HTTP MJPEG stream, not a Matter
camera endpoint. In Home Assistant, add a Generic Camera and use
`http://<camera-ip>/stream` as its still-image/stream source as supported by
your Home Assistant version.

Matter camera streaming uses the Matter 1.5 camera clusters and WebRTC. The
official Espressif implementation currently targets ESP32-P4 hardware (and
ESP32-C6 in its split-mode setup); it is not supported by this AI-Thinker
ESP32-CAM firmware.

## Android app

The companion app for Google Pixel and other Android phones is in
[`mobile-app/`](./mobile-app/). It views the MJPEG stream and opens the camera
settings page over the same local Wi-Fi network.
