#pragma once
#include <Epub.h>
#include <Epub/FootnoteEntry.h>
#include <Epub/Page.h>
#include <Epub/Section.h>

#include "EpubReaderMenuActivity.h"
#include "activities/Activity.h"

class EpubReaderActivity final : public Activity {
  struct NextChapterIndexJob {
    std::unique_ptr<Section> section;
    Section::BuildState state;
    int spineIndex = -1;
    uint16_t viewportWidth = 0;
    uint16_t viewportHeight = 0;
    bool active = false;
  };
  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  // Set when navigating to a footnote href with a fragment (e.g. #note1).
  // Cleared on the next render after the new section loads and resolves it to a page.
  std::string pendingAnchor;
  int pagesUntilFullRefresh = 0;
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  unsigned long lastPageTurnTime = 0UL;
  unsigned long pageTurnDuration = 0UL;
  // Signals that the next render should reposition within the newly loaded section
  // based on a cross-book percentage jump.
  bool pendingPercentJump = false;
  // Normalized 0.0-1.0 progress within the target spine item, computed from book percentage.
  float pendingSpineProgress = 0.0f;
  bool pendingScreenshot = false;
  bool skipNextButtonCheck = false;  // Skip button processing for one frame after subactivity exit
  bool automaticPageTurnActive = false;
  int lastSavedSpineIndex = -1;
  int lastSavedPage = -1;
  int lastSavedPageCount = -1;
  unsigned long lastProgressSaveTime = 0UL;
  std::unique_ptr<Page> prefetchedPage = nullptr;
  int prefetchedSpineIndex = -1;
  int prefetchedPageNumber = -1;
  std::unique_ptr<NextChapterIndexJob> nextChapterIndexJob = nullptr;
  Section::BuildState currentSectionBuildState;
  bool currentSectionBuildActive = false;
  bool firstBuildPageDisplayed = false;
  int currentSectionBuildSpine = -1;

  // Footnote support
  std::vector<FootnoteEntry> currentPageFootnotes;
  struct SavedPosition {
    int spineIndex;
    int pageNumber;
  };
  static constexpr int MAX_FOOTNOTE_DEPTH = 3;
  SavedPosition savedPositions[MAX_FOOTNOTE_DEPTH] = {};
  int footnoteDepth = 0;

  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  void renderStatusBar() const;
  void saveProgress(int spineIndex, int currentPage, int pageCount, bool force = false);
  std::unique_ptr<Page> loadCurrentPage();
  void clearPrefetchedPage();
  void prefetchNextPage();
  void scheduleNextChapterIndex(uint16_t viewportWidth, uint16_t viewportHeight);
  void pumpNextChapterIndexJob();
  void cancelNextChapterIndexJob();
  bool startCurrentSectionBuild(uint16_t viewportWidth, uint16_t viewportHeight);
  bool pumpCurrentSectionBuildUntilFirstPage();
  void pumpCurrentSectionBuildJob();
  void cancelCurrentSectionBuildJob();
  // Jump to a percentage of the book (0-100), mapping it to spine and page.
  void jumpToPercent(int percent);
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action);
  void applyOrientation(uint8_t orientation);
  void toggleAutoPageTurn(uint8_t selectedPageTurnOption);
  void pageTurn(bool isForwardTurn);

  // Footnote navigation
  void navigateToHref(const std::string& href, bool savePosition = false);
  void restoreSavedPosition();

 public:
  explicit EpubReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Epub> epub)
      : Activity("EpubReader", renderer, mappedInput), epub(std::move(epub)) {}
  ~EpubReaderActivity() override;
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool isReaderActivity() const override { return true; }
};
