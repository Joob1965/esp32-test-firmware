# ESP32-CAM Remote (Android)

Android companion app for the AI-Thinker ESP32-CAM firmware in the parent
folder. It displays the camera's MJPEG stream and opens its settings page.

## Use

1. Connect the Pixel 9 to the same Wi-Fi network as the ESP32-CAM.
2. Enter the camera's IP address or hostname in the app.
3. Tap **Verbinden** to view `http://<camera-host>/stream`.
4. Tap **Einstellungen** to open `http://<camera-host>:81/`.

The last camera address is saved on the phone. The app does not send camera
addresses or data to a cloud service. The camera and phone must be able to
communicate directly over the local network.

## Build an APK

With the Android SDK installed, run `gradlew.bat assembleDebug` from this
folder. The generated APK is under `app/build/outputs/apk/debug/`.

To install it on a Pixel 9, copy `app/build/outputs/apk/debug/app-debug.apk`
to the phone, open the file, and confirm Android's install prompt.

The app uses plain HTTP because that is what the current ESP32-CAM firmware
serves. It is intended for a trusted home network, not for exposure to the
public internet.
