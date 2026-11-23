#pragma once

#include <Arduino.h>

#if FEATURE_CAMERA
bool cameraBegin();
bool cameraStartStreamServer();
void cameraApplySettings(int frameSize, int quality, int brightness,
                         int contrast, int saturation);
#endif
