#include "Ao3LibraryActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <ZipFile.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "Ao3CompactIndexRecord.h"
#include "Ao3Librarian.h"
#include "MappedInputManager.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;
constexpr char AO3_INDEX_PATH[] = "/.crosspoint/ao3_library_index.bin";
constexpr uint8_t AO3_INDEX_VERSION = 1;

uint32_t ao3PathHash(const char* path) {
  if (!path || path[0] == '\0') return 0;
  return static_cast<uint32_t>(ZipFile::fnvHash64(path, strlen(path)));
}

std::string compactField(const char* value) {
  return value && value[0] != '\0' ? std::string(value) : std::string{};
}

std::string wordsLabel(uint32_t wordCount) {
  if (wordCount >= 1000) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%luk words", static_cast<unsigned long>((wordCount + 500) / 1000));
    return buf;
  }
  return std::to_string(wordCount) + " words";
}

std::string subtitleFor(const Ao3LibraryActivity::Row& row) {
  std::string subtitle = compactField(row.metadata.author);
  if (row.metadata.wordCount > 0) {
    if (!subtitle.empty()) subtitle += " · ";
    subtitle += wordsLabel(row.metadata.wordCount);
  }
  if (!row.fandom.empty()) {
    if (!subtitle.empty()) subtitle += " · ";
    subtitle += row.fandom;
  }
  return subtitle;
}

int compareText(const std::string& a, const std::string& b) {
  const size_t count = std::min(a.size(), b.size());
  for (size_t i = 0; i < count; ++i) {
    const char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
    const char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(b[i])));
    if (ca < cb) return -1;
    if (ca > cb) return 1;
  }
  if (a.size() < b.size()) return -1;
  if (a.size() > b.size()) return 1;
  return 0;
}

bool readIndexRecordCount(HalFile& file, uint16_t& recordCount) {
  char magic[4];
  uint8_t version = 0;
  if (file.read(magic, 4) != 4 || file.read(&version, 1) != 1 ||
      file.read(reinterpret_cast<uint8_t*>(&recordCount), sizeof(recordCount)) != sizeof(recordCount)) {
    return false;
  }
  return memcmp(magic, "AO3X", 4) == 0 && version == AO3_INDEX_VERSION;
}

void enrichRowsFromIndex(std::vector<Ao3LibraryActivity::Row>& rows) {
  FsFile file;
  if (!Storage.openFileForRead("AO3", AO3_INDEX_PATH, file)) {
    return;
  }

  uint16_t recordCount = 0;
  if (!readIndexRecordCount(file, recordCount)) {
    file.close();
    return;
  }

  uint32_t nextSequence = 0;
  uint8_t reserved = 0;
  file.read(reinterpret_cast<uint8_t*>(&nextSequence), sizeof(nextSequence));
  file.read(&reserved, sizeof(reserved));

  for (uint16_t i = 0; i < recordCount; ++i) {
    CompactIndexRecord rec;
    if (file.read(reinterpret_cast<uint8_t*>(&rec), sizeof(rec)) != sizeof(rec)) {
      break;
    }
    if (rec.flags & 0x01) {
      continue;
    }

    for (auto& row : rows) {
      if (ao3PathHash(row.metadata.filepath) != rec.cacheHash) {
        continue;
      }
      row.fandom = compactField(rec.fandom);
      row.relationship1 = compactField(rec.relationship1);
      row.relationship2 = compactField(rec.relationship2);
      row.addedSequence = rec.addedSequence;
      break;
    }
  }
  file.close();
}

const char* sortLabel(SortMode mode) {
  switch (mode) {
    case SortMode::ALPHABETIC:
      return "Title";
    case SortMode::WORD_COUNT:
      return "Words";
    case SortMode::DATE_ADDED:
      return "Recent";
    case SortMode::SERIES:
      return "Series";
    case SortMode::AUTHOR:
      return "Author";
  }
  return "Title";
}

SortMode nextSortMode(SortMode mode, int direction) {
  constexpr SortMode modes[] = {
      SortMode::ALPHABETIC,
      SortMode::AUTHOR,
      SortMode::WORD_COUNT,
      SortMode::DATE_ADDED,
      SortMode::SERIES,
  };
  constexpr int count = static_cast<int>(sizeof(modes) / sizeof(modes[0]));
  int index = 0;
  for (int i = 0; i < count; ++i) {
    if (modes[i] == mode) {
      index = i;
      break;
    }
  }
  index = (index + direction + count) % count;
  return modes[index];
}
}  // namespace

Ao3LibraryActivity::Ao3LibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       const size_t initialSelectorIndex)
    : Activity("Ao3Library", renderer, mappedInput),
      selectorIndex(initialSelectorIndex),
      uiTarget(makeUiTarget(renderer)),
      app(uiTarget, uiTarget.deviceContext()) {}

void Ao3LibraryActivity::onEnter() {
  Activity::onEnter();
  loadRows();
  if (rows.empty()) {
    selectorIndex = 0;
  } else if (selectorIndex >= rows.size()) {
    selectorIndex = rows.size() - 1;
  }
  topIndex = followListSelection(static_cast<int>(selectorIndex), 0, visibleRows, static_cast<int>(rows.size()));
  uiReady = false;
  app.setTheme(uiThemeTokens(uiTarget));
  app.on(ACTION_ROW, &Ao3LibraryActivity::onRowEvent, this);
  app.setScreen(&Ao3LibraryActivity::listScreen, this);
  requestUpdate();
}

void Ao3LibraryActivity::onExit() {
  Activity::onExit();
  rows.clear();
}

void Ao3LibraryActivity::loadRows() {
  rows.clear();
  std::vector<Ao3LibraryMetadata> metadata;
  Ao3Librarian::sanitizeIndex();
  Ao3Librarian::scanGlobalLibrary(metadata);
  rows.reserve(metadata.size());
  for (const auto& meta : metadata) {
    if (meta.filepath[0] == '\0' || !Storage.exists(meta.filepath)) {
      continue;
    }
    Row row;
    row.metadata = meta;
    rows.push_back(row);
  }
  enrichRowsFromIndex(rows);
  sortRows();
  rebuildSubtitles();
}

void Ao3LibraryActivity::sortRows() {
  const bool ascendingSort = ascending;
  std::sort(rows.begin(), rows.end(), [this, ascendingSort](const Row& a, const Row& b) {
    int cmp = 0;
    switch (sortMode) {
      case SortMode::AUTHOR:
        cmp = compareText(compactField(a.metadata.author), compactField(b.metadata.author));
        break;
      case SortMode::WORD_COUNT:
        cmp = (a.metadata.wordCount < b.metadata.wordCount) ? -1 : (a.metadata.wordCount > b.metadata.wordCount ? 1 : 0);
        break;
      case SortMode::DATE_ADDED:
        cmp = (a.addedSequence < b.addedSequence) ? -1 : (a.addedSequence > b.addedSequence ? 1 : 0);
        break;
      case SortMode::SERIES:
        cmp = compareText(compactField(a.metadata.seriesName), compactField(b.metadata.seriesName));
        if (cmp == 0) {
          cmp = (a.metadata.seriesPart < b.metadata.seriesPart) ? -1
                                                               : (a.metadata.seriesPart > b.metadata.seriesPart ? 1 : 0);
        }
        break;
      case SortMode::ALPHABETIC:
      default:
        cmp = compareText(compactField(a.metadata.title), compactField(b.metadata.title));
        break;
    }
    if (cmp == 0) {
      cmp = compareText(compactField(a.metadata.title), compactField(b.metadata.title));
    }
    return ascendingSort ? cmp < 0 : cmp > 0;
  });
}

void Ao3LibraryActivity::rebuildSubtitles() {
  for (auto& row : rows) {
    row.subtitle = subtitleFor(row);
  }
}

void Ao3LibraryActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<Ao3LibraryActivity*>(user);
  if (event.value < 0 || event.value >= static_cast<int16_t>(self->rows.size())) return;
  self->selectorIndex = static_cast<size_t>(event.value);
  self->app.clearTapFlash();
  self->openSelected();
}

void Ao3LibraryActivity::loop() {
  if (TouchHeaderBackButton::wasTapped(mappedInput, renderer) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome(HomeMenuItem::AO3_LIBRARY);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelected();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    cycleSort(-1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    cycleSort(1);
    return;
  }

  if (uiReady) {
    const fui::InputSnapshot snap = touchSnapshotFrom(mappedInput);
    if (snap.touchPressed || snap.touchReleased) {
      const auto event = app.route(snap);
      if (app.invalidated()) requestUpdate();
      if (event) return;
    }
  }

  const int listSize = static_cast<int>(rows.size());
  if (listSize <= 0) {
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    const int next = scrollListBy(topIndex, swipe == MappedInputManager::SwipeDir::Up ? visibleRows : -visibleRows,
                                  visibleRows, listSize);
    if (next != topIndex) {
      topIndex = next;
      requestUpdate();
    }
    return;
  }

  const auto moveSelection = [this, listSize](const int next) {
    selectorIndex = static_cast<size_t>(next);
    topIndex = followListSelection(static_cast<int>(selectorIndex), topIndex, visibleRows, listSize);
    requestUpdate();
  };
  buttonNavigator.onNextRelease([this, listSize, &moveSelection] {
    moveSelection(ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize));
  });
  buttonNavigator.onPreviousRelease([this, listSize, &moveSelection] {
    moveSelection(ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize));
  });
  buttonNavigator.onNextContinuous([this, listSize, &moveSelection] {
    moveSelection(ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, visibleRows));
  });
  buttonNavigator.onPreviousContinuous([this, listSize, &moveSelection] {
    moveSelection(ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, visibleRows));
  });
}

void Ao3LibraryActivity::cycleSort(const int direction) {
  sortMode = nextSortMode(sortMode, direction);
  ascending = sortMode == SortMode::ALPHABETIC || sortMode == SortMode::AUTHOR || sortMode == SortMode::SERIES;
  sortRows();
  rebuildSubtitles();
  if (!rows.empty() && selectorIndex >= rows.size()) selectorIndex = rows.size() - 1;
  topIndex = followListSelection(static_cast<int>(selectorIndex), topIndex, visibleRows, static_cast<int>(rows.size()));
  requestUpdate();
}

void Ao3LibraryActivity::openSelected() {
  if (rows.empty() || selectorIndex >= rows.size()) {
    return;
  }
  onSelectBook(rows[selectorIndex].metadata.filepath);
}

void Ao3LibraryActivity::listScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<Ao3LibraryActivity*>(user)->buildListScreen(screen);
}

void Ao3LibraryActivity::buildListScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + TouchHeaderBackButton::height(metrics, mappedInput)), 0,
                  static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (rows.empty()) {
    screen.centeredText("No indexed AO3 works", screen.theme().bodyText);
    return;
  }

  std::vector<fui::ListItem> items;
  items.reserve(rows.size());
  for (size_t i = 0; i < rows.size(); ++i) {
    fui::ListItem item;
    item.label = rows[i].metadata.title[0] ? rows[i].metadata.title : "(Untitled)";
    if (!rows[i].subtitle.empty()) item.subtitle = rows[i].subtitle.c_str();
    item.icon = listIconFor(UIIcon::Book, 32);
    item.actionValue = static_cast<int16_t>(i);
    items.push_back(item);
  }

  fui::ListProps props;
  props.items = items.data();
  props.count = static_cast<uint16_t>(items.size());
  props.selectedIndex = static_cast<int16_t>(selectorIndex);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.iconSize = 28;
  props.labelText = screen.theme().bodyText;
  props.labelText.bold = true;
  const fui::Rect listBounds = screen.body();
  listTop = listBounds.y;
  listBottom = listBounds.bottom();
  const auto visible = configureUiList(props, screen.theme(), listBounds, UiListRowType::WithSubtitle);
  listRowHeight = props.rowHeight;
  listRowStep = listRowHeight + props.rowGap;
  visibleRows = visible > 0 ? visible : 1;
  topIndex = scrollListBy(topIndex, 0, visibleRows, static_cast<int>(rows.size()));
  props.topIndex = static_cast<uint16_t>(topIndex);
  screen.list(props);
}

void Ao3LibraryActivity::render(RenderLock&&) {
  renderer.clearScreen();

  char title[48];
  snprintf(title, sizeof(title), "AO3 Library · %s", sortLabel(sortMode));
  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::draw(renderer, uiTarget, header, title, false);
  } else {
    GUI.drawHeader(renderer, header, title);
  }

  uiReady = false;
  app.render();
  uiReady = true;

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
