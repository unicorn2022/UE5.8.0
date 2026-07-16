# UE 5.8.0 渲染管线架构

## 项目概述

这是 Unreal Engine 5.8.0 的源代码仓库。渲染管线核心位于 `Engine/Source/Runtime/Renderer/` 模块，基于 **延迟渲染管线（Deferred Shading）** 与 **Nanite 虚拟几何系统** 深度集成，同时包含 **Lumen 全局光照**、**MegaLights 动态光照**、**Virtual Shadow Maps（VSM）** 等子系统。

---

## 核心目录结构

```
Engine/Source/Runtime/Renderer/
├── Private/
│   ├── DeferredShadingRenderer.cpp    # 延迟渲染管线总入口
│   ├── DeferredShadingRenderer.h      # FDeferredShadingSceneRenderer 类
│   ├── SceneRendering.cpp             # 场景渲染设置
│   ├── SceneRendering.h               # FSceneRenderer 基类
│   ├── BasePassRendering.cpp          # G-Buffer Base Pass
│   ├── LightRendering.cpp             # 延迟光照计算
│   ├── ShadowDepthRendering.cpp       # 阴影深度渲染
│   ├── TranslucentRendering.cpp       # 半透明渲染
│   ├── PostProcess/PostProcessing.cpp # 后处理
│   ├── GPUScene.cpp                   # GPU 场景数据
│   ├── MeshPassProcessor.cpp          # Mesh Pass 处理
│   ├── Nanite/                        # Nanite 虚拟几何系统
│   │   ├── NaniteCullRaster.cpp       # GPU 裁剪与光栅化
│   │   ├── NaniteShading.cpp          # Compute Shader 材质着色
│   │   ├── NaniteMaterials.cpp        # 材质管线加载
│   │   ├── NaniteVisibility.cpp       # 预裁剪可见性
│   │   ├── NaniteComposition.cpp      # 合成到最终场景
│   │   └── NaniteShared.h             # 共享类型与常量
│   ├── VirtualShadowMaps/             # 虚拟阴影贴图
│   │   ├── VirtualShadowMapArray.cpp  # VSM 数组管理
│   │   ├── VirtualShadowMapCacheManager.cpp # VSM 缓存
│   │   ├── VirtualShadowMapClipmap.cpp      # 方向光 Clipmap
│   │   └── VirtualShadowMapProjection.cpp   # 阴影投影
│   ├── Lumen/                         # Lumen 全局光照
│   │   ├── LumenScene.cpp             # Lumen 场景表示
│   │   ├── LumenSceneLighting.cpp     # 直接光照
│   │   ├── LumenRadianceCache.cpp     # 辐射度缓存
│   │   ├── LumenScreenProbeGather.cpp # 屏幕探针收集
│   │   ├── LumenReflections.cpp       # Lumen 反射
│   │   └── LumenReSTIRGather.cpp      # ReSTIR 收集
│   ├── MegaLights/                    # MegaLights 大量动态光源 (5.8)
│   │   ├── MegaLights.cpp             # 核心模块
│   │   ├── MegaLightsRayTracing.cpp   # 硬件光追
│   │   ├── MegaLightsSampling.cpp     # 光源采样
│   │   └── MegaLightsDenoising.cpp    # 降噪
│   └── InstanceCulling/               # GPU 实例裁剪
│       └── InstanceCullingContext.cpp
├── Public/
│   ├── MeshPassProcessor.h            # EMeshPass::Type 枚举
│   └── PrimitiveSceneInfo.h           # ENaniteMeshPass 枚举
└── Renderer.vcxproj
```

---

## 延迟渲染管线主流程

入口函数：`FDeferredShadingSceneRenderer::Render()`（`DeferredShadingRenderer.cpp:1822`）

```
1. CommitFinalPipelineState()        — 确定管线最终状态
2. BeginInitViews()                   — 可见性计算开始
   ├── Frustum Culling
   ├── Occlusion Culling
   └── Nanite Visibility Begin
3. EndInitViews()                     — 可见性计算结束
4. RenderPrePass()                    — 深度预通道 (Depth Only)
5. RenderNanite()                     — Nanite VisBuffer 裁剪+光栅化+深度导出
   ├── InitRasterContext              — 分配 VisBuffer 和深度目标
   ├── DrawGeometry (GPU Cull+Raster) — 裁剪→光栅化→VisBuffer
   ├── ExtractResults                 — 提取可见性结果
   └── EmitDepthTargets              — 从 VisBuffer 解析深度
6. RenderVelocities()                 — 运动矢量
7. RenderShadowDepthMaps()            — 阴影深度贴图 (VSM/CSM)
8. RenderBasePass()                   — G-Buffer 写入
   ├── 传统几何: FMeshDrawCommand → 硬件光栅化 → Pixel Shader
   └── Nanite: ShadeBinning → DispatchBasePass (Compute Shader)
9. RenderLumenSceneLighting()         — Lumen 场景光照
10. RenderDiffuseIndirectAndAmbientOcclusion()  — 漫反射间接光 + AO
11. RenderLights()                     — 延迟光照 (Clustered Deferred)
12. RenderDeferredReflectionsAndSkyLighting()   — 反射 + 天空光照
13. RenderTranslucency()              — 半透明 (Forward)
14. PostProcessing()                   — 后处理 (Bloom/DOF/Tonemap/TAA)
15. RenderFog()                        — 雾效
```

### 类层级

```
FSceneRenderer (虚基类)
├── FDeferredShadingSceneRenderer   — 高端桌面/主机延迟渲染器
└── FMobileSceneRenderer             — 移动端前向/延迟渲染器
```

### Mesh Pass 枚举 (`EMeshPass::Type`)

定义在 `Public/MeshPassProcessor.h:56`：

| Pass | 说明 |
|---|---|
| `DepthPass` | 深度预通道 |
| `BasePass` | G-Buffer 基础通道 |
| `CSMShadowDepth` | 级联阴影贴图 |
| `VSMShadowDepth` | 虚拟阴影贴图 |
| `TranslucencyStandard` | 标准半透明 |
| `Velocity` | 运动矢量 |
| `NaniteMeshPass` | Nanite 专用 mini depth pass |
| `LumenCardNanite` | Lumen Card 捕获 (Nanite) |
| `DitheredLODFadingOutMaskPass` | Dithered LOD 过渡标记 |
| `SingleLayerWaterPass` | 单层水体 |

---

## Nanite 虚拟几何系统

### 与传统管线的核心区别

| 维度 | 传统渲染 | Nanite |
|---|---|---|
| 几何裁剪 | CPU Frustum/Occlusion | GPU Cluster Culling + Two-Pass HZB |
| 光栅化 | 硬件光栅化 (Pixel Shader) | 软件+硬件混合光栅化 (Compute) |
| 中间输出 | 直接写 GBuffer | Visibility Buffer (VisBuffer64) |
| 材质着色 | Pixel Shader | Compute Shader (Screen-Space Binning) |
| DrawCall | FMeshDrawCommand (CPU 提交) | GPU Indirect Dispatch |

### Nanite 渲染流程（`RenderNanite()` —— `DeferredShadingRenderer.cpp:1449`）

```
1. 可见性预查询
   └── FNaniteVisibility::BeginVisibilityQuery() — CPU端预裁剪

2. InitRasterContext (VisBuffer + Depth)
   └── 分配 RWTexture2D<uint> 深度 + RWTexture2D<UlongType> VisBuffer64

3. GPU 裁剪流水线
   ├── Instance Culling      — 实例级可见性
   ├── Primitive Filter      — 隐藏对象筛选
   ├── BVH Node Culling      — 层次包围盒节点遍历裁剪
   ├── Cluster Culling       — Cluster 级别裁剪
   └── HZB Occlusion         — 两阶段遮挡剔除
       ├── Main Pass (CULLING_PASS_OCCLUSION_MAIN=1)
       └── Post Pass (CULLING_PASS_OCCLUSION_POST=2，重测试上一帧被遮挡的对象)

4. 光栅化 (混合调度)
   ├── ERasterScheduling::HardwareOnly       — 仅硬件光栅化
   ├── ERasterScheduling::HardwareThenSoftware  — 大三角形硬件、小三角形软件
   └── ERasterScheduling::HardwareAndSoftwareOverlap — 硬软重叠执行

5. EmitDepthTargets   — 从 VisBuffer 解析深度写入 SceneDepth
6. ShadeBinning       — 屏幕空间按材质分桶 (Compute Shader)
7. DispatchBasePass   — 按桶 dispatch 材质计算着色器
```

### 关键数据结构

```cpp
// 裁剪参数 (NaniteCullRaster.h)
EOutputBufferMode::VisBuffer   // 输出 ID + 深度 (默认)
EOutputBufferMode::DepthOnly   // 仅输出深度 32-bit

// 光栅调度策略
ERasterScheduling::HardwareOnly
ERasterScheduling::HardwareThenSoftware
ERasterScheduling::HardwareAndSoftwareOverlap

// 光栅上下文
struct FRasterContext {
    FVector2f RcpViewSize;     // 视图尺寸倒数
    FIntPoint TextureSize;     // 纹理尺寸
    FRDGTextureRef DepthBuffer;   // 深度目标
    FRDGTextureRef VisBuffer64;   // 可见性缓冲区 (64-bit)
    // ...
};

// 光栅化结果
struct FRasterResults {
    FIntVector4 PageConstants;     // 页常量
    uint32 MaxVisibleClusters;     // 最大可见 Cluster 数
    FRDGBufferRef VisibleClustersSWHW; // 可见 Cluster 列表
    FRDGTextureRef VisBuffer64;    // 可见性缓冲区
    // ...
};
```

### Nanite 材质管线

- `BuildShadingCommands()` — 构建着色命令列表 (`NaniteShading.cpp`)
- `DispatchBasePass()` — 发射 Compute Shader 材质计算 (`NaniteShading.h`)
- 材质管线枚举: `ENaniteMeshPass::BasePass`, `LumenCardCapture`, `MaterialCache` (`PrimitiveSceneInfo.h:246`)
- `FShadeBinning` 结构包含 ShadingBinData、ShadingDispatchArgs、ThreadGroupData

### 关键 Console Variables

```cpp
r.Nanite.ComputeRasterization    // 软件光栅化开关 (NaniteCullRaster.cpp:79)
r.Nanite.AsyncRasterization      // 异步计算光栅化
r.Nanite.AsyncRasterization.ShadowDepths  // 阴影深度异步光栅化
r.SceneCulling                   // 场景裁剪（Nanite 启用时激活实例层级）
```

---

## Virtual Shadow Maps (VSM)

VSM 是专为 Nanite 设计的虚拟化阴影系统。

### 核心特性

- **基于页面的虚拟化**: 128x128 像素的 Shadow Page，按需分配和渲染
- **虚拟分辨率**: 16Kx16K，定向光源使用 Clipmap (6-22 级)
- **与 Nanite 深度集成**: Nanite 几何体可以单 Pass 渲染所有光源的阴影，绕过传统 DrawCall
- **帧间缓存**: Shadow Page 在帧间缓存，仅失效时重新渲染

### 渲染路径

```
阴影深度渲染:
├── Nanite 几何
│   ├── 所有方向光 → 单个 Nanite Pass
│   └── 所有局部光源 → 第二个 Nanite Pass
└── 非 Nanite 几何 → 逐光源、逐实例 DrawCall (传统路径)
```

非 Nanite 几何体过多时会触发 `VSM Non-Nanite Marking Job Queue overflow` 警告，可通过 `r.Shadow.Virtual.NonNanite.IncludeInCoarsePages 0` 缓解。

### 核心文件

- `VirtualShadowMapArray.cpp` — VSM 数组和页面分配
- `VirtualShadowMapCacheManager.cpp` — 缓存管理和失效
- `VirtualShadowMapClipmap.cpp` — 方向光 Clipmap 层级
- `VirtualShadowMapProjection.cpp` — 阴影投影到屏幕空间

---

## Lumen 全局光照

### 系统组成

```
Lumen 全局光照:
├── Surface Cache       — 表面缓存 (卡片表示)
├── Radiance Cache      — 辐射度缓存 (世界空间探针)
├── Screen Probe Gather — 屏幕探针收集 (默认 GI 方法)
├── ReSTIR Gather       — 替代 GI 方法
├── Reflections         — 反射 (支持 HWRT/Surface Cache)
└── Scene Lighting      — 直接光照评估
```

### GI 方法枚举

```cpp
EDiffuseIndirectMethod::Lumen   // Lumen GI (默认)
EDiffuseIndirectMethod::None    // 无 GI
```

### 关键入口

- `RenderLumenSceneLighting()` — 更新 Lumen 场景 + 光照 (`DeferredShadingRenderer.cpp:3041`)
- `RenderDiffuseIndirectAndAmbientOcclusion()` — GI + AO (`DeferredShadingRenderer.cpp`)

---

## MegaLights (UE5.8 新特性)

大量动态可投影光源系统，在 5.8 中正式可用。

### 核心文件 (`Private/MegaLights/`)

- `MegaLights.cpp` — 核心光源管理和分帧调度
- `MegaLightsRayTracing.cpp` — 硬件光追加速
- `MegaLightsSampling.cpp` — 光源重要性采样
- `MegaLightsDenoising.cpp` — 时序/空间降噪
- `MegaLightsResolve.cpp` — 最终合成
- `MegaLightsVisualize.cpp` — 调试可视化

### 管线集成

在 `Render()` 中：
1. `CreateMegaLightsFrameTemporaries()` — 创建帧临时资源
2. `DispatchAsyncMegaLightsVolumePasses()` — 异步体积 Pass
3. `GenerateMegaLightsSamples()` — 生成采样点
4. `RenderMegaLights()` — 最终渲染到 SceneColor

---

## UE5.8 渲染特性矩阵 (Desktop Deferred SM6)

| 特性 | 状态 |
|---|---|
| Nanite 虚拟几何 | ✅ 正式 |
| Lumen GI (Software RT) | ✅ 正式 |
| Lumen GI (Hardware RT) | ✅ 正式 |
| Virtual Shadow Maps | ✅ 正式 |
| Temporal Super Resolution | ✅ 正式 |
| MegaLights | ✅ 正式 (5.8) |
| Lumen Lite | ✅ 新增 (5.8) |
| Accumulation DOF | ✅ 正式 (5.8) |
| Movie Render Graph | ✅ 正式 (5.8) |
| Path Tracer | ✅ 正式 |
| 硬件光追 (HWRT) | ✅ 可选加速 |

---

## 关键 Console Variables

```cpp
// 延迟渲染
r.UseClusteredDeferredShading   // 聚簇延迟着色
r.EarlyZPass                    // 深度预通道模式 (DDM_None/DDM_AllOpaque/...)

// Nanite
r.Nanite                        // Nanite 主开关
r.Nanite.ComputeRasterization   // 软件光栅化
r.Nanite.AsyncRasterization     // 异步光栅化

// VSM
r.Shadow.Virtual.Enable         // VSM 开关
r.Shadow.Virtual.Cache.StaticSeparate  // 静/动分离缓存

// Lumen
r.Lumen.DiffuseIndirect.Allow   // Lumen GI 开关
r.Lumen.Reflections.Allow       // Lumen 反射开关

// MegaLights
r.MegaLights.Allow              // MegaLights 开关
r.MegaLights.Denoise            // MegaLights 降噪
```

---

## 编码约定

- 渲染代码遵循 UE 的 Render Dependency Graph (RDG) 模式：`FRDGBuilder& GraphBuilder` 贯穿所有 Pass
- 所有 Pass 通过 `SHADER_PARAMETER_STRUCT` 定义参数，`SHADER_PARAMETER_RDG_TEXTURE/BUFFER` 绑定资源
- Nanite 使用 `Nanite::` 命名空间，核心类型在 `NaniteShared.h`、`NaniteCullRaster.h` 中定义
- 传统几何遍历 `FMeshPassProcessor → FMeshDrawCommand` 路径；Nanite 绕过此路径

## 参考资料

- 官方文档: [UE5.8 渲染功能支持](https://dev.epicgames.com/documentation/unreal-engine/supported-features-by-rendering-path-for-desktop-with-unreal-engine)
- VSM 集成分析: [深入 UE5 Virtual Shadow Maps](https://wenku.csdn.net/column/grx18xxn1am)
- Nanite 源码对比: [UE5 Nanite 与传统渲染管线的深度源码对比](https://mr0ptimist.github.io/posts/ue/nanite/ue5-nanite-%E4%B8%8E%E4%BC%A0%E7%BB%9F%E6%B8%B2%E6%9F%93%E7%AE%A1%E7%BA%BF%E7%9A%84%E6%B7%B1%E5%BA%A6%E6%BA%90%E7%A0%81%E5%AF%B9%E6%AF%94/)
