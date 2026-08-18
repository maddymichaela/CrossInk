#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class Ao3ReadingState : uint8_t {
  None = 0,
  WaitingForChapter = 1,
  UpdateAvailable = 2,
  Unread = 3,
  Reading = 4,
  Finished = 5,
};

class Ao3ReadingStateStore {
 public:
  static Ao3ReadingState load(const std::string& cachePath);
  static bool save(const std::string& cachePath, Ao3ReadingState state);
  static bool remove(const std::string& cachePath);
  static const char* labelFor(Ao3ReadingState state);
};

std::vector<std::string> ao3ReadingStateOptions();
uint8_t ao3ReadingStateOptionIndex(Ao3ReadingState state);
Ao3ReadingState ao3ReadingStateForOption(uint8_t index);
