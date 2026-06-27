# BUGS_TO_FIX.md — Qt6 Downloader Code Review Findings

> Generated from a comprehensive code review across **Core / UI / Persistence / Network / Build** domains.
> Total: ~244 bugs grouped by severity. Use this as a checklist when fixing.

## Severity legend

- **CRITICAL** — security vulnerabilities, data corruption, UAF, crashes, double-free.
- **HIGH** — race conditions, deadlock potential, correctness bugs, resource leaks.
- **MEDIUM** — UX issues, validation gaps, fragile error handling.
- **LOW** — cosmetics, dead code, style, defensive checks.

## Fix strategy

- One agent per file-group in its own git worktree (use `isolation: "worktree"`).
- Agents commit fixes incrementally on their own branch.
- Merge all branches back to `dev` after Wave 1 + Wave 2 complete.
- Verify with `cmake --build` (or at least `cmake -G "MinGW Makefiles" ..`).

---

## DOMAIN 1 — Core download (downloadtask.cpp, downloadtask.h)

### CRITICAL

- **downloadtask.cpp:800** — `onWorkerError` does `m_status = Failed;` raw assignment, skipping `statusChanged` emit. Always go through `setStatus()`.
- **downloadtask.cpp:166-184** — `start()` sets `Downloading` *before* HEAD even fires. Move `setStatus(Downloading)` into the `QTimer::singleShot(0,...)` lambda immediately before `initializeDownload()`.
- **downloadtask.cpp:1046-1050 + 716-727** — `updateDownloadedSize(totalSize)` after merge races with late `onWorkerProgress` increments. Discard late progress after merge (check status == Completed/Failed).
- **downloadtask.cpp:447, 463** — `m_headManager = new QNetworkAccessManager(this)` double-delete risk. Don't parent the QNAM; manage lifetime via `deleteLater()`.
- **downloadtask.cpp:466** — `connect(m_headReply, ..., this, ...)` UAF if DownloadTask destroyed before reply emits. Use `QPointer<DownloadTask> safeThis` at slot start.
- **downloadtask.cpp:480-487** — Timeout fallback only checks `m_status != Failed`; can overwrite a `Cancelled` status. Change to check `!= Failed && != Cancelled && != Completed`.

### HIGH

- **downloadtask.cpp:308-316** — `cancel()` snapshot uses different fields than `mergeFiles`. Snapshot once, use everywhere.
- **downloadtask.cpp:520-541** — `processHeadResponse` takes `m_mutex` but all access is main-thread; gratuitous locking. Remove lock.
- **downloadtask.cpp:670-704** — Clamp `m_threadCount` to reasonable upper bound (e.g. 16) to prevent INT_MAX runaway.
- **downloadtask.cpp:686-688** — Load-imbalance: last worker takes the remainder. Distribute `leftover` across first workers.
- **downloadtask.cpp:709-727** — `onWorkerProgress` adds bytes unconditionally; possible double-counting on retry. Use per-worker atomic counters and sum.
- **downloadtask.cpp:919-936** — `validateFinalFile` size check is unreliable right after rename on Windows. Use `totalBytesWritten` tracked value as authoritative size.
- **downloadtask.cpp:451** — `RedirectPolicyAttribute = NoLessSafeRedirectPolicy` allows HTTPS → HTTP downgrade. Use `SameOriginRedirectPolicy` or manual.

### MEDIUM

- **downloadtask.cpp:204-213, 251-252, 306-307** — Lock-order inversion between `pause()`/`resume()` (m_mutex → m_statusMutex) and `cancel()` (m_statusMutex → m_mutex). Establish strict global order: `m_statusMutex` before `m_mutex`.
- **downloadtask.cpp:147-165** — `start()` calls `worker->stop()` synchronously inside `m_mutex`. Snapshot, release lock, then stop.
- **downloadtask.cpp:373-400** — `setStatus` always queues `statusChanged` via `singleShot(0)`; rapid state changes deliver stale intermediate states.
- **downloadtask.cpp:284-352** — `cancel()` `waitForDone(3000)` is arbitrary; workers may not honor stop in 3 s. Have workers acknowledge stop via signal.
- **downloadtask.cpp:572-580** — `createHttpWorkers` via `QTimer::singleShot(0, ...)` can fire after `cancel()` and create workers post-cancellation. Add status check at top of timer lambda.
- **downloadtask.cpp:919-936** — No SHA256 verification of merged file. Add checksum.

### LOW

- **downloadtask.cpp:30-43** — Singleton access in ctor may UB if not yet initialized. Use lazy init.
- **downloadtask.cpp:46-49** — `m_threadCount` not clamped to upper bound (e.g. 16).
- **downloadtask.cpp:65-67** — `m_speedCalculationTimer` not restarted on re-entrant `start()`.
- **downloadtask.cpp:106-110** — `worker->deleteLater()` before `m_workers.clear()` — late signals can fire into fresh DownloadTask. `worker->disconnect(this)` first.
- **downloadtask.cpp:166-184** — Same: old workers not disconnected before clear.
- **downloadtask.cpp:326-348** — After `waitForDone`, queued timers may still fire. Process events.
- **downloadtask.cpp:355-371** — `progressPercentage` int cast loses precision for huge files. Use integer math.
- **downloadtask.cpp:373** — `setStatus` uses `this` as context; minor race.
- **downloadtask.cpp:451** — Already noted (high); redirect policy.
- **downloadtask.cpp:709-727** — Same as HIGH section; double-counting on retry.
- **downloadtask.cpp:790-831** — Cosmetic only; OK.
- **downloadtask.cpp:840** — Speed may go negative after merge override; clamp to 0.
- **downloadtask.cpp:951-957** — OK.
- **downloadtask.cpp:973** — `totalTempFileSize` is dead; remove or use.
- **downloadtask.cpp:994** — Same as CRITICAL section; late progress.
- **downloadtask.cpp:1060-1075** — Captured `record` may have copy elision issues; explicit copy ctor.
- **downloadtask.cpp:1060-1068** — `m_historyMutex` gratuitous around `m_url` read.
- **downloadtask.cpp:1067** — `m_finishTime` may be unset for cancelled/never-finished paths.
- **downloadtask.cpp:1080-1101** — `getTempDirectory` doesn't check `TMPDIR` (Unix). Use `QStandardPaths::TempLocation` first.
- **downloadtask.cpp:1118-1123** — TOCTOU on `QFile::exists`/`remove`. Just call `remove`.

---

## DOMAIN 1b — HttpWorker (httpworker.cpp, httpworker.h)

### CRITICAL

- **httpworker.cpp:236-246 + 181-198** — `continueDownload` opens part file in `Append` mode but trusts `Accept-Ranges: bytes` from HEAD implies GET respects `Range`. If server returns 200 OK + full body, the file gets prepended bytes **plus** full body appended → corrupt. Inspect response status (must be 206 Partial Content) and `Content-Range` header.
- **httpworker.cpp:419-436 + 412-450** — Retry path calls `startDownload()` from `onErrorOccurred` (main thread), but worker thread's `run()` has already returned. Worker is destroyed by DownloadTask destructor (`waitForDone(3000)` returns immediately), then retry touches dead `m_file`/`m_reply`. Make retry re-queue worker via `m_threadPool->start(this)` or disallow destruction during retry.
- **httpworker.cpp:238** — `m_netManager->get(request)` not null-checked before `connect()`. Null `m_reply` → null deref. Add null check.

### HIGH

- **httpworker.cpp:181** — `m_bytesReceived.store(m_file->size(), ...)` overloaded semantics (resume offset vs session bytes). Add `m_resumeOffset` separate from `m_sessionBytesReceived`.
- **httpworker.cpp:201** — Resume trusts `m_file->size()`; no integrity check vs server content. Validate or warn on size mismatch.
- **httpworker.cpp:226** — Only inactivity timeout (`setTransferTimeout(30000)`); no overall deadline.
- **httpworker.cpp:278-283** — `stopAsync` `QThread::currentThread() == this->thread()` compares against creation thread, not thread-pool thread. Always true → always takes invokeMethod path. Compare against `qApp->thread()`.
- **httpworker.cpp:300-307** — `readAll()` after `abort()` is redundant.
- **httpworker.cpp:355-359** — 1MB log throttle is hardcoded.
- **httpworker.cpp:412-438** — Retry `m_reply->disconnect(this); m_reply->deleteLater()` race; `m_reply->error()` may fire before deleteLater completes. `abort()` first.

### MEDIUM

- **httpworker.cpp:84-98** — `run()` doesn't handle `m_isStopped` set between check and `startDownload()` call. Re-check after.
- **httpworker.cpp:129-159** — `QTimer::singleShot(0, qApp, ...)` for QNAM creation — if app shuts down, lambda never fires and worker hangs. Use `safeThis` context.
- **httpworker.cpp:373** — `m_reply->error()` after `m_reply` may be null (concurrent `stop()`). Null-check at top.
- **httpworker.cpp:392-450** — `onFinished` may run after reply cleanup; double-emit guarded by `m_alreadyFinished` but check `if (!m_reply) return;` first.

### LOW

- **httpworker.cpp:62-63** — Destructor calls `m_reply->disconnect(this)` on partially destructed object. Use `disconnect()` (no args).
- **httpworker.cpp:80** — Destructor `m_netManager->deleteLater()` may double-free child QNetworkReplies. Disconnect reply first.
- **httpworker.cpp:85-98** — Cosmetic thread-id formatting.
- **httpworker.cpp:88** — Cosmetic.
- **httpworker.cpp:112** — Use `QAtomicInteger` instead of `std::atomic`.
- **httpworker.cpp:129-159** — `qApp` is null-check missing; defensive.
- **httpworker.cpp:260-271** — Same double-delete concern as :80.
- **httpworker.cpp:412** — `const int maxRetries = 3`; make `static constexpr`.
- **httpworker.cpp:432** — 2s retry delay hardcoded.
- **httpworker.cpp:466** — `connect(... this ...)` UAF; same as downloadtask.cpp:466.

---

## DOMAIN 2 — DownloadManager (downloadmanager.cpp, downloadmanager.h)

- **downloadmanager.cpp** — Cross-thread `onTaskFinished` via DirectConnection can race with DownloadTask destructor. Use QueuedConnection + QPointer.
- **downloadmanager.cpp** — `waitForDone(5000)` + `qDeleteAll(workers)` UAF if a worker emits `finished` during delete.
- **downloadmanager.cpp** — Inconsistent singleton style vs `ScheduleManager` (instance() pointer vs instance() reference).

---

## DOMAIN 3 — UI: MainWindow + SystemTray (mainwindow.cpp/.h/.ui, systemtray.cpp/.h)

### CRITICAL

- **mainwindow.cpp:1051-1071** — `QSet<DownloadTask*> m_tasksToUpdate` UAF: task may be destroyed before UI repaints. Guard with `QPointer<DownloadTask>` or remove on task destruction.
- **mainwindow.cpp:791-803** — Static `QHash` leaks on app exit. Move to member.
- **mainwindow.cpp:662-679** — `QtConcurrent::map` race with row mutation during UI update. Use only when table is locked, or remove entirely.
- **mainwindow.cpp:878-894** — `actionExit` routes through `closeEvent` which can be overridden to minimize-to-tray. Add a hard-exit path that bypasses the close event.

### HIGH

- **mainwindow.cpp** — Throttled UI updates via `m_uiUpdateTimer` + `m_tasksToUpdate` lose updates when tasks are removed mid-tick.
- **mainwindow.cpp** — Tray-icon double-click handler can race with main window show.
- **mainwindow.cpp** — Settings change emits don't propagate to running tasks.
- **mainwindow.cpp** — DownloadTask lifecycle: pause/cancel buttons not disabled during Pending state.
- **mainwindow.cpp** — Theme switch while dialogs open leaves dialogs in old theme.
- **mainwindow.cpp** — `closeEvent` minimization path can lose unsaved user input.
- **systemtray.cpp:59** — `m_trayMenu` unparented leak. Parent to `m_trayIcon` or member-init.

### MEDIUM

- **mainwindow.cpp** — Selection cleared on row update.
- **mainwindow.cpp** — Right-click context menu on header missing.
- **mainwindow.cpp** — Status bar overlap when error message + progress shown.
- **mainwindow.cpp** — Drag-drop of URLs only accepts HTTP/HTTPS; rejects magnet links etc.
- **mainwindow.cpp** — Table header columns not persisted across runs.
- **mainwindow.cpp** — Search/filter missing for active downloads (history has it).
- **mainwindow.cpp** — Window geometry not restored on next launch.
- **systemtray.cpp** — Tooltip not updated when active download count changes.
- **systemtray.cpp** — Notification click handler missing.
- **systemtray.cpp** — Single-click activation toggles window but no "show on top".

### LOW

- **mainwindow.ui** — Hard-coded English strings in some labels (others use tr()).
- **mainwindow.cpp** — Magic numbers (column widths, row heights).
- **mainwindow.cpp** — Comments in Chinese (inconsistent with English UI).
- **mainwindow.cpp** — `qDebug` instead of `LOGD` in some places.
- **systemtray.cpp** — QMenu actions don't have icons.

---

## DOMAIN 4 — UI: 4 Dialogs (newtaskdialog, settingsdialog, scheduledialog, historydialog)

### CRITICAL

- **newtaskdialog.cpp** — `accept()` override may allow empty URL or invalid filename through.
- **settingsdialog.cpp/ui** — UI only has Apply | Cancel buttons; no OK button. Easy to dismiss without saving.
- **settingsdialog.cpp** — Validation of port/thread-count paths missing.

### HIGH

- **newtaskdialog.cpp** — Filename sanitization: blocks some but allows `..` in middle, embedded NUL bytes, Windows reserved names.
- **newtaskdialog.cpp** — Save path may escape default download dir.
- **settingsdialog.cpp** — Proxy URL not validated for scheme (socks5:// in HTTP proxy).
- **settingsdialog.cpp** — Thread count not bounded (allows INT_MAX).
- **settingsdialog.cpp** — Port not validated against in-use system ports.
- **scheduledialog.cpp** — Schedule cron expression not validated.
- **scheduledialog.cpp** — Time-zone handling (always uses local; no DST awareness).
- **historydialog.cpp** — Search box doesn't filter by date range / size.
- **historydialog.cpp** — Right-click "Delete" doesn't confirm.

### MEDIUM

- **newtaskdialog.cpp** — URL validation only checks scheme; allows userinfo.
- **newtaskdialog.cpp** — No paste-from-clipboard button.
- **newtaskdialog.cpp** — Drag-drop of file not supported.
- **newtaskdialog.cpp** — Auto-fill filename from URL may include query string.
- **settingsdialog.cpp** — "Reset to defaults" doesn't restore sub-options.
- **settingsdialog.cpp** — Theme/language selectors not synced with current values on open.
- **scheduledialog.cpp** — Recurring task UI doesn't show next-fire time.
- **scheduledialog.cpp** — No bulk enable/disable.
- **historydialog.cpp** — Sort by column header not preserved.
- **historydialog.cpp** — Export to CSV missing.
- **historydialog.cpp** — Re-download from history requires URL only (no headers/cookies).
- **historydialog.cpp** — Clear-history confirmation dialog not shown.

### LOW

- **newtaskdialog.ui** — Tab order inconsistent.
- **newtaskdialog.cpp** — Layout breaks at small window widths.
- **settingsdialog.ui** — No keyboard shortcut for Save.
- **settingsdialog.cpp** — Tooltips missing for advanced options.
- **scheduledialog.ui** — Time picker doesn't accept typed input.
- **scheduledialog.cpp** — Validation error messages not translated.
- **historydialog.ui** — Column header text hard-coded English.
- **historydialog.cpp** — Empty-state message missing.
- **historydialog.cpp** — Date format inconsistent with system locale.

---

## DOMAIN 5 — Persistence (settingsmanager, historymanager, schedulemanager, logger.h, main.cpp)

### CRITICAL

- **historymanager.cpp:189-198** — On open failure, destroys history file. Wrap in atomic rename to `.bak` and recover.
- **schedulemanager.cpp** — Inconsistent singleton style: returns pointer + `m_instance`; others use reference. Migrate to match.
- **logger.h** — Static cache race during static init; file handle open/close per log line is slow.

### HIGH

- **historymanager.cpp** — JSON file read into memory; large histories consume RAM and slow startup.
- **historymanager.cpp** — No write atomicity (no `.tmp` + rename) — partial writes corrupt the file.
- **historymanager.cpp** — Search/filter doesn't index; scans entire file.
- **historymanager.cpp** — `addRecord` not thread-safe with concurrent `getAll`/`delete`.
- **historymanager.cpp** — Records with empty fields get added (URL empty, etc.).
- **settingsmanager.cpp** — `QSettings` not flushed after writes; OS crash can lose settings.
- **settingsmanager.cpp** — Default value changes after release don't migrate existing user settings.
- **settingsmanager.cpp** — Boolean stored as int "1/0" inconsistently.
- **schedulemanager.cpp** — QTimer single-shot for periodic check; if app suspends, scheduled tasks miss.
- **schedulemanager.cpp** — Recurring task next-fire calculation has off-by-one on DST boundary.
- **schedulemanager.cpp** — No max history of past runs (only current scheduled task list).
- **main.cpp:40-41** — Init order: `SettingsManager` → `HistoryManager` but `ScheduleManager` not pre-initialized; lazy access in dialog constructor may race.
- **logger.h** — Default path `D:/LOG/downloader.log` not writable on non-Windows.

### MEDIUM

- **historymanager.cpp** — Record date stored as ISO string vs QDateTime inconsistent.
- **historymanager.cpp** — No way to mark record as "important" or tag it.
- **settingsmanager.cpp** — Proxy type radio buttons not disabled when "no proxy" selected.
- **settingsmanager.cpp** — Port collision with running server not detected at save time.
- **schedulemanager.cpp** — Task list not exported/importable.
- **schedulemanager.cpp** — `loadTasks()` doesn't deduplicate by ID.
- **schedulemanager.cpp** — `removeTask(id)` silently no-ops on missing ID.
- **logger.h** — Thread-safety of file handle depends on caller locking; LOGD macro does no locking.
- **logger.h** — Log rotation missing (file grows forever).
- **main.cpp** — Translation loading happens before UI exists; if loading fails, app starts in English silently.

### LOW

- **settingsmanager.cpp** — String keys duplicated in header and `.cpp`.
- **settingsmanager.cpp** — `getDefault*()` not documented.
- **historymanager.cpp** — Constructor writes file if missing; can race with first read.
- **schedulemanager.cpp** — QTimer::start parameter magic numbers.
- **logger.h** — Macro expansion in `LOGD` can shadow variables.
- **logger.h** — No log-level filtering.

---

## DOMAIN 6 — Network: HttpServer (httpserver.cpp, httpserver.h, plus shared localserver.cpp/.h)

### CRITICAL

- **httpserver.cpp:284** — `Access-Control-Allow-Origin: *` + no auth + localhost-bound port = CSRF: any web page can `fetch('http://localhost:8080/download', ...)`. Implement bearer token or origin allowlist.
- **httpserver.cpp:75-111** — `isValidDownloadUrl` misses `127.0.0.1` in IPv6-mapped `[::ffff:127.0.0.1]`, `127.1`, `0`, `0x7f000001`, `localhost.localdomain`, DNS-rebind. Use `QHostAddress::parse` + resolve hostname via `QHostInfo::lookupHost` + reject private/link-local/loopback/CGNAT ranges.
- **httpserver.cpp:117-127, 543** — `isSafeFileName` misses Windows reserved device names (CON/PRN/AUX/NUL/COM1-9/LPT1-9), trailing `.`/space, embedded `C:` drive, control chars. Reject any character `<>:"/\|?*` anywhere; reject basename matching reserved list (case-insensitive); reject NUL/control chars.
- **httpserver.cpp:152, 162, 171, 184, 193, 203, 213, 222, 232** — All `QHttpServerResponse` error returns default to HTTP 200. Use `QHttpServerResponse::StatusCode::BadRequest` etc.

### HIGH

- **httpserver.cpp:326-392** — Slowloris DoS: no connection count cap, no idle timeout, no Content-Length upper bound (memory exhaustion via `m_buffers.append`). Cap connections, reject `Content-Length > 1 MiB`, idle-disconnect after 30 s, hard-cap buffer at 2 MiB.
- **httpserver.cpp:374** — `Content-Length` parse math: `line.mid(line.size() - lower.size() + 15)`. Use `indexOf(':')` + `mid(idx + 1).trimmed().toInt()`; reject `< 0` or `> MAX`.
- **httpserver.cpp:412-488** — HTTP pipelining silently drops bytes: `m_buffers.remove(clientSocket)` after processing loses subsequent requests. Preserve tail.
- **httpserver.cpp:331-332, 397-404** — `disconnected` handler doesn't disconnect `readyRead` from socket before `deleteLater` → potential UAF on race.
- **httpserver.cpp:243-264** — Race on port binding: `startServer` doesn't `stopServer` first; second start leaks `QTcpServer` and fails `listen()`. Add `SO_REUSEADDR`; always call `stopServer` at top.
- **localserver.cpp:126-165** — Bare JSON parsing, no auth, no validation, no SSRF protection. Either delete or apply same validations as `HttpServer`.
- **httpserver.cpp:440** — `waitForBytesWritten(1000)` blocks event loop. Use `bytesWritten` signal async.
- **httpserver.cpp:415-442** — Response CRLF injection risk if `contentType` ever becomes user-controlled. Whitelist.

### MEDIUM

- **httpserver.cpp:271-275** — `try/catch(std::exception)` around Qt code that doesn't throw. Remove.
- **httpserver.cpp:378** — O(N) byte-by-byte header parse; duplicate parser. Extract `parseContentLength` helper.
- **httpserver.cpp:391** — Double `m_buffers.remove` (in `onReadyRead` and `onClientDisconnected`). Centralize cleanup.
- **httpserver.cpp:494-497** — `Access-Control-Max-Age: 86400` caches permissive CORS for 24h. Lower to 300.
- **httpserver.cpp:506-509** — `GET /` has no `Content-Length`.
- **httpserver.cpp:332** — `clientSocket` parent is `m_tcpServer` not `this`; buffer key may dangle.
- **插件/background.js:96-101** — Protocol fields (`fileSize`, `mimeType`) sent but not parsed.
- **插件/popup.js:25** — Port validation: `port < 1` lexicographic compare for non-numeric strings. Use `Number.isInteger`.
- **插件/popup.js:35-42** — `chrome.storage.local.set` callback doesn't check `chrome.runtime.lastError`.
- **插件/popup.js:53-62** — `chrome.runtime.lastError` checked in rejection path only.
- **插件/manifest.json:11-16** — `activeTab` permission unused (no content script).
- **插件/manifest.json:25-28** — `host_permissions` only matches default ports (`:80`); service worker `fetch` doesn't check it anyway. Either remove or use `:*/*`.
- **httpserver.cpp:57-65** — Hardcoded `corsHeaders()` with `Allow-Origin: *`. Echo validated origin instead.
- **httpserver.cpp:419-425** — `statusText` switch defaults to "OK" for 413/414/500/503. Use proper lookup.
- **httpserver.cpp:513** — `bodySection.left(contentLength)` clamps; if shorter than Content-Length, partial body accepted. Reject 400.
- **httpserver.cpp:178-179** — `obj.value("filename").toString()` silently coerces non-strings. Reject non-string.
- **httpserver.cpp:549** — `qDebug` logs URL with userinfo. Strip `userInfo` and mask tokens.
- **插件/background.js:5** — `DEFAULT_PORT = 8080` hardcoded in 3 places.
- **插件/background.js:20-25** — `currentPort = newValue` may be `undefined` after storage delete. Validate type.
- **插件/background.js:78-161** — `downloadItem.filename` interpolated into notification text without sanitization. Strip control chars, normalize whitespace, truncate.

### LOW

- **httpserver.cpp:8** — `#if USE_QHTTPSERVER` vs `#ifdef QT_HTTPSERVER_LIB`; use `#if defined` consistently.
- **httpserver.cpp:57-65** — `corsHeaders()` allocates each call; `static const` cache.
- **httpserver.cpp:75** — `isValidDownloadUrl` doesn't block hostname `"0"`.
- **httpserver.cpp:97-100** — IPv6 private ranges (`fc00::/7`, `fe80::/10`) not blocked.
- **httpserver.cpp:117-127** — NUL byte not rejected in `isSafeFileName`.
- **httpserver.cpp:122** — `name.contains("..")` substring match too broad.
- **httpserver.cpp:200, 549** — `qDebug()` instead of `LOGD`.
- **httpserver.cpp:332** — `qDebug` peer-address logs noisy under probe.
- **httpserver.cpp:264** — `m_tcpServers` redundant given object tree.
- **httpserver.cpp:351** — `qDebug` on every `readyRead`.
- **httpserver.cpp:243** — Lifetime documentation in header.
- **httpserver.cpp:156-157 etc.** — CORS headers inline 5+ times; extract `applyCors` helper.
- **localserver.cpp:1-96** — All Chinese comments; OK for i18n-agnostic but document.
- **localserver.cpp:134** — Logs entire request body; info disclosure.
- **localserver.cpp:156** — Response uses `\n` not `\r\n`; document non-HTTP framing.
- **插件/background.js:66-75** — `AbortController.abort` from setTimeout can fire just as fetch resolves. Use `AbortSignal.timeout()`.
- **插件/background.js:104-108** — `pause()` called twice (onCreated + sendDownloadToLocalApp).
- **插件/background.js:32-36** — `setTimeout(60s)` doesn't survive MV3 worker termination. Use `chrome.alarms`.
- **插件/popup.js:1-69** — Non-ASCII digit validation missing.
- **插件/popup.js:31-47** — Can't save port without successful test; minor UX.
- **插件/manifest.json:6-10** — Three icon sizes point to same file.
- **httpserver.h:11-19** — `#ifdef QT_HTTPSERVER_LIB` may not be portable; use `__has_include`.
- **httpserver.h:103** — `QHash<QTcpSocket*, QByteArray>` raw pointer keys; auto-remove on destroyed via QObject::destroyed connect.
- **httpserver.cpp:130-136** — Same as CRITICAL — all errors return 200.
- **httpserver.cpp:278-280** — `m_buffers` not cleared before reopen.
- **httpserver.cpp:412-413** — `processHttpRequest` style only.
- **localserver.h:13-23** — Add `@warning` about no validation.
- **httpserver.cpp:201** — Signal param named `savePath` but passed a filename.
- **插件/background.js:177-189** — `onChanged` with `state.current === 'interrupted'` infinite pause loop. Track intentional pause flag.
- **插件/background.js:191-209** — `onMessage` doesn't validate `sender.id`.
- **插件/test_local_server.py:50** — `timeout=5` single value (both connect+read). Use `timeout=(3,10)`.
- **插件/test_local_server.py:108-114** — `test_download_request()` return value ignored.
- **插件/test_local_server.py:10** — No friendly error if `requests` not installed.

---

## DOMAIN 7 — Build & Misc (CMakeLists.txt, .gitignore, .gitattributes, icon.qrc, installer/, release-page/, README.md, 思路.md, logs_config.txt, styles/, translations/, LICENSE, test_local_server.py)

### HIGH

- **CMakeLists.txt** — Qt path hardcoded `D:/Qt/6.10.0/mingw_64`; non-portable. Use `find_package(Qt6 ...)` defaults or detect.
- **CMakeLists.txt** — Missing `qt_add_translations` for `translations/*.ts`; current build does not generate `.qm`.
- **CMakeLists.txt** — `qt_generate_deploy_app_script` referenced but deployment steps not exercised in CI.
- **installer/build.bat** — Qt Installer Framework path hardcoded to IFW 4.6 (`C:/Qt/Tools/QtInstallerFramework/4.6/bin`).
- **installer/config.xml** — `<Logo>` references `.ico`; installer framework expects PNG/SVG in modern IFW.
- **installer/config.xml** — No `<Dependencies>` block for required runtime libs.
- **installer/packages/** — Missing `installscript.qs` for first-run port-config.
- **installer/packages/** — `package.xml` uses placeholder `<Version>1.0.0</Version>` but project is `0.1.0`.
- **installer/installer.qs** — Introduction callback runs after page is hidden.

### MEDIUM

- **CMakeLists.txt** — Redundant `find_package(Qt6 ...)` declarations.
- **.gitignore** — Typo: `CmakeLists.txt.user` lowercase should be `CMakeLists.txt.user`.
- **.gitignore** — Doesn't ignore `*.bak`, `*.tmp`, `*.orig`.
- **.gitattributes** — Missing; line-ending normalization inconsistent across contributors.
- **icon.qrc** — References `translations/*.qm` but no `lrelease` step builds them.
- **README.md** — Self-acknowledges "好了骗你的用不了" for SQLite.
- **README.md** — Inconsistent line numbers referencing translation files.
- **思路.md** — Design doc references file paths that no longer exist after refactor.
- **logs_config.txt** — Hardcoded `D:/LOG/downloader.log`; cross-platform broken.
- **styles/dark.qss** — Some selectors don't match actual widget objectNames.
- **styles/light.qss** — Same as dark.
- **translations/zh_CN.ts/.qm** — Translation context comments outdated.
- **translations/en_US.ts/.qm** — Same.
- **release-page/index.html** — Version mismatch (0.1.0 vs 1.0.0).
- **release-page/style.css** — Duplicate class definitions.
- **release-page/script.js** — `execCommand` deprecated.
- **release-page/index.html** — Copyright year 2025; update to current.
- **release-page/index.html** — Download links broken (point to `releases/latest` placeholder).
- **LICENSE** — Year not updated.

### LOW

- **CMakeLists.txt** — Magic numbers (target version 1.0.0).
- **CMakeLists.txt** — No `target_compile_features(cxx_std_17)` explicit (defaults may vary).
- **.gitignore** — Doesn't ignore `.vscode/`, `.idea/`.
- **installer/build.bat** — Doesn't check IFW presence before invoking.
- **installer/packages/** — Description strings not translated.
- **installer/installer.qs** — No rollback on partial install failure.
- **release-page/index.html** — Favicon missing.
- **release-page/script.js** — No CSP meta tag.
- **README.md** — Screenshots reference local paths.
- **思路.md** — Chinese comments mixed with English code identifiers.
- **logs_config.txt** — `[log_path]` documented but no other keys.
- **styles/dark.qss, light.qss** — `qproperty-*` selectors use deprecated syntax.
- **translations/*.ts** — Some source strings missing `// TRANSLATOR` comments.
- **installer/config.xml** — `<Title>` not localized.

---

## Cross-cutting suggestions

- Public API signatures should not change unless required. If a signature change is unavoidable, update all callers in the same agent's domain.
- Don't introduce new dependencies (no Boost, no spdlog). Stick to Qt6.
- Prefer `QPointer<T>` over raw `T*` for QObject pointers crossing thread boundaries.
- Prefer `QStringLiteral` for hot-path string construction.
- All new code must compile cleanly with `-Wall -Wextra -Werror` (if the project enables that, otherwise follow existing style).
- Do NOT modify any file outside your assigned domain. If a fix touches another domain's code, leave it as a comment and report it back.

## End of BUGS_TO_FIX.md