#include "HalClock.h"

#include <Logging.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_sntp.h>
#include <time.h>

#include <M5Unified.h>

HalClock halClock;  // Singleton instance

namespace {
constexpr uint8_t BM8563_ADDR = 0x51;
// S3: BM8563 on I2C1 (shared bus with GT911 touch)
constexpr int BM8563_SDA_PIN = 41;
constexpr int BM8563_SCL_PIN = 42;
constexpr uint32_t BM8563_FREQ = 400000;

// BM8563 register map (PCF8563 family). All time fields are BCD.
//   0x02  Seconds   (bit 7 = VL — set when voltage was low; time invalid)
//   0x03  Minutes
//   0x04  Hours     (24-hour mode, bits 5-0)
constexpr uint8_t REG_SECONDS = 0x02;
constexpr uint8_t REG_MINUTES = 0x03;
constexpr uint8_t REG_HOURS = 0x04;

uint8_t bcdToDec(uint8_t b) { return ((b >> 4) * 10) + (b & 0x0F); }
uint8_t decToBcd(uint8_t d) { return ((d / 10) << 4) | (d % 10); }
}  // namespace

void HalClock::begin() {
  // M5.begin() (called in HalGPIO::begin) already initialises M5.Rtc (BM8563).
  // Probe by attempting a time read.
  m5::rtc_time_t t;
  _available = M5.Rtc.getTime(&t);
  if (_available) {
    LOG_INF("CLK", "BM8563 RTC available via M5.Rtc");
    uint8_t h, m2;
    getTime(h, m2);
  } else {
    LOG_INF("CLK", "BM8563 RTC not available");
  }

}

bool HalClock::getTime(uint8_t& hour, uint8_t& minute) const {
  if (!_available) return false;

  const unsigned long now = millis();
  if (_hasCachedTime && (now - _lastPollMs) < CLOCK_POLL_MS) {
    hour = _cachedHour;
    minute = _cachedMinute;
    return true;
  }

  m5::rtc_time_t t;
  if (!M5.Rtc.getTime(&t)) {
    if (_hasCachedTime) { hour = _cachedHour; minute = _cachedMinute; return true; }
    return false;
  }
  _cachedHour   = (uint8_t)t.hours;
  _cachedMinute = (uint8_t)t.minutes;

  _hasCachedTime = true;
  _lastPollMs = now;
  hour = _cachedHour;
  minute = _cachedMinute;
  return true;
}

bool HalClock::formatTime(char* buf, size_t bufSize, uint8_t utcOffsetQuarterHoursBiased, bool use12Hour) const {
  if (bufSize < 6) return false;
  uint8_t h, m;
  if (!getTime(h, m)) return false;

  // Quarter-hour offset, biased so 48 = UTC+0. Range 0..104 covers
  // UTC-12:00 to UTC+14:00 in 15-minute steps (Nepal, India, Chatham, ...).
  const int offsetQuarterHours = static_cast<int>(utcOffsetQuarterHoursBiased) - 48;
  int totalMinutes = static_cast<int>(h) * 60 + static_cast<int>(m) + offsetQuarterHours * 15;
  totalMinutes = ((totalMinutes % 1440) + 1440) % 1440;  // wrap 24h

  const int hour24 = totalMinutes / 60;
  const int minute = totalMinutes % 60;

  if (use12Hour) {
    if (bufSize < 9) return false;
    const bool pm = hour24 >= 12;
    int h12 = hour24 % 12;
    if (h12 == 0) h12 = 12;
    snprintf(buf, bufSize, "%d:%02d %s", h12, minute, pm ? "PM" : "AM");
  } else {
    snprintf(buf, bufSize, "%02d:%02d", hour24, minute);
  }
  return true;
}

bool HalClock::writeTimeToRTC(uint8_t hour, uint8_t minute, uint8_t second) {
  if (!_available) return false;

  bool ok = false;

  m5::rtc_time_t t;
  t.hours   = (int8_t)hour;
  t.minutes = (int8_t)minute;
  t.seconds = (int8_t)second;
  M5.Rtc.setTime(&t);  // returns void in M5Unified 0.2.x
  ok = true;


  if (ok) {
    _cachedHour = hour;
    _cachedMinute = minute;
    _hasCachedTime = true;
    _lastPollMs = millis();
  }
  return ok;
}

bool HalClock::syncFromNTP() {
  if (!_available) return false;

  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("CLK", "WiFi not connected, cannot sync NTP");
    return false;
  }

  LOG_INF("CLK", "Starting NTP sync...");
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");

  constexpr int maxAttempts = 50;
  for (int i = 0; i < maxAttempts; i++) {
    const time_t now = time(nullptr);
    if (now > 1700000000) {  // sanity: after ~Nov 2023
      struct tm timeinfo;
      gmtime_r(&now, &timeinfo);
      const bool ok = writeTimeToRTC(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      if (ok) {
        LOG_INF("CLK", "RTC set to %02d:%02d:%02d UTC", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      }
      return ok;
    }
    delay(100);
  }

  LOG_ERR("CLK", "NTP sync timed out");
  return false;
}
