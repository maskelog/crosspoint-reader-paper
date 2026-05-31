#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../Activity.h"

class FileBrowserActivity final : public Activity {
 private:
  // Deletion
  void clearFileMetadata(const std::string& fullPath);

  size_t selectorIndex = 0;

  bool lockLongPressBack = false;
  bool ignoreConfirmRelease = false;
  uint32_t lastNavigationTime = 0;

  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;

  // Data loading
  void loadFiles();
  size_t findEntry(const std::string& name) const;
  void navigateToDirectory(const std::string& entry);
  void moveSelection(int nextIndex);

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/")
      : Activity("FileBrowser", renderer, mappedInput), basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
