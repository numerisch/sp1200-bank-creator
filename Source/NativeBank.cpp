// SPDX-License-Identifier: AGPL-3.0-only
// Copyright © 2026 Numerisch GmbH
#include "NativeBank.h"
#include "BinaryData.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <tuple>

namespace sp {
using namespace juce;
namespace {
constexpr int bankSize = 824546, audioBase = 0x94e2, zoneBytes = 98304;
constexpr int counters[] = {0x9258, 0x925a, 0x9254, 0x9256,
                           0x9110, 0x9112, 0x925c, 0x925e};
int slot(int pad) { return (3 - pad / 8) * 8 + pad % 8; }
int u16(const uint8 *b, int p) { return b[p] | b[p + 1] << 8; }
void u16(uint8 *b, int p, int n) {
  b[p] = uint8(n);
  b[p + 1] = uint8(n >> 8);
}
void require(bool ok, const String &message) {
  if (!ok)
    throw std::runtime_error(message.toStdString());
}
// Address individual frames, including odd starts and odd-sized allocations.
int sample(const uint8 *b, int zone, int frame) {
  const int p = audioBase + zone * zoneBytes + (frame / 2) * 3;
  return (b[p + (frame & 1)] << 4) |
         ((b[p + 2] >> ((frame & 1) * 4)) & 15);
}
void sample(uint8 *b, int zone, int frame, int value) {
  const int p = audioBase + zone * zoneBytes + (frame / 2) * 3;
  const int shift = (frame & 1) * 4;
  b[p + (frame & 1)] = uint8(value >> 4);
  b[p + 2] = uint8((b[p + 2] & ~(15 << shift)) | ((value & 15) << shift));
}
MemoryBlock wav(const std::vector<int> &values) {
  // A lossless PCM container for portable kits. The native pitch rate can be
  // fractional and is restored from the bank metadata, not the WAV header.
  MemoryOutputStream out;
  out.write("RIFF", 4);
  out.writeInt(uint32(36 + values.size() * 2));
  out.write("WAVEfmt ", 8);
  out.writeInt(16);
  out.writeShort(1);
  out.writeShort(1);
  out.writeInt(rate);
  out.writeInt(rate * 2);
  out.writeShort(2);
  out.writeShort(16);
  out.write("data", 4);
  out.writeInt(uint32(values.size() * 2));
  for (int v : values)
    out.writeShort(short((v - 2048) * 16));
  return out.getMemoryBlock();
}
struct Group {
  int size = 0, zone = -1, start = 0;
  std::shared_ptr<const BankRegion> source;
  std::vector<int> pads;
};
using Ranges = std::array<std::vector<std::pair<int, int>>, 8>;
bool place(Group &g, Ranges &ranges) {
  for (int z = 7; z >= 0; --z) {
    auto &r = ranges[z];
    std::sort(r.begin(), r.end());
    int at = 0;
    for (const auto &[begin, end] : r) {
      if (at + g.size <= begin)
        break;
      at = (std::max(at, end) + 1) & ~1;
    }
    if (at + g.size <= 65535) {
      g.zone = z;
      g.start = at;
      r.emplace_back(at, at + g.size);
      return true;
    }
  }
  return false;
}
} // namespace

Project readBank(const MemoryBlock &bytes, String bankName) {
  validateBank(bytes);
  const auto *b = static_cast<const uint8 *>(bytes.getData());
  Project p;
  p.bankName = std::move(bankName);
  p.originalBank = std::make_shared<const MemoryBlock>(bytes);
  p.autoTune = p.autoTrim = p.autoSort = false;
  p.normalizeOnExport = p.fadeOutOnExport = false;
  std::map<std::tuple<int, int, int>, std::shared_ptr<const BankRegion>> regions;
  for (int i = 0; i < 32; ++i) {
    const int s = slot(i), d = 0x9262 + s * 10;
    const int a = u16(b, 0x91d4 + s * 4), n = u16(b, 0x91d6 + s * 4);
    if (!n)
      continue;
    const int z = b[d + 1] & 7;
    auto &region = regions[{z, a, n}];
    if (!region) {
      auto decoded = std::make_shared<BankRegion>();
      decoded->zone = z;
      decoded->start = a;
      decoded->values.reserve(n);
      for (int j = 0; j < n; ++j)
        decoded->values.push_back(sample(b, z, a + j));
      region = decoded;
    }
    auto native = std::make_shared<NativeSound>();
    native->region = region;
    native->sourcePad = i;
    native->offset = u16(b, d + 2) - a;
    native->span = u16(b, d + 4) ^ 65535;
    native->tune = b[d + 1] >> 3;
    native->flags = b[d];
    const int loopStart = u16(b, d + 6), loopSpan = u16(b, d + 8) ^ 65535;
    native->loopFrames = loopStart == 0 && loopSpan == 2 ? 0 : loopSpan;
    for (int j = 0; j < 6; ++j) {
      const int c = b[0x9114 + s * 6 + j] & 127;
      require(c >= 32 && c <= 126, "Invalid bank sound name on " + padName(i));
      native->name += String::charToString(char(c));
    }
    native->name = native->name.trimEnd();
    auto values = region->values;
    // The firmware saves five prefix words at the playback start. Each word
    // contains an upper byte and a low nibble (not left-aligned 16-bit PCM).
    for (int j = 0; j < std::min(5, n - native->offset); ++j) {
      const int c = 0x93a2 + s * 10 + j * 2;
      values[native->offset + j] = (b[c + 1] << 4) | (b[c] & 15);
    }
    auto asset = decode(wav(values), padName(i) + " " + native->name + ".wav",
                        padName(i) + ".wav");
    asset->sampleRate = native->sampleRate();
    asset->native = native;
    p.pool.push_back(asset);
    p.pads[i] = makePad(*asset, i, false, false);
  }
  return p;
}
Project loadBank(const File &file) {
  require(file.getSize() == bankSize, "Unsupported SP-1200 bank size (requires 824546 bytes).");
  MemoryBlock bytes;
  require(file.loadFileAsData(bytes), "Cannot read SP-1200 bank.");
  return readBank(bytes, file.getFileNameWithoutExtension());
}
bool retainNativeAudio(const Project &p, const Pad &pad, const Asset &a) {
  if (!a.native || pad.reverse || p.normalizeOnExport ||
      (p.fadeOutOnExport && !pad.loopEnabled) || (p.autoTrim && !pad.manualTrim))
    return false;
  const auto &n = *a.native;
  return std::abs(pad.start - n.start()) * a.sampleRate < .25 &&
         std::abs(pad.end - n.end()) * a.sampleRate < .25;
}
static bool sameNativeProcessing(const Project &p, int first, int second,
                                 const std::array<int, 32> &frames) {
  const auto &x = p.pads[first], &y = p.pads[second];
  const auto a = assetFor(p, x.asset), b = assetFor(p, y.asset);
  if (!a || !b || !a->native || !b->native ||
      a->native->region != b->native->region || a->original != b->original ||
      x.reverse != y.reverse || frames[first] != frames[second] ||
      (p.autoTrim && (!x.manualTrim || !y.manualTrim)) ||
      x.start < 0 || y.start < 0 || x.end < 0 || y.end < 0)
    return false;
  const int xb = int(std::llround(x.start * a->sampleRate));
  const int xe = int(std::llround(x.end * a->sampleRate));
  const int yb = int(std::llround(y.start * b->sampleRate));
  const int ye = int(std::llround(y.end * b->sampleRate));
  if (xb != yb || xe != ye || xe - xb != frames[first])
    return false; // Includes pitch conversion and capacity-limited selections.
  const bool fadeX = p.fadeOutOnExport && !x.loopEnabled;
  const bool fadeY = p.fadeOutOnExport && !y.loopEnabled;
  return fadeX == fadeY && (!fadeX || a->sampleRate == b->sampleRate);
}
BankStorage planNativeStorage(const Project &p, const std::array<int, 32> &frames) {
  std::vector<Group> groups;
  for (int i = 0; i < 32; ++i) {
    if (!frames[i])
      continue;
    const auto a = assetFor(p, p.pads[i].asset);
    require(a != nullptr, "Missing bank sample");
    const auto region = retainNativeAudio(p, p.pads[i], *a)
                            ? a->native->region : nullptr;
    auto found = std::find_if(groups.begin(), groups.end(), [&](const auto &g) {
      return region ? g.source == region
                    : !g.source && sameNativeProcessing(p, i, g.pads.front(), frames);
    });
    if (found != groups.end()) {
      found->pads.push_back(i);
      continue;
    }
    Group g;
    g.source = region;
    g.size = region ? int(region->values.size())
                   : (frames[i] + (a->native ? 11 : 13)) & ~1;
    g.pads.push_back(i);
    groups.push_back(std::move(g));
  }
  std::stable_sort(groups.begin(), groups.end(), [](const auto &a, const auto &b) {
    return a.size > b.size;
  });
  Ranges ranges;
  // Keep existing sample addresses whenever the new sounds fit into holes.
  for (auto &g : groups)
    if (g.source) {
      g.zone = g.source->zone;
      g.start = g.source->start;
      ranges[g.zone].emplace_back(g.start, g.start + g.size);
    }
  bool fits = true;
  for (auto &g : groups)
    if (!g.source && !place(g, ranges)) {
      fits = false;
      break;
    }
  if (!fits) {
    ranges = {};
    fits = true;
    for (auto &g : groups)
      if (!place(g, ranges)) {
        fits = false;
        break;
      }
  }
  if (!fits) {
    std::array<bool, 32> affected{};
    int64_t total = 0;
    for (const auto &g : groups) {
      total += g.size;
      for (int i : g.pads)
        affected[i] = true;
    }
    throw MemoryError("SP memory zone limit: edited sounds do not fit. Shorten or clear sounds.",
                      affected, std::max(int64_t(0), total - 8 * 65534), total);
  }
  BankStorage out;
  for (const auto &g : groups) {
    out.used[g.zone] = std::max(out.used[g.zone], g.start + g.size);
    for (int i : g.pads) {
      out.zones[i] = g.zone;
      out.starts[i] = g.start;
      out.sizes[i] = g.size;
    }
  }
  return out;
}
MemoryBlock buildNativeBank(const Project &p, const std::array<Render, 32> &sounds,
                            std::array<int, 8> &used) {
  require(p.originalBank && p.originalBank->getSize() == bankSize, "Missing original bank");
  std::array<int, 32> frames{};
  for (int i = 0; i < 32; ++i)
    frames[i] = int(sounds[i].values.size());
  const auto layout = planNativeStorage(p, frames);
  used = layout.used;
  auto bank = *p.originalBank;
  auto *b = static_cast<uint8 *>(bank.getData());
  const auto *original = static_cast<const uint8 *>(p.originalBank->getData());
  const auto *empty = reinterpret_cast<const uint8 *>(BinaryData::EMPTY__sp12);
  for (int i = 0; i < 32; ++i) {
    const int s = slot(i), d = 0x9262 + s * 10, prefix = 0x93a2 + s * 10;
    const auto &pad = p.pads[i];
    const auto a = assetFor(p, pad.asset);
    const auto &r = sounds[i];
    if (!frames[i]) {
      if (u16(original, 0x91d6 + s * 4)) {
        for (auto [base, width] : {std::pair{0x9114, 6}, {0x91d4, 4},
                                   {0x9262, 10}, {0x93a2, 10}})
          std::copy_n(empty + base + s * width, width, b + base + s * width);
        b[0x9260] = 255;
      }
      continue;
    }
    require(a && pad.channel >= 1 && pad.channel <= 8 && r.tune >= 0 && r.tune <= 31,
            "Invalid bank sound settings");
    require(pad.name.length() <= 6 && pad.name.containsOnly(
                " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"),
            "Hardware names must contain up to six printable ASCII characters");
    const int z = layout.zones[i], at = layout.starts[i], size = layout.sizes[i];
    const bool raw = retainNativeAudio(p, pad, *a);
    if (a->native) {
      const int sourceSlot = slot(a->native->sourcePad);
      for (auto [base, width] : {std::pair{0x9114, 6}, {0x91d4, 4},
                                 {0x9262, 10}, {0x93a2, 10}})
        std::copy_n(original + base + sourceSlot * width, width, b + base + s * width);
      if (sourceSlot != s)
        b[0x9260] = 255;
    } else {
      // A replacement WAV gets the same sound defaults as a fresh app bank.
      for (auto [base, width] : {std::pair{0x9114, 6}, {0x9262, 10}, {0x93a2, 10}})
        std::copy_n(empty + base + s * width, width, b + base + s * width);
    }
    u16(b, 0x91d4 + s * 4, at);
    u16(b, 0x91d6 + s * 4, size);
    if (raw) {
      const auto &n = *a->native;
      const auto &region = *n.region;
      if (z != region.zone || at != region.start)
        for (int j = 0; j < size; ++j)
          sample(b, z, at + j, region.values[j]);
      u16(b, d + 2, at + n.offset);
      const bool originalLoop = pad.loopEnabled == (n.loopFrames > 0) &&
          (!pad.loopEnabled || std::abs(pad.loopSeconds - n.loopSeconds()) * a->sampleRate < .25);
      if (originalLoop) {
        if (n.loopFrames)
          u16(b, d + 6, at + n.offset + n.span - n.loopFrames);
      } else {
        writeLoopDescriptor(bank, i, loopFrameCount(pad, r));
      }
    } else {
      // Edited native audio keeps counter coordinates: its cached first five
      // frames are the attack. Newly imported WAVs retain the existing guards.
      const int leading = a->native ? 0 : 5;
      for (int j = 0; j < size; ++j) {
        const int index = j - leading;
        int value = index >= 0 && index < frames[i] ? r.values[index] : 2048;
        require(value >= 0 && value <= 4095, "Invalid 12-bit bank audio");
        if (j < 5)
          value = 2048;
        sample(b, z, at + j, value);
      }
      for (int j = 0; j < 5; ++j) {
        const int v = a->native && j < frames[i] ? r.values[j] : 2048;
        b[prefix + j * 2] = uint8(v & 15);
        b[prefix + j * 2 + 1] = uint8(v >> 4);
      }
      u16(b, d + 2, at);
      u16(b, d + 4, (a->native ? frames[i] : size - 10) ^ 65535);
      const int storedTune = a->native && (a->native->flags & 128) ? a->native->tune : r.tune;
      b[d + 1] = uint8(storedTune << 3 | z);
      u16(b, 0x91d4 + s * 4, at);
      u16(b, 0x91d6 + s * 4, size);
      writeLoopDescriptor(bank, i, loopFrameCount(pad, r));
      b[0x9260] = 255;
    }
    u16(b, 0x91d4 + s * 4, at);
    u16(b, 0x91d6 + s * 4, size);
    b[d + 1] = uint8((b[d + 1] & 0xf8) | z);
    b[d] = uint8((b[d] & 0xf8) | (pad.channel - 1));
    if (!a->native || pad.name != a->native->name) {
      const auto name = pad.name.paddedRight(' ', 6);
      for (int j = 0; j < 6; ++j)
        b[0x9114 + s * 6 + j] = uint8(name[j] | (b[0x9114 + s * 6 + j] & 128));
    }
  }
  for (int z = 0; z < 8; ++z)
    u16(b, counters[z], used[z]);
  validateBank(bank);
  return bank;
}
} // namespace sp
