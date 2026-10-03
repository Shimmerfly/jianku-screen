# 独立复核：官方安装包中的动画参数

2026-10-03｜目的：独立验证此前静态研究的关键数值，避免把推测或搜索引擎传言当结论。

## 方法

- 只读提取本机官方安装的 `/Applications/Screen Studio.app`（bundle 版本 **3.4.4-3794**）中的 `Contents/Resources/app.asar`，在会话临时目录按字面量核对。
- 没有运行其任何业务函数，没有修改安装包，没有把专有代码复制进本仓库；这里只记录「名称 → 原始字面量 → 解码值 → 是否与既有研究一致」。
- 该 3.4.4 构建使用了字符串表 + 十六进制常量混淆，因此数值以 `0x` 形式出现；弹簧对象通过变量间接引用（`FJ=s5`、`VJ=RQ`、`zJ` 直接量），已逐一解析。
- 研究基线是官方 **3.7.5-4595**（安装包已不在本机，哈希见 [manifest](2026-09-30-official-3.7.5/manifest.json)）。本轮用 3.4.4 交叉验证「机制与数值」，**不能**用它替代 3.7.5 的运行结果，也不能替代「新工程最终生效默认值」的受控验证。

## 已逐一核对的弹簧与数值

| 字段 / 用途 | 原始字面量（3.4.4） | 解码 | 与既有研究 |
|---|---|---|---|
| mouseClickSpring | `zJ={'stiffness':0x2bc,'damping':0x1e,'mass':0x1}` | 700 / 30 / 1 | 一致 |
| 光标 Smooth（`s5`/`FJ`） | `{'stiffness':0x1d6,'damping':0x46,'mass':0x3}` | 470 / 70 / 3 | 一致 |
| 光标 Medium | `{'stiffness':0x154,'damping':0x3c,'mass':0x3}` | 340 / 60 / 3 | 一致 |
| 临近点击弹簧 | `{'stiffness':0x212,'damping':0x28,'mass':0x1}` | 530 / 40 / 1 | 一致 |
| 拖拽弹簧 | `{'stiffness':0x3e8,'damping':0x28,'mass':0x1}` | 1000 / 40 / 1 | 一致 |
| 画面 Focused（`RQ`/`VJ`） | `{'mass':2.25,'stiffness':0xc8,'damping':0x28}` | 200 / 40 / 2.25 | 一致 |
| 画面 Smooth（`JEe`） | `{'stiffness':0xaa,'damping':0x32,'mass':0x3}` | 170 / 50 / 3 | 一致 |
| 点击/隐藏外层 fader | `{'stiffness':0x12c,'damping':0x1e,'mass':0.3}` | 300 / 30 / 0.3 | 一致 |
| 内层 mask-blurrer | `{'stiffness':0x596,'damping':0x46,'mass':0x1}` | 1430 / 70 / 1 | 一致 |
| 关闭平滑占位弹簧 | `{'stiffness':0x1,'damping':0x1,'mass':0x0}` | 1 / 1 / 0 | 一致 |
| 求解器回退配置 | `{'stiffness':0x5a,'damping':0x9,'mass':0x1,'clamp':false,'precision':0.002}` | 90 / 9 / 1 | 一致 |
| **新发现（此前未记录）** | `{'stiffness':0x1770,'damping':0x15e,'mass':0x1}` | **6000 / 350 / 1** | 角色未定位，待查 |

## 已核对的基础配置（`gw` 对象）

- `backgroundPaddingRatio`: `0xa` → **10**
- `backgroundBlur`: `0x0` → **0**
- `shadowIntensity`: `0.75`；`shadowAngle`: `0x5a` → **90**；`shadowDistance`: `0x19` → **25**；`shadowBlur`: `0x14` → **20**
- `motionBlurAmount` / `motionBlurCursorAmount` / `motionBlurScreenMoveAmount` / `motionBlurScreenZoomAmount`: 均 `0x1` → **1**
- `insetPadding` 四边 `0x0`；`insetAlpha` `0.5`
- `mouseMovementSpring`: `FJ`；`screenMovementSpring`: `VJ`；`mouseClickSpring`: `zJ`（见上表）
- 录制类型覆盖分支：`backgroundPaddingRatio = cond ? gw[...] : (4*-0x8c6 + 0x1fac + 0x36c)`，该算式等于 **0**；`windowBorderRadius` 同一分支同样等于 **0**。即：窗口/区域/外接设备模式取基础值 10/12，两者均否时取 0。与既有研究一致。

## 结论与仍未验证的部分

- **既有研究记录的弹簧数值与官方包干净一致，可以采信为「静态基线」。** 本轮没有发现任何数值被臆造。
- 新增一个 `6000/350/1` 弹簧，用途未知；不要凭数值大小猜用途，需继续定位。
- 以下**仍只是静态结论，未升级为已复现**：新工程最终生效默认值（会被用户偏好、上个工程设置、预设覆盖）；自动焦点区间生成的时间规则（3.7.5 的 `floor` 前后 300/2500/800 ms、1 s 尾部过滤、2500 ms 合并容差）；逐帧求解的 `ceil(dt)-dt` 分数尾步；运动模糊的 `fps/60`、位移 kernel 21 / 缩放 kernel 13、采样权重与伪随机偏移。这些需受控运行 + 逐帧对照，见 [验证计划](../../验证计划.md)。
- 3.4.4 的混淆形式下，与时间相关的函数路径未再做逐字复核；若用户能重新提供 3.7.5 安装包，应优先用它复现研究哈希并做运行验证。
