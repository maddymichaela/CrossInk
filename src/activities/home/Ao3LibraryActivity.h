#pragma once

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>

#include <atomic>
#include <string>
#include <vector>

#include "Ao3LibraryMetadata.h"
#include "Ao3SortFilterState.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class Ao3LibraryActivity final : public Activity {
 public:
  Ao3LibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, size_t initialSelectorIndex = 0);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  struct Row {
    Ao3LibraryMetadata metadata;
    std::string fandom;
    std::string relationship1;
    std::string relationship2;
    uint32_t addedSequence = 0;
    std::string status;
    std::string subtitle;
  };

 private:
  using UiApp = freeink::ui::FreeInkApp<20, 4>;

  ButtonNavigator buttonNavigator;
  size_t selectorIndex = 0;
  SortMode sortMode = SortMode::DATE_ADDED;
  bool ascending = false;

  std::vector<Row> rows;

  freeink::ui::GfxRendererTarget uiTarget;
  UiApp app;
  std::atomic<bool> uiReady{false};
  int visibleRows = 1;
  int topIndex = 0;
  int listTop = 0;
  int listBottom = 0;
  int listRowHeight = 0;
  int listRowStep = 0;

  static void listScreen(UiApp::ScreenType& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);

  void loadRows();
  void sortRows();
  void rebuildSubtitles();
  void buildListScreen(UiApp::ScreenType& screen);
  void openSelected();
  void cycleSort(int direction);
};
