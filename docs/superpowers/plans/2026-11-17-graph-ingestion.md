# 图内采集接入实施计划

> **For agentic workers:** Use superpowers:executing-plans inline in the existing `codex/implementation` worktree. 用户已授权继续开发和验收后部署，不重复申请通用开发许可。

**Goal:** 在已验证的本机播放配置上形成原生来源/就绪事件，经现有桥接、引擎和 SQLite 生成正确详细记录。

**Architecture:** 连接链证明与渲染器就绪采集负责准入；原生状态归一化负责 epoch/事件顺序；复用现有协议、ACK、引擎和数据库。不支持或不确定的观察不伪造成完整记录。

**Tech Stack:** 既有 C++/DirectShow、.NET、SQLite、Python 隔离实播验证。

**Spec:** `../specs/2026-11-17-graph-ingestion-design.md`。

## 约束与验收重点

- 只操作 out 播放器及生成的媒体；正式目录在全部通过前不部署。
- 计时阈值保持严格 >10000 / >5000 ms。初步实播边界目标为与独立可见切换证据差异不超过50ms；误差区间跨阈值时不以测试容差更改规则。
- 失效窗口、多个图候选、跨 Stop 的晚查询、同一路径重开、延迟旧脚本均不能污染当前记录。
- 同一图被复用不能合并不同影片；预滚和队列不计入。
- 播放位置查询不能阻塞身份/状态采样，原生流线程不等待宿主或数据库。

## Task 1：准入证明与失效隔离

Files: `probes/DirectShowProbe/ConnectedRenderer.h`, `RendererWatch.h`, `GraphProbe.cpp`, `run_graph_probe.py`; `native/PotTrace.Bridge/include/ObservationFence.h`; `tests/native/ObservationFenceTests.cpp`。

- [x] 验证输入来源与实际视频输出链，不从失败的全图枚举选渲染器。
- [x] 捕获渲染器异步就绪，分离状态与位置查询时间；采集红/蓝画面变化旁证。
- [x] 先写失效测试，再实现 `ObservationFence.capture/changing/retire/publish`，验证嵌套转换及跨停止查询不可发布。
- [x] 将 fence 接入原型并复测 Stop/重开、预览、预加载及自然下一集；保存绑定和时间证据。
- [ ] 验证首次即暂停/阈值附近的停留边界，明确剩余不确定项，准入不成立时禁止后续安装。

已实播验证首次渲染器Paused就绪先于Run、9.9/10.1秒停留、4.9/5.1秒片段以及1fps；强制首次零播放的旧暂停门禁仍无可靠保证，不宣称已解决该额外控制能力。

## Task 2：原生事件归一化和桥接

Files: `native/PotTrace.Bridge/include/GraphTimeline.h`, `src/Bridge.cpp`; `tests/native/GraphTimelineTests.cpp`; 图内收集器接入文件在原型规则验证后提取。

Interfaces: `GraphTimeline.apply(GraphFact) -> vector<GraphTransition>`；Fact 携带绑定ID、来源、捕获时间、状态、可选位置/时长、倍率和种类；Transition 携带独立 epoch 序号并映射既有 PlaybackEvent。

- [x] RED：未就绪/无归属不能开会话；暂停就绪可开；同名不同路径产生独立 epoch；重复状态不重开；旧绑定晚结果不覆盖新来源；图复用下一集、seek不误增打开次数。
- [x] GREEN：实现无 Windows 依赖的归一化器并通过 native CTest。
- [x] 桥接显式图内模式，保留原生捕获时间；旧脚本仅负责启动，不更改图内会话；复用有界队列及提交 ACK。
- [x] 验证位置结果按绑定/代次归属，超时与不确定区间不补造播放时长。

实现补充：MPSC队列消除常规锁竞争丢事件；终止消息背压重试；退休绑定压缩通过5000次换片回归；暂停尾部允许同代次250ms内补齐位置并保留原UTC；迟到/已消费/缺口前流起点禁止用于新来源。两轮独立审查发现的反例均已加入测试并修复。

## Task 3：真实引擎与 SQLite 验收

Files: 隔离集成探针、必要的 native/core/storage 回归测试、验证报告。

- [x] 在独立数据目录启动真实宿主，启用 Detailed 模式；不使用离线导入替代在线链路。
- [ ] 暂停停留>10秒、播放0；两次>5秒重复片段独立排序、热区次数2；同名不同目录汇总/来源分离。
- [x] 首开、切换、预加载取消、自然下一集、无效文件、停止重开，逐项核对原生事件和数据库值。
- [ ] 重连重放、故障退出及尾部、缓冲/倍速/睡眠规则；播放稳定性与必要的全套回归。
- [ ] 独立检查重要实现风险，修复后保存可回退包；仅当全部验收通过且目标配置一致时部署。

当前证据：纯暂停0播放和热区次数2由真实Storage契约测试验证；实播暂停区间新增播放0、重复明细及同名路径分离成立。宿主强制故障后自动恢复、同SessionId重放及阈值实播通过。60项托管+5组原生已通过。仍需播放中倍率变化可信分界、长时稳定性、正式AX提取和安装回退。当前所有 GetRate 接口在2倍速仍返回1，不以基础动作通过代替完整准确性通过。详见 `docs/graph-ingestion-validation.md`。

前阶段增量（变速修复前）：真实实播DB的WPF明细/热区核对已通过，另修复了会话选择未同步热区来源；8秒隔离进程挂起恢复也通过。播放中1→2→1倍速实播明确失败（缺少RateChanged，三个阶段合并为Rate=1的一段），维持部署阻塞。标准MF/参考时钟速率接口补查无可用倍率；先验证clock与位置连续性方案的反例，不凭两个采样端点相等就修改统计引擎。仍需纯暂停全会话的真实控制证据、缓冲/睡眠、长时稳定性与正式模块/安装回退。

2026-11-17 变速修复增量：用户已确认连续变速不切片，改记片段内事件。现已接入有效速度估计、未知null、观测窗口导出、时钟失效持久标记及暂停尾部保护。半速1fps、1.1倍速及2倍速往返均已有新实播通过证据；观测停滞保守切段的失败记录同样保留。后续以 effective-rate-fix.md 和 graph-ingestion-validation.md 的新记录为准，仍未达到正式部署门槛。
