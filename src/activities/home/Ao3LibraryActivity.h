#pragma once

#include <array>
#include <string>
#include <vector>

#include "Ao3LibraryMetadata.h"
#include "Ao3SortFilterState.h"
#include "Ao3ViewEntry.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class Ao3LibraryActivity final : public Activity {
 public:
  Ao3LibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, size_t initialSelectorIndex = 0);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;

  enum class DisplayStatus : uint8_t { Unread, Reading, Waiting, UpdateAvailable, Finished };

 private:
  enum class ScreenState : uint8_t { Library, FilterPanel, FandomPicker, RelationshipPicker, ManagePanel };
  enum class FilterMode : uint8_t { Automatic = 0, FolderTree = 1 };

  static constexpr int PAGE_SIZE = 3;
  static constexpr int ROW_TOUCH_BASE = 320;

  ButtonNavigator buttonNavigator;
  size_t selectorIndex = 0;
  SortMode sortMode = SortMode::DATE_ADDED;
  bool ascending = false;
  SortFilterState activeState;
  SortFilterState pendingState;
  FilterMode filterMode = FilterMode::Automatic;
  ScreenState screenState = ScreenState::Library;
  std::string ao3Folder;
  int batchSize = 10;
  int overlayRowIndex = 0;
  int manageRowIndex = 0;
  size_t pickerSelectedIndex = 0;
  bool pickerHasNone = false;
  std::vector<std::string> pickerItems;
  std::vector<uint32_t> allowedHashes;
  std::vector<ViewEntry> viewEntries;

  std::array<Ao3LibraryMetadata, PAGE_SIZE> pageMetadata;
  std::array<DisplayStatus, PAGE_SIZE> pageStatus = {
      DisplayStatus::Unread, DisplayStatus::Unread, DisplayStatus::Unread};
  std::array<bool, PAGE_SIZE> pageMetadataLoaded = {false, false, false};
  std::array<std::vector<std::string>, PAGE_SIZE> wrappedSummary;
  int cachedPage = -1;

  void loadViewEntries();
  void loadSettings();
  void saveSettings() const;
  void loadSortFilterState();
  void saveSortFilterState() const;
  void buildAllowedHashes(const std::string& root, int minimumDepth);
  void buildFandomList(std::vector<std::string>& output) const;
  void buildRelationshipList(const char* fandom, std::vector<std::string>& output, bool& hasNone) const;
  void sortViewEntries();
  void loadPageCache(int page);
  void openSelected();
  void cycleSort(int direction);
  void moveSelection(int index);
  void applyPendingFilter();
  void activateFilterRow();
  void activateManageRow();
  void renderFilterOverlay();
  void renderPicker();
  void renderManagePanel();

  void renderEntry(RenderLock& lock, int y, const ViewEntry& entry, int cacheSlot, bool selected,
                   int entryHeight);
  void drawAo3Square(RenderLock& lock, int x, int y, int size, char rating, char warning,
                     bool completed, DisplayStatus status);
  void renderRatingSymbol(int x, int y, int size, char rating, bool topLeft, bool topRight,
                          bool bottomLeft, bool bottomRight, int yOffset = 0);
  void renderStatusSymbol(int x, int y, int size, DisplayStatus status, bool topLeft,
                          bool topRight, bool bottomLeft, bool bottomRight, int yOffset = 0);
  void renderWarningSymbol(int x, int y, int size, char warning, bool topLeft, bool topRight,
                           bool bottomLeft, bool bottomRight, int yOffset = 0);
  void renderCompletionSymbol(int x, int y, int size, bool completed, bool topLeft,
                              bool topRight, bool bottomLeft, bool bottomRight, int yOffset = 0);
};
