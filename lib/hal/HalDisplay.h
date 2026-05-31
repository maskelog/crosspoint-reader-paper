#pragma once
#include <Arduino.h>

#include <M5GFX.h>  // LGFX_Sprite
#include <M5Unified.h>

class HalDisplay {
 public:
  // Constructor
  HalDisplay();

  // Destructor
  ~HalDisplay();

  // Refresh modes
  enum RefreshMode {
    FULL_REFRESH,  // Full refresh with complete waveform
    HALF_REFRESH,  // Half refresh - balanced quality and speed
    FAST_REFRESH   // Fast refresh
  };

  // Initialize the display hardware and driver
  void begin();

  // Physical framebuffer dimensions — landscape (960x540).
  // M5Paper native panel is 540x960 portrait; setRotation(1) gives the
  // 960x540 landscape framebuffer that GfxRenderer rotates portrait content
  // into via its Portrait orientation mapping.
  static constexpr uint16_t DISPLAY_WIDTH  = 960;
  static constexpr uint16_t DISPLAY_HEIGHT = 540;
  // 8bpp palette-index framebuffer: 1 byte per pixel, values 0(white)..3(black)
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH * DISPLAY_HEIGHT;
  // For unpacking 1-bit source images only
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;

  // Runtime accessors — M5Paper panel is fixed-size so these return constants.
  uint16_t getDisplayWidth() const { return DISPLAY_WIDTH; }
  uint16_t getDisplayHeight() const { return DISPLAY_HEIGHT; }
  uint16_t getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
  uint32_t getBufferSize() const { return BUFFER_SIZE; }

  // Frame buffer operations
  void clearScreen(uint8_t color = 0xFF) const;
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 bool fromProgmem = false) const;
  void drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            bool fromProgmem = false) const;

  void displayBuffer(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);
  void refreshDisplay(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);

  // Power management
  void deepSleep();

  // Access to frame buffer
  uint8_t* getFrameBuffer() const;

  void copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer);
  void copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t* msbBuffer);
  void cleanupGrayscaleBuffers(const uint8_t* bwBuffer);

  void displayGrayBuffer(bool turnOffScreen = false);

 private:
  // 8bpp palette-index framebuffer in PSRAM (0=white, 3=black).
  // On M5Paper this points directly into the LGFX_Sprite internal buffer.
  uint8_t* frameBuffer = nullptr;

  // Grayscale buffers (8bpp, allocated on demand for two-pass rendering)
  uint8_t* grayLsbBuffer = nullptr;
  uint8_t* grayMsbBuffer = nullptr;
  void freeGrayscaleBuffers();

  LGFX_Sprite* sprite = nullptr;
};

// Global singleton — defined in src/main.cpp.
extern HalDisplay display;
