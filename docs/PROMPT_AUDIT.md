# Project prompt audit and completion record

Updated 2026-10-14. The defects and omitted implementation features from the initial review are completed. Verification boundaries are listed separately.

## Architecture

The application remains C++20, Qt 6/QML/Quick Controls, WinAPI, and CMake. The controller connects separate settings, profile, screenshot, hotkey, and action managers. No foreground-app monitoring, automatic profile switching, keyboard hooks, full-disk indexing, cloud account, uploading, shell-text executor, or automatic elevation was added.

Each profile owns its sequences and assignments in an atomic schema 2 JSON file. The reserved `global` scope owns application-wide assignments and sequences. The runner snapshots its steps, uses event-loop scheduling, awaits screenshot results, and ignores callbacks from cancelled runs.

## Resolution of remaining defects and omissions

| Initial finding | Completed behavior | Verification |
| --- | --- | --- |
| Shared sequences and dangling references | Profiles embed sequences. Duplication clones sequence/step/assignment IDs and remaps references. Referenced sequences cannot be deleted until their shortcuts are removed. Legacy shared actions migrate independently with original files and profile backups retained. Missing legacy references become disabled, unassigned sequence bindings for review. | Serialization, independent editing/deletion, duplication, and migration tests |
| Failed settings saves and lost startup errors | Candidate settings publish only after atomic commit. Failures reach the UI. Construction-time errors are retained and delivered after initialization. Rejected editors restore saved values. | Rollback, future-schema preservation, startup delivery, and failed-settings-switch UI tests |
| Unsafe IDs and malformed records | IDs, filenames, schemas, scalar types, limits, duplicates, sequence references, and absolute destinations are validated. Invalid profiles remain intact; newer settings/global schemas are protected. | Unsafe IDs, duplicates, mismatched filenames, corrupt profiles, and newer-schema tests |
| Screenshot overwrite race | A same-directory staging file is closed before a Windows no-replace commit. Failures clean only staging data. | Existing-file preservation and actual PNG-save tests |
| Unnecessary global re-registration | Reconciliation preserves unchanged native registrations and stable IDs across profile changes. | Actual Windows registration, scope, stable-ID, pause/restore, injected dispatch, and OS conflict tests |
| Ctrl+V intercepting dialog text | Screenshot paste excludes dialogs and focused text editors. | Rename-dialog keyboard paste test with no screenshot dispatched |
| Recorder lifetime and modified keys | Recording ends on focus loss, hiding, deactivation, and destruction. Plain Escape/Tab cancel and plain Delete/Backspace clear; modified versions record normally. | Modified Delete, focus loss, and hiding checks |
| Keyboard access and Dashboard counts | Sidebar, profile/sequence cards, gallery, and recent items support keyboard interaction and visible focus. Preview has an accessible button. Dashboard counts successful registrations. | Editor, recorder, paste, shell, and native-count checks |
| Gallery/thumbnail performance | Metadata runs on a worker, refreshes coalesce, stale results are discarded, a recent proxy exposes four entries, and visible thumbnails load asynchronously with a disk cache. | Cache reuse, missing entries, recent-limit checks, and 2,000-image measurements |
| Application-wide action hotkeys | Actions and Hotkeys have profile/global scope pickers. Global actions stay registered across switches, with cross-scope conflict checks. | Controller validation and native scope tests |
| Start with Windows | Per-user Run registration stores a quoted executable path. Errors are reported; failed settings persistence restores the previous registration preference. | Isolated registration persistence test; real sign-in is manual |
| Minimize-to-tray | A persisted option hides a minimized window when a tray is available; reopening restores a normal window. | Build/source verification; physical tray use is manual |
| Import/export | Self-contained schema 2 exports include sequences and metadata. Import regenerates IDs, clears activation shortcuts, disables bindings, and disables unavailable switch targets for review. | Round trip, remapped self-targets, disabled bindings, and metadata checks |
| Filename preferences | Validated timestamp/date/time templates retain automatic PNG extensions and unique suffixes. | Preference and collision tests |
| Appearance/accent | Dark, light, system appearance, and opaque accents persist and update a central palette and Qt Material. | Preference tests, shell theme changes, and inspected dark/light renders |
| Notifications | Nonessential tray notifications can be disabled; errors remain visible. Profile feedback preserves registration failures. | Source/build and error checks; physical toast behavior is manual |
| Profile descriptions/icons | Metadata can be edited, persisted, copied/imported, and displayed in profile cards. | Metadata and import/export checks |
| Image resource policy | 64 megapixels/256 MiB per image, two pending saves, 512 MiB queued source data, and 256 MiB decoding allocation. Previews/thumbnails are bounded, and shutdown completes/indexes dispatched writes. | Queue limits, thumbnail sizing, save completion/failure, and shutdown checks |
| Recovery, deployment, and measurements | Recovery tests and a packaging script build/test Release, deploy Qt/MinGW, and smoke-test the packaged executable without development search paths before ZIP creation. | Results in VERIFICATION.md |

## Original sequence-editor failure

Four C++ query methods lacked `Q_INVOKABLE`, the step delegate lacked its required index, and an empty sequence's scroll content could retain zero width. All are fixed. Step synchronization preserves delegates while editing. Selection, rename, adding/editing/reordering/disabling/removing steps, deletion, and creation are exercised by the real QML editor regression.

## Verification boundaries

Tests use temporary data roots and an offscreen Qt platform. Startup registration uses an isolated INI substitute; tests do not modify the user's real Run key. Native hotkey tests call actual Windows APIs and inject `WM_HOTKEY` through the thread queue. They ran outside the Codex sandbox because it denies native registration. Injected dispatch is not a claim of physical-keyboard or third-party-app testing.

The complete QML shell loads all six pages and changes themes. The portable smoke test starts the distributed executable with a fresh temporary data root, removes development DLL/plugin/import paths, and records QML warnings and a short idle CPU/working-set sample. Gallery measurements use 2,000 tiny PNGs and event-loop ticks. These are local offscreen measurements, not universal native GPU/DPI guarantees.

Actual results are recorded in [VERIFICATION.md](VERIFICATION.md). Tray clicks, real sign-in, physical shortcuts while other apps are focused, real Windows clipboard tools, external associations, Windows 10, and DPI variations retain a manual checklist in the README. These are implemented features with explicit verification limits, not unfinished placeholders.

## Subsequent UI corrections — 2026-10-15

The first window presentation centers its complete native frame on the pointer's monitor, retaining that monitor's available geometry through native creation and DPI changes. Later presentations preserve the user's chosen position. Actions dropdowns measure the widest option, and the step-type picker occupies a separate row so all option labels fit at the minimum window width. Native Windows regression checks and updated Debug/Release results are recorded in VERIFICATION.md.

## Application name — 2026-10-15

The product is now **SmallDTL** in the window title, sidebar, tray tooltip, notifications, Windows executable metadata, and portable-build manifest. Debug and Release outputs are `SmallDTL.exe`. Existing data and single-instance identities are retained so saved profiles, sequences, settings, and configured destinations remain accessible. New installations default to `Pictures/SmallDTL`.

## Hotkeys scope menu correction — 2026-10-15

The separate Hotkeys scope selector was omitted from the earlier Actions-only fix. Both scope controls now size for their longest text, including Material's internal text-field padding. Regression coverage checks selected labels and popup options on both pages, and native Windows screenshots confirm the full `Application-wide` label is visible.
