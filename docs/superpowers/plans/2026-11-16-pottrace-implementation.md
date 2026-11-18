# PotTrace 开发实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. If the user selects delegated execution, use superpowers:subagent-driven-development instead. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现首次启用后随 PotPlayer 自动运行的播放统计插件，提供精简汇总、详细观看时间轴与高频区间。

**Architecture:** AngelScript 接收播放器事件，经显式加载的 x64 原生桥接 DLL 传至同一登录会话中的单实例后台。后台承载纯统计引擎、SQLite 存储和 WPF 窗口；关闭窗口不停止统计。原生桥接路线须先通过任务 1 验证。

**Tech Stack:** C#、.NET 10 LTS、WPF、Microsoft.Data.Sqlite、xUnit；AngelScript；C++20、MSVC、CMake、Windows 命名管道。发布采用 win-x64 自包含目录包，不要求用户另装 .NET。

**Spec:** [产品与技术设计](../specs/2026-11-16-pottrace-design.md)。执行者必须同时阅读两份文档。

## Global Constraints

- 首次启用后随 PotPlayer 自动运行；统计窗口不需要每次手动打开。
- 当前影片停留严格大于 10 秒才有效；播放与暂停均计入停留，队列中尚未成为当前媒体的条目不计入。
- 详细片段需实际播放严格大于 5 秒。
- 重复观看重复累加，暂停不增加累计时长、片段播放时长或热度，仅参与打开停留是否超过 10 秒的判断。
- 同名文件合并次数与时长；热度默认按实际来源计算；文件名包含扩展名并使用 OrdinalIgnoreCase 比较。
- 采集模式与查看方式独立；模式更改从下一会话生效；精简模式保留 Session 和 SessionDay，不永久保存片段时间轴。
- 首版目标为 Windows 10/11、64 位 PotPlayer、本地视频和文件共享视频；最终支持的具体 Windows 构建由任务 1 与发布验证记录。
- 所有数据默认保存在本机用户目录；不上传观看记录，不改视频，不注入进程，不依赖播放器内存偏移。
- 用户主动暂停统计持久生效；用户主动退出与崩溃重启采用不同处理；后台对最后一个播放器退出有 3 秒宽限期。
- 当前工作范围是实施方案设计。本计划中的任务和验证均尚未执行，不以文档中的预期结果代替测试结果。

## Review Focus

1. 无媒体启动、多个播放器同时启动以及初始化重复回调：后台只启动一次，未打开媒体不计观看；任务 1、3 验证。
2. 中文、空格、引号、长路径、不同目录的同名不同版本视频：通信与启动不损坏路径，热度不跨版本混算；任务 2、3、6、10 验证。
3. 系统时钟回拨、休眠、跨午夜、倍速切换与缓冲：不产生负时长或虚增；任务 4、5 验证。
4. 磁盘满、数据库忙、损坏、事件重发与后台崩溃：不确认尚未持久化的数据，不重复计次；任务 2、6、9 验证。
5. 暂停统计后重启、用户主动退出、数据删除与仍在重试的旧事件：不自动解除暂停，不自动拉起被主动关闭的后台，不复活已删除历史；任务 3、6、9 验证。

## 开发基线与接入门槛

仓库当前只有 README 与设计文档，没有可复用业务代码。本机注册表记录 PotPlayer 26.04.22.0，目录 `E:\pot\PotPlayer`；其 `Extension/api.txt` 包含 DLL 调用接口，但没有播放统计回调与 HostCreateThread 的明文说明。已发现 Visual Studio Community 2026 和 MSVC；`dotnet --list-sdks` 未返回 SDK，已安装运行时为 .NET 7.0.20。实施前安装 .NET 10 SDK并固定实际可用补丁版本，不复用过时运行时作为开发基线。.NET 10 的 LTS 状态依据 [Microsoft 支持策略](https://dotnet.microsoft.com/en-us/platform/support/policy/dotnet-core)。

任务 1 交付运行时能力矩阵后，才能冻结正式插件入口、回调参数单位及脚本字符串到原生指针的转换。纯统计引擎、数据库及模拟器可使用冻结的内部协议独立开发；接入失败不能用模拟器的通过结果宣布插件可用。若自动初始化或关键播放事件不可用，先修订接入设计，保留用户已确认的自动运行目标。

## 文件边界

以下路径均相对于仓库根目录；各目录的职责不得交叉依赖。Core 不引用 WPF、SQLite 或 PotPlayer API。

| 路径 | 职责 |
| --- | --- |
| `PotTrace.sln`、`global.json`、`Directory.Build.props`、`Directory.Packages.props` | 解决方案、SDK 和统一编译/包版本 |
| `probes/PotPlayerProbe/` | 一次性接入验证脚本、DLL 源码及操作手册 |
| `docs/compatibility/potplayer-26.04.22.md` | 实测能力、事件序列、单位和精度结果 |
| `src/PotTrace.Contracts/` | 协议 DTO、枚举、验证及序列化 |
| `src/PotTrace.Core/` | 会话、片段、时长、日分配、身份与热度算法 |
| `src/PotTrace.Storage/` | 数据库迁移、事务、恢复与历史操作 |
| `src/PotTrace.App/` | 后台宿主、IPC、生命周期、WPF 与托盘 |
| `native/PotTrace.Bridge/` | ABI、即时复制入队、非阻塞启动与通信 |
| `plugin/PlaybackStatistics - PotTrace.as` | 宿主回调适配；不含统计和数据库逻辑 |
| `tests/PotTrace.*.Tests/`、`tests/native/` | 对应模块行为测试、原生桥接测试 |
| `tools/PotTrace.Simulator/` | 事件场景回放与故障注入，不伪装真实播放器 |
| `scripts/`、`packaging/` | 构建、发布、受控安装卸载及包清单 |

## 共享接口约定

任务 2 在 `src/PotTrace.Contracts/PlaybackEvent.cs` 定义 `PlaybackEvent`：`ProtocolVersion:int`、`InstanceId:Guid`、`MediaEpoch:Guid?`、`Sequence:long`、`Kind:PlaybackEventKind`、`ObservedAtUtc:DateTimeOffset`、`MonotonicMs:long`、`SourcePath:string?`、`PositionMs:long?`、`DurationMs:long?`、`State:PlaybackState`、`Rate:double?`、`Quality:ObservationQuality`。所有时间单位为毫秒；null 表示未知，禁止用 0 冒充未知位置。

`PlaybackEventKind` 为 InstanceStarted、MediaOpened、Snapshot、Paused、Resumed、Seeked、RateChanged、BufferingStarted、BufferingEnded、Completed、Stopped、MediaClosed、Suspended、Gap、InstanceExited。`PlaybackState` 为 Unknown、Playing、Paused、Buffering、Stopped；`ObservationQuality` 为 Observed、Estimated、Gap。媒体生命周期由适配器分配 MediaEpoch；不能用路径相同推断为同一打开。

桥接层输出的是经目标版本验证的观察结果。不可靠的位置只允许标记 Estimated 或 Gap，统计引擎不能跨 Gap 插值。异常事件和未知协议版本在进入 Core 前拒绝并记录脱敏错误。

任务 4 在 `src/PotTrace.Core/Models/` 定义 `CaptureMode`、`EngineUpdate`、`SessionRecord`、`SessionDayRecord`、`SegmentRecord`、`TimelineEventRecord`。`EngineUpdate` 包含需要 upsert 的会话和日记录、追加片段/状态事件、实例恢复检查点，不直接操作数据库。`SessionId` 为独立 GUID，关联 `InstanceId + MediaEpoch + CaptureGeneration`，用于恢复采集时区分同一媒体的新会话。

任务 6 定义 `HistoryFilter(DateOnly? OpenedFrom, DateOnly? OpenedTo, string? Search, Guid? MovieId, Guid? SourceId, int Offset, int Limit)`，日期范围两端包含，按会话开始日期筛选。`DailyUsageFilter(DateOnly From, DateOnly To)` 只用于按实际发生日统计，不能和 HistoryFilter 混用。

## Task 1 接入能力验证与开发环境

**Files:** 创建 `probes/PotPlayerProbe/PlaybackStatistics - PotTraceProbe.as`、`ProbeBridge.cpp`、`CMakeLists.txt`、`README.md`；创建 `docs/compatibility/potplayer-26.04.22.md`。正式项目文件在任务 2 创建。

**Interfaces:** 输入本机 `Extension/api.txt` 与真实播放器操作；输出能力矩阵、回调签名/单位、初始化时机、停止及循环语义、位置/状态查询能力和原生字符串 ABI。ProbeBridge 仅导出 `int32_t Probe_CopyUtf8(const uint8_t* data, uint32_t length)` 用于检查跨边界复制，不安装正式统计程序。

- [ ] 确认 .NET 10 SDK、MSVC x64、CMake 和目标 Windows 版本；记录准确版本。SDK 不存在时按实施授权准备环境，不能声称当前机器可构建 C# 项目。
- [ ] 编写探针和操作表，先确认未部署时没有事件日志；将探针部署到隔离测试用播放器副本，避免覆盖用户现有扩展。
- [ ] 依次操作无媒体启动、打开暂停 11 秒、播放、跳转、倍速、队列预加载、同文件重开、整片/AB 循环、双实例、休眠及异常关闭；记录事件顺序、参数和宿主版本，不记录无关私人历史。
- [ ] 检查 Unicode 和带引号的测试路径是否原样复制；验证 DLL 只在显式初始化创建工作线程，不在 DllMain 做线程等待或启动程序。
- [ ] 用 Visual Studio x64 开发环境运行 `cmake -S probes/PotPlayerProbe -B out/probe`、`cmake --build out/probe --config Debug`；按 README 执行完整操作表。通过标准是关键能力有实际日志证据，含暂停计时和无媒体初始化；缺失项明确失败并修订接入路线。
- [ ] 保存兼容性结果与脱敏事件样本，提交 `docs: verify PotPlayer integration capabilities`。此任务不以“编译成功”代替宿主运行验证。

## Task 2 内部协议与模拟器

**Files:** 创建根构建文件及 `src/PotTrace.Contracts/{PlaybackEvent,WireMessage,EventValidator,FrameCodec}.cs`、对应 csproj；创建 `tools/PotTrace.Simulator/{Program,ScenarioRunner}.cs`、`scenarios/*.json`；创建 `tests/PotTrace.Contracts.Tests/ProtocolTests.cs` 及测试项目。

**Interfaces:** `EventValidator.Validate(PlaybackEvent e) -> ValidationResult`；`FrameCodec.Encode(WireMessage message) -> byte[]`；`FrameCodec.ReadAsync(Stream stream, CancellationToken ct) -> ValueTask<WireMessage?>`。WireMessage 是 Hello、EventBatch、Ack、Control、Status 的带类型封装；所有 DTO 在同目录定义，不互传任意字典。

- [ ] 固定 .NET SDK 和包版本，加入锁文件；建立 Contracts 与测试项目。协议版本为 1，序号从 1 递增；帧为 4 字节小端长度加 UTF-8 JSON，单帧最大 1 MiB、每批最多 128 事件。
- [ ] 添加行为用例 `Utf8RoundTripPreservesPath`、`TruncatedFrameIsNotAnEvent`、`OversizedFrameIsRejected`、`UnknownVersionRejected`、`DuplicateSequenceNotDoubleApplied`；断言路径完全相等、超限拒绝、重复批次仅产生一次业务效果。去重业务效果由任务 6 集成测试补证。
- [ ] 运行 `dotnet test tests/PotTrace.Contracts.Tests --filter ProtocolTests`，确认未实现编解码时失败；实现增量读帧和验证，不假设一次 Read 返回完整帧。
- [ ] 定义 Ack 为每个实例最后一个已持久化的处理序号；先接收不等于确认。序号缺失须显式 Gap，不能静默跳过后继续拼接片段。
- [ ] 实现 `ScenarioRunner.RunAsync(string scenarioPath, Stream output, CancellationToken ct) -> Task`；内置正常、暂停、重复片段、跨日、重复传输、缺口场景。运行上述测试全通过，并以 `dotnet run --project tools/PotTrace.Simulator -- --scenario scenarios/pause-only.json --validate` 验证协议。
- [ ] 提交 `feat: define playback event protocol and simulator`。

## Task 3 自动启动与单实例生命周期

**Files:** 创建 `src/PotTrace.App/{Program,App}.cs`、`App.xaml`、项目文件；`Hosting/{SingleInstanceCoordinator,PlayerRegistry,HostLifetimeCoordinator,NamedPipeServer}.cs`；`Settings/{CaptureSettings,SettingsStore}.cs`；创建 `native/PotTrace.Bridge/{CMakeLists.txt,include/PotTraceBridge.h,src/Bridge.cpp,src/LaunchWorker.cpp,src/PipeClient.cpp}`；创建 `tests/PotTrace.App.Tests/LifecycleTests.cs`、`tests/native/BridgeTests.cpp`。

**Interfaces:** `HostLifetimeCoordinator.OnPlayerConnected(Guid instanceId)`、`OnPlayerDisconnected(Guid instanceId)`、`SetWindowVisible(bool visible)`、`RequestUserExit()`；`SettingsStore.LoadAsync(CancellationToken ct) -> Task<CaptureSettings>`、`SaveAsync(CaptureSettings settings, CancellationToken ct) -> Task`。后台入口为 `PotTrace.exe --background` 或 `PotTrace.exe --show`。

- [ ] 添加生命周期测试：双实例同时启动只得到一个后台；无媒体不产生会话；最后断开后 2,999 毫秒仍存活、3,000 毫秒允许退出；新连接取消退出；显示窗口时不退出；主动退出不触发崩溃重启；暂停统计设置在重新加载后保持 true。
- [ ] 运行 `dotnet test tests/PotTrace.App.Tests --filter LifecycleTests` 确认失败，再实现上述接口，使用注入的 `TimeProvider` 和假进程启动器控制时间与竞争。
- [ ] 使用当前用户及登录会话范围的命名互斥和命名管道；显式设置访问控制并拒绝远程客户端。进程启动使用独立应用路径及正确参数编码，隐藏窗口，不能拼接 shell 命令。安全边界依据 [Windows 命名管道文档](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights)。
- [ ] 桥接导出 `int32_t PotTrace_Initialize(const uint8_t* configUtf8, uint32_t length)`、`int32_t PotTrace_Enqueue(const uint8_t* eventUtf8, uint32_t length)`、`int32_t PotTrace_Shutdown()`。初始化 JSON 含后台绝对路径与实例信息；入口只做长度检查和即时复制，返回 0 成功、1 队列满、负值为错误；后台启动和 IPC 全在工作线程。Shutdown 停止接受新事件并触发有界清理，不在宿主线程无限等待；安全卸载条件由任务 1 验证。
- [ ] 桥接队列上限 4,096 事件且总字节不超过 8 MiB，拒绝整条超限事件并在下一可用批次发送缺口标记；启动失败退避 1/5/15 秒，每分钟至多 3 次。UI 关闭与 UserExit 控制消息分开；用户退出令当前桥接实例停止自动重启。
- [ ] 运行生命周期测试和 `ctest --test-dir out/native -C Debug --output-on-failure`，检查中文/空格/引号路径、启动竞争、缓冲上限及重复初始化。提交 `feat: start statistics host with PotPlayer lifecycle`。

## Task 4 有效会话与实际播放时长

**Files:** 创建 `src/PotTrace.Core/` 项目；`Models/*.cs`、`SessionTracker.cs`、`MovieIdentity.cs`、`ObservationReducer.cs`、`DailyAllocator.cs`；创建 `tests/PotTrace.Core.Tests/{SessionTrackerTests,DailyAllocatorTests,MovieIdentityTests}.cs`。

**Interfaces:** `SessionTracker.Apply(PlaybackEvent e) -> EngineUpdate`；`SessionTracker.SetCaptureEnabled(bool enabled, DateTimeOffset utc, long monotonicMs) -> EngineUpdate`；`SessionTracker.SetNextCaptureMode(CaptureMode mode)`；`MovieIdentity.GetKey(string sourcePath) -> string`。DailyAllocator 按会话固定时区把已确认时间跨度分成 SessionDayRecord。

- [ ] 用测试构造器 `PlaybackScenario` 产生已定义协议事件，加入断言：停留 10,000 毫秒计次 0、10,001 毫秒计次 1；暂停 11,000 毫秒计次 1、播放时长 0；播放 6,000 加暂停 5,000 毫秒计次 1、播放时长 6,000；2 倍速实际 12,000 毫秒累计仍为 12,000。
- [ ] 添加队列不建会话、同名不同路径分别开会话、暂停恢复不加次、停止和整片循环分会话的用例；名称使用完整扩展名，`电影.mkv` 与 `电影.MKV` 相同，`.mp4` 与 `.mkv` 不同。
- [ ] 运行 `dotnet test tests/PotTrace.Core.Tests --filter SessionTrackerTests` 确认失败，实现每实例独立状态机。候选时长暂存，超过门槛后追认；只在可靠、连续且位置前进的观测间积分，暂停只计停留；重复 Snapshot 不成为重复打开。
- [ ] 加入时钟回拨不改变单调时长、休眠不累计、缓冲不累计及跨午夜各 10,000 毫秒的用例；日时长和总时长必须相等，次数只归属打开日期。
- [ ] 加入“暂停统计”立即截断、恢复新建 CaptureGeneration 并重新验证门槛的用例；模式设置只影响下一会话。运行本任务三个测试类，全部通过后提交 `feat: track valid sessions and actual playback time`。

## Task 5 详细片段与热度

**Files:** 创建 `src/PotTrace.Core/{SegmentTracker,HeatmapBuilder}.cs`、`Models/HeatInterval.cs`；创建 `tests/PotTrace.Core.Tests/{SegmentTrackerTests,HeatmapBuilderTests}.cs`；修改 `SessionTracker.cs` 集成片段输出。

**Interfaces:** `SegmentTracker.Apply(PlaybackEvent e) -> SegmentUpdate`，SegmentUpdate 在 Models 中定义，含候选/结束片段及其日时长分配；`HeatmapBuilder.Build(IEnumerable<SegmentRecord> segments) -> IReadOnlyList<HeatInterval>`，HeatInterval 含 SourceId、StartMs、EndMs、Count。

- [ ] 写测试断言：5,000 毫秒片段不入明细、5,001 毫秒入明细；4 秒播放/暂停/4 秒播放产生 0 个有效片段，短片段合计 8,000 毫秒；重复 8 秒片段保留两条；暂停无热度；跳转跨越内容不入区间。
- [ ] 运行 `dotnet test tests/PotTrace.Core.Tests --filter SegmentTrackerTests` 确认失败；实现暂停、缓冲、跳转、回绕、结束、休眠和 Gap 截断。连续变速按2026-11-17用户确认保留为片段内事件，不再截断。会话无效时禁止把候选片段提交到正式明细。
- [ ] 增加热度测试：区间 `[600000,620000)` 两次和 `[610000,630000)` 一次，输出 `[600000,610000):2`、`[610000,620000):3`、`[620000,630000):1`；相接端点不重叠；不同 SourceId 不互相增加；少于 5 秒的热点交集不能被再次过滤。
- [ ] 实现端点扫描，同位置增减合并后推进；热点排行按次数降序、长度降序。精简模式仅输出时长事实，不永久输出 Segment/TimelineEvent；详细片段跨午夜先判断整段门槛再分配日时长。
- [ ] 运行 `dotnet test tests/PotTrace.Core.Tests`；补证倍速变化、连续快速往返跳转的可观察边界，不对不可观测事件推算。提交 `feat: record playback segments and heat intervals`。

## Task 6 SQLite 持久化与查询

**Files:** 创建 `src/PotTrace.Storage/{Database,MigrationRunner,HistoryRepository,IngestionWriter}.cs`、`Migrations/001_initial.sql`、查询 DTO；创建 `tests/PotTrace.Storage.Tests/{PersistenceTests,QueryTests}.cs`；修改 App 宿主接入 writer。

**Interfaces:** `IngestionWriter.ApplyAsync(PlaybackEvent e, CancellationToken ct) -> Task<IngestReceipt>`，内部协调 Core 和事务；`HistoryRepository.GetMoviesAsync(HistoryFilter filter, CancellationToken ct) -> Task<IReadOnlyList<MovieSummary>>`、`GetSessionsAsync(HistoryFilter filter, CancellationToken ct) -> Task<IReadOnlyList<SessionRecord>>`、`GetSegmentsAsync(Guid sessionId, CancellationToken ct) -> Task<IReadOnlyList<SegmentRecord>>`、`GetDailyUsageAsync(DailyUsageFilter filter, CancellationToken ct) -> Task<IReadOnlyList<DailyUsage>>`。IngestReceipt 包含已提交序号和是否重复；MovieSummary、DailyUsage 在 Storage 的查询 DTO 中定义，分别包含列表列值和日期/播放/短片段时长。

- [ ] 建表 Movie、MediaSource、Session、SessionDay、Segment、TimelineEvent、Aggregate、IngestionCheckpoint、DeletionTombstone、SchemaVersion；唯一约束保护实例序号与逻辑会话。MediaSource 在路径或文件版本变化时分版本，读取失败保留未知标记而非随意合并。
- [ ] 添加真实临时 SQLite 文件测试：同事件重放只计一次；跨午夜总计等于日之和；精简无片段但可重建日统计；同名不同版本总计合并但热度独立；事务中断无半条会话。
- [ ] 运行 `dotnet test tests/PotTrace.Storage.Tests --filter PersistenceTests` 确认失败；实现单写入队列、WAL、本机数据目录、参数化 SQL。每秒批量事务，关闭/切换立即触发；持久化业务数据与检查点后才返回可发送的 Ack。
- [ ] 内存状态只有在事务成功后推进已提交检查点；失败保留待提交批次，必要时从已提交检查点重建后重放。未通过 10 秒门槛的数据只处于有期限的恢复区，不计入查询；会话结束无效则删除候选事实。
- [ ] 实现分页、搜索、排序和日期口径；按实际源与大小/修改时间分热度版本，禁止将两个同名来源的明细自动叠加。测试磁盘不可写/数据库忙时不 Ack、不向播放器抛未捕获错误；恢复后不重复累计。
- [ ] 运行本项目所有测试并以 `PRAGMA integrity_check` 返回 `ok` 验证临时库；提交 `feat: persist playback history transactionally`。

## Task 7 正式 PotPlayer 适配与实时采集

**Files:** 创建 `plugin/PlaybackStatistics - PotTrace.as`；创建 `native/PotTrace.Bridge/src/PlayerObserver.cpp`；创建 `tests/fixtures/potplayer/*.json` 和 `tests/PotTrace.Integration.Tests/PlayerTraceTests.cs`；更新兼容性报告。

**Interfaces:** 消费任务 1 已验证的宿主回调及 ABI，调用任务 3 的 Initialize/Enqueue/Shutdown；生产任务 2 的 PlaybackEvent。位置补充查询只使用验证过的消息/接口，具体常量须从探针结果移入命名常量并注明单位。

- [ ] 将真实操作日志转为脱敏回归样本：打开后一直暂停、队列预加载、同文件重开、整片循环、AB 循环、跳转、双实例。期望计次/时长按设计文档明确写入样本，缺失能力标记失败，不能删掉对应场景。
- [ ] 运行 `dotnet test tests/PotTrace.Integration.Tests --filter PlayerTraceTests` 确认无适配结果时失败；实现回调映射、MediaEpoch 与单调时间。原生指针必须在同步 Enqueue 返回前复制，不能跨异步边界保存脚本字符串指针。
- [ ] 原生观察器默认每 250 毫秒查询一次已验证的状态/位置，暂停时每 500 毫秒检查当前媒体及进程；回调和观察器通过同一排序队列分配 Sequence。超时/未知时不继续补时间；宿主消息使用有界超时，避免卡住工作线程。
- [ ] 在隔离播放器中验证首次启用后重新打开自动连接、窗口不弹出、暂停门槛、跳转精度及多实例；记录实际边界误差，目标不超过 500 毫秒，未达到则记录并调整，不能声称帧级精确。
- [ ] 运行集成回归和真实操作表，检查后台退出后播放器不被拖慢。提交 `feat: integrate PotPlayer playback collection`。

## Task 8 精简与详细统计界面

**Files:** 创建 `src/PotTrace.App/Views/{MainWindow,SettingsWindow}.xaml` 及对应代码；`ViewModels/{MainViewModel,MovieListViewModel,SessionViewModel,SettingsViewModel}.cs`；`Controls/{WallTimelineControl,HeatmapControl}.cs`；`Themes/{Dark,Light}.xaml`；`Tray/TrayController.cs`；创建 `tests/PotTrace.App.Tests/ViewModelTests.cs`。

**Interfaces:** `MainViewModel.RefreshAsync(HistoryFilter filter, CancellationToken ct) -> Task`、`SessionViewModel.LoadAsync(Guid sessionId, CancellationToken ct) -> Task`；Controls 只消费 SegmentRecord、TimelineEventRecord 或 HeatInterval 的只读集合；UI 不直接修改统计数据表。

- [ ] 添加 ViewModel 测试：切换查看方式不改 CaptureMode；旧精简会话显示“当时未采集明细”；待确认显示“尚未计入”；暂停统计状态刷新后保留；异步查询取消不让旧结果覆盖新结果。
- [ ] 运行 `dotnet test tests/PotTrace.App.Tests --filter ViewModelTests` 确认失败，接入任务 6 的查询与任务 3 的设置/生命周期接口。
- [ ] 实现精简表格的文件名、次数、累计时长、最近打开时间，支持分页/虚拟化、列排序、搜索；详细视图采用左侧影片、右侧会话与现实时间轴，下方影片热度和排行。两条横轴分别标“现实时间”和“影片位置”。
- [ ] 实现缩放/悬停、短片段时长说明、来源切换、日期筛选、托盘和可配置快捷键；播放跳转入口只在任务 1/7 验证控制接口后启用，否则明确隐藏该可选动作。
- [ ] 用固定模拟数据在 100%/150%/200% DPI、深浅色、长文件名与空历史状态下人工检查布局；保留截图到 `artifacts/ui-review/` 并在验收记录引用。运行 ViewModel 测试和 `dotnet build PotTrace.sln -c Release`；提交 `feat: add compact playback statistics UI`。

## Task 9 历史管理、隐私与异常恢复

**Files:** 创建 `src/PotTrace.Storage/{HistoryMaintenance,BackupService,RecoveryService}.cs`；`src/PotTrace.App/Services/ExportService.cs`；`src/PotTrace.Core/ExclusionPolicy.cs`；创建 `tests/PotTrace.Storage.Tests/{MaintenanceTests,RecoveryTests}.cs`、`tests/PotTrace.App.Tests/ExportTests.cs`；修改设置与历史操作界面。

**Interfaces:** `HistoryMaintenance.DeleteAsync(HistoryFilter filter, CancellationToken ct) -> Task<int>`、`MergeMoviesAsync(Guid targetMovieId, Guid sourceMovieId, CancellationToken ct) -> Task`、`GroupSourceTimelinesAsync(IReadOnlyList<Guid> sourceIds, CancellationToken ct) -> Task`；`BackupService.CreateAsync(string destination, CancellationToken ct) -> Task`、`RestoreAsync(string backup, CancellationToken ct) -> Task`；`ExportService.ExportAsync(HistoryFilter filter, string path, ExportFormat format, CancellationToken ct) -> Task`，ExportFormat 为 Csv/Json。

`ExclusionPolicy.IsExcluded(string sourcePath, IReadOnlyCollection<string> excludedDirectories) -> bool` 在进入 SessionTracker 前执行，按规范化完整目录及分隔符边界比较；不为检查排除规则打开视频文件。

- [ ] 写测试：按打开日期删除完整跨日会话及缓存；重试旧事件不能复活删除记录；合并影片不丢来源、不自动合并热度；明确合并热度后覆盖计数正确；排除目录按路径边界匹配，`D:\Private` 不错误匹配 `D:\Private2`。
- [ ] 添加备份恢复往返、坏备份拒绝且旧数据库仍可用、损坏数据库保留副本、不自动删库、崩溃只恢复到可靠观测点、暂停统计不记录空白时间的用例；运行对应测试确认失败。
- [ ] 实现事务删除与删除标记/采集代际隔离；删除涉及当前会话时先截断并更新代际，旧事件不再落入新会话。人工合并影片与来源时间轴是两种显式动作，各自预览影响。
- [ ] 使用 SQLite 备份接口而非复制正在使用的主文件；恢复先暂停摄取、关闭连接、验证备份、原子替换并保留回滚副本，再恢复并标记缺口。[备份接口依据](https://learn.microsoft.com/en-us/dotnet/standard/data/sqlite/backup)
- [ ] 实现 CSV/JSON 导出、路径排除和记录清理。CSV 正确转义引号/换行，对以 `= + - @` 开头的文件名单元格作文本化处理；JSON 保留原始字段，避免导出改变源记录。
- [ ] 在模拟器中注入进程中断、重发、磁盘错误、序号缺口，运行本任务与全部 Storage 测试；提交 `feat: manage history and recover interrupted collection`。

## Task 10 安装升级与发行验收

**Files:** 创建 `scripts/{build,test,publish}.ps1`、`packaging/{Install,Uninstall}.ps1`、`packaging/manifest.json`；更新 `README.md`；创建 `docs/{installation,acceptance}.md`、`tests/packaging/Install.Tests.ps1`。

**Interfaces:** `Install.ps1 -PotPlayerPath <directory> -PackagePath <directory>`；`Uninstall.ps1 -PotPlayerPath <directory> [-RemoveHistory]`。安装只使用明确包清单，注册文件所有权；不递归删除播放器目录。

- [ ] 在临时目录建立安装测试：安装仅新增自有文件；更新保留配置与历史；只读目录/播放器占用时给出可执行说明并不留下半安装；卸载默认保留数据库，RemoveHistory 才移除指定用户数据。
- [ ] 运行 `Invoke-Pester tests/packaging/Install.Tests.ps1` 确认安装动作缺失时失败，建立用户级程序目录、受控插件部署、版本化包及失败回滚；系统目录确需提升权限时只提升部署部分，不让统计后台长期管理员运行。
- [ ] 发布 `dotnet publish src/PotTrace.App -c Release -r win-x64 --self-contained true -p:PublishSingleFile=false`，连同 x64 Bridge、脚本和安装说明打包。检查包内无测试路径、用户历史、凭据及调试日志。
- [ ] 完成首次启用引导、自动连接检查、启用状态/采集模式保存、版本兼容提示。需要重启播放器时在引导中明确说明，不能将“复制成功”显示为“正在统计”。
- [ ] 在目标 Windows 构建与 PotPlayer 版本逐项执行设计文档全部验收案例，含关闭窗口继续记录、双实例、最后退出、主动退出抑制、暂停统计跨重启、崩溃恢复、长路径和高 DPI。
- [ ] 用 10 万条模拟片段验证分页和来源热度查询，记录 CPU、内存、查询延迟与实际采样误差；单次播放器回调入队耗时目标 p95 小于 2 毫秒。数字是待测目标，未通过不能写为性能成果。
- [ ] 运行 `pwsh -File scripts/test.ps1`、`pwsh -File scripts/publish.ps1` 并核对产物清单；提交 `build: package PotTrace and document acceptance`。完成独立代码审阅后才声明首版可交付。

## 顺序与完成判定

任务依赖为 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10；其中 Core 可以在任务 1 的运行时验证等待期间用模拟协议推进，但任务 7 和发行不能绕过接入门槛。每项只提交其自身文件，提交前运行对应验证；不把尚未通过的案例勾选为完成。

完整交付必须同时满足：真实播放器自动采集、两种统计模式口径一致、详细片段与热度符合案例、安装升级可回滚、异常恢复不虚增，以及目标环境实际验收通过。仅有界面、模拟数据或核心单元测试通过均不代表插件已经完成。

## 计划自检

已将首次启用、自动生命周期、10/5 秒阈值、暂停口径、同名归并、重复片段、热度、跨日、模式切换、UI、导出/备份/删除、安装和异常恢复分别映射到任务。共享时间单位固定为毫秒，IPC Ack 只表示持久化，Core 不依赖 UI/存储。接入不确定性集中在任务 1 的实测产物，后续任务不假定社区回调在本机必然可用。

建议在当前会话按任务顺序实施，先做接入验证；若用户选择分工执行，可在协议与接入契约冻结后按模块分工。开发计划尚未开始执行。
