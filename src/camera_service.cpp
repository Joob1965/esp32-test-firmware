#include "feature_config.h"

#if FEATURE_CAMERA

#include "camera_service.h"

#include <esp_camera.h>
#include <esp_http_server.h>

namespace {

constexpr int kCameraPwdn = 32;
constexpr int kCameraReset = -1;
constexpr int kCameraXclk = 0;
constexpr int kCameraSiod = 26;
constexpr int kCameraSioc = 27;
constexpr int kCameraY9 = 35;
constexpr int kCameraY8 = 34;
constexpr int kCameraY7 = 39;
constexpr int kCameraY6 = 36;
constexpr int kCameraY5 = 21;
constexpr int kCameraY4 = 19;
constexpr int kCameraY3 = 18;
constexpr int kCameraY2 = 5;
constexpr int kCameraVsync = 25;
constexpr int kCameraHref = 23;
constexpr int kCameraPclk = 22;

constexpr char kStreamContentType[] =
    "multipart/x-mixed-replace;boundary=frame";
constexpr char kStreamBoundary[] = "\r\n--frame\r\n";

httpd_handle_t streamServer = nullptr;

esp_err_t streamHandler(httpd_req_t* request) {
  httpd_resp_set_type(request, kStreamContentType);
  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");

  while (true) {
    camera_fb_t* frame = esp_camera_fb_get();
    if (frame == nullptr) {
      Serial.println("Camera frame capture failed");
      return ESP_FAIL;
    }

    char header[96];
    const int headerLength = snprintf(
        header, sizeof(header),
        "%sContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
        kStreamBoundary, static_cast<unsigned int>(frame->len));

    const esp_err_t headerResult =
        httpd_resp_send_chunk(request, header, headerLength);
    const esp_err_t frameResult =
        headerResult == ESP_OK
            ? httpd_resp_send_chunk(request,
                                    reinterpret_cast<const char*>(frame->buf),
                                    frame->len)
            : headerResult;
    const esp_err_t endingResult =
        frameResult == ESP_OK
            ? httpd_resp_send_chunk(request, "\r\n", 2)
            : frameResult;

    esp_camera_fb_return(frame);
    if (endingResult != ESP_OK) {
      return endingResult;
    }
  }
}

}  // namespace

bool cameraBegin() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = kCameraY2;
  config.pin_d1 = kCameraY3;
  config.pin_d2 = kCameraY4;
  config.pin_d3 = kCameraY5;
  config.pin_d4 = kCameraY6;
  config.pin_d5 = kCameraY7;
  config.pin_d6 = kCameraY8;
  config.pin_d7 = kCameraY9;
  config.pin_xclk = kCameraXclk;
  config.pin_pclk = kCameraPclk;
  config.pin_vsync = kCameraVsync;
  config.pin_href = kCameraHref;
  config.pin_sccb_sda = kCameraSiod;
  config.pin_sccb_scl = kCameraSioc;
  config.pin_pwdn = kCameraPwdn;
  config.pin_reset = kCameraReset;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = psramFound() ? FRAMESIZE_SVGA : FRAMESIZE_VGA;
  config.jpeg_quality = 12;
  config.fb_count = psramFound() ? 2 : 1;
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  const esp_err_t result = esp_camera_init(&config);
  if (result != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", result);
    return false;
  }
  return true;
}

bool cameraStartStreamServer() {
  if (streamServer != nullptr) {
    return true;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.ctrl_port = 32768;
  config.max_open_sockets = 4;
  config.lru_purge_enable = true;

  if (httpd_start(&streamServer, &config) != ESP_OK) {
    Serial.println("Could not start camera stream server on port 80");
    streamServer = nullptr;
    return false;
  }

  httpd_uri_t root = {};
  root.uri = "/";
  root.method = HTTP_GET;
  root.handler = streamHandler;
  httpd_uri_t stream = {};
  stream.uri = "/stream";
  stream.method = HTTP_GET;
  stream.handler = streamHandler;

  if (httpd_register_uri_handler(streamServer, &root) != ESP_OK ||
      httpd_register_uri_handler(streamServer, &stream) != ESP_OK) {
    Serial.println("Could not register camera stream routes");
    httpd_stop(streamServer);
    streamServer = nullptr;
    return false;
  }
  return true;
}

void cameraApplySettings(int frameSize, int quality, int brightness,
                         int contrast, int saturation) {
  sensor_t* sensor = esp_camera_sensor_get();
  if (sensor == nullptr) {
    return;
  }

  if (frameSize == 5 || frameSize == 6 || frameSize == 8 || frameSize == 10) {
    sensor->set_framesize(sensor, static_cast<framesize_t>(frameSize));
  }
  if (quality >= 10 && quality <= 63) {
    sensor->set_quality(sensor, quality);
  }
  if (brightness >= -2 && brightness <= 2) {
    sensor->set_brightness(sensor, brightness);
  }
  if (contrast >= -2 && contrast <= 2) {
    sensor->set_contrast(sensor, contrast);
  }
  if (saturation >= -2 && saturation <= 2) {
    sensor->set_saturation(sensor, saturation);
  }
}

#endif
