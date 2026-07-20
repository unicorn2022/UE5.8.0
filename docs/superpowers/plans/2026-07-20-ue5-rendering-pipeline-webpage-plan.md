# UE5.8.0 渲染管线解读网页 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 生成一套中文技术解读网页（11 个 HTML 文件），以"先总后分"方式讲解 UE5.8.0 延迟渲染管线。

**Architecture:** 多页分层式静态网页，深色技术文档风格，inline CSS。所有页面共享同一套 CSS 变量体系。总览页展示九阶段时间线和模块架构图，模块页按"概念→管线位置→源码实现"三层结构编写。

**Tech Stack:** 纯静态 HTML5 + inline CSS + inline SVG（无 JS 框架，无构建工具）

## Global Constraints

- 所有文字必须简体中文
- 术语保留英文原文（如 G-Buffer、Clustered Deferred Shading、VisBuffer）
- 代码注释使用中文
- 仅深入材质 + 光照 + Nanite 三个模块，其余占位
- 沿用现有 `UE5.8源码解读` 项目深色主题 CSS 变量体系
- 输出目录：`UE5.8渲染流程解读/`
- 源码基准：`D:\project_ue5\ue5.8.0-main\Engine\Source\Runtime\Renderer\Private\DeferredShadingRenderer.cpp` L1826-L4456

---

## 文件结构

```
UE5.8渲染流程解读/
├── index.html                          # 总览页
├── part/
│   ├── 01-管线总览/index.html           # Render 函数九阶段完整讲解
│   ├── 02-Nanite/index.html             # ★ 深度：Nanite 全流程
│   ├── 03-光照/index.html               # ★ 深度：光照全流程
│   ├── 04-材质/index.html               # ★ 深度：材质全流程
│   ├── 05-Lumen/index.html              # [计划]
│   ├── 06-VSM/index.html                # [计划]
│   ├── 07-后处理/index.html             # [计划]
│   ├── 08-半透明与体积/index.html        # [计划]
│   ├── 09-RDG与多线程/index.html         # [计划]
│   └── 10-光追/index.html               # [计划]
```

---

### Task 1: 项目初始化 — 创建目录结构 + 共享 CSS 模板

**Files:**
- Create: 所有必要的空目录

**Interfaces:**
- Produces: 目录树 `UE5.8渲染流程解读/` + `UE5.8渲染流程解读/part/` 下 10 个子目录

- [ ] **Step 1: 创建所有目录**

```bash
mkdir -p "D:/project_ue5/ue5.8.0-docs/UE5.8渲染流程解读/part/01-管线总览"
mkdir -p "D:/project_ue5/ue5.8.0-docs/UE5.8渲染流程解读/part/02-Nanite"
mkdir -p "D:/project_ue5/ue5.8.0-docs/UE5.8渲染流程解读/part/03-光照"
mkdir -p "D:/project_ue5/ue5.8.0-docs/UE5.8渲染流程解读/part/04-材质"
mkdir -p "D:/project_ue5/ue5.8.0-docs/UE5.8渲染流程解读/part/05-Lumen"
mkdir -p "D:/project_ue5/ue5.8.0-docs/UE5.8渲染流程解读/part/06-VSM"
mkdir -p "D:/project_ue5/ue5.8.0-docs/UE5.8渲染流程解读/part/07-后处理"
mkdir -p "D:/project_ue5/ue5.8.0-docs/UE5.8渲染流程解读/part/08-半透明与体积"
mkdir -p "D:/project_ue5/ue5.8.0-docs/UE5.8渲染流程解读/part/09-RDG与多线程"
mkdir -p "D:/project_ue5/ue5.8.0-docs/UE5.8渲染流程解读/part/10-光追"
```

- [ ] **Step 2: 验证目录结构**

```bash
ls -R "D:/project_ue5/ue5.8.0-docs/UE5.8渲染流程解读/"
```

Expected: 1 个 index.html 占位（不存在） + part/ 下 10 个子目录

- [ ] **Step 3: Commit**

```bash
git -C "D:/project_ue5/ue5.8.0-docs" add "UE5.8渲染流程解读/" && git -C "D:/project_ue5/ue5.8.0-docs" commit -m "$(cat <<'EOF'
初始化 UE5.8渲染流程解读 目录结构

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: 总览页（index.html）

**Files:**
- Create: `UE5.8渲染流程解读/index.html`

**Interfaces:**
- Consumes: 所有子模块页面的相对路径（`part/01-管线总览/index.html` 等）
- Produces: 完整的总览页，包含去往所有子模块的链接

**内容区块清单：**
1. Hero 横幅 — 标题 "UE5.8.0 渲染管线源码解读" + 副标题 + 标签
2. 九阶段时间线 — 9 张卡片（阶段名 + 入口函数 + 职责 + 行号区间）
3. 帧内依赖关系图 — 箭头连接流程图（含异步并行标注）
4. 模块架构分层 — 三层卡片网格
5. 推荐阅读路径 — 3 条路径
6. 全量目录 — 表格含完成状态标记
7. Footer

**共享 CSS 变量（所有页面复用）：**
```css
:root {
  --bg: #0a0a0f;
  --card: #131318;
  --code: #09090d;
  --accent: #4FC3F7;
  --accent-dim: rgba(79,195,247,0.12);
  --text: #e0e0e0;
  --text-dim: #909090;
  --border: rgba(255,255,255,0.06);
  --font-sans: 'Segoe UI','PingFang SC','Microsoft YaHei',sans-serif;
  --font-mono: 'Cascadia Code','Fira Code','Consolas',monospace;
}
```

**CSS 布局关键类（总览页特有）：**
- `.hero` — 居中渐变背景横幅
- `.timeline` — 九阶段卡片网格（`grid-template-columns: repeat(auto-fit, minmax(280px, 1fr))`）
- `.timeline-card` — 单张阶段卡片，含阶段编号、名称、行号、职责描述，hover 上浮效果
- `.flow-diagram` — 流程图区域，flexbox 排列
- `.layer-wrap` — 架构分层包装器
- `.layer-title` — 分层标签（等宽字体 + 大写字母间距）
- `.layer-cards` — 模块卡片网格
- `.mod-card` — 模块卡片，hover 边框高亮 + 上浮
- `.path-list` — 阅读路径三列网格
- `.toc-table` — 全量目录表格

**九阶段卡片数据（直接写入 HTML）：**

| 编号 | 阶段名 | 源码行号 | 职责 | 锚点 |
|------|--------|---------|------|------|
| 1 | 视图与管线初始化 | L1826-L1868 | 确定 RendererOutput、Nanite 启用状态 | #stage1 |
| 2 | 光追初始化 | L1870-L1900 | 更新 RT 几何体、重置 SBT | #stage2 |
| 3 | 视图与可见性 | L1902-L2465 | OnRenderBegin → BeginInitViews → GPUScene → EndInitViews | #stage3 |
| 4 | 预通道与 Nanite | L2476-L2534 | 深度清屏 → PrePass → Nanite 光栅化 → 深度解析 | #stage4 |
| 5 | BasePass | L3094-L3131 | G-Buffer 写入（传统 + Nanite CS） | #stage5 |
| 6 | 延迟光照 | L3507-L3523 | Clustered Deferred + MegaLights + 半透明光照体积 | #stage6 |
| 7 | 半透明渲染 | L4057-L4098 | 前向半透明 + 扭曲 + OIT | #stage7 |
| 8 | 后处理 | L4274-L4382 | TSR/TAA → Bloom → DOF → Tonemap → 色彩分级 | #stage8 |
| 9 | 收尾 | L4423-L4456 | OnRenderFinish → 纹理提取 → 帧历史释放 | #stage9 |

**阶段卡片 HTML 模板：**
```html
<div class="timeline-card">
  <div class="stage-num">阶段 1</div>
  <div class="stage-title">视图与管线初始化</div>
  <div class="stage-loc">DeferredShadingRenderer.cpp:1826-1868</div>
  <div class="stage-desc">确定渲染器输出模式、Nanite 启用状态、光追覆盖模式</div>
</div>
```

**模块架构分层数据（3 层 × N 卡片）：**
- 核心管线层：BasePass + 延迟光照 + 半透明渲染 + 后处理
- 子系统层：Nanite + Lumen + VSM + MegaLights + 光追
- 框架层：RDG + RHI + Shader + 材质 + 多线程

**全量目录表格数据（11 行）：**
| # | 章节 | 说明 | 状态 |
|---|------|------|------|
| 01 | 管线总览 | Render 函数九阶段完整讲解 | 已完成 |
| 02 | Nanite | 虚拟几何 GPU 驱动管线 | 已完成 |
| 03 | 光照 | 光源收集 → Clustered Deferred → MegaLights | 已完成 |
| 04 | 材质 | 材质→Shader→GBuffer 全路径 | 已完成 |
| 05-10 | 其余6章 | 各模块说明 | 计划中 |

- [ ] **Step 1: 编写完整 HTML 文件**

写入 `UE5.8渲染流程解读/index.html`，包含以上所有内容和完整 CSS。

- [ ] **Step 2: 在浏览器中打开验证**

使用 `start` 命令或直接双击打开 index.html，确认所有区块渲染正确、链接可点击。

- [ ] **Step 3: Commit**

```bash
git -C "D:/project_ue5/ue5.8.0-docs" add "UE5.8渲染流程解读/index.html" && git -C "D:/project_ue5/ue5.8.0-docs" commit -m "$(cat <<'EOF'
总览页：九阶段时间线 + 模块架构 + 全量目录

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: 管线总览页面（part/01-管线总览/index.html）

**Files:**
- Create: `UE5.8渲染流程解读/part/01-管线总览/index.html`

**源码参考：**
- `D:\project_ue5\ue5.8.0-main\Engine\Source\Runtime\Renderer\Private\DeferredShadingRenderer.cpp` L1826-L4456

**Interfaces:**
- Consumes: 总览页 `../index.html`（面包屑导航中引用）
- Produces: 含 9 个锚点的完整管线讲解页

**内容区块清单：**
1. 面包屑导航 — `首页 > 渲染管线总览`
2. 概述段 — Render 函数定位说明
3. SVG 九阶段总览流程图
4. 9 张可折叠详细阶段卡片
5. 阶段间数据流说明
6. 异步并行执行标注
7. 去往各模块页面的链接

**九阶段详细信息（每个阶段一张卡片）：**

阶段 1 L1826-L1868：`GetRendererOutput()` 确定输出模式 → `ShouldRenderNanite()` → `HasRayTracedOverlay()` → RenderCapture 调试捕获

阶段 2 L1870-L1900：`GRayTracingGeometryManager->Update()` → `ProcessBuildRequests()` → `RayTracingSBT.ResetMissAndCallableShaders()` → 遍历 Views 注册到 `RayTracingScene`

阶段 3 L1902-L2465：`OnRenderBegin()` 启动可见性 → `CommitFinalPipelineState()` → `GSystemTextures.InitializeTextures()` → `UpdateLightFunctionAtlasTask` 异步 → `VirtualShadowMapArray.Initialize()` → `BeginUpdateLumenSceneTasks()` → `NaniteVisibility.BeginVisibilityFrame()` → `PrepareDistanceFieldScene()` → `BeginInitViews()` 执行可见性计算 → GPU Scene 上传 → `EndInitViews()` → Substrate/HairStrands 初始化

阶段 4 L2476-L2534（RenderPrepassAndVelocity lambda）：`AddClearDepthStencilPass()` → `RenderPrePass()` 深度预通道 → `RenderVelocities()`（若 DDM_AllOpaqueNoVelocity）→ `RenderNanite()` Nanite 光栅化 → `AddResolveSceneDepthPass()` 深度解析

阶段 5 L3094-L3131：`RenderBasePass()` — 传统几何 FMeshDrawCommand 硬件光栅化 + Nanite Compute Shader 着色 → GBuffer（A/B/C/D/E）写入 → `AddResolveSceneDepthPass()`

阶段 6 L3507-L3523：`RenderDiffuseIndirectAndAmbientOcclusion()` 间接光照 → `RenderLights()` Clustered Deferred 直接光照 → `RenderMegaLights()` UE5.8 大量动态光源 → `RenderTranslucencyLightingVolume()` 半透明体积光照 → `RenderDeferredReflectionsAndSkyLighting()` 反射和天空光

阶段 7 L4057-L4098：`RenderTranslucency()` — 半透明前向渲染 → `RenderDistortion()` 扭曲 → `RenderVelocities()` 半透明速度

阶段 8 L4274-L4382：`AddPostProcessingPasses()` — TSR/TAA 抗锯齿 → Bloom → DOF 景深 → Tonemap 色调映射 → 色彩分级 → 输出到 ViewFamilyTexture

阶段 9 L4423-L4456：`OnRenderFinish()` → `QueueSceneTextureExtractions()` → `HairStrands::PostRender()` → 释放帧历史 → `NaniteVisibility.FinishVisibilityFrame()`

**每张阶段卡片 HTML 模板：**
```html
<div class="stage-detail" id="stage3">
  <div class="stage-header">
    <span class="stage-num">阶段 3</span>
    <h2>视图与可见性初始化</h2>
    <span class="stage-range">DeferredShadingRenderer.cpp:1902-2465</span>
  </div>
  <div class="stage-body">
    <p class="stage-purpose">目的说明...</p>
    <div class="call-chain">
      <h4>关键函数调用链</h4>
      <pre><code>OnRenderBegin(GraphBuilder, SceneUpdateInputs)
  → CommitFinalPipelineState()
  → VirtualShadowMapArray.Initialize()
  → NaniteVisibility.BeginVisibilityFrame()
  → BeginInitViews(GraphBuilder, ...)</code></pre>
    </div>
    <div class="stage-output">
      <h4>产出资源</h4>
      <ul>
        <li>InitViewTaskDatas — 可见性计算结果</li>
        <li>GPU Scene 动态图元数据已上传</li>
        <li>Nanite 可见性查询已启动</li>
      </ul>
    </div>
    <a href="../02-Nanite/index.html" class="deep-link">深入：Nanite 可见性 →</a>
  </div>
</div>
```

**SVG 流程图（嵌入 HTML）：**
- 9 个矩形节点从左到右排列
- 每节点标注阶段名和行号
- 箭头连接表示执行顺序
- 虚线箭头标注异步并行路径（Lumen/MegaLights/VolumetricClouds 以 AsyncCompute 执行）

- [ ] **Step 1: 编写管线总览 HTML 文件**

写入 `UE5.8渲染流程解读/part/01-管线总览/index.html`，包含完整 CSS（复用变量体系）和全部 9 阶段详细内容。阅读 DeferredShadingRenderer.cpp L1826-L4456 以验证关键函数名和行号。

- [ ] **Step 2: 验证页面**

在浏览器中打开 index.html，确认所有折叠卡片可展开/收起、SVG 流程图正常显示、跳转链接正确。

- [ ] **Step 3: Commit**

---

### Task 4: Nanite 深度模块页面（part/02-Nanite/index.html）

**Files:**
- Create: `UE5.8渲染流程解读/part/02-Nanite/index.html`

**源码参考：**
- `D:\project_ue5\ue5.8.0-main\Engine\Source\Runtime\Renderer\Private\DeferredShadingRenderer.cpp` — Nanite 相关调用
- `D:\project_ue5\ue5.8.0-main\Engine\Source\Runtime\Renderer\Private\Nanite\NaniteVisibility.cpp` — 可见性查询
- `D:\project_ue5\ue5.8.0-main\Engine\Source\Runtime\Renderer\Private\Nanite\NaniteRender.cpp` — 光栅化
- `D:\project_ue5\ue5.8.0-main\Engine\Source\Runtime\Renderer\Private\Nanite\NaniteShading.cpp` — 着色命令构建
- `D:\project_ue5\ue5.8.0-main\Engine\Source\Runtime\Renderer\Private\Nanite\NaniteStreaming.cpp` — 流式加载

**Interfaces:**
- Consumes: 总览页 `../../index.html`、管线总览 `../01-管线总览/index.html`
- Produces: Nanite 全流程深度解读页

**内容结构（对应规格）：**

1. **管线中的位置** — 三个关键介入点说明
2. **核心概念** — Cluster/BVH/Page → GPU 驱动裁剪 → VisBuffer → 延迟材质着色
3. **可见性阶段** — 源码追踪到 `NaniteVisibility.cpp`
4. **光栅化阶段** — 源码追踪到 `NaniteRender.cpp`
5. **着色阶段** — 源码追踪到 `NaniteShading.cpp`
6. **流式加载** — `GStreamingManager` API
7. **与传统管线交互** — VSM/Lumen/光追接口
8. **CVar 速查**
9. **核心源码索引**

**实现要点：**
- 每个源码引用标注精确文件路径和函数名
- "管线中的位置"用时间线图标注 Nanite 在 Phase 3/4/5 的三个介入点
- "核心概念"用 SVG 框图展示 BVH 层级 → Cluster → 三角形 的三级结构
- 可见性阶段详述 `BeginVisibilityFrame()` → `BeginVisibilityQuery()` 调用链
- 光栅化阶段对比硬件光栅化 vs Compute Shader 光栅化两路径
- 着色阶段解释 Material Bin 批处理机制，展示 ShadingMask → GBuffer 合并流程
- 流式加载展示 `BeginAsyncUpdate → GetAndClearModifiedPages → EndAsyncUpdate` 调用时序
- CVar 表格：`r.Nanite`, `r.Nanite.MaxPixelsPerEdge`, `r.Nanite.Streaming`, 等

- [ ] **Step 1: 阅读 Nanite 相关源码**

阅读 DeferredShadingRenderer.cpp 中 Nanite 相关代码段（L1861、L1991-L2017、L2064-L2089、L2395-L2409、L2511-L2517、L2726-L2734、L3096-L3128），以及 `Nanite/` 目录下的核心实现文件。

- [ ] **Step 2: 编写 Nanite 模块 HTML**

写入 `UE5.8渲染流程解读/part/02-Nanite/index.html`，包含完整 CSS、SVG 架构图、源码摘录代码块、CVar 表格。

- [ ] **Step 3: 验证并 Commit**

---

### Task 5: 光照深度模块页面（part/03-光照/index.html）

**Files:**
- Create: `UE5.8渲染流程解读/part/03-光照/index.html`

**源码参考：**
- `D:\project_ue5\ue5.8.0-main\Engine\Source\Runtime\Renderer\Private\LightRendering.cpp` — Clustered Deferred Shading
- `D:\project_ue5\ue5.8.0-main\Engine\Source\Runtime\Renderer\Private\LightFunctionAtlas.cpp` — 光照函数图集
- `D:\project_ue5\ue5.8.0-main\Engine\Source\Runtime\Renderer\Private\MegaLights\` — MegaLights 系统
- `D:\project_ue5\ue5.8.0-main\Engine\Source\Runtime\Renderer\Private\VirtualShadowMaps\` — VSM
- `D:\project_ue5\ue5.8.0-main\Engine\Source\Runtime\Renderer\Private\IndirectLightRendering.cpp` — 间接光照

**Interfaces:**
- Consumes: 总览页、管线总览
- Produces: 光照全流程深度解读页

**内容结构（对应规格）：**

1. **管线中的位置** — 光照贯穿 Phase 3/6
2. **核心概念** — 延迟光照 vs 前向光照、Clustered Deferred 聚簇网格原理（3D 相机空间网格剖分）
3. **光源收集与排序** — `GatherAndSortLights` 异步任务（源码追踪）
4. **Clustered Deferred** — `PrepareForwardLightData` → `ComputeLightGridOutput` → `RenderLights`
5. **MegaLights** — UE5.8 新增系统全流程
6. **虚拟阴影贴图** — VSM 页面分配 + 渲染流程
7. **间接光照与 AO**
8. **反射与天空光**
9. **半透明光照体积**
10. **CVar 速查**
11. **核心源码索引**

**实现要点：**
- Clustered Deferred 聚簇原理用 SVG 图示：相机视锥 → 3D Tile 网格 → 每个 Tile 的光源列表
- MegaLights 详述随机光源采样算法、HWRT/SDF 双路径、时序降噪策略
- `GatherAndSortLights` 展示光源分类逻辑（Batched/Unbatched/MegaLights 三桶）
- VSM 简述页面分配 → `BeginMarkVirtualShadowMapPages()` → `RenderShadowDepthMaps()` 流程

- [ ] **Step 1: 阅读光照相关源码**

阅读 DeferredShadingRenderer.cpp 中光照相关代码段（L1951-L1961、L2325-L2368、L2826-L2857、L3053-L3088、L3507-L3523、L3562-L3612），以及 `LightRendering.cpp` 和 `MegaLights/` 目录。

- [ ] **Step 2: 编写光照模块 HTML**

写入 `UE5.8渲染流程解读/part/03-光照/index.html`。

- [ ] **Step 3: 验证并 Commit**

---

### Task 6: 材质深度模块页面（part/04-材质/index.html）

**Files:**
- Create: `UE5.8渲染流程解读/part/04-材质/index.html`

**源码参考：**
- `D:\project_ue5\ue5.8.0-main\Engine\Source\Runtime\Renderer\Private\BasePassRendering.cpp` — BasePass 材质着色
- `D:\project_ue5\ue5.8.0-main\Engine\Source\Runtime\Renderer\Private\MeshPassProcessor.cpp` — MeshDrawCommand 处理
- `D:\project_ue5\ue5.8.0-main\Engine\Source\Runtime\Engine\Private\Materials\Material.cpp` — 材质核心
- `D:\project_ue5\ue5.8.0-main\Engine\Source\Runtime\Engine\Private\Materials\MaterialRenderProxy.cpp` — 材质渲染代理

**Interfaces:**
- Consumes: 总览页、管线总览
- Produces: 材质全流程深度解读页

**内容结构（对应规格）：**

1. **管线中的位置** — 材质贯穿 BasePass + 光照 + 半透明 + Nanite
2. **核心概念** — UMaterial → FMaterialRenderProxy → FMaterial/ShaderMap → MeshDrawCommand，材质域分类
3. **材质与 MeshDrawCommand** — `FinishGatherDynamicMeshElements` + PSO 缓存机制
4. **BasePass 中的材质** — G-Buffer 通道（A/B/C/D/E）编码详解
5. **Nanite 中的材质** — Material Bin 批处理 vs 传统路径对比
6. **半透明材质** — 混合模式 + 排序 + 扭曲 + OIT
7. **DBuffer 贴花** — 贴花材质 → GBuffer 投影
8. **材质着色模型** — Substrate + 传统 Shading Model 对照
9. **CVar 速查**
10. **核心源码索引**

**实现要点：**
- 材质→Shader 转化全路径用流程图展示
- MeshDrawCommand 结构图解：材质排序键 + Shader 绑定 + Pipeline State
- G-Buffer 编码表格：A=WorldNormal, B=Specular/Metallic/Roughness, C=BaseColor, D=CustomData, E=PrecomputedShadowFactor
- Nanite 延迟材质着色图解：Material Bin → Shading Command → Compute Shader → GBuffer Write
- DBuffer 流程：DBufferTextures 创建 → 贴花材质绑定 → BasePass 应用

- [ ] **Step 1: 阅读材质相关源码**

阅读 DeferredShadingRenderer.cpp 中材质相关码段（L2241、L3098、L3236-L3274），以及 `BasePassRendering.cpp`、`MeshPassProcessor.cpp`、`Material.cpp`。

- [ ] **Step 2: 编写材质模块 HTML**

写入 `UE5.8渲染流程解读/part/04-材质/index.html`。

- [ ] **Step 3: 验证并 Commit**

---

### Task 7: 占位模块页面 × 6（part/05-Lumen 到 10-光追）

**Files:**
- Create:
  - `UE5.8渲染流程解读/part/05-Lumen/index.html`
  - `UE5.8渲染流程解读/part/06-VSM/index.html`
  - `UE5.8渲染流程解读/part/07-后处理/index.html`
  - `UE5.8渲染流程解读/part/08-半透明与体积/index.html`
  - `UE5.8渲染流程解读/part/09-RDG与多线程/index.html`
  - `UE5.8渲染流程解读/part/10-光追/index.html`

**Interfaces:**
- Consumes: 总览页路径 `../../index.html`、管线总览路径 `../01-管线总览/index.html`
- Produces: 6 个占位页面

**统一 HTML 模板（每个页面替换 `{{MODULE_NAME}}`、`{{STAGE}}`、`{{DESC}}`、`{{OUTLINE}}`）：**

```html
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>{{MODULE_NAME}} — UE5.8.0 渲染管线解读 [计划中]</title>
<style>
:root {
  --bg: #0a0a0f; --card: #131318; --code: #09090d;
  --accent: #4FC3F7; --accent-dim: rgba(79,195,247,0.12);
  --text: #e0e0e0; --text-dim: #909090;
  --border: rgba(255,255,255,0.06);
  --font-sans: 'Segoe UI','PingFang SC','Microsoft YaHei',sans-serif;
  --font-mono: 'Cascadia Code','Fira Code','Consolas',monospace;
}
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
html{scroll-behavior:smooth;font-size:15px}
body{font-family:var(--font-sans);background:var(--bg);color:var(--text);line-height:1.72;min-height:100vh}
a{color:var(--accent);text-decoration:none;transition:0.2s}
a:hover{text-decoration:underline}
.breadcrumb{max-width:900px;margin:0 auto;padding:24px 24px 0;font-size:0.85rem;color:var(--text-dim)}
.breadcrumb a{color:var(--text-dim)}
.page-header{max-width:900px;margin:0 auto;padding:48px 24px 32px;text-align:center}
.page-header .badge{display:inline-block;font-family:var(--font-mono);font-size:0.78rem;padding:4px 14px;border-radius:20px;background:rgba(255,183,77,0.12);color:#FFB74D;border:1px solid rgba(255,183,77,0.2);margin-bottom:16px}
.page-header h1{font-size:2rem;font-weight:800;margin-bottom:8px}
.page-header h1 span{color:var(--accent)}
.page-header .stage-ref{font-size:0.88rem;color:var(--text-dim)}
.content{max-width:900px;margin:0 auto;padding:0 24px 80px}
.outline-card{background:var(--card);border:1px solid var(--border);border-radius:10px;padding:28px;margin-bottom:20px}
.outline-card h3{font-size:1.1rem;color:var(--accent);margin-bottom:12px}
.outline-card ol{font-size:0.9rem;color:var(--text-dim);padding-left:1.4em}
.outline-card ol li{margin-bottom:8px}
.links{display:flex;gap:16px;flex-wrap:wrap;margin-top:32px;justify-content:center}
.links a{font-family:var(--font-mono);font-size:0.85rem;padding:8px 18px;border-radius:8px;background:var(--card);border:1px solid var(--border);transition:all 0.2s}
.links a:hover{border-color:var(--accent);text-decoration:none}
.footer{text-align:center;padding:40px 24px;border-top:1px solid var(--border);color:var(--text-dim);font-size:0.83rem}
@media(max-width:768px){.page-header h1{font-size:1.4rem}}
</style>
</head>
<body>

<nav class="breadcrumb">
  <a href="../../index.html">首页</a> &rsaquo; <a href="../01-管线总览/index.html">渲染管线总览</a> &rsaquo; {{MODULE_NAME}}
</nav>

<header class="page-header">
  <div class="badge">计划中</div>
  <h1>{{MODULE_NAME}}</h1>
  <p class="stage-ref">{{DESC}}</p>
</header>

<main class="content">
  <div class="outline-card">
    <h3>未来计划涵盖主题</h3>
    <ol>
      {{OUTLINE}}
    </ol>
  </div>

  <div class="links">
    <a href="../../index.html">回到首页</a>
    <a href="../01-管线总览/index.html">管线总览</a>
    <!-- 关联到自己完成模块（按实际情况填写）-->
  </div>
</main>

<footer class="footer">
  基于 UE5.8.0 引擎源码分析 &nbsp;|&nbsp; 2026.07 &nbsp;|&nbsp; 此章节内容正在规划中
</footer>

</body>
</html>
```

**6 个占位模块的数据：**

| 文件 | MODULE_NAME | STAGE | DESC | OUTLINE |
|------|-------------|-------|------|---------|
| 05-Lumen | Lumen 全局光照 | Phase 3/6 | `BeginUpdateLumenSceneTasks` → `RenderLumenSceneLighting` / 关联链接：02-Nanite, 03-光照 | `<li>Surface Cache — 表面缓存更新与页面管理</li><li>Screen Probe Gather — 屏幕探针采样与追踪</li><li>Radiance Cache — 辐射度缓存加速间接光照</li><li>硬件光追（HWRT）可选路径 — 与软件 SDF 追踪的对比</li><li>与 Nanite Card Capture 的集成</li>` |
| 06-VSM | 虚拟阴影贴图 | Phase 3/6 | `VirtualShadowMapArray.Initialize` → `RenderShadowDepthMaps` / 关联链接：03-光照 | `<li>页面分配策略 — 按需分配阴影页面</li><li>Clipmap 层级结构 — 近处高精度、远处粗粒度</li><li>One-Pass Projection — 光源投影优化</li><li>页面缓存与失效 — 跨帧重用阴影数据</li><li>与 Nanite 几何体的深度绑定</li>` |
| 07-后处理 | 后处理 | Phase 8 | TSR/TAA → Bloom → DOF → Tonemap → 色彩分级 / 关联链接：01-管线总览 | `<li>TSR（Temporal Super Resolution）— UE5 自有时序超分</li><li>眼部适应与曝光控制</li><li>Bloom 与镜头光晕</li><li>景深（DOF）— Circle DOF 与物理 Bokeh</li><li>色彩分级 — LUT + 参数调节链</li>` |
| 08-半透明与体积 | 半透明渲染与体积效果 | Phase 7 | `RenderTranslucency` + 体积云/雾 / 关联链接：04-材质 | `<li>半透明排序 — 从后向前绘制与 OIT</li><li>扭曲通道 — Separated Translucency + Distortion</li><li>体积云 — 体积渲染目标 + 屏幕空间散射</li><li>体积雾 — Froxel 3D 网格注入</li><li>异质体积渲染（Heterogeneous Volumes）</li>` |
| 09-RDG与多线程 | RDG 与多线程调度 | 贯穿全阶段 | FRDGBuilder + TaskGraph 调度 / 关联链接：01-管线总览 | `<li>FRDGBuilder — Pass 注册与资源生命周期</li><li>Transient 资源分配 — 内存池化与别名分析</li><li>Pass 依赖分析 — 自动屏障插入</li><li>TaskGraph 与 RenderThread 调度模型</li><li>AsyncCompute 与 Graphics 管线并行</li>` |
| 10-光追 | 实时光线追踪 | 贯穿全阶段 | DXR/VulkanRT + SBT + 加速结构 / 关联链接：02-Nanite, 03-光照 | `<li>BLAS/TLAS — 加速结构管理</li><li>SBT — Shader Binding Table 布局</li><li>内联光追 vs 动态管线 — 两种 API 路径</li><li>路径追踪参考渲染器</li><li>光追阴影/反射/AO 的集成</li>` |

- [ ] **Step 1: 依次创建 6 个占位页面**

对每个占位模块，用模板替换对应数据写入文件。

- [ ] **Step 2: 验证所有占位页面**

在浏览器中抽查 2-3 个页面，确认 CSS、面包屑、链接正确。

- [ ] **Step 3: Commit**

---

### Task 8: 交叉链接验证与最终审核

**Files:**
- 无需新建，验证所有已存在文件

- [ ] **Step 1: 验证所有页面间的链接完整性**

检查以下链接关系全部正确：
- 总览页 → 每个 part/ 子页面
- 每个 part/ 子页面的面包屑 → 总览页 + 管线总览
- 管线总览每阶段的"深入链接" → 对应模块页面
- 占位页面的关联链接 → 对应已完成模块

- [ ] **Step 2: 验证 CSS 变量一致性**

确认所有页面使用同一套 `:root` CSS 变量（`--bg`, `--card`, `--accent`, `--text`, `--text-dim`, `--border`, `--font-sans`, `--font-mono`）。

- [ ] **Step 3: 在浏览器中打开所有页面**

逐个确认页面渲染无异常（CSS 未断裂、SVG 正常显示、移动端响应式基本可用）。

- [ ] **Step 4: 最终 Commit**

```bash
git -C "D:/project_ue5/ue5.8.0-docs" add -A && git -C "D:/project_ue5/ue5.8.0-docs" commit -m "$(cat <<'EOF'
完成 UE5.8.0 渲染管线解读网页（11页）

总览页 + 管线总览 + Nanite/光照/材质 深度模块 + 6 占位模块

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## 依赖关系

```
Task 1 (目录) → Task 2 (总览) ─┬→ Task 3 (管线总览) ─┬→ Task 4 (Nanite)
                               │                      ├→ Task 5 (光照)
                               │                      ├→ Task 6 (材质)
                               │                      └→ Task 7 (占位×6)
                               └──────────────────────→ Task 7 (占位链接回总览)
                                                              │
Task 4 + Task 5 + Task 6 + Task 7 ──────────────────────────→ Task 8 (验证)
```

Task 2-7 可并行执行（各自独立创建 HTML 文件），Task 8 必须在所有文件创建完成后执行。
