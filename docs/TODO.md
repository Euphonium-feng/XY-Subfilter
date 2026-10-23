# 任务进度

## 状态

功能实现已推送，云端 CI 三矩阵全绿。真实播放器稳定性存在**疑似回归**：用户报告
PotPlayer + madVR 在 seek 时至少崩溃两次；当前自动化压力回路尚未复现，也尚未证明
问题由本轮改动引入。

2026-10-24 又收到一个更明确的渲染问题：SUP/PGS 在 HDR 下无论切换 HDR 模式还是
白点，画面都没有变化。随后尝试的“provider 每帧重新读取 consumer 选项”版本
（run `39984554888`）虽然云端构建全绿，但真实播放出现稳定性问题和花屏，已撤回。
当前实现已增加 consumer 传递函数检测、HDR generation、合并刷新和带结果的
`Clear()`；代码与自动化测试已完成，CI run `39997437527` 三矩阵全绿，
真实播放器验证由用户暂缓。

2026-10-24 后续实测：PQ 203 的亮度已正确，Auto 在 HDR 片源下仍等同 Off/SDR，
Relative white 仍过曝。已定位到 consumer 的 `yuvMatrix` 从未赋值，并修正：
consumer 从输入 `VIDEOINFOHEADER2` 上报范围/矩阵，HDR media side data 以事件方式
通知 provider；HDMV SubPic 按完整 HDR 配置而非仅 generation 失效。属性页已重排，
Relative white 不再显示。CI run `40007950976` 三矩阵全绿，尚待实机复测。

随后实机仍为 `PQ < Auto = SDR`。已确认不能按 10bit 位深猜测：SDR 测试文件
同样是 10bit，但元数据为 BT.709。当前修复改为扫描整张 DirectShow 图，避免只读
consumer 转换后的引脚；同时接受未设 colour-info 标志但明确为 PQ/HLG 的传递函数
码。待新 CI 绿后复测。

### 稳定性诊断（2026-10-23）

- 实际链路为 PotPlayer 26.07.01 + LAV 0.82 + madVR 0.92.17 +
  `XySubFilter` 3.0.0.0（2026-10-23 构建）。
- `.debug/repro-seek.ps1` 已建立自动压力回路。单次 Right 键已通过画面 OSD
  验证确实从 `00:01:00` 跳到 `00:01:05`，并非空操作。
- 3 轮 × 1058 次键盘往返 seek 后进程均存活，未产生新的 WER、
  `bugreport.txt` 或 crash dump。该结果只能说明当前触发方式不足以复现。
- 旧 `D:\PotPlayer\bugreport.txt` 的日期是 2026-05-09，不能作为本轮证据；
  其历史栈为 madVR `CRendering.TakeScreenshot` 中的 `memcpy` 写越界。
- 当前阻塞：还没有一个能稳定变红的回路；问题只能按疑似缺陷跟踪，尚未进入根因假设或代码修复。

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

根因仍需真实播放器确认：当前只证明了配置读取链路存在缺口，尚未证明 madVR 对
SUP 位图的 HDR 解释方式。下一步应在同一帧分别记录/对比 `Off`、`PQ`、`Relative
white` 的输出。

目标语义（明确，不再把 SDR 直通当目标）：SUP 调色板先按 HDR/PQ 曲线预编码到
8bit SDR 容器码值，再由 madVR 的 HDR 处理恢复绝对亮度。SDR 直通只能作为对照，
不是修复方案。

### 第五轮：HDR 配置可靠性（实机验证暂缓）

| 阶段 | 内容 |
| --- | --- |
| A | Auto 优先读取 consumer 视频引脚的 `VIDEOINFOHEADER2` 传递函数；PQ/HLG 为 HDR，明确 SDR 曲线覆盖 BT.2020 矩阵回退 |
| B | HDR 配置带递增 generation，贯穿 `CompositionObject`、Hdmv SubPic、文本缓存和外层 bitmap ID |
| C | `SetRgbOutputLevel()` 改为按解析后的配置判断 PQ，Auto 解析成 PQ 时也强制 PC 全幅 |
| D | 属性页 Apply 合并 RGB/HDR 刷新；`ISubRenderConsumer2::Clear()` 只调用一次并记录结果 |
| E | 属性页显示实际 provider、解析后的 HDR 模式、白点、缓存清理状态和构建短哈希 |
| F | 增加调色板像素集成测试、generation 测试和传递函数分类测试 |

当前状态：代码修改和测试用例已完成；云端 CI 三矩阵全绿。真实播放器复测暂缓，
后续必须先确认 PotPlayer 实际加载的 DLL 哈希。

### 第六轮：Auto 判定与属性页布局（CI 绿，实机待测）

| 阶段 | 内容 |
| --- | --- |
| A | `XySubFilterConsumer` 从输入 `VIDEOINFOHEADER2` 生成范围/矩阵并填充 `yuvMatrix` |
| B | HDR10/HDR10+/DoVi media side data 通过 `isHdrVideo` 事件通知 provider，不进每帧配置读取热路径 |
| C | HDMV provider 按完整 HDR 配置判断 SubPic 失效，矩阵回退变化会重新渲染 |
| D | 属性页 HDR 区域改为两列四行；HDR 诊断独占底部状态行；Relative white 从界面隐藏 |
| E | 增加视频矩阵分类测试，并把调色板测试改为复用同一 `CompositionObject` |

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

`39962614076` 的两项失败：

- **x64**：`TextColorPqKeepsAlphaAndLowersWhite` 期望值写错。输入 `0xFF808080` 是中间灰，PQ 映射后为 106（128 → 0.19 线性 → 38.8 nits → 码 106），不是参考白的 148。已拆分出纯白用例验证 148 端点。
- **win32**：`yasm` 退出码 `-1073741515`（`STATUS_DLL_NOT_FOUND`）。runner 环境偶发，同一工作流上一轮与下一轮均通过，与代码无关。

## 待验证（CI 无法覆盖，需真实环境）

1. **madVR 等外部消费端如何解读字幕位图**：按 PQ 还是按 SDR。这决定「自动」模式是否该保持默认；若实际按 SDR 处理，应改为「相对白」。
2. **自动判定的误判率**：SDR 的 BT.2020 内容会被判为 HDR，需实测该场景出现频率。
3. **中文属性页排版**：字体从 8pt 光栅字体换成雅黑 9pt 后，原按 8pt 排布的控件可能文字截断，需逐页目视检查。
4. **HDR 片源下的实际观感**：字幕是否仍偏亮或偏暗，四种模式的表现差异。
5. **属性页语言切换在真实播放器中的行为**：`AFX_TARG_CHS` + `SetThreadUILanguage` + 宿主线程的组合需实测。
6. **文本字幕 HDR 映射的主观效果**：ASS 字幕多为高饱和彩色，BT.601 约定下映射后的色偏是否可接受。

文本字幕与图形字幕共用同一 Auto 解析结果和同一个白点设置。之前的 Auto 缺口会
同时影响两者；HDMV 独有的旧 SubPic 复用问题不适用于文本路径，因为
`CRenderedTextSubtitle::SetHdrConfig` 已按完整配置失效整帧与 bitmap 缓存。

## 下一步

1. 云端 CI 三矩阵完成后，在用户恢复测试时核对 PotPlayer 实际加载 DLL 的 SHA256。
   本轮 x64 artifact 的 SHA256 为
   `a0ac4c51f02fcc62332c2737c19ecbb366da02ad6025a7c7b7637ece883e2a1a`。

2. 属性页状态行应显示 `Auto>PQ 203` 且带 `sd1` 或 BT.2020；若仍显示 Auto>Off，
   记录 `tf/sd/matrix` 三项后继续修检测来源。

3. 用 `J-焦虑` 或 `B-巴里·林登` 的 SUP 在 HDR 下复测：保持同一字幕帧，切换
   `Off`、`PQ 203`、`PQ 50`，确认画面随模式或白点变化。

4. 若原始字幕像素已是 `148` 但屏幕不变，继续排查 madVR 对缓存或传递函数的解释；
   若原始像素仍为 `255`，继续排查 provider 实例和配置下发。

其余待验证项保留在上方清单，疑似稳定性问题查清前不推进默认值调整或排版收尾。
