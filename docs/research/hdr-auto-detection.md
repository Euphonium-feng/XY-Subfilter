# 深层 Auto HDR 判定可行性调研

## 结论摘要

本轮只调研“PQ 或 Off”的 Auto 二选一判定，不扩展 HLG、Dolby Vision 或
其他传递函数的专用映射。结论如下：

- **GO：修正现有 DirectShow 证据链。** 活动视频路径约束、MPEG-2/Dirac
  嵌套 `VIDEOINFOHEADER2`、matrix 与 transfer 独立合并，都是现有架构内可做、
  且不需要改变公开协议的改进。
- **GO：增加本地 MKV 头部探针。** 只进行有界 EBML 读取，提取
  `Colour`、mastering metadata 和内容亮度信息，不扫描视频样本或完整码流。
- **CONDITIONAL：codec extradata。** 只有在媒体类型或 MKV CodecPrivate
  已经暴露 `avcC`、`hvcC`、`av1C` 时才值得解析；不承诺覆盖只存在于完整
  SEI/RPU 中的信息。
- **NO-GO：完整码流扫描、视频 tap、帧级画面分析、渲染器私有接口主判据。**
  这些方案会改变 DirectShow 拓扑或引入过高的性能、兼容性和维护风险。
- **暂缓：MP4/MOV、TS/M2TS、BDMV、网络流、HLG/DoVi 专项分类。**
  它们作为独立范围处理，不与首轮 MKV 兜底混在一起。

你当前的 PotPlayer + madVR 使用场景中，文件名是主要正向信号：规范化命名的
文件本身包含独立的 `HDR` 标记。容器、码流和 DirectShow 元数据的价值，是在
文件名没有提供提示时兜底，而不是取代文件名。任何 HDR 证据仍应胜过冲突的
明确 SDR 元数据；metadata 是保底项。

本报告只做静态调研。没有修改生产代码，没有运行播放器、单元测试或云端构建，
也没有用真实文件验证探测结果。

## 当前实现

### 证据入口

当前 Auto HDR 不是从视频样本直接判定，而是合并以下几类信息：

| 证据 | 当前入口 | 作用范围 |
| --- | --- | --- |
| DirectShow 图内媒体类型 | `XySubFilter::UpdateConsumerHdrSignal()` | 外部 madVR 也可用 |
| consumer `yuvMatrix` | `UpdateParamFromConsumer()` | 外部 madVR 可提供色域/矩阵提示 |
| 视频文件名 | `IFileSourceFilter::GetCurFile()` + `FilenameIndicatesHdr()` | 本地文件和可提供路径的源滤镜 |
| HDR/DoVi media side data | `XySubFilterConsumer::UpdateHdrHintFromSample()` | 仅内置 consumer 能接触视频样本 |
| 用户手动模式 | `INT_PGS_HDR_MODE` / `INT_TEXT_HDR_MODE` | 覆盖 Auto |

主要实现位置：

- 媒体类型读取与 `VIDEOINFOHEADER2.dwControlFlags` 解码：
  `src/filters/transform/vsfilter/xy_sub_filter.cpp:55`
- consumer 选择和整图 HDR 扫描：
  `src/filters/transform/vsfilter/xy_sub_filter.cpp:3290`
- consumer `yuvMatrix` 读取：
  `src/filters/transform/vsfilter/xy_sub_filter.cpp:2667`
- 文件名 HDR 标记：
  `src/subpic/hdr_color.cpp:136`
- Auto 最终解析：
  `src/filters/transform/vsfilter/DirectVobSub.cpp:897`
- 证据合并：
  `src/subpic/hdr_evidence.cpp:5`
- side data 接口：
  `src/filters/transform/basevideofilter/MediaSideData.h:6`
- 内置 consumer 样本检查：
  `src/filters/transform/vsfilter/xy_sub_filter_consumer.cpp:508`

### 现有优先级

`ResolveAutoPq()` 和 `ShouldUsePqForAuto()` 当前表达的优先级为：

1. HDR/DoVi media side data。
2. 图内明确的 PQ/HLG transfer function。
3. 文件名中的独立 `HDR` 标记。
4. 图内明确的 SDR transfer function。
5. `BT.2020` matrix 或 consumer `yuvMatrix=2020` 回退。
6. 全部未知时 Off。

这符合当前使用策略：

- 文件名负责覆盖个人使用中命名规范、但底层元数据缺失或错误的文件。
- 明确 SDR 只在没有 HDR 正向证据时阻止 BT.2020 回退。
- 文件名与明确 SDR 冲突时，文件名 HDR 仍然胜出。

### 外部 madVR 的硬限制

`XySubFilter` 是字幕 provider，只暴露字幕输入引脚和
`ISubRenderProvider`；视频帧直接经过播放器/解码器/madVR，不经过
`XySubFilter`。因此：

- provider 可以在 DirectShow 控制面枚举 filter、pin 和媒体类型。
- 内置 `XySubFilterConsumer` 在视频链中，能检查 `IMediaSample` 上的
  `IMediaSideData`。
- 外部 madVR 场景下，provider 无法取得送入 madVR 的视频样本、side data
  或解码器内部 HDR 状态。
- `include/SubRenderIntf.h` 1.0.9 没有 EOTF/transfer-function 字段，
  不能通过现有 provider/consumer 协议补拿该信息。

因此，“更底层”必须区分：

1. **不改变拓扑的源端文件探测**：可做。
2. **沿 DirectShow 主视频路径读取已有媒体类型**：可做。
3. **拦截或读取实际视频样本**：外部 madVR 场景不可做，除非插入
   tap/中转滤镜，而这被本轮排除。

## 现有缺口

### 证据顺序仍可能影响结论

`PreferHdrEvidence()` 在比较两个 `HDR_SIGNAL_UNKNOWN` 证据时，只检查候选
证据是否带有具体 transfer function，没有独立比较 matrix。这样，一个后扫描到
的“有 transfer、无 matrix”引脚可能覆盖前面已经发现的 BT.2020 matrix 证据；
反过来则可能保留。最终是否得到 PQ，取决于图枚举顺序和 consumer 是否同时
提供 `yuvMatrix=2020`。

这不是文件名主策略的问题，而是底层兜底证据不够稳定。后续应把 transfer、
matrix 和来源置信度作为可独立合并的字段，保证证据顺序不改变结论。

### 图扫描没有限制在活动主视频路径

当前逻辑先在 consumer 上发现 HDR 就返回；否则枚举整张图，任意已连接视频
引脚出现 HDR 也会被采用。多视频流、辅助画面、缩略图或转换分支中的无关
HDR 媒体类型理论上可能污染结果。

后续应从所选 consumer 的活动视频输入引脚反向追踪主路径，只采集该路径上的
媒体类型；未连接引脚和其他图分支只进诊断日志，不进入最终判定。

### 媒体格式覆盖不完整

`ReadVideoColorMetadata()` 当前只接受 `FORMAT_VideoInfo2`。项目其他位置已经
知道 MPEG-2 和 Dirac 媒体类型内含 `VIDEOINFOHEADER2`：

- `FORMAT_MPEG2_VIDEO` 的 `MPEG2VIDEOINFO::hdr`
- `FORMAT_DiracVideoInfo` 的 `DIRACINFOHEADER::hdr`

参考现有提取逻辑：
`src/DSUtil/DSUtil.cpp:1002` 和 `src/DSUtil/DSUtil.cpp:1008`。

首轮修复应复用同一结构规则，同时保留长度、format type 和空指针检查。

### 动态媒体类型变化可能留下陈旧证据

HDR 扫描主要发生在 consumer 连接/重连阶段。播放中如果源滤镜、解码器或
consumer 切换媒体类型，目前每帧更新的是 consumer `yuvMatrix`，不保证重新
扫描 transfer/matrix。后续需要媒体代次或媒体签名，在新媒体类型确定后执行
一次有界重扫。

### side data 只证明“存在”

内置 consumer 目前只检查 HDR/DoVi side data GUID 是否存在，存在即通过
`isHdrVideo` 通知 provider，不解析 payload，也不区分 HDR10、HDR10+、
Dolby Vision profile 或 HLG。

对当前 PQ/Off 目标，这是可接受的保守正向证据；但诊断日志应记录命中的 GUID
类型。缺失 side data 只能表示未知，不能表示 SDR。

## 可行性矩阵

| 路线 | 外部 madVR 可用性 | 侵入性 | 覆盖范围 | 性能/稳定性 | 结论 |
| --- | --- | --- | --- | --- | --- |
| 现有 DirectShow 图扫描 | 可用 | 低 | 取决于解码器/转换器是否保留 `VIDEOINFOHEADER2` | 连接时一次，稳定 | GO：先修正拓扑和格式覆盖 |
| consumer `yuvMatrix` | 可用 | 低 | 只能说明矩阵，不能证明 EOTF | 低 | 保留为回退，不作 HDR 主证据 |
| 内置 consumer side data | 不可用 | 已有实现 | HDR10/HDR10+/DoVi 等，取决于上游 | 每个媒体代次检查到首次命中 | 保留；不扩大为外部路径依赖 |
| 本地 MKV 头部解析 | 可用 | 中 | MKV 容器；不覆盖 TS、BDMV、网络流 | 连接时有界读取，不进入逐帧路径 | GO：首轮新增兜底 |
| codec extradata | 可用但覆盖不稳 | 中 | 取决于 `avcC`/`hvcC`/`av1C`/CodecPrivate 是否暴露完整信息 | 一次解析，复杂度较高 | CONDITIONAL |
| 完整 NAL/OBU/SEI/RPU 扫描 | 需要原始码流或额外 tap | 高 | 高但实现复杂 | 可能进入热路径 | NO-GO |
| MPC/LAV/madVR 私有接口 | 取决于具体滤镜和版本 | 中 | 不稳定 | ABI 风险高 | 不作为主判据 |
| 解码少量帧并分析亮度 | provider 当前不可达 | 极高 | 内容自适应但不可靠 | 高 CPU、高延迟 | NO-GO |
| 插入探针或中转滤镜 | 可用但改变拓扑 | 高 | 可取得样本 | 增加延迟和故障面 | NO-GO |

## MKV 探针方案

### 范围

首轮只支持本地普通 MKV 文件，并只读取定位所需的文件区域：

- EBML Header：确认文件是 Matroska/WebM。
- Segment：允许未知长度。
- Tracks：只处理 `TrackType=1` 的视频轨道。
- TrackEntry：记录 `TrackNumber`、`CodecID`、`CodecPrivate`。
- Video：读取 `Colour`、`MasteringMetadata` 和内容亮度元素。

不读取 Cluster，不扫描视频 payload，不解析字幕或音频轨道。

### 关键元素

| 元素 | ID | 用途 |
| --- | --- | --- |
| EBML | `0x1A45DFA3` | 文件格式确认 |
| Segment | `0x18538067` | 顶层容器 |
| Tracks | `0x1654AE6B` | 轨道表 |
| TrackEntry | `0xAE` | 单轨道 |
| TrackNumber | `0xD7` | 绑定后续证据 |
| TrackType | `0x83` | 只接受视频轨道 |
| CodecID | `0x86` | 判断 HEVC/H.264/AV1/VP9 |
| CodecPrivate | `0x63A2` | 可选 extradata 输入 |
| Video | `0xE0` | 视频配置 |
| Colour | `0x55B0` | 色彩描述 |
| MatrixCoefficients | `0x55B1` | 颜色矩阵 |
| TransferCharacteristics | `0x55BA` | 传递函数 |
| Primaries | `0x55BB` | 色域原色 |
| MaxCLL | `0x55BC` | 内容峰值亮度 |
| MasteringMetadata | `0x55D0` | 母版亮度/色度信息 |
| LuminanceMax | `0x55D9` | 母版峰值亮度 |
| LuminanceMin | `0x55DA` | 母版最低亮度 |

元素 ID 的实现应以 Matroska 规范为准，解析器必须处理 EBML variable-size
integer、未知长度、嵌套深度和截断文件。

### H.273 与 DXVA2 枚举不能混用

MKV `Colour` 使用 H.273/FFmpeg 风格取值，而现有
`HdrColor::ClassifyTransferFunction()` 接受 DXVA2 取值：

- H.273 `TransferCharacteristics=16` 为 PQ。
- H.273 `TransferCharacteristics=18` 为 HLG。
- 现有 `ClassifyTransferFunction()` 中 PQ 为 `8`、HLG 为 `10`。

因此 MKV 探针必须先做明确枚举转换，不能把容器值直接传入现有函数。
转换表只覆盖能由规范确认的值；未知值返回 Unknown，不做猜测。

`Primaries=BT.2020` 或 `MatrixCoefficients=BT.2020` 只能作为弱回退线索。
静态 mastering metadata 和 MaxCLL/MaxFALL 是非零正向 HDR 证据，但缺失不等于
SDR。

### 文件访问与失败策略

- 只接受可打开的本地普通文件；URL、设备路径、网络共享和非常规路径返回
  Unsupported。
- 读取必须有固定字节上限和元素数量上限，例如只解析前 2 MiB，最多读取
  4096 个 EBML 元素；超过限制返回 Unknown，并写入诊断原因。
- `Segment` 或 `Tracks` 未知长度时按子元素边界继续，不尝试寻址文件末尾。
- 任何越界、VINT 溢出、非法深度或 I/O 失败都关闭探针并返回 Unknown。
- 不在 DirectShow streaming thread 同步读取大文件；首轮可只在 consumer
  连接阶段执行一次短时有界探测，并在诊断中记录耗时。

## DirectShow 证据链修正

推荐在未来实现阶段按以下规则改造：

1. 从所选 consumer 的视频输入引脚沿 `ConnectedTo()` 反向追踪主路径。
2. 只读取该路径上已连接引脚的 `ConnectionMediaType()`。
3. 支持 `FORMAT_VideoInfo2`、`FORMAT_MPEG2_VIDEO` 和
   `FORMAT_DiracVideoInfo` 中的 `VIDEOINFOHEADER2`。
4. 把 transfer、matrix、来源阶段和媒体代次作为独立证据保存。
5. 先归一化所有证据，再调用纯函数决策，禁止“扫描过程中发现 HDR 就立即
   返回”决定最终优先级。
6. 同等级证据使用稳定排序，排序只影响诊断中选中的来源，不影响 PQ/Off。
7. 文件路径只从主路径上的 source/splitter 读取，不采用其他分支的文件名。
8. 诊断继续使用 `%TEMP%\xy_hdr_diag.txt`，增加 source、stage、depth、
   transfer、matrix、primaries、mastering、CLL、探针结果和拒绝原因。

该改造不修改 `SubRenderIntf.h`、COM ABI、选项 ID 或注册表键。

## codec extradata 的边界

可评估的输入仅限已经暴露的数据：

- H.264：MKV CodecPrivate 或媒体类型中的 `avcC`，读取 SPS VUI
  `transfer_characteristics`。
- HEVC：MKV CodecPrivate 或媒体类型中的 `hvcC`，读取 SPS VUI
  `transfer_characteristics`。
- AV1：`av1C` 或 sequence header OBU 中的 color config。
- VP9：容器 `Colour` 通常已足够，不额外承诺解析完整 VP9 帧。

即便实现 extradata 解析，也不能保证取得 HDR10+ dynamic metadata、
mastering display SEI 或 Dolby Vision RPU。报告不得把 extradata 支持描述为
完整码流 HDR 识别。

## 明确否决的路线

### 完整码流扫描

需要 demux、NALU/OBU 拆分、SEI/RPU 解析、加密和截断处理，并且 provider
当前没有视频样本入口。为 Auto 的一个布尔结果引入完整码流解析器，复杂度和
故障面都不可接受。

### 插入视频 tap 或中转滤镜

这会改变播放器已有的 decoder -> renderer 直连拓扑，引入样本复制、延迟、
协商失败和兼容问题。不能为了 HDR 提示破坏已验证的播放稳定性。

### madVR/LAV 私有接口作为主判据

madVR 的 HDR 状态更接近当前渲染/显示状态，而非源视频 EOTF；LAV 当前公开
接口没有所需 HDR 元数据字段。私有 ABI 会随版本变化，不能作为唯一真相。

MPC-BE splitter 的 `IExFilterInfo` 可作为未来单独评估的低成本软探测，但必须：

- 通过 `QueryInterface` 可选使用，不存在时完全忽略。
- 不建立硬依赖。
- 不复制 GPLv3 实现到本项目；接口定义和使用条款需单独审查。

### 帧级亮度分析

解码若干帧不能可靠区分 PQ SDR 容器、BT.2020 SDR、HLG 和艺术性暗场内容；
同时需要视频解码和像素访问，外部 madVR 路径不可达。

## 推荐的后续实施顺序

### 阶段一：稳定现有证据模型

- 修正 `PreferHdrEvidence()` 的 matrix/transfer 顺序依赖。
- 增加活动主视频路径约束。
- 补上 MPEG-2/Dirac 嵌套 `VIDEOINFOHEADER2`。
- 增加媒体代次和有界重扫条件。
- 用纯函数单测覆盖任意证据排列、相同等级冲突和全未知回退。

此阶段不新增容器 I/O，风险最低，应先做。

### 阶段二：本地 MKV 兜底

- 增加有界 EBML 头部探针。
- 正确转换 H.273 transfer/matrix 枚举。
- 将 `Colour` transfer、mastering metadata 和 CLL 作为正向 HDR 证据。
- 文件名仍优先于明确 SDR；探针主要用于文件名没有 HDR 提示时补足证据。
- 失败、超限或不支持一律返回 Unknown，不改变已有 Auto 结果。

### 阶段三：可选 codec extradata

只有在阶段二证明仍有足够漏判，且 CodecPrivate 在真实滤镜图中稳定可用时，
再解析 `avcC`/`hvcC`/`av1C`。否则保持 NO-GO，避免为低增量收益维护多套
码流语法。

### 后续独立范围

- MP4/MOV `colr`、`mdcv`、`clli`、`dvcC`/`dvvC`。
- TS/M2TS、BDMV 和网络流。
- HLG 与 PQ 的独立分类或映射。
- Dolby Vision profile 语义。
- MPC splitter 私有接口的兼容性与许可审查。

## 后续测试草案

以下测试均未在本轮执行，只作为实现阶段的验收草案：

- 纯函数证据测试：任意引脚顺序得到相同 PQ/Off；HDR 胜过 SDR；明确 SDR
  胜过 matrix/Unknown；side data 缺失不等于 SDR。
- `VIDEOINFOHEADER2` 测试：直接格式、MPEG-2 嵌套、Dirac 嵌套、截断格式、
  缺少 `AMCONTROL_COLORINFO_PRESENT` 但 transfer=PQ 的情况。
- MKV 合成样本：普通 EBML、未知长度 Segment、Colour 完整/缺失、H.273
  PQ/HLG/SDR、mastering/CLL、截断 VINT、超深嵌套和超过读取上限。
- 图拓扑测试：consumer 前置转换器、活动路径含 PQ、无关分支含 HDR、
  未连接引脚含 HDR、路径循环和 COM 失败。
- 缓存测试：媒体代次变化触发一次重扫；相同快照不刷新字幕；最终 Auto
  结果变化才递增有效 HDR generation 并调用一次 `Clear()`。
- 回归要求：现有 HDR 色值、文本渲染 golden、资源中文化和 seek 脚本测试
  不受影响。

## 本轮不改变的接口

本报告没有定义或承诺任何公开接口。未来设计中的 `HdrEvidenceSource`、
`HdrObservation`、`HdrProbeSnapshot` 等类型如需实现，应保持为内部类型，
不进入 COM、`SubRenderIntf.h` 或跨 DLL ABI。

以下内容保持不变：

- `DirectVobSubXyOptions` ID 和顺序。
- 注册表键和持久化值。
- `SubRenderIntf.h`。
- 默认 Auto/Off/PQ 行为和默认 203 nits 白点。
- 字幕渲染路径和 HDR 映射算法。

