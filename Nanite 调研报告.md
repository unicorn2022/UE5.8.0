# UE5.8 Nanite 虚拟几何系统渲染管线调研报告

## 1. 概述

Nanite 是 UE5 引入的虚拟几何系统，核心目标是将几何处理的主导权从 CPU 转移到 GPU，实现三角形数量不受传统 DrawCall 瓶颈限制的渲染管线。其基本思路是：

- **材质着色与可见性解耦**：先确定哪些三角形可见（生成 Visibility Buffer），再用 Compute Shader 对可见像素进行材质着色
- **GPU-Driven 裁剪**：由 GPU 而非 CPU 驱动整个裁剪流程，彻底消除 CPU 端 DrawCall 提交瓶颈
- **虚拟几何流送**：类似虚拟纹理按需加载的思路，只流送当前帧需要的 Cluster 数据

在 UE5.8 中，Nanite 已是一个成熟的正式系统，与 Deferred Shading、Virtual Shadow Maps、Lumen、MegaLights 等子系统深度集成。

## 2. 与传统管线的架构对比

```
传统管线:
  CPU: Frustum Cull → Occlusion Cull → Generate DrawCalls → Submit
  GPU: Vertex Shader → Raster → Pixel Shader (直接写 GBuffer)

Nanite 管线:
  CPU: 仅做轻量级预裁剪（Binning），生成 Candidate 列表
  GPU: Instance Cull → BVH Traverse → Cluster Cull → HZB Occlude
       → HW/SW Raster → VisBuffer → Material Bin → Compute Shade → GBuffer
```

| 维度 | 传统渲染 | Nanite |
|---|---|---|
| 几何裁剪 | CPU Frustum/Occlusion | GPU BVH Traversal + HZB Two-Pass |
| 光栅化 | 硬件光栅化 (VS→PS) | HW/SW 混合，含软件 Compute 光栅 |
| 中间输出 | 直接写 GBuffer | Visibility Buffer (64-bit) |
| 材质着色 | Pixel Shader per-triangle | Compute Shader per-screen-tile |
| DrawCall | FMeshDrawCommand (CPU) | GPU Indirect Dispatch |
| LOD | 手动创建 | 自动 Cluster 层级选择 |

## 3. 完整管线流程

### 3.1 入口与调度

入口函数：`FDeferredShadingSceneRenderer::RenderNanite()` (`DeferredShadingRenderer.cpp:1449`)

```
RenderNanite()
├── InitRasterContext (分配 VisBuffer + Depth 目标)
├── IRenderer::Create (创建 GPU 渲染器)
├── DrawGeometry (GPU 裁剪+光栅化)
├── ExtractResults (提取裁剪/光栅化结果)
├── EmitDepthTargets (从 VisBuffer 解析深度)
├── BuildShadingCommands (构建着色命令)
├── ShadeBinning (按材质分桶)
└── DispatchBasePass (Compute Shader 材质着色)
```

### 3.2 Phase 0: CPU 端预裁剪可见性

在执行 GPU 流水线之前，CPU 端会进行轻量级的预裁剪：

**`FNaniteVisibility`** (`NaniteVisibility.h:81`)

```cpp
class FNaniteVisibility {
    // 每个 Primitive 的 RasterBin 和 ShadingBin 引用
    struct FRasterBin { uint16 Primary; uint16 Fallback; };
    struct FShadingBin { uint16 Triangle; uint16 Voxel; uint16 Curve; };
    
    // 主入口：发起可见性查询（异步任务执行）
    BeginVisibilityQuery(Allocator, Scene, ViewList, RasterPipelines, ShadingPipelines, PrerequisiteTask);
};
```

这一步确定：
- 哪些 Primitive 的 RasterBin（光栅化批次）可见
- 哪些 Primitive 的 ShadingBin（着色批次）可见
- 通过 async task 并行执行，不阻塞主渲染线程

### 3.3 Phase 1: 光栅化上下文初始化

**`Nanite::InitRasterContext()`** (`NaniteCullRaster.h:144`)

分配核心渲染目标：
- **VisBuffer64**: `RWTexture2D<UlongType>` — 64 位可见性缓冲区，存储 [Depth, TriangleIndex, ClusterIndex, MaterialID, ...]
- **DepthBuffer**: `RWTexture2D<uint>` — 32 位深度缓冲区
- **DbgBuffer64/DbgBuffer32**: 调试缓冲区

两种光栅输出模式：
```cpp
enum class EOutputBufferMode : uint8 {
    VisBuffer,  // 默认：同时输出 ID 和深度
    DepthOnly,  // 仅输出 32 位深度（用于阴影）
};
```

### 3.4 Phase 2: GPU 裁剪流水线（DrawGeometry）

入口：`FRenderer::DrawGeometry()` (`NaniteCullRaster.cpp:6731`)

这是 Nanite 最核心的 GPU 计算阶段。

#### 3.4.1 裁剪 Pass 分类

```cpp
#define CULLING_PASS_NO_OCCLUSION     0  // 无遮挡裁剪（单 Pass）
#define CULLING_PASS_OCCLUSION_MAIN   1  // 主遮挡裁剪 Pass
#define CULLING_PASS_OCCLUSION_POST   2  // 后遮挡裁剪 Pass（两阶段遮挡）
#define CULLING_PASS_EXPLICIT_LIST    3  // 显式列表裁剪（VSM/Lumen）
```

#### 3.4.2 完整 GPU 裁剪链

```
DrawGeometry()
│
├── 1. InitArgs (FInitArgs_CS)
│     初始化全局队列状态、间接绘制参数
│
├── 2. AddPass_PrimitiveFilter()
│     GPU 端 Primitive 级别过滤（NanitePrimitiveFilter.usf）
│     过滤隐藏、EditorOnly、ForceHidden 等对象
│
├── 3. [SceneCulling] Instance Hierarchy Culling
│     └── SceneCulling 构建实例层级并执行 GPU 实例裁剪
│     └── 如果禁用 SceneCulling，则对所有场景实例暴力遍历
│
├── 4. AddPass_InstanceHierarchyAndClusterCull(CULLING_PASS_OCCLUSION_MAIN)
│     │
│     ├── 4a. Instance Culling (NaniteInstanceCulling.usf)
│     │     检测每个实例的可见性，将可见实例的 BVH 根节点追加到
│     │     CandidateNodesAndClusters 工作队列
│     │
│     ├── 4b. BVH Node Culling
│     │     使用 Persistent Threads 遍历 BVH:
│     │     - 每个线程组处理最多 8 个 BVH 节点
│     │     - 每个节点最多 8 个子节点 (MAX_BVH_NODE_FANOUT = 8)
│     │     - 64 个线程并行处理 64 个子节点
│     │     - 视锥裁剪 (ShouldVisitChild)
│     │     - 可见内部节点 → 追加回 CandidateNodesAndClusters
│     │     - 可见叶节点 → 产生 Cluster 条目
│     │     - MPMC (Multiple Producer Multiple Consumer) 作业队列
│     │     持续消费/生产直到所有节点处理完毕
│     │
│     └── 4c. Cluster Culling
│           以 PERSISTENT_CLUSTER_CULLING_GROUP_SIZE 批处理 Cluster
│           优化：仅写每批第一个 Cluster 到全局内存
│           后续 Cluster 从线程索引推导（大幅减少内存写入）
│
├── 5. AddPass_Rasterize() — 主 Pass（CULLING_PASS_OCCLUSION_MAIN）
│     │
│     ├── 硬件光栅化 (FHWRasterizeVS + FHWRasterizePS)
│     │     - 三角形边长 ≥ r.Nanite.MinPixelsPerEdgeHW (默认 32px) → HW 路径
│     │     - 支持 Primitive Shader / Mesh Shader 路径
│     │     - 深度写入 VisBuffer / DepthBuffer
│     │
│     └── 软件光栅化 (FMicropolyRasterizeCS)
│           - 三角形边长 < MinPixelsPerEdgeHW → Compute 路径
│           - NaniteRasterizer.usf: MicropolyRasterize
│           - 支持 Work Graphs (FMicropolyRasterizeWG)
│           - 支持 Tessellation (可编程细分)
│           - 子像素三角形高效处理
│
├── 6. BuildPreviousOccluderHZB (两阶段遮挡)
│     使用上一帧遮挡物构建 HZB（层级 Z-Buffer）
│     - 或使用 VSM 的 HZB 物理数组
│
├── 7. AddPass_InstanceHierarchyAndClusterCull(CULLING_PASS_OCCLUSION_POST)
│     重测试上一帧被遮挡的节点/Cluster
│     使用新构建的 HZB 进行遮挡测试
│     HZB 多 Mip 级别比较: IsVisibleHZB (NaniteHZBCull.ush)
│
└── 8. AddPass_Rasterize() — 后 Pass（CULLING_PASS_OCCLUSION_POST）
      渲染之前被遮挡、现在变为可见的几何体
```

#### 3.4.3 光栅化调度策略

```cpp
enum class ERasterScheduling : uint8 {
    HardwareOnly              = 0,  // 仅硬件光栅化
    HardwareThenSoftware      = 1,  // 先硬件后软件（串行）
    HardwareAndSoftwareOverlap = 2,  // 硬件+软件重叠执行（高级）
};
```

软件光栅化由 `r.Nanite.ComputeRasterization` 控制，可选异步计算执行 (`r.Nanite.AsyncRasterization`)。

#### 3.4.4 HZB 层遮挡剔除

```cpp
// GPU 中执行:
IsVisibleHZB(clusterBBox, HZBTexture, HZBSize)
    → 比较 cluster BBox 与多级 HZB Mip 的深度值
    → 返回是否可见
```

### 3.5 Phase 3: 深度导出

**`Nanite::EmitDepthTargets()`** (`Nanite.cpp`)

从 VisBuffer 解析深度值，写入场景深度缓冲区（SceneDepth）：
- `FEmitSceneDepthPS` (Pixel Shader) — 从 VisBuffer64 解码深度
- 支持 Velocity Export（运动矢量导出）
- 支持 Shading Mask Export
- 支持 Skinning（蒙皮）

### 3.6 Phase 4: 材质着色

这是 Nanite 区别于传统管线的第二个关键创新——使用 Compute Shader 而非 Pixel Shader 进行材质评估。

#### 3.6.1 构建着色命令

**`BuildShadingCommands()`** (`NaniteShading.h:50`)

为每种材质-光照组合生成 Compute Shader 着色管线：
- `FNaniteShadingPipeline` — 包含 Compute Shader + PSO + 绑定参数
- `LoadBasePassPipeline()` — 为每个材质 Section 加载基础通道管线
- `LoadLumenCardPipeline()` — 为 Lumen 卡片捕获加载管线

#### 3.6.2 屏幕空间分桶

**`ShadeBinning()`** (`NaniteShading.h:34`)

这是一个 Compute Shader Pass，对 Visibility Buffer 中每个像素：
1. 读取 VisBuffer64 → 解码 MaterialID
2. 将像素按材质分组（Binning）
3. 生成间接 Dispatch Args（每种材质一个 Dispatch）

```cpp
struct FShadeBinning {
    FRDGBufferRef ShadingBinData;       // 材质 Bin 数据
    FRDGBufferRef ShadingBinStats;      // Bin 统计信息
    FRDGBufferRef ShadingDispatchArgs;  // 间接 Dispatch 参数
    FRDGBufferRef ThreadGroupData;      // 线程组数据
};
```

#### 3.6.3 材质评估 Dispatch

**`DispatchBasePass()`** (`NaniteShading.h:82`)

使用 Indirect Dispatch，每种材质一个 Compute Shader Group：
- 计算 Pixel → GBuffer (A, B, C, D, E 等)
- 支持 Substrate 材质系统
- 输出与通道光栅化路径完全一致

### 3.7 Phase 5: 合成

**`NaniteComposition.cpp`**

- `FEmitSceneDepthPS` — 将 Nanite VisBuffer 深度写入 SceneDepth
- HTile 重摘要 (`r.Nanite.ResummarizeHTile`) — 更新 Hi-Z 层级结构

### 3.8 多用途渲染

| 用途 | 入口 | 说明 |
|---|---|---|
| **阴影** (VSM) | `EmitShadowMap`, `EmitCubemapShadow` | 所有方向光单 Pass，所有局部光源第二个 Pass |
| **Lumen 卡片** | `LumenCardNanite` MeshPass | 捕获表面缓存卡片 |
| **半透明** | `NaniteTranslucency.cpp` | Nanite 半透明渲染 |
| **光追** | `NaniteRayTracing.cpp` | RT 几何体更新和 BLAS 管理 |
| **材质缓存** | `MaterialCache` MeshPass | 离线材质烘焙加速 |
| **体素** | `Voxel.cpp` | Nanite 体素表示 |
| **可视化** | `NaniteVisualize.cpp` | Overdraw、Cluster、Assembly 等调试视图 |

## 4. 关键数据结构

### 4.1 FPackedView (`NaniteShared.h:59`)

每个 GPU 视图的核心打包结构，包含：变换矩阵、ViewRect、HZBTestViewRect、LODScales（基于 `r.Nanite.MaxPixelsPerEdge`）、StreamingPriorityCategory 等。

### 4.2 FRasterContext (`NaniteCullRaster.h:57`)

```cpp
struct FRasterContext {
    FVector2f           RcpViewSize;
    FIntPoint           TextureSize;
    EOutputBufferMode   RasterMode;
    ERasterScheduling   RasterScheduling;
    FRDGTextureRef      DepthBuffer;
    FRDGTextureRef      VisBuffer64;
};
```

### 4.3 FRasterResults (`NaniteCullRaster.h:77`)

```cpp
struct FRasterResults {
    FIntVector4     PageConstants;
    uint32          MaxVisibleClusters;
    uint32          MaxNodes;
    FRDGBufferRef   ViewsBuffer;
    FRDGBufferRef   VisibleClustersSWHW;
    FRDGBufferRef   RasterBinMeta;
    FRDGBufferRef   RasterBinArgs;
    FRDGTextureRef  VisBuffer64;
    FRDGTextureRef  ShadingMask;
    FNaniteVisibilityQuery* VisibilityQuery;
};
```

### 4.4 FConfiguration (`NaniteCullRaster.h:152`)

```cpp
struct FConfiguration {
    uint32 bTwoPassOcclusion    : 1;  // 两阶段遮挡剔除
    uint32 bUpdateStreaming     : 1;  // 更新流送
    uint32 bPrimaryContext      : 1;  // 主渲染上下文
    uint32 bForceHWRaster       : 1;  // 强制硬件光栅化
    uint32 bIsShadowPass        : 1;  // 阴影 Pass
    uint32 bIsSceneCapture      : 1;  // 场景捕获
    uint32 bIsLumenCapture      : 1;  // Lumen 捕获
    uint32 bDisableProgrammable : 1;  // 禁用可编程光栅化
    // ... 更多标志位
};
```

## 5. 与虚拟阴影贴图 (VSM) 的集成

### 5.1 阴影渲染效率

```
VSM 阴影深度渲染:
  Nanite 几何:
    ├── 所有方向光 → 单个 Nanite Pass (单次 DrawGeometry 覆盖所有视图)
    └── 所有局部光源 → 第二个 Nanite Pass
  非 Nanite 几何:
    └── 逐光源、逐实例 → 传统 DrawCall 路径（费时）
```

### 5.2 缓存与失效

VSM 的 Shadow Page（128x128 像素）在帧间缓存：
- 光源移动/旋转 → 所有缓存页失效
- Nanite Cluster LOD 流送发生变化 → 对应区域失效
- 静态/动态分离缓存 (`r.Shadow.Virtual.Cache.StaticSeparate`)

## 6. 几何流送系统

```
NaniteStreamingManager:
  ├── BeginAsyncUpdate()    — 开始异步流送更新
  ├── EndAsyncUpdate()      — 结束异步流送更新
  ├── GetAndClearModifiedResources()  — 获取变更的资源
  └── GetAndClearModifiedPages()      — 获取变更的页面
```

- 流送请求在 GPU 裁剪阶段生成（识别可见 Cluster 的 Page Range）
- CPU 通过 `Nanite::Readback` 回读并处理流送请求
- SSD 直接流送到 GPU 内存

## 7. 光照与反射集成

### 7.1 Lumen 集成

- `DispatchLumenMeshCapturePass()` — 使用 Nanite 光栅化结果捕获 Lumen 卡片
- Lumen Scene Lighting 使用 GPU Scene 数据（由 Nanite 共享）

### 7.2 光线追踪集成

```cpp
Nanite::GRayTracingManager:
  ├── UpdateUniformBuffer()  — 更新 RT Uniform Buffer
  ├── RequestUpdates()       — 请求 BLAS 更新
  └── ProcessBuildRequests() — 处理构建请求
```

## 8. 关键 Console Variables

### 8.1 核心控制

| CVar | 默认值 | 说明 |
|---|---|---|
| `r.Nanite` | 1 | Nanite 主开关 |
| `r.Nanite.ComputeRasterization` | 1 | 软件光栅化开关 |
| `r.Nanite.ProgrammableRaster` | 1 | 可编程光栅化 |
| `r.Nanite.Tessellation` | 1 | 运行时细分 |


### 8.2 性能调优

| CVar | 默认值 | 说明 |
|---|---|---|
| `r.Nanite.MaxPixelsPerEdge` | 1.0 | 目标三角形边长（像素），越小越精细 |
| `r.Nanite.MinPixelsPerEdgeHW` | 32.0 | 硬件光栅化最小边长阈值 |
| `r.Nanite.DicingRate` | 2.0 | 微多边形细分大小 |
| `r.Nanite.AsyncRasterization` | 1 | Async Compute 光栅化 |

### 8.3 调试与可视化

| CVar | 默认值 | 说明 |
|---|---|---|
| `r.Nanite.ShowStats` | 0 | 显示统计信息 |
| `r.Nanite.ResummarizeHTile` | 1 | 深度合成后重摘要 HTile |
| `r.Nanite.VSMInvalidateOnLODDelta` | 0 | LOD 变化触发 VSM 失效 |

## 9. 核心源码文件索引

```
Renderer/Private/Nanite/
├── NaniteCullRaster.cpp    — GPU 裁剪 + HW/SW 光栅化（最大文件，~7000+ 行）
├── NaniteCullRaster.h      — FRasterContext, FRasterResults, IRenderer
├── NaniteShading.cpp       — 材质管线加载 + ShadeBinning + Compute Shader 着色
├── NaniteShading.h         — FShadeBinning, BuildShadingCommands, DispatchBasePass
├── NaniteShared.h          — FPackedView, FPackedViewArray, 全局常量声明
├── NaniteVisibility.h      — FNaniteVisibility, FNaniteVisibilityQuery (CPU 预裁剪)
├── Nanite.cpp              — EmitDepthTargets, EmitShadowMap, EmitCubemapShadow
├── NaniteComposition.cpp   — 深度合成、HTile 重摘要、自定义深度导出
├── NaniteMaterials.cpp     — 材质管线缓存和查找
├── NaniteRayTracing.cpp    — RT BLAS 管理和几何体更新
├── NaniteTranslucency.cpp  — 半透明渲染
├── NaniteVisualize.cpp     — 调试可视化模式
├── NaniteFeedback.cpp      — GPU Feedback 回读
├── NaniteStreamOut.cpp     — 流送淘汰管理
├── Voxel.cpp               — Nanite 体素表示
└── TessellationTable.cpp   — 细分表（PN Triangles 等）

Renderer/Public/:
├── MeshPassProcessor.h     — EMeshPass::Type (含 NaniteMeshPass, LumenCardNanite)
└── PrimitiveSceneInfo.h    — ENaniteMeshPass (BasePass/LumenCardCapture/MaterialCache)
```

## 10. 性能要点总结

1. **GPU 裁剪开销与三角形数量解耦**：得益于 BVH 层级结构和 Persistent Culling，裁剪 100 万三角形和 1000 万三角形的开销差别不大——关键是 BVH 的深度而非三角形总数

2. **光栅化策略自适应**：大三角形走硬件路径（高吞吐），小三角形走软件路径（避免 Quad Overdraw / Sub-pixel 浪费），`r.Nanite.MinPixelsPerEdgeHW` 控制阈值

3. **材质着色成本与屏幕像素成比例**：因为 Binning + Indirect Dispatch 只处理可见像素，额外的三角形不会增加材质评估成本

4. **Two-Pass 遮挡的意义**：单 Pass 遮挡会遇到"鸡蛋问题"（某些遮挡物在当前帧才可见）；Two-Pass 通过复用上一帧的遮挡数据解决了这个问题

5. **VSM 集成是 Nanite 阴影方案的最佳路径**：非 Nanite 几何体放入 VSM 会引发严重的降级性能问题

## 11. 参考资料

- [UE5.8 Supported Features by Rendering Path](https://dev.epicgames.com/documentation/unreal-engine/supported-features-by-rendering-path-for-desktop-with-unreal-engine)
- [UE5 Nanite 与传统渲染管线的深度源码对比](https://mr0ptimist.github.io/posts/ue/nanite/ue5-nanite-%E4%B8%8E%E4%BC%A0%E7%BB%9F%E6%B8%B2%E6%9F%93%E7%AE%A1%E7%BA%BF%E7%9A%84%E6%B7%B1%E5%BA%A6%E6%BA%90%E7%A0%81%E5%AF%B9%E6%AF%94/)
- [深入 UE5 Virtual Shadow Maps](https://wenku.csdn.net/column/grx18xxn1am)
- [UE5 Nanite 源码解析之渲染篇：BVH 与 Cluster 的 Culling](https://zhuanlan.zhihu.com/p/376786001)
- [UE5 Nanite System (PDF)](https://raw.githubusercontent.com/AndrewAltimit/Media/main/ue5_nanite_system.pdf)
