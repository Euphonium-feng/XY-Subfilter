## 这是什么

这是一个 DirectShow 字幕渲染滤镜，供 MPC-HC / MPC-BE、PotPlayer + madVR 等播放器使用，以 libass 渲染 ASS 与 SSA 格式字幕。项目源自 [Cyberbeing/xy-VSFilter#16](https://github.com/Cyberbeing/xy-VSFilter/pull/16)，即带 libass 的 XySubFilter 变体。

## 使用方法

1. 以管理员身份运行 `Install_XySubFilter.bat`，之后不要移动或删除 `XySubFilter.dll`。
   - 也可以通过 Scoop 安装 [XySubFilter](https://scoop.sh/#/apps?q=xysubfilter-libass-np&s=0&d=1&o=true)：
     ```
     scoop bucket add nonportable
     scoop install xysubfilter-libass-np -g
     ```
2. 在播放器中把 XySubFilter 选为字幕渲染器。
   - MPC-BE：选项 → 字幕 → 字幕渲染器
   - MPC-HC：选项 → 播放 → 输出 → 字幕渲染器

## 如何编译

1. 从 https://github.com/ShiftMediaProject/VSYASM 与 https://github.com/ShiftMediaProject/VSNASM 克隆或下载发布包，以管理员身份运行 `install_script.bat`。
2. 克隆本仓库，并在目录中执行 `git submodule update --init --recursive --remote`。
3. 在 Visual Studio 中手动把 libass 及其依赖的运行时库设为 Release 用 `/MT`、Debug 用 `/MTd`，详见 [这里](https://docs.microsoft.com/en-us/cpp/build/reference/md-mt-ld-use-run-time-library?view=msvc-170)。
4. 编译 `xy_sub_filter` 工程。

本项目按云端构建验证：推送后由 GitHub Actions（`.github/workflows/main.yml`）编译并运行回归测试，产物在 Actions 的 artifact 中。

## HDR 字幕白点映射

HDR 片源下，位图字幕（PGS/SUP、DVB）的调色板是 8bit SDR 色彩。若直接沿用 SDR 处理，字幕白会被当成 HDR 容器的绝对码值解读，实际亮度可达数千 nits，表现为明显过曝。

本版本内置了按 BT.2408 参考白的映射：

| 选项 | 说明 |
| --- | --- |
| HDR 映射 = 自动（默认） | 依据消费者上报的 `yuvMatrix` 判断；以 2020 结尾时按 PQ 编码 |
| HDR 映射 = 关闭 | 保持旧行为，按 SDR 处理 |
| HDR 映射 = PQ | 把白点编码为绝对 PQ 信号，适合按 PQ 解读字幕位图的渲染器 |
| HDR 映射 = 相对白 | 保持 SDR 码值空间，只在亮度上补偿到更高白点 |
| 白点（nits） | 参考白亮度，默认 203，可调范围 50–1000 |

自动判定只依据 `yuvMatrix`，属于启发式：SDR 的 BT.2020 内容也会被判定为 HDR。遇到这种情况把 HDR 映射手动设为「关闭」即可。

## 说明

本项目是带 libass 的 XySubFilter **变体**，因此：

1. 在 SSA/ASS 渲染上可能与 XySubFilter 存在兼容性差异，这主要取决于 libass（我们欢迎反馈问题，如果你不确定来源是 libass 还是本项目）。
2. 项目其余部分（srt、sup 等基础功能）基本保持原样，因此继承了原项目的全部优缺点。
3. 界面支持英文与简体中文，可在属性页「更多」中切换，默认跟随系统语言。
