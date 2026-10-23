# 项目目标

XY-SubFilter 是 DirectShow 字幕渲染滤镜，供 MPC-HC / MPC-BE / PotPlayer + madVR 等播放器使用，以 libass 渲染 ASS/SSA，同时保留原有 PGS(SUP)/VobSub/DVB 位图字幕路径。

## 当前工作：SUP 字幕 HDR 过曝修复

### 需求

1. 翻译为中文，用语符合视频渲染领域。
2. 调研分析 SUP 字幕在 SDR 与 HDR 下的渲染管线，定位 HDR 下过曝的原因。
3. 优化渲染管线：可行时内置 HDR 映射，白点对应亮度可配置，默认 203 nits。

### 范围

纳入：

- PGS / DVB 位图字幕的调色板级色彩映射（`CompositionObject::InitColor`）。
- VSFilter 内部把 8bit 字幕面写入 P010/P016 的混合路径。
- HDR 模式自动判定与手动覆盖。
- 白点亮度选项（默认 203 nits）。
- 属性页与托盘菜单简体中文化。

排除：

- ASS/SRT 等文字字幕的 HDR 映射（接口预留，不在本次实现）。
- 16bit 字幕面（调色板级 8bit 映射已满足平色字幕精度要求，钩子预留）。
- 修改 `SubRenderIntf.h` 协议本身。

### 验收标准

1. 同一 SUP 字幕在 HDR 片源下不再过曝：字幕白对应 203 nits 而非数千 nits。
2. `INT_PGS_HDR_WHITE_NITS` 可调，默认 203；改动后字幕亮度随之变化。
3. HDR 模式三档（自动/强制开/强制关）均可用；自动模式下 SDR 片源行为与改动前逐字节一致。
4. 简体中文界面可用，且英文界面不受影响。
5. 新增单元测试在 GitHub Actions 上全绿。
