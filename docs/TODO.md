# 任务进度

## 状态

前置阶段进行中。

## 已完成

- 代码调研：确认 SDR/HDR 两条管线的完整数据流，定位四个过曝成因（见 `ARCHITECTURE.md`）。
- 建立 `docs/` 五件套。
- `git init`，登记 9 个 SMP 子模块 gitlink。

## 进行中

- 建立远程仓库并首推，确认 `Regression CI` 首次运行。

## 待办

| 阶段 | 内容 |
| --- | --- |
| 0 | `src/subpic/hdr_color.h/.cpp`：PQ OETF/EOTF、nits 换算、ARGB→PQ 映射、相对白缩放 |
| 1 | `CompositionObject` 调色板级映射；`SetHdrConfig` 沿 `BaseSub`/`CHdmvSub`/`CDVBSub`/各 Provider 直通 |
| 2 | `ResolveHdrMode` 自动判定；`XySubFilter::SetHdrMode` 下发 |
| 3 | `MemSubPic` P010/P016 混合核按 `HdrConfig` 建 256 项 LUT |
| 4 | `INT_PGS_HDR_MODE` / `INT_PGS_HDR_WHITE_NITS` 选项、注册表、属性页控件 |
| 5 | `.rc` 简体中文语言块 + `INT_UI_LANGUAGE` 切换 |
| 6 | `test/unit_test/test_hdr_color.h` 并接入 `main.cpp` 与 `unit_test.vcxproj` |
| 7 | `README.zh-CN.md` |

## 阻塞项

无。

## 下一步

提交并推送初始版本，等待 `Regression CI` 结果。若工作流首次运行即出现与本次改动无关的既有失败，需先记录基线，再逐阶段推进。
