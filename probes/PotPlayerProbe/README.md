# 本机接入探针

仅用于隔离播放器副本，不部署到用户现有安装目录。复制运行所需的 EXE/DLL、默认皮肤与 English.ini 到 `out/player`，不复制 History、Playlist 或用户配置。

1. 用 MSVC x64 构建本目录的 CMake 项目。
2. 将 DLL 与 `PlaybackStatistics - PotTraceProbe.as` 放到副本的 `Extension/Playback/Statistics`。
3. 执行 `python probes/PotPlayerProbe/make_fixture.py` 生成测试 AVI。
4. 执行 `run_probe.py` 在副本的设置中启用 INI 与统计扩展，并检查暂停/恢复。工具设置独立 WebView2 数据目录，始终隐藏启动播放器。
5. 执行 `verify_callbacks.py`，退出码 0 表示自动初始化、播放状态查询、暂停 11 秒及关键回调验证通过。

日志写在探针 DLL 同目录，不通过播放器的用户配置目录写入。脚本中的宿主签名保持原始数值顺序，日志 a/b 列实际为 duration/position；正式适配器必须纠正语义名称。
