#include "EpubReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long NAV_REPEAT_START_MS = 300;
constexpr unsigned long NAV_REPEAT_INTERVAL_MS = 450;
}  // namespace

int EpubReaderChapterSelectionActivity::getTotalItems() const { return epub->getTocItemsCount(); }

int EpubReaderChapterSelectionActivity::getPageItems() const {
  // Layout constants used in renderScreen
  constexpr int lineHeight = 75;

  const int screenHeight = renderer.getScreenHeight();
  const auto orientation = renderer.getOrientation();
  // In inverted portrait, the button hints are drawn near the logical top.
  // Reserve vertical space so list items do not collide with the hints.
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int startY = 60 + hintGutterHeight;
  const int availableHeight = screenHeight - startY - lineHeight;
  // Clamp to at least one item to avoid division by zero and empty paging.
  return std::max(1, availableHeight / lineHeight);
}

void EpubReaderChapterSelectionActivity::onEnter() {
  Activity::onEnter();

  if (!epub) {
    return;
  }

  selectorIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (selectorIndex == -1) {
    selectorIndex = 0;
  }

  const int totalItems = getTotalItems();
  tocCache.clear();
  if (totalItems > 0) {
    tocCache.resize(static_cast<size_t>(totalItems));
  }

  // Trigger first update
  requestUpdate();
}

void EpubReaderChapterSelectionActivity::onExit() { Activity::onExit(); }

void EpubReaderChapterSelectionActivity::moveSelection(int nextIndex) {
  const int totalItems = getTotalItems();
  if (totalItems <= 0) return;
  if (RenderLock::peek()) return;

  selectorIndex = nextIndex;
  lastNavigationTime = millis();
  requestUpdate();
}

void EpubReaderChapterSelectionActivity::confirmSelection() {
  const auto newSpineIndex = epub->getSpineIndexForTocIndex(selectorIndex);
  if (newSpineIndex == -1) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
  } else {
    setResult(ChapterResult{newSpineIndex});
    finish();
  }
}

void EpubReaderChapterSelectionActivity::loop() {
  const int totalItems = getTotalItems();

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    confirmSelection();
    return;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (totalItems <= 0) return;

  const bool previousPressed = mappedInput.wasPressed(MappedInputManager::Button::Up) ||
                               mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool nextPressed = mappedInput.wasPressed(MappedInputManager::Button::Down) ||
                           mappedInput.wasPressed(MappedInputManager::Button::Right);
  const bool previousHeld = mappedInput.isPressed(MappedInputManager::Button::Up) ||
                            mappedInput.isPressed(MappedInputManager::Button::Left);
  const bool nextHeld = mappedInput.isPressed(MappedInputManager::Button::Down) ||
                        mappedInput.isPressed(MappedInputManager::Button::Right);
  const bool repeatDue = mappedInput.getHeldTime() >= NAV_REPEAT_START_MS &&
                         (millis() - lastNavigationTime) >= NAV_REPEAT_INTERVAL_MS;

  if (previousPressed || (previousHeld && repeatDue)) {
    moveSelection(ButtonNavigator::previousIndex(selectorIndex, totalItems));
    return;
  }

  if (nextPressed || (nextHeld && repeatDue)) {
    moveSelection(ButtonNavigator::nextIndex(selectorIndex, totalItems));
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    lastNavigationTime = 0;
  }
}

void EpubReaderChapterSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto orientation = renderer.getOrientation();
  // Landscape orientation: reserve a horizontal gutter for button hints.
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  // Inverted portrait: reserve vertical space for hints at the top.
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 30 : 0;
  // Landscape CW places hints on the left edge; CCW keeps them on the right.
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int contentY = hintGutterHeight;
  const int pageItems = getPageItems();
  const int totalItems = getTotalItems();

  // Manual centering to honor content gutters.
  const int titleX =
      contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, tr(STR_SELECT_CHAPTER), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, tr(STR_SELECT_CHAPTER), true, EpdFontFamily::BOLD);

  constexpr int lineHeight = 75;
  const int textLineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int textYOff = (lineHeight - textLineH) / 2;

  const auto pageStartIndex = selectorIndex / pageItems * pageItems;
  // Highlight only the content area, not the hint gutters.
  renderer.fillRect(contentX, 60 + contentY + (selectorIndex % pageItems) * lineHeight, contentWidth - 1, lineHeight);

  for (int i = 0; i < pageItems; i++) {
    int itemIndex = pageStartIndex + i;
    if (itemIndex >= totalItems) break;
    const int displayY = 60 + contentY + i * lineHeight;
    const bool isSelected = (itemIndex == selectorIndex);

    auto& cachedItem = tocCache[static_cast<size_t>(itemIndex)];
    if (!cachedItem.loaded) {
      const auto item = epub->getTocItem(itemIndex);
      cachedItem.loaded = true;
      cachedItem.level = item.level;
      cachedItem.title = item.title;
    }

    // Indent per TOC level while keeping content within the gutter-safe region.
    const int indentSize = contentX + 20 + (cachedItem.level - 1) * 15;
    const int maxTitleWidth = contentWidth - 40 - indentSize;
    if (cachedItem.maxWidth != maxTitleWidth) {
      cachedItem.displayTitle = renderer.truncatedText(UI_10_FONT_ID, cachedItem.title.c_str(), maxTitleWidth);
      cachedItem.maxWidth = maxTitleWidth;
    }

    renderer.drawText(UI_10_FONT_ID, indentSize, displayY + textYOff, cachedItem.displayTitle.c_str(), !isSelected);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
