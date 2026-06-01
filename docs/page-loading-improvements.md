# Page Loading Improvement Plan for M5Paper

This document summarizes the current EPUB page loading path on M5Paper and the
next changes to improve perceived page-turn latency without reintroducing WDT
resets.

## Current Observations

- The reader loads a section cache on chapter entry. If the cache is missing or
  invalid, the UI blocks while `Section::createSectionFile()` parses HTML,
  lays out pages, serializes them, and writes the section cache.
- A cached section avoids the indexing path, but each page turn still performs a
  font prewarm scan, a real render pass, status bar render, and an EPD refresh.
- Korean/CJK text increases font cost because compressed font groups can be
  large. Current code prevents WDT by yielding during decompression, but the
  first page using a large group still has a visible delay.
- M5Paper display refresh time is a hard floor. Software improvements can reduce
  parsing, font, and render time, but cannot remove the EPD transfer/refresh
  cost.

## Current Code Path

### Chapter Entry

Evidence:

- `src/activities/reader/EpubReaderActivity.cpp:563` creates `Section` only when
  the current chapter section is not loaded.
- `src/activities/reader/EpubReaderActivity.cpp:571` tries
  `Section::loadSectionFile()`.
- `src/activities/reader/EpubReaderActivity.cpp:579` falls back to
  `Section::createSectionFile()` and shows `STR_INDEXING`.
- `lib/Epub/Epub/Section.cpp:66` validates the section cache header against
  font, viewport, hyphenation, embedded style, and image settings.
- `lib/Epub/Epub/Section.cpp:140` builds the section cache when validation
  fails or the cache file is missing.

Implication:

- Any reader setting that changes cache parameters invalidates the whole chapter
  cache.
- The cache is per section/chapter, not per page. A long chapter can block the
  UI for a long time before the first page appears.

### Section Build

Evidence:

- `lib/Epub/Epub/Section.cpp:171` streams the spine item to a temporary HTML
  file.
- `lib/Epub/Epub/Section.cpp:212` constructs `ChapterHtmlSlimParser`.
- `lib/Epub/Epub/Section.cpp:218` runs `parseAndBuildPages()`.
- `lib/Epub/Epub/Section.cpp:232` writes a page LUT after all pages are built.
- `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp:1006` reads and parses the
  HTML in chunks.
- `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp:1116` performs paragraph
  layout via `TextBlock::layoutAndExtractLines()`.

Implication:

- The first-page delay is dominated by full-section work, not only by rendering
  the visible page.
- The current file format writes the final LUT near the end, so true incremental
  first-page display requires either a format change or a temporary in-memory
  first-page path.

### Page Render

Evidence:

- `src/activities/reader/EpubReaderActivity.cpp:735` starts a font prewarm scan.
- `src/activities/reader/EpubReaderActivity.cpp:736` renders the page once in
  scan mode.
- `src/activities/reader/EpubReaderActivity.cpp:737` prewarms glyph cache.
- `src/activities/reader/EpubReaderActivity.cpp:752` renders the page for real.
- `src/activities/reader/EpubReaderActivity.cpp:778` logs
  `Page render: prewarm=... render=... display=... total=...`.
- `lib/GfxRenderer/FontCacheManager.cpp:71` keeps the CJK hot group alive and
  clears only per-page cache.
- `lib/EpdFont/FontDecompressor.cpp:69` uses streaming inflate with WDT yields.

Implication:

- Cached page turns still do two logical page traversals: scan/prewarm and real
  render.
- The log already has enough timing fields to distinguish font, render, and
  display costs.

## Improvement Goals

1. Keep WDT stability. No long CPU-bound loop should run without `vTaskDelay(1)`
   or equivalent yielding.
2. Show the next visible page sooner, even if background cache building continues
   later.
3. Avoid repeated CJK font decompression on common page turns.
4. Keep memory bounded on ESP32 M5Paper. Large buffers should live in PSRAM and
   should not be repeatedly allocated/freed in hot paths.
5. Preserve cache correctness when reader settings, viewport, font, or image
   settings change.

## Proposed Work

### Phase 1: Measurement and Low-Risk Wins

Add more timing logs around the existing synchronous path.

Recommended logs:

- `Section::createSectionFile()`
  - ZIP stream to temp HTML time.
  - CSS cache load time.
  - parser/layout total time.
  - section file write/LUT patch time.
- `ChapterHtmlSlimParser::makePages()`
  - accumulated layout time.
  - number of blocks and lines processed.
- `EpubReaderActivity::renderContents()`
  - keep existing `prewarm`, `render`, `display`, `total` log.

Low-risk changes:

- Skip the prewarm scan for pages with very small text payload or when the same
  font group is already hot.
- Do not run status bar rendering through compressed fallback fonts when an
  external fixed CJK status font is selected.
- Debounce progress saves so page turns do not write progress on every page.

Expected result:

- Better evidence before changing cache format.
- Reduced latency on cached page turns where font prewarm currently costs more
  than it saves.

### Phase 2: Next-Page Cache and Font Warm Retention

Add a small in-memory next-page cache after rendering the current page.

Design:

- After `section->loadPageFromSectionFile()` succeeds, opportunistically load
  `currentPage + 1` from the same section file into a retained `Page` object.
- Keep only one prefetched page to avoid PSRAM growth.
- Invalidate the prefetch on chapter change, orientation change, font setting
  change, cache clear, percent jump, or anchor jump.
- Never prefetch while a render lock is active or when heap is below a defined
  threshold.

Why this helps:

- SD seek/read and page deserialization move out of the immediate page-turn path.
- It does not change the section cache format.

Risks:

- `Page` object memory must be measured. If a page with images or many elements
  is too large, prefetch should be skipped.
- Prefetch must not run synchronously before `displayBuffer()` completes, or it
  will worsen perceived latency.

### Phase 3: Non-Blocking Chapter Pre-Indexing

Rework `silentIndexNextChapterIfNeeded()` so it does not block the UI.

Current issue:

- `src/activities/reader/EpubReaderActivity.cpp:678` has a synchronous silent
  next-chapter indexing function. If called directly on a large chapter, it can
  block page turns and trigger WDT unless every inner loop yields.

Proposed design:

- Create an `IndexingJob` state machine that advances in small slices from the
  main loop.
- Each slice should have a time budget, for example 10-20 ms.
- The job should be cancelable when the user turns page, opens menu, changes
  settings, or leaves the reader.
- Persist only complete section cache files. Use temp files and atomic rename or
  explicit remove-on-cancel.

Expected result:

- Penultimate-page pre-indexing returns without blocking the current UI loop.
- Next chapter may already be cached by the time the user reaches it.

Risks:

- Expat parser and current `ChapterHtmlSlimParser` are written as one blocking
  parse call. A true sliced parser may require restructuring parser state so it
  can pause between input chunks.
- The section cache LUT is written after page generation. Incremental writes need
  careful file format handling.

### Phase 4: First-Page-First Section Build

For cache misses, show page 0 as soon as it is laid out, while the rest of the
chapter continues indexing.

Possible approach:

- During `createSectionFile()`, allow parser callback to expose the first
  completed `Page` before the full LUT is finished.
- Render that page from memory.
- Continue building the complete section cache in a background/sliced job.

Required format decision:

- Keep current cache format and treat first-page display as a temporary
  non-persisted path.
- Or introduce a new section cache version with a provisional LUT region.

Recommendation:

- Start with the temporary first-page path. It has less risk and can be removed
  if the final cache build fails.

## Verification Plan

Use serial monitor logs with a cold cache and warm cache.

Cold cache test:

1. Remove the relevant section cache under `/.crosspoint/epub_*/sections/`.
2. Open a large Korean EPUB chapter.
3. Capture:
   - time from `Loading file` to first visible page.
   - `Time to parse and build pages`.
   - `Page render: prewarm=... render=... display=... total=...`.
   - WDT absence.

Warm cache test:

1. Open the same chapter after cache exists.
2. Turn 10 pages forward.
3. Capture median and worst page-turn log values.
4. Confirm there is no unexpected `Cache not found, building...`.

Regression checks:

- Change font size or margins and confirm cache invalidation still happens.
- Turn Korean pages with CJK text and confirm no `task_wdt` backtrace.
- Enter next chapter and confirm `indexing` does not block indefinitely.
- Open image-heavy pages and confirm memory remains above the configured heap
  threshold.

## Priority

1. Add detailed timing logs.
2. Implement one-page prefetch for warm-cache page turns.
3. Add prewarm skip heuristics for small or already-hot pages.
4. Convert silent next-chapter indexing into a cancelable sliced job.
5. Add first-page-first cache-miss rendering.

## Implementation Status

- Phase 1 timing logs: implemented.
- Warm-cache next-page prefetch: implemented with one retained `Page`.
- Progress save debounce: implemented with forced save on reader exit.
- Image-only prewarm skip: implemented.
- Sliced parser API: implemented through `ChapterHtmlSlimParser::beginParse()`,
  `parseNextChunk()`, and `finishParse()`.
- Next-chapter indexing job: implemented as a queued reader job that advances
  from `EpubReaderActivity::loop()` and cancels on user input or reader exit.
- First-page-first rendering for cold cache misses: implemented for page 0
  without pending anchors or percent jumps. The first generated page is cloned in
  memory and rendered before the section cache LUT is finalized.

Remaining work:

- More aggressive prewarm skip heuristics for text pages where the hot CJK font
  group already covers the whole page.
