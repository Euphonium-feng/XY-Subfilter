<div align="center">

<a id="top"></a>

# 🎬 XY-SubFilter

**An HDR-aware subtitle render filter for DirectShow players**

_libass ASS/SSA rendering · Palette-level HDR white-point mapping for PGS/SUP · Full Simplified Chinese UI_

<br/>

[![Regression CI](https://github.com/Euphonium-feng/XY-Subfilter/actions/workflows/main.yml/badge.svg)](https://github.com/Euphonium-feng/XY-Subfilter/actions/workflows/main.yml)
[![License](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](License.txt)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20DirectShow-0078D6?logo=windows)](https://learn.microsoft.com/windows/win32/directshow/directshow)
[![C++](https://img.shields.io/badge/C%2B%2B-ATL%20%2F%20MFC-00599C?logo=cplusplus)](https://isocpp.org/)
[![libass](https://img.shields.io/badge/Renderer-libass-FF6F00)](https://github.com/libass/libass)
[![HDR](https://img.shields.io/badge/HDR-BT.2408%20%C2%B7%20203%20nits-8A2BE2)](#hdr)

<br/>

[简体中文](README.md) · **English**

</div>

---

<div align="center">

### ✨ The problem in one sentence

> On HDR content, an 8-bit bitmap subtitle palette gets interpreted as absolute HDR code values,
> pushing "white" to **thousands of nits** — glaring subtitles that overpower the picture.
>
> This project adds **BT.2408 reference-white mapping** to the rendering pipeline,
> bringing subtitle white back down to **203 nits**.

</div>

---

#<a id="what"></a>

# 💡 What is this

**XY-SubFilter** is a DirectShow subtitle render filter for **MPC-HC / MPC-BE**, **PotPlayer + madVR**, and similar players.

It renders ASS/SSA subtitles with **libass** while keeping the full bitmap subtitle path intact:

<table>
<tr>
<th align="left">🎞️ Text subtitles</th>
<th align="left">🖼️ Bitmap subtitles</th>
</tr>
<tr>
<td valign="top">

- ASS / SSA (libass)
- SRT / TXT / USF / SAMI
- MicroDVD / RealText / SubViewer

</td>
<td valign="top">

- **PGS / SUP** (Blu-ray)
- VobSub (.idx / .sub)
- DVB subtitles

</td>
</tr>
</table>

> **Origin** — derived from [Cyberbeing/xy-VSFilter#16](https://github.com/Cyberbeing/xy-VSFilter/pull/16),
> i.e. XySubFilter with libass.

---

#<a id="install"></a>

# 🚀 Installation

### Option 1 · Installer script (recommended)

```bat
:: run as administrator
Install_XySubFilter.bat
```

> ⚠️ Do **not** move or delete `XySubFilter.dll` afterwards, or the filter registration breaks.

### Option 2 · Scoop

```powershell
scoop bucket add nonportable
scoop install xysubfilter-libass-np -g
```

---

#<a id="usage"></a>

# 🎛️ Usage

Select **XY-SubFilter** as the subtitle renderer:

<table>
<tr>
<th align="left">Player</th>
<th align="left">Path</th>
</tr>
<tr><td>🎬 <b>MPC-BE</b></td><td>Options → Subtitles → Subtitle renderer</td></tr>
<tr><td>🎬 <b>MPC-HC</b></td><td>Options → Playback → Output → Subtitle Renderer</td></tr>
<tr><td>🎬 <b>PotPlayer</b></td><td>Set it as the external DirectShow subtitle filter</td></tr>
</table>

### 🌐 UI language

The **"More"** property page offers three settings:

| Option | Behaviour |
| :--- | :--- |
| `Auto` (default) | Follow the Windows UI language |
| `English` | Force the English UI |
| `中文（简体）` | Force the Simplified Chinese UI |

---

#<a id="hdr"></a>

# 🖼️ HDR white-point mapping

> The most significant change from upstream. Everything here applies to bitmap subtitles (PGS/SUP, DVB).

### Where to find it

Property page → **More** → **Subtitle Output** group.

### The four modes

<table>
<tr>
<th align="left">Mode</th>
<th align="left">When to use</th>
<th align="left">Effect</th>
</tr>
<tr>
<td><b>Auto</b> <sub>default</sub></td>
<td>Most cases</td>
<td>Encodes as PQ when the consumer's <code>yuvMatrix</code> ends in <code>2020</code>; otherwise keeps SDR behaviour</td>
</tr>
<tr>
<td><b>Off</b></td>
<td>When SDR BT.2020 content is misdetected</td>
<td>Restores the legacy SDR path exactly</td>
</tr>
<tr>
<td><b>PQ</b></td>
<td>Renderer treats subtitle bitmaps as PQ</td>
<td>Encodes the white point as an absolute PQ signal</td>
</tr>
<tr>
<td><b>Relative white</b></td>
<td>Renderer treats subtitle bitmaps as SDR</td>
<td>Keeps the SDR code space, only compensates luminance</td>
</tr>
</table>

### White point

| Item | Value |
| :--- | :--- |
| Default | **203 nits** (BT.2408 graphics white) |
| Range | 50 – 1000 nits |
| Meaning | Absolute luminance of subtitle white within the HDR image |

> 💡 **Tuning hints**
>
> - Subtitles look **dim and washed out** → raise the white point (e.g. 300–400).
> - Subtitles are **still glaring** and overpower highlights → lower it (e.g. 150–180).
> - Usually you don't need to touch it; 203 nits is the standard recommendation.

---

#<a id="why"></a>

# 🔬 Why subtitles overexpose in HDR

<details open>
<summary><b>Expand: how a code value of 235 becomes 4650 nits</b></summary>

<br/>

PGS palette entries are **8-bit YCbCr + 8-bit alpha**, with no higher-bit-depth variant. Subtitle "white" is typically the limited-range maximum:

```
Y = 235  →  shifted left by 8 into a 10-bit container  →  940
```

If the video is PQ (ST 2084) encoded, 940 is interpreted as an **absolute luminance code**:

| 10-bit code | Luminance |
| :---: | ---: |
| 594 | 203 nits ← BT.2408 reference white |
| 940 | ≈ **4650 nits** ← where subtitle white actually lands |
| 1023 | 10000 nits |

Subtitles that should simply match the picture's white get pushed to a brightness the display can barely reach. That is the overexposure.

</details>

<details>
<summary><b>Expand: the hidden second problem — double TV-range compression</b></summary>

<br/>

PQ code values are absolute. If the subtitle bitmap is subsequently squeezed into TV range (16–235), the white point is pulled down another ~9%.

This project therefore **forces full-range ARGB in PQ mode**. See D9 in `docs/DECISIONS.md`.

</details>

<br/>

<div align="center">

### The fix in one line

```
SDR palette code  ──▶  linear light  ──▶  × white point  ──▶  PQ encode  ──▶  HDR code
     Y=235              0.87                 203 nits          N=0.58         148
                                                                            ↑
                                                              no longer 255 (≈10000 nits)
```

</div>

---

#<a id="pipeline"></a>

# 🧬 Rendering pipeline

### SDR path (unchanged baseline)

```
.sup file
  │
  ▼
SupFileSubtitleProvider::ThreadProc        reads "PG" sync word, 90 kHz PTS
  │
  ▼
CHdmvSub::ParseSample                      PCS / PDS / ODS segments
  │                                        → 8-bit Y,Cr,Cb,T palette
  ▼
CHdmvSub::Render                           matrix (≥720 → 709) and range (TV)
  │
  ▼
CompositionObject::InitColor               A8Y8U8V8_TO_ARGB → premultiplied ARGB
  │
  ▼
CompositionObject::RenderHdmv              PGS RLE decode → run filling
  │
  ▼
XySubRenderFrameWrapper                    handed over as XY_CS_ARGB_F
  │
  ▼
XySubFilterConsumer::AlphaBlt              dst = dst·A + RGB
```

### HDR path (this fork)

<div align="center">

```
                        ┌─────────────────────────────┐
                        │   HdrColor::Config          │
                        │   mode + white_nits         │
                        └──────────────┬──────────────┘
                                       │
                                       ▼
┌──────────────┐    ┌──────────────────────────────┐    ┌─────────────────┐
│ 8-bit palette │───▶│ CompositionObject::InitColor │───▶│ mapped codes    │
│ Y,Cr,Cb,T     │    │  MapArgb(argb, config)       │    │ m_Colors[256]   │
└──────────────┘    └──────────────────────────────┘    └────────┬────────┘
                                                                  │
                                              ┌───────────────────┘
                                              ▼
                                   ┌──────────────────────┐
                                   │ RLE fill (LUT only)  │
                                   └──────────────────────┘
```

</div>

**Key design**: mapping happens at the **palette level** — each colour is computed once, at most 256 times; every subsequent pixel only does a table lookup and integer blend, so the **hot path costs nothing extra**.

> 📌 Full call stacks, line numbers and data structures: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

---

#<a id="build"></a>

# 🛠️ Building from source

### Prerequisites

<table>
<tr><th align="left">Dependency</th><th align="left">Notes</th></tr>
<tr><td><b>Visual Studio</b></td><td>2019 / 2022 with the C++ desktop workload</td></tr>
<tr><td><b>VSNASM</b></td><td><a href="https://github.com/ShiftMediaProject/VSNASM">ShiftMediaProject/VSNASM</a></td></tr>
<tr><td><b>VSYASM</b></td><td><a href="https://github.com/ShiftMediaProject/VSYASM">ShiftMediaProject/VSYASM</a></td></tr>
</table>

### Steps

```powershell
# 1. install the assembler toolchains (run each install_script.bat as administrator)
git clone https://github.com/ShiftMediaProject/VSNASM.git
git clone https://github.com/ShiftMediaProject/VSYASM.git

# 2. fetch submodules (libass and its 9 dependencies)
git submodule update --init --recursive --remote

# 3. in Visual Studio set libass and its dependencies' runtime library to
#    /MT for Release and /MTd for Debug

# 4. build the xy_sub_filter project
```

> 🔁 **This project only accepts cloud builds as verification**: pushing runs the GitHub Actions workflow, which compiles and runs regression tests; artifacts are attached to the run.

### Running regression tests locally

```powershell
./scripts/Run-RegressionTests.ps1 -Configuration Release -Platform x64
```

---

#<a id="limits"></a>

# ⚠️ Known limitations

<table>
<tr><th align="left">Limitation</th><th align="left">Details</th></tr>
<tr>
<td><b>Auto-detection is a heuristic</b></td>
<td>The render interface has no transfer-function field, so only <code>yuvMatrix</code> is available. SDR BT.2020 content reads as HDR; switch to "Off" manually.</td>
</tr>
<tr>
<td><b>Bitmap subtitles only</b></td>
<td>ASS/SRT text subtitles are not mapped. The interface is reserved for a later extension.</td>
</tr>
<tr>
<td><b>8-bit palette precision</b></td>
<td>A format limitation. Around 203 nits each code step is roughly 2%, acceptable for flat graphics.</td>
</tr>
<tr>
<td><b>Depends on consumer semantics</b></td>
<td>How madVR and other consumers interpret subtitle bitmaps cannot be determined from code, hence the "Relative white" fallback.</td>
</tr>
<tr>
<td><b>No tone mapping</b></td>
<td>Only the subtitle white point is handled; video HDR→SDR mapping is the renderer's job.</td>
</tr>
</table>

---

## 📚 Documentation

| Document | Contents |
| :--- | :--- |
| [`docs/PROJECT.md`](docs/PROJECT.md) | Goals, scope, acceptance criteria |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | SDR/HDR pipelines and module responsibilities |
| [`docs/DECISIONS.md`](docs/DECISIONS.md) | 14 technical decisions and trade-offs |
| [`docs/CONSTRAINTS.md`](docs/CONSTRAINTS.md) | Hard constraints and ruled-out approaches |
| [`docs/TODO.md`](docs/TODO.md) | Progress, unverified items, next steps |

---

## 📄 License

Released under **GPL v2**. See [License.txt](License.txt).

---

<div align="center">

### Compatibility & feedback

This project is a **variant** of XySubFilter with libass:

1. SSA/ASS rendering behaviour depends largely on libass and may differ slightly from the original XySubFilter.
2. Everything else (SRT, SUP and the other basic parts) is essentially unchanged, so it inherits all the strengths and weaknesses of the upstream project.
3. Issues are welcome at [Issues](https://github.com/Euphonium-feng/XY-Subfilter/issues) — please mention whether the problem is libass-related.

<br/>

**[⬆ Back to top](#top)**

</div>
