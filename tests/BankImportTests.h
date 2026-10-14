// SPDX-License-Identifier: AGPL-3.0-only
// Copyright © 2026 Numerisch GmbH
#pragma once
#include "NativeBank.h"

namespace bankTests {
static int slot(int i) { return (3 - i / 8) * 8 + i % 8; }
static int word(const MemoryBlock &b, int at) {
  const auto *p = static_cast<const uint8 *>(b.getData());
  return p[at] | p[at + 1] << 8;
}
static void word(MemoryBlock &b, int at, int n) {
  auto *p = static_cast<uint8 *>(b.getData());
  p[at] = uint8(n);
  p[at + 1] = uint8(n >> 8);
}
static void counters(MemoryBlock &b) {
  const int offsets[] = {0x9258, 0x925a, 0x9254, 0x9256,
                         0x9110, 0x9112, 0x925c, 0x925e};
  std::array<int, 8> ends{};
  const auto *p = static_cast<const uint8 *>(b.getData());
  for (int s = 0; s < 32; ++s) {
    const int n = word(b, 0x91d6 + s * 4);
    if (n) {
      const int z = p[0x9263 + s * 10] & 7;
      ends[z] = std::max(ends[z], word(b, 0x91d4 + s * 4) + n);
    }
  }
  for (int z = 0; z < 8; ++z)
    word(b, offsets[z], ends[z]);
}
static MemoryBlock fixture() {
  MemoryBlock b;
  check(File(PROJECT_ROOT).getChildFile("tests/fixtures/codec32.sp12").loadFileAsData(b),
        "Load public synthetic bank fixture");
  auto *p = static_cast<uint8 *>(b.getData());
  // Opaque non-sound bytes stand in for sequence/device data.
  p[0x100] = 0x57;
  p[0x321] = 0xa9;
  const int a = slot(0), alias = slot(1);
  std::copy_n(p + 0x91d4 + a * 4, 4, p + 0x91d4 + alias * 4);
  p[0x9263 + alias * 10] = uint8((p[0x9263 + alias * 10] & 248) |
                                (p[0x9263 + a * 10] & 7));
  word(b, 0x9264 + alias * 10, word(b, 0x9264 + a * 10));
  // A genuine saved prefix has low nibbles in the low byte, not PCM bits.
  const int prefix[] = {0x123, 0xabc, 0x800, 0, 0xfff};
  for (int j = 0; j < 5; ++j) {
    p[0x93a2 + a * 10 + j * 2] = uint8(prefix[j] & 15);
    p[0x93a3 + a * 10 + j * 2] = uint8(prefix[j] >> 4);
  }
  const int odd = slot(3), d = 0x9262 + odd * 10;
  word(b, d + 2, word(b, d + 2) + 1);
  word(b, d + 4, ((word(b, d + 4) ^ 65535) - 1) ^ 65535);
  p[0x9262 + slot(5) * 10] |= 128; // Decay instead of tuning.
  sp::writeLoopDescriptor(b, 2, 333);
  counters(b);
  sp::validateBank(b);
  return b;
}
static void opaqueUnchanged(const MemoryBlock &a, const MemoryBlock &b) {
  check(a.getSize() == b.getSize() &&
            std::memcmp(a.getData(), b.getData(), 0x9110) == 0 &&
            static_cast<const uint8 *>(a.getData())[0x9261] ==
                static_cast<const uint8 *>(b.getData())[0x9261],
        "Sequence/global/unknown fields survive sound edits");
}
static void testRepacking(const MemoryBlock &empty) {
  auto fragmented = empty;
  auto *bytes = static_cast<uint8 *>(fragmented.getData());
  // Each zone has two free holes smaller than the new sound, although the
  // complete bank has ample capacity. This forces relocation of raw regions.
  for (int i = 0; i < 8; ++i) {
    const int s = slot(i), d = 0x9262 + s * 10;
    word(fragmented, 0x91d4 + s * 4, 16384);
    word(fragmented, 0x91d6 + s * 4, 32760);
    bytes[d] = uint8(i);
    bytes[d + 1] = uint8(16 << 3 | i);
    word(fragmented, d + 2, 16384);
    word(fragmented, d + 4, 32750 ^ 65535);
    const auto name = sp::padName(i).paddedRight(' ', 6);
    for (int j = 0; j < 6; ++j)
      bytes[0x9114 + s * 6 + j] = uint8(name[j]);
    for (int j = 0; j < 32760; j += 2) {
      const int at = 0x94e2 + i * 98304 + (16384 + j) * 3 / 2;
      const int a = j % 4096, b = (j + 1) % 4096;
      bytes[at] = uint8(a >> 4);
      bytes[at + 1] = uint8(b >> 4);
      bytes[at + 2] = uint8((a & 15) | ((b & 15) << 4));
    }
  }
  counters(fragmented);
  auto p = sp::readBank(fragmented, "Fragmented");
  const auto initial = sp::process(p);
  const auto newSound = tone("NEW.wav", 22000. / sp::rate);
  p.pool.push_back(newSound);
  sp::assignPad(p, 8, *newSound);
  const auto repacked = sp::process(p);
  opaqueUnchanged(fragmented, repacked.bank);
  const auto loaded = sp::readBank(repacked.bank, "Repacked");
  for (int i = 0; i < 8; ++i)
    check(sp::renderPadPreview(loaded, i).values == initial.sounds[i].values,
          "Repacking preserves raw region audio, saved prefix and playback offsets");
  check(loaded.pool.size() == 9 && repacked.used != initial.used,
        "Mixed native/WAV bank fits by repacking fragmented memory");
}
static void audit(const File &file) {
  const auto p = sp::loadBank(file);
  MemoryBlock original;
  file.loadFileAsData(original);
  check(sp::process(p).bank == original, "Real bank opens and exports byte-identically");
  const auto kit = File::getSpecialLocation(File::tempDirectory)
                       .getNonexistentChildFile("bank-audit", ".spkit", false);
  sp::savePreset(p, kit);
  auto restored = sp::loadPreset(kit);
  check(sp::process(restored).bank == original, "Real bank survives portable kit roundtrip");
  kit.deleteFile();
  std::cout << "PASS: native bank and kit roundtrip " << file.getFileName() << '\n';
}
static void run(const File &hardwareOutput = {}) {
  const auto original = fixture();
  auto p = sp::readBank(original, "Native Bank");
  check(p.pool.size() == 32 && p.originalBank && !p.autoTune && !p.autoTrim &&
            !p.autoSort && !p.normalizeOnExport && !p.fadeOutOnExport,
        "Bank opens all pads with preservation defaults");
  const auto baseline = p;
  const auto asset0 = sp::assetFor(p, p.pads[0].asset);
  const auto asset1 = sp::assetFor(p, p.pads[1].asset);
  check(asset0->native->region == asset1->native->region,
        "Copied sounds share physical sample storage despite different tuning");
  auto identical = original;
  auto *identicalBytes = static_cast<uint8 *>(identical.getData());
  std::copy_n(identicalBytes + 0x93a2 + slot(0) * 10, 10,
              identicalBytes + 0x93a2 + slot(1) * 10);
  auto same = sp::readBank(identical, "Same");
  same.normalizeOnExport = true;
  const auto sameResult = sp::process(same);
  const auto sameLoaded = sp::readBank(sameResult.bank, "Same Edited");
  check(sp::assetFor(sameLoaded, sameLoaded.pads[0].asset)->native->region ==
            sp::assetFor(sameLoaded, sameLoaded.pads[1].asset)->native->region,
        "Identical normalized aliases with different tuning still share storage");
  const auto rendered = sp::process(p);
  check(rendered.bank == original, "Unchanged bank exports byte-identically");
  check(rendered.sounds[0].values[0] == 0x123 &&
            rendered.sounds[0].values[1] == 0xabc &&
            rendered.sounds[0].values[4] == 4095,
        "Saved prefix restores exact 12-bit attack values");
  for (int i = 0; i < 32; ++i) {
    const auto a = sp::assetFor(p, p.pads[i].asset);
    const auto &r = rendered.sounds[i];
    check(r.tune == (i == 5 ? 16 : i), "All native tuning values and decay mode survive preview");
    check(int(r.values.size()) == a->native->span, "Native playback counter span has no invented guard adjustment");
    check(r.loopFrames == (i == 2 ? 333 : 0), "Native loop does not snap to another crossing");
  }
  const auto dir = File::getSpecialLocation(File::tempDirectory)
                       .getNonexistentChildFile("bank-import-tests", "", false);
  dir.createDirectory();
  const auto kit = dir.getChildFile("Native Kit.spkit");
  sp::savePreset(p, kit);
  check(sp::process(sp::loadPreset(kit)).bank == original,
        "Format 8 retains original bank, prefix, shared storage and native rate");
  rewriteKitManifest(kit, [](var &root) {
    root["sourceBank"].getDynamicObject()->setProperty("sha256", "wrong");
  });
  rejects([&] { sp::loadPreset(kit); }, "Corrupt original bank checksum is rejected");
  sp::savePreset(p, kit);
  rewriteKitManifest(kit, [](var &root) {
    root["assets"].getArray()->getReference(0).getDynamicObject()->setProperty("bankPad", 32);
  });
  rejects([&] { sp::loadPreset(kit); }, "Invalid native source pad is rejected");
  dir.deleteRecursively();

  p.pads[0].name = "RENAMD";
  auto edited = sp::process(p).bank;
  const auto *before = static_cast<const uint8 *>(original.getData());
  const auto *after = static_cast<const uint8 *>(edited.getData());
  for (size_t i = 0; i < original.getSize(); ++i)
    check(before[i] == after[i] || (i >= 0x9114 + slot(0) * 6 && i < 0x9114 + slot(0) * 6 + 6),
          "Renaming changes only the six name bytes");
  p = baseline;
  p.pads[0].channel = 8;
  edited = sp::process(p).bank;
  after = static_cast<const uint8 *>(edited.getData());
  for (size_t i = 0; i < original.getSize(); ++i)
    check(before[i] == after[i] || i == 0x9262 + slot(0) * 10,
          "Output edit changes only the channel flag byte");

  p = baseline;
  p.pads[0].loopEnabled = true;
  p.pads[0].loopSeconds = 101. / asset0->sampleRate;
  edited = sp::process(p).bank;
  const auto looped = sp::readBank(edited, "Loop");
  check(sp::assetFor(looped, looped.pads[0].asset)->native->loopFrames == 101,
        "Editing loops retains exact selected frame count");
  check(std::memcmp(before + 0x94e2, static_cast<const uint8 *>(edited.getData()) + 0x94e2,
                    original.getSize() - 0x94e2) == 0,
        "Loop edit keeps shared sample audio untouched");
  p.pads[0].loopSeconds = 2. / asset0->sampleRate;
  rejects([&] { sp::process(p); }, "Two-frame native loop is rejected");

  for (int edit = 0; edit < 4; ++edit) {
    p = baseline;
    if (edit == 0)
      p.pads[0].reverse = true;
    else if (edit == 1) {
      p.pads[0].start += 6. / asset0->sampleRate;
      p.pads[0].end -= 9. / asset0->sampleRate;
    } else if (edit == 2)
      p.normalizeOnExport = true;
    else
      p.fadeOutOnExport = true;
    const auto result = sp::process(p);
    opaqueUnchanged(original, result.bank);
    const auto loaded = sp::readBank(result.bank, "Edited");
    const auto a0 = sp::assetFor(loaded, loaded.pads[0].asset);
    const auto a1 = sp::assetFor(loaded, loaded.pads[1].asset);
    check(a0->native->region != a1->native->region,
          "Different processed aliases get independent storage");
    const auto preview = sp::renderPadPreview(loaded, 0);
    check(preview.values == result.sounds[0].values,
          "Edited native audio, attack and playback span roundtrip exactly");
    if (edit < 2)
      check(sp::renderPadPreview(loaded, 1).values == rendered.sounds[1].values,
            "Editing one alias leaves the other sound untouched");
    dir.createDirectory();
    sp::savePreset(p, kit, &result);
    check(sp::process(sp::loadPreset(kit)).bank == result.bank,
          "Edited native sound settings survive format-8 kit roundtrip");
    dir.deleteRecursively();
  }
  p = baseline;
  std::swap(p.pads[0], p.pads[15]);
  edited = sp::process(p).bank;
  opaqueUnchanged(original, edited);
  auto moved = sp::readBank(edited, "Moved");
  check(sp::renderPadPreview(moved, 15).values == rendered.sounds[0].values &&
            sp::renderPadPreview(moved, 0).tune == 15,
        "Pad swaps retain sample, prefix, tuning and device flags");
  p = baseline;
  sp::clearPad(p, 0);
  edited = sp::process(p).bank;
  opaqueUnchanged(original, edited);
  auto cleared = sp::readBank(edited, "Cleared");
  check(cleared.pads[0].asset.isEmpty() &&
            sp::renderPadPreview(cleared, 1).values == rendered.sounds[1].values,
        "Clearing one shared pad keeps the other copy");
  sp::clearKit(p);
  edited = sp::process(p).bank;
  opaqueUnchanged(original, edited);
  check(sp::readBank(edited, "Empty").pool.empty(),
        "Clear All clears sounds and retains sequence/device data");
  p = baseline; // Undo restores the whole immutable bank-backed project.
  check(sp::process(p).bank == original, "Undo restores byte-identical native bank");

  auto corrupt = original;
  corrupt.setSize(original.getSize() - 1);
  rejects([&] { sp::readBank(corrupt, "Bad"); }, "Truncated bank is rejected");
  corrupt = original;
  word(corrupt, 0x9264 + slot(0) * 10, 65535);
  rejects([&] { sp::readBank(corrupt, "Bad"); }, "Playback outside allocation is rejected");
  corrupt = original;
  word(corrupt, 0x9258, word(corrupt, 0x9258) ^ 1);
  rejects([&] { sp::readBank(corrupt, "Bad"); }, "Corrupt native zone counter is rejected");
  MemoryBlock empty;
  File(PROJECT_ROOT).getChildFile("DemoBanks/EMPTY_.sp12").loadFileAsData(empty);
  check(sp::process(sp::readBank(empty, "Empty")).bank == empty,
        "Empty hardware bank exports byte-identically");
  testRepacking(empty);
  p = baseline;
  const auto replacement = tone("REPLACE.wav");
  p.pool.push_back(replacement);
  sp::assignPad(p, 5, *replacement);
  const auto replaced = sp::process(p);
  auto replacementBank = sp::readBank(replaced.bank, "Replace");
  check((sp::assetFor(replacementBank, replacementBank.pads[5].asset)->native->flags & 128) == 0,
        "WAV replacement gets normal tune mode instead of inheriting old decay mode");
  opaqueUnchanged(original, replaced.bank);
  if (hardwareOutput != File()) {
    hardwareOutput.createDirectory();
    // Device fixtures use a valid empty sequence template, not the opaque-byte
    // sentinels used above to test preservation independently of the hardware.
    sp::Project tones;
    tones.normalizeOnExport = false;
    for (int i = 0; i < 4; ++i) {
      const double hz[] = {440, 660, 220, 880};
      auto sound = tone("TONE" + String(i) + ".wav", i == 2 ? 4. : 1., .1, 1, false, hz[i]);
      tones.pool.push_back(sound);
      tones.pads[i] = sp::makePad(*sound, i, false, true);
      tones.pads[i].channel = 1;
    }
    tones.pads[2].loopEnabled = true;
    tones.pads[2].loopSeconds = .5;
    const auto reference = sp::process(tones).bank;
    auto imported = sp::readBank(reference, "BANKREF");
    const auto copy = sp::process(imported).bank;
    check(copy == reference, "Hardware reference bank copy is byte-identical");
    sp::atomicWrite(hardwareOutput.getChildFile("BANKREF.sp12"), reference);
    sp::atomicWrite(hardwareOutput.getChildFile("BANKCOPY.sp12"), copy);
    sp::savePreset(imported, hardwareOutput.getChildFile("BANKREF.spkit"));
    imported.pads[0].start += .1;
    imported.pads[0].end -= .1;
    imported.pads[0].name = "TRIM";
    imported.pads[1].reverse = true;
    imported.pads[1].name = "REV";
    sp::atomicWrite(hardwareOutput.getChildFile("BANKEDIT.sp12"), sp::process(imported).bank);
    hardwareOutput.getChildFile("BANK-CHECK.txt").replaceWithText(
        "SP-1200 bank import hardware check\n\n"
        "Load BANKREF, then BANKCOPY. Their files are byte-identical.\n"
        "A1: 440 Hz, nominally 1 s. A2: 660 Hz, nominally 1 s.\n"
        "A3: Pitch Fit 220 Hz, nominally 4 s, last 0.5 s looped.\n"
        "A4: 880 Hz, nominally 1 s. All pads use output 1; A4 stops A3.\n\n"
        "Load BANKEDIT: A1 has 0.1 s removed at each end; A2 is reversed.\n"
        "A3 and A4 must remain unchanged. Save back under a new filename\n"
        "and compare it with BANKEDIT. Record attacks, cut boundaries and\n"
        "loop timing; a digital sample dump can settle exact counter endpoints.\n"
        "Hardware playback and exact endpoints are not confirmed by software tests.\n");
  }
  std::cout << "PASS: native bank codec, aliases, tune/decay, prefix, cuts, loops, edits, opaque data and format 8\n";
}
} // namespace bankTests
