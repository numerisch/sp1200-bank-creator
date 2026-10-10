// SPDX-License-Identifier: AGPL-3.0-only
// Copyright © 2026 Numerisch GmbH

static void writeHelpSnapshot() {
  const auto destination =
      SystemStats::getEnvironmentVariable("SP_UI_SNAPSHOT_DIR", {});
  if (destination.isEmpty())
    return;
  auto &desktop = Desktop::getInstance();
  for (int i = 0; i < desktop.getNumComponents(); ++i) {
    auto *window = dynamic_cast<DocumentWindow *>(desktop.getComponent(i));
    if (!window || window->getName() != "SP-1200 Bank Creator Help")
      continue;
    auto *content = window->getContentComponent();
    if (!content)
      continue;
    FileOutputStream stream(File(destination).getChildFile("help.png"));
    stream.setPosition(0);
    stream.truncate();
    PNGImageFormat().writeImageToStream(
        content->createComponentSnapshot(content->getLocalBounds()), stream);
  }
}

#pragma once

// Optional visual QA during --smoke-test; no files are read/written by default.
static void writeUiSnapshots(MainView &view) {
  const auto destination =
      SystemStats::getEnvironmentVariable("SP_UI_SNAPSHOT_DIR", {});
  if (destination.isEmpty())
    return;
  const auto kit = SystemStats::getEnvironmentVariable("SP_UI_PREVIEW_KIT", {});
  if (kit.isNotEmpty()) {
    StringArray warnings;
    view.project = sp::importFiles({kit}, warnings, true);
    view.project.autoTrim =
        SystemStats::getEnvironmentVariable("SP_UI_MANUAL_IMPORT", {}) != "1";
    view.selected =
        jlimit(0, 31,
               SystemStats::getEnvironmentVariable("SP_UI_SELECTED_PAD", "0")
                   .getIntValue());
    view.project.autoSort = true;
    const bool memoryWarning =
        SystemStats::getEnvironmentVariable("SP_UI_MEMORY_WARNING", {}) == "1";
    if (memoryWarning) {
      view.project.autoTune = false;
      view.project.autoTrim = false;
      // Simulate an older preset whose manual selections exceed the limit.
      for (auto &pad : view.project.pads)
        pad.start = pad.end = -1;
    }
    if (SystemStats::getEnvironmentVariable("SP_UI_LOOP", {}) == "1") {
      auto &pad = view.project.pads[view.selected];
      pad.loopEnabled = true;
      pad.loopSeconds = .03;
    }
    try {
      view.result = std::make_shared<sp::Result>(sp::process(view.project));
    } catch (const sp::MemoryError &e) {
      view.memoryProblems = e.pads;
      view.status.setText(e.what(), dontSendNotification);
    }
    if (view.result) {
      for (int i = 0; i < 32; ++i) {
        const auto &r = view.result->sounds[i];
        view.padDisplayAssets[i] = view.project.pads[i].asset;
        view.padDisplayText[i] = String(r.values.size() / double(sp::rate), 2) +
                                 "s / T" + String(r.tune);
      }
      const int frames = std::accumulate(view.result->used.begin(),
                                         view.result->used.end(), 0);
      view.status.setText(
          String(view.project.pool.size()) + " samples in pool  |  " +
              String(frames / double(sp::rate), 2) + " s stored  |  " +
              String((8 * 65534 - frames) / double(sp::rate), 2) + " s free",
          dontSendNotification);
    }
  }
  const auto map = SystemStats::getEnvironmentVariable("SP_UI_AUTO_MAP", {});
  if (map.isNotEmpty())
    sp::setAutoMap(view.project, map == "1");
  const File output(destination);
  output.createDirectory();
  for (const int width : {1280, 1100}) {
    view.setSize(width, width == 1280 ? 850 : 730);
    view.refresh();
    const bool editThreshold =
        SystemStats::getEnvironmentVariable("SP_UI_EDIT_THRESHOLD", {}) == "1";
    if (editThreshold)
      view.threshold.showTextBox();
    const auto snapshot = view.createComponentSnapshot(view.getLocalBounds());
    if (editThreshold)
      view.threshold.hideTextBox(true);
    const auto file =
        output.getChildFile("sp1200-ui-" + String(width) + ".png");
    FileOutputStream stream(file);
    stream.setPosition(0);
    stream.truncate();
    PNGImageFormat().writeImageToStream(snapshot, stream);
  }
}
