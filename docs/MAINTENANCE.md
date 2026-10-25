# Storage, privacy, transfers, and verification

These changes retain the application-data identity and `screenshots.db` filename. The current schema is 7, adding Unicode case-folded screenshot path keys and merging case-only duplicate records while preserving tags and source links. There is no data relocation. `ApplicationDatabase` is the shared storage service, with separate clipboard, shelf, and maintenance SQL files. File Shelf receives the database and clipboard directly; history uses ScreenshotManager only for explicit gallery export.

## Clipboard privacy

Capture enable/disable is persisted, including pause/resume from the page, popup, and tray. Disabling invalidates pending image captures. Application exclusions accept executable basenames or full paths, one per line, and compare case-insensitively with the Windows clipboard owner's process. An unidentified owner cannot be matched. These exclusions concern history; automatic screenshot saving has its own switch.

The clipboard boundary rejects sensitive markers before extracting content: Windows `ExcludeClipboardContentFromMonitorProcessing` and a zero DWORD `CanIncludeInClipboardHistory`, KDE's secret hint, and the concealed-content MIME marker. This also prevents automatic image capture of marked content. Windows formats follow [Microsoft's clipboard documentation](https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats#cloud-clipboard-and-clipboard-history-formats). Unmarked passwords cannot reliably be identified.

“Clear history on exit” removes all history, including pins, on normal shutdown and at next startup after an interrupted run. Data remains stored locally during the run. This is not memory-only storage, encryption, or secure erasure. Failed payload deletion is reported and retried by maintenance.

## Clearing and maintenance

Both clear buttons open the same confirmation component. It describes clearing across all search results, displays the affected count, and preserves pins unless “Also remove pinned entries” is selected.

Deleting an entry commits the database removal, then removes only its UUID-named owned image. Failure is reported. Startup and five-minute maintenance reconcile orphaned UUID PNGs and retry deletion. Maintenance protects stored references and pending image reservations, skips files created after its snapshot, and rejects linked/junction payload directories. It preserves arbitrary filenames, gallery images, and original referenced files.

Settings shows history-image, shared-database, and thumbnail bytes, plus cleanup failures. Refresh/retry, database compaction, and thumbnail clearing run on a worker. VACUUM reclaims database space while preserving other records. Ordinary deletion and compaction do not guarantee forensic erasure or removal from backups/Windows clipboard history.

Age/count/image-byte retention runs every five minutes while idle, as well as its existing triggers. Pins retain their exemption. Retention reads metadata without loading text or file-list payloads.

## Performance and transfers

History search is debounced and uses a worker-owned SQLite connection. The first page has 100 results; Load more adds 100. Previews hold at most 240 text characters and three file URLs, with the true file count retained. Restoration reads complete content by ID. Availability checks, including file references beyond the preview, run on the worker. Incremental model updates preserve existing rows; revision checks discard stale results.

Shelf availability/size checks update cached roles from a worker. Copy/move counts bytes on the worker and supports progress and cancellation between one-MiB chunks/directory entries. Uncommitted staging is removed. Cancellation after destination commit but before source removal preserves both copies and reports that outcome. Completed moves cannot be undone by cancelling later work. Shutdown requests cancellation, waits for the current filesystem operation, and applies completed reference updates once. An operating-system call can delay cancellation until it returns.

The thumbnail cap defaults to 128 MiB, configurable from 16 to 4096 MiB. Oldest owned hash-named PNGs are evicted above the cap; files older than 30 days expire. Other filenames are preserved and linked/junction cache directories are rejected. Atomic cache writes allow evicted images to be regenerated.

## Verification workflow

Tests are split by subsystem, with shared support and one Qt Test entry point. `MaintenanceTests.cpp` covers capture preferences, exclusions/markers, clear-on-exit/restart, pin-preserving clear, locked deletion/retry, orphan ownership, compaction preserving other data, bounded search/full restoration, idle retention, cache eviction, confirmation dialogs, and cancelled moves/shutdown.

`.github/workflows/windows.yml` installs Qt 6.8.3/MinGW, runs Release build/test/deploy/smoke checks, and uploads the package and evidence. Local execution does not establish that this remote workflow has run.

`tools/verify-windows-interactive.ps1` records pass/fail/not-run plus evidence for physical Explorer/application drops, shortcuts, clipboard interoperability, mixed-DPI/disconnected monitors, elevated windows, network shares, cross-volume moves, and shutdown. A checklist entry or offscreen render does not establish physical verification. See `VERIFICATION.md` for executed checks.
