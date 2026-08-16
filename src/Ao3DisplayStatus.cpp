#include "Ao3DisplayStatus.h"

#include <Epub.h>

#include "Ao3Librarian.h"
#include "Ao3ReadingState.h"
#include "activities/reader/BookReadingStats.h"

Ao3DisplayStatus deriveAo3DisplayStatus(const Ao3LibraryMetadata& metadata) {
  if (metadata.filepath[0] == '\0') return Ao3DisplayStatus::Unread;

  const std::string cachePath = Epub::cachePathForFilePath(metadata.filepath, "/.crosspoint");
  switch (Ao3ReadingStateStore::load(cachePath)) {
    case Ao3ReadingState::UpdateAvailable:
      return Ao3DisplayStatus::UpdateAvailable;
    case Ao3ReadingState::WaitingForChapter:
      return Ao3DisplayStatus::Waiting;
    case Ao3ReadingState::None:
      break;
  }

  const BookReadingStats stats = BookReadingStats::load(cachePath);
  if (stats.isCompleted) {
    return metadata.isCompleted ? Ao3DisplayStatus::Finished : Ao3DisplayStatus::Waiting;
  }
  if (stats.sessionCount > 0 || stats.totalPagesTurned > 0 || stats.totalReadingSeconds > 0) {
    return Ao3DisplayStatus::Reading;
  }
  return Ao3DisplayStatus::Unread;
}

bool loadAo3DisplayStatus(const std::string& path, Ao3DisplayStatus& status) {
  Epub epub(path, "/.crosspoint");
  Ao3LibraryMetadata metadata;
  if (!Ao3Librarian::getLibraryInfo(epub, metadata)) return false;
  status = deriveAo3DisplayStatus(metadata);
  return true;
}

const char* ao3DisplayStatusLabel(const Ao3DisplayStatus status) {
  switch (status) {
    case Ao3DisplayStatus::Unread:
      return "Unread";
    case Ao3DisplayStatus::Reading:
      return "Reading";
    case Ao3DisplayStatus::Waiting:
      return "Waiting";
    case Ao3DisplayStatus::UpdateAvailable:
      return "New Chapter";
    case Ao3DisplayStatus::Finished:
      return "Finished";
  }
  return "";
}
