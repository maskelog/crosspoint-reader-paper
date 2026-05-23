#pragma once

#include <Arduino.h>
#include <Wire.h>

class HalClock;
extern HalClock halClock;  // Singleton

// Real-time clock abstraction.
//
// On Paper S3 this is backed by the on-board BM8563 RTC, addressed directly
// over Wire1 (I2C1, the same physical bus as GT911 touch). The BM8563 is
// less accurate than the DS3231 used on the X3 (~20 ppm vs ~2 ppm) and
// drifts further during deep sleep, so we expose a best-effort time and rely
// on NTP resync to correct drift over time.
//
// Time is stored in the RTC as UTC. The display-time offset (set by the user)
// is applied only in formatTime().
class HalClock {
  bool _available = false;
  mutable uint8_t _cachedHour = 0;
  mutable uint8_t _cachedMinute = 0;
  mutable bool _hasCachedTime = false;
  mutable unsigned long _lastPollMs = 0;

  static constexpr unsigned long CLOCK_POLL_MS = 10000;  // 10 seconds

 public:
  // Call after gpio.begin() (which initialises the touch driver and thereby
  // the shared I2C1 bus). Probes the BM8563 at 0x51 and marks _available
  // accordingly.
  void begin();

  // True if the BM8563 RTC responded to the probe at begin() time.
  bool isAvailable() const { return _available; }

  // Get current UTC hour (0-23) and minute (0-59) with 10-second caching.
  // Returns false if RTC is not available.
  bool getTime(uint8_t& hour, uint8_t& minute) const;

  // Format time into a caller-provided buffer.
  // 24h mode produces "HH:MM" (needs >=6 bytes); 12h mode produces
  // "H:MM AM"/"HH:MM PM" (needs >=9 bytes).
  // utcOffsetQuarterHoursBiased: biased quarter-hour offset
  //   (48 = UTC+0, 0 = UTC-12, 104 = UTC+14).
  // use12Hour: when true, format as 12-hour clock with AM/PM suffix.
  // Returns false if RTC is not available.
  bool formatTime(char* buf, size_t bufSize, uint8_t utcOffsetQuarterHoursBiased = 48, bool use12Hour = false) const;

  // Sync the RTC from an NTP server. Requires WiFi to be connected.
  // Blocks for up to ~5s while waiting for SNTP response.
  // Returns true if the RTC was successfully updated.
  //
  // Debouncing (skip if already synced once) is enforced by the caller, not
  // here, so the HAL stays free of any app-layer settings dependency.
  bool syncFromNTP();

 private:
  bool writeTimeToRTC(uint8_t hour, uint8_t minute, uint8_t second);
};
