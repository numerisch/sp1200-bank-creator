// SPDX-License-Identifier: AGPL-3.0-only
// Copyright © 2026 Numerisch GmbH

#pragma once

// Keep this embedded guide in sync with user-facing behavior.
namespace appInfo {
inline constexpr auto copyright =
    "Copyright © 2026 Knut Schade / Numerisch GmbH";
inline constexpr auto repository =
    "https://github.com/numerisch/sp1200-bank-creator";
inline constexpr auto licenseNotice =
    "Free and open-source software under the GNU Affero General Public License "
    "version 3 (AGPLv3). You may modify and redistribute it under this license. "
    "Provided without warranty, to the extent permitted by law. "
    "Choose Help > License for the full terms. "
    "The matching source code is provided alongside the app download.";
inline constexpr auto legalNotice =
    "E-MU and Rossum are trademarks of their respective owners. "
    "SP-1200 Bank Creator is independently developed by Numerisch GmbH and "
    "is not affiliated with, endorsed by, or sponsored by E-MU, Rossum, "
    "or their trademark owners.";
inline constexpr auto quickGuide = R"HELP(# SP-1200 Bank Creator
Quick Guide

## Import and Organize
Choose *Import Sample Folder* (Cmd+I), or drop WAV/AIFF files or folders into the app. Subfolders are included. Imports add to the pool and fill free pads; extra sounds stay in the pool. Unsupported files are skipped.

Drag a pool sound onto a pad to assign it. Drag between pads to move or swap sounds and their settings. Green pool entries are already assigned; the selected pad is green.

*Auto Map* On assigns sounds by instrument name:
• A: Kick, Snare, Clap, Rim, Closed Hat, Open Hat, Ride, Crash.
• B: Low/Mid/High Tom, Low/High Conga, Shaker, Tambourine, Cowbell.
• C: The second sound of each A instrument.
• D: Further variants and other percussion.

*Auto Map* Off restores the previous layout with your edits. New assignments fill free slots; overflow stays in the pool. Unedited sounds added only by mapping return to the pool. Instrument labels are hidden when Off.

## Listen and Edit
Click a pad or pool sound to play the SP-1200 preview: mono, processed, 12-bit audio with pitch compensation. *Stop Sound* stops playback. Analog SP filters and envelopes are not emulated.

Below the waveform, edit the six-character *Sound Name* and *Channel Output* (default 1–8 by pad position). *Reverse* changes playback direction. Original files remain unchanged.

## Trim
Drag the start/end handles, or drag inside the selection to move it without changing its length. Manual cuts are green and protected; automatic/default cuts are yellow. Each drag is one *Undo* step.

*Auto Trim* On removes leading and trailing silence on automatic pads. The threshold ranges from −60 to −5 dBFS; double-click resets it to −40 dBFS. Off restores the default selection. Manual cuts survive *Auto Trim*, threshold and *Pitch Fit* changes.

*Reset Trim* releases manual protection and reapplies the current trim mode. It also resets the loop start to the selection start without switching *Loop Off*.

## *Pitch Fit* and Memory
*Pitch Fit* On speeds up selections longer than about 2.5 seconds so they fit one sample slot. The exported *Tune* parameter restores their original pitch and duration. Up to about 6.27 seconds can fit, with reduced high-frequency detail. Shorter sounds keep neutral *Tune* 16.

*Pitch Fit* Off allows about 2.5 seconds. Switching it resets automatic selections only; manual cuts remain protected. Overlong automatic selections are limited to their slot capacity.

A red pad outline means the original file exceeds the full-export duration limit, even if its current selection fits. Memory warnings in the status bar show missing stored-audio capacity. Playback and *Save Kit* remain available; export is blocked until the bank fits. Shorten or clear sounds to free space. *Auto Trim* never cuts audible tails to solve total bank memory pressure.

## Loops
*Loop On* initially repeats the full selection. The blue middle handle sets the loop start; the trim end is also the loop end. The attack plays once, then the loop repeats until *Stop Sound* or another sound. *Loop Off* remembers the point.

Dragging *End* keeps a custom loop start fixed until it crosses that point. *Start* clamps it only when necessary. Moving the selection keeps the loop length. Loop playback continues as you edit. A shortened custom loop does not automatically grow again when the selection expands.

With *Loop On*, loop-start and end handles snap to nearby rising zero crossings within 5 ms; *End* moves inward only. This reduces clicks, but there is no crossfade. Export needs at least three stored loop frames. Loops never receive an export fade-out.

## *Settings* and Tooltips
Choose SP-1200 Bank Creator > *Settings* (Cmd+,):
• *Fade Out Sound on Export*: Off by default; On fades one-shots over up to 40 ms.
• *Normalize on Export*: On by default; sets each processed sound to −0.5 dBFS peak. Off keeps unity gain.

Both settings also affect preview and are saved in kits. *Pitch Fit*, *Auto Map*, *Auto Trim* and the threshold are remembered across restarts. Opening a kit restores its settings.

*Help* > *Show Tooltips* toggles hints for the global checkboxes and threshold slider; this preference is remembered.

## Clear, *Undo* and Save
*Clear Pad* or Delete/Backspace removes an assignment and keeps its sound in the pool. *Clear All* empties pads and pool. Text fields keep their usual editing keys.

• *Undo*: Cmd+Z. *Redo*: Shift+Cmd+Z.
• *Save Kit*: Cmd+S. Saves the entire pool, original samples, assignments and settings in a portable .spkit, even when memory is exceeded.
• *Open Kit*: Cmd+O. Replaces the current kit. Kit format 7 is supported; earlier versions are not migrated.
• *Export SP-1200 Bank*: Shift+Cmd+E. Prepares and validates all assigned sounds, then writes a Rossum .sp12 bank.

Suggested filenames use the first imported folder: up to 16 ASCII characters, with umlauts converted. Sound names use up to six ASCII characters.

)HELP";
} // namespace appInfo
