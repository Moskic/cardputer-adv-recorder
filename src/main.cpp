#include <Arduino.h>

#include "recorder/recorder_app.h"

namespace {

cardputer_recorder::RecorderApp application;

}  // namespace

void setup()
{
    application.begin();
}

void loop()
{
    application.update();
}
