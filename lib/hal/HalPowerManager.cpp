#include "HalPowerManager.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include <cassert>

#include "HalGPIO.h"

#include <M5Unified.h>
#include <driver/rtc_io.h>
static constexpr gpio_num_t M5PAPER_WAKEUP_PIN = GPIO_NUM_38;  // BtnB (CONFIRM / ON)

// M5PaperS3 power-off pulse pin (active-high pulse turns off PMIC)
static constexpr int PWROFF_PULSE_PIN = 44;

// M5PaperS3 battery voltage ADC pin (hardware voltage divider, ~2.04x ratio)
static constexpr int BAT_ADC_PIN = 3;
static constexpr int BAT_ADC_SAMPLES = 16;         // Number of ADC samples to average
static constexpr uint16_t BAT_HYSTERESIS_MV = 30;  // Only update if voltage changed by ≥30mV (~3%)
static uint16_t lastBattMv = 0;                    // Cached smoothed voltage

HalPowerManager powerManager;  // Singleton instance

void HalPowerManager::begin() {
  normalFreq = getCpuFrequencyMhz();
  modeMutex = xSemaphoreCreateMutex();
  assert(modeMutex != nullptr);

  // Battery voltage via ADC on GPIO3 (voltage divider, ~2.04x).
  pinMode(BAT_ADC_PIN, INPUT);
  analogSetAttenuation(ADC_11db);
  // M5Paper: M5.Power is already initialised by M5.begin() in HalGPIO::begin().
}

void HalPowerManager::setPowerSaving(bool enabled) {
  // EPD panel is the dominant power consumer; CPU throttling only adds
  // touch/render latency without meaningful battery benefit.
  (void)enabled;
  return;
}

void HalPowerManager::startDeepSleep(HalGPIO& gpio) const {
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }

  // Configure BtnA (GPIO37, active LOW) as EXT0 wakeup source.
  rtc_gpio_init(M5PAPER_WAKEUP_PIN);
  rtc_gpio_set_direction(M5PAPER_WAKEUP_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en(M5PAPER_WAKEUP_PIN);
  rtc_gpio_pulldown_dis(M5PAPER_WAKEUP_PIN);
  esp_sleep_enable_ext0_wakeup(M5PAPER_WAKEUP_PIN, 0);  // wake on LOW
  esp_deep_sleep_start();

}

void HalPowerManager::powerOff() const {
  LOG_DBG("PWR", "Powering off");
  WiFi.mode(WIFI_OFF);
  delay(100);
  M5.Power.powerOff();
}

uint16_t HalPowerManager::getBatteryPercentage() const {
  int level = M5.Power.getBatteryLevel();  // 0-100, or -1 if not available
  return (level >= 0) ? (uint16_t)level : 0;
}

HalPowerManager::Lock::Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  // Current limitation: only one lock at a time
  if (powerManager.currentLockMode != None) {
    LOG_ERR("PWR", "Lock already held, ignore");
    valid = false;
  } else {
    powerManager.currentLockMode = NormalSpeed;
    valid = true;
  }
  xSemaphoreGive(powerManager.modeMutex);
  if (valid) {
    // Immediately restore normal CPU frequency if currently in low-power mode
    powerManager.setPowerSaving(false);
  }
}

HalPowerManager::Lock::~Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  if (valid) {
    powerManager.currentLockMode = None;
  }
  xSemaphoreGive(powerManager.modeMutex);
}
