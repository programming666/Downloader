# 翻译者说明 / Translator Notes

本文档汇总了翻译 `translations/zh_CN.ts` 与 `translations/en_US.ts` 时需要遵循的约定。
This document collects conventions for translating `translations/zh_CN.ts` and `translations/en_US.ts`.

## 关于 Qt TS 文件格式 / About Qt TS file format

Qt Linguist 的 TS 文件使用 XML 格式。**顶层只接受 `<TS>` 直接子元素 `<context>`**。
Qt's TS files are XML. **The `<TS>` root accepts only `<context>` as direct children.**

工具 `lupdate` 会从 C++/Qt 源码中扫描 `tr()` / `QObject::tr()` / `.ui` 文件中的可翻译字符串，
生成 `<message><source>...</source><translation>...</translation></message>` 条目。
The `lupdate` tool scans C++/Qt sources (`tr()`, `QObject::tr()`, `.ui`) for translatable strings
and generates `<message>...</message>` entries.

## 在源码中重新生成 .ts / Regenerating .ts from sources

```bash
# 用 CMake 构建目标触发更新：
cmake --build . --target Downloader_lupdate
# 或者手工调用：
# Or call manually:
lupdate mainwindow.cpp downloadtask.cpp httpworker.cpp \
        newtaskdialog.ui settingsdialog.ui scheduledialog.ui \
        historydialog.ui systemtray.cpp -ts translations/zh_CN.ts translations/en_US.ts
```

⚠️ **不要在 `.ts` 文件顶部添加 `<translatorcomment>...</translatorcomment>` 块**——
它不是合法的 Qt TS 顶层元素，会导致 `lrelease` 报错 `Unexpected tag <translatorcomment>`。
⚠️ **Do NOT add a `<translatorcomment>...</translatorcomment>` block at the top of the .ts file** —
it is not a valid Qt TS root-level element and will cause `lrelease` to fail with
`Unexpected tag <translatorcomment>`.

如果想在 `.ts` 文件里留下说明，请改用标准 XML 注释：
If you want to leave notes inside the .ts file, use standard XML comments instead:

```xml
<!-- This is ignored by lrelease / lupdate -->
```

## 翻译约定 / Translation conventions

### 占位符 / Placeholders

- `%1`, `%2`, `%3` ... 是 Qt 在运行时按顺序替换的位置占位符。**绝对不要改变它们的顺序。**
- `%1`, `%2`, `%3` ... are positional placeholders Qt substitutes at runtime. **Never change their order.**

### 状态关键词（窄列展示）/ Status keywords (narrow column)

下列词会出现在 `mainwindow.cpp:57` 的"状态"列以及状态栏中，列宽有限，请保持简短：

| Chinese | English |
|---------|---------|
| 等待中 | Pending |
| 下载中 | Downloading |
| 已暂停 | Paused |
| 已取消 | Cancelled |
| 已完成 | Completed |
| 失败 | Failed |

### 表头（`mainwindow.cpp:57`）/ Table headers

| Chinese | English |
|---------|---------|
| 文件名 | Filename |
| URL | URL *(brand term, do NOT translate)* |
| 进度 | Progress |
| 大小 | Size |
| 速度 | Speed |
| 状态 | Status |
| 操作 | Action |

### 不要翻译的内容 / What NOT to translate

- 文件路径（如 `D:/Downloads/setup.exe`）
- 主机名 / IP（如 `cdn.example.com`）
- 协议名（HTTP、HTTPS、FTP、SFTP）
- 品牌术语 / API 名（URL、JSON、HTTP、CMake、Qt、GitHub）
- 已英文源码（如 `"Switch to English interface"`）

### 已英文的源串 / Already-English source strings

部分源串本身就是英文（来自 `QObject::tr("Switch to English interface")` 等）。
**保持原样**，不要二次"翻译"成别的英文。
Some source strings are already English (from `QObject::tr("Switch to English interface")` etc.).
**Keep them verbatim** — don't "translate" them to a different English wording.

## 涉及到的源文件 / Source files covered

- `mainwindow.cpp` / `mainwindow.ui`
- `downloadtask.cpp` / `downloadtask.h`
- `httpworker.cpp` / `httpworker.h`
- `newtaskdialog.cpp` / `newtaskdialog.ui`
- `settingsdialog.cpp` / `settingsdialog.ui`
- `scheduledialog.cpp` / `scheduledialog.ui`
- `historydialog.cpp` / `historydialog.ui`
- `systemtray.cpp`