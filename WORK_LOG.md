# CrossPoint Reader M5Paper 포팅 작업 로그

## 대상 하드웨어
- **기기**: M5Paper (오리지널)
- **MCU**: ESP32-D0WDQ6-V3 (Xtensa LX6 듀얼코어, 240MHz)
- **PSRAM**: 8MB Quad SPI
- **디스플레이**: IT8951E (960×540 EPD)
- **원본**: CrossPoint PaperS3 (M5PaperS3, ESP32-S3 기반)

---

## 작업 1: WDT 크래시 수정 (epub 열 때 재부팅 문제)

### 증상
epub 파일 열 때 SCT 섹션 캐시를 빌드하는 도중 Task Watchdog Timer(WDT)가 트리거되어 재부팅됨.

### 원인
`Section::onPageComplete()`가 페이지 직렬화를 반복하는 동안 IDLE0 태스크에 CPU를 양보하지 않아
ESP32 WDT가 만료됨.

### 수정

**`lib/Epub/Epub/Section.cpp`**
```cpp
uint32_t Section::onPageComplete(std::unique_ptr<Page> page) {
  vTaskDelay(1);  // Yield to IDLE task to prevent WDT during long cache builds
  // ...
}
```

### 결과
epub 열 때 재부팅 없이 정상 동작 확인.

---

## 작업 2: 한국어(한글) 폰트 지원

### 목표
NotoSans 14pt 폰트에 한글 유니코드 범위를 추가하여 한글 epub 표시.

### 2-1. NotoSans 폰트 재생성

**폰트 소스 파일 다운로드**
```
lib/EpdFont/builtinFonts/source/NotoSansKR/NotoSansKR-Regular.otf (4.6MB)
lib/EpdFont/builtinFonts/source/NotoSansKR/NotoSansKR-Bold.otf   (4.8MB)
```

**생성 커맨드 (notosans_14_regular)**
```bash
fontconvert.py notosans_14_regular 14 \
  NotoSans-Regular.ttf NotoSansKR-Regular.otf \
  --2bit --compress \
  --additional-intervals 0xAC00,0xD7A3 \
  --additional-intervals 0x3130,0x318F \
  --additional-intervals 0x1100,0x11FF
```

추가된 유니코드 범위:
| 범위 | 내용 |
|------|------|
| U+AC00–U+D7A3 | 한글 음절 (가–힣, 11,172자) |
| U+1100–U+11FF | 한글 자모 (초·중·종성) |
| U+3130–U+318F | 한글 호환 자모 |

**결과물**
- `notosans_14_regularBitmaps[406290]` (약 397KB, 2비트 압축)
- 한글 음절 그룹(group 13): 11,266 글리프, 비압축 2MB

**참고: Bold/Italic/BoldItalic는 라틴 전용 유지**
한글 4개 폰트 웨이트 동시 포함 시 펌웨어 플래시 오버플로우(107%) 발생 →
Regular만 한글 포함, 나머지는 NotoSans 라틴 그대로 유지.

### 2-2. EpdFont::containsCodepoint() 구현

`getGlyph()`는 코드포인트가 없어도 대체 글리프(U+FFFD)를 반환하여
기존 `g == nullptr` 폴백 체크가 동작하지 않는 문제 수정.

**`lib/EpdFont/EpdFont.h`** — 선언 추가
```cpp
bool containsCodepoint(uint32_t cp) const;
```

**`lib/EpdFont/EpdFont.cpp`** — 구현 추가
```cpp
bool EpdFont::containsCodepoint(const uint32_t cp) const {
  const int count = data->intervalCount;
  if (count == 0) return false;
  const EpdUnicodeInterval* intervals = data->intervals;
  const auto* end = intervals + count;
  const auto it = std::upper_bound(
      intervals, end, cp,
      [](uint32_t value, const EpdUnicodeInterval& interval) { return value < interval.first; });
  if (it != intervals) {
    const auto& interval = *(it - 1);
    if (cp <= interval.last) return true;
  }
  return false;
}
```

### 2-3. EpdFontFamily::containsCodepoint() 구현

**`lib/EpdFont/EpdFontFamily.h`** — 선언 추가
```cpp
bool containsCodepoint(uint32_t cp, Style style = REGULAR) const;
```

**`lib/EpdFont/EpdFontFamily.cpp`** — 구현 추가
```cpp
bool EpdFontFamily::containsCodepoint(const uint32_t cp, const Style style) const {
  return getFont(style)->containsCodepoint(cp);
}
```

### 2-4. GfxRenderer 폴백 폰트 메커니즘

**설계 원칙**
- `renderCharImpl()`은 `fontFamily.getGlyph(cp, style)`와 `fontFamily.getData(style)`를
  동일한 폰트 객체에서 가져와야 글리프 인덱스 계산(`glyph - fontData->glyph`)이 정확함.
- 따라서 폴백은 GfxRenderer 레이어에서 코드포인트별로 EpdFontFamily를 교체하는 방식으로 구현.

**`lib/GfxRenderer/GfxRenderer.h`**
```cpp
// private
int fallbackFontId_ = -1;
const EpdFontFamily* resolveFontFamily(const EpdFontFamily& primary, uint32_t cp,
                                        EpdFontFamily::Style style) const;

// public
void setFallbackFontId(int id) { fallbackFontId_ = id; }
```

**`lib/GfxRenderer/GfxRenderer.cpp`** — resolveFontFamily() 구현
```cpp
const EpdFontFamily* GfxRenderer::resolveFontFamily(const EpdFontFamily& primary,
                                                     const uint32_t cp,
                                                     const EpdFontFamily::Style style) const {
  if (fallbackFontId_ < 0 || primary.containsCodepoint(cp, style)) {
    return &primary;
  }
  const auto it = fontMap.find(fallbackFontId_);
  if (it != fontMap.end() && it->second.containsCodepoint(cp, EpdFontFamily::REGULAR)) {
    return &it->second;
  }
  return &primary;
}
```

폴백이 적용된 함수:
- `drawText()` — epub 본문 및 UI 텍스트 렌더링
- `getTextAdvanceX()` — 단어 너비 측정 (줄 바꿈 레이아웃)
- `drawTextRotated90CW()` — 회전 텍스트 (사이드 버튼 레이블)

폴백 스타일: 기본 폰트에 코드포인트가 없으면 NotoSans REGULAR 사용
(Bold/Italic 한글 폰트 부재)

### 2-5. main.cpp 폴백 폰트 등록

**`src/main.cpp`**
```cpp
void setupDisplayAndFonts() {
  // ... 기존 insertFont 호출들 ...
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
  renderer.setFallbackFontId(NOTOSANS_14_FONT_ID);  // 한글 폴백
  LOG_DBG("MAIN", "Fonts setup");
}
```

### 2-6. SCT 캐시 버전 관리

캐시 파일 포맷/파라미터 변경 시 버전을 올려 자동 재생성을 강제함.

| 버전 | 이유 |
|------|------|
| 19 → 20 | WDT 수정 관련 (이전 세션) |
| 20 → 21 | 한글 레이아웃 메트릭 수정 (getTextAdvanceX 폴백) |
| 21 → 22 | NotoSans 14 Bold/Italic 한글 폴백 스타일 수정 |
| 22 → 23 | CJK 표시를 efont 16x16 비트맵 경로로 전환 |

**`lib/Epub/Epub/Section.cpp`**
```cpp
constexpr uint8_t SECTION_FILE_VERSION = 23;
```

---

## 플래시 메모리 사용량

| 항목 | 크기 |
|------|------|
| 전체 펌웨어 | 5,264,427 bytes |
| 플래시 사용률 | 80.3% (6,553,600 bytes 중) |
| RAM 사용률 | 1.3% (PSRAM 포함 4,521,984 bytes 중) |

---

## 동작 원리 요약

```
epub 텍스트 렌더링 흐름:

ChapterHtmlSlimParser
  └─ renderer.getTextAdvanceX(fontId, word)   ← 폴백 적용 (레이아웃)
  └─ Page 직렬화 → SCT 캐시 파일

Page 렌더링
  └─ renderer.drawText(fontId, x, y, text)    ← 폴백 적용 (렌더링)
       └─ resolveFontFamily(primary, cp)
            ├─ primary.containsCodepoint(cp)? → primary 사용
            └─ fallback.containsCodepoint(cp)? → NotoSans 14 사용
                 └─ renderCharImpl(fallbackFont, cp, ...)
                      ├─ fallbackFont.getGlyph(cp, REGULAR)   ┐ 동일 폰트
                      └─ fallbackFont.getData(REGULAR)        ┘ → 인덱스 정확
```

---

## 잔여 작업 (미완료)

- [ ] `getTextWidth()` 한글 측정 정확도 개선
  (현재 `EpdFont::getTextBounds` 레이어에서 폴백 미적용 → □ 너비로 측정)
- [ ] FontCacheManager 스캔 모드에서 폴백 폰트 사전 워밍
  (현재 Bookerly로 한글 읽을 때 KR 그룹 2MB 핫 그룹 압축해제, 느림)
- [ ] HalGPIO.cpp 터치 진단 로그 제거
- [ ] HalDisplay.cpp 임시 디스플레이 진단 printf 제거
- [ ] 전체 M5Paper 포팅 변경사항 커밋
