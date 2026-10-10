# Project maintenance

- Keep the embedded English quick guide in `Source/HelpContent.h` current whenever user-facing behavior, controls, shortcuts, defaults, import, processing, presets, or export change. Keep it concise.
- `CMakeLists.txt`'s project version is the single source of truth for the app bundle and About dialog. Increment the patch version for subsequent fixes or minor UI changes and the minor version for feature releases; do not hard-code another version in the UI.
- Preserve `Copyright © 2026 Knut Schade / Numerisch GmbH` in About and bundle metadata.
- After changes, build and run appropriate checks, then restart the app as requested by the user. The user has explicitly authorized discarding unsaved kits during agent-initiated restarts. Try a regular quit first; if the Unsaved Kit dialog blocks it, terminate only the verified SP-1200 Bank Creator process and relaunch the built app. Do not ask for confirmation again. This authorization does not change the normal in-app quit dialog.

- Use English Title Case for control labels, menu items, dialog titles and buttons (e.g. Clear All, Clear Pad, Auto Trim, Auto Sort). Keep explanatory sentences and status messages in normal sentence case.
