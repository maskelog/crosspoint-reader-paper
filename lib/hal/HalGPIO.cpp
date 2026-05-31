#include <HalGPIO.h>
#include <Logging.h>

#include <driver/rtc_io.h>
#include <esp_sleep.h>
// Physical side buttons on M5Paper (active LOW, pulled up):
//   GPIO37 = BtnA = UP
//   GPIO38 = BtnB = ON / CONFIRM (also long-press → sleep, wakeup source)
//   GPIO39 = BtnC = DOWN
// There is no software-readable power switch; sleep is requested by long-press
// on BtnB, and the device wakes from deep sleep when BtnB is pressed.

// Long-press duration on BtnB that requests deep sleep.
static constexpr unsigned long M5PAPER_BTNB_SLEEP_HOLD_MS = 1500;

// Touch zones in LOGICAL portrait coordinates (540 wide x 960 tall).
// The physical display is 960x540 landscape; GfxRenderer rotates to portrait.
// GT911 on M5PaperS3 reports in portrait: x[0-539], y[0-959].
static constexpr int16_t PORT_W = 540;
static constexpr int16_t PORT_H = 960;

static void getLogicalDimensions(uint8_t orientation, int16_t* outWidth, int16_t* outHeight) {
  switch (orientation) {
    case 1:  // LandscapeClockwise
    case 3:  // LandscapeCounterClockwise
      *outWidth = PORT_H;
      *outHeight = PORT_W;
      break;
    case 0:  // Portrait
    case 2:  // PortraitInverted
    default:
      *outWidth = PORT_W;
      *outHeight = PORT_H;
      break;
  }
}

void HalGPIO::transformTouchPoint(int16_t rawX, int16_t rawY, int16_t* outX, int16_t* outY) const {
  switch (touchOrientation) {
    case 1:  // LandscapeClockwise
      *outX = PORT_H - 1 - rawY;
      *outY = rawX;
      break;
    case 2:  // PortraitInverted
      *outX = PORT_W - 1 - rawX;
      *outY = PORT_H - 1 - rawY;
      break;
    case 3:  // LandscapeCounterClockwise
      *outX = rawY;
      *outY = PORT_W - 1 - rawX;
      break;
    case 0:  // Portrait
    default:
      *outX = rawX;
      *outY = rawY;
      break;
  }
}

// 3-zone vertical split: each zone is 1/3 of screen width (180px)
static constexpr int16_t ZONE_LEFT_END = PORT_W / 3;         // 180
static constexpr int16_t ZONE_RIGHT_START = PORT_W * 2 / 3;  // 360

void HalGPIO::begin() {
  // M5.begin() initialises display, touch (FT6336 on M5Paper), I2C, power.
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);
  LOG_INF("GPIO", "M5Paper M5.begin() done");
}

int16_t HalGPIO::getLastTouchX() const {
  int16_t logicalX = 0;
  int16_t logicalY = 0;
  transformTouchPoint(lastTouchX, lastTouchY, &logicalX, &logicalY);
  return logicalX;
}

int16_t HalGPIO::getLastTouchY() const {
  int16_t logicalX = 0;
  int16_t logicalY = 0;
  transformTouchPoint(lastTouchX, lastTouchY, &logicalX, &logicalY);
  return logicalY;
}

int HalGPIO::touchZoneToButton(int16_t touchX, int16_t touchY) const {
  // GT911 on M5PaperS3 reports portrait coordinates directly: x[0-539], y[0-959].
  int16_t logicalX = 0;
  int16_t logicalY = 0;
  transformTouchPoint(touchX, touchY, &logicalX, &logicalY);

  int16_t logicalW = 0;
  int16_t logicalH = 0;
  getLogicalDimensions(touchOrientation, &logicalW, &logicalH);

  if (logicalX < 0 || logicalX >= logicalW || logicalY < 0 || logicalY >= logicalH) return -1;

  // Footer nav bar: bottom footerHeight pixels are split into 4 equal tap zones
  // mapping to Back / Confirm / Up / Down (matches drawButtonHints layout)
  if (footerHeight > 0) {
    if (logicalY >= logicalH - footerHeight) {
      const int16_t quarter = logicalW / 4;
      if (logicalX < quarter) return BTN_BACK;
      if (logicalX < quarter * 2) return BTN_CONFIRM;
      if (logicalX < quarter * 3) return BTN_UP;
      return BTN_DOWN;
    }
    // Content-area tap in footer mode — suppress entirely so only the
    // footer buttons drive input in non-reader / non-keyboard activities.
    return -1;
  }

  // Simple 3-zone vertical split across the content area
  const int16_t zoneLeftEnd = logicalW / 3;
  const int16_t zoneRightStart = logicalW * 2 / 3;
  if (logicalX < zoneLeftEnd) return BTN_LEFT;
  if (logicalX >= zoneRightStart) return BTN_RIGHT;
  return BTN_CONFIRM;
}

void HalGPIO::update() {
  previousState = currentState;
  currentState = 0;

  // --- Platform-specific input reading ---
  bool touching = false;
  uint8_t numPoints = 0;

  M5.update();

  // Physical side buttons:
  //   BtnA (GPIO37) → UP
  //   BtnB (GPIO38) → CONFIRM; held ≥1.5s → POWER (sleep request)
  //   BtnC (GPIO39) → DOWN
  if (M5.BtnA.isPressed()) currentState |= (1 << BTN_UP);
  if (M5.BtnC.isPressed()) currentState |= (1 << BTN_DOWN);
  if (M5.BtnB.isPressed()) {
    currentState |= (1 << BTN_CONFIRM);
    if (M5.BtnB.pressedFor(M5PAPER_BTNB_SLEEP_HOLD_MS)) {
      currentState |= (1 << BTN_POWER);
    }
  }

  if (millis() < cooldownUntil) {
    touchActive = false;
    sawMultiTouch = false;
    uint16_t newPresses = currentState & ~previousState;
    if (newPresses) pressStartTime = millis();
    return;
  }

  {
    auto td = M5.Touch.getDetail(0);
    touching = td.isPressed() || td.isHolding();
    numPoints = (uint8_t)M5.Touch.getCount();
    if (touching) {
      // FT6336 reports raw landscape coords regardless of setRotation().
      // Hardware rotation=1 (90° CW) maps landscape (td.x, td.y) to portrait as:
      //   portrait logX = 539 - td.y,  portrait logY = td.x
      LOG_DBG("TOUCH", "raw td.x=%d td.y=%d", (int)td.x, (int)td.y);
      lastTouchX = 539 - (int16_t)td.y;
      lastTouchY = (int16_t)td.x;
    }
  }


  // --- Shared gesture classification (portrait logical coords) ---
  if (touching) {
    if (numPoints >= 2) sawMultiTouch = true;

    if (!touchActive) {
      touchActive = true;
      touchStartX = lastTouchX;
      touchStartY = lastTouchY;
    }
    int btn = touchZoneToButton(touchStartX, touchStartY);
    if (btn >= 0 && btn < HALGPIO_NUM_BUTTONS) currentState |= (1 << btn);

  } else if (touchActive) {
    lastHeldTime = millis() - pressStartTime;
    touchActive = false;

    if (footerHeight > 0) {
      int btn = touchZoneToButton(touchStartX, touchStartY);
      if (btn >= 0 && btn < HALGPIO_NUM_BUTTONS) currentState |= (1 << btn);
      LOG_DBG("TOUCH", "tap (%d,%d) btn=%d footer", touchStartX, touchStartY, btn);
    } else if (sawMultiTouch) {
      currentState |= (1 << BTN_BACK);
      currentState |= (1 << BTN_TWO_FINGER);
      LOG_DBG("TOUCH", "2-finger -> BACK");
    } else {
      int16_t startLogX = 0, startLogY = 0, lastLogX = 0, lastLogY = 0;
      transformTouchPoint(touchStartX, touchStartY, &startLogX, &startLogY);
      transformTouchPoint(lastTouchX,  lastTouchY,  &lastLogX,  &lastLogY);
      const int16_t deltaY = lastLogY - startLogY;

      if (deltaY < -SWIPE_THRESHOLD) {
        currentState |= (1 << BTN_SWIPE_UP);
        currentState |= (1 << BTN_UP);
        LOG_DBG("TOUCH", "swipe up dy=%d", deltaY);
      } else if (deltaY > SWIPE_THRESHOLD) {
        currentState |= (1 << BTN_SWIPE_DOWN);
        currentState |= (1 << BTN_DOWN);
        LOG_DBG("TOUCH", "swipe down dy=%d", deltaY);
      } else {
        int btn = touchZoneToButton(touchStartX, touchStartY);
        if (btn >= 0 && btn < HALGPIO_NUM_BUTTONS) currentState |= (1 << btn);
        LOG_DBG("TOUCH", "tap (%d,%d) btn=%d", touchStartX, touchStartY, btn);
      }
    }
    sawMultiTouch = false;
  }

  // Track press timing
  uint16_t newPresses = currentState & ~previousState;
  if (newPresses) {
    pressStartTime = millis();
    for (uint8_t i = 0; i < HALGPIO_NUM_BUTTONS; i++) {
      if (newPresses & (1 << i)) { lastPressedButton = i; break; }
    }
  }
}

void HalGPIO::clearState() {
  previousState = 0;
  currentState = 0;
  pressStartTime = 0;
  lastHeldTime = 0;
  touchActive = false;
  sawMultiTouch = false;
  cooldownUntil = millis() + 200;  // Suppress input for 200ms after activity transition
}

bool HalGPIO::isPressed(uint8_t buttonIndex) const {
  if (buttonIndex >= HALGPIO_NUM_BUTTONS) return false;
  return currentState & (1 << buttonIndex);
}

bool HalGPIO::wasPressed(uint8_t buttonIndex) const {
  if (buttonIndex >= HALGPIO_NUM_BUTTONS) return false;
  // Rising edge: pressed now but not before
  return (currentState & (1 << buttonIndex)) && !(previousState & (1 << buttonIndex));
}

bool HalGPIO::wasAnyPressed() const { return (currentState & ~previousState) != 0; }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const {
  if (buttonIndex >= HALGPIO_NUM_BUTTONS) return false;
  // Falling edge: not pressed now but was before
  return !(currentState & (1 << buttonIndex)) && (previousState & (1 << buttonIndex));
}

bool HalGPIO::wasAnyReleased() const { return (previousState & ~currentState) != 0; }

unsigned long HalGPIO::getHeldTime() const {
  if (currentState == 0) return lastHeldTime;
  return millis() - pressStartTime;
}

bool HalGPIO::isUsbConnected() const {
  // M5Paper uses UART, not USB CDC — always return true so cold-boot is
  // treated as AfterFlash (proceed to boot) rather than PowerButton.
  return true;
}

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const auto wakeupCause = esp_sleep_get_wakeup_cause();

  // M5Paper enters deep sleep with EXT0 on GPIO37 (BtnA).
  // All other wakeup causes are cold-boot / after-flash — proceed normally.
  if (wakeupCause == ESP_SLEEP_WAKEUP_EXT0) return WakeupReason::PowerButton;
  return WakeupReason::Other;
}