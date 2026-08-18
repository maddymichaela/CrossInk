#include "Ao3IgnoredFoldersActivity.h"

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

std::string normalizedPath(std::string path) {
  while (path.size() > 1 && path.back() == '/') path.pop_back();
  return path.empty() ? "/" : path;
}

bool isSameOrChild(const std::string& path, const std::string& root) {
  if (path == root) return true;
  if (root == "/") return !path.empty() && path.front() == '/';
  return path.size() > root.size() && path.compare(0, root.size(), root) == 0 && path[root.size()] == '/';
}
}  // namespace

Ao3IgnoredFoldersActivity::Ao3IgnoredFoldersActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     std::vector<std::string> ignoredFolders,
                                                     std::string protectedRoot)
    : Activity("Ao3IgnoredFolders", renderer, mappedInput),
      protectedRoot(normalizedPath(std::move(protectedRoot))),
      ignoredFolders(std::move(ignoredFolders)) {
  for (std::string& path : this->ignoredFolders) path = normalizedPath(std::move(path));
  std::sort(this->ignoredFolders.begin(), this->ignoredFolders.end());
  this->ignoredFolders.erase(std::unique(this->ignoredFolders.begin(), this->ignoredFolders.end()),
                             this->ignoredFolders.end());
}

void Ao3IgnoredFoldersActivity::loadDirectories() {
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

std::string Ao3IgnoredFoldersActivity::childPath(const size_t directoryIndex) const {
  std::string path = currentPath;
  if (path.back() != '/') path += '/';
  path += directories[directoryIndex];
  return path;
}

bool Ao3IgnoredFoldersActivity::isIgnored(const std::string& path) const {
  return std::find(ignoredFolders.begin(), ignoredFolders.end(), path) != ignoredFolders.end();
}

void Ao3IgnoredFoldersActivity::toggleIgnored(const std::string& path) {
  const std::string normalized = normalizedPath(path);
  if (normalized == "/" || isSameOrChild(protectedRoot, normalized)) return;
  const auto exact = std::find(ignoredFolders.begin(), ignoredFolders.end(), normalized);
  if (exact != ignoredFolders.end()) {
    ignoredFolders.erase(exact);
    return;
  }

  ignoredFolders.erase(std::remove_if(ignoredFolders.begin(), ignoredFolders.end(),
                                      [&](const std::string& existing) { return isSameOrChild(existing, normalized); }),
                       ignoredFolders.end());
  ignoredFolders.push_back(normalized);
  std::sort(ignoredFolders.begin(), ignoredFolders.end());
}

void Ao3IgnoredFoldersActivity::finishSelection() {
  ActivityResult result{FolderListResult{std::move(ignoredFolders)}};
  result.isCancelled = false;
  setResult(std::move(result));
  finish();
}

void Ao3IgnoredFoldersActivity::onEnter() {
  Activity::onEnter();
  buttonNavigator.setMappedInputManager(mappedInput);
  currentPath = "/";
  selectorIndex = 0;
  loadDirectories();
  requestUpdate(true);
}

void Ao3IgnoredFoldersActivity::onExit() {
  Activity::onExit();
  directories.clear();
}

void Ao3IgnoredFoldersActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (currentPath == "/") {
      finishSelection();
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
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (currentPath == "/" && selectorIndex == 0) {
      finishSelection();
      return;
    }
    if (currentPath != "/" && selectorIndex == 0) {
      toggleIgnored(currentPath);
      requestUpdate(true);
      return;
    }

    const size_t directoryIndex = selectorIndex - 1;
    const std::string path = childPath(directoryIndex);
    if (mappedInput.getHeldTime() >= ENTER_FOLDER_HOLD_MS) {
      currentPath = path;
      selectorIndex = 0;
      loadDirectories();
    } else {
      toggleIgnored(path);
    }
    requestUpdate(true);
    return;
  }

  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate(true);
  });
  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate(true);
  });
}

void Ao3IgnoredFoldersActivity::render(RenderLock&&) {
  renderer.clearScreen(0xFF);
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect header{0, metrics.topPadding, pageWidth, metrics.headerHeight};
  GUI.drawHeader(renderer, header, "Ignored Folders");

  std::string displayPath = "SD Card" + currentPath;
  if (displayPath.back() != '/') displayPath += '/';
  displayPath = renderer.truncatedText(UI_10_FONT_ID, displayPath.c_str(), pageWidth - metrics.contentSidePadding * 2);
  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, header.y + header.height + 8,
                    displayPath.c_str(), true, EpdFontFamily::BOLD);

  const int contentTop = header.y + header.height + renderer.getLineHeight(UI_10_FONT_ID) + 18;
  const int helperHeight = renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - helperHeight - metrics.verticalSpacing;
  const int listSize = static_cast<int>(directories.size()) + 1;
  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, listSize, static_cast<int>(selectorIndex),
               [this](const int index) {
                 if (index == 0) {
                   if (currentPath == "/") {
                     return std::string("Done (") + std::to_string(ignoredFolders.size()) + " selected)";
                   }
                   if (isSameOrChild(protectedRoot, currentPath)) return std::string("AO3 path (cannot ignore)");
                   return std::string(isIgnored(currentPath) ? "[x] " : "[ ] ") + "Ignore this folder";
                 }
                 const std::string path = childPath(static_cast<size_t>(index - 1));
                 const bool protectedPath = isSameOrChild(protectedRoot, path);
                 return std::string(protectedPath ? "[-] " : (isIgnored(path) ? "[x] " : "[ ] ")) +
                        directories[static_cast<size_t>(index - 1)] + "/";
               });

  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - metrics.buttonHintsHeight - helperHeight,
                            "Select toggles; hold Select opens folder");
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
