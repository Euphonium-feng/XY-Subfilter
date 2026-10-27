# 任务进度

## 状态

2026-10-28 用户确认：当前构建已在 PotPlayer + madVR 下连续多日实机使用，
HDR 亮度、Auto/Off/PQ、seek、五个属性页和连续输入白点均未出现问题。
原先的 seek 稳定性阻塞关闭；历史崩溃未能稳定复现，因此没有根因结论。

深层 Auto HDR 阶段一+二已实现并通过三矩阵；本轮没有再次启动播放器实机测试。
仓库已于同日切换为 public，以避免私有仓库 Actions 分钟额度再次阻塞标准 runner。

可靠性收口代码已完成，并通过云端三矩阵 CI（run `40552057870`；
最终记录提交 run `40553848048`）：

- `src/subpic/hdr_evidence.{h,cpp}` 提供可单测的 HDR 证据合并和 Auto 判定。
- 属性页运行时下拉项、缓存窗口和提示文案已接入中英文资源。
- More 页增加“打开日志”按钮，打开 `%TEMP%\xy_hdr_diag.txt`。
- `scripts/Test-SeekStability.ps1` 取代本地 `.debug/repro-seek.ps1`。
- `scripts/Test-ResourceLocalization.ps1` 在 `debug-x64` 构建前校验资源。
- HLG 保留现有处理，但不在个人使用范围内，也未做专项验收。

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

`39962614076` 的两项失败：

- **x64**：`TextColorPqKeepsAlphaAndLowersWhite` 期望值写错。输入 `0xFF808080` 是中间灰，PQ 映射后为 106（128 → 0.19 线性 → 38.8 nits → 码 106），不是参考白的 148。已拆分出纯白用例验证 148 端点。
- **win32**：`yasm` 退出码 `-1073741515`（`STATUS_DLL_NOT_FOUND`）。runner 环境偶发，同一工作流上一轮与下一轮均通过，与代码无关。

## 已关闭的真实验证

- PotPlayer + madVR 多日使用通过，HDR 亮度、Auto/Off/PQ、seek 和文本/位图字幕均正常。
- Main / More / Timing / Paths / About 五个标签页和连续输入白点已由用户确认无问题。
- 多日使用未复现历史 seek 崩溃；旧 `D:\PotPlayer\bugreport.txt` 仍不属于本轮证据。
- HLG 未进入个人使用范围，保留为未专项验证项，不作为本轮阻塞。

## 下一步

### 深层 Auto HDR

阶段一+二代码已完成并推送；run `40569779347` 已确认
`release-win32`、`release-x64`、`debug-x64` 全绿。本轮按计划只做自动化验收，
未启动 PotPlayer 或进行人工画面测试。codec extradata 仍不在本轮范围。

### 现有验收收尾

1. 绿色产物下载在当前网络下被 Azure Blob 连接 EOF 阻塞；网络恢复后下载
   `vsfilter-xysubfilter-x64`，记录 SHA256，并检查五个属性页的中英文标签与下拉项、
   诊断按钮存在/缺失提示。
2. 对最终 DLL 做一次播放器冒烟测试，确认属性页和诊断按钮可用。
3. 将产物 SHA256 和冒烟结果写回本文件；代码和资源本身已通过三矩阵 CI。
