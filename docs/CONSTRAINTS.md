# 约束

## 构建

- 构建必须是云端构建：提交 → 用 `gh` push → GitHub Actions 工作流（`.github/workflows/main.yml`，名 `Regression CI`）产出产物。本地构建不算验证。
- 推送后用 `gh run watch --exit-status` 盯到退出码干净才算绿；红则 `gh run view --log-failed` 定位真因、修复、再推。
- 未拿到绿色运行前，不得报告构建通过。
- 工作流矩阵为 `release-win32` / `release-x64` / `debug-x64`，三者都要绿。

## 环境限制

- 本机无 MSVC / MSBuild / yasm，`SMP/*` 子模块在本地为空目录。所有编译与测试验证只能来自云端 CI。
- 仓库根 `SMP/` 下 9 个子模块（libass、harfbuzz、freetype2、fontconfig、fribidi、libiconv、libxml2、liblzma、zlib）均指向 `https://github.com/ShiftMediaProject/<name>.git` 的 master，以 gitlink 形式记录，由 CI 递归拉取。
- win32 构建的 `yasm` 步骤偶发 `-1073741515`（`STATUS_DLL_NOT_FOUND`），属 runner 环境问题，重跑通常通过；不要把该失败当作代码缺陷。

## 色彩与精度

- PGS/HDMV 调色板是 8bit `Y, Cb, Cr, T`，没有 10/12bit 变体，这是格式本身的硬限制。
- 现有色彩转换（`color_conv_table.cpp`）全部是 8bit 定点（16.16），处于热路径，不得在其中引入浮点或改变既有数值结果。
- `MSP_P010` / `MSP_P016` 是 16bit 容器，但现有实现把 8bit 值 `<<8` 放入高位，并无真正的 10bit 精度。
- 参考白按 BT.2408：HDR 图文字幕 203 nits。默认白点范围 50–1000 nits。
- `ASS_Image::color` 是 libass 的 `0xRRGGBBAA`（alpha 反向），每图一个 DWORD，不是逐像素；转换为项目 `0xAARRGGBB` 的公式是 `(color << 24) ^ (color >> 8) ^ 0xFF000000`。
- `HdrColor::MapArgb` 只改 RGB 三分量、保留 alpha 字节。文本路径的 alpha 是覆盖度遮罩的输入，绝不可被映射改动。

## 协议

- `include/SubRenderIntf.h` 1.0.9 没有传递函数（EOTF）字段。provider 无法通过协议告知消费端字幕位图是 SDR 还是 PQ 编码，只能靠选项约定或消费端已知行为。
- `SubRenderIntf.h` 为第三方 BSD 协议头，接口版本语义需保持兼容，不修改其定义。

## 兼容性

- `DirectShow` / `AviSynth` / `VirtualDub` / `CSRI` 四条接入面都要保持可用。
- 新增选项 ID 必须小于 `XyOptionsImpl::MAX_OPTION_ID`（127）。当前 `OPTION_COUNT = 100`，最大 ID 为 99。
- 语言资源不得影响现有英文界面的字符串 ID 与行为。
- 外部消费者（如 madVR）把自己编译的选项常量传给 provider，既有 ID 数值不得改变。

## 已排除的做法

- 不在 `color_conv_table.cpp` 中直接加 PQ 运算（污染定点热路径、破坏既有测试基线）。
- 不把字幕面升级为 16bit（混合核、分配器、所有消费端都要改，回归风险过大）。
- 不默认对所有片源开启 HDR 映射（会让 SDR 内容变淡）。
- 不在枚举中间插入新选项 ID（会位移既有 ID，破坏外部消费者的 ABI）。
- 不在 `XySubRenderFrameCreater::TransColor` 内统一做 HDR 映射（它是进程级单例，libass 文本、旧渲染器文本与 PGS 位图共用，会互相污染）。
- 不原地改写 libass 持有的 `ASS_Image::color`（第三方数据结构，生命周期不受本项目控制）。

## 已验证的实现约束

- 新增选项 ID 必须追加在 `DirectVobSubXyOptions` 枚举末尾、`OPTION_COUNT` 之前。当前新增项为 96–99。
- PQ 模式下 ARGB/文本路径必须输出全幅范围，否则 PQ 码会被 TV 范围二次压缩、白点被拉低约 9%。
- `.rc` 中文块以 GBK 存储并配 `#pragma code_page(936)`；英文块保持 ASCII。若编辑器把文件另存为 UTF-8 而 pragma 未改，中文会变成乱码。
- 中文资源块必须与英文块包含完全相同的控件 ID 与字符串 ID，否则切换语言后控件缺失。
- 资源编译必须同时定义 `AFX_TARG_ENU` 与 `AFX_TARG_CHS`；缺后者会让整个中文块被预处理器丢弃。
- 对话框 `FONT` 必须使用矢量字体。`MS Sans Serif` 是 `SSERIFE.FON` 光栅字体，无 CJK 字形且不参与字体链接，会导致中文标签整片不显示。
- 属性页由宿主 UI 线程创建，不继承滤镜线程的 `SetThreadUILanguage`；页面入口需自行切换资源语言。
- 缓存失效必须覆盖两层：libass 的 `m_last_frame`/`BitmapMruCache`，以及旧渲染器的 `DrawItemHashKey`（后者已对完整 `switchpts` 做 `memcmp`）。
- 文本 HDR 默认必须解析为 `MAP_OFF`，否则 `libass_regression_test` 的 golden SHA256 基线会全部失效。
- 不得在 `HdmvSubtitleProviderImpl::RequestFrame` 内每帧回调 consumer 读取
  HDR 选项；该路径已在真实播放中导致稳定性退化和花屏。选项变化后清空
  consumer 缓存帧，而不是把配置读取放到渲染热路径。
- Auto 判定必须优先采用 consumer 视频引脚 `VIDEOINFOHEADER2` 中的传递函数；
  BT.2020 矩阵只是在没有传递函数信息时的回退，不能覆盖明确的 SDR 曲线。
- 不能按 10bit/P010 位深自动判定 HDR；10bit SDR 同样常见。consumer 引脚不含
  元数据时必须沿活动视频路径向上游读取，不得枚举无关分支，也不得把路径扫描
  放进逐帧热路径。
- Auto 的 BT.2020 回退必须同时接受 consumer `yuvMatrix` 字符串和图内
  `VIDEOINFOHEADER2` matrix 位；明确的 SDR transfer 仍优先。
- 文件名回退只检查源滤镜返回的文件名本身，不检查目录；`HDR` 必须为独立标记，
  不能把 `HDRip` 当作 HDR。
- 本地 MKV 探针只读取前 2 MiB、最多 4096 个 EBML 元素和 8 层嵌套；不读取
  Cluster 或视频样本。文件名已有 HDR 标记、远程路径、UNC、设备路径和非 MKV
  文件不得触发文件读取。
- MKV 使用 H.273 transfer/matrix 枚举；PQ=`16`、HLG=`18`，不得与 DXVA2 的
  PQ=`8`、HLG=`10` 混用。多视频轨无法按当前媒体签名唯一匹配时返回 Unknown。
- 图探测必须支持 `FORMAT_MPEG2_VIDEO` 与 `FORMAT_DiracVideoInfo` 中的嵌套
  `VIDEOINFOHEADER2`，并限制为 64 个 filter、128 个 pin；顺序不得改变结论。
- 内置 consumer 必须在媒体类型变化时填充 `yuvMatrix`，否则 provider 的 BT.2020
  回退没有输入；HDR side data 只能作为事件提示，不得恢复每帧读取 consumer 配置。
- 每次 HDR 配置变化必须递增 generation，并让 SubPic、调色板和对外 bitmap ID
  一起失效；仅比较 mode/white 数值不足以证明缓存已刷新。
- 只有最终 Auto PQ/Off 结果变化才需要递增有效 HDR generation；仅诊断字段变化
  不得造成无意义重渲染。
- 即使 generation 不变，Auto 因矩阵回退从 Off 解析到 PQ 时也必须丢弃 SubPic；
  provider 的失效判据必须比较完整 HDR 配置。
- 属性页一次 Apply 引发的 RGB/HDR 选项变化必须合并，只执行一次重新渲染和一次
  `ISubRenderConsumer2::Clear()`。
- XySubFilter 的属性页必须共用 `360×280` DLU 的窗口尺寸；Main 与 More 使用
  左右两栏，不允许再退回高窄的纵向布局。
- 即时应用会重建控件文本。必须保留正在编辑的 `Edit` 控件选区/插入符，且在
  归一化文本与当前文本相同时不得再次调用 `SetWindowText`。
- SUP/PGS 的目标是先把 HDR 绝对亮度按 PQ 曲线编码到 8bit SDR 容器，再由
  madVR 做 HDR 处理；SDR 直通只用于对照，不是默认方案。
- `Relative white` 已证明会让 madVR 路径过曝，不再暴露给用户；保留枚举值只为避免
  改变既有外部 ABI。
- 位图与文本字幕共用 HDR Auto 证据和白点，但模式选项独立；任何 consumer 信号
  修复必须同时验证两条管线，不能只修 PGS。ASS/SSA/SRT 只受文本模式控制。
- HDR 证据合并和 Auto 判定必须使用 `HdrDetection` 纯函数；DirectShow 引脚枚举
  只能负责采集，不得在扫描循环中重新实现优先级。
- 属性页运行时用户可见文本必须来自 `IDS_XY_UI_*`（产品或标准缩写除外）；
  `Test-ResourceLocalization.ps1` 失败时不得绕过，除非在对应行标注
  `l10n-exempt:` 并说明技术理由。
- `%TEMP%\xy_hdr_diag.txt` 在探测或 HDR 配置刷新时生成并追加，超过 512 KiB
  后轮换；disconnect 不得覆盖上次有效证据。打开日志按钮本身不得创建空文件。
- HLG 当前与 PQ 共用 HDR 证据分类，个人范围不做专用映射或专项验收；文档不得把
  HLG 观感描述为已验证。
