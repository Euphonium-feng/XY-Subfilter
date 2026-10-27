# 项目目标

XY-SubFilter 是 DirectShow 字幕渲染滤镜，供 MPC-HC / MPC-BE / PotPlayer + madVR 等播放器使用，以 libass 渲染 ASS/SSA，同时保留原有 PGS(SUP)/VobSub/DVB 位图字幕路径。

## 当前工作

已完成的增强：

1. **SUP/PGS 与 ASS/SRT 字幕的 HDR 白点映射** —— 解决 HDR 片源下字幕过曝。
2. **界面简体中文化** —— 并修复了中文资源从未被编译、对话框使用光栅字体导致的标签空白。
3. **属性页宽幅布局与输入修复** —— 所有 XySubFilter 标签页统一为 `360×280` DLU，
   More 页改为横向两栏；白点输入在即时应用刷新后保留插入符和选段。
4. **可靠性与诊断收口** —— HDR 证据选择逻辑独立可测，运行时界面文本全部走中英文资源，
   More 页可打开 HDR 诊断日志，seek 压力脚本纳入仓库。

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
- 属性页统一尺寸与横向排版；数字编辑框在即时应用期间不得丢失光标位置。

排除：

- 16bit 字幕面（调色板级 8bit 映射已满足平色字幕精度要求，接口预留）。
- 修改 `SubRenderIntf.h` 协议本身。

## 验收标准

1. 同一 SUP 或 ASS 字幕在 HDR 片源下不再过曝：字幕白对应 203 nits 而非数千 nits。
2. `INT_PGS_HDR_WHITE_NITS` 可调，默认 203；改动后字幕亮度随之变化。
3. 位图与文本各自的 HDR 模式三档（自动/关闭/PQ）均可用；`Relative white` 只保留内部
   ABI 值，不出现在界面；自动模式下 SDR 片源行为与改动前逐字节一致。
4. 简体中文界面可用，英文界面不受影响，可手动切换。
5. 新增单元测试与既有回归测试在 GitHub Actions 三矩阵上全绿。
6. XySubFilter 的“主要 / 更多 / 时间轴 / 路径 / 关于”使用相同页面尺寸；
   白点输入连续键入时不需手动移动插入符。

## 当前状态

2026-10-28 用户确认：当前构建已在 PotPlayer + madVR 下连续多日实机使用，
HDR 亮度、Auto/Off/PQ、seek、五个属性页和连续输入白点均未出现问题。
原先的 seek 稳定性阻塞关闭；2026-05-09 的旧 `bugreport.txt` 仍不能作为本轮证据。

Auto 的正式优先级为 side data、明确 PQ/HLG、文件名 HDR、明确 SDR、BT.2020 /
consumer `yuvMatrix` 回退、Off。HDR 配置仍由 generation 驱动缓存失效，属性页
Apply 仍合并 RGB/HDR 刷新并调用一次 `ISubRenderConsumer2::Clear()`。

HLG 不在个人使用范围内，不实现专用映射、不做专项验收；文档不得把 HLG 观感
描述为已验证。本轮可靠性改动待云端三矩阵 CI 完成后记录 run ID。
