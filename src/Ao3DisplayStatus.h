#pragma once

#include <cstdint>
#include <string>

#include "Ao3LibraryMetadata.h"

enum class Ao3DisplayStatus : uint8_t {
  Unread,
  Reading,
  Waiting,
  UpdateAvailable,
  Finished,
};

Ao3DisplayStatus deriveAo3DisplayStatus(const Ao3LibraryMetadata& metadata);

// Loads the indexed AO3 sidecar for path and derives its current display state.
// Returns false for ordinary or not-yet-indexed EPUBs.
bool loadAo3DisplayStatus(const std::string& path, Ao3DisplayStatus& status);

// Every EPUB has a basic reading status. Indexed AO3 works additionally use
// Waiting and Update Available. isAo3 is optional.
bool loadEpubDisplayStatus(const std::string& path, Ao3DisplayStatus& status, bool* isAo3 = nullptr);

const char* ao3DisplayStatusLabel(Ao3DisplayStatus status);
