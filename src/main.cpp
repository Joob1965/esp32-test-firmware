#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>

#include "feature_config.h"
#include "secrets.h"

#if FEATURE_CAMERA
#include "camera_service.h"
#endif

namespace {

constexpr char kSettingsNamespace[] = "espfirmware";
constexpr unsigned long kWifiTimeoutMs = 15000;
constexpr unsigned long kMqttRetryMs = 5000;
constexpr unsigned long kSensorPublishMs = 10000;
constexpr unsigned long kInputDebounceMs = 40;
constexpr int kSettingsPort = FEATURE_CAMERA ? 81 : 80;

struct DebouncedInput {
  int pin = -1;
  bool stableState = false;
  bool lastReading = false;
  unsigned long lastChange = 0;
};

struct Settings {
  String wifiSsid;
  String wifiPassword;
  String deviceName;
  String mqttHost;
  uint16_t mqttPort = 1883;
  String mqttUser;
  String mqttPassword;
  String otaPassword;
};

Settings settings;
Preferences preferences;
WebServer webServer(kSettingsPort);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
bool setupAccessPointActive = false;
#if FEATURE_CAMERA
bool cameraReady = false;
bool cameraStreamStarted = false;
#endif
bool otaStarted = false;
unsigned long lastMqttAttempt = 0;
bool gpioState1 = false;
bool gpioState2 = false;
#if FEATURE_GPIO && GPIO_SWITCH_INPUT >= 0
DebouncedInput switchInput;
#endif
#if FEATURE_GPIO && GPIO_BUTTON_INPUT >= 0
DebouncedInput buttonInput;
#endif
#if FEATURE_GPIO && GPIO_SWITCH_INPUT_1 >= 0
DebouncedInput switchInput1;
#endif
#if FEATURE_GPIO && GPIO_SWITCH_INPUT_2 >= 0
DebouncedInput switchInput2;
#endif
#if FEATURE_MQTT && FEATURE_SENSORS && SENSOR_ADC_PIN >= 0
unsigned long lastSensorPublish = 0;
#endif

String htmlEscape(const String& input) {
  String escaped;
  escaped.reserve(input.length());
  for (const char character : input) {
    switch (character) {
      case '&':
        escaped += F("&amp;");
        break;
      case '<':
        escaped += F("&lt;");
        break;
      case '>':
        escaped += F("&gt;");
        break;
      case '"':
        escaped += F("&quot;");
        break;
      case '\'':
        escaped += F("&#39;");
        break;
      default:
        escaped += character;
        break;
    }
  }
  return escaped;
}

String devicePrefix() {
  return settings.deviceName.length() == 0 ? F("esp32") : settings.deviceName;
}

String deviceMacSuffix() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  return mac.substring(mac.length() > 6 ? mac.length() - 6 : 0);
}

void loadSettings() {
  preferences.begin(kSettingsNamespace, true);
  settings.wifiSsid = preferences.getString("ssid", WIFI_SSID);
  settings.wifiPassword = preferences.getString("wifiPass", WIFI_PASSWORD);
  settings.deviceName = preferences.getString("device", "");
  settings.mqttHost = preferences.getString("mqttHost", "");
  settings.mqttPort = preferences.getUShort("mqttPort", 1883);
  settings.mqttUser = preferences.getString("mqttUser", "");
  settings.mqttPassword = preferences.getString("mqttPass", "");
  settings.otaPassword = preferences.getString("otaPass", "");
  preferences.end();

  if (settings.deviceName.length() == 0) {
    settings.deviceName = DEVICE_NAME_DEFAULT;
    if (settings.deviceName == "esp32") {
      settings.deviceName += "-" + deviceMacSuffix();
    }
  }
}

bool connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  if (settings.wifiSsid.length() == 0) {
    return false;
  }

  WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPassword.c_str());
  Serial.printf("Connecting to configured Wi-Fi: %s\n",
                settings.wifiSsid.c_str());

  const unsigned long deadline = millis() + kWifiTimeoutMs;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

void startSetupAccessPoint() {
  const String suffix = deviceMacSuffix();
  const String apName = "ESP32-Setup-" + suffix;
  const String apPassword = "setup" + suffix;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apName.c_str(), apPassword.c_str());
  setupAccessPointActive = true;
  Serial.printf("Wi-Fi setup AP: %s\n", apName.c_str());
  Serial.printf("Setup AP password: %s\n", apPassword.c_str());
  Serial.printf("Open http://%s:%d/ to configure this device\n",
                WiFi.softAPIP().toString().c_str(), kSettingsPort);
}

bool gpioIsAvailable(int pin) {
#if FEATURE_GPIO
  return pin == GPIO_OUTPUT_1 || pin == GPIO_OUTPUT_2;
#else
  (void)pin;
  return false;
#endif
}

void writeGpio(int pin, bool state) {
#if FEATURE_GPIO
  if (pin == GPIO_OUTPUT_1 && GPIO_OUTPUT_1 >= 0) {
    gpioState1 = state;
    digitalWrite(GPIO_OUTPUT_1, state ? HIGH : LOW);
  } else if (pin == GPIO_OUTPUT_2 && GPIO_OUTPUT_2 >= 0) {
    gpioState2 = state;
    digitalWrite(GPIO_OUTPUT_2, state ? HIGH : LOW);
  }
#if GPIO_STATUS_LED >= 0
  if (pin == GPIO_OUTPUT_1 && GPIO_OUTPUT_1 >= 0) {
    const bool ledOn = state;
    digitalWrite(GPIO_STATUS_LED,
                 ledOn != static_cast<bool>(GPIO_STATUS_LED_INVERTED) ? HIGH
                                                                      : LOW);
  }
#endif
#else
  (void)pin;
  (void)state;
#endif
}

void publishGpioState() {
#if FEATURE_MQTT && FEATURE_GPIO
  if (!mqttClient.connected()) {
    return;
  }
  char topic[96];
  if (GPIO_OUTPUT_1 >= 0) {
    snprintf(topic, sizeof(topic), "%s/tele/GPIO%d",
             devicePrefix().c_str(), GPIO_OUTPUT_1);
    mqttClient.publish(topic, gpioState1 ? "ON" : "OFF", true);
  }
  if (GPIO_OUTPUT_2 >= 0) {
    snprintf(topic, sizeof(topic), "%s/tele/GPIO%d",
             devicePrefix().c_str(), GPIO_OUTPUT_2);
    mqttClient.publish(topic, gpioState2 ? "ON" : "OFF", true);
  }
#endif
}

void mqttMessageHandler(char* topic, uint8_t* payload, unsigned int length) {
#if FEATURE_MQTT && FEATURE_GPIO
  const String receivedTopic(topic);
  String command;
  command.reserve(length);
  for (unsigned int index = 0; index < length; ++index) {
    command += static_cast<char>(payload[index]);
  }
  command.trim();
  command.toUpperCase();
  if (command != "ON" && command != "OFF" && command != "1" &&
      command != "0") {
    return;
  }
  const bool state = command == "ON" || command == "1";

  if (GPIO_OUTPUT_1 >= 0 &&
      receivedTopic == devicePrefix() + "/cmnd/GPIO" +
                           String(GPIO_OUTPUT_1)) {
    writeGpio(GPIO_OUTPUT_1, state);
  } else if (GPIO_OUTPUT_2 >= 0 &&
             receivedTopic == devicePrefix() + "/cmnd/GPIO" +
                                 String(GPIO_OUTPUT_2)) {
    writeGpio(GPIO_OUTPUT_2, state);
  }
  publishGpioState();
#else
  (void)topic;
  (void)payload;
  (void)length;
#endif
}

void configureGpio() {
#if FEATURE_GPIO
  if (GPIO_OUTPUT_1 >= 0) {
    pinMode(GPIO_OUTPUT_1, OUTPUT);
    digitalWrite(GPIO_OUTPUT_1, LOW);
  }
  if (GPIO_OUTPUT_2 >= 0 && GPIO_OUTPUT_2 != GPIO_OUTPUT_1) {
    pinMode(GPIO_OUTPUT_2, OUTPUT);
    digitalWrite(GPIO_OUTPUT_2, LOW);
  }
#if GPIO_STATUS_LED >= 0
  pinMode(GPIO_STATUS_LED, OUTPUT);
  digitalWrite(GPIO_STATUS_LED, GPIO_STATUS_LED_INVERTED ? HIGH : LOW);
#endif
#if GPIO_SWITCH_INPUT >= 0
  switchInput.pin = GPIO_SWITCH_INPUT;
  pinMode(GPIO_SWITCH_INPUT, INPUT_PULLUP);
  switchInput.lastReading = digitalRead(GPIO_SWITCH_INPUT) == LOW;
  switchInput.stableState = switchInput.lastReading;
  switchInput.lastChange = millis();
#endif
#if GPIO_BUTTON_INPUT >= 0
  buttonInput.pin = GPIO_BUTTON_INPUT;
  pinMode(GPIO_BUTTON_INPUT, INPUT_PULLUP);
  buttonInput.lastReading = digitalRead(GPIO_BUTTON_INPUT) == LOW;
  buttonInput.stableState = buttonInput.lastReading;
  buttonInput.lastChange = millis();
#endif
#if GPIO_SWITCH_INPUT_1 >= 0
  switchInput1.pin = GPIO_SWITCH_INPUT_1;
  pinMode(GPIO_SWITCH_INPUT_1, INPUT_PULLUP);
  switchInput1.lastReading = digitalRead(GPIO_SWITCH_INPUT_1) == LOW;
  switchInput1.stableState = switchInput1.lastReading;
  switchInput1.lastChange = millis();
#endif
#if GPIO_SWITCH_INPUT_2 >= 0
  switchInput2.pin = GPIO_SWITCH_INPUT_2;
  pinMode(GPIO_SWITCH_INPUT_2, INPUT_PULLUP);
  switchInput2.lastReading = digitalRead(GPIO_SWITCH_INPUT_2) == LOW;
  switchInput2.stableState = switchInput2.lastReading;
  switchInput2.lastChange = millis();
#endif
#endif
}

bool inputChanged(DebouncedInput& input) {
  const bool reading = digitalRead(input.pin) == LOW;
  if (reading != input.lastReading) {
    input.lastReading = reading;
    input.lastChange = millis();
  }
  if (input.lastReading == input.stableState ||
      millis() - input.lastChange < kInputDebounceMs) {
    return false;
  }
  input.stableState = input.lastReading;
  return true;
}

void handleDeviceInputs() {
#if FEATURE_GPIO && GPIO_SWITCH_INPUT >= 0 && GPIO_OUTPUT_1 >= 0
  if (inputChanged(switchInput) && switchInput.stableState) {
    writeGpio(GPIO_OUTPUT_1, !gpioState1);
    publishGpioState();
  }
#endif
#if FEATURE_GPIO && GPIO_BUTTON_INPUT >= 0 && GPIO_OUTPUT_1 >= 0
  if (inputChanged(buttonInput) && buttonInput.stableState) {
    writeGpio(GPIO_OUTPUT_1, !gpioState1);
    publishGpioState();
  }
#endif
#if FEATURE_GPIO && GPIO_SWITCH_INPUT_1 >= 0 && GPIO_OUTPUT_1 >= 0
  if (inputChanged(switchInput1)) {
    writeGpio(GPIO_OUTPUT_1, switchInput1.stableState);
    publishGpioState();
  }
#endif
#if FEATURE_GPIO && GPIO_SWITCH_INPUT_2 >= 0 && GPIO_OUTPUT_2 >= 0
  if (inputChanged(switchInput2)) {
    writeGpio(GPIO_OUTPUT_2, switchInput2.stableState);
    publishGpioState();
  }
#endif
}

String gpioControlsHtml() {
  String html;
#if FEATURE_GPIO
  const int pins[] = {GPIO_OUTPUT_1, GPIO_OUTPUT_2};
  const bool states[] = {gpioState1, gpioState2};
  for (int index = 0; index < 2; ++index) {
    const int pin = pins[index];
    if (pin < 0 || (index == 1 && pin == pins[0])) {
      continue;
    }
    html += "<form class=\"row\" method=\"post\" action=\"/gpio\"><b>Ausgang GPIO ";
    html += String(pin);
    html += states[index] ? " — EIN</b>" : " — AUS</b>";
    html += "<input type=\"hidden\" name=\"pin\" value=\"";
    html += String(pin);
    html += "\"><input type=\"hidden\" name=\"value\" value=\"";
    html += states[index] ? "0" : "1";
    html += "\"><button>Schalten</button></form>";
  }
#endif
  if (html.length() == 0) {
    html = "<p>Für dieses Profil sind keine Schaltausgänge konfiguriert.</p>";
  }
  return html;
}

String cameraControlsHtml() {
#if FEATURE_CAMERA
  return F(
      "<h2>Kamera</h2>"
      "<p><a target=\"_blank\" id=\"stream-link\">MJPEG-Stream auf Port 80 "
      "öffnen</a></p>"
      "<img class=\"preview\" id=\"preview\" alt=\"Livebild\">"
      "<form method=\"post\" action=\"/camera\">"
      "<label>Auflösung <select name=\"framesize\">"
      "<option value=\"5\">VGA</option><option value=\"6\" selected>SVGA"
      "</option><option value=\"8\">XGA</option><option value=\"10\">UXGA"
      "</option></select></label>"
      "<label>JPEG-Qualität <input name=\"quality\" type=\"number\" "
      "min=\"10\" max=\"63\" value=\"12\"></label>"
      "<label>Helligkeit <input name=\"brightness\" type=\"number\" "
      "min=\"-2\" max=\"2\" value=\"0\"></label>"
      "<label>Kontrast <input name=\"contrast\" type=\"number\" "
      "min=\"-2\" max=\"2\" value=\"0\"></label>"
      "<label>Sättigung <input name=\"saturation\" type=\"number\" "
      "min=\"-2\" max=\"2\" value=\"0\"></label>"
      "<button>Kamera anwenden</button></form>");
#else
  return "";
#endif
}

String configPage() {
  String page;
  page.reserve(6000);
  page += F(
      "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<title>ESP32-Gerätefirmware</title><style>"
      "body{font:16px system-ui,sans-serif;max-width:760px;margin:2rem auto;"
      "padding:0 1rem;color:#172033;background:#f4f6fa}"
      "h1{color:#174a7e}.card{background:white;padding:1rem;margin:1rem 0;"
      "border-radius:12px;box-shadow:0 2px 8px #0001}"
      "label{display:block;margin:.8rem 0}.row{display:flex;gap:1rem;"
      "align-items:center;margin:.7rem 0}input,select,button{font:inherit;"
      "padding:.55rem;border-radius:6px;border:1px solid #aab4c3}"
      "input{box-sizing:border-box;width:100%}button{background:#1769aa;"
      "color:white;border:0}img.preview{display:block;width:100%;"
      "max-width:640px;margin-top:1rem;background:#222}"
      ".muted{color:#56637a;font-size:.9rem}</style></head><body>"
      "<h1>ESP32-Gerätefirmware</h1><p class=\"muted\">Gerät: ");
  page += htmlEscape(devicePrefix());
  page += F("</p><section class=\"card\"><h2>Gerätestatus</h2><p>");
  if (WiFi.status() == WL_CONNECTED) {
    page += "WLAN verbunden — ";
    page += WiFi.localIP().toString();
  } else if (setupAccessPointActive) {
    page += "Einrichtungs-WLAN aktiv — ";
    page += WiFi.softAPIP().toString();
  } else {
    page += "WLAN nicht verbunden";
  }
  page += "</p><p>MQTT-Verbindung: ";
#if FEATURE_MQTT
  page += mqttClient.connected() ? "verbunden" : "nicht verbunden";
#else
  page += "in diesem Profil deaktiviert";
#endif
  page += "</p>";
#if FEATURE_SENSORS && SENSOR_ADC_PIN >= 0
  page += "<p>Analogeingang GPIO ";
  page += String(SENSOR_ADC_PIN);
  page += ": ";
  page += String(analogRead(SENSOR_ADC_PIN));
  page += "</p>";
#endif
  page += F("</section><section class=\"card\"><h2>Schaltausgänge</h2>");
  page += gpioControlsHtml();
  page += F("</section>");
  page += cameraControlsHtml();
  page += F(
      "<section class=\"card\"><h2>Gerät konfigurieren</h2>"
      "<form method=\"post\" action=\"/save\">"
      "<label>Gerätename<input name=\"device\" maxlength=\"31\" value=\"");
  page += htmlEscape(devicePrefix());
  page += F("\"></label><label>WLAN-Name (SSID)<input name=\"ssid\" "
            "maxlength=\"32\" value=\"");
  page += htmlEscape(settings.wifiSsid);
  page += F("\"></label><label>WLAN-Passwort<input name=\"wifiPass\" "
            "type=\"password\" autocomplete=\"new-password\" "
            "placeholder=\"leer = unverändert\"></label>");
#if FEATURE_MQTT
  page += F("<h3>MQTT</h3><label>Broker-Adresse<input name=\"mqttHost\" "
            "value=\"");
  page += htmlEscape(settings.mqttHost);
  page += F("\"></label><label>Broker-Port<input name=\"mqttPort\" "
            "type=\"number\" min=\"1\" max=\"65535\" value=\"");
  page += String(settings.mqttPort);
  page += F("\"></label><label>Benutzername<input name=\"mqttUser\" "
            "value=\"");
  page += htmlEscape(settings.mqttUser);
  page += F("\"></label><label>MQTT-Passwort<input name=\"mqttPass\" "
            "type=\"password\" autocomplete=\"new-password\" "
            "placeholder=\"leer = unverändert\"></label>");
#endif
#if FEATURE_OTA
  page += F("<h3>Firmware-Update über WLAN (OTA)</h3>"
            "<label>OTA-Passwort<input name=\"otaPass\" "
            "type=\"password\" autocomplete=\"new-password\" "
            "placeholder=\"leer = unverändert\"></label>");
#endif
  page += F("<button>Speichern und neu starten</button></form></section>"
            "<p class=\"muted\">Nur im vertrauenswürdigen Heimnetz verwenden. "
            "Die Weboberfläche nutzt unverschlüsseltes HTTP.</p>");
#if FEATURE_CAMERA
  page += F("<script>const s='http://'+location.hostname+'/stream';"
            "document.getElementById('preview').src=s;"
            "document.getElementById('stream-link').href=s;</script>");
#endif
  page += F("</body></html>");
  return page;
}

void handleRoot() {
  webServer.send(200, "text/html; charset=utf-8", configPage());
}

void handleGpio() {
#if FEATURE_GPIO
  const int pin = webServer.arg("pin").toInt();
  if (!gpioIsAvailable(pin)) {
    webServer.send(400, "text/plain; charset=utf-8",
                   "GPIO ist für dieses Profil nicht freigegeben.");
    return;
  }
  const String value = webServer.arg("value");
  if (value != "0" && value != "1") {
    webServer.send(400, "text/plain; charset=utf-8",
                   "GPIO-Wert muss 0 oder 1 sein.");
    return;
  }
  writeGpio(pin, value == "1");
  publishGpioState();
  webServer.sendHeader("Location", "/", true);
  webServer.send(303, "text/plain", "Weiterleitung");
#else
  webServer.send(404, "text/plain", "GPIO-Modul deaktiviert");
#endif
}

void handleSave() {
#if FEATURE_WEB_CONFIG
  const String newSsid = webServer.arg("ssid");
  if (newSsid.length() == 0 || newSsid.length() > 32) {
    webServer.send(400, "text/plain; charset=utf-8",
                   "Bitte eine gültige WLAN-SSID eingeben.");
    return;
  }

  String newDeviceName = webServer.arg("device");
  newDeviceName.trim();
  if (newDeviceName.length() == 0 || newDeviceName.length() > 31) {
    webServer.send(400, "text/plain; charset=utf-8",
                   "Bitte einen Gerätenamen mit 1 bis 31 Zeichen eingeben.");
    return;
  }

  String newWifiPassword = webServer.arg("wifiPass");
  if (newWifiPassword.length() == 0 && newSsid == settings.wifiSsid) {
    newWifiPassword = settings.wifiPassword;
  }

  preferences.begin(kSettingsNamespace, false);
  preferences.putString("ssid", newSsid);
  preferences.putString("wifiPass", newWifiPassword);
  preferences.putString("device", newDeviceName);

#if FEATURE_MQTT
  const String newMqttHost = webServer.arg("mqttHost");
  const String newMqttUser = webServer.arg("mqttUser");
  String newMqttPassword = webServer.arg("mqttPass");
  if (newMqttPassword.length() == 0 &&
      newMqttHost == settings.mqttHost &&
      newMqttUser == settings.mqttUser) {
    newMqttPassword = settings.mqttPassword;
  }
  const long parsedPort = webServer.arg("mqttPort").toInt();
  const uint16_t newMqttPort =
      parsedPort >= 1 && parsedPort <= 65535
          ? static_cast<uint16_t>(parsedPort)
          : settings.mqttPort;
  preferences.putString("mqttHost", newMqttHost);
  preferences.putUShort("mqttPort", newMqttPort);
  preferences.putString("mqttUser", newMqttUser);
  preferences.putString("mqttPass", newMqttPassword);
#endif

#if FEATURE_OTA
  const String newOtaPassword = webServer.arg("otaPass");
  preferences.putString(
      "otaPass",
      newOtaPassword.length() == 0 ? settings.otaPassword : newOtaPassword);
#endif
  preferences.end();

  webServer.send(200, "text/html; charset=utf-8",
                 "<!doctype html><meta charset=\"utf-8\"><p>Gespeichert. "
                 "Das Gerät startet neu.</p>");
  delay(500);
  ESP.restart();
#else
  webServer.send(404, "text/plain", "Web-Konfiguration deaktiviert");
#endif
}

#if FEATURE_CAMERA
void handleCameraSettings() {
  cameraApplySettings(webServer.arg("framesize").toInt(),
                      webServer.arg("quality").toInt(),
                      webServer.arg("brightness").toInt(),
                      webServer.arg("contrast").toInt(),
                      webServer.arg("saturation").toInt());
  webServer.sendHeader("Location", "/", true);
  webServer.send(303, "text/plain", "Weiterleitung");
}
#endif

void configureWebServer() {
  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/save", HTTP_POST, handleSave);
  webServer.on("/gpio", HTTP_POST, handleGpio);
#if FEATURE_CAMERA
  webServer.on("/camera", HTTP_POST, handleCameraSettings);
#endif
  webServer.onNotFound([]() {
    webServer.send(404, "text/plain; charset=utf-8", "Nicht gefunden");
  });
  webServer.begin();
  Serial.printf("Configuration page: http://%s:%d/\n",
                WiFi.localIP().toString().c_str(), kSettingsPort);
}

void configureOta() {
#if FEATURE_OTA
  if (otaStarted || WiFi.status() != WL_CONNECTED ||
      settings.otaPassword.length() == 0) {
    return;
  }

  ArduinoOTA.setHostname(devicePrefix().c_str());
  ArduinoOTA.setPassword(settings.otaPassword.c_str());
  ArduinoOTA.onStart([]() { Serial.println("OTA update started"); });
  ArduinoOTA.onEnd([]() { Serial.println("\nOTA update complete"); });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA update failed, error %u\n", error);
  });
  ArduinoOTA.begin();
  otaStarted = true;
  Serial.println("OTA enabled");
#endif
}

void configureMqtt() {
#if FEATURE_MQTT
  if (settings.mqttHost.length() == 0) {
    return;
  }
  mqttClient.setServer(settings.mqttHost.c_str(), settings.mqttPort);
  mqttClient.setCallback(mqttMessageHandler);
  mqttClient.setBufferSize(512);
#endif
}

void connectMqtt() {
#if FEATURE_MQTT
  if (WiFi.status() != WL_CONNECTED || settings.mqttHost.length() == 0 ||
      mqttClient.connected() || millis() - lastMqttAttempt < kMqttRetryMs) {
    return;
  }
  lastMqttAttempt = millis();

  const String clientId = devicePrefix() + "-" + deviceMacSuffix();
  const String willTopic = devicePrefix() + "/tele/LWT";
  bool connected;
  if (settings.mqttUser.length() > 0) {
    connected = mqttClient.connect(clientId.c_str(), settings.mqttUser.c_str(),
                                  settings.mqttPassword.c_str(),
                                  willTopic.c_str(), 0, true, "Offline");
  } else {
    connected = mqttClient.connect(clientId.c_str(), willTopic.c_str(), 0, true,
                                  "Offline");
  }
  if (!connected) {
    Serial.printf("MQTT connection failed, state %d\n", mqttClient.state());
    return;
  }

  String availabilityTopic = devicePrefix() + "/tele/LWT";
  mqttClient.publish(availabilityTopic.c_str(), "Online", true);
#if FEATURE_GPIO
  if (GPIO_OUTPUT_1 >= 0) {
    const String topic =
        devicePrefix() + "/cmnd/GPIO" + String(GPIO_OUTPUT_1);
    mqttClient.subscribe(topic.c_str());
  }
  if (GPIO_OUTPUT_2 >= 0 && GPIO_OUTPUT_2 != GPIO_OUTPUT_1) {
    const String topic =
        devicePrefix() + "/cmnd/GPIO" + String(GPIO_OUTPUT_2);
    mqttClient.subscribe(topic.c_str());
  }
#endif
  publishGpioState();
  Serial.println("MQTT connected");
#endif
}

void publishSensor() {
#if FEATURE_MQTT && FEATURE_SENSORS && SENSOR_ADC_PIN >= 0
  if (!mqttClient.connected() ||
      millis() - lastSensorPublish < kSensorPublishMs) {
    return;
  }
  lastSensorPublish = millis();
  char topic[96];
  char value[24];
  snprintf(topic, sizeof(topic), "%s/tele/ADC%d",
           devicePrefix().c_str(), SENSOR_ADC_PIN);
  snprintf(value, sizeof(value), "%d", analogRead(SENSOR_ADC_PIN));
  mqttClient.publish(topic, value, true);
#endif
}

void printAddresses() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Connected IP: %s\n", WiFi.localIP().toString().c_str());
#if FEATURE_CAMERA
    Serial.printf("Camera stream: http://%s/stream\n",
                  WiFi.localIP().toString().c_str());
#endif
    Serial.printf("Settings: http://%s:%d/\n",
                  WiFi.localIP().toString().c_str(), kSettingsPort);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  WiFi.mode(WIFI_STA);
  loadSettings();
  configureGpio();

#if FEATURE_CAMERA
  cameraReady = cameraBegin();
  if (!cameraReady) {
    Serial.println("Continuing without camera stream");
  }
#endif

  const bool wifiConnected = connectWifi();
  if (!wifiConnected) {
    startSetupAccessPoint();
  }

  configureWebServer();
  configureMqtt();
  configureOta();

#if FEATURE_CAMERA
  if (cameraReady && WiFi.status() == WL_CONNECTED) {
    cameraStreamStarted = cameraStartStreamServer();
  }
#endif
  printAddresses();
}

void loop() {
  webServer.handleClient();
  handleDeviceInputs();

  if (WiFi.status() == WL_CONNECTED) {
    setupAccessPointActive = false;
#if FEATURE_CAMERA
    if (cameraReady && !cameraStreamStarted) {
      cameraStreamStarted = cameraStartStreamServer();
    }
#endif
    configureOta();
    connectMqtt();
#if FEATURE_MQTT
    mqttClient.loop();
#endif
    publishSensor();
  }

#if FEATURE_OTA
  if (otaStarted) {
    ArduinoOTA.handle();
  }
#endif
  delay(2);
}
