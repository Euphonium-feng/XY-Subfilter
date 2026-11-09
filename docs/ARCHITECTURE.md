# 架构

## 模块职责

| 模块 | 路径 | 职责 |
| --- | --- | --- |
| 过滤器 | `src/filters/transform/vsfilter/` | DirectShow 滤镜：XySubFilter（provider）、VSFilter/DirectVobSub（内嵌字幕烧录）、XySubFilterConsumer（合成端） |
| 基类 | `src/filters/transform/basevideofilter/` | `CBaseVideoFilter`、色彩标志传播、HDR 侧数据透传 |
| 字幕 | `src/subtitles/` | 解析器（PGS/DVB/VobSub/ASS…）、光栅化、libass/CSRI 后端、位图字幕 Provider |
| 子图 | `src/subpic/` | 字幕面分配与混合（`MemSubPic`）、色彩转换表（`color_conv_table`）、HDR 色彩与证据判定、Alpha 混合器 |
| 公共头 | `include/` | `SubRenderIntf.h`（provider/consumer 协议）、DX/AviSynth/VirtualDub SDK 头 |

## SDR 渲染管线（现状基准）

```
.sup 文件
  └─ SupFileSubtitleProvider::Open / ThreadProc        hdmv_subtitle_provider.cpp:423,462
       读 "PG" 同步字、90kHz PTS、段长度，整文件预解析进 CMemFile
  └─ CHdmvSub::ParseSample                              HdmvSub.cpp:135
       ├─ 0x16 PCS → ParsePresentationSegment → ParseCompositionObject
       ├─ 0x14 PDS → ParsePalette（8bit Y,Cr,Cb,T，每项 5 字节）
       ├─ 0x15 ODS → ParseObject（RLE 字节流，可分段）
       ├─ 0x17 WDS → 直接 SkipBytes（窗口定义未使用）
       └─ 下一 PCS 到达时 EnqueuePresentationSegment（快照 CLUT、复制 RLE）
  └─ HdmvSubtitleProviderImpl::RequestFrame            hdmv_subtitle_provider.cpp:45
       ResetAllocator 申请 MSP_RGB32 → CPooledSubPicAllocator 实际给出 MSP_RGBA
  └─ HdmvSubtitleProviderImpl::Render                   hdmv_subtitle_provider.cpp:148
       ClearDirtyRect(0xFF000000) → m_pSub->Render → FlipAlphaValue
  └─ CHdmvSub::Render                                   HdmvSub.cpp:362
       矩阵：m_colorTypeSetting，未设则按视频宽 ≥720 判 BT.709，否则 BT.601
       范围：m_yuvRangeSetting，未设则 RANGE_TV
  └─ CompositionObject::InitColor                       CompositionObject.cpp:56
       MSP_RGBA 分支：A8Y8U8V8_TO_ARGB(T,Y,Cb,Cr,range,matrix,output_tv_level)
       MSP_AYUV* 分支：A8Y8U8V8_TO_CUR_AYUV
  └─ CompositionObject::RenderHdmv                      CompositionObject.cpp:162
       PGS RLE 解码 → FillSolidRect（Rasterizer.cpp:2121，src-over 混合）
  └─ XySubRenderFrameWrapper                            XySubRenderFrameWrapper.cpp:61
       恒定返回 XY_CS_ARGB_F，32bpp 预乘（反转 alpha）
  └─ XySubFilterConsumer::Transform / AlphaBlt          xy_sub_filter_consumer.cpp:218,319
       BaseAlphaBlender（AlphaBlenderArgbC）：dst = dst*A + RGB，8bit PC 域
  └─ CopyBuffer → 输出样本
```

全程 8bit、无传递函数参与，白 = 255 即被视为 100 nits，SDR 下语义自洽。

## HDR 下的偏差点

1. **外部消费端缺少传递函数约定**：当前已在位图调色板与文本图元输出处加入 PQ
   映射，协议仍无 EOTF 字段；实际 madVR 语义已由多日实机使用确认，Relative white
   仅保留内部兼容值。
2. **8bit 塞进 10bit 容器**：`MemSubPic.cpp` 的 `AlphaBltAnv12_P010` 与 `xy_intrinsics.h` 的 `mix_16_y_p010_*` 用 `src << 8`。TV 白 235 → 940，按 PQ 反解约 4650 nits；全幅 255 → 1020，约 9700 nits。
3. **协议无 EOTF 字段**：`SubRenderIntf.h` 1.0.9 只规定 `outputLevels` 的 PC/TV，没有说明位图使用何种传递函数，消费端无从分辨。
4. **范围与矩阵的次要偏差**：`supportedLevels` 为"无偏好"时输出 TV 范围 RGB，消费端若忽略 `outputLevels` 会整体偏亮约 9%；UHD 片源下按 720 阈值猜 BT.709 会让彩色字幕偏色。

## 关键数据结构

- `HDMV_PALETTE { BYTE entry_id, Y, Cr, Cb, T; }`（`CompositionObject.h:26`）—— PGS 调色板项，8bit YCbCr + alpha。
- `SubPicDesc { int type, w, h, bpp, pitch, pitchUV; BYTE *bits, *bitsU, *bitsV; RECT vidrect; }`（`src/subpic/ISubPic.h:47`）。
- `ColorType` 枚举（`ISubPic.h:28`）：`MSP_RGBA`（预乘）、`MSP_RGBA_F`（反转 alpha）、`MSP_AYUV_PLANAR`、`MSP_P010`/`MSP_P016` 等。
- `XyColorSpace`（`XySubRenderIntf.h:6`）：`XY_CS_ARGB`、`XY_CS_ARGB_F`、`XY_CS_AUYV`、`XY_CS_AYUV`、`XY_CS_AYUV_PLANAR`。

## 选项体系

- ID 枚举与三张注册表：`src/filters/transform/vsfilter/IDirectVobSubXy.h`。
- 存储：`XyOptionsImpl` 的按 ID 索引数组，`MAX_OPTION_ID = 127`。
- 跨协议映射：`xy_sub_filter.cpp:39`（provider 侧）、`xy_sub_filter_consumer.cpp:56`（consumer 侧）。
- 持久化：`DirectVobSub.cpp` 的 `LoadSettings` / `UpdateRegistry`，键名在 `VSFilter.rc` 与 `resource.h`。

## HDR 映射实现（已落地）

### 位图字幕（PGS / DVB）

```
.sup → CHdmvSub::ParseSample（8bit Y,Cr,Cb,T 调色板）
     → CHdmvSub::Render
          SetPalette(..., m_hdrConfig)        HdrColor::Config 由 XySubFilter 下发
     → CompositionObject::InitColor
          MSP_RGBA 分支：A8Y8U8V8_TO_ARGB → HdrColor::MapArgb(argb, hdr)
          AYUV* 分支：先映射成 ARGB，再 Argb2Ayuv 预编码 PQ 码
          m_AppliedHdr 与 m_RequestedHdr 不同则重建缓存
     → RenderHdmv（RLE 填充，只查表，无额外色算）
     → P010/P016 目标时，MemSubPic 的 src<<8 把 8bit 码放大 4 倍，
        恰好等于写入 16bit 字高 10 位的 10bit 码（无需改混合核）
```

映射发生在**调色板级**：≤256 项各算一次，之后每帧每像素只做查表与整数混合，热路径无额外开销。

### 文本字幕（ASS / SSA / SRT）

两条文本路径都不经过 `CompositionObject`，因此位图的调色板映射对它们无效。二者各有独立插入点。

**libass 后端（默认）**

```
ass_render_frame
  → 每个 ASS_Image::color（0xRRGGBBAA，alpha 反向，每图一个 DWORD）
  → MapTextColor(): 转成 0xAARRGGBB → HdrColor::MapArgb → 保留 alpha
  → XySubRenderFrameCreater::TransColor（转 AYUV / TV 编码等）
  → packed_pix_mix / ayuv_planar_mix（颜色 × 覆盖度）
```

映射按 `ASS_Image` 逐节点做，一个字符/描边/阴影算一次，与分辨率无关。六处 call site（`CompositeLibassComponent` 与单图中五个 `XY_CS_*` 分支）共用 `MapTextColor`。

整帧缓存 `m_last_frame` 在 `changed == 0` 时直接复用，因此 `SetHdrConfig` 配置变化时会置空该缓存并 `Deinit()`（后者清空 `BitmapMruCache` 等派生缓存）。

**旧 VSFilter 渲染器**

```
CLine::PaintAll
  → 由 STSStyle 颜色 + alpha 构造 sw[6]（0xAARRGGBB）
  → TransColorWithHdr(): HdrColor::MapArgb → TransColor
  → DrawItem::switchpts → Rasterizer::Draw（颜色 × 覆盖度遮罩）
```

`HdrColor::MapArgb` 只改 RGB 三分量、保留 alpha 字节；alpha 是 `Rasterizer::CompositeAlphaMask` 的输入，所以遮罩逻辑不受影响。

缓存安全性：`DrawItemHashKey::operator==` 对完整 `switchpts[6]` 做 `memcmp`，映射后的颜色已进入键值，模式切换不会命中旧位图。

### 判定与下发

```
XySubFilter::SetSubtitle
  → SetYuvMatrix()        选 YCbCr 矩阵与范围
  → SetRgbOutputLevel()   PQ 模式强制全幅 RGB（位图与文本各自判断）
  → SetHdrMode()          位图 → Provider::SetHdrConfig
                          文本 → CRenderedTextSubtitle::SetHdrConfig
```

HDR 模式、白点或 RGB 输出范围变化后，`XySubFilter` 会先重新下发配置并递增
`m_context_id`，再通过 `ISubRenderConsumer2::Clear()` 要求 consumer 丢弃已缓存
的字幕帧。属性页一次 Apply 使用 HDR option batch，把多个字段变化合并成一次刷新。
HDR 配置还携带递增 generation；`HdmvSubtitleProviderImpl`、`CompositionObject`
和 `XySubRenderFrameWrapper2` 的 bitmap ID 都纳入该代次。
否则 madVR 可能继续显示同一字幕区间内由旧调色板生成的帧，导致属性页改变但
画面不变。不要在 `RequestFrame` 内每帧回调 consumer 读取配置；该实现已在
真实播放中造成稳定性退化和花屏。

`ResolveHdrMode(mode)` 是位图与文本共用的解析实现（`DirectVobSubImpl`）：

| 模式 | 结果 |
| --- | --- |
| `PGS_HDR_MODE_DISABLED` | `MAP_OFF` |
| `PGS_HDR_MODE_PQ` | `MAP_PQ`，白点取自 `INT_PGS_HDR_WHITE_NITS` |
| `PGS_HDR_MODE_RELATIVE_WHITE` | `MAP_RELATIVE_WHITE` |
| `PGS_HDR_MODE_AUTO` | side data、图内明确 HDR、文件名 HDR 或 MKV 明确 HDR → `MAP_PQ`；随后按明确 SDR、BT.2020/consumer `yuvMatrix` 回退 |

入口：`ResolvePgsHdrConfig()` 读 `INT_PGS_HDR_MODE`，`ResolveTextHdrConfig()` 读
`INT_TEXT_HDR_MODE`，白点共用。`XySubFilter::UpdateConsumerHdrSignal()` 委托
`ProbeHdrGraph()` 从所选 consumer 的视频输入沿 `ConnectedTo()` 反向追踪活动路径；
只读取路径上已连接引脚的媒体类型，最多 64 个 filter、128 个 pin。

`ReadVideoColorMetadata()` 支持直接 `FORMAT_VideoInfo2`，以及
`FORMAT_MPEG2_VIDEO`、`FORMAT_DiracVideoInfo` 内嵌的
`VIDEOINFOHEADER2`。所有证据先归一化为 `HdrEvidenceSet`，HDR、SDR、BT.2020
和静态 mastering/CLL 字段分别合并，因此引脚枚举顺序不改变 Auto 结论。

当文件名未含独立 `HDR` 标记且活动路径返回本地 `.mkv` 路径时，
`ProbeMatroskaFile()` 最多读取前 2 MiB，交给 `ProbeMatroskaHeader()` 解析
Tracks/Colour/MasteringMetadata/MaxCLL/MaxFALL。探测最多读取 4096 个 EBML
元素、深度 8；不读取 Cluster 或视频样本。H.273 transfer=`16`/`18` 会先转换为
内部 HDR 信号，不能直接复用 DXVA2 的 `8`/`10` 枚举。

多视频轨 MKV 用当前 DirectShow 媒体签名（codec、宽高、帧时长）做唯一匹配；
无法唯一确定时容器证据为 Unknown，不按首轨猜测。文件探测结果按路径、大小和
mtime 缓存；文件名已经提示 HDR 时不执行文件 I/O。

文件名来自活动路径上的 `IFileSourceFilter::GetCurFile()`。标准化视频文件名中的
独立 `HDR` 标记优先于错误的 SDR 元数据；匹配限定在文件名而非目录，并要求 `HDR`
右侧不是字母，以避免 `HDRip` 误判。

内置 `XySubFilterConsumer` 在输入媒体类型变化时把同一组颜色信息转换成
`yuvMatrix`（如 `TV.2020`）供 provider 读取；HDR10/HDR10+/DoVi side data
通过只写选项 `isHdrVideo` 发出一次性事件。provider 只记录该事件和递增
generation，在下一帧 `RequestFrame` 应用配置，不在每帧枚举引脚或读取 consumer
配置。

色彩科学集中在 `src/subpic/hdr_color.{h,cpp}`，HDR 证据合并与 AUTO 优先级集中在
`src/subpic/hdr_evidence.{h,cpp}`，EBML 解析集中在 `src/subpic/hdr_mkv_probe.{h,cpp}`；
这些模块不依赖 DirectShow。`HdrGraphProbe` 位于 basevideofilter，负责 COM 图遍历、
媒体类型读取和本地文件包装。`UpdateConsumerHdrSignal()` 只在连接或媒体签名变化后
执行一次有界探测，不进入逐帧热路径。

`DirectVobSubImpl::GetHdrDiagnosticPath()` 与日志写入共用
`%TEMP%\xy_hdr_diag.txt`。属性页的 `Open log` / “打开日志”按钮只打开已存在的日志；
文件缺失或 `ShellExecuteW` 失败时显示本地化提示，不创建空文件。
日志在每次进程启动后的第一次写入时截断，因此每次运行只保留本次证据；单次运行
超过 512 KiB 后重新开始。每次图/容器探测和 HDR 配置刷新都会记录时间、事件、
实际渲染后端（libass/VSFilter/CSRI）、位图/文本请求模式、最终解析模式、
generation 和当前 HDR 证据。断开连接只追加事件，不再用全零结果覆盖上一次有效探测。

位图 provider 另写有界的 `xy-bitmap-frame/1` 事件，只在几何、位图 ID 或渲染统计
变化时追加。字段包括 `canvasActive`、`bufferSize`、`publishOutput`、
`publishClip`、`bitmapOffset`、`dirtyRect`、`objectCount`、`drawnObjects`、
`skippedObjects`、`bitmapCount`、`bitmap0..2`、`videoInCanvas`、
`partitionCount` 与 `partition0..2`，用于区分“没有位图对象”、
“对象被缓冲区裁空”、“拆分未生效”和“consumer 丢帧”。`render-empty`、
`alloc-failed`、`flip-failed` 也会记录。`xy-hdr-config/1` 同时记录 `isMovable`，
用于确认位图流没有被重新标记为可移动。

文本与位图共用 `DirectVobSubImpl::ResolveHdrMode()`、同一个 consumer HDR 信号和
`INT_PGS_HDR_WHITE_NITS`，但模式选项仍然独立。属性页将二者明确标为
“位图字幕 HDR”和“文本字幕 HDR”，HDR 状态行也以 `Bitmap:` / `Text:` 前缀
区分当前字幕类型。

## 字幕显示区域

`src/subpic/subtitle_display_layout.{h,cpp}` 是不依赖 DirectShow 的布局选择模块，
只提供纯函数：严格全屏判定、画布解析和帧几何构造。

显示尺寸有两个来源，按顺序取第一个可用的：

1. `SIZE_DISPLAY_AREA`：由属性页在 UI 线程用
   `GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN)` 读取主显示器尺寸，存入
   `General\display_area_width/height`，启动时加载。这是**跨渲染器**的来源。
2. consumer 的 `fullscreenRect`：若有效、完整包含并严格大于
   `videoOutputRect`，取其宽高。madVR 在全屏时提供该字段。

两者都不可用时回退原行为。播放线程只读选项值，不做任何窗口或显示器查询。

画布解析有两条路径：

- `ResolveCanvasForAspect()`：已知显示尺寸时（上节两个来源之一），按显示宽高比在
  **视频源像素空间**构造画布，居中并保证包含视频。1920×800 在 16:9 显示器上得到
  1920×1080（y=-140）；21:9 对 16:9 视频得到 2520×1080（左右各 300）。
- `ResolveCanvas()`：没有显示尺寸时退回 consumer 矩形；Auto 直接用
  `subtitleTargetRect`，显式画幅在其内部取内接画布。

两条路径都要求画布完整包含 `videoOutputRect`；否则回退视频矩形，因为
`RTS::RenderEx` 会以 `E_NOTIMPL` 丢弃整帧。位图画布必须完整包含视频，但对象
可以超出画布并被裁剪；位图侧不使用 `RTS::RenderEx`。

文本数据流：

```
XySubRenderProviderWrapper2
  → ResolveConsumerCanvas()
  → RenderEx(videoRect = targetRect = (0,0,W,H), originalVideoSize = (W,H))
       libass/CSRI/legacy 都在同一块零基画布内排版，字号按画布高度缩放
  → XySubRenderFrameWrapper2::SetFrameGeometry()
       GetOutputRect = (0,0,videoW,videoH) 视频零基空间
       GetClipRect  = 画布减去 videoOutputRect 原点（可含负值，进入黑边）
       GetBitmap.position -= videoOutputRect 原点
```

`BuildFrameLayout()` 遵循 `SubRenderIntf.h` 的示例契约：`GetClipRect`、
`GetBitmap.position` 与 `GetOutputRect` 属于同一空间；示例 1 的
`output=(0,0,720,480)`、`clip=(0,-80,720,560)` 即“超视频并含负值”的合法写法。
consumer 把零基 output 映射到 `videoOutputRect`，因此该布局在屏幕上 1:1 还原。

关闭、非全屏、字段缺失或矩形无效时完全走原路径，参数逐字节不变。
`RTS::Init()` 在布局变化时清空 `m_last_frame`，避免 libass 在“文本未变、
仅几何变化”时复用旧布局帧。

PGS/SUP/DVB 的位图对象按流内坐标绘制，provider 不做缩放、留白或重排。
`ResolveBitmapFrame()` 按显示宽高比构造画布，位图渲染缓冲区直接使用该画布尺寸，
因此 PGS/DVB 行可以落在画布的上下黑边。对象坐标不随画布变化；超出缓冲区的
像素由 `Rasterizer::FillSolidRect` 与各 dirty-rect 计算裁剪。位图流固定
`isMovable=false`；它表示 consumer 不得把绝对画布坐标重新定位或缩放。

位图帧沿用文本路径的发布几何：`GetOutputRect` 是零基视频尺寸（consumer 用它
把帧映射到 `videoOutputRect`），`GetClipRect` 与 `GetBitmap.position` 减去同一
video 原点，因此可以进入黑边。不能把物理显示尺寸（2560×1440）直接写成
`GetOutputRect`：那会把整幅画布当成视频参考帧，被 consumer 缩回 1920×800，
造成压扁（`fff048c` 的实机结果）。

单张 bitmap 不能跨越视频带边界（依据 `32127f8` 的实机现象推断，madVR 精确
规则未从源码确认）。`PartitionBitmapRects()` 把 dirty rect 按
`videoInCanvas`（`0,140,1920,940`）的上下沿切成最多三块
（上黑边、视频带、下黑边），
`XySubRenderFrameWrapper` 的多 bitmap 构造为每块发布
`position = rect.TopLeft() + bitmapOffset`、`size = rect.Size()`、
`pixels = bits + rect.top*pitch + rect.left*4`，共享同一 `CMemSubPic` 且零拷贝。
画布关闭时仍是单 bitmap，几何与改动前一致。

2026-11-09 14:15 的 `xy-bitmap-frame/1` 日志把发布错误固定下来：
`publishOutput=(0,0,1920,800)`、`publishClip=(0,-140,1920,940)`、
`bitmapOffset=(0,-140)`，而 `bitmap0=(660,790,1260,880)`。画布底栏对象
发布到只有 800 行的 output 之外，只剩约 10 行与视频底边重叠。

`ResolveBitmapFrame()` 因此重新使用 `BuildFrameLayout()`，并额外给出
`videoInCanvas`（视频带在画布本地坐标中的位置）。provider 在 `Unlock` 后
把 dirty rect 拆成不跨界的 bitmap；`CHdmvSub::Render` 仍只要求对象与缓冲区
相交，不要求完整落在 `spd` 内。

**验证状态（2026-11-09）：** 多 bitmap 拆分与发布代码尚未经实机验证。
最新的 `fff048c`/`0940012`/`32127f8` 实机结果均未闭环，文档不得写成已修复。

画布缓存只在输入变化时重算，`Invalidate()` 与 `Connect()` 会使其失效。
画布或发布几何变化还会丢弃当前 `m_pSubPic`，因为 `Render()` 在字幕区间未变时
会提前返回并复用旧帧；仅清画布缓存不足以让属性页切换立即生效。

## 界面与本地化

- 资源：单份 `VSFilter.rc` 内含英文与简体中文两个语言块，由 `AFX_TARG_ENU` / `AFX_TARG_CHS` 同时定义后一并编译进 DLL；中文块以 GBK + `#pragma code_page(936)` 存储。
- 字体：全部 26 个对话框使用 `Microsoft YaHei UI` 9pt（矢量字体，含 CJK 字形且可参与字体链接）。
- 语言选择：`INT_UI_LANGUAGE`（Auto / English / 简体中文）。Auto 跟随系统 UI 语言，非中文系统回落英文。
- 运行时下拉项（色域、范围、RGB、HDR、布局、文本渲染器、后端等）使用
  `IDS_XY_UI_*`；`Test-ResourceLocalization.ps1` 校验中英文资源字符串 ID、
  对话框控件 ID 和属性页硬编码文本。
- 生效路径：`DirectVobSubImpl::ApplyUiLanguage` 设置滤镜线程语言；属性页另在 `GetPageInfo` / `Activate` 入口调用 `ApplyActiveUiLanguageToCurrentThread()`，因为属性页运行在宿主 UI 线程上。
- 位图字幕不使用 libass/CSRI/VSFilter 文本渲染器；属性页对非文本流显示
  `Bitmap (internal)`，避免把 PGS/DVB 的“实际后端”误报为 VSFilter。
- XySubFilter 的 Main / More / Timing / Paths / About 统一采用 `360×280` DLU。
  Main 与 More 使用左右两栏：More 左栏为缓存、布局和行为选项，右栏为字幕输出、
  文本渲染器与重定位选项；HDR 状态独占底部整行。
- XySubFilter 的 Timing / Paths / About 使用独立资源
  `IDD_XY_SUB_FILTER_*PAGE`；DirectVobSub 原有的
  `IDD_DVSTIMINGPAGE` / `IDD_DVSPATHSPAGE` / `IDD_DVSABOUTPAGE` 保持原尺寸，
  不会因 XySubFilter 的布局调整而被拉宽。
- 即时应用（`OnApplyChanges`）会重建页面控件。基础属性页在重建前后捕获并恢复
  当前 `Edit` 控件的选区和插入符；白点文本仅在归一化结果确实变化时才重写，
  避免连续键入时插入符跳回行首。
- Relative white 仅保留内部兼容值，不再出现在下拉菜单。
