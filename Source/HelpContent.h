// SPDX-License-Identifier: AGPL-3.0-only
// Copyright © 2026 Numerisch GmbH

#pragma once

// Keep this embedded guide in sync with user-facing behavior.
namespace appInfo {
inline constexpr auto copyright = "Copyright © 2026 Numerisch GmbH";
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
inline constexpr auto quickGuide = R"HELP(SP-1200 Bank Creator — Quick Guide

About
Choose SP-1200 Bank Creator > About SP-1200 Bank Creator for the version, copyright, license and trademark notice. Help > License displays the full AGPLv3 license. This app is free and open source; the matching source code is provided alongside the download.

Settings
Choose SP-1200 Bank Creator > Settings (Cmd+,). Fade Out Sound on Export defaults to Off; On applies up to 40 ms of fade-out at compensated speed to every one-shot, including manual cuts. Loops always stay unfaded. Normalize on Export defaults to On and independently peak-normalizes each sound to -0.5 dBFS after processing. Off keeps unity gain; 12-bit conversion still applies. These settings also affect preview, persist across restarts, are stored in kits, and support Undo/Redo.

Import and Organize
Use Import Sample Folder (Cmd+I), or drop WAV/AIFF files or folders onto the app. Subfolders are included. Imports add to the pool and fill empty pads; existing assignments stay intact. Extra samples remain in the pool. Unsupported files are skipped.

Empty pads show only their pad label with Auto Map Off. With Auto Map On, A/B/C show their fixed target instruments; every D pad is labeled Other, including empty pads. C mirrors A: BD, SD, Clap, Rim, Closed Hi-Hat, Open Hi-Hat, Ride, Crash. The first hit of each type fills A, the second its matching C slot; A does not need to be completely full. Further variants and other percussion fill D. These instrument labels are hidden when Auto Map is Off; the pool keeps its category labels.

Auto Map is Off on first launch: new samples fill free pads in order. Turn it On to map the kit by instrument name (kick on A1, snare on A2, etc.), preserving existing sound settings and remembering the previous layout. HHO means open hi-hat (A6); CYM/Cymbal/China uses the crash pad (A8), while an explicit Ride name takes priority. Sidestick/Side Stick/Cross Stick uses the rimshot pad (A4). Bongo, Timbale and generic Perc/Percussion are recognized as extra groups on D; excess samples remain in the pool. Perc Shake is recognized as Shaker. Specific instrument names take priority over generic Perc labels. LoFi is an effect label, not a low-pitch indication. Turning it Off restores the previous layout, keeping current names, trims, loops, Reverse and outputs with their assignments. Cleared or replaced assignments stay removed. New imports and new pool assignments fill free pads in pool order; overflow stays in the pool. Unedited sounds added only by the initial map return to the pool. Editing or moving one retains it as a new assignment on return. Re-enabling remembers the current layout afresh. Undo/Redo and Save Kit include the return layout, including temporarily displaced assignments and independent copies of the same sound. Drag from the pool to assign a sound. Drag between pads to swap or move sounds. Green pool entries show assigned samples. A green tint and outline mark the selected pad; if it also has a red duration warning, the outer red outline remains and a green inner outline marks selection; red pad indicators mark assigned sounds. A red pad outline means the original sample is too long to export in full: over about 2.5 seconds with Pitch Fit Off or 6.27 seconds with Pitch Fit On. The outline remains even if a shorter selection fits and can be exported. It depends on original source length, independently of manual cuts and Auto Trim.

Listen
Click a pad or pool sample to hear the SP-1200 preview: mono, processed and quantized to 12 bits, with tuning compensation. Unassigned pool samples are previewed independently. Settings update the trim, tuning and memory plan for all pads; only the selected or auditioned pad is rendered. Unchanged audio is cached. Gain details show pending until that pad is ready. Editing stays available during background processing. Pads remain playable when the bank exceeds memory: they are previewed individually with their current cuts, Reverse and processing settings. Memory warnings remain until the bank fits; export stays unavailable. Red outlines indicate original samples exceeding the per-sample duration limit. Analog SP filters are not emulated. Stop Sound is active only during playback.

Trim and Edit
Auto Trim is Off on first launch. Drag the waveform handles in either mode to set manual start/end points, or drag inside the selection to move both boundaries without changing its length. Automatic/default boundaries are yellow; both handles and boundary lines turn green for a manually edited selection. Reset Trim (button beside Clear Pad or Edit menu) restores yellow boundaries. Only a changed selection becomes protected; each drag creates one Undo step. Details show Trim: Manual for protected cuts, Trim: Auto for threshold trimming and Trim: Default for the default selection.

Auto Trim On trims automatic pads using the threshold (-60 to -5 dBFS; double-click resets to -40 dBFS). Off resets automatic pads to start 0 through the current maximum, capped by source duration. Manual cuts survive On/Off, threshold and Pitch Fit changes. Reset Trim, beside Clear Pad or in the Edit menu, releases the selected pad's manual protection: On applies automatic trimming; Off restores the default selection. It also resets the loop start to the selection start (full-selection loop), keeping Loop On/Off unchanged. Reset Trim is available for a custom loop point even without manual trimming. Undo/Redo restores cuts, protection and global settings together.

With Pitch Fit Off, new selections span up to about 2.5 seconds; On permits about 6.27 seconds of source audio stored in a 2.5-second slot. Switching Pitch Fit resets only automatic selections. If a protected selection becomes too long after switching Off, it stays intact and playable, with a memory warning and export blocked. Reverse mirrors cuts and preserves protection. Moving or swapping pads carries their cuts and protection; assigning a pool sample starts without protection.

Hover over Pitch Fit, Auto Map, Auto Trim or the threshold slider for a tooltip explaining the control. Help > Show Tooltips enables or disables tooltips immediately; a checkmark means On. Tooltips default to On and this preference is remembered across restarts, independently of kits.

Pitch Fit, Auto Map, Auto Trim and the threshold are remembered across app restarts, even without saving a kit. Opening a kit restores its settings; these become the remembered values. Undo/Redo also updates the remembered settings.

Edit the six-character Sound Name and Channel Output below the waveform. New assignments default to outputs 1–8 by pad position in each bank. Reverse toggles the selected pad's direction. Original source files are preserved.

Loops
Loop Off / Loop On beside Reverse controls a forward tail loop. On initially repeats the entire selection; the blue middle handle sets the loop start, while the selection end is also the loop end. The attack plays once, then repeats until Stop Sound or another sound. Retriggering starts from the beginning. Dragging the blue handle creates one Undo step without making the trim Manual. Off hides the handle and remembers its setting.

Dragging End keeps the custom loop start at its sample position; if End crosses it, the loop start follows End with a one-frame minimum for preview. Dragging Start clamps the loop start only when necessary. Moving the whole selection keeps the loop duration. Looping preview continues while dragging Start, End or the loop handle; new bounds apply in the background without restarting the attack. Stop Sound and sound changes cancel pending updates. A custom loop keeps its length in seconds when the selection moves, Reverse changes or Pitch Fit changes. Automatic selection shortening, or moving Start past the loop point, permanently clamps its duration. This includes Auto Trim, threshold and individual sample limits; later automatic expansion does not regrow it. Manual End edits instead change the loop duration to preserve the loop point. The default full-selection loop follows the selection. Undo/Redo includes these corrections. Pad moves and swaps keep loops; new assignments and Clear Pad reset them to Off. Loop samples skip fade-out even when enabled in Settings. With Loop On, the loop and end handles snap to nearby rising zero crossings in the processed mono audio (within 5 ms at compensated speed). End only moves inward; if no crossing is available, the boundary stays unchanged. Snapping applies after the background calculation and Undo/Redo restores it with the edit. Preview and export use the same bounds. Snapping reduces clicks but cannot guarantee a seamless loop; there is no crossfade. Hardware export requires at least three stored loop frames; shorter loops remain playable and saveable, with export blocked and a status message.

Memory
Pitch Fit (left of Auto Map) is On on first launch. It only speeds up selected audio longer than one 2.5-second slot, choosing the smallest speedup that fits. The SP-1200 Sound Program stores the matching lower tune value to restore the original pitch and playback duration. The maximum original duration is about 6.27 seconds. Shorter sounds keep neutral tune (16), even if the bank is full. Global memory pressure never applies additional pitch changes. With Pitch Fit Off, samples keep their pitch and neutral tune; the maximum stored duration is about 2.5 seconds per sample. Auto Trim removes leading/trailing silence using the threshold; it never shortens audible material to fit the total bank. If the kit exceeds capacity or a zone cannot hold the sounds, the selections stay intact and a status warning blocks only export. Playback and Save Kit remain available. Shorten or clear samples to free memory. Protected manual selections are never automatically shortened. Pitch Fit helps only with individual samples longer than one slot. With Auto Trim On, an individual overlong automatic sample is still shortened if it exceeds its own slot limit after Pitch Fit (when enabled); fade-out is optional in Settings and applies only with Loop Off. Memory errors appear only in the status bar, never in a popup. Kit warnings show the excess in seconds of stored audio, including guard frames and all eight memory zones. When total capacity is sufficient but the remaining space is split across zones, the warning instead shows unplaced audio and free space across zones. Sample-limit warnings show the combined excess above individual sample limits. Seconds refer to stored audio before playback tune compensation.

Clear and Undo
Clear Pad or Delete/Backspace removes the selected assignment but keeps its sample in the pool. Text fields retain normal editing keys. Clear All empties the pool and pads. Edit > Undo (Cmd+Z) and Redo (Shift+Cmd+Z) restore kit changes.

Save and Export
File > Save Kit (Cmd+S) creates a portable .spkit with the entire pool, original samples and settings. The proposed kit name uses the first imported folder, with the same 16-character ASCII name and umlaut conversion as export. Saving is available even while processing or when memory is exceeded; only bank export requires a valid memory and loop plan. Reopening an overfull kit preserves its cuts and assignments, and shows the memory warning again. Open Kit (Cmd+O) replaces the current project. Kit format version 7 stores manual trim protection and loop settings per pad. Earlier kit versions are unsupported and are not migrated.

Export SP-1200 Bank (Shift+Cmd+E) renders any missing or changed pads, validates the full bank, and writes a Rossum .sp12 containing assigned sounds. Allow preparation to finish before the file is written. The proposed name comes from the first imported folder. Export names use up to 16 ASCII characters before .sp12; umlauts are transliterated. If an entered name needs adjustment, confirm the corrected name in the save dialog.
)HELP";
} // namespace appInfo
