# 任务进度

## 状态

2026-10-28 用户确认：当前构建已在 PotPlayer + madVR 下连续多日实机使用，
HDR 亮度、Auto/Off/PQ、seek、五个属性页和连续输入白点均未出现问题。
原先的 seek 稳定性阻塞关闭；历史崩溃未能稳定复现，因此没有根因结论。

深层 Auto HDR 阶段一+二已合并到 `master`，最终三矩阵 run `40658251653` 全绿；
本轮没有对 tagged build 再次启动播放器实机测试。仓库已于同日切换为 public，
以避免私有仓库 Actions 分钟额度再次阻塞标准 runner。

2026-10-30 用户报告：去掉视频文件名中的 `HDR` 后，ASS + libass 的 Auto 不再映射，
手动 PQ 也未生效。已确认不是 CSRI 路径。当前注册表记录的 `text_hdr_mode` 仍为
Auto；正在增加可保留的运行态诊断，以区分属性页未下发、刷新未应用和实际渲染器
不匹配三种情况。

2026-11-07 当前活动任务：在分支 `codex/display-area-subtitle-layout` 修复
“按显示器区域排版字幕”。用户确认的目标是：16:9 显示器全屏播放 21:9 视频时，
15:9/16:9 母版字幕继续按完整显示区域排版，不随活动画面等比缩小。

第一版在实机失败：选择“自动/16:9”后字幕全部消失，“关闭”仍正常。
只读调研结论（三路独立核对，均指向同一根因）：

- 第一版把 consumer 的屏幕显示矩形当成渲染画布，直接传进以源视频像素为基准的
  文本渲染器；`GetOutputRect`（视频空间）、`GetClipRect`（屏幕空间）与
  `GetBitmap.position`（画布空间）三者不同域。
- 按 `include/SubRenderIntf.h` 示例 1（output `(0,0,720,480)`、
  clip `(0,-80,720,560)`），clip 与 position 必须与 output 同空间，允许超出
  视频并含负值，但不得使用屏幕/桌面绝对坐标。
- `RTS.cpp` 的 `cvideo_rect &= subtitle_target_rect` 在目标不能完整包含视频时
  直接返回 `E_NOTIMPL`，整帧不交付；这也是“字幕全没”的独立触发条件。
- libass 在“文本未变、仅几何变化”时复用 `m_last_frame`，布局切换后会继续
  返回旧布局帧。

第二版修复（本轮提交，待云端验证）：

- 画布改为直接取 consumer 的 `subtitleTargetRect`（关闭时字幕就靠它显示在黑边），
  Auto 用它本身，显式画幅在其内部取最大居中内接画布；不再用 `fullscreenRect`
  当渲染画布。画布不能完整包含视频时回退。
- 文本渲染改为整块零基画布：`RenderEx(videoRect=targetRect=(0,0,W,H),
  originalVideoSize=(W,H))`，libass/CSRI/legacy 共享同一排版空间，字号按画布
  高度缩放。
- 帧几何：`GetOutputRect=(0,0,videoW,videoH)`；`GetClipRect` 与
  `GetBitmap.position` 都减去 `videoOutputRect` 原点，符合 SubRenderIntf 同类空间
  约束，同时把黑边内容交给 consumer 1:1 映射。
- 严格全屏判定：`subtitleTargetRect` 尺寸与显示尺寸相差 ≤5%、包含视频、且严格
  大于视频；三项均在同一显示空间比较，窗口模式不再误判。
- `RTS::Init()` 清空 `m_last_frame`。
- PGS/SUP/DVB 本轮强制回到基线行为（`XySubRenderProviderWrapper` 去掉显示区域
  参数，`HdmvSubtitleProviderImpl` 恢复原实现），列为待办。

第二版云端验证：提交 `87480be` 的 run `42371393326` 三矩阵全绿
（`regression-release-win32`、`regression-release-x64`、`regression-debug-x64`）。
本轮尚未做播放器实机验收，结果由用户提供。

2026-11-07 实机反馈：四个选项效果都等于关闭，日志显示
`videoRect=displayRect=targetRect=0,0,1920,800`、`fullscreenRect` 同为该矩形、
`gateLarger=0`、`fallback=2`。即 PotPlayer + madVR 把 consumer 的全部矩形都按
**源视频像素**上报，不提供屏幕尺寸，"严格全屏"判据永远不成立。

第三版修复（提交 `947bb5e`，run `42375746794` 三矩阵全绿）：

- 新增只读字段 `SIZE_DISPLAY_AREA`（追加在选项枚举末尾）。
- 滤镜侧从播放器窗口取显示器尺寸：取本进程最大的可见顶层窗口，`GetMonitorInfo`
  得到 `rcMonitor`，并要求窗口矩形覆盖显示器（±5%）才算全屏，按 500 ms 缓存。
- 全屏判定放宽为：consumer 提供显示尺寸时，只要求 `subtitleTargetRect` 包含视频；
  否则仍走原来的严格尺寸匹配。
- 画布改为按**显示宽高比**在源像素空间构造：1920×800 在 16:9 显示器上得到
  1920×1080 画布（上下各 140）；显式画幅按同一规则扩展，绝不裁剪视频。

第四版（简化收口）：实机反馈"每次播放都要重新切换设置才生效"，且此前
`EnumWindows` 在播放线程上阻塞导致全屏卡死/闪退。

- 显示尺寸改为**持久化**：属性页在 UI 线程用 `GetSystemMetrics` 读取并写入
  `General\display_area_width/height`，启动即加载，新开播放立刻生效。
- 删除整套 consumer 全屏推断（`EvaluateFullscreenGate` 及 4 个标志位）、
  `RefreshDisplayArea()`、窗口枚举/监视器查询，以及已无调用者的
  `SetFrameGeometry`。净删约 400 行。
- 显示尺寸有两个来源：`SIZE_DISPLAY_AREA`（持久化）优先，其次 consumer 的
  `fullscreenRect`。两者都无则回退原行为。
- 播放线程零窗口调用；画布解析结果按输入缓存，几何直接写入渲染帧，无每帧分配。

第五轮：位图字幕 + 属性页排版。

- PGS/SUP/DVB 改为直接发布设计分辨率画布（`ResolveBitmapCanvas`）：设计比例与
  显示比例一致时 `output=clip=设计画布`、位图偏移 0，consumer 把整块放大到屏幕，
  字幕进入黑边且不重采样；比例不一致回退原行为。
- 画布按设计分辨率缓存在 provider 内，`Invalidate()` 时失效，下一次请求重新读取
  consumer 的布局选项和显示尺寸。
- 属性页修复：H/V（水平/垂直）标签宽度由 9/8 DLU 扩到 30–34 DLU，不再被截成
  单个汉字；四个缓存编辑框由 31 扩到 40 DLU，spin 加 `UDS_ALIGNRIGHT` 后不再
  压住右对齐数字；日志按钮移到状态行右侧，不再覆盖"字幕显示区域"下拉。
- 中英两块资源同步修改，控件顺序与坐标一致。

2026-11-08 构建阻塞与修复（run `42396774723`，提交 `10f7ba97`）：

- 三矩阵均编译失败，`src/subpic/subtitle_display_layout.cpp:297` 的
  `ResolveBitmapCanvas()` 引用了未定义变量 `design_height`（C2065 / C2737）。
  交叉相乘应使用 `display_height * design_size.cx`。
- 已修正该行，并增加“同宽但设计画布更高必须拒绝”的单测，防止再写成
  `design_size.cy` 这类等宽自比较。
- 同时修复位图画布缓存只在设计分辨率变化时重算的问题：`Invalidate()` 现在
  会清掉 `m_canvas_cached`，布局选项或显示尺寸变化后不会继续发布旧几何。
- 以上修改尚未经过云端验证；三矩阵全绿后才能把本条标记为完成。
- run `42470777553`（提交 `3acfdb5`）在 `Install VSNASM` / `Install VSYASM`
  阶段失败：安装脚本从 `github.com/Microsoft/vswhere/releases` 下载
  `vswhere.exe` 解析失败，重跑仍复现。工作流现在先复制 runner 自带的
  `%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe` 到两个
  安装脚本目录，避免该下载依赖。

历史实现（第一版，已被本轮替换）：

- 已新增纯函数模块 `src/subpic/subtitle_display_layout.{h,cpp}`，定义
  Off/Auto/16:9/16:10/21:9/32:9/4:3 和显示区域内接画布计算。
- 已追加内部选项 `BOOL_USE_DISPLAY_AREA_FOR_SUBTITLES` 与
  `INT_SUBTITLE_DISPLAY_ASPECT`；默认关闭和 Off，未重排既有选项 ID。
- 已声明并持久化 `use_display_area_for_subtitles`、
  `subtitle_display_aspect` 两个 General 注册表值。
- `XySubFilter` 显式读取外部 consumer 的 `fullscreenRect` /
  `displayModeSize`，并用独立的 `BOOL_CONSUMER_IS_FULLSCREEN` 记录字段是否
  确实大于活动视频矩形；窗口模式、字段缺失和无效矩形路径回退视频矩形。
- `XySubRenderProviderWrapper2` 使用统一的 `SubtitleDisplayLayout::ConsumerInput`
  解析文本目标矩形，避免与 provider 各自维护一套优先级。
- `HdmvSubtitleProviderImpl`（PGS/SUP/DVB）使用
  `SubtitleDisplayLayout::ResolveBitmapPlacement` 设置 `GetOutputRect` /
  `GetClipRect`：源位图仍是视频坐标，consumer 按显示区域缩放/裁剪。
- 已新增合成单测 `test_subtitle_display_layout.h` 并接入项目文件，覆盖显示矩形、
  画幅内接、字段缺失回退、consumer 输入和位图放置。
- More 页已增加“字幕显示区域”下拉与中英文资源；
  `Test-ResourceLocalization.ps1` 已通过（53 个 UI 字符串、16 个对话框）。
- `XySubRenderProviderWrapper` 仅对 `CRenderedHdmvSubtitle` 开启显示区域解析，
  VobSub/SSF 保持旧行为；该通用 wrapper 与 `HdmvSubtitleProviderImpl` 在
  output/clip 变化时调用 `Invalidate()`，避免同字幕区间返回旧布局帧。
- `ResolveInput` 增加默认构造函数；修复 Win32 Release 下未初始化字段导致的
  `MissingDisplayFallsBackToVideo` / `InvalidRectFallsBackSafely` 失败。
- 功能提交 `4b64448` 的 run `42259761429` 三矩阵全绿；Win32 首轮为已知
  yasm `STATUS_DLL_NOT_FOUND`，重跑通过。末次文档提交 `4f1b00a` 的 run
  `42262535081` 同样三矩阵全绿。
- PGS/SUP/DVB 依赖 consumer 按标准 `GetOutputRect`/`GetClipRect` 语义执行缩放。
  外部 consumer 若忽略该语义，实际画面仍需用户手动验收；不能仅凭代码宣称
  所有渲染器都已生效。
- 诊断日志追加 video/display/chosen/fullscreen/target/scale/fallback 字段；
  已做参数审计，并随 run `42259761429` 通过云端编译与回归测试。

可靠性收口代码已完成，并通过云端三矩阵 CI（run `40552057870`；
最终记录提交 run `40553848048`）：

- `src/subpic/hdr_evidence.{h,cpp}` 提供可单测的 HDR 证据合并和 Auto 判定。
- 属性页运行时下拉项、缓存窗口和提示文案已接入中英文资源。
- More 页增加“打开日志”按钮，打开 `%TEMP%\xy_hdr_diag.txt`。
- `scripts/Test-SeekStability.ps1` 取代本地 `.debug/repro-seek.ps1`。
- `scripts/Test-ResourceLocalization.ps1` 在 `debug-x64` 构建前校验资源。
- HLG 保留现有处理，但不在个人使用范围内，也未做专项验收。

## 0.1.0 发布

2026-10-28 已完成首个正式版本发布：

- annotated tag `0.1.0` 指向提交 `65171fd76fdb576935114b9d5d84da7202c9d677`。
- GitHub Release：`https://github.com/Euphonium-feng/XY-Subfilter/releases/tag/0.1.0`
- `XY-SubFilter-0.1.0-win64.zip`：5,892,808 bytes，SHA-256
  `9110DA20D4008217102FD7B1AE5EC0B94C58BF3AF184709B556970A3BEB31FA3`
- `XY-SubFilter-0.1.0-win32.zip`：5,223,006 bytes，SHA-256
  `07027A9E691D8EF1C8963B9ADB69E07A7DB7B2B3C25CF29D8377D5249937B732`
- 发布源码最终三矩阵 run `40658251653`：`release-win32`、`release-x64`、
  `debug-x64` 全部通过。
- 两个 ZIP 已从 Release 回读，SHA-256 与本地产物一致；每个 ZIP 均包含
  `XySubFilter.dll`、`VSFilter.dll` 及安装/卸载脚本。

发布验收基于自动化测试和云端构建；没有对 tagged build 启动 PotPlayer 做实机复测。
此前多日 PotPlayer + madVR 使用记录仍有效，但不能替代本次发布包的单独冒烟记录。

## 已完成

### 第一轮：位图字幕 HDR 映射与初版中文化

| 阶段 | 内容 |
| --- | --- |
| 调研 | 梳理 SDR/HDR 两条管线，定位四个过曝成因 |
| 0 | `src/subpic/hdr_color.{h,cpp}`：PQ OETF/EOTF、nits 与码值互转、BT.1886 线性化、ARGB 映射 |
| 1 | `CompositionObject::InitColor` 调色板级映射；`SetHdrConfig` 沿 `CBaseSub` → `CHdmvSub`/`CDVBSub` → 各 Provider 直通 |
| 2 | 自动判定（consumer `yuvMatrix` 以 `2020` 结尾）+ 手动覆盖 |
| 3 | 验证 P010/P016 路径免改：PQ 8bit 码经 `src<<8` 恰好得到正确 10bit 码 |
| 4 | `INT_PGS_HDR_MODE` / `INT_PGS_HDR_WHITE_NITS` / `INT_UI_LANGUAGE` 选项、注册表持久化、属性页控件 |
| 5 | `VSFilter.rc` 简体中文语言块 |
| 6 | `test_hdr_color.h` 单测 |
| 7 | README 中英双版 |

注意：此轮的中文资源块因缺少 `AFX_TARG_CHS` 定义而**从未被编译**，实际在第二轮才真正生效。

### 第二轮：界面修复与文本字幕 HDR 映射

| 阶段 | 内容 |
| --- | --- |
| A | 启用 `AFX_TARG_CHS`；26 处对话框字体改为 Microsoft YaHei UI 9pt；修正误译的窗口类名 `"静态色"`；修正 More 页（330→400）与 Main 页（244→262）高度；属性页在宿主 UI 线程切换资源语言 |
| B | `INT_TEXT_HDR_MODE` 全链路：选项 ID、注册表键、属性页下拉、`ResolveTextHdrConfig` |
| C | libass 路径：`MapTextColor` 辅助函数 + 六处 call site + `CRenderedTextSubtitle::SetHdrConfig` + 双层缓存失效 |
| D | 旧 VSFilter 渲染器：`TransColorWithHdr` + 配置随 `DoRender`/`RenderOneSubtitle` 下传 |
| E | 文本颜色单测、docs 同步 |

### 第三轮：README 重写

`README.md` 改为简体中文（GitHub 默认展示），`README.en.md` 保留英文版，两版互链；标题插入显式锚点避免 emoji 锚点失效。

### 第四轮：HDR 配置缓存刷新修复

| 阶段 | 内容 |
| --- | --- |
| A | 撤回 `RequestFrame` 内每帧回调 consumer 读取选项的实现；该路径导致真实播放稳定性退化 |
| B | HDR 模式/白点变化后，经 `ISubRenderConsumer2::Clear()` 清除 consumer 缓存的字幕帧 |
| C | 位图字幕的“实际后端”显示改为 `Bitmap (internal)`，不再把 PGS 误报成 VSFilter 文本后端 |

当时只证明了配置读取链路存在缺口；后续测试确认 `Off`、`PQ 203`、`PQ 50` 有可见差异，
该问题已闭环。`Relative white` 仍只作为兼容值保留，不回到界面。

目标语义（明确，不再把 SDR 直通当目标）：SUP 调色板先按 HDR/PQ 曲线预编码到
8bit SDR 容器码值，再由 madVR 的 HDR 处理恢复绝对亮度。SDR 直通只能作为对照，
不是修复方案。

### 第五轮：HDR 配置可靠性

| 阶段 | 内容 |
| --- | --- |
| A | Auto 优先读取 consumer 视频引脚的 `VIDEOINFOHEADER2` 传递函数；PQ/HLG 为 HDR，明确 SDR 曲线覆盖 BT.2020 矩阵回退 |
| B | HDR 配置带递增 generation，贯穿 `CompositionObject`、Hdmv SubPic、文本缓存和外层 bitmap ID |
| C | `SetRgbOutputLevel()` 改为按解析后的配置判断 PQ，Auto 解析成 PQ 时也强制 PC 全幅 |
| D | 属性页 Apply 合并 RGB/HDR 刷新；`ISubRenderConsumer2::Clear()` 只调用一次并记录结果 |
| E | 属性页显示实际 provider、解析后的 HDR 模式、白点、缓存清理状态和构建短哈希 |
| F | 增加调色板像素集成测试、generation 测试和传递函数分类测试 |

当前状态：代码修改和测试用例已完成；云端 CI 三矩阵全绿，后续多日实机使用通过。

### 第六轮：Auto 判定与属性页布局

| 阶段 | 内容 |
| --- | --- |
| A | `XySubFilterConsumer` 从输入 `VIDEOINFOHEADER2` 生成范围/矩阵并填充 `yuvMatrix` |
| B | HDR10/HDR10+/DoVi media side data 通过 `isHdrVideo` 事件通知 provider，不进每帧配置读取热路径 |
| C | HDMV provider 按完整 HDR 配置判断 SubPic 失效，矩阵回退变化会重新渲染 |
| D | 属性页 HDR 区域初步改为两列四行；HDR 诊断独占底部状态行并配“打开日志”按钮；Relative white 从界面隐藏（尺寸与分栏由第七轮最终确定） |
| E | 增加视频矩阵分类测试，并把调色板测试改为复用同一 `CompositionObject` |

### 第七轮：属性页宽幅布局与白点输入修复

| 阶段 | 内容 |
| --- | --- |
| A | XySubFilter 的 Main / More / Timing / Paths / About 统一为 `360×280` DLU |
| B | Main 与 More 改为左右两栏；More 从 `247×420` 压缩到 `360×280` |
| C | Timing / Paths / About 扩展到同一宽幅尺寸，列表与路径按钮同步加宽 |
| D | 即时应用前后捕获并恢复编辑框选区和插入符 |
| E | 白点值文本未变化时不重复 `SetWindowText`，避免输入过程中光标回到行首 |

当前状态：代码与资源修改完成；用户已确认 Main / More / Timing / Paths / About
五个标签页和连续输入白点的体验正常。

### 第八轮：可靠性与诊断收口

| 阶段 | 内容 |
| --- | --- |
| A | 提取 `HdrDetection::HdrEvidence` / `ResolveAutoPq`，用单测覆盖优先级和引脚顺序 |
| B | 运行时属性页文本改用 `IDS_XY_UI_*` 资源，补齐 Paths/About 的中文内容 |
| C | 增加资源 ID、控件 ID 和硬编码文本检查脚本 |
| D | More 页增加 HDR 诊断日志入口 |
| E | 将 seek 压力脚本迁移到 `scripts/Test-SeekStability.ps1`，支持 `-PlanOnly` 和 JSON 输出 |
| F | README、PROJECT、ARCHITECTURE、CONSTRAINTS、DECISIONS 同步当前实现 |

### 第九轮：深层 Auto HDR 判定调研

2026-10-28 在本地分支 `codex/research-hdr-auto-depth` 完成静态调研，
结论记录在 `docs/research/hdr-auto-detection.md`。本轮未修改生产代码，
未运行播放器、单元测试或云端构建，也未推送远程。

- GO：修正 DirectShow 活动主视频路径、MPEG-2/Dirac 嵌套 VIH 和证据顺序问题。
- GO：增加本地 MKV 有界 EBML 头部探针，提取 Colour、mastering 和 CLL。
- CONDITIONAL：仅在 CodecPrivate 已暴露时解析 `avcC`/`hvcC`/`av1C`。
- NO-GO：完整码流扫描、视频 tap、帧级分析、madVR/LAV 私有接口主判据。
- 暂缓：MP4/MOV、TS/M2TS、BDMV、网络流和 HLG/DoVi 专项分类。

### 第十轮：深层 Auto HDR 阶段一+二实现

实现已落在分支 `codex/deep-hdr-auto-implementation`，并通过云端三矩阵：

- `HdrEvidenceSet` 以顺序无关方式合并 transfer、matrix 与 mastering/CLL 证据。
- `HdrGraphProbe` 只沿 consumer 活动视频路径读取 VIH2、MPEG-2、Dirac 媒体类型。
- `hdr_mkv_probe` 有界解析本地 MKV Tracks/Colour/mastering/CLL，H.273 枚举显式转换。
- 多视频轨只在 codec、宽高、帧时长唯一匹配时采用；文件名已提示 HDR 时跳过文件 I/O。
- 媒体签名变化和 side-data 事件只在最终 Auto PQ/Off 变化时刷新字幕。
- 新增合成 EBML、图拓扑、嵌套媒体类型和证据顺序测试。

### 第十一轮：ASS 文本 HDR 可观测性与控件语义

2026-10-30，针对 `libass` + ASS 场景中“手动 PQ 看似无效”的反馈：

- 诊断日志从覆盖写改为追加，超过 512 KiB 后轮换。
- 断开 consumer 时只追加 disconnect 事件，不再用全零证据覆盖上一次有效探测。
- 每次 HDR 配置刷新记录实际后端、位图/文本请求模式、解析后模式、白点、
  generation、yuvMatrix 和当前证据。
- HDR 状态行增加 `Bitmap:` / `Text:` 前缀。
- 属性页标签改为“Bitmap HDR / Text HDR”和“位图字幕 HDR / 文本字幕 HDR”，
  明确 PGS/SUP 与 ASS/SSA/SRT 使用两套独立选项。
- 云端三矩阵 run `41018526246` 已全绿；尚未用用户的移动硬盘素材复现，
  也未完成 ASS 手动 PQ 的功能闭环。

## CI 记录

| Run | 内容 | 结果 |
| --- | --- | --- |
| `39944694745` | 仓库基线 | success |
| `39949013750` | 位图 HDR 映射 + 初版中文化 | success |
| `39951652326` | README 重写 | success |
| `39962614076` | 界面修复 + 文本 HDR 映射 | failure（两项，见下） |
| `39963898276` | 修正测试期望 | success |
| `39965292609` | docs 记录 | success |
| `39997437527` | HDR 信号检测、generation、合并刷新与调色板集成测试 | success |
| `40007950976` | consumer 矩阵上报、HDR side data 事件、SubPic 失效修复与属性页重排 | success |
| `40011453627` | 全图 HDR 信号扫描、无 colour-info 的 PQ/HLG 码检测 | success |
| `40068012174` | 文件名 HDR 回退、BT.2020 回退与连接诊断文件 | success |
| `40550761466` | 可靠性收口首轮 | failure（win32 文件选择框参数错误） |
| `40552057870` | 可靠性收口复跑（release-win32 / release-x64 / debug-x64） | success |
| `40553848048` | 最终 CI 记录提交复核 | success |
| `40564644382` | 深层 Auto HDR 首轮 | failure（runner 未启动：GitHub 账户计费/额度限制） |
| `40569779347` | 深层 Auto HDR 实现（release-win32 / release-x64 / debug-x64） | success |
| `40658251653` | 合并后最终三矩阵（`0.1.0` 发布源码） | success |
| `41018526246` | ASS 文本 HDR 诊断与控件语义修复 | success |
| `42248047195` | 显示区域首轮 | failure（`RECT` 初始化列表编译错误） |
| `42249026874` | Win32 显式字段修复 | failure（Win32 Release 单测未初始化字段） |
| `42255336219` | 缓存失效 + Win32 单测初始化修复 | success（三矩阵） |
| `42257724330` | 类型 gating + README/docs | failure（`pSubStream.p` 编译错误） |
| `42259761429` | 功能提交 `4b64448` | success（三矩阵；Win32 yasm 首轮失败后重跑通过） |
| `42262535081` | 文档提交 `4f1b00a` | success（三矩阵） |

`39962614076` 的两项失败：

- **x64**：`TextColorPqKeepsAlphaAndLowersWhite` 期望值写错。输入 `0xFF808080` 是中间灰，PQ 映射后为 106（128 → 0.19 线性 → 38.8 nits → 码 106），不是参考白的 148。已拆分出纯白用例验证 148 端点。
- **win32**：`yasm` 退出码 `-1073741515`（`STATUS_DLL_NOT_FOUND`）。runner 环境偶发，同一工作流上一轮与下一轮均通过，与代码无关。

## 已关闭的真实验证

- PotPlayer + madVR 多日使用通过，HDR 亮度、Auto/Off/PQ、seek 和文本/位图字幕均正常。
- Main / More / Timing / Paths / About 五个标签页和连续输入白点已由用户确认无问题。
- 多日使用未复现历史 seek 崩溃；旧 `D:\PotPlayer\bugreport.txt` 仍不属于本轮证据。
- HLG 未进入个人使用范围，保留为未专项验证项，不作为本轮阻塞。

## 下一步

### 按显示器区域排版字幕

1. 记录类型 gating、README 与 docs 最终提交的三矩阵 run ID（三矩阵必须全绿）。
2. 交付用户手动验收 PotPlayer + madVR：16:9 显示器全屏播放 1920×800
   ASS/SSA/SRT，依次检查 `关闭` / `自动` / `16:9`；关闭必须与现状一致，
   自动与 16:9 字幕应进入上下黑边且字号不缩小；窗口模式无变化。
3. PGS/SUP/DVB 使用同一选项，但只发布设计分辨率画布，不做重采样；设计比例与
   显示比例一致时才生效，否则回退。需要用实际 PGS/SUP 素材验收黑边落点与
   缩放是否正确。
4. 代理不得启动 PotPlayer 或进行自动实机测试，实机结果由用户提供。

### 深层 Auto HDR

阶段一+二代码已合并；run `40658251653` 已确认
`release-win32`、`release-x64`、`debug-x64` 全绿。本轮按计划只做自动化验收，
未启动 PotPlayer 或进行人工画面测试。codec extradata 仍不在本轮范围。

### 发布后可选人工验证

1. 解压 `XY-SubFilter-0.1.0-win64.zip`，用管理员权限运行
   `Install_XySubFilter.bat`，在 PotPlayer + madVR 中做一次字幕、HDR 和 seek 冒烟。
2. 检查五个属性页的中英文标签与下拉项、More 页“打开日志”在日志存在/缺失两种状态下
   的行为，并把结果补写到本文件。
3. 该冒烟用于补齐发布包实机记录，不再是代码合并或 Release 发布的阻塞项。
