# Configuration decisions

The displayed application name and executable are SmallDTL and `SmallDTL.exe`. The original `SmallDTL/ScreenshotHotkeyManager` data identity, single-instance identity, and startup registry value name remain stable for compatibility. Existing configured screenshot destinations are retained; fresh settings use `Pictures/SmallDTL`.

## Atomic ownership

Profile schema 2 stores sequences and assignments together. Candidate edits are validated and atomically committed before models publish them, avoiding half-saved cross-file references. Reserved `global` scope is excluded from user profile activation/deletion.

Profile fields: `schemaVersion`, `id`, `name`, `description`, `icon`, `screenshotDirectory`, `activationKey`, `activationModifiers`, `isDefault`, `hotkeys`, `sequences`. Existing structured hotkey/sequence/step fields are retained. Duplication/import regenerate IDs and remap sequence references and self-targeting switches. Import never executes actions.

Schema 1 profiles/shared actions migrate on startup. Each profile receives independent copies. Timestamped `.pre-sequences-*` profile backups and original actions are retained. Invalid sequence references become disabled, cleared bindings for repair. Disabled unassigned sequence bindings are valid placeholders; they cannot register or execute before receiving a valid target. Unsupported newer schemas are preserved.

## Settings

Schema 1 retains existing directories, active profile, close-to-tray, and start-minimized fields. Optional new fields: `startWithWindows`, `minimizeToTray`, `notificationsEnabled`, `filenamePattern`, `appearance`, `accentColor`. Unknown valid settings fields survive edits; supported malformed files are backed up before repair.

Startup uses a quoted executable path in the current user's Run key, without elevation. Tests substitute a temporary INI file. State publishes only after the JSON commit; a failed startup-preference commit restores the previous registration preference.

## Hotkeys and execution

Thread-owned Windows registrations use `MOD_NOREPEAT`. Reconciliation retains unchanged global/activation bindings and removes only obsolete ones. Recording releases registrations temporarily and ends on focus/visibility/lifetime changes. Selection, saved definitions, and actual registration status remain distinct.

The runner snapshots steps and uses queued callbacks and a wait timer. Generation tokens discard cancelled callbacks. Save results match exact request paths; unrelated writes cannot advance a sequence. Launching uses explicit executable paths/parsed arguments, not shell interpretation.

## Screenshots

Resolution is session override → active profile → general default. PNGs stage in the chosen folder and commit without replacement. Failure cleans staging data; originals are never moved/deleted. Dispatched writes are completed/indexed at shutdown.

Screenshot records and tags live in `screenshots.db`, with versioned migrations, an owning-thread SQLite connection, and foreign keys enabled. Schema records have stable UUIDs, paths, UTC timestamps when available, dimensions, byte size, a source label, and an optional source screenshot ID. Edited copies keep that source link and may copy its tags in the same registration transaction. Source links clear if their parent record is removed. Tags are unique without case sensitivity; screenshot/tag pairs are unique. Rename changes the record path and retains its ID and assignments. Bulk assignment and removal use a single transaction and roll back together on failure. Removing a tag or gallery membership leaves image files untouched.

On first launch with a legacy `screenshot-index.json`, all valid paths (including paths to missing images) are imported in one transaction with a migration marker. The source JSON stays byte-for-byte intact. An import error is reported and the import is retried on a later launch. Settings and profiles remain in their existing JSON files.

The gallery supports tag filtering, checkbox and Ctrl/Shift selection, Select All and clear selection. Bulk tag controls show unchecked, checked, and mixed states.

The editor opens from a gallery card or preview. It crops rectangular regions, optionally locked to free, square, 4:3, or 16:9 ratios, and adds freehand strokes with selectable color and image-pixel width. Undo, redo, reset, zoom, and pan operate on editor state. Pointer positions map from the displayed image rectangle to source-image pixels, so exports remain aligned at different zoom and DPI scales. The editor preview and drawing overlay are bounded in size; full-resolution decoding, crop, drawing, and PNG encoding run on a worker. Saving creates a collision-safe copy in the active screenshot destination, registers it with its source ID, and offers to inherit source tags. The original image is retained. Closing with unsaved crop or drawing changes asks before discarding.

Limits: 64 megapixels/256 MiB per image, two pending saves, 512 MiB queued image data, 256 MiB decoding allocation. Previews/thumbnails request bounded sizes. Metadata work is asynchronous, refreshes coalesce, and stale results are ignored. A recent proxy exposes four items. Visible thumbnails cache by source fingerprint and requested dimensions. On a cache miss, PNG decoding may need transient source-image memory within the limit.

All data/cache operations are local. There is no background polling, application monitoring, cloud service, or automatic profile switching.
