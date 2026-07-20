# RDG 与多线程页面拆分设计

## 概述

将现有占位页面 `09-RDG与多线程` 拆分为两个独立页面：
- `09-RDG` — 渲染依赖图，极致深度（12 章节）
- `12-多线程与RHI` — 多线程调度与 RHI 硬件抽象层，标准深度（9 章节）

同时更新 `index.html` 首页目录、卡片链接、推荐阅读路径。

## 目录结构调整

### 重命名
- `UE5.8渲染流程解读/part/09-RDG与多线程/` → `UE5.8渲染流程解读/part/09-RDG/`

### 新建
- `UE5.8渲染流程解读/part/12-多线程与RHI/index.html`

### 首页更新
- 框架层卡片：RDG、多线程、RHI 三卡片 → RDG（链接 09）、多线程与RHI（链接 12）两张卡片
- 全量目录：拆分 09 行，新增 12 行
- 推荐阅读路径：更新链接

## 09-RDG 页面内容大纲（12 章节）

1. **管线中的位置** — FRDGBuilder 创建/执行时机图，与 Render 函数九阶段的关系
2. **核心概念** — Pass/资源/Builder 三大原语，注册 vs 分配模型，UAV/SRV 访问声明
3. **Pass 注册与执行** — AddPass 流程、拓扑排序、无用 Pass 裁剪
4. **资源生命周期** — Create/Register/Allocate/Release 全链路，External 资源交互，Upload/Readback
5. **Transient 资源分配** — 内存池化、别名分析、堆分配策略
6. **依赖分析与自动屏障** — 资源引用→依赖图→Barrier 插入，D3D12 ResourceBarrier 生成
7. **自定义 RDG Pass 完整模板** — 光栅化 + Compute 双模板，黑板通信，CVar 控制
8. **RDG 验证层** — Validation 模式：未使用资源警告、循环依赖检测、访问冲突检查
9. **RDG Dump 与调试** — GraphViz 导出、Insights 集成、瓶颈诊断
10. **性能与陷阱** — CPU 开销分析、Lambda 捕获事项、大纹理 Upload 最佳实践
11. **CVar 速查** — `r.RDG.*` 完整列表
12. **核心源码索引** — 关键文件 + 类函数定位

## 12-多线程与RHI 页面内容大纲（9 章节）

1. **管线中的位置** — 三条线程在 Render 函数中的交互节点与职责边界
2. **三线程模型** — 时序图：GameThread→RenderThread→RHIThread，同步点与 Latency 分析
3. **TaskGraph 调度** — FTaskGraphInterface 实现，任务依赖 DAG，Named Thread/Worker Thread
4. **FRenderCommandFence 与同步原语** — 同步机制、FlushRenderingCommands、阻塞风险
5. **RHI 命令队列** — FRHICommandList 录制→提交→执行，Immediate vs Deferred 上下文
6. **FDynamicRHI 与平台抽象** — 接口体系，D3D12/Vulkan 实现差异，资源创建与状态转换
7. **管道状态对象（PSO）** — PSO 缓存、预编译、查找/创建/哈希机制
8. **AsyncCompute 并行** — AsyncCompute Queue 与 Graphics Queue 并行，跨 Queue 同步
9. **CVar 速查与性能分析** — `r.RHIThread.*`、`r.RenderThread.*`、Insights 线程可视化

## 视觉元素

- RDG 页面：Pass 依赖图（DAG 示例图）、资源生命周期状态机图、Transient 别名分析对比图、Dump 输出示例
- 多线程页面：三线程时序交互图、TaskGraph DAG 示意图、AsyncCompute 并行执行时间线图

## 实现顺序

1. 先编写 09-RDG 页面（重命名目录 + 完整 12 章节内容）
2. 再编写 12-多线程与RHI 页面（新建目录 + 9 章节内容）
3. 最后更新首页 index.html（卡片、目录、阅读路径）

## 样式与格式

沿用已完成的 04-材质 页面 CSS 体系，包括：
- 相同 CSS 变量（配色、字体）
- 相同组件样式（面包屑、Hero、section-block、code-block、info-table、call-chain、card-grid、info-card、info-callout、cvar-table、source-index、SVG 容器、footer、back-top）
- 一致的信息密度与代码块语法高亮方案
