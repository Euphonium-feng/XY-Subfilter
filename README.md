<div align="center">

<a id="top"></a>

# 🎬 XY-SubFilter

**为 DirectShow 播放器打造的 HDR 感知字幕渲染滤镜**

_基于 libass 的 ASS/SSA 渲染 · PGS/SUP 调色板级 HDR 白点映射 · 全中文界面_

<br/>

[![Regression CI](https://github.com/Euphonium-feng/XY-Subfilter/actions/workflows/main.yml/badge.svg)](https://github.com/Euphonium-feng/XY-Subfilter/actions/workflows/main.yml)
[![License](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](License.txt)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20DirectShow-0078D6?logo=windows)](https://learn.microsoft.com/windows/win32/directshow/directshow)
[![C++](https://img.shields.io/badge/C%2B%2B-ATL%20%2F%20MFC-00599C?logo=cplusplus)](https://isocpp.org/)
[![libass](https://img.shields.io/badge/Renderer-libass-FF6F00)](https://github.com/libass/libass)
[![HDR](https://img.shields.io/badge/HDR-BT.2408%20%C2%B7%20203%20nits-8A2BE2)](#hdr)

<br/>

**简体中文** · [English](README.en.md)

</div>

---

<div align="center">

### ✨ 一句话说清它解决的问题

> HDR 片源里，位图字幕的 8bit 调色板被当成 HDR 绝对码值解读，
> "白色" 被推到 **数千 nits**，字幕白得刺眼、压过整幅画面。
>
> 本项目在内置渲染管线里加入 **BT.2408 参考白映射**，
> 让字幕白落回 **203 nits** —— 该亮的地方亮，该安静的地方安静。

</div>

---

## 📖 目录

<table>
<tr>
<td valign="top" width="50%">

**快速上手**

- [💡 这是什么](#what)
- [🚀 安装](#install)
- [🎛️ 使用](#usage)
- [🖼️ HDR 白点映射](#hdr)

</td>
<td valign="top" width="50%">

**深入一点**

- [🔬 为什么 HDR 下字幕会过曝](#why)
- [🧬 渲染管线](#pipeline)
- [🛠️ 从源码编译](#build)
- [⚠️ 已知限制](#limits)

</td>
</tr>
</table>

---

#<a id="what"></a>

# 💡 这是什么

**XY-SubFilter** 是一个 DirectShow 字幕渲染滤镜，为 **MPC-HC / MPC-BE**、**PotPlayer + madVR** 等播放器提供字幕渲染能力。

它使用 **libass** 渲染 ASS/SSA 格式字幕，同时完整保留原有的位图字幕路径：

<table>
<tr>
<th align="left">🎞️ 文本字幕</th>
<th align="left">🖼️ 位图字幕</th>
</tr>
<tr>
<td valign="top">

- ASS / SSA（libass 渲染）
- SRT / TXT / USF / SAMI
- MicroDVD / RealText / SubViewer

</td>
<td valign="top">

- **PGS / SUP**（蓝光原盘）
- VobSub（.idx / .sub）
- DVB 字幕

</td>
</tr>
</table>

> **上游出处** —— 项目源自 [Cyberbeing/xy-VSFilter#16](https://github.com/Cyberbeing/xy-VSFilter/pull/16)，
> 即「带 libass 的 XySubFilter」变体。

<div align="center">

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│  播放器 /     │ ──▶ │  XY-SubFilter │ ──▶ │  视频渲染器   │
│  DirectShow   │     │  字幕合成     │     │  madVR / EVR │
└──────────────┘     └──────────────┘     └──────────────┘
```

</div>

---

#<a id="install"></a>

# 🚀 安装

### 方式一 · 安装脚本（推荐）

```bat
:: 以管理员身份运行
Install_XySubFilter.bat
```

> ⚠️ 安装后**不要移动或删除** `XySubFilter.dll`，否则滤镜注册会失效。

### 方式二 · Scoop

```powershell
scoop bucket add nonportable
scoop install xysubfilter-libass-np -g
```

---

#<a id="usage"></a>

# 🎛️ 使用

在播放器里把 **XY-SubFilter** 选为字幕渲染器：

<table>
<tr>
<th align="left">播放器</th>
<th align="left">路径</th>
</tr>
<tr><td>🎬 <b>MPC-BE</b></td><td>选项 → 字幕 → 字幕渲染器</td></tr>
<tr><td>🎬 <b>MPC-HC</b></td><td>选项 → 播放 → 输出 → 字幕渲染器</td></tr>
<tr><td>🎬 <b>PotPlayer</b></td><td>字幕设置中指定外部 DirectShow 字幕滤镜</td></tr>
</table>

### 🌐 界面语言

属性页 **「更多」** 中提供三档语言切换：

| 选项 | 行为 |
| :--- | :--- |
| `自动`（默认） | 跟随 Windows 系统 UI 语言 |
| `English` | 强制英文界面 |
| `中文（简体）` | 强制简体中文界面 |

---

#<a id="hdr"></a>

# 🖼️ HDR 白点映射

> 这是本版本相对上游最重要的增强，覆盖位图字幕（PGS/SUP、DVB）和
> ASS/SSA/SRT 等文本字幕两条路径。

### 选项位置

属性页 → **更多** → **字幕输出** 分组：

```
┌─ 字幕输出 ────────────────────────────────────────┐
│  YCbCr 电平范围   [自动 ▾]                        │
│  YCbCr 矩阵       [自动 ▾]                        │
│  RGB 电平         [自动 ▾]                        │
│                                                   │
│  HDR 映射         [自动 ▾]                        │
│  白点（nits）     [ 203 ] ▲▼                      │
└───────────────────────────────────────────────────┘
```

布局或 Auto 结果异常时，点击“打开日志”查看 `%TEMP%\xy_hdr_diag.txt`；
日志只在播放器连接字幕滤镜时生成。

### 映射模式

<table>
<tr>
<th align="left">模式</th>
<th align="left">适用场景</th>
<th align="left">效果</th>
</tr>
<tr>
<td><b>自动</b> <sub>默认</sub></td>
<td>绝大多数情况</td>
<td>优先使用视频引脚的传递函数；必要时扫描 DirectShow 图、BT.2020 矩阵或文件名 HDR 标记，决定是否按 PQ 编码</td>
</tr>
<tr>
<td><b>关闭</b></td>
<td>SDR 的 BT.2020 片源被误判时</td>
<td>完全恢复旧行为，按 SDR 处理</td>
</tr>
<tr>
<td><b>PQ</b></td>
<td>渲染器按 PQ 解读字幕位图</td>
<td>把白点编码成绝对 PQ 信号</td>
</tr>
</table>

### 白点亮度

| 项目 | 值 |
| :--- | :--- |
| 默认值 | **203 nits**（BT.2408 图文参考白） |
| 可调范围 | 50 – 1000 nits |
| 作用 | 决定"字幕白"在 HDR 画面中的绝对亮度 |

> 💡 **调参建议**
>
> - 字幕在 HDR 画面里**发灰、不够醒目** → 把白点调高（如 300–400）。
> - 字幕**仍然刺眼**、压过画面高光 → 调低（如 150–180）。
> - 一般不需要动，203 nits 是标准推荐值。

---

#<a id="why"></a>

# 🔬 为什么 HDR 下字幕会过曝

<details open>
<summary><b>点击展开：一条 235 的像素如何变成 4650 nits</b></summary>

<br/>

PGS 调色板里的颜色是 **8bit YCbCr + 8bit alpha**，没有任何位深变体。字幕里的"白"通常是限幅范围的最大值：

```
Y = 235  →  写入 10bit 容器时左移 8 位  →  940
```

如果视频是 PQ（ST 2084）编码，940 会被当作一个**绝对亮度码值**来解读：

| 10bit 码 | 对应亮度 |
| :---: | ---: |
| 594 | 203 nits ← BT.2408 参考白 |
| 940 | ≈ **4650 nits** ← 字幕白实际落点 |
| 1023 | 10000 nits |

也就是说，原本"和画面白一样亮"的字幕，被强行拉到了显示设备几乎无法呈现的亮度。这就是过曝。

</details>

<details>
<summary><b>点击展开：还有一个隐藏问题——TV 范围二次压缩</b></summary>

<br/>

PQ 码值本身就是绝对量。如果字幕位图在 PQ 编码之后又被压进 TV 范围（16–235），白点会被整体再拉低约 9%。

因此本项目在 PQ 模式下**强制 ARGB 走全幅范围**，避免码值被二次缩放。详见 `docs/DECISIONS.md` 的 D9。

</details>

<br/>

<div align="center">

### 一句话总结修复原理

```
SDR 调色板码值  ──▶  线性光  ──▶  × 白点亮度  ──▶  PQ 编码  ──▶  HDR 码值
     Y=235            0.87        203 nits          N=0.58        码 148
                                                                     ↑
                                                        不再是 255（≈10000 nits）
```

</div>

---

#<a id="pipeline"></a>

# 🧬 渲染管线

### SDR 路径（改造前，作为基准）

```
.sup 文件
  │
  ▼
SupFileSubtitleProvider::ThreadProc        读取 "PG" 同步字、90kHz PTS
  │
  ▼
CHdmvSub::ParseSample                       PCS / PDS / ODS 分段解析
  │                                         → 8bit Y,Cr,Cb,T 调色板
  ▼
CHdmvSub::Render                            选矩阵（≥720 猜 709）与限幅（TV）
  │
  ▼
CompositionObject::InitColor                A8Y8U8V8_TO_ARGB → 预乘 ARGB
  │
  ▼
CompositionObject::RenderHdmv               PGS RLE 解码 → 逐行填充
  │
  ▼
XySubRenderFrameWrapper                     以 XY_CS_ARGB_F 交给消费端
  │
  ▼
XySubFilterConsumer::AlphaBlt               dst = dst·A + RGB
```

### HDR 路径（改造后）

<div align="center">

```
                        ┌─────────────────────────────┐
                        │   HdrColor::Config          │
                        │   mode + white_nits         │
                        └──────────────┬──────────────┘
                                       │
                                       ▼
┌──────────────┐    ┌──────────────────────────────┐    ┌─────────────────┐
│ 8bit 调色板   │───▶│ CompositionObject::InitColor │───▶│ 映射后的码值缓存  │
│ Y,Cr,Cb,T    │    │  MapArgb(argb, config)       │    │ m_Colors[256]   │
└──────────────┘    └──────────────────────────────┘    └────────┬────────┘
                                                                  │
                                              ┌───────────────────┘
                                              ▼
                                   ┌──────────────────────┐
                                   │ RLE 填充（纯查表）    │
                                   └──────────────────────┘
```

</div>

**关键设计**：映射发生在**调色板级别**——每种颜色只计算一次，最多 256 次；之后每帧每像素只做查表与整数混合，**热路径零额外开销**。

### 自动判定链路

```
XySubFilter::SetSubtitle
      │
      ├──▶ SetYuvMatrix()          选矩阵与范围
      ├──▶ SetRgbOutputLevel()     PQ 模式强制全幅 RGB
      └──▶ SetHdrMode()            ResolvePgsHdrConfig() → Provider::SetHdrConfig
                                          │
                                          ▼
                                  自动模式判定依据：
                                   video pin transfer function
                                   → graph scan / side data / matrix
                                   → filename HDR fallback → PQ or Off
```

> 📌 完整的调用栈、行号与数据结构见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)。

---

#<a id="build"></a>

# 🛠️ 从源码编译

### 前置要求

<table>
<tr><th align="left">依赖</th><th align="left">说明</th></tr>
<tr><td><b>Visual Studio</b></td><td>2019 / 2022，含 C++ 桌面开发工作负载</td></tr>
<tr><td><b>VSNASM</b></td><td><a href="https://github.com/ShiftMediaProject/VSNASM">ShiftMediaProject/VSNASM</a></td></tr>
<tr><td><b>VSYASM</b></td><td><a href="https://github.com/ShiftMediaProject/VSYASM">ShiftMediaProject/VSYASM</a></td></tr>
</table>

### 步骤

```powershell
# 1. 安装汇编工具链（管理员权限运行各自的 install_script.bat）
git clone https://github.com/ShiftMediaProject/VSNASM.git
git clone https://github.com/ShiftMediaProject/VSYASM.git

# 2. 拉取子模块（libass 及其依赖，共 9 个）
git submodule update --init --recursive --remote

# 3. 在 Visual Studio 中把 libass 及依赖的运行时库设为
#    Release → /MT    Debug → /MTd

# 4. 编译 xy_sub_filter 工程
```

> 🔁 **本项目只接受云端构建验证**：推送后由 GitHub Actions 编译并运行回归测试，产物在 Actions 的 artifact 中。

### 本地跑回归测试

```powershell
./scripts/Run-RegressionTests.ps1 -Configuration Release -Platform x64
```

---

## 📁 仓库结构

```text
XY-Subfilter/
├── src/
│   ├── subpic/
│   │   ├── hdr_color.{h,cpp}        ← 🆕 HDR 色彩科学（PQ / nits / 映射）
│   │   ├── color_conv_table.*        8bit 定点色彩转换
│   │   └── MemSubPic.cpp             字幕面混合（P010/P016）
│   ├── subtitles/
│   │   ├── CompositionObject.*       ← 调色板级 HDR 映射插入点
│   │   ├── HdmvSub.*                 PGS 解析与渲染
│   │   ├── DVBSub.*                  DVB 字幕
│   │   └── hdmv_subtitle_provider.*  SUP 文件 Provider
│   └── filters/transform/vsfilter/
│       ├── IDirectVobSubXy.h         选项 ID 与注册表
│       ├── VSFilter.rc               英文 + 简体中文资源
│       └── DirectVobSubPropPage.cpp  属性页
├── test/unit_test/
│   └── test_hdr_color.h             ← 🆕 HDR 单测
├── docs/                             项目五件套文档
└── .github/workflows/main.yml        Regression CI
```

---

#<a id="limits"></a>

# ⚠️ 已知限制

<table>
<tr><th align="left">限制</th><th align="left">说明</th></tr>
<tr>
<td><b>自动判定是启发式</b></td>
<td>协议本身没有传递函数字段，因此 Auto 仍采用启发式链：consumer 传递函数、整图扫描、BT.2020 矩阵和文件名回退。SDR 的 BT.2020 内容可能被误判，需手动切到「关闭」。</td>
</tr>
<tr>
<td><b>HLG 尚未单独验收</b></td>
<td>当前实现会把 PQ/HLG 都作为 HDR 触发 PQ 路径，但个人使用范围未包含 HLG，不能把 HLG 观感视为已验证。</td>
</tr>
<tr>
<td><b>8bit 调色板精度</b></td>
<td>格式本身限制。203 nits 附近每码约 2% 亮度步进，平色图形字幕可接受。</td>
</tr>
<tr>
<td><b>依赖消费端语义</b></td>
<td>madVR 等外部消费端如何解读字幕位图无法从代码判定；Relative white 仅保留内部 ABI 值，已从用户界面隐藏。</td>
</tr>
<tr>
<td><b>不做色调映射</b></td>
<td>只处理字幕白点，不参与视频画面的 HDR→SDR 映射——那是渲染器的职责。</td>
</tr>
</table>

---

## 📚 文档

| 文档 | 内容 |
| :--- | :--- |
| [`docs/PROJECT.md`](docs/PROJECT.md) | 目标、范围、验收标准 |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | SDR/HDR 管线与模块职责 |
| [`docs/DECISIONS.md`](docs/DECISIONS.md) | 技术决策及取舍 |
| [`docs/CONSTRAINTS.md`](docs/CONSTRAINTS.md) | 硬约束与已排除做法 |
| [`docs/TODO.md`](docs/TODO.md) | 进度、待验证项、下一步 |

---

## 📄 许可

本项目以 **GPL v2** 发布，详见 [License.txt](License.txt)。

---

<div align="center">

### 兼容性与反馈

本项目是带 libass 的 XySubFilter **变体**：

1. ASS/SSA 渲染行为主要取决于 libass，可能与原版 XySubFilter 存在细微差异。
2. 其余部分（SRT、SUP 等基础功能）基本保持上游原样，因此继承了上游的全部优缺点。
3. 遇到问题时，欢迎在 [Issues](https://github.com/Euphonium-feng/XY-Subfilter/issues) 反馈，并注明是否与 libass 相关。

<br/>

**[⬆ 回到顶部](#top)**

<sub>Built with ❤️ for people who care about subtitles looking right.</sub>

</div>
