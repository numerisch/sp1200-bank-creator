# Website Releases

This project is distributed free of charge under AGPLv3 by Numerisch GmbH.
The public repository is named `sp1200-bank-creator`. Its GitHub owner and URL
must be set when the repository is created; they are not assumed here.

## Public Repository

Use `tools/prepare_public_repo.py` to copy only the publication allowlist to a
new directory. It does not copy the private Git history, hardware captures,
sample libraries, firmware, research notes, build products or credentials.
It refuses an existing destination and never modifies or deletes the private
development checkout.

```sh
python3 tools/prepare_public_repo.py output/public-repo/sp1200-bank-creator
git -C output/public-repo/sp1200-bank-creator init -b main
git -C output/public-repo/sp1200-bank-creator add .
git -C output/public-repo/sp1200-bank-creator commit -m "Initial AGPLv3 release"
```

Create the public GitHub repository without another README or license, then add
its actual URL as `origin` in this new checkout and push `main`. Do not push the
private development checkout or its old history. A GitHub organization can be
administered by personal GitHub accounts; use Numerisch's organization if one is
created, or an appropriate existing account with the company's authorization.

## Build and Test

Follow the README's Universal release build and CTest commands. Check import,
preview, kit save/open and bank export on a separate Mac. Hardware bank tests
are supplemental; the public suite works without private captures.

## Signing and Notarization

Use Numerisch's Apple Developer team. Website downloads require a **Developer
ID Application** certificate. **Apple Distribution** is an App Store identity
and must not be used for this release. App Sandbox is not required for direct
distribution; enable Hardened Runtime for the signed release.

Create the Developer ID Application certificate through the account holder in
Xcode or the Apple Developer portal and install its private key in the signing
Mac's Keychain. Keep keys and notarization credentials out of Git and archives.
Sign the release with Hardened Runtime and a secure timestamp, package it as a
DMG, submit it with `xcrun notarytool`, and attach the accepted ticket with
`xcrun stapler`. Verify signing, stapling and Gatekeeper behavior after a real
HTTPS download before offering the release publicly.

References:
- https://developer.apple.com/developer-id/
- https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution

## Corresponding Source

For each binary release, generate a complete source archive from that exact
code revision, including the unmodified JUCE sources used to build it:

```sh
python3 tools/prepare_public_repo.py output/source-release/sp1200-bank-creator \
  --juce-source build/_deps/juce-src \
  --archive output/sp1200-bank-creator-source.tar.gz
```

The archive includes JUCE at `third_party/JUCE`, so it can be built without
downloading the framework:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64' \
  -DFETCHCONTENT_SOURCE_DIR_JUCE="$PWD/third_party/JUCE"
cmake --build build -j 6
ctest --test-dir build --output-on-failure
```

Place the DMG and corresponding source archive next to each other on the
download page. Include the version, minimum macOS version, license, repository
link and SHA-256 checksums. Users can build and redistribute modified versions
under AGPLv3; signing is a separate distribution step, not a prerequisite for
building or testing the source.
