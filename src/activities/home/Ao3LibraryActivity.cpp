#include "Ao3LibraryActivity.h"

#include <Arduino.h>
#include <ArduinoJson.h>
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
#include <new>

#include "Ao3CompactIndexRecord.h"
#include "Ao3Librarian.h"
#include "Ao3DisplayStatus.h"
#include "Ao3ReadingState.h"
#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "activities/home/Ao3FolderPickerActivity.h"
#include "activities/home/Ao3IgnoredFoldersActivity.h"
#include "activities/home/Ao3IndexActivity.h"
#include "activities/util/OptionSelectionActivity.h"
#include "components/TouchHeaderBackButton.h"
#include "components/TouchRegistry.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char AO3_INDEX_PATH[] = "/.crosspoint/ao3_library_index.bin";
constexpr uint8_t AO3_INDEX_VERSION = 1;
constexpr char AO3_SETTINGS_PATH[] = "/.crosspoint/ao3_settings.json";
constexpr char AO3_FILTER_PATH[] = "/.crosspoint/ao3SortFilterState.json";
constexpr unsigned long STATUS_PICKER_HOLD_MS = 1000;

uint32_t stablePathHash(const std::string& path) {
  return static_cast<uint32_t>(ZipFile::fnvHash64(path.c_str(), path.size()));
}

bool isEpubName(std::string name) {
  const size_t dot = name.find_last_of('.');
  if (dot == std::string::npos) return false;
  name = name.substr(dot + 1);
  std::transform(name.begin(), name.end(), name.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return name == "epub";
}

template <typename T>
void sortUnique(std::vector<T>& values) {
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
}

int compareText(const char* a, const char* b) {
  if (!a) a = "";
  if (!b) b = "";
  while (*a && *b) {
    const char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(*a)));
    const char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(*b)));
    if (ca < cb) return -1;
    if (ca > cb) return 1;
    ++a;
    ++b;
  }
  if (*a) return 1;
  if (*b) return -1;
  return 0;
}

bool readIndexHeader(HalFile& file, uint16_t& recordCount) {
  char magic[4];
  uint8_t version = 0;
  uint32_t sequence = 0;
  uint8_t reserved = 0;
  if (file.read(magic, sizeof(magic)) != sizeof(magic) || file.read(&version, 1) != 1 ||
      file.read(reinterpret_cast<uint8_t*>(&recordCount), sizeof(recordCount)) != sizeof(recordCount) ||
      file.read(reinterpret_cast<uint8_t*>(&sequence), sizeof(sequence)) != sizeof(sequence) ||
      file.read(&reserved, 1) != 1) {
    return false;
  }
  return memcmp(magic, "AO3X", 4) == 0 && version == AO3_INDEX_VERSION &&
         recordCount <= MAX_LIBRARY_BOOKS;
}

const char* sortLabel(const SortMode mode) {
  switch (mode) {
    case SortMode::ALPHABETIC:
      return "Title";
    case SortMode::AUTHOR:
      return "Author";
    case SortMode::WORD_COUNT:
      return "Word Count";
    case SortMode::DATE_ADDED:
      return "Date Added";
    case SortMode::SERIES:
      return "Series";
  }
  return "Title";
}

SortMode nextSortMode(const SortMode mode, const int direction) {
  constexpr SortMode modes[] = {SortMode::ALPHABETIC, SortMode::AUTHOR, SortMode::WORD_COUNT,
                                SortMode::DATE_ADDED, SortMode::SERIES};
  constexpr int count = sizeof(modes) / sizeof(modes[0]);
  int index = 0;
  for (int i = 0; i < count; ++i) {
    if (modes[i] == mode) {
      index = i;
      break;
    }
  }
  return modes[(index + direction + count) % count];
}

std::string truncatedToFit(const GfxRenderer& renderer, std::string text, const int fontId,
                           const int maxWidth, const EpdFontFamily::Style style) {
  if (renderer.getTextWidth(fontId, text.c_str(), style) <= maxWidth) return text;
  while (!text.empty() && renderer.getTextWidth(fontId, (text + "..").c_str(), style) > maxWidth) {
    do {
      text.pop_back();
    } while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80);
  }
  return text + "..";
}
}  // namespace

Ao3LibraryActivity::Ao3LibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       const size_t initialSelectorIndex)
    : Activity("Ao3Library", renderer, mappedInput), selectorIndex(initialSelectorIndex) {}

void Ao3LibraryActivity::onEnter() {
  Activity::onEnter();
  buttonNavigator.setMappedInputManager(mappedInput);
  screenState = ScreenState::Library;
  loadSettings();
  loadSortFilterState();
  loadViewEntries();
  if (viewEntries.empty()) {
    selectorIndex = 0;
  } else if (selectorIndex >= viewEntries.size()) {
    selectorIndex = viewEntries.size() - 1;
  }
  loadPageCache(static_cast<int>(selectorIndex) / PAGE_SIZE);
  requestUpdate();
}

void Ao3LibraryActivity::onExit() {
  Activity::onExit();
  viewEntries.clear();
  for (auto& summary : wrappedSummary) summary.clear();
}

void Ao3LibraryActivity::loadViewEntries() {
  viewEntries.clear();
  allowedHashes.clear();
  if (filterMode == FilterMode::FolderTree && !ao3Folder.empty()) {
    std::string root = ao3Folder;
    int minimumDepth = 2;
    if (activeState.fandom[0]) {
      if (root.back() != '/') root += '/';
      root += activeState.fandom;
      minimumDepth = 1;
    }
    if (activeState.relationship[0]) {
      if (root.back() != '/') root += '/';
      root += activeState.relationship;
      minimumDepth = 0;
    }
    buildAllowedHashes(root, minimumDepth);
  }
  HalFile file;
  if (!Storage.openFileForRead("AO3L", AO3_INDEX_PATH, file)) return;

  uint16_t recordCount = 0;
  if (!readIndexHeader(file, recordCount)) {
    file.close();
    LOG_ERR("AO3L", "Library index missing, corrupt, or above the 400-work limit");
    return;
  }

  viewEntries.reserve(recordCount);
  for (uint16_t i = 0; i < recordCount; ++i) {
    CompactIndexRecord record;
    const uint32_t offset = offsetOf(i);
    if (file.read(reinterpret_cast<uint8_t*>(&record), sizeof(record)) != sizeof(record)) break;
    if (record.flags & 0x01) continue;
    const ViewEntry entry = buildViewEntry(record, offset);
    bool include = true;
    if (filterMode == FilterMode::FolderTree) {
      include = std::binary_search(allowedHashes.begin(), allowedHashes.end(), entry.cacheHash);
    } else {
      if (activeState.fandom[0] && entry.fandomHash != fnv1a(activeState.fandom)) include = false;
      if (include && activeState.relationshipNoneOnly && (entry.rel1Hash != 0 || entry.rel2Hash != 0)) include = false;
      if (include && activeState.relationship[0]) {
        const uint32_t relationshipHash = fnv1a(activeState.relationship);
        include = entry.rel1Hash == relationshipHash || entry.rel2Hash == relationshipHash;
      }
    }
    if (include) viewEntries.push_back(entry);
  }
  file.close();
  sortMode = activeState.sortMode;
  ascending = activeState.ascending;
  sortViewEntries();
  LOG_INF("AO3L", "Loaded %u rich-browser keys (%u bytes each, free=%u maxAlloc=%u)",
          static_cast<unsigned>(viewEntries.size()), static_cast<unsigned>(sizeof(ViewEntry)),
          ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

void Ao3LibraryActivity::loadSettings() {
  ao3Folder.clear();
  ignoredFolders.clear();
  batchSize = 10;
  filterMode = FilterMode::Automatic;
  if (!Storage.exists(AO3_SETTINGS_PATH)) return;
  const String json = Storage.readFile(AO3_SETTINGS_PATH);
  if (json.isEmpty()) return;
  JsonDocument document;
  if (deserializeJson(document, json)) return;
  ao3Folder = document["ao3Folder"] | "";
  for (JsonVariantConst value : document["ignoredFolders"].as<JsonArrayConst>()) {
    std::string path = value.as<const char*>() ? value.as<const char*>() : "";
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    if (!path.empty() && path != "/") ignoredFolders.push_back(std::move(path));
  }
  sortUnique(ignoredFolders);
  batchSize = document["batchSize"] | 10;
  if (batchSize != 10 && batchSize != 15 && batchSize != 20) batchSize = 10;
  filterMode = (document["filterMode"] | 0) == 1 ? FilterMode::FolderTree : FilterMode::Automatic;
}

void Ao3LibraryActivity::saveSettings() const {
  JsonDocument document;
  document["ao3Folder"] = ao3Folder;
  document["batchSize"] = batchSize;
  document["filterMode"] = filterMode == FilterMode::FolderTree ? 1 : 0;
  JsonArray ignored = document["ignoredFolders"].to<JsonArray>();
  for (const std::string& path : ignoredFolders) ignored.add(path);
  String json;
  serializeJson(document, json);
  Storage.writeFile(AO3_SETTINGS_PATH, json);
}

void Ao3LibraryActivity::loadSortFilterState() {
  activeState = SortFilterState{};
  activeState.sortMode = SortMode::DATE_ADDED;
  activeState.ascending = false;
  if (Storage.exists(AO3_FILTER_PATH)) {
    const String json = Storage.readFile(AO3_FILTER_PATH);
    JsonDocument document;
    if (!json.isEmpty() && !deserializeJson(document, json)) {
      const std::string fandom = document["fandom"] | "";
      const std::string relationship = document["relationship"] | "";
      strncpy(activeState.fandom, fandom.c_str(), sizeof(activeState.fandom) - 1);
      strncpy(activeState.relationship, relationship.c_str(), sizeof(activeState.relationship) - 1);
      activeState.relationshipNoneOnly = document["relationshipNoneOnly"] | false;
      const uint8_t mode = document["sortMode"] | static_cast<uint8_t>(SortMode::DATE_ADDED);
      if (mode <= static_cast<uint8_t>(SortMode::AUTHOR)) activeState.sortMode = static_cast<SortMode>(mode);
      activeState.ascending = document["ascending"] | false;
    }
  }
  pendingState = activeState;
}

void Ao3LibraryActivity::saveSortFilterState() const {
  JsonDocument document;
  document["fandom"] = activeState.fandom;
  document["relationship"] = activeState.relationship;
  document["relationshipNoneOnly"] = activeState.relationshipNoneOnly;
  document["sortMode"] = static_cast<uint8_t>(activeState.sortMode);
  document["ascending"] = activeState.ascending;
  String json;
  serializeJson(document, json);
  Storage.writeFile(AO3_FILTER_PATH, json);
}

void Ao3LibraryActivity::buildAllowedHashes(const std::string& root, const int minimumDepth) {
  struct PendingDirectory {
    std::string path;
    int depth;
  };
  std::vector<PendingDirectory> pending;
  pending.push_back({root, 0});
  while (!pending.empty()) {
    PendingDirectory current = std::move(pending.back());
    pending.pop_back();
    HalFile directory = Storage.open(current.path.c_str());
    if (!directory || !directory.isDirectory()) {
      if (directory) directory.close();
      continue;
    }
    HalFile child;
    char name[256];
    while ((child = directory.openNextFile())) {
      child.getName(name, sizeof(name));
      std::string path = current.path;
      if (path.back() != '/') path += '/';
      path += name;
      if (child.isDirectory()) {
        if (name[0] != '.' && strcmp(name, "System Volume Information") != 0 && current.depth < 6) {
          pending.push_back({std::move(path), current.depth + 1});
        }
      } else if (current.depth >= minimumDepth && isEpubName(name)) {
        allowedHashes.push_back(stablePathHash(path));
      }
      child.close();
    }
    directory.close();
    yield();
  }
  sortUnique(allowedHashes);
}

void Ao3LibraryActivity::buildFandomList(std::vector<std::string>& output) const {
  output.clear();
  if (filterMode == FilterMode::FolderTree) {
    if (ao3Folder.empty()) return;
    HalFile directory = Storage.open(ao3Folder.c_str());
    if (!directory || !directory.isDirectory()) {
      if (directory) directory.close();
      return;
    }
    HalFile child;
    char name[256];
    while ((child = directory.openNextFile())) {
      child.getName(name, sizeof(name));
      if (child.isDirectory() && name[0] != '.' && strcmp(name, "System Volume Information") != 0) {
        output.emplace_back(name);
      }
      child.close();
    }
    directory.close();
  } else {
    HalFile file;
    if (!Storage.openFileForRead("AO3L", AO3_INDEX_PATH, file)) return;
    uint16_t count = 0;
    if (!readIndexHeader(file, count)) {
      file.close();
      return;
    }
    for (uint16_t i = 0; i < count; ++i) {
      CompactIndexRecord record;
      if (file.read(reinterpret_cast<uint8_t*>(&record), sizeof(record)) != sizeof(record)) break;
      if (!(record.flags & 1) && record.fandom[0]) output.emplace_back(record.fandom);
    }
    file.close();
  }
  sortUnique(output);
}

void Ao3LibraryActivity::buildRelationshipList(const char* fandom, std::vector<std::string>& output,
                                               bool& hasNone) const {
  output.clear();
  hasNone = false;
  if (!fandom || !fandom[0]) return;
  if (filterMode == FilterMode::FolderTree) {
    std::string path = ao3Folder;
    if (!path.empty() && path.back() != '/') path += '/';
    path += fandom;
    HalFile directory = Storage.open(path.c_str());
    if (!directory || !directory.isDirectory()) {
      if (directory) directory.close();
      return;
    }
    HalFile child;
    char name[256];
    while ((child = directory.openNextFile())) {
      child.getName(name, sizeof(name));
      if (child.isDirectory() && name[0] != '.') output.emplace_back(name);
      child.close();
    }
    directory.close();
  } else {
    const uint32_t fandomHash = fnv1a(fandom);
    HalFile file;
    if (!Storage.openFileForRead("AO3L", AO3_INDEX_PATH, file)) return;
    uint16_t count = 0;
    if (!readIndexHeader(file, count)) {
      file.close();
      return;
    }
    for (uint16_t i = 0; i < count; ++i) {
      CompactIndexRecord record;
      if (file.read(reinterpret_cast<uint8_t*>(&record), sizeof(record)) != sizeof(record)) break;
      if ((record.flags & 1) || fnv1a(record.fandom) != fandomHash) continue;
      if (record.relationship1[0]) output.emplace_back(record.relationship1);
      if (record.relationship2[0]) output.emplace_back(record.relationship2);
      if (!record.relationship1[0] && !record.relationship2[0]) hasNone = true;
    }
    file.close();
  }
  sortUnique(output);
}

void Ao3LibraryActivity::sortViewEntries() {
  const bool ascendingSort = ascending;
  std::sort(viewEntries.begin(), viewEntries.end(), [this, ascendingSort](const ViewEntry& a, const ViewEntry& b) {
    int comparison = 0;
    switch (sortMode) {
      case SortMode::AUTHOR:
        comparison = compareText(a.authorKey, b.authorKey);
        break;
      case SortMode::WORD_COUNT:
        comparison = a.wordCount < b.wordCount ? -1 : (a.wordCount > b.wordCount ? 1 : 0);
        break;
      case SortMode::DATE_ADDED:
        comparison = a.addedSequence < b.addedSequence ? -1 : (a.addedSequence > b.addedSequence ? 1 : 0);
        break;
      case SortMode::SERIES:
        comparison = a.seriesHash < b.seriesHash ? -1 : (a.seriesHash > b.seriesHash ? 1 : 0);
        if (comparison == 0) {
          comparison = a.seriesPart < b.seriesPart ? -1 : (a.seriesPart > b.seriesPart ? 1 : 0);
        }
        break;
      case SortMode::ALPHABETIC:
      default:
        comparison = compareText(a.title, b.title);
        break;
    }
    if (comparison == 0) comparison = compareText(a.title, b.title);
    return ascendingSort ? comparison < 0 : comparison > 0;
  });
  cachedPage = -1;
}

void Ao3LibraryActivity::loadPageCache(const int page) {
  if (page == cachedPage) return;
  const int start = page * PAGE_SIZE;
  const int count = std::max(0, std::min(PAGE_SIZE, static_cast<int>(viewEntries.size()) - start));
  uint32_t hashes[PAGE_SIZE] = {};

  for (int slot = 0; slot < PAGE_SIZE; ++slot) {
    new (&pageMetadata[slot]) Ao3LibraryMetadata();
    pageMetadataLoaded[slot] = false;
    pageStatus[slot] = DisplayStatus::Unread;
    wrappedSummary[slot].clear();
    if (slot < count) hashes[slot] = viewEntries[start + slot].cacheHash;
  }

  if (count > 0) {
    Ao3Librarian::findLibraryInfoByCacheHashes(hashes, count, pageMetadata.data(), pageMetadataLoaded.data());
  }

  const int summaryWidth = renderer.getScreenWidth() - 40;
  for (int slot = 0; slot < count; ++slot) {
    if (!pageMetadataLoaded[slot]) continue;
    pageStatus[slot] = deriveAo3DisplayStatus(pageMetadata[slot]);
    if (pageMetadata[slot].summary[0]) {
      wrappedSummary[slot] = renderer.wrappedText(SMALL_FONT_ID, pageMetadata[slot].summary, summaryWidth, 3);
    }
  }
  cachedPage = page;
}

void Ao3LibraryActivity::moveSelection(const int index) {
  if (viewEntries.empty()) return;
  const int size = static_cast<int>(viewEntries.size());
  const int wrapped = (index % size + size) % size;
  selectorIndex = static_cast<size_t>(wrapped);
  loadPageCache(wrapped / PAGE_SIZE);
  requestUpdate();
}

void Ao3LibraryActivity::cycleSort(const int direction) {
  sortMode = nextSortMode(sortMode, direction);
  ascending = sortMode == SortMode::ALPHABETIC || sortMode == SortMode::AUTHOR || sortMode == SortMode::SERIES;
  activeState.sortMode = sortMode;
  activeState.ascending = ascending;
  saveSortFilterState();
  sortViewEntries();
  if (!viewEntries.empty() && selectorIndex >= viewEntries.size()) selectorIndex = viewEntries.size() - 1;
  loadPageCache(static_cast<int>(selectorIndex) / PAGE_SIZE);
  requestUpdate();
}

void Ao3LibraryActivity::applyPendingFilter() {
  activeState = pendingState;
  saveSortFilterState();
  loadViewEntries();
  selectorIndex = 0;
  cachedPage = -1;
  loadPageCache(0);
  screenState = ScreenState::Library;
  requestUpdate(true);
}

void Ao3LibraryActivity::activateFilterRow() {
  if (overlayRowIndex == 0) {
    std::vector<std::string> values;
    buildFandomList(values);
    pickerItems.clear();
    pickerItems.emplace_back("Any");
    pickerItems.insert(pickerItems.end(), values.begin(), values.end());
    pickerSelectedIndex = 0;
    for (size_t i = 1; i < pickerItems.size(); ++i) {
      if (strcmp(pendingState.fandom, pickerItems[i].c_str()) == 0) pickerSelectedIndex = i;
    }
    screenState = ScreenState::FandomPicker;
  } else if (overlayRowIndex == 1 && pendingState.fandom[0]) {
    std::vector<std::string> values;
    buildRelationshipList(pendingState.fandom, values, pickerHasNone);
    pickerItems.clear();
    pickerItems.emplace_back("Any");
    if (pickerHasNone) pickerItems.emplace_back("None");
    pickerItems.insert(pickerItems.end(), values.begin(), values.end());
    pickerSelectedIndex = 0;
    for (size_t i = 1; i < pickerItems.size(); ++i) {
      if (pendingState.relationshipNoneOnly && pickerItems[i] == "None") pickerSelectedIndex = i;
      if (!pendingState.relationshipNoneOnly && strcmp(pendingState.relationship, pickerItems[i].c_str()) == 0) {
        pickerSelectedIndex = i;
      }
    }
    screenState = ScreenState::RelationshipPicker;
  } else if (overlayRowIndex == 2) {
    pendingState.sortMode = nextSortMode(pendingState.sortMode, 1);
  } else if (overlayRowIndex == 3) {
    pendingState.ascending = !pendingState.ascending;
  } else if (overlayRowIndex == 4) {
    applyPendingFilter();
    return;
  }
  requestUpdate(true);
}

void Ao3LibraryActivity::activateManageRow() {
  if (manageRowIndex == 0) {
    if (ao3Folder.empty()) {
      chooseAo3Folder();
      return;
    }
    viewEntries.clear();
    viewEntries.shrink_to_fit();
    startActivityForResult(
        std::make_unique<Ao3IndexActivity>(renderer, mappedInput, ao3Folder, batchSize, ignoredFolders),
                           [this](const ActivityResult&) {
                             loadViewEntries();
                             selectorIndex = 0;
                             cachedPage = -1;
                             loadPageCache(0);
                             screenState = ScreenState::Library;
                             requestUpdate(true);
                           });
  } else if (manageRowIndex == 1) {
    chooseAo3Folder();
  } else if (manageRowIndex == 2) {
    chooseIgnoredFolders();
  } else if (manageRowIndex == 3) {
    batchSize = batchSize == 10 ? 15 : (batchSize == 15 ? 20 : 10);
    saveSettings();
    requestUpdate(true);
  } else if (manageRowIndex == 4) {
    filterMode = filterMode == FilterMode::Automatic ? FilterMode::FolderTree : FilterMode::Automatic;
    activeState.fandom[0] = '\0';
    activeState.relationship[0] = '\0';
    activeState.relationshipNoneOnly = false;
    pendingState = activeState;
    saveSettings();
    saveSortFilterState();
    loadViewEntries();
    selectorIndex = 0;
    cachedPage = -1;
    loadPageCache(0);
    requestUpdate(true);
  } else if (manageRowIndex == 5) {
    Ao3Librarian::sanitizeIndex();
    loadViewEntries();
    if (!viewEntries.empty() && selectorIndex >= viewEntries.size()) selectorIndex = viewEntries.size() - 1;
    cachedPage = -1;
    loadPageCache(static_cast<int>(selectorIndex) / PAGE_SIZE);
    requestUpdate(true);
  }
}

void Ao3LibraryActivity::chooseAo3Folder() {
  startActivityForResult(std::make_unique<Ao3FolderPickerActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             if (const auto* path = std::get_if<FilePathResult>(&result.data);
                                 path && !path->path.empty() && Storage.exists(path->path.c_str())) {
                               ao3Folder = path->path;
                               ignoredFolders.erase(
                                   std::remove_if(ignoredFolders.begin(), ignoredFolders.end(), [this](const std::string& ignored) {
                                     return ao3Folder == ignored ||
                                            (ao3Folder.size() > ignored.size() &&
                                             ao3Folder.compare(0, ignored.size(), ignored) == 0 &&
                                             ignored != "/" && ao3Folder[ignored.size()] == '/');
                                   }),
                                   ignoredFolders.end());
                               saveSettings();
                               if (filterMode == FilterMode::FolderTree) {
                                 activeState.fandom[0] = '\0';
                                 activeState.relationship[0] = '\0';
                                 activeState.relationshipNoneOnly = false;
                                 saveSortFilterState();
                                 loadViewEntries();
                               }
                             }
                           }
                           requestUpdate(true);
                         });
}

void Ao3LibraryActivity::chooseIgnoredFolders() {
  if (ao3Folder.empty()) {
    chooseAo3Folder();
    return;
  }
  startActivityForResult(
      std::make_unique<Ao3IgnoredFoldersActivity>(renderer, mappedInput, ignoredFolders, ao3Folder),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          if (const auto* folders = std::get_if<FolderListResult>(&result.data)) {
            ignoredFolders = folders->paths;
            saveSettings();
          }
        }
        requestUpdate(true);
      });
}

void Ao3LibraryActivity::openSelected() {
  if (viewEntries.empty() || selectorIndex >= viewEntries.size()) return;
  const int slot = static_cast<int>(selectorIndex) % PAGE_SIZE;
  if (cachedPage != static_cast<int>(selectorIndex) / PAGE_SIZE) {
    loadPageCache(static_cast<int>(selectorIndex) / PAGE_SIZE);
  }
  if (!pageMetadataLoaded[slot] || pageMetadata[slot].filepath[0] == '\0' ||
      !Storage.exists(pageMetadata[slot].filepath)) {
    LOG_ERR("AO3L", "Could not resolve selected AO3 work (hash %u)", viewEntries[selectorIndex].cacheHash);
    return;
  }
  // AO3 works use the normal CrossInk reader without any AO3-specific reader state.
  activityManager.goToReaderFromAo3(pageMetadata[slot].filepath, selectorIndex);
}

void Ao3LibraryActivity::chooseSelectedStatus() {
  if (viewEntries.empty() || selectorIndex >= viewEntries.size()) return;
  const int page = static_cast<int>(selectorIndex) / PAGE_SIZE;
  const int slot = static_cast<int>(selectorIndex) % PAGE_SIZE;
  if (cachedPage != page) loadPageCache(page);
  if (!pageMetadataLoaded[slot] || pageMetadata[slot].filepath[0] == '\0') return;

  const std::string cachePath = Epub::cachePathForFilePath(pageMetadata[slot].filepath, "/.crosspoint");
  const uint8_t currentIndex = ao3ReadingStateOptionIndex(Ao3ReadingStateStore::load(cachePath));
  startActivityForResult(
      std::make_unique<OptionSelectionActivity>(renderer, mappedInput, "Ao3StatusSelect", StrId::STR_DISPLAY_STATUS,
                                                ao3ReadingStateOptions(), currentIndex),
      [this, cachePath](const ActivityResult& result) {
        if (!result.isCancelled) {
          if (const auto* selection = std::get_if<OptionSelectionResult>(&result.data)) {
            Ao3ReadingStateStore::save(cachePath, ao3ReadingStateForOption(selection->index));
          }
        }
        cachedPage = -1;
        loadPageCache(static_cast<int>(selectorIndex) / PAGE_SIZE);
        requestUpdate(true);
      });
}

void Ao3LibraryActivity::loop() {
  if (screenState == ScreenState::Library) {
    if (TouchHeaderBackButton::wasTapped(mappedInput, renderer) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome(HomeMenuItem::AO3_LIBRARY);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      screenState = ScreenState::ManagePanel;
      manageRowIndex = 0;
      requestUpdate(true);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      screenState = ScreenState::FilterPanel;
      pendingState = activeState;
      overlayRowIndex = 0;
      requestUpdate(true);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (mappedInput.getHeldTime() >= STATUS_PICKER_HOLD_MS) {
        chooseSelectedStatus();
      } else {
        openSelected();
      }
      return;
    }

    int touched = -1;
    if (mappedInput.wasItemTapped(touched) && touched >= ROW_TOUCH_BASE && touched < ROW_TOUCH_BASE + PAGE_SIZE) {
      const int index = (static_cast<int>(selectorIndex) / PAGE_SIZE) * PAGE_SIZE + touched - ROW_TOUCH_BASE;
      if (index >= 0 && index < static_cast<int>(viewEntries.size())) {
        selectorIndex = static_cast<size_t>(index);
        openSelected();
      }
      return;
    }

    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
      moveSelection(static_cast<int>(selectorIndex) +
                    (swipe == MappedInputManager::SwipeDir::Up ? PAGE_SIZE : -PAGE_SIZE));
      return;
    }

    buttonNavigator.onRelease({MappedInputManager::Button::Right},
                              [this] { moveSelection(static_cast<int>(selectorIndex) + 1); });
    buttonNavigator.onRelease({MappedInputManager::Button::Left},
                              [this] { moveSelection(static_cast<int>(selectorIndex) - 1); });
    buttonNavigator.onContinuous({MappedInputManager::Button::Right},
                                 [this] { moveSelection(static_cast<int>(selectorIndex) + PAGE_SIZE); });
    buttonNavigator.onContinuous({MappedInputManager::Button::Left},
                                 [this] { moveSelection(static_cast<int>(selectorIndex) - PAGE_SIZE); });
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (screenState == ScreenState::FandomPicker || screenState == ScreenState::RelationshipPicker) {
      screenState = ScreenState::FilterPanel;
    } else {
      screenState = ScreenState::Library;
    }
    requestUpdate(true);
    return;
  }

  if (screenState == ScreenState::FilterPanel) {
    buttonNavigator.onNextRelease([this] {
      overlayRowIndex = (overlayRowIndex + 1) % 5;
      if (overlayRowIndex == 1 && !pendingState.fandom[0]) overlayRowIndex = 2;
      requestUpdate(true);
    });
    buttonNavigator.onPreviousRelease([this] {
      overlayRowIndex = (overlayRowIndex + 4) % 5;
      if (overlayRowIndex == 1 && !pendingState.fandom[0]) overlayRowIndex = 0;
      requestUpdate(true);
    });
    buttonNavigator.onNextContinuous([this] {
      overlayRowIndex = 4;
      requestUpdate(true);
    });
    buttonNavigator.onPreviousContinuous([this] {
      overlayRowIndex = 4;
      requestUpdate(true);
    });
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) activateFilterRow();
    return;
  }

  if (screenState == ScreenState::FandomPicker || screenState == ScreenState::RelationshipPicker) {
    if (!pickerItems.empty()) {
      buttonNavigator.onNextRelease([this] {
        pickerSelectedIndex = (pickerSelectedIndex + 1) % pickerItems.size();
        requestUpdate(true);
      });
      buttonNavigator.onPreviousRelease([this] {
        pickerSelectedIndex = (pickerSelectedIndex + pickerItems.size() - 1) % pickerItems.size();
        requestUpdate(true);
      });
      buttonNavigator.onNextContinuous([this] {
        pickerSelectedIndex = (pickerSelectedIndex + 3) % pickerItems.size();
        requestUpdate(true);
      });
      buttonNavigator.onPreviousContinuous([this] {
        pickerSelectedIndex = (pickerSelectedIndex + pickerItems.size() - (3 % pickerItems.size())) % pickerItems.size();
        requestUpdate(true);
      });
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && !pickerItems.empty()) {
      if (screenState == ScreenState::FandomPicker) {
        pendingState.fandom[0] = '\0';
        pendingState.relationship[0] = '\0';
        pendingState.relationshipNoneOnly = false;
        if (pickerSelectedIndex > 0) {
          strncpy(pendingState.fandom, pickerItems[pickerSelectedIndex].c_str(), sizeof(pendingState.fandom) - 1);
        }
      } else {
        pendingState.relationship[0] = '\0';
        pendingState.relationshipNoneOnly = false;
        if (pickerSelectedIndex > 0 && pickerItems[pickerSelectedIndex] == "None") {
          pendingState.relationshipNoneOnly = true;
        } else if (pickerSelectedIndex > 0) {
          strncpy(pendingState.relationship, pickerItems[pickerSelectedIndex].c_str(),
                  sizeof(pendingState.relationship) - 1);
        }
      }
      screenState = ScreenState::FilterPanel;
      requestUpdate(true);
    }
    return;
  }

  if (screenState == ScreenState::ManagePanel) {
    buttonNavigator.onNextRelease([this] {
      manageRowIndex = (manageRowIndex + 1) % 6;
      requestUpdate(true);
    });
    buttonNavigator.onPreviousRelease([this] {
      manageRowIndex = (manageRowIndex + 5) % 6;
      requestUpdate(true);
    });
    buttonNavigator.onNextContinuous([this] {
      manageRowIndex = (manageRowIndex + 3) % 6;
      requestUpdate(true);
    });
    buttonNavigator.onPreviousContinuous([this] {
      manageRowIndex = (manageRowIndex + 3) % 6;
      requestUpdate(true);
    });
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) activateManageRow();
  }
}

void Ao3LibraryActivity::render(RenderLock&& lock) {
  if (screenState == ScreenState::FandomPicker || screenState == ScreenState::RelationshipPicker) {
    renderPicker();
    return;
  }
  if (screenState == ScreenState::ManagePanel) {
    renderManagePanel();
    return;
  }
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  char headerTitle[48];
  if (activeState.fandom[0]) {
    snprintf(headerTitle, sizeof(headerTitle), "%.30s", activeState.fandom);
  } else {
    snprintf(headerTitle, sizeof(headerTitle), "AO3 Library");
  }
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::draw(renderer, header, headerTitle, false);
  } else {
    GUI.drawHeader(renderer, header, headerTitle);
  }

  if (viewEntries.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2 - 12, "No AO3 books indexed yet.");
    renderer.drawCenteredText(SMALL_FONT_ID, renderer.getScreenHeight() / 2 + 16,
                              "Press and hold Up to choose a folder and index.");
  } else {
    const int page = static_cast<int>(selectorIndex) / PAGE_SIZE;
    loadPageCache(page);
    const int start = page * PAGE_SIZE;
    const int end = std::min(start + PAGE_SIZE, static_cast<int>(viewEntries.size()));
    const int contentTop = header.y + header.height + 12;
    const int contentBottom = renderer.getScreenHeight() - metrics.buttonHintsHeight;
    const int entryHeight = (contentBottom - contentTop) / PAGE_SIZE;

    for (int i = start; i < end; ++i) {
      const int slot = i - start;
      const int y = contentTop + slot * entryHeight;
      renderEntry(lock, y, viewEntries[i], slot, i == static_cast<int>(selectorIndex), entryHeight);
      TouchRegistry::getInstance().add(Rect{0, y, renderer.getScreenWidth(), entryHeight}, ROW_TOUCH_BASE + slot,
                                       TouchRegistry::Item);
      if (slot < PAGE_SIZE - 1 && i < end - 1) {
        renderer.drawLine(15, y + entryHeight - 5, renderer.getScreenWidth() - 15, y + entryHeight - 5);
      }
    }

    if (viewEntries.size() > PAGE_SIZE) {
      char pageLabel[24];
      snprintf(pageLabel, sizeof(pageLabel), "%d / %d", page + 1,
               (static_cast<int>(viewEntries.size()) + PAGE_SIZE - 1) / PAGE_SIZE);
      renderer.drawText(SMALL_FONT_ID,
                        renderer.getScreenWidth() - metrics.contentSidePadding -
                            renderer.getTextWidth(SMALL_FONT_ID, pageLabel),
                        header.y + 7, pageLabel);
    }
  }

  if (screenState == ScreenState::FilterPanel) renderFilterOverlay();
  const auto labels = screenState == ScreenState::Library
                          ? mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT))
                          : mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void Ao3LibraryActivity::renderFilterOverlay() {
  const int screenWidth = renderer.getScreenWidth();
  const int startY = 48;
  const int overlayHeight = std::min(340, renderer.getScreenHeight() - startY - 50);
  const int margin = 20;
  renderer.fillRect(0, startY, screenWidth, overlayHeight, White);
  renderer.fillRect(0, startY, screenWidth, 1, Black);
  renderer.fillRect(0, startY + overlayHeight - 5, screenWidth, 5, Black);
  renderer.drawText(UI_12_FONT_ID, margin + 8, startY + 18, "Sort & Filter", true, EpdFontFamily::BOLD);

  const char* values[5] = {};
  std::string fandom = pendingState.fandom[0] ? pendingState.fandom : "Any";
  std::string relationship = pendingState.relationshipNoneOnly
                                 ? "None"
                                 : (pendingState.relationship[0] ? pendingState.relationship : "Any");
  values[0] = fandom.c_str();
  values[1] = relationship.c_str();
  values[2] = sortLabel(pendingState.sortMode);
  values[3] = pendingState.ascending ? "Ascending" : "Descending";
  values[4] = "Apply";
  const char* labels[5] = {"Fandom", "Relationship", "Sort by", "Order", ""};
  const int firstY = startY + 58;
  const int rowHeight = 51;
  for (int row = 0; row < 5; ++row) {
    const int rowY = firstY + row * rowHeight;
    const bool disabled = row == 1 && !pendingState.fandom[0];
    if (row == overlayRowIndex && !disabled) {
      renderer.fillRoundedRect(margin, rowY - 6, screenWidth - margin * 2, 38, 6, LightGray);
    }
    if (row == 4) {
      const int width = 150;
      const int x = (screenWidth - width) / 2;
      if (row == overlayRowIndex) renderer.fillRoundedRect(x, rowY - 6, width, 38, 6, Black);
      renderer.drawCenteredText(UI_10_FONT_ID, rowY, values[row], row != overlayRowIndex, EpdFontFamily::BOLD);
      continue;
    }
    renderer.drawText(UI_10_FONT_ID, margin + 10, rowY, labels[row], !disabled);
    std::string value = values[row];
    if (value.length() > 24) value = value.substr(0, 22) + "..";
    renderer.drawText(UI_10_FONT_ID,
                      screenWidth - margin - 10 - renderer.getTextWidth(UI_10_FONT_ID, value.c_str()), rowY,
                      value.c_str(), !disabled);
  }
}

void Ao3LibraryActivity::renderPicker() {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  const char* title = screenState == ScreenState::FandomPicker ? "Choose Fandom" : "Choose Relationship";
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::draw(renderer, header, title, false);
  } else {
    GUI.drawHeader(renderer, header, title);
  }

  const int visible = 8;
  const int total = static_cast<int>(pickerItems.size());
  const int top = total <= visible ? 0 : std::clamp(static_cast<int>(pickerSelectedIndex) - visible / 2, 0, total - visible);
  const int rowHeight = 62;
  const int startY = header.y + header.height + 10;
  for (int row = 0; row < visible && top + row < total; ++row) {
    const int index = top + row;
    const int y = startY + row * rowHeight;
    if (index == static_cast<int>(pickerSelectedIndex)) {
      renderer.fillRoundedRect(16, y, renderer.getScreenWidth() - 32, rowHeight - 8, 6, LightGray);
    }
    std::string label = pickerItems[index];
    if (label.length() > 44) label = label.substr(0, 42) + "..";
    renderer.drawText(UI_10_FONT_ID, 28, y + 12, label.c_str(), true,
                      index == static_cast<int>(pickerSelectedIndex) ? EpdFontFamily::BOLD
                                                                    : EpdFontFamily::REGULAR);
  }
  if (total > 0) {
    char count[24];
    snprintf(count, sizeof(count), "%u / %u", static_cast<unsigned>(pickerSelectedIndex + 1),
             static_cast<unsigned>(pickerItems.size()));
    renderer.drawText(SMALL_FONT_ID, renderer.getScreenWidth() - metrics.contentSidePadding -
                                         renderer.getTextWidth(SMALL_FONT_ID, count),
                      header.y + 7, count);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void Ao3LibraryActivity::renderManagePanel() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen(0xFF);
  const Rect header{0, metrics.topPadding, pageWidth, metrics.headerHeight};
  GUI.drawHeader(renderer, header, "Manage AO3 Library");
  const int margin = 20;
  const char* labels[6] = {"Index New Books", "AO3 Folder", "Ignored Folders", "Index Batch Size", "Filter Mode",
                           "Library Cleanup"};
  const std::string folderLabel = ao3Folder.empty() ? "Not set - select before indexing" : ao3Folder;
  char batchLabel[12];
  snprintf(batchLabel, sizeof(batchLabel), "%d", batchSize);
  char ignoredLabel[24];
  snprintf(ignoredLabel, sizeof(ignoredLabel), "%u selected", static_cast<unsigned>(ignoredFolders.size()));
  const char* values[6] = {"", folderLabel.c_str(), ignoredLabel, batchLabel,
                           filterMode == FilterMode::Automatic ? "Automatic" : "Folder Tree", ""};
  const int contentTop = header.y + header.height + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int rowHeight = contentHeight / 6;
  for (int row = 0; row < 6; ++row) {
    const int y = contentTop + row * rowHeight;
    if (row == manageRowIndex) {
      renderer.fillRoundedRect(margin, y + 2, pageWidth - margin * 2, rowHeight - 5, 6, LightGray);
    }
    renderer.drawText(UI_10_FONT_ID, margin + 10, y + 6, labels[row], true,
                      row == manageRowIndex ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    if (values[row][0]) {
      const std::string value = truncatedToFit(renderer, values[row], SMALL_FONT_ID, pageWidth - margin * 2 - 20,
                                                EpdFontFamily::REGULAR);
      renderer.drawText(SMALL_FONT_ID, margin + 10, y + 30, value.c_str(), true);
    }
  }
  const auto buttonLabels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, buttonLabels.btn1, buttonLabels.btn2, buttonLabels.btn3, buttonLabels.btn4);
  renderer.displayBuffer();
}

void Ao3LibraryActivity::renderEntry(RenderLock& lock, const int y, const ViewEntry& entry,
                                     const int cacheSlot, const bool selected, const int entryHeight) {
  const int margin = 20;
  const int squareSize = 56;
  const int textX = margin + squareSize + 15;
  const auto& metadata = pageMetadata[cacheSlot];
  const bool loaded = pageMetadataLoaded[cacheSlot];

  if (selected) {
    renderer.fillRoundedRect(textX - 8, y - 3, renderer.getScreenWidth() - textX - 15, squareSize + 6, 6,
                             LightGray);
  }

  drawAo3Square(lock, margin, y, squareSize, loaded ? metadata.rating : '-', loaded ? metadata.warning : 0,
                loaded && metadata.isCompleted, pageStatus[cacheSlot]);

  std::string title = loaded && metadata.title[0] ? metadata.title : entry.title;
  std::string byline = loaded && metadata.author[0] ? metadata.author : entry.authorKey;
  if (loaded && metadata.seriesName[0]) {
    if (byline.length() > 11) byline = byline.substr(0, 11) + ".";
    char series[180];
    if (metadata.seriesPart > 0) {
      snprintf(series, sizeof(series), " - %u of %s", metadata.seriesPart, metadata.seriesName);
    } else {
      snprintf(series, sizeof(series), " - %s", metadata.seriesName);
    }
    byline += series;
  }
  const int maxTextWidth = renderer.getScreenWidth() - textX - 25;
  title = truncatedToFit(renderer, title, UI_12_FONT_ID, maxTextWidth, EpdFontFamily::BOLD);
  byline = truncatedToFit(renderer, byline, UI_10_FONT_ID, maxTextWidth, EpdFontFamily::REGULAR);
  renderer.drawText(UI_12_FONT_ID, textX, y + 6, title.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, textX, y + 32, byline.c_str());

  if (!loaded) return;
  int blockY = y + squareSize + 11;
  int tagX = margin;
  for (int i = 0; i < 4; ++i) {
    if (!metadata.tags[i][0]) break;
    const int width = renderer.getTextWidth(SMALL_FONT_ID, metadata.tags[i]) + 16;
    if (tagX + width > renderer.getScreenWidth() - margin) break;
    renderer.drawRoundedRect(tagX, blockY, width, 20, 1, 6, true);
    renderer.drawText(SMALL_FONT_ID, tagX + 8, blockY - 2, metadata.tags[i]);
    tagX += width + 8;
  }
  blockY += 28;

  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int metadataLineY = y + entryHeight - lineHeight - 13;
  for (const auto& line : wrappedSummary[cacheSlot]) {
    if (blockY + lineHeight > metadataLineY) break;
    renderer.drawText(SMALL_FONT_ID, margin, blockY, line.c_str());
    blockY += lineHeight;
  }

  char details[144];
  if (metadata.updatedDate[0]) {
    snprintf(details, sizeof(details), "Chapters: %u   Words: %lu   Updated: %s", metadata.chapterCount,
             static_cast<unsigned long>(metadata.wordCount), metadata.updatedDate);
  } else {
    snprintf(details, sizeof(details), "Chapters: %u   Words: %lu", metadata.chapterCount,
             static_cast<unsigned long>(metadata.wordCount));
  }
  renderer.drawText(SMALL_FONT_ID, margin, metadataLineY, details);
}

void Ao3LibraryActivity::drawAo3Square(RenderLock&, const int x, const int y, const int size,
                                       const char rating, const char warning, const bool completed,
                                       const DisplayStatus status) {
  const int half = size / 2;
  renderRatingSymbol(x + 1, y + 1, half - 1, rating, true, false, false, false, -1);
  renderStatusSymbol(x + half + 1, y + 1, half - 1, status, false, true, false, false, -1);
  renderWarningSymbol(x + 1, y + half + 1, half - 1, warning, false, false, true, false, -2);
  renderCompletionSymbol(x + half + 1, y + half + 1, half - 1, completed, false, false, false, true, -2);
  renderer.drawRoundedRect(x, y, size, size, 1, 6, true);
  renderer.drawLine(x + 1, y + half, x + size - 1, y + half);
  renderer.drawLine(x + half, y + 1, x + half, y + size - 1);
}

void Ao3LibraryActivity::renderRatingSymbol(const int x, const int y, const int size, const char rating,
                                            const bool topLeft, const bool topRight, const bool bottomLeft,
                                            const bool bottomRight, const int yOffset) {
  Color background = White;
  if (rating == 'T') background = LightGray;
  if (rating == 'M') background = DarkGray;
  if (rating == 'E') background = Black;
  if (background != White) {
    renderer.fillRoundedRect(x, y, size, size, 6, topLeft, topRight, bottomLeft, bottomRight, background);
  }
  char label[2] = {rating ? rating : '-', 0};
  const int width = renderer.getTextWidth(UI_10_FONT_ID, label);
  const int height = renderer.getTextHeight(UI_10_FONT_ID);
  renderer.drawText(UI_10_FONT_ID, x + (size - width) / 2, y + (size - height) / 2 + yOffset, label,
                    background != DarkGray && background != Black);
}

void Ao3LibraryActivity::renderStatusSymbol(const int x, const int y, const int size,
                                            const DisplayStatus status, const bool topLeft,
                                            const bool topRight, const bool bottomLeft,
                                            const bool bottomRight, const int yOffset) {
  if (status == DisplayStatus::Waiting || status == DisplayStatus::UpdateAvailable) {
    const int triangleWidth = size / 2;
    const int triangleHeight = size / 2;
    const int triangleX = x + (size - triangleWidth) / 2;
    const int triangleY = y + (size - triangleHeight) / 2 + yOffset;
    const int xs[] = {triangleX + triangleWidth / 2, triangleX, triangleX + triangleWidth};
    const int ys[] = {triangleY, triangleY + triangleHeight, triangleY + triangleHeight};
    renderer.fillPolygon(xs, ys, 3, Black);
    if (status == DisplayStatus::UpdateAvailable) {
      renderer.fillRoundedRect(x + size - 6, y - 3, 11, 10, 4, true, true, true, true, Black);
    }
    return;
  }

  const char* label = "-";
  Color background = White;
  if (status == DisplayStatus::Reading) {
    background = LightGray;
    label = "R";
  } else if (status == DisplayStatus::Finished) {
    background = Black;
    label = "F";
  }
  if (background != White) {
    renderer.fillRoundedRect(x, y, size, size, 6, topLeft, topRight, bottomLeft, bottomRight, background);
  }
  const int width = renderer.getTextWidth(UI_10_FONT_ID, label);
  const int height = renderer.getTextHeight(UI_10_FONT_ID);
  renderer.drawText(UI_10_FONT_ID, x + (size - width) / 2, y + (size - height) / 2 + yOffset, label,
                    background != Black);
}

void Ao3LibraryActivity::renderWarningSymbol(const int x, const int y, const int size, const char warning,
                                             const bool topLeft, const bool topRight, const bool bottomLeft,
                                             const bool bottomRight, const int yOffset) {
  Color background = White;
  const char* label = "-";
  if (warning == 'B') {
    background = DarkGray;
    label = "!?";
  } else if (warning == '!') {
    background = Black;
    label = "!";
  }
  if (background != White) {
    renderer.fillRoundedRect(x, y, size, size, 6, topLeft, topRight, bottomLeft, bottomRight, background);
  }
  const int width = renderer.getTextWidth(UI_10_FONT_ID, label);
  const int height = renderer.getTextHeight(UI_10_FONT_ID);
  renderer.drawText(UI_10_FONT_ID, x + (size - width) / 2, y + (size - height) / 2 + yOffset, label,
                    background != DarkGray && background != Black);
}

void Ao3LibraryActivity::renderCompletionSymbol(const int x, const int y, const int size,
                                                const bool completed, const bool topLeft,
                                                const bool topRight, const bool bottomLeft,
                                                const bool bottomRight, const int yOffset) {
  const Color background = completed ? LightGray : Black;
  renderer.fillRoundedRect(x, y, size, size, 6, topLeft, topRight, bottomLeft, bottomRight, background);
  if (completed) {
    for (int offset = 0; offset < 4; ++offset) {
      renderer.drawLine(x + 8, y + 14 + yOffset + offset, x + 12, y + 18 + yOffset + offset);
      renderer.drawLine(x + 12, y + 18 + yOffset + offset, x + 19, y + 11 + yOffset + offset);
    }
  } else {
    renderer.drawLine(x + 7, y + 8 + yOffset, x + 18, y + 18 + yOffset, 4, false);
    renderer.drawLine(x + 7, y + 18 + yOffset, x + 18, y + 8 + yOffset, 4, false);
  }
}
