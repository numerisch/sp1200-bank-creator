# SP-1200 Bank Creator

A free, open-source macOS kit editor for the Rossum SP-1200 `.sp12` format.
Developed by Numerisch GmbH. Requires **macOS 13 or later**; Universal builds
support Apple Silicon and Intel.

## Features

- Import WAV/AIFF files and folders, including subfolders.
- Arrange 32 pads, with optional instrument recognition and automatic mapping.
- Preview the processed mono, 12-bit sound with compensated tuning.
- Edit sample boundaries, reverse, output routing and forward tail loops.
- Optional silence trimming, pitch fitting, normalization and one-shot fades.
- Save portable `.spkit` projects containing the original samples and settings.
- Export Rossum `.sp12` banks, with memory and sample-length validation.

The app preserves original samples. Undo/Redo includes assignments and edits.
The embedded guide under **Help > SP-1200 Bank Creator Help** explains controls,
shortcuts and defaults. **Help > License** displays the full AGPLv3 license.

Export targets Rossum `.sp12` banks. Importing `.sp12`, HFE/floppy export, MIDI
transfer, sequencer editing and analog output-filter emulation are not included.

## Download

Downloads are published free of charge through
[GitHub Releases](https://github.com/numerisch/sp1200-bank-creator/releases).
Each release includes a Developer ID-signed and Apple-notarized macOS DMG and
the matching complete source archive, including JUCE. Until a release is
published, build the app from source using the instructions below.

## Build

Install the Xcode command-line tools, CMake 3.22 or later, and Git:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64'
cmake --build build -j 6
ctest --test-dir build --output-on-failure
```

The first configure downloads pinned **JUCE 8.0.9** from its official repository.
The app is `build/SP1200BankCreator_artefacts/Release/SP-1200 Bank Creator.app`.
Python is not required to build or run the app; the source packaging helper uses
Python 3.9 or later.

Complete release source archives also contain JUCE at `third_party/JUCE`. To
build from these without downloading JUCE, add
`-DFETCHCONTENT_SOURCE_DIR_JUCE="$PWD/third_party/JUCE"` to the configure command.

## Tests and Source Layout

- `Source/`: application, audio processing, bank format, preferences and guide.
- `Assets/`: original app artwork, including editable SVG.
- `DemoBanks/EMPTY_.sp12`: the maintainer's self-created empty export template.
- `tests/`: synthetic codec, processing, mapping, persistence and UI regressions.
- `LICENSES/`: retained third-party notices.
- `docs/RELEASING.md`: source packaging and GitHub release instructions.
- `tools/prepare_public_repo.py`: creates a publication snapshot from an allowlist.

Core tests use generated signals and a synthetic codec fixture. The instrument
recognition fixture contains filenames only, with no sample-library audio.
Private hardware captures can supplement local tests but are not needed for the
public test suite. The AppStartup and MemoryErrorPlayback tests open the native
UI and require a macOS graphical session.

## License

Copyright © 2026 Numerisch GmbH

The original project is licensed under **GNU AGPLv3 only** (`AGPL-3.0-only`).
You may use, modify and redistribute it under that license, including commercially.
It is provided without warranty to the extent permitted by law.
See [LICENSE](LICENSE), [LICENSING.md](LICENSING.md) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

JUCE is used under its AGPLv3 option. E-MU and Rossum are trademarks of their
respective owners. This project is independent and is not affiliated with,
endorsed by, or sponsored by those manufacturers or their trademark owners.
