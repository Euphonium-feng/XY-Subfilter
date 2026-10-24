# 项目目标

XY-SubFilter 是 DirectShow 字幕渲染滤镜，供 MPC-HC / MPC-BE / PotPlayer + madVR 等播放器使用，以 libass 渲染 ASS/SSA，同时保留原有 PGS(SUP)/VobSub/DVB 位图字幕路径。

## 当前工作

两项已完成的增强：

1. **SUP/PGS 与 ASS/SRT 字幕的 HDR 白点映射** —— 解决 HDR 片源下字幕过曝。
2. **界面简体中文化** —— 并修复了中文资源从未被编译、对话框使用光栅字体导致的标签空白。

## 需求

1. 翻译为中文，用语符合视频渲染领域。
2. 调研分析 SUP 字幕在 SDR 与 HDR 下的渲染管线，定位 HDR 下过曝的原因。
3. 优化渲染管线：可行时内置 HDR 映射，白点对应亮度可配置，默认 203 nits。

## 范围

纳入：

- PGS / DVB 位图字幕的调色板级色彩映射（`CompositionObject::InitColor`）。
- ASS / SSA / SRT 等文本字幕的颜色映射（libass 与旧 VSFilter 两条渲染路径）。
- VSFilter 内部把 8bit 字幕面写入 P010/P016 的混合路径（经分析确认无需修改）。
- HDR 模式自动判定与手动覆盖，位图与文本各有独立开关。
- 白点亮度选项（默认 203 nits），两条路径共用。
- 属性页、托盘菜单与提示文案的简体中文化，支持中英切换。

排除：

- 16bit 字幕面（调色板级 8bit 映射已满足平色字幕精度要求，接口预留）。
- 修改 `SubRenderIntf.h` 协议本身。

## 验收标准

1. 同一 SUP 或 ASS 字幕在 HDR 片源下不再过曝：字幕白对应 203 nits 而非数千 nits。
2. `INT_PGS_HDR_WHITE_NITS` 可调，默认 203；改动后字幕亮度随之变化。
3. 位图与文本各自的 HDR 模式四档（自动/关闭/PQ/相对白）均可用；自动模式下 SDR 片源行为与改动前逐字节一致。
4. 简体中文界面可用，英文界面不受影响，可手动切换。
5. 新增单元测试与既有回归测试在 GitHub Actions 三矩阵上全绿。

## 当前状态

功能验收项已实现并通过云端 CI（run `39965292609`，release-win32 /
release-x64 / debug-x64 均 success）。真实播放器存在**疑似稳定性问题**：
用户报告 PotPlayer + madVR 的 seek 路径至少崩溃两次，当前尚未稳定复现，也尚未确认
与本次改动存在因果关系。真实显示环境下的稳定性、观感与排版见 `TODO.md`。

2026-10-24 真实播放反馈：SUP/PGS 在 HDR 下切换 HDR 模式或白点没有可见变化。
随后尝试的 provider 每帧读取 consumer 版本（run `39984554888`）造成真实播放
稳定性退化和花屏，已撤回。当前实现改为：consumer 视频引脚提供 PQ/HLG 提示时
优先采用，HDR 配置带递增 generation 并贯穿 provider、SubPic、调色板和帧 ID；
属性页 Apply 会把 RGB/HDR 相关选项合并成一次刷新，再调用 consumer 的
`Clear()`。代码与自动化测试已完成，CI run `39997437527` 三矩阵全绿；
真实播放器复测由用户暂缓。

后续真实播放确认 PQ 映射本身有效，但 Auto 在 HDR 片源下仍解析为 Off，与 SDR
相同。根因是内置 `XySubFilterConsumer` 从未填充对 provider 报告的 `yuvMatrix`，
使 Auto 的 BT.2020 回退失效；同时 HDMV provider 仅按 generation 判断 SubPic
失效，矩阵回退变化时可能复用旧帧。现已补充视频引脚矩阵/传递函数上报、HDR media
side data 事件提示、按完整配置判断 SubPic 失效，并重排属性页 HDR 区域。
Relative white 已从界面移除。CI run `40007950976` 的 release-win32 /
release-x64 / debug-x64 三矩阵全绿；真实播放效果与稳定性仍待复测。

该版实测仍为 `PQ < Auto = SDR`。进一步确认 HDR 测试文件本身是
BT.2020/PQ，而当前播放的 SDR 文件虽是 10bit，但颜色元数据为 BT.709；
因此不能按位深猜测 HDR。检测现改为扫描整张 DirectShow 图，优先寻找解码器
输出引脚上的 PQ/HLG，并允许未设置 colour-info 标志但传递函数码明确为
PQ/HLG 的情况。CI run `40011453627` 三矩阵全绿；实机复测暂缓，等待用户恢复测试。

该轮仍未改善 Auto。现增加 BT.2020 矩阵位作为独立回退信号，并在 consumer 连接时
把每个视频引脚的 filter CLSID、pin、media type、`dwControlFlags` 写入
`%TEMP%\xy_hdr_diag.txt`。同时通过源滤镜 `IFileSourceFilter::GetCurFile()` 读取
视频文件名，把标准化命名中的独立 `HDR` 标记作为回退；`HDRip` 这类词不会误匹配。
CI run `40068012174` 三矩阵全绿；实机复测待用户恢复。
