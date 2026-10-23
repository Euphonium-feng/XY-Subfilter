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
`Clear()`。代码与自动化测试已完成，真实播放器复测由用户暂缓。
