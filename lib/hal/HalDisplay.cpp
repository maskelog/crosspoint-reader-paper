#include <HalDisplay.h>

#include <cstring>

HalDisplay::HalDisplay() {}

HalDisplay::~HalDisplay() {
  freeGrayscaleBuffers();
  if (sprite) {
    sprite->deleteSprite();
    delete sprite;
    sprite = nullptr;
  }
  frameBuffer = nullptr;
}

void HalDisplay::freeGrayscaleBuffers() {
  if (grayLsbBuffer) {
    heap_caps_free(grayLsbBuffer);
    grayLsbBuffer = nullptr;
  }
  if (grayMsbBuffer) {
    heap_caps_free(grayMsbBuffer);
    grayMsbBuffer = nullptr;
  }
}

void HalDisplay::begin() {
  // M5.begin() is already called by HalGPIO::begin(). Just configure the display.
  // Physical panel is 960x540 at the chip level, but the M5Paper chassis is portrait.
  // setRotation(3) gives 540x960 logical portrait with the correct top-left corner.
  M5.Display.setRotation(1);

  // 8bpp palette sprite: GfxRenderer writes EPD values (0=white, 3=black) as
  // palette indices. pushSprite() converts palette RGB565 entries to EPD gray.
  sprite = new LGFX_Sprite(&M5.Display);
  sprite->setPsram(true);
  sprite->setColorDepth(8);
  sprite->createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT);

  sprite->createPalette();
  // 4-level grayscale palette: GfxRenderer writes 0/1/2/3 as palette indices,
  // pushSprite converts each index to the configured RGB565 then to EPD gray.
  sprite->setPaletteColor(0, lgfx::color565(255, 255, 255));  // 0 = white
  sprite->setPaletteColor(1, lgfx::color565(170, 170, 170));  // 1 = light gray
  sprite->setPaletteColor(2, lgfx::color565(85,  85,  85));   // 2 = dark gray
  sprite->setPaletteColor(3, lgfx::color565(0,   0,   0));    // 3 = black
  // Remaining palette entries (4-255) default to black; unused by GfxRenderer.

  // frameBuffer points directly into the sprite's internal pixel buffer.
  frameBuffer = static_cast<uint8_t*>(sprite->getBuffer());
  if (frameBuffer) {
    memset(frameBuffer, 0, BUFFER_SIZE);  // fill with palette index 0 = white
  }

  // Initial full-quality clear to sync the EPD panel state.
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  sprite->pushSprite(0, 0);
  M5.Display.setEpdMode(epd_mode_t::epd_fast);

  if (Serial) {
    Serial.printf("[%lu] HalDisplay: begin() M5Paper - sprite %s\n", millis(), frameBuffer ? "OK" : "FAIL");
    Serial.printf("[%lu] Display logical: %dx%d, DISPLAY_WIDTH/HEIGHT: %dx%d, sprite: %dx%d\n",
                  millis(), M5.Display.width(), M5.Display.height(),
                  (int)DISPLAY_WIDTH, (int)DISPLAY_HEIGHT,
                  sprite->width(), sprite->height());
  }
}

void HalDisplay::clearScreen(uint8_t color) const {
  if (!frameBuffer) return;
  // Map old 1-bit convention to 8bpp palette index:
  // 0xFF (old white) → 0 (palette white), 0x00 (old black) → 3 (palette black)
  uint8_t epdColor = (color == 0xFF) ? 0 : 3;
  memset(frameBuffer, epdColor, BUFFER_SIZE);
}

void HalDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           bool fromProgmem) const {
  if (!frameBuffer) return;

  // Source images are 1-bit packed (8 pixels/byte, MSB first, bit=1=white, bit=0=black)
  // Unpack into 8bpp framebuffer: 0=white, 3=black
  const uint16_t imageWidthBytes = w / 8;

  for (uint16_t row = 0; row < h; row++) {
    const uint16_t destY = y + row;
    if (destY >= DISPLAY_HEIGHT) break;

    const uint32_t destRowStart = destY * DISPLAY_WIDTH + x;
    const uint16_t srcOffset = row * imageWidthBytes;

    for (uint16_t col = 0; col < imageWidthBytes; col++) {
      if (x + col * 8 >= DISPLAY_WIDTH) break;
      uint8_t srcByte = fromProgmem ? pgm_read_byte(&imageData[srcOffset + col]) : imageData[srcOffset + col];
      uint32_t dstIdx = destRowStart + col * 8;
      for (int bit = 7; bit >= 0; bit--) {
        if (dstIdx < BUFFER_SIZE) {
          frameBuffer[dstIdx] = (srcByte & (1 << bit)) ? 0 : 3;  // bit=1→white(0), bit=0→black(3)
        }
        dstIdx++;
      }
    }
  }
}

void HalDisplay::drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                      bool fromProgmem) const {
  if (!frameBuffer) return;

  // Transparent draw: only set black pixels, leave white pixels unchanged
  const uint16_t imageWidthBytes = w / 8;

  for (uint16_t row = 0; row < h; row++) {
    const uint16_t destY = y + row;
    if (destY >= DISPLAY_HEIGHT) break;

    const uint32_t destRowStart = destY * DISPLAY_WIDTH + x;
    const uint16_t srcOffset = row * imageWidthBytes;

    for (uint16_t col = 0; col < imageWidthBytes; col++) {
      if (x + col * 8 >= DISPLAY_WIDTH) break;
      uint8_t srcByte = fromProgmem ? pgm_read_byte(&imageData[srcOffset + col]) : imageData[srcOffset + col];
      uint32_t dstIdx = destRowStart + col * 8;
      for (int bit = 7; bit >= 0; bit--) {
        if (dstIdx < BUFFER_SIZE && !(srcByte & (1 << bit))) {
          frameBuffer[dstIdx] = 3;  // source bit=0 → black
        }
        dstIdx++;
      }
    }
  }
}

void HalDisplay::displayBuffer(RefreshMode mode, bool turnOffScreen) {
  if (!sprite || !frameBuffer) return;

  switch (mode) {
    case FULL_REFRESH:
      M5.Display.setEpdMode(epd_mode_t::epd_quality);
      sprite->pushSprite(0, 0);
      M5.Display.setEpdMode(epd_mode_t::epd_fast);
      break;
    case HALF_REFRESH:
      M5.Display.setEpdMode(epd_mode_t::epd_text);
      sprite->pushSprite(0, 0);
      M5.Display.setEpdMode(epd_mode_t::epd_fast);
      break;
    case FAST_REFRESH:
    default:
      // epd_text supports 4 gray levels — required for anti-aliased glyphs
      // written as palette values 1 (light gray) and 2 (dark gray). epd_fast
      // (A2 binary) would drop those to white and the page would look blank.
      M5.Display.setEpdMode(epd_mode_t::epd_text);
      sprite->pushSprite(0, 0);
      break;
  }
}

void HalDisplay::refreshDisplay(RefreshMode mode, bool turnOffScreen) {
  displayBuffer(mode, turnOffScreen);
}

void HalDisplay::deepSleep() {
  // HalPowerManager::startDeepSleep() handles the actual sleep call.
  // Nothing to tear down here — M5.Power.deepSleep() is called there.
}

uint8_t* HalDisplay::getFrameBuffer() const { return frameBuffer; }

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  // Not used in the current flow — grayscale uses LSB/MSB copy + displayGrayBuffer
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) {
  if (!lsbBuffer) return;
  if (!grayLsbBuffer) {
    grayLsbBuffer = static_cast<uint8_t*>(heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!grayLsbBuffer) grayLsbBuffer = static_cast<uint8_t*>(malloc(BUFFER_SIZE));
  }
  if (grayLsbBuffer) memcpy(grayLsbBuffer, lsbBuffer, BUFFER_SIZE);
}

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) {
  if (!msbBuffer) return;
  if (!grayMsbBuffer) {
    grayMsbBuffer = static_cast<uint8_t*>(heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!grayMsbBuffer) grayMsbBuffer = static_cast<uint8_t*>(malloc(BUFFER_SIZE));
  }
  if (grayMsbBuffer) memcpy(grayMsbBuffer, msbBuffer, BUFFER_SIZE);
}

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) {
  // No cleanup needed — M5GFX handles delta updates internally so we don't need
  // to push the BW buffer back to "reset" the display state
}

void HalDisplay::displayGrayBuffer(bool turnOffScreen) {
  if (!grayLsbBuffer || !grayMsbBuffer || !frameBuffer) return;

  // Combine two 8bpp gray planes into 4-level grayscale.
  // EPD values written to frameBuffer: 0=white, 1=lgray, 2=dgray, 3=black
  for (uint32_t i = 0; i < BUFFER_SIZE; i++) {
    uint8_t lsb_bit = (grayLsbBuffer[i] == 0) ? 1 : 0;
    uint8_t msb_bit = (grayMsbBuffer[i] == 0) ? 1 : 0;
    uint8_t gray = (msb_bit << 1) | lsb_bit;
    frameBuffer[i] = 3 - gray;
  }

  if (!sprite) { freeGrayscaleBuffers(); return; }
  // GC16 waveform supports 16 gray levels — best for anti-aliased text rendering.
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  sprite->pushSprite(0, 0);
  M5.Display.setEpdMode(epd_mode_t::epd_fast);

  freeGrayscaleBuffers();
}
