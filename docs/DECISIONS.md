# 技术决策

## D1：HDR 映射放在调色板级，而非像素级或 16bit 面

调色板最多 256 项，PGS 与 DVB 都是"少量颜色 + 大面积平色"的图形字幕。在 `CompositionObject::InitColor` 里对每个调色板项做一次映射，成本是常数 256 次数学运算，与分辨率无关。

被否方案：

- **像素级映射**：每帧每像素都算 PQ，1080p 双行字幕约 40 万像素，代价高且无收益。
- **16bit 字幕面**：精度最好，但 `MemSubPic` 全部混合核、`CPooledSubPicAllocator`、`XySubFilterConsumer::AlphaBlt`、`SimpleSubPicProviderImpl` 都要改，回归面巨大。

遗留精度：8bit PQ 码在 203 nits 附近每码约 2% 亮度步进。图形字幕可接受；若将来需要，`SetHdrConfig` 接口已预留升级到 16bit 面的位置。

## D2：ARGB 路径默认 PQ 预编码，并保留"相对白缩放"备选

协议无法告知消费端位图的传递函数，因此只有两种可行语义：

- **PQ 预编码**：把 0..255 直接编码为 PQ 码（203 nits → 码 148）。消费端若把字幕位图当 PQ 数据使用，亮度正确。
- **相对白缩放**：保持 SDR 相对白，只把线性域白点从 100 nits 缩到 203 nits。消费端若把字幕当 SDR/线性处理，这个更合适。

两者的正确性取决于消费端（尤其 madVR）的实际行为，无法从本仓库代码判定。因此两种都实现，做成交互选项，默认 PQ 预编码。

## D3：自动判定以"yuvMatrix 以 2020 结尾"为触发条件

`SubRenderIntf.h` 1.0.9 没有 EOTF 字段。可用信号只有两个：

1. 自家 consumer 的 `VIDEOINFOHEADER2.dwControlFlags` 传递函数位（PQ / HLG）——准确，但外部 consumer 不提供。
2. consumer 上报的 `yuvMatrix` 字符串——madVR 会给出 `TV.2020` 一类值。

规则：优先读传递函数位；拿不到时退化为"`yuvMatrix` 以 2020 结尾即判为 HDR"。另设 Auto / 强制开 / 强制关三级手动覆盖，误判可纠正。

被否方案：

- **只信传递函数位**：外部 consumer 永远无法自动开启，实际退化为纯手动。
- **默认全开**：SDR 片源字幕会明显变淡。

已知风险：SDR 的 BT.2020 内容会被误判为 HDR。手动覆盖即为兜底。

## D4：色彩科学独立成模块，不并入 color_conv_table

`color_conv_table.cpp` 是 8bit 定点（16.16）、处于逐帧热路径、且被 `test_color_conv_table.h` 的数值基线锁定。在其中加入浮点 PQ 运算会污染既有实现并可能移动基线。

新模块 `src/subpic/hdr_color.h/.cpp` 为纯函数，无 Win32 依赖，可独立单测。

## D5：文本字幕分两轮实现，接口先预留

第一轮需求明确指向 SUP 字幕，因此先在 `CBaseSub` 层预留 `SetHdrConfig`，文本字幕不接入。第二轮补齐：`CRenderedTextSubtitle` 独立持有 `m_hdr_config`，libass 与旧渲染器两条路径分别接入（见 D16）。

当初延后的理由成立——文本路径确实不同（libass 走 `ASS_Image` 颜色，旧渲染器走 `switchpts`），无法复用位图那条链路。

## D6：界面中文化采用新增语言块而非直改英文

`.rc` 已有匈牙利语空块与英文块，`CString(MAKEINTRESOURCE)` 依赖线程 UI 语言。直接改英文块会让英文用户失去英文界面。

方案：新增 `LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED` 全量块，新增 `INT_UI_LANGUAGE`（Auto / English / 中文），Auto 跟随 `GetUserDefaultUILanguage()`，通过 `SetThreadUILanguage()` 切换并重建属性页。

## D7：云构建闭环

按全局规范，本地构建不算验证。仓库建立后每轮改动都走"提交 → push → `gh run watch --exit-status`"，红色则读 `gh run view --log-failed` 修因重推，直至绿。

当前仓库目录原本没有 `.git`，账号下也无对应远程仓库，因此首次提交包含全量源码，并用 `git update-index --cacheinfo` 直接登记 9 个 SMP 子模块的 gitlink（避免本地克隆数百 MB），由 CI 的 `submodules: recursive` 拉取源码。

## D8：P010/P016 路径不修改混合核

`MemSubPic` 的 P010 混合核用 `src << 8` 把 8bit 值写入 16bit 容器的低位对齐位置。实测结论：`8bit << 8` 恰好等于「同一码值 ×4」，而 10bit 容器的 10bit 样本就存放在 16bit 字的高 10 位（即码值 ×64）。

因此在调色板阶段输出的 8bit PQ 码（203 nits → 148）经混合核后得到 10bit 码 592，反解约 199 nits，与目标 203 nits 相差约 2%，这正好是 8bit 量化精度本身的下限。

结论：只要 HDR 映射在调色板阶段完成，10bit 烧录路径无需任何改动。这也印证了 D1 选择调色板级映射的正确性。

## D9：PQ 模式下强制全幅 RGB 输出

`SetRgbOutputLevel` 原本会在 `supportedLevels` 为「无偏好」时输出 TV 范围 RGB。但 PQ 码值本身是绝对量，若再用 TV 范围压缩一次，白点会被整体拉低约 9%。

因此在 `INT_PGS_HDR_MODE == PGS_HDR_MODE_PQ` 时强制 `m_output_tv_range_rgb = false`，其余模式维持原有逻辑。

## D10：界面语言用资源语言块而非运行时替换

`.rc` 中原有匈牙利语空块与英文块，`CString(MAKEINTRESOURCE)` 依赖线程 UI 语言选择资源。方案是新增 `LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED` 全量块（`#pragma code_page(936)`，文件以 GBK 存储），并通过 `SetThreadUILanguage` 切换。

`INT_UI_LANGUAGE` 提供 Auto / English / 简体中文 三档，Auto 跟随 `GetUserDefaultUILanguage()`；非中文系统一律回落到英文，避免出现空白界面。

被否方案：直接改写英文块为中文（英文用户失去英文界面）、只在中文系统显示（无法手动切换）。

## D11：中文资源块以 GBK 编码存储

`.rc` 说明符用 `#pragma code_page(936)`，源码本身以 GBK 存储，这是 MSVC 资源编译器的标准配套方式。文件中英文块仍为 ASCII，两种语言块共存于同一文件，由 `#if !defined(AFX_RESOURCE_DLL) || defined(AFX_TARG_xxx)` 分隔。

## D12：HDR 映射插入位置选在 InitColor 而非混合阶段

调色板项在 `InitColor` 中一次算好、缓存进 `m_Colors[256]`，之后 RLE 填充每像素只做查表与整数混合。把映射放在这里，热路径完全没有额外开销，且对 ARGB 与 AYUV 两种目标格式都能覆盖。

混合阶段插入需要为每种目标格式（ARGB、AYUV、AYUV_PLANAR、P010/P016）各写一套路径，且 P010 的 SSE2 核要改内联汇编级代码，收益为零、风险更高。

## D13：`IsSameOutput` 用作缓存失效判据

`InitColor` 原本只比较 `spd.type`。加入 HDR 配置后，若用户改动白点或模式，必须重建调色板。`HdrColor::IsSameOutput` 在 `MAP_OFF` 时忽略白点（因为输出与白点无关），避免无意义的重算。

## D14：新增选项 ID 追加在枚举末尾

`IDirectVobSubXy.h` 的选项 ID 是跨 DLL 的 ABI 契约：外部消费者（如 madVR）把自己编译进去的常量值传给 provider。若在枚举中间插入新项，其后所有 ID 都会位移，导致既有消费者读写错误的选项。

因此新 ID 一路追加在 `OPTION_COUNT` 之前、`INT_TEXT_RENDERER_MODE` 之后，既有 ID 数值全部保持不变：

| 选项 | ID |
| --- | --- |
| `INT_PGS_HDR_MODE` | 96 |
| `INT_PGS_HDR_WHITE_NITS` | 97 |
| `INT_UI_LANGUAGE` | 98 |
| `INT_TEXT_HDR_MODE` | 99 |

当前 `OPTION_COUNT = 100`，最大 ID 99，仍在 `MAX_OPTION_ID = 127` 之内。

## D15：文本字幕单独一组选项

位图（`INT_PGS_HDR_MODE`）与文本（`INT_TEXT_HDR_MODE`）各有独立模式，共用 `INT_PGS_HDR_WHITE_NITS` 白点。

理由：两者的历史色彩约定不同。PGS 调色板是原生 YCbCr，而 ASS 文本颜色是 RGB，且 VSFilter 传统上按 BT.601 处理并被固化在大量字幕里。共用开关会导致：想关掉文本映射（某个 ASS 字幕映射后偏色）时连位图一起关掉。

被否方案：共用一个开关（无法单独调节）、只加一个跟随布尔开关（无法给文本选不同模式）。

## D16：libass 与旧渲染器各设显式插入点，不放在 TransColor

`XySubRenderFrameCreater::TransColor` 是进程级单例，libass 文本、旧渲染器文本、PGS 位图都走它。在该处统一插入会连带影响位图路径，语义混杂且无法区分两者。

实际采用：

- **libass**：`ASS_Image` 颜色在六处 call site 经 `MapTextColor()` 映射，再交给 `TransColor`。
- **旧渲染器**：`CLine::PaintAll` 构造 `switchpts` 后经 `TransColorWithHdr()` 映射。

两处都复用 `HdrColor::MapArgb`，行为一致但不互相污染。

## D17：不原地改写 ASS_Image::color

最初的设想是在 `ass_render_frame` 返回后遍历链表改写 `color` 字段，这样一处改动即可覆盖所有 call site。放弃的原因：`ASS_Image` 链表由 libass 持有，其生命周期与内部状态不由本项目控制，原地写入属于对第三方数据结构的未定义使用。

改为在合成时用局部变量映射（六处 call site 各取一次），代价是每个图元多一次函数调用，实际开销可忽略（每帧每图元一次，而非每像素）。

## D18：HDR 配置变化时清空两层缓存

libass 路径有两层缓存：整帧缓存 `m_last_frame`（`changed == 0` 时复用）与 `BitmapMruCache`（按 `GroupedDrawItemsHashKey` 复用合成位图）。

`SetHdrConfig` 在配置真正变化时置空 `m_last_frame` 并调用 `Deinit()`，后者清空 `BitmapMruCache`、`ClipperAlphaMaskMruCache`、`TextInfoCache`、`AssTagListMruCache`、`ScanLineDataMruCache` 等派生物。

旧渲染器路径另有 `DrawItemHashKey`，其 `operator==` 对完整 `switchpts[6]` 做 `memcmp`，映射后的颜色已参与比较，因此无需额外把配置塞进哈希。

## D19：PQ 模式下文本也强制全幅 RGB

与 D9 同理：PQ 码值是绝对量，若再压进 TV 范围（16–235），白点会被二次拉低约 9%。`SetRgbOutputLevel` 在 `INT_TEXT_HDR_MODE == PGS_HDR_MODE_PQ` 时对文本路径也强制 `SetRgbOutputTvLevel(false)`。

## D20：资源编译必须同时定义 AFX_TARG_ENU 与 AFX_TARG_CHS

`VSFilter.props` 的 `ResourceCompile` 原先只定义 `AFX_TARG_ENU`。`.rc` 的中文块条件是 `#if !defined(AFX_RESOURCE_DLL) || defined(AFX_TARG_CHS)`，在 `AFX_RESOURCE_DLL` 已定义的前提下，缺少 `AFX_TARG_CHS` 会让整块中文资源被预处理器丢弃。

即上一版提交的"中文化"实际是死代码。现已改为同时定义两个 `AFX_TARG_*`。MFC 按线程 UI 语言在编译进同一 DLL 的多个语言块中选择。

## D21：对话框字体改用 Microsoft YaHei UI

原有 26 处 `FONT` 指令全部是 `MS Sans Serif` 或 `MS Shell Dlg`：

- `MS Sans Serif` 来自 `SSERIFE.FON`，是**光栅字体**，没有 CJK 字形，且光栅字体不参与 GDI 字体链接，中文标签会整片不显示。
- `MS Shell Dlg` 是逻辑字体，可解析到可链接的矢量字体。

统一改为 `Microsoft YaHei UI`（`msyh.ttc`，Vista 起随所有语言版本的 Windows 提供），字号从 8 调到 9，因为雅黑在同字号下笔画更细、显得偏小。

## D22：属性页在宿主 UI 线程显式切换资源语言

`SetThreadUILanguage` 只影响调用线程。属性页由宿主（播放器/属性框架）的 UI 线程创建，不会继承滤镜线程的语言设置，因此仅靠 `ApplyUiLanguage` 无法让页面按所选语言加载。

现在 `GetPageInfo` 与 `Activate` 一进入就调用 `DirectVobSubImpl::ApplyActiveUiLanguageToCurrentThread()`，读取由 `INT_UI_LANGUAGE` 发布的静态语言值并设置当前线程，使随后的 `LoadString` 与 `CreateDialogParam` 命中对应语言块。

## D23：缓存键不需要额外纳入 HDR 配置

曾计划把 HDR 模式与白点加入 `DrawItemHashKey`，以免切换模式时复用陈旧位图。检查后确认不必要：该键的 `operator==` 对完整 `switchpts[6]` 做 `memcmp`，而映射发生在 `switchpts` 构造之后、`TransColor` 之前，映射结果已经写进这些槽位。模式一变颜色就变，键自然不同。

副作用也一并消失：无需再维护"配置参与哈希"这层同步逻辑。

（`UpdateHashValue` 只混入 `switchpts[0]`，但那只是哈希函数不够均匀，等价性仍由 `memcmp` 保证，不影响正确性。）

## D24：文本 HDR 默认 Auto，保证 golden 测试基线不变

`libass_regression_test` 对渲染结果按 SHA256 与 golden 清单逐字节比对。若文本 HDR 默认开启，所有基线会立即失效。

因此 `INT_TEXT_HDR_MODE` 默认 `PGS_HDR_MODE_AUTO`。测试环境没有 consumer，`yuvMatrix` 为空，Auto 解析为 `MAP_OFF`，输出与改动前逐字节一致。这一点由 `HdrColorTest.TextColorOffModeIsIdentity` 与既有 golden 测试双重保障。

## D25：HDR 选项变化后清空 consumer 缓存帧

属性页改变 HDR 模式或白点后，provider 会重新生成调色板，但 consumer 可能仍
持有当前字幕区间的旧帧。`XySubFilter` 因此在重新下发配置、递增 `m_context_id`
之后，通过 `ISubRenderConsumer2::Clear()` 请求 consumer 丢弃旧帧并重新请求。

先取得 consumer 的临时引用，释放 `m_csConsumer` 后再调用 `Clear()`，避免
consumer 在清理过程中同步回调 provider 导致锁重入。

被否方案：在 `HdmvSubtitleProviderImpl::RequestFrame` 内每帧读取 consumer
选项。run `39984554888` 的该实现导致真实播放稳定性退化和花屏，已撤回。

## D26：位图字幕的实际后端显示为 Bitmap (internal)

PGS/DVB 走 `CompositionObject` 位图管线，不经过 libass、CSRI 或 VSFilter 文本
渲染器。原先把非文本流的实际后端显示为 `VSFilter`，会让用户误以为 HDR 设置
没有生效是因为后端选错。属性页现在根据 `BOOL_IS_BITMAP` 显示
`Bitmap (internal)`；API 返回值保持兼容。

## D27：SUP 的目标是 PQ 预编码，不是 SDR 直通

SUP/PGS 调色板仍是 SDR 8bit YCbCr。修复目标是把目标 HDR 亮度按 PQ 曲线
编码回 8bit SDR 容器码值（例如 203 nits → 148），再由 madVR 的 HDR 处理
恢复绝对亮度。直接关闭映射让原始 SDR 码值进入 HDR 管线，正是需要解决的过曝
路径，不作为默认或目标方案。

## D28：Auto 优先使用 consumer 视频引脚的传递函数

`yuvMatrix=2020` 只能说明色域，不能证明传递函数是 PQ/HLG；SDR BT.2020 内容
会被旧规则误判。`XySubFilter` 连接 consumer 时枚举其已连接视频输入引脚，读取
`VIDEOINFOHEADER2.dwControlFlags`：

- PQ（SMPTE ST 2084）或 HLG → 自动启用 PQ 预编码。
- 明确的 BT.709/SMPTE 170M/240M/线性/对数/BT.2020 SDR → 不启用。
- 没有颜色信息或未知传递函数 → 回退到 `yuvMatrix=2020`。

显式 `Off/PQ/Relative white` 始终覆盖自动判定。

后续实测发现内置 consumer 的 `yuvMatrix` 从未赋值，Auto 实际没有 BT.2020
回退输入。现已由 `XySubFilterConsumer::UpdateYuvMatrixFromInputType()` 从输入
`VIDEOINFOHEADER2` 生成 `TV/PC.601/709/2020`；HDR10/HDR10+/DoVi side data
再通过 `isHdrVideo` 事件补充确认。

第二次实机仍失败后，检测进一步改为扫描整张 DirectShow 图，而不是只看 consumer
自己的输入引脚。原因是 consumer 可能是转换器或 madVR，其连接媒体类型已经丢失
解码器输出上的颜色元数据。明确的传递函数码 8/10（PQ/HLG）即使缺少
`AMCONTROL_COLORINFO_PRESENT` 也接受；普通 SDR 曲线仍必须带 colour-info 标志。
不按 10bit 位深猜测 HDR，因为实测 SDR 片源也可以是 10bit。

第三次实机反馈仍为 Auto=Off 后，graph scan 保留并上报 BT.2020 matrix 位，
在 transfer 未知时用其触发 PQ；同时在 `%TEMP%\xy_hdr_diag.txt` 记录所有视频
引脚的原始媒体类型和 `dwControlFlags`，以便下一次不再依赖属性页人工抄写。

用户的视频文件命名标准化，因此增加 `IFileSourceFilter` 文件名回退。独立的
`HDR` 标记优先于 BT.709/unknown 元数据；匹配只作用于文件名且排除 `HDRip`，
避免把普通高清录制误判为 HDR。

## D29：HDR generation 与属性页批量刷新

只比较 mode/white 不足以覆盖“配置被重新应用但数值相同”和 consumer HDR 提示变化。
因此 HDR 配置携带递增 generation，并进入 SubPic 缓存判据、调色板重算判据和外层
bitmap ID。属性页一次 Apply 同时写入 RGB 输出范围、位图 HDR 模式、白点与文本 HDR
模式时，通过 batch 合并为一次 `UpdateSubtitle()` 和一次 consumer `Clear()`。
`Clear()` 的 QI 与 HRESULT 会显示在属性页状态中；不得把 consumer 配置读取放回
`RequestFrame` 每帧热路径。

## D30：HDMV SubPic 按完整 HDR 配置失效

原先 `HdmvSubtitleProviderImpl` 只在 generation 变化时丢弃 SubPic。Auto 的
matrix 回退从 Off 变为 PQ 时 generation 可能未变，配置语义却已变化，导致旧
SubPic 被复用。现在保存上一次完整 `HdrColor::Config`，用 `IsSameRevision()`
同时检查模式和 generation。

## D31：Relative white 退出用户界面

Relative white 是早期为“consumer 把字幕当线性 SDR”准备的备选，但 madVR 会把
SUP 位图纳入 HDR 处理，实测继续过曝。用户界面只保留 Auto / Off / PQ；枚举值和
映射实现暂时保留，避免改变既有外部消费者的 ABI。

## D32：HDR 状态与 backend 分行显示

把较长的 HDR 诊断拼进 Main 页的 Actual backend 会截断两边内容。Main 页只显示
实际 backend；More 页底部单独显示 `Auto>PQ`、白点、`tf/sd/matrix` 与 `Clear()`
状态。具体页面尺寸和分栏由 D33 取代。

## D33：属性页统一为宽幅两栏，并保留编辑选区

属性页原先有多套尺寸：Main 为 `247×262` DLU，More 为 `247×420` DLU，其余页为
`247×188` DLU。More 页在 HDR 控件增加后变成纵向长页，切换标签时窗口尺寸也来回
变化。

现在 XySubFilter 的 Main / More / Timing / Paths / About 全部使用 `360×280` DLU。
Main 和 More 重排为左右两栏：

- Main 左栏：常规设置、文字设置、加载、系统托盘。
- Main 右栏：VobSub 设置、SSA/ASS 设置与实际渲染后端。
- More 左栏：缓存、渲染布局、即时应用/重新加载等行为。
- More 右栏：字幕输出、HDR、文本渲染器、视频渲染器重定位。
- HDR 诊断保留为底部整行，不挤压任何下拉框。

Timing / Paths / About 原先与 DirectVobSub 共用对话框资源。为统一 XySubFilter
页面尺寸而不改变 DirectVobSub 的属性页，新增
`IDD_XY_SUB_FILTER_TIMINGPAGE` / `IDD_XY_SUB_FILTER_PATHSPAGE` /
`IDD_XY_SUB_FILTER_ABOUTPAGE`，并给对应页面构造函数增加可选 `dialogId`；
共享资源保持原尺寸。

白点输入的原问题不是输入法，而是即时应用路径：每次 `EN_CHANGE` 都会执行
`UpdateControlData(false)`，用 `SetWindowText` 重写编辑框，Windows 因而把插入符
移到行首。修复分两层：

1. 页面应用前后保存和恢复当前 `Edit` 的 `EM_GETSEL` 选区与焦点。
2. 白点归一化文本和当前文本相同时不调用 `SetWindowText`。

被否方案：关闭白点的即时应用。这样会改变现有交互语义，而且其他触发整页刷新的
选项仍可能出现同类光标跳动。

## D34：HDR 证据合并与 Auto 判定提取为纯函数

`UpdateConsumerHdrSignal()` 仍负责启动图探测，但不再内嵌“哪种证据优先”的规则。
`HdrDetection::HdrEvidenceSet` 负责顺序无关地合并 transfer/matrix/mastering 证据，
`HdrDetection::ResolveAutoPq()` 负责最终 Auto 判定。

这样可以直接单测 HDR 与 SDR 冲突、未知传输函数、side data、文件名 HDR 和
BT.2020 回退，而不需要构造 COM 图。规则本身保持既有优先级：side data、
明确 PQ/HLG、文件名 HDR、明确 SDR、BT.2020 / consumer `yuvMatrix`、Off。

被否方案：把所有规则继续留在匿名命名空间中。这样只能跑集成测试，无法稳定覆盖
多引脚顺序和冲突证据。

## D35：运行时界面文本统一走双语言资源

属性页中运行时创建的组合框、对话框标题、缓存统计和诊断提示全部使用
`IDS_XY_UI_*`，不再由 C++ 直接拼接用户可见英文。纯技术产品名和标准缩写
（libass、VSFilter、CSRI、PQ、HLG、BT.2020、nits、GDI、Uniscribe）保持原样。

`Test-ResourceLocalization.ps1` 在 `debug-x64` 云端构建前检查英文/中文资源字符串
ID、对话框控件 ID，并拒绝属性页新增未标记豁免的硬编码 `AddString`。

被否方案：只修 Paths 页和 HDR 下拉项。这样 English/中文切换仍会出现半英文界面，
也会继续产生两套重复填充代码。

## D36：HDR 诊断日志通过属性页按钮打开

日志路径由 `XySubFilter::GetHdrDiagnosticPath()` 统一提供，More 页按钮通过
`ShellExecuteW` 打开现存日志。路径为空、文件不存在或打开失败时显示本地化提示，
不自动生成空日志，避免用户误把空文件当作有效诊断结果。

## D37：多日真实使用关闭稳定性阻塞

2026-10-28 用户确认 PotPlayer + madVR 多日使用无异常。没有稳定复现的历史
seek 崩溃不再作为当前阻塞，也不宣称找到了根因；后续若再次出现，使用
`scripts/Test-SeekStability.ps1` 收集进程退出、WER、bugreport 和 dump 证据。

## D38：Auto 图探测只沿活动视频路径

原实现先检查 consumer，再枚举整张图；任意已连接视频引脚的 HDR 都可能提前结束扫描，
既缺少拓扑约束，也让无法独立比较的 transfer/matrix 证据受枚举顺序影响。

现在 `ProbeHdrGraph()` 从所选 consumer 的视频输入引脚沿 `ConnectedTo()` 反向追踪
活动路径，只读取该路径上已连接输入的媒体类型。扫描限制为 64 个 filter、128 个
pin；未连接引脚和旁路 filter 不参与决策。媒体类型解析同时覆盖
`FORMAT_VideoInfo2`、`FORMAT_MPEG2_VIDEO` 和 `FORMAT_DiracVideoInfo`。

被否方案：保留整图扫描后只调整优先级。这样仍可能被辅助视频流或无关分支污染，
也无法保证证据顺序稳定。

## D39：文件名之后才读取本地 MKV 头部

文件名中的独立 `HDR` 标记是该用户的规范命名主信号。为减少无意义 I/O，只有文件名
未提示 HDR、活动视频路径提供本地 `.mkv` 路径时才启动容器探针。

探针最多读取 2 MiB，最多处理 4096 个 EBML 元素和 8 层嵌套，支持未知长度 Segment；
读取 Tracks/Colour/MasteringMetadata/MaxCLL/MaxFALL，不读取 Cluster 或视频样本。
H.273 transfer=`16`/`18` 转换为内部 HDR，matrix=`9`/`10` 作为 BT.2020 回退。

多视频轨使用当前 DirectShow 媒体签名中的 codec、宽高和帧时长做唯一匹配；无法
唯一命中的轨道不采用。文件结果按规范化路径、文件大小和 mtime 缓存。

被否方案：直接解析第一条视频轨。多轨文件中可能误用辅助轨，不能作为“当前播放轨”
的可靠替代。

## D40：媒体签名变化只在边界触发一次重扫

连接时完成一次图与文件探测。播放中 `UpdateParamFromConsumer()` 检测到原始尺寸、
AR 尺寸、帧率或 consumer `yuvMatrix` 变化时，只设置一次重扫标记；下一次
`RequestFrame()` 处理该标记并执行一次有界探测。常规逐帧路径不读取文件。

只有最终 Auto PQ/Off 结果变化才递增有效 HDR generation；side-data 事件同样先比较
Auto 结果，避免文件名已经确定 PQ 时仅因冗余提示触发不必要的字幕刷新。

被否方案：在 `RequestFrame()` 每帧读取 consumer 或文件。该做法已被实际播放证明会
造成稳定性退化，也会制造不必要的 I/O。

## D41：HDR 配置诊断必须保留运行态并区分字幕类型

ASS/libass 下的文本 PQ 不受文件名或 Auto 判定影响，因此“显式 PQ 无效”必须能从
运行态区分三种情况：属性页没有把文本模式写入 provider、写入后没有触发刷新，
或刷新后实际渲染并未使用预期后端。原日志在断开连接时以全零证据覆盖上一次探测，
无法完成这种判断。

现在日志改为追加写入，超过 512 KiB 后轮换；探测断开只追加事件并保留上次证据。
每次 HDR 配置刷新记录实际后端、位图/文本请求模式、解析后模式、白点、generation、
yuvMatrix 和当前 HDR 证据。资源标签改为“Bitmap HDR / Text HDR”与
“位图字幕 HDR / 文本字幕 HDR”，状态行增加字幕类型前缀，避免把两套独立模式
混为一个全局开关。
