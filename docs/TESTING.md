# 验证记录

## 1.1.2（2026-10-17）

问题定位：本机安装 DLL 与正式 1.1.1 相同。固定麦克风输入 PCM 4096、共享增益 0%，原版从 50% 拖至 300% 但不应用时，输出仍为 2048、电平约 -24.1 dBFS；应用后输出 12288、电平约 -8.5 dBFS。计算位置已在增益后，交互的显示／生效时机不一致是本次确认的问题。

1.1.2 的四组 CTest 与真实回环配合模拟 TS3 回调验证通过：

- 同一输入从 50% 拖至 300%，无需点击应用，输出即从 2048 变为 12288，稳定电平从 -24.1 变为 -8.5 dBFS；0% 输出及电平立即归零。
- 共享输入在 0%、50%、100%、200%、300% 下，实际 PCM 与增益后电平一致。
- 实时音量自动保存；未确认的来源保持待定，不写入、不切换采集；隐藏再打开保留待定选择。
- 未应用项、顶部橙色提示、按钮星号与置灰状态正确；应用或还原后标记消失。正常和待定界面分别渲染检查，窗口尺寸不增加。
- 增益变化在音频块之间提交并清除旧峰值，平滑状态按增益比例同步调整；保持电平总开关与原有发送／静音规则。

GitHub [构建检查](https://github.com/MaidTendouAris/ts3-soundbridge/actions/runs/39013042919) 与 [发布工作流](https://github.com/MaidTendouAris/ts3-soundbridge/actions/runs/39013046040) 均成功；正式 [v1.1.2](https://github.com/MaidTendouAris/ts3-soundbridge/releases/tag/v1.1.2) 的附件只有安装包和校验文件。已重新下载并核对摘要、7 文件包结构和 1.1.2 版本；包内实际 DLL 的 `--gain-probe --updates` 通过，本地 dist 已同步为正式包。

```text
TS3-SoundBridge-1.1.2-win64.ts3_plugin
SHA256: 3dd7325f642fc458fe639abb832080a2e487e57d3c746787e17982f4538f37ff
```

增益探针使用真实 Windows 回环与模拟 SDK，命令为 `soundbridge_plugin_tests <DLL> --gain-probe`，不连接服务器、不保存音频。真实 TS3 频道及长期兼容性仍待实测。以下保留历史记录。

## 1.1.1（2026-10-16）

Release x64 及四组自动测试通过；真实 Windows 回环配合模拟 TS3 函数表的 `--capture` 与窗口渲染检查通过。

- 总开关缺失时默认关闭；立即保存／恢复，切换不会提交尚未应用的增益设置。关闭后隐藏电平、归零并停止高频定时器，混音跳过峰值计算及发布；关闭前后同一输入的 PCM 与发送标志一致。
- 启用后采用约 30 帧／秒刷新，指数上升／回落平滑、100 ms 短峰值保持、100 ms 数字刷新；验证稳态收敛、平滑上升／回落以及不同时间步长的一致性。
- 原始短暂过载保留 800 ms 红色提示，平滑不会隐藏该提示；持续过载仍显示正 dBFS。
- 共享中关闭显示、隐藏窗口、最小化／恢复，以及断线／关闭／重载后偏好恢复通过；暂停电平期间 300% 增益与正常音频处理保持有效。
- 圆点位于 SVG 的 (128, 128)，调整邻近曲线保留间隙，重新导出 PNG 与七尺寸 ICO；窗口保持 1.1.0 的紧凑双栏尺寸。
- 发布工作流仅上传安装包及 `SHA256SUMS.txt`，图标保留在源码及插件资源内。

GitHub [构建检查](https://github.com/MaidTendouAris/ts3-soundbridge/actions/runs/38861926903) 与 [发布工作流](https://github.com/MaidTendouAris/ts3-soundbridge/actions/runs/38861931386) 均成功，正式发布为 [v1.1.1](https://github.com/MaidTendouAris/ts3-soundbridge/releases/tag/v1.1.1)。API 确认仅上传安装包及校验文件，没有独立 SVG／PNG／ICO 附件。

已下载正式包，核对资产摘要、SHA256SUMS、7 文件结构、1.1.1 版本及居中 SVG；包内实际 DLL 的 `--capture --updates` 验证开关、最小化恢复、音频与手动更新通过。本地 dist 已同步为正式包：

```text
TS3-SoundBridge-1.1.1-win64.ts3_plugin
SHA256: 706fc977e7734e2a8efc5b1187699f856c3051648e071b7caa8f1c888da27a3c
```

以下为历史验证记录；真实 TS3 频道、PTT／VAD、长期听感与设备自动恢复仍未实测。

## 1.1.0（2026-10-16）

Release x64 编译及四组 CTest 通过。新增验证涵盖：

- 两路 300% 增益实际作用于 PCM；GUI 上限、设置保存／恢复和引擎约束均支持 300%，非法值继续回退／限幅。
- 分别测量共享合成信号与麦克风的增益后峰值；混音前的反相信号不会掩盖各自电平，保留超过满幅的峰值；单声道下混、麦克风发言门控、独立静音和零增益显示正确。
- GUI 轮询期间保留瞬时峰值，两路使用同一对数刻度，电平回落及断线归零正常。
- `soundbridge_plugin_tests --capture` 使用真实 Windows 回环和模拟 TS3 函数表，确认 300% 麦克风增益的 PCM 限幅及正 dBFS 读数、共享增益为零时电平保持静音，以及独立静音／断线归零。
- 额外用 3% 振幅的 440 Hz 短测试音驱动共享输入：共享增益 50% 时约 -36.5 dBFS；模拟麦克风 PCM 10000、增益 100% 时约 -10.3 dBFS。两条电平分别刷新；不连接服务器，不保存音频。

按用户反馈改为左右双栏：左侧来源／应用，右侧增益／电平／更新，底部共享／应用设置按钮；不再纵向堆叠。当前 1440P 环境下窗口由 988×1358 缩为 1233×877 像素，高度减少约 35%，控件与数值完整可见。新图标也随 1.1.0 安装包分发。

GitHub 的 [1.1.0 构建检查](https://github.com/MaidTendouAris/ts3-soundbridge/actions/runs/38775112876) 与 [正式发布工作流](https://github.com/MaidTendouAris/ts3-soundbridge/actions/runs/38775118752) 均成功。正式 [v1.1.0](https://github.com/MaidTendouAris/ts3-soundbridge/releases/tag/v1.1.0) 不是草稿或预发布。

重新下载正式安装包，核对 GitHub 资产摘要、SHA256SUMS、7 文件结构、安装元数据与 DLL 版本 1.1.0，确认包含新版 SVG。提取包内实际 DLL 后运行 `--capture --updates`，电平、300% 增益、GUI／音频生命周期与真实手动更新检查通过。本地 dist 的 1.1.0 同步为该正式包：

```text
TS3-SoundBridge-1.1.0-win64.ts3_plugin
SHA256: 219dbd2985441c35a259adae27f56140a9d245b94668c0582cdb51c1edcb6cfc
```

真实 TS3 频道听感、PTT／VAD、长期音质和设备自动恢复仍未实测。以下保留 1.0.0 的历史验证与包校验记录。

## 1.0.0 历史记录

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
