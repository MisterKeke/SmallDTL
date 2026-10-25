# Verification record

## Review fixes, bulk tags, annotations, and CI paths — 2026-10-25

The local Windows Release build with Qt 6.11.0 passed. The directly logged Qt Test run reported **93 passed, 0 failed, 2 skipped**. Windows denied temporary junction creation and native global-hotkey registration in this sandbox; these skips do not establish physical desktop verification.

New regression coverage verifies:

- A rejected profile edit exceeding the 4 MiB serialized limit keeps the previous file bytes and in-memory arguments, and that profile still loads after restart.
- Schema-6-to-7 migration merges Unicode case-only screenshot paths, unions tag assignments, redirects edited-copy links, and preserves lookup and restoration identities across restart.
- A Move rejects sources held open for editing. A file created inside a source folder during a 128 MiB transfer survives, and the operation reports failure while preserving the source. Existing successful moves, conflict handling, and cancellation checks also pass.
- File Shelf and clipboard-history shortcut setters reject Ctrl+V; the native registration path also rejects older saved or imported Ctrl+V bindings. Missing layout IDs fail sequence validation, and shortcut/sequence references prevent layout deletion.
- Arrow, rectangle, ellipse, highlight, text, and opaque redaction output, including crop coordinates, original-file preservation, and alpha behavior. The real QML editor creates all six annotations through pointer events, exercises undo/redo, and renders without QML warnings.
- The real bulk-tag popup opens at its button, displays mixed state, assigns a tag to both selected screenshots, updates its check state, and removes the assignments on a subsequent click, without QML warnings.

Visual inspection used `build-release/editor-visual.png` and `build-release/bulk-tag-fixed.png`. Full test details are in `build-release/fixes-full.txt`.

The packaging argument regression exercised `tools/package-windows.ps1` using a temporary compiler path containing a CI-style backslash-a component. Its compiler cache arguments used typed CMake paths with forward slashes, and the generated `CMakeRCCompiler.cmake` parsed successfully with CMake. The local CMake resource-compiler template directly embeds its compiler path in a quoted CMake string, explaining why an unnormalized native path triggered the reported escape error. This local check does not establish that the hosted GitHub workflow has rerun.

The final portable packaging run rebuilt the application and tests, passed CTest, deployed Qt and the SQLite driver, and passed isolated startup with **0 QML warnings**. Artifacts: `out/SmallDTL-fixes-final-20261025/SmallDTL.exe` and `out/SmallDTL-fixes-final-20261025.zip`. The package records its results in `build-manifest.json` and `smoke-result.json`; the full packaging log is `build-release/fixes-package-final.log`.

## Storage, privacy, transfer, and maintainability fixes — 2026-10-18

Implemented the review's cleanup error reporting/retry and orphan reconciliation, storage usage/compaction controls, persisted capture enablement and application exclusions, supported sensitive clipboard markers, clear-on-exit behavior, confirmed clearing with optional pins, five-minute idle retention, asynchronous bounded history queries and filesystem checks, incremental history rows, transfer progress/cancellation and shutdown cancellation, and bounded/expiring thumbnail storage. The shared SQLite service is now `ApplicationDatabase`, its domain SQL is split, tests are organized by subsystem, and Windows CI builds/tests/packages with an isolated smoke check. Existing data identity, database filename, and schema remain unchanged. Behavior and limitations are detailed in `MAINTENANCE.md`.

The Release application and test targets built with Qt 6.11.0/MinGW. The full offscreen Qt Test run reported **85 passed, 0 failed, 2 skipped**. Skips were native Windows registration and temporary junction creation denied by the sandbox. Both focused native tests then passed outside the sandbox: **4 passed, 0 failed, 0 skipped**, including init/cleanup. The native registration fixture now assigns an isolated shelf shortcut because another application on this desktop owns the default. Its deliberately competing binding still verifies conflict reporting.

New checks cover persisted pause/exclusions, sensitive flags, invalidating in-flight captures when paused, clear-on-exit/restart, clearing search-hidden entries with/without pins, locked image deletion and retry, owned orphan cleanup, compaction preserving tags, paged text previews with full restoration, idle age cleanup, thumbnail byte/age eviction, cancellation during a 256-MiB replacement move, source/destination preservation, shutdown cancellation, and both clear confirmation dialogs. The final focused UI/privacy/cache run passed **5 checks, 0 failed**. The QML-engine warning count was zero; native registration denial messages in the restricted run were expected.

Rendered `build-maintenance/history-clear-confirmation.png`, `maintenance-settings.png`, and `maintenance-storage.png` were inspected. The clear dialog has an opaque body and readable pin/count/scope text; the storage panel's usage, cache cap, and maintenance buttons fit. These are offscreen checks on this host, not physical mixed-DPI validation.

Evidence: `build-maintenance/maintenance-tests.txt`, `maintenance-ui-checked.txt`, `native-maintenance-tests.txt`, and build logs. Portable packaging records its own CTest/deployment/startup outcomes in `build-manifest.json` and `smoke-result.json`. The new remote CI workflow has not been dispatched from this local task.

Verified portable artifacts: `out/SmallDTL-maintenance-20261018/SmallDTL.exe` and `out/SmallDTL-maintenance-20261018.zip`. Packaging reran Release CTest successfully (**1/1 test executable passed**) and its isolated startup check reported **loaded=true, 0 QML warnings** with development search paths removed. SQLite's driver and the QR dependency license are bundled.

Physical Explorer/third-party drops and key presses, real password-manager interoperability, cross-volume/network-share behavior, protected windows, and mixed-DPI/disconnected displays still require interactive hardware checks. `tools/verify-windows-interactive.ps1` records pass/fail/not-run and evidence for those checks; it was not run here. Normal filesystem calls can delay cancellation until the current call returns. Clear-on-exit stores data locally until cleanup and does not claim RAM-only storage or secure erasure.

## Minimized-window layout persistence and recovery — 2026-10-16

The previous build could save a minimized window's tiny rectangle at `(-32000,-32000)`, then reject its own normalized coordinates on restart. Saving now reads live restored bounds without restoring the window, retains whether a minimized window should return maximized, constrains placement to the work area, and validates serialized layouts before committing. Applying a placement sets restored bounds and show state in one asynchronous native operation, including when the target was minimized from maximized state.

Recognizable legacy minimized-window records are recovered only if the remaining file validates. Recovery keeps layout IDs, profile references, rules, and monitor nicknames, centers the affected placement, and preserves the original bytes in a uniquely named backup before the atomic write. Unrelated invalid data stays unchanged and write-protected.

Focused Release checks reported **7 passes, 0 failures, 0 skips**, including initialization/cleanup. They cover recovery and a second restart, byte-identical backups, retained monitor nicknames, rejection of unrelated invalid data, off-screen placement bounds, and saving/applying a real native helper window minimized both from normal and maximized states. The helper is the only window manipulated by these checks. Evidence: `build-release/layout-recovery-results.txt`. User configuration was inspected read-only during development; recovery happens when the updated application starts.

## Text layout and friendly monitor names — 2026-10-16

Clipboard History now uses a valid palette entry in the page and detached popup, and row heights follow their contents so buttons and timestamps remain inside their cards. QR input/output use inset, wrapped placeholders; narrow windows stack the Generate and Decode panels in a scrolling page instead of overlapping them. Windows display choices, window rows, and layout previews show reported model names, full display resolution, and primary status. Missing model names fall back to numbered labels; identical names are disambiguated. Optional nicknames persist separately from layout device identifiers in `window-layouts.json`.

The Release build and complete Release CTest run passed. Focused text/monitor checks reported **4 passes, 0 failures, 0 skips**, including initialization/cleanup. Text checks seed image and text history entries, verify action containment and palette colors, verify empty/focused/populated QR placeholders and non-overlapping panels, and capture dark/light renders at 1040×680 and 1360×850. Those renders were inspected. Monitor checks cover label fallback, nickname validation, restart persistence, and reset. The restricted display probe exposed a generic display rather than a hardware model, so it verifies fallback behavior; hardware-name availability depends on Windows exposing its active display configuration.

Evidence: `build-release/text-monitor-results.txt`, `build-release/history-text-*.png`, `build-release/qr-text-*.png`, and `build-release/Testing/Temporary/LastTest.log`. The portable package is produced with the existing deployment script, including its isolated runtime/database smoke check and third-party licenses.

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

## Automatic clipboard image saving — 2026-10-15

Automatic image saving is an opt-in setting (off by default), with a tray pause/resume action and an optional persisted screenshot tag. The shared clipboard service provides image snapshots and filters SmallDTL-owned clipboard writes. Automatic events capture the resolved destination and profile ID before asynchronous fingerprinting, use session-only SHA-256 deduplication, then reuse bounded collision-safe background PNG writes. Registered records store source `clipboard-auto`, the captured profile ID, and the chosen tag. Explicit manual saves do not consult the automatic fingerprint cache. No clipboard history was added.

The database is now schema version 4, adding the captured profile ID through a migration. The portable packaging workflow deployed `sqldrivers/qsqlite.dll` and passed its isolated packaged-runtime database smoke test. Verified package: `out/SmallDTL-automatic-clipboard-release/SmallDTL.exe` and `out/SmallDTL-automatic-clipboard-release.zip`.

The Release build passed. The Qt Test suite reported **49 passed, 0 failed, 1 skipped**; the only skip is the existing native hotkey registration test, which Windows denied in this restricted environment. New checks cover shared-service suppression of app-owned clipboard images, duplicate image notifications across pixel formats, manual saves despite automatic deduplication, persisted opt-in/tag preferences, pause/resume behavior, profile and destination snapshots across rapid changes, queue saturation, removed destinations, tag-registration failure reporting with file preservation, and shutdown completion. The v2 database migration fixture advances through the new profile-capture column and verifies stored profile IDs.

Automatic clipboard event flow and Settings QML were exercised with the offscreen test runner. External clipboard-tool interoperability, native tray interaction, and native Windows clipboard timing still need a physical Windows check. Deduplication is intentionally in-memory for a single application run; a later copy after restart can be saved again. Clipboard history remains out of scope.

## Clipboard history — 2026-10-15

Clipboard history adds schema 5 records to the existing SQLite service and keeps PNG payloads in the separate application-owned `clipboard-history/` directory. Text, images, and file URLs persist and deduplicate by fingerprint; restored entries are copied through the shared clipboard service and do not re-enter history or automatic screenshot saving. The page and detached compact popup support search, previews, pin/unpin, delete, clear, pause/resume, and image export to the screenshot gallery with an optional tag. File references remain at their source locations and missing local paths are shown and rejected during restore. Settings JSON stores the configurable global shortcut and age/count/image-payload retention limits. Ordinary cleanup preserves pins and deletes only UUID-named history payloads inside the owned directory.

Release CTest passed: **54 passed, 0 failed, 1 skipped**. The skip is the native Windows registration test when run sandboxed. Running that test outside the sandbox passed with **3 passed, 0 failed, 0 skipped**; it registered the clipboard-history shortcut and dispatched its `WM_HOTKEY` signal. The test also verified rejection of a duplicate shortcut. Other new checks cover schema-4-to-5 migration, text/file/image restart persistence, text/file/image deduplication, missing references, image payload ownership, history pause/resume, retention and pinned-item behavior, clipboard restoration without automatic-save loops, gallery export/tagging, and shortcut/retention preference persistence.

The final Release portable package passed its isolated runtime smoke test with development Qt paths removed, database initialization enabled, and **0 QML warnings**. `sqldrivers/qsqlite.dll` is bundled. Package artifacts: `out/SmallDTL-clipboard-history-verified-20261015/SmallDTL.exe` and `out/SmallDTL-clipboard-history-verified-20261015.zip`; build and smoke outcomes are recorded in the package manifest and `smoke-result.json`. The detached popup was opened while the main window was hidden and visually inspected in the offscreen render at `build-release/clipboard-history-popup.png`.

The native registration test uses Windows `RegisterHotKey` and injected `WM_HOTKEY` messages; it does not replace checking physical key presses against third-party applications. Real external clipboard-tool interoperability, native tray interaction, Windows DPI variations, and live clipboard ownership timing still need a manual Windows check. Clipboard history stores text directly in SQLite and limits clipboard image payloads to 64 megapixels/256 MiB, with two active payload writes and a bounded pending queue.

## File shelf — 2026-10-15

SQLite schema 6 adds persistent file-shelf references keyed by a Unicode case-folded normalized path. File and folder contents remain at their source paths when added. The shelf page and detached compact panel support add/drop, selection, open/location/path actions, removal, and clearing. Qt `QMimeData::urls` carries selected local paths as file-transfer data, with copy action. Copy and Move to folder run in a background worker, stage writes, preview destination conflicts with skip/rename/replace choices, and return per-item outcomes. Successful moves update the existing reference ID to the new path; removal and clearing only delete database rows.

The Release Qt Test run reported **59 passed, 0 failed, 1 skipped**. New checks cover schema-5-to-6 migration, Unicode path persistence, case-folded duplicate constraints, restart persistence and missing paths, source preservation after reference removal/clear, local file URL MIME data, selection behavior, batch filename conflicts, skip/rename/replace, recursive folder copy/move, stable IDs after moves, and partial failures. The one skip is native Windows shortcut registration under the restricted run; the focused unsandboxed test passed with **3 passed, 0 failed, 0 skipped**, including the file-shelf `WM_HOTKEY` dispatch.

The hidden-window compact shelf panel loaded in the offscreen Qt render with no QML warnings; the capture is `build-release/file-shelf-popup.png`. The portable Release build passed CTest and its isolated runtime smoke test with development Qt paths removed; `sqldrivers/qsqlite.dll` is present and startup reports **0 QML warnings**. Artifacts: `out/SmallDTL-file-shelf-final-20261015-r3/SmallDTL.exe` and `out/SmallDTL-file-shelf-final-20261015-r3.zip`.

External drag data was checked through Qt's local file URL MIME representation, but a physical drop into Explorer or a third-party Windows application was not exercised. Copying a folder that contains symbolic links reports an item failure rather than traversing the link. The folder-copy tests exercise ordinary nested files; filesystem permissions, network shares, and cross-volume Move behavior still need manual Windows validation.

## Local QR tools — 2026-10-15

The QR Codes page generates locally from text or URLs with pixel-per-module size, quiet-zone margin, and L/M/Q/H error-correction controls. It decodes clipboard, gallery, picked, and dropped images off the UI thread, shows each detected result in a selectable list, and keeps decoded data as reviewable text. Copy uses the shared app-owned clipboard marker; export is atomic; gallery saving is explicit, respects existing image/write limits, and registers a `qr-generated` screenshot with an optional tag. Sequence actions can generate to the clipboard and decode an image path.

ZXing-C++ v3.1.1 is fetched at a fixed tag and linked statically with only QR reader/writer formats enabled. It is licensed under Apache-2.0; the portable package includes the upstream license at `licenses/ZXing-C++-Apache-2.0.txt` and contains no ZXing DLL. Packaged smoke test: `out/SmallDTL-qr-local-verified-20261015/smoke-result.json` reports loaded=true and 0 QML warnings. The package also contains the Qt SQLite driver. UI capture: `build-release/qr-code-page.png`.

The Release Qt Test run reported **63 passed, 0 failed, 1 skipped**; the skipped check is native Windows global-hotkey registration, which Windows denies in this restricted run. QR checks cover Unicode round trips at all four error-correction levels, rotated input, two codes in one image, blank/invalid input, file decoding, app-owned clipboard loop prevention, unavailable destination and invalid-tag save failures, gallery metadata/tag registration, and generate/decode sequence persistence and execution. CTest passed after the final test change.

Image decoding is local and bounded by the existing 64-megapixel/256-MiB limits. Manual drag of an image from Explorer onto the QR drop target and scanning a generated code with a physical reader were not separately exercised; the automated checks cover the local file-decoding pipeline and generated image output.

ZXing-C++ v3.1.1 marks `MultiFormatWriter` as deprecated in favor of its Zint-backed creator API. This build keeps the compatibility writer enabled because it directly supports the requested error-correction and arbitrary quiet-zone module settings without fetching/enabling the optional Zint submodule. The resulting compiler warnings are non-fatal; revisit this choice if a future ZXing writer exposes equivalent margin controls.

## Manual window arrangements — 2026-10-15

Window Arrangements lists eligible open top-level windows and supports checkbox, Ctrl-click, Shift-click, and Select All selection. Direct placement includes halves, quadrants, center, maximize, and moving the selected windows to a monitor. Saved layouts persist profile association, executable path identity, optional title-contains rules, monitor device, and normalized placement. The preview shows matched, missing, and ambiguous windows; ambiguous candidates can be resolved before applying. Layouts can also be used by configured hotkeys and user-started action sequences. These paths do not launch applications or apply on profile activation. Restoring the previous arrangement is available during the current application run.

The Release build completed and Release CTest passed. The direct Qt Test run reported **68 passed, 0 failed, 1 skipped**. New tests cover normalized geometry on negative-origin work areas and changed resolutions, executable matching and title disambiguation, layout serialization/restart persistence, shortcut/sequence layout references, and native monitor enumeration. The skipped test is native global-hotkey registration; Windows denied registration in this restricted run. Offscreen UI rendering was inspected at `build-release/window-arrangements-page.png`.

The build uses existing Win32 system APIs and adds no third-party runtime dependency. The executable resource embeds a Per-Monitor-V2 manifest; native monitor enumeration passed on this Windows host. An independent visible third-party window was not moved during verification, and the portable archive was not rebuilt for this stage. Actual physical key delivery, protected/elevated windows, mixed-DPI multi-monitor placement, disconnected-monitor fallback, and unresponsive application behavior still need interactive Windows validation. Inaccessible processes and special shell windows may not appear; previous positions are not persisted across application restarts.

## Latest review fixes — 2026-10-15

The six review findings were corrected:

- File transfers resolve existing paths through Windows filesystem aliases, compare final paths/file IDs, and recheck folder replacements and source deletion. The regression creates a junction only under a temporary test directory and verifies a Replace/Move through an alias cannot remove the source. It passed in an outside-sandbox run; this sandbox denies junction reparse-point creation (Win32 error 5), so the full in-sandbox run marks that one case skipped.
- Retention accounting excludes unpinned rows already selected by the age rule before enforcing storage and count limits. Tests cover an oversized expired payload beside a fresh one and combined storage/count cleanup.
- File Shelf and QR drop handlers pass QML URL values to a C++ local-file URL converter. Tests exercise Unicode, spaces, percent characters, UNC URL conversion, remote-URL rejection, and the actual QML drop functions with a file and a decodable QR image.
- Window arrangement ListView delegates now take the view's width explicitly. The populated-window QML check asserts positive width and view/delegate alignment for open windows, saved layouts, and the refreshed preview, with zero QML engine warnings. Capture: `build-release/window-arrangements-populated-review.png`.
- Window preview and apply are serialized asynchronous jobs that refresh monitor/window enumeration first. Placement revalidates HWND, process ID, executable identity, and title rule. The sequence runner awaits the apply result and cancels pending requests. A native helper launched by a sequence was absent during the initial refresh, then was matched and positioned at its normalized work-area bounds; cancellation of a second request left its bounds unchanged.
- Clear History increments a capture generation, drops queued images, and deletes late payloads from active pre-clear writes. A regression queued three images, cleared, accepted a new image, and verified only the post-clear image and payload remained.

Release CTest passed **1/1 tests**. The direct Release Qt Test run reported **72 passed, 0 failed, 2 skipped**: sandbox-blocked junction creation and native global-hotkey registration. The junction alias test separately passed outside the sandbox (**3 passed, 0 failed, 0 skipped**, including Qt Test init/cleanup). The populated QML checks ran against the real Windows native test-window helper with offscreen rendering. The `fullShellPagesAndPreferences` check exercised both QML drop handlers and reported zero QML-engine warnings.

The portable package was rebuilt to `out/SmallDTL-review-fixes-final-20261015/SmallDTL.exe` and `out/SmallDTL-review-fixes-final-20261015.zip`. Its isolated smoke run passed with development Qt paths removed; the package contains `sqldrivers/qsqlite.dll` and the ZXing-C++ license. This verifies portable deployment and startup, not live Explorer drag-and-drop or physical shortcut delivery.
