# Configuration decisions

The displayed application name and executable are SmallDTL and `SmallDTL.exe`. The original `SmallDTL/ScreenshotHotkeyManager` data identity, single-instance identity, and startup registry value name remain stable for compatibility. Existing configured screenshot destinations are retained; fresh settings use `Pictures/SmallDTL`.

## Atomic ownership

Profile schema 3 stores sequences, hotkey assignments, and stable references to saved window-layout shortcuts together. Candidate edits are validated and atomically committed before models publish them, avoiding half-saved cross-file references. Reserved `global` scope is excluded from user profile activation/deletion.

Profile fields: `schemaVersion`, `id`, `name`, `description`, `icon`, `screenshotDirectory`, `activationKey`, `activationModifiers`, `isDefault`, `hotkeys`, `sequences`. Hotkey assignments may include `layoutId`; action steps may include `layoutId` for the `applyWindowLayout` step. Existing structured hotkey/sequence/step fields are retained. Duplication/import regenerate IDs and remap sequence references and self-targeting switches. A copied/imported profile does not inherit layout shortcuts that point to layouts owned by another profile. Import never executes actions.

Schema 1 profiles/shared actions migrate on startup. Each profile receives independent copies. Timestamped `.pre-sequences-*` profile backups and original actions are retained. Invalid sequence references become disabled, cleared bindings for repair. Disabled unassigned sequence bindings are valid placeholders; they cannot register or execute before receiving a valid target. Unsupported newer schemas are preserved.

## Settings

Schema 1 retains existing directories, active profile, close-to-tray, and start-minimized fields. Optional new fields: `startWithWindows`, `minimizeToTray`, `notificationsEnabled`, `filenamePattern`, `appearance`, `accentColor`, `automaticClipboardSavingEnabled`, `automaticClipboardSaveTagId`, `clipboardHistoryShortcutKey`, `clipboardHistoryShortcutModifiers`, `clipboardHistoryRetentionDays`, `clipboardHistoryRetentionCount`, `clipboardHistoryStorageLimitMB`, `fileShelfShortcutKey`, and `fileShelfShortcutModifiers`. Automatic screenshot saving is opt-in and off by default. Both tray pause controls affect the current run without changing persisted preferences. The selected automatic-save tag is a stable screenshot-database tag ID; deleting that tag clears the preference. Unknown valid settings fields survive edits; supported malformed files are backed up before repair.

Startup uses a quoted executable path in the current user's Run key, without elevation. Tests substitute a temporary INI file. State publishes only after the JSON commit; a failed startup-preference commit restores the previous registration preference.

## Hotkeys and execution

Thread-owned Windows registrations use `MOD_NOREPEAT`. Reconciliation retains unchanged global/activation bindings and removes only obsolete ones. Recording releases registrations temporarily and ends on focus/visibility/lifetime changes. Selection, saved definitions, and actual registration status remain distinct.

The runner snapshots steps and uses queued callbacks and a wait timer. Generation tokens discard cancelled callbacks. Save results match exact request paths; unrelated writes cannot advance a sequence. Launching uses explicit executable paths/parsed arguments, not shell interpretation.

## Window arrangements

Window definitions are stored atomically in `window-layouts.json`; they reference a profile ID but do not modify screenshot/settings storage. A layout matches open top-level windows by case-insensitive normalized executable path. An optional title-contains string narrows the match. The preview identifies missing and ambiguous rules; applying a saved layout requires explicit resolution of each ambiguous rule. No missing application is launched.

Win32 enumeration runs on a worker. Placement requests use asynchronous `SetWindowPos` flags so unresponsive external windows do not hold up the UI. Monitor `rcWork` rectangles exclude taskbars, retain negative virtual-screen coordinates, and are normalized in the saved layout so changed resolutions recalculate placement. The executable manifest opts into Per-Monitor-V2 DPI awareness so native window and monitor coordinates use the same physical pixel space. If a saved display device is disconnected, the primary monitor work area is used and reported. “Restore previous arrangement” holds the last native rectangles in memory for the current run; it is not a cross-restart history.

Window layout actions run only when a user applies a layout, presses a configured shortcut, or starts an action sequence containing the step. Profile activation never applies layouts automatically. Inaccessible processes may not appear in the eligible list. Windows protected by higher integrity levels, special shell/virtual windows, or windows without a queryable executable identity cannot be matched.

## Screenshots

Resolution is session override → active profile → general default. PNGs stage in the chosen folder and commit without replacement. Failure cleans staging data; originals are never moved/deleted. Dispatched writes are completed/indexed at shutdown.

Automatic clipboard saving is opt-in. The shared clipboard service emits immutable image, text, and file-reference snapshots for downstream consumers. Automatic screenshot saves accept only external images; history records all three supported content kinds unless its separate tray pause is active. The screenshot destination, active profile ID, and selected tag are captured when an external image event is accepted. Screenshot content fingerprints are calculated off the UI thread and deduplicated in memory for the current run. SmallDTL clipboard writes are marked as app-owned, and matching delayed notifications are filtered, preventing history and screenshot save loops. Explicit manual screenshot saves bypass automatic fingerprint deduplication. Pending fingerprint and file-write work is drained at shutdown.

Clipboard-history metadata shares the versioned SQLite database but remains separate from gallery screenshot rows. Text and file URLs live in history records; image payloads are atomically stored under the application-managed `clipboard-history/` directory and never in the screenshot gallery. Images are fingerprinted and encoded on background workers, with two active writes and a bounded queue under the existing 64-megapixel/256-MiB and 512-MiB queued-data limits. Duplicate notifications reuse the existing ID and payload. The global popup shortcut defaults to Ctrl+Shift+V; Windows registration status and conflicts are reported in Settings. Restore copies content to the clipboard, without simulating keystrokes or pasting. File references are not copied; missing local paths are shown and cannot be restored.

History retention defaults to 30 days, 500 entries, and 512 MiB of stored image payloads. Settings persist in the existing JSON file. Ordinary retention removes expired/old unpinned rows and their history-owned payloads; pinned rows remain even if they exceed limits. Explicit Clear History also removes pinned rows. Payload cleanup requires a generated UUID filename in the app-owned history directory. File references and screenshot-gallery files are never removed by history cleanup. Saving a history image to the gallery creates a separate screenshot copy and can assign a selected screenshot tag.

Screenshot records and tags live in `screenshots.db`, with versioned migrations, an owning-thread SQLite connection, and foreign keys enabled. Schema records have stable UUIDs, paths, UTC timestamps when available, dimensions, byte size, a source label, and an optional source screenshot ID. Edited copies keep that source link and may copy its tags in the same registration transaction. Source links clear if their parent record is removed. Tags are unique without case sensitivity; screenshot/tag pairs are unique. Rename changes the record path and retains its ID and assignments. Bulk assignment and removal use a single transaction and roll back together on failure. Removing a tag or gallery membership leaves image files untouched.

On first launch with a legacy `screenshot-index.json`, all valid paths (including paths to missing images) are imported in one transaction with a migration marker. The source JSON stays byte-for-byte intact. An import error is reported and the import is retried on a later launch. Settings and profiles remain in their existing JSON files.

The gallery supports tag filtering, checkbox and Ctrl/Shift selection, Select All and clear selection. Bulk tag controls show unchecked, checked, and mixed states.

## QR codes

QR generation and decoding are local and use the statically linked ZXing-C++ v3.1.1 library (Apache-2.0); the portable bundle includes the upstream license in `licenses/`. The QR page accepts text or URLs, with configurable module size, quiet-zone margin, and L/M/Q/H error correction. It decodes clipboard images, gallery screenshots, selected files, and dropped files, and lists multiple codes for review. Decoded data is displayed as text only; the app does not open URLs or execute content. Copying a generated code marks the clipboard write as app-owned, so it cannot feed automatic screenshot saving or clipboard history.

Generation, file decoding, and gallery image decoding run on worker threads and obey the 64-megapixel/256-MiB image limits. Saving a generated image to the gallery is explicit and supports an optional existing tag. The `Generate QR` sequence action writes the generated image to the clipboard through the shared ownership-aware service; `Decode QR image` reads a configured local image path and reports decoded text in sequence status. Neither action contacts a remote service.

Database schema version 6 stores `capture_profile_id` without coupling SQLite to profile JSON files, adds independently fingerprinted clipboard-history records, and stores file-shelf references. Automatic clipboard screenshot records retain the profile ID captured when the clipboard event was accepted; existing and manual records use an empty value when no profile snapshot applies.

## File shelf

The shelf stores normalized source paths and metadata in SQLite. It never copies source contents into application storage when a reference is added. Path keys use Unicode case folding to avoid case-only duplicates while retaining the display path. Missing references remain in the model until removed. Removing one reference or clearing the shelf changes only database rows.

The default configurable global panel shortcut is Ctrl+Shift+F. Windows external drags use `QMimeData::urls`, exposed by Qt as native local-file transfer data; the drag advertises copy semantics. Copy/Move operations to a selected folder run off the UI thread, stage copied files/folders before committing, and report per-item failures. Destination collisions are shown before execution with skip, rename, or explicit replace choices. A successful Move removes the source only after the destination is committed, then updates the existing shelf row so its stable ID follows the moved item. Replacing a directory stages the existing destination for recovery before committing the replacement.

The editor opens from a gallery card or preview. It crops rectangular regions, optionally locked to free, square, 4:3, or 16:9 ratios, and adds freehand strokes with selectable color and image-pixel width. Undo, redo, reset, zoom, and pan operate on editor state. Pointer positions map from the displayed image rectangle to source-image pixels, so exports remain aligned at different zoom and DPI scales. The editor preview and drawing overlay are bounded in size; full-resolution decoding, crop, drawing, and PNG encoding run on a worker. Saving creates a collision-safe copy in the active screenshot destination, registers it with its source ID, and offers to inherit source tags. The original image is retained. Closing with unsaved crop or drawing changes asks before discarding.

Limits: 64 megapixels/256 MiB per image, two pending saves, 512 MiB queued image data, 256 MiB decoding allocation. Previews/thumbnails request bounded sizes. Metadata work is asynchronous, refreshes coalesce, and stale results are ignored. A recent proxy exposes four items. Visible thumbnails cache by source fingerprint and requested dimensions. On a cache miss, PNG decoding may need transient source-image memory within the limit.

All data/cache operations are local. There is no background polling, application monitoring, cloud service, or automatic profile switching.
