#include "FontCacheManager.h"

#include <FontDecompressor.h>
#include <Logging.h>

#include <cstring>

FontCacheManager::FontCacheManager(const std::map<int, EpdFontFamily>& fontMap) : fontMap_(fontMap) {}

void FontCacheManager::setFontDecompressor(FontDecompressor* d) { fontDecompressor_ = d; }

void FontCacheManager::clearCache() {
  if (fontDecompressor_) fontDecompressor_->clearCache();
}

void FontCacheManager::clearPageCacheOnly() {
  if (fontDecompressor_) fontDecompressor_->clearPageCacheOnly();
}

void FontCacheManager::prewarmCache(int fontId, const char* utf8Text, uint8_t styleMask) {
  if (!fontDecompressor_ || fontMap_.count(fontId) == 0) return;

  for (uint8_t i = 0; i < 4; i++) {
    if (!(styleMask & (1 << i))) continue;
    auto style = static_cast<EpdFontFamily::Style>(i);
    const EpdFontData* data = fontMap_.at(fontId).getData(style);
    if (!data || !data->groups) continue;
    int missed = fontDecompressor_->prewarmCache(data, utf8Text);
    if (missed > 0) {
      LOG_DBG("FCM", "prewarmCache: %d glyph(s) not cached for style %d", missed, i);
    }
  }
}

void FontCacheManager::logStats(const char* label) {
  if (fontDecompressor_) fontDecompressor_->logStats(label);
}

void FontCacheManager::resetStats() {
  if (fontDecompressor_) fontDecompressor_->resetStats();
}

bool FontCacheManager::isScanning() const { return scanMode_ == ScanMode::Scanning; }

void FontCacheManager::recordText(const char* text, int fontId, EpdFontFamily::Style style) {
  scanText_ += text;
  uint8_t fontSlot = MAX_SCAN_FONTS;
  for (uint8_t i = 0; i < scanFontCount_; i++) {
    if (scanFontIds_[i] == fontId) {
      fontSlot = i;
      break;
    }
  }
  if (fontSlot == MAX_SCAN_FONTS) {
    if (scanFontCount_ >= MAX_SCAN_FONTS) return;
    fontSlot = scanFontCount_++;
    scanFontIds_[fontSlot] = fontId;
  }
  const uint8_t baseStyle = static_cast<uint8_t>(style) & 0x03;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
  uint32_t cpCount = 0;
  while (*p) {
    if ((*p & 0xC0) != 0x80) cpCount++;
    p++;
  }
  scanStyleCounts_[fontSlot][baseStyle] += cpCount;
}

// --- PrewarmScope implementation ---

FontCacheManager::PrewarmScope::PrewarmScope(FontCacheManager& manager) : manager_(&manager) {
  manager_->scanMode_ = ScanMode::Scanning;
  // Keep the CJK fallback hot group alive across pages — only the per-page
  // prewarm buffer gets rebuilt. Freeing the multi-MB hot group every page
  // fragments PSRAM and eventually fails to reallocate on M5Paper.
  manager_->clearPageCacheOnly();
  manager_->resetStats();
  manager_->scanText_.clear();
  manager_->scanText_.reserve(2048);  // Pre-allocate to avoid heap fragmentation from repeated concat
  memset(manager_->scanStyleCounts_, 0, sizeof(manager_->scanStyleCounts_));
  for (uint8_t i = 0; i < FontCacheManager::MAX_SCAN_FONTS; i++) {
    manager_->scanFontIds_[i] = -1;
  }
  manager_->scanFontCount_ = 0;
}

void FontCacheManager::PrewarmScope::endScanAndPrewarm() {
  manager_->scanMode_ = ScanMode::None;
  if (manager_->scanText_.empty()) return;

  for (uint8_t fontSlot = 0; fontSlot < manager_->scanFontCount_; fontSlot++) {
    uint8_t styleMask = 0;
    for (uint8_t i = 0; i < 4; i++) {
      if (manager_->scanStyleCounts_[fontSlot][i] > 0) styleMask |= (1 << i);
    }
    if (styleMask == 0) styleMask = 1;  // default to regular
    manager_->prewarmCache(manager_->scanFontIds_[fontSlot], manager_->scanText_.c_str(), styleMask);
  }

  // Free scan string memory
  manager_->scanText_.clear();
  manager_->scanText_.shrink_to_fit();
}

FontCacheManager::PrewarmScope::~PrewarmScope() {
  if (active_) {
    endScanAndPrewarm();  // no-op if already called (scanText_ is empty)
    // Hot group stays — see PrewarmScope ctor comment. Only the page buffer
    // gets freed at the start of the next prewarmCache() call.
    manager_->clearPageCacheOnly();
  }
}

FontCacheManager::PrewarmScope::PrewarmScope(PrewarmScope&& other) noexcept
    : manager_(other.manager_), active_(other.active_) {
  other.active_ = false;
}

FontCacheManager::PrewarmScope FontCacheManager::createPrewarmScope() { return PrewarmScope(*this); }
