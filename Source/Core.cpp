// SPDX-License-Identifier: AGPL-3.0-only
// Copyright © 2026 Numerisch GmbH

#include "Core.h"
#include "BinaryData.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
namespace sp {
using namespace juce;
static void need(bool ok, const String &message) {
  if (!ok)
    throw std::runtime_error(message.toStdString());
}
String padName(int p) {
  return String::charToString(char('A' + p / 8)) + String(p % 8 + 1);
}
String bankFileStem(String name) {
  // Handle both composed and decomposed umlauts from macOS filenames.
  const char *from[] = {"ä", "ö", "ü", "Ä", "Ö", "Ü", "ß",
                        "ẞ", "ä", "ö", "ü", "Ä", "Ö", "Ü"};
  const char *to[] = {"ae", "oe", "ue", "Ae", "Oe", "Ue", "ss",
                      "SS", "ae", "oe", "ue", "Ae", "Oe", "Ue"};
  for (size_t i = 0; i < std::size(from); ++i)
    name = name.replace(String::fromUTF8(from[i]), to[i]);
  name = name.retainCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstu"
                               "vwxyz0123456789 _-")
             .trim()
             .substring(0, 16)
             .trim();
  return name.isEmpty() ? String("KIT") : name;
}
static const StringArray kinds{"BD",         "SD",      "Clap",     "Rim",
                               "CH",         "OH",      "Ride",     "Crash",
                               "Low Tom",    "Mid Tom", "High Tom", "Low Conga",
                               "High Conga", "Shaker",  "Tamb",     "Cowbell"};
String padInstrument(const Project &p, int i) {
  if (!p.autoSort)
    return {};
  static const StringArray labels{
      "BD",          "SD",        "Clap",       "Rim",     "Closed Hi-Hat",
      "Open Hi-Hat", "Ride",      "Crash",      "Low Tom", "Mid Tom",
      "High Tom",    "Low Conga", "High Conga", "Shaker",  "Tambourine",
      "Cowbell"};
  if (i < 16)
    return labels[i];
  return i < 24 ? labels[i - 16] : String("Other");
}
String classify(String name) {
  auto tokens = StringArray::fromTokens(
      name.toLowerCase().replaceCharacters("_-.", "   "), " ", "");
  auto has = [&](std::initializer_list<const char *> words) {
    for (auto w : words)
      if (tokens.contains(w))
        return true;
    return false;
  };
  if (has({"oh", "ohh", "hho"}) ||
      (has({"open"}) && has({"hh", "hat", "hihat"})))
    return "OH";
  if (has({"ch", "chh"}) ||
      (has({"closed", "close"}) && has({"hh", "hat", "hihat"})))
    return "CH";
  if (has({"bd", "kick", "bassdrum"}))
    return "BD";
  if (has({"rim", "rimshot", "rs", "sidestick", "crossstick"}) ||
      (has({"side", "cross"}) && has({"stick"})))
    return "Rim";
  if (has({"sd", "snare"}))
    return "SD";
  if (has({"clap", "cp", "clp"}))
    return "Clap";
  if (has({"ride", "rd"}))
    return "Ride";
  if (has({"crash", "cr", "cym", "cymbal", "china", "chinese"}))
    return "Crash";
  if (has({"tom", "toms", "lt", "mt", "ht"})) {
    if (has({"low", "lo", "floor", "lt"}))
      return "Low Tom";
    if (has({"high", "hi", "ht"}))
      return "High Tom";
    if (has({"mid", "medium", "mt"}))
      return "Mid Tom";
    return "Tom";
  }
  if (has({"conga", "congas"})) {
    if (has({"low", "lo"}))
      return "Low Conga";
    if (has({"high", "hi"}))
      return "High Conga";
    return "Conga";
  }
  if (has({"shaker", "cabasa"}) ||
      (has({"perc", "percussion"}) && has({"shake"})))
    return "Shaker";
  if (has({"tamb", "tambourine"}))
    return "Tamb";
  if (has({"cowbell", "cb"}))
    return "Cowbell";
  if (has({"bongo", "bongos"}))
    return "Bongo";
  if (has({"timbale", "timbales"}))
    return "Timbale";
  if (has({"hh", "hat", "hihat"}))
    return "CH";
  if (has({"perc", "percussion"}))
    return "Percussion";
  return "Other";
}
std::shared_ptr<Asset> decode(const MemoryBlock &bytes, String filename,
                              String relative) {
  AudioFormatManager formats;
  formats.registerBasicFormats();
  std::unique_ptr<AudioFormatReader> reader(formats.createReaderFor(
      std::make_unique<MemoryInputStream>(bytes, false)));
  need(reader != nullptr, "Cannot decode " + filename);
  need(reader->numChannels >= 1 && reader->numChannels <= 2 &&
           reader->lengthInSamples > 0 && reader->lengthInSamples <= INT_MAX,
       "Unsupported channel count or length: " + filename);
  auto a = std::make_shared<Asset>();
  a->id = Uuid().toString();
  a->filename = filename;
  a->relative = relative;
  a->category = classify(File(filename).getFileNameWithoutExtension());
  a->original = bytes;
  a->sampleRate = reader->sampleRate;
  need(std::isfinite(a->sampleRate) && a->sampleRate >= 1000 &&
           a->sampleRate <= 768000,
       "Unsupported sample rate");
  a->audio.setSize((int)reader->numChannels, (int)reader->lengthInSamples);
  need(reader->read(&a->audio, 0, a->audio.getNumSamples(), 0, true, true),
       "Read failed: " + filename);
  for (int c = 0; c < a->audio.getNumChannels(); ++c)
    for (int i = 0; i < a->audio.getNumSamples(); ++i)
      if (!std::isfinite(a->audio.getSample(c, i)))
        need(false, "Non-finite audio: " + filename);
  return a;
}
std::shared_ptr<Asset> assetFor(const Project &p, const String &id) {
  for (auto a : p.pool)
    if (a->id == id)
      return a;
  return {};
}
static int maxSelectionFrames(double sampleRate, bool autoTune) {
  // Round down on the source grid so export resampling never exceeds capacity.
  return std::max(1, (int)std::floor(maxFrames * sampleRate / rate /
                                     (autoTune ? pitchRatio(0) : 1.0)));
}
double maxSelectionDuration(double sampleRate, bool autoTune) {
  return maxSelectionFrames(sampleRate, autoTune) / sampleRate;
}
bool exceedsSampleDuration(const Asset &asset, bool autoTune) {
  return asset.audio.getNumSamples() >
         maxSelectionFrames(asset.sampleRate, autoTune);
}
static void resetTrimBounds(const Project &p, Pad &pad) {
  pad.manualTrim = false;
  pad.start = pad.end = -1;
  if (!p.autoTrim)
    if (const auto asset = assetFor(p, pad.asset)) {
      pad.start = 0;
      pad.end = std::min(asset->audio.getNumSamples() / asset->sampleRate,
                         maxSelectionDuration(asset->sampleRate, p.autoTune));
    }
}
void resetTrim(Project &p, int index) {
  resetTrimBounds(p, p.pads.at(index));
  p.pads.at(index).loopSeconds = -1;
}
static void resetAutomaticTrimBounds(Project &p) {
  auto reset = [&](auto &pads) {
    for (auto &pad : pads)
      if (!pad.manualTrim && pad.asset.isNotEmpty())
        resetTrimBounds(p, pad);
  };
  reset(p.pads);
  if (p.autoMapRestore)
    reset(p.autoMapRestore->previous);
}
void setAutoTune(Project &p, bool enabled) {
  syncAutoMapRestore(p);
  p.autoTune = enabled;
  resetAutomaticTrimBounds(p);
}
void setAutoTrim(Project &p, bool enabled) {
  syncAutoMapRestore(p);
  p.autoTrim = enabled;
  resetAutomaticTrimBounds(p);
}
double clampTrimPosition(const Asset &a, bool isStart, double requestedTime,
                         double oppositeBoundary, bool autoTune) {
  const int n = a.audio.getNumSamples();
  const int anchor =
      std::clamp((int)std::llround(oppositeBoundary * a.sampleRate),
                 isStart ? 1 : 0, isStart ? n : n - 1);
  const int requested = (int)std::llround(requestedTime * a.sampleRate);
  const int maximum = maxSelectionFrames(a.sampleRate, autoTune);
  const int low = isStart ? std::max(0, anchor - maximum) : anchor + 1;
  const int high = isStart ? anchor - 1 : std::min(n, anchor + maximum);
  return std::clamp(requested, low, high) / a.sampleRate;
}
juce::Range<double> shiftTrimSelection(const Asset &a, double start, double end,
                                       double offset) {
  const int n = a.audio.getNumSamples();
  const int first =
      std::clamp((int)std::llround(start * a.sampleRate), 0, n - 1);
  const int last =
      std::clamp((int)std::llround(end * a.sampleRate), first + 1, n);
  const int shift = (int)std::clamp(std::round(offset * a.sampleRate),
                                    -double(first), double(n - last));
  return {(first + shift) / a.sampleRate, (last + shift) / a.sampleRate};
}
Pad makePad(const Asset &a, int padIndex, bool autoTrim, bool autoTune) {
  Pad p;
  p.assignmentId = Uuid().toString();
  p.asset = a.id;
  p.name = a.filename.upToFirstOccurrenceOf(".", false, false)
               .toUpperCase()
               .retainCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ")
               .substring(0, 6)
               .trim();
  if (p.name.isEmpty())
    p.name = "SOUND";
  p.channel = padIndex % 8 + 1;
  if (!autoTrim &&
      a.audio.getNumSamples() > maxSelectionFrames(a.sampleRate, autoTune)) {
    p.start = 0;
    p.end = maxSelectionDuration(a.sampleRate, autoTune);
  }
  return p;
}
static void ensureAssignmentIds(std::array<Pad, 32> &pads) {
  StringArray seen;
  for (auto &pad : pads) {
    if (pad.asset.isEmpty()) {
      pad.assignmentId.clear();
      continue;
    }
    if (pad.assignmentId.isEmpty() || seen.contains(pad.assignmentId))
      pad.assignmentId = Uuid().toString();
    seen.add(pad.assignmentId);
  }
}
static void ensureAutoMapRestore(Project &p) {
  if (p.autoSort && !p.autoMapRestore) {
    ensureAssignmentIds(p.pads);
    p.autoMapRestore = AutoMapState{p.pads, {}};
  }
}
void syncAutoMapRestore(Project &p) {
  if (!p.autoMapRestore)
    return;
  for (auto &previous : p.autoMapRestore->previous)
    if (previous.asset.isNotEmpty())
      for (const auto &current : p.pads)
        if (current.assignmentId == previous.assignmentId) {
          previous = current;
          break;
        }
}
void retainMappedAssignment(Project &p, int i) {
  if (p.autoMapRestore)
    p.autoMapRestore->transient.removeString(p.pads.at(i).assignmentId);
}
void clearPad(Project &p, int i) {
  ensureAutoMapRestore(p);
  const auto id = p.pads.at(i).assignmentId;
  if (p.autoMapRestore && id.isNotEmpty()) {
    for (int j = 0; j < 32; ++j)
      if (p.autoMapRestore->previous[j].assignmentId == id)
        p.autoMapRestore->previous[j] = Project{}.pads[j];
    p.autoMapRestore->transient.removeString(id);
  }
  p.pads[i] = Project{}.pads[i];
}
void assignPad(Project &p, int i, const Asset &a) {
  clearPad(p, i);
  p.pads[i] = makePad(a, i, p.autoTrim, p.autoTune);
}
void clearKit(Project &p) {
  p.pool.clear();
  p.pads = Project{}.pads;
  p.bankName.clear();
  p.autoMapRestore.reset();
  ensureAutoMapRestore(p);
}
static void fillEmptyPads(Project &p,
                          std::vector<std::shared_ptr<Asset>> sorted,
                          bool classify, bool fillAllGaps) {
  if (!classify) {
    size_t next = 0;
    for (int i = 0; i < 32 && next < sorted.size(); ++i)
      if (p.pads[i].asset.isEmpty())
        p.pads[i] = makePad(*sorted[next++], i, p.autoTrim, p.autoTune);
    return;
  }
  std::stable_sort(sorted.begin(), sorted.end(), [](auto a, auto b) {
    return a->relative.compare(b->relative) < 0;
  });
  std::vector<std::shared_ptr<Asset>> rest;
  auto assign = [&](int i, std::shared_ptr<Asset> a) {
    p.pads[i] = makePad(*a, i, p.autoTrim, p.autoTune);
  };
  for (auto a : sorted) {
    int i = kinds.indexOf(a->category);
    // A and C share instrument positions: first hit on A, second on C.
    if (i >= 0 && i < 8 && p.pads[i].asset.isNotEmpty())
      i += 16;
    if (a->category == "Tom") {
      i = 8;
      while (i <= 10 && p.pads[i].asset.isNotEmpty())
        ++i;
      if (i > 10)
        i = -1;
    }
    if (a->category == "Conga") {
      i = 11;
      while (i <= 12 && p.pads[i].asset.isNotEmpty())
        ++i;
      if (i > 12)
        i = -1;
    }
    if (i >= 0 && p.pads[i].asset.isEmpty())
      assign(i, a);
    else
      rest.push_back(a);
  }
  auto order = [](String k) {
    int i = kinds.indexOf(k);
    return i >= 0              ? i
           : k == "Tom"        ? 8
           : k == "Conga"      ? 11
           : k == "Bongo"      ? 16
           : k == "Timbale"    ? 17
           : k == "Percussion" ? 18
                               : 99;
  };
  std::stable_sort(rest.begin(), rest.end(), [&](auto a, auto b) {
    return order(a->category) < order(b->category);
  });
  size_t next = 0;
  for (int i = 24; i < 32 && next < rest.size(); ++i)
    if (p.pads[i].asset.isEmpty())
      assign(i, rest[next++]);
  if (fillAllGaps)
    for (int i = 0; i < 16 && next < rest.size(); ++i)
      if (p.pads[i].asset.isEmpty())
        assign(i, rest[next++]);
}
void sortPads(Project &p) {
  ensureAssignmentIds(p.pads);
  ensureAutoMapRestore(p);
  syncAutoMapRestore(p);
  const auto previous = p.pads;
  p.pads = Project{}.pads;
  fillEmptyPads(p, p.pool, true, false);
  for (auto &mapped : p.pads) {
    if (mapped.asset.isEmpty())
      continue;
    auto old = std::find_if(previous.begin(), previous.end(), [&](auto &pad) {
      return pad.asset == mapped.asset;
    });
    if (old != previous.end())
      mapped = *old;
    else if (p.autoMapRestore)
      p.autoMapRestore->transient.add(mapped.assignmentId);
  }
  if (p.autoMapRestore)
    for (int i = p.autoMapRestore->transient.size(); --i >= 0;)
      if (std::none_of(p.pads.begin(), p.pads.end(), [&](const auto &pad) {
            return pad.assignmentId == p.autoMapRestore->transient[i];
          }))
        p.autoMapRestore->transient.remove(i);
}
void setAutoMap(Project &p, bool enabled) {
  if (enabled == p.autoSort) {
    ensureAutoMapRestore(p);
    return;
  }
  if (enabled) {
    ensureAssignmentIds(p.pads);
    p.autoMapRestore = AutoMapState{p.pads, {}};
    p.autoSort = true;
    sortPads(p);
    return;
  }
  syncAutoMapRestore(p);
  if (p.autoMapRestore) {
    auto restored = p.autoMapRestore->previous;
    std::vector<Pad> added;
    for (const auto &pad : p.pads)
      if (pad.asset.isNotEmpty() &&
          !p.autoMapRestore->transient.contains(pad.assignmentId) &&
          std::none_of(restored.begin(), restored.end(), [&](const auto &old) {
            return old.assignmentId == pad.assignmentId;
          }))
        added.push_back(pad);
    auto poolIndex = [&](const Pad &pad) {
      return std::find_if(p.pool.begin(), p.pool.end(),
                          [&](auto a) { return a->id == pad.asset; }) -
             p.pool.begin();
    };
    std::stable_sort(added.begin(), added.end(),
                     [&](const auto &a, const auto &b) {
                       return poolIndex(a) < poolIndex(b);
                     });
    size_t next = 0;
    for (auto &pad : restored)
      if (pad.asset.isEmpty() && next < added.size())
        pad = added[next++];
    p.pads = std::move(restored);
  }
  p.autoMapRestore.reset();
  p.autoSort = false;
}
void appendSamples(Project &p,
                   const std::vector<std::shared_ptr<Asset>> &samples) {
  ensureAutoMapRestore(p);
  p.pool.insert(p.pool.end(), samples.begin(), samples.end());
  fillEmptyPads(p, samples, p.autoSort, true);
}
Project importFiles(const StringArray &paths, StringArray &warnings,
                    bool autoSort) {
  struct Entry {
    File file;
    String relative, folderName;
  };
  std::vector<Entry> entries;
  for (auto path : paths) {
    File f(path);
    if (f.isDirectory()) {
      auto children = f.findChildFiles(File::findFiles, true);
      std::sort(
          children.begin(), children.end(), [&f](const File &a, const File &b) {
            return a.getRelativePathFrom(f).compare(b.getRelativePathFrom(f)) <
                   0;
          });
      for (auto child : children)
        entries.push_back(
            {child, child.getRelativePathFrom(f), f.getFileName()});
    } else
      entries.push_back(
          {f, f.getFileName(), f.getParentDirectory().getFileName()});
  }
  Project p;
  p.autoSort = autoSort;
  StringArray seen;
  for (auto &e : entries) {
    if (seen.contains(e.file.getFullPathName()))
      continue;
    seen.add(e.file.getFullPathName());
    if (e.file.getFileName().startsWithChar('.'))
      continue;
    if (!e.file.hasFileExtension("wav;aif;aiff")) {
      warnings.add(e.relative + ": unsupported file");
      continue;
    }
    try {
      MemoryBlock bytes;
      need(e.file.loadFileAsData(bytes), "Cannot read file");
      p.pool.push_back(decode(bytes, e.file.getFileName(), e.relative));
      if (p.pool.size() == 1)
        p.bankName = e.folderName;
    } catch (const std::exception &ex) {
      warnings.add(e.relative + ": " + ex.what());
    }
  }
  need(!p.pool.empty(), "No readable WAV or AIFF samples found.");
  if (p.autoSort) {
    sortPads(p);
    // These are newly imported assignments, not a temporary re-sort of a kit.
    p.autoMapRestore->transient.clear();
  } else
    for (int i = 0; i < std::min(32, (int)p.pool.size()); ++i)
      p.pads[i] = makePad(*p.pool[i], i, p.autoTrim, p.autoTune);
  return p;
}
double pitchRatio(int t) {
  static constexpr int table[] = {50,  53,  56,  59,  63,  67,  71,  75,
                                  80,  84,  90,  95,  101, 107, 113, 120,
                                  127, 135, 143, 151, 160, 170, 180, 191,
                                  202, 214, 227, 241, 255, 255, 255, 255};
  need(t >= 0 && t < 32, "Invalid tune");
  return (table[t] + 1) / 128.;
}
static std::vector<double> mono(const Asset &a, bool &left) {
  auto n = a.audio.getNumSamples();
  std::vector<double> x(n);
  left = false;
  if (a.audio.getNumChannels() == 1) {
    std::copy_n(a.audio.getReadPointer(0), n, x.begin());
    return x;
  }
  double l = 0, r = 0, m = 0;
  for (int i = 0; i < n; ++i) {
    double v = a.audio.getSample(0, i),
           w = a.audio.getSample(a.audio.getNumChannels() - 1, i);
    x[i] = (v + w) * .5;
    l += v * v;
    r += w * w;
    m += x[i] * x[i];
  }
  left = a.audio.getNumChannels() == 2 &&
         std::sqrt(std::max(l, r) / n) > 1e-6 && m < .0625 * std::max(l, r);
  if (left)
    for (int i = 0; i < n; ++i)
      x[i] = a.audio.getSample(0, i);
  return x;
}
// Windowed-sinc low-pass resampling; cutoff follows the conversion ratio.
static std::vector<double> resample(const std::vector<double> &x, int begin,
                                    int end, double ratio, int limit,
                                    const CancelCheck &cancel) {
  int count = std::min(limit, (int)std::ceil((end - begin) * ratio));
  std::vector<double> out(std::max(1, count));
  double cutoff = std::min(1., ratio) * .94;
  int radius = (int)std::ceil(32 / cutoff);
  for (int i = 0; i < count; ++i) {
    if ((i & 127) == 0 && cancel && cancel())
      throw ProcessingCancelled{};
    double pos = begin + i / ratio, sum = 0, weight = 0;
    int centre = (int)std::floor(pos);
    for (int j = centre - radius; j <= centre + radius; ++j) {
      double d = j - pos, u = d / radius;
      if (std::abs(u) >= 1)
        continue;
      double sinc = std::abs(d) < 1e-12
                        ? cutoff
                        : std::sin(MathConstants<double>::pi * d * cutoff) /
                              (MathConstants<double>::pi * d);
      double win = .42 + .5 * std::cos(MathConstants<double>::pi * u) +
                   .08 * std::cos(2 * MathConstants<double>::pi * u);
      double w = sinc * win;
      weight += w;
      if (j >= begin && j < end)
        sum += x[j] * w;
    }
    out[i] = weight != 0 ? sum / weight : 0;
  }
  return out;
}
static int allocation(int n) { return (n + 13) & ~1; }
static bool allocate(const std::array<int, 32> &lengths,
                     std::array<int, 8> &used,
                     std::array<bool, 32> *conflicts = nullptr,
                     std::array<int, 32> *zones = nullptr,
                     std::array<int, 32> *starts = nullptr) {
  used = {};
  if (conflicts)
    *conflicts = {};
  bool fits = true;
  std::array<int, 32> order;
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(),
                   [&](int a, int b) { return lengths[a] > lengths[b]; });
  for (int i : order) {
    if (!lengths[i])
      continue;
    int z = 7;
    while (z >= 0 && used[z] + allocation(lengths[i]) > 65535)
      --z;
    if (z < 0) {
      if (!conflicts)
        return false;
      (*conflicts)[i] = true;
      fits = false;
      continue;
    }
    if (zones)
      (*zones)[i] = z;
    if (starts)
      (*starts)[i] = used[z];
    used[z] += allocation(lengths[i]);
  }
  return fits;
}
static String memorySeconds(int64_t frames) {
  // Round up so a real excess never appears as 0.00 s.
  return String(std::ceil(frames * 100.0 / rate) / 100.0, 2) + " s";
}
static std::pair<int, int> trimRange(const Project &p, const Pad &pad,
                                     const std::vector<double> &x, double sr) {
  int b = 0, e = (int)x.size();
  if (p.autoTrim && !pad.manualTrim) {
    double threshold = std::pow(10., p.threshold / 20.);
    while (b < e && std::abs(x[b]) <= threshold)
      ++b;
    while (e > b && std::abs(x[e - 1]) <= threshold)
      --e;
    if (b == e) {
      b = 0;
      bool silent =
          std::all_of(x.begin(), x.end(), [](double v) { return v == 0; });
      e = silent ? std::min((int)x.size(), std::max(1, (int)(.002 * sr)))
                 : (int)x.size();
    } else {
      b = std::max(0, b - (int)(.002 * sr));
      e = std::min((int)x.size(), e + (int)(.002 * sr));
    }
  } else {
    if (pad.start >= 0)
      b = std::clamp((int)std::llround(pad.start * sr), 0, (int)x.size() - 1);
    if (pad.end >= 0)
      e = std::clamp((int)std::llround(pad.end * sr), b + 1, (int)x.size());
  }
  if (e <= b)
    e = b + 1;
  return {b, e};
}
// Lightweight fallback for invalid banks, calculated on the audio worker.
std::array<Render, 32> selectionMetadata(const Project &p) {
  std::array<Render, 32> metadata;
  for (int i = 0; i < 32; ++i) {
    const auto a = assetFor(p, p.pads[i].asset);
    if (!a)
      continue;
    auto &r = metadata[i];
    auto x = mono(*a, r.left);
    if (p.pads[i].reverse)
      std::reverse(x.begin(), x.end());
    const auto [b, e] = trimRange(p, p.pads[i], x, a->sampleRate);
    r.start = b / a->sampleRate;
    r.end = e / a->sampleRate;
    while (p.autoTune && r.tune > 0 &&
           std::ceil((e - b) * double(rate) / a->sampleRate *
                     pitchRatio(r.tune)) > maxFrames)
      --r.tune;
    if (p.autoTrim && !p.pads[i].manualTrim)
      r.removed = std::max(
          0., r.end - r.start - maxFrames / double(rate) / pitchRatio(r.tune));
  }
  return metadata;
}
// Derived corrections join the initiating UI edit; the full-selection sentinel
// deliberately remains a sentinel so later expansion follows the selection.
void clampLoopLengths(Project &p, const std::array<Render, 32> &sounds) {
  for (int i = 0; i < 32; ++i) {
    auto &pad = p.pads[i];
    if (pad.asset.isNotEmpty() && pad.loopSeconds >= 0)
      pad.loopSeconds = std::min(
          pad.loopSeconds,
          std::max(0., sounds[i].end - sounds[i].start - sounds[i].removed));
  }
}
void clampAutoMapRestoreLoops(Project &p) {
  syncAutoMapRestore(p);
  if (!p.autoMapRestore)
    return;
  auto hidden = p;
  hidden.pads = Project{}.pads;
  hidden.autoMapRestore.reset();
  bool hasLoops = false;
  for (int i = 0; i < 32; ++i) {
    const auto &pad = p.autoMapRestore->previous[i];
    if (pad.asset.isNotEmpty() && pad.loopSeconds >= 0 &&
        std::none_of(p.pads.begin(), p.pads.end(), [&](const auto &current) {
          return current.assignmentId == pad.assignmentId;
        })) {
      hidden.pads[i] = pad;
      hasLoops = true;
    }
  }
  if (!hasLoops)
    return;
  clampLoopLengths(hidden, selectionMetadata(hidden));
  for (int i = 0; i < 32; ++i)
    if (hidden.pads[i].asset.isNotEmpty())
      p.autoMapRestore->previous[i].loopSeconds = hidden.pads[i].loopSeconds;
}
double loopSecondsAfterTrim(double oldStart, double oldEnd, double seconds,
                            double newStart, double newEnd, double sampleRate) {
  if (seconds < 0)
    return -1; // The full-selection default keeps following the start handle.
  const double minimum = std::min(1. / sampleRate, newEnd - newStart);
  if (std::abs(newStart - oldStart) < .5 / sampleRate) {
    const double point =
        std::clamp(oldEnd - seconds, newStart, newEnd - minimum);
    return newEnd -
           point; // Moving only End leaves the loop's source point fixed.
  }
  // Moving Start clamps against it; moving the entire selection keeps its
  // length.
  return std::min(seconds, newEnd - newStart);
}
static int loopFramesFor(const Pad &pad, int n, int tune) {
  if (!pad.loopEnabled || n == 0)
    return 0;
  if (pad.loopSeconds < 0)
    return n;
  return (int)std::clamp(std::round(pad.loopSeconds * rate * pitchRatio(tune)),
                         1., double(n));
}
int loopFrameCount(const Pad &pad, const Render &r) {
  if (!pad.loopEnabled)
    return 0;
  if (r.loopStart >= 0)
    return r.loopFrames;
  return loopFramesFor(pad, (int)r.values.size(), r.tune);
}
static ProcessingPlan planAudio(const Project &p, bool preview) {
  ProcessingPlan out;
  auto &begin = out.begin;
  auto &end = out.end;
  auto &lengths = out.frames;
  std::array<int, 32> caps{};
  std::array<double, 32> rates{};
  std::array<bool, 32> oversized{};
  int64_t sampleExcess = 0;
  for (int i = 0; i < 32; ++i) {
    if (p.pads[i].asset.isEmpty())
      continue;
    auto a = assetFor(p, p.pads[i].asset);
    need(a != nullptr, "Missing sample");
    auto &r = out.sounds[i];
    auto x = mono(*a, r.left);
    if (p.pads[i].reverse)
      std::reverse(x.begin(), x.end());
    rates[i] = a->sampleRate;
    const auto [b, e] = trimRange(p, p.pads[i], x, rates[i]);
    begin[i] = b;
    end[i] = e;
    r.start = b / rates[i];
    r.end = e / rates[i];
    // Auditioning a pad is independent of bank capacity. Keep manual cuts,
    // including protected selections beyond the current slot limit.
    caps[i] = preview && (!p.autoTrim || p.pads[i].manualTrim)
                  ? std::numeric_limits<int>::max()
                  : maxFrames;
    while (p.autoTune && r.tune > 0 &&
           std::ceil((e - b) * double(rate) / rates[i] * pitchRatio(r.tune)) >
               maxFrames)
      --r.tune;
    const double requested =
        std::ceil((e - b) * double(rate) / rates[i] * pitchRatio(r.tune));
    oversized[i] =
        (!p.autoTrim || p.pads[i].manualTrim) && requested > maxFrames;
    if (oversized[i])
      sampleExcess += (int64_t)requested - maxFrames;
    lengths[i] = (int)std::min(double(caps[i]), requested);
  }
  if (!preview && sampleExcess > 0)
    throw MemoryError(
        "Sample exceeds SP memory: " + memorySeconds(sampleExcess) +
            " over sample limits. Shorten, enable Pitch Fit, or Reset Trim.",
        oversized, sampleExcess);
  for (int i = 0; i < 32; ++i) {
    if (!lengths[i])
      continue;
    auto &r = out.sounds[i];
    // Match the resampler's arithmetic exactly. Reordering floating-point
    // division/multiplication can change ceil by one frame at integral ratios.
    lengths[i] = std::min(
        caps[i], (int)std::ceil((end[i] - begin[i]) *
                                (rate / rates[i] * pitchRatio(r.tune))));
    r.removed =
        std::max(0., (end[i] - begin[i]) / rates[i] -
                         lengths[i] / double(rate) / pitchRatio(r.tune));
    r.loopFrames = loopFramesFor(p.pads[i], lengths[i], r.tune);
  }
  if (!preview && !allocate(lengths, out.used)) {
    std::array<bool, 32> conflicts{};
    allocate(lengths, out.used, &conflicts);
    int64_t required = 0, unplaced = 0;
    for (int i = 0; i < 32; ++i) {
      if (lengths[i])
        required += allocation(lengths[i]);
      if (conflicts[i])
        unplaced += lengths[i];
    }
    // Auto Trim removes silence and enforces individual sample limits. Bank
    // pressure must never silently shorten audible audio, including auto pads.
    const int64_t capacity = 8 * 65534;
    const int64_t excess = std::max(int64_t(0), required - capacity);
    const auto message =
        excess > 0 ? "Kit exceeds SP memory: " + memorySeconds(excess) +
                         " over capacity. Shorten or clear samples. "
                         "Auto Trim removes silence only."
                   : "SP memory zone limit: " + memorySeconds(unplaced) +
                         " unplaced; " +
                         String((capacity - std::accumulate(out.used.begin(),
                                                            out.used.end(),
                                                            int64_t(0))) /
                                    double(rate),
                                2) +
                         " s free across zones. Shorten or clear samples.";
    throw MemoryError(message, conflicts, excess, unplaced);
  }
  return out;
}
ProcessingPlan planBank(const Project &p) {
  auto plan = planAudio(p, false);
  for (int i = 0; i < 32; ++i)
    if (plan.sounds[i].loopFrames > 0 && plan.sounds[i].loopFrames < 3) {
      LoopError error(
          "Loop needs at least 3 stored frames within the selection. "
          "Extend the loop or turn Loop Off to export.");
      error.selections = std::make_shared<std::array<Render, 32>>(plan.sounds);
      error.plan = std::make_shared<ProcessingPlan>(plan);
      throw error;
    }
  return plan;
}
ProcessingPlan planPadPreview(const Project &p, int padIndex) {
  need(padIndex >= 0 && padIndex < 32, "Invalid preview pad");
  auto single = p;
  single.pads = Project{}.pads;
  single.pads[padIndex] = p.pads[padIndex];
  return planAudio(single, true);
}
// A crossing is a rising edge (or digital silence). The search is bounded in
// compensated playback time and runs on already resampled mono, never on the
// audio callback or on all inactive pads. End may only move inward.
static int nearestCrossing(const std::vector<double> &audio, int requested,
                           int lower, int upper, int radius) {
  const int n = (int)audio.size();
  lower = std::max(lower, requested - radius);
  upper = std::min(upper, requested + radius);
  auto crossing = [&](int i) {
    if (i == n)
      return audio.back() == 0; // Keep an already silent endpoint intact.
    if (i == 0)
      return audio[0] == 0;
    return (audio[i - 1] < 0 && audio[i] >= 0) ||
           (audio[i - 1] == 0 && audio[i] == 0);
  };
  for (int distance = 0; distance <= radius; ++distance) {
    const int before = requested - distance, after = requested + distance;
    if (before >= lower && before <= upper && crossing(before))
      return before;
    if (distance && after >= lower && after <= upper && crossing(after))
      return after;
  }
  return requested;
}
static std::vector<double> renderAudio(const Project &p,
                                       const ProcessingPlan &plan, int i,
                                       const CancelCheck &cancel) {
  const auto a = assetFor(p, p.pads[i].asset);
  need(a != nullptr, "Missing sample");
  bool left = false;
  auto input = mono(*a, left);
  if (p.pads[i].reverse)
    std::reverse(input.begin(), input.end());
  const auto &r = plan.sounds[i];
  const double ratio = rate / a->sampleRate * pitchRatio(r.tune);
  auto audio = resample(input, plan.begin[i], plan.end[i], ratio,
                        plan.frames[i], cancel);

  return audio;
}
static Render finishAudio(const Project &p, int padIndex, Render r,
                          const std::vector<double> &audio,
                          const CancelCheck &cancel) {
  if (cancel && cancel())
    throw ProcessingCancelled{};
  const auto &pad = p.pads[padIndex];
  const int n = (int)audio.size();
  int end = n;
  r.loopFrames = loopFramesFor(pad, n, r.tune);
  if (pad.loopEnabled && r.loopFrames >= 3) {
    const int radius = (int)std::floor(.005 * rate * pitchRatio(r.tune));
    end = nearestCrossing(audio, n, 3, n, radius);
    const int requestedStart = std::min(n - r.loopFrames, end - 3);
    // Do not hide an invalid tiny loop by enlarging it, or exceed selection/
    // storage limits by extending End. Both edges use the same direction.
    const int start =
        nearestCrossing(audio, requestedStart, 0, end - 3, radius);
    r.loopFrames = end - start;
    const double framesPerSecond = rate * pitchRatio(r.tune);
    r.playbackEnd =
        end < n ? r.start + end / framesPerSecond : r.end - r.removed;
    r.loopStart =
        std::max(r.start, r.playbackEnd - r.loopFrames / framesPerSecond);
  } else if (pad.loopEnabled) {
    r.playbackEnd = r.end - r.removed;
    r.loopStart = r.playbackEnd - r.loopFrames / (rate * pitchRatio(r.tune));
  }
  // Keep raw resampling cached. Fade/normalization are inexpensive final
  // stages, so changing Settings or Loop never needs another resampling pass.
  const int fade =
      p.fadeOutOnExport && !pad.loopEnabled
          ? std::min(end, std::max(2, (int)(.04 * rate * pitchRatio(r.tune))))
          : 0;
  auto valueAt = [&](int i) {
    if (fade > 1 && i >= end - fade)
      return audio[i] * (1. - (i - end + fade) / double(fade - 1));
    return audio[i];
  };
  double peak = 0;
  if (p.normalizeOnExport)
    for (int i = 0; i < end; ++i)
      peak = std::max(peak, std::abs(valueAt(i)));
  const double gain = peak > 0 ? std::pow(10., -.5 / 20.) / peak : 1.;
  r.gainDb = 20 * std::log10(gain);
  r.values.reserve(end);
  for (int i = 0; i < end; ++i)
    r.values.push_back(std::clamp(
        (int)std::llround(valueAt(i) * gain * 2048) + 2048, 0, 4095));
  if (cancel && cancel())
    throw ProcessingCancelled{};
  return r;
}
Render renderPlannedPad(const Project &p, const ProcessingPlan &plan, int i,
                        const CancelCheck &cancel) {
  if (cancel && cancel())
    throw ProcessingCancelled{};
  if (!plan.frames.at(i))
    return plan.sounds[i];
  return finishAudio(p, i, plan.sounds[i], renderAudio(p, plan, i, cancel),
                     cancel);
}
Render RenderCache::render(const Project &p, const ProcessingPlan &plan, int i,
                           const CancelCheck &cancel) {
  if (cancel && cancel())
    throw ProcessingCancelled{};
  const auto asset = assetFor(p, p.pads.at(i).asset);
  if (!asset)
    return {};
  const auto &metadata = plan.sounds[i];
  auto found =
      std::find_if(entries.begin(), entries.end(), [&](const Entry &e) {
        return e.asset == asset && e.begin == plan.begin[i] &&
               e.end == plan.end[i] && e.frames == plan.frames[i] &&
               e.tune == metadata.tune && e.reverse == p.pads[i].reverse;
      });
  if (found == entries.end()) {
    auto audio = renderAudio(p, plan, i, cancel);
    if (cancel && cancel())
      throw ProcessingCancelled{};
    ++renders;
    if (entries.size() == 32)
      entries.erase(entries.begin());
    entries.push_back({asset, plan.begin[i], plan.end[i], plan.frames[i],
                       metadata.tune, p.pads[i].reverse, std::move(audio)});
  } else {
    // Retain unquantized, unshortened audio so changing loop points never
    // compounds normalization, loses the tail, or requires resampling again.
    auto entry = std::move(*found);
    entries.erase(found);
    entries.push_back(std::move(entry));
  }
  return finishAudio(p, i, metadata, entries.back().audio, cancel);
}
Result renderBank(const Project &p, const ProcessingPlan &plan,
                  RenderCache &cache, const CancelCheck &cancel) {
  Result out;
  for (int i = 0; i < 32; ++i)
    out.sounds[i] = cache.render(p, plan, i, cancel);
  if (cancel && cancel())
    throw ProcessingCancelled{};
  out.bank = buildBank(p, out.sounds, out.used);
  return out;
}
Result process(const Project &p) {
  RenderCache cache;
  return renderBank(p, planBank(p), cache);
}
Render renderPadPreview(const Project &p, int padIndex) {
  return renderPlannedPad(p, planPadPreview(p, padIndex), padIndex);
}
static constexpr int zoneOffsets[] = {0x9258, 0x925a, 0x9254, 0x9256,
                                      0x9110, 0x9112, 0x925c, 0x925e};
static int slot(int i) { return (3 - i / 8) * 8 + i % 8; }
static void word(uint8 *b, int pos, int value) {
  b[pos] = (uint8)value;
  b[pos + 1] = (uint8)(value >> 8);
}
static int word(const uint8 *b, int pos) { return b[pos] | (b[pos + 1] << 8); }
void writeLoopDescriptor(MemoryBlock &bank, int padIndex, int frames) {
  need(bank.getSize() == 824546 && padIndex >= 0 && padIndex < 32,
       "Invalid loop descriptor target");
  auto *b = (uint8 *)bank.getData();
  const int s = slot(padIndex), d = 0x9262 + s * 10;
  const int start = word(b, d + 2), span = word(b, d + 4) ^ 65535;
  const int allocationStart = word(b, 0x91d4 + s * 4);
  const int allocationSize = word(b, 0x91d6 + s * 4);
  if (frames != 0 && (allocationSize == 0 || frames < 3 || frames > span - 2 ||
                      start + span - frames < allocationStart ||
                      start + span > allocationStart + allocationSize))
    throw LoopError("Loop needs at least 3 stored frames within the selection. "
                    "Extend the loop or turn Loop Off to export.");
  word(b, d + 6, frames == 0 ? 0 : start + span - frames);
  word(b, d + 8, frames == 0 ? 65533 : frames ^ 65535);
}
MemoryBlock buildBank(const Project &p, const std::array<Render, 32> &sounds,
                      std::array<int, 8> &used) {
  MemoryBlock bank(BinaryData::EMPTY__sp12, BinaryData::EMPTY__sp12Size);
  need(bank.getSize() == 824546, "Invalid template size");
  static const bool validTemplate =
      SHA256(BinaryData::EMPTY__sp12, BinaryData::EMPTY__sp12Size)
          .toHexString() ==
      "92b242054ae2c53caf8e9b706813a09fd2ebef0194e65a7b732630c23413a0f5";
  need(validTemplate, "Invalid bank template hash");
  auto *b = (uint8 *)bank.getData();
  for (int i = 0x94e2; i < 824546; i += 3) {
    b[i] = b[i + 1] = 128;
    b[i + 2] = 0;
  }
  std::array<int, 32> lengths{}, zones{}, starts{};
  for (int i = 0; i < 32; ++i) {
    need(sounds[i].values.size() <= maxFrames,
         "Sample exceeds memory zone capacity");
    lengths[i] = (int)sounds[i].values.size();
  }
  need(allocate(lengths, used, nullptr, &zones, &starts),
       "Sample exceeds memory zone capacity");
  for (int i = 0; i < 32; ++i) {
    auto &r = sounds[i];
    if (r.values.empty())
      continue;
    auto &pad = p.pads[i];
    need(pad.channel >= 1 && pad.channel <= 8, "Invalid channel");
    need(r.tune >= 0 && r.tune <= 31, "Invalid tune");
    need(pad.name.length() > 0 && pad.name.length() <= 6 &&
             pad.name.containsOnly(
                 " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz01234567"
                 "89!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"),
         "Hardware names must be 1–6 printable ASCII characters");
    const int n = allocation(lengths[i]), z = zones[i];
    int start = starts[i], s = slot(i),
        off = 0x94e2 + z * 98304 + start * 3 / 2;
    std::vector<int> values(n, 2048);
    std::copy(r.values.begin(), r.values.end(), values.begin() + 5);
    for (int j = 0; j < n; j += 2) {
      int a = values[j], c = values[j + 1];
      need(a >= 0 && a < 4096 && c >= 0 && c < 4096, "Invalid 12-bit audio");
      b[off++] = (uint8)(a >> 4);
      b[off++] = (uint8)(c >> 4);
      b[off++] = (uint8)((a & 15) | ((c & 15) << 4));
    }
    word(b, 0x91d4 + s * 4, start);
    word(b, 0x91d6 + s * 4, n);
    int d = 0x9262 + s * 10;
    b[d] = (uint8)((b[d] & 0xf8) | (pad.channel - 1));
    b[d + 1] = (uint8)((r.tune << 3) | z);
    word(b, d + 2, start);
    word(b, d + 4, (n - 10) ^ 65535);
    try {
      writeLoopDescriptor(bank, i, loopFrameCount(pad, r));
    } catch (LoopError &error) {
      // Preserve the actual processed selection even when its loop cannot
      // be exported. Never copy audio vectors into the failure presentation.
      error.selections = std::make_shared<std::array<Render, 32>>();
      for (int j = 0; j < 32; ++j) {
        auto &selection = (*error.selections)[j];
        selection.start = sounds[j].start;
        selection.end = sounds[j].end;
        selection.removed = sounds[j].removed;
        selection.tune = sounds[j].tune;
        selection.gainDb = sounds[j].gainDb;
        selection.left = sounds[j].left;
        selection.playbackEnd = sounds[j].playbackEnd;
        selection.loopStart = sounds[j].loopStart;
        selection.loopFrames = sounds[j].loopFrames;
      }
      throw;
    }
    auto name = pad.name.paddedRight(' ', 6);
    for (int j = 0; j < 6; ++j)
      b[0x9114 + s * 6 + j] = (uint8)(name[j] | (j < 3 ? 128 : 0));
    for (int j = 0; j < 10; j += 2) {
      b[0x93a2 + s * 10 + j] = 0;
      b[0x93a3 + s * 10 + j] = 128;
    }
    word(b, zoneOffsets[z], used[z]);
    b[0x9260] = 255;
  }
  validateBank(bank);
  return bank;
}
void validateBank(const MemoryBlock &bank) {
  need(bank.getSize() == 824546, "Invalid bank size");
  auto *b = (const uint8 *)bank.getData();
  std::array<std::vector<std::pair<int, int>>, 8> regions;
  std::array<int, 8> used{};
  for (int s = 0; s < 32; ++s) {
    int start = word(b, 0x91d4 + s * 4), n = word(b, 0x91d6 + s * 4);
    if (!n)
      continue;
    int d = 0x9262 + s * 10, z = b[d + 1] & 7, p = word(b, d + 2),
        span = word(b, d + 4) ^ 65535;
    need(start + n <= 65535 && p >= start && p < start + n &&
             p + span <= start + n,
         "Invalid bank allocation");
    const int loopStart = word(b, d + 6);
    const int loopSpan = word(b, d + 8) ^ 65535;
    need((loopStart == 0 && loopSpan == 2) ||
             (loopSpan >= 3 && loopSpan <= span - 2 && loopStart >= start &&
              loopStart + loopSpan == p + span),
         "Invalid bank loop bounds");
    for (auto [a, c] : regions[z])
      need(start >= c || start + n <= a || (start == a && start + n == c),
           "Overlapping bank allocation");
    regions[z].push_back({start, start + n});
    used[z] = std::max(used[z], start + n);
  }
  for (int z = 0; z < 8; ++z)
    need(used[z] == word(b, zoneOffsets[z]), "Invalid zone counters");
}
void atomicWrite(const File &file, const MemoryBlock &bytes) {
  TemporaryFile tmp(file);
  {
    FileOutputStream stream(tmp.getFile());
    need(stream.openedOk(), "Cannot create output file");
    need(stream.write(bytes.getData(), bytes.getSize()), "Write failed");
    stream.flush();
    need(stream.getStatus().wasOk(), "Write failed");
  }
  need(tmp.overwriteTargetFileWithTemporary(), "Cannot replace output file");
}
static var object() { return var(new DynamicObject()); }
static void set(var &v, const Identifier &k, const var &value) {
  v.getDynamicObject()->setProperty(k, value);
}
static var padJson(const Pad &pad) {
  auto v = object();
  set(v, "asset", pad.asset);
  set(v, "assignmentId", pad.assignmentId);
  set(v, "name", pad.name);
  set(v, "channel", pad.channel);
  set(v, "reverse", pad.reverse);
  set(v, "manualTrim", pad.manualTrim);
  set(v, "loopEnabled", pad.loopEnabled);
  set(v, "loopSeconds", pad.loopSeconds);
  set(v, "start", pad.start);
  set(v, "end", pad.end);
  return v;
}
static Pad padFromJson(const var &v, const Project &p, StringArray &ids,
                       bool requireId = false) {
  Pad pad;
  pad.asset = v["asset"].toString();
  pad.name = v["name"].toString();
  pad.channel = (int)v["channel"];
  pad.reverse = (bool)v["reverse"];
  need(v.hasProperty("manualTrim"), "Missing manual trim status");
  pad.manualTrim = (bool)v["manualTrim"];
  need(v.hasProperty("loopEnabled") && v.hasProperty("loopSeconds"),
       "Missing loop settings");
  pad.loopEnabled = (bool)v["loopEnabled"];
  pad.loopSeconds = (double)v["loopSeconds"];
  need(std::isfinite(pad.loopSeconds) &&
           (pad.loopSeconds == -1 || pad.loopSeconds >= 0),
       "Invalid loop duration");
  pad.start = (double)v["start"];
  pad.end = (double)v["end"];
  const auto a = assetFor(p, pad.asset);
  need(pad.asset.isEmpty() || a != nullptr, "Missing pad sample");
  need(pad.channel >= 1 && pad.channel <= 8 && std::isfinite(pad.start) &&
           std::isfinite(pad.end),
       "Invalid pad settings");
  if (pad.manualTrim)
    need(a && pad.start >= 0 && pad.end > pad.start &&
             pad.end <= a->audio.getNumSamples() / a->sampleRate + 1e-9,
         "Invalid manual trim range");
  if (pad.asset.isNotEmpty()) {
    need(!requireId || v.hasProperty("assignmentId"),
         "Missing assignment ID in Auto Map state");
    need(!v.hasProperty("assignmentId") || v["assignmentId"].isString(),
         "Invalid assignment ID");
    pad.assignmentId = v.hasProperty("assignmentId")
                           ? v["assignmentId"].toString()
                           : Uuid().toString();
    need(pad.assignmentId.isNotEmpty() && !ids.contains(pad.assignmentId),
         "Duplicate or empty assignment ID");
    ids.add(pad.assignmentId);
  }
  return pad;
}
void savePreset(const Project &source, const File &file, const Result *result) {
  auto p = source;
  ensureAssignmentIds(p.pads);
  ensureAutoMapRestore(p);
  if (std::any_of(p.pads.begin(), p.pads.end(), [](const Pad &pad) {
        return pad.asset.isNotEmpty() && pad.loopSeconds >= 0;
      })) {
    if (result)
      clampLoopLengths(p, result->sounds);
    else {
      // A save requested during processing still captures permanent loop
      // clamps. Resolve allocation/trim metadata on the worker without
      // resampling audio.
      try {
        clampLoopLengths(p, planAudio(p, false).sounds);
      } catch (const MemoryError &) {
        clampLoopLengths(p, selectionMetadata(p));
      }
    }
  }
  clampAutoMapRestoreLoops(p);
  auto root = object();
  set(root, "version", 7);
  set(root, "bankName", p.bankName);
  set(root, "autoTune", p.autoTune);
  set(root, "autoSort", p.autoSort);
  set(root, "autoTrim", p.autoTrim);
  set(root, "threshold", p.threshold);
  set(root, "fadeOutOnExport", p.fadeOutOnExport);
  set(root, "normalizeOnExport", p.normalizeOnExport);
  Array<var> assets, pads;
  ZipFile::Builder zip;
  for (auto a : p.pool) {
    auto v = object();
    String entry = "samples/" + a->id + File(a->filename).getFileExtension();
    set(v, "id", a->id);
    set(v, "filename", a->filename);
    set(v, "relative", a->relative);
    set(v, "entry", entry);
    set(v, "sha256", SHA256(a->original).toHexString());
    assets.add(v);
    zip.addEntry(new MemoryInputStream(a->original, false), 6, entry,
                 Time::getCurrentTime());
  }
  for (int i = 0; i < 32; ++i) {
    auto &pad = p.pads[i];
    auto v = padJson(pad);
    // Derived values are optional; originals and settings fully define the kit.
    // Omit them when no valid bank render exists instead of storing stale data.
    if (result && !result->sounds[i].values.empty()) {
      set(v, "mono", result->sounds[i].left ? "left" : "average");
      set(v, "gainDb", result->sounds[i].gainDb);
      set(v, "tune", result->sounds[i].tune);
    }
    pads.add(v);
  }
  set(root, "assets", assets);
  set(root, "pads", pads);
  if (p.autoMapRestore) {
    Array<var> previous, transient;
    for (const auto &pad : p.autoMapRestore->previous)
      previous.add(padJson(pad));
    for (const auto &id : p.autoMapRestore->transient)
      transient.add(id);
    auto state = object();
    set(state, "previous", previous);
    set(state, "transient", transient);
    set(root, "autoMapRestore", state);
  }
  bool pending = result == nullptr;
  if (result)
    for (int i = 0; i < 32; ++i)
      pending |=
          p.pads[i].asset.isNotEmpty() && result->sounds[i].values.empty();
  set(root, "processingPending", pending);
  set(root, "normalizationPeakDb", -.5);
  auto json = JSON::toString(root);
  zip.addEntry(
      new MemoryInputStream(json.toRawUTF8(), json.getNumBytesAsUTF8(), true),
      6, "project.json", Time::getCurrentTime());
  MemoryOutputStream output;
  need(zip.writeToStream(output, nullptr), "Cannot write preset archive");
  atomicWrite(file, output.getMemoryBlock());
}
Project loadPreset(const File &file) {
  ZipFile zip(file);
  int index = zip.getIndexOfFileName("project.json");
  need(index >= 0, "Missing preset manifest");
  std::unique_ptr<InputStream> in(zip.createStreamForEntry(index));
  need(in != nullptr, "Cannot read manifest");
  auto root = JSON::parse(in->readEntireStreamAsString());
  const int version = (int)root["version"];
  need(version == 7, "Unsupported preset version (requires version 7)");
  auto *assets = root["assets"].getArray();
  auto *pads = root["pads"].getArray();
  need(assets && pads && pads->size() == 32, "Invalid preset structure");
  Project p;
  p.bankName = root["bankName"].toString();
  for (auto v : *assets) {
    String id = v["id"], name = v["filename"], entry = v["entry"];
    need(id.isNotEmpty() && !assetFor(p, id), "Duplicate or empty sample ID");
    int n = zip.getIndexOfFileName(entry);
    need(n >= 0, "Missing embedded sample");
    std::unique_ptr<InputStream> stream(zip.createStreamForEntry(n));
    need(stream != nullptr, "Cannot read sample");
    MemoryBlock bytes;
    stream->readIntoMemoryBlock(bytes);
    need(SHA256(bytes).toHexString() == v["sha256"].toString(),
         "Embedded sample checksum mismatch");
    auto a = decode(bytes, name, v["relative"].toString());
    a->id = id;
    p.pool.push_back(a);
  }
  StringArray ids;
  for (int i = 0; i < 32; ++i)
    p.pads[i] = padFromJson(pads->getReference(i), p, ids,
                            root.hasProperty("autoMapRestore"));
  p.threshold = (double)root["threshold"];
  need(root.hasProperty("threshold") && std::isfinite(p.threshold) &&
           p.threshold >= -60 && p.threshold <= -5,
       "Invalid global trim threshold");
  p.autoTune = (bool)root["autoTune"];
  p.autoSort = (bool)root["autoSort"];
  p.autoTrim = (bool)root["autoTrim"];
  for (auto key : {"fadeOutOnExport", "normalizeOnExport"})
    need(!root.hasProperty(key) || root[key].isBool(),
         "Invalid export processing setting");
  if (root.hasProperty("fadeOutOnExport"))
    p.fadeOutOnExport = (bool)root["fadeOutOnExport"];
  if (root.hasProperty("normalizeOnExport"))
    p.normalizeOnExport = (bool)root["normalizeOnExport"];
  if (root.hasProperty("autoMapRestore")) {
    auto state = root["autoMapRestore"];
    auto *previous = state["previous"].getArray();
    auto *transient = state["transient"].getArray();
    need(p.autoSort && previous && previous->size() == 32 && transient,
         "Invalid Auto Map restore state");
    AutoMapState restored;
    StringArray previousIds;
    for (int i = 0; i < 32; ++i) {
      restored.previous[i] =
          padFromJson(previous->getReference(i), p, previousIds, true);
      const auto &old = restored.previous[i];
      for (const auto &current : p.pads)
        need(old.assignmentId.isEmpty() ||
                 old.assignmentId != current.assignmentId ||
                 old.asset == current.asset,
             "Assignment ID refers to different samples");
    }
    for (const auto &id : *transient) {
      need(id.isString() && ids.contains(id.toString()) &&
               !previousIds.contains(id.toString()) &&
               !restored.transient.contains(id.toString()),
           "Invalid transient Auto Map assignment");
      restored.transient.add(id.toString());
    }
    p.autoMapRestore = std::move(restored);
  } else
    ensureAutoMapRestore(p);
  return p;
}
} // namespace sp
