#pragma once

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>

#include <atomic>
#include <string>
#include <vector>

#include "Ao3CompactIndexRecord.h"
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

  enum class DisplayStatus : uint8_t { Unread, Reading, Waiting, UpdateAvailable, Finished };

  struct Row {
    char title[64] = {};
    char author[32] = {};
    char seriesName[32] = {};
    char fandom[32] = {};
    uint32_t wordCount = 0;
    uint32_t addedSequence = 0;
    uint32_t cacheHash = 0;
    uint16_t seriesPart = 0;
    DisplayStatus status = DisplayStatus::Unread;
    bool present = false;
  };
  static_assert(sizeof(Row) <= 192, "AO3 library rows must remain compact");

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
  void buildListScreen(UiApp::ScreenType& screen);
  void openSelected();
  void cycleSort(int direction);
};
