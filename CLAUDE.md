# UE5.8.0 渲染源码解读项目

## 项目定位

本仓库（`ue5.8.0-docs`）是 UE5.8.0 渲染体系的技术解读文档站，以 HTML 网页形式讲解渲染管线的源码实现。源码本体位于同级目录 `ue5.8.0-main`。

## 分支策略（必须严格遵守）

- **`main` 分支**：仅放 ue5.8.0-main 源码本身的内容（由其他流程维护），本仓库不向其合并任何内容
- **`docs` 分支**：仅放文档解读网页，包括设计文档、实现计划、HTML 页面等
- **两个分支永远各自 push 各自的内容，永不合并**

## UE5.8.0 渲染源码阅读路径

以下所有路径均相对于 `../ue5.8.0-main/`（即 UE5.8.0 引擎源码根目录）。阅读渲染代码时应从顶层入口开始，按需下钻到子系统。

### 顶层入口 — 一帧渲染主循环

| 文件 | 说明 |
|------|------|
| `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp` | **渲染主循环**，`FDeferredShadingSceneRenderer::Render()` (L1826-L4456) 包含完整的九阶段帧渲染流程 |
| `Engine/Source/Runtime/Renderer/Private/SceneRendering.cpp` | 场景渲染器基类，可见性计算入口 `BeginInitViews` |

### 核心 Pass — 帧内各阶段的实现文件

| 文件 | 对应阶段 | 说明 |
|------|---------|------|
| `Engine/Source/Runtime/Renderer/Private/BasePassRendering.cpp` | BasePass | G-Buffer 写入，材质着色到 GBuffer A/B/C/D/E 通道 |
| `Engine/Source/Runtime/Renderer/Private/LightRendering.cpp` | 延迟光照 | Clustered Deferred Shading 光源遍历与计算 |
| `Engine/Source/Runtime/Renderer/Private/IndirectLightRendering.cpp` | 间接光照 | 漫反射间接光照 + AO 合成 |
| `Engine/Source/Runtime/Renderer/Private/ShadowRendering.cpp` | 阴影 | 传统阴影贴图渲染（非 VSM） |
| `Engine/Source/Runtime/Renderer/Private/DepthRendering.cpp` | PrePass | 深度预通道渲染 |
| `Engine/Source/Runtime/Renderer/Private/VelocityRendering.cpp` | 速度 | 运动向量渲染 |
| `Engine/Source/Runtime/Renderer/Private/MeshPassProcessor.cpp` | 可见性→渲染 | Mesh Draw Command 生成与处理 |
| `Engine/Source/Runtime/Renderer/Private/TranslucencyRendering.cpp` | 半透明 | 半透明渲染 |

### 子系统目录

#### Nanite — 虚拟几何系统

```
Engine/Source/Runtime/Renderer/Private/Nanite/
├── NaniteCullRaster.cpp/h     — GPU 裁剪 + 光栅化（核心，最大文件）
├── NaniteShading.cpp/h        — 延迟材质着色、Material Bin 批处理
├── NaniteVisibility.cpp/h     — 可见性查询框架
├── NaniteShared.cpp/h         — 共享数据结构和常量
├── Nanite.cpp/h               — 全局资源管理、RenderNanite 入口
├── NaniteComposition.cpp/h    — 与传统管线合成
├── NaniteMaterials.cpp/h      — 材质管理
├── NaniteRayTracing.cpp/h     — 光追集成
├── NaniteStreamOut.cpp/h      — 流式换出
├── NaniteDrawList.cpp/h       — 绘制列表
├── NaniteFeedback.cpp/h       — 性能反馈
├── NaniteVisualize.cpp/h      — 可视化调试
├── NaniteEditor.cpp/h         — 编辑器支持
├── NaniteTranslucency.cpp/h   — 半透明 Nanite
└── Voxel.cpp/h                — 体素表示
```

对应的 Shader 文件：
```
Engine/Shaders/Private/Nanite/
├── NaniteClusterCulling.usf         — Cluster 裁剪 CS
├── NaniteExportGBuffer.usf          — GBuffer 导出
├── NaniteDepthDecode.usf            — 深度解码
├── NaniteEmitShadow.usf             — 阴影深度
├── NaniteDataDecode.ush             — 数据解码共享头
├── NaniteCullingCommon.ush          — 裁剪公共函数
└── NaniteHierarchyTraversal.ush     — BVH 层级遍历
```

#### Lumen — 实时全局光照

```
Engine/Source/Runtime/Renderer/Private/Lumen/
├── LumenSceneLighting.cpp      — 场景光照评估
├── LumenSurfaceCache.cpp       — 表面缓存
├── LumenRadianceCache.cpp      — 辐射度缓存
├── LumenScreenProbeGather.cpp  — 屏幕探针收集
├── LumenReflections.cpp        — Lumen 反射
├── LumenTracingUtils.cpp/h     — 追踪工具（SDF/HWRT）
├── LumenHardwareRayTracing.cpp — 硬件光追路径
└── LumenVisualize.cpp          — 可视化调试
```

#### MegaLights — UE5.8 大量动态光源系统

```
Engine/Source/Runtime/Renderer/Private/MegaLights/
├── MegaLights.cpp/h            — 主入口 + 帧级资源管理
├── MegaLightsSampling.cpp      — 随机光源采样
├── MegaLightsRayTracing.cpp    — HWRT 路径
├── MegaLightsDenoising.cpp     — 时序降噪
├── MegaLightsResolve.cpp       — 结果合成
├── MegaLightsLightPowerDelta.cpp — 光照功率增量
├── MegaLightsVisualize.cpp     — 调试可视化
└── MegaLightsViewState.h       — 视图状态
```

#### Virtual Shadow Maps — 虚拟阴影贴图

```
Engine/Source/Runtime/Renderer/Private/VirtualShadowMaps/
├── VirtualShadowMapArray.cpp/h       — VSM 数组管理
├── VirtualShadowMapCacheManager.cpp  — 页面缓存管理
├── VirtualShadowMapProjection.cpp    — 阴影投影
├── VirtualShadowMapPageMarking.cpp   — 页面标记
└── VirtualShadowMapPageAllocation.cpp — 页面分配
```

#### PostProcess — 后处理管线

```
Engine/Source/Runtime/Renderer/Private/PostProcess/
├── PostProcessing.cpp          — 后处理主调度（AddPostProcessingPasses）
├── TemporalSuperResolution.cpp — TSR（时序超分）
├── PostProcessTonemap.cpp      — 色调映射
├── PostProcessBloom.cpp        — Bloom
├── PostProcessDOF.cpp          — 景深
├── PostProcessEyeAdaptation.cpp — 眼部适应
└── PostProcessMotionBlur.cpp   — 运动模糊
```

#### RayTracing — 实时光线追踪

```
Engine/Source/Runtime/Renderer/Private/RayTracing/
├── RayTracing.cpp/h              — 主入口 + TLAS/BLAS 管理
├── RayTracingShadows.cpp         — 光追阴影
├── RayTracingReflections.cpp     — 光追反射
├── RayTracingAmbientOcclusion.cpp — 光追 AO
├── RayTracingTranslucency.cpp    — 光追半透明
├── RayTracingSkyLight.cpp        — 光追天空光
└── RayTracingMaterialHitShaders.cpp — 材质 Hit Shader
```

#### 其他子系统

| 目录 | 说明 |
|------|------|
| `Engine/Source/Runtime/Renderer/Private/CompositionLighting/` | 合成光照（贴花、SSAO、SSR） |
| `Engine/Source/Runtime/Renderer/Private/HairStrands/` | 毛发渲染（Hair Strands） |
| `Engine/Source/Runtime/Renderer/Private/HeterogeneousVolumes/` | 异质体积渲染 |
| `Engine/Source/Runtime/Renderer/Private/Substrate/` | Substrate 材质框架 |
| `Engine/Source/Runtime/Renderer/Private/OIT/` | 顺序无关半透明 |
| `Engine/Source/Runtime/Renderer/Private/InstanceCulling/` | 实例化裁剪 |
| `Engine/Source/Runtime/Renderer/Private/VT/` | 虚拟纹理 |
| `Engine/Source/Runtime/Renderer/Private/Froxel/` | 视锥体素（用于前向光照网格） |
| `Engine/Source/Runtime/Renderer/Private/SceneCulling/` | 场景裁剪 |
| `Engine/Source/Runtime/Renderer/Private/StateStream/` | 状态流 |

### 渲染框架层

| 文件/目录 | 说明 |
|-----------|------|
| `Engine/Source/Runtime/RenderCore/Public/RenderGraphBuilder.h` | **RDG**：FRDGBuilder — Pass 注册、资源生命周期、自动屏障 |
| `Engine/Source/Runtime/RenderCore/Public/RenderGraphResources.h` | RDG 资源类型（FRDGTextureRef, FRDGBufferRef） |
| `Engine/Source/Runtime/RenderCore/Public/RenderGraphUtils.h` | RDG 工具函数（AddPass, AddClearRenderTargetPass 等） |
| `Engine/Source/Runtime/RenderCore/Public/RenderGraphBlackboard.h` | RDG 黑板（跨 Pass 数据传递） |
| `Engine/Source/Runtime/RenderCore/Public/ShaderParameterStruct.h` | `SHADER_PARAMETER_STRUCT` 宏 — Shader 参数绑定 |
| `Engine/Source/Runtime/RenderCore/Public/Shader.h` | FShader 基类 |
| `Engine/Source/Runtime/RenderCore/Public/GlobalShader.h` | FGlobalShader |
| `Engine/Source/Runtime/RenderCore/Public/MaterialShader.h` | FMaterialShader |
| `Engine/Source/Runtime/RenderCore/Public/MeshMaterialShader.h` | FMeshMaterialShader |
| `Engine/Source/Runtime/RHI/Public/RHICommandList.h` | FRHICommandList — GPU 命令列表 |
| `Engine/Source/Runtime/RHI/Public/RHIDefinitions.h` | RHI 平台抽象定义 |

### 材质系统

| 文件 | 说明 |
|------|------|
| `Engine/Source/Runtime/Engine/Public/Materials/Material.h` | UMaterial / FMaterialResource |
| `Engine/Source/Runtime/Engine/Private/Materials/Material.cpp` | 材质编译主逻辑 |
| `Engine/Source/Runtime/Engine/Public/Materials/MaterialRenderProxy.h` | FMaterialRenderProxy |
| `Engine/Source/Runtime/Engine/Private/Materials/MaterialRenderProxy.cpp` | 渲染代理 + Uniform Expression 缓存 |
| `Engine/Source/Runtime/Engine/Public/Materials/MaterialInterface.h` | UMaterialInterface |
| `Engine/Source/Runtime/Engine/Public/Materials/MaterialExpression.h` | 材质表达式节点（100+ 种） |
| `Engine/Source/Runtime/Renderer/Public/MeshPassProcessor.h` | FMeshPassProcessor / FMeshDrawCommand / FMeshDrawCommandSortKey |
| `Engine/Source/Runtime/Engine/Public/MeshBatch.h` | FMeshBatch / FMeshBatchElement |

### Shader 源码（.usf / .ush）

| 目录 | 说明 |
|------|------|
| `Engine/Shaders/Private/` | 所有渲染 Shader 源码 |
| `Engine/Shaders/Private/Nanite/` | Nanite Shader |
| `Engine/Shaders/Private/Lumen/` | Lumen Shader |
| `Engine/Shaders/Private/MegaLights/` | MegaLights Shader |
| `Engine/Shaders/Private/VirtualShadowMaps/` | VSM Shader |
| `Engine/Shaders/Private/PostProcess/` | 后处理 Shader |
| `Engine/Shaders/Private/RayTracing/` | 光追 Shader |
| `Engine/Shaders/Private/Substrate/` | Substrate Shader |

### 阅读建议

1. **理解一帧全貌**：从 `DeferredShadingRenderer.cpp` 的 `Render()` 函数入手（约 2600 行），按九阶段顺序跟踪
2. **深入子系统**：找到 `Render()` 中的关键函数调用，追溯至具体实现文件
3. **对照 Shader**：C++ 端定义 Pass 和资源绑定，`.usf` 文件定义 GPU 端具体计算
4. **关键数据结构**：理解 `FViewInfo`、`FScene`、`FMeshDrawCommand`、`FRDGBuilder` 即可串联大部分流程
