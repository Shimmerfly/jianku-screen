# Jianku Screen 图标

## 当前在用的图标（2026-10-04）

来源：仓库根目录的 `logo.png`（用户提供，1254×1254 RGBA）。由
`scripts/build-branding.py` 生成全部派生产物，脚本可重复运行：

| 产物 | 用途 |
|---|---|
| `JiankuScreen.icns` | 应用图标：访达、Dock、关于面板 |
| `icon-source.png` | 裁掉画布留白后的源图，脚本的中间产物 |
| `jianku-mark-22/48/128.png` | 界面内的标识：菜单栏、标题栏、权限引导 |
| `jianku-menubar.png` | 菜单栏模板图（纯黑 + alpha，系统按明暗反色） |

脚本做两处校正，都是「不做就会看出来」的：
- **裁掉透明留白**：源图四周有约 7% 的空白，不裁的话图标在 Dock 里会明显偏小。
- **缩到 80.5%**：Apple 的 macOS 图标网格在 1024 画布上给圆角方块 824 px。
  源图的方块占自己画布的 84.7%，直接当图标用会比邻座大一圈。
  **不再套第二层圆角遮罩**——源图本身已经是一块画好的 macOS 圆角底板，
  再遮一次会出现双圆角。

实测过的取舍：菜单栏**没有**用模板剪影，而是用彩色标识。剪影理论上更「正确」
（系统会反色），但实测下来它是「一块圆角方块，中间留着四条缎带之间的缝」，
22 pt 下像个淤青，不像标识。彩色版的平均亮度 64，浅色菜单栏（240）对比充足，
深色菜单栏（28）对比偏低但可辨。`Branding::menuBarIcon()` 保留给将来
专门按剪影设计的图。

## 历史：v1 设计稿

生成日期：2026-10-03。使用内置 image_gen 生图工具。**未在使用的存档稿。**

资产：[jianku-screen-logo-v1.png](jianku-screen-logo-v1.png)。保留原始生成 PNG，外部透明。

设计思路：沿用简库的蓝、青、薄荷绿、黄绿四色，通过弯曲分块形成屏幕取景框和环绕运动；深石墨色圆角底板适配桌面应用图标。参考 Screen Studio 的简洁与图标完成度。

该图目前作为设计稿保存，尚未替换应用图标。用户提供的两张截图只用作参考形象。

## 完整生成提示词

Use case: logo-brand.
Asset type: final macOS application icon for Jianku Screen / 简库镜传, a premium screen recording and live presentation app known for smoothly moving cursor, elegant automatic zoom and cinematic motion.

Input images: Image 1 is the parent Jianku brand reference ONLY: four separate rounded organic geometric pieces, blue, cyan, mint green and lime, arranged with a white opening. The screenshot is low resolution; reinterpret the family resemblance, do not reproduce its outline or its screenshot. Image 2 is Screen Studio's app icon ONLY as a reference for restraint, clarity, compactness and macOS polish. Do not copy its purple ring, its composition or any proprietary mark.

Create ONE original, finished app icon, straight-on, perfectly centered, square composition, isolated on genuinely transparent outside background. A charcoal graphite macOS squircle tile with subtle satin depth and carefully restrained soft edge lighting. The central original symbol is a continuous visual motion made from exactly four distinct softly bent geometric ribbon segments: cyan top, blue left, mint right, fresh lime bottom, recalling Jianku's family colors. Transform them into a balanced rounded rectangular camera/viewfinder aperture, with a clear horizontally rectangular negative-space screen in the center. The four pieces almost flow clockwise around the screen opening, conveying framing and fluid motion. Refined contour transitions, purposeful small separations, excellent optical balance. The aperture opening stays the same graphite as the tile, not a white patch. Keep the design simple enough to recognize at 16–32 px. Symbol occupies about 60% of the tile width and has gently rounded ends/corners, no thin strokes or tiny details. Muted clean brand colors, luminous but never neon, gentle nuanced highlights rather than complicated shiny gradients. This should feel like an expensive professional creative utility.

Only one icon. No words, letters, captions, labels, border, mockup, multiple options, surrounding desktop, checkerboard pattern, extra arrows, cursor glyph, play triangle, sparkle, lens, eye, literal camera, purple ring or photorealistic material. Do not paste or upscale either reference. Transparent beyond the rounded square tile. High resolution crisp anti-aliased edges.
