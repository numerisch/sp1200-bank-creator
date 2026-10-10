// SPDX-License-Identifier: AGPL-3.0-only
// Copyright © 2026 Numerisch GmbH

#include "AppPreferences.h"
#include "AppLegal.h"
#include "Core.h"
#include "HelpContent.h"
#include "Theme.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <numeric>
#include <thread>
using namespace juce;
class MainView;
class PadButton : public Component, public DragAndDropTarget {
public:
  MainView &owner;
  int index;
  bool hover = false;
  PadButton(MainView &o, int i) : owner(o), index(i) {
    setMouseCursor(MouseCursor::PointingHandCursor);
  }
  void paint(Graphics &) override;
  std::unique_ptr<AccessibilityHandler> createAccessibilityHandler() override;
  void mouseDown(const MouseEvent &) override;
  void mouseDrag(const MouseEvent &) override;
  bool isInterestedInDragSource(const SourceDetails &) override { return true; }
  void itemDragEnter(const SourceDetails &) override {
    hover = true;
    repaint();
  }
  void itemDragExit(const SourceDetails &) override {
    hover = false;
    repaint();
  }
  void itemDropped(const SourceDetails &) override;
};
class Wave : public Component, public SettableTooltipClient {
public:
  std::shared_ptr<sp::Asset> asset;
  sp::Render render;
  bool reversed = false;
  bool autoTune = true;
  bool manualTrim = false;
  bool loopEnabled = false;
  double loopSeconds = -1;
  std::function<void(double)> onLoop;
  std::function<void(double, double, double, int)> onAuditionChange;
  std::function<void(double, double)> onTrim;

  Rectangle<float> plotArea() const {
    return getLocalBounds().toFloat().reduced(8).withTrimmedTop(20);
  }
  double duration() const {
    return asset->audio.getNumSamples() / asset->sampleRate;
  }
  double startTime() const { return dragging != 0 ? draftStart : render.start; }
  double endTime() const {
    return dragging != 0             ? draftEnd
           : render.playbackEnd >= 0 ? render.playbackEnd
                                     : render.end - render.removed;
  }
  double loopStartTime() const {
    if (!dragging && render.loopStart >= 0 && loopEnabled)
      return render.loopStart;
    const double length = dragging ? draftLoop : loopSeconds;
    return length < 0 ? startTime() : std::max(startTime(), endTime() - length);
  }
  float positionFor(double seconds) const {
    auto area = plotArea();
    return area.getX() + float(seconds / duration()) * area.getWidth();
  }
  int dragTargetAt(Point<float> point) const {
    if (!asset || !isEnabled() || !plotArea().contains(point))
      return 0;
    if (loopEnabled && std::abs(point.x - positionFor(loopStartTime())) <= 7 &&
        std::abs(point.y - plotArea().getCentreY()) <= 10)
      return 4;
    const float leftX = positionFor(startTime()),
                rightX = positionFor(endTime());
    const float left = std::abs(point.x - leftX),
                right = std::abs(point.x - rightX);
    const float tolerance =
        std::min(10.f, std::max(2.f, (rightX - leftX) * .25f));
    if (left <= tolerance || right <= tolerance) {
      if (left <= tolerance && right <= tolerance)
        return point.y < plotArea().getCentreY() ? 1 : 2;
      return left < right ? 1 : 2;
    }
    return point.x > leftX && point.x < rightX ? 3 : 0;
  }
  void mouseMove(const MouseEvent &event) override {
    const int target = dragTargetAt(event.position);
    setMouseCursor(target == 3 ? MouseCursor::DraggingHandCursor
                   : target    ? MouseCursor::LeftRightResizeCursor
                               : MouseCursor::NormalCursor);
  }
  void mouseExit(const MouseEvent &) override {
    if (!dragging)
      setMouseCursor(MouseCursor::NormalCursor);
  }
  void mouseDown(const MouseEvent &event) override {
    if (!event.mods.isLeftButtonDown())
      return;
    const int handle = dragTargetAt(event.position);
    if (!handle)
      return;
    draftStart = render.start;
    draftEnd = endTime();
    initialStart = draftStart;
    initialEnd = draftEnd;
    initialLoop = draftLoop = loopSeconds < 0 ? -1 : draftEnd - loopStartTime();
    dragAnchorX = event.position.x;
    dragging = handle;
    setMouseCursor(handle == 3 ? MouseCursor::DraggingHandCursor
                               : MouseCursor::LeftRightResizeCursor);
  }
  void mouseDrag(const MouseEvent &event) override {
    if (!dragging || !asset || !isEnabled())
      return;
    const double previousStart = draftStart, previousEnd = draftEnd,
                 previousLoop = draftLoop;
    const auto area = plotArea();
    const double time =
        std::clamp(double((event.position.x - area.getX()) / area.getWidth()),
                   0.0, 1.0) *
        duration();
    if (dragging == 4) {
      // Permit tiny loops for auditioning; export validates its three-frame
      // limit.
      draftLoop =
          draftEnd -
          std::clamp(time, draftStart,
                     std::max(draftStart, draftEnd - 1. / asset->sampleRate));
    } else if (dragging == 3) {
      const double offset =
          (event.position.x - dragAnchorX) / area.getWidth() * duration();
      const auto moved =
          sp::shiftTrimSelection(*asset, initialStart, initialEnd, offset);
      draftStart = moved.getStart();
      draftEnd = moved.getEnd();
    } else if (dragging == 1)
      draftStart =
          sp::clampTrimPosition(*asset, true, time, draftEnd, autoTune);
    else
      draftEnd =
          sp::clampTrimPosition(*asset, false, time, draftStart, autoTune);
    if (dragging != 4)
      draftLoop =
          sp::loopSecondsAfterTrim(initialStart, initialEnd, initialLoop,
                                   draftStart, draftEnd, asset->sampleRate);
    if (onAuditionChange &&
        (std::abs(draftStart - previousStart) > .5 / asset->sampleRate ||
         std::abs(draftEnd - previousEnd) > .5 / asset->sampleRate ||
         std::abs(draftLoop - previousLoop) > .5 / asset->sampleRate))
      onAuditionChange(draftStart, draftEnd, draftLoop, dragging);
    repaint();
  }
  void mouseUp(const MouseEvent &event) override {
    if (!dragging)
      return;
    const int target = dragging;
    dragging = 0;
    repaint();
    if (target == 4) {
      const double oldLength =
          initialLoop < 0 ? initialEnd - initialStart : initialLoop;
      if (asset && isEnabled() && event.mouseWasDraggedSinceMouseDown() &&
          std::abs(draftLoop - oldLength) > .5 / asset->sampleRate && onLoop) {
        loopSeconds = draftLoop;
        render.loopStart = -1;
        onLoop(draftLoop);
      }
      return;
    }
    // One history entry and one render per gesture, never during the drag.
    if (asset && isEnabled() && event.mouseWasDraggedSinceMouseDown() &&
        (std::abs(draftStart - initialStart) > 0.5 / asset->sampleRate ||
         std::abs(draftEnd - initialEnd) > 0.5 / asset->sampleRate) &&
        onTrim) {
      // Keep the dropped selection visible until the background render arrives.
      // The callback needs the previous boundaries to anchor a custom loop.
      onTrim(draftStart, draftEnd);
      render.start = draftStart;
      render.end = draftEnd;
      render.removed = 0;
      render.playbackEnd = render.loopStart = -1;
      loopSeconds = draftLoop;
    }
  }
  void setRenderMetadata(const sp::Render &source) {
    render.gainDb = source.gainDb;
    render.start = source.start;
    render.end = source.end;
    render.removed = source.removed;
    render.tune = source.tune;
    render.left = source.left;
    render.playbackEnd = source.playbackEnd;
    render.loopStart = source.loopStart;
  }
  // Assets are immutable: only source, direction or display width invalidate
  // peaks.
  bool updateEnvelope(int width) {
    if (cachedAsset.lock() == asset && cachedWidth == width &&
        cachedReverse == reversed)
      return false;
    cachedAsset = asset;
    cachedWidth = width;
    cachedReverse = reversed;
    envelope.clear();
    if (!asset || width <= 0)
      return true;
    envelope.reserve(width);
    const int n = asset->audio.getNumSamples();
    const auto *audio = asset->audio.getReadPointer(0);
    for (int px = 0; px < width; ++px) {
      const int begin = int(int64_t(px) * n / width);
      const int end =
          std::min(n, std::max(begin + 1, int(int64_t(px + 1) * n / width)));
      float low = 0, high = 0;
      for (int i = begin; i < end; ++i) {
        const float value = audio[reversed ? n - 1 - i : i];
        low = std::min(low, value);
        high = std::max(high, value);
      }
      envelope.emplace_back(low, high);
    }
    return true;
  }
  void paint(Graphics &g) override {
    g.fillAll(spStyle::well);
    g.setColour(spStyle::silver.withAlpha(.35f));
    g.drawRect(getLocalBounds(), 1);
    if (!asset)
      return;
    auto area = plotArea();
    updateEnvelope((int)area.getWidth());
    const float left = positionFor(startTime()), right = positionFor(endTime());
    g.setColour(Colour(0xff44362b));
    g.fillRect(Rectangle<float>(left, area.getY(), std::max(0.f, right - left),
                                area.getHeight()));
    g.setColour(spStyle::orange);
    for (int px = 0; px < (int)envelope.size(); ++px) {
      const auto [lo, hi] = envelope[px];
      g.drawVerticalLine((int)area.getX() + px,
                         area.getCentreY() - hi * area.getHeight() * .45f,
                         area.getCentreY() - lo * area.getHeight() * .45f);
    }
    const bool manual =
        manualTrim ||
        (dragging && dragging != 4 &&
         (std::abs(draftStart - initialStart) > .5 / asset->sampleRate ||
          std::abs(draftEnd - initialEnd) > .5 / asset->sampleRate));
    const auto handleColour = manual ? spStyle::green : Colour(0xffffb35c);
    g.setColour(isEnabled() ? handleColour : handleColour.darker(.45f));
    g.drawVerticalLine((int)left, area.getY(), area.getBottom());
    g.drawVerticalLine((int)right, area.getY(), area.getBottom());
    g.fillRoundedRectangle(left - 4, area.getY(), 8, 14, 2);
    g.fillRoundedRectangle(right - 4, area.getBottom() - 14, 8, 14, 2);
    if (loopEnabled) {
      const float x = positionFor(loopStartTime());
      const auto blue = Colour(0xff5eaaff);
      g.setColour(isEnabled() ? blue : blue.darker(.45f));
      g.drawVerticalLine((int)x, area.getY(), area.getBottom());
      g.fillRoundedRectangle(x - 6, area.getCentreY() - 9, 12, 18, 3);
    }
    g.setFont(spStyle::display(11));
    g.drawText("Start " + String(startTime(), 3) + " s", 8, 2,
               getWidth() / 2 - 8, 20, Justification::centredLeft);
    g.drawText("End " + String(endTime(), 3) + " s", getWidth() / 2, 2,
               getWidth() / 2 - 8, 20, Justification::centredRight);
  }

private:
  std::weak_ptr<sp::Asset> cachedAsset;
  int cachedWidth = -1;
  bool cachedReverse = false;
  std::vector<std::pair<float, float>> envelope;
  int dragging = 0;
  double draftStart = 0, draftEnd = 0, initialStart = 0, initialEnd = 0;
  double draftLoop = -1, initialLoop = -1;
  float dragAnchorX = 0;
};
class MainView : public AudioAppComponent,
                 public FileDragAndDropTarget,
                 public DragAndDropContainer,
                 public ListBoxModel,
                 private Timer {
public:
  sp::Project project;
  std::unique_ptr<sp::AppPreferences> preferences;
  std::shared_ptr<sp::Result> result;
  std::shared_ptr<sp::ProcessingPlan> currentPlan;
  sp::RenderCache renderCache; // Accessed only by the audio worker.
  std::shared_ptr<std::array<sp::Render, 32>> failedTrims;
  // Presentation cache survives invalidation of the audio/export result.
  std::array<String, 32> padDisplayAssets, padDisplayText;
  int selected = 0;
  int waveformPad = -1;
  bool busy = false, dirty = false;
  std::array<bool, 32> memoryProblems{};
  std::atomic<uint64_t> generation{0};
  std::atomic<uint64_t> previewGeneration{0};
  std::array<std::unique_ptr<PadButton>, 32> pads;
  ListBox pool{"Samples", this};
  Wave wave;
  TextButton import{"Import Sample Folder"}, exportBank{"Export SP-1200 Bank"},
      stop{"Stop Sound"}, remove{"Clear Pad"}, reverse{"Reverse"},
      clearAll{"Clear All"}, resetTrimButton{"Reset Trim"}, loop{"Loop Off"};
  ToggleButton tune{"Pitch Fit"}, sort{"Auto Map"}, reset{"Auto Trim"};
  ComboBox channel;
  TextEditor name;
  Slider threshold;
  std::unique_ptr<TooltipWindow> tooltips;
  Label status, details, nameLabel, channelLabel;
  std::unique_ptr<FileChooser> chooser;
  std::function<void()> onStateChanged;
  std::vector<sp::Project> past, future;
  CriticalSection audioLock;
  AudioBuffer<float> playing;
  double position = 0, step = 1, deviceRate = 44100;
  bool active = false, updating = false;
  int playbackLoopFrames = 0, playingPad = -1;
  double playbackSourceStart = 0, playbackFramesPerSecond = sp::rate;
  std::thread worker;
  std::mutex jobsMutex;
  std::condition_variable jobsReady;
  enum class JobKind { Normal, Processing, Audition };
  struct Job {
    std::function<void()> run;
    JobKind kind;
  };
  std::deque<Job> jobs;
  bool shuttingDown = false;
  explicit MainView(bool silent = false) {
    // Smoke tests and visual fixtures must never touch personal preferences.
    if (!silent) {
      preferences = std::make_unique<sp::AppPreferences>();
      preferences->restore(project);
    }
    if (!preferences || preferences->tooltipsEnabled())
      tooltips = std::make_unique<TooltipWindow>(this, 600);
    setOpaque(true);
    setWantsKeyboardFocus(true);
    for (Component *c : std::initializer_list<Component *>{&pool,
                                                           &wave,
                                                           &import,
                                                           &exportBank,
                                                           &clearAll,
                                                           &tune,
                                                           &sort,
                                                           &stop,
                                                           &reset,
                                                           &remove,
                                                           &resetTrimButton,
                                                           &reverse,
                                                           &loop,
                                                           &channel,
                                                           &name,
                                                           &threshold,
                                                           &status,
                                                           &details,
                                                           &nameLabel,
                                                           &channelLabel})
      addAndMakeVisible(c);
    for (int i = 0; i < 32; ++i) {
      pads[i] = std::make_unique<PadButton>(*this, i);
      addAndMakeVisible(*pads[i]);
    }
    pool.setRowHeight(46);
    // JUCE suppresses mouse-up selection/click callbacks after a drag.
    pool.setRowSelectedOnMouseDown(false);
    pool.setColour(ListBox::backgroundColourId, spStyle::well);
    pool.setColour(ListBox::outlineColourId, spStyle::muted.withAlpha(.4f));
    pool.setOutlineThickness(1);
    name.setFont(spStyle::display(14));
    details.setFont(spStyle::display(12));
    details.setColour(Label::textColourId, spStyle::ivory);
    status.setFont(spStyle::display(12));
    status.setColour(Label::backgroundColourId, spStyle::orange);
    status.setColour(Label::textColourId, spStyle::ink);
    nameLabel.setText("Sound Name", dontSendNotification);
    channelLabel.setText("Channel Output", dontSendNotification);
    for (auto *label : {&nameLabel, &channelLabel}) {
      label->setFont(spStyle::heading(15));
      label->setColour(Label::textColourId, spStyle::ivory);
      label->setJustificationType(Justification::centredLeft);
    }
    channel.setTitle("Hardware Output Channel");
    name.setTitle("Hardware Sample Name");
    threshold.setTitle("Global Auto-Trim Threshold");
    threshold.setTooltip(
        "Trim automatic pads only: -60 to -5 dBFS. Manual cuts stay protected. "
        "Double-click for -40 dBFS.");
    for (int i = 1; i <= 8; ++i)
      channel.addItem("Output " + String(i), i);
    channel.onChange = [this] {
      if (!updating)
        edit([&](auto &p) { p.channel = channel.getSelectedId(); });
    };
    name.setInputRestrictions(6, " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqr"
                                 "stuvwxyz0123456789!#$%&'()+,-._");
    name.onReturnKey = [this] { commitName(); };
    name.onFocusLost = [this] { commitName(); };
    threshold.setRange(-60, -5, 1);
    threshold.setDoubleClickReturnValue(true, -40);
    threshold.setTextValueSuffix(" dBFS");
    threshold.setSliderStyle(Slider::LinearHorizontal);
    threshold.setTextBoxStyle(Slider::TextBoxRight, false, 85, 24);
    threshold.onDragEnd = [this] { commitThreshold(); };
    threshold.onValueChange = [this] {
      if (!updating && !threshold.isMouseButtonDown())
        commitThreshold();
    };
    wave.onTrim = [this](double start, double end) {
      if (busy)
        return;
      edit(
          [&](auto &pad) {
            pad.loopSeconds = sp::loopSecondsAfterTrim(
                wave.render.start, wave.endTime(),
                pad.loopSeconds < 0 ? -1
                                    : wave.endTime() - wave.loopStartTime(),
                start, end, wave.asset->sampleRate);
            pad.start = start;
            pad.end = end;
            pad.manualTrim = true;
          },
          true);
    };
    wave.onLoop = [this](double seconds) {
      if (!busy)
        edit(
            [&](auto &pad) {
              // Map the displayed frame boundary back to the requested
              // selection.
              pad.loopSeconds =
                  seconds + std::max(0., wave.render.end - wave.render.removed -
                                             wave.endTime());
            },
            true);
    };
    wave.onAuditionChange = [this](double start, double end, double seconds,
                                   int target) {
      if (!isLoopPlaying(selected))
        return;
      auto preview = project;
      auto &pad = preview.pads[selected];
      if (target == 4 && currentPlan) {
        pad.loopSeconds =
            seconds + std::max(0., wave.render.end - wave.render.removed -
                                       wave.endTime());
        queueLivePreview(std::move(preview), selected, currentPlan);
      } else {
        pad.start = start;
        pad.end = end;
        pad.manualTrim = true; // Includes the displayed bank cuts.
        pad.loopSeconds = seconds;
        queueLivePreview(std::move(preview), selected);
      }
    };
    loop.setClickingTogglesState(true);
    loop.setTooltip("On: play the beginning once, then repeat from the blue "
                    "handle to the selection end until Stop Sound. "
                    "Off: retain the loop point without repeating. "
                    "Drag the blue handle to set a tail length. Loop and end "
                    "handles snap to nearby zero crossings after processing.");
    loop.onClick = [this] {
      edit([](auto &pad) { pad.loopEnabled = !pad.loopEnabled; });
    };
    wave.setTooltip(
        "Drag the handles to trim, or drag inside the selection "
        "to move it without changing its length. Manual edits are protected "
        "from Auto Trim and shown in green. Automatic boundaries are yellow. "
        "Use Edit > Reset Trim to restore automatic control. With Loop On, "
        "drag the blue middle handle to set the loop start. Loop and end "
        "handles snap to nearby zero crossings after processing.");
    import.onClick = [this] { chooseImport(); };
    exportBank.onClick = [this] { chooseSave(true); };
    clearAll.setTooltip(
        "Clear the sample pool and all pads. Undo restores the kit.");
    clearAll.onClick = [this] {
      if (busy || project.pool.empty())
        return;
      checkpoint();
      sp::clearKit(project);
      selected = 0;
      pool.deselectAllRows();
      changed();
    };
    tune.onClick = [this] {
      if (busy)
        return;
      checkpoint();
      sp::setAutoTune(project, tune.getToggleState());
      changed();
    };
    tune.setTooltip(
        "On: fit long samples using pitch compensation; select up to 6.27 s.\n"
        "Off: keep neutral pitch; select up to 2.5 s.\n"
        "Switching resets automatic selections; manual cuts stay protected.");
    sort.onClick = [this] {
      if (busy)
        return;
      checkpoint();
      sp::setAutoMap(project, sort.getToggleState());
      changed();
    };
    sort.setTooltip(
        "On: map by instrument and show the target instruments on pads.\n"
        "Off: restore the previous layout and hide instrument labels.\n"
        "Sound edits stay with their assignments. New assignments fill free "
        "pads; cleared or replaced assignments stay removed.");
    stop.onClick = [this] { stopAudio(); };
    reverse.setTooltip("Reverse this pad for export and export preview. "
                       "Original audio stays unchanged.");
    reverse.onClick = [this] {
      auto asset = sp::assetFor(project, project.pads[selected].asset);
      if (!asset || busy)
        return;
      const double duration = asset->audio.getNumSamples() / asset->sampleRate;
      edit([&](auto &pad) {
        const double oldStart = pad.start, oldEnd = pad.end;
        pad.start = oldEnd >= 0 ? duration - oldEnd : -1;
        pad.end = oldStart >= 0 ? duration - oldStart : -1;
        pad.reverse = !pad.reverse;
      });
    };
    reset.onClick = [this] {
      if (busy)
        return;
      checkpoint();
      sp::setAutoTrim(project, reset.getToggleState());
      changed();
    };
    reset.setTooltip(
        "On: remove leading/trailing silence using the global threshold.\n"
        "Off: restore automatic pads to their default selection.\n"
        "Manual cuts remain protected in both modes. Edit > Reset "
        "Trim releases them. Bank memory pressure never cuts audible tails; "
        "only individual sample limits can shorten automatic pads.");
    resetTrimButton.onClick = [this] { resetSelectedTrim(); };
    resetTrimButton.setTooltip(
        "Release this pad's manual trim protection. Auto Trim On "
        "re-trims it; Off restores the default selection. Reset the loop start "
        "to the selection start; keep Loop On/Off unchanged.");
    remove.onClick = [this] { clearSelectedPad(); };
    remove.setTooltip("Clear selected pad (Delete / Backspace). The sample "
                      "stays in the pool.");
    worker = std::thread([this] {
      for (;;) {
        std::function<void()> job;
        {
          std::unique_lock lock(jobsMutex);
          jobsReady.wait(lock,
                         [this] { return shuttingDown || !jobs.empty(); });
          if (shuttingDown)
            return;
          job = std::move(jobs.front().run);
          jobs.pop_front();
        }
        job();
      }
    });
    setSize(1200, 820);
    if (!silent)
      setAudioChannels(0, 2);
    refresh();
    startTimerHz(10);
  }
  ~MainView() override {
    stopTimer();
    ++generation;
    ++previewGeneration;
    {
      std::lock_guard lock(jobsMutex);
      shuttingDown = true;
      jobs.clear();
    }
    jobsReady.notify_one();
    if (worker.joinable())
      worker.join();
    shutdownAudio();
  }
  void queue(std::function<void()> fn, JobKind kind = JobKind::Normal) {
    std::lock_guard lock(jobsMutex);
    if (kind != JobKind::Normal)
      std::erase_if(jobs, [kind](const Job &job) { return job.kind == kind; });
    if (kind == JobKind::Audition)
      jobs.push_front({std::move(fn), kind});
    else
      jobs.push_back({std::move(fn), kind});
    jobsReady.notify_one();
  }
  void error(String text, bool statusOnly = false) {
    // The caller knows the error type; message wording must not control UI.
    if (statusOnly) {
      status.setText(text, dontSendNotification);
      return;
    }
    AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon,
                                     "SP-1200 Bank Creator", text);
  }
  void toggleTooltips() {
    const bool enabled = !tooltips;
    if (enabled)
      tooltips = std::make_unique<TooltipWindow>(this, 600);
    else
      tooltips.reset();
    if (preferences && !preferences->setTooltipsEnabled(enabled))
      error("Could not save tooltip settings. Check available disk space and "
            "permissions.");
    if (onStateChanged)
      onStateChanged();
  }
  void checkpoint() {
    dirty = true;
    past.push_back(project);
    if (past.size() > 50)
      past.erase(past.begin());
    future.clear();
  }
  void clearSelectedPad() {
    if (busy || project.pads[selected].asset.isEmpty())
      return;
    checkpoint();
    sp::clearPad(project, selected);
    changed();
  }
  bool canResetTrim() const {
    const auto &pad = project.pads[selected];
    return !busy && pad.asset.isNotEmpty() &&
           (pad.manualTrim || pad.loopSeconds >= 0);
  }
  void resetSelectedTrim() {
    if (!canResetTrim())
      return;
    checkpoint();
    sp::resetTrim(project, selected);
    sp::retainMappedAssignment(project, selected);
    changed({}, true);
  }
  bool canClearPadFromKeyboard() const {
    if (busy || project.pads[selected].asset.isEmpty())
      return false;
    for (auto *focus = Component::getCurrentlyFocusedComponent(); focus;
         focus = focus->getParentComponent())
      if (dynamic_cast<TextEditor *>(focus) != nullptr)
        return false;
    return true;
  }
  void commitName() {
    if (updating || project.pads[selected].asset.isEmpty())
      return;
    auto text = name.getText().trim();
    if (text.isEmpty())
      text = "SOUND";
    if (text != project.pads[selected].name)
      edit([&](auto &p) { p.name = text; });
  }
  void setExportSettings(bool fadeOut, bool normalize) {
    if (busy || (project.fadeOutOnExport == fadeOut &&
                 project.normalizeOnExport == normalize))
      return;
    checkpoint();
    project.fadeOutOnExport = fadeOut;
    project.normalizeOnExport = normalize;
    changed({}, true);
  }
  void commitThreshold() {
    if (updating || busy || threshold.getValue() == project.threshold)
      return;
    checkpoint();
    project.threshold = threshold.getValue();
    changed();
  }

  template <class Fn> void edit(Fn fn, bool keepLoopPlaying = false) {
    if (project.pads[selected].asset.isEmpty())
      return;
    checkpoint();
    fn(project.pads[selected]);
    sp::retainMappedAssignment(project, selected);
    changed({}, keepLoopPlaying);
  }
  void changed(String completionNote = {}, bool keepLoopPlaying = false) {
    sp::syncAutoMapRestore(project);
    if (preferences && !preferences->save(project))
      error("Could not save app settings. Check available disk space and "
            "permissions.");
    dirty = true;
    const bool continuing = keepLoopPlaying && isLoopPlaying(selected);
    const int auditionPad = selected;
    if (continuing)
      ++previewGeneration;
    else
      stopAudio();
    const auto auditionRequest = previewGeneration.load();
    auto version = ++generation;
    // Regular edits remain usable while planning/rendering. Import/export still
    // use busy; an absent plan temporarily disables only bank export.
    busy = false;
    result.reset();
    currentPlan.reset();
    failedTrims.reset();
    refresh();
    status.setText("Updating sample settings...", dontSendNotification);
    auto snapshot = project;
    Component::SafePointer<MainView> safe(this);
    queue(
        [this, safe, snapshot, version, completionNote, continuing, auditionPad,
         auditionRequest]() mutable {
          if (generation != version)
            return;
          std::shared_ptr<sp::ProcessingPlan> plan, invalidLoopPlan;
          std::shared_ptr<std::array<sp::Render, 32>> trims;
          String message;
          std::array<bool, 32> problems{};
          bool statusOnly = true;
          try {
            plan = std::make_shared<sp::ProcessingPlan>(sp::planBank(snapshot));
          } catch (const std::exception &e) {
            message = e.what();
            const auto *memory = dynamic_cast<const sp::MemoryError *>(&e);
            const auto *loopError = dynamic_cast<const sp::LoopError *>(&e);
            problems = memory ? memory->pads : std::array<bool, 32>{};
            statusOnly = memory || loopError;
            invalidLoopPlan = loopError ? loopError->plan : nullptr;
            trims = loopError && loopError->selections
                        ? loopError->selections
                        : std::make_shared<std::array<sp::Render, 32>>(
                              sp::selectionMetadata(snapshot));
          }
          if (generation != version)
            return;
          sp::clampAutoMapRestoreLoops(snapshot);
          const auto restore = snapshot.autoMapRestore;
          MessageManager::callAsync([safe, plan, trims, version, completionNote,
                                     problems, message, statusOnly, restore] {
            if (!safe || safe->generation != version)
              return;
            safe->currentPlan = plan;
            safe->failedTrims = trims;
            const auto &metadata = plan ? plan->sounds : *trims;
            safe->project.autoMapRestore = restore;
            sp::clampLoopLengths(safe->project, metadata);
            sp::syncAutoMapRestore(safe->project);
            if (plan) {
              safe->result = std::make_shared<sp::Result>();
              safe->result->sounds = plan->sounds;
              safe->result->used = plan->used;
              safe->memoryProblems = {};
              int frames =
                  std::accumulate(plan->used.begin(), plan->used.end(), 0);
              safe->status.setText(
                  String(safe->project.pool.size()) + " samples in pool  |  " +
                      String(frames / double(sp::rate), 2) + " s stored  |  " +
                      String((8 * 65534 - frames) / double(sp::rate), 2) +
                      " s free" + completionNote,
                  dontSendNotification);
            } else {
              safe->memoryProblems = problems;
              safe->error(message, statusOnly);
            }
            for (int i = 0; i < 32; ++i) {
              safe->padDisplayAssets[i] = safe->project.pads[i].asset;
              const auto &sound = metadata[i];
              const int frames =
                  plan ? plan->frames[i]
                       : (int)std::ceil(
                             (sound.end - sound.start - sound.removed) *
                             sp::rate * sp::pitchRatio(sound.tune));
              safe->padDisplayText[i] = String(frames / double(sp::rate), 2) +
                                        "s / T" + String(sound.tune);
            }
            safe->refresh();
          });
          try {
            const auto cancel = [this, version, auditionRequest] {
              return generation != version ||
                     previewGeneration != auditionRequest;
            };
            const auto renderPlan = plan ? plan : invalidLoopPlan;
            const auto previewPlan =
                renderPlan ? *renderPlan
                           : sp::planPadPreview(snapshot, auditionPad);
            auto rendered = std::make_shared<sp::Render>(
                renderCache.render(snapshot, previewPlan, auditionPad, cancel));
            MessageManager::callAsync([safe, rendered, version, auditionPad,
                                       auditionRequest, continuing] {
              if (!safe || safe->generation != version ||
                  safe->previewGeneration != auditionRequest)
                return;
              safe->acceptRender(*rendered, auditionPad);
              if (continuing)
                safe->playRender(*rendered, auditionPad, true);
            });
          } catch (const sp::ProcessingCancelled &) {
          } catch (const std::exception &e) {
            const String text = e.what();
            MessageManager::callAsync([safe, text, version] {
              if (safe && safe->generation == version)
                safe->error(text);
            });
          }
        },
        JobKind::Processing);
  }
  void acceptRender(const sp::Render &rendered, int padIndex) {
    if (result) {
      auto updated = std::make_shared<sp::Result>(*result);
      updated->sounds[padIndex] = rendered;
      result = std::move(updated);
    } else if (failedTrims) {
      auto &metadata = (*failedTrims)[padIndex];
      metadata.gainDb = rendered.gainDb;
      metadata.playbackEnd = rendered.playbackEnd;
      metadata.loopStart = rendered.loopStart;
      metadata.loopFrames = rendered.loopFrames;
    }
    if (selected == padIndex)
      refresh();
  }
  void load(StringArray paths, bool preset = false) {
    stopAudio();
    auto version = ++generation;
    busy = true;
    refresh();
    status.setText("Loading samples...", dontSendNotification);
    Component::SafePointer<MainView> safe(this);
    const auto snapshot = project;
    queue([safe, paths, preset, version, snapshot] {
      try {
        StringArray warnings;
        auto p = snapshot;
        if (preset)
          p = sp::loadPreset(File(paths[0]));
        else {
          auto incoming = sp::importFiles(paths, warnings, false);
          if (p.pool.empty())
            p.bankName = incoming.bankName;
          sp::appendSamples(p, incoming.pool);
        }
        MessageManager::callAsync(
            [safe, p = std::move(p), warnings, version, preset]() mutable {
              if (!safe || safe->generation != version)
                return;
              safe->checkpoint();
              safe->project = std::move(p);
              if (preset)
                safe->selected = 0;
              const auto note =
                  warnings.isEmpty()
                      ? String()
                      : "  |  " + String(warnings.size()) + " files skipped";
              safe->changed(note);
              safe->dirty = !preset;
            });
      } catch (const std::exception &e) {
        String message = e.what();
        MessageManager::callAsync([safe, message, version, preset] {
          if (!safe || safe->generation != version)
            return;
          safe->busy = false;
          safe->refresh();
          if (preset)
            safe->error(message);
          else
            safe->status.setText(message, dontSendNotification);
        });
      }
    });
  }
  void chooseImport() {
    chooser = std::make_unique<FileChooser>("Import Sample Folder");
    chooser->launchAsync(
        FileBrowserComponent::openMode |
            FileBrowserComponent::canSelectDirectories,
        [safe = Component::SafePointer<MainView>(this)](const FileChooser &c) {
          if (safe && c.getResult().exists())
            safe->load({c.getResult().getFullPathName()});
        });
  }
  void chooseOpen() {
    chooser = std::make_unique<FileChooser>("Open Kit", File(), "*.spkit");
    chooser->launchAsync(
        FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
        [safe = Component::SafePointer<MainView>(this)](const FileChooser &c) {
          if (safe && c.getResult().existsAsFile())
            safe->load({c.getResult().getFullPathName()}, true);
        });
  }
  bool canSave(bool bank) const { return !bank || (result && !busy); }
  void chooseSave(bool bank, File suggestedFile = {}) {
    if (!canSave(bank))
      return;
    const auto bankName = sp::bankFileStem(project.bankName);
    if (suggestedFile == File())
      suggestedFile = File::getSpecialLocation(File::userDocumentsDirectory)
                          .getChildFile(bankName + (bank ? ".sp12" : ".spkit"));
    chooser = std::make_unique<FileChooser>(
        bank ? "Export SP-1200 Bank" : "Save Kit", suggestedFile,
        bank ? "*.sp12" : "*.spkit");
    chooser->launchAsync(
        FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles |
            FileBrowserComponent::warnAboutOverwriting,
        [safe = Component::SafePointer<MainView>(this),
         bank](const FileChooser &c) {
          if (!safe || c.getResult() == File())
            return;
          auto file = c.getResult();
          if (!file.hasFileExtension(bank ? "sp12" : "spkit")) {
            safe->error(bank ? "Use the .sp12 extension."
                             : "Use the .spkit extension.");
            return;
          }
          if (bank) {
            const auto normalised = file.getSiblingFile(
                sp::bankFileStem(file.getFileNameWithoutExtension()) + ".sp12");
            if (normalised != file) {
              // Review the actual name and let the native dialog confirm
              // overwriting that exact file, not the unsanitised destination.
              safe->status.setText("Bank names use up to 16 ASCII characters. "
                                   "Please confirm the adjusted filename.",
                                   dontSendNotification);
              MessageManager::callAsync([safe, normalised] {
                if (safe)
                  safe->chooseSave(true, normalised);
              });
              return;
            }
          }
          auto p = safe->project;
          if (!safe->canSave(bank))
            return;
          auto r = safe->busy ? std::shared_ptr<sp::Result>{} : safe->result;
          auto savedVersion = safe->generation.load();
          if (bank) {
            safe->busy = true;
            safe->status.setText("Preparing SP-1200 bank...",
                                 dontSendNotification);
            safe->refresh();
          }
          safe->queue([safe, p, r, file, bank, savedVersion] {
            try {
              if (bank) {
                const auto cancel = [safe, savedVersion] {
                  return !safe || safe->generation != savedVersion;
                };
                auto complete = sp::renderBank(p, sp::planBank(p),
                                               safe->renderCache, cancel);
                if (cancel())
                  throw sp::ProcessingCancelled{};
                sp::atomicWrite(file, complete.bank);
              } else
                sp::savePreset(p, file, r.get());
              MessageManager::callAsync([safe, file, bank, savedVersion] {
                if (safe) {
                  if (!bank && safe->generation == savedVersion)
                    safe->dirty = false;
                  if (safe->generation == savedVersion) {
                    if (bank) {
                      safe->busy = false;
                      safe->refresh();
                    }
                    const bool memoryWarning =
                        std::any_of(safe->memoryProblems.begin(),
                                    safe->memoryProblems.end(),
                                    [](bool problem) { return problem; });
                    const String savedNote("Kit saved | ");
                    const auto previous = safe->status.getText();
                    const auto warning =
                        previous.startsWith(savedNote)
                            ? previous.substring(savedNote.length())
                            : previous;
                    safe->status.setText(
                        memoryWarning ? savedNote + warning
                                      : "Saved " + file.getFullPathName(),
                        dontSendNotification);
                  }
                }
              });
            } catch (const sp::ProcessingCancelled &) {
            } catch (const std::exception &e) {
              String m = e.what();
              MessageManager::callAsync([safe, m, bank, savedVersion] {
                if (safe && safe->generation == savedVersion) {
                  if (bank) {
                    safe->busy = false;
                    safe->refresh();
                  }
                  safe->error(m);
                }
              });
            }
          });
        });
  }
  bool isInterestedInFileDrag(const StringArray &) override { return true; }
  void filesDropped(const StringArray &files, int, int) override {
    if (files.size() == 1 && File(files[0]).hasFileExtension("spkit"))
      load(files, true);
    else
      load(files);
  }
  int getNumRows() override { return (int)project.pool.size(); }
  void paintListBoxItem(int row, Graphics &g, int w, int h,
                        bool chosen) override {
    if (row >= getNumRows())
      return;
    auto a = project.pool[row];
    StringArray assignedPads;
    for (int i = 0; i < 32; ++i)
      if (project.pads[i].asset == a->id)
        assignedPads.add(sp::padName(i));
    const bool assigned = !assignedPads.isEmpty();
    const Colour assignmentColour = spStyle::green;
    if (assigned)
      g.fillAll(chosen ? Colour(0xff35483e) : Colour(0xff25362d));
    else if (chosen)
      g.fillAll(Colour(0xff40392f));
    if (assigned) {
      g.setColour(assignmentColour);
      g.fillRect(0, 3, 3, h - 6);
    }
    g.setColour(assigned ? assignmentColour : spStyle::ivory);
    g.setFont(spStyle::body(14));
    g.drawText(a->filename, 10, 3, w - 20, 20, Justification::centredLeft);
    g.setFont(spStyle::body(12));
    String badge = assignedPads.joinIntoString(", ", 0, 3);
    if (assignedPads.size() > 3)
      badge += " +" + String(assignedPads.size() - 3);
    const int badgeWidth = assigned ? std::min(140, w / 2) : 0;
    g.setColour(spStyle::muted);
    g.drawText(a->category + "  |  " +
                   String(a->audio.getNumSamples() / a->sampleRate, 2) + " s",
               10, 24, w - 20 - badgeWidth, h - 24, Justification::centredLeft);
    if (assigned) {
      g.setColour(assignmentColour);
      g.drawText(badge, w - 10 - badgeWidth, 24, badgeWidth, h - 24,
                 Justification::centredRight);
    }
  }

  var getDragSourceDescription(const SparseSet<int> &rows) override {
    return rows.size() ? var("pool:" + String(rows[0])) : var();
  }
  void listBoxItemClicked(int row, const MouseEvent &event) override {
    if (event.mods.isLeftButtonDown() &&
        !event.mouseWasDraggedSinceMouseDown() && row >= 0 &&
        row < getNumRows())
      playPoolSample(project.pool[row]);
  }
  void drop(int target, String source) {
    if (busy)
      return;
    int i = source.fromFirstOccurrenceOf(":", false, false).getIntValue();
    checkpoint();
    if (source.startsWith("pad:") && i >= 0 && i < 32) {
      std::swap(project.pads[i], project.pads[target]);
      sp::retainMappedAssignment(project, i);
      sp::retainMappedAssignment(project, target);
    } else if (source.startsWith("pool:") && i >= 0 && i < getNumRows())
      sp::assignPad(project, target, *project.pool[i]);
    selected = target;
    changed();
  }
  void select(int i) {
    commitName();
    grabKeyboardFocus();
    selected = i;
    refresh();
    playPad();
  }
  void updateStopButton() {
    bool isPlaying;
    {
      const ScopedLock lock(audioLock);
      isPlaying = active;
    }
    stop.setEnabled(isPlaying);
  }
  void stopAudio() {
    ++previewGeneration;
    {
      const ScopedLock lock(audioLock);
      active = false;
      playingPad = -1;
    }
    updateStopButton();
  }
  void playPoolSample(std::shared_ptr<sp::Asset> asset) {
    stopAudio();
    if (busy)
      return;
    // Preserve the assigned pad's cuts and Reverse even if the bank is invalid.
    if (project.pads[selected].asset == asset->id) {
      playAssignedPad(selected);
      return;
    }
    for (int i = 0; i < 32; ++i)
      if (project.pads[i].asset == asset->id) {
        playAssignedPad(i);
        return;
      }
    sp::Project preview;
    preview.autoTune = project.autoTune;
    preview.autoTrim = project.autoTrim;
    preview.threshold = project.threshold;
    preview.fadeOutOnExport = project.fadeOutOnExport;
    preview.normalizeOnExport = project.normalizeOnExport;
    preview.pool.push_back(asset);
    preview.pads[0] =
        sp::makePad(*asset, 0, preview.autoTrim, preview.autoTune);
    queuePadPreview(std::move(preview), 0);
  }
  bool isLoopPlaying(int padIndex) {
    const ScopedLock lock(audioLock);
    return active && playbackLoopFrames > 0 && playingPad == padIndex;
  }
  void queueLivePreview(sp::Project preview, int padIndex,
                        std::shared_ptr<sp::ProcessingPlan> plan = nullptr) {
    const auto request = ++previewGeneration;
    queuePadPreview(std::move(preview), padIndex, padIndex, true, request,
                    std::move(plan));
  }
  void
  queuePadPreview(sp::Project preview, int padIndex, int assignedPad = -1,
                  bool continuing = false, uint64_t request = 0,
                  std::shared_ptr<sp::ProcessingPlan> draftPlan = nullptr) {
    if (!request)
      request = previewGeneration.load();
    const auto version = generation.load();
    // Draft loop edits and unassigned pool samples use their own selection.
    auto bankPlan = draftPlan                         ? std::move(draftPlan)
                    : !continuing && assignedPad >= 0 ? currentPlan
                                                      : nullptr;
    Component::SafePointer<MainView> safe(this);
    queue(
        [this, safe, preview = std::move(preview), padIndex, assignedPad,
         continuing, request, version, bankPlan] {
          const auto cancel = [this, version, request] {
            return previewGeneration != request || generation != version;
          };
          if (cancel())
            return;
          try {
            auto plan = bankPlan;
            if (!plan && assignedPad >= 0 && !continuing) {
              try {
                plan =
                    std::make_shared<sp::ProcessingPlan>(sp::planBank(preview));
              } catch (const sp::MemoryError &) {
              } catch (const sp::LoopError &error) {
                plan = error.plan;
              }
            }
            const auto selection =
                plan ? *plan : sp::planPadPreview(preview, padIndex);
            auto rendered = std::make_shared<sp::Render>(
                renderCache.render(preview, selection, padIndex, cancel));
            MessageManager::callAsync(
                [safe, rendered, request, version, assignedPad, continuing] {
                  if (safe && safe->previewGeneration == request &&
                      safe->generation == version) {
                    if (assignedPad >= 0 && !continuing)
                      safe->acceptRender(*rendered, assignedPad);
                    safe->playRender(*rendered, assignedPad, continuing);
                  }
                });
          } catch (const sp::ProcessingCancelled &) {
          } catch (const std::exception &e) {
            const String message = e.what();
            MessageManager::callAsync([safe, message, request, version] {
              if (safe && safe->previewGeneration == request &&
                  safe->generation == version)
                safe->status.setText(message, dontSendNotification);
            });
          }
        },
        JobKind::Audition);
  }
  void playPad() {
    stopAudio();
    if (busy || project.pads[selected].asset.isEmpty())
      return;
    playAssignedPad(selected);
  }
  void playAssignedPad(int padIndex) {
    if (result && !result->sounds[padIndex].values.empty())
      playRender(result->sounds[padIndex], padIndex);
    else
      queuePadPreview(project, padIndex, padIndex);
  }
  void playRender(const sp::Render &r, int assignedPad = -1,
                  bool continuing = false) {
    AudioBuffer<float> prepared(1, (int)r.values.size());
    for (int i = 0; i < prepared.getNumSamples(); ++i)
      prepared.setSample(0, i, (r.values[i] - 2048) / 2048.f);
    {
      const ScopedLock lock(audioLock);
      if (continuing &&
          (!active || playbackLoopFrames <= 0 || playingPad != assignedPad))
        return; // Stop/retrigger/sound changes must never revive an old live
                // edit.
      const double sourceTime =
          playbackSourceStart + position / playbackFramesPerSecond;
      std::swap(playing, prepared);
      playbackFramesPerSecond = sp::rate * sp::pitchRatio(r.tune);
      playbackSourceStart = r.start;
      position =
          continuing
              ? std::max(0., (sourceTime - r.start) * playbackFramesPerSecond)
              : 0;
      step = playbackFramesPerSecond / deviceRate;
      playingPad = assignedPad;
      // playing owns the prepared buffer after the swap.
      playbackLoopFrames = std::clamp(r.loopFrames, 0, playing.getNumSamples());
      if (continuing && playbackLoopFrames > 0 &&
          position >= playing.getNumSamples())
        position = playing.getNumSamples() - playbackLoopFrames +
                   std::fmod(position - playing.getNumSamples(),
                             double(playbackLoopFrames));
      active = !r.values.empty();
    }
    updateStopButton();
  }
  void prepareToPlay(int, double sr) override {
    const ScopedLock lock(audioLock);
    deviceRate = sr;
    active = false;
  }
  void releaseResources() override {}
  void getNextAudioBlock(const AudioSourceChannelInfo &info) override {
    info.clearActiveBufferRegion();
    const ScopedTryLock lock(audioLock);
    if (!lock.isLocked() || !active)
      return;
    int n = playing.getNumSamples();
    for (int i = 0; i < info.numSamples; ++i) {
      if (playbackLoopFrames > 0 && position >= n)
        position = n - playbackLoopFrames +
                   std::fmod(position - n, double(playbackLoopFrames));
      int j = (int)position;
      if (j >= n) {
        active = false;
        break;
      }
      float frac = float(position - j);
      for (int c = 0; c < info.buffer->getNumChannels(); ++c) {
        int src = std::min(c, playing.getNumChannels() - 1);
        float a = playing.getSample(src, j),
              b = playing.getSample(src, j + 1 < n ? j + 1
                                         : playbackLoopFrames > 0
                                             ? n - playbackLoopFrames
                                             : n - 1);
        info.buffer->setSample(c, info.startSample + i, a + (b - a) * frac);
      }
      position += step;
    }
  }
  void undoEdit() {
    if (busy || past.empty())
      return;
    future.push_back(project);
    project = past.back();
    past.pop_back();
    changed();
  }
  void redoEdit() {
    if (busy || future.empty())
      return;
    past.push_back(project);
    project = future.back();
    future.pop_back();
    changed();
  }
  void refresh() {
    updateStopButton();
    updating = true;
    pool.updateContent();
    pool.repaint();
    for (int i = 0; i < 32; ++i) {
      pads[i]->setTitle(sp::padName(i) + " " + project.pads[i].name);
      pads[i]->repaint();
    }
    bool has = project.pads[selected].asset.isNotEmpty();
    exportBank.setEnabled(result && !busy);
    clearAll.setEnabled(!busy && !project.pool.empty());
    tune.setEnabled(!busy);
    tune.setToggleState(project.autoTune, dontSendNotification);
    sort.setEnabled(!busy);
    sort.setToggleState(project.autoSort, dontSendNotification);
    for (Component *c : std::initializer_list<Component *>{
             &name, &channel, &remove, &reverse, &loop})
      c->setEnabled(has && !busy);
    auto &p = project.pads[selected];
    resetTrimButton.setEnabled(canResetTrim());
    reverse.setToggleState(p.reverse, dontSendNotification);
    loop.setToggleState(p.loopEnabled, dontSendNotification);
    loop.setButtonText(p.loopEnabled ? "Loop On" : "Loop Off");
    name.setText(p.name, false);
    channel.setSelectedId(p.channel, dontSendNotification);
    reset.setEnabled(!busy);
    reset.setToggleState(project.autoTrim, dontSendNotification);
    wave.setEnabled(has && !busy);
    threshold.setEnabled(!busy && project.autoTrim);
    const auto thumbColour = getLookAndFeel().findColour(Slider::thumbColourId);
    threshold.setColour(Slider::thumbColourId, threshold.isEnabled()
                                                   ? thumbColour
                                                   : thumbColour.darker(.65f));
    threshold.setValue(project.threshold, dontSendNotification);
    auto nextAsset = sp::assetFor(project, p.asset);
    const bool sameWaveform = nextAsset == wave.asset &&
                              waveformPad == selected &&
                              wave.reversed == p.reverse;
    wave.asset = nextAsset;
    wave.reversed = p.reverse;
    wave.autoTune = project.autoTune;
    wave.manualTrim = p.manualTrim;
    wave.loopEnabled = p.loopEnabled;
    wave.loopSeconds = p.loopSeconds;
    waveformPad = selected;
    if (!sameWaveform)
      wave.render = {};
    if (wave.asset) {
      if (result) {
        const auto &metadata = result->sounds[selected];
        const double previousEnd = wave.render.playbackEnd;
        const double previousLoop = wave.render.loopStart;
        const bool retainBounds = sameWaveform && metadata.values.empty() &&
                                  metadata.start == wave.render.start &&
                                  metadata.end == wave.render.end &&
                                  metadata.removed == wave.render.removed;
        wave.setRenderMetadata(metadata);
        if (retainBounds) {
          wave.render.playbackEnd = previousEnd;
          wave.render.loopStart = previousLoop;
        }
      } else if (failedTrims && !busy)
        wave.setRenderMetadata((*failedTrims)[selected]);
      else if (!sameWaveform || !project.autoTrim || p.manualTrim) {
        const double duration =
            wave.asset->audio.getNumSamples() / wave.asset->sampleRate;
        wave.render.start = p.start >= 0 ? p.start : 0;
        wave.render.end = p.end >= 0 ? p.end : duration;
        wave.render.removed = 0;
        wave.render.playbackEnd = wave.render.loopStart = -1;
      }
      // A missing result means processing is pending, not that the trim is
      // empty.
      auto &r = wave.render;
      const bool gainPending =
          result && result->sounds[selected].values.empty();
      details.setText(
          sp::padName(selected) + "  " + wave.asset->filename + "  |  Trim: " +
              String(p.manualTrim       ? "Manual"
                     : project.autoTrim ? "Auto"
                                        : "Default") +
              "  |  Tune " + String(r.tune) + "  |  Gain " +
              (gainPending ? String("pending") : String(r.gainDb, 1) + " dB") +
              "  |  Tail Removed " + String(r.removed, 3) + " s",
          dontSendNotification);
    } else
      details.setText(sp::padName(selected) + "  |  Empty Pad",
                      dontSendNotification);
    wave.repaint();
    updating = false;
    if (onStateChanged)
      onStateChanged();
  }
  void timerCallback() override { updateStopButton(); }
  void paint(Graphics &g) override {
    g.fillAll(spStyle::shell);
    g.setColour(spStyle::panel);
    g.fillRoundedRectangle(12, 64, float(getWidth() - 24),
                           float(getHeight() - 76), 4);
    g.setColour(spStyle::ivory);
    g.setFont(spStyle::heading(14));
    g.drawText("Sample Pool", 20, 63, 120, 20, Justification::centredLeft);
    g.setColour(spStyle::muted);
    g.drawHorizontalLine(75, 138, 330);
    g.setColour(spStyle::ivory);
    g.drawText("Performance", 348, 63, 120, 20, Justification::centredLeft);
    g.setColour(spStyle::muted);
    g.drawHorizontalLine(75, 472, float(getWidth() - 26));
  }
  void resized() override {
    auto bounds = getLocalBounds().reduced(20);
    auto toolbar = bounds.removeFromTop(36);
    threshold.setBounds(toolbar.removeFromRight(240).reduced(3));
    reset.setBounds(toolbar.removeFromRight(110).reduced(3));
    sort.setBounds(toolbar.removeFromRight(110).reduced(3));
    tune.setBounds(toolbar.removeFromRight(110).reduced(3));
    for (auto *b : {&import, &exportBank})
      b->setBounds(toolbar.removeFromLeft(180).reduced(3));
    clearAll.setBounds(toolbar.removeFromLeft(110).reduced(3));
    bounds.removeFromTop(28);
    status.setBounds(bounds.removeFromBottom(28));
    bounds.removeFromBottom(12);
    auto editor = bounds.removeFromBottom(204);
    pool.setBounds(bounds.removeFromLeft(310).reduced(0, 3));
    bounds.removeFromLeft(18);
    int ph = bounds.getHeight() / 4, pw = bounds.getWidth() / 8;
    for (int i = 0; i < 32; ++i)
      pads[i]->setBounds(bounds.getX() + i % 8 * pw, bounds.getY() + i / 8 * ph,
                         pw - 6, ph - 6);
    details.setBounds(editor.removeFromTop(28));
    wave.setBounds(editor.removeFromTop(136));
    editor.removeFromTop(10);
    auto row = editor.removeFromTop(30);
    nameLabel.setBounds(row.removeFromLeft(85).reduced(2));
    name.setBounds(row.removeFromLeft(100).reduced(2));
    row.removeFromLeft(12);
    channelLabel.setBounds(row.removeFromLeft(110).reduced(2));
    channel.setBounds(row.removeFromLeft(115).reduced(2));
    reverse.setBounds(row.removeFromLeft(100).reduced(2));
    loop.setBounds(row.removeFromLeft(90).reduced(2));
    stop.setBounds(row.removeFromRight(110).reduced(2));
    remove.setBounds(row.removeFromRight(110).reduced(2));
    resetTrimButton.setBounds(row.removeFromRight(110).reduced(2));
  }
};
std::unique_ptr<AccessibilityHandler> PadButton::createAccessibilityHandler() {
  return std::make_unique<AccessibilityHandler>(
      *this, AccessibilityRole::button,
      AccessibilityActions().addAction(AccessibilityActionType::press,
                                       [this] { owner.select(index); }));
}
void PadButton::paint(Graphics &g) {
  const bool assigned = owner.project.pads[index].asset.isNotEmpty();
  const bool selected = owner.selected == index;
  const auto bounds = getLocalBounds().toFloat().reduced(1);
  g.setColour(spStyle::ink.darker(.4f));
  g.fillRoundedRectangle(bounds, 4);
  const auto face = selected
                        ? spStyle::well.interpolatedWith(spStyle::green, .22f)
                    : hover ? Colour(0xff4b5655)
                            : Colour(0xff303a3d);
  const auto panel = bounds.reduced(4).withTrimmedBottom(3);
  g.setGradientFill(ColourGradient(face.brighter(.08f), 0, panel.getY(),
                                   face.darker(.15f), 0, panel.getBottom(),
                                   false));
  g.fillRoundedRectangle(panel, 2);
  auto a = sp::assetFor(owner.project, owner.project.pads[index].asset);
  const bool sourceTooLong =
      a && sp::exceedsSampleDuration(*a, owner.project.autoTune);
  g.setColour(sourceTooLong ? spStyle::red
              : selected    ? spStyle::green
              : hover       ? spStyle::orange
                            : Colour(0xff667779));
  g.drawRoundedRectangle(bounds.reduced(1), 3,
                         sourceTooLong || selected || hover ? 2.f : 1.f);
  if (selected && sourceTooLong) {
    g.setColour(spStyle::green);
    g.drawRoundedRectangle(panel.reduced(1), 2, 2);
  }
  g.setColour(assigned ? spStyle::red : spStyle::well);
  g.fillEllipse(float(getWidth() - 19), 12, 7, 7);
  auto area = getLocalBounds().reduced(10, 7);
  g.setFont(spStyle::heading(19));
  g.setColour(selected   ? spStyle::green
              : assigned ? spStyle::ivory
                         : spStyle::muted);
  g.drawText(sp::padName(index), area.removeFromTop(22),
             Justification::centredLeft);
  g.setFont(spStyle::display(12));
  const auto instrument = sp::padInstrument(owner.project, index);
  auto soundArea = area.removeFromTop(20);
  if (assigned)
    g.drawFittedText(owner.project.pads[index].name, soundArea,
                     Justification::centredLeft, 1);
  if (instrument.isNotEmpty()) {
    g.setFont(spStyle::body(12));
    g.setColour(spStyle::muted);
    g.drawFittedText(instrument, area.removeFromTop(20),
                     Justification::centredLeft, 1);
  }
  if (a) {
    if (owner.padDisplayAssets[index] == a->id) {
      g.setFont(spStyle::display(10.5f));
      g.setColour(selected ? spStyle::green : spStyle::ivory);
      g.drawText(owner.padDisplayText[index], area.removeFromTop(16),
                 Justification::centredLeft);
    }
  }
}
void PadButton::mouseDown(const MouseEvent &) { owner.select(index); }
void PadButton::mouseDrag(const MouseEvent &e) {
  if (e.getDistanceFromDragStart() > 6 &&
      !owner.project.pads[index].asset.isEmpty())
    owner.startDragging("pad:" + String(index), this);
}
void PadButton::itemDropped(const SourceDetails &s) {
  hover = false;
  owner.drop(index, s.description.toString());
  repaint();
}
class HelpWindow : public DocumentWindow {
public:
  HelpWindow(const String &title = "SP-1200 Bank Creator Help",
             const String &text = String::fromUTF8(appInfo::quickGuide),
             bool formatGuide = true)
      : DocumentWindow(title, spStyle::panel,
                       DocumentWindow::closeButton) {
    setUsingNativeTitleBar(true);
    auto *guide = new TextEditor();
    guide->setMultiLine(true, true);
    guide->setScrollbarsShown(true);
    guide->setCaretVisible(false);
    guide->setFont(spStyle::body(formatGuide ? 17 : 15));
    guide->setLineSpacing(formatGuide ? 1.3f : 1.15f);
    guide->setColour(TextEditor::backgroundColourId, spStyle::panel);
    guide->setColour(TextEditor::textColourId, spStyle::ivory);
    guide->setIndents(26, 24);
    if (formatGuide) {
      for (auto line : StringArray::fromLines(text)) {
        const bool heading = line.startsWith("# ") || line.startsWith("## ");
        const bool titleLine = line.startsWith("# ");
        const auto font = heading
                              ? spStyle::body(titleLine ? 25 : 20).boldened()
                              : spStyle::body(17);
        if (heading)
          line = line.substring(titleLine ? 2 : 3);
        int position = 0;
        while (position < line.length()) {
          const int start = line.indexOfChar(position, '*');
          const int end = start >= 0 ? line.indexOfChar(start + 1, '*') : -1;
          guide->setFont(font);
          if (end < 0) {
            guide->insertTextAtCaret(line.substring(position));
            break;
          }
          guide->insertTextAtCaret(line.substring(position, start));
          guide->setFont(font.italicised());
          guide->insertTextAtCaret(line.substring(start + 1, end));
          position = end + 1;
        }
        guide->setFont(font);
        guide->insertTextAtCaret("\n");
      }
    } else {
      guide->setText(text, false);
    }
    guide->setReadOnly(true);
    guide->setCaretPosition(0);
    guide->setSize(760, 680);
    setContentOwned(guide, true);
    setResizable(true, false);
    setResizeLimits(460, 320, 1200, 1000);
    centreWithSize(760, 680);
  }
  void closeButtonPressed() override { setVisible(false); }
};
class SettingsPanel : public Component {
public:
  MainView &view;
  ToggleButton fadeOut{"Fade Out Sound on Export"};
  ToggleButton normalize{"Normalize on Export"};
  Label heading, fadeNote, normalizeNote, previewNote;
  explicit SettingsPanel(MainView &v) : view(v) {
    for (Component *c : std::initializer_list<Component *>{
             &heading, &fadeOut, &fadeNote, &normalize, &normalizeNote,
             &previewNote})
      addAndMakeVisible(c);
    heading.setText("Export Processing", dontSendNotification);
    heading.setFont(spStyle::heading(20));
    fadeNote.setText("Fade out one-shots over up to 40 ms. Loops stay unfaded.",
                     dontSendNotification);
    normalizeNote.setText("Normalize each sound to -0.5 dBFS sample peak.",
                          dontSendNotification);
    previewNote.setText(
        "Changes also apply to preview and are saved immediately.",
        dontSendNotification);
    for (auto *note : {&fadeNote, &normalizeNote, &previewNote}) {
      note->setFont(spStyle::body(13));
      note->setColour(Label::textColourId, spStyle::muted);
    }
    fadeOut.onClick = normalize.onClick = [this] {
      view.setExportSettings(fadeOut.getToggleState(),
                             normalize.getToggleState());
      refresh();
    };
    refresh();
    setSize(480, 260);
  }
  void refresh() {
    fadeOut.setToggleState(view.project.fadeOutOnExport, dontSendNotification);
    normalize.setToggleState(view.project.normalizeOnExport,
                             dontSendNotification);
    fadeOut.setEnabled(!view.busy);
    normalize.setEnabled(!view.busy);
  }
  void resized() override {
    auto area = getLocalBounds().reduced(24, 18);
    heading.setBounds(area.removeFromTop(38));
    area.removeFromTop(10);
    fadeOut.setBounds(area.removeFromTop(30));
    fadeNote.setBounds(area.removeFromTop(30).withTrimmedLeft(24));
    area.removeFromTop(12);
    normalize.setBounds(area.removeFromTop(30));
    normalizeNote.setBounds(area.removeFromTop(30).withTrimmedLeft(24));
    area.removeFromTop(12);
    previewNote.setBounds(area.removeFromTop(30));
  }
  void paint(Graphics &g) override { g.fillAll(spStyle::panel); }
};
class SettingsWindow : public DocumentWindow {
public:
  explicit SettingsWindow(MainView &view)
      : DocumentWindow("Settings", spStyle::panel,
                       DocumentWindow::closeButton) {
    setUsingNativeTitleBar(true);
    setContentOwned(new SettingsPanel(view), true);
    centreWithSize(getWidth(), getHeight());
  }
  void refresh() {
    static_cast<SettingsPanel *>(getContentComponent())->refresh();
  }
  void closeButtonPressed() override { setVisible(false); }
};
class AppMenu : public MenuBarModel, public ApplicationCommandTarget {
public:
  enum {
    importFolder = 0x2000,
    openPreset,
    savePreset,
    exportBank,
    undoEdit,
    redoEdit,
    clearPad,
    resetTrim,
    showHelp,
    toggleTooltips,
    showAbout,
    showSettings,
    showLicense
  };
  explicit AppMenu(MainView &v) : view(v) {
    commands.registerAllCommandsForTarget(this);
    commands.setFirstCommandTarget(this);
    setApplicationCommandManagerToWatch(&commands);
    view.addKeyListener(commands.getKeyMappings());
    view.onStateChanged = [this] {
      commands.commandStatusChanged();
      if (settingsWindow)
        settingsWindow->refresh();
    };
  }
  ~AppMenu() override {
    view.onStateChanged = nullptr;
    view.removeKeyListener(commands.getKeyMappings());
    setApplicationCommandManagerToWatch(nullptr);
  }
  PopupMenu getApplicationMenu() {
    PopupMenu menu;
    menu.addCommandItem(&commands, showAbout);
    menu.addSeparator();
    menu.addCommandItem(&commands, showSettings);
    return menu;
  }
  StringArray getMenuBarNames() override { return {"File", "Edit", "Help"}; }
  PopupMenu getMenuForIndex(int index, const String &) override {
    PopupMenu menu;
    if (index == 2) {
      menu.addCommandItem(&commands, showHelp);
      menu.addCommandItem(&commands, showLicense);
      menu.addSeparator();
      menu.addCommandItem(&commands, toggleTooltips);
      return menu;
    }
    if (index == 1) {
      menu.addCommandItem(&commands, undoEdit);
      menu.addCommandItem(&commands, redoEdit);
      menu.addSeparator();
      menu.addCommandItem(&commands, resetTrim);
      menu.addCommandItem(&commands, clearPad);
      return menu;
    }
    menu.addCommandItem(&commands, importFolder);
    menu.addCommandItem(&commands, openPreset);
    menu.addSeparator();
    menu.addCommandItem(&commands, savePreset);
    menu.addCommandItem(&commands, exportBank);
    return menu;
  }
  void menuItemSelected(int, int) override {}
  ApplicationCommandTarget *getNextCommandTarget() override { return nullptr; }
  void getAllCommands(Array<CommandID> &ids) override {
    ids.addArray({importFolder, openPreset, savePreset, exportBank, undoEdit,
                  redoEdit, clearPad, resetTrim, showHelp, toggleTooltips,
                  showAbout, showSettings, showLicense});
  }
  void getCommandInfo(CommandID id, ApplicationCommandInfo &info) override {
    const auto modifier = ModifierKeys::commandModifier;
    switch (id) {
    case showSettings:
      info.setInfo(String::fromUTF8("Settings…"), "Export processing settings",
                   "Application", 0);
      info.addDefaultKeypress(',', modifier);
      break;
    case toggleTooltips:
      info.setInfo("Show Tooltips", "Enable or disable control tooltips",
                   "Help", 0);
      info.setTicked(bool(view.tooltips));
      break;
    case showHelp:
      info.setInfo("SP-1200 Bank Creator Help", "Read the quick guide", "Help",
                   0);
      break;
    case showAbout:
      info.setInfo("About SP-1200 Bank Creator", "Version and copyright",
                   "Help", 0);
      break;
    case showLicense:
      info.setInfo("License", "Read the AGPLv3 license", "Help", 0);
      break;
    case resetTrim:
      info.setInfo("Reset Trim", "Reset manual trimming and the loop start",
                   "Edit", 0);
      info.setActive(view.canResetTrim());
      break;
    case clearPad:
      info.setInfo("Clear Pad", "Clear the selected pad", "Edit", 0);
      info.addDefaultKeypress(KeyPress::deleteKey, ModifierKeys::noModifiers);
      info.addDefaultKeypress(KeyPress::backspaceKey,
                              ModifierKeys::noModifiers);
      info.setActive(view.canClearPadFromKeyboard());
      break;
    case undoEdit:
      info.setInfo("Undo", "Undo the last kit edit", "Edit", 0);
      info.addDefaultKeypress('z', modifier);
      info.setActive(!view.busy && !view.past.empty());
      break;
    case redoEdit:
      info.setInfo("Redo", "Redo the last kit edit", "Edit", 0);
      info.addDefaultKeypress('z', modifier | ModifierKeys::shiftModifier);
      info.setActive(!view.busy && !view.future.empty());
      break;
    case importFolder:
      info.setInfo(String::fromUTF8("Import Sample Folder…"),
                   "Import a sample folder", "File", 0);
      info.addDefaultKeypress('i', modifier);
      break;
    case openPreset:
      info.setInfo(String::fromUTF8("Open Kit…"), "Open a portable kit", "File",
                   0);
      info.addDefaultKeypress('o', modifier);
      break;
    case savePreset:
      info.setInfo(String::fromUTF8("Save Kit…"), "Save a portable kit", "File",
                   0);
      info.addDefaultKeypress('s', modifier);
      info.setActive(view.canSave(false));
      break;
    case exportBank:
      info.setInfo(String::fromUTF8("Export SP-1200 Bank…"),
                   "Export a Rossum .sp12 bank", "File", 0);
      info.addDefaultKeypress('e', modifier | ModifierKeys::shiftModifier);
      info.setActive(view.canSave(true));
      break;
    }
  }
  bool perform(const InvocationInfo &info) override {
    switch (info.commandID) {
    case showSettings:
      if (!settingsWindow)
        settingsWindow = std::make_unique<SettingsWindow>(view);
      settingsWindow->refresh();
      settingsWindow->setVisible(true);
      settingsWindow->toFront(true);
      return true;
    case toggleTooltips:
      view.toggleTooltips();
      return true;
    case showHelp:
      if (!helpWindow)
        helpWindow = std::make_unique<HelpWindow>();
      helpWindow->setVisible(true);
      helpWindow->toFront(true);
      return true;
    case showAbout:
      AlertWindow::showMessageBoxAsync(
          MessageBoxIconType::InfoIcon, "About SP-1200 Bank Creator",
          "SP-1200 Bank Creator\nVersion " +
              JUCEApplication::getInstance()->getApplicationVersion() + "\n\n" +
              String::fromUTF8(appInfo::copyright) + "\n\n" +
              String::fromUTF8(appInfo::repository) + "\n\n" +
              String::fromUTF8(appInfo::licenseNotice) + "\n\nLegal\n" +
              String::fromUTF8(appInfo::legalNotice),
          "OK");
      return true;
    case showLicense:
      if (!licenseWindow)
        licenseWindow = std::make_unique<HelpWindow>(
            "License", String::fromUTF8(AppLegal::LICENSE, AppLegal::LICENSESize),
            false);
      licenseWindow->setVisible(true);
      licenseWindow->toFront(true);
      return true;
    case resetTrim:
      view.resetSelectedTrim();
      return true;
    case clearPad:
      if (!view.canClearPadFromKeyboard())
        return false;
      view.clearSelectedPad();
      return true;
    case undoEdit:
      view.undoEdit();
      return true;
    case redoEdit:
      view.redoEdit();
      return true;
    case importFolder:
      view.chooseImport();
      return true;
    case openPreset:
      view.chooseOpen();
      return true;
    case savePreset:
      view.chooseSave(false);
      return true;
    case exportBank:
      view.chooseSave(true);
      return true;
    default:
      return false;
    }
  }

private:
  MainView &view;
  ApplicationCommandManager commands;
  std::unique_ptr<HelpWindow> helpWindow;
  std::unique_ptr<HelpWindow> licenseWindow;
  std::unique_ptr<SettingsWindow> settingsWindow;
};
#include "../tests/UiPlaybackTests.h"
#include "../tests/UiSnapshots.h"

class App : public JUCEApplication {
  spStyle::LookAndFeel theme;
  std::unique_ptr<DocumentWindow> window;
  std::unique_ptr<AppMenu> fileMenu;
  struct Window : DocumentWindow {
    explicit Window(bool silent = false)
        : DocumentWindow("SP-1200 Bank Creator", spStyle::shell,
                         DocumentWindow::allButtons) {
      setUsingNativeTitleBar(true);
      setContentOwned(new MainView(silent), true);
      setResizable(true, false);
      setResizeLimits(1100, 760, 2200, 1600);
      centreWithSize(1280, 880);
      setVisible(true);
    }
    void closeButtonPressed() override {
      JUCEApplication::getInstance()->systemRequestedQuit();
    }
  };

public:
  const String getApplicationName() override { return "SP-1200 Bank Creator"; }
  const String getApplicationVersion() override { return SP_APP_VERSION; }
  void initialise(const String &args) override {
    const bool smoke = args == "--smoke-test";
    const bool playbackTest = args == "--playback-test";
    LookAndFeel::setDefaultLookAndFeel(&theme);
    window = std::make_unique<Window>(smoke || playbackTest);
    fileMenu = std::make_unique<AppMenu>(
        *static_cast<MainView *>(window->getContentComponent()));
    const auto appMenu = fileMenu->getApplicationMenu();
    MenuBarModel::setMacMainMenu(fileMenu.get(), &appMenu);
    if (playbackTest) {
      testMemoryPlayback(
          *static_cast<MainView *>(window->getContentComponent()),
          [this](int code) {
            setApplicationReturnValue(code);
            quit();
          });
    }
    if (smoke) {
      auto &view = *static_cast<MainView *>(window->getContentComponent());
      writeUiSnapshots(view);
      if (!testSettingsMenu(*fileMenu, view))
        setApplicationReturnValue(1);
      fileMenu->perform(
          ApplicationCommandTarget::InvocationInfo(AppMenu::showHelp));
      writeHelpSnapshot();
      fileMenu->perform(
          ApplicationCommandTarget::InvocationInfo(AppMenu::showLicense));
      Timer::callAfterDelay(200, [] { JUCEApplication::quit(); });
    }
  }
  void systemRequestedQuit() override {
    auto *view = dynamic_cast<MainView *>(window->getContentComponent());
    if (view && view->dirty) {
      AlertWindow::showOkCancelBox(
          MessageBoxIconType::WarningIcon, "Unsaved Kit",
          "Save a kit before quitting to keep your changes.",
          "Discard and Quit", "Keep Editing", nullptr,
          ModalCallbackFunction::create([this](int answer) {
            if (answer != 0)
              quit();
          }));
    } else
      quit();
  }
  void shutdown() override {
    MenuBarModel::setMacMainMenu(nullptr);
    fileMenu.reset();
    window.reset();
    LookAndFeel::setDefaultLookAndFeel(nullptr);
  }
};
START_JUCE_APPLICATION(App)
