#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "ble_hr_client.h"

namespace ui_hr {

void init(BleHrClient* client);
void refresh(const HrSnapshot& snap, uint32_t now_ms);

}  // namespace ui_hr

