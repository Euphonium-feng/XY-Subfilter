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

## D5：先做位图字幕，文字字幕只预留接口

需求明确指向 SUP 字幕。ASS/SRT 由 libass 输出 ARGB，管线不同（`RTS.cpp` 的 tile 合成路径），改动面和回归风险都更大。因此在 `CBaseSub` 层预留 `SetHdrConfig`，文字字幕暂不接入。

## D6：界面中文化采用新增语言块而非直改英文

`.rc` 已有匈牙利语空块与英文块，`CString(MAKEINTRESOURCE)` 依赖线程 UI 语言。直接改英文块会让英文用户失去英文界面。

方案：新增 `LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED` 全量块，新增 `INT_UI_LANGUAGE`（Auto / English / 中文），Auto 跟随 `GetUserDefaultUILanguage()`，通过 `SetThreadUILanguage()` 切换并重建属性页。

## D7：云构建闭环

按全局规范，本地构建不算验证。仓库建立后每轮改动都走"提交 → push → `gh run watch --exit-status`"，红色则读 `gh run view --log-failed` 修因重推，直至绿。

当前仓库目录原本没有 `.git`，账号下也无对应远程仓库，因此首次提交包含全量源码，并用 `git update-index --cacheinfo` 直接登记 9 个 SMP 子模块的 gitlink（避免本地克隆数百 MB），由 CI 的 `submodules: recursive` 拉取源码。
