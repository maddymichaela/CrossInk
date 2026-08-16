#include "Ao3LibraryActivity.h"

#include <Arduino.h>
#include <Epub.h>
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
#include "Ao3ReadingState.h"
#include "MappedInputManager.h"
#include "activities/reader/BookReadingStats.h"
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
constexpr size_t AO3_VIEW_HEAP_RESERVE = 48U * 1024U;

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
  std::string subtitle;
  const std::string author = compactField(row.author);
  if (!author.empty()) {
    subtitle += author;
  }
  if (row.wordCount > 0) {
    if (!subtitle.empty()) subtitle += " · ";
    subtitle += wordsLabel(row.wordCount);
  }
  if (row.fandom[0] != '\0') {
    if (!subtitle.empty()) subtitle += " · ";
    subtitle += row.fandom;
  }
  return subtitle;
}

Ao3LibraryActivity::DisplayStatus deriveStatus(const Ao3LibraryMetadata& meta) {
  const std::string cachePath = Epub::cachePathForFilePath(meta.filepath, "/.crosspoint");
  const Ao3ReadingState ao3State = Ao3ReadingStateStore::load(cachePath);
  if (ao3State == Ao3ReadingState::UpdateAvailable) {
    return Ao3LibraryActivity::DisplayStatus::UpdateAvailable;
  }
  if (ao3State == Ao3ReadingState::WaitingForChapter) {
    return Ao3LibraryActivity::DisplayStatus::Waiting;
  }

  const BookReadingStats stats = BookReadingStats::load(cachePath);
  if (stats.isCompleted) {
    return meta.isCompleted ? Ao3LibraryActivity::DisplayStatus::Finished
                            : Ao3LibraryActivity::DisplayStatus::Waiting;
  }
  if (stats.sessionCount > 0 || stats.totalPagesTurned > 0 || stats.totalReadingSeconds > 0) {
    return Ao3LibraryActivity::DisplayStatus::Reading;
  }
  return Ao3LibraryActivity::DisplayStatus::Unread;
}

const char* statusLabel(const Ao3LibraryActivity::DisplayStatus status) {
  switch (status) {
    case Ao3LibraryActivity::DisplayStatus::Reading:
      return "Reading";
    case Ao3LibraryActivity::DisplayStatus::Waiting:
      return "Waiting";
    case Ao3LibraryActivity::DisplayStatus::UpdateAvailable:
      return "New Chapter";
    case Ao3LibraryActivity::DisplayStatus::Finished:
      return "Finished";
    case Ao3LibraryActivity::DisplayStatus::Unread:
    default:
      return "Unread";
  }
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

template <size_t N>
void copyField(char (&destination)[N], const char* source) {
  if (!source) return;
  strncpy(destination, source, N - 1);
  destination[N - 1] = '\0';
}

void populateRowFromIndex(Ao3LibraryActivity::Row& row, const CompactIndexRecord& rec) {
  copyField(row.title, rec.title);
  copyField(row.author, rec.author);
  copyField(row.seriesName, rec.seriesName);
  copyField(row.fandom, rec.fandom);
  row.wordCount = rec.wordCount;
  row.addedSequence = rec.addedSequence;
  row.cacheHash = rec.cacheHash;
  row.seriesPart = rec.seriesPart;
}

size_t safeRowCapacity(const size_t desired) {
  const size_t available = std::min(static_cast<size_t>(ESP.getFreeHeap()),
                                    static_cast<size_t>(ESP.getMaxAllocHeap()));
  if (available <= AO3_VIEW_HEAP_RESERVE) return 0;
  return std::min(desired, (available - AO3_VIEW_HEAP_RESERVE) / sizeof(Ao3LibraryActivity::Row));
}

bool growRowsForFallback(std::vector<Ao3LibraryActivity::Row>& rows) {
  if (rows.size() < rows.capacity()) return true;

  const size_t current = rows.capacity();
  const size_t desired = std::min(static_cast<size_t>(MAX_LIBRARY_BOOKS), current == 0 ? 32U : current * 2U);
  const size_t safeCapacity = safeRowCapacity(desired);
  if (safeCapacity <= current) return false;
  rows.reserve(safeCapacity);
  return rows.size() < rows.capacity();
}

bool loadRowsFromIndex(std::vector<Ao3LibraryActivity::Row>& rows) {
  FsFile file;
  if (!Storage.openFileForRead("AO3", AO3_INDEX_PATH, file)) {
    return false;
  }

  uint16_t recordCount = 0;
  if (!readIndexRecordCount(file, recordCount)) {
    file.close();
    return false;
  }

  uint32_t nextSequence = 0;
  uint8_t reserved = 0;
  file.read(reinterpret_cast<uint8_t*>(&nextSequence), sizeof(nextSequence));
  file.read(&reserved, sizeof(reserved));

  size_t liveCount = 0;
  for (uint16_t i = 0; i < recordCount; ++i) {
    CompactIndexRecord rec;
    if (file.read(reinterpret_cast<uint8_t*>(&rec), sizeof(rec)) != sizeof(rec)) {
      break;
    }
    if (!(rec.flags & 0x01)) liveCount++;
  }

  const size_t rowCapacity = safeRowCapacity(liveCount);
  rows.reserve(rowCapacity);
  if (!file.seek(INDEX_HEADER_SIZE)) {
    file.close();
    return false;
  }
  for (uint16_t i = 0; i < recordCount; ++i) {
    CompactIndexRecord rec;
    if (file.read(reinterpret_cast<uint8_t*>(&rec), sizeof(rec)) != sizeof(rec)) break;
    if (rec.flags & 0x01) continue;

    Ao3LibraryActivity::Row row;
    populateRowFromIndex(row, rec);
    if (rows.size() < rowCapacity) {
      rows.push_back(row);
      continue;
    }

    if (rowCapacity > 0) {
      const auto oldest = std::min_element(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return a.addedSequence < b.addedSequence;
      });
      if (oldest != rows.end() && row.addedSequence > oldest->addedSequence) *oldest = row;
    }
  }
  file.close();
  if (rowCapacity < liveCount) {
    LOG_INF("AO3L", "Library view limited to %u/%u works by heap budget", static_cast<unsigned>(rowCapacity),
            static_cast<unsigned>(liveCount));
  }
  return true;
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
  Ao3Librarian::sanitizeIndex();
  loadRowsFromIndex(rows);

  Ao3Librarian::forEachLibraryInfo([this](const Ao3LibraryMetadata& meta) {
    if (meta.filepath[0] == '\0' || !Storage.exists(meta.filepath)) return;

    const uint32_t cacheHash = ao3PathHash(meta.filepath);
    auto row = std::find_if(rows.begin(), rows.end(), [cacheHash](const Row& candidate) {
      return candidate.cacheHash == cacheHash;
    });
    if (row == rows.end()) {
      if (!growRowsForFallback(rows)) return;
      Row fallback;
      copyField(fallback.title, meta.title);
      copyField(fallback.author, meta.author);
      copyField(fallback.seriesName, meta.seriesName);
      fallback.wordCount = meta.wordCount;
      fallback.seriesPart = meta.seriesPart;
      fallback.cacheHash = cacheHash;
      rows.push_back(fallback);
      row = rows.end() - 1;
    }

    row->present = true;
    row->status = deriveStatus(meta);
  });

  rows.erase(std::remove_if(rows.begin(), rows.end(), [](const Row& row) { return !row.present; }), rows.end());
  LOG_INF("AO3L", "Loaded %u compact row(s), %u bytes/row (free=%u maxAlloc=%u)",
          static_cast<unsigned>(rows.size()), static_cast<unsigned>(sizeof(Row)), ESP.getFreeHeap(),
          ESP.getMaxAllocHeap());
  sortRows();
}

void Ao3LibraryActivity::sortRows() {
  const bool ascendingSort = ascending;
  std::sort(rows.begin(), rows.end(), [this, ascendingSort](const Row& a, const Row& b) {
    int cmp = 0;
    switch (sortMode) {
      case SortMode::AUTHOR:
        cmp = compareText(compactField(a.author), compactField(b.author));
        break;
      case SortMode::WORD_COUNT:
        cmp = (a.wordCount < b.wordCount) ? -1 : (a.wordCount > b.wordCount ? 1 : 0);
        break;
      case SortMode::DATE_ADDED:
        cmp = (a.addedSequence < b.addedSequence) ? -1 : (a.addedSequence > b.addedSequence ? 1 : 0);
        break;
      case SortMode::SERIES:
        cmp = compareText(compactField(a.seriesName), compactField(b.seriesName));
        if (cmp == 0) {
          cmp = (a.seriesPart < b.seriesPart) ? -1 : (a.seriesPart > b.seriesPart ? 1 : 0);
        }
        break;
      case SortMode::ALPHABETIC:
      default:
        cmp = compareText(compactField(a.title), compactField(b.title));
        break;
    }
    if (cmp == 0) {
      cmp = compareText(compactField(a.title), compactField(b.title));
    }
    return ascendingSort ? cmp < 0 : cmp > 0;
  });
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
  if (!rows.empty() && selectorIndex >= rows.size()) selectorIndex = rows.size() - 1;
  topIndex = followListSelection(static_cast<int>(selectorIndex), topIndex, visibleRows, static_cast<int>(rows.size()));
  requestUpdate();
}

void Ao3LibraryActivity::openSelected() {
  if (rows.empty() || selectorIndex >= rows.size()) {
    return;
  }

  Ao3LibraryMetadata metadata;
  if (!Ao3Librarian::findLibraryInfoByCacheHash(rows[selectorIndex].cacheHash, metadata) ||
      metadata.filepath[0] == '\0' || !Storage.exists(metadata.filepath)) {
    LOG_ERR("AO3L", "Could not resolve selected AO3 work (hash %u)", rows[selectorIndex].cacheHash);
    return;
  }
  onSelectBook(metadata.filepath);
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
  std::vector<std::string> subtitles;
  items.reserve(rows.size());
  subtitles.reserve(rows.size());
  for (size_t i = 0; i < rows.size(); ++i) {
    subtitles.push_back(subtitleFor(rows[i]));
    fui::ListItem item;
    item.label = rows[i].title[0] ? rows[i].title : "(Untitled)";
    if (!subtitles.back().empty()) item.subtitle = subtitles.back().c_str();
    item.value = statusLabel(rows[i].status);
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
