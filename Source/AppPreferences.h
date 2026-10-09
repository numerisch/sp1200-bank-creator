// SPDX-License-Identifier: AGPL-3.0-only
// Copyright © 2026 Numerisch GmbH

#pragma once
#include "Core.h"
#include <cmath>

namespace sp {
// Only global controls are remembered here; kit audio remains in .spkit files.
class AppPreferences {
public:
  static juce::PropertiesFile::Options options() {
    juce::PropertiesFile::Options o;
    o.applicationName = "SP-1200 Bank Creator";
    o.filenameSuffix = "settings";
    o.folderName = "SP-1200 Bank Creator";
    o.osxLibrarySubFolder = "Application Support";
    o.millisecondsBeforeSaving = -1;
    return o;
  }
  explicit AppPreferences(const juce::File &file = options().getDefaultFile())
      : properties(file, options()) {}

  void restore(Project &project) const {
    const Project defaults;
    project.autoTune = properties.getBoolValue("autoTune", defaults.autoTune);
    project.autoSort = properties.getBoolValue("autoSort", defaults.autoSort);
    project.autoTrim = properties.getBoolValue("autoTrim", defaults.autoTrim);
    project.fadeOutOnExport =
        properties.getBoolValue("fadeOutOnExport", defaults.fadeOutOnExport);
    project.normalizeOnExport = properties.getBoolValue(
        "normalizeOnExport", defaults.normalizeOnExport);
    const auto value =
        properties.getDoubleValue("threshold", defaults.threshold);
    project.threshold = std::isfinite(value)
                            ? juce::jlimit(-60.0, -5.0, std::round(value))
                            : defaults.threshold;
  }
  bool tooltipsEnabled() const {
    return properties.getBoolValue("tooltipsEnabled", true);
  }
  bool setTooltipsEnabled(bool enabled) {
    properties.setValue("tooltipsEnabled", enabled);
    return properties.saveIfNeeded();
  }
  bool save(const Project &project) {
    properties.setValue("autoTune", project.autoTune);
    properties.setValue("autoSort", project.autoSort);
    properties.setValue("autoTrim", project.autoTrim);
    properties.setValue("threshold", project.threshold);
    properties.setValue("fadeOutOnExport", project.fadeOutOnExport);
    properties.setValue("normalizeOnExport", project.normalizeOnExport);
    return properties.saveIfNeeded();
  }

private:
  juce::PropertiesFile properties;
};
} // namespace sp
