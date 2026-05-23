#include "HalClock.h"

#include <Logging.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_sntp.h>
#include <time.h>

HalClock halClock;  // Singleton instance

// BM8563 (PCF8563-compatible) lives on M5PaperS3's I2C1, the same physical bus
// as the GT911 touch controller (SDA=GPIO41, SCL=GPIO42). The lgfx GT911
// driver installs the ESP-IDF I2C master on port 1 during HalTouch::begin;
// we re-bind Wire1 to the same pins here so Arduino-level I2C transactions
// can talk to the RTC without disturbing touch.
namespace {
constexpr uint8_t BM8563_ADDR = 0x51;
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
  // Arduino-ESP32 v3.x's TwoWire::begin() is idempotent — if lgfx has already
  // configured the underlying ESP-IDF I2C driver on port 1, this call binds
  // Wire1 to it without reinstalling.
  Wire1.begin(BM8563_SDA_PIN, BM8563_SCL_PIN, BM8563_FREQ);

  Wire1.beginTransmission(BM8563_ADDR);
  Wire1.write(REG_SECONDS);
  if (Wire1.endTransmission(false) != 0) {
    _available = false;
    LOG_INF("CLK", "BM8563 RTC not detected on I2C1 (addr 0x51)");
    return;
  }
  if (Wire1.requestFrom(BM8563_ADDR, static_cast<uint8_t>(1)) < 1) {
    _available = false;
    return;
  }
  Wire1.read();  // discard — only probing connectivity

  _available = true;
  LOG_INF("CLK", "BM8563 RTC detected");

  // Prime the cache with an initial read so the first status-bar render after
  // boot has a value, not a 00:00 placeholder.
  uint8_t h, m;
  getTime(h, m);
}

bool HalClock::getTime(uint8_t& hour, uint8_t& minute) const {
  if (!_available) return false;

  const unsigned long now = millis();
  if (_lastPollMs != 0 && (now - _lastPollMs) < CLOCK_POLL_MS) {
    hour = _cachedHour;
    minute = _cachedMinute;
    return true;
  }

  Wire1.beginTransmission(BM8563_ADDR);
  Wire1.write(REG_SECONDS);
  if (Wire1.endTransmission(false) != 0) {
    // Bus error — return stale cache rather than 0.
    _lastPollMs = now;
    hour = _cachedHour;
    minute = _cachedMinute;
    return true;
  }
  if (Wire1.requestFrom(BM8563_ADDR, static_cast<uint8_t>(3)) < 3) {
    _lastPollMs = now;
    hour = _cachedHour;
    minute = _cachedMinute;
    return true;
  }

  (void)Wire1.read();  // 0x02 seconds — discard
  const uint8_t rawMin = Wire1.read();
  const uint8_t rawHour = Wire1.read();

  _cachedMinute = bcdToDec(rawMin & 0x7F);
  _cachedHour = bcdToDec(rawHour & 0x3F);  // mask reserved bits 7-6
  _lastPollMs = now;

  hour = _cachedHour;
  minute = _cachedMinute;
  return true;
}

bool HalClock::formatTime(char* buf, size_t bufSize, uint8_t utcOffsetBiased, bool use24h) const {
  if (bufSize < 6) return false;
  uint8_t h, m;
  if (!getTime(h, m)) return false;

  const int offsetHalfHours = static_cast<int>(utcOffsetBiased) - 24;
  int totalMinutes = static_cast<int>(h) * 60 + static_cast<int>(m) + offsetHalfHours * 30;
  totalMinutes = ((totalMinutes % 1440) + 1440) % 1440;

  const int hour24 = totalMinutes / 60;
  const int minute = totalMinutes % 60;

  if (use24h) {
    snprintf(buf, bufSize, "%02d:%02d", hour24, minute);
  } else {
    if (bufSize < 9) return false;
    const bool pm = hour24 >= 12;
    int h12 = hour24 % 12;
    if (h12 == 0) h12 = 12;
    snprintf(buf, bufSize, "%d:%02d %s", h12, minute, pm ? "PM" : "AM");
  }
  return true;
}

bool HalClock::writeTimeToRTC(uint8_t hour, uint8_t minute, uint8_t second) {
  if (!_available) return false;

  Wire1.beginTransmission(BM8563_ADDR);
  Wire1.write(REG_SECONDS);
  Wire1.write(decToBcd(second));  // 0x02 — VL bit cleared by writing
  Wire1.write(decToBcd(minute));  // 0x03
  Wire1.write(decToBcd(hour));    // 0x04 — 24-hour mode (bit 6 = 0)
  if (Wire1.endTransmission() != 0) {
    LOG_ERR("CLK", "Failed to write time to BM8563");
    return false;
  }

  _lastPollMs = 0;  // invalidate cache
  _cachedHour = hour;
  _cachedMinute = minute;
  return true;
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
