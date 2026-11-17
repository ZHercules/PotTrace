# PotTrace

PotPlayer 的本地播放统计扩展：AngelScript 回调与 x64 桥接器采集，独立 WPF 窗口查询，SQLite 保存历史。另有隔离验证中的 DirectShow 图内采集链路。

当前状态：**开发预览，尚未完成稳定版验收**。脚本采集仍有启动回调延迟限制；DirectShow 实验链路已接入真实统计引擎和 SQLite，在本机 EVR 配置验证首开、换片、自动下一集及详细记录，详见[图内采集验收](docs/graph-ingestion-validation.md)和[本次 develop 合并验证](docs/develop-validation.md)。实验 AX 不包含在安装包中，未部署到日用 PotPlayer。

- 精简：同文件名归并打开次数与实际播放时长。
- 详细：独立观看会话、现实时间轴、重复片段、按来源分开的高频区间及次数。
- 当前影片停留严格超过 10 秒才有效；暂停只增加停留。连续实际播放严格超过 5 秒才成为详细片段。
- 连续变速不切分播放片段；估计速度及变速观测窗口可随 JSON 时间轴导出，未知倍率不填成 1。
- 首次在 PotPlayer 勾选一次扩展，以后自动运行；支持暂停统计、全局快捷键、导出、备份、恢复、删除和目录排除。

安装和首次启用步骤见[安装说明](docs/installation.md)。发行包为 Windows x64 自包含目录，不要求另装 .NET；只包含 PotTrace，不包含 PotPlayer。

开发环境：.NET SDK 10.0.404、Visual Studio 2026 C++ 工具、Windows SDK、CMake。此机器的 SDK 位于仓库旁的 `.tools/dotnet`，构建包装脚本会使用它。

```powershell
./scripts/build.ps1
./scripts/test.ps1
./scripts/publish.ps1 -Version '0.1.0-preview.1'
```

`probes/PotPlayerProbe` 是实机验证工具；只能对隔离测试播放器运行。`tools/PotTrace.Simulator` 可生成示例数据和回放事件，模拟器通过不代表播放器兼容性通过。

设计与后续完整范围见[设计文档](docs/superpowers/specs/2026-11-16-pottrace-design.md)和[实施计划](docs/superpowers/plans/2026-11-16-pottrace-implementation.md)。当前预览未完成来源手工分组、更多播放器版本、AB/整片循环及系统休眠实机验证。
