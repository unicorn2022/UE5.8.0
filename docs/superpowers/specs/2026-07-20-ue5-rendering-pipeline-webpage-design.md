# UE5.8.0 渲染管线解读网页设计规格

## 概述

基于 UE5.8.0 `DeferredShadingRenderer.cpp` 中 `FDeferredShadingSceneRenderer::Render` 函数的完整源码分析，生成一套中文技术解读网页，以"先总后分"的方式讲解 UE5.8.0 延迟渲染管线。

- **源码基准**：`D:\project_ue5\ue5.8.0-main\Engine\Source\Runtime\Renderer\Private\DeferredShadingRenderer.cpp` L1826-L4456
- **输出目录**：`UE5.8渲染流程解读/`
- **页面风格**：深色技术文档风格，沿用现有 `UE5.8源码解读` 项目的 CSS 变量体系
- **页面架构**：多页分层式（仿现有项目），总览页 + part/ 下各模块独立页面

---

## 文件结构

```
UE5.8渲染流程解读/
├── index.html                      # 总览页（九阶段时间线 + 模块架构 + 目录）
├── part/
│   ├── 01-管线总览/
│   │   └── index.html              # Render 函数九阶段完整讲解
│   ├── 02-Nanite/
│   │   └── index.html              # ★ 深度模块：Nanite 全流程
│   ├── 03-光照/
│   │   └── index.html              # ★ 深度模块：光照全流程
│   ├── 04-材质/
│   │   └── index.html              # ★ 深度模块：材质全流程
│   ├── 05-Lumen/
│   │   └── index.html              # [计划] Lumen 全局光照
│   ├── 06-VSM/
│   │   └── index.html              # [计划] 虚拟阴影贴图
│   ├── 07-后处理/
│   │   └── index.html              # [计划] TSR/TAA/Bloom/DOF/Tonemap
│   ├── 08-半透明与体积/
│   │   └── index.html              # [计划] 半透明渲染 + 体积云/雾
│   ├── 09-RDG与多线程/
│   │   └── index.html              # [计划] RDG Pass 调度 + TaskGraph
│   └── 10-光追/
│       └── index.html              # [计划] 实时光线追踪
```

---

## 总览页面（index.html）

深色技术文档风格，启用 `:root` CSS 变量：

- `--bg: #0a0a0f`, `--card: #131318`, `--code: #09090d`
- `--accent: #4FC3F7`
- `--text: #e0e0e0`, `--text-dim: #909090`
- `--border: rgba(255,255,255,0.06)`
- 字体：`'Segoe UI','PingFang SC','Microsoft YaHei',sans-serif`

**内容区块（自上而下）：**

1. **Hero** — 标题 "UE5.8.0 渲染管线源码解读" + 副标题 + 标签（Deferred Shading、Nanite、Lumen、MegaLights）
2. **九阶段时间线** — 9 张阶段卡片（水平或纵向排列），每张含阶段名、入口函数、职责描述，点击跳转到 01-管线总览 页面对应阶段锚点
3. **帧内依赖关系图** — 箭头连接的简化流程图，标注关键数据依赖和异步并行标注
4. **模块架构分层** — 三层：核心管线层（BasePass/Lighting/Translucency）→ 子系统层（Nanite/Lumen/VSM）→ 框架层（RDG/RHI），卡片网格，点击进入各模块页面
5. **推荐阅读路径** — 三条路径：新手（从上到下）、有经验者（从管线入口下钻）、排错优化
6. **全量目录** — 表格含编号、章节名、说明、完成状态标记（已完成 / 计划中）
7. **Footer** — 基于 UE5.8.0 源码分析、日期、模块统计

---

## 管线总览页面（part/01-管线总览/index.html）

**内容区块：**

1. **面包屑导航** — 首页 > 渲染管线总览
2. **概述段** — Render 函数定位（L1826，约 2600 行），输入 FRDGBuilder + SceneUpdateInputs，输出一帧完整画面
3. **九阶段总览图** — SVG 横向流程图，9 节点按时间线排列
4. **每阶段详细卡片**（9 张展开式）— 源码行号区间、目的说明、关键函数调用链、输出资源
5. **阶段间数据流** — RDG 纹理/缓冲区依赖关系
6. **异步并行标注** — Lumen、MegaLights、Volumetric Clouds 的 AsyncCompute 路径
7. **跳转链接** — 每阶段末尾链接到对应模块页面

**九阶段划分：**

| 阶段 | 源码行号 | 职责 |
|------|---------|------|
| 1. 视图与管线初始化 | L1826-L1868 | 确定 RendererOutput、Nanite 启用状态 |
| 2. 光追初始化 | L1870-L1900 | 更新 RT 几何体、重置 SBT |
| 3. 视图与可见性 | L1902-L2465 | OnRenderBegin → BeginInitViews → GPUScene → EndInitViews |
| 4. 预通道与 Nanite | L2476-L2534 | 深度清屏 → PrePass → Nanite 光栅化 → 深度解析 |
| 5. BasePass | L3094-L3131 | G-Buffer 写入（传统 + Nanite Compute Shader） |
| 6. 延迟光照 | L3507-L3523 | Clustered Deferred + MegaLights + 半透明光照体积 |
| 7. 半透明渲染 | L4057-L4098 | 前向半透明 + 扭曲 + OIT |
| 8. 后处理 | L4274-L4382 | TSR/TAA → Bloom → DOF → Tonemap → 色彩分级 |
| 9. 收尾 | L4423-L4456 | OnRenderFinish → 纹理提取 → 帧历史释放 |

---

## Nanite 模块页面（part/02-Nanite/index.html）

**定位**：完整重写，从 Render 函数中的 Nanite 调用出发，下钻底层实现。

**内容区块：**

1. **管线中的位置** — 三个关键介入点：Phase 3 可见性查询 → Phase 4 光栅化 → Phase 5 BasePass CS 着色
2. **核心概念** — 虚拟几何（Cluster/BVH/Page）→ GPU 驱动裁剪流水线 → VisBuffer → 延迟材质着色
3. **可见性阶段** — `BeginVisibilityFrame` / `BeginVisibilityQuery`，视锥裁剪 + 遮挡裁剪 + LOD，源码至 `Nanite/NaniteVisibility.cpp`
4. **光栅化阶段** — `RenderNanite`，硬件光栅化 vs CS 光栅化，VisBuffer 64 位编码，深度输出，源码至 `Nanite/NaniteRender.cpp`
5. **着色阶段** — `BuildShadingCommands` → `NaniteShadingCommands`，CS 延迟着色，Material Bin 批处理，Shading Mask，GBuffer 合并
6. **流式加载** — `GStreamingManager.BeginAsyncUpdate/EndAsyncUpdate`，页面请求，安装/卸载，LOD 按需加载
7. **与传统管线交互** — 与 VSM、Lumen、光追的接口（Nanite Card Capture、RayTracing Manager）
8. **CVar 速查** — 关键 `r.Nanite.*` CVar
9. **核心源码索引** — 源文件路径 + 关键函数汇总

---

## 光照模块页面（part/03-光照/index.html）

**定位**：覆盖 Render 函数中所有光照相关流程，从光源收集到最终着色。

**内容区块：**

1. **管线中的位置** — 光照贯穿 Phase 3（光源收集排序）、Phase 3（前向光照数据）、Phase 6（延迟光照计算）、Phase 6（半透明光照体积）
2. **核心概念** — 延迟光照 vs 前向光照、Clustered Deferred Shading 聚簇原理、光源类型层级、光照函数图集
3. **光源收集与排序** — `GatherAndSortLights` 异步任务，SortedLightSet，Batched/Unbatched/MegaLights 分桶
4. **Clustered Deferred** — `PrepareForwardLightData` → `ComputeLightGridOutput`，光源注入 3D 聚簇网格，`RenderLights`
5. **MegaLights** — UE5.8 新增，`CreateMegaLightsFrameTemporaries` → `GenerateMegaLightsSamples` → `RenderMegaLights`，随机采样 + HWRT/SDF 双路径 + 时序降噪
6. **虚拟阴影贴图** — VSM 初始化 → 页面标记 → `RenderShadowDepthMaps`，基于页面的虚拟化阴影，Clipmap 层级
7. **间接光照与 AO** — `RenderDiffuseIndirectAndAmbientOcclusion`，Lumen GI + SSAO + DFAO 合成
8. **反射与天空光** — `RenderDeferredReflectionsAndSkyLighting`，Lumen 反射 + 天空光照
9. **半透明光照体积** — `RenderTranslucencyLightingVolume`，体素标记 + 光照累积
10. **CVar 速查** — `r.Light.*` / `r.Shadow.*` / `r.MegaLights.*`
11. **核心源码索引** — `LightRendering.cpp`, `LightFunctionAtlas.cpp`, `MegaLights/`, `VirtualShadowMaps/`

---

## 材质模块页面（part/04-材质/index.html）

**定位**：关注材质系统如何嵌入渲染管线，而非材质编辑器或蓝图编译细节。

**内容区块：**

1. **管线中的位置** — 材质贯穿：BasePass G-Buffer → 延迟光照 BRDF → 半透明前向着色 → Nanite 延迟材质着色
2. **核心概念** — UMaterial → FMaterialRenderProxy → FMaterial/ShaderMap → MeshDrawCommand，材质域分类
3. **材质与 MeshDrawCommand** — FMeshDrawCommand 携带材质状态，材质排序键，Shader 绑定，PSO 缓存，`FinishGatherDynamicMeshElements`
4. **BasePass 中的材质** — `RenderBasePass`，传统几何 MeshDrawCommand 硬件光栅化，材质→GBuffer 通道（A/B/C/D/E），材质分类与编码
5. **Nanite 中的材质** — `BuildShadingCommands` → `NaniteShadingCommands`，Material Bin 批处理，CS 直接写 GBuffer，与传统路径对比
6. **半透明材质** — `RenderTranslucency`，前向渲染中的混合、排序、扭曲材质、OIT
7. **DBuffer 贴花** — `CompositionLighting.ProcessBeforeBasePass`，DBuffer 纹理创建，贴花材质投影到 GBuffer
8. **材质着色模型** — Substrate 框架简介（若启用），传统 Shading Model（Default/Subsurface/ClearCoat 等）
9. **CVar 速查** — `r.Material.*` / `r.Substrate.*`
10. **核心源码索引** — `Material.cpp`, `MaterialRenderProxy.cpp`, `BasePassRendering.cpp`, `MeshPassProcessor.cpp`

---

## 占位模块统一模板

每个 `[计划]` 页面（05-Lumen 到 10-光追）包含：

1. **头部** — 模块名称 + `[计划中]` 标记
2. **管线中的位置** — 在 Render 函数第几阶段触发（1-2 句话）
3. **内容大纲** — 3-5 条未来计划涵盖的主题列表
4. **关联链接** — 链回管线总览页面、相关已完成模块

---

## 实现方案

采用**概念+源码双轨**（方案 B）：每个深度模块页面先讲渲染概念和设计目标，再展示在 Render 函数中的调用位置，然后下钻核心源码实现，用 SVG 架构图串联概念和代码。

与其他项目的关系：全新重写（方案 B），不引用现有 `UE5.8源码解读` 项目内容，自成体系。

---

## 约束

- 所有文字必须简体中文
- 术语保留英文原文（如 G-Buffer、Clustered Deferred Shading、VisBuffer）
- 代码注释使用中文
- 仅深入材质 + 光照 + Nanite 三个模块，其余占位
- 沿用现有项目深色主题 CSS 变量体系
