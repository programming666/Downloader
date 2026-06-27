# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

基于 Qt6 的多线程下载器，支持 HTTP/HTTPS、断点续传、分块下载、浏览器插件接管下载、系统托盘、定时下载、深色/浅色主题、中英文界面切换。组织名 `Programming666`，应用名 `Downloader`（在 `main.cpp` 中通过 `QCoreApplication::setOrganizationName/setApplicationName` 设置，决定 `QSettings` 与 `QStandardPaths` 的存储位置）。

## 构建与运行

### 环境要求

- Qt **6.10.0** beta3（MinGW 64-bit，路径硬编码为 `D:/Qt/6.10.0/mingw_64`）
- CMake ≥ 3.19
- C++17 编译器（推荐 MinGW）
- 可选 `Qt6::HttpServer` 模块（CMakeLists.txt 通过 `find_package` 自动探测；存在则定义 `QT_HTTPSERVER_LIB` 宏；缺失时回退到 `QTcpServer` 手动实现 HTTP 协议）

### 构建命令

```bash
mkdir build
cd build
cmake -G "MinGW Makefiles" ..
cmake --build .
```

Qt 路径在 `CMakeLists.txt:3` 硬编码，迁机或换 Qt 安装位置需要同步修改。`qt_generate_deploy_app_script` 会在安装时生成部署脚本。

### 安装包构建

`installer/build.bat` 通过 Qt Installer Framework 的 `binarycreator` 把 `packages/` 打成 `output/DownloaderInstaller.exe`：

```bash
cd installer
build.bat
```

### 浏览器插件

`插件/` 目录是 Chrome/Edge Manifest V3 扩展，加载后在浏览器中拦截下载并通过 HTTP POST 把 `{url, filename}` 发到 `http://localhost:<port>/download`。`manifest.json` 与 `background.js` 在该目录下；监听端口默认在 `SettingsManager` 中配置（见下方）。

### 本地服务器联调测试

`test_local_server.py`（需 `requests`）模拟插件发送请求，便于在没打开浏览器时调试 HTTP 服务器。

```bash
python test_local_server.py
```

## 架构概览

### 单例组件

| 类 | 头文件 | 职责 |
|----|--------|------|
| `DownloadManager` | `downloadmanager.h` | 任务调度中心，持有全局 `QThreadPool`，负责 `createTask/startTask/pauseTask/resumeTask/cancelTask` |
| `SettingsManager` | `settingsmanager.h` | 包装 `QSettings`，持久化代理、主题、默认下载路径、线程数、本地监听端口、静默模式 |
| `HistoryManager` | `historymanager.h` | JSON 文件读写下载记录（`DownloadRecord`），add/get/delete/clear |
| `ScheduleManager` | `schedulemanager.h` | 定时下载任务管理，`QTimer` 周期检查 `ScheduledTask`，支持重复任务；**注意：此单例使用 `static m_instance` 指针，与前三者的 `static instance()` 引用返回风格不一致** |

初始化顺序在 `main.cpp:40-41`：`SettingsManager::instance()` → `HistoryManager::instance()`（构造时立刻加载，避免后续竞态）。`ScheduleManager` 未在此处预热。

### 一次下载的数据流

```
HttpServer (本地 HTTP 监听)
   │  newDownloadRequest(url, savePath)
   ▼
MainWindow::onNewDownloadRequestFromBrowser
   │  弹出 NewTaskDialog（预填 URL/路径）
   ▼
DownloadManager::createTask / startTask
   │  把 DownloadTask 投递到 QThreadPool
   ▼
DownloadTask (downloadtask.cpp, 1000+ 行核心)
   │  onHeadRequestFinished → 获取文件大小/支持 Range？
   │  createHttpWorkers → 按 threadCount 切分字节区间
   │  每个 HttpWorker 写自己的临时分片文件
   ▼
HttpWorker (httpworker.h, QObject + QRunnable)
   │  Range 请求 + 边读边写
   │  progress/finished/error 信号
   ▼
DownloadTask 汇总进度 → 进度信号 → MainWindow 更新 QTableWidget 行
   │  allWorkersFinished → mergeFiles → 临时分片合并为目标文件
   ▼
saveToHistory → HistoryManager::addRecord
```

### 关键文件与作用

- `main.cpp` — 应用入口；翻译加载（zh_CN/en_US）；启动 `HttpServer`；信号连接到 `MainWindow::onNewDownloadRequestFromBrowser`
- `mainwindow.cpp/.h` — UI 主线程上下文，所有下载在 `QTableWidget` 展示；包含节流 UI 更新的 `QTimer m_uiUpdateTimer` 和 `QSet<DownloadTask*> m_tasksToUpdate`；关闭时通过 `closeEvent` 最小化到托盘
- `downloadtask.cpp` — 任务生命周期最复杂处，包含 HEAD 请求、分片、合并、暂停续传、速度采样定时器；状态枚举 `DownloadTaskStatus`：`Pending/Downloading/Paused/Cancelled/Completed/Failed`
- `httpworker.cpp/.h` — 单个分片下载工作单元，写入 `*.part` 临时文件
- `httpserver.cpp/.h` — `#ifdef QT_HTTPSERVER_LIB` 二选一实现：`QHttpServer` 路由 `/download`（POST）或 `QTcpServer` 手动解析 HTTP/JSON
- `localserver.cpp/.h` — 另一套本地 TCP 实现（结构同 `HttpServer` 的回退分支）；当前 `main.cpp` 仅实例化 `HttpServer`，`LocalServer` 处于备用状态
- `newtaskdialog.cpp/.h` — 新建任务对话框，覆写 `accept()` 做 URL/路径校验
- `settingsdialog.cpp/.h` — 设置 UI，加载/保存到 `SettingsManager`
- `scheduledialog.cpp/.h` + `schedulemanager.cpp` — 定时下载入口与持久化
- `historydialog.cpp/.h` — 历史表格 + 搜索框 + 右键菜单
- `systemtray.cpp/.h` — `QSystemTrayIcon` + 上下文菜单
- `logger.h` — 提供 `LOGD(x)` 宏，把日志写入 `logs_config.txt` 中 `[log_path]` 配置的路径（默认 `D:/LOG/downloader.log`）；无配置或打开失败则静默失败

### 资源与样式

- `styles/dark.qss`、`styles/light.qss` — QSS 主题，由 `MainWindow::loadStyleSheet(themeName)` 在启动与主题切换时加载
- `translations/zh_CN.ts/.qm`、`en_US.ts/.qm` — Qt Linguist 翻译文件；`.ts` 是源码，`.qm` 是已编译二进制
- `icon.qrc` + `icon.ico` — Qt 资源系统，UI 图标统一从这里取（避免 `README.md` 提到的图标加载偶尔失败）
- `icon.png` 在 `插件/` 子目录，浏览器扩展独立使用

### 配置键

`SettingsManager` 内部常量（`settingsmanager.h:130-148`）按 group 组织：`GROUP_NETWORK`（代理）、`GROUP_UI`（主题）、`GROUP_DOWNLOAD`（默认路径/线程数）、`GROUP_LOCAL_SERVER`（监听端口）、`GROUP_NOTIFICATION`（静默模式）。新增配置项时记得同时加 key 常量与读写方法。

## 已知约束与坑

1. **`LocalServer` 与 `HttpServer` 并存** — `main.cpp` 只用 `HttpServer`，但 `LocalServer` 完整保留；改动其中一个时注意不要假定另一个行为一致。
2. **状态机分散** — `DownloadTaskStatus` 的转换在 `DownloadTask::setStatus` 与多处直接赋值共存，修改状态前先 `grep` 所有写入点。
3. **Qt 路径硬编码** — `CMakeLists.txt:3` 与 `logger.h` 默认日志路径 `D:/LOG/downloader.log` 都基于 Windows 固定盘符，跨平台或换盘符需要同步改两处。
4. **HEAD 请求超时** — `DownloadTask` 有 `m_headRequestTimedOut` 标志和互斥锁（`m_mutex/m_statusMutex/m_historyMutex`），并发修改这三类状态时记得锁对应的 mutex，不要复用 `m_mutex`。
5. **`HttpWorker` 同时继承 `QObject` 和 `QRunnable`** — 通过 `DownloadManager` 的 `QThreadPool` 调度，但网络对象的所有权仍归主线程，跨线程 `stop()`/`stopAsync()` 时序敏感。
6. **`HistoryManager` 用 JSON 而不是 SQLite** — `README.md` 自承 "好了骗你的用不了" 指的就是这里；扩展历史查询前要清楚它读全文件到内存。
7. **`SCHEDULE` 单例风格不统一** — `ScheduleManager::instance()` 返回指针并使用 `m_instance`，不要按 `DownloadManager::instance()` 的引用风格调用。
8. **`installer/`、`release-page/`、`build/`、`*.user`、`插件.crx/.pem` 被 `.gitignore` 忽略** — 修改这些目录的产物不会被 git 追踪。

## 浏览器插件通信契约

`/download` POST JSON：
```json
{ "url": "https://...", "filename": "..." }
```
服务器解析后发射 `newDownloadRequest(url, savePath)`，`MainWindow` 弹出 `NewTaskDialog` 预填。修改请求体字段需要同步 `HttpServer` 的解析、`MainWindow::onNewDownloadRequestFromBrowser` 的处理、以及 `插件/background.js` 的发送端。