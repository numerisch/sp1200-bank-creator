// SPDX-License-Identifier: AGPL-3.0-only
// Copyright © 2026 Numerisch GmbH

#pragma once
#include <iostream>

static bool testWaveSelectionDrag() {
  Wave wave;
  wave.setSize(1000, 160);
  wave.asset = std::make_shared<sp::Asset>();
  wave.asset->sampleRate = 1000;
  wave.asset->audio.setSize(1, 8000);
  wave.asset->audio.clear();
  wave.render.start = 1;
  wave.render.end = 3;
  int commits = 0;
  wave.onTrim = [&](double start, double end) {
    ++commits;
    wave.render.start = start;
    wave.render.end = end;
  };
  auto gesture = [&](double from, double to, bool expectCommit) {
    const Point<float> anchor(wave.positionFor(from),
                              wave.plotArea().getCentreY());
    auto event = [&](double time, bool dragged) {
      return MouseEvent(Desktop::getInstance().getMainMouseSource(),
                        {wave.positionFor(time), anchor.y},
                        ModifierKeys(ModifierKeys::leftButtonModifier), 1, 0, 0,
                        0, 0, &wave, &wave, Time::getCurrentTime(), anchor,
                        Time::getCurrentTime(), 1, dragged);
    };
    const int before = commits;
    wave.mouseDown(event(from, false));
    wave.mouseDrag(event(to, true));
    if (commits != before)
      return false;
    wave.mouseUp(event(to, true));
    return commits == before + (expectCommit ? 1 : 0);
  };
  auto rangeIs = [&](double start, double end) {
    return std::abs(wave.startTime() - start) < .0001 &&
           std::abs(wave.endTime() - end) < .0001;
  };
  bool cached = wave.updateEnvelope(984) && !wave.updateEnvelope(984);
  bool ok = gesture(2, 3.5, true) && rangeIs(2.5, 4.5);
  ok &= gesture(3.5, 20, true) && rangeIs(6, 8);
  ok &= gesture(7, -20, true) && rangeIs(0, 2);
  ok &= gesture(0, .5, true) && rangeIs(.5, 2);
  cached &= !wave.updateEnvelope(984);
  wave.reversed = true;
  cached &= wave.updateEnvelope(984) && !wave.updateEnvelope(984);
  cached &= wave.updateEnvelope(500) && !wave.updateEnvelope(500);
  wave.asset = std::make_shared<sp::Asset>(*wave.asset);
  cached &= wave.updateEnvelope(500);
  ok &= gesture(1, 1, false) && rangeIs(.5, 2);
  ok &= cached;
  wave.setEnabled(false);
  ok &= gesture(1, 3, false) && rangeIs(.5, 2);
  std::cout << (ok ? "PASS" : "FAIL")
            << ": waveform selection drag, boundary clamp, handle resize and "
               "disabled state\n";
  return ok;
}

static bool testLoopWaveAndPlayback(MainView &view) {
  Wave wave;
  wave.setSize(1000, 160);
  wave.asset = std::make_shared<sp::Asset>();
  wave.asset->sampleRate = 1000;
  wave.asset->audio.setSize(1, 8000);
  wave.asset->audio.clear();
  wave.render.start = 1;
  wave.render.end = 3;
  wave.loopEnabled = true;
  int loopCommits = 0, trimCommits = 0;
  wave.onLoop = [&](double seconds) {
    ++loopCommits;
    wave.loopSeconds = seconds;
  };
  wave.onTrim = [&](double, double) { ++trimCommits; };
  const auto area = wave.plotArea();
  bool ok = wave.dragTargetAt({wave.positionFor(1), area.getCentreY()}) == 4 &&
            wave.dragTargetAt({wave.positionFor(1), area.getY() + 4}) == 1 &&
            wave.dragTargetAt({wave.positionFor(3), area.getBottom() - 4}) == 2;
  const Point<float> anchor(wave.positionFor(1), area.getCentreY());
  auto event = [&](double time, bool dragged) {
    return MouseEvent(Desktop::getInstance().getMainMouseSource(),
                      {wave.positionFor(time), anchor.y},
                      ModifierKeys(ModifierKeys::leftButtonModifier), 1, 0, 0,
                      0, 0, &wave, &wave, Time::getCurrentTime(), anchor,
                      Time::getCurrentTime(), 1, dragged);
  };
  wave.mouseDown(event(1, false));
  wave.mouseDrag(event(2.5, true));
  ok &= loopCommits == 0 && trimCommits == 0 && !wave.manualTrim;
  wave.mouseUp(event(2.5, true));
  ok &= loopCommits == 1 && trimCommits == 0 &&
        std::abs(wave.loopSeconds - .5) < .0001 && wave.startTime() == 1 &&
        wave.endTime() == 3;
  auto trimGesture = [&](double from, double to, bool start) {
    const Point<float> trimAnchor(
        wave.positionFor(from), start ? area.getY() + 4 : area.getBottom() - 4);
    auto trimEvent = [&](double time, bool dragged) {
      return MouseEvent(Desktop::getInstance().getMainMouseSource(),
                        {wave.positionFor(time), trimAnchor.y},
                        ModifierKeys(ModifierKeys::leftButtonModifier), 1, 0, 0,
                        0, 0, &wave, &wave, Time::getCurrentTime(), trimAnchor,
                        Time::getCurrentTime(), 1, dragged);
    };
    wave.mouseDown(trimEvent(from, false));
    wave.mouseDrag(trimEvent(to, true));
    const auto pointWhileDragging = wave.loopStartTime();
    wave.mouseUp(trimEvent(to, true));
    return pointWhileDragging;
  };
  ok &= std::abs(trimGesture(3, 3.5, false) - 2.5) < .0001 &&
        std::abs(wave.loopStartTime() - 2.5) < .0001;
  ok &= std::abs(trimGesture(3.5, 2, false) - 1.999) < .0001 &&
        std::abs(wave.loopStartTime() - 1.999) < .0001;
  ok &= std::abs(trimGesture(2, 2.8, false) - 1.999) < .0001;
  ok &= std::abs(trimGesture(1, 2.2, true) - 2.2) < .0001;
  wave.loopEnabled = false;
  ok &= wave.dragTargetAt({wave.positionFor(2.5), area.getCentreY()}) == 3;

  sp::Render r;
  r.values = {2048, 2304, 2560, 2816};
  r.loopFrames = 2;
  view.prepareToPlay(16, sp::rate);
  view.playRender(r);
  AudioBuffer<float> output(2, 16);
  view.getNextAudioBlock({&output, 0, 10});
  for (int i = 0; i < 10; ++i) {
    const int source = i < 4 ? i : 2 + (i - 4) % 2;
    ok &= std::abs(output.getSample(0, i) - source * .125f) < 1e-6f &&
          output.getSample(0, i) == output.getSample(1, i);
  }
  ok &= view.active;
  view.stopAudio();
  view.getNextAudioBlock({&output, 0, 10});
  ok &= !view.active && output.getMagnitude(0, 10) == 0;
  view.playRender(r);
  view.getNextAudioBlock({&output, 0, 1});
  ok &= output.getSample(0, 0) == 0; // Retrigger starts at the attack.
  view.step = 5.5; // Cross multiple loop boundaries inside a single block.
  view.position = 3.5;
  view.getNextAudioBlock({&output, 0, 3});
  ok &= std::abs(output.getSample(0, 0) - .3125f) < 1e-6f &&
        std::abs(output.getSample(0, 1) - .375f) < 1e-6f &&
        std::abs(output.getSample(0, 2) - .3125f) < 1e-6f && view.active;
  sp::Render other;
  other.values = {1024, 1024};
  view.playRender(other);
  view.getNextAudioBlock({&output, 0, 10});
  ok &= output.getSample(0, 0) == -.5f && !view.active &&
        view.playbackLoopFrames == 0;
  sp::Render neutral;
  neutral.values.assign(1000, 3000);
  neutral.loopFrames = 200;
  neutral.start = 1;
  view.playRender(neutral, 0);
  view.position = 400;
  const double sourceTime = 1 + 400. / sp::rate;
  auto pitched = neutral;
  pitched.tune = 7;
  pitched.start = 1.001;
  view.playRender(pitched, 0, true);
  ok &= view.active && std::abs(view.playbackSourceStart +
                                view.position / view.playbackFramesPerSecond -
                                sourceTime) < 1e-9;
  pitched.values.resize(100);
  pitched.loopFrames = 20;
  view.playRender(pitched, 0, true);
  ok &= view.active && view.position >= 80 && view.position < 100;
  view.stopAudio();
  std::cout
      << (ok ? "PASS" : "FAIL")
      << ": loop handle separation, single drag commit, attack/tail playback, "
         "stop, retrigger, sound switch and multi-wrap interpolation\n";
  return ok;
}

static bool testPadLengthWarnings(MainView &view) {
  view.project = sp::Project{};
  for (int i = 0; i < 3; ++i) {
    auto asset = std::make_shared<sp::Asset>();
    asset->id = "length-fixture-" + String(i);
    asset->filename = "LENGTH.wav";
    asset->sampleRate = sp::rate;
    asset->audio.setSize(1, (i == 0 ? 2 : i == 1 ? 4 : 7) * sp::rate);
    asset->audio.clear();
    view.project.pool.push_back(asset);
    view.project.pads[i] = sp::makePad(*asset, i, false, false);
  }
  view.result = std::make_shared<sp::Result>();
  view.memoryProblems.fill(
      true); // Bank allocation failures must not colour short sources.
  auto redBorder = [&](int pad) {
    const auto image = view.pads[pad]->createComponentSnapshot(
        view.pads[pad]->getLocalBounds());
    return image.getPixelAt(2, 40) == spStyle::red;
  };
  view.project.autoTune = false;
  view.refresh();
  bool ok = !redBorder(0) && redBorder(1) && redBorder(2) && !redBorder(3);
  sp::setAutoTune(view.project, true);
  view.refresh();
  ok &= !redBorder(0) && !redBorder(1) && redBorder(2);
  view.project.autoTrim = true;
  view.project.pads[2].start = 1;
  view.project.pads[2].end = 1.5;
  view.refresh();
  ok &= redBorder(2); // Cuts, successful rendering and Auto Trim do not hide
                      // original length.
  view.project.pads[2] = sp::Pad{};
  view.refresh();
  ok &= !redBorder(2);
  view.project = sp::Project{};
  view.result.reset();
  view.memoryProblems = {};
  view.refresh();
  std::cout << (ok ? "PASS" : "FAIL")
            << ": red pad outlines follow source duration and Pitch Fit, "
               "independently of cuts and bank errors\n";
  return ok;
}

static bool testSettingsMenu(AppMenu &menu, MainView &view) {
  ApplicationCommandInfo info(AppMenu::showSettings);
  menu.getCommandInfo(AppMenu::showSettings, info);
  bool ok = info.defaultKeypresses.contains(
      KeyPress(',', ModifierKeys::commandModifier, 0));
  bool found = false;
  const auto applicationMenu = menu.getApplicationMenu();
  for (PopupMenu::MenuItemIterator it(applicationMenu); it.next();)
    found |= it.getItem().itemID == AppMenu::showSettings;
  ok &= found && menu.perform(ApplicationCommandTarget::InvocationInfo(
                     AppMenu::showSettings));
  SettingsWindow *window = nullptr;
  for (int i = 0; i < TopLevelWindow::getNumTopLevelWindows(); ++i)
    if (auto *w = dynamic_cast<SettingsWindow *>(
            TopLevelWindow::getTopLevelWindow(i)))
      window = w;
  if (!window) {
    std::cerr << "FAIL: Settings window missing\n";
    return false;
  }
  auto *panel = static_cast<SettingsPanel *>(window->getContentComponent());
  ok &= window->isVisible() && !panel->fadeOut.getToggleState() &&
        panel->normalize.getToggleState();
  panel->fadeOut.setToggleState(true, dontSendNotification);
  panel->fadeOut.onClick();
  ok &= view.project.fadeOutOnExport && view.project.normalizeOnExport;
  panel->normalize.setToggleState(false, dontSendNotification);
  panel->normalize.onClick();
  ok &= view.project.fadeOutOnExport && !view.project.normalizeOnExport;
  view.undoEdit();
  ok &= panel->normalize.getToggleState() && view.project.normalizeOnExport;
  view.redoEdit();
  ok &= !panel->normalize.getToggleState() && !view.project.normalizeOnExport;
  view.busy = true;
  view.refresh();
  ok &= !panel->fadeOut.isEnabled() && !panel->normalize.isEnabled();
  view.busy = false;
  view.refresh();
  view.setExportSettings(false, true);
  window->closeButtonPressed();
  ok &= !window->isVisible();
  menu.perform(ApplicationCommandTarget::InvocationInfo(AppMenu::showSettings));
  ok &= window->isVisible() && !panel->fadeOut.getToggleState() &&
        panel->normalize.getToggleState();
  const auto destination =
      SystemStats::getEnvironmentVariable("SP_UI_SNAPSHOT_DIR", {});
  if (destination.isNotEmpty()) {
    FileOutputStream stream(File(destination).getChildFile("settings.png"));
    stream.setPosition(0);
    stream.truncate();
    PNGImageFormat().writeImageToStream(
        panel->createComponentSnapshot(panel->getLocalBounds()), stream);
  }
  std::cout << (ok ? "PASS" : "FAIL")
            << ": Settings app menu, Cmd+comma, checkbox callbacks, Undo/Redo, "
               "busy state and window reopening\n";
  return ok;
}

// Run only in --playback-test: no audio device, personal settings or files.
static void afterPreviewJobs(MainView &view, std::function<void()> check) {
  view.queue([check = std::move(check)] { MessageManager::callAsync(check); });
}
struct AutoTuneSelectionTest
    : std::enable_shared_from_this<AutoTuneSelectionTest> {
  MainView &view;
  std::function<void(int)> finish;
  int stage = 0;
  AutoTuneSelectionTest(MainView &v, std::function<void(int)> done)
      : view(v), finish(std::move(done)) {}
  void run() {
    const bool enabled = stage % 2 == 0;
    if (stage < 2) {
      view.tune.setToggleState(enabled, dontSendNotification);
      view.tune.onClick();
    } else if (stage == 2) {
      view.undoEdit();
    } else {
      view.redoEdit();
    }
    afterPreviewJobs(view, [self = shared_from_this(), enabled] {
      auto &v = self->view;
      const auto limit = sp::maxSelectionDuration(sp::rate, enabled);
      bool correct = !v.busy && !v.result && v.project.autoTune == enabled &&
                     v.wave.startTime() == 0 && v.wave.endTime() == limit;
      for (int i = 0; i < 9; ++i)
        correct &=
            v.project.pads[i].start == 0 && v.project.pads[i].end == limit;
      if (!correct) {
        std::cerr
            << "FAIL: Pitch Fit selection/Undo/Redo during memory error, stage "
            << self->stage << "\n";
        self->finish(1);
        return;
      }
      if (++self->stage == 4) {
        std::cout
            << "PASS: Pitch Fit resets automatic selections and waveform, "
               "including Undo/Redo during memory errors\n";
        self->finish(0);
      } else {
        self->run();
      }
    });
  }
};
static void testAutoTuneSelections(MainView &view,
                                   std::function<void(int)> finish) {
  auto asset = std::make_shared<sp::Asset>();
  asset->id = "selection-fixture";
  asset->filename = "LONG.wav";
  asset->sampleRate = sp::rate;
  asset->audio.setSize(1, 8 * sp::rate);
  asset->audio.clear();
  view.project = sp::Project{};
  view.project.autoTune = false;
  view.project.pool = {asset};
  for (int i = 0; i < 9; ++i)
    view.project.pads[i] = sp::makePad(*asset, i, false, false);
  view.past.clear();
  view.future.clear();
  view.result.reset();
  view.selected = 0;
  view.refresh();
  std::make_shared<AutoTuneSelectionTest>(view, std::move(finish))->run();
}
static void testMemoryPlaybackAfterMetadata(MainView &view,
                                            std::function<void(int)> finish) {
  if (!testWaveSelectionDrag()) {
    finish(1);
    return;
  }
  auto asset = std::make_shared<sp::Asset>();
  asset->id = "playback-fixture";
  asset->filename = "TEST.wav";
  asset->sampleRate = sp::rate;
  asset->audio.setSize(1, 3 * sp::rate);
  for (int i = 0; i < asset->audio.getNumSamples(); ++i)
    asset->audio.setSample(0, i,
                           float(.1 * std::sin(2 * MathConstants<double>::pi *
                                               440 * i / sp::rate)));
  view.project.autoTune = false;
  view.project.autoTrim = false;
  view.project.pool = {asset};
  // An oversized selection remains playable after Pitch Fit is disabled.
  view.project.pads[0] = sp::makePad(*asset, 0, true, true);
  view.project.pads[1] = view.project.pads[0];
  view.project.pads[1].start = .05;
  view.project.pads[1].end = .1;
  view.project.pads[1].reverse = true;
  view.changed();
  if (!view.canSave(false) || view.canSave(true)) {
    std::cerr << "FAIL: preset save must stay available during processing\n";
    finish(1);
    return;
  }
  afterPreviewJobs(view, [&view, asset, finish] {
    if (view.busy || !view.memoryProblems[0] || !view.canSave(false) ||
        view.canSave(true) ||
        !view.status.getText().containsIgnoreCase("memory") ||
        Component::getNumCurrentlyModalComponents() != 0) {
      std::cerr
          << "FAIL: capacity error must be shown without a modal dialog\n";
      finish(1);
      return;
    }
    view.error("Capacity exceeded.", true);
    if (view.status.getText() != "Capacity exceeded." ||
        Component::getNumCurrentlyModalComponents() != 0) {
      std::cerr << "FAIL: kit memory warning must remain in the status bar\n";
      finish(1);
      return;
    }
    view.refresh();
    view.select(0);
    afterPreviewJobs(view, [&view, asset, finish] {
      if (!view.active || !view.stop.isEnabled() || view.result ||
          view.exportBank.isEnabled() || !view.memoryProblems[0] ||
          view.status.getText() != "Capacity exceeded." ||
          view.playing.getNumSamples() != 3 * sp::rate ||
          view.playing.getMagnitude(0, view.playing.getNumSamples()) < .5f) {
        std::cerr
            << "FAIL: memory-error pad did not start processed playback\n";
        finish(1);
        return;
      }
      view.stopAudio();
      view.selected = 1;
      view.refresh();
      view.playPoolSample(asset);
      afterPreviewJobs(view, [&view, finish] {
        if (!view.active || view.playing.getNumSamples() != sp::rate / 20) {
          std::cerr << "FAIL: pool playback lost the selected pad's cuts\n";
          finish(1);
          return;
        }
        view.stopAudio();
        view.playPad();
        view.stopAudio();
        afterPreviewJobs(view, [&view, finish] {
          const bool stopped = !view.active && !view.stop.isEnabled();
          std::cout << (stopped
                            ? "PASS: pad/pool playback during memory errors "
                              "and pending-preview cancellation\n"
                            : "FAIL: delayed preview restarted after Stop\n");
          if (stopped)
            testAutoTuneSelections(view, finish);
          else
            finish(1);
        });
      });
    });
  });
}

static void testExistingPlayback(MainView &view,
                                 std::function<void(int)> finish) {
  if (!testPadLengthWarnings(view)) {
    finish(1);
    return;
  }
  auto asset = std::make_shared<sp::Asset>();
  asset->id = "metadata-fixture";
  asset->filename = "META.wav";
  asset->sampleRate = sp::rate;
  asset->audio.setSize(1, 1000);
  for (int i = 0; i < 1000; ++i)
    asset->audio.setSample(0, i, float(.1 * std::sin(i * .1)));
  view.project.pool = {asset};
  view.project.pads[0] = sp::makePad(*asset, 0, false, true);
  view.changed();
  afterPreviewJobs(view, [&view, finish] {
    if (!view.result) {
      finish(1);
      return;
    }
    const auto originalRenders = view.renderCache.renderCount();
    const auto audio = view.result->sounds[0].values;
    view.name.setText("RENAMD", false);
    view.commitName();
    afterPreviewJobs(view, [&view, originalRenders, audio, finish] {
      std::array<int, 8> used{};
      if (!view.result || view.renderCache.renderCount() != originalRenders ||
          view.result->sounds[0].values != audio ||
          !view.result->bank.isEmpty() ||
          sp::buildBank(view.project, view.result->sounds, used) !=
              sp::process(view.project).bank) {
        std::cerr
            << "FAIL: name-only edit must reuse cached audio and defer bank "
               "metadata\n";
        finish(1);
        return;
      }
      // Simulate a save holding the previous result while another edit arrives.
      auto saved = view.result;
      const auto savedBank = saved->bank;
      view.edit([](auto &pad) { pad.channel = 7; }, false);
      afterPreviewJobs(view, [&view, saved, savedBank, audio, originalRenders,
                              finish] {
        std::array<int, 8> used{};
        if (!view.result || saved->bank != savedBank ||
            view.renderCache.renderCount() != originalRenders ||
            saved->sounds[0].values != audio ||
            view.result->sounds[0].values != audio ||
            !view.result->bank.isEmpty() ||
            sp::buildBank(view.project, view.result->sounds, used) !=
                sp::process(view.project).bank) {
          std::cerr << "FAIL: channel edit must preserve an in-flight save "
                       "snapshot\n";
          finish(1);
          return;
        }
        std::cout << "PASS: metadata edits reuse audio, match full export and "
                     "isolate save snapshots\n";
        testMemoryPlaybackAfterMetadata(view, finish);
      });
    });
  });
}

struct ProtectedTrimTest : std::enable_shared_from_this<ProtectedTrimTest> {
  MainView &view;
  std::function<void(int)> finish;
  int stage = 0;
  ProtectedTrimTest(MainView &v, std::function<void(int)> done)
      : view(v), finish(std::move(done)) {}
  void run() {
    switch (stage) {
    case 0:
      view.changed();
      break;
    case 1:
      view.wave.onTrim(1, 4);
      break;
    case 2:
      view.threshold.setValue(-5, dontSendNotification);
      view.commitThreshold();
      break;
    case 3:
      view.reset.setToggleState(false, dontSendNotification);
      view.reset.onClick();
      break;
    case 4:
      view.tune.setToggleState(false, dontSendNotification);
      view.tune.onClick();
      break;
    case 5:
      view.resetSelectedTrim();
      break;
    case 6:
      view.undoEdit();
      break;
    case 7:
      view.redoEdit();
      break;
    case 8:
      view.undoEdit();
      break;
    case 9:
      view.reset.setToggleState(true, dontSendNotification);
      view.reset.onClick();
      break;
    case 10:
      view.resetSelectedTrim();
      break;
    case 11:
      view.undoEdit();
      break;
    case 12:
      view.reverse.onClick();
      break;
    case 13:
      view.drop(1, "pad:0");
      view.selected = 1;
      view.refresh();
      break;
    case 14:
      view.drop(1, "pool:0");
      break;
    }
    if (stage == 1 && (view.past.size() != 1 || view.wave.startTime() != 1 ||
                       view.wave.endTime() != 4)) {
      fail();
      return;
    }
    afterPreviewJobs(view, [self = shared_from_this()] {
      auto &v = self->view;
      const auto &pad = v.project.pads[v.selected];
      bool ok = !v.busy && v.wave.isEnabled();
      const int stage = self->stage;
      const bool manual = (stage >= 1 && stage <= 4) || stage == 6 ||
                          stage == 8 || stage == 9 ||
                          (stage >= 11 && stage <= 13);
      ok &=
          pad.manualTrim == manual &&
          v.details.getText().contains(manual               ? "Trim: Manual"
                                       : v.project.autoTrim ? "Trim: Auto"
                                                            : "Trim: Default");
      if (manual) {
        const double start = stage >= 12 ? 2 : 1;
        ok &= pad.start == start && pad.end == start + 3 &&
              v.wave.startTime() == start && v.wave.endTime() == start + 3;
      }
      if (stage == 5 || stage == 7)
        ok &= pad.start == 0 &&
              pad.end == sp::maxSelectionDuration(sp::rate, false);
      if (manual && stage >= 4)
        ok &= !v.result && !v.canSave(true) && v.canSave(false) &&
              v.memoryProblems[v.selected] &&
              Component::getNumCurrentlyModalComponents() == 0;
      else
        ok &= bool(v.result);
      if (!ok) {
        self->fail();
        return;
      }
      if (++self->stage == 15) {
        std::cout
            << "PASS: protected trim UI, threshold/mode changes, Reset Trim, "
               "Undo/Redo, invalid-bank waveform, Reverse and pad moves\n";
        self->finish(0);
      } else
        self->run();
    });
  }
  void fail() {
    std::cerr << "FAIL: protected trim UI, stage " << stage << "\n";
    finish(1);
  }
};
static void testProtectedTrim(MainView &view, std::function<void(int)> finish) {
  auto asset = std::make_shared<sp::Asset>();
  asset->id = "protected-trim";
  asset->filename = "PROTECT.wav";
  asset->sampleRate = sp::rate;
  asset->audio.setSize(1, 6 * sp::rate);
  asset->audio.clear();
  for (int i = sp::rate / 2; i < 5 * sp::rate; ++i)
    asset->audio.setSample(0, i, .5f);
  view.project = sp::Project{};
  view.project.autoTrim = true;
  view.project.pool = {asset};
  view.project.pads[0] = sp::makePad(*asset, 0, true, true);
  view.selected = 0;
  view.past.clear();
  view.future.clear();
  std::make_shared<ProtectedTrimTest>(view, std::move(finish))->run();
}

struct LoopUiTest : std::enable_shared_from_this<LoopUiTest> {
  MainView &view;
  std::function<void(int)> finish;
  int stage = 0;
  LoopUiTest(MainView &v, std::function<void(int)> done)
      : view(v), finish(std::move(done)) {}
  void run() {
    auto &v = view;
    switch (stage) {
    case 0:
      v.loop.onClick();
      break;
    case 1:
      v.wave.onLoop(1.5);
      break;
    case 2:
      v.wave.onTrim(.5, 1.2);
      break;
    case 3:
      v.undoEdit();
      break;
    case 4:
      v.redoEdit();
      break;
    case 5:
      v.wave.onTrim(.2, 1.5);
      break;
    case 6:
      v.reverse.onClick();
      break;
    case 7:
    case 8:
      v.loop.onClick();
      break;
    case 9:
      v.drop(1, "pad:0");
      break;
    case 10:
      v.drop(2, "pool:0");
      break;
    case 11:
      v.selected = 1;
      v.refresh();
      v.wave.onLoop(1. / sp::rate);
      break;
    case 12:
      v.loop.onClick();
      break;
    case 13:
      v.clearSelectedPad();
      break;
    case 14:
      v.undoEdit();
      break;
    case 15:
      v.edit([](auto &pad) {
        pad.manualTrim = false;
        pad.start = 0;
        pad.end = 2;
        pad.loopSeconds = 1.5;
        pad.loopEnabled = true;
      });
      break;
    case 16:
      v.reset.setToggleState(true, dontSendNotification);
      v.reset.onClick();
      break;
    case 17:
      v.threshold.setValue(-20, dontSendNotification);
      v.commitThreshold();
      break;
    case 18:
      v.undoEdit();
      break;
    case 19:
      v.redoEdit();
      break;
    case 20:
      v.reset.setToggleState(false, dontSendNotification);
      v.reset.onClick();
      break;
    case 21:
      v.wave.onTrim(1.2, 1.8);
      break;
    case 22:
      v.resetSelectedTrim();
      break;
    case 23:
      v.undoEdit();
      break;
    case 24:
      v.redoEdit();
      break;
    case 25:
      v.wave.onLoop(.3);
      break;
    case 26:
      v.resetTrimButton.onClick();
      break;
    case 27:
      v.undoEdit();
      break;
    case 28:
      v.loop.onClick();
      break;
    case 29:
      v.resetTrimButton.onClick();
      break;
    }
    afterPreviewJobs(v, [self = shared_from_this()] {
      auto &v = self->view;
      const int stage = self->stage;
      const auto &p = v.project.pads[stage >= 9 ? 1 : 0];
      bool ok = !v.busy && v.canSave(false);
      if (stage == 0)
        ok &= v.result && p.loopEnabled && p.loopSeconds == -1 &&
              v.wave.loopEnabled &&
              v.result->sounds[0].loopFrames ==
                  (int)v.result->sounds[0].values.size();
      if (stage == 1 || stage == 3)
        ok &= !p.manualTrim && p.loopSeconds == 1.5 && v.past.size() == 2;
      if (stage >= 2 && stage <= 10 && stage != 3)
        ok &= p.manualTrim && std::abs(p.loopSeconds - .7) < 1e-6;
      if (stage == 2 || stage == 4)
        ok &= v.wave.startTime() == .5 && v.wave.endTime() == 1.2;
      if (stage == 6)
        ok &= p.reverse && std::abs(p.start - .5) < 1e-6 &&
              std::abs(p.end - 1.8) < 1e-6;
      if (stage == 7)
        ok &= !p.loopEnabled && !v.wave.loopEnabled;
      if (stage == 8 || stage == 9)
        ok &= p.loopEnabled;
      if (stage == 9)
        ok &= v.project.pads[0].asset.isEmpty();
      if (stage == 10)
        ok &= !v.project.pads[2].loopEnabled &&
              v.project.pads[2].loopSeconds == -1;
      if (stage == 11)
        ok &= !v.result && !v.canSave(true) && v.wave.isEnabled() &&
              v.wave.loopEnabled &&
              v.status.getText().contains("3 stored frames") &&
              Component::getNumCurrentlyModalComponents() == 0;
      if (stage == 12 || stage == 14)
        ok &= v.result && !p.loopEnabled && p.loopSeconds == 1. / sp::rate;
      if (stage == 13)
        ok &= p.asset.isEmpty() && !p.loopEnabled && p.loopSeconds == -1;
      if (stage == 15 || stage == 16 || stage == 18)
        ok &= v.result && !p.manualTrim && p.loopSeconds == 1.5;
      if (stage == 17 || stage == 19 || stage == 20)
        ok &= v.result && !p.manualTrim && p.loopSeconds > 1 &&
              p.loopSeconds < 1.01;
      if (stage == 18)
        ok &= v.project.threshold == -40;
      if (stage == 20)
        ok &= !v.project.autoTrim && v.wave.startTime() == 0 &&
              v.wave.endTime() == 2;
      if (stage == 21 || stage == 23)
        ok &= v.result && std::abs(p.loopSeconds - .6) < 1e-6 && p.manualTrim;
      if (stage == 22 || stage == 24 || stage == 26 || stage == 29)
        ok &= v.result && !p.manualTrim && p.loopSeconds == -1 &&
              p.loopEnabled == (stage != 29) && !v.canResetTrim() &&
              !v.resetTrimButton.isEnabled() && v.wave.startTime() == 0 &&
              v.wave.endTime() == 2 && v.wave.loopStartTime() == 0;
      if (stage == 25 || stage == 27 || stage == 28)
        ok &= v.result && !p.manualTrim && p.loopSeconds == .3 &&
              p.loopEnabled == (stage != 28) && v.canResetTrim() &&
              v.resetTrimButton.isEnabled();
      if (!ok) {
        std::cerr << "FAIL: Loop UI stage " << stage << "\n";
        self->finish(1);
      } else if (++self->stage == 30) {
        std::cout << "PASS: loop toggle, persistent clamp, Undo/Redo, Reverse, "
                     "moves, "
                     "new assignment, clear, threshold/mode clamp Undo/Redo, "
                     "Reset Trim and invalid-export waveform\n";
        self->finish(0);
      } else
        self->run();
    });
  }
};
static void testLoopUi(MainView &view, std::function<void(int)> finish) {
  if (!testLoopWaveAndPlayback(view)) {
    finish(1);
    return;
  }
  auto asset = std::make_shared<sp::Asset>();
  asset->id = "loop-ui";
  asset->filename = "LOOP.wav";
  asset->sampleRate = sp::rate;
  asset->audio.setSize(1, 2 * sp::rate);
  for (int i = 0; i < asset->audio.getNumSamples(); ++i)
    asset->audio.setSample(0, i, i < sp::rate ? .2f : .02f);
  view.project = sp::Project{};
  view.project.pool = {asset};
  view.project.pads[0] = sp::makePad(*asset, 0, false, true);
  view.result.reset();
  view.failedTrims.reset();
  view.past.clear();
  view.future.clear();
  view.selected = 0;
  view.refresh();
  std::make_shared<LoopUiTest>(view, std::move(finish))->run();
}
struct LiveLoopEditTest : std::enable_shared_from_this<LiveLoopEditTest> {
  MainView &view;
  std::function<void(int)> finish;
  int stage = 0;
  Point<float> anchor, destination;
  LiveLoopEditTest(MainView &v, std::function<void(int)> done)
      : view(v), finish(std::move(done)) {}
  MouseEvent event(Point<float> point, bool dragged) {
    return MouseEvent(Desktop::getInstance().getMainMouseSource(), point,
                      ModifierKeys(ModifierKeys::leftButtonModifier), 1, 0, 0,
                      0, 0, &view.wave, &view.wave, Time::getCurrentTime(),
                      anchor, Time::getCurrentTime(), 1, dragged);
  }
  void drag(double from, double to, int target) {
    const auto area = view.wave.plotArea();
    const float y = target == 1   ? area.getY() + 4
                    : target == 2 ? area.getBottom() - 4
                                  : area.getCentreY();
    anchor = {view.wave.positionFor(from), y};
    destination = {view.wave.positionFor(to), y};
    view.wave.mouseDown(event(anchor, false));
    view.wave.mouseDrag(event(destination, true));
  }
  void run() {
    auto &v = view;
    switch (stage) {
    case 0:
      v.changed();
      break;
    case 1:
      v.playAssignedPad(0);
      v.position = .8 * sp::rate;
      drag(1.5, 1.8, 2);
      break;
    case 2:
    case 4:
    case 6:
    case 8:
    case 12:
      v.wave.mouseUp(event(destination, true));
      break;
    case 3:
      drag(1.1, 1.3, 4);
      break;
    case 5:
      drag(.1, .5, 1);
      break;
    case 7:
      drag(1.8, 1.2, 2);
      break;
    case 9:
      drag(1.2, 1.1, 2);
      v.stopAudio();
      v.wave.mouseUp(event(destination, true));
      break;
    case 10:
      v.playPad();
      break;
    case 11: {
      drag(v.wave.loopStartTime(), 1., 4);
      v.stopAudio();
      sp::Render other;
      other.values.assign(100, 2048);
      v.playRender(other, 1);
      break;
    }
    }
    if (stage >= 1 && stage <= 8 && !v.active) {
      fail("Playback stopped before background completion");
      return;
    }
    afterPreviewJobs(v, [self = shared_from_this()] {
      auto &v = self->view;
      const auto &pad = v.project.pads[0];
      const int stage = self->stage;
      bool ok = true;
      if (stage >= 1 && stage <= 8)
        ok &= v.active && v.playingPad == 0 && v.playbackLoopFrames > 0 &&
              v.stop.isEnabled();
      if (stage >= 1 && stage <= 4)
        ok &= std::abs(v.position / sp::rate - .8) < 1e-6;
      if (stage >= 5 && stage <= 8)
        ok &= std::abs(v.position / sp::rate - .4) < 1e-6;
      if (stage == 1)
        ok &= v.past.empty() && pad.end == 1.5 &&
              std::abs(v.playing.getNumSamples() / double(sp::rate) - 1.7) <=
                  .005 &&
              std::abs(v.wave.loopStartTime() - 1.1) <= .005;
      if (stage == 2)
        ok &= v.past.size() == 1 && std::abs(pad.end - 1.8) < 1e-6 &&
              std::abs(pad.loopSeconds - .7) <= .005;
      if (stage == 3)
        ok &= v.past.size() == 1 &&
              std::abs(v.playbackLoopFrames / double(sp::rate) - .5) <= .01;
      if (stage == 4)
        ok &= v.past.size() == 2 && std::abs(pad.loopSeconds - .5) <= .005;
      if (stage == 5)
        ok &= v.past.size() == 2 && std::abs(v.playbackSourceStart - .5) < 1e-6;
      if (stage == 6)
        ok &= v.past.size() == 3 && std::abs(pad.start - .5) < 1e-6 &&
              std::abs(v.wave.loopStartTime() - 1.3) <= .005;
      if (stage == 7 || stage == 8)
        ok &= v.playbackLoopFrames == 1;
      if (stage == 8)
        ok &= !v.result && v.wave.isEnabled() && v.canSave(false) &&
              !v.canSave(true) &&
              Component::getNumCurrentlyModalComponents() == 0;
      if (stage == 9)
        ok &= !v.active && !v.stop.isEnabled();
      if (stage == 10)
        ok &= v.active && v.playingPad == 0 && v.playbackLoopFrames == 1;
      if (stage == 11)
        ok &= v.active && v.playingPad == 1 && v.playbackLoopFrames == 0 &&
              v.playing.getNumSamples() == 100;
      if (!ok) {
        self->fail("Unexpected live state");
      } else if (++self->stage == 13) {
        v.stopAudio();
        std::cout << "PASS: live trim/loop drag and drop without stopping, "
                     "anchored End, "
                     "preserved playhead, one Undo per drag, invalid-bank "
                     "preview and stale-job cancellation\n";
        self->finish(0);
      } else
        self->run();
    });
  }
  void fail(const char *message) {
    std::cerr << "FAIL: live loop stage " << stage << ": " << message << "\n";
    finish(1);
  }
};
static void testLiveLoopEdits(MainView &view, std::function<void(int)> finish) {
  auto asset = std::make_shared<sp::Asset>();
  asset->id = "live-loop";
  asset->filename = "LIVE.wav";
  asset->sampleRate = sp::rate;
  asset->audio.setSize(1, 3 * sp::rate);
  for (int i = 0; i < asset->audio.getNumSamples(); ++i)
    asset->audio.setSample(0, i, float(.2 * std::sin(i * .05)));
  view.project = sp::Project{};
  view.project.autoTune = false;
  view.project.pool = {asset};
  auto &pad = view.project.pads[0];
  pad = sp::makePad(*asset, 0, false, false);
  pad.start = .1;
  pad.end = 1.5;
  pad.manualTrim = true;
  pad.loopEnabled = true;
  pad.loopSeconds = .4;
  view.result.reset();
  view.failedTrims.reset();
  view.past.clear();
  view.future.clear();
  view.selected = 0;
  view.prepareToPlay(128, sp::rate);
  view.refresh();
  std::make_shared<LiveLoopEditTest>(view, std::move(finish))->run();
}
static void testLazyProcessingUi(MainView &view,
                                 std::function<void(int)> finish) {
  view.project = sp::Project{};
  view.past.clear();
  view.future.clear();
  view.selected = 0;
  for (int i = 0; i < 32; ++i) {
    auto asset = std::make_shared<sp::Asset>();
    asset->id = "lazy-ui-" + String(i);
    asset->filename = "LAZY.wav";
    asset->sampleRate = sp::rate;
    asset->audio.setSize(1, 1000);
    for (int j = 0; j < 1000; ++j)
      asset->audio.setSample(0, j, float(.1 * std::sin(j * (.05 + i * .001))));
    view.project.pool.push_back(asset);
    view.project.pads[i] = sp::makePad(*asset, i, false, true);
  }
  const auto count = view.renderCache.renderCount();
  view.changed();
  if (view.busy || !view.wave.isEnabled() || !view.canSave(false) ||
      view.canSave(true)) {
    std::cerr << "FAIL: lazy processing blocked editing or kit saving\n";
    finish(1);
    return;
  }
  afterPreviewJobs(view, [&view, count, finish] {
    bool ok = view.result && view.currentPlan && view.result->bank.isEmpty() &&
              view.renderCache.renderCount() == count + 1 && view.canSave(true);
    for (int i = 0; i < 32; ++i)
      ok &= view.currentPlan && view.currentPlan->frames[i] == 1000 &&
            view.result && view.result->sounds[i].values.empty() == (i != 0);
    if (!ok) {
      std::cerr
          << "FAIL: initial edit must plan all pads and render only one\n";
      finish(1);
      return;
    }
    // Another pad is materialized only when selected/auditioned.
    view.select(15);
    afterPreviewJobs(view, [&view, count, finish] {
      if (!view.active || view.playingPad != 15 ||
          view.renderCache.renderCount() != count + 2) {
        std::cerr << "FAIL: selecting an uncached pad did not render it\n";
        finish(1);
        return;
      }
      view.stopAudio();
      // Rapid edits can supersede pending audio; Undo must stay available.
      view.wave.onTrim(.005, .02);
      view.undoEdit();
      afterPreviewJobs(view, [&view, count, finish] {
        if (!view.result || view.project.pads[15].manualTrim || view.busy ||
            view.result->sounds[15].values.size() != 1000 ||
            view.renderCache.renderCount() > count + 3) {
          std::cerr << "FAIL: rapid trim/Undo failed or left stale audio\n";
          finish(1);
          return;
        }
        auto snapshot = view.project;
        view.queue([&view, snapshot, finish] {
          auto complete = sp::renderBank(snapshot, sp::planBank(snapshot),
                                         view.renderCache);
          const bool identical = complete.bank == sp::process(snapshot).bank;
          MessageManager::callAsync([&view, identical, finish] {
            if (!identical || !view.result || !view.result->bank.isEmpty()) {
              std::cerr
                  << "FAIL: deferred export did not match complete bank\n";
              finish(1);
              return;
            }
            std::cout
                << "PASS: lazy UI plans 32 pads, renders active pads only, "
                   "keeps editing/Undo available and completes export\n";
            finish(0);
          });
        });
      });
    });
  });
}
struct AutoTrimPressureTest
    : std::enable_shared_from_this<AutoTrimPressureTest> {
  MainView &view;
  std::function<void(int)> finish;
  int stage = 0;
  AutoTrimPressureTest(MainView &v, std::function<void(int)> done)
      : view(v), finish(std::move(done)) {}
  void run() {
    auto &v = view;
    switch (stage) {
    case 0:
      v.changed();
      break;
    case 1:
      v.reset.setToggleState(true, dontSendNotification);
      v.reset.onClick();
      break;
    case 2:
      v.playPad();
      break;
    case 3:
      v.undoEdit();
      break;
    case 4:
      v.redoEdit();
      break;
    case 5:
      v.checkpoint();
      for (int i = 8; i < 32; ++i)
        v.project.pads[i] = sp::Project{}.pads[i];
      v.changed();
      break;
    }
    afterPreviewJobs(v, [self = shared_from_this()] {
      auto &v = self->view;
      const auto a = v.wave.asset;
      const double duration = a->audio.getNumSamples() / a->sampleRate;
      bool ok = !v.busy && v.wave.isEnabled() && v.canSave(false) &&
                v.wave.startTime() == 0 &&
                std::abs(v.wave.endTime() - duration) < 1e-9 &&
                v.wave.render.removed == 0 && v.wave.render.tune == 16 &&
                Component::getNumCurrentlyModalComponents() == 0;
      if (self->stage < 5)
        ok &= !v.result && !v.exportBank.isEnabled() && !v.canSave(true) &&
              v.status.getText().contains("over capacity");
      if (self->stage == 1 || self->stage == 2 || self->stage == 4)
        ok &= v.project.autoTrim;
      if (self->stage == 3)
        ok &= !v.project.autoTrim && v.past.empty();
      if (self->stage == 2)
        ok &= v.active && v.playing.getNumSamples() > sp::rate;
      if (self->stage == 4)
        ok &= v.past.size() == 1;
      if (self->stage == 5)
        ok &= v.result && v.exportBank.isEnabled() && v.canSave(true) &&
              !v.status.getText().contains("over capacity");
      if (!ok) {
        std::cerr << "FAIL: Auto Trim pressure stage " << self->stage << "\n";
        self->finish(1);
      } else if (++self->stage == 6) {
        std::cout << "PASS: Auto Trim in a full bank keeps complete short "
                     "selections, status-only warnings, playback, Save Kit "
                     "and Undo/Redo; clearing pads restores export\n";
        self->finish(0);
      } else
        self->run();
    });
  }
};
static void testAutoTrimPressureUi(MainView &view,
                                   std::function<void(int)> finish) {
  auto a = std::make_shared<sp::Asset>();
  a->id = "alto-pressure";
  a->filename = "ALTO2.VC.wav";
  a->sampleRate = 14080;
  a->audio.setSize(1, 16384);
  for (int i = 0; i < a->audio.getNumSamples(); ++i)
    a->audio.setSample(0, i, .2f);
  view.project = sp::Project{};
  view.project.pool = {a};
  for (int i = 0; i < 32; ++i)
    view.project.pads[i] = sp::makePad(*a, i, false, true);
  view.selected = 6;
  view.result.reset();
  view.failedTrims.reset();
  view.past.clear();
  view.future.clear();
  view.refresh();
  std::make_shared<AutoTrimPressureTest>(view, std::move(finish))->run();
}
struct SnappedHandlesTest : std::enable_shared_from_this<SnappedHandlesTest> {
  MainView &view;
  std::function<void(int)> finish;
  int stage = 0;
  double originalEnd = 0, originalLoop = 0, editedEnd = 0, editedLoop = 0;
  Point<float> anchor, destination;
  SnappedHandlesTest(MainView &v, std::function<void(int)> done)
      : view(v), finish(std::move(done)) {}
  MouseEvent event(Point<float> point, bool dragged) {
    return MouseEvent(Desktop::getInstance().getMainMouseSource(), point,
                      ModifierKeys(ModifierKeys::leftButtonModifier), 1, 0, 0,
                      0, 0, &view.wave, &view.wave, Time::getCurrentTime(),
                      anchor, Time::getCurrentTime(), 1, dragged);
  }
  void gesture(double from, double to, bool loop) {
    const auto area = view.wave.plotArea();
    const float y = loop ? area.getCentreY() : area.getBottom() - 4;
    anchor = {view.wave.positionFor(from), y};
    destination = {view.wave.positionFor(to), y};
    view.wave.mouseDown(event(anchor, false));
    view.wave.mouseDrag(event(destination, true));
    view.wave.mouseUp(event(destination, true));
  }
  void run() {
    auto &v = view;
    if (stage == 0)
      v.changed();
    else if (stage == 1)
      gesture(originalLoop, originalLoop + .0113, true);
    else if (stage == 2 || stage == 5)
      v.undoEdit();
    else if (stage == 3 || stage == 6)
      v.redoEdit();
    else if (stage == 4)
      gesture(originalEnd, originalEnd + .0173, false);
    afterPreviewJobs(v, [self = shared_from_this()] {
      auto &v = self->view;
      if (!v.result || v.result->sounds[0].values.empty()) {
        std::cerr << "FAIL: snapped handle render missing\n";
        self->finish(1);
        return;
      }
      bool ok = true;
      const auto &r = v.result->sounds[0];
      const double end = v.wave.endTime(), point = v.wave.loopStartTime();
      ok &= end == r.playbackEnd && point == r.loopStart && point >= r.start &&
            point < end &&
            std::abs(end - r.start - r.values.size() / double(sp::rate)) < 1e-9;
      const int loopIndex = (int)r.values.size() - r.loopFrames;
      ok &= loopIndex > 0 && r.values[loopIndex - 1] < 2048 &&
            r.values[loopIndex] >= 2048;
      switch (self->stage) {
      case 0:
        self->originalEnd = end;
        self->originalLoop = point;
        ok &= v.past.empty();
        break;
      case 1:
        self->editedLoop = point;
        ok &= v.past.size() == 1 && end == self->originalEnd &&
              std::abs(point - self->originalLoop - .0113) <= .005;
        break;
      case 2:
        ok &= end == self->originalEnd && point == self->originalLoop &&
              v.past.empty();
        break;
      case 3:
        ok &= end == self->originalEnd && point == self->editedLoop &&
              v.past.size() == 1;
        break;
      case 4:
        self->editedEnd = end;
        self->editedLoop = point;
        ok &= v.past.size() == 2 &&
              std::abs(end - self->originalEnd - .0173) <= .005;
        break;
      case 5:
        ok &= end == self->originalEnd && v.past.size() == 1;
        break;
      case 6:
        ok &= end == self->editedEnd && point == self->editedLoop &&
              v.past.size() == 2;
        break;
      }
      if (!ok) {
        std::cerr << "FAIL: snapped handles stage " << self->stage << "\n";
        self->finish(1);
      } else if (++self->stage == 7) {
        std::cout
            << "PASS: real handle drags display processed zero crossings, "
               "one-step Undo and deterministic Redo\n";
        self->finish(0);
      } else
        self->run();
    });
  }
};
static void testSnappedHandles(MainView &view,
                               std::function<void(int)> finish) {
  auto asset = std::make_shared<sp::Asset>();
  asset->id = "snap-ui";
  asset->filename = "SNAP.wav";
  asset->sampleRate = sp::rate;
  asset->audio.setSize(1, sp::rate);
  for (int i = 0; i < asset->audio.getNumSamples(); ++i)
    asset->audio.setSample(0, i, float(.2 * std::sin(i * .07)));
  view.project = sp::Project{};
  view.project.autoTune = false;
  view.project.pool = {asset};
  auto &pad = view.project.pads[0];
  pad = sp::makePad(*asset, 0, false, false);
  pad.manualTrim = true;
  pad.start = .011;
  pad.end = .1513;
  pad.loopSeconds = .0487;
  pad.loopEnabled = true;
  view.selected = 0;
  view.result.reset();
  view.failedTrims.reset();
  view.past.clear();
  view.future.clear();
  view.refresh();
  std::make_shared<SnappedHandlesTest>(view, std::move(finish))->run();
}
struct AutoMapUiTest : std::enable_shared_from_this<AutoMapUiTest> {
  MainView &view;
  std::function<void(int)> finish;
  int stage = 0;
  AutoMapUiTest(MainView &v, std::function<void(int)> done)
      : view(v), finish(std::move(done)) {}
  void toggle(bool enabled) {
    view.sort.setToggleState(enabled, dontSendNotification);
    view.sort.onClick();
  }
  void snapshot(const String &name) {
    const auto destination =
        SystemStats::getEnvironmentVariable("SP_UI_SNAPSHOT_DIR", {});
    if (destination.isEmpty())
      return;
    File directory(destination);
    directory.createDirectory();
    FileOutputStream stream(directory.getChildFile(name));
    stream.setPosition(0);
    stream.truncate();
    PNGImageFormat().writeImageToStream(
        view.createComponentSnapshot(view.getLocalBounds()), stream);
  }
  void run() {
    switch (stage) {
    case 0:
      view.changed();
      break;
    case 1:
      toggle(true);
      break;
    case 2:
      view.edit([](auto &pad) {
        pad.name = "KEPT";
        pad.channel = 5;
        pad.reverse = true;
        pad.manualTrim = true;
        pad.start = .005;
        pad.end = .025;
        pad.loopEnabled = true;
        pad.loopSeconds = .01;
      });
      break;
    case 3:
      toggle(false);
      break;
    case 4:
      view.undoEdit();
      break;
    case 5:
      view.redoEdit();
      break;
    case 6:
      toggle(true);
      break;
    case 7:
      view.selected = 1;
      view.clearSelectedPad();
      break;
    case 8:
      toggle(false);
      break;
    case 9:
      view.undoEdit();
      break;
    case 10:
      view.clearAll.onClick();
      break;
    case 11:
      toggle(false);
      break;
    case 12:
      view.undoEdit();
      break;
    case 13:
      view.undoEdit();
      break;
    case 14:
      toggle(false);
      break;
    case 15:
      toggle(true);
      break;
    case 16:
      view.selected = 2;
      view.edit([](auto &pad) { pad.name = "CLKEEP"; });
      break;
    case 17:
      toggle(false);
      break;
    }
    afterPreviewJobs(view, [self = shared_from_this()] { self->check(); });
  }
  void check() {
    auto &v = view;
    bool ok = !v.busy;
    if (stage == 1) {
      ok &= v.project.autoSort && v.project.autoMapRestore &&
            v.project.pads[0].asset == "map-ui-kick" &&
            v.project.pads[1].asset == "map-ui-snare" &&
            sp::padInstrument(v.project, 3) == "Rim" &&
            sp::padInstrument(v.project, 16) == "BD" &&
            sp::padInstrument(v.project, 17) == "SD" &&
            v.project.pads[16].asset == "map-ui-kick2" &&
            v.project.pads[17].asset == "map-ui-snare2" &&
            sp::padInstrument(v.project, 23) == "Crash" &&
            sp::padInstrument(v.project, 24) == "Other" &&
            sp::padInstrument(v.project, 31) == "Other" &&
            v.project.pads[3].asset.isEmpty();
      snapshot("auto-map-on.png");
    }
    if (stage == 3 || stage == 5 || stage == 14) {
      const auto &pad = v.project.pads[7];
      ok &= !v.project.autoSort && !v.project.autoMapRestore &&
            pad.name == "KEPT" && pad.channel == 5 && pad.reverse &&
            pad.manualTrim && pad.start == .005 && pad.end == .025 &&
            pad.loopEnabled && pad.loopSeconds == .01 &&
            sp::padInstrument(v.project, 7).isEmpty();
      if (stage == 3)
        snapshot("auto-map-off.png");
    }
    if (stage == 4)
      ok &= v.project.autoSort && v.project.autoMapRestore &&
            v.project.pads[0].name == "KEPT";
    if (stage == 8 || stage == 14)
      ok &= v.project.pads[0].asset.isEmpty();
    if (stage == 10 || stage == 11 || stage == 12)
      ok &= v.project.pool.empty() &&
            std::all_of(v.project.pads.begin(), v.project.pads.end(),
                        [](const auto &pad) { return pad.asset.isEmpty(); });
    if (stage == 13)
      ok &= v.project.autoSort && v.project.autoMapRestore &&
            v.project.pool.size() == 5 && v.project.pads[0].name == "KEPT" &&
            v.project.pads[1].asset.isEmpty();
    if (stage == 17)
      ok &= !v.project.autoSort && v.project.pads[0].name == "CLKEEP" &&
            v.project.pads[0].asset == "map-ui-clap" &&
            v.project.pads[7].name == "KEPT";
    if (!ok) {
      std::cerr << "FAIL: reversible Auto Map UI at stage " << stage << '\n';
      finish(1);
    } else if (++stage < 18)
      run();
    else {
      std::cout << "PASS: Auto Map UI toggles, instrument labels, sound edits, "
                   "Undo/Redo, Clear Pad and Clear All\n";
      finish(0);
    }
  }
};
static void testAutoMapUi(MainView &view, std::function<void(int)> finish) {
  view.project = sp::Project{};
  view.past.clear();
  view.future.clear();
  view.result.reset();
  view.failedTrims.reset();
  view.selected = 0;
  for (const auto *kind : {"snare", "kick", "clap", "kick2", "snare2"}) {
    auto asset = std::make_shared<sp::Asset>();
    asset->id = "map-ui-" + String(kind);
    asset->filename = String(kind).replace("2", "_2") + ".wav";
    asset->relative = asset->filename;
    asset->category = sp::classify(String(kind).replace("2", ""));
    asset->sampleRate = sp::rate;
    asset->audio.setSize(1, 800);
    for (int i = 0; i < 800; ++i)
      asset->audio.setSample(0, i, float(.1 * std::sin(i * .04)));
    view.project.pool.push_back(asset);
    if (String(kind) == "snare" || String(kind) == "kick")
      sp::assignPad(view.project, String(kind) == "snare" ? 0 : 7, *asset);
  }
  std::make_shared<AutoMapUiTest>(view, std::move(finish))->run();
}
static void testMemoryPlaybackSuite(MainView &view,
                                    std::function<void(int)> finish) {
  testAutoTrimPressureUi(view, [&view, finish](int trimCode) {
    if (trimCode) {
      finish(trimCode);
      return;
    }
    testSnappedHandles(view, [&view, finish](int snapCode) {
      if (snapCode) {
        finish(snapCode);
        return;
      }
      testLazyProcessingUi(view, [&view, finish](int lazyCode) {
        if (lazyCode) {
          finish(lazyCode);
          return;
        }
        testLiveLoopEdits(view, [&view, finish](int liveCode) {
          if (liveCode) {
            finish(liveCode);
            return;
          }
          testLoopUi(view, [&view, finish](int loopCode) {
            if (loopCode) {
              finish(loopCode);
              return;
            }
            testProtectedTrim(view, [&view, finish](int code) {
              if (code) {
                finish(code);
                return;
              }
              view.project = sp::Project{};
              view.result.reset();
              view.failedTrims.reset();
              view.selected = 0;
              view.past.clear();
              view.future.clear();
              testExistingPlayback(view, finish);
            });
          });
        });
      });
    });
  });
}
static void testMemoryPlayback(MainView &view,
                               std::function<void(int)> finish) {
  auto asset = std::make_shared<sp::Asset>();
  asset->id = "settings-pool";
  asset->filename = "QUIET.wav";
  asset->sampleRate = sp::rate;
  asset->audio.setSize(1, sp::rate / 10);
  for (int i = 0; i < asset->audio.getNumSamples(); ++i)
    asset->audio.setSample(0, i, .02f);
  view.project = sp::Project{};
  view.project.pool = {asset};
  view.project.normalizeOnExport = false;
  view.project.fadeOutOnExport = true;
  view.result.reset();
  view.selected = 0;
  view.playPoolSample(asset);
  afterPreviewJobs(view, [&view, asset, finish] {
    if (!view.active || view.playingPad != -1 ||
        std::abs(view.playing.getSample(0, 0) - .02f) > 1.f / 2048 ||
        view.playing.getSample(0, view.playing.getNumSamples() - 1) != 0) {
      std::cerr
          << "FAIL: unassigned pool preview ignored Fade On/Normalize Off\n";
      finish(1);
      return;
    }
    view.setExportSettings(false, true);
    view.playPoolSample(asset);
    afterPreviewJobs(view, [&view, finish] {
      const float target = float(std::pow(10., -.5 / 20.));
      const float peak =
          view.playing.getMagnitude(0, 0, view.playing.getNumSamples());
      if (!view.active || std::abs(peak - target) > 1.f / 2048 ||
          view.playing.getSample(0, view.playing.getNumSamples() - 1) <= 0) {
        std::cerr << "FAIL: unassigned pool preview ignored changed Settings\n";
        finish(1);
        return;
      }
      view.stopAudio();
      std::cout << "PASS: unassigned pool preview follows export Settings\n";
      testAutoMapUi(view, [&view, finish](int code) {
        if (code)
          finish(code);
        else
          testMemoryPlaybackSuite(view, finish);
      });
    });
  });
}
