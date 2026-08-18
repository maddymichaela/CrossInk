#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class Ao3IgnoredFoldersActivity final : public Activity {
 public:
  Ao3IgnoredFoldersActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                            std::vector<std::string> ignoredFolders, std::string protectedRoot);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  ButtonNavigator buttonNavigator;
  std::string currentPath = "/";
  std::string protectedRoot;
  std::vector<std::string> directories;
  std::vector<std::string> ignoredFolders;
  size_t selectorIndex = 0;

  void loadDirectories();
  void finishSelection();
  void toggleIgnored(const std::string& path);
  bool isIgnored(const std::string& path) const;
  std::string childPath(size_t directoryIndex) const;
};
