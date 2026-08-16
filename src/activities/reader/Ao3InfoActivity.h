#pragma once

#include <string>

#include "Ao3LibraryMetadata.h"
#include "activities/Activity.h"

class Ao3InfoActivity final : public Activity {
 public:
  Ao3InfoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Ao3LibraryMetadata metadata,
                  std::string cachePath, std::string workId, std::string updateDate);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool allowPowerAsConfirmInReaderMode() const override { return true; }

 private:
  void requestUpdateCheck();

  Ao3LibraryMetadata metadata_;
  std::string cachePath_;
  std::string workId_;
  std::string updateDate_;
};
