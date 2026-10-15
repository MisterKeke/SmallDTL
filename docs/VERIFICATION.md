# Verification record

Date: 2026-10-14, Europe/Istanbul. Host: Windows 11 x64, Qt 6.11.0, GNU/MinGW 13.1.0.

The final Debug build completed and the Qt Test run reported **33 passes, 0 failures, 0 skips**, including initialization/cleanup (31 test cases/data rows). The suite includes real native Windows registration/conflict/scope checks, the QML sequence editor, failed settings/hotkey editor recovery, configuration recovery/migration, import/export, clipboard-save completion, shutdown, paste, and resource/cache checks.

Native registrations were tested outside the Codex sandbox because Windows denied sandboxed registration. They are temporary and released by the tests. Dispatch uses injected `WM_HOTKEY`; physical keys and third-party application interaction were not manually tested. Startup tests use an isolated INI substitute, not the user's real Run key.

The final **Debug and Release builds passed**. Release CTest passed, and the portable-runtime smoke test passed with **0 QML warnings**, with development DLL/plugin/import search paths removed. The package includes Qt/MinGW runtime libraries and documentation.

Artifacts: `out/portable-complete/ScreenshotHotkeyManager.exe`, `out/portable-complete.zip`, `out/portable-complete/build-manifest.json`, and `out/portable-complete/smoke-result.json`.

Final local measurements:

| Measurement | Result |
| --- | --- |
| Release gallery metadata, 2,000 tiny PNGs | 499 ms; 39 event-loop timer ticks |
| Release test process working set | 69.2 MiB; 94.0 MiB peak |
| Fresh packaged application working set | 80.1 MiB |
| Fresh packaged application idle CPU sample | 15.625 ms CPU over 2,000 ms wall sample, about 0.8% of one core |

These are offscreen Windows measurements with software rendering and synthetic images. Short samples are subject to Windows CPU-clock granularity; native GPU/DPI configurations and large source images can differ.

Offscreen dark/light settings renders were inspected. This does not establish native GPU, DPI, real tray, sign-in, Windows 10, external association, or live Windows clipboard-tool behavior. Those checks remain in the README's manual checklist.

## UI corrections — 2026-10-15

Startup now centers the complete window frame in the available area of the monitor under the pointer, with primary-screen fallback. The target geometry is retained during native creation so that Qt's screen/DPI changes cannot select another monitor. Reopening preserves the user's moved position. Actions scope and step-type dropdowns size to their longest text, and the step picker has its own row so labels fit at the minimum window width.

Debug and Release builds passed. The Debug editor/shell/UI checks reported **7 passes**; the complete Release suite reported **36 passes, 0 failures, 0 skips**. A separate run using Qt's actual Windows platform reported **5 passes**, covering monitor selection, frame centering with a small physical-pixel rounding tolerance, reopening, and every popup label at 1040- and 1360-pixel logical window widths. This checks the host's current monitor configuration, not every possible DPI arrangement.

The refreshed portable executable passed its isolated runtime smoke check with **0 QML warnings** and development library paths removed. Latest artifacts: `out/portable-ui-fixes/ScreenshotHotkeyManager.exe` and `out/portable-ui-fixes.zip`. Test records: `build/ui-fixes-results.txt`, `build-release/ui-fixes-results.txt`, and `build-release/native-ui-fixes-results.txt`.

## SmallDTL name and build — 2026-10-15

The application title, Qt display name, sidebar, tray labels, notifications, executable name, and packaging now use SmallDTL. Windows version resources identify the product and file description as SmallDTL and the original filename as `SmallDTL.exe`. The previous internal application/data identity remains in use for compatibility; saved settings and profiles require no migration.

Both Debug and Release builds completed. Release CTest passed the existing **36-check** suite. The isolated packaged-runtime smoke test loaded successfully with **0 QML warnings** and recorded `applicationDisplayName: SmallDTL` and `windowTitle: SmallDTL`. Windows file metadata was checked on the packaged executable.

Latest artifacts are `out/SmallDTL-portable/SmallDTL.exe` and `out/SmallDTL-portable.zip`. Build/test evidence is recorded in `out/SmallDTL-portable/build-manifest.json`, `out/SmallDTL-portable/smoke-result.json`, and `build-release/Testing/Temporary/LastTest.log`.

## Hotkeys scope menu correction — 2026-10-15

The earlier dropdown correction missed the separate scope selector on Hotkeys. That selector now measures its widest option and reserves Material text-field padding inside the control. Actions uses the same padding allowance. This fixes the selected label as well as the open popup.

The dropdown regression now checks every selected label in the Actions scope, step-type, and Hotkeys scope controls, plus every popup option, at minimum and default window widths. Offscreen and actual Windows runs each reported **4 passes, 0 failures**, with no QML warnings. The native Hotkeys screenshots were inspected with `Application-wide` selected and the popup open.

Latest corrected portable package: `out/SmallDTL-menu-fix.zip`, containing `SmallDTL.exe`. Targeted evidence: `build-release/scope-labels-results.txt` and `build-release/native-scope-labels-results.txt`.

## SQLite screenshot records and tags — 2026-10-15

Screenshot membership now uses a versioned SQLite database. The legacy index is retained unchanged and imported restart-safely; tags, associations, stable IDs, tag filtering, multi-selection, and transactional mixed-state bulk tagging are covered by new tests. Settings and profile JSON storage remain unchanged.

The Release build passed. The test executable reported **38 passes, 0 failures, 1 skip** after the native hotkey registration check detected that the current restricted execution environment denied Windows registration. New migration, restart, metadata, persistence, uniqueness, rename identity, foreign-key cascade, mixed-state, rollback, and file-preservation checks passed. The portable package includes `sqldrivers/qsqlite.dll`; its isolated smoke test verifies database initialization with development search paths removed.

Latest package: `out/SmallDTL-portable-20261015-162200.zip`. The archive contains `SmallDTL-portable-20261015-162200/sqldrivers/qsqlite.dll`; the build manifest and smoke result record successful tests, database startup, and **0 QML warnings**. The same suite was run directly after packaging and reported **38 passed, 1 skipped**.

## Screenshot crop and drawing editor — 2026-10-15

The Screenshots gallery can open a bounded-preview editor from a card or screenshot preview. It supports rectangular crops with free, 1:1, 4:3, and 16:9 ratios; freehand strokes with color and image-pixel width; undo/redo/reset; and zoom/pan. Pointer mapping and exported crop/stroke coordinates use source pixels. Save as copy is the default, registers an `edited-copy` record with a foreign-key source screenshot ID, and can copy the source tags transactionally. The original file remains intact. Closing with unsaved crop or drawing changes prompts before discard. Full-resolution decode, crop, drawing, and PNG encoding run on a worker and retain the existing image limits and no-replace save path.

The Release build completed. The direct Qt Test run reported **45 passes, 0 failures, 1 skip**; CTest passed. New checks cover coordinate mapping at multiple display scales, aspect-ratio crop bounds, crop/draw pixel output and DPI metadata, v2-to-v3 database migration, persisted source/tag links, successful copy export and rename identity, failed decode cleanup, undo/redo, the unsaved-edit prompt, and opening the editor through the gallery. An offscreen 1200×850 gallery-to-editor capture was inspected at `build-release/editor-visual.png`; the editor loaded with no QML warnings.

The hotkey registration test was skipped because this restricted environment denied native Windows registration. The visual check used offscreen Qt rendering and the current display; it does not replace manual validation with physical Windows DPI settings or tablet input.
