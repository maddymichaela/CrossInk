#include "Ao3InfoActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "Ao3ReadingState.h"
#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "components/TouchHeaderBackButton.h"
#include "components/TouchRegistry.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int CHECK_UPDATE_TARGET = 0;

const char* ratingLabel(const char rating) {
  switch (rating) {
    case 'G':
      return "General";
    case 'T':
      return "Teen";
    case 'M':
      return "Mature";
    case 'E':
      return "Explicit";
    default:
      return "Not rated";
  }
}

const char* warningLabel(const char warning) {
  switch (warning) {
    case 'B':
      return "Creator chose not to warn";
    case '!':
      return "Archive warning applies";
    case '-':
      return "No archive warnings";
    default:
      return "Not specified";
  }
}

void drawRow(const GfxRenderer& renderer, const int x, const int valueX, const int y, const char* label,
             const char* value) {
  renderer.drawText(SMALL_FONT_ID, x, y, label, true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, valueX, y, value && value[0] ? value : "-", true);
}
}  // namespace

Ao3InfoActivity::Ao3InfoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 Ao3LibraryMetadata metadata, std::string cachePath, std::string workId,
                                 std::string updateDate)
    : Activity("Ao3Info", renderer, mappedInput),
      metadata_(std::move(metadata)),
      cachePath_(std::move(cachePath)),
      workId_(std::move(workId)),
      updateDate_(std::move(updateDate)) {}

void Ao3InfoActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void Ao3InfoActivity::requestUpdateCheck() {
  setResult(Ao3InfoResult{true});
  finish();
}

void Ao3InfoActivity::loop() {
  if (TouchHeaderBackButton::wasTapped(mappedInput, renderer) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  int target = -1;
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      (mappedInput.wasItemTapped(target) && target == CHECK_UPDATE_TARGET)) {
    requestUpdateCheck();
  }
}

void Ao3InfoActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::draw(renderer, header, "AO3 Information", true);
  } else {
    GUI.drawHeader(renderer, header, "AO3 Information", nullptr, true);
  }

  const int left = metrics.contentSidePadding;
  const int valueX = left + 118;
  const int contentWidth = renderer.getScreenWidth() - left * 2;
  int y = header.y + header.height + metrics.verticalSpacing;

  const char* title = metadata_.title[0] ? metadata_.title : "AO3 Work";
  const auto titleLines = renderer.wrappedText(UI_12_FONT_ID, title, contentWidth, 2, EpdFontFamily::BOLD);
  const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  for (const auto& line : titleLines) {
    renderer.drawText(UI_12_FONT_ID, left, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += titleLineHeight;
  }
  y += metrics.verticalSpacing;

  char words[24];
  snprintf(words, sizeof(words), "%lu", static_cast<unsigned long>(metadata_.wordCount));
  char chapters[24];
  if (metadata_.isCompleted) {
    snprintf(chapters, sizeof(chapters), "%u/%u", metadata_.chapterCount, metadata_.chapterCount);
  } else {
    snprintf(chapters, sizeof(chapters), "%u/?", metadata_.chapterCount);
  }

  const Ao3ReadingState state = Ao3ReadingStateStore::load(cachePath_);
  const char* status = Ao3ReadingStateStore::labelFor(state);
  if (!status || status[0] == '\0') status = metadata_.isCompleted ? "Complete" : "Work in progress";

  const int rowHeight = renderer.getLineHeight(SMALL_FONT_ID) + 7;
  drawRow(renderer, left, valueX, y, "Rating", ratingLabel(metadata_.rating));
  y += rowHeight;
  drawRow(renderer, left, valueX, y, "Warnings", warningLabel(metadata_.warning));
  y += rowHeight;
  drawRow(renderer, left, valueX, y, "Words", words);
  y += rowHeight;
  drawRow(renderer, left, valueX, y, "Chapters", chapters);
  y += rowHeight;
  drawRow(renderer, left, valueX, y, "Updated", metadata_.updatedDate[0] ? metadata_.updatedDate : updateDate_.c_str());
  y += rowHeight;
  drawRow(renderer, left, valueX, y, "Status", status);
  y += rowHeight;
  if (metadata_.seriesName[0]) {
    drawRow(renderer, left, valueX, y, "Series", metadata_.seriesName);
    y += rowHeight;
  }
  drawRow(renderer, left, valueX, y, "Work ID", workId_.c_str());
  y += rowHeight + metrics.verticalSpacing;

  if (metadata_.summary[0]) {
    renderer.drawText(SMALL_FONT_ID, left, y, "Summary", true, EpdFontFamily::BOLD);
    y += rowHeight;
    const int availableHeight = renderer.getScreenHeight() - metrics.buttonHintsHeight - y - 58;
    const int maxLines = std::max(1, availableHeight / renderer.getLineHeight(SMALL_FONT_ID));
    const auto lines = renderer.wrappedText(SMALL_FONT_ID, metadata_.summary, contentWidth, maxLines);
    for (const auto& line : lines) {
      renderer.drawText(SMALL_FONT_ID, left, y, line.c_str(), true);
      y += renderer.getLineHeight(SMALL_FONT_ID);
    }
  }

  const int buttonWidth = std::min(220, contentWidth);
  const int buttonHeight = 38;
  const int buttonX = (renderer.getScreenWidth() - buttonWidth) / 2;
  const int buttonY = renderer.getScreenHeight() - metrics.buttonHintsHeight - buttonHeight - 8;
  renderer.drawRect(buttonX, buttonY, buttonWidth, buttonHeight);
  const char* buttonLabel = "Check for Updates";
  const int buttonTextWidth = renderer.getTextWidth(UI_10_FONT_ID, buttonLabel, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, buttonX + (buttonWidth - buttonTextWidth) / 2,
                    buttonY + (buttonHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2, buttonLabel, true,
                    EpdFontFamily::BOLD);
  TouchRegistry::getInstance().add(Rect{buttonX, buttonY, buttonWidth, buttonHeight}, CHECK_UPDATE_TARGET,
                                   TouchRegistry::Item);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CHECK_UPDATES), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  renderer.displayBuffer();
}
