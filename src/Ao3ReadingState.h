#pragma once

#include <cstdint>
#include <string>

enum class Ao3ReadingState : uint8_t {
  None = 0,
  WaitingForChapter = 1,
  UpdateAvailable = 2,
};

class Ao3ReadingStateStore {
 public:
  static Ao3ReadingState load(const std::string& cachePath);
  static bool save(const std::string& cachePath, Ao3ReadingState state);
  static bool remove(const std::string& cachePath);
  static const char* labelFor(Ao3ReadingState state);
};
