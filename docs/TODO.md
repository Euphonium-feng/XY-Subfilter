# 任务进度

## 状态

功能已实现并通过云端 CI 验证。剩余文档收尾。

## 已完成

| 阶段 | 内容 | 验证 |
| --- | --- | --- |
| 调研 | 梳理 SDR/HDR 两条管线，定位四个过曝成因 | 代码级确认（见 `ARCHITECTURE.md`） |
| 0 | `src/subpic/hdr_color.{h,cpp}`：PQ OETF/EOTF、nits 与码值互转、BT.1886 线性化、ARGB 映射 | CI 编译通过；单测通过 |
| 1 | `CompositionObject::InitColor` 调色板级映射；`SetHdrConfig` 沿 `CBaseSub → CHdmvSub/CDVBSub → Provider` 直通 | CI 通过 |
| 2 | `ResolvePgsHdrConfig` 自动判定（`yuvMatrix` 以 2020 结尾）+ 手动覆盖；`XySubFilter::SetHdrMode` 下发 | CI 通过 |
| 3 | 验证 P010/P016 路径：PQ 8bit 码经 `src<<8` 得到正确 10bit 码，无需改混合核 | 数值验算（203 nits → 592/1023 ≈ 199 nits） |
| 4 | `INT_PGS_HDR_MODE` / `INT_PGS_HDR_WHITE_NITS` / `INT_UI_LANGUAGE` 选项、注册表持久化、属性页控件 | CI 通过 |
| 5 | `VSFilter.rc` 简体中文语言块（code_page 936）+ 界面语言切换 | CI 通过 |
| 6 | `test_hdr_color.h`：PQ 端点/往返/单调性、Off 模式逐字节不变、alpha 保留、白点变化、16bit 字对齐 | CI 三矩阵全绿（run 39949013750） |
| 7 | README 中英双版（中文为默认、英文为 `README.en.md`）与 docs 同步 | — |

## CI 记录

- `39944694745` 基线：success（9m26s）
- `39949013750` HDR 映射 + 中文化：success，三个矩阵（debug-x64、release-win32、release-x64）全部通过

## 未验证项

以下行为无法由 CI 覆盖，需在真实播放环境中确认：

1. **外部消费端（madVR）的位图传递函数语义**。目前 PQ 预编码假设消费端把字幕位图按 PQ 解读。若实际按 SDR 处理，应切到「相对白」模式。
2. **自动判定的误判率**。SDR 的 BT.2020 内容会被判为 HDR，需实测该场景出现频率。
3. **中文界面在真实 Windows 对话框中的排版**。CI 只保证资源编译通过，不检查文字是否溢出或截断。
4. **HDR 片源下的实际观感**（字幕是否仍偏亮或偏暗）。

## 下一步

1. 在 HDR10 片源 + PGS 字幕下实测三种模式，确定默认模式是否合适。
2. 检查中文属性页文字是否溢出，必要时调整控件宽度。
3. 若需要覆盖 ASS/SRT 文字字幕，按 `BaseSub` 已预留的 `SetHdrConfig` 接口接入 libass 输出路径。
