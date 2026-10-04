# AVAssetWriter `-11800` / `-16341`：调查记录

日期：2026-10-04｜状态：**根因已定位到一层，触发条件未完全确定；已做的是让错误可诊断**

## 现象

一次真实录制（2026-10-04 06:51:59 那次，`/tmp/jianku-realtest`）在第 70.8 秒中断：

```
state: failed
error: The operation could not be completed（AVFoundationErrorDomain -11800）
```

`raw.mp4` 有 35 MB、3856 帧（中位 60.0 fps，帧间隔单调无异常），
顶层是分片结构（67 个 `moof`、1 个 `moov`），`ffprobe` 能读出 70.03 秒、3846 个包，
完整解码无报错。**文件本身没坏，是写入在某一刻停住了。**

同一时刻 `pointer-events.jsonl`、`cursor-observations.jsonl`、`microphone.m4a`
都还在正常增长——它们不经过 AVAssetWriter。所以问题在 writer 这一侧，不在时钟、
不在采集、不在磁盘。

## 关键：`-11800` 不是原因，是包装

`errorText()` 只取了 `localizedDescription` + `domain` + `code`，
而 `-11800` 的文案是「The operation could not be completed」——**什么都没说**。
真正的码在 `NSError.userInfo[NSUnderlyingErrorKey]` 里。

写了一个最小复现程序（`/tmp/sctest/main.mm`，SCStream → AVAssetWriter，
只有一个视频轨、无音频、无分片之外的额外设置），第一次跑就复现了，
并且把底层错误打了出来：

```
!!! append 在第 116 帧失败
writer.status = 3                      ← AVAssetWriterStatusFailed
domain=AVFoundationErrorDomain code=-11800 desc=The operation could not be completed
  domain=NSOSStatusErrorDomain code=-16341 desc=The operation couldn’t be completed. (OSStatus error -16341.)
reason=An unknown error occurred (-16341)
input 状态: ready=1                    ← 输入流本身是「就绪」的，是 writer 整体失效
```

**底层码是 `NSOSStatusErrorDomain -16341`。** 这个码在 SDK 头文件里搜不到，
但它把「无法完成的操作」变成了一个可检索、可区分的具体失败——
`-11800` 有很多种成因，`-16341` 是其中一种。

## 已排除的因素

用参数化探针（`/tmp/scmatrix/main3.mm`）逐项排除，每项跑 3 次 × 15 秒：

| 变量 | 取值 | 结果 |
|---|---|---|
| 分辨率 | 3360×2100 / 2560×1600 / 1920×1080 / 1280×800 | 每种都是 3 次里失败 1 次 |
| 码率 | 36 / 25 / 8 Mbps | 无差别 |
| 关键帧间隔 | 120 / 30 帧 | 无差别 |
| `movieFragmentInterval` | 1 秒 / 不设 | 无差别 |
| 音频轨 | 有 / 无 | 无差别（复现程序只有视频轨） |

**所以不是尺寸、不是码率、不是分片、不是音频。** 它在任何配置下都以约 1/3 的
概率出现，包括 1280×800 这种最普通的尺寸。

## 尚未确定的

- **是不是系统负载引起的。** 复现时 `uptime` 显示 load average ≈ 6，
  `replayd` 占 77% CPU。（`replayd` 正是 ScreenCaptureKit 的后台服务。）
  同一台机器在不同时刻的失败率看起来不同，但没有做受控的负载实验，
  所以这条只是**最可疑的方向，不是结论**。
- **是不是 `-16341` 有更细的成因。** 该码没有公开文档。

## 已做的修改

1. **`errorText()` 现在会走 `NSUnderlyingErrorKey` 链**（最多 4 层，防自引用死循环），
   并把 `localizedFailureReason` 附在后面。下一次失败时 `project.json` 里的 `error`
   会是 `…（AVFoundationErrorDomain -11800）← …（NSOSStatusErrorDomain -16341）`，
   而不是一句无信息量的「操作无法完成」。
2. **失败时已有的内容不丢**：这条路本来就调用 `finishWritingWithCompletionHandler`，
   把已写的分片收尾成可读文件（实测 70 秒的失败工程能完整解码）。
   缺的是「自动续录」，**尚未实现**。

## 尚未做的（按价值排序）

1. **自动续录**：一次 append 失败就判定整段失败，代价太大。
   `writer.status == Failed` 之后确实无法继续写同一个文件，但可以
   `finishWriting` 收尾当前分片 → 新建 writer → 追加写到 `raw-2.mp4`，
   工程加载时把多个文件按时序拼起来。一次瞬时错误只损失一段，而不是整场录制。
2. **受控负载实验**：在同一台机器上人为加压（例如并行跑满 CPU），
   对比失败率。这能把「疑似」变成「确认」。
3. **失败时立刻告知用户**：现在录制中止后界面只是状态文字变化，
   应该明确说「编码器中断，已保存到第 N 秒」。

## 复现方法

```sh
# 最小复现（只有一个视频轨，无音频）
cd /tmp/sctest && clang++ -std=c++20 -fobjc-arc \
  -framework Foundation -framework AVFoundation -framework ScreenCaptureKit \
  -framework CoreMedia -o sctest main.mm && ./sctest 100
```
