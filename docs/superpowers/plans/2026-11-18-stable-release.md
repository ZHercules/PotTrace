# PotTrace 首个稳定版 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. 默认由当前执行者顺序实施；本文件不是启动实现或部署的指令。

**Goal:** 将已验证的实验图内采集正式接入安装包，补齐准确性、恢复、安装回退和长稳证据后发布首个受限支持范围的稳定版。

**Architecture:** 保留 C++ DirectShow → Bridge → 命名管道 → C# 引擎 → SQLite → WPF。新增正式 Collector，复用原型的归属与观测逻辑，以协议能力握手和独立稳定数据库保护升级兼容性。所有实现先在隔离副本验证。

**Tech Stack:** Windows x64、PotPlayer 26.4.22 / EVR、C++20 / MSVC / CMake、.NET 10 / WPF、Microsoft.Data.Sqlite、PowerShell 5.1、Python 3.12 实播工具。

**Spec:** [稳定版设计](../specs/2026-11-18-stable-release-design.md)。本计划是其待审阅实施拆分，所有验收复选框初始未完成。

## Global Constraints

- 停留严格 `>10000ms`；详细片段实播严格 `>5000ms`；暂停仅贡献已确认停留。
- 连续变速不分段；未知时间不补算；普通播放产生无法解释的明细缺口即验收失败。
- 复合查询超过 `50ms` 无效；队列上限 `4096` 项 / `8MiB`。这不是计时精度承诺。
- Collector 建议最多五个后台线程、两个并发受观察图；外部阻塞 COM 不强杀，不无限创建替代线程。
- 不注册全局滤镜，不改用户渲染器，不依赖测试环境变量，不通过脚本回填虚构首段。
- 新库 `pottrace-stable.db`，旧历史一致性导入且原库保留；旧预览不能打开新库。
- 仅受支持组合发布；真实睡眠至少 3 轮、长稳至少 24 小时 / 500 次换片，缺一项保持预览。
- 不动 `E:\pot\PotPlayer`，直到所有发布门槛通过且实际配置核对一致。

## Review Focus

1. 同一路径重开、图复用与预加载取消：次数正确，旧结果不得写入新会话。任务 2、5、9。
2. COM 永不返回与退出重入：播放器仍可播放/退出，引用和线程数有界。任务 3、10。
3. 预览版仍运行或新版库已存在：不双写、不重复导入、不让旧版误读 nullable rate。任务 4、6、7。
4. 用户在升级间隔修改滤镜/INI：回退只处理自己仍拥有的变更，不能恢复整份陈旧配置。任务 7。
5. 跨日、时区/系统时间变化与取消大导出：持续时间不受墙钟跳变影响，数据顺序稳定、操作可取消。任务 5、8、10。

## 工作区、命令与模块分工

实施时从最新 develop 创建独立 `codex/stable-collector` 工作树；先核对其与已合并 master 的代码关系。当前主工作区有三份既存修改：`probes/PotPlayerProbe/review_ui.py`、`src/PotTrace.App/Views/MainWindow.xaml`、`src/PotTrace.App/Views/MainWindow.xaml.cs`，不能覆盖或夹带提交。UI 任务到达时先核对这些修改的最终归属再集成。

主要新增文件均标注为新建；既有文件尽量就地扩展，不重写已通过验证的算法。任务间共享的类型在所属任务定义，后续任务只消费。

从实施工作树根目录执行，以下命令为现有入口：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/dotnet.ps1 test PotTrace.sln -c Release --filter FullyQualifiedName~CollectorProtocolTests
```

最后一条按任务替换测试类名。原生新测试加入现有 `out/native` 的 CTest 清单，使用 build/test 总入口运行，不另建遗漏的测试通道。每个实现任务遵循失败测试 → 确认失败原因 → 最小实现 → 针对性测试通过 → 提交自己的明确路径；避免 `git add .`。全量测试在任务 1、9、11 以及集成风险变更后运行，不在纯文档调整后反复运行。

这些任务组成一个有顺序依赖的发布项目；Collector、协议、迁移和安装不能各自发布成稳定包。

## Task 1：建立可比较的基线与验收清单

**Files:** 新建 `docs/stable-validation.md`、`probes/DirectShowProbe/stable_matrix.json`、`probes/DirectShowProbe/run_stable_suite.py`、`tests/probes/test_stable_suite.py`；复用 `run_graph_probe.py`，不改变已有失败证据。

**Interfaces:** Python `load_matrix(path: Path) -> dict`、`validate_result(result: dict) -> list[str]`；每轮 manifest 包含 build/player/config 哈希、输入、命令及独立见证窗口、证据目录、检查结果和失败原因。运行入口 `run_stable_suite.py --suite NAME --output DIR --player PATH`；缺失证据退出非零。

- [ ] 写失败测试 `test_missing_witness_is_failure`、`test_player_fingerprint_mismatch_is_failure`、`test_failed_attempt_is_preserved`；断言缺见证/版本不符不能 PASS，重复运行生成新 evidence 目录。
- [ ] 运行 `python -m unittest discover -s tests/probes -p test_stable_suite.py`，确认因新验证器尚未实现而失败。
- [ ] 实现上述接口和 manifest；定义 suite 名 `smoke/accuracy/faults/soak/package/ui`，记录每项的预期和 G1–G6 对应关系；此时未实现的 suite 返回未完成，不伪造通过。
- [ ] 新测试通过；全新 Release 构建和现有全量测试通过，记录实际数量和基线资源；校准画面见证间隔及时间戳误差。
- [ ] 单独提交基线工具与验收模板：`test: establish stable release evidence baseline`。

## Task 2：提取正式采集器与自动启动链路

**Files:** 新建 `native/PotTrace.Collector/CMakeLists.txt`、`src/CollectorFilter.cpp`、`Collector.def`、`include/ConnectedRenderer.h`、`include/RendererWatch.h`、`include/GraphSink.h`、`include/CollectorConfig.h`、`src/CollectorConfig.cpp`（以上相对该模块目录）；新建 `tests/native/CollectorActivationTests.cpp`、`src/PotTrace.App/Settings/CapturePolicyStore.cs`。修改 `src/PotTrace.App/Settings/SettingsStore.cs`、`src/PotTrace.App/AppHost.cs`、`src/PotTrace.App/Views/SettingsWindow.xaml.cs`、`probes/DirectShowProbe/GraphProbe.cpp` 及其 CMake、`native/PotTrace.Bridge/CMakeLists.txt`、`scripts/build.ps1`。保留实验包装器入口。

**Interfaces:** `CollectorConfig LoadCollectorConfig(const std::filesystem::path& modulePath)`，返回受控 Bridge 路径及安装标识；`bool EnsureBridgeStarted(const CollectorConfig&) noexcept` 幂等初始化。`CapturePolicy(bool Enabled, bool RecordingPaused, long Revision)` 与 `Task SaveAsync(CapturePolicy policy, CancellationToken ct)` 在 CapturePolicyStore 定义；唯一权威文件为 `capture-policy.json`。配置无效时过滤器透明透传，停止统计，不能阻止播放。

- [ ] 写 `ActivationWithoutScript`、`RepeatedActivationIsIdempotent`、`RejectRelativeBridgePath`、`SourceReusedGraphNeedsNewEpoch`；断言在已授权策略下，未发生脚本影片回调也建立观察缓冲，重复初始化只有一个 Bridge 实例，相对路径拒绝，图复用不继承旧身份。补 `MissingPolicyDoesNotCapture`、`UncheckedPluginDisablesCapture`、`UserExitPreventsAxRestart`，验证未知授权不缓存影片身份、取消勾选与主动退出立即抑制本实例。
- [ ] 将新 CTest 目标登记后运行 build/test，确认缺失正式实现导致新测试失败。
- [ ] 从原型提取共用观察逻辑，生产 AX 使用独立 CLSID，加载自己的受控配置；统一策略开关并验证插件勾选状态的早期读取/取消通知。固定 baseclasses 下载版本/哈希并核对分发许可。ABI v2 暂不改布局。
- [ ] 原生测试通过；隔离 `smoke` 验证 AX 实际载入、无需测试环境变量、首次启用后的第二次启动自动采样；产生并保存一条在线入库会话。
- [ ] 提交 `feat: introduce production DirectShow collector`。

## Task 3：固定生命周期与有界故障退化

**Files:** 新建 `native/PotTrace.Collector/include/CollectorRuntime.h`、`src/CollectorRuntime.cpp`、`include/CollectorDiagnostics.h`、`tests/native/CollectorLifecycleTests.cpp`；修改任务 2 的过滤器/观察器以及 `native/PotTrace.Bridge/include/ObservationFence.h`、`TerminalDelivery.h`（仅必要处）。

**Interfaces:** `GraphLease CollectorRuntime::Attach(GraphBinding binding)`、`void CollectorRuntime::Retire(GraphLease lease) noexcept`、`CollectorHealth CollectorRuntime::Snapshot() const noexcept`。`GraphLease` 为绑定 ID + generation；`CollectorHealth` 含在途查询数、占用槽数、待回收引用数、队列丢失数、故障原因；类型在 `CollectorRuntime.h` 定义。

- [ ] 写 `RetiredGenerationCannotPublish`、`BlockedQueryDoesNotBlockRetire`、`ExhaustedSlotsDoNotSpawnWorkers`、`TerminalDropBecomesGap`；断言流线程无磁盘/等待调用、线程 ≤5、受观察图 ≤2、过期包零提交、满队列不静默丢最终边界。
- [ ] 使用可控阻塞假 COM 对象跑新 CTest，确认现有无限/逐图线程实现不能满足新约束。
- [ ] 实现固定查询槽及无等待 retire；长期阻塞保留有界引用，不强杀、不卸载仍被调用模块；诊断文件默认不记路径且总量 ≤10MiB。
- [ ] 测试释放阻塞后引用可回收；隔离执行 100 次换片、退图时挂住查询、关闭播放器，确认无阻塞退出和资源线性增长。
- [ ] 提交 `fix: bound collector lifetime and stalled observations`。

## Task 4：明确版本握手和采集状态

**Files:** 新建 `src/PotTrace.Contracts/CaptureCapabilities.cs`、`tests/PotTrace.Contracts.Tests/CollectorProtocolTests.cs`；修改 `WireMessage.cs`、`EventValidator.cs`、`FrameCodec.cs`、`PlaybackEvent.cs`、`native/PotTrace.Bridge/src/Bridge.cpp`、`src/PotTrace.App/Hosting/NamedPipeServer.cs`、`SingleInstanceCoordinator.cs`；补 `tests/PotTrace.App.Tests/PipeTests.cs`。

**Interfaces:** `CaptureCapabilities(int ProtocolVersion, string BuildId, string[] Features)`；`ValidationResult ValidateCapabilities(CaptureCapabilities peer)`。协议 v2 必需能力固定为 `NullableRate`、`ContinuousRateEvents`、`PositionDiscontinuity`、`CaptureQuality`。新增状态消息不经过播放统计计数路径；管道 channel 区分 v2，数据文件写锁另行约束。

- [ ] 写 `MissingCapabilityRejectsBeforeEvents`、`V1PeerCannotConnectToStableChannel`、`UncommittedBatchIsNotAcked`；断言缺能力时事件提交数为 0、DB 失败不 ACK、重连后的相同 sequence 只计一次。补 `PolicyCutoffRejectsLaterSamples`：策略 revision 和单调截止以前的已确认数据可提交，以后数据丢弃，重新启用不回填。
- [ ] 运行 `scripts/dotnet.ps1 test PotTrace.sln -c Release --filter FullyQualifiedName~CollectorProtocolTests` 及 PipeTests，确认新增约束先失败。
- [ ] 实现握手和 Bridge 对称验证；保持当前用户/进程身份检查，禁止脚本播放事件覆盖 DirectShow 状态，传输错误转为可显示状态。
- [ ] 新旧版本配对测试、同时启动两个宿主及不同 player 实例通过；版本不兼容不能悄悄走 v1 统计。
- [ ] 提交 `feat: negotiate stable collector protocol and status`。

## Task 5：补齐质量证据和连续变速明细

**Files:** 修改 `src/PotTrace.Core/Models.cs`、`SessionTracker.cs`、`SegmentTracker.cs`、`native/PotTrace.Bridge/include/GraphTimeline.h`、`EffectiveRate.h`（需要时）；新建 `tests/PotTrace.Core.Tests/CaptureQualityTests.cs`，扩展 `EffectiveRateTests.cs`、`ObservationTests.cs`、原生 GraphTimeline/EffectiveRate 测试。

**Interfaces:** 新增 `CaptureStatus { Complete, Incomplete, Legacy }`；`SessionRecord` 增加设计 §4.3 元数据，其中排序字段为 `long? InstanceOrder`、`long? StartedMonotonicMs`（旧记录可空/Legacy，新采集记录必须非空）。`TimelineEventRecord` 增加 `Guid? SegmentId`、`long? GapFromMonotonicMs`、`long? GapToMonotonicMs`、`string? Reason`。现有 `EngineUpdate` 输出结构保持，事件与片段在同一次更新保持一致；InstanceOrder 由任务 6 在首次持久化实例时分配并保持。

- [ ] 用确定性回放写以下断言：`Stay(10000).IsValid == false`、`Stay(10001).IsValid == true`、`Detail(5000).Count == 0`、`Detail(5001).Count == 1`；各 helper 使用现有 SessionTracker 驱动方式，不绕过引擎。
- [ ] 写 `SpeedChangesKeepOneSegment`：1→0.5→1 的 6+6+6 秒连续实播只有一段、PlayedMs=18000、两条变化事件；写 `GapNeverBridgesShortDetails` 和 `ClockCorrectionDoesNotAddDuration`，确认未知区间不拼接、UTC 跳变不加时长。运行相关 Core/Native 测试观察新增失败。
- [ ] 扩展规则和元数据；只在证据足够时维护连续片段，短期采样欠缺不能直接误认 seek；证据不足仍输出缺口。速率只影响媒体映射，不乘墙钟时长；不足 5 秒片段的事件保留会话关联，SegmentId 留空。
- [ ] 加 `PausedStayWithoutPlay`、`SamePathReopenNewSession`、跨午夜/时区、退图乱序和 clock identity 更换的回归，全部通过；`UtcRollbackKeepsPerInstanceOrder` 断言同播放器按单调起点/sequence/Order 排序，多播放器分轨道，不能按回拨后的 UTC 重排。真实冻结位置既有失败样本作为固定输入保留，不能删掉使统计“完整”。
- [ ] 提交 `fix: preserve capture evidence and continuous speed segments`。

## Task 6：数据库版本门禁、导入和迁移

**Files:** 新建 `src/PotTrace.Storage/DatabaseMigrator.cs`、`StableHistoryImport.cs`、`Migrations/002_capture_quality.sql`、`tests/PotTrace.Storage.Tests/MigrationTests.cs`；修改 `Database.cs`、`IngestionWriter.cs`、`BackupService.cs`、Storage 项目资源配置、`src/PotTrace.App/AppHost.cs` 和启动配置。

**Interfaces:** `Task<MigrationResult> DatabaseMigrator.UpgradeAsync(string path, CancellationToken ct)`；`Task<ImportResult> StableHistoryImport.ImportOnceAsync(string legacyPath, string stablePath, CancellationToken ct)`。结果包含源/目标 schema、备份路径、是否新建/已存在；不接受不兼容高版本读写。写锁覆盖导入/迁移整个期间。

- [ ] 写 `ExistingStableDatabaseIsNeverOverwritten`、`LegacyFileRemainsByteUnchanged`（以已关闭、无 WAL 写入的源库比较）、`FailedMigrationKeepsOldSchema`、`FutureSchemaRejectedBeforeDeserialize`、`LegacyRowsRemainLegacy`、`InstanceOrderSurvivesHostRestart`；迁移增加实例顺序表/唯一 InstanceId 索引，分配使用同一写事务。
- [ ] 运行 `scripts/dotnet.ps1 test PotTrace.sln -c Release --filter FullyQualifiedName~MigrationTests`，确认当前 CreateAsync 不满足这些约束。
- [ ] 实现先备份校验再事务迁移；从现有 `pottrace.db` 一次性导入 `pottrace-stable.db`，旧库不迁移。新库存在即验证后沿用，不重复覆盖；字段默认 Legacy，提交质量数据与 checkpoint 同事务。
- [ ] 注入备份失败、迁移半途异常、已有预览写入者和重新安装；验证两个版本不会同时写同一库、恢复后事件去重不变，回退仍可读旧库。
- [ ] 提交 `feat: migrate stable history with protected preview rollback`。

## Task 7：私有加载安装、配置事务和回退

**Files:** 新建 `packaging/PlayerProfile.ps1`、`packaging/InstallJournal.ps1`；修改 `packaging/Install.ps1`、`Uninstall.ps1`、`scripts/publish.ps1`、`plugin/PlaybackStatistics - PotTrace.as`；扩展 `tests/packaging/Install.Tests.ps1`，新建 `tests/packaging/PlayerProfile.Tests.ps1` 并接入 `scripts/test.ps1`。

**Interfaces:** `Get-PotTracePlayerProfile -PlayerPath` 返回版本/位数/配置后端/归属路径；`Set-PotTraceCollectorOverride -Profile -CollectorPath -JournalPath` 写仅自有 CLSID 项；`Restore-PotTraceOwnedChanges -JournalPath` 按当前值与已写值比较恢复。清单纳入生产 AX、Bridge、协议/schema 兼容信息和配置模板；旧实验 AX 不入包。

- [ ] 写 Pester 用例：不同 CLSID/优先级不变；用户已修改自有项则冲突而非覆盖；播放器占用时预检失败；中断后 journal 可恢复；包哈希不符不触碰配置；中文路径可安装并卸载保留历史。
- [ ] 在独立临时 INI 和测试注册表子键上运行新 Pester 文件，确认生产加载配置尚未实现导致失败。禁止测试触碰真实用户 PotPlayer 配置。
- [ ] 实现受支持配置后端和原子安装步骤；明确配置变更所有权，安装事务异常可恢复。稳定安装启用后无需每次手动开启；旧脚本只管启动/开关，不再提交竞争的播放事实。
- [ ] 打包候选预览，使用 `--suite package` 在隔离副本验证 AX 加载和实际在线入库、升级/重复安装、回退旧历史、新库保留，以及卸载后其他文件和滤镜不变。
- [ ] 提交 `feat: install and roll back private collector configuration`。

## Task 8：状态、时间轴及大历史查询

**Files:** 修改 `src/PotTrace.App/ViewModels/MainViewModel.cs`、`Controls/TimelineControl.cs`、`Views/MainWindow.xaml` 及代码、`Services/ExportService.cs`、`src/PotTrace.Storage/HistoryRepository.cs`、`QueryModels.cs`；新建 `tests/PotTrace.App.Tests/CaptureStatusTests.cs`、`tests/PotTrace.Storage.Tests/LargeHistoryTests.cs`，扩展 ExportTests。

**Interfaces:** `CaptureStatusViewModel`（新建同名文件）接收任务 4 状态和任务 5 质量数据；`IAsyncEnumerable<SegmentRecord> HistoryRepository.StreamSourceSegmentsAsync(Guid sourceId, CancellationToken ct)` 为导出/热区提供有界批次。既有查询/导出用户功能保持兼容，新增接口由存储层控制分页，不让 UI 拼 SQL。

- [ ] 写 `UnsupportedIsNotShownAsRecording`、`PauseAddsStayNotPlay`、`RateWindowIsNotExactCommandTime`、`CancelledExportDoesNotPublishPartialFile`；断言界面区分不支持/缺口，导出临时文件成功后才替换目标。
- [ ] 新增大数据 fixture，运行 App/Storage 针对性测试，确认现有一次性读入或状态缺失不能满足要求。
- [ ] 实现状态与原因、变速/缺口时间轴；分页可取消，热区分批扫描并保持跨批边界正确，CSV/JSON 流式写；按用户当前 UI 工作成果集成，不覆盖未完成修改。
- [ ] `--suite ui` 比较 1 万和 100 万片段工作内存趋势，10 万会话首屏 P95 ≤1 秒；检查 100/150/200% DPI、小窗口/长片名，所有明细/热区/导出计数相符。
- [ ] 提交 `feat: show recording quality and stream large history`。

## Task 9：完整准确性与兼容组合验收

**Files:** 扩展任务 1 的矩阵、运行器与 `run_graph_probe.py`；新增 `probes/DirectShowProbe/verify_stable_history.py`、`tests/probes/test_stable_history.py`；更新 `docs/stable-validation.md`。测试夹具和原始视频留在忽略的 `out/`。

**Interfaces:** `verify_history(database: Path, evidence: dict) -> dict` 输出每条需求的预期/实际/误差窗口/结果。任何缺证据标为未验收；不以总 bool 掩盖子项失败。

- [ ] 写错误来源、重复 ACK、漏首段、缺短促变速证据、暂停误加播放和预加载提前计数的反例；先确认验证器能拒绝这些坏数据库。`WitnessUncertaintyOver100msIsNotPass` 必须把过宽见证窗口标为测量不足；低帧率画面不能替代计时证据。
- [ ] 扩展 `accuracy`：首次启动/切换/下一集各 20 轮；先确认暂停再等待 >10 秒，验证该暂停区间只增停留；纯暂停零播放由可控图夹具证明，不把暂停前瞬间实播抹掉。
- [ ] 执行设计 G2 全矩阵；对每个支持的编解码/容器/解码设置记录结果；新增 D3D11 负例必须显示“不支持”，不得写伪完整会话。
- [ ] 核对数据库、热区、事件及导出；单元规则精确相等，实播偏差按设计 §5 的已校准见证窗口和预算判断。停留单独用已确认暂停增量核对，受控图提供纯暂停/ready 边界；未能独立界定的真实载入时刻不宣称精确，所有无法解释缺口阻断后续稳定发布。
- [ ] Release 全量测试通过后提交测试与报告 `test: validate stable playback identity and timing`，失败目录保留并在报告列出。

## Task 10：异常、真实睡眠和长稳验收

**Files:** 新建 `probes/DirectShowProbe/run_fault_suite.py`、`run_soak.py`，扩展稳定运行器和 `docs/stable-validation.md`；必要故障修复各自补测试并独立提交，禁止只改断言门槛。

**Interfaces:** `--suite faults` 输出故障开始/结束、丢失/重放/ACK 证据和资源；`--suite soak --hours 24 --switches 500` 输出定期资源样本与退出结果。真实睡眠作为显式独立选项，未执行时结果为 NOT_RUN。

- [ ] 先用构造报告测试“缺睡眠”“不足 24 小时”“只换片 499 次”“资源持续增长”必定不能通过 G3/G4。
- [ ] 在隔离环境运行宿主/播放器崩溃、管道断连、队列背压、数据库只读/模拟磁盘写满、COM 超时；只能终止本轮创建的进程。写满采用故障注入或限额测试卷，不填满用户系统盘。
- [ ] 校时先用可注入时钟测试，不擅改系统时间；安排可接受时段执行至少 3 次真实睡眠/唤醒，确认睡眠不贡献播放/停留、唤醒后当前来源重新确认。
- [ ] 运行 24 小时 / 500 次换片长稳，按设计资源基线与数值门槛检查，正常退出后 SQLite 完整性通过；存在归因不明崩溃/卡死保持失败，不靠重跑覆盖。
- [ ] 提交报告与必要测试 `test: verify recovery sleep and long running stability`。测试耗时是验收的真实下限，不许用模拟时间替代。

## Task 11：稳定候选包、审查与发布

**Files:** 修改 `README.md`、`docs/stable-validation.md`、`docs/acceptance.md`、`docs/directshow-validation.md`；新建 `docs/stable-release-checklist.md`。产物位于 `artifacts/`，不提交二进制与个人数据。

**Interfaces:** 发布清单必须关联同一 build 的 G1–G6 证据、manifest SHA、版本、支持组合、已知限制及回退路径；Gate 为 PASS/FAIL/NOT_RUN，不接受“预计通过”。

- [ ] 从干净提交重新构建最终候选，执行 `scripts/test.ps1` 和安装验收；若与长稳受测产物哈希不同，评估差异并重跑所有受影响门槛，运行时代码变动须重跑长稳。
- [ ] 检查包包含正式 AX，且无 probe 环境依赖、测试媒体、私人路径日志；执行无开发 SDK 环境启动、重新安装和升级回退。
- [ ] 整体审查准确性证据、生命周期、迁移/所有权；修复后复验。只有 G1–G6 均 PASS 才移除 preview 标记，稳定版本号发布时确定，不伪造本次已发布版本。
- [ ] 按用户届时的集成要求合入 develop，再合入 master、推送相应分支；源码推送与发布发行包分开记录。保留用户现有工作树和未提交 UI 工作。
- [ ] 按已授权“全部通过后部署”核对日用目标版本/配置，在用户播放器关闭后部署并冒烟；不匹配则报告阻塞，不强切渲染器或强杀播放器。确认首次启用、第二次启动自动采集及卸载/回退资料可用。
- [ ] 提交最终验收和发布记录，明示实际部署版本、数据位置和仍未支持的播放配置。

## 覆盖索引与开发次序

| 设计要求 | 任务 | 发布证据 |
| --- | --- | --- |
| 自动加载、当前来源和就绪窗口 | 1、2、3、9 | G2、G5 |
| 严格阈值、暂停停留、连续变速 | 5、9 | G1、G2 |
| 单调时间、缺口、ACK 与故障恢复 | 3、4、5、10 | G1、G3、G4 |
| 迁移、预览回退、配置所有权 | 6、7、11 | G5 |
| 质量显示、热区/导出、大历史 | 8、9 | G6 |
| 支持清单、长稳、正式发布 | 1、9、10、11 | G1–G6 |

推荐从任务 1–3 开始：先证明正式采集器能稳定自动加载、退出和回收资源，再扩展协议和安装。任务 3 若暴露受支持配置中无法有界处理的阻塞，不应绕过继续宣称稳定；可继续开发不依赖该缺陷的迁移/验证工具，但发布保持阻塞。
