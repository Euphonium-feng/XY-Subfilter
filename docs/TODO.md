# 任务进度

## 状态

功能实现已推送，云端 CI 三矩阵全绿。真实播放器稳定性存在**疑似回归**：用户报告
PotPlayer + madVR 在 seek 时至少崩溃两次；当前自动化压力回路尚未复现，也尚未证明
问题由本轮改动引入。

2026-10-24 又收到一个更明确的渲染问题：SUP/PGS 在 HDR 下无论切换 HDR 模式还是
白点，画面都没有变化。随后尝试的“provider 每帧重新读取 consumer 选项”版本
（run `39984554888`）虽然云端构建全绿，但真实播放出现稳定性问题和花屏，已撤回。
当前改为在 HDR 选项变化后清空 consumer 的缓存帧，让下一帧重新请求；真实播放器
验证尚未完成。

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

## CI 记录

| Run | 内容 | 结果 |
| --- | --- | --- |
| `39944694745` | 仓库基线 | success |
| `39949013750` | 位图 HDR 映射 + 初版中文化 | success |
| `39951652326` | README 重写 | success |
| `39962614076` | 界面修复 + 文本 HDR 映射 | failure（两项，见下） |
| `39963898276` | 修正测试期望 | success |
| `39965292609` | docs 记录 | success |

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

## 下一步

1. **唯一优先项**：把鼠标点击/拖动 seek bar 的路径加入压力回路，在 2160p HDR
   素材上循环到变红，并抓取崩溃模块与调用栈。若仍未复现，再启用 WER LocalDumps。

2. 用 `J-焦虑` 或 `B-巴里·林登` 的 SUP 在 HDR 下复测：保持同一字幕帧，切换
   `Off`、`PQ 203`、`PQ 50`、`Relative white 203`，确认画面至少随模式或白点变化；
   若仍完全不变，再抓取 XySubFilter 实际加载的 provider 类型和属性页对象实例。

其余待验证项保留在上方清单，疑似稳定性问题查清前不推进默认值调整或排版收尾。
