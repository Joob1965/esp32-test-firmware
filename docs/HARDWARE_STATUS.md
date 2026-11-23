# Hardware Status

This project contains three kinds of targets:

1. **Compile-verified development profiles** for ESP32, ESP32-CAM, ESP32-S2,
   ESP32-S3 and ESP32-C6 boards.
2. **Community pin-map profiles** for selected devices. These are built and
   compile-checked but remain untested until flashed and validated on the exact
   hardware revision.
3. **Inventoried devices** for which no evidence-backed profile or named binary
   exists yet.

## Named firmware

| Firmware | Profile status | Safety and functional status |
| --- | --- | --- |
| `Shelly_Plus_1.bin` | Compile-verified | Relay pin map present; NTC not calibrated; mains device |
| `Shelly_Plus_1_Mini.bin` | Compile-verified | Relay pin map present; NTC not implemented as a temperature sensor |
| `Shelly_Plus_1PM_Mini.bin` | Compile-verified | Relay pin map present; BL0942 meter not implemented |
| `Sonoff_Dual_R3.bin` | Compile-verified | Dual relay and switches present; CSE7761/BL0939 meter not implemented |
| `Sonoff_Dual_R3_Lite.bin` | Compile-verified | Dual relay and switches present |

## Safety

Do not install or flash a mains-connected device unless the exact PCB revision,
boot mode, GPIO map, relay behavior, flash layout, and factory safety features
have been verified. Keep mains power disconnected during flashing and probing.
The web interface uses local unencrypted HTTP. OTA requires an explicit password
and should be used only on a trusted network.
