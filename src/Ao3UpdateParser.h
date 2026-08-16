#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct Ao3RemoteWorkInfo {
  std::string updatedDate;
  bool isCompleted = false;
  bool hasDate = false;
  bool hasChapterStatus = false;
};

class Ao3UpdateParser {
 public:
  bool feed(const uint8_t* data, size_t length);
  const Ao3RemoteWorkInfo& result() const { return result_; }
  bool complete() const { return result_.hasDate && result_.hasChapterStatus; }

 private:
  void parseWindow();

  std::string window_;
  Ao3RemoteWorkInfo result_;
};
