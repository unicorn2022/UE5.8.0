# RDG 与多线程页面拆分 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 将占位页面 `09-RDG与多线程` 拆分为 `09-RDG`（12 章极致深度）和 `12-多线程与RHI`（9 章标准深度）两个独立 HTML 页面，并更新首页链接。

**架构：** 纯静态 HTML 文档站，单文件每页面约 800-1200 行。沿用 `04-材质` 页面的 CSS 体系（共享 CSS 变量、组件样式），每页面包含SVG 流程图、代码块（C++/HLSL 语法高亮）、信息表格、CVar 速查表、源码索引。

**技术栈：** HTML5 + 内联 CSS + 内联 SVG

---

## 文件结构

| 操作 | 路径 | 职责 |
|------|------|------|
| 重命名 | `UE5.8渲染流程解读/part/09-RDG与多线程/` → `09-RDG/` | 目录重命名 |
| 重写 | `UE5.8渲染流程解读/part/09-RDG/index.html` | RDG 12 章完整页面 |
| 新建 | `UE5.8渲染流程解读/part/12-多线程与RHI/index.html` | 多线程+RHI 9 章页面 |
| 修改 | `UE5.8渲染流程解读/index.html` | 更新卡片、目录表、阅读路径链接 |

CSS 样式完全复用 `04-材质/index.html` 的 CSS 块（L7-L144），不再重复写出。每个页面的 CSS 直接从 04-材质 页复制，然后按需微调。

---

### 任务 1：重命名目录并提交骨架

**文件：**
- 重命名：`UE5.8渲染流程解读/part/09-RDG与多线程/` → `UE5.8渲染流程解读/part/09-RDG/`
- 重写：`UE5.8渲染流程解读/part/09-RDG/index.html`（新骨架，占位内容替换为完整页面）

- [ ] **步骤 1：git mv 重命名目录**

```bash
git -C "D:\project_ue5\ue5.8.0-docs" mv "UE5.8渲染流程解读/part/09-RDG与多线程" "UE5.8渲染流程解读/part/09-RDG"
```

- [ ] **步骤 2：创建 12-多线程与RHI 目录**

```bash
mkdir "D:\project_ue5\ue5.8.0-docs\UE5.8渲染流程解读\part\12-多线程与RHI"
```

- [ ] **步骤 3：写入 RDG 页面基础骨架（Hero + 面包屑，正文待填充）**

写入 `UE5.8渲染流程解读/part/09-RDG/index.html`，文件结构：
- 复用 04-材质 页面的完整 CSS 块
- 面包屑导航：`首页 > 管线总览 > RDG`
- Hero 区域：标题 `RDG<span>渲染依赖图</span>`，副标题描述 FRDGBuilder 核心功能，标签列表
- 正文：12 个 `<section class="section-block" id="...">` 占位（每个含 h2 标题和 p 描述，等待后续任务填充）
- Footer

- [ ] **步骤 4：写入 多线程与RHI 页面基础骨架**

写入 `UE5.8渲染流程解读/part/12-多线程与RHI/index.html`，结构同上：
- 面包屑：`首页 > 管线总览 > 多线程与RHI`
- Hero：标题 `多线程<span>与RHI</span>`
- 正文：9 个 section 占位
- Footer

- [ ] **步骤 5：验证文件存在并 commit**

```bash
ls -la "D:\project_ue5\ue5.8.0-docs\UE5.8渲染流程解读\part\09-RDG\index.html"
ls -la "D:\project_ue5\ue5.8.0-docs\UE5.8渲染流程解读\part\12-多线程与RHI\index.html"
```

```bash
git -C "D:\project_ue5\ue5.8.0-docs" add -A
git -C "D:\project_ue5\ue5.8.0-docs" commit -m "$(cat <<'EOF'
拆分 09-RDG与多线程 为独立骨架：09-RDG + 12-多线程与RHI

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

### 任务 2：RDG 第 1-3 章节 — 管线位置 + 核心概念 + Pass 注册

**文件：** 修改 `UE5.8渲染流程解读/part/09-RDG/index.html`

#### 章节 1：管线中的位置

- [ ] 填充 `#pipeline-position` section，包含：

**内容要点：**
- FRDGBuilder 在 `Render()` 函数中 L1829 附近创建：`FRDGBuilder GraphBuilder(RHICmdList, ...)`
- 创建时机：管线段 1（视图与管线初始化）中创建
- 执行时机：`GraphBuilder.Execute()` 在管线段 9（收尾）前调用，一次性提交所有 Pass 到 GPU
- RDG 覆盖范围：管线段 4-8 的所有 Pass 都通过 GraphBuilder 注册
- 在构造后立即预热 Blackboard：`Substrate::InitialiseSubstrateFrameSceneData(GraphBuilder, ...)`

**SVG 元素：** RDG Builder 生命周期时间线图 — 标注 Builder 构造、Pass 注册期、Execute 提交点在整个九阶段帧中的位置

**表格：** Render 函数中 RDG 相关关键行号

**callout：** RDG 的核心价值——将命令式资源管理变为声明式依赖图

#### 章节 2：核心概念

- [ ] 填充 `#core-concepts` section，包含：

**三大原语卡片网格：**

1. **FRDGBuilder** — 依赖图构建器
   - 源码：`RenderGraphBuilder.h`
   - 职责：注册 Pass、创建资源、构建依赖图、在 Execute 时调度执行
   - 单帧生命周期：构造 → AddPass/CreateTexture → Execute → 析构
   - 关键方法：`AddPass()`、`CreateTexture()`、`CreateBuffer()`、`QueueTextureExtraction()`

2. **FRDGTextureRef / FRDGBufferRef** — 图资源句柄
   - 编译时引用计数句柄，不是实际 GPU 资源
   - 仅在 Execute 时实际分配（Transient 池化）
   - 不可跨帧持有（析构后失效）
   - 使用 `QueueTextureExtraction` 将资源移出 RDG 图

3. **RDG Pass** — 渲染单元
   - 三种类型：Raster（`ERDGPassFlags::Raster`）、Compute（`ERDGPassFlags::Compute`）、AsyncCompute、Copy
   - Pass 声明包含：参数结构体 + 执行 Lambda + Flags
   - Pass 间通过资源引用隐式建立依赖关系

**访问声明概念：** 每个 Pass 声明对资源的访问方式（Read/Write），RDG 据此判定依赖和插入屏障

**code block：** FRDGBuilder 创建和 Execute 的典型代码模式（简化版）

#### 章节 3：Pass 注册与执行

- [ ] 填充 `#pass-registration` section，包含：

**AddPass 内部流程：**
1. Lambda 参数捕获 → 编译时 `TParameterStructTypeInfo` 类型推导
2. 遍历参数结构体反射信息，注册每个参数（Texture SRV/UAV、Buffer SRV/UAV、RenderTarget）
3. 根据访问方式建立资源依赖边
4. 当所有 Pass 注册完毕，Execute 调用 `Compile()` 进行拓扑排序
5. 按排序结果依次 Dispatch Pass Lambda 到 RHI 命令列表

**拓扑排序与无用 Pass 裁剪：**
- 只有对最终输出（`QueueTextureExtraction` 的资源）有贡献的 Pass 才会执行
- 未连接的 Pass（纯副作用且无下游消费）会被 RDG 自动剔除（Cull）
- 最终 SceneColor/SceneDepth 外提 Pass 不会被裁剪

**code block：** `AddPass` 调用示例——Raster Pass 和 Compute Pass 各一例

**call-chain：** AddPass → SetupParameterStruct → 遍历 Resources → RegisterRead/Write → AddEdge

---

### 任务 3：RDG 第 4-6 章节 — 资源生命周期 + Transient 分配 + 依赖与屏障

**文件：** 修改 `UE5.8渲染流程解读/part/09-RDG/index.html`

#### 章节 4：资源生命周期

- [ ] 填充 `#resource-lifecycle` section，包含：

**状态机图（SVG）：** Create（注册）→ Register（到图）→ Allocate（Execute 时实际分配）→ Use（Pass 内访问）→ Release（图析构时释放）

**External 资源注册：**
- `RegisterExternalTexture(SceneTextures.Color.Target)` — 将已有池化纹理注册到 RDG
- External 资源不归 RDG 管理生命周期，仅借用引用
- 必须通过 `QueueTextureExtraction` 将 RDG 内部纹理提取回外部

**Upload/Readback 机制：**
- `AddUploadPass` — 将 CPU 数据上传到 GPU Buffer
- `AddReadbackPass` — 从 GPU Buffer 回读到 CPU（异步，需 Fence 等待）
- `FRDGBuilder::QueueBufferExtraction` — 提取后在 Execute 后仍可用

**code block：** External 纹理注册 + QueueTextureExtraction 完整示例

#### 章节 5：Transient 资源分配

- [ ] 填充 `#transient-allocation` section，包含：

**核心概念：**
- 声明 vs 实际分配分离
- TransientResourceAllocator（`TransientResourceAllocator.cpp`）在 Execute 时决策
- 分配策略：堆分配（大纹理/多帧共享）vs 独立分配（小纹理/单帧使用）

**别名分析（Aliasing）：**
- 两个不重叠生命周期的 Pass 可以使用同一块 GPU 内存
- RDG 通过资源依赖图分析 Pass 间资源的存活区间
- 无依赖、生命周期不重叠的资源自动别名到相同内存
- 别名分析显著降低 GPU 显存峰值占用

**SVG 元素：** 别名分析对比图 — 展示三个 Pass 引用四个资源时，别名分析如何将两个资源合并到同一块内存

**堆分配策略：**
- `FRHITransientHeapAllocator` — 为大纹理提供环形缓冲区式堆分配
- `FRHITransientResourceHeapAllocator` — 为小资源提供页式分配

**CVar 速查（提前引用）：** `r.TransientAllocator.*` 控制分配行为

#### 章节 6：依赖分析与自动屏障

- [ ] 填充 `#dependency-barriers` section，包含：

**依赖图构建流程（SVG）：**
资源引用 → 读写分析 → Pass 依赖边 → DAG → 拓扑排序 → 屏障插入

**自动屏障插入逻辑：**
- Pass A 写入 UAV → Pass B 读取 SRV → RDG 自动插入 UAV→SRV 转换屏障
- Pass A 作为 RTV 写入 → Pass B 作为 SRV 读取 → 自动插入 RTV→SRV 转换
- 同一 Pass 内多次访问同一资源不插入屏障（隐式串行）

**底层 D3D12 ResourceBarrier 生成：**
- RDG 屏障 → 翻译为 RHI 抽象 Barrier → D3D12/Vulkan 具体 Barrier
- 支持 Barrier 批处理（Batching）：相邻 Barrier 合并为数组提交
- Split Barrier 机制：Begin/End 分离以提升并行粒度

**code block：** 展示一个三 Pass 依赖链的 RDG 调试输出（屏障插入前后对比）

**callout：** RDG 的屏障系统是 UE 相对于手写 RHI 代码的最大生产力提升之一

---

### 任务 4：RDG 第 7-9 章节 — 自定义 Pass + 验证层 + 调试

**文件：** 修改 `UE5.8渲染流程解读/part/09-RDG/index.html`

#### 章节 7：自定义 RDG Pass 完整模板

- [ ] 填充 `#custom-pass` section，包含：

**光栅化 Pass 模板：**
```
1. 定义 FScreenPassTexture 输入/输出
2. 创建 Pixel Shader（.usf）+ FGlobalShader 类
3. 定义 SHADER_PARAMETER_STRUCT
4. GraphBuilder.AddPass() 注册 Raster Pass
5. 在 Lambda 中设置 PSO + DrawFullScreenTriangle
```

**Compute Pass 模板：**
```
1. 定义 SHADER_PARAMETER_STRUCT（含 UAV SRV 声明）
2. FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME, ComputeShader, Parameters, GroupCount)
```

**RDG 黑板通信：**
- `FRDGBlackboard` — 跨 Pass 传递元数据（非 GPU 资源）
- 用法：`GraphBuilder.Blackboard.Create<FMyData>()` / `GraphBuilder.Blackboard.Get<FMyData>()`
- 适用于控制标志、统计数据等非渲染数据

**CVar 控制开关：** 用 `TAutoConsoleVariable<int32>` 控制自定义 Pass 的启用/禁用

**完整 code block：** 一个端到端的自定义 Compute Pass 示例（含 .usf + C++）

#### 章节 8：RDG 验证层

- [ ] 填充 `#validation` section，包含：

**启用方式：** `-rdgdebug` 命令行参数或 `r.RDG.Debug` CVar

**三个验证级别：**
1. **Verbose（1）** — 打印 Pass 列表、资源使用摘要
2. **Extended（2）** — 资源生命周期追踪、未使用资源警告
3. **Max（3）** — 所有 Pass Lambda 均在 RHI 线程验证模式执行，检查所有访问

**常见检测到的错误：**
- 资源未声明访问（Resource not registered）
- 循环依赖（Cycle detected）
- Pass 间资源读写冲突（Read-After-Write hazard）
- 未使用资源（Unreferenced resource）

**code block：** 验证层报错示例输出（Resource not registered 错误信息）

#### 章节 9：RDG Dump 与调试

- [ ] 填充 `#dump-debug` section，包含：

**GraphViz 导出：**
- `r.RDG.DumpGraph=1` — 导出依赖图为 DOT 格式
- 生成 GraphViz PNG/SVG 可视化
- 每个节点 = 一个 Pass，每条边 = 资源依赖

**Unreal Insights 集成：**
- RDG Pass 自动生成 Insights Trace Event
- 时间线上可看到每个 Pass 的 CPU 注册时间和 GPU 执行时间
- 通过 `RDG_EVENT_NAME` 宏给 Pass 命名

**常见瓶颈诊断：**
- Pass 注册 CPU 开销过高 → 合并微小 Pass
- 屏障风暴 → 检查不必要的 UAV→SRV 转换
- Transient 分配峰值 → 检查是否存在生命周期异常延长的资源

**SVG 元素：** RDG 依赖图 Dump 输出示例

---

### 任务 5：RDG 第 10-12 章节 — 性能/陷阱 + CVar + 源码索引

**文件：** 修改 `UE5.8渲染流程解读/part/09-RDG/index.html`

#### 章节 10：性能与陷阱

- [ ] 填充 `#performance-pitfalls` section，包含：

**RDG 开销分析：**
- Pass 注册 CPU 开销：参数结构体反射遍历 + 依赖边建立（约 5-50μs/Pass）
- Execute 阶段：拓扑排序（约 1-5ms/1000 Pass）+ RHI 命令提交
- 典型帧：200-500 RDG Pass，CPU 总开销 2-5ms

**Lambda 捕获注意事项：**
- 按引用捕获 RDG 资源（`[&]`）→ 必须确保 Execute 时资源仍有效
- 按值捕获大对象 → 堆分配开销
- 推荐：透传 Parameters 结构体指针，减少强引用

**大纹理 Upload 最佳实践：**
- 避免同步 Upload → 使用 RDG Upload Pass（异步）
- 分块上传大纹理（Tile Upload）
- Streaming 纹理优先使用 External Texture 注册

**信息卡片：** 常见 RDG 性能反模式列表（过度细粒度 Pass、不必要的 Readback、同步等待）

#### 章节 11：CVar 速查

- [ ] 填充 `#cvar-reference` section，包含：

**CVar 表格（cvar-table 样式）：**

| CVar | 默认值 | 说明 |
|------|--------|------|
| `r.RDG.Debug` | 0 | 调试模式（1-3 级别） |
| `r.RDG.DumpGraph` | 0 | 导出依赖图为 GraphViz DOT |
| `r.RDG.DumpGraphUnknowns` | 0 | 导出未知资源依赖 |
| `r.RDG.ClobberResources` | 0 | 用调试值填充未初始化资源 |
| `r.RDG.ImmediateMode` | 0 | Immediate Mode（调试用，逐 Pass 立即执行） |
| `r.RDG.CullPasses` | 1 | 裁剪未输出 Pass |
| `r.RDG.OverlapUAVs` | 1 | 允许 UAV 重叠（别名分析启用） |
| `r.TransientAllocator.*` | — | Transient 分配器控制（子项展开） |

#### 章节 12：核心源码索引

- [ ] 填充 `#source-index` section，包含（source-item 样式）：

| 文件 | 关键类/函数 |
|------|------------|
| `RenderCore/Public/RenderGraphBuilder.h` | `FRDGBuilder`（主类）、`AddPass()`、`CreateTexture()`、`Execute()` |
| `RenderCore/Private/RenderGraphBuilder.cpp` | `Compile()`、`Execute()` 实现 |
| `RenderCore/Public/RenderGraphResources.h` | `FRDGTextureRef`、`FRDGBufferRef`、`FRDGViewRef` |
| `RenderCore/Public/RenderGraphUtils.h` | `FComputeShaderUtils::AddPass()`、`FPixelShaderUtils::AddFullscreenPass()` |
| `RenderCore/Public/RenderGraphBlackboard.h` | `FRDGBlackboard` |
| `RenderCore/Public/RenderGraphValidation.h` | `ERDGValidationLevel` |
| `RenderCore/Private/RenderGraphValidation.cpp` | 验证层实现 |
| `RenderCore/Public/RenderGraphPass.h` | `FRDGPass`、`ERDGPassFlags` |
| `RHICore/Public/TransientResourceAllocator.h` | `FRHITransientResourceAllocator` |
| `RHICore/Private/TransientResourceAllocator.cpp` | 别名分析 + 堆分配实现 |

- [ ] **步骤 5：验证页面完整性并 commit**

```bash
wc -l "D:\project_ue5\ue5.8.0-docs\UE5.8渲染流程解读\part\09-RDG\index.html"
# 预期 > 800 行
```

```bash
git -C "D:\project_ue5\ue5.8.0-docs" add "UE5.8渲染流程解读/part/09-RDG/index.html"
git -C "D:\project_ue5\ue5.8.0-docs" commit -m "$(cat <<'EOF'
RDG 页面：12 章节极致深度解读（FRDGBuilder 全流程 + Transient + 屏障 + 调试）

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

### 任务 6：多线程与RHI 第 1-3 章节 — 管线位置 + 三线程模型 + TaskGraph

**文件：** 修改 `UE5.8渲染流程解读/part/12-多线程与RHI/index.html`

#### 章节 1：管线中的位置

- [ ] 填充 `#pipeline-position` section，包含：

**内容要点：**
- 渲染管线的三条关键线程：GameThread（主逻辑/可见性）→ RenderThread（RDG Pass 执行）→ RHIThread（GPU 命令转换）
- 线程间数据流：GameThread 提交 `ENQUEUE_RENDER_COMMAND` → RenderThread 构建 RDG → RHIThread 录制 D3D12 CommandList
- 在 Render 函数中的关键交互节点：`InitViews`（GameThread → RenderThread）、`GraphBuilder.Execute()`（RenderThread → RHIThread）
- 第四线程：AsyncCompute（独立队列，RDG 中通过 `ERDGPassFlags::AsyncCompute` 使用）

**SVG 元素：** 渲染管线各阶段中的线程交互图（三列：GameThread / RenderThread / RHIThread，标注各阶段的并行/串行关系）

#### 章节 2：三线程模型

- [ ] 填充 `#three-thread-model` section，包含：

**线程职责边界：**

| 线程 | 职责 | 关键入口 |
|------|------|---------|
| GameThread | 游戏逻辑、物理、可见性计算提交 | `ENQUEUE_RENDER_COMMAND`、`FlushRenderingCommands` |
| RenderThread | RDG 构建+执行、DrawCall 提交 | `FDeferredShadingSceneRenderer::Render()` |
| RHIThread | RHI 命令→D3D12/Vulkan 转换+提交 | `FRHICommandListExecutor::Execute()` |

**时序图（SVG）：** 展示一帧中三条线程的并行关系
- GameThread 在 InitViews 时与 RenderThread 重叠
- RenderThread 执行 RDG 时 RHIThread 处理上一帧的命令
- 同步点标注（`FRenderCommandFence`、`FRHICommandListFence`）

**Latency 分析与线程并行度：**
- 理想情况：三条线程完全流水线化（Pipeline），每帧延迟 3 帧但吞吐量 3x
- 实际瓶颈：RenderThread 的 InitViews 是最大开销（可见性计算难以并行）
- `r.RHIThread.Enable` 默认开启，关闭后 RenderThread 兼任 RHIThread

**code block：** 展示 `ENQUEUE_RENDER_COMMAND` 的典型用法

#### 章节 3：TaskGraph 调度

- [ ] 填充 `#taskgraph` section，包含：

**FTaskGraphInterface 核心概念：**
- 任务系统：UE 底层基于 Named Thread + Worker Thread Pool
- 每个线程有一个 `FTaskThreadBase`，维护任务队列
- 任务递交：`TGraphTask<FMyTask>::CreateTask(Prerequisites).ConstructAndDispatchWhenReady(Args...)`

**渲染线程绑定：**
- `ENamedThreads::ActualRenderingThread` — 渲染专用的命名线程
- `ENamedThreads::RHIThread` — RHI 命令执行线程
- RenderThread 不是 Worker Thread — 它有固定优先级和专用队列

**任务依赖 DAG（SVG）：** 展示一个 InitViews 的 TaskGraph 任务图
- 父任务 → 子任务 → 孙任务的依赖关系
- `FGraphEventRef` 作为任务完成的事件信号
- `FTaskGraphInterface::WaitUntilTasksComplete()` 等待一组任务完成

**code block：** TaskGraph 创建依赖任务的示例（Prerequisite → MainTask → CompletionEvent）

---

### 任务 7：多线程与RHI 第 4-6 章节 — 同步原语 + RHI 命令队列 + 平台抽象

**文件：** 修改 `UE5.8渲染流程解读/part/12-多线程与RHI/index.html`

#### 章节 4：FRenderCommandFence 与同步原语

- [ ] 填充 `#sync-primitives` section，包含：

**核心同步类型：**
- `FRenderCommandFence` — GameThread 等待 RenderThread 完成所有已提交命令
- `FRHICommandListFence` — RenderThread 等待 RHIThread 完成 GPU 命令提交
- `FFence` / `FGPUFenceRHIRef` — GPU 端同步（跨 Queue 或跨帧）

**FlushRenderingCommands 详解：**
- `FlushRenderingCommands()` — GameThread 阻塞直到 RenderThread + RHIThread 完成所有命令
- 使用场景：截帧、资源释放、渲染到纹理截图
- 风险：破坏流水线并行，导致 CPU 停顿
- 替代方案：尽量使用异步 Readback + Fence 轮询

**code block：** FRenderCommandFence 创建与等待的标准模式

**callout：** 过度使用 FlushRenderingCommands 是渲染性能的常见杀手

#### 章节 5：RHI 命令队列

- [ ] 填充 `#rhi-command-queue` section，包含：

**FRHICommandList 结构：**
- 命令录制（Record）：`RHICmdList.SetRenderTarget()`、`RHICmdList.DrawPrimitive()` 等写入命令缓冲区
- 命令提交（Submit）：通过 `FRHICommandListExecutor` 将命令队列提交到 RHI 线程
- 命令执行（Execute）：RHI 线程消费命令，翻译为 D3D12 `ID3D12GraphicsCommandList` 调用

**Immediate vs Deferred 上下文：**
- Immediate 模式：RHI 命令直接执行（`r.RHIThread.Enable=0`）
- Deferred 模式：命令先录制到队列，RHI 线程异步消费（默认）

**FRHICommand 宏生成机制：**
- 每个 RHI 函数调用生成对应的 `FRHICommand*` 派生类
- `RHICMD_*` 宏展开生成录制回调 + 执行回调
- 命令参数被序列化到命令对象中（POD 拷贝）

**code block：** FRHICommand 宏展开前后的对比代码

#### 章节 6：FDynamicRHI 与平台抽象

- [ ] 填充 `#platform-abstraction` section，包含：

**FDynamicRHI 接口体系：**
- 约 300+ 纯虚函数，定义完整的 GPU 操作接口
- 关键类别：资源创建（CreateTexture/Buffer）、状态设置（SetRenderTarget）、绘制（DrawPrimitive）、查询（OcclusionQuery）

**D3D12RHI vs VulkanRHI 实现差异表：**

| 特性 | D3D12RHI | VulkanRHI |
|------|---------|----------|
| 命令列表 | ID3D12GraphicsCommandList | VkCommandBuffer |
| 资源屏障 | D3D12_RESOURCE_BARRIER | VkImageMemoryBarrier / VkBufferMemoryBarrier |
| 描述符堆 | ID3D12DescriptorHeap | VkDescriptorPool / VkDescriptorSet |
| 根签名 | ID3D12RootSignature | VkPipelineLayout |
| PSO 缓存 | ID3D12PipelineState | VkPipeline + VkPipelineCache |
| 绑定模型 | Resource Binding Tier 2/3 | 标准 Descriptor Set Binding |
| 光追 | DXR (ID3D12RaytracingAccelerationStructure) | VK_KHR_ray_tracing / VK_KHR_acceleration_structure |
| Mesh Shader | ID3D12MeshShader | VK_EXT_mesh_shader |

**资源创建路径：** `FRDGBuilder::CreateTexture()` → `FDynamicRHI::RHICreateTexture()` → `FD3D12DynamicRHI::RHICreateTexture()` → `CreateCommittedResource()`

**状态转换：** RHI ResourceTransition → D3D12_RESOURCE_BARRIER 翻译

---

### 任务 8：多线程与RHI 第 7-9 章节 — PSO + AsyncCompute + CVar

**文件：** 修改 `UE5.8渲染流程解读/part/12-多线程与RHI/index.html`

#### 章节 7：管道状态对象（PSO）

- [ ] 填充 `#pso` section，包含：

**PSO 缓存系统：**
- `FGraphicsPipelineState` — 完整图形 PSO 描述符
- `FGraphicsMinimalPipelineStateId` — PSO 哈希 → ID 映射（全局 `PersistentIdTable`）
- PSO 创建流程：状态描述符 → 哈希 → 查表 → 命中返回 ID / 未命中编译 PSO

**PSO 预编译（Precache）：**
- 游戏启动时预编译所有已知 PSO 排列
- 材质首次可见时执行 PSO 预编译请求（`SkipDrawOnPSOPrecaching` 控制是否跳过绘制）
- 运行时 PSO 未命中导致 Hitching（PSO 编译延迟 5-100ms）
- `r.SkipUnloadedShaders` — Shader 未完全预加载时跳过 DrawCall

**性能影响：**
- PSO 切换开销：D3D12 约 5-10μs（单次 SetPipelineState），Vulkan 较高（10-20μs）
- PSO 数量管理：UE5 材质排列可能导致 10^5 级别的潜在 PSO 数量
- StateBucket 合批：相同 PSO ID 的连续 DrawCall 合并为 Instancing

#### 章节 8：AsyncCompute 并行

- [ ] 填充 `#async-compute` section，包含：

**AsyncCompute Queue 概念：**
- GPU 有两个主要命令队列：Graphics Queue（3D 渲染）和 Compute Queue（通用计算）
- 两个队列可并行执行，前提是无资源依赖
- 在 Desktop GPU（NVIDIA/AMD/Intel）上通常为 1 Graphics + 1-2 Compute Queue

**RDG 中的 AsyncCompute：**
- `GraphBuilder.AddPass(..., ERDGPassFlags::AsyncCompute, ...)` — 注册 AsyncCompute Pass
- RDG 自动分析 AsyncCompute Pass 与 Graphics Pass 之间的依赖
- 无依赖的 AsyncCompute Pass 可与 Graphics Pass 完全并行

**跨 Queue 同步：**
- 同一 Queue 内：隐式串行
- 跨 Queue：需要 GPU Fence 同步
- RDG 自动生成跨 Queue Barrier

**SVG 元素：** AsyncCompute 并行时间线图 — 展示 Graphics Queue 执行 BasePass 时，Compute Queue 并行执行 Shadow Filter 等任务

**典型 AsyncCompute 用途：**
- Shadow Depth Filter
- PostProcess Bloom Downsample
- MegaLights 降噪（信号相关处理）
- GPU Particle Simulation

**CVar：** `r.AsyncCompute`、`r.AsyncComputeOverlap` 控制并行执行

#### 章节 9：CVar 速查与性能分析

- [ ] 填充 `#cvar-reference` section，包含：

**多线程相关 CVar：**

| CVar | 默认值 | 说明 |
|------|--------|------|
| `r.RHIThread.Enable` | 1 | 启用 RHI 线程（禁用后 RenderThread 兼任） |
| `r.RenderThread.EnablebyDefault` | 1 | 是否自动启用专属 RenderThread |
| `r.RenderThread.TaskGraph` | 1 | RenderThread 使用 TaskGraph 调度 |
| `r.RHIThread.TaskGraph` | 1 | RHIThread 使用 TaskGraph 调度 |

**AsyncCompute CVar：**

| CVar | 默认值 | 说明 |
|------|--------|------|
| `r.AsyncCompute` | 1 | 启用 AsyncCompute |
| `r.AsyncComputeOverlap` | 1 | 允许 AsyncCompute 与 Graphics 并行 |

**D3D12 特定：**

| CVar | 默认值 | 说明 |
|------|--------|------|
| `r.D3D12.AllowAsyncCompute` | 1 | D3D12 允许 AsyncCompute |
| `r.D3D12.PSO.DiskCache` | 1 | PSO 磁盘缓存（重启加载） |
| `r.D3D12.PSO.PrecompileThreadPool` | 1 | 使用线程池预编译 PSO |

**Vulkan 特定：**

| CVar | 默认值 | 说明 |
|------|--------|------|
| `r.Vulkan.EnableAsyncComputeQueue` | 1 | Vulkan AsyncCompute Queue |
| `r.Vulkan.PipelineCache` | 1 | Vulkan Pipeline 缓存 |

**Unreal Insights 线程可视化：**
- 使用 `UnrealInsights.exe` 打开 .utrace 文件
- Timing 视图展示三条线程的并行度
- GPU 轨道展示各 Pass 的 GPU 执行时间线
- 关注点：RenderThread 瓶颈（红色区域占比）、RHIThread Bubble（等待 RHI 命令执行）

- [ ] **步骤 5：验证页面完整性并 commit**

```bash
wc -l "D:\project_ue5\ue5.8.0-docs\UE5.8渲染流程解读\part\12-多线程与RHI\index.html"
# 预期 > 600 行
```

```bash
git -C "D:\project_ue5\ue5.8.0-docs" add "UE5.8渲染流程解读/part/12-多线程与RHI/index.html"
git -C "D:\project_ue5\ue5.8.0-docs" commit -m "$(cat <<'EOF'
新增 12-多线程与RHI 页面：三线程模型 + TaskGraph + RHI 命令队列 + PSO + AsyncCompute 9 章解读

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

### 任务 9：更新首页 index.html

**文件：** 修改 `UE5.8渲染流程解读/index.html`

需要修改的位置（通过 Edit 工具精确替换）：

- [ ] **步骤 1：更新框架层卡片**

将原 RDG、RHI、多线程 三张卡片（L345-L374）替换为两张：

**旧代码（L345-L374）：**
```html
<a href="part/09-RDG与多线程/index.html" class="mod-card">
  <div class="mod-num">框架</div>
  <div class="mod-title">RDG</div>
  ...
</a>
<a href="part/09-RDG与多线程/index.html" class="mod-card">
  <div class="mod-num">框架</div>
  <div class="mod-title">RHI</div>
  ...
</a>
...
<a href="part/09-RDG与多线程/index.html" class="mod-card">
  <div class="mod-num">框架</div>
  <div class="mod-title">多线程</div>
  ...
</a>
```

**替换为：**
```html
<a href="part/09-RDG/index.html" class="mod-card">
  <div class="mod-num">框架</div>
  <div class="mod-title">RDG</div>
  <div class="mod-desc">渲染依赖图：自动资源生命周期 + Pass 调度 + 屏障插入 + 内存别名</div>
  <div class="mod-source">RenderGraphBuilder.h / RenderGraphValidation.cpp</div>
</a>
<a href="part/12-多线程与RHI/index.html" class="mod-card">
  <div class="mod-num">框架</div>
  <div class="mod-title">多线程与RHI</div>
  <div class="mod-desc">GameThread / RenderThread / RHIThread 三线程模型 + TaskGraph 调度 + 平台 RHI 抽象</div>
  <div class="mod-source">TaskGraphInterfaces.h / RenderingThread.cpp / D3D12RHI/</div>
</a>
```

- [ ] **步骤 2：更新全量目录表第 9 行**

将目录表中的 09 行描述和链接改为：
```html
<tr>
  <td class="toc-idx">09</td>
  <td class="toc-name"><a href="part/09-RDG/index.html">RDG</a></td>
  <td class="toc-desc">FRDGBuilder 全流程：Pass 注册/执行、Transient 分配、自动屏障、验证层与调试</td>
  <td><span class="toc-tag tag-planned">计划中</span></td>
</tr>
```

- [ ] **步骤 3：在目录表末尾（11 之后）新增 12 行**

```html
<tr>
  <td class="toc-idx">12</td>
  <td class="toc-name"><a href="part/12-多线程与RHI/index.html">多线程与RHI</a></td>
  <td class="toc-desc">三线程模型、TaskGraph 调度、RHI 命令队列、PSO 缓存、AsyncCompute 并行</td>
  <td><span class="toc-tag tag-planned">计划中</span></td>
</tr>
```

- [ ] **步骤 4：更新推荐阅读路径中的 RDG 链接**

将所有 `part/09-RDG与多线程/index.html` 中的链接按语义拆分为：
- 自底向上路径：第 1 条改为 `part/09-RDG/index.html`，第 2 条改为 `part/12-多线程与RHI/index.html`
- 问题驱动路径：性能瓶颈条目的链接改为 `part/09-RDG/index.html` 和 `part/12-多线程与RHI/index.html`

- [ ] **步骤 5：更新 Footer 中的章节计数**

将 `11 个章节` 改为 `12 个章节`

- [ ] **步骤 6：验证并 commit**

```bash
git -C "D:\project_ue5\ue5.8.0-docs" diff "UE5.8渲染流程解读/index.html"
# 验证所有链接从 09-RDG与多线程 → 09-RDG 或 12-多线程与RHI 正确替换
# 验证无遗漏的旧链接
```

```bash
git -C "D:\project_ue5\ue5.8.0-docs" add "UE5.8渲染流程解读/index.html"
git -C "D:\project_ue5\ue5.8.0-docs" commit -m "$(cat <<'EOF'
更新首页：拆分 09-RDG与多线程 为 09-RDG + 12-多线程与RHI（卡片/目录/阅读路径）

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

### 任务 10：最终验证

- [ ] **步骤 1：检查所有文件存在**

```bash
ls -la "D:\project_ue5\ue5.8.0-docs\UE5.8渲染流程解读\part\09-RDG\index.html"
ls -la "D:\project_ue5\ue5.8.0-docs\UE5.8渲染流程解读\part\12-多线程与RHI\index.html"
ls -la "D:\project_ue5\ue5.8.0-docs\UE5.8渲染流程解读\index.html"
```

- [ ] **步骤 2：检查无旧目录残留**

```bash
# 确保旧的 09-RDG与多线程 目录已不存在
[ ! -d "D:\project_ue5\ue5.8.0-docs\UE5.8渲染流程解读\part\09-RDG与多线程" ] && echo "OK: old dir removed" || echo "FAIL: old dir still exists"
```

- [ ] **步骤 3：全局搜索所有旧链接并确认已更新**

```bash
grep -r "09-RDG与多线程" "D:\project_ue5\ue5.8.0-docs\UE5.8渲染流程解读/" --include="*.html"
# 预期：无输出（所有旧链接已清理）
```

- [ ] **步骤 4：验证两页面的 HTML 结构完整性**

在浏览器中打开两个页面，检查：
- 面包屑导航可点击且跳转正确
- Hero 区域显示正确标题和标签
- 所有 section-block 展开无 CSS 错位
- SVG 图表正常渲染
- 代码块语法高亮正确
- 目录锚点链接跳转正确
- Footer 链接正确

- [ ] **步骤 5：Commit 最终验证结果**

```bash
git -C "D:\project_ue5\ue5.8.0-docs" status
# 确认无未提交变更
```

---

## 自检

**1. 规格覆盖度：**
- [x] 目录重命名（09-RDG与多线程 → 09-RDG）→ 任务 1
- [x] 09-RDG 12 章节 → 任务 2-5
- [x] 12-多线程与RHI 9 章节 → 任务 6-8
- [x] 首页卡片更新 → 任务 9 步骤 1
- [x] 首页目录表更新 → 任务 9 步骤 2-3
- [x] 首页阅读路径更新 → 任务 9 步骤 4
- [x] Footer 更新 → 任务 9 步骤 5
- [x] SVG 视觉元素 → 分布在各章节内容要点中标记

**2. 占位符扫描：** 所有步骤包含确切的文件路径、内容要点和命令。无 TODO/待定。

**3. 类型一致性：** 文件路径使用正斜杠（HTML 标准），git 命令使用 `git -C` 指定仓库路径，bash 命令适配 win32 平台。
