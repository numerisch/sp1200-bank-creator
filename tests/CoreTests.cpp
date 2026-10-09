// SPDX-License-Identifier: AGPL-3.0-only
// Copyright © 2026 Numerisch GmbH

#include "AppPreferences.h"
#include "Core.h"
#include <cmath>
#include <iostream>
#include <numeric>
using namespace juce;
static int checks = 0;
static void check(bool ok, const char *m) {
  ++checks;
  if (!ok)
    throw std::runtime_error(m);
}
template <class F> static void rejects(F f, const char *m) {
  bool rejected = false;
  try {
    f();
  } catch (...) {
    rejected = true;
  }
  check(rejected, m);
}
static sp::MemoryError memoryFailure(const sp::Project &project) {
  try {
    sp::process(project);
  } catch (const sp::MemoryError &e) {
    return e;
  }
  throw std::runtime_error("Expected a structured memory error");
}
static std::array<bool, 32> memoryProblems(const sp::Project &project) {
  return memoryFailure(project).pads;
}
static std::shared_ptr<sp::Asset> tone(String filename, double seconds = .1,
                                       double amplitude = .1, int channels = 1,
                                       bool inverse = false, double hz = 440) {
  AudioBuffer<float> b(channels, (int)(seconds * 44100));
  for (int c = 0; c < channels; ++c)
    for (int i = 0; i < b.getNumSamples(); ++i)
      b.setSample(
          c, i,
          float(amplitude *
                std::sin(2 * MathConstants<double>::pi * hz * i / 44100) *
                (inverse && c == 1 ? -1 : 1)));
  WavAudioFormat wav;
  auto stream = std::make_unique<MemoryOutputStream>();
  auto *raw = stream.get();
  std::unique_ptr<AudioFormatWriter> writer(wav.createWriterFor(
      stream.release(), 44100, (unsigned int)channels, 24, {}, 0));
  check(writer != nullptr, "WAV writer");
  check(writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples()),
        "WAV write");
  writer->flush();
  MemoryBlock bytes = raw->getMemoryBlock();
  writer.reset();
  return sp::decode(bytes, filename, filename);
}
static sp::Project single(std::shared_ptr<sp::Asset> a) {
  sp::Project p;
  p.autoTrim = true; // These audio fixtures exercise automatic processing.
  p.pool.push_back(a);
  p.pads[0] = sp::makePad(*a, 0, true, true);
  return p;
}
static bool samePad(const sp::Pad &a, const sp::Pad &b) {
  return a.asset == b.asset && a.assignmentId == b.assignmentId &&
         a.name == b.name && a.channel == b.channel && a.reverse == b.reverse &&
         a.manualTrim == b.manualTrim && a.start == b.start && a.end == b.end &&
         a.loopEnabled == b.loopEnabled && a.loopSeconds == b.loopSeconds;
}
static void rewriteKitManifest(const File &file,
                               const std::function<void(var &)> &edit) {
  MemoryOutputStream bytes;
  {
    ZipFile original(file);
    ZipFile::Builder zip;
    for (int i = 0; i < original.getNumEntries(); ++i) {
      std::unique_ptr<InputStream> stream(original.createStreamForEntry(i));
      MemoryBlock entry;
      stream->readIntoMemoryBlock(entry);
      const auto name = original.getEntry(i)->filename;
      if (name == "project.json") {
        auto manifest = JSON::parse(entry.toString());
        edit(manifest);
        const auto json = JSON::toString(manifest);
        entry = MemoryBlock(json.toRawUTF8(), json.getNumBytesAsUTF8());
      }
      zip.addEntry(new MemoryInputStream(entry, true), 6, name,
                   Time::getCurrentTime());
    }
    check(zip.writeToStream(bytes, nullptr), "Rewrite kit manifest fixture");
  }
  sp::atomicWrite(file, bytes.getMemoryBlock());
}
static void testMirroredAutoMap() {
  const StringArray kinds{"BD", "SD", "Clap", "Rim",
                          "CH", "OH", "Ride", "Crash"};
  sp::Project kit;
  for (int level : {3, 2, 1})
    for (const auto &kind : kinds) {
      auto a = tone(kind + "_0" + String(level) + ".wav", .01);
      kit.pool.push_back(a);
      sp::assignPad(kit, int(kit.pool.size()) - 1, *a);
    }
  kit.pads[8].name = "KICK2";
  kit.pads[8].channel = 7;
  kit.pads[8].reverse = true;
  const auto previous = kit.pads;
  sp::setAutoMap(kit, true);
  for (int i = 0; i < 8; ++i) {
    check(samePad(kit.pads[i], previous[16 + i]) &&
              samePad(kit.pads[16 + i], previous[8 + i]) &&
              samePad(kit.pads[24 + i], previous[i]),
          "First/second/third variants map to A/C/D without resetting edits");
    check(sp::padInstrument(kit, i) == sp::padInstrument(kit, 16 + i) &&
              sp::padInstrument(kit, 24 + i) == "Other" &&
              kit.pads[8 + i].asset.isEmpty(),
          "C mirrors A labels; D stays Other and missing B categories stay "
          "empty");
  }
  sp::setAutoMap(kit, false);
  for (int i = 0; i < 32; ++i)
    check(samePad(kit.pads[i], previous[i]),
          "Mirrored mapping still restores the complete previous layout");

  sp::Project sparse;
  sparse.pool = {tone("SD_03.wav"), tone("BD_02.wav"), tone("SD_01.wav"),
                 tone("BD_01.wav"), tone("BD_03.wav"), tone("SD_02.wav")};
  sp::setAutoMap(sparse, true);
  check(sparse.pads[0].asset == sparse.pool[3]->id &&
            sparse.pads[1].asset == sparse.pool[2]->id &&
            sparse.pads[16].asset == sparse.pool[1]->id &&
            sparse.pads[17].asset == sparse.pool[5]->id &&
            sparse.pads[24].asset == sparse.pool[4]->id &&
            sparse.pads[25].asset == sparse.pool[0]->id &&
            sparse.pads[2].asset.isEmpty() && sparse.pads[18].asset.isEmpty(),
        "Second kicks/snares use C even when the rest of A is empty");
  const auto oldKick = sparse.pads[0], oldSecondKick = sparse.pads[16];
  const auto additions = std::vector<std::shared_ptr<sp::Asset>>{
      tone("OH_02.wav"), tone("BD_04.wav"), tone("OH_01.wav"),
      tone("Unknown.wav")};
  sp::appendSamples(sparse, additions);
  check(sparse.pads[5].asset == additions[2]->id &&
            sparse.pads[21].asset == additions[0]->id &&
            sparse.pads[26].asset == additions[1]->id &&
            sparse.pads[27].asset == additions[3]->id &&
            samePad(sparse.pads[0], oldKick) &&
            samePad(sparse.pads[16], oldSecondKick),
        "Additional imports fill matching A/C slots, then D, without replacing "
        "pads");
  std::vector<std::shared_ptr<sp::Asset>> others;
  for (int i = 0; i < 40; ++i)
    others.push_back(tone("Unknown_" + String(i) + ".wav", .001));
  sp::appendSamples(sparse, others);
  for (int i = 16; i < 24; ++i)
    check(sparse.pads[i].asset.isEmpty() ||
              sp::assetFor(sparse, sparse.pads[i].asset)->category ==
                  kinds[i - 16],
          "Unclassified overflow never fills reserved C instrument slots");
  check(sparse.pool.size() == 50,
        "Imported overflow remains available in the pool");
  std::cout << "PASS: mirrored A/C instruments, D Other, sparse kits, stable "
               "variants, edits, append and reserved C slots\n";
}
static void testReversibleAutoMap() {
  sp::Project kit;
  kit.pool = {tone("SD.wav"), tone("BD.wav"), tone("Clap.wav"),
              tone("Ride.wav")};
  sp::assignPad(kit, 0, *kit.pool[0]);
  sp::assignPad(kit, 3, *kit.pool[1]);
  sp::assignPad(kit, 10, *kit.pool[1]);
  auto &kick = kit.pads[3];
  kick.name = "CUSTOM";
  kick.channel = 7;
  kick.manualTrim = true;
  kick.start = .01;
  kick.end = .08;
  kick.reverse = true;
  kick.loopEnabled = true;
  kick.loopSeconds = .025;
  kit.pads[10].name = "COPY";
  kit.pads[10].manualTrim = true;
  kit.pads[10].start = .02;
  kit.pads[10].end = .06;
  const auto original = kit.pads;
  check(kit.pads[3].assignmentId != kit.pads[10].assignmentId,
        "Independent assignments of the same sample have distinct IDs");
  sp::RenderCache cache;
  const auto rendered = cache.render(kit, sp::planBank(kit), 3);
  sp::setAutoMap(kit, true);
  check(kit.autoSort && kit.autoMapRestore &&
            samePad(kit.pads[0], original[3]) &&
            samePad(kit.pads[1], original[0]) &&
            kit.pads[2].asset == kit.pool[2]->id,
        "Mapping preserves all pad edits and uses the first duplicate");
  check(cache.render(kit, sp::planBank(kit), 0).values == rendered.values &&
            cache.renderCount() == 1,
        "Moving a sound through Auto Map reuses cached DSP");
  check(sp::padInstrument(kit, 4) == "Closed Hi-Hat" &&
            kit.pads[4].asset.isEmpty() &&
            sp::padInstrument(kit, 15) == "Cowbell" &&
            sp::padInstrument(kit, 16) == "BD" &&
            sp::padInstrument(kit, 23) == "Crash" &&
            sp::padInstrument(kit, 31) == "Other",
        "Mapped A/B/C pads show targets; every D pad shows Other");
  sp::setAutoMap(kit, false);
  for (int i = 0; i < 32; ++i)
    check(
        samePad(kit.pads[i], original[i]) &&
            sp::padInstrument(kit, i).isEmpty(),
        "Off restores exact positions, edits, duplicate assignments and gaps");

  auto editedPoolOnly = kit;
  sp::setAutoMap(editedPoolOnly, true);
  editedPoolOnly.pads[2].name = "MYCLAP";
  editedPoolOnly.pads[2].manualTrim = true;
  editedPoolOnly.pads[2].start = .01;
  editedPoolOnly.pads[2].end = .05;
  sp::retainMappedAssignment(editedPoolOnly, 2);
  const auto editedClap = editedPoolOnly.pads[2];
  sp::setAutoMap(editedPoolOnly, false);
  check(
      samePad(editedPoolOnly.pads[1], editedClap),
      "Editing an initially pool-only sound retains it in a free return slot");

  sp::setAutoMap(kit, true);
  kit.pads[0].name = "EDITED";
  kit.pads[0].channel = 4;
  kit.pads[0].start = .015;
  kit.pads[0].loopSeconds = .018;
  const auto edited = kit.pads[0];
  std::swap(kit.pads[0], kit.pads[1]);
  const auto dir = File::getSpecialLocation(File::tempDirectory)
                       .getChildFile("sp-auto-map-" + Uuid().toString());
  dir.createDirectory();
  const auto file = dir.getChildFile("mapped.spkit");
  sp::savePreset(kit, file);
  auto restored = sp::loadPreset(file);
  sp::setAutoMap(restored, false);
  check(samePad(restored.pads[3], edited) &&
            samePad(restored.pads[10], original[10]) &&
            samePad(restored.pads[0], original[0]),
        "Kit roundtrip returns mapped edits and hidden duplicate settings");
  sp::setAutoMap(kit, false);
  check(samePad(kit.pads[3], edited),
        "Pad swapping in mapped mode carries edits back to the original slot");
  std::swap(kit.pads[3], kit.pads[7]);
  const auto latest = kit.pads;
  sp::setAutoMap(kit, true);
  sp::setAutoMap(kit, false);
  for (int i = 0; i < 32; ++i)
    check(samePad(kit.pads[i], latest[i]),
          "Each activation uses the latest off-layout, not the first layout");

  sp::setAutoMap(kit, true);
  sp::clearPad(kit, 1); // Previously A1 snare.
  auto replacement = tone("Cowbell.wav");
  kit.pool.push_back(replacement);
  sp::assignPad(kit, 0, *replacement); // Replaces the original kick assignment.
  auto shake = tone("Shaker.wav"), hat = tone("OH.wav");
  sp::appendSamples(kit, {shake, hat});
  kit.pads[13].name = "SHAKE";
  const auto replaced = kit.pads[0];
  sp::setAutoMap(kit, false);
  check(samePad(kit.pads[0], replaced) && kit.pads[1].asset == shake->id &&
            kit.pads[1].name == "SHAKE" && kit.pads[2].asset == hat->id &&
            kit.pads[7].asset.isEmpty() && samePad(kit.pads[10], original[10]),
        "Cleared/replaced assignments stay removed; additions fill gaps in "
        "pool order while hidden duplicates survive");

  sp::Project overflow;
  for (int i = 0; i < 40; ++i)
    overflow.pool.push_back(
        tone("BD " + String(i).paddedLeft('0', 2) + ".wav", .7));
  for (int i = 0; i < 32; ++i)
    sp::assignPad(overflow, i, *overflow.pool[i + 8]);
  overflow.pads[31].name = "HIDDEN";
  overflow.pads[31].manualTrim = true;
  overflow.pads[31].start = .02;
  overflow.pads[31].end = .65;
  const auto full = overflow.pads;
  sp::setAutoMap(overflow, true);
  check(std::none_of(overflow.pads.begin(), overflow.pads.end(),
                     [&](auto &pad) {
                       return pad.assignmentId == full[31].assignmentId;
                     }),
        "Auto Map can temporarily displace an original assignment");
  sp::appendSamples(overflow, {tone("SD new.wav", .7)});
  sp::savePreset(overflow, file);
  restored = sp::loadPreset(file);
  sp::setAutoMap(restored, false);
  for (int i = 0; i < 32; ++i)
    check(samePad(restored.pads[i], full[i]),
          "Full previous layout wins over new mapped assignments on return");
  check(restored.pool.size() == 41 && memoryFailure(restored).excessFrames > 0,
        "Overfilled kits restore all assignments and retain new overflow in "
        "pool");

  // Hidden automatic cuts follow global modes; hidden manual cuts do not.
  auto longKit = single(tone("BD long.wav", 7));
  longKit.autoTrim = false;
  sp::assignPad(longKit, 1, *longKit.pool[0]);
  sp::assignPad(longKit, 2, *longKit.pool[0]);
  longKit.pads[2].manualTrim = true;
  longKit.pads[2].start = 1;
  longKit.pads[2].end = 4;
  sp::setAutoMap(longKit, true);
  sp::setAutoTune(longKit, false);
  check(longKit.autoMapRestore->previous[1].end < 2.5 &&
            longKit.autoMapRestore->previous[2].start == 1 &&
            longKit.autoMapRestore->previous[2].end == 4,
        "Pitch Fit updates hidden automatic cuts and protects hidden manual "
        "cuts");
  sp::setAutoTune(longKit, true);
  check(longKit.autoMapRestore->previous[1].end > 6.2,
        "Hidden automatic cuts expand when Pitch Fit returns");
  auto trimmed = single(tone("BD trim.wav"));
  for (int i = 1500; i < trimmed.pool[0]->audio.getNumSamples(); ++i)
    trimmed.pool[0]->audio.setSample(0, i, 0);
  trimmed.autoTrim = false;
  sp::assignPad(trimmed, 1, *trimmed.pool[0]);
  trimmed.pads[1].loopSeconds = .09;
  sp::setAutoMap(trimmed, true);
  sp::setAutoTrim(trimmed, true);
  sp::clampAutoMapRestoreLoops(trimmed);
  const auto shortLoop = trimmed.autoMapRestore->previous[1].loopSeconds;
  check(shortLoop < .04 && shortLoop > .03,
        "Auto Trim clamps a hidden custom loop using metadata only");
  sp::setAutoTrim(trimmed, false);
  sp::setAutoMap(trimmed, false);
  check(trimmed.pads[1].loopSeconds == shortLoop,
        "Expanding a hidden selection does not regrow its clamped loop");

  sp::savePreset(overflow, file);
  rewriteKitManifest(file, [](var &manifest) {
    manifest.getDynamicObject()->removeProperty("autoMapRestore");
    for (auto &pad : *manifest["pads"].getArray())
      pad.getDynamicObject()->removeProperty("assignmentId");
  });
  restored = sp::loadPreset(file);
  const auto legacyLayout = restored.pads;
  sp::setAutoMap(restored, false);
  for (int i = 0; i < 32; ++i)
    check(
        samePad(restored.pads[i], legacyLayout[i]),
        "Existing version-7 mapped kits use their current layout as baseline");
  sp::savePreset(overflow, file);
  rewriteKitManifest(file, [](var &manifest) {
    auto *pads = manifest["pads"].getArray();
    pads->getReference(16).getDynamicObject()->setProperty(
        "assignmentId", pads->getReference(0)["assignmentId"]);
  });
  rejects([&] { sp::loadPreset(file); },
          "Malformed duplicate assignment IDs are rejected");
  sp::savePreset(overflow, file);
  rewriteKitManifest(file, [](var &manifest) {
    manifest["autoMapRestore"]["previous"]
        .getArray()
        ->getReference(0)
        .getDynamicObject()
        ->setProperty("asset", "missing-sample");
  });
  rejects([&] { sp::loadPreset(file); },
          "Restore-layout sample references receive the same validation");
  sp::savePreset(overflow, file);
  rewriteKitManifest(file, [](var &manifest) {
    manifest["autoMapRestore"]["previous"]
        .getArray()
        ->getReference(0)
        .getDynamicObject()
        ->removeProperty("assignmentId");
  });
  rejects([&] { sp::loadPreset(file); },
          "Saved return layouts require assignment links, not invented IDs");
  sp::clearKit(overflow);
  sp::setAutoMap(overflow, false);
  check(overflow.pool.empty() &&
            std::all_of(overflow.pads.begin(), overflow.pads.end(),
                        [](auto &pad) { return pad.asset.isEmpty(); }) &&
            !overflow.autoMapRestore,
        "Clear All removes both layouts so Off never resurrects assignments");
  dir.deleteRecursively();
  std::cout << "PASS: reversible Auto Map, labels, edits, duplicates, hidden "
               "cuts/loops, additions, removals, overflow and portable kits\n";
}
static void testLazyRendering() {
  sp::Project kit;
  for (int i = 0; i < 32; ++i) {
    auto a = tone("BD" + String(i) + ".wav", .025 + i * .0005, .02 + i * .005,
                  2, i == 7, 150 + i * 20);
    kit.pool.push_back(a);
    kit.pads[i] = sp::makePad(*a, i, false, true);
  }
  auto plan = sp::planBank(kit);
  for (int i = 0; i < 32; ++i)
    check(plan.sounds[i].values.empty() && plan.frames[i] > 0,
          "Full-bank planning allocates no rendered audio");
  sp::RenderCache cache;
  auto selected = cache.render(kit, plan, 0);
  check(cache.renderCount() == 1 && !selected.values.empty(),
        "Selecting one pad renders only one sound");
  const auto expected = sp::process(kit);
  auto complete = sp::renderBank(kit, plan, cache);
  check(complete.bank == expected.bank && cache.renderCount() == 32,
        "Lazy export fills missing pads and matches complete export bytes");
  check(sp::renderBank(kit, plan, cache).bank == complete.bank &&
            cache.renderCount() == 32,
        "Unchanged repeated export reuses all cached audio");
  kit.pads[0].name = "RENAMD";
  kit.pads[0].channel = 7;
  kit.pads[0].loopEnabled = true;
  kit.pads[0].loopSeconds = .01;
  plan = sp::planBank(kit);
  auto loop = cache.render(kit, plan, 0);
  check(cache.renderCount() == 32 &&
            loop.values.size() <= selected.values.size() &&
            (selected.values.size() - loop.values.size()) <=
                (size_t)(.005 * sp::rate) &&
            loop.loopFrames == sp::loopFrameCount(kit.pads[0], loop),
        "Names, routing and loop point edits reuse unchanged audio");
  check(sp::renderBank(kit, plan, cache).bank == sp::process(kit).bank &&
            cache.renderCount() == 32,
        "Cache reuse writes current routing, names and loop descriptors");
  kit.pads[0].manualTrim = true;
  kit.pads[0].start = .003;
  kit.pads[0].end = .02;
  plan = sp::planBank(kit);
  auto cut = cache.render(kit, plan, 0);
  check(cache.renderCount() == 33 && cut.values != selected.values,
        "A manual trim invalidates only the edited sound");
  const auto before = cache.renderCount();
  sp::renderBank(kit, plan, cache);
  // A bounded 32-entry LRU may have evicted one old unedited entry.
  check(cache.renderCount() <= before + 1 && cache.size() <= 32,
        "Export after a single edit reuses other pads and cache stays bounded");
  std::swap(kit.pads[0], kit.pads[31]);
  plan = sp::planBank(kit);
  const auto movedCount = cache.renderCount();
  check(cache.render(kit, plan, 31).values == cut.values &&
            cache.renderCount() == movedCount,
        "Pad moves carry processing without invalidating audio");
  kit.pads[31].reverse = true;
  plan = sp::planBank(kit);
  cache.render(kit, plan, 31);
  check(cache.renderCount() == movedCount + 1,
        "Reverse invalidates the affected sound");
  auto longKit = single(tone("LONG.wav", 4, .2, 2));
  for (int i = 1; i < 8; ++i) {
    auto a = tone("LONG" + String(i) + ".wav", 4, .2, 2, false, 300 + i);
    longKit.pool.push_back(a);
    longKit.pads[i] = sp::makePad(*a, i, true, true);
  }
  const auto longPlan = sp::planBank(longKit);
  auto optimized = cache.render(longKit, longPlan, 0);
  const auto all = sp::process(longKit);
  check(optimized.values == all.sounds[0].values &&
            optimized.removed == all.sounds[0].removed &&
            optimized.values.size() == (size_t)longPlan.frames[0],
        "Planned preview matches the full export without bank shortening");
  // Individual overlong samples still receive the approved slot-limit cut.
  longKit = single(longKit.pool[0]);
  longKit.autoTune = false;
  longKit.fadeOutOnExport = true;
  const auto limitedPlan = sp::planBank(longKit);
  const auto faded = cache.render(longKit, limitedPlan, 0);
  longKit.pads[0].loopEnabled = true;
  const auto loopingPlan = sp::planBank(longKit);
  const auto count = cache.renderCount();
  auto unfaded = cache.render(longKit, loopingPlan, 0);
  check(cache.renderCount() == count && unfaded.values != faded.values,
        "Loop On removes fade using cached original audio without resampling");
  longKit.pads[0].loopSeconds = 1e-9;
  bool invalidLoop = false;
  try {
    sp::planBank(longKit);
  } catch (const sp::LoopError &error) {
    invalidLoop = error.plan && error.selections &&
                  error.plan->frames[0] == loopingPlan.frames[0] &&
                  cache.render(longKit, *error.plan, 0).values.size() >=
                      unfaded.values.size();
  }
  check(invalidLoop, "Invalid loop retains the individual slot-limit plan for "
                     "accurate audition");
  auto rounding = single(tone("ROUND.wav", 6));
  rounding.autoTrim = false;
  rounding.pool[0]->sampleRate = 22050;
  rounding.pool[0]->audio.setSize(1, 115920, true);
  rounding.pads[0] = sp::makePad(*rounding.pool[0], 0, false, true);
  const auto exact = sp::planBank(rounding);
  check(exact.sounds[0].tune == 3 && exact.frames[0] == 64171 &&
            cache.render(rounding, exact, 0).values.size() == 64171,
        "Metadata frame count uses the resampler's exact rounding order");
  auto cancelled = single(tone("CANCEL.wav", 4));
  auto cancelledPlan = sp::planBank(cancelled);
  const auto oldCount = cache.renderCount();
  int polls = 0;
  bool stopped = false;
  try {
    cache.render(cancelled, cancelledPlan, 0, [&] { return ++polls == 5; });
  } catch (const sp::ProcessingCancelled &) {
    stopped = true;
  }
  check(stopped && polls == 5 && cache.renderCount() == oldCount,
        "Obsolete rendering stops within audio processing and is never cached");
  std::cout << "PASS: lazy planning, cache invalidation, export parity, "
               "individual limits and cancellation\n";
}
static void testExportSettings() {
  check(!sp::Project{}.fadeOutOnExport && sp::Project{}.normalizeOnExport,
        "Export defaults are Fade Off and Normalize On");
  auto kit = single(tone("SETTINGS.wav", .1, .01, 1, false, 431));
  kit.autoTrim = false;
  const auto bytes = kit.pool[0]->original;
  const auto plan = sp::planBank(kit);
  sp::RenderCache cache;
  const auto normalized = cache.render(kit, plan, 0);
  check(normalized.gainDb > 30,
        "Normalization defaults to the existing target");
  kit.normalizeOnExport = false;
  const auto dry = cache.render(kit, plan, 0);
  int peak = 0;
  for (int value : dry.values)
    peak = std::max(peak, std::abs(value - 2048));
  check(dry.gainDb == 0 && peak >= 19 && peak <= 22 &&
            dry.values.back() != 2048 && cache.renderCount() == 1,
        "Normalize Off uses unity gain and Fade Off leaves the one-shot tail");
  kit.fadeOutOnExport = true;
  const auto faded = cache.render(kit, plan, 0);
  const int fadeFrames = (int)(.04 * sp::rate);
  check(faded.values.size() == dry.values.size() &&
            faded.values.back() == 2048 && faded.gainDb == 0 &&
            cache.renderCount() == 1 &&
            std::equal(dry.values.begin(), dry.values.end() - fadeFrames,
                       faded.values.begin()),
        "Fade On affects only the final 40 ms, including unshortened samples");
  kit.normalizeOnExport = true;
  const auto both = cache.render(kit, plan, 0);
  peak = 0;
  for (int value : both.values)
    peak = std::max(peak, std::abs(value - 2048));
  check(both.values.back() == 2048 &&
            std::abs(peak - std::pow(10., -.5 / 20.) * 2048) <= 1 &&
            cache.renderCount() == 1,
        "Normalize On measures the finished fade and reaches -0.5 dBFS");
  kit.fadeOutOnExport = false;
  check(
      cache.render(kit, plan, 0).values == normalized.values &&
          cache.renderCount() == 1,
      "Toggling settings restores audio without cumulative gain or DSP misses");
  kit.pads[0].loopEnabled = true;
  kit.pads[0].loopSeconds = .035;
  const auto loop = cache.render(kit, sp::planBank(kit), 0);
  kit.fadeOutOnExport = true;
  const auto loopWithFadeSetting = cache.render(kit, sp::planBank(kit), 0);
  check(loopWithFadeSetting.values == loop.values &&
            loopWithFadeSetting.loopFrames == loop.loopFrames &&
            cache.renderCount() == 1,
        "Fade setting never fades loops or changes their snapped bounds");
  kit.normalizeOnExport = false;
  const auto loopDry = cache.render(kit, sp::planBank(kit), 0);
  const auto exported = sp::renderBank(kit, sp::planBank(kit), cache);
  check(exported.sounds[0].values == loopDry.values &&
            exported.sounds[0].gainDb == 0 && kit.pool[0]->original == bytes,
        "Export and preview apply the same settings without editing originals");
  const auto dir = File::getSpecialLocation(File::tempDirectory)
                       .getChildFile("sp-export-settings-" + Uuid().toString());
  dir.createDirectory();
  const auto file = dir.getChildFile("kit.spkit");
  sp::savePreset(kit, file, &exported);
  const auto restored = sp::loadPreset(file);
  check(restored.fadeOutOnExport && !restored.normalizeOnExport &&
            sp::process(restored).bank == exported.bank,
        "Kit restores export settings and exactly reproduces exported audio");
  dir.deleteRecursively();
  std::cout << "PASS: export toggles, fade timing, unity/normalized gain, "
               "loop safety, cache reuse and kit roundtrip\n";
}
static void testAutoTrimBankPressure() {
  // Regression for ALTO2.VC: 32 ~1.164 s sounds used to be cut to ~0.625 s.
  auto a = tone("ALTO2.VC.wav", 1.163636363636, .2);
  auto kit = single(a);
  for (int i = 0; i < 32; ++i)
    kit.pads[i] = sp::makePad(*a, i, true, true);
  const auto sourceBytes = a->original;
  const auto before = sp::selectionMetadata(kit);
  const auto failure = memoryFailure(kit);
  check(failure.excessFrames > 0 &&
            String(failure.what()).contains("Auto Trim removes silence only"),
        "Overfull auto-trim kit reports its deficit instead of cutting sounds");
  for (int i = 0; i < 32; ++i)
    check(before[i].end - before[i].start > 1.16 && before[i].removed == 0 &&
              before[i].tune == 16,
          "Sub-limit selections retain their audible tails on every pad");
  auto preview = sp::renderPadPreview(kit, 6);
  check(preview.values.size() > sp::rate && preview.removed == 0,
        "Memory-invalid auto-trim pad auditions its complete selected audio");
  // Fit by removing actual silence, rather than shortening audible material.
  auto silence = single(tone("SILENCE.wav", 1.2));
  const int n = silence.pool[0]->audio.getNumSamples();
  for (int j = 0; j < n; ++j)
    silence.pool[0]->audio.setSample(0, j, j >= n / 3 && j < n / 2 ? .2f : 0.f);
  for (int i = 0; i < 32; ++i)
    silence.pads[i] = sp::makePad(*silence.pool[0], i, true, true);
  const auto fit = sp::planBank(silence);
  for (const auto &r : fit.sounds)
    check(
        r.start > .39 && r.end < .61 && r.removed == 0 && r.tune == 16,
        "Global Auto Trim still removes silence and preserves the attack/tail");
  const auto dir = File::getSpecialLocation(File::tempDirectory)
                       .getChildFile("sp-autotrim-" + Uuid().toString());
  dir.createDirectory();
  const auto file = dir.getChildFile("overfull.spkit");
  sp::savePreset(kit, file);
  const auto restored = sp::loadPreset(file);
  check(restored.autoTrim && restored.pool[0]->original == sourceBytes &&
            memoryFailure(restored).excessFrames == failure.excessFrames,
        "Overfull automatic kit remains saveable and restores without cuts");
  auto eight = kit;
  for (int i = 8; i < 32; ++i)
    eight.pads[i] = sp::Project{}.pads[i];
  const auto fitting = sp::planBank(eight);
  for (int i = 0; i < 8; ++i)
    check(fitting.sounds[i].start == before[i].start &&
              fitting.sounds[i].end == before[i].end &&
              fitting.sounds[i].removed == 0,
          "Clearing other pads changes memory validity, not remaining cuts");
  dir.deleteRecursively();
  std::cout << "PASS: Auto Trim under bank pressure preserves short sounds, "
               "trims real silence and saves full kits\n";
}
static void testZeroCrossingLoops() {
  auto kit = single(tone("ZERO.wav", .3, .13, 2, true, 437));
  kit.autoTrim = false;
  auto &pad = kit.pads[0];
  pad.manualTrim = true;
  pad.start = .013;
  pad.end = .1479;
  pad.loopSeconds = .049;
  sp::RenderCache cache;
  const auto raw = cache.render(kit, sp::planBank(kit), 0);
  pad.loopEnabled = true;
  const auto snapped = cache.render(kit, sp::planBank(kit), 0);
  const int n = (int)snapped.values.size();
  const int start = n - snapped.loopFrames;
  const int requestedStart =
      (int)raw.values.size() - (int)std::round(pad.loopSeconds * sp::rate);
  check(snapped.left && cache.renderCount() == 1,
        "Zero search uses processed left fallback and reuses resampled audio");
  check(n <= (int)raw.values.size() &&
            ((int)raw.values.size() - n) / double(sp::rate) <= .005 &&
            std::abs(start - requestedStart) / double(sp::rate) <= .005,
        "Both crossings stay within five compensated milliseconds and bounds");
  check(n < (int)raw.values.size() && raw.values[n - 1] < 2048 &&
            raw.values[n] >= 2048 && start > 0 &&
            snapped.values[start - 1] < 2048 && snapped.values[start] >= 2048,
        "Loop start and exclusive End use rising processed zero crossings");
  check(std::abs(snapped.playbackEnd - (snapped.start + n / double(sp::rate))) <
                1e-9 &&
            std::abs(snapped.loopStart -
                     (snapped.start + start / double(sp::rate))) < 1e-9,
        "Displayed snapped handles describe the exact exported frame spans");
  const auto repeated = cache.render(kit, sp::planBank(kit), 0);
  check(repeated.values == snapped.values &&
            repeated.loopStart == snapped.loopStart &&
            repeated.playbackEnd == snapped.playbackEnd &&
            repeated.gainDb == snapped.gainDb && cache.renderCount() == 1,
        "Repeated snapping is deterministic and never accumulates gain");
  pad.loopSeconds = .023;
  auto edited = cache.render(kit, sp::planBank(kit), 0);
  check(cache.renderCount() == 1 && edited.values == snapped.values &&
            edited.loopFrames != snapped.loopFrames,
        "Moving only the blue handle needs no additional resampling");
  const auto complete = sp::renderBank(kit, sp::planBank(kit), cache);
  const auto *bytes = (const uint8 *)complete.bank.getData();
  const int descriptor = 0x9262 + 24 * 10;
  check(((bytes[descriptor + 8] | bytes[descriptor + 9] << 8) ^ 65535) ==
                edited.loopFrames &&
            complete.sounds[0].values == edited.values,
        "Hardware descriptor and audition share the snapped frame span");
  pad.loopEnabled = false;
  auto off = cache.render(kit, sp::planBank(kit), 0);
  check(off.values == raw.values && off.loopFrames == 0 && off.loopStart < 0 &&
            off.playbackEnd < 0 && cache.renderCount() == 1,
        "Loop Off restores one-shot audio from the intact cache");
  pad.loopEnabled = true;
  pad.loopSeconds = 1. / sp::rate;
  auto tiny = cache.render(kit, sp::planPadPreview(kit, 0), 0);
  check(tiny.values == raw.values && tiny.loopFrames == 1,
        "Snapping never enlarges a tiny invalid loop to hide export errors");
  rejects([&] { sp::planBank(kit); }, "Tiny snapped-loop export stays blocked");
  auto quiet = single(tone("QUIET.wav", .1, 0));
  quiet.autoTrim = false;
  quiet.pads[0].loopEnabled = true;
  auto silence = sp::process(quiet).sounds[0];
  check(silence.loopFrames == (int)silence.values.size() &&
            silence.playbackEnd == .1 && silence.loopStart == 0 &&
            silence.gainDb == 0,
        "Silent selections and full-loop default stay unchanged");
  auto dc = single(tone("DC.wav", .1));
  dc.autoTrim = false;
  for (int i = 0; i < dc.pool[0]->audio.getNumSamples(); ++i)
    dc.pool[0]->audio.setSample(0, i, .2f);
  const auto dcRaw = sp::process(dc).sounds[0];
  dc.pads[0].loopEnabled = true;
  dc.pads[0].loopSeconds = .025;
  const auto dcLoop = sp::process(dc).sounds[0];
  check(dcLoop.values == dcRaw.values &&
            dcLoop.loopFrames == (int)std::round(.025 * sp::rate) &&
            dcLoop.playbackEnd == .1,
        "No crossing preserves requested boundaries without DC processing");
  auto pitched = single(tone("PITCH-ZERO.wav", 4, .09, 1, false, 173));
  pitched.pads[0].manualTrim = true;
  pitched.pads[0].end = 3.9;
  pitched.pads[0].start = .1;
  pitched.pads[0].reverse = true;
  const auto pitchRaw = sp::process(pitched).sounds[0];
  pitched.pads[0].loopEnabled = true;
  pitched.pads[0].loopSeconds = .731;
  const auto pitchLoop = sp::process(pitched).sounds[0];
  const double fps = sp::rate * sp::pitchRatio(pitchLoop.tune);
  check(pitchLoop.tune < 16 &&
            (pitchRaw.values.size() - pitchLoop.values.size()) / fps <= .005 &&
            std::abs(pitchLoop.loopStart -
                     (pitchLoop.start + pitchRaw.values.size() / fps - .731)) <=
                .005 + 1. / fps,
        "Reverse/Pitch Fit snapping uses a bounded compensated time window");
  int peak = 0;
  for (int value : pitchLoop.values)
    peak = std::max(peak, std::abs(value - 2048));
  check(std::abs(peak - std::pow(10., -.5 / 20.) * 2048) <= 1,
        "Normalization after snapping still reaches the 12-bit peak target");
  const auto dir = File::getSpecialLocation(File::tempDirectory)
                       .getChildFile("sp-snap-" + Uuid().toString());
  dir.createDirectory();
  const auto preset = dir.getChildFile("snap.spkit");
  sp::savePreset(pitched, preset);
  const auto restored = sp::process(sp::loadPreset(preset)).sounds[0];
  check(restored.values == pitchLoop.values &&
            restored.playbackEnd == pitchLoop.playbackEnd &&
            restored.loopStart == pitchLoop.loopStart,
        "Portable kit restores snapped positions without drift");
  dir.deleteRecursively();
  std::cout << "PASS: zero-crossing loops, cache reuse, bounds, silence, "
               "Pitch Fit, Reverse, normalization and portable kits\n";
}
static void benchmarkLoopUpdates() {
  auto p = single(tone("BENCH.wav", 2.4));
  p.autoTrim = false;
  p.pads[0].loopEnabled = true;
  auto plan = sp::planBank(p);
  sp::RenderCache cache;
  cache.render(p, plan, 0);
  constexpr int runs = 300;
  int64_t checksum = 0;
  for (bool on : {false, true}) {
    p.pads[0].loopEnabled = on;
    const double before = Time::getMillisecondCounterHiRes();
    for (int i = 0; i < runs; ++i) {
      p.pads[0].loopSeconds = .7 + (i % 17) * .0001;
      const auto r = cache.render(p, plan, 0);
      checksum += r.loopFrames + r.values.back();
    }
    std::cout << "Cached Loop " << (on ? "On" : "Off") << ": "
              << (Time::getMillisecondCounterHiRes() - before) / runs
              << " ms/update (normalization + quantization included)\n";
  }
  check(cache.renderCount() == 1, "Benchmark performs one initial resampling");
  std::cout << "Resamplings: " << cache.renderCount()
            << ", checksum: " << checksum << "\n";
}
static int auditInstrumentNames(bool test) {
  const auto lines = StringArray::fromLines(
      File(PROJECT_ROOT)
          .getChildFile("tests/fixtures/dr-sample-instruments.tsv")
          .loadFileAsString());
  int mismatches = 0, total = 0;
  for (const auto &line : lines) {
    if (line.isEmpty())
      continue;
    const auto expected = line.upToFirstOccurrenceOf("\t", false, false);
    const auto filename = line.fromFirstOccurrenceOf("\t", false, false);
    const auto actual =
        sp::classify(File(filename).getFileNameWithoutExtension());
    ++total;
    if (actual != expected) {
      ++mismatches;
      std::cout << filename << ": " << actual << " (expected " << expected
                << ")\n";
    }
    if (test)
      check(actual == expected,
            ("Instrument recognition: " + filename).toRawUTF8());
  }
  if (test)
    check(total == 233, "Complete Dr Sample From Mars filename fixture");
  std::cout << "Instrument audit: " << total << " filenames, " << mismatches
            << " mismatches\n";
  return mismatches ? 1 : 0;
}
static void testLoops(const File &hardwareOutput = {}) {
  check(sp::loopSecondsAfterTrim(1, 3, .5, 1, 4, 1000) == 1.5,
        "Extending End anchors the loop at its source position");
  check(std::abs(sp::loopSecondsAfterTrim(1, 3, .5, 1, 2, 1000) - .001) < 1e-9,
        "End crossing loop clamps to one playable source frame");
  check(sp::loopSecondsAfterTrim(1, 3, .5, 2.7, 3, 1000) < .301,
        "Start crossing loop clamps it against Start");
  check(sp::loopSecondsAfterTrim(1, 3, .5, 2, 4, 1000) == .5,
        "Moving selection preserves loop duration");
  check(sp::loopSecondsAfterTrim(1, 3, -1, 1, 4, 1000) == -1,
        "Full-loop sentinel continues to follow selection");
  const auto root = File(PROJECT_ROOT);
  // Private hardware captures are optional; public tests use synthetic audio.
  const auto hardwareFixture = root.getChildFile("DemoBanks/LRT.sp12");
  if (hardwareFixture.existsAsFile()) {
    MemoryBlock hardware;
    check(hardwareFixture.loadFileAsData(hardware),
          "Load hardware loop fixture");
    auto restored = hardware;
    sp::writeLoopDescriptor(restored, 1, 0);
    sp::writeLoopDescriptor(restored, 1, 5208);
    check(restored == hardware,
          "Native loop writer reproduces hardware LRT byte for byte");
    const auto *hb = (const uint8 *)hardware.getData();
    check(hb[0x9362] == 0x76 && hb[0x9363] == 0x8e && hb[0x9364] == 0xa7 &&
              hb[0x9365] == 0xeb,
          "Hardware loop start and bitwise-complement span");
    sp::validateBank(restored);
    for (int span : {-1, 1, 2, 20833, 65536})
      rejects([&] { sp::writeLoopDescriptor(restored, 1, span); },
              "Invalid loop spans rejected");
  } else {
    std::cout << "Hardware LRT capture not supplied; running synthetic loop tests\n";
  }
  sp::Project codec;
  std::array<sp::Render, 32> sounds;
  for (int i = 0; i < 2; ++i) {
    codec.pads[i].name = "LOOP";
    sounds[i].values.assign(i == 0 ? 2000 : 1000, 2048);
  }
  std::array<int, 8> used;
  const auto off = sp::buildBank(codec, sounds, used);
  codec.pads[1].loopEnabled = true;
  codec.pads[1].loopSeconds = 500. / sp::rate;
  auto looped = sp::buildBank(codec, sounds, used);
  const auto *b = (const uint8 *)looped.getData();
  check(b[0x935e] == 0xdc && b[0x935f] == 0x07,
        "Loop pad has a nonzero allocation start");
  check(b[0x9362] == 0xd2 && b[0x9363] == 0x09 && b[0x9364] == 0x0b &&
            b[0x9365] == 0xfe,
        "Zone-relative loop start and complemented 500-frame span");
  int differences = 0;
  for (size_t i = 0; i < off.getSize(); ++i)
    if (((const uint8 *)off.getData())[i] != b[i]) {
      ++differences;
      check(i >= 0x9362 && i < 0x9366, "Only loop descriptor bytes change");
    }
  check(differences > 0, "Loop descriptor differs from Off");
  sounds[0].values.resize(4000, 2048);
  looped = sp::buildBank(codec, sounds, used);
  b = (const uint8 *)looped.getData();
  check((b[0x9362] | b[0x9363] << 8) == 4514,
        "Repacking recalculates loop address");
  codec.pads[1].loopEnabled = false;
  auto rebuiltOff = sp::buildBank(codec, sounds, used);
  sp::writeLoopDescriptor(looped, 1, 0);
  check(looped == rebuiltOff, "Disabling loop restores exact Off bank");
  codec.pads[1].loopEnabled = true;
  codec.pads[1].loopSeconds = 2. / sp::rate;
  rejects([&] { sp::buildBank(codec, sounds, used); },
          "Two-frame loop blocks export");
  auto damaged = off;
  sp::writeLoopDescriptor(damaged, 1, 500);
  ((uint8 *)damaged.getData())[0x9362] ^= 1;
  rejects([&] { sp::validateBank(damaged); }, "Invalid loop end rejected");

  auto p = single(tone("LOOP.wav", 4));
  p.pads[0].loopEnabled = true;
  p.pads[0].manualTrim = true;
  p.pads[0].start = 0;
  p.pads[0].end = 4;
  auto r = sp::process(p);
  check(r.sounds[0].tune < 16 &&
            r.sounds[0].loopFrames <= (int)r.sounds[0].values.size() &&
            r.sounds[0].loopStart - r.sounds[0].start <= .005,
        "Full-selection loop follows Pitch Fit storage with bounded snapping");
  p.pads[0].loopSeconds = .5;
  r = sp::process(p);
  check(std::abs(r.sounds[0].loopFrames /
                     (sp::rate * sp::pitchRatio(r.sounds[0].tune)) -
                 .5) <= .01,
        "Tail loop duration converts using compensated tune with bounded "
        "snapping");
  p.pads[0].end = .2;
  r = sp::process(p);
  sp::clampLoopLengths(p, r.sounds);
  check(std::abs(p.pads[0].loopSeconds - .2) < .0001,
        "Trim permanently shortens manual loop duration");
  p.pads[0].end = .8;
  sp::clampLoopLengths(p, sp::selectionMetadata(p));
  check(std::abs(p.pads[0].loopSeconds - .2) < .0001,
        "Expanding selection never regrows a manual loop");
  p.pads[0].start = 1;
  p.pads[0].end = 1.8;
  p.pads[0].reverse = true;
  sp::setAutoTune(p, false);
  r = sp::process(p);
  check(std::abs(r.sounds[0].loopFrames / double(sp::rate) - .2) <= .01 &&
            p.pads[0].start == 1 && p.pads[0].end == 1.8,
        "Selection shift, Reverse and Pitch Fit preserve loop seconds");
  p.pads[0].loopSeconds = -1;
  sp::clampLoopLengths(p, r.sounds);
  check(p.pads[0].loopSeconds == -1, "Full-selection sentinel remains dynamic");
  check(!sp::makePad(*p.pool[0], 1, false, false).loopEnabled &&
            sp::makePad(*p.pool[0], 1, false, false).loopSeconds == -1,
        "New assignment resets loop settings");

  auto longConstant = tone("CONSTANT.wav", 3);
  for (int i = 0; i < longConstant->audio.getNumSamples(); ++i)
    longConstant->audio.setSample(0, i, .2f);
  auto cut = single(longConstant);
  cut.autoTune = false;
  cut.fadeOutOnExport = true;
  auto faded = sp::process(cut);
  cut.pads[0].loopEnabled = true;
  cut.pads[0].loopSeconds = 3;
  auto unfaded = sp::process(cut);
  sp::clampLoopLengths(cut, unfaded.sounds);
  check(faded.sounds[0].values.back() == 2048 &&
            unfaded.sounds[0].values.back() > 3500 &&
            cut.pads[0].loopSeconds < 2.5,
        "Loop skips tail fade and clamps to automatically shortened selection");
  cut.autoTune = true;
  cut.pads[0].loopSeconds = -1;
  r = sp::process(cut);
  check(r.sounds[0].loopFrames == (int)r.sounds[0].values.size(),
        "Full loop expands with longer effective selection");

  const auto dir = File::getSpecialLocation(File::tempDirectory)
                       .getChildFile("sp-loop-" + Uuid().toString());
  dir.createDirectory();
  const auto preset = dir.getChildFile("loop.spkit");
  p.pads[0].loopSeconds = .15;
  p.pads[1] = p.pads[0];
  p.pads[1].loopEnabled = false;
  std::swap(p.pads[0], p.pads[1]);
  sp::savePreset(p, preset);
  auto loaded = sp::loadPreset(preset);
  check(!loaded.pads[0].loopEnabled && loaded.pads[1].loopEnabled &&
            loaded.pads[0].loopSeconds == .15 &&
            loaded.pads[1].loopSeconds == .15,
        "Preset retains moved enabled/disabled loop settings");
  for (auto &pad : p.pads)
    pad = p.pads[1];
  for (auto &pad : p.pads) {
    pad.start = 0;
    pad.end = 4;
  }
  rejects([&] { sp::process(p); }, "Overfull loop kit cannot export");
  sp::savePreset(p, preset);
  loaded = sp::loadPreset(preset);
  check(loaded.pads[31].loopEnabled && loaded.pads[31].loopSeconds == .15,
        "Overfull kit still saves and restores loops");
  const auto preview = sp::renderPadPreview(loaded, 31);
  check(preview.loopFrames > 0 && preview.values.size() > sp::maxFrames,
        "Invalid-bank preview preserves tail loop");
  // Overfull automatic kits retain their complete requested loop lengths.
  auto budget = single(tone("BUDGET.wav", 2.4));
  for (int i = 0; i < 32; ++i) {
    budget.pads[i] = sp::makePad(*budget.pool[0], i, true, true);
    budget.pads[i].loopEnabled = true;
    budget.pads[i].loopSeconds = 2.4;
  }
  sp::savePreset(budget, preset);
  loaded = sp::loadPreset(preset);
  const auto preserved = sp::selectionMetadata(budget);
  for (int i = 0; i < 32; ++i)
    check(
        std::abs(loaded.pads[i].loopSeconds -
                 (preserved[i].end - preserved[i].start -
                  preserved[i].removed)) < 1e-6,
        "Saving an overfull automatic kit preserves the complete loop lengths");
  auto loopFailure = budget;
  for (int i = 8; i < 32; ++i)
    loopFailure.pads[i] = sp::Project{}.pads[i];
  loopFailure.pads[0].loopSeconds = 1. / sp::rate;
  try {
    sp::process(loopFailure);
    check(false, "Expected bank loop failure");
  } catch (const sp::LoopError &error) {
    check(error.selections && (*error.selections)[0].removed == 0 &&
              (*error.selections)[0].values.empty(),
          "Invalid loop preserves full selection metadata without audio "
          "copies");
  }

  auto automatic = single(tone("AUTO.wav", 1.5));
  for (int i = 0; i < automatic.pool[0]->audio.getNumSamples(); ++i)
    automatic.pool[0]->audio.setSample(0, i,
                                       i < .2 * 44100 || i >= 44100 ? 0.f
                                       : i < .4 * 44100             ? .2f
                                                                    : .02f);
  automatic.threshold = -50;
  automatic.pads[0].loopSeconds = .7;
  automatic.pads[0].loopEnabled = true;
  sp::clampLoopLengths(automatic, sp::process(automatic).sounds);
  automatic.threshold = -20;
  sp::clampLoopLengths(automatic, sp::process(automatic).sounds);
  const double shortenedLoop = automatic.pads[0].loopSeconds;
  check(shortenedLoop < .21 && shortenedLoop > .2,
        "Threshold shrink permanently clamps custom loop");
  sp::setAutoTrim(automatic, false);
  sp::clampLoopLengths(automatic, sp::process(automatic).sounds);
  check(automatic.pads[0].loopSeconds == shortenedLoop,
        "Auto Trim Off does not regrow custom loop");
  automatic.pads[0].manualTrim = true;
  automatic.pads[0].start = 0;
  automatic.pads[0].end = .1;
  sp::clampLoopLengths(automatic, sp::process(automatic).sounds);
  sp::resetTrim(automatic, 0);
  sp::clampLoopLengths(automatic, sp::process(automatic).sounds);
  check(automatic.pads[0].loopSeconds == -1 && automatic.pads[0].loopEnabled,
        "Reset Trim restores whole-selection loop and retains Loop On");
  automatic.pads[0].loopSeconds = .05;
  automatic.pads[0].loopEnabled = false;
  sp::setAutoTrim(automatic, true);
  check(automatic.pads[0].loopSeconds == .05,
        "Changing Auto Trim preserves the custom loop independently of Reset "
        "Trim");
  sp::resetTrim(automatic, 0);
  check(automatic.pads[0].loopSeconds == -1 && !automatic.pads[0].loopEnabled,
        "Reset Trim with Auto Trim On resets stored point while retaining Loop "
        "Off");
  auto pitchModes = single(tone("PITCH.wav", 4));
  pitchModes.autoTrim = false;
  sp::resetTrim(pitchModes, 0);
  pitchModes.pads[0].loopSeconds = 3;
  sp::setAutoTune(pitchModes, false);
  sp::clampLoopLengths(pitchModes, sp::process(pitchModes).sounds);
  const double shortPitchLoop = pitchModes.pads[0].loopSeconds;
  sp::setAutoTune(pitchModes, true);
  sp::clampLoopLengths(pitchModes, sp::process(pitchModes).sounds);
  check(shortPitchLoop < 2.5 &&
            pitchModes.pads[0].loopSeconds == shortPitchLoop,
        "Pitch Fit changes permanently clamp automatic loops");

  if (hardwareOutput != File()) {
    hardwareOutput.createDirectory();
    auto kit = single(tone("FULL.wav", 1, .1, 1, false, 440));
    kit.autoTrim = false;
    sp::resetTrim(kit, 0);
    kit.pads[0].loopEnabled = true;
    auto tail = tone("TAIL.wav", 1, .1, 1, false, 660);
    auto pitched = tone("PITCH.wav", 4, .1, 1, false, 220);
    kit.pool.push_back(tail);
    kit.pool.push_back(pitched);
    kit.pads[1] = sp::makePad(*tail, 1, false, true);
    kit.pads[1].loopEnabled = true;
    kit.pads[1].loopSeconds = .25;
    kit.pads[2] = sp::makePad(*pitched, 2, false, true);
    kit.pads[2].loopEnabled = true;
    kit.pads[2].loopSeconds = .5;
    kit.pads[3] = sp::makePad(*tail, 3, false, true); // One-shot comparison.
    kit.pads[3].channel = 1; // Stops A1 on hardware sharing this channel.
    const auto native = sp::process(kit);
    sp::atomicWrite(hardwareOutput.getChildFile("CPPLOOP.sp12"), native.bank);
    sp::savePreset(kit, hardwareOutput.getChildFile("CPPLOOP.spkit"), &native);
  }
  dir.deleteRecursively();
}
int main(int argc, char **argv) {
  ScopedJuceInitialiser_GUI init;
  if (argc == 2 && String(argv[1]) == "--benchmark-loops") {
    benchmarkLoopUpdates();
    return 0;
  }
  if (argc == 2 && String(argv[1]) == "--audit-instruments")
    return auditInstrumentNames(false);
  try {
    testMirroredAutoMap();
    testReversibleAutoMap();
    testExportSettings();
    testAutoTrimBankPressure();
    testZeroCrossingLoops();
    testLazyRendering();
    testLoops(argc == 2
                  ? File::getCurrentWorkingDirectory().getChildFile(argv[1])
                  : File());
    check(!sp::Project{}.autoTrim && !sp::Project{}.autoSort,
          "New projects default both automatic modes to Off");
    check(sp::bankFileStem(String::fromUTF8("Müller Größe")) ==
              "Mueller Groesse",
          "Bank filename transliterates umlauts and sharp S");
    check(sp::bankFileStem(String::fromUTF8("ÄÖÜẞ")) == "AeOeUeSS",
          "Bank filename transliterates uppercase umlauts");
    check(sp::bankFileStem(String::fromUTF8("Müller")) == "Mueller",
          "Bank filename handles decomposed macOS umlauts");
    check(sp::bankFileStem("12345678901234567890") == "1234567890123456",
          "Bank filename limits stem to sixteen characters");
    check(sp::bankFileStem("  Drum Kit_01-AB  ") == "Drum Kit_01-AB",
          "Bank filename preserves safe ASCII");
    check(sp::bankFileStem(String::fromUTF8("🎹/:?")) == "KIT",
          "Bank filename has safe fallback after filtering");
    auto root = File(PROJECT_ROOT);
    for (auto f : root.getChildFile("DemoBanks")
                      .findChildFiles(File::findFiles, false, "*.sp12")) {
      MemoryBlock b;
      f.loadFileAsData(b);
      sp::validateBank(b);
      ++checks;
    }
    sp::Project codec;
    std::array<sp::Render, 32> renders;
    for (int i = 0; i < 32; ++i) {
      codec.pads[i].name = sp::padName(i);
      codec.pads[i].channel = i % 8 + 1;
      renders[i].tune = i;
      renders[i].values.resize(4096);
      std::iota(renders[i].values.begin(), renders[i].values.end(), 0);
    }
    std::array<int, 8> used;
    auto bank = sp::buildBank(codec, renders, used);
    MemoryBlock expected;
    root.getChildFile("tests/fixtures/codec32.sp12").loadFileAsData(expected);
    check(bank == expected, "C++ writer must match Python byte for byte for "
                            "all values and 32 pads");
    auto corrupt = bank;
    ((uint8 *)corrupt.getData())[0x9258] ^= 1;
    rejects([&] { sp::validateBank(corrupt); }, "Corrupt counters rejected");
    check(sp::classify("BD-Rock_01") == "BD", "Kick alias");
    check(sp::classify("Closed_HH-02") == "CH", "Closed hat");
    check(sp::classify("open-HH") == "OH", "Open hat");
    sp::Project aliases;
    aliases.pool = {tone("Kit_hHo_01.wav"), tone("Kit_CYM_01.wav"),
                    tone("Kit_CYM_Ride.wav")};
    sp::sortPads(aliases);
    check(
        aliases.pads[5].asset == aliases.pool[0]->id &&
            aliases.pads[7].asset == aliases.pool[1]->id &&
            aliases.pads[6].asset == aliases.pool[2]->id,
        "New HHO/CYM aliases map to A6/A8, with explicit Ride taking priority");
    check(sp::classify("MyHHOish_MyCYMish") == "Other",
          "HHO/CYM recognition preserves token boundaries");
    check(sp::classify("tom_high") == "High Tom", "Tom pitch");
    check(sp::classify("MyKickish") == "Other", "Token boundaries");
    auditInstrumentNames(true);
    check(sp::classify("SD_side-stick_room") == "Rim" &&
              sp::classify("SD_crossstick_room") == "Rim",
          "Side/cross stick takes priority over generic snare");
    check(sp::classify("Tom LoFi") == "Tom" &&
              sp::classify("Conga LoFi") == "Conga",
          "LoFi processing does not imply low-pitched tom or conga");
    check(sp::classify("CH Ride Bell") == "CH" &&
              sp::classify("OH Splash") == "OH",
          "Explicit hi-hat labels take priority over descriptive cymbal words");
    check(
        sp::classify("Perc Bongos") == "Bongo" &&
            sp::classify("Perc Timbales") == "Timbale" &&
            sp::classify("Perc Cowbell") == "Cowbell",
        "Specific percussion names take priority over the generic Perc label");
    check(sp::classify("MyBongoish_MyPercussionish") == "Other",
          "Extended instrument recognition preserves token boundaries");
    sp::Project percussionOrder;
    for (const auto *filename :
         {"Unknown.wav", "Perc Door.wav", "Timbale.wav", "Bongo.wav"})
      percussionOrder.pool.push_back(tone(filename));
    sp::sortPads(percussionOrder);
    check(sp::assetFor(percussionOrder, percussionOrder.pads[24].asset)
                      ->category == "Bongo" &&
              sp::assetFor(percussionOrder, percussionOrder.pads[25].asset)
                      ->category == "Timbale" &&
              sp::assetFor(percussionOrder, percussionOrder.pads[26].asset)
                      ->category == "Percussion" &&
              sp::assetFor(percussionOrder, percussionOrder.pads[27].asset)
                      ->category == "Other",
          "Extra percussion groups are ordered on D ahead of unclassified "
          "sounds");
    sp::Project mapping;
    for (auto n : {"SD 2.wav", "BD 2.wav", "BD 1.wav", "OH.wav", "Tom 1.wav",
                   "Tom 2.wav", "Cowbell.wav"})
      mapping.pool.push_back(tone(n));
    sp::sortPads(mapping);
    check(sp::assetFor(mapping, mapping.pads[0].asset)->filename == "BD 1.wav",
          "Stable primary kick");
    check(mapping.pads[2].asset.isEmpty(), "Missing clap stays empty");
    check(!mapping.pads[5].asset.isEmpty(), "Open hat fixed");
    check(!mapping.pads[8].asset.isEmpty() && !mapping.pads[9].asset.isEmpty(),
          "Generic tom assignment");
    check(sp::assetFor(mapping, mapping.pads[16].asset)->filename == "BD 2.wav",
          "Variant on C1");
    auto quiet = single(tone("BD.wav", .1, .01));
    auto loud = single(tone("BD.wav", .1, .8));
    auto qr = sp::process(quiet), lr = sp::process(loud);
    auto peak = [](const sp::Render &r) {
      int v = 0;
      for (int x : r.values)
        v = std::max(v, std::abs(x - 2048));
      return v / 2048.;
    };
    check(std::abs(peak(qr.sounds[0]) - std::pow(10., -.5 / 20.)) <= 1. / 2048,
          "Quiet normalization");
    check(std::abs(peak(lr.sounds[0]) - std::pow(10., -.5 / 20.)) <= 1. / 2048,
          "Loud normalization");
    check(qr.sounds[0].gainDb > 30, "Quiet sound amplified");
    check(sp::process(quiet).bank == qr.bank, "No cumulative gain");
    auto anti = single(tone("SD.wav", .1, .4, 2, true));
    auto ar = sp::process(anti);
    check(ar.sounds[0].left, "Antiphase uses left automatically");
    auto stereo = single(tone("SD.wav", .1, .4, 2));
    check(!sp::process(stereo).sounds[0].left, "In-phase uses average");
    auto silence = single(tone("silent.wav", .1, 0, 2));
    auto sr = sp::process(silence);
    check(!sr.sounds[0].left, "Silence not cancellation");
    check(sr.sounds[0].gainDb == 0, "Silence gain finite unity");
    for (int v : sr.sounds[0].values)
      check(v == 2048, "Silence preserved");
    auto mixed = single(tone("mixed.wav", .5, .1));
    for (int i = 0; i < mixed.pool[0]->audio.getNumSamples(); ++i)
      mixed.pool[0]->audio.setSample(
          0, i,
          float(.1 *
                (std::sin(2 * MathConstants<double>::pi * 1000 * i / 44100) +
                 std::sin(2 * MathConstants<double>::pi * 18000 * i / 44100))));
    auto mr = sp::process(mixed);
    auto amplitude = [](const sp::Render &r, double frequency) {
      double re = 0, im = 0;
      int n = (int)r.values.size();
      for (int i = 300; i < n - 300; ++i) {
        double v = (r.values[i] - 2048) / 2048.;
        double phase = 2 * MathConstants<double>::pi * frequency * i / sp::rate;
        re += v * std::cos(phase);
        im += v * std::sin(phase);
      }
      return std::hypot(re, im);
    };
    check(amplitude(mr.sounds[0], 8040) / amplitude(mr.sounds[0], 1000) < .005,
          "Resampler suppresses aliased 18 kHz input");
    check(std::abs(mr.sounds[0].values.size() / double(sp::rate) - .5) < .001,
          "Neutral resampling preserves duration");
    auto compensated = sp::process(single(tone("Ride.wav", 4, .1)));
    auto &cr = compensated.sounds[0];
    check(cr.tune < 16 && cr.removed == 0,
          "Speed compensation before truncation");
    check(
        std::abs(cr.values.size() / double(sp::rate) / sp::pitchRatio(cr.tune) -
                 4) < .001,
        "Tune compensation preserves duration");
    check(amplitude(cr, 440 / sp::pitchRatio(cr.tune)) > 100,
          "Compensated pitch remains 440 Hz at hardware rate");
    check(sp::Project{}.autoTune,
          "Pitch Fit defaults to On for existing behavior");
    auto untuned = single(tone("Long.wav", 3, .1));
    untuned.autoTune = false;
    untuned.autoTrim = false;
    untuned.pads[1] = sp::makePad(*untuned.pool[0], 1, true, true);
    auto shortSource = tone("Short.wav", .1);
    untuned.pool.push_back(shortSource);
    untuned.pads[2] = sp::makePad(*shortSource, 2, true, true);
    const auto oversizedPreview = sp::renderPadPreview(untuned, 0);
    check(oversizedPreview.tune == 16 && oversizedPreview.removed == 0 &&
              oversizedPreview.values.size() == 3 * sp::rate,
          "An oversized pad can be auditioned without pitch or truncation when "
          "both modes are Off");
    check(amplitude(oversizedPreview, 440) > 100,
          "Memory-error preview contains processed audible sound");
    const auto tooLong = memoryProblems(untuned);
    check(tooLong[0] && tooLong[1] && !tooLong[2] &&
              std::count(tooLong.begin(), tooLong.end(), true) == 2,
          "All oversized pads are reported, excluding short/empty pads");
    untuned.pads[0].end = 1;
    const auto afterCut = memoryProblems(untuned);
    check(!afterCut[0] && afterCut[1],
          "Manual trim clears the corrected pad's warning");
    untuned.pads[1] = sp::Pad{};
    const auto noPitch = sp::process(untuned);
    check(noPitch.sounds[0].tune == 16 && noPitch.sounds[0].removed == 0 &&
              noPitch.sounds[0].values.size() == sp::rate,
          "Pitch Fit Off preserves manual duration and neutral tune");
    check(amplitude(noPitch.sounds[0], 440) > 100,
          "Pitch Fit Off leaves stored pitch unchanged");
    const auto *neutralBank =
        static_cast<const uint8 *>(noPitch.bank.getData());
    check((neutralBank[0x9262 + 24 * 10 + 1] >> 3) == 16,
          "Export records neutral tune when Pitch Fit is Off");
    untuned.autoTrim = true;
    untuned.fadeOutOnExport = true;
    untuned.pads[0].end = -1;
    const auto trimOnly = sp::process(untuned);
    check(trimOnly.sounds[0].tune == 16 && trimOnly.sounds[0].removed > .49 &&
              trimOnly.sounds[0].values.size() <= sp::maxFrames &&
              trimOnly.sounds[0].values.back() == 2048,
          "Auto Trim On shortens and fades long sounds without pitch changes");
    auto sharedMemory = single(tone("Zone.wav", 2.4));
    sharedMemory.autoTune = false;
    sharedMemory.autoTrim = false;
    for (int i = 0; i < 9; ++i)
      sharedMemory.pads[i] = sp::makePad(*sharedMemory.pool[0], i, true, true);
    auto previewWithEdits = sharedMemory;
    previewWithEdits.pads[8].start = .2;
    previewWithEdits.pads[8].end = .4;
    previewWithEdits.pads[8].reverse = true;
    auto isolatedPreview = previewWithEdits;
    isolatedPreview.pads = sp::Project{}.pads;
    isolatedPreview.pads[8] = previewWithEdits.pads[8];
    const auto editedPreview = sp::renderPadPreview(previewWithEdits, 8);
    check(editedPreview.values ==
                  sp::process(isolatedPreview).sounds[8].values &&
              editedPreview.start == .2 && editedPreview.end == .4,
          "Independent preview preserves pad cuts, Reverse, normalization and "
          "12-bit output");
    check(!sp::renderPadPreview(sharedMemory, 8).values.empty(),
          "Pad beyond bank memory capacity still has a preview");
    check(sp::renderPadPreview(sharedMemory, 31).values.empty(),
          "Empty pad preview stays silent");
    const auto shortage = memoryFailure(sharedMemory);
    check(shortage.excessFrames > 0 &&
              String(shortage.what()).contains("over capacity") &&
              String(shortage.what()).contains(" s over"),
          "Kit memory warning includes a positive stored-duration deficit");
    const auto overflow = shortage.pads;
    check(overflow[8] &&
              std::count(overflow.begin(), overflow.end(), true) == 1,
          "Zone overflow reports the pad that cannot be allocated, in stable "
          "order");
    sharedMemory.autoTrim = true;
    check(memoryFailure(sharedMemory).excessFrames == shortage.excessFrames,
          "Auto Trim warns instead of shortening audible audio for the bank");
    const auto fullBudgetMetadata = sp::selectionMetadata(sharedMemory);
    for (int i = 0; i < 9; ++i)
      check(fullBudgetMetadata[i].tune == 16 &&
                fullBudgetMetadata[i].removed == 0,
            "Full bank retains sub-limit samples and neutral tune");
    const int sourceLength =
        (int)std::ceil(sharedMemory.pool[0]->audio.getNumSamples() *
                       double(sp::rate) / sharedMemory.pool[0]->sampleRate);
    check(shortage.excessFrames == 9 * ((sourceLength + 13) & ~1) - 8 * 65534,
          "Capacity deficit includes guard frames and all eight zones");
    auto fragmented = single(tone("Fragment.wav", 1.3));
    fragmented.autoTrim = false;
    fragmented.autoTune = false;
    for (int i = 0; i < 9; ++i)
      fragmented.pads[i] = sp::makePad(*fragmented.pool[0], i, false, false);
    const auto fragmentedWarning = memoryFailure(fragmented);
    check(
        fragmentedWarning.excessFrames == 0 &&
            fragmentedWarning.unplacedFrames > 0 &&
            String(fragmentedWarning.what()).contains("unplaced") &&
            String(fragmentedWarning.what()).contains("free across zones"),
        "Fragmented zones report missing space even below total bank capacity");
    auto tinyExcess = single(tone("Tiny.wav", 3));
    tinyExcess.autoTrim = false;
    tinyExcess.autoTune = false;
    tinyExcess.pool[0]->sampleRate = sp::rate;
    tinyExcess.pool[0]->audio.setSize(1, sp::maxFrames + 1);
    const auto tinyWarning = memoryFailure(tinyExcess);
    check(tinyWarning.excessFrames == 1 &&
              String(tinyWarning.what()).contains("0.01 s"),
          "Single-frame sample excess rounds up to a visible nonzero duration");
    sharedMemory.autoTrim = false;
    sharedMemory.autoTune = true;
    check(memoryProblems(sharedMemory) == overflow,
          "Pitch Fit never pitches sub-limit samples to solve global memory "
          "pressure");
    for (int i = 0; i < 9; ++i)
      check(sp::renderPadPreview(sharedMemory, i).tune == 16,
            "Short samples retain neutral tune even when the bank is full");
    auto manualImport = sp::Project{};
    manualImport.autoTune = false;
    auto longSource = tone("Long Import.wav", 4);
    sp::appendSamples(manualImport, {longSource, shortSource});
    const auto limit = sp::maxSelectionDuration(longSource->sampleRate);
    check(manualImport.pads[0].start == 0 && manualImport.pads[0].end == limit,
          "Manual import initially selects start through maximum sample "
          "duration");
    check(manualImport.pads[1].start == -1 && manualImport.pads[1].end == -1,
          "Short imports retain the full sample");
    const auto initialRender = sp::process(manualImport);
    check(initialRender.sounds[0].tune == 16 &&
              initialRender.sounds[0].values.size() <= sp::maxFrames,
          "Initial long import fits without Pitch Fit or automatic truncation");
    check(sp::clampTrimPosition(*longSource, false, 4, 0) == limit,
          "Right trim handle stops at maximum selection length");
    const auto leftLimit = sp::clampTrimPosition(*longSource, true, 0, 4);
    check(4 - leftLimit <= limit + 1e-12 && leftLimit > 1.49,
          "Left trim handle cannot expand selection past maximum length");
    check(sp::clampTrimPosition(*longSource, false, 0, 1) > 1 &&
              sp::clampTrimPosition(*longSource, true, 4, 1) < 1,
          "Trim handles cannot cross or collapse to zero frames");
    check(std::abs(sp::clampTrimPosition(*shortSource, false, 4, 0) - .1) <
              1e-9,
          "Right trim handle also respects the end of a short source");
    const auto movedSelection =
        sp::shiftTrimSelection(*longSource, .5, 1.5, 1.25);
    check(movedSelection.getStart() == 1.75 && movedSelection.getEnd() == 2.75,
          "Moving a selection shifts both boundaries by the same amount");
    const auto atEnd = sp::shiftTrimSelection(*longSource, .5, 1.5, 20);
    const auto atStart = sp::shiftTrimSelection(*longSource, .5, 1.5, -20);
    check(atEnd.getStart() == 3 && atEnd.getEnd() == 4 &&
              atStart.getStart() == 0 && atStart.getEnd() == 1,
          "Moving clamps against either source edge without shrinking the "
          "selection");
    const auto wholeSource = sp::shiftTrimSelection(*longSource, 0, 4, 2);
    check(wholeSource.getStart() == 0 && wholeSource.getEnd() == 4,
          "A full-source selection cannot move or shrink");
    const auto oneFrame =
        sp::shiftTrimSelection(*longSource, 0, 1.0 / 44100, .12345);
    check(std::llround(oneFrame.getLength() * 44100) == 1,
          "Moving a one-frame selection preserves sample accuracy");
    const auto autoAssigned = sp::makePad(*longSource, 0, true, true);
    check(autoAssigned.start == -1 && autoAssigned.end == -1,
          "Auto Trim import keeps full source available for automatic "
          "processing");
    auto tuneEnabledImport = sp::Project{};
    sp::appendSamples(tuneEnabledImport, {longSource, shortSource});
    check(
        tuneEnabledImport.pads[0].start == -1 &&
            tuneEnabledImport.pads[0].end == -1,
        "Pitch Fit On imports the full four-second source with Auto Trim Off");
    const auto tunedImport = sp::process(tuneEnabledImport);
    const auto &tunedSound = tunedImport.sounds[0];
    check(tunedImport.sounds[1].tune == 16,
          "Short sound stays neutral alongside a tuned long sound");
    check(tunedSound.tune < 16 && tunedSound.removed == 0 &&
              tunedSound.values.size() <= sp::maxFrames &&
              std::abs(tunedSound.values.size() / double(sp::rate) /
                           sp::pitchRatio(tunedSound.tune) -
                       4) < .001,
          "Imported long sound fits its slot and plays for the original "
          "duration");
    check(std::ceil(4 * sp::rate * sp::pitchRatio(tunedSound.tune + 1)) >
              sp::maxFrames,
          "Pitch Fit uses the least speedup needed for the individual sample");
    const auto *tunedBank =
        static_cast<const uint8 *>(tunedImport.bank.getData());
    check((tunedBank[0x9262 + 24 * 10 + 1] >> 3) == tunedSound.tune,
          "The SP-1200 Sound Program contains the compensating tune value");
    check(amplitude(tunedSound, 440 / sp::pitchRatio(tunedSound.tune)) > 100,
          "Stored audio is pitched up to match the hardware tune compensation");
    check(sp::clampTrimPosition(*longSource, false, 4, 0, true) == 4,
          "Pitch Fit allows source selections longer than the stored slot");
    auto eightSeconds = tone("Eight.wav", 8);
    auto maximumImport = sp::Project{};
    sp::appendSamples(maximumImport, {eightSeconds, longSource});
    const auto tunedLimit =
        sp::maxSelectionDuration(eightSeconds->sampleRate, true);
    check(tunedLimit > 6.27 && tunedLimit < 6.28 &&
              maximumImport.pads[0].end == tunedLimit,
          "New sources beyond the tuning range select approximately 6.27 "
          "seconds");
    const auto maximumKit = sp::process(maximumImport);
    const auto &maximumSound = maximumKit.sounds[0];
    check(maximumKit.sounds[1].tune < 16,
          "Every overlong assigned sound is tuned independently");
    check(maximumSound.tune == 0 && maximumSound.values.size() <= sp::maxFrames,
          "Maximum source selection fits at the lowest hardware tune");
    check(
        sp::clampTrimPosition(*eightSeconds, false, 8, 0, true) == tunedLimit,
        "The tuned selection cannot exceed the maximum hardware tuning range");
    auto switchSelections = sp::Project{};
    switchSelections.pool = {eightSeconds, longSource, shortSource};
    for (int i = 0; i < 31; ++i) {
      const auto &source = switchSelections.pool[i % 3];
      switchSelections.pads[i] = sp::makePad(*source, i, false, false);
      switchSelections.pads[i].start = .02;
      switchSelections.pads[i].end = .08;
      switchSelections.pads[i].reverse = i % 2 == 0;
    }
    const auto beforeSwitch = switchSelections.pads;
    for (const bool enabled : {true, false, true}) {
      sp::setAutoTune(switchSelections, enabled);
      check(switchSelections.autoTune == enabled,
            "Pitch Fit switch updates global state");
      for (int i = 0; i < 31; ++i) {
        const auto &pad = switchSelections.pads[i];
        const auto source = sp::assetFor(switchSelections, pad.asset);
        const auto expectedEnd =
            std::min(source->audio.getNumSamples() / source->sampleRate,
                     sp::maxSelectionDuration(source->sampleRate, enabled));
        check(pad.start == 0 && pad.end == expectedEnd,
              "Both Pitch Fit directions reset every assigned pad to the new "
              "full selection");
        check(pad.asset == beforeSwitch[i].asset &&
                  pad.name == beforeSwitch[i].name &&
                  pad.channel == beforeSwitch[i].channel &&
                  pad.reverse == beforeSwitch[i].reverse,
              "Pitch Fit resets cuts without changing assignments, names, "
              "outputs or Reverse");
      }
      check(switchSelections.pads[31].asset.isEmpty() &&
                switchSelections.pads[31].end == -1,
            "Pitch Fit leaves empty pads empty");
    }
    switchSelections.autoTrim = true;
    sp::setAutoTune(switchSelections, false);
    check(switchSelections.autoTrim && switchSelections.pads[0].end == -1,
          "Pitch Fit clears automatic boundaries while Auto Trim is "
          "On");
    auto protectedKit = single(longSource);
    protectedKit.pads[0].manualTrim = true;
    protectedKit.pads[0].start = .2;
    protectedKit.pads[0].end = 3.8;
    sp::setAutoTrim(protectedKit, false);
    sp::setAutoTrim(protectedKit, true);
    protectedKit.threshold = -5;
    const auto protectedAudio = sp::process(protectedKit).sounds[0];
    check(protectedAudio.start == .2 && protectedAudio.end == 3.8 &&
              protectedAudio.removed == 0 && protectedAudio.tune < 16,
          "Auto Trim and threshold preserve manual cuts and fit their full "
          "audio");
    sp::setAutoTune(protectedKit, false);
    check(protectedKit.pads[0].start == .2 && protectedKit.pads[0].end == 3.8 &&
              protectedKit.pads[0].manualTrim &&
              memoryProblems(protectedKit)[0],
          "Pitch Fit Off preserves an oversized manual selection and blocks "
          "export");
    const auto protectedPreview = sp::renderPadPreview(protectedKit, 0);
    const auto invalidMetadata = sp::selectionMetadata(protectedKit)[0];
    check(protectedPreview.start == .2 && protectedPreview.end == 3.8 &&
              protectedPreview.removed == 0 && invalidMetadata.start == .2 &&
              invalidMetadata.end == 3.8,
          "Oversized protected audio remains fully playable and visible");
    sp::resetTrim(protectedKit, 0);
    check(
        !protectedKit.pads[0].manualTrim && protectedKit.pads[0].start == -1 &&
            sp::process(protectedKit).sounds[0].values.size() <= sp::maxFrames,
        "Reset Trim releases protection and restores automatic processing");
    protectedKit.pads[0].manualTrim = true;
    protectedKit.pads[0].start = .2;
    protectedKit.pads[0].end = .8;
    sp::setAutoTrim(protectedKit, false);
    sp::resetTrim(protectedKit, 0);
    check(
        !protectedKit.pads[0].manualTrim && protectedKit.pads[0].start == 0 &&
            protectedKit.pads[0].end == limit,
        "Reset Trim with Auto Trim Off restores the default source-grid limit");
    auto shortImport = sp::Project{};
    sp::appendSamples(shortImport, {shortSource});
    const auto shortOn = sp::process(shortImport);
    shortImport.autoTune = false;
    check(
        shortOn.bank == sp::process(shortImport).bank,
        "Pitch Fit has no audio or metadata effect below the per-sample limit");
    manualImport.pads[0].start = .5;
    manualImport.pads[0].end = 1;
    sp::appendSamples(manualImport, {longSource});
    check(manualImport.pads[0].start == .5 && manualImport.pads[0].end == 1 &&
              manualImport.pads[2].end == limit,
          "Append caps new assignments while preserving existing cuts");
    for (const double sampleRate : {22050., 26040., 44100., 48000., 96000.}) {
      const auto seconds = sp::maxSelectionDuration(sampleRate);
      check(std::ceil(std::llround(seconds * sampleRate) * double(sp::rate) /
                      sampleRate) <= sp::maxFrames,
            "Source-grid maximum never overflows export frame capacity");
      const auto tunedSeconds = sp::maxSelectionDuration(sampleRate, true);
      check(std::ceil(std::llround(tunedSeconds * sampleRate) *
                      double(sp::rate) / sampleRate * sp::pitchRatio(0)) <=
                sp::maxFrames,
            "Tuned source-grid maximum fits at every supported fixture sample "
            "rate");
    }
    auto longSound = single(tone("Ride.wav", 7, .4));
    longSound.fadeOutOnExport = true;
    auto longResult = sp::process(longSound);
    check(longResult.sounds[0].tune == 0, "Long sound reaches lowest tune");
    check(longResult.sounds[0].removed > .6,
          "Tail shortened after pitch exhausted");
    check(longResult.sounds[0].values.back() == 2048, "Fade ends at zero");
    check(longResult.sounds[0].values.size() <= sp::maxFrames,
          "Per-sample capacity");
    auto budget = single(tone("Long.wav", 2.4, .1));
    for (int i = 0; i < 32; ++i)
      budget.pads[i] = sp::makePad(*budget.pool[0], i, true, true);
    const auto budgetWarning = memoryFailure(budget);
    check(budgetWarning.excessFrames > 0,
          "32 long sounds warn when they cannot fit eight zones");
    const auto fullBudget = sp::selectionMetadata(budget);
    for (const auto &sound : fullBudget)
      check(sound.tune == 16 && sound.removed == 0,
            "Auto Trim preserves audible sub-limit tails under bank pressure");
    auto manualBudget = budget;
    for (auto &pad : manualBudget.pads) {
      pad.manualTrim = true;
      pad.start = 0;
      pad.end = 2.4;
    }
    const auto protectedBudgetFailure = memoryFailure(manualBudget);
    check(protectedBudgetFailure.excessFrames > 0 &&
              sp::renderPadPreview(manualBudget, 0).removed == 0,
          "An entirely manual kit is never shortened to fit bank memory");
    auto mixedBudget = manualBudget;
    for (int i = 1; i < 32; ++i)
      mixedBudget.pads[i].manualTrim = false;
    check(memoryFailure(mixedBudget).excessFrames > 0,
          "Mixed automatic/manual overfull bank also reports memory pressure");
    const auto mixedMetadata = sp::selectionMetadata(mixedBudget);
    check(
        mixedMetadata[0].start == 0 && mixedMetadata[0].end == 2.4 &&
            mixedMetadata[0].removed == 0 && mixedMetadata[1].removed == 0,
        "Bank pressure preserves both manual and automatic audible selections");
    auto globalTrim = single(tone("BD.wav"));
    for (int i = 0; i < globalTrim.pool[0]->audio.getNumSamples() / 2; ++i)
      globalTrim.pool[0]->audio.setSample(0, i, .0001f);
    for (int i = 0; i < 32; ++i)
      globalTrim.pads[i] = sp::makePad(*globalTrim.pool[0], i, true, true);
    globalTrim.threshold = -90;
    const auto lowThreshold = sp::process(globalTrim);
    globalTrim.threshold = -40;
    const auto highThreshold = sp::process(globalTrim);
    for (int i = 0; i < 32; ++i)
      check(highThreshold.sounds[i].start > lowThreshold.sounds[i].start + .04,
            "Global threshold trims every pad");
    globalTrim.pads[0].start = .01;
    globalTrim.pads[0].end = .08;
    const auto automaticBounds = sp::process(globalTrim);
    check(automaticBounds.sounds[0].start > .04,
          "Auto trim ignores stored manual boundaries");
    globalTrim.autoTrim = false;
    const auto manualBounds = sp::process(globalTrim);
    check(std::abs(manualBounds.sounds[0].start - .01) < 1. / 44100 &&
              std::abs(manualBounds.sounds[0].end - .08) < 1. / 44100,
          "Off restores manual boundaries");
    check(manualBounds.sounds[1].start == 0 &&
              std::abs(manualBounds.sounds[1].end - .1) < 1. / 44100,
          "Off preserves full source when manual boundaries are unset");
    globalTrim.threshold = -10;
    check(sp::process(globalTrim).bank == manualBounds.bank,
          "Threshold has no effect in manual mode");
    globalTrim.threshold = -40;
    budget.autoTrim = false;
    rejects([&] { sp::process(budget); },
            "Manual mode refuses automatic memory trimming");
    sp::sortPads(globalTrim);
    check(globalTrim.threshold == -40, "Sorting preserves global threshold");
    auto reversible = single(tone("Reverse.wav"));
    const auto originalBytes = reversible.pool[0]->original;
    for (int i = 0; i < reversible.pool[0]->audio.getNumSamples() / 3; ++i)
      reversible.pool[0]->audio.setSample(0, i, .02f);
    const auto forward = sp::process(reversible);
    reversible.pads[0].reverse = true;
    const auto backward = sp::process(reversible);
    auto expectedReverse = reversible;
    expectedReverse.pool[0] = std::make_shared<sp::Asset>(*reversible.pool[0]);
    expectedReverse.pool[0]->audio.reverse(
        0, expectedReverse.pool[0]->audio.getNumSamples());
    expectedReverse.pads[0].reverse = false;
    check(backward.sounds[0].values ==
              sp::process(expectedReverse).sounds[0].values,
          "Reverse processes audio in reverse order");
    check(backward.sounds[0].values != forward.sounds[0].values,
          "Reverse changes exported sound");
    reversible.pads[0].reverse = false;
    check(sp::process(reversible).bank == forward.bank,
          "Disabling reverse restores original processing");
    check(reversible.pool[0]->original == originalBytes,
          "Reverse leaves embedded original untouched");
    auto dir = File::getSpecialLocation(File::tempDirectory)
                   .getNonexistentChildFile("sp1200-tests", "", false);
    dir.createDirectory();
    const auto settingsFile = dir.getChildFile("app.settings");
    {
      sp::AppPreferences preferences(settingsFile);
      auto startup = anti;
      preferences.restore(startup);
      check(startup.autoTune && !startup.autoSort && !startup.autoTrim &&
                startup.threshold == -40 && !startup.fadeOutOnExport &&
                startup.normalizeOnExport,
            "First launch restores Off/Off and -40 dBFS");
      check(startup.pool == anti.pool &&
                startup.pads[0].asset == anti.pads[0].asset,
            "App preferences only affect global controls");
      startup.autoTune = false;
      startup.autoSort = true;
      startup.autoTrim = true;
      startup.threshold = -23;
      startup.fadeOutOnExport = true;
      startup.normalizeOnExport = false;
      check(preferences.save(startup), "Save app preferences without a preset");
      sp::AppPreferences reopened(settingsFile);
      sp::Project nextLaunch;
      reopened.restore(nextLaunch);
      check(!nextLaunch.autoTune && nextLaunch.autoSort &&
                nextLaunch.autoTrim && nextLaunch.threshold == -23 &&
                nextLaunch.fadeOutOnExport && !nextLaunch.normalizeOnExport,
            "Settings are on disk immediately, before the previous instance "
            "closes");
    }
    {
      sp::AppPreferences preferences(settingsFile);
      sp::Project nextLaunch;
      preferences.restore(nextLaunch);
      check(!nextLaunch.autoTune && nextLaunch.autoSort &&
                nextLaunch.autoTrim && nextLaunch.threshold == -23 &&
                nextLaunch.fadeOutOnExport && !nextLaunch.normalizeOnExport,
            "All global controls survive a preferences restart");
      nextLaunch.autoTune = true;
      nextLaunch.autoSort = false;
      nextLaunch.autoTrim = false;
      nextLaunch.threshold = -60;
      check(preferences.save(nextLaunch),
            "Save disabled modes and retained threshold");
    }
    {
      sp::AppPreferences preferences(settingsFile);
      sp::Project nextLaunch;
      preferences.restore(nextLaunch);
      check(nextLaunch.autoTune && !nextLaunch.autoSort &&
                !nextLaunch.autoTrim && nextLaunch.threshold == -60,
            "Off modes and threshold also survive restart");
    }
    {
      PropertiesFile invalid(settingsFile, sp::AppPreferences::options());
      invalid.setValue("threshold", -120);
      check(invalid.saveIfNeeded(), "Write out-of-range preference fixture");
    }
    {
      sp::AppPreferences preferences(settingsFile);
      sp::Project nextLaunch;
      preferences.restore(nextLaunch);
      check(nextLaunch.threshold == -60,
            "Stored threshold is clamped to supported range");
    }
    auto preset = dir.getChildFile("portable.spkit");
    anti.autoTune = false;
    anti.bankName = "First Kit";
    anti.threshold = -60;
    anti.autoTrim = false;
    anti.autoSort = false;
    anti.pads[0].reverse = true;
    anti.pads[0].manualTrim = true;
    anti.pads[0].start = .01;
    anti.pads[0].end = .08;
    ar = sp::process(anti);
    sp::savePreset(anti, preset, &ar);
    auto restored = sp::loadPreset(preset);
    check(restored.bankName == "First Kit",
          "Bank name survives preset roundtrip");
    check(!restored.autoTune, "Pitch Fit mode survives preset roundtrip");
    check(!restored.autoSort, "Auto Map mode survives preset roundtrip");
    check(!restored.autoTrim, "Manual trim mode survives preset roundtrip");
    check(restored.pads[0].reverse && restored.pads[0].manualTrim,
          "Reverse and manual protection survive version 7 preset roundtrip");
    check(restored.threshold == -60,
          "Global trim threshold survives preset roundtrip");
    check(restored.pool[0]->original == anti.pool[0]->original,
          "Original embedded exactly");
    check(sp::process(restored).bank == ar.bank,
          "Preset audio/metadata roundtrip");
    // Memory-invalid kits must stay portable and retain every editable field.
    const auto overfullFile = dir.getChildFile("overfull.spkit");
    auto overfull = sharedMemory;
    overfull.autoTrim = false;
    overfull.autoTune = false;
    overfull.bankName = "OVERFULL";
    overfull.threshold = -27;
    overfull.pads[0].name = "CUSTOM";
    overfull.pads[0].channel = 5;
    overfull.pads[0].reverse = true;
    for (auto &pad : overfull.pads)
      if (pad.asset.isNotEmpty()) {
        pad.manualTrim = true;
        pad.start = 0;
        pad.end = 2.4;
      }
    overfull.pads[0].start = .01;
    overfull.pads[0].end = 2.4;
    overfull.pool.push_back(
        quiet.pool[0]); // Include an unassigned pool sample.
    const auto beforeFailure = memoryFailure(overfull);
    sp::savePreset(overfull, overfullFile);
    const auto reopenedOverfull = sp::loadPreset(overfullFile);
    check(reopenedOverfull.pool.size() == overfull.pool.size() &&
              reopenedOverfull.pool[1]->original ==
                  overfull.pool[1]->original &&
              reopenedOverfull.bankName == overfull.bankName &&
              reopenedOverfull.threshold == -27 && !reopenedOverfull.autoTrim &&
              !reopenedOverfull.autoTune,
          "Memory-invalid preset retains pool, original audio and global "
          "settings");
    for (int i = 0; i < 32; ++i) {
      const auto &expectedPad = overfull.pads[i];
      const auto &restoredPad = reopenedOverfull.pads[i];
      check(restoredPad.asset == expectedPad.asset &&
                restoredPad.name == expectedPad.name &&
                restoredPad.channel == expectedPad.channel &&
                restoredPad.reverse == expectedPad.reverse &&
                restoredPad.manualTrim == expectedPad.manualTrim &&
                restoredPad.start == expectedPad.start &&
                restoredPad.end == expectedPad.end,
            "Memory-invalid preset retains pad assignments, edits and cuts");
    }
    check(memoryFailure(reopenedOverfull).pads == beforeFailure.pads,
          "Reopening an overfull preset does not silently change it to fit");
    {
      ZipFile zip(overfullFile);
      std::unique_ptr<InputStream> in(
          zip.createStreamForEntry(zip.getIndexOfFileName("project.json")));
      const auto manifest = JSON::parse(in->readEntireStreamAsString());
      check(bool(manifest["processingPending"]) &&
                !manifest["pads"][0].hasProperty("gainDb"),
            "Presets without bank renders never invent derived processing "
            "metadata");
    }
    auto oversizeFile = dir.getChildFile("oversize.spkit");
    auto oversizedKit = single(tone("Oversized.wav", 3));
    oversizedKit.autoTrim = false;
    oversizedKit.autoTune = false;
    const auto oversizedFailure = memoryFailure(oversizedKit);
    sp::savePreset(oversizedKit, oversizeFile);
    check(memoryFailure(sp::loadPreset(oversizeFile)).excessFrames ==
              oversizedFailure.excessFrames,
          "Individually oversized selections can be saved and restored "
          "unchanged");
    // Unreleased preset formats are deliberately unsupported; no migration.
    const auto legacyPreset = dir.getChildFile("legacy.spkit");
    sp::Project emptyLegacy;
    emptyLegacy.autoTune = false;
    sp::savePreset(emptyLegacy, legacyPreset);
    var manifest;
    {
      ZipFile zip(legacyPreset);
      std::unique_ptr<InputStream> in(
          zip.createStreamForEntry(zip.getIndexOfFileName("project.json")));
      manifest = JSON::parse(in->readEntireStreamAsString());
    }
    check(sp::loadPreset(legacyPreset).pool.empty(),
          "Empty version 7 presets roundtrip");
    manifest.getDynamicObject()->removeProperty("fadeOutOnExport");
    manifest.getDynamicObject()->removeProperty("normalizeOnExport");
    for (int version : {7, 1, 2, 3, 4, 5, 6, 8}) {
      manifest.getDynamicObject()->setProperty("version", version);
      const auto legacyJson = JSON::toString(manifest);
      ZipFile::Builder legacyZip;
      legacyZip.addEntry(new MemoryInputStream(legacyJson.toRawUTF8(),
                                               legacyJson.getNumBytesAsUTF8(),
                                               true),
                         6, "project.json", Time::getCurrentTime());
      MemoryOutputStream legacyBytes;
      check(legacyZip.writeToStream(legacyBytes, nullptr),
            "Create preset version fixture");
      sp::atomicWrite(legacyPreset, legacyBytes.getMemoryBlock());
      if (version == 7) {
        const auto compatible = sp::loadPreset(legacyPreset);
        check(!compatible.fadeOutOnExport && compatible.normalizeOnExport,
              "Version 7 kits without export flags use Fade Off/Normalize On");
      } else
        rejects([&] { sp::loadPreset(legacyPreset); },
                "Earlier and unknown preset versions are rejected without "
                "migration");
    }
    StringArray sequentialPaths;
    for (const auto *filename : {"SD.wav", "BD.wav", "OH.wav"}) {
      auto file = dir.getChildFile(filename);
      sp::atomicWrite(file, quiet.pool[0]->original);
      sequentialPaths.add(file.getFullPathName());
    }
    for (int i = 0; i < 31; ++i) {
      auto file = dir.getChildFile("Other" + String(i) + ".wav");
      sp::atomicWrite(file, quiet.pool[0]->original);
      sequentialPaths.add(file.getFullPathName());
    }
    StringArray orderWarnings;
    auto sequential = sp::importFiles(sequentialPaths, orderWarnings, false);
    check(!sequential.autoSort && sequential.pool.size() == 34,
          "Sequential import retains overflow and mode");
    for (int i = 0; i < 32; ++i)
      check(sp::assetFor(sequential, sequential.pads[i].asset)->filename ==
                File(sequentialPaths[i]).getFileName(),
            "Auto Map Off fills pads in supplied import order");
    sequential.autoSort = true;
    sp::sortPads(sequential);
    check(sp::assetFor(sequential, sequential.pads[0].asset)->filename ==
                  "BD.wav" &&
              sp::assetFor(sequential, sequential.pads[1].asset)->filename ==
                  "SD.wav" &&
              sp::assetFor(sequential, sequential.pads[5].asset)->filename ==
                  "OH.wav",
          "Enabling Auto Map classifies an already imported kit");
    const auto sortedImport =
        sp::importFiles(sequentialPaths, orderWarnings, true);
    for (int i = 0; i < 32; ++i) {
      auto before = sp::assetFor(sortedImport, sortedImport.pads[i].asset);
      auto after = sp::assetFor(sequential, sequential.pads[i].asset);
      check((!before && !after) ||
                (before && after && before->relative == after->relative),
            "Sorting after import matches Auto Map On import");
    }
    auto accumulated = single(tone("Existing.wav"));
    accumulated.autoSort = true;
    accumulated.pads[0].name = "CUSTOM";
    accumulated.pads[0].channel = 6;
    accumulated.pads[0].start = .01;
    accumulated.pads[0].end = .08;
    accumulated.pads[0].reverse = true;
    const auto oldPad = accumulated.pads[0];
    const auto oldAsset = accumulated.pool[0];
    sp::appendSamples(accumulated, sequential.pool);
    check(accumulated.pool.size() == 35 && accumulated.pool[0] == oldAsset,
          "Appending retains existing pool and adds new samples");
    check(accumulated.pads[0].asset == oldPad.asset &&
              accumulated.pads[0].name == oldPad.name &&
              accumulated.pads[0].channel == oldPad.channel &&
              accumulated.pads[0].start == oldPad.start &&
              accumulated.pads[0].end == oldPad.end &&
              accumulated.pads[0].reverse == oldPad.reverse,
          "Appending preserves occupied pad and edits");
    check(sp::assetFor(accumulated, accumulated.pads[1].asset)->filename ==
                  "SD.wav" &&
              sp::assetFor(accumulated, accumulated.pads[5].asset)->filename ==
                  "OH.wav",
          "Appending with Auto Map prefers free instrument pads");
    for (int i = 0; i < 32; ++i)
      check(accumulated.pads[i].asset.isNotEmpty() == (i < 17 || i >= 24),
            "Append reserves C slots for matching second A-instrument hits");
    const auto fullPads = accumulated.pads;
    sp::appendSamples(accumulated, {tone("Extra.wav")});
    check(accumulated.pool.size() == 36, "Full kit still accepts pool samples");
    for (int i = 0; i < 32; ++i)
      check(accumulated.pads[i].asset == fullPads[i].asset,
            "Full kit assignments remain untouched");
    auto unsortedAppend = single(tone("Keep.wav"));
    unsortedAppend.autoSort = false;
    unsortedAppend.pads[1] = unsortedAppend.pads[0];
    unsortedAppend.pads[0] = sp::Pad{};
    const auto keptId = unsortedAppend.pads[1].asset;
    sp::appendSamples(unsortedAppend, sequential.pool);
    check(unsortedAppend.pads[0].asset == sequential.pool[0]->id &&
              unsortedAppend.pads[1].asset == keptId &&
              unsortedAppend.pads[2].asset == sequential.pool[1]->id,
          "Auto Map Off appends in import order while skipping occupied pads");
    auto nested = dir.getChildFile("input/sub");
    nested.createDirectory();
    sp::atomicWrite(nested.getChildFile("BD.wav"), quiet.pool[0]->original);
    dir.getChildFile("input/bad.wav").replaceWithText("bad");
    StringArray warnings;
    auto imported = sp::importFiles(
        {dir.getChildFile("input").getFullPathName()}, warnings);
    check(imported.bankName == "input",
          "Bank name uses imported root folder, not nested folder");
    check(imported.pool.size() == 1 && warnings.size() == 1,
          "Recursive import skips bad file");
    auto ir = sp::process(imported);
    sp::savePreset(imported, preset, &ir);
    dir.getChildFile("input").deleteRecursively();
    check(sp::process(sp::loadPreset(preset)).bank == ir.bank,
          "Preset independent from original folder");
    for (int i = 0; i < 40; ++i)
      mapping.pool.push_back(tone("Unknown " + String(i) + ".wav", .01));
    sp::sortPads(mapping);
    for (int i = 0; i < 32; ++i)
      check(mapping.pads[i].channel == i % 8 + 1,
            "Default routing follows pad position in every bank");
    int count = 0;
    for (auto &p : mapping.pads)
      count += p.asset.isNotEmpty();
    check(count <= 32 && mapping.pool.size() > 32, "Overflow retained in pool");
    sp::atomicWrite(dir.getChildFile("native.sp12"), ir.bank);
    // Real kit integration fixtures are optional output, not test side effects.
    for (auto folder : root.getChildFile("kits/02. Breakbeat")
                           .findChildFiles(File::findDirectories, false)) {
      StringArray skipped;
      auto kit = sp::importFiles({folder.getFullPathName()}, skipped, true);
      kit.autoTrim = true;
      auto rendered = sp::process(kit);
      sp::validateBank(rendered.bank);
      check(kit.pool.size() == 16,
            "Both real demo kits decode 16 source samples");
      if (argc == 2) {
        File destination =
            File::getCurrentWorkingDirectory().getChildFile(argv[1]);
        destination.createDirectory();
        auto stem = folder.getFileName().startsWith("08") ? "CPPBIG" : "CPPHND";
        sp::atomicWrite(destination.getChildFile(String(stem) + ".sp12"),
                        rendered.bank);
        sp::savePreset(kit, destination.getChildFile(String(stem) + ".spkit"),
                       &rendered);
      }
    }
    dir.deleteRecursively();
    std::cout << "PASS: " << checks
              << " checks (codec parity, fixtures, classification, "
                 "normalization, stereo, limits, presets).\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL after " << checks << " checks: " << e.what() << "\n";
    return 1;
  }
}
