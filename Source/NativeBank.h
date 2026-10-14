// SPDX-License-Identifier: AGPL-3.0-only
// Copyright © 2026 Numerisch GmbH
#pragma once
#include "Core.h"

namespace sp {
struct BankRegion {
  int zone = 0, start = 0;
  std::vector<int> values;
};
struct NativeSound {
  int sourcePad = 0, offset = 0, span = 0, loopFrames = 0;
  int tune = 16, flags = 0;
  juce::String name;
  std::shared_ptr<const BankRegion> region;
  // Decay mode uses neutral pitch; the stored five-bit value remains untouched.
  int playbackTune() const { return (flags & 128) ? 16 : tune; }
  double sampleRate() const { return rate * pitchRatio(playbackTune()); }
  double start() const { return offset / sampleRate(); }
  double end() const { return (offset + span) / sampleRate(); }
  double loopSeconds() const { return loopFrames / sampleRate(); }
};
bool retainNativeAudio(const Project &, const Pad &, const Asset &);
struct BankStorage {
  std::array<int, 32> zones{}, starts{}, sizes{};
  std::array<int, 8> used{};
};
BankStorage planNativeStorage(const Project &, const std::array<int, 32> &);
juce::MemoryBlock buildNativeBank(const Project &,
                                const std::array<Render, 32> &,
                                std::array<int, 8> &);
} // namespace sp
