#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class Ao3FolderPickerActivity final : public Activity {
 public:
  Ao3FolderPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  ButtonNavigator buttonNavigator;
  std::string currentPath = "/";
  std::vector<std::string> directories;
  size_t selectorIndex = 0;

  void loadDirectories();
  void selectPath(const std::string& path);
};
