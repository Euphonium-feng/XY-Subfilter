# 架构

## 模块职责

| 模块 | 路径 | 职责 |
| --- | --- | --- |
| 过滤器 | `src/filters/transform/vsfilter/` | DirectShow 滤镜：XySubFilter（provider）、VSFilter/DirectVobSub（内嵌字幕烧录）、XySubFilterConsumer（合成端） |
| 基类 | `src/filters/transform/basevideofilter/` | `CBaseVideoFilter`、色彩标志传播、HDR 侧数据透传 |
| 字幕 | `src/subtitles/` | 解析器（PGS/DVB/VobSub/ASS…）、光栅化、libass/CSRI 后端、位图字幕 Provider |
| 子图 | `src/subpic/` | 字幕面分配与混合（`MemSubPic`）、色彩转换表（`color_conv_table`）、Alpha 混合器 |
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

1. **无传递函数概念**：仓库内不存在 PQ/HLG/nits/白点代码。BT.2020 只作为 YUV→RGB 矩阵存在，既无原色转换也无 EOTF，字幕白被当作 SDR 相对白直接放入 HDR 容器。
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

## 新增 HDR 映射的插入点

- 调色板级：`CompositionObject::InitColor` 的 `MSP_RGBA` 分支（每项调色板算一次，≤256 次）。
- 10bit 烧录：`MemSubPic` 的 P010/P016 混合核入口（256 项 LUT）。
- 判定与下发：`XySubFilter::SetHdrMode()`，与 `SetYuvMatrix()` / `SetRgbOutputLevel()` 并列。
