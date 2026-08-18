#include "Ao3FolderPickerActivity.h"

#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>
#include <strings.h>
#include <utility>

#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long ENTER_FOLDER_HOLD_MS = 1000;
}

Ao3FolderPickerActivity::Ao3FolderPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Ao3FolderPicker", renderer, mappedInput) {}

void Ao3FolderPickerActivity::loadDirectories() {
  directories.clear();
  HalFile root = Storage.open(currentPath.c_str());
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }

  root.rewindDirectory();
  char name[256];
  HalFile file;
  while ((file = root.openNextFile())) {
    file.getName(name, sizeof(name));
    if (name[0] != '.' && file.isDirectory() && strcmp(name, "System Volume Information") != 0 &&
        strcmp(name, ".crosspoint") != 0) {
      directories.emplace_back(name);
    }
    file.close();
  }
  root.close();
  std::sort(directories.begin(), directories.end(), [](const std::string& a, const std::string& b) {
    return strcasecmp(a.c_str(), b.c_str()) < 0;
  });
}

void Ao3FolderPickerActivity::selectPath(const std::string& path) {
  ActivityResult result{FilePathResult{path}};
  result.isCancelled = false;
  setResult(std::move(result));
  finish();
}

void Ao3FolderPickerActivity::onEnter() {
  Activity::onEnter();
  buttonNavigator.setMappedInputManager(mappedInput);
  currentPath = "/";
  selectorIndex = 0;
  loadDirectories();
  requestUpdate(true);
}

void Ao3FolderPickerActivity::onExit() {
  Activity::onExit();
  directories.clear();
}

void Ao3FolderPickerActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (currentPath == "/") {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
    } else {
      const size_t slash = currentPath.find_last_of('/');
      currentPath = slash == 0 ? "/" : currentPath.substr(0, slash);
      selectorIndex = 0;
      loadDirectories();
      requestUpdate(true);
    }
    return;
  }

  const int listSize = static_cast<int>(directories.size()) + 1;
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && listSize > 0) {
    if (selectorIndex == 0) {
      selectPath(currentPath);
      return;
    }
    const size_t directoryIndex = selectorIndex - 1;
    std::string path = currentPath;
    if (path.back() != '/') path += '/';
    path += directories[directoryIndex];
    if (mappedInput.getHeldTime() >= ENTER_FOLDER_HOLD_MS) {
      currentPath = std::move(path);
      selectorIndex = 0;
      loadDirectories();
      requestUpdate(true);
    } else {
      selectPath(path);
    }
    return;
  }

  if (listSize <= 0) return;
  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate(true);
  });
  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate(true);
  });
}

void Ao3FolderPickerActivity::render(RenderLock&&) {
  renderer.clearScreen(0xFF);
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect header{0, metrics.topPadding, pageWidth, metrics.headerHeight};
  GUI.drawHeader(renderer, header, "Select AO3 Folder");

  std::string displayPath = "SD Card" + currentPath;
  if (displayPath.back() != '/') displayPath += '/';
  displayPath = renderer.truncatedText(UI_10_FONT_ID, displayPath.c_str(), pageWidth - metrics.contentSidePadding * 2);
  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, header.y + header.height + 8,
                    displayPath.c_str(), true, EpdFontFamily::BOLD);

  const int contentTop = header.y + header.height + renderer.getLineHeight(UI_10_FONT_ID) + 18;
  const int helperHeight = renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - helperHeight - metrics.verticalSpacing;
  const int listSize = static_cast<int>(directories.size()) + 1;
  if (listSize == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + 20, "No folders found.");
  } else {
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, listSize,
                 static_cast<int>(selectorIndex), [this](const int index) {
                   if (index == 0) return currentPath == "/" ? std::string("Use SD Card root")
                                                               : std::string("Use this folder");
                   const int directoryIndex = index - 1;
                   return directories[directoryIndex] + "/";
                 });
  }

  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - metrics.buttonHintsHeight - helperHeight,
                            "Hold Select to open a folder");
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
