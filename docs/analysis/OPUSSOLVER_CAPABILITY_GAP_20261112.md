# OpusSolver 能力盘点：我们到底缺什么（2026-11-12）

这一轮的结论是**路线需要换**。不是因为我们拿不到那些指标，而是因为之前一直在“往我们的
emitter 里加零件”，而上游根本不是靠加零件赢的。

证据分两部分：**(一) 上游是什么**（读 `gtw123/OpusSolver@fdedd3f0` 源码与 README）；
**(二) 我们和它的结构性差异**（对 438 个参考解与我们的 139 台机器做机械盘点，
脚本 `tools/reference_inventory.py`）。

---

## 1. 上游是什么（事实，不是推断）

oracle 是 `--solver Standard --optimize` 生成的。上游有两个 solver：`Standard` 与 `LowCost`。
我们对着的是 **Standard**——README 自己说它“far from optimal”，但“does generally produce
solutions that take less cycles”。所以**它赢在周期，不是赢在技巧**。

它的解永远是三段式：

1. 把试剂拆成单原子（一次一个）
2. 用 glyph 把单原子转换成所需元素
3. 逐原子装配产物

生成过程是固定的七步（README `Design overview`）：

| 步骤 | 内容 | 关键类 |
|---|---|---|
| 1 | 建 recipe：试剂数/产物数/每种反应次数/是否有 waste | `RecipeGenerator`, `RecipeBuilder`（**lp_solve 整数线性规划**，`scale=1..6`，等式失败后放宽为不等式并按 cardinal 元素逐个放宽） |
| 2 | 建 plan：拆解顺序、装配顺序、各种 flag | `SolutionPlan`, `MoleculeDisassemblerFactory`, `MoleculeAssemblerFactory` |
| 3 | 建 element pipeline：按反应顺序排列元素级生成器 | `ElementPipeline`, `ElementGenerators.*` |
| 4 | **dry run**：OutputGenerator 逐个向上游请求元素，得到 `Consume/Generate/PassThrough` 命令序列 | `CommandSequence`, `Command` |
| 5 | 每个 element generator 建一个 atom generator（真正摆放物体并编程） | `Solver/Standard/*Generator.cs` |
| 6 | 顺序执行命令，产出 program fragments | `ProgramWriter` |
| 7 | 合并、**移除从未使用的部件**、写出 | `CostOptimizer`, `*.OptimizeParts()` |

第 4 步是整个架构的关键：**在摆放任何物体之前，先把“这一台机器要按什么顺序、消费/产出哪
些原子”算成一条与几何无关的逻辑命令序列**；缓冲需求、waste 身份、纯化步数都是这一步的
副产品。第 5 步只是把这条序列翻译成模块。

Standard solver 的模块库（`Solver/Standard/**`）：

- 输入：`SimpleInputArea`（≤4 个单原子试剂）、`ComplexInputArea` + `MoleculeDisassemblerFactory`
- 传送：`AtomConveyor`（2 格的往返搬运）
- 缓冲：`AtomBuffer`、`AtomBufferNoWaste`、`MultiStackElementBuffer`
- 元素生成：`SaltGenerator`、`SaltGeneratorNoCardinalPassThrough`、`VanBerloGenerator`、
  `MetalProjector`、`MetalPurifier`、`MorsVitaeGenerator`、`QuintessenceGenerator`、
  `QuintessenceDisperser`
- 输出：`SimpleOutputArea` + `MoleculeAssembler`，Standard 用 `UniversalAssembler`
  （`AssemblyArea` 有 **lower/upper 两组臂 + 60°/120°/triplex bonder 阵列**，逐行 `GetAtomsToGrabForRow` / `FinishRow`），另有 Hex3 变体

每个模块都是**预编程的小机器**，有固定的输入/输出 access cell、固定的节拍偏移；拼装 = 平移
+ 旋转 + 对齐节拍。**布局是严格线性的**：每个生成器的出臂直接对着下一个的入臂。

---

## 2. 我们的机器 vs 参考机器（139 对，机械盘点）

参考解的 `_cycles` 版本（周期即我们要追的那个数）。

### 2.1 周期比与节拍比

| class | n | 周期比 (ours/ref) | ticks per grab 比 | cycles 比 | ref batches | 
|---|---:|---:|---:|---:|---:|
| `linear/mono` | 39 | **2.00×** | 2.11× | 5.62× | 1.00 |
| `pipeline/cheap-arms` | 29 | **27.8×** | 7.60× | 23.47× | 4.92 |
| `pipeline/heavy` | 7 | 1.65× | 5.05× | 7.32× | 1.00 |
| `universal` | 64 | **2.85×** | 3.39× | 13.69× | 1.01 |
| 全部（中位） | 139 | 2.9× | 3.4× | 9.6× | 1.0 |

`85 / 139` 题的周期超过参考的 2 倍。

### 2.2 单原子延迟

| 量 | 我们 | 参考 | 倍数 |
|---|---:|---:|---:|
| ticks per grab（周期 ÷ 抓取次数） | **12.8** | **3.8** | 3.4× |
| rotations per grab | **1.56** | **0.38** | 4.1× |
| 臂利用率（各臂 tape 总长 ÷ (臂数×周期)） | **6.3%** | **15.8%** | 2.5× 更闲 |

也就是说：**参考机器每搬一个原子只花 3.8 拍、转 0.38 次；我们花 12.8 拍、转 1.56 次，而且
臂还更闲。** 我们的机器是“一只臂按顺序把整台机器的活干完”，它是一只臂串行做完 N 次搬运；
参考是“每只臂在一个周期里只干 1–2 件小事，所有臂同时干”。

### 2.3 机制词表

| object | 有多少台参考机用 | 有多少台我们的机器用 | ref 中位数量 | ours 中位数量 |
|---|---:|---:|---:|---:|
| `arm1` | **139 / 139** | **60 / 139** | **9** | **1** |
| piston | 119 | 108 | 5 | **15** |
| track | 140 | 109 | 3 | 5 |
| baron | 31 | 26 | 1 | 2 |
| unbonder | 74 | 62 | 3 | 6 |
| bonder-prisma | 11 | 10 | 3 | 9 |
| glyph-disposal | **0** | **21** | 0 | 2 |

- `79 / 139` 题我们的机器**完全没有 arm1**；参考**一台都没有缺席**。
- 臂长词表：参考用 `extension=3` 的臂 **547 次**，我们只有 **72 次**。
- track 用法完全不同：`cheap-arms` 里参考是 3 条轨道 / 39 格（3–17 格的短传送带），
  我们是 5 条 / **221 格**（当长总线用）。
- 参考从不用 `glyph-disposal`：它把 waste 写进 recipe（有 waste 配方），我们反而花 cost 造
  销毁 glyph。

---

## 3. 缺失能力清单（全）

### A. 并发与调度（第一杀手，缺它其它都是零头）

- **A1 逻辑命令序列层**（上游 Step 4）。在摆放几何之前，先算出“按什么顺序消费/产出哪些
  原子”的元素级序列。我们没有这一层，所以无法知道哪里该并发。
- **A2 每原子独立机制**。参考给在飞的每个原子配一只臂（每臂每周期 1–2 次抓取）；
  我们让一只臂在一个周期里做 N 次抓取。
- **A3 节拍偏移作为一等内容**。模块拼接时对齐 `tick offset`，使所有模块同一周期内同时工作。
  我们的 `tick` 是“追加到末尾”的游标，天然串行。
- **A4 双 bank 装配区**。`AssemblyArea` 的 lower/upper 两组臂能在同一周期往同一行放两个原子；
  我们只有单臂或活塞行。
- **A5 多副本在飞**。参考在 `cheap-arms` 类里 4.9 个 batch、在另三类里 1.0 个 batch——它能把
  6 个产物同时在流水线里摊开。我们的 `batches = cycles/period` 恒为 1，靠拉长周期硬顶。

### B. 模块库（缺的是“每步一个可复用机器”，不是机型）

- **B1 access point 抽象**：`(位置, 朝向, 可被抓取的原子序列, 节拍)`。这是模块化的地基，
  我们没有。
- **B2 单原子传送器**（`AtomConveyor`）：1–2 格/周期的往返搬运。我们只有长轨 + 活塞。
- **B3 原子缓冲**：`AtomBuffer` / `AtomBufferNoWaste` / `MultiStackElementBuffer`，含“用废键链
  暂存原子”的变体（无 disposal 时唯一解）。
- **B4 试剂拆解器族**：按试剂形状/大小选几何（`MoleculeDisassemblerFactory`）。
- **B5 产品装配器族**：逐行 / 逐原子 / 双 bank / Hex3 变体（`MoleculeAssemblerFactory`）。
- **B6 我们完全没有实现的生成器**：`MetalPurifier`（按序列纯化、步数可变）、`MorsVitaeGenerator`、
  `QuintessenceGenerator` / `QuintessenceDisperser`、`SaltGeneratorNoCardinalPassThrough`、
  `VanBerloGenerator`（我们有 `baron`，但只当搬运器用，没当元素生成器用）。

### C. 配方层（整数规划）

- **C1 反应组合的整数线性规划**。变量 = 试剂数 / 每种反应次数 / 产物数，方程 = 每个元素守恒。
- **C2 规模化**（`scale = 1..6`）：一次建多个产物有时反而更省。
- **C3 waste 的显式建模**：先等式求解，失败再放宽为不等式（此时必然产 waste）。
- **C4 按 cardinal 元素逐个放宽**（比一次全放宽更优）。
- **C5 `IncludeUnneededReactions`**：多余的转换反应有时能换来更短的周期。

我们现在是硬编码“有单原子端口就用端口，否则钙化盐”；没有配方层，也就无法回答“这台机器
到底该用几次投影 / 几次 van Berlo”。

### D. 参数化与搜索（`--optimize`）

- **D1 SolutionParameter 注册表**：臂长、元素顺序（试剂/产物、首个/其它产物）、键遍历方向、
  拆解器额外宽度、无臂缓冲、盐不旋转、复杂产物用 BFS 顺序……
- **D2 组合枚举 + 去重 + 并行验证**。
- **D3 每指标各自保最优**（cost / cycles / area 各一台）。我们现在只有“2/3 多数发布”一台。

### E. 后处理

- **E1 未使用部件裁剪**（`Step 7` + `*.OptimizeParts()`）：从未抓取的臂、从未被经过的轨道、
  从未用到的 bonder 全部删掉。
- **E2 `CostOptimizer`**：生成后统一的 cost 削减 pass。

### F. 可量化的小项（直接抄就行）

- **F1** 把 `arm1` 变成默认搬运机制（139/139 vs 60/139）。
- **F2** 打开 `extension=3` 的臂（547 次 vs 72 次）：长臂能省掉轨道。
- **F3** 轨道当短传送带而非长总线（39 格 vs 221 格）。
- **F4** 少用活塞（中位 5 vs 15）。
- **F5** 停用 `glyph-disposal`（参考 0 台，我们 21 台）。

---

## 4. 对路线的判断

**差距不是“少了几种机制”。** 对象词表我们几乎是齐的（`bonder-speed` 和 `unbonder-prisma`
两边都是 0；唯一明显的覆盖率差是 `arm1`）。缺的是三层架构：**逻辑命令序列（A1）→ 带
access point 的模块库（B1）→ 节拍调度（A3）**。

这也解释了之前的收益上限：M1 放宽取料、行流水线默认化、臂网络、sibling 候选，全都作用在
“既有 family 的局部参数”上。它们把 cycles 中位从 10.38× 压到 9.6×，但没有一层能改变
“周期 = Σ 所有原子的活”这个结构性事实。所以：

- 继续加第 N 个 family，收益上限就是 cheap-arms 那 27.8× 里的零头；
- 补 A1+B1+A3 之后，**每一个 family 都能变成流水线**，收益是乘性的。

建议的新顺序（与 §14 的 M2–M5 不同）：

1. **M-A：逻辑命令序列 + dry run**（纯数据层，可单测，无几何风险）。
2. **M-B：access point + 模块库骨架**（先做 `AtomConveyor`、`AtomBuffer`、`SimpleInputArea`、
   `UniversalAssembler` 四个模块，够覆盖大多数 `linear/mono` 与 `universal`）。
3. **M-C：节拍调度器**（把模块按 offset 拼成一条线性流水线，让周期等于最慢单原子路径）。
4. **M-D：参数注册表 + 枚举**（复刻 `--optimize`，用它做 A/B 的优势方）。
5. **M-E：后处理裁剪**（`OptimizeParts` + `CostOptimizer`）。

工具：`tools/reference_inventory.py`（本轮新增）可以随时重跑，用同一张表衡量每一层落地后
周期比、ticks/grab、rot/grab、利用率、`arm1` 覆盖率的移动。
