# 正式采集器开发增量

`PotTrace.Collector.ax` 是独立于诊断探针的 x64 DirectShow 视频透传滤镜，使用私有 CLSID `{A5092F25-09C8-46EB-9138-638E3C0B4287}`。目前仅供隔离验证，未进入安装包，也未完成稳定版验收。不要运行 `regsvr32`；全局注册入口主动拒绝注册。

## 构建

在仓库根目录先运行 `python probes/DirectShowProbe/fetch_baseclasses.py`，再运行 `scripts/build.ps1`。下载器固定到 Windows classic samples 的提交 `434f6002bdf9cf9829406c3ff2b33387982d6168`；构建前 `scripts/verify-directshow.ps1` 校验源文件、许可证和 SHA-256 清单。现有 C++20 构建对旧版 DirectShow 头文件显式使用兼容选项，未修改上游代码。

输出为 `out/collector/Release/PotTrace.Collector.ax`。生产与诊断包装器共用当前来源、连接渲染器和复合观测逻辑；诊断包装器仍可独立构建。

## 隔离配置

AX 邻接的 UTF-16 `pottrace-collector.ini`：

```ini
[PotTrace]
BridgePath=D:\isolated\player\Extension\Playback\Statistics\PotTrace.Bridge.dll
PolicyPath=D:\isolated\data\capture-policy.json
PlayerIni=D:\isolated\player\PotPlayerMini64.ini
Diagnostics=0
```

三个路径必须是本地绝对路径；PlayerIni 必须对应当前进程可执行文件。Bridge 邻接的 `pottrace-bridge.ini` 须含 `SourceMode=DirectShow`、`CaptureBackend=Collector`，并配置隔离宿主、数据目录与 channel。测试工具负责生成私有滤镜覆盖配置，退出后恢复副本原文件；安装及所有权回退机制仍属后续任务。

唯一采集授权文件为 UTF-8 `capture-policy.json`：

```json
{"Enabled":true,"RecordingPaused":false,"Revision":1}
```

没有文件、格式错误、未知字段或插件未勾选时停止采集。`--collector` 宿主即使首次缺文件也保留策略存储入口；在设置中勾选“启用播放统计”，或者点击“启用统计”，即可创建策略。修改使用递增版本、串行写入及原子替换；原生读者允许删除共享。旧脚本预览未启用 collector 模式时保持原行为。

## 生命周期与边界

- 滤镜只透传视频。源文件查询、外部 COM 查询、配置读取及 Bridge 加载放在后台；无需等待脚本报告影片身份，也不依赖 `POTTRACE_*` 测试环境变量。
- 每个播放器进程固定四个查询线程和一个控制线程，同时最多观察两个图。查询或最终 COM Release 阻塞时占用原槽，不强杀线程、不无限创建替代线程。模块固定到播放器退出。
- 只在槽位接纳后分配绑定编号，过期 generation 的查询不能发布。退图通过独立终止邮箱及时关闭旧绑定，不等待阻塞查询；终止邮箱耗尽时抑制采集，不能假定关闭成功。
- 控制线程约每 25ms 检查策略及 PotPlayer 的真实扩展勾选状态；操作系统调度和文件访问可能增加延迟，因此这不是用户操作到截止时间的精度承诺。关闭期间不回填；重新启用从新观察开始。主动退出宿主抑制当前播放器实例重启宿主；下次启动播放器重新按授权加载。
- 暂停影片计入已确认停留，不增加实播时长。队列中的文件不作为当前影片。未知载入区间、复合查询超过 50ms 或来源不一致时不补算。
- `Diagnostics=1` 写固定大小的 `collector-health.json` 与临时文件，包含时间、线程/占用槽、异常/拒绝/丢失计数、开关状态，不写影片路径。默认关闭；这不是用户可见的完整质量诊断界面。

## 验证

`scripts/test.ps1` 包含激活、生命周期、策略并发保存和桥接终止边界回归。真实播放器使用 `probes/DirectShowProbe/run_collector_probe.py`；它只允许 `out/player`，检测其他 PotPlayer 实例以防 `/current` 命令误发。

```powershell
python probes/DirectShowProbe/run_collector_probe.py --controls --ui-controls --switches 100 --restarts 3
python probes/DirectShowProbe/run_collector_probe.py --first-enable --auto-next --controls --ui-controls --restarts 3
```

本机工具需 Python 3.12，以及 `.tools/python` 中的 psutil、pywinauto、imageio-ffmpeg；先准备已配置播放统计扩展的独立播放器副本。每轮生成媒体、数据库、配置及结果，失败不覆盖。`--baseline` 为无 AX 资源对照。通过这些检查不代表 100ms 精度、预加载所有配置、第三方永久阻塞、睡眠或 24 小时稳定门槛通过，结果始终包含 `deployment_eligible=false`。

当前 Bridge/宿主仍为配套构建的预览通信协议和数据库。版本握手、稳定库迁移、正式安装回退、用户界面质量状态与大历史性能尚未实现，详见 [验收报告](../../docs/stable-validation.md)。

启动阶段现在单独保存 `startup-<尝试编号>-<PID>.json`，在清理前记录退出码、窗口与模块快照；启动失败不会自动重试成成功。早期一次退出仍未定因，参见 [专项调查](../../docs/startup-exit-investigation.md)。
