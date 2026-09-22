# WorkBFF（工作搭档）

一款 Windows 桌面快捷工具：将常用的文本(各种记不住的命令)、HTTP 地址、文件夹路径、可执行文件、自定义 scheme 协议收藏到本地数据库，通过关键词快速搜索，单击复制、双击打开。

## 功能特性

- **Dock 悬浮图标**（32×32 圆形）
  - hover 时彩色跑马灯动画
  - 可拖拽移动，启动时停靠在屏幕右上角（距顶/右各 80px）
  - 单击开关主界面
- **主界面**（480×480，无边框圆角窗口，`Qt::Tool` 不进任务栏）
  - **点击窗口外部自动隐藏**（失活监听 + 150ms 延迟隐藏；Dock 点击前取消待隐藏），再次点击 Dock 重新打开
  - 32px 标题栏：可拖拽移动；☰ 菜单（登录 / 添加 / 帮助 / 版本）
  - 搜索框：输入即筛选，多关键词（空格/逗号/分号分隔，需全部命中）
  - 列表：条目自动换行、行高自适应；hover 高亮；每行下方分割线、右上角删除按钮
  - 条目交互：**单击复制内容；双击按类型打开**（文件夹→资源管理器、exe→启动进程、http/scheme→系统 Shell 打开）
- **添加**
  - 最多 3 个搜索关键词
  - 内容支持多行（`QPlainTextEdit`）
  - 类型：文本 / 文件夹 / 可执行文件 / HTTP 地址 / **Scheme 协议**
  - **优先级（级 1/2/3）**：搜索框为空时只显示第 1 级；输入关键词后显示全部级别
  - **显示优先级（1~5）**：同级内排序用，1→5 在前，同级最新添加在前
  - 可加密：加密内容经 AES-256-CBC 加密后入库
- **登录 / 加密**
  - 首次使用需设置密码（密码加盐 SHA-256 存 `meta` 表）
  - 加密条目未登录时隐藏；登录后解密显示
  - 登录状态仅本次运行有效
- **系统托盘**：右键菜单（打开主界面 / 退出），左键单击切换主界面
- **DeepLink**：`workbff://action?start=1` 单实例 + 参数转发

## 技术栈

| 项 | 说明 |
|---|---|
| 语言/标准 | C++17 |
| UI 框架 | Qt 5.15.2（Widgets，含 QtSql） |
| 数据库 | SQLite（Qt SQL 模块，QSQLITE 驱动） |
| 加密 | OpenSSL EVP（AES-256-CBC）+ SHA-256 |
| 包管理 | vcpkg（mttool / curl / cpr / nlohmann-json / log-cpp / OpenSSL） |
| 日志 | spdlog + log-cpp（rotating file sink，`logs/workbff.log`） |
| 三方库 | `third_part/`：mtNet（HTTP/WS）、mtPool（线程池）、qwindowkit（无边框框架，暂未使用其 API） |
| 打包 | Inno Setup（`workBFF.iss`） |

## 目录结构

```
workBFF/
├── CMakeLists.txt            # 顶层：版本管理、编译选项、子目录
├── CMakePresets.json         # workBFF 预设（VS2022 x64 + vcpkg）
├── install.bat               # 一键构建 + windeployqt 部署 + ISCC 打包
├── workBFF.iss               # Inno Setup 安装包脚本
├── vcpkg.json / vcpkg-configuration.json
├── Version.txt               # 版本号（每次构建自动递增最后一位）
├── cmake/                    # 版本/RC 资源模板（从其它工程改写）
├── task/workbff_task.html    # 原始 UI 设计参考（HTML 原型）
├── third_part/               # mtNet / mtPool / qwindowkit
└── workbff/
    ├── main.cpp              # 入口：deepLink、日志、DB、Dock+MainUI+托盘
    ├── stdafx.h              # 引入 w_log_cpp.h
    ├── CMakeLists.txt        # Qt5 组件、AUTOMOC/AUTORCC、链接、RC
    ├── model/Item.h          # 数据模型（类型/关键词/内容/优先级/加密）
    ├── db/DatabaseManager.*  # SQLite：建表/增删查/密码；DB 位于 %LOCALAPPDATA%\workbff\workbff.db
    ├── crypto/AesCipher.*    # AES-256-CBC 加解密（OpenSSL EVP）
    ├── base/deepLink.h       # scheme 注册 + 单实例管道转发
    ├── ui_module/
    │   ├── mainUI.*          # 主界面（Popup 圆角、搜索页/添加页、菜单、toast、自动关闭）
    │   ├── DockIcon.*        # 圆形 Dock 图标（跑马灯、拖拽、单击）
    │   ├── TitleBar.*        # 32px 标题栏（拖拽、菜单/关闭按钮）
    │   ├── ListItemWidget.*  # 列表行（换行自适应、hover、单击复制/双击打开/删除）
    │   ├── AddPanel.*        # 添加页（3 关键词、多行内容、类型、优先级、加密）
    │   ├── PasswordDialog.*  # 登录 / 首次设置密码对话框
    │   └── res/resources.qrc # 应用图标资源（:/app.ico）
    └── (构建生成) Version.h、workbff.rc
```

## 构建

前置要求：
- Visual Studio 2022（C++ 桌面开发）
- Qt 5.15.2 msvc2019_64（默认路径 `C:\Qt\5.15.2\msvc2019_64`，可在 `install.bat` 顶部 `QT_ROOT` 修改）
- vcpkg（默认 `D:\vcpkg`，在 `CMakePresets.json` 的 `VCPKG_ROOT`）
- Inno Setup 6（打包用）

一键构建 + 部署 + 打包：

```bat
install.bat
```

或手动：

```bat
cmake --preset workBFF
cmake --build build/windows-x64 --config RelWithDebInfo
%QT_ROOT%\bin\windeployqt.exe .\bin\RelWithDebInfo\workBFF.exe
```

产物：`bin\RelWithDebInfo\workBFF.exe`；安装包输出到 `target\`。

## 数据库

路径：`%LOCALAPPDATA%\workbff\workbff.db`（首次运行自动创建目录）

```sql
items(id INTEGER PRIMARY KEY AUTOINCREMENT,
      keywords TEXT, content TEXT,
      type TEXT,            -- text/folder/exe/http/scheme
      priority INTEGER DEFAULT 1,
      encrypted INTEGER DEFAULT 0,
      created_at TEXT)
meta(key TEXT PRIMARY KEY, value TEXT)   -- password_salt / password_hash
```

- 加密条目：`content` 存 Base64(AES-256-CBC) 密文，未登录时查询自动隐藏
- 搜索：关键词/明文内容 LIKE 匹配；搜索框为空时 `WHERE priority = 1`

## 关键交互约定

| 操作 | 行为 |
|---|---|
| 单击列表项 | 复制内容到剪贴板 |
| 双击列表项 | 文件夹→资源管理器；exe→启动；http/scheme→Shell 打开 |
| 点窗口外部 | 主界面自动隐藏（失活监听），再点 Dock 重新打开 |
| 单击 Dock/托盘 | 开关主界面 |
| 托盘右键 → 退出 | 结束进程 |
| 搜索框为空 | 只显示第 1 优先级条目 |

## 说明

- `cmake/`、`workBFF.iss`、`install.bat` 由其它工程（omni_station）改写而来，品牌已统一为 workBFF
- `qwindowkit` 已链接但未使用其无边框 API（窗口为自绘无边框 Tool 窗口）
- 版本号由 `cmake/Version.cmake` 在每次配置时自动递增 `Version.txt` 末位
