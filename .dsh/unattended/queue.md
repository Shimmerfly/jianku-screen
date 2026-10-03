# 简库镜传｜无人值守推进队列

唯一真源。每轮只取「待办」的首项，做完勾掉并追一行结论。结论必须写清「改了什么 / 怎么验证 / 看到什么」。

- 项目根：`/Users/moyingxz/Documents/Zzx/05_项目开发/Jianku Screen`
- 验证命令：`cd build && cmake --build . -j8 && ctest --output-on-failure`
- 运行日志：`.dsh/unattended/`
- 上次更新：2026-10-04 03:30

## 授权边界（本次无人值守）

可以：读改本地代码、跑构建与测试、本地 git 提交、写文档、写运行日志、读 `~/Movies` 下的录制工程。
不做（等用户回来决定）：`git push`、改 CI、装系统级软件、**修改或删除用户的录制素材**。
额外约束：本仓库为**私有本地仓库**，不做任何远端配置。

## 现状（2026-10-04 03:30）

- 构建：`cmake --build build -j8` 通过，warning 归零。
- 测试：`ctest` **10/10** 通过（timeline-boundaries、spring-solver、cursor-engine、
  auto-focus、media-clock、canvas-layout、capture-source、motion-blur、compositor、
  export-controller）。
- 版本历史：基线 `85af2cd`；此后 10 个提交，均在本地。
- 权限：approval=never / danger-full-access，无人值守期间无审批阻塞。
- 工作区无其他执行体（Trae 停在 23:38，报的是模型侧 4054，不是代码错误）。

## 已完成

| # | 项 | 提交 | 验证 |
|---|---|---|---|
| Q1 | 暂停后停止产出损坏 MP4（无 moov atom） | `bee294b` | 用坏文件的真实时间戳做夹具，`media-clock` 单测把旧表达式钉成负向对照 |
| Q2 | `cursorSmoothing` / `clickEffect` 写了没人读 | `c1fad97` | `cursor-engine` 补设置映射断言；实测 None 预设从「仍然平滑」变为「吸附原始位置」 |
| Q3 | 画布布局三处各算各的 | `f3a7de4` | 新增 `canvas-layout` 单测（contain/padding/inset/退化输入/比例换算） |
| Q4 | 设置映射会被预览与合成器解释成两样 | `f3a7de4` | 抽出 `driverSettingsFromMap()`，两个读取方共用 |
| Q5 | 录制成片没有指针、没有镜头、没有画布包装 | `f7c9901` | 新增 `jianku-compose`，真实 124.66s 工程合成出 h264+aac；`--no-cursor` 差分定位到指针像素 |
| Q6 | 合成器长片导出被内核 OOM 杀掉 | `8b86a63` | 加写缓冲水位做背压；RSS 从 82GB 降到稳定 284MB |
| Q7 | `project.json` 的 `error` 字段语义混乱 | `8b86a63` | 成功写空串、失败写原因 |
| Q8 | 成片被 `-shortest` 静默裁短仍报成功 | `6eab3e4` | 加 ffprobe 回读校验；全片 7480 写入 / 7480 回读，完整解码无错 |
| Q9 | 合成器只有命令行入口 | `e6b80bc` | 新增 `export-controller-tests` 端到端跑真实管线；应用内可导出并可取消 |
| Q10 | 麦克风轨未并入成片 | `c082ccc` `a18c87c` | 互相关确认对齐偏移 13.2 ms、相关系数 0.994；顺带修掉 `"microphone": true` 的类型转换 bug |
| Q11 | 只有整屏来源 | `06aefdf` | 新增 `capture-source` 单测；实测窗口阴影会内缩内容 64px（必须关闭），区域映射无系统性偏移（用噪声基线对照证明） |
| Q12 | 动态模糊被整体回退 | `f1c9ac2` | 新增 `motion-blur` 单测；真实工程全片扫描 + 缩放过渡抽帧 |

## 待办（按顺序取首项；已完成项就地保留结论）

### Q8 合成器不校验成片帧数 ✅
结论：`-shortest` 会把已经合成好的视频裁到音轨长度。系统声音比视频早停
（124.533s vs 124.677s），7480 帧的合成结果实际只写出 7470 帧，程序仍报成功。
已去掉 `-shortest`，并新增回读校验：ffprobe `-count_packets` 数成片里真实视频包数，
与写入帧数不符就把整次合成标为失败。
验证：`--frames 300` 写入 300 / 回读 300；**全片 7480 帧写入 / 回读 7480**，
`ffmpeg -f null -` 完整解码一遍无错误，时长 124.667s，体积 37.8MB。
产物副本：`build/composed-demo-22-46-50-891.mp4`（build/ 已 gitignore，不入库）。

### Q9 合成器只有命令行入口，没接进 UI ✅
结论：新增 `src/render/ExportController.{h,cpp}`，把同步的 `composeProject()` 放到工作
线程，进度经队列信号回 QML；主工具栏加「导出成片」按钮 + 预览区底部进度条，
完成后自动在 Finder 定位产物。`ComposeOptions` 增加 `shouldCancel` 轮询
（取消不是失败，UI 不报错）。
顺带修掉一个真实环境问题：Finder 启动的应用只继承 `/usr/bin:/bin:/usr/sbin:/sbin`，
Homebrew 的 ffmpeg 按名字根本找不到；ffmpeg/ffprobe 定位抽成
`findFfmpeg()` / `findFfprobe()`。
验证：新增 `export-controller-tests` 端到端跑真实管线（ffmpeg 现造 30 帧源视频 →
控制器导出 → 回读成片确认 60 个输出帧）；空工程目录被拒绝而不是假装导出。
应用启动无 QML 报错。提交 `e6b80bc`。

### Q10 麦克风轨 `microphone.m4a` 未并入成片 ✅
结论：合成时按测得的偏移把麦克风混入。录制端在真正开始录音后打 host 时间戳写进
`project.json`；导出端用它与视频首帧的差作为常量偏移（两边折叠同样的暂停区间，
所以偏移全程不变）。麦克风晚于首帧用 `adelay` 推后，早于首帧用 `-ss` 裁掉开头。
`amix` 用 `normalize=0` 保持录制电平。只有系统声音时仍走 `-c:a copy` 不重编码。
**顺带查出一个一直存在的 bug**：`project.json` 里四个工程的 `microphone` 字段全是
`true`——`impl_->projectManifest.insert("microphone", [micRecorder metadata])` 把
NSDictionary 交给了 QJsonValue，重载解析落到指针转 bool。已加 `jsonFromDictionary()`
显式转换，加载端同时兼容新旧两种格式。
验证：真实工程成片音轨与源 `microphone.m4a` 互相关——最佳偏移 13.2 ms、
相关系数 0.994；`volumedetect` 确认混音后电平上升（-91.0 dB → -66.1 dB）。
提交 `c082ccc`、`a18c87c`。

### Q11 窗口与区域录制来源 ✅
结论：三种来源走同一条管线，只有 filter 与裁切不同。
- `src/capture/CaptureSource.{h,cpp}`：来源描述 + 几何解析，纯函数可单测。
  三种来源都归结为「全局点空间里的一个矩形，以已知像素尺寸录制」，指针记录器 /
  动画引擎 / 合成器都不分来源类型。像素尺寸取偶（H.264 4:2:0 不能编码奇数宽高）。
  区域按显示器裁切并如实记录裁切后矩形；窗口不裁切（SCK 交出整个窗口）。
- `PointerEventRecorder` 不再自己从 displayId 推包围盒，改由调用方传入实际录制矩形。
  否则窗口/区域录制里每个指针事件都会按整屏换算，位置全错。
- `MacCapture::startSource()` 统一入口；窗口用 `initWithDesktopIndependentWindow`
  （遮挡时仍产出自己的像素）。UI 加 屏幕/窗口/区域 选择与 `ui/RegionSelector.qml`
  （覆盖所有屏的并集，跨屏可拖）。

**两个实测出来的坑（都写了独立探针直接问 SCK，不是推断）：**
1. **窗口阴影必须在流配置里关掉。** 含阴影时帧尺寸仍是 `window.frame × scale`，
   但内容从 (64,49) px 才开始不透明（外圈 alpha 全 0）；关掉后从 (0,0) 铺满。
   不关的话「记录的矩形」和「帧里的内容」差一圈，指针事件全偏。
2. **区域映射没有系统性偏移。** 互相关一开始测出 ~6px 位移，看着像原点算错；
   做了噪声基线对照（两次全屏截图互比，真值 0,0，同一搜索也报 +5~+8px）后确认：
   那 6px 是桌面内容在两次采集之间自己变了。区域标称位相符率与噪声基线持平。

验证：`ctest` 9/9，新增 `capture-source`（几何/裁切/取偶/序列化往返/坐标映射，
含「用整屏包围盒换算会算错」的负向对照）；`compositor-tests` 增加 region 来源
manifest 的加载断言。应用启动无 QML 报错。提交 `06aefdf`。
证据：`.dsh/unattended/evidence/window-capture.png`。

### Q12 动态模糊 ✅（观感待用户验收）
结论：这次做成**离线逐帧纯函数**，不是上次那套实时 GLSL。逐条对着 3.7.5 静态
定位的渲染链写：通道二选一（尺寸变化严格大于中心位移才选 zoom）、选定变化
<1 px 不加滤镜、选中通道强度为 0 不退回另一通道、强度乘 fps/60、move kernel 21
单侧分布 `速度×i/20`、zoom kernel 13 权重 `4(p−p²)` + 稳定伪随机、父层运动扣除与
分轴反号清零。累加走浮点，避免弱样本舍入造成的暗边。

**两个实测出来的坑：**
1. **位移上限是必须的。** 静态笔记的两个阈值（1 与 0.01）差一百倍，说明强度乘子
   的单位没定死。按字面把速度当一帧位移用，真实录制 122.2s 处（相机快速回位，
   帧间位移 >200 px）整个画面糊成半透明、背景从画面中间透出来。加了显式上限
   `kMaximumSmearRatio`（对角线 0.3% ≈ 12 px）。**这是选择而非发现**，注释与这里
   都写明，等有参考输出帧对照再校准。
2. 打开模糊后 3360×2100 全片从 20.9 帧/秒掉到 1.5 帧/秒，并行化两个 pass 后
   仍属离线能力；实时预览要用它得另做优化，本次**不承诺**实时。

设置默认值补齐四个 motionBlur 键，设置页新增「动态模糊」面板（明细标注观感未对照），
CLI 加 `--motion-blur[-cursor|-move|-zoom]`。
验证：`ctest` 10/10，新增 `motion-blur`；真实工程全片扫描 move 209 / zoom 378 帧，
缩放过渡处 14–24% 像素变化。提交 `f1c9ac2`。
证据：`.dsh/unattended/evidence/motion-blur-zoom-{on,off}.png`。

**待用户验收**：这个上限值是我选的，不是量出来的。如果拖影看起来太弱/太强，
改 `src/render/MotionBlur.h` 里的 `kMaximumSmearRatio` 一处即可。

## 待用户验证（无人值守期间做不了）

按重要性排序。前两项直接影响能不能继续往下做。

1. **真机复录一次**（开始录制 → 暂停 → 继续 → 结束，屏幕/窗口/区域各一次）。
   自动验证做不到：`~/Movies` 下的素材按授权边界是只读的，而且需要真实的
   屏幕录制与输入监控授权。要看的是产出可播放 MP4、
   `project.json` 的 `state=readyForProcessing` 且 `error` 为空、
   窗口与区域来源的 `source.regionPoints` 与实际录制范围一致。
2. **观感验收**：成片里指针平滑度、点击缩放、相机跟随、**动态模糊强度**。
   证据帧在 `.dsh/unattended/evidence/`。动态模糊的位移上限
   （`src/render/MotionBlur.h` 的 `kMaximumSmearRatio`）是我选的不是量出来的，
   这是最可能需要调的一个数，改动只在一处。
3. **麦克风混音策略**：现在系统声音与麦克风等权相加（`normalize=0`）。
   是否该给麦克风更高权重、要不要提供两条独立音量，需要产品判断。
4. **编辑器与时间线**的形态：这是剩下最大的一块，动手前值得先定范围。

## 明确未做

- 编辑器与时间线 UI、裁剪/切分/变速、撤销重做。
- 摄像头、字幕、快捷键显示、点击音效、背景音乐。
- 演示模式的实时合成输出（当前只对录制工程离线合成）。
- Windows 侧捕获适配器。
- 动态模糊的**实时**预览（离线已实现；实时需要另做优化，见 Q12）。
