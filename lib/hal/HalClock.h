#pragma once

#include <Arduino.h>

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
  mutable unsigned long _lastPollMs = 0;

  // Polling cadence — RTC reads go over the shared I2C bus, so we cache to
  // avoid hammering it when the status bar redraws multiple times per second.
  static constexpr unsigned long CLOCK_POLL_MS = 10000;  // 10 seconds

 public:
  // Call after gpio.begin() (which initialises the touch driver and thereby
  // the shared I2C1 bus). Probes the BM8563 at 0x51 and marks _available
  // accordingly.
  void begin();

  // True if the RTC was found and is usable.
  bool isAvailable() const { return _available; }

  // Read the current UTC time from the RTC (with 10-second caching). Returns
  // false if the RTC is unavailable.
  bool getTime(uint8_t& hour, uint8_t& minute) const;

  // Format "HH:MM" into a caller-provided buffer (>= 6 bytes for 24h, >= 9 for
  // 12h with " AM"/" PM" suffix).
  //
  // utcOffsetBiased: half-hour offset biased so 24 = UTC+0, 0 = UTC-12:00,
  //                  52 = UTC+14:00.
  // use24h:          when false, format as 12h with AM/PM.
  //
  // Returns false if the RTC is unavailable.
  bool formatTime(char* buf, size_t bufSize, uint8_t utcOffsetBiased = 24, bool use24h = true) const;

  // Sync the RTC from an NTP server. Requires WiFi to be already connected.
  // Blocks briefly (~2-3s) while waiting for the first NTP response.
  bool syncFromNTP();

 private:
  // Push a fresh UTC time into the BM8563.
  bool writeTimeToRTC(uint8_t hour, uint8_t minute, uint8_t second);
};
