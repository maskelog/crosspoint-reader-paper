#include "Activity.h"

#include "ActivityManager.h"
#include "components/UITheme.h"

void Activity::onEnter() {
  LOG_DBG("ACT", "Entering activity: %s", name.c_str());
  mappedInput.clearState();  // Prevent stale touches from triggering actions in the new activity
  if (!isReaderActivity()) {
    // Non-reader activities always render in portrait so footer buttons match
    // the physical bottom of the device where drawButtonHints draws them.
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  }
  mappedInput.setTouchOrientation(renderer.getOrientation());
  // Enable footer nav buttons for all non-reader activities; readers use full screen for content
  mappedInput.setFooterHeight(isReaderActivity() ? 0 : UITheme::getInstance().getMetrics().buttonHintsHeight);
}

void Activity::onExit() { LOG_DBG("ACT", "Exiting activity: %s", name.c_str()); }

void Activity::requestUpdate(bool immediate) { activityManager.requestUpdate(immediate); }

void Activity::requestUpdateAndWait() { activityManager.requestUpdateAndWait(); }

void Activity::onGoHome() { activityManager.goHome(); }

void Activity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void Activity::startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler) {
  this->resultHandler = std::move(resultHandler);
  activityManager.pushActivity(std::move(activity));
}

void Activity::setResult(ActivityResult&& result) { this->result = std::move(result); }

void Activity::finish() { activityManager.popActivity(); }
