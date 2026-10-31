# BilibiliDown (C++23 / Qt6 重写版)

基于 [NiceLeee/BilibiliDown](https://gitee.com/NiceLeee/BilibiliDown) (Java) 重写的 B 站视频下载器。

**新增核心功能：合集合并为单个 MP4** —— 多分P视频、番剧、收藏夹、UP主合集可下载全部后合并为一个 MP4 文件。

## 技术栈

- **语言**: C++23
- **框架**: Qt 6.11 (Widgets / Network / Concurrent)
- **编译器**: LLVM clang 22 (`D:/MainAppx/llvm/llvm-mingw-20260616-ucrt-x86_64`)
- **链接器**: lld (`-fuse-ld=lld`)
- **构建**: CMake + Ninja，面向 CLion

## 目录结构

```
Bilidown/
├── CMakeLists.txt            # 根构建脚本 (仿 uninstaller 工程风格)
├── main.cpp                  # 程序入口
├── appicon.rc                # Windows 图标资源
├── bin/ffmpeg.exe            # 合并/转码用的 ffmpeg (随程序自动部署)
├── assets/                   # 图标与翻译资源
├── api/                      # ★ 外置库 (静态库 libapi.a)
│   ├── HttpClient            # HTTP 客户端 (同步请求/Cookie/Range断点续传)
│   ├── Crypto                # MD5/SHA1/WBI签名/AV-BV互转
│   ├── QrCode                # 二维码生成器 (扫码登录, 无第三方依赖)
│   ├── Ffmpeg                # ffmpeg 进程封装 (合并/拼接/转封装)
│   ├── Config                # 配置持久化 (QSettings)
│   └── Logger                # 日志
├── code/                     # ★ 核心库 (静态库 libcode.a): 解析/下载/GUI
│   ├── model/                # VideoInfo / ClipInfo / DownloadTask 数据模型
│   ├── parser/               # URL 解析器 (BV/AV/SS/EP/MD/ML/UP主空间/音频/b23短链)
│   ├── downloader/           # 下载引擎 (任务队列/并发/断点续传) + 合集合并
│   ├── util/                 # 登录客户端
│   └── ui/                   # Qt Widgets 界面
└── BilibiliDown-src/         # 原 Java 版源码 (仅供参照)
```

## 构建 (CLion)

1. 用 CLion 打开本目录 (CMake 会自动使用 `D:/MainAppx/llvm/...` 的 clang 与 lld)
2. 选择构建类型 (Debug/Release) 并构建
3. 运行 `BilibiliDown` (POST_BUILD 会自动 windeployqt 部署 Qt 运行时并复制 ffmpeg)

命令行构建：

```bash
cmake -G Ninja -B build -DCMAKE_MAKE_PROGRAM="D:/MainAppx/Qt/Tools/Ninja/ninja.exe"
ninja -C build
```

## 功能说明

| 功能 | 说明 |
| --- | --- |
| 链接解析 | `BV1xx411c7mD` / `av123` / `b23.tv` 短链 / 完整 URL / 番剧 `ss|ep|md` / 收藏夹 `ml` / UP主空间链接 / 音频 `au` |
| 清晰度 | 240P ~ 8K、杜比视界，自动查询可用清晰度列表 |
| 下载格式 | MP4 (DASH 音视频分流) / FLV / 仅音频 |
| 断点续传 | `.part` 临时文件 + HTTP Range，中断后可续传 |
| **合集合并** | 勾选后，多分P/合集下载完毕自动用 ffmpeg concat 合并为单个 MP4 (流拷贝优先，参数不一致时回退重编码)，可删除中间文件 |
| 扫码登录 | 内置二维码生成器，无需第三方库；也支持手动粘贴 Cookie |
| 下载管理 | 任务队列、并发数可调 (1~8)、开始/停止/移除、实时速度与进度 |
| 任务栏托盘 | 托盘图标常驻通知区域；悬停显示下载速度/任务数；双击切换主窗口；关闭窗口可最小化到托盘 (下载不中断)，任务完成/失败弹气泡通知 (均可在设置开关) |

## 使用说明

1. **解析下载** 标签页输入链接 → 解析 → 选择清晰度/格式 → (可选)勾选"合集合并为单个MP4" → 加入下载列表
2. **下载列表** 标签页查看进度、速度，可开始/停止/移除任务
3. **设置** 标签页配置下载目录、默认清晰度、ffmpeg 路径、Cookie
4. 高清画质 (1080P+) 需要登录：菜单 **账户 → 扫码登录**

## 配置位置

- 设置与 Cookie：Windows 注册表 (`HKCU\Software\BilibiliDown\BilibiliDown`)
- 日志：`%APPDATA%/NiceLee/BilibiliDown/BilibiliDown.log`

## 第三方组件

- Qt 6 (LGPL) — https://www.qt.io
- ffmpeg (随附 `bin/ffmpeg.exe`，定制裁剪构建，支持 concat/copy 合并) — http://www.ffmpeg.org
- AV/BV 转换算法参考 [Colerar/abv](https://github.com/Colerar/abv) (MIT)
- 二维码生成基于 [Nayuki QR-Code-generator](https://github.com/nayuki/QR-Code-generator) 算法 (MIT)

原项目 Apache 2.0 协议，仅供学习交流使用。
