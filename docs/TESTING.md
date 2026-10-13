# 验证记录

版本：1.0.0；日期：2026-10-13。环境：Windows 26300.9457；VS 2022 / MSVC 19.44；Windows SDK 10.0.26100.0；Release x64。已安装 TS3 3.6.2 x64，但尚未进行真实客户端频道测试。用户发言模式为持续发送。

## 已通过

Release 构建成功，API 26，Windows `LoadLibrary` 可加载 DLL；静态 MSVC 运行库。移除 `ts3plugin_commandKeyword`、`ts3plugin_processCommand`，提供配置窗口导出、菜单和热键。

CTest 四组通过：

- `audio_core`：麦克风原始发送门控、独立音量／静音、单／双声道、饱和限幅、缺帧补零、积压修剪、溢出恢复与 20 万帧并发顺序；多个应用求和后限幅，麦克风只混入一次。
- `app_selection`：路径大小写匹配、多实例、父子树去重、拒绝 TS3 本身／子进程／祖先进程、未运行应用不回退到全部声音、16 独立进程树上限、中文路径保存与加载、非法音量回退、验证失败停止。
- `plugin_contract`：模拟 TS3 函数表验证初始化／菜单 ABI、命令入口移除、未共享音频原样返回；打开 GUI、模式切换、多选、空选择拒绝、滑块设置、保存、重载恢复与默认不启动。测试只操作测试进程自己创建的窗口。
- `update_checker`：数字版本比较、相同／更旧版本、非法版本号、草稿／预发布、缺少安装包、不匹配下载地址、HTTP 错误／限流、JSON 错误／大小限制、24 小时检查间隔、自动开关、手动后台检查、单请求并发、通知去重和时间保存。

1.0.0 同时检查窗口 ICO、菜单 PNG 路径和 GUI 自动检查偏好保存。SVG 经 Direct2D 渲染为七种 PNG 尺寸及 ICO；窗口标题、设置页和插件菜单使用同一设计。安装包版本与 DLL 产品版本一致，并包含 MIT 和第三方声明。

正式 Release 发布前真实 WinHTTP 检查返回 `NoRelease`，与“当前最新”正确区分。发布后返回 `Current`，版本为 `1.0.0`，发布页为本仓库 `releases/tag/v1.0.0`。GUI 手动更新按钮的真实联网验证通过。

GitHub 的 [main 构建检查](https://github.com/MaidTendouAris/ts3-soundbridge/actions/runs/38359537770) 与 [Release 发布工作流](https://github.com/MaidTendouAris/ts3-soundbridge/actions/runs/38359538379) 均成功。正式发布为 [v1.0.0](https://github.com/MaidTendouAris/ts3-soundbridge/releases/tag/v1.0.0)，不是草稿或预发布。

已重新下载正式安装包，核对资产摘要及 `SHA256SUMS.txt`，确认包内 7 个文件；从包中提取实际 DLL，运行 `--capture --updates`，GUI、音频生命周期和手动更新全部通过。该检查使用模拟 TS3 函数表，没有连接频道。

```text
TS3-SoundBridge-1.0.0-win64.ts3_plugin
SHA256: dd8e335b7f2f623be3e96f20224b31357fe18415865acc314cb006e9e2981743
```

真实 Windows 采集结合模拟 TS3 函数表的 `--capture` 测试通过：GUI 开始共享；共享中切换到未运行的选定应用并等待，再切回全部声音；麦克风门控、独立静音、其他服务器隔离、关闭窗口继续共享；TS3 静音、切换标签、断线立即停止；关闭后重载。没有连接服务器，没有调用 TS3 发言配置修改函数。

原生窗口已渲染检查：本机高 DPI 下应用列表、音量、状态和操作按钮完整可见；主题控件生效。预览在 `build/settings-preview.png`，其中的两个播放器为测试配置，非实际应用。

## 真实音频捕获

`scripts/test-selected-apps.ps1` 启动三个独立可执行文件的测试进程，分别播放 440 / 997 / 1499 Hz，PCM 振幅 3%。只选前两个，通过真实捕获、混音回调及短 Hann 窗频谱统计验证：

```text
READY selected sources=2
frames=288000 sources=2
amplitude440=0.0295454573
amplitude997=0.0294832688
amplitude1499=0.0000021507582
dropped=0 trimmed=0
```

两个所选分量均超过 0.003；未选分量低于任一所选分量的 10%。此项通过，未保存音频。

`scripts/test-app-restart.ps1` 验证同一完整应用路径在退出／重启时的关联：

```text
WATCH waiting
WATCH sharing
WATCH waiting
WATCH sharing
WATCH waiting
transitions=5
```

从等待、启动捕获、退出释放到新进程重新采集均通过。共享请求保持开启，未运行时没有退回全部声音。

原有模式的 `scripts/test-loopback.ps1` 回归通过：目标进程播放 440 Hz，自身被排除；兄弟进程播放 997 Hz，被捕获。五秒结果：

```text
frames=237600
amplitude440=0.00000345919755
amplitude997=0.0179788973
dropped=0 discontinuities=0 HRESULT=0x0
```

0.1.0 阶段另有三十秒采集健康检查通过：1,438,560 帧，无丢帧或不连续。这些本机测试均不等同于 TS3 频道的发送效果或长期听感测试。

## 频道待实测

| 场景 | 方法与通过条件 |
| --- | --- |
| 持续发送 | 耳机、测试频道，播放音乐并说话；听众同时听到两路声音，独立音量有效。 |
| 选定应用 | 同时勾选播放器和浏览器，另开一个未选应用播放；听众只听到所选声音和允许发送的麦克风。 |
| 排除 TS3 | 其他成员发言、触发 TS3 提示音；没有数字回传。 |
| 静音与停止 | GUI 独立麦克风静音继续共享；TS3 自带静音停止全部共享；取消后不自动重启。 |
| 断线／切换标签 | 立即停止，不向其他连接传送；重连后需手动开始。 |
| 语音激活 | 不说话时共享声音连续，麦克风只在原有规则允许时传送。 |
| 按键说话 | 松开按键，开启 GUI 运行详情观察回调计数；当前无法保证共享声音连续。 |
| 编码与立体声 | 默认语音和 Opus Music 频道分别记录 codec、quality、声道与听感。 |
| 长时间 | 连续 30–60 分钟，观察积压／丢弃／修剪和实际听感。 |
| 设备与休眠 | 插拔耳机、切换设备、休眠恢复没有崩溃；设备自动恢复尚未实现。 |
| 端到端延迟 | 比较发送端与接收端脉冲／节拍，记录网络、编码和测量方法。 |

## 排查与接口依据

- GUI 提示等待：所选路径尚未找到运行进程；先启动应用，刷新确认路径。应用更新导致路径变化时重新选择。
- GUI 显示错误及 HRESULT：检查系统版本、音频设备与服务，手动重新开始。
- 采集增长、回调不增长：Windows 捕获正常，但 TS3 未进入混音回调，可能与 PTT 停止捕获有关。
- 回调增长、听众听不到：检查频道权限、客户端发送控制与编码路径。
- 积压修剪增长：回调停顿或时钟差异，平滑漂移处理待实现。

依据为 [微软进程回环示例](https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/)、[包含／排除进程树参数](https://learn.microsoft.com/en-us/windows/win32/api/audioclientactivationparams/ne-audioclientactivationparams-process_loopback_mode)、[音频会话进程标识](https://learn.microsoft.com/en-us/windows/win32/api/audiopolicy/nf-audiopolicy-iaudiosessioncontrol2-getprocessid)、[TeamSpeak 官方插件示例](https://github.com/teamspeak/ts3client-pluginsdk/blob/4aa90a53aa150cbf81e13bc97e68c0431b26499f/src/plugin.c) 和 [官方发言常量](https://github.com/teamspeak/ts3client-pluginsdk/blob/4aa90a53aa150cbf81e13bc97e68c0431b26499f/include/teamspeak/public_definitions.h)。

Windows 异步激活没有取消接口，插件首次采集后保留模块映射至进程结束，保护可能迟到的完成回调；禁用仍释放设备和线程，更新 DLL 必须退出 TS3。配置保存依据 [WritePrivateProfileStringW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-writeprivateprofilestringw)：缓存刷新返回零不代表写入失败，保存结果按实际字段写入判断。
