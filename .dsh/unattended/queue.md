# 简库镜传｜无人值守推进队列

唯一真源。每轮只取「待办」的首项，做完勾掉并追一行结论。结论必须写清「改了什么 / 怎么验证 / 看到什么」。

- 项目根：`/Users/moyingxz/Documents/Zzx/05_项目开发/Jianku Screen`
- 验证命令：`cd build && cmake --build . -j8 && ctest --output-on-failure`
- 运行日志：`.dsh/unattended/`
- 上次更新：2026-10-04 00:12

## 授权边界（本次无人值守）

可以：读改本地代码、跑构建与测试、本地 git 提交、写文档、写运行日志。
不做（等用户回来决定）：`git push`、改 CI、装系统级软件、动 `~/Movies` 下的用户录制素材（只读检查可以）。
额外约束：本仓库为**私有本地仓库**，不做任何远端配置。

## 现状（开工基线，2026-10-04 00:10）

- 构建：`cmake --build build -j8` 通过（2 条 `NSMutableDictionary` 指针类型 warning）。
- 测试：`ctest` 4/4 通过（timeline-boundaries、spring-solver、cursor-engine、auto-focus）。
- 版本历史：原本没有 git 仓库，已建立基线提交 `85af2cd`。
- 权限：本会话 approval=never / 文件策略 danger-full-access，无人值守期间无审批阻塞。
- 工作区里没有其他执行体（Trae 停在 23:38，报的是模型侧 4054，不是代码错误）。

## 已确认的关键缺陷（基线排查所得，尚未修）

- **D1 暂停后停止会产出损坏 MP4**：`~/Movies/.../23-27-15-049.jianku/raw.mp4` 是 28MB 的
  无 moov atom 废文件，`project.json` 的 `error` 为 "The operation could not be completed"。
- **D2 相机弹簧参数没有透传**：`AnimationDriver::setSettings` 里对已运行的弹簧调 `setConfig`，
  运行时改 `screenMovementSpring` 不生效。
- **D3 录制成片完全没有产品价值点**：`automatic-zooms.json` / `pointer-timeline.jsonl` 无消费方，
  指针、相机、画布包装都进不了 MP4；也没有任何导出路径。
- **D4 `project.json` 的 `error` 字段在成功路径也可能残留**（含英文系统串）。

## 队列

| # | 项 | 状态 |
|---|---|---|
| Q1 | 修复暂停后停止产出损坏 MP4（D1） | 待办 |
| Q2 | 相机弹簧参数透传（D2） | 待办 |
| Q3 | 实现离线合成器：指针 + 相机 + 画布包装进成片（D3） | 待办 |
| Q4 | 工程 `error` 字段语义（D4） | 待办 |
| Q5 | 合成器输出比例与自动缩放倍率 | 待办 |

### Q1 修复暂停后停止产出损坏 MP4

判据：`stop()` 在 finalize 期间不再打断 stream；构建 + ctest 全绿；用已有工程数据能完整读回。

### Q2 相机弹簧参数透传

判据：改 `screenMovementSpring` 后新值真正作用到相机弹簧上；构建 + ctest 全绿。

### Q3 实现离线合成器

判据：新增可执行文件，吃一个已录制工程目录，输出带指针与相机变换的 MP4；
真实工程跑通并 ffprobe 出正确的轨道/时长/尺寸。

### Q4 工程 `error` 字段语义

判据：成功路径 `error` 为空，只有失败才写。

### Q5 合成器输出比例与自动缩放倍率

判据：画布比例按设置的 `outputAspectRatio` 生效，缩放倍率取自工程数据。

## 待用户验证（无人值守期间做不了）

1. 真机复录一次（开始录制 → 暂停 → 继续 → 结束），确认新收尾时序产出可播放 MP4。
2. 观感验收：合成器输出的指针平滑、点击缩放、相机跟随是否符合预期。
3. 决定合成器是否接回 UI（当前计划：命令行，未接菜单）。

## 明确未做（留给后续轮次）

- 编辑器与时间线 UI、裁剪/切分/变速、撤销重做。
- 窗口与区域录制来源。
- 动态模糊（此前实测闪烁花屏已回退，重做需先解决多采样合成层稳定性）。
- 音画同步的实机核对（麦克风轨仍未与成片合并）。
