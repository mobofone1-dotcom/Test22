#pragma once

#include <Arduino.h>

#ifndef HR_DUMMY_MODE
#if defined(__has_include)
#if __has_include(<NimBLEDevice.h>)
#include <NimBLEDevice.h>
#define HRM_USE_NIMBLE 1
#elif __has_include(<BLEDevice.h>)
#include <BLEDevice.h>
#define HRM_USE_CLASSIC_BLE 1
#endif
#endif
#endif

enum class HrConnState : uint8_t {
  DISCONNECTED = 0,
  SCANNING,
  CONNECTING,
  SUBSCRIBED,
  ERROR
};

struct HrSnapshot {
  uint16_t current_bpm;
  uint16_t min_bpm;
  uint16_t max_bpm;
  bool has_hr;
  uint32_t start_ms;
  uint32_t last_hr_ms;
  HrConnState state;
};

class BleHrClient {
 public:
  void begin();
  void loop();
  void resetMetrics();
  HrSnapshot snapshot() const;

  void onHeartRate(uint16_t bpm);
  void handleDisconnected();

 private:
  void setState(HrConnState new_state);

  volatile uint16_t current_bpm_ = 0;
  volatile uint16_t min_bpm_ = 0;
  volatile uint16_t max_bpm_ = 0;
  volatile bool has_hr_ = false;
  volatile uint32_t start_ms_ = 0;
  volatile uint32_t last_hr_ms_ = 0;
  volatile HrConnState state_ = HrConnState::DISCONNECTED;

  uint32_t next_scan_ms_ = 0;
  uint32_t next_connect_ms_ = 0;
  uint32_t backoff_ms_ = 1000;
  bool target_found_ = false;

#ifdef HR_DUMMY_MODE
  uint32_t next_dummy_ms_ = 0;
  uint16_t dummy_bpm_ = 60;
  int8_t dummy_step_ = 2;
#endif
};
