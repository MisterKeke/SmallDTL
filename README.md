# SmallDTL

A local Windows productivity application built with C++20, Qt 6/QML, WinAPI, and CMake. It saves additional PNG copies of clipboard images, manages independent profiles/action sequences, and registers global shortcuts. It works offline and never uploads user files.

## Features

- Ten pages: Dashboard, Screenshots, QR Codes, Clipboard History, File Shelf, Window Arrangements, Hotkeys, Profiles, Actions, Settings; dark/light/system appearance and a configurable accent.
- Clipboard saving from Screenshots, Ctrl+V, tray, or a configured hotkey. Text paste remains functional in editors/dialogs.
- Optional automatic saving of images copied from other apps, with a tray pause/resume action, duplicate-content filtering, and an optional automatic tag. Manual saves remain available; app-owned clipboard copies are ignored.
- Clipboard history for copied text, images, and file references, with a searchable page and configurable global popup shortcut. Entries can be restored to the clipboard, pinned, deleted, or explicitly saved to the screenshot gallery with an optional tag.
- File shelf for drag-and-drop or dialog-added file/folder references, with Unicode-safe duplicate checks, availability details, selection, open/copy-path actions, native file URL dragging, and a configurable compact-panel shortcut. Copy and Move to folder run in the background and preview skip/rename/explicit replacement choices; removing shelf references never deletes source files.
- Destination priority: session folder, active profile folder, general default. Unavailable destinations produce errors without redirection.
- Validated filename templates, Unicode paths, and a no-replace PNG commit. Original Windows screenshots remain untouched.
- SQLite-backed screenshot records and tags, with gallery filtering, checkboxes, Ctrl/Shift multi-selection, Select All, and transactional mixed-state bulk tagging.
- Managed gallery, lazy cached thumbnails, metadata, preview, open/copy/rename, remove membership, and separately confirmed deletion. Removing tags or gallery membership keeps files.
- Screenshot editor with source-pixel cropping, optional aspect-ratio locking, freehand drawing, arrows, rectangles, ellipses, highlighting, text, and opaque black redaction. Color, stroke width, text size, undo/redo, reset, zoom, and pan are supported. Save as copy is the default, and can inherit the source screenshot's tags.
- Local QR generation and decoding from text, URLs, clipboard images, gallery screenshots, files, or drag-and-drop. Generated codes can be copied, exported, or explicitly saved to the screenshot gallery with an optional tag. Decoded content is shown for review; SmallDTL never opens URLs or runs decoded content.
- Profile CRUD, independent duplication, descriptions/icons, destinations, activation shortcuts, and self-contained import/export.
- Profile-specific and application-wide hotkeys with recording, editing, disabling, actual registration status, and conflicts.
- Manually triggered window placement for open applications, named profile layouts with executable/title matching, ambiguity preview, monitor placement, and restore previous arrangement.
- Ordered executable/arguments, folder, file, HTTP/HTTPS URL, screenshot, profile-switch, window-layout, and asynchronous-wait actions, with editing, cancellation, and failure reporting.
- Windows startup, start minimized, minimize/close to tray, and optional success notifications. Exit cancels sequences, completes/indexes pending PNG writes, and unregisters hotkeys.

## Profiles and actions

Only explicit user interaction, a configured hotkey, or a sequence started by the user switches profiles. The app does not monitor foreground applications or switch automatically.

Each profile owns its sequences and assignments. Duplication creates fresh IDs and independent copies. The same shortcut can be reused across different profiles. Global actions and all profile activation shortcuts remain registered across switches. Use the Actions/Hotkeys scope picker to edit application-wide definitions.

Window Arrangements operates only on eligible windows that are already open. Users can place selected windows in halves, quadrants, center, maximize, or move them to a selected monitor; saving/applying a layout is always manual, shortcut-triggered, or a user-started sequence step. The app does not launch missing matches or change profiles automatically. Preview reports missing and ambiguous matches before saved layouts are applied, with explicit selection for ambiguous windows. Previous native positions are kept in memory for the current run.

Display choices show the monitor model reported by Windows, full display resolution, and a Primary marker. If Windows cannot provide a model, a numbered Display label is used. Identical names receive a display-number suffix. Optional nicknames can be saved or reset from Window Arrangements; these change SmallDTL's labels only. Nicknames use the monitor device path when available, with the Windows display identifier as fallback. Layout placement continues to use its existing Windows display identifiers.

Remove shortcut assignments before deleting their sequence. Running sequences use a snapshot; editing cannot rewrite an in-progress run. Stop cancels later actions and waits. Already launched processes, opened URLs, and dispatched PNG writes cannot be recalled.

## Persistence and limits

Window layouts live in `window-layouts.json` beside application state. Each layout records an owning profile, executable identity, optional title-contains rule, monitor device, and normalized placement; the file is separate from settings and screenshot records.

Settings use schema 1 with backward-compatible preferences; profiles use schema 3 with embedded sequences and layout shortcut references. Stable IDs determine filenames. `profiles/global.json` owns application-wide definitions. Atomic-save failures report errors and preserve the previous state.

Legacy shared `actions.json` is migrated independently into each legacy profile. Original actions and profile backups are retained. Invalid/newer configurations are preserved. Import accepts current self-contained profile exports, regenerates IDs, disables shortcuts, and clears activation shortcuts for review. Window layouts are stored separately in `window-layouts.json`, associated with a profile, and refer to executable paths plus normalized monitor work-area placement. Profile copies do not silently bind to another profile's layouts.

Data uses Qt's application-data directory, not the executable folder: settings and profiles remain in `settings.json` and `profiles/*.json`; screenshot records, tags, and clipboard-history metadata use `screenshots.db`; history-owned image payloads use `clipboard-history/`; thumbnails use `cache/thumbnails/`. Automatic clipboard saving is off by default and can be enabled in Settings. Pause or resume it from the tray without opening the window. A selected tag is applied to each automatic save. Repeated image notifications are fingerprinted on a worker and deduplicated for the current run; manual saves are never deduplicated. Accepted automatic saves keep the destination and profile active at event time. Unavailable destinations and a full save queue are reported, and saves are not redirected. The legacy `screenshot-index.json` is imported transactionally on first launch and retained unchanged as a migration source. Import failures are reported and retried on the next launch.

Clipboard history captures text, clipboard images, and file URLs from external clipboard changes. It does not record SmallDTL's own clipboard writes. The default popup shortcut is Ctrl+Shift+V and can be changed in Settings; native registration status appears beside the recorder. Restore copies the selected content to the clipboard only, without typing or pasting into another application. File references keep their original URLs; missing local files are reported and neither copied nor deleted. Saving a history image to the gallery creates a separate PNG and gallery record. History payloads are separate from gallery files. Age, count, and image-storage limits are configurable; ordinary cleanup removes oldest unpinned entries, while pins are preserved even when they exceed configured limits. Clear History also clears pinned entries. Pause/resume applies to history capture and is available from the tray.

The File Shelf stores only source paths and display metadata in SQLite. Add files/folders through the buttons or by dropping them onto the shelf. The default global shortcut is Ctrl+Shift+F. Shelf selection supports checkboxes, Ctrl-click, Shift-click, and Select All. The compact popup can open items and locations, copy paths, and start a native file URL drag for compatible Windows applications. Copy/Move to folder shows destination-name collisions and applies per-item skip, rename, or explicit replacement choices on a worker thread. A successful Move updates the shelf reference while retaining its stable ID. Removing a reference or clearing the shelf never removes its source; missing references stay listed and can be removed.

The screenshot database uses versioned SQLite migrations (schema 7), foreign-key enforcement, stable screenshot IDs, and unique tag names and screenshot-tag pairs. Screenshot and file-shelf paths use Unicode case-folded keys. Upgrading merges existing case-only screenshot duplicates, preserves their tags, and redirects edited-copy source links to the retained ID. Clipboard history records use unique content fingerprints and stable IDs. Renames update paths without changing IDs or tags. Removing gallery membership removes the database record and its assignments while leaving the image on disk.

Profile saves and exports enforce the same 4 MiB limit used by profile loading and import. Oversized edits fail while keeping the previous saved state. Ctrl+V is reserved across all shortcut editors. Sequences check that referenced window layouts exist before running, and layouts referenced by shortcuts or sequence steps must be unassigned before deletion.

File Shelf moves protect source files from concurrent writes and renames until copying and source removal finish. If an application is editing a source, close it before retrying the move. Folder moves copy a protected manifest, reject changes to that manifest, and remove only copied objects without recursively deleting newly added files. A failed or cancelled source-removal phase reports the complete destination copy and preserves remaining source data.

QR generation and decoding run locally through statically linked ZXing-C++ v3.1.1, licensed under Apache-2.0. The portable package includes the upstream license at `licenses/ZXing-C++-Apache-2.0.txt`; no QR runtime DLL or network service is required. The QR page supports module size, quiet-zone margin, and L/M/Q/H error correction. Action sequences can generate a QR image to the clipboard or decode a local image path.

Automatic clipboard records use the source label `clipboard-auto` and retain the profile ID captured at event time. Settings, including history retention and shortcut preferences, and profiles stay in JSON. The portable package carries Qt's `sqldrivers/qsqlite.dll` alongside the executable.

Filename templates support `{timestamp}`, `{date}`, and `{time}`; default `Screenshot_{timestamp}`. PNG extensions and collision suffixes are automatic. Limits: 64 megapixels/256 MiB per image, two pending writes, 512 MiB queued image data, and 256 MiB Qt decoding allocation. Editor previews and overlay textures are bounded; full-resolution crop/draw export runs on a worker. Metadata is asynchronous and thumbnails load on demand.

## Build and run

Prerequisites: Windows 10/11, C++20, CMake 3.24+, and Qt 6.8+ with Core, Gui, Quick, Quick Controls 2, Widgets, Concurrent, Network, Sql, and Test. The QSQLITE driver must be available. Use the compiler compatible with the Qt kit. Test is only required with `BUILD_TESTING` enabled.

Example from a Qt/compiler-enabled PowerShell:

```powershell
cmake -S . -B build-debug -G "MinGW Makefiles" `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/mingw_64" `
  -DCMAKE_CXX_COMPILER="C:/Qt/Tools/mingw1310_64/bin/g++.exe" `
  -DCMAKE_RC_COMPILER="C:/Qt/Tools/mingw1310_64/bin/windres.exe" `
  -DCMAKE_MAKE_PROGRAM="C:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe"
cmake --build build-debug --parallel 4
ctest --test-dir build-debug --output-on-failure
./build-debug/SmallDTL.exe
```

For Release, configure another directory with `-DCMAKE_BUILD_TYPE=Release`. Multi-configuration generators use `--config Debug`/`--config Release`. Native hotkey tests require Windows desktop API access; restricted sandboxes can deny registration even with offscreen Qt.

## Portable packaging

```powershell
./tools/package-windows.ps1 `
  -QtRoot "C:/Qt/6.11.0/mingw_64" `
  -CompilerBin "C:/Qt/Tools/mingw1310_64/bin" `
  -CMakePath "C:/Qt/Tools/CMake_64/bin/cmake.exe"
```

This builds/tests Release, deploys Qt/MinGW and `sqldrivers/qsqlite.dll`, smoke-tests database initialization without development search paths, and creates a directory/ZIP under `out/`. Existing output directories are preserved; choose a fresh `-OutputDirectory` when rerunning. Manifest and smoke-result JSON record outcomes. `--smoke-test` uses temporary data and exits automatically; ordinary runs use persistent user data.

Exit an older running instance through **tray → Exit** before launching an updated build. Closing its window may only hide it; a second instance normally activates the existing window.

The executable is now `SmallDTL.exe`. The existing data directory and internal storage identity are retained so profiles, sequences, and settings continue to load. If Windows startup was enabled for an older executable path, switch it off and on after launching the new build to update that path.

## Verification

Storage cleanup/retry and compaction controls, persistent clipboard privacy preferences, confirmed clearing with optional pins, asynchronous paged history, cancellable file transfers, and bounded thumbnail caching are described in [maintenance behavior](docs/MAINTENANCE.md). Tests are split by subsystem, and Windows CI builds/tests/packages the app. Use `tools/verify-windows-interactive.ps1` to record physical desktop checks separately.

See [the completion audit](docs/PROMPT_AUDIT.md), [configuration decisions](docs/CONFIGURATION.md), and [executed checks](docs/VERIFICATION.md).

Manual Windows checklist (not implied by unit/offscreen success):

- Save images from real Windows screenshot tools; verify original files and clipboard data stay unchanged. Try text/empty clipboard and unavailable destinations.
- Edit a screenshot at several zoom levels and monitor DPI settings; verify crop bounds and drawing line widths in the saved copy. Exercise undo/redo, reset, tag inheritance, save failure, and discard confirmation.
- Use physical shortcuts while other apps are focused, switch profiles, and test a binding registered by another program.
- Arrange windows on mixed-DPI and multi-monitor desktops, including a monitor left of the primary screen; verify work-area placement around the taskbar, saved-layout preview/resolution, disconnected-monitor fallback, and Restore previous arrangement.
- Copy text, images, and file references from other apps; open the history popup with its shortcut; restore, pin, search, pause, and save an image to the gallery. Rename or remove a referenced file and verify the missing-reference message and ownership boundaries.
- Add file/folder references through dialogs and Windows drag-and-drop; verify Unicode paths, duplicate detection, persistence, and missing items. Drag selected references into Explorer or another compatible application, and exercise Copy/Move conflicts, partial failures, and shelf clearing while confirming source files are preserved except for explicit successful Move operations.
- Minimize/close/reopen via the real tray, inspect its profile menu, and exit; verify hotkey release and hidden operation.
- Enable/disable startup in the real Windows account and sign out/in. Check the installed executable path.
- Restart the actual app and verify restored profile, preferences, sequences, order, enabled states, and assignments.
- Exercise Windows executable/file/folder/browser associations, permissions failures, and cancellation during waits/saves.
- Check native dialogs, keyboard navigation, DPI scaling, and Windows 10 as well as Windows 11.
