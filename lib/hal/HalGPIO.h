#pragma once

#include <Arduino.h>
#include <HalTouch.h>

// Number of virtual buttons (touch zones + gestures + power)
#define HALGPIO_NUM_BUTTONS 10

class HalGPIO {
 public:
  HalGPIO() = default;

  // Start touch input via GT911 and setup SD card SPI
  void begin();

  // Button input methods (touch zones mapped to virtual buttons)
  void update();
  void clearState();  // Reset button state (call during activity transitions)
  bool isPressed(uint8_t buttonIndex) const;

  // Raw touch coordinate access for tap-to-select navigation
  int16_t getLastTouchX() const;
  int16_t getLastTouchY() const;

  // Footer nav bar: taps in the bottom footerHeight pixels map to
  // Back / Confirm / Up / Down instead of the normal 3-zone split.
  // Set to 0 to disable (e.g. in reader activities).
  void setFooterHeight(int16_t height) { footerHeight = height; }
  int16_t getFooterHeight() const { return footerHeight; }
  void setTouchOrientation(uint8_t orientation) { touchOrientation = orientation; }
  bool wasPressed(uint8_t buttonIndex) const;
  bool wasAnyPressed() const;
  bool wasReleased(uint8_t buttonIndex) const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;

  // Check if USB is connected
  bool isUsbConnected() const;

  enum class WakeupReason { PowerButton, AfterFlash, AfterUSBPower, Other };

  WakeupReason getWakeupReason() const;

  // Button indices — touch zones map to these
  // 3-zone vertical split: LEFT / CENTER / RIGHT
  static constexpr uint8_t BTN_BACK = 0;     // 2-finger tap (gesture)
  static constexpr uint8_t BTN_CONFIRM = 1;  // Center zone tap (select / in-book settings)
  static constexpr uint8_t BTN_LEFT = 2;     // Left zone tap (prev page / back in lists)
  static constexpr uint8_t BTN_RIGHT = 3;    // Right zone tap (next page / forward in lists)
  static constexpr uint8_t BTN_UP = 4;       // Swipe up (prev page in lists)
  static constexpr uint8_t BTN_DOWN = 5;     // Swipe down (next page in lists)
  static constexpr uint8_t BTN_POWER = 6;
  static constexpr uint8_t BTN_SWIPE_UP = 7;    // Explicit swipe-up gesture
  static constexpr uint8_t BTN_SWIPE_DOWN = 8;  // Explicit swipe-down gesture
  static constexpr uint8_t BTN_TWO_FINGER = 9;  // 2-finger tap (also sets BTN_BACK)

 private:
  // 3-zone vertical split: converts touch X to LEFT/CENTER/RIGHT
  int touchZoneToButton(int16_t touchX, int16_t touchY) const;

  HalTouch touch;

  // Button state tracking (per-frame edge detection)
  uint16_t currentState = 0;   // Bitmask of currently pressed buttons
  uint16_t previousState = 0;  // Bitmask from last frame
  unsigned long pressStartTime = 0;
  unsigned long lastHeldTime = 0;
  unsigned long cooldownUntil = 0;  // Suppress input until this millis() timestamp
  uint8_t lastPressedButton = 0xFF;
  int16_t lastTouchX = -1;
  int16_t lastTouchY = -1;

  // Gesture tracking: swipe detection + multi-finger
  bool touchActive = false;                       // A finger is currently down
  int16_t touchStartX = -1;                       // X at touch-down
  int16_t touchStartY = -1;                       // Y at touch-down
  bool sawMultiTouch = false;                     // Saw 2+ fingers during this touch sequence
  static constexpr int16_t SWIPE_THRESHOLD = 50;  // Min Y movement for swipe (px)

  // Footer nav bar height (portrait pixels from bottom edge)
  int16_t footerHeight = 0;
  uint8_t touchOrientation = 0;

  void transformTouchPoint(int16_t rawX, int16_t rawY, int16_t* outX, int16_t* outY) const;
};

// Global singleton — defined in src/main.cpp.
extern HalGPIO gpio;
