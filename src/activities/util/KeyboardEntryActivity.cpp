#include "KeyboardEntryActivity.h"

#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void KeyboardEntryActivity::onEnter() {
  Activity::onEnter();
  // Disable footer touch zones — this keyboard handles raw touch coordinates
  // via handleTouchAt(). Without this, taps on the bottom row (MODE/SPACE/OK)
  // fall in the footer zone and get remapped to Back/Confirm/Up/Down.
  mappedInput.setFooterHeight(0);
  requestUpdate();
}

void KeyboardEntryActivity::onExit() { Activity::onExit(); }

void KeyboardEntryActivity::onComplete(std::string text) {
  setResult(KeyboardResult{std::move(text)});
  finish();
}

void KeyboardEntryActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

// =============================================================================
// Paper S3 touch keyboard — 4-row layout, tap-to-type
// =============================================================================

// Layouts: [mode][row]  mode 0=lower, 1=upper, 2=numbers
const char* const KeyboardEntryActivity::touchKb[3][NUM_ROWS] = {
    {"qwertyuiop", "asdfghjkl", "zxcvbnm", ""},
    {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM", ""},
    {"1234567890", "-/:;()$&@\"", ".,?!'", ""},
};

int KeyboardEntryActivity::touchRowCharCount(const int row) const {
  if (row < 0 || row >= NUM_ROWS) return 0;
  const int mode = (shiftState == 2) ? 2 : shiftState;
  return static_cast<int>(strlen(touchKb[mode][row]));
}

int KeyboardEntryActivity::getKeyboardStartY() const {
  const int screenH = renderer.getScreenHeight();
  const int kbHeight = NUM_ROWS * TK_KEY_H + (NUM_ROWS - 1) * TK_SPACING;
  return screenH - TK_BOTTOM_PAD - kbHeight;
}

bool KeyboardEntryActivity::handleTouchAt(const int16_t tx, const int16_t ty) {
  const int screenW = renderer.getScreenWidth();
  const int kbY = getKeyboardStartY();
  const int mode = (shiftState == 2) ? 2 : shiftState;
  const char* const* layout = touchKb[mode];

  // Determine which row
  if (ty < kbY) return false;
  const int rowIdx = (ty - kbY) / (TK_KEY_H + TK_SPACING);
  if (rowIdx < 0 || rowIdx >= NUM_ROWS) return false;
  const int rowY = kbY + rowIdx * (TK_KEY_H + TK_SPACING);
  if (ty > rowY + TK_KEY_H) return false;  // in the gap

  // ---- Row 3: MODE | SPACE | OK ----
  if (rowIdx == 3) {
    const int margin = 4;
    const int spaceW = screenW - 2 * margin - 2 * TK_MODE_W - 2 * TK_SPACING;
    int cx = margin;
    // MODE button
    if (tx >= cx && tx < cx + TK_MODE_W) {
      shiftState = (shiftState == 2) ? 0 : 2;
      return true;
    }
    cx += TK_MODE_W + TK_SPACING;
    // SPACE
    if (tx >= cx && tx < cx + spaceW) {
      if (maxLength == 0 || text.length() < maxLength) text += ' ';
      return true;
    }
    cx += spaceW + TK_SPACING;
    // OK
    if (tx >= cx && tx < cx + TK_MODE_W) {
      onComplete(text);
      return false;
    }
    return false;
  }

  // ---- Row 2: SHIFT/toggle + chars + BACKSPACE ----
  if (rowIdx == 2) {
    const int nChars = touchRowCharCount(2);
    const int charsW = nChars * TK_KEY_W + (nChars - 1) * TK_SPACING;
    const int totalW = TK_WIDE_W + TK_SPACING + charsW + TK_SPACING + TK_WIDE_W;
    const int margin = (screenW - totalW) / 2;
    int cx = margin;

    // SHIFT / #+= toggle
    if (tx >= cx && tx < cx + TK_WIDE_W) {
      if (shiftState == 2) {
        // In number mode: no shift, ignore (or could add #+= page)
      } else {
        shiftState = (shiftState == 0) ? 1 : 0;
      }
      return true;
    }
    cx += TK_WIDE_W + TK_SPACING;

    // Character keys
    for (int i = 0; i < nChars; i++) {
      const int kx = cx + i * (TK_KEY_W + TK_SPACING);
      if (tx >= kx && tx < kx + TK_KEY_W) {
        const char c = layout[2][i];
        if (c != '\0' && (maxLength == 0 || text.length() < maxLength)) {
          text += c;
          if (shiftState == 1) shiftState = 0;  // auto-unshift
        }
        return true;
      }
    }
    cx += charsW + TK_SPACING;

    // BACKSPACE
    if (tx >= cx && tx < cx + TK_WIDE_W) {
      if (!text.empty()) text.pop_back();
      return true;
    }
    return false;
  }

  // ---- Rows 0-1: regular character keys ----
  const int nKeys = touchRowCharCount(rowIdx);
  if (nKeys <= 0) return false;
  const int totalW = nKeys * TK_KEY_W + (nKeys - 1) * TK_SPACING;
  const int margin = (screenW - totalW) / 2;

  const int col = (tx - margin + TK_SPACING / 2) / (TK_KEY_W + TK_SPACING);
  if (col < 0 || col >= nKeys) return false;

  const char c = layout[rowIdx][col];
  if (c != '\0' && (maxLength == 0 || text.length() < maxLength)) {
    text += c;
    if (shiftState == 1) shiftState = 0;
  }
  return true;
}

void KeyboardEntryActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onCancel();
    return;
  }

  // Any single-finger tap in any zone
  const bool anyTap = mappedInput.wasReleased(MappedInputManager::Button::Left) ||
                      mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
                      mappedInput.wasReleased(MappedInputManager::Button::Right);
  if (anyTap) {
    const int16_t tx = mappedInput.getTouchX();
    const int16_t ty = mappedInput.getTouchY();
    if (tx >= 0 && ty >= 0) {
      handleTouchAt(tx, ty);
      requestUpdate();
    }
  }
}

void KeyboardEntryActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const int screenW = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenW, metrics.headerHeight}, title.c_str());

  // ---- Input field ----
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int inputStartY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 5;

  std::string displayText = isPassword ? std::string(text.length(), '*') : text;
  displayText += "_";

  int inputHeight = 0;
  int lineStartIdx = 0;
  int lineEndIdx = static_cast<int>(displayText.length());
  int textWidth = 0;
  while (true) {
    std::string lineText = displayText.substr(lineStartIdx, lineEndIdx - lineStartIdx);
    textWidth = renderer.getTextWidth(UI_12_FONT_ID, lineText.c_str());
    if (textWidth <= screenW - 2 * metrics.contentSidePadding) {
      renderer.drawCenteredText(UI_12_FONT_ID, inputStartY + inputHeight, lineText.c_str());
      if (lineEndIdx == static_cast<int>(displayText.length())) break;
      inputHeight += lineHeight;
      lineStartIdx = lineEndIdx;
      lineEndIdx = static_cast<int>(displayText.length());
    } else {
      lineEndIdx -= 1;
    }
  }
  GUI.drawTextField(renderer, Rect{0, inputStartY, screenW, inputHeight}, textWidth);

  // ---- Keyboard ----
  const int kbY = getKeyboardStartY();
  const int mode = (shiftState == 2) ? 2 : shiftState;
  const char* const* layout = touchKb[mode];

  // Rows 0-1: regular character keys
  for (int row = 0; row < 2; row++) {
    const int nKeys = static_cast<int>(strlen(layout[row]));
    const int totalW = nKeys * TK_KEY_W + (nKeys - 1) * TK_SPACING;
    const int margin = (screenW - totalW) / 2;
    const int rowYPos = kbY + row * (TK_KEY_H + TK_SPACING);

    for (int col = 0; col < nKeys; col++) {
      const int kx = margin + col * (TK_KEY_W + TK_SPACING);
      std::string label(1, layout[row][col]);
      GUI.drawKeyboardKey(renderer, Rect{kx, rowYPos, TK_KEY_W, TK_KEY_H}, label.c_str(), false);
    }
  }

  // Row 2: SHIFT/toggle + chars + BACKSPACE
  {
    const int nChars = static_cast<int>(strlen(layout[2]));
    const int charsW = nChars * TK_KEY_W + (nChars - 1) * TK_SPACING;
    const int totalW = TK_WIDE_W + TK_SPACING + charsW + TK_SPACING + TK_WIDE_W;
    const int margin = (screenW - totalW) / 2;
    const int rowYPos = kbY + 2 * (TK_KEY_H + TK_SPACING);
    int cx = margin;

    // Shift / #+= label
    const char* shiftLabel = (shiftState == 2) ? "#+=" : (shiftState == 1) ? "SHIFT" : "shift";
    GUI.drawKeyboardKey(renderer, Rect{cx, rowYPos, TK_WIDE_W, TK_KEY_H}, shiftLabel, shiftState == 1);
    cx += TK_WIDE_W + TK_SPACING;

    for (int i = 0; i < nChars; i++) {
      std::string label(1, layout[2][i]);
      GUI.drawKeyboardKey(renderer, Rect{cx, rowYPos, TK_KEY_W, TK_KEY_H}, label.c_str(), false);
      cx += TK_KEY_W + TK_SPACING;
    }

    GUI.drawKeyboardKey(renderer, Rect{cx, rowYPos, TK_WIDE_W, TK_KEY_H}, "<-", false);
  }

  // Row 3: MODE | SPACE | OK
  {
    const int margin = 4;
    const int spaceW = screenW - 2 * margin - 2 * TK_MODE_W - 2 * TK_SPACING;
    const int rowYPos = kbY + 3 * (TK_KEY_H + TK_SPACING);
    int cx = margin;

    const char* modeLabel = (shiftState == 2) ? "ABC" : "123";
    GUI.drawKeyboardKey(renderer, Rect{cx, rowYPos, TK_MODE_W, TK_KEY_H}, modeLabel, false);
    cx += TK_MODE_W + TK_SPACING;

    GUI.drawKeyboardKey(renderer, Rect{cx, rowYPos, spaceW, TK_KEY_H}, "space", false);
    cx += spaceW + TK_SPACING;

    GUI.drawKeyboardKey(renderer, Rect{cx, rowYPos, TK_MODE_W, TK_KEY_H}, tr(STR_OK_BUTTON), false);
  }

  renderer.displayBuffer();
}

// =============================================================================
