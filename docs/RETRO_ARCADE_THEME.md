# Retro Arcade theme maintenance

The persistent entry point is `SettingsManager::visualStyle`. It is independent
from the saved `appearance` and `accentColor` choices, so Standard restores the
user's Dark, Light, or System mode and accent after a theme switch. The
`SettingsManager::palette` map is the shared token provider for the main window,
pages, reusable controls, and detached windows. It contains semantic roles
(`window`, `surface`, `text`, `focus`, `success`, and related tokens) plus the
legacy hex keys still used by existing pages.

When adding UI, use semantic palette roles for new surfaces, text, borders, and
states. Keep the existing hex aliases complete until every old QML lookup has
been migrated. Use the current QML components and page models for behavior;
theme changes should only update colors, metrics, and presentation. Register
new QML files and assets in both `ScreenshotHotkeyManager` and
`ScreenshotHotkeyManagerTests` in `CMakeLists.txt`.

The Retro Arcade chrome uses original local SVG artwork in `qml/assets` and the
bundled Cascadia Code monospaced font. Its Microsoft copyright notice and SIL
Open Font License are in `qml/assets/fonts/LICENSE-CASCADIA-CODE.txt`. The
standard theme retains the platform default font. Native Windows caption
controls, native file dialogs, tray menus, and OS notifications keep their
native styling.
