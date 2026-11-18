# MPCVR-Plus 字幕接入分析

本文件记录 XY-SubFilter 适配 MPCVR-Plus 所需的源码事实与推导，供后续版本漂移时复核。

## 分析对象

| 项目 | 值 |
| --- | --- |
| 仓库 | `https://github.com/HumbleUser33/MPCVR-Plus` |
| 基线提交 | `2f0643b5d71eea04ad369a3697eb84d71c097896` |
| 提交日期 | 2026-11-17 |
| 版本 | `0.11.1`（`Include/Version.h:26-28`） |
| 上游 | fork 自 `Aleksoid1978/VideoRenderer` |
| 分析方式 | 只读源码 + 与本仓库 `SubRenderIntf.h` 1.0.9 对照，未改动对方仓库 |

## 字幕数据流

```
CMpcVideoRenderer::Connect                        Source/VideoRenderer.cpp:2516
  └─ SetBool("combineBitmaps", true)               Source/VideoRenderer.cpp:2524
  └─ new CXySubPicProvider(subtitleRenderer)       Source/VideoRenderer.cpp:2538
  └─ new CXySubPicQueueNoThread(allocator)         Source/VideoRenderer.cpp:2539
CXySubPicQueueNoThread::LookupSubPic               Source/SubPic/XySubPicQueueImpl.cpp:47
  └─ IXyCompatProvider::RequestFrame               Source/SubPic/XySubPicProvider.cpp:60
  └─ ISubRenderProvider::RequestFrame              provider（本仓库）
  └─ DeliverFrame → m_pSubFrame                    Source/SubPic/XySubPicProvider.cpp:49
CXySubPicProvider::Render                          Source/SubPic/XySubPicProvider.cpp:94
  └─ GetBitmap(0)                                  Source/SubPic/XySubPicProvider.cpp:110
CXySubPicProvider::GetTextureSize                  Source/SubPic/XySubPicProvider.cpp:126
  └─ GetOutputRect / GetClipRect                   Source/SubPic/XySubPicProvider.cpp:128-129
CDX11VideoProcessor::DrawSubtitles                 Source/DX11VideoProcessor.cpp:4475
  └─ GetSourceAndDest(..., bUseSpecialCase=FALSE)  Source/DX11VideoProcessor.cpp:4482
  └─ CDX11SubPicAllocator::Render                  Source/SubPic/DX11SubPic.cpp:480
```

队列是 `CXySubPicQueueNoThread`（`Source/VideoRenderer.cpp:2539`），`LookupSubPic` 在渲染线程同步等待 provider 交付；
provider 的耗时直接落在该线程上。

## 能力矩阵

MPCVR 作为 consumer 暴露 `ISubRenderConsumer2`（`Source/VideoRenderer.h:357-364`），实际作答的字段如下。

| 字段 | 方向 | 实现 | 位置 |
| --- | --- | --- | --- |
| `name` | consumer → provider | `MPC Video Renderer` | `Source/VideoRenderer.cpp:2649-2651` |
| `version` | consumer → provider | `VERSION_STR`（`0.11.1.N`） | `Source/VideoRenderer.cpp:2652-2654` |
| `yuvMatrix` | consumer → provider | `TV.709` / `TV.601` / `TV.2020` | `Source/VideoRenderer.cpp:2655-2665` |
| `originalVideoSize` | consumer → provider | `m_videoSize` | `Source/VideoRenderer.cpp:2579-2582` |
| `arAdjustedVideoSize` | consumer → provider | `m_videoSize`（含 AR 修正） | `Source/VideoRenderer.cpp:2583-2588` |
| `videoOutputRect` | consumer → provider | 零基 `m_videoRect` 尺寸 | `Source/VideoRenderer.cpp:2597-2612` |
| `subtitleTargetRect` | consumer → provider | 同上，与 `videoOutputRect` 相同 | `Source/VideoRenderer.cpp:2597-2612` |
| `frameRate` / `refreshRate` | consumer → provider | 已实现 | `Source/VideoRenderer.cpp:2618-2641` |
| `supportedLevels` | consumer → provider | **未实现**，`GetInt` 恒返回 `E_INVALIDARG` | `Source/VideoRenderer.h:367-368` |
| `fullscreenRect` | consumer → provider | **未实现** | `Source/VideoRenderer.cpp:2594-2616` |
| `displayModeSize` | consumer → provider | **未实现** | `Source/VideoRenderer.cpp:2575-2592` |
| `isHdrVideo` | consumer → provider | **未实现**；HDR 由 MPCVR 自己从媒体类型与 side data 判定 | `Source/DX11VideoProcessor.cpp:3051-3092` |
| `combineBitmaps` | provider → consumer | MPCVR 在 `Connect` 时置 `true` | `Source/VideoRenderer.cpp:2524` |
| `isBitmap` | provider → consumer | **MPCVR 不读取**（`GetBool` 恒 `E_INVALIDARG`） | `Source/VideoRenderer.h:367` |
| `isMovable` | provider → consumer | **MPCVR 不读取** | 无 `GetBool` 实现 |
| `outputLevels` | provider → consumer | **MPCVR 不读取** | 无 `GetString` 分支 |

结论：provider 从 MPCVR 只能得到名称、版本、矩阵、视频尺寸与视频矩形；**拿不到显示区域、输出传递函数或 HDR 状态**。

## 几何语义

### 四个矩形的空间

- `m_videoRect`：播放器通过 `IBaseVideo::SetDestinationPosition` 设置的**画面矩形**（`Source/VideoRenderer.cpp:1665-1677`），
  是窗口后缓冲上的像素坐标；黑边位于它之外。
- `m_windowRect`：窗口后缓冲尺寸（`Source/VideoRenderer.cpp:1963-2023`）。
- `GetOutputRect` / `GetClipRect`：由 `CXySubPicProvider::GetTextureSize` 直接转换为
  `MaxTextureSize = outputRect.Size()`、`VirtualSize = clipRect.Size()`、`VirtualTopLeft = clipRect.TopLeft()`
  （`Source/SubPic/XySubPicProvider.cpp:126-134`）。
- `GetBitmap.position`：MPCVR 只把它的 `x/y` 当作**源缓冲左上角**（`Source/SubPic/XySubPicProvider.cpp:110-121`），
  与 clip 无关。

### MPCVR 的缩放公式

`CSubPicImpl::GetSourceAndDest`（`Source/SubPic/SubPicImpl.cpp:105-182`）在 `bUseSpecialCase=FALSE` 时：

```
scaleX = targetW / VirtualSize.cx
scaleY = targetH / VirtualSize.cy
source = dirtyRect
dest   = round((dirtyRect.topLeft + VirtualTopLeft + xOffset) * scale) + target.topLeft
```

其中非文本字幕的 `target` 固定为 `m_videoRect`（`Source/SubPic/SubPicImpl.cpp:122`）。
两个轴独立缩放，**不是等比**。

`bUseSpecialCase=true` 的等比分支持支（`Source/SubPic/SubPicImpl.cpp:124-173`）只在
`ST_HDMV`/`ST_DVB`/`ST_XYSUBPIC` 且 `VirtualSize.cx > 720` 时启用，但调用点固定传 `FALSE`
（`Source/DX11VideoProcessor.cpp:4482`、`Source/DX9VideoProcessor.cpp:2888`），当前 fork 无其他调用者，
因此该分支实际不生效。

### 对 provider 的推论

1. `GetBitmap(0)` 是唯一的纹理来源，多 bitmap 中只有第 0 张会被采样。
2. `Render()` 的 `memcpy` 以 `position` 作为目标位置，但源指针始终是该 bitmap 的起点；
   `position` 为负时不会跳过源行，只会把目标裁剪到缓冲内。因此 provider 不应依赖负偏移。
3. 目标矩形恒为 `m_videoRect`：任何发布画布都会被两轴系数分别缩放回画面矩形。
   黑边内容无法保留，最终可见比例等于“设计画布 → 画面矩形”的比例，
   与画布大小无关（画布只影响缓冲区尺寸与诊断值）。
4. 当 `VirtualSize` 与 `m_videoRect` 宽高比一致时，两轴系数相等，画面不会被拉伸；
   否则会按两轴独立拉伸。

## HDR 语义

MPCVR 的字幕是在 `Process()` 之后叠加到后缓冲上的覆盖层（`Source/DX11VideoProcessor.cpp:3637` 与
`Source/DX11VideoProcessor.cpp:3652`）。

### HDR 输出

`UpdateBitmapShader()`（`Source/DX11VideoProcessor.cpp:4225-4253`）在
`m_bHdrDisplayModeEnabled && (SourceIsHDR() || m_bVPUseRTXVideoHDR)` 时选择
`IDF_PS_11_CONVERT_BITMAP_TO_PQ*`，把字幕纹理从 SDR 转到 PQ。

`Shaders/d3d11/ps_convert_bitmap_to_pq.hlsl` 的精确公式：

```
x = pow(x, 1/0.45)                 // REC.709 到线性
x = mul(matx, x)                   // REC.709 到 BT.2020
x = pow(x / SDR_peak_lum, m1)      // 线性除以 SDR 白点
x = (c1 + c2*x) / (1 + c3*x)
x = pow(x, m2)                     // PQ OETF
```

`SDR_peak_lum` 由 `HdrOsdBrightness` 决定（`Source/VideoRenderer.cpp:262-264`）：

| 档位 | 白点 |
| --- | --- |
| 0（默认） | 100 nits |
| 1 | 50 nits |
| 2 | 30 nits |

因此 MPCVR 已经把 SDR 字幕按自己的白点转成 PQ。provider 若再做 PQ 预编码，
等于把传递函数应用两次。

### SDR 输出

`UpdateBitmapShader()` 的 `else` 分支把字幕 shader 换成 `m_pPS_Simple`
（`Source/DX11VideoProcessor.cpp:4249-4251`），也就是直通。此路径下 provider 必须保持原始 SDR 码值。

### 混合

`CDX11SubPicAllocator::CreateBlendState` 使用 `SrcBlend = ONE`、
`DestBlend = INV_SRC_ALPHA`（`Source/SubPic/DX11SubPic.cpp:439-451`），
即预乘 alpha 的 `src + dst*(1-a)`；`SetInverseAlpha(true)` 在 `Connect` 时设置
（`Source/VideoRenderer.cpp:2547`）。

## 已知限制

1. 不实现 `fullscreenRect`、`displayModeSize`、`supportedLevels`、`outputLevels`、`isHdrVideo`。
2. `videoOutputRect` 与 `subtitleTargetRect` 都等于 `m_videoRect`，无法表达“字幕画布大于画面”。
3. 非文本字幕的目标矩形固定为 `m_videoRect`（`Source/SubPic/SubPicImpl.cpp:122`）。
4. 等比缩放分支 `bUseSpecialCase` 未被任何调用点启用。
5. 只读取 `GetBitmap(0)`，多 bitmap 的其余部分被忽略。
6. `GetSourceAndDest` 对非文本字幕按两个轴独立缩放。
7. 字幕纹理为 8bit BGRA（`Source/SubPic/DX11SubPic.cpp:417`），HDR 转换发生在合成 shader 中。

## 与 madVR 的对照

| 行为 | madVR（已实机验证） | MPCVR-Plus 0.11.1（源码结论） |
| --- | --- | --- |
| 位图黑边 | 需 `isBitmap=false`，按覆盖层合成 | 不读 `isBitmap`（`GetBool` 返回 `E_INVALIDARG`），固定缩放到 `m_videoRect` |
| `isMovable` | 读取并可能重定位 | 不读，位置由 `GetSourceAndDest` 决定 |
| clip 黑边 | 按 clip 合成 | clip 只贡献 `VirtualSize`/`VirtualTopLeft` |
| 多 bitmap | 支持分区发布 | 只取 bitmap 0 |
| 等比缩放 | consumer 负责 | 两轴独立缩放；等比需 provider 保证近似同比例 |
| HDR 归属 | provider 预编码 PQ | **consumer 自行把 SDR 转 PQ** |
| 显示区域来源 | `fullscreenRect` + provider 持久化尺寸 | 仅 provider 持久化尺寸 |

## 适配结论

### 由 provider 负责

- 位图：在画布启用时发布**单张、零基、整幅画布**纹理，位置 `(0,0)`、尺寸等于缓冲；
  画布关闭时保持视频内基线。
- 文本：同样以零基画布发布，让 `VirtualSize` 等于画布。
- HDR：MPCVR 分类下，位图与文本都返回 `MAP_OFF`，保留 SDR 码值；
  白点由 MPCVR 的 `HdrOsdBrightness` 决定，本项目的 203 nits 不作用于 MPCVR。
- 呈现：按用户决定，MPCVR 在画布启用且位图时同样把 `isBitmap` 读为 false；
  `isMovable` 保持 `false`。该版本 `GetBool` 恒返回 `E_INVALIDARG`，
  所以伪装不改变它的实际合成；真实身份通过 `BOOL_INTERNAL_IS_BITMAP` 提供给
  属性页与状态文本。

### 由 MPCVR 负责

- 把 SDR 字幕纹理按自身白点转成 PQ（HDR 输出时）。
- 把收到的纹理缩放到 `m_videoRect`。
- 缓存与 `Clear()`（`Source/VideoRenderer.cpp:2500-2504`）。

### 无法由 provider 解决

- MPCVR 的字幕目标矩形固定在 `m_videoRect`，provider 的 clip 无法让它画进黑边。
  若实机确认黑边不可达，应把该限制记录为 MPCVR 侧行为，不使用 `isBitmap=false` 伪装绕过。

## 验证入口

实机时回传 `%TEMP%\xy_hdr_diag.txt`，按下列字段判定：

| 字段 | 期望 | 含义 |
| --- | --- | --- |
| `consumer` | `MPC_Video_Renderer` | 分类命中 |
| `contract` | `mpcvr-texture` | 走 MPCVR 契约 |
| `presentedBitmap` | `1` | 未伪装 `isBitmap` |
| `usesOversizedBitmap` | `1` | 发布了整幅画布 |
| `consumerScale` | 两轴接近 | 画布与视频矩形的比例 |
| `publishOutput` / `publishClip` | 均为 `0,0,canvasW,canvasH` | MPCVR 的 `VirtualSize` 来源 |
| `partitionCount` | `0` | 未走 madVR 的三段拆分 |
| `hdrOwner` | `consumer` | provider 未做 PQ 预编码 |

`xy-hdr-config/1` 中的 `hdrOwner`、`contract` 由 `XySubFilter::LogHdrConfiguration` 写入；
`xy-bitmap-frame/1` 中的 `contract`、`usesOversizedBitmap`、`consumerScale` 由
`HdmvSubtitleProviderImpl::LogBitmapFrame` 写入。
