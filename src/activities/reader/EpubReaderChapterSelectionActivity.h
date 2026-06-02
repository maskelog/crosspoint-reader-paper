#pragma once
#include <Epub.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class EpubReaderChapterSelectionActivity final : public Activity {
  struct CachedTocItem {
    bool loaded = false;
    int level = 1;
    int maxWidth = -1;
    std::string title;
    std::string displayTitle;
  };

  std::shared_ptr<Epub> epub;
  std::string epubPath;
  int currentSpineIndex = 0;
  int selectorIndex = 0;
  uint32_t lastNavigationTime = 0;
  std::vector<CachedTocItem> tocCache;

  // Number of items that fit on a page, derived from logical screen height.
  // This adapts automatically when switching between portrait and landscape.
  int getPageItems() const;

  // Total TOC items count
  int getTotalItems() const;
  void moveSelection(int nextIndex);
  void confirmSelection();

 public:
  explicit EpubReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                              const std::shared_ptr<Epub>& epub, const std::string& epubPath,
                                              const int currentSpineIndex)
      : Activity("EpubReaderChapterSelection", renderer, mappedInput),
        epub(epub),
        epubPath(epubPath),
        currentSpineIndex(currentSpineIndex) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
};
