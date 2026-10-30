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
日志采用追加写入并在超过 512 KiB 后轮换。每次图/容器探测和 HDR 配置刷新都会
记录时间、事件、实际渲染后端（libass/VSFilter/CSRI）、位图/文本请求模式、
最终解析模式、generation 和当前 HDR 证据。断开连接只追加事件，不再用全零结果
覆盖上一次有效探测。

文本与位图共用 `DirectVobSubImpl::ResolveHdrMode()`、同一个 consumer HDR 信号和
`INT_PGS_HDR_WHITE_NITS`，但模式选项仍然独立。属性页将二者明确标为
“位图字幕 HDR”和“文本字幕 HDR”，HDR 状态行也以 `Bitmap:` / `Text:` 前缀
区分当前字幕类型。

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
