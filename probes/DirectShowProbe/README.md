# DirectShow 隔离取证原型

这不是安装包。默认只记录诊断；`--ingest` 会在本次 `out/graph-probe-*/data` 中启动真实宿主并写入实验历史，退出后恢复副本配置和桥接文件。**通用兼容性及部署验收尚未完成，禁止部署到正式播放器**。来源原型见 [验证报告](../../docs/directshow-validation.md)，在线链路见 [接入进展](../../docs/graph-ingestion-validation.md)。

## 范围与隔离

- 仅允许 `out/` 下的播放器；运行前拒绝已有其他 PotPlayer 主窗口，防止 `/current` 发错实例。
- 使用合成的静音 AVI 或 H.264/AAC MP4/MKV。MP4/MKV 当前集红色、下一集蓝色；同名影片位于不同目录。
- 临时修改副本 INI 的私有滤镜 Override 和桥接配置，退出后按原始字节恢复。无系统 COM 注册；DLL 注册入口直接返回拒绝。
- 文件依赖位于忽略的 `out/deps/`；Python 工具依赖位于工作区 `.tools/python`。不修改用户 WebView2 目录权限。
- 当前图内模块固定到进程退出，后台日志线程随进程结束；这是取证原型的生命周期，不能直接作为产品实现。

## 复现

本机使用 MSVC 19.51 / CMake Visual Studio 18 2026 / Windows SDK / Python 3.12。Python 依赖：`imageio-ffmpeg`（本次 0.8.1）、`psutil`（7.2.2）、Pillow；UI 配置辅助脚本另需 pywinauto / pywin32。工具路径沿用本仓库已有本机探针约定。

```powershell
python probes/DirectShowProbe/fetch_baseclasses.py
cmake -S probes/DirectShowProbe -B out/directshow-probe -G "Visual Studio 18 2026" -A x64
cmake --build out/directshow-probe --config Release

python probes/DirectShowProbe/run_graph_probe.py --format avi
python probes/DirectShowProbe/run_graph_probe.py --format mp4 --baseline --error-reopen
python probes/DirectShowProbe/run_graph_probe.py --format mp4 --preload --held-preload resume
python probes/DirectShowProbe/run_graph_probe.py --format mp4 --preload --held-preload cancel
python probes/DirectShowProbe/run_graph_probe.py --format mkv --error-reopen

# 先构建 Debug bridge 和真实 WPF host。详细采集写入独立 SQLite；
# 覆盖暂停停留、重复片段、排队排除、2倍速、同名切换及自然下一集。
python probes/DirectShowProbe/run_graph_probe.py --format mp4 --ingest --detailed-flow --preload --held-preload resume --presentation

# 可选：另一个已准备好的隔离版本，仍须在 out 内。
$env:POTTRACE_TEST_PLAYER = (Resolve-Path out/player-current/PotPlayerMini64.exe).Path
python probes/DirectShowProbe/run_graph_probe.py --format mkv --preload --error-reopen
```

运行前须准备本仓库既有隔离播放器副本及诊断桥接。程序不会从正式安装目录自动复制或安装插件。每次运行输出 `out/graph-probe-<id>`，内含原始图事件、脚本事件、外部状态查询、测试配置及结果。带颜色验证的用例仅截图所属播放器客户区。

```powershell
python probes/DirectShowProbe/summarize_probe.py out/graph-probe-<id>
```

## 如何读证据

- `upstreamSource/upstreamResult`：从本滤镜实际输入连接的对端获取 `IFileSourceFilter`。只有已连接、调用成功且非空的路径可作为这条输入流的来源。若对端不提供该接口，就保留未知，不猜测。
- `sources/uncertain/enumEnd`：遍历整个图所得的诊断结果。实测 PotPlayer 的枚举在末尾返回 `E_FAIL`，必须保持 `uncertain=true`，不能拿遍历到的唯一文件冒充完整来源列表。
- `videoWindows/windowCapabilities`：渲染器的窗口及标准接口支持情况。原版副本 EVR 可返回归属；新版副本内置 Direct3D11 渲染器返回 `E_NOINTERFACE`。不能用进程 ID 或窗口标题补猜当前图。
- `at`：滤镜边界入口处的 `GetTickCount64`；`inspectionStartedMs/inspectionEndedMs` 包围实际同步查询。它不是播放器公开确认的“影片载入时刻”，也不能把区间内取得的来源回填到更早时间。
- `FirstSample/NewSegment`：只记录流时间，不访问图、不读写样本数据、不落盘。首样本可能属于预滚，不等于实际播放开始。
- `lost`、进程退出码、强杀标志、已使用实例缺失 Destroy 都参与基础验收。日志有界，尾部在析构中限时排空，禁止在 DllMain 等待。
- `basic_probe_passed` 只表示本次指定动作和来源检查通过；`deployment_eligible` 始终为 false。之前运行的原始证据可能早于这两个字段，不能仅看播放器退出码判断整个用例通过。
- 红/蓝画面与已打开文件句柄是测试的独立旁证。文件句柄只证明提前打开，不能证明下一集已经解码就绪；Windows 句柄枚举缺失也不能证明未打开。截图不是产品的身份来源。

暂停用例会等待超过 11 秒。原生 Pause→Run 边界包含截图和采样耗时，因此实际暂停可能长于 11 秒；不能把固定 sleep 时长当作统计真值。外部位置查询有缓存，刚暂停后仍可能更新一次；检查稳定后的区间，不能把缓存更新算成暂停时播放。

## Additional online checks (experimental)

`run_graph_probe.py --format mp4 --ingest --startup-only --suspend-probe` suspends only its owned isolated player for eight seconds and checks that unknown time does not qualify a short opening. It does not suspend Windows.

`run_graph_probe.py --format mp4 --ingest --startup-only --rate-transition-probe` exercises live 1x/2x/1x transitions. This currently **fails** the detailed rate-boundary assertion; it must not be presented as a deployment pass.

`review_recorded_history.py out/graph-probe-<id>` opens a SQLite backup of a completed detailed online fixture in the real WPF UI and validates same-name summaries, chronological repeated segments, source-specific heat and source selection. It does not edit the original evidence database.
## 注册表配置迁移评估

在日用账户离线运行下列命令。它只读取当前用户的 PotPlayer 注册表项，向用户临时目录写入一次试验 INI 并在结束时移除。返回非零表示有差异或原生映射待验证的字段。传入现有 INI 路径还可比较已有副本。输出只包含计数，测试描述只包含源账户 SID、播放器文件摘要和渲染器编号。

```powershell
$playerDir = Split-Path (Read-Host '请输入 PotPlayerMini64.exe 的完整路径')
python probes/DirectShowProbe/registry_profile.py $playerDir --trial --fixture-description out/registry-fixture.json
```

在**独立 Windows 测试账户的桌面**运行同版本安装实播。为避免复用日用设置，脚本在源账户执行时会提前退出。提供已明示勾选测试扩展的便携夹具和实际发行包。探针只为临时副本调整解压后安装器的版本检查，输出 `out/installed-collector-*/acceptance.json`。测试描述只重现渲染器选择，完整偏好迁移还需单独验证。

```powershell
python probes/DirectShowProbe/verify_installed_package.py --package artifacts/PotTrace-0.1.0-preview.4-win-x64.zip --fixture .worktrees/implementation/out/player-current --registry-fixture out/registry-fixture.json
```

### 日用注册表配置下的预加载实播核对

`verify_confirmed_playback.py` 用已安装的正式滤镜和详细统计数据库核对真实播放。它在两个目录生成同名的红、蓝影片。第一轮在 A 暂停期间确认 B 文件已打开，随后直接关闭播放器。第二轮从 A 自然切到 B，将 B 的开头播放两次。报告把屏幕颜色、文件句柄、播放器位置、单调时钟和 SQLite 片段分开保存。加上 `--equal-duration` 可以让两集连时长也相同。

```powershell
$playerDir = Split-Path (Read-Host '请输入 PotPlayerMini64.exe 的完整路径')
python probes/DirectShowProbe/verify_confirmed_playback.py --player (Join-Path $playerDir 'PotPlayerMini64.exe') --equal-duration
```

执行前退出播放器并启用详细统计。该命令会在当前 Windows 账户临时开启连续播放，结束时恢复原值并核对渲染器。生成的验收会话留在日用数据库中，文件名以 `confirmed-preload-` 开头。每轮结果单独写入 `out/confirmed-preload-*/result.json`。
