# 约束

## 构建

- 构建必须是云端构建：提交 → 用 `gh` push → GitHub Actions 工作流（`.github/workflows/main.yml`，名 `Regression CI`）产出产物。本地构建不算验证。
- 推送后用 `gh run watch --exit-status` 盯到退出码干净才算绿；红则 `gh run view --log-failed` 定位真因、修复、再推。
- 未拿到绿色运行前，不得报告构建通过。

## 环境限制

- 本机无 MSVC / MSBuild / yasm，`SMP/*` 子模块在本地为空目录。所有编译与测试验证只能来自云端 CI。
- 仓库根 `SMP/` 下 9 个子模块（libass、harfbuzz、freetype2、fontconfig、fribidi、libiconv、libxml2、liblzma、zlib）均指向 `https://github.com/ShiftMediaProject/<name>.git` 的 master。

## 色彩与精度

- PGS/HDMV 调色板是 8bit `Y, Cb, Cr, T`，没有 10/12bit 变体，这是格式本身的硬限制。
- 现有色彩转换（`color_conv_table.cpp`）全部是 8bit 定点（16.16），处于热路径，不得在其中引入浮点或改变既有数值结果。
- `MSP_P010` / `MSP_P016` 是 16bit 容器，但现有实现把 8bit 值 `<<8` 放入高位，并无真正的 10bit 精度。
- 参考白按 BT.2408：HDR 图文字幕 203 nits。

## 协议

- `include/SubRenderIntf.h` 1.0.9 没有传递函数（EOTF）字段。provider 无法通过协议告知消费端字幕位图是 SDR 还是 PQ 编码，只能靠选项约定或消费端已知行为。
- `SubRenderIntf.h` 为第三方 BSD 协议头，接口版本语义需保持兼容，不修改其定义。

## 兼容性

- `DirectShow` / `AviSynth` / `VirtualDub` / `CSRI` 四条接入面都要保持可用。
- 新增选项 ID 必须小于 `XyOptionsImpl::MAX_OPTION_ID`（127）。当前 `OPTION_COUNT` = 96。
- 语言资源不得影响现有英文界面的字符串 ID 与行为。

## 已排除的做法

- 不在 `color_conv_table.cpp` 中直接加 PQ 运算（污染定点热路径、破坏既有测试基线）。
- 不把字幕面升级为 16bit（混合核、分配器、所有消费端都要改，回归风险过大）。
- 不默认对所有片源开启 HDR 映射（会让 SDR 内容变淡）。
- 不在枚举中间插入新选项 ID（会位移既有 ID，破坏外部消费者的 ABI）。

## 已验证的实现约束

- 新增选项 ID 必须追加在 `DirectVobSubXyOptions` 枚举末尾、`OPTION_COUNT` 之前。当前最大 ID 为 98。
- PQ 模式下 ARGB 必须输出全幅范围（`CompositionObject::m_output_tv_range_rgb = false`），否则 PQ 码会被 TV 范围二次压缩。
- `.rc` 中文块以 GBK 存储并配 `#pragma code_page(936)`；英文块保持 ASCII。
- 中文资源块必须与英文块包含完全相同的控件与字符串 ID，否则切换语言后控件缺失。
