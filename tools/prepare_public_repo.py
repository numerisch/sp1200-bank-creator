#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright © 2026 Numerisch GmbH
"""Create a fresh publication snapshot without private development history."""

import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
FILES = (
    ".gitignore", ".gitattributes", "AGENTS.md", "CMakeLists.txt", "README.md", "LICENSE",
    "LICENSING.md", "THIRD_PARTY_NOTICES.md", "docs/RELEASING.md",
    "DemoBanks/EMPTY_.sp12", "tests/CoreTests.cpp", "tests/UiPlaybackTests.h",
    "tests/UiSnapshots.h", "tests/fixtures/codec32.sp12",
    "tests/fixtures/dr-sample-instruments.tsv", "tools/prepare_public_repo.py",
)
TREES = {
    "Source": {".h", ".cpp"},
    "Assets": {".svg", ".png", ".icns"},
    "LICENSES": {".txt", ".md"},
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--juce-source", type=Path,
                        help="Include the pinned JUCE sources for a binary release")
    parser.add_argument("--archive", type=Path,
                        help="Create a .tar.gz of the complete source snapshot")
    args = parser.parse_args()
    destination = args.destination.resolve()
    if destination.exists():
        parser.error("Destination already exists; choose a new directory")
    if args.archive and not args.juce_source:
        parser.error("A release archive requires --juce-source")
    archive = args.archive.resolve() if args.archive else None
    if archive and (archive.exists() or archive.is_relative_to(destination)):
        parser.error("Archive must be a new file outside the snapshot")
    selected = [ROOT / name for name in FILES]
    for name, extensions in TREES.items():
        selected.extend(path for path in sorted((ROOT / name).rglob("*"))
                        if path.suffix in extensions)
    selected = [path for path in selected if path.is_file()]
    for name in FILES:
        if not (ROOT / name).is_file():
            parser.error(f"Required publication file is missing: {name}")
    for path in selected:
        if path.is_symlink() or not path.resolve().is_relative_to(ROOT):
            parser.error(f"Publication file must be a regular local file: {path}")
    juce = args.juce_source.resolve() if args.juce_source else None
    if juce:
        required = re.search(r"GIT_TAG\s+(\S+)",
                             (ROOT / "CMakeLists.txt").read_text()).group(1)
        try:
            actual = subprocess.check_output(
                ["git", "-C", str(juce), "describe", "--tags", "--exact-match"],
                text=True, stderr=subprocess.DEVNULL).strip()
            dirty = subprocess.check_output(
                ["git", "-C", str(juce), "status", "--porcelain"], text=True)
        except subprocess.CalledProcessError:
            parser.error("JUCE source must be a Git checkout of the pinned tag")
        if actual != required or dirty:
            parser.error(f"JUCE must be an unmodified checkout of {required}")
    destination.mkdir(parents=True)
    for path in selected:
        target = destination / path.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        target.chmod(0o644)
    if juce:
        shutil.copytree(juce, destination / "third_party/JUCE",
                        ignore=shutil.ignore_patterns(".git", ".DS_Store", "__pycache__"))
    manifest = []
    for path in sorted(destination.rglob("*")):
        if path.is_file():
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            manifest.append(f"{digest}  {path.relative_to(destination).as_posix()}")
    if juce:
        (destination / "SOURCE_SHA256SUMS").write_text("\n".join(manifest) + "\n")
    if archive:
        archive.parent.mkdir(parents=True, exist_ok=True)
        with tarfile.open(archive, "w:gz") as output:
            output.add(destination, arcname="sp1200-bank-creator")
    print(f"Prepared {len(manifest)} publication files in {destination}")
    if archive:
        print(f"Source archive: {archive}")


if __name__ == "__main__":
    main()
