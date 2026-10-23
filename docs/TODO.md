# 任务进度

## 状态

本轮改动（界面修复 + 文本字幕 HDR 映射）已实现，待云端 CI 验证。

## 已完成

### 第一轮：位图字幕 HDR 映射

| 阶段 | 内容 |
| --- | --- |
| 调研 | 梳理 SDR/HDR 两条管线，定位四个过曝成因 |
| 0 | `src/subpic/hdr_color.{h,cpp}`：PQ OETF/EOTF、nits 与码值互转、BT.1886 线性化 |
| 1 | `CompositionObject::InitColor` 调色板级映射；`SetHdrConfig` 沿 `CBaseSub` 直通 |
| 2 | 自动判定（`yuvMatrix` 以 2020 结尾）+ 手动覆盖 |
| 3 | P010/P016 路径免改（PQ 8bit 码经 `src<<8` 得到正确 10bit 码） |
| 4 | `INT_PGS_HDR_MODE` / `INT_PGS_HDR_WHITE_NITS` / `INT_UI_LANGUAGE` 选项与属性页 |
| 5 | `VSFilter.rc` 简体中文语言块 |
| 6 | `test_hdr_color.h` 单测；CI run `39949013750` 三矩阵全绿 |
| 7 | README 中英双版 + docs 同步 |

### 第二轮：界面修复 + 文本字幕 HDR 映射

| 阶段 | 内容 |
| --- | --- |
| A | 启用 `AFX_TARG_CHS`（中文资源块此前从未编译）；26 处字体改为 Microsoft YaHei UI；修正误译的窗口类名 `"静态色"`；修正 More/Main 页高度；属性页在宿主线程切换资源语言 |
| B | `INT_TEXT_HDR_MODE` 选项全链路（ID/注册表/属性页控件/`ResolveTextHdrConfig`） |
| C | libass 路径：`MapTextColor` + `SetHdrConfig` + 六处 call site + 缓存失效 |
| D | 旧 VSFilter 渲染器：`PaintAll` 经 `TransColorWithHdr` 映射，配置随 `DoRender`/`RenderOneSubtitle` 下传 |
| E | 文本颜色单测、docs 更新 |

## 待验证（需真实环境，CI 无法覆盖）

1. **madVR 是否把字幕位图按 PQ 解读**。决定「自动」模式是否应保持默认；若是按 SDR 处理，应改用「相对白」。
2. **自动判定的误判率**：SDR 的 BT.2020 内容会被判为 HDR。
3. **中文界面在真实对话框中的排版**：改用雅黑 9 号后，原有按 8 号字号排布的控件可能出现文字截断。
4. **HDR 片源下的实际观感**。
5. **属性页语言切换**：`AFX_TARG_CHS` 与 `SetThreadUILanguage` 的组合需在真实播放器中确认能正确切到中文界面。

## 下一步

1. 观察本轮 CI 结果；若资源编译或新增参数有误，按 `gh run view --log-failed` 修因重推。
2. 在 HDR10 片源 + PGS 与 ASS 字幕下实测各模式，确定默认值。
3. 逐页目视检查中文属性页排版。
