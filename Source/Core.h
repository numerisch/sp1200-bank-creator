// SPDX-License-Identifier: AGPL-3.0-only
// Copyright © 2026 Numerisch GmbH

#pragma once
#include <array>
#include <functional>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_cryptography/juce_cryptography.h>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>
namespace sp {
constexpr int rate = 26040, maxFrames = 65088;
struct Asset {
  juce::String id, filename, relative, category;
  juce::MemoryBlock original;
  juce::AudioBuffer<float> audio;
  double sampleRate = 44100;
};
struct Pad {
  juce::String asset, name, assignmentId;
  int channel = 8;
  bool reverse = false;
  bool manualTrim = false;
  double start = -1, end = -1;
  bool loopEnabled = false;
  double loopSeconds = -1; // -1 follows the entire effective selection.
};
struct AutoMapState {
  std::array<Pad, 32> previous;
  // Assignments introduced by the initial sort from previously unassigned
  // pool sounds belong only to the mapped layout, not the restored layout.
  juce::StringArray transient;
};
struct Project {
  juce::String bankName;
  double threshold = -40;
  bool autoTune = true;
  bool autoTrim = false;
  bool autoSort = false;
  bool fadeOutOnExport = false;
  bool normalizeOnExport = true;
  std::vector<std::shared_ptr<Asset>> pool;
  std::array<Pad, 32> pads = [] {
    std::array<Pad, 32> defaults;
    for (int i = 0; i < 32; ++i)
      defaults[i].channel = i % 8 + 1;
    return defaults;
  }();
  std::optional<AutoMapState> autoMapRestore;
};
struct Render {
  std::vector<int> values;
  double gainDb = 0, start = 0, end = 0, removed = 0;
  // Derived playback bounds; requested cuts stay intact for deterministic Undo
  // and presets. Negative values mean no zero-crossing correction.
  double playbackEnd = -1, loopStart = -1;
  int tune = 16;
  int loopFrames = 0; // Tail span in stored audio; zero means Off.
  bool left = false;
};
struct ProcessingPlan;
struct LoopError : std::runtime_error {
  using std::runtime_error::runtime_error;
  std::shared_ptr<std::array<Render, 32>> selections;
  std::shared_ptr<ProcessingPlan> plan;
};
struct MemoryError : std::runtime_error {
  std::array<bool, 32> pads;
  int64_t excessFrames, unplacedFrames;
  MemoryError(const juce::String &message, std::array<bool, 32> affectedPads,
              int64_t frames, int64_t unplaced = 0)
      : std::runtime_error(message.toStdString()), pads(affectedPads),
        excessFrames(frames), unplacedFrames(unplaced) {}
};
struct Result {
  std::array<Render, 32> sounds;
  std::array<int, 8> used{};
  juce::MemoryBlock bank;
};
// Planning contains no resampled/quantized audio and remains cheap for a full
// kit.
struct ProcessingPlan {
  std::array<Render, 32> sounds;
  std::array<int, 32> begin{}, end{}, frames{};
  std::array<int, 8> used{};
};
using CancelCheck = std::function<bool()>;
struct ProcessingCancelled {};
ProcessingPlan planBank(const Project &);
ProcessingPlan planPadPreview(const Project &, int padIndex);
Render renderPlannedPad(const Project &, const ProcessingPlan &, int padIndex,
                        const CancelCheck & = {});
// Worker-owned, bounded LRU. Source assets are immutable after decoding.
class RenderCache {
public:
  Render render(const Project &, const ProcessingPlan &, int padIndex,
                const CancelCheck & = {});
  size_t renderCount() const { return renders; }
  size_t size() const { return entries.size(); }

private:
  struct Entry {
    std::shared_ptr<Asset> asset;
    int begin, end, frames, tune;
    bool reverse;
    std::vector<double> audio;
  };
  std::vector<Entry> entries;
  size_t renders = 0;
};
Result renderBank(const Project &, const ProcessingPlan &, RenderCache &,
                  const CancelCheck & = {});
juce::String padName(int);
juce::String bankFileStem(juce::String);
juce::String classify(juce::String);
juce::String padInstrument(const Project &, int padIndex);
std::shared_ptr<Asset> decode(const juce::MemoryBlock &, juce::String filename,
                              juce::String relative);
std::shared_ptr<Asset> assetFor(const Project &, const juce::String &);
Project importFiles(const juce::StringArray &, juce::StringArray &warnings,
                    bool autoSort = false);
void sortPads(Project &);
void setAutoMap(Project &, bool enabled);
void syncAutoMapRestore(Project &);
void retainMappedAssignment(Project &, int padIndex);
// Worker-only: update custom loops on assignments hidden by Auto Map.
void clampAutoMapRestoreLoops(Project &);
void clearPad(Project &, int padIndex);
void assignPad(Project &, int padIndex, const Asset &);
void clearKit(Project &);
void appendSamples(Project &, const std::vector<std::shared_ptr<Asset>> &);
Pad makePad(const Asset &, int padIndex, bool autoTrim, bool autoTune);
double maxSelectionDuration(double sampleRate, bool autoTune = false);
bool exceedsSampleDuration(const Asset &, bool autoTune);
void setAutoTune(Project &, bool enabled);
void setAutoTrim(Project &, bool enabled);
void resetTrim(Project &, int padIndex);
std::array<Render, 32> selectionMetadata(const Project &);
double clampTrimPosition(const Asset &, bool isStart, double requestedTime,
                         double oppositeBoundary, bool autoTune = false);
juce::Range<double> shiftTrimSelection(const Asset &, double start, double end,
                                       double offset);
double pitchRatio(int);
void clampLoopLengths(Project &, const std::array<Render, 32> &);
int loopFrameCount(const Pad &, const Render &);
double loopSecondsAfterTrim(double oldStart, double oldEnd, double loopSeconds,
                            double newStart, double newEnd, double sampleRate);
void writeLoopDescriptor(juce::MemoryBlock &, int padIndex, int frames);
Result process(const Project &);
Render renderPadPreview(const Project &, int padIndex);
juce::MemoryBlock buildBank(const Project &, const std::array<Render, 32> &,
                            std::array<int, 8> &);
void validateBank(const juce::MemoryBlock &);
void savePreset(const Project &, const juce::File &, const Result * = nullptr);
Project loadPreset(const juce::File &);
void atomicWrite(const juce::File &, const juce::MemoryBlock &);
} // namespace sp
