# TS3 SoundBridge

<img src="assets/icon-256.png" width="88" alt="TS3 SoundBridge">

Windows x64 的 TeamSpeak 3 实时音频共享插件。**1.1.0** 提供原生中文设置窗口，可以共享所有非当前 TS3 进程树的声音，或同时共享多个指定应用；再与麦克风混音发送。两路音量各支持 0–300%，分别显示增益后的实时峰值电平。支持自动及手动更新检查，通过 GitHub Releases 分发，源码和图标采用 [MIT 协议](LICENSE)。设置调整全部通过 GUI 完成。

本机已通过构建、GUI、混音、进程排除与多应用捕获测试。真实 TS3 频道的发送效果、听感和音质仍需验证。用户当前使用**持续发送**，插件保留 TS3 原有麦克风发言规则。

## 安装与使用

1. 从 [GitHub Releases](https://github.com/MaidTendouAris/ts3-soundbridge/releases/latest) 下载 `TS3-SoundBridge-1.1.0-win64.ts3_plugin`，**完全退出 TS3** 后安装。文件关联未生效时，使用 TS3 安装目录的 `package_inst.exe` 打开。`SHA256SUMS.txt` 可核对文件完整性。
2. 重启 TS3，启用 TS3 SoundBridge，连接测试频道。每次加载插件时共享默认关闭。
3. 从“插件 → TS3 SoundBridge → SoundBridge 设置…”打开窗口；插件管理页面的“设置”按钮也可打开。
4. 选择声音来源，调整音量与麦克风独立静音，点击“应用设置”。
5. 点击“开始共享”，由频道听众确认共享声音与麦克风的实际效果。“停止共享”只停止插件混音，麦克风恢复按 TS3 自身设置工作。

手动安装时，把包内 `plugins` 下的 DLL **和同名资源文件夹**一起放到 TS3 插件目录。资源包含菜单图标和许可证；窗口 ICO 嵌入 DLL。插件无需虚拟声卡、额外服务或录音文件。

## 设置窗口

窗口采用紧凑双栏：左侧选择来源和应用，右侧调整两路增益、观察实时电平并检查更新，底部集中放置共享与应用设置按钮。

| 设置 | 行为 |
| --- | --- |
| 共享所有非 TeamSpeak 的声音 | 沿用原有模式，排除当前 TS3 进程及其子进程。 |
| 只共享选定应用 | 勾选多个应用同时共享，包含各应用的子进程；不共享未选应用的声音。 |
| 刷新应用 | 枚举活动输出设备上的音频应用。先打开应用并播放声音，再刷新、勾选。 |
| 共享声音 / 麦克风音量 | 各自支持 0–300%；默认分别为 50% 和 100%。 |
| 两路峰值电平 | 共享期间每 50 ms 刷新，分别显示增益后的共享声音与麦克风，使用相同的 -60 至 0 dBFS 刻度及数值。 |
| 麦克风独立静音 | 关闭插件混音中的麦克风，共享应用声音继续发送。 |
| 显示运行详情 | 显示音频源、采集帧、回调、积压、丢弃与修剪，用于排查。 |
| 应用设置 | 保存并立即应用；正在共享时更改来源会在后台重新建立采集。 |
| 开始 / 停止共享 | 控制当前服务器连接的音频共享。 |
| 关闭 | 隐藏设置窗口；共享继续运行。 |
| 每天自动检查更新 | 默认启用，可取消勾选并应用设置。 |
| 检查更新 | 立即在后台读取 GitHub 最新正式 Release。 |
| 下载 / 发布页 | 打开官方发布页，获取安装包。 |

选定应用按**完整可执行文件路径**保存，包含该程序的所有运行实例与子进程，不区分浏览器标签页或单个窗口。父子进程树重叠时只采集一次，避免声音翻倍。最多同时捕获 16 个独立进程树；超限会报错并停止。所选应用暂未运行时等待它启动，退出后停止该应用的采集，重启后自动重新关联；不会退回共享全部声音。程序更新后路径改变时需要重新选择。

电平条测量两路信号各自的峰值，位置在应用增益、麦克风发言门控／独立静音之后，最终混音及 PCM 限幅之前。多应用声音先相加，单声道时按实际输出下混；双声道时取两声道的较高峰值。绿色低于 -12 dBFS，黄色接近满幅，红色达到或超过 0 dBFS；超过满幅时数值保留正 dBFS，便于降低增益。两路相加也可能导致最终混音过载。电平瞬时上升、缓慢回落；停止、断线或切换服务器后归零。未开始共享时，插件不提供麦克风预览。

模式、应用、两路音量和独立麦克风静音保存到 TS3 配置目录的 `TS3-SoundBridge.ini`，支持中文路径；共享开关不保存。插件菜单还提供开始／停止，可在 TS3 中绑定开始／停止、麦克风独立静音和打开设置三个热键。

TS3 自带麦克风静音会**停止全部共享**。断线、切换服务器标签、禁用插件也停止共享；取消静音或重连后需手动开始。插件不修改 TS3 的持续发送、VAD 或 PTT 设置。首次采集后 DLL 会保留映射至进程结束，保护 Windows 异步激活回调，因此替换 DLL 前必须完全退出 TS3；禁用插件仍会停止线程并释放设备。

## 更新

加载后等待约 15 秒，再根据保存的检查时间判断是否需要检查；插件运行时每天检查一次。手动检查不受每日间隔限制，并更新检查时间，避免随后重复自动请求。发现新版本会在当前 TS3 标签本地提示，GUI 显示版本和下载入口；同一版本在一次加载期间只提示一次。

只接收本仓库的正式 Release 和命名匹配的 Windows x64 安装包；草稿、预发布、错误响应和不匹配的链接不会作为可用更新。网络错误或 GitHub 限流会显示重试提示。请求在独立线程运行，不经过音频回调。检查只读取公开版本信息，不上传音频或 TS3 连接信息。下载与安装由用户完成，安装前完全退出 TS3。

## 图标与许可证

[`assets/icon.svg`](assets/icon.svg) 是唯一设计基准。构建时使用 Windows Direct2D 的 SVG 渲染器生成 16、24、32、48、64、128、256 像素 PNG 与多尺寸 ICO，输出到 `build/icons`；ICO 嵌入窗口资源，32 像素 PNG 用于 TS3 插件菜单。

运行 `scripts/export-icons.ps1` 可更新仓库内 PNG／ICO 预览。图标生成无需 Python、图片生成服务或额外图形库。项目采用 MIT，依赖声明见 [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt)，安装包包含两份许可证文件。

## 兼容性与限制

- Windows build 20348+、TS3 x64、插件 API 26。本机环境：Windows 26300.9457、TS3 3.6.2 x64；真实频道测试未进行。
- 麦克风按音频回调进入时的原始发送标志门控。当前持续发送是优先实测目标。PTT 关闭时 TS3 可能停止捕获及回调，插件无法保证这段时间持续发送共享音频；VAD 也待真实客户端确认。
- 捕获 48 kHz 双声道 PCM16，支持 TS3 回调单／双声道。多个源在浮点域求和后限幅，麦克风只混入一次。频道编码仍可能限制音乐质量和立体声。
- 有界缓冲、缺帧补零、积压修剪与溢出恢复已实现。平滑时钟漂移补偿、柔和限幅、设备自动恢复和长时间听感尚未验证。
- 优先单 TS3 实例、耳机场景和普通 Windows 播放音频。受保护内容、独占输出、ASIO、多实例 TS3、扬声器回声处理未测试。

## 构建与验证

需要 VS 2022 C++ 桌面开发工具、CMake 3.22+ 和 Windows SDK 20348+。构建脚本获取固定版本的 [TeamSpeak 官方 Plugin SDK](https://github.com/teamspeak/ts3client-pluginsdk/tree/4aa90a53aa150cbf81e13bc97e68c0431b26499f) 与 [nlohmann JSON 3.12.0](https://github.com/nlohmann/json/releases/tag/v3.12.0)，JSON 头文件通过官方 SHA-256 校验。SDK 头文件保留版权，不打入安装包。

```powershell
cd D:\github\ts3-soundbridge
.\scripts\build.ps1
.\scripts\package.ps1
.\scripts\export-icons.ps1
```

默认 SDK 路径为 `.deps/ts3-sdk`，可用 CMake 的 `TS3_SDK_DIR` 指定已有 SDK；构建脚本的 `-WindowsSdkVersion 10.0.26100.0` 可指定 Windows SDK。产物在 `build/Release` 与 `dist`，MSVC 运行库静态链接。

构建自动运行音频核心、应用选择／设置保存、更新检查、插件 ABI／GUI 四组测试。下列音频测试使用真实 Windows 捕获与模拟 TS3 函数表，不连接服务器，不保存音频；音频测试会短暂播放 3% 振幅的测试音。

```powershell
# GUI 操作、共享模式切换、静音／断线／标签切换停止与重载
.\build\Release\soundbridge_plugin_tests.exe .\build\Release\ts3_soundbridge_win64.dll --capture

# 原有模式：捕获兄弟进程，排除目标进程
.\scripts\test-loopback.ps1

# 两个指定应用同时捕获，第三个未选应用排除
.\scripts\test-selected-apps.ps1

# 指定应用退出／重启后重新关联
.\scripts\test-app-restart.ps1

# 真实 GitHub Releases API；需要联网
.\build\Release\soundbridge_update_tests.exe --live
```

测试记录与频道实测清单见 [docs/TESTING.md](docs/TESTING.md)，需求和交接见 [TS3-SoundBridge-PROJECT.md](TS3-SoundBridge-PROJECT.md)。

GitHub Actions 对 main 和 PR 构建、测试并保留安装包；推送与版本匹配的 `vX.Y.Z` 标签后，Release 工作流重新构建和测试，发布安装包、校验值及 SVG／PNG／ICO。发布说明位于 `docs/releases/X.Y.Z.md`。已发布安装包不会被工作流覆盖。
