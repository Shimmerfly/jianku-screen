# 简库镜传｜无人值守推进队列

唯一真源。每轮只取「待办」的首项，做完勾掉并追一行结论。结论必须写清「改了什么 / 怎么验证 / 看到什么」。

- 项目根：`/Users/moyingxz/Documents/Zzx/05_项目开发/Jianku Screen`
- 验证命令：`cd build && cmake --build . -j8 && ctest --output-on-failure`
- 运行日志：`.dsh/unattended/`
- 上次更新：2026-10-04 01:20

## 授权边界（本次无人值守）

可以：读改本地代码、跑构建与测试、本地 git 提交、写文档、写运行日志、读 `~/Movies` 下的录制工程。
不做（等用户回来决定）：`git push`、改 CI、装系统级软件、**修改或删除用户的录制素材**。
额外约束：本仓库为**私有本地仓库**，不做任何远端配置。

## 现状（2026-10-04 01:20）

- 构建：`cmake --build build -j8` 通过，warning 归零。
- 测试：`ctest` **7/7** 通过（timeline-boundaries、spring-solver、cursor-engine、
  auto-focus、media-clock、canvas-layout、compositor）。
- 版本历史：基线 `85af2cd`；此后 5 个提交，均在本地。
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

## 待办（按顺序取首项）

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

### Q11 窗口与区域录制来源
当前只有整屏。判据：`SCContentFilter` 支持窗口/区域，工程记录来源类型与裁切矩形。

### Q12 动态模糊重新实现
此前实测放大闪烁、平移花屏，已整体回退。重做前提是先解决多采样合成层稳定性，
且必须与参考逐帧对照（`research/画布与运动模糊.md`）。

## 本轮（2026-10-04 01:00–01:30）

Q8 已完成，结论与验证写在上面「待办」区的 Q8 条目里（就地勾选，避免同一件事两处记账）。

## 待用户验证（无人值守期间做不了）

1. **真机复录一次**（开始录制 → 暂停 → 继续 → 结束），确认新的收尾时序产出可播放 MP4，
   `project.json` 的 `state=readyForProcessing`、`error` 为空。
2. **观感验收**：合成的成片里指针平滑度、点击缩放、相机跟随是否符合预期。
   证据帧在 `.dsh/unattended/evidence/`。
3. 决定合成器是否接回 UI（现在是命令行 `jianku-compose <工程目录>`）。
4. 决定系统声音与麦克风的混音策略（当前只封装系统声音）。

## 明确未做

- 编辑器与时间线 UI、裁剪/切分/变速、撤销重做。
- 窗口与区域录制来源。
- 动态模糊（重做前提见 Q12）。
- 摄像头、字幕、快捷键显示等后续能力账本项。
