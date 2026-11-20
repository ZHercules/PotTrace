# PotPlayer 26.04.22 本机接入验证

验证日期：2026-11-17。来源为日用播放器，测试仅在项目 `out/player` 副本中进行；媒体为脚本生成的 30 秒无声 AVI。

| 能力 | 实测结果 |
| --- | --- |
| 宿主版本 | HostGetVersion 返回 26.4.22 |
| 无媒体启动 | 收到 OnInitialize；不产生 Open |
| 首次启用 | 参数选项 → 扩展 → Playback Statistics → 勾选 PotTrace；默认未勾选 |
| 启用持久化 | 开启存储到 INI 后，ExtensionSection.PlaybackStatistics 保存脚本文件名；重启后播放事件自动到达 |
| PlaybackStart | 一个整数参数，总时长，毫秒 |
| PlaybackTime | 两个整数依次为总时长、当前位置，均为毫秒；与早期参考假设相反 |
| 播放事件 | Open、Start、Time、Pause、Resume、Complete、Close 已在真实播放中观察 |
| 暂停超过 10 秒 | 暂停稳定后连续 11 秒进度变化 0 毫秒；暂停期间没有周期 Time 回调 |
| 正常退出 | Close 可观测；未观察到可靠 OnFinalize，不能依赖它关闭后台 |
| Win32 查询 | WM_USER 0x5002 返回总时长；0x5004 返回进度；0x5006 返回播放 2、暂停 1 |
| 跳转 | WM_USER 0x5005 指定绝对毫秒位置生效；不另发专用跳转回调，需检测位置不连续 |
| DLL 及 UTF-8 | HostLoadLibrary、HostGetProcAddress、HostCallProcInt 可用；HostString2UIntPtr 指向的字节可同步复制，中文和空格路径原样保存 |

原生查询常量交叉参考：[thePotPlayerServer 作者代码](https://github.com/skaveesh/thePotPlayerServer/blob/main/thePotPlayerServer.ahk)。本机测试日志与结果位于 `out/player/Extension/Playback/Statistics/probe-native.tsv`、`out/probe-result.json`，不将测试生成路径和二进制播放器提交到仓库。

测试环境：Windows 10.0.26300，MSVC 19.51.36243，Windows SDK 10.0.26100.0，项目本地 .NET SDK 10.0.404 / runtime 10.0.15。受限环境中 MSVC 配置检测与部分 NuGet 用户配置访问失败，构建/测试通过执行工具的批准权限运行；没有修改用户目录 ACL。

WebView2 的测试环境变量 WEBVIEW2_USER_DATA_FOLDER 显式设置为项目 `out/player-webview`。初次测试曾因尝试写入用户 Roaming 目录出现权限提示，后续重试改用独立目录，未改动原安装目录权限。

复测：生成测试媒体后，先运行 `probes/PotPlayerProbe/run_probe.py` 完成隔离副本的首次勾选，再运行 `verify_callbacks.py` 验证自动初始化与播放回调。Python UI 自动化依赖安装在项目工具目录，不作为插件运行依赖。

正式桥接已补测双实例、后台崩溃恢复与主动退出、重复区间、2 倍速、同文件重开和仅入队排除；自包含发布包的实际安装播放链路也已通过一轮。完整数字和未解决的启动回调延迟见 [验收记录](../acceptance.md)。整片/AB 循环、休眠及更多构建仍待实机验证，不能表述为所有场景兼容性已通过。
