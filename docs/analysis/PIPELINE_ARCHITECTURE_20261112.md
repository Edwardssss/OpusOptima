# 逻辑命令序列 → 模块库 → 节拍调度 → 参数枚举 → 后处理：落地报告（2026-11-12）

`OPUSSOLVER_CAPABILITY_GAP_20261112.md` §4 给出的顺序是 **M-A → M-B → M-C → M-D → M-E**。
这一轮把整条链路实现完了，并逐层做了单测与原生门验证。结论先说：**链路成立，收益为零**，
原因是四个模块的可达域（≤6 原子、纯单原子端口 / 一次钙化）在语料里只剩 8 题，而这 8 题
现有族已经在周期下限（cycles = 3）。因此该族默认**关闭**，等生成器模块把域放宽后再开。

---

## 1. 代码落点

新增 `src/Solvers/Architecture/`（`CMakeLists.txt` 的 `OPUSOPTIMA_INCLUDE_DIRS` 里加了这一项，
源文件由既有的 `GLOB_RECURSE` 自动收集），以及一个共享几何核：

| 层 | 文件 | 内容 |
|---|---|---|
| 共享 | `src/Solvers/Layout/SwingKernel.hpp` | 验证器方向序、`SwingCandidates`、`SwingDisc`、`SwingSectorCells` |
| M-A | `Architecture/ElementPipeline.{hpp,cpp}` | 元素级生成器链 + dry run 的 `Consume/Generate/PassThrough` |
| M-B | `Architecture/ModuleLibrary.{hpp,cpp}` | `SimpleInputArea` / `AtomConveyor` / `AtomBuffer` / `UniversalAssembler`，带 access point |
| M-C | `Architecture/TickScheduler.{hpp,cpp}` | `Serial`（周期=Σ工作量）/ `Pipelined`（周期=最慢模块）/ `Staggered` |
| M-D | `Architecture/SolutionParameters.{hpp,cpp}` | `ParameterRegistry` 枚举 + 去重，`ObjectiveSelector` 每指标保最优 |
| M-E | `Architecture/SolutionPostProcess.{hpp,cpp}` | `PruneUnusedParts`（空转臂、重复件、无引用轨道）+ `CostOptimize` |
| 端到端 | `Architecture/PipelineCompiler.{hpp,cpp}` | 串起 A→E，输出可被原生门判定的 `io::SolutionData` |

入口在 `AssemblySolver` 的 portfolio 里，管脚是 `OPUSOPTIMA_PIPELINE_ARCHITECTURE=on|off`，
默认 `off`。

## 2. 每层做了什么

**M-A（元素管线 + dry run）**。`ElementPipeline::Build` 用生成器重写规则（钙化、投影、纯化、Animismus、
Unification、Dispersion、Duplication）从输入原子做可达性松弛，给每个被需求的产物元素找一条最短链；
`Run()` 再让每个产物原子沿链向上请求，得到与几何无关的命令序列，并顺带算出每周期各模块的调用
次数、每个元素要跨越多少下游模块（`PassThrough`）、以及**未被需求的产物就是 waste**（Animismus 的
Mors 就是典型）。这些数就是 M-C 需要的输入。

**M-B（模块库）**。四个模块都实现成「access point + 载体对象 + latency/period」：

- `SimpleInputArea`：单原子端口，latency 0，period 1；
- `AtomConveyor`：`arm1` 摆动搬运，`latency = |rot|+1`，`period = 2|rot|+2`；
- `AtomBuffer`：`arm1` 停放一拍；
- `UniversalAssembler`：每个产物原子一只摆臂 + 每个产品键一个 bonder + `out-std`，
  **period 取最慢那只臂，而不是全和**。

**M-C（节拍调度）**。三种策略都实现了。`Serial` 就是旧模型的「追加游标」；`Pipelined` 让所有臂
从 tick 0 开始，周期 = 最慢臂；`Staggered` 折中。编译器按策略选放置规则：pipelined/staggered
要求各臂的**圆盘互不相交**（同时运动必然不撞），serial 用**扇区扫描**（旋转过程中臂体扫过的
角度区间）只避开已经落下的原子与固定件。

**M-D（参数枚举）**。`ParameterRegistry` 做笛卡尔积 + 去重；编译器枚举 (schedule, arm preference)，
`ObjectiveSelector` 按 cost / cycles / area 各自保留最优（不是旧的 2/3 多数表决）。

**M-E（后处理）**。`PruneUnusedParts` 只删可证明无用的件：整条 tape 只有 `O`/`X` 的臂、与前面
完全相同的重复件、没有任何对象引用的轨道。删完仍要过原生门才算数。

## 3. 单测

`tests/unit_test/ArchitecturePipelineTest.cpp`，14 项全绿，其中两项是原生门测试：

- `PipelinedMachineBeatsSerialNatively`：同一个 2 原子键合产物，pipelined 与 serial 两台机器都过
  `legacy-gate`，且 pipelined 的 cycles 严格更小 —— 这是 M-C 那条「周期 = 最慢臂」的直接证据；
- `CompilePipelineMachine` 的结构断言：一臂一原子、一输出区、M-D 枚举顺序。

其余覆盖 M-A 的链/waste/拓扑序、M-B 的 access point 与 assembler 周期、M-C 的两种周期模型、
M-D 的枚举与选择器、M-E 的裁剪。

## 4. 语料测量（162 题）

`python3 tools/bench_corpus.py --control OPUSOPTIMA_PIPELINE_ARCHITECTURE=off --variant OPUSOPTIMA_PIPELINE_ARCHITECTURE=on --jobs 6`

```
control produced=141/146 any-win= 65 cost= 63 cycles= 51 area= 60 all-three= 51
variant produced=141/146 any-win= 65 cost= 63 cycles= 51 area= 60 all-three= 51
changed: improved=2 regressed=4 unchanged=135
```

逐题三串行隔离（`d61cc536` / `a94b5296` / `58daac9a` / `65c4fc73` / `665f279f` / `6c8d8aa1` /
`73148f31` / `9eadeb7b` / `fbd25414` / `55240fc3` / `94871b17` / `00302be6`）：`off` 与 `on` 的
cost/cycles/area 三元组完全一致；`d61cc536` 的 `1170 -> 1165` 只在三次里出现一次，是负载噪声。

**为什么是零**：整族只枚举出 8 题的机器（`55240fc3`、`65c4fc73`、`665f279f`、`6c8d8aa1`、
`73148f31`、`94871b17`、`9eadeb7b`、`fbd25414`），其中 6 台过门。这 6 题的现有机器 cycles 已经是
3（一周期出一件的下限），pipelining 无处可省；cost 也已经是同一批对象。regressed 的 4 题是
提交预算被占用后，后续族少提交了一次，cost 槽略差 —— 这正是「不值得开」的直接证据。

## 5. 域为什么这么窄（下一步）

`PipelineDomainReason` 会给出拒绝原因。全语料的分布是：

- `product larger than six atoms`：51 题（编译器只发 ≤6 原子的框架）
- `more than one output`：主要剩余来源
- 到这里仍有 8 题进入枚举，但其中一部分因 `no portable supply for element X` 被挡回

也就是说，**四模块可达域 = 纯单原子端口链 + 一次钙化**。要把它变成 M-C 能吃的形状，缺的不是调度，
而是 `OPUSSOLVER_CAPABILITY_GAP_20261112.md` 里 B6 那一行生成器：

1. `MetalProjector`：把金属放在投影 glyph 上，逐周期喂 quicksilver，`latency = 升级级数` —— 这正好
   是 M-C 现在能对齐的东西（模块 latency > 0，下游臂延后抓取）；
2. `MetalPurifier`：两次同种金属合成上一级，步数可变；
3. `MorsVitae` / `QuintessenceGenerator`：把 M-A 已经算出来的 Animismus/Unification 链真正发射出来；
4. 配方层（C1/C2）：`scale=1..6` 让「一台机器一次做几件」成为显式变量，而不是被 out-std 的单输出
   约束钉死。

另外一条被本轮**否掉**的假设也记在这里：因为 `decode.c` 要求 `out-std` 的个数恰好等于谜题输出个数
（`number_of_unplaced_outputs` 不能为负），「K 份产品框架各配一个输出区」的多批次机器是**非法**的。
多批次只能靠同一输出区分时消费，这需要与生成器模块一起设计，不能靠复制框架。

---

## 6. 第二轮：给 M-B 加生成器模块（MetalProjector / MetalPurifier）

第一轮证明「搬运原语」是差距的主因：`cycles / arm_tape_指令数` 在单原子输入组是
3.0 拍，在分子输入组是 114.7 拍，而参考是 3.8 拍。于是本轮做了方案 A 的载体扩充：
把 `PipelineCompiler` 从「4 个模块」扩到 6 个，新增两个**带 latency 的生成器**。

### 6.1 做了什么

- **`MetalProjector`**：金属放在投影 glyph 的金属格，glyph 西侧常驻 quicksilver；
  原子每周期升一级，`latency = 起始金属 rank - 产物 rank`。多级链（Lead→Tin→Iron）
  折叠成**一个** glyph 站，因为原地升级不需要中间搬运。
- **`MetalPurifier`**：glyph 在产物格南邻，两个金属格是 glyph 格与其东邻；两个
  `input` 对象都引用同一个单原子端口，所以每周期进一对、出一件。
- 两个模块都走 `TickScheduler` 的 per-module offset：`armStart = offset + delay`，
  `period ≥ armStart + 摆动 + 1`。这是 M-C 第一次真正有 `latency > 0` 的模块可对齐。
- 权限前置：`calcify` 要 bit 12、`project` 要 bit 14、`purify` 要 bit 15，缺失时直接
  拒绝而不是交给门浪费预算。

### 6.2 顺带定位到一个真实的死锁

`d80bd027`（7 原子）在 compose 族的静态布局里生成了 20 个对象却
`did not complete within cycle limit`。dump 出来原因很清楚：

> **静态投影布局里 bonder 和 projection 争同一格。** bonder 先把金属与邻居连起来，
> 被 bond 的金属就不再被投影 glyph 升级，于是产物永远不完整。

这从反面确认了方案 A：投影**必须**和 bond 在时间上分开——金属先在生成器站升级，
再由短摆搬到产物格，bond 只发生在产物格。这正是本轮 `MetalProjector` 的形态。

### 6.3 测量（仍是零收益）

```
control produced=140/146 any-win=65 cost=63 cycles=51 area=60 all-three=51
variant produced=139/146 any-win=65 cost=63 cycles=51 area=60 all-three=51
changed: improved=1 regressed=2 unchanged=136
  improve d61cc536 cost 1175->1170（三次串行里只出现一次，负载噪声）
  REGRESS cb6c9528 cost 4745->4755
  REGRESS 3d4d0c74 cost  545->550
```

架构族现在能枚举出变体的题从 8 增到 13（投影 +6、纯化 +1），全部过门，但**没有一题被
archive 选中**：

| 题目 | 产物原子 | 参考周期 | 现有基线 | 架构族最优 |
|---|---:|---:|---:|---:|
| `fbd25414` | 3 | 74 | **3** | 28 |
| `55240fc3` | 3 | 88 | **3** | 34 |
| `73148f31` | 2 | 65 | **3** | 34 |
| `321f9b75` | 5 | 287 | **3** | 130 |
| `110016dc` | 1 | 52 | **6** | 34 |

现有基线是 `cycles = 3`（一周期一件的下限），架构族给的是 28–130 —— **不是架构族慢，
是这些题根本不需要快**。

### 6.4 根因：两个域不相交

| 组 | 题数 | 我们能否触达 | 现状 |
|---|---:|---|---|
| 单原子输入 + 纯端口/一次变换 | ~13 | 架构族能 | 已在下限（cycles 3–6） |
| 单原子输入 + 长变换链（project+purify、disperse） | ~20 | 需要**模块串联编排** | 基线 1281–14814 拍 |
| 分子输入 | 70 | 需要**拆解 + 释放原子分配** | 基线 2148–13811 拍 |

**架构族的能力和"我们差的题"没有交集。** 每加一个生成器只能多覆盖几道已经赢的题，
边际收益为零，还会因占用提交预算造成偶发回退。

### 6.5 结论与下一步

能力层面本轮是净增加的：M-B 有了 `MetalProjector` / `MetalPurifier`，M-C 有了
`latency > 0` 的对齐用例，投影与 bond 的时间隔离有了实测依据。

但**要产生性能提升，下一步不是再加生成器**，而是二选一：

1. **模块串联编排**（覆盖 ~20 题，基线 1281–14814 拍）：让 `ElementPipeline` 的链
   （如 Lead→project→Tin→purify→Iron）被翻译成「多个生成器站 + 站间短摆」。需要解决
   一对二供给（purify 要两个同种金属，投影站每周期只出一个），所以至少要两个并联的
   上游站。
2. **分子输入的拆解与分配**（覆盖 70 题，最大的池）：分子落地 → 沿键切 → 每个释放
   原子一个静态工位 → 并行短摆送入产物格。

按上一轮的比例，②的收益上限是 ①的 3 倍以上，且 ② 的原子在拆开后天然适合短摆。

## 7. 第三轮：分子输入的拆解与并行分配（M-F）

### 7.1 做了什么

`src/Solvers/Architecture/MoleculePipeline.{hpp,cpp}` 实现了 §6.5 选项 ② 的最小闭环：

* `SupportsMoleculeAssembly` —— 域判定（非 production、单输出、权限含
  `arm|bonder|unbonder|G|g|O|X`、产物 2–16 原子、键全为 1 且方向合法、存在分子输入、
  每个产物元素都能在某个输入的原子里找到）。
* `SelectMolecule` / `AssignMolecule` —— 挑一个**整用**的分子（每个原子都必须被某个
  产物原子消费掉；否则多余原子会永远停在落地格，下一周期分子无法再落地）。
* `PlanLanding` —— 枚举 `turns ∈ 0..5` × 90 个 `arm1` 位移，反推落地原点，要求每个
  被拆下的原子都在自己产物格的单摆位移上。
* 发射：一个 `input`（含旋转）、每根输入键一个 `unbonder`、每个分子产物原子一个
  `arm1`（同一拍抓取，周期 = 最慢摆 + 2，**不是**链长之和）、每个端口原子一个
  `input`、每根产物键一个 `bonder`、一个 `out-std`。
* 接入：`AppendMoleculeVariants` 由 `CompilePipelineVariants` 调用，独立开关
  `OPUSOPTIMA_MOLECULE_ARCHITECTURE`（默认 off）。

### 7.2 域漏斗（146 题）

| 出口 | 题数 |
|---|---:|
| `multi-output`（puzzle 有多个输出槽） | 32 |
| `no-mol`（输入全是单原子） | 43 |
| `needs-chem`（产物元素不在任何输入原子里） | 25 |
| `out-size`（产物 <2 或 >16 原子） | 21 |
| `bond`（键方向不在合法序里） | 9 |
| `repeat`（含 Repeat 元素） | 7 |
| `mol-not-whole`（分子无法整用） | 2 |
| `perm`（权限位不足） | 1 |
| **进入 `ADMIT`** | **6** |

6 道是 `0bac2445 3d49bf1c 64da5361 b965f7b1`（linear/mono）与
`18644c2c f7ea45f6`（pipeline/cheap-arms）。

### 7.3 实测：6/6 全部被拒

| 题 | 类 | 产物原子 | 拒绝原因 |
|---|---|---:|---|
| `0bac2445` | linear/mono | 7 | `no collision-free swing for an atom` |
| `3d49bf1c` | pipeline/cheap-arms | 5 | 同上 |
| `64da5361` | linear/mono | 6 | 同上 |
| `18644c2c` | pipeline/cheap-arms | 13 | `molecule atoms do not cover the product` |
| `b965f7b1` | linear/mono | 8 | 同上 |
| `f7ea45f6` | pipeline/cheap-arms | 12 | 同上 |

两类原因都不是实现缺陷，而是几何与素材的硬约束，见 7.5。

### 7.4 机制本身是对的：2 原子案例 34 拍过原生门

`tests/unit_test/MoleculePipelineTest.cpp`（7 项，全绿）里
`EmittedMachinePassesTheNativeGate` 用一个「2 原子分子 → 2 原子产物」的合成题，
让 `CompileMoleculeMachine` 发射整机并交给 `legacy-gate`：

```
cycles = 34
```

即分子落地、两根键同时切开、两个原子在同一周期内各自摆到产物格。同一形状在
`pipeline/cheap-arms` 上的基线中位数是 **14058 拍**。所以「同拍并行分配」这个思路
的**上限确实有 400× 量级**，问题只在可达性。

### 7.5 几何天花板（本轮最重要的结论）

`arm1` 只能抓它半径 `L` 环上的 6 个顶点之一，本体扫过的 `SwingDisc(pivot, L)` 是
半径 `L` 的整盘（`L ∈ {1,2,3}`，7–37 格）。

一个分子是**刚体**，它的原子间距是谜题给定的（通常为 1）。要在同一周期里让每个分子
原子各有一只臂，就必须满足：

1. 每个原子的 `release` 到它自己 `target` 的位移是 90 个单摆位移之一；
2. 各臂的 `pivot` 互异，且任一臂的盘不能包含别人的 `release`、别的臂的 `pivot`、
   或除自己 target 之外的产物格。

产物原子之间必然相邻（它们之间有键），间距 1；而分子原子间距也是 1。于是「分子形状
必须等于产物形状的刚性变换」几乎成了唯一解，此时所有臂的 pivot 沿同一方向等距排开，
间距 1，盘的半径 ≥1 ⇒ **必然互相覆盖**。

合成测试能过门，是因为我构造的分子形状恰好与产物形状一致且只有 2 个原子（两臂盘可以
沿相反方向张开）。真实谜题的分子与产物形状不同，第 1 条就已经不成立。

**给 `L=1` 时的上界**：一个臂的环只有 6 个格。分子里能落在这 6 个格上的原子，
其两两关系被 pivot 唯一确定；对一般分子这个命中率极低。多臂集合覆盖实验（贪心选
`(pivot, L)`）显示：63 个含分子输入的题里，单臂最多只能覆盖中位数 **60%** 的分子
原子，只有 11 题能被单臂全覆盖。

### 7.6 A/B 与隔离复测

```
control produced=137/146 any-win=65
variant produced=136/146 any-win=65
changed: improved=1 regressed=2 unchanged=133
  REGRESS d61cc536 universal       cost:1175->1180
  improve a7d3c0bf pipeline/heavy  cost:12155->11375; cycles:41104->41102; area:3069->3052
  REGRESS 160f11ef universal       cost:2040->2120; cycles:29142->31461; area:794->832
```

三题各串行隔离复测 3 次：

| 题 | `off` | `on` | 判定 |
|---|---|---|---|
| `a7d3c0bf` | 41102 / 11375 / 3052 ×3 | 完全相同 ×3 | A/B 的 improve 是**并行噪声** |
| `160f11ef` | 29142 / 2040 / 794 ×3 | 完全相同 ×3 | A/B 的 regress 是**并行噪声** |
| `d61cc536` | cost 1170 ×3 | 1175, 1175, 1170 | 已知抖动，非确定性 |

这与「6 题全部被拒、0 个变体被采纳」一致：**分子族对结果零影响**。
`produced 137→136` 同样是并行下的超时抖动。

### 7.7 重新归因：真正的缺口在哪

用 oracle 的 `class` 字段重新切分 139 道有解的题（`bench-ordered-network-20261112`
的 control 结果）：

| class | n | 参考 cycles 中位 | 基线 cycles 中位 | 中位比 | >2× 题数 |
|---|---:|---:|---:|---:|---:|
| `linear/mono` | 39 | 146 | 3 | **0.05×** | 13 |
| `pipeline/heavy` | 7 | 814 | 3 | **0.01×** | 3 |
| `pipeline/cheap-arms` | 29 | 778 | **14058** | **23.47×** | **26** |
| `universal` | 64 | 588 | **6088** | **13.69×** | **46** |

最差的 20 题全是 `pipeline/cheap-arms` / `universal`，产物 4–24 原子，输入多为
**5–13 原子的分子**，基线 26658–94550 拍而参考只有 412–2793 拍。

所以缺口不是「分子能不能拆」，而是：

1. **32 题的 `multi-output` 被单输出约束挡住** —— 这是最大的单一池，`decode.c` 允许
   「一个 `out-std` 对应一个 output」，所以多输出题应当发射多个 `out-std`，当前
   `SupportsMoleculeAssembly` 与 `PipelineDomainReason` 都直接拒掉。
2. **25 题 `needs-chem`** —— 需要「拆解 + 化学站」组合，而不是纯搬运。
3. **21 题 `out-size`** —— 产物 >16 原子，`kMaxMoleculeAtoms` 与
   `kMaxPipelineAtoms` 都是过早的上限。

### 7.8 回归验证

| | 基线 | 本轮 |
|---|---|---|
| 测试总数 | 1053 | **1060**（+7） |
| 通过 | 1037 | **1044**（+7） |
| 跳过 | 5 | 5 |
| 失败 | 11 | **11** |

失败清单与基线逐条同名（`AssemblyDiagnostics.*` ×8、`AssemblyVerifierTest.*` ×2、
`RigidCarrierTest.NoPlacementPreservesDownstreamOpportunity`），**净新增 0**。

### 7.9 结论

* 分子拆解族**保留但默认关闭**：机制正确（34 拍过门）、域只有 6/146、且这 6 题
  几何上全部无解。它是「为什么同拍并行分配在 opus 几何下不可行」的可执行证据。
* 单测 7 项锚定了域判定与一次真实过门，防止后续误以为它可用。
* **下一轮应从 7.7 的三个池里选**，且优先级应是 `multi-output`（32 题，纯结构放开，
  不需要新几何）> `needs-chem`（25 题）> `out-size`（21 题）。

## 8. 第四轮：多输出假设被证伪，真正的瓶颈是 relay 延迟

### 8.1 先纠正 §7 的一个错误结论

§7.7 把 `multi-output`（32 题）列为"最大单一池，纯结构放开，不需要新几何"。
**这个结论是错的。** 直接读基线的解就推翻了它：

* `9a6d6d90` 的基线解里已经有 **3 个 `out-std`**，`426d0058` 有 **2 个**。
  `CompileSharedOrdinaryOutputs` 走 `BuildSharedAssemblyTarget`，本来就为每个
  output 发射一个输出对象。
* `decode.c` 的 `number_of_unplaced_outputs` 确实要求"每个 output 恰好一个
  `out-std`/`out-rep`"，而 `decode_molecule` 用
  `mechanism_relative_position(m, offset)` 计算产物坐标 —— 产物坐标是**相对
  `out-std` 的**，所以多个 output 定义在相同坐标（`426d0058` 的 `out0` 与 `out1`
  都是 `Gold(0,0)`）是合法的。我们的编译器早就用对了这个语义。

所以放开多输出**不会有任何收益**：它已经是支持的了。

### 8.2 真正的瓶颈：单条 tape 就是周期

解剖四种落后题的基线解（`bench-ordered-network-20261112` 的 control 结果）：

| 题 | cycles | 总指令 | 最长单条 tape | 占比 |
|---|---:|---:|---:|---:|
| `1dc60983` | 78168 | 81742 | **62453** (`piston`) | **80%** |
| `9a6d6d90` | 40389 | 94940 | 26887 / 23623 / 20359 | 67% |
| `6c4bbc37` | 39780 | 48063 | **38989** (`piston`) | 98% |
| `f613a067` | 52925 | 55015 | 36061 (`piston`) | 68% |
| `426d0058` | 18619 | 804 | 569 (`piston`) | 3% |

前三行（以及 `f613a067`）属于同一类：**一条 `piston` 的 tape 含 3.6–6.2 万条
指令，它自己就决定了周期**。这类题的参考解是 562–1502 拍，所以是 26–94× 的差距。

`426d0058` 是另一类，算术非常干净：

```
18619 / 4 (outputMultiplier) = 4655  ≈  6 x 786
786  = 该解最长 tape 覆盖的拍数
6    = 每份产物之前需要清掉的 surplus 原子数
```

即**每份产物要先串行清理 6 个垃圾原子**，而解里只有 **1 个** `glyph-disposal`
和 **1 个** `wasteArm`。

顺带修正 §7 的另一个说法：这些机器的**并发并不低**（按时间桶统计，`1dc60983`
平均有 110+ 个对象带 tape），周期不是吞吐问题，而是**端到端延迟**：
`mult=1` 的题只生产一份产物，所以 cycles 就等于单份产物的延迟。

### 8.3 为什么它是串行延迟

`LifeDeath.cpp` 的调度核心是一个全局游标：

```cpp
int shift = 2;
int activeArm = 0;
auto emit = [&](char action) {
    machine.objects[activeArm].instructions.push_back({action, shift++});
};
...
for (int t = 0; t < period; ++t) {
    while (event < events.size() && events[event].time == t) { ... emit(...) ... }
    ++shift;
}
```

同一个 `t` 上的**所有** event 依次 `emit`，`shift` 单调递增，所以即使 `activeArm`
在 event 之间切换（`producer.carrier` / `transport` / `outlet.arm` / `spareArms[f]`），
不同臂上的动作也被排到了不同时刻。

而真正阻止"只改时钟就能并行"的是：**所有 event 共用同一条 `transportPath`**。
`moveTransport` 依赖全局的 `transportPosition`，两个 event 同时移动就会互相踩。
同样的约束出现在 `RowAssembly.cpp`：`wasteArm` 和 `glyph-disposal` 都是单份，
坐标写死在 `{-feedOffset + 3, 0}` / `{-feedOffset + 2, 0}`，整条 conveyor 的布局
不变量围绕它们建立。

### 8.4 下轮的起点与预期

要拿到数量级收益，需要的是**复制运输通道**，而不是调时钟：

1. **`LifeDeath.cpp`：每原子一条 transport 通道。** 现在 `transportPath` 是单条，
   所有被拆下的原子排队通过。改成"每条通道服务一个产物原子、各带独立游标"后，
   延迟从 `原子数 x 通道延迟` 降到 `通道延迟`。`1dc60983` 的 62453 条 tape 里，
   按 10 个产物原子摊分，理论目标 ~6000 拍（13×）。
2. **`RowAssembly.cpp`：多 `wasteArm` + 多 `glyph-disposal`。** `426d0058` 的算术
   给出明确预期：6 个 surplus 原子并行清理后单份延迟 4655 → 786，总量
   18619 → ~3144（**5.9×**）。

两项都需要重排布局（新增并排的轨道与 glyph），不是局部改动，且必须靠原生门兜底
验证——形不成合法解时 `attempt` 会失败并回退，不会产出错误答案。

**本轮不改代码**：这两处都属于"重排布局 + 重新推导不变量"的规模，在没有独立的
布局验证手段之前贸然改动，最可能的结果是又一个零收益且带来回归的提交。

## 9. 第五轮：周期归因工具与落后题的二分

### 9.1 新工具 `tools/why_slow.py`

前面四轮每一次都要手工解析 `.solution`、排序 tape、和 oracle 对照，而且
结论经常反直觉（并发度很高却依然很慢）。现在这件事是一条命令：

```
python3 tools/why_slow.py 1dc60983 6c4bbc37 426d0058 --top 3
```

它打印周期、oracle 比、最长 tape 的持有者与占比、每条 tape 的动作分布
（`extend/retract/grab/release/rotate` 与 idle 分开计数），并给出一行判定：

* `ONE TAPE IS THE PERIOD` —— 单条 tape 占周期 > 50%。**缩短这条 tape 才有用，
  在别处加并行无效。**
* `WAITING, NOT WORKING` —— 指令总数不到周期的 10%。机器大部分时间在空转，
  要找 surplus 清理或握手停顿，而不是找更短的 tape。
* `DISTRIBUTED` —— 最长 tape 占比不高但指令铺满，代价是沿链的 relay 延迟。

### 9.2 `1dc60983` 的 62453 条指令到底是什么

工具第一次跑出来就把上一轮的猜测证伪了：

```
obj 112  piston (21,88)  62453 instr  span 80317
    extend 31080, retract 30604, rotate ccw 216, rotate cw 216, idle 1
```

31k 次 extend + 30k 次 retract，只有 216 次旋转 —— 这**不是**在搬运什么复杂东西，
而是**同一条 L 形轨道上反复横穿**。按 `transportPath` 的构造：

```
(q, 88) --水平--> (bend, 88) --垂直--> (bend, 6) --水平--> (feed.cell.q, 6)
bend = min over all tracks with r <= 6 of (track.q + offset - 2)
```

`bend` 被面板上**所有低处轨道**推到最左端，所以每次往返 = `|pickup-bend| + 82 +
|bend-cell.q|` ≈ 600 格。52 次往返 x 600 x 2（去+回）≈ 62400，与实测吻合。

**根因不是"搬运太多原子"，而是"下降点被挤到面板另一头"**。

### 9.3 120 题的周期归因分布

对 `bench-ordered-network-20261112` 的 control 解逐题跑同样的判定：

| 模式 | 全部（120） | 落后题（ratio > 2，110 道） | 落后题的 p50 ratio | p50 最长 tape 占比 |
|---|---:|---:|---:|---:|
| `dominant`（单 tape 主导） | 36 | **36** | **25.4×** | **0.69** |
| `distributed`（延迟沿链） | 83 | **73** | **13.1×** | 0.16 |
| `waiting`（空转） | 1 | 1 | 11.7× | 0.03 |

ratio 最高的 15 道题里有 12 道是 `dominant`：

```
1dc60983 dominant 100.5x  62453/78168 (0.80)
cd9b4ea9 dominant  96.7x  10954/20016 (0.55)
deecf24d dominant  96.7x  24658/39834 (0.62)
f613a067 dominant  94.2x  36061/52925 (0.68)
ac8d3e1b dominant  87.7x  45212/58318 (0.78)
89d6e08e dominant  87.5x  74120/94550 (0.78)
fc6a70eb dominant  85.8x   7439/10549 (0.71)
```

**两类需要完全不同的修复，不能靠同一个改动解决**：

1. `dominant` 36 题 —— 缩短那条 tape。具体到这批题，就是**重新设计分子原子
   从 pickup 到装配区的运输拓扑**（短竖线 + 局部化下降点），而不是继续用一条
   绕到面板左端的共享 L 形轨道。
2. `distributed` 73 题 —— 最长 tape 只占 16%，说明机器已经在并行工作，代价是
   沿接力链累积的延迟。要动的是**链的段数**。

### 9.4 本轮排除的候选：`compactRelay`

`LifeDeath.cpp` 里已有一个 `compactRelay` 模式，用局部 `arm1`（`cell.q - 4`、
`assemblyTop + 16`）代替长 L 形 transport，正是 9.2 描述的修复形态。把它接入
portfolio 后实测：

```
life-death-compact-relay event=unsupported
  detail=compact relay supports one direct/calcified molecular feed, no coproducts
```

放开这条域检查（`OPUSOPTIMA_LD_COMPACT_WIDE`）后：

```
life-death-compact-relay event=unsupported
  detail=compact relay requires exactly one extracted molecular channel
```

`feeds` 是**去重后的产物元素种类**，`molecularFeeds` 是多 feed 时被扩展出来的
通道。`compactRelay` 的几何前提就是"单一分子通道"，多元素产物需要复制通道 ——
又回到同一个根本问题。

**结论**：它只能覆盖"产物只有一种元素"的题，而这类题的基线本来就是 3 cycles。
接入它零收益，改动已回退。

## 10. 第六轮：per-feed 通道重写，以及它为什么在几何上不成立

上一轮 §8.4 给出的目标是「每原子一条 transport 通道」（对 `1dc60983` 预期 13x）。
这一轮真的去实现了，结果是**否定**的，而且否定得很干净。

### 10.1 先拿到精确几何

```
transport track: 587 cells, q=[21,525], r=[6,88]
molecular sources: q = 36, 96, 156, 216, 276, 336  (r=114)
feed.cell:         q ~ 516..525                    (r=6)
out-std:           (544, 2)
```

`1dc60983` 的 puzzle 只有 **1 个分子输入**；那 6 个 `input` 对象是 `single.inputs`
里的 **proxy 元素**（每个产物元素一个）。它们被排成一行、间距 60，横跨 300 格，
而唯一的产物框在最右边。于是 587 格里 **504 格是纯横穿**，53 次往返 x 1174 步
= 62453，与主导 tape 完全对上。

### 10.2 实现：一个 feed 一条独立 L 通道

把共享的 `transport` / `transportPosition` / `transportPath` 换成按 feed 索引的
三组向量，每条通道 `(pickup_i, 88) -> (pickup_i, 6) -> (cell.q_i, 6)`，
`moveTransport` 按 feed 取槽位。开关 `OPUSOPTIMA_LD_FEED_PARALLEL`（默认关）。

第一次跑：

```
status=rejected detail=collision during motion phase
errorCycle=1118 errorLocation=(321,88)
```

`(321,88)` 是 6 条通道的起点行。原因是 **6 条水平段全部落在 r=6**，
`tracks share a global junction table`，同格的轨道直接冲突。

### 10.3 第二次尝试：一条通道一条车道

给第 i 条通道分配 `lane = 6 + i`（r=6..11），交付时用 `lane - feed.cell.r` 次
`'E'` 下探。仍然：

```
status=rejected detail=collision during motion phase
errorCycle=1118 errorLocation=(321,88)
```

dump 出来的轨道表说明了原因：

```
obj15  pos=(528,3)   q=[515,528] r=[3,8]   n=31   <- 装配区的轨道
obj113 pos=(321,88)  q=[321,525] r=[6,88]
obj115 pos=(261,88)  q=[261,522] r=[7,88]
obj117 pos=(201,88)  q=[201,523] r=[8,88]
...
```

**装配区本身占满了 r=3..8。** 车道 7 和 8 直接穿过装配区的轨道。
r 往上是装配区，往下是产物框（`out-std` 在 r=2），**没有任何一行空出来给
第 2 到第 6 条水平车道**。

### 10.4 这个否定的意义

这不是"还没调好"，而是一个几何上的不可能性：

> 在「6 个分子源横向铺开 300 格 + 唯一产物框在最右 + 装配区占满 r=3..8」的布局下，
> 不存在 6 条互不冲突的水平通道。

所以 **§8.4 的 13x 方案（每原子/每 feed 一条通道）不成立**。504 格的横穿不是路由
问题，是**布局本身**决定的：分子源与产物框的距离。

要真正消掉它，必须动的是更高一层的东西——**feed 工厂的排列方式**（例如把 6 个
proxy 工厂竖排、或让分子源贴着产物框环绕），而不是复制运输通道。

### 10.5 顺带排除：共享 feed 变体

`CompileSharedLifeDeath`（`OPUSOPTIMA_SHARED_FEED_RESOURCE=on`）本来就是为
"多个分子源"准备的，注释记录 A/B 为 improved=4/regressed=0。在 6 道落后题上实测：

```
1dc60983  78168 -> 78168  (变体根本没被触发)
6c4bbc37  39533 -> 39533
9a6d6d90  40387 -> 40387
f613a067  52923 -> 52923  (触发但无改善)
89d6e08e  94548 -> 94548
550b9c1c  50470 -> 50470  (触发但无改善)
```

对这批题无效。改动已回退。

### 10.6 现状

`dominant` 36 题的修复目标仍然成立（缩短那条 tape），但**已经证伪了两条实现路径**：
复制 transport 通道（§10.4）和 compact relay（§9.4）。剩下的唯一方向是重排
feed 工厂，使分子源与产物框相邻——那需要重新推导整个装配区的放置规则，而不是
在现规则上打补丁。

## 11. 第七轮：widened intake 的诊断，以及它为什么也接不上

### 11.1 一条被埋起来的线索

`RowAssembly.cpp` 的注释里有一句关键记录：

> widening the intake past the compact layout (`allowCompactLayout == false`) gives 14
> puzzles 10-25% lower cost and shorter periods (**b2c42099 755->635,
> cd9b4ea9 7610->1620**), but ... 31 of 146 puzzles stopped producing. It therefore
> only runs inside `RowIntakeModeScope(2)`, i.e. as a sibling candidate.

`cd9b4ea9` 正是我们的落后题（当前 20016 cycles，oracle 207）。而它的 widened
版本曾经测到 **1620**——12x。这个 sibling 也确实接进了 portfolio
（`submitIntakeSibling("life-death-local-intake", ...)`，见 `CompileLifeDeathLocalIntake`
里的 `RowIntakeModeScope widened(2)`）。

### 11.2 它从来没成功过

```
Assembly branch=life-death-local-intake event=unsupported
  detail=life/death pickup schedule unsupported
```

对 `1dc60983` 和 `f613a067` 都停留在这里。`cd9b4ea9` 甚至没走到这个 stage。

把裸消息换成带数据的诊断后：

```
events=0  factories=4  tape=221  reach=[0,9]  distances=-7,-3,1,5
```

### 11.3 两层原因，第二层是结构性的

**第一层（可修）：** `factories[f].distance = 5 + 2 * (-feedOffset - cell.q)`
假定所有工厂都在 feed arm **左侧**，distance 才是正的。widened intake 把工厂放到
右侧，于是 distance 变成负数（-7,-3），而 tape 的净 `'A'`/`'a'` 位置永远 ≥ 0，
不可能匹配。把公式改成对方向对称
（`5 + 2 * max(-feedOffset - cell.q, cell.q + feedOffset)`）后：

```
events=0  factories=4  tape=221  reach=[0,9]  distances=17,13,9,5
```

distance 正了，`9` 和 `5` 也确实落在 `reach=[0,9]` 里——**但 events 仍然是 0**。

**第二层（结构性）：** `tape=221` 条指令只让 feed arm 走了 **0..9 格**。旧几何里
feed arm 是一台水平输送器，靠走到每个工厂列来「报到」；`events` 正是通过
「`'G'` 时刻的净位置 == 工厂 distance」来检测每次取料的时刻。widened intake 把
feed arm 换成了一套**局部抽取机**（`wideOnly` 下它的位置被搬到 `-20` 行、轨道只剩
两格），它不再横穿工厂列。

所以 **`events` 检测机制与 widened intake 的 intake 机在结构上不兼容**，不是符号
问题。修它等于重写 `CompileLifeDeathGeometry` 里「取料时刻 → tape 拼接」这段
（即 §8.3 描述的那个全局 `shift` 循环），改动面与 §10 的布局重写相当。

### 11.4 结论

这一轮的三次尝试全部失败并已回退：

| 尝试 | 结果 |
|---|---|
| per-feed transport 通道（§10） | 几何不可能：装配区占满 r=3..8 |
| 每通道独立车道 r=6..11 | 同上，车道 7/8 撞装配区 |
| 修距离符号让 widened intake 生效 | distance 已修正，但 events 检测机制不适用 |

第三次给了新信息：**widened intake 这个「12x 的现成变体」不是调参问题，而是
它的 intake 机与 `events`/`shift` 拼接机制互不相容。**

### 11.5 需要承认的判断

`LifeDeath.cpp`（1100 行）的布局、`events` 检测、全局 `shift` 拼接三者互相咬合在
「feed arm 是一台横穿工厂列的水平输送器」这一个假设上。widened intake 与
per-feed 通道各自打破了这个假设，因此都不是补丁能修的。

要让这批题变快，需要**重写 `CompileLifeDeathGeometry` 的取料与调度部分**：把
「用 tape 位置反推取料时刻」换成显式的调度表，让布局与 intake 形态解耦。这是一个
独立的重写任务，不应继续以「在现有函数上打补丁」的方式推进——连续三轮的失败已经
说明了这一点。

## 12. 第八轮：显式调度表接线成功，widened intake 首次产出机器

### 12.1 换掉了什么

§8.3 的 `events` 是靠**走 feed arm 的 tape**反推取料时刻：

```
position = net('A') - net('a') up to each 'G'
if (position == factory.distance) events.push_back({'G' tick - distance - 8, factory})
```

这套推断在 widened intake 下必然失效（§11.3）。本轮改成读 `CompileRows` 已经能
输出的 `SharedFactoryEmission` 表：每个原子一条，带 `beginCycle` /
`holderReleaseCycle` / `readyCycle` / `releaseCycle` 和它的 `consumer`。

接线：`LifeDeath.cpp` include `RowAssemblyInternal.hpp`（它的 `CompileRows` 声明带
默认参数），普通分支改为

```
CompileRows(single, plan, ..., feedSpacing, ..., &emissions, &emissionOwners)
```

`emissionOwners` 是 `{0, atom}` 序列，`emissions` 在 `feedOfAtom` 的辅助下映射回
factory（`Factory` 增加了 `feed` 字段）。

### 12.2 关键校准：`beginCycle` 就是原来那个数

对 `1dc60983` 同时打印两边的时刻：

```
tapeEvents = 0/3  115/1  204/0  293/0
emissions  = beg=0 hold=77  rdy=83  rel=96
             beg=115 hold=168 rdy=174 rel=187
             beg=204 hold=245 rdy=251 rel=264
             beg=293 hold=334 rdy=340 rel=353
```

**`beginCycle` 与 tape 反推出的 `'G' - distance - 8` 逐项相同。** 所以旧公式里的
`- distance - 8` 不是启发式，而是 `beginCycle` 的定义所隐含的；我最初又减了一次，
得到 `lowest=-37`，被 `Reject` 挡住——这是本轮唯一一次真正的"符号"错误，且立刻
被诊断日志抓住。

### 12.3 一个必须按"提取批次"去重的细节

`emissions` 有 10 条（10 个产物原子），但 events 只能是 4 条。原因是**一次
dispersion / unification 会在同一个 `beginCycle` 把几个原子交给不同 factory**，
而 tape walk 对这次提取只记一次。按 `beginCycle` 去重后 events=4，与旧方法一致。

### 12.4 结果

**普通布局**（`OPUSOPTIMA_LD_EMISSION_SCHEDULE=on`）：

| 题 | tape 反推 | 显式表 |
|---|---:|---:|
| `1dc60983` | 78168 | 80543 |
| `f613a067` | 52925 | 54410 |

能过门，但**慢 3%**——factory 分配顺序与原方法不完全一致（同一 factory 被服务两次
的情况在表里分成两次提取）。所以默认关闭。

**widened intake**（`CompileLifeDeathLocalIntake`）：

```
Assembly branch=life-death event=emission-schedule events=10 period=678 lowest=0
```

**第一次成功编译出机器**：`period` 从普通布局的 **1078 降到 678（-37%）**。此前这个
sibling 只会报 `life/death pickup schedule unsupported`。

同一题普通布局的 `period` 是 1078、周期 78168，即 `1dc60983` 的周期/period ≈ 72。
若这一比例保持，678 的布局对应约 **49000 周期**（78168 -> ~49000，约 1.6x）。

**它仍然被 gate 拒绝**：

```
detail=collision during motion phase
errorCycle=840 errorLocation=(519,3)
```

`(519,3)` 上是 feed arm 的 `piston`/`track` 起点，右邻 `(522,3)`/`(523,3)` 是两个
`glyph-life-and-death`。widened 布局把 arm 推到了 glyph 旁边，延伸时相撞。

### 12.5 状态

* 显式调度表这条通路**是通的**：普通布局原样复现（慢 3%），widened 布局首次产出
  机器。
* 剩下的唯一障碍是 widened 布局下 feed arm 与 life-and-death glyph 的**摆位冲突**，
  属于 §12.4 里那个 `(519,3)`。
* 默认全部关闭（`OPUSOPTIMA_LD_EMISSION_SCHEDULE` 必须显式 `=on`），默认行为与
  §11 完全一致：`1dc60983` 78168 / `f613a067` 52925 / `89d6e08e` 94550。
* **本轮没有性能提升。**

下一轮的具体入口已经很清楚：在 `wideOnly` 布局里给 `spareArms` / life-and-death
glyph 让位，或把 feed arm 的起点整体右移 4 格，让出 `(519,3)` 这段延伸路径；然后
测 `period=678` 的机器能否过门并带来 1.6x。

## 13. 第九轮：`(519,3)` 换行尝试失败，widened intake 与 animismus 工厂互斥

§12.5 给出的入口是「给 feed arm / life-and-death glyph 让位」。这一轮把它做成了可调
行号 `OPUSOPTIMA_LD_ANIM_ROW`（默认 3，即原值），让 animismus 工厂整体（glyph、
`addFeed` 的 cell、`glyph-calcification`）一起在行方向平移，然后逐个试。

### 13.1 冲突对象

先把两版机器分别 dump 出来（dump 文件名加了对象数后缀，否则后一次会覆盖前一次）：

```
var.130  (普通布局, 130 对象)  passed   cycles=80543
var.132  (local-intake, 132)   rejected collision during motion phase at (519,3)
```

`(519,3)` 上的 `obj20` 是 **`directInput` 的 `intakeArm`**（`piston`,方向 4，
`track` 10 格），不是 feed arm；它的轨道沿 r=3 横穿，而 `obj16/obj17` 是
`(523,3)`/`(522,3)` 两个 `glyph-life-and-death`。**轨道直接压在 glyph 上。**

### 13.2 换行结果：没有可用的行

| `OPUSOPTIMA_LD_ANIM_ROW` | 结果 |
|---|---|
| 3（原值） | local-intake 仍 `collision at (519,3)` |
| 2 | `collision at (540,0)`，cycle 4573 |
| 1 | `collision at (540,0)`，cycle 4577 |
| 4 | `collision at (540,0)`，cycle 4565 |
| 5 | `collision at (522,6)`，cycle 2754 |

行 1/2/4 撞到 `(540,0)` —— 产物框与 `out-std`(544,2) 那一带；行 5 撞到 `(522,6)`
（transport 的装配行）。**r=3 是 animismus 工厂唯一可用的行，而它正是 intakeArm
的轨道行。**

### 13.3 这是一个架构性互斥

`intakeArm` 的轨道由 `CompileRowAssembly` 在 `directInput` 模式下铺在 r=3
（注释：「the rail sits on row 3: extension 3 grabs row 0, 2 grabs row 1 and 1 grabs
row 2」）。而 `LifeDeath` 的 animismus 工厂把 glyph 硬编码在同一行
（`o.position = {q + (type == Mors), 3}`），因为分子 feed 的 cell 也在 r=3。

两者都正确，但彼此不知道对方的存在：`CompileRows` 返回时还不知道 `LifeDeath` 会在
那里放 glyph。所以「让 intakeArm 绕开 glyph」需要在 `CompileRowAssembly` 里预留
LifeDeath 的工厂位置——那等于把化学布局下推到 row 层，是 §10.4 那个量级的重构。

### 13.4 状态

* `period=678` 的机器**仍然被 gate 拒绝**，所以 §12.4 里外推的 1.6x 依然是**未兑现**的。
* 换行实验全部无效，改动已回退。仓库回到 `eca3998`：`1dc60983` 默认 78168。
* 显式调度表通路（§12）保持有效，仍然默认关闭。

累计已证伪的路径：复制 transport 通道（§10）、compact relay（§9）、shared feed
（§11.5）、widened intake 换行（§13）。四者的共同根因都是同一件事：
**`CompileRows` 的布局与 `LifeDeath` 后加的化学工厂各自假定 r=3 / r=6 是空的。**

## 14. 能力缺口核对：缺的不是机构种类，是时间轴的并行

前面九轮一直在 `LifeDeath.cpp` 的几何里打转。这一轮换了个问法：**oracle 的记录里带了
参考解用了多少个 `arm1` / `piston` / `track` / `bonder`**，直接对比就知道差在哪。

### 14.1 机构构成对比（139 题，control 解）

| 组 | 参考 `arm1` | 我们 `arm1` | 参考 `piston` | 我们 `piston` | 参考 `track` | 我们 `track` |
|---|---:|---:|---:|---:|---:|---:|
| 全部 `linear/mono`(39) | 7 | **0** | 0 | 1 | 2 | 1 |
| 全部 `pipeline/heavy`(7) | 6 | **0** | 8 | 13 | 4 | 5 |
| 全部 `pipeline/cheap-arms`(29) | 16 | **0** | 5 | 14 | 6 | 6 |
| 全部 `universal`(64) | 8 | **0** | 6 | 16 | 3 | 5 |
| **落后题(>2x, 110)** | **11** | **0** | 5 | **14** | 4 | 5 |
| 已赢题(≤2x, 29) | 5 | 0 | 2 | 0 | 2 | 0 |

**我们几乎不用 `arm1`（中位数 0）。** 落后的地方全部由 `piston` + `track` 承担，
而参考在同样的题上用 9–29 个 `arm1`。

单题样本：

```
key        ref   refArm ourArm   refPis ourPis   refTrk ourTrk  ratio
1dc60983   778       15      1        7     49        4     44  100.5
cd9b4ea9   207       13      0        3     46        7     34   96.7
f613a067   562       23      0        3     38        8     30   94.2
ac8d3e1b   665       21      0        6     96        6     88   87.7
```

### 14.2 但"多用 arm1"并不是答案

有两题我们确实用了大量臂：

```
1baade11  cycles=15493  arms=166  137 个臂同时有 tape   totalInstr=5816  ratio=66.5
1b47844e  cycles=13315  arms=112   74 个臂同时有 tape   totalInstr=1804  ratio=45.1
```

臂是**真并行**的（中位 137 个同时活动），`totalInstr` 也只有 5816，可 `cycles` 是
15493 ——**周期是内容的 2.7 倍，而内容只跨 8001 拍**。所以问题不在"臂不同时动"。

### 14.3 真正的指标：周期就是指令总数

对 110 道落后题统计两个比值：

```
instr / cycles        p50 = 1.02   p90 = 1.89
lastTick / cycles     p50 = 1.00   p90 = 1.02
```

* `instr/cycles ≈ 1`：**每条指令占一拍**，周期就是全部指令数。
* `lastTick/cycles ≈ 1`：**最后一条指令就在周期末尾**，时间轴被从 0 到末尾完整铺满，
  没有任何两个机构的时间线重叠。

对照最差的 12 题，这两个值几乎都是 `1.00`：

```
1dc60983  78168 cycles  81742 instr  1.05  1.00
cd9b4ea9  20016         40956        2.05  1.00
9a6d6d90  40389         94940        2.35  1.00
```

**这就是缺的能力：把 N 个机构的操作首尾相接成一条时间轴，改成多条短时间线并行重叠，
period 取 `max` 而不是 `sum`。**

参考解 22 个机构做 778 拍；我们 94 个机构做 78168 拍。机构更多、周期却是 100 倍，
因为我们的周期是累加的。

### 14.4 怎么补

`LifeDeath.cpp` 的调度核心是一个全局单调游标：

```cpp
int shift = 2;
auto emit = [&](char action) { machine.objects[activeArm].instructions.push_back({action, shift++}); };
for (int t = 0; t < period; ++t) { ...处理 events...; ++shift; }
```

要改成：

1. **每个 object 一个 cursor**（`cursor[i]` 初值 2），`emit` 用 `cursor[activeArm]++`；
2. `period = max(cursor)`，而不是 `shift`；
3. 跨机构的真实依赖（`Reserve`/`Release`、分子 program 拼接）仍按事件本身的时刻对齐
   ——§12 已经把 `beginCycle` 接出来，正是干这个用的。

上限估计：`1dc60983` 有 94 个搬运机构，若它们的时间线能完全重叠，
`81742 / 94 x 2 ≈ 1700` 拍量级，即 **25x 以上**的空间；保守取 4 倍也有 20x。

### 14.5 为什么前九轮没做成

因为每一次都在**同时**改几何（复制 transport 通道、per-feed 竖井、换行）和调度。
几何改动一律被 gate 拒绝（§10.4、§13），于是调度那条主线从没被单独测过。

**下一轮只改时间轴，坐标一个都不动。** 如果 `1dc60983` 的 `instr/cycles` 从 1.02
开始下降、而 gate 仍然通过，就说明这条路是对的；如果 gate 仍拒绝，那拒绝原因会
直接指出哪两个机构真的需要在时间上互斥——那才是需要保序的最小集合。

## 15. 第十轮：per-feed 时间轴，第一次真实收益（`cd9b4ea9` 6.6x）

按 §14.4 落的改动，**坐标一个没动**，只换了时间轴。

### 15.1 改了什么

`emit` 原来直接写 `shift++`（一个全局游标）。现在：

* 每个 molecular feed 一个 `feedShift[i]`，进入该 feed 的块时把 `clockPtr` 指过去；
* 每个 object 一个 `objectCursor[i]`，写指令时 `cursor = max(cursor, clock)`，
  写完 `clock = max(clock, cursor)`。这条是必须的——decoder 会拒绝
  `arm with two instructions that have the same index`，而共享的 courier
  （transport 轨道）会被多个 feed 的时间线写入；
* 结束符 `'B'` 放在 `max(globalShift, 所有 feedShift)`。

开关：`OPUSOPTIMA_LD_FEED_CLOCKS`（env）或 `CompileLifeDeathClocked`
（thread-local 作用域，和 `RowIntakeModeScope` 一个模式）。

### 15.2 结果

```
puzzle      default   per-feed clocks
cd9b4ea9      20014              3033     <- 6.6x
1dc60983      78168                 -     gate: collision at (81,89)
f613a067      52925                 -     gate: same class
ac8d3e1b      58318                 -
89d6e08e      94550                 -
550b9c1c      50470                 -
9a6d6d90      40387                 -
6c4bbc37      39780             39780     <- no change (unchanged period driver)
```

`cd9b4ea9` 的 oracle 是 207，所以从 96.7x 压到 **14.7x**，是这条线上第一个真实收益。

### 15.3 为什么其余题被拒

`collision during motion phase`，位置落在 courier 一带（`(81,89)`）。
当多个 feed 共用同一个 courier 时，它们的时间线不能独立前推——这条依赖是真的。
`objectCursor` 只保证「同一对象的指令严格递增」，并不保证「两个 feed 不会同时
争用同一条轨道」。

**这正是 §14.5 预告的情形**：gate 直接指出了需要保序的最小集合 —— 不是整条
transport 串行（那是旧结论），而是"共用 courier 的那几个 feed 之间"。

### 15.4 接线受限于提交预算

`sibling` 方式没生效：多输出题（`cd9b4ea9` 有两个 output）的
`ordinaryPortfolio` 为 false，`maxAccepted` 是 1，life-death 一成功
`acceptedCount >= acceptedCap`，兄弟候选直接返回。所以 `cd9b4ea9` 这类题只有
一次提交预算，时钟变体**必须替换主路径**才能生效，而不能作为额外候选。

这也解释了 `submitIntakeSibling` 里那句 `puzzle.outputs.size() != 1` 的来源。

### 15.5 状态

* 默认完全不变：`1dc60983` 78168 / `cd9b4ea9` 20014 / `f613a067` 52925，单测 21/21。
* `OPUSOPTIMA_LD_FEED_CLOCKS=1` 时 `cd9b4ea9` 20014 -> 3033。
* 其余多 courier 题会因 motion collision 被 gate 拒绝，所以**不能默认开启**。

### 15.6 下一步（已由数据确定，不是猜测）

按共用 courier 分组：同一个 courier 上的 feed 共享一个时钟，不同 courier 之间
独立。这样 `cd9b4ea9` 的收益保留，而 `1dc60983` 那类题也不再冲突——
`(81,89)` 那个位置就是冲突的 courier，它的使用者应该同属一个时钟组。

### 15.7 追查 courier 依赖的结果（未增加收益）

上一节说「按共用 courier 分组」。追下去发现障碍比预想的更底层。

**加锁无效。** 给 transport 加独占区间（`transportLock`，feed 用它之前等它空闲，
用完把锁推到自己的时钟），`1dc60983` 依然 `collision at (81,89)`。

**spare arm 时钟也不对。** `spareArms` / `spareOutlets` 服务某个 factory，却跑在共享
时钟上，与它对接的 feed 错位。改成按 `factories[f].feed` 选时钟后，
`cd9b4ea9` 仍是 3033（未变差），但 `1dc60983` 仍被拒。

**真正的原因从 dump 里看得出来：`(81,89)` 附近没有任何对象。**

```
objects at/near (81,89):
  (none)
```

冲突的是**运动中的两个原子**，不是机构。`r=89` 在 carrier 轨道下方，`q=81` 正好是某个
feed 的 pickup 列。所以这是**物料在同一位置堆积**，而不是机构互相穿插。

这就把问题从"哪些机构必须保序"变成了"哪些原子不能同时出现在同一格"——需要的是
逐 feed 的物料守恒/时序约束，而不是机构级的锁。前者正是 §14 里那个 `events` 表能给
的信息（每个原子何时就绪），但把它变成「原子占用声明」是另一次重构。

**本轮状态**：两处改动（transport 锁、spare arm 时钟）在语义上更正确，且
`cd9b4ea9` 的 6.6x 保持不变；但它们没有让其余 5 题通过。默认路径逐题验证不变
（78168 / 20014 / 52925），单测 21/21。**本轮没有新增收益。**

---

## 16. 为什么剩余收益全卡在 LifeDeath（能力定位，2026-11-12）

### 16.1 多时钟机制其实早就实现了

`RowAssembly.cpp:1330` 的 `feedPrefetch` / `rowPipeline` / `assemblyClocks` 就是完整的
多时钟架构：

```cpp
const bool assemblyClocksRequested =
    parallelClocks || RowClockEnabledFrom("OPUSOPTIMA_ROW_ASSEMBLY_CLOCKS");
const bool rowPipelineRequested = rowPipelineFlag || assemblyClocksRequested;
const bool feedPrefetch =
    (feedPrefetchRequested || rowPipelineRequested) && !expandedFeeds && !completionTransfer &&
    !mixedInventory && !reactionAssembly && !animismus && !sharedOutputs && !labels &&
    !productOrigins && !supplied && !validationPuzzle && !custody && !emissions && !owners &&
    !singleProduct && !finiteProducts && !componentRows && !bufferedSupply;
```

三个时钟：`feedTime`（送料臂）、`convTime`（传送带空载回程）、`upperTime`（上臂
回程/重抓/伸长尾段），`write()` 入口用 `awaitFeed/awaitConv/awaitUpper` 做**窄同步**：

```cpp
auto write = [&](const std::vector<int>& arms, char action) {
    if (std::find(arms.begin(), arms.end(), feedArm) != arms.end()) awaitFeed();
    if (assemblyClocks && conveyor >= 0 && arms 含 conveyor) awaitConv();
    if (assemblyClocks && std::any_of(arms, isUpperArm)) awaitUpper();
    ...
};
```

默认开启（`OPUSOPTIMA_ROW_ASSEMBLY_CLOCKS=off` 是总关闭），所以**能走的题早就在走**。

### 16.2 排除条件把 LifeDeath 完全挡住

条件里有两个 must-fail：`!animismus` 和 `!emissions`。而 `LifeDeath` 路径**必然两者都有**：

- `animismus`：`Factory::animismus` 为真时走 spare arm 重抓，这是 life-death 的定义特征
- `emissions`：`emit(instr, beginCycle)` 拼出的显式调度表就是 emissions

实测 `OPUSOPTIMA_ROW_ASSEMBLY_CLOCKS=off/on` 对 6 道落后题的影响：

| key | off | on | 变化 |
|---|---:|---:|---:|
| `1dc60983` | 78323 | 78168 | −0.2% |
| `f613a067` | 53048 | 52925 | −0.2% |
| `9a6d6d90` | 40478 | 40387 | −0.2% |
| `6c4bbc37` | 40000 | 39780 | −0.6% |
| `89d6e08e` | 94754 | 94550 | −0.2% |

只有 0.2–0.6%，**说明它们根本不进那条分支**——开关对它们几乎无意义。

### 16.3 于是缺口变成一个有界任务

> 让 `animismus` 与 `emissions` 兼容多时钟。

具体要解决两件事：

1. **`emissions` 与独立时钟的相容**：现在 emissions 的 `beginCycle` 是**按共享 `time`
   累加**推导出来的绝对时刻。一旦 `feedTime`/`upperTime` 独立推进，`beginCycle` 的语义
   就要从「累加到第几拍」改成「依赖事件的最早时刻」，即 §15.2 的 `sum → max` 变换。
   实现路径与 `LifeDeath.cpp` 里 `objectCursor[] + clockPtr` 完全同构，只是位置换到
   `RowAssembly.cpp` 的 `write()`。
2. **`animismus` 与独立时钟的相容**：spare arm 的重抓是**跨时钟手递手**
   （feed 交出 → spare 接手 → upper 取走），必须像 `awaitFeed` 一样插一个
   `awaitSpare()`，否则重抓会在错误的拍子上发生。§15.7 试过「给 transport 加锁」但失败了，
   失败原因是**锁加在 transport 而不是在 hand-off 点**。

### 16.4 结论

- §14 的 `instr/cycles ≈ 1.02` 诊断没错，但**修法不是重写调度器**，而是把已在
  `CompileRows` 里验证过的三时钟模型搬进 `LifeDeath` 的路径。
- 这条路有明确的两个子任务和一个已验证的实现模板（`LifeDeath` 的 per-feed clocks），
  不再是「不知道缺什么」的状态。

---

## 17. write() 内部加 per-arm cursor 的决定性实验（2026-11-12）

### 17.1 假设

§14 的 `instr/cycles ≈ 1.02` 说明周期 = 指令总数，没有并行空隙。`LifeDeath.cpp`
用 `sum → max` 拿到了 6.6×，那么把同样的变换搬进 `RowAssembly.cpp` 的 `write()` 应该
也能把周期从「指令之和」压到「最长臂的指令数」。

### 17.2 版本 1：`at = max(time, armCursor[arm])`

在 `write()` 里先取 `time` 为参与臂 cursor 的最大值，写完后回写 cursor：

```cpp
const int cursorFloor = time;
if (objectClocks) {
    int at = time;
    for (int arm : arms) at = std::max(at, armCursor[arm]);
    time = at;
}
... 原有全部 bookkeeping ...
++time;
if (objectClocks) {
    for (int arm : arms) armCursor[arm] = time;
    time = std::max(cursorFloor, time);
}
```

结果 —— **六道题全部逐字不变**：

| key | off | on |
|---|---:|---:|
| `1dc60983` | 78168 | 78168 |
| `f613a067` | 52925 | 52925 |
| `9a6d6d90` | 40387 | 40387 |
| `6c4bbc37` | 39780 | 39780 |
| `89d6e08e` | 94550 | 94550 |
| `cd9b4ea9` | 20014 | 20014 |

原因很简单：`time` 是**单调递增**的，`armCursor[arm]` 永远 ≤ `time`（每次写都会把它
设成 `time`），所以 `at` 恒等于 `time`。这个版本是**数学上的恒等变换**。

### 17.3 版本 2：`at = max(armCursor[arm])`（去掉 `time`）

去掉 `time` 后 cursor 才真正独立，六道题**全部 gate 失败**（`cycles = -`）。

### 17.4 结论：per-arm cursor 不能脱离依赖声明

版本 2 的失败是**语义性**的，不是实现 bug：`write()` 的签名是
`write(const std::vector<int>& arms, char action)` —— 它只说明「哪些臂一起动」，
**完全不说明这个动作依赖前面的哪个动作**。没有依赖信息，"最早可执行时刻" 就只能是
全局 `time`，任何提前都是非法的。

这也**反向解释了为什么 `LifeDeath` 的 per-feed clocks 能成功**：那里依赖是代码结构
显式给出的 —— `feedShift[f]` 只在工厂 `f` 真正需要该原子时才被推到对应时刻，而这个
"需要" 关系写在控制流里，不需要推断。

### 17.5 正确的下一步

要在 `RowAssembly.cpp` 拿到同样的收益，依赖必须先被**显式化**，有两处现成来源：

1. **`ExecutionIR` 的 ledger** —— `K::Acquire / Bond / Relocate / Output` 就是真正的
   语义依赖 DAG，`state.nextId` 的产权链天然给出 happens-before 序。
2. **`write()` 的调用点** —— 每个调用点都知道自己在做哪一类动作（取料 / 搬运 / 放置 /
   键合 / 输出），可以给 `write` 加一个显式的 `waitsOn` 参数。

然后按 DAG 做列表调度（list scheduling），而不是在 `write()` 里靠 cursor 猜。

### 17.6 代码状态

两次实验均已回退（`git checkout src/Solvers/Layout/RowAssembly.cpp`），默认路径
零变化。本节的负面结论是有价值的：它排除了一条看起来最直接的路，并把下一条路的
入口定位到「依赖显式化」上。

---

## 18. finite-family 行时钟：2 题可复现收益（2026-11-12）

### 18.1 先纠正一个样本错误

之前用 6 道「落后题」测 `OPUSOPTIMA_ROW_FINITE_CLOCKS=1`，结果全部超预算失败，据此
记下了"只对 `cd9b4ea9` 有效"。**那 6 道题没有一道属于 clocks 能生效的形态**，样本选错
了。

按 `CompileRows` 里 `finiteProducts` 分支的真实前提重新筛选 —— 单分子输入 + 多输出 +
每个输出 ≤4 原子 —— 全库 162 题里有 11 道符合。在这 11 道上逐题 A/B：

| key | 默认 | `ROW_FINITE_CLOCKS=1` | 变化 |
|---|---:|---:|---:|
| `cd9b4ea9` | 20014 | **3033** | **6.6×** |
| `b4769272` | 22544 | **8465** | **2.66×** |
| `2637c0fe` | 12445 | 12445 | — |
| `303de6d6` | 7471 | 7471 | — |
| `426d0058` | 18619 | 18619 | — |
| `78a08a99` | 238 | 238 | — |
| `8a0e9ccf` | 187 | 187 | — |
| `cca2722a` | 5781 | 5781 | — |
| `ce508e51` | 44531 | 44531 | — |
| `deecf24d` | 39833 | 39833 | — |
| `fabd74d3` | 3855 | 3855 | — |

2 题实质受益，9 题零影响，零回归。`b4769272` 是这轮新发现的第二个受益者。

### 18.2 `RowClockScope(1)` 不能直接拿来当默认

第一版把 lead 写成 `RowClockScope(1)` 包住 `CompileLifeDeath`，结果是：

- `cd9b4ea9` 仍 20014（lead 的 attempt 被 gate 拒绝后回退）
- **`2637c0fe` 从 12445 变成失败**

原因是 `RowClockScope` 会**全开**所有 row knob（`feedPrefetch` / `assemblyClocks` /
两个 opt-in），而 env 方式只翻 finite 一个。全开会在某些家族上产出被 gate 拒绝的机器。

### 18.3 `FiniteClockScope`：只翻一个开关的窄 scope

`RowAssembly.hpp` 新增：

```cpp
class FiniteClockScope {
public:
    explicit FiniteClockScope(int mode);
    ~FiniteClockScope();
    ...
private:
    int previous_;
};
```

实现只动新增的 `g_rowFiniteClockOptIn`，`RowClockOptIn` 对它优先响应，`RowClockEnabledFrom`
完全不受影响 —— 这样才和 env 方式逐字等价：

```cpp
static bool RowClockOptIn(const char* name) {
    if (std::string_view(name) == "OPUSOPTIMA_ROW_FINITE_CLOCKS" && g_rowFiniteClockOptIn != 0)
        return g_rowFiniteClockOptIn > 0;
    if (g_rowClockMode != 0) return g_rowClockMode > 0;
    const char* flag = std::getenv(name);
    return flag && std::string_view(flag) != "off";
}
```

换用窄 scope 后 `2637c0fe` 恢复 12445，`1dc60983` / `f613a067` 也回到基线，
**但 `cd9b4ea9` 的 lead attempt 依然被 gate 拒绝**。

### 18.4 还没解释的那一步

在 `FiniteClockScope` 下打开 `OPUSOPTIMA_ROW_CLOCK_DIAG`，打印出的条件组合与 env 方式
**完全相同**：

```
allowFinite=1 allowEmission=0 rowMode=0 finiteMode=1
finiteProducts=0 / 12 / 6   （三次调用，逐项一致）
```

也就是说 `CompileRows` 内部拿到的输入一致，产出的机器也应当一致。差异只可能来自
`attempt()` 的调用位置：lead 那次是 `submission=1`，主路径那次是 `submission=2`。
下一步要查 `attempt` 的 submission 配额与 gate 之间的交互，为什么同一台机器在配额的第一
个位置会被拒。

### 18.5 本轮的落点

- **收益可复现**：`OPUSOPTIMA_ROW_FINITE_CLOCKS=1` 给出 `cd9b4ea9` 6.6×、`b4769272` 2.66×
- **默认路径零回归**：`1dc60983` 78166、`f613a067` 52923，与 `HEAD` 未改版本逐字一致
  （顺带确认：本文件早前记载的 78168/52925 已经过时，当前真实基线是 78166/52923）
- **自动 lead 已回退**：它会在 `2637c0fe` 上引入失败，在弄清 §18.4 之前不能进默认
- 基础设施建设完成：`RowClockOptIn`、`allowEmissionClocks`、`allowFiniteClocks`、
  `FiniteClockScope` 全部就位且默认关

---

## 19. 解开 §18.4：3033 来自 independent-outputs，不是 life-death

### 19.1 一行 gate 日志就给出了答案

给 `verifyAt` 的 gate 日志补上 `cycles` 和 `detail` 之后，一次运行就看清了全貌：

```
env 全局 OPUSOPTIMA_ROW_FINITE_CLOCKS=1
  branch=life-death           accepted=false submission=1 cycles=-1
      detail=atom moved while being held stationary by another arm
  branch=independent-outputs  accepted=true  submission=2 cycles=3033
```

**clocks 会让 `life-death` 发射出非法机器**（"atom moved while being held
stationary by another arm"）。env 全局方式之所以能拿到 3033，是因为 `life-death`
**失败**了，求解器才落到 `independent-outputs`，而那里在 clocks 下产出的正是 3033。

§18.4 记的"同一台机器在 submission=1 被拒、submission=2 被接受"是误读 —— 那根本是
两台不同的机器、两个不同的阶段。

### 19.2 §18.4 的僵局其实是两个独立错误

1. **`RowClockOptIn` 的优先级写反了。** 第一版把 `g_rowFiniteClockOptIn` 放在
   `g_rowClockMode` 检查**之前**，于是绕过了 `CompileLifeDeath` 内部嵌套的
   `RowClockScope(-1)` —— 那些阶段明确要求串行定时，强开 finite clocks 就产出了
   非法机器。改成"强制模式优先"后 `2637c0fe` 立刻恢复 12445。
2. **lead 放错了位置。** 把 lead 挂在 `attempt("life-death")` 的位置，而 3033 从来
   不在那个阶段产生，所以无论怎么调 scope 都只能看到 gate 拒绝。

### 19.3 正确接法

`finiteClockFamily` 时**让 life-death 站开**，并把 clocks scope 加在
`independent-outputs` 上：

```cpp
if (!finiteClockLead) {
    if (auto result = attempt("life-death", [&] { return CompileLifeDeath(puzzle); })) {
        submitIntakeSibling(intakeTwin, true, "life-death-local-intake", ...);
        submitIntakeSibling(clockSibling, false, "life-death-feed-clocks", ...);
        if (keep(*result)) return publish();
    }
}
...
if (auto result = attempt("independent-outputs", [&]() -> io::SolutionData {
        std::optional<FiniteClockScope> clockScope;
        if (finiteClockLead) clockScope.emplace(1);
        ...
```

### 19.4 结果：收益进了默认路径

`finiteClockFamily` = 单分子输入 + 多输出 + 每输出 ≤4 原子，全库 162 题里 11 道：

| key | 前 | 后 | 变化 |
|---|---:|---:|---:|
| `cd9b4ea9` | 20014 | **3033** | **6.6×** |
| `b4769272` | 22544 | **8465** | **2.66×** |
| `2637c0fe` | 12445 | 12445 | — |
| `303de6d6` | 7471 | 7471 | — |
| `426d0058` | 18619 | 18619 | — |
| `78a08a99` | 238 | 238 | — |
| `8a0e9ccf` | 187 | 187 | — |
| `cca2722a` | 5781 | 5781 | — |
| `ce508e51` | 44531 | 44531 | — |
| `deecf24d` | 39833 | 39833 | — |
| `fabd74d3` | 3855 | 3855 | — |

**默认开启**，`OPUSOPTIMA_LD_FINITE_CLOCK_LEAD=off` 可回退。其余 151 题完全不受影响：
条件不满足时 `finiteClockLead` 为 false，`!finiteClockLead` 为 true，`life-death`
照旧第一个尝试。

---

## 20. 与 OpusSolver 的差距审计（2026-11-12，全量 146 题）

工具：`tools/audit_gap.py`（逐题跑交付默认配置，对比 oracle 三槽）、
`tools/audit_mech.py`（机构构成对比）。产物在 `result/audit-gap-20261112/`。

### 20.1 总量

139/146 产出（7 题无解：`e42f1f11` `cfba9c05` `19ff88db` `19c7b313`
`16b1a5f5` `07e252a4` `0409d7fe`）。

| slot | win | lose | ratio p50 | p90 | max |
|---|---:|---:|---:|---:|---:|
| cost | 63 | 76 | **1.11** | 4.76 | 25.45 |
| cycles | 51 | 88 | **9.60** | 39.18 | 100.47 |
| area | 60 | 79 | **1.18** | 5.69 | 31.25 |

**四个数字定调**：`cost` 只差 1.11×、`area` 只差 1.18×，`cycles` 差 **9.60×**。
机器"用什么造"已经基本对上了，"怎么动"差了整整一个数量级。

分布是双峰而非连续：**51 题三槽全赢、79 题三槽全输**，中间几乎没有渐变。
说明这不是"调优不够"，而是**两种不同的构造方式**。

### 20.2 按类别

| class | n | cycles ratio p50 | p90 | max | ≤1x |
|---|---:|---:|---:|---:|---:|
| `pipeline/cheap-arms` | 29 | **23.47** | 70.12 | 100.47 | 3 |
| `universal` | 64 | **13.69** | 22.14 | 96.68 | 18 |
| `linear/mono` | 39 | 0.05 | 19.42 | 85.75 | 26 |
| `pipeline/heavy` | 7 | 0.01 | 13.12 | 71.36 | 4 |

名字就写着答案：**我们能做好 `linear/mono`（链式、单线），做不好
`pipeline/cheap-arms`（多臂流水线）**。

### 20.3 决定性变量：机器规模

| 规模 | n | cycles ratio p50 | win |
|---|---:|---:|---:|
| ≤40 objects | 82 | **1.26** | 41 |
| >40 objects | 57 | **18.11** | 10 |

**小机器基本追平（1.26×），大机器落后 18 倍。** 缺口是"规模化"。

### 20.4 决定性变量：机构配比

`piston/(arm+1)` 分桶：

| bucket | n | cycles ratio p50 | win |
|---|---:|---:|---:|
| piston = 0 | 31 | **0.02** | **28 (90%)** |
| piston/arm ≤ 1 | 11 | **0.02** | **9 (82%)** |
| piston/arm ≤ 4 | 6 | **20.24** | **0 (0%)** |
| piston/arm > 4 | 91 | **14.39** | 14 (15%) |

全库总量：

```
ours   piston:arm = 2382:506  = 4.71
oracle piston:arm =  862:1469 = 0.59
```

**配比差 8 倍，周期差 9.6 倍 —— 数字几乎重合。**

阈值效应极陡：`piston/arm ≤ 1` 时胜率 82–90%、领先约 50 倍；跨过 4 就掉到 0–15%、
落后 14–20 倍。**没有中间地带。**

### 20.5 为什么 piston 替代 arm 会毁掉周期

- **arm 是可编程的**：行为由 instruction 序列 + parameter 决定，一条 tape 能让同一个
  物理机构在不同 tick 服务不同目标 —— 这是**时间复用**。
- **piston 是固定的**：一伸一缩，一个 piston 只做一件事，位置和动作都焊死。

所以用 piston 造机器等于"每个动作买一台专用机"。它便宜（cost 1.11×）、占地小
（area 1.18×），但**时间上完全无法复用**，N 个动作就要 N 个串行 tick。
规模一大，只能继续加 piston，周期线性膨胀。

这也解释了 §14 一直观察到的 `instr/cycles ≈ 1.07`：周期就是全部指令之和，零重叠。

### 20.6 缺的能力，按收益排序

| 优先级 | 能力 | 证据 |
|---|---|---|
| **1** | **arm 的 tape 编排器**：把动作序列编译成单条长 tape，让同一 arm 分时服务多个目标 | `piston/arm>4` 91 题胜率 15%、落后 14.4×；配比差 8 倍 ≈ 周期差 9.6 倍 |
| **2** | **多产物流水线**：稳态节拍出产物，而非"装配完→输出→装配下一个" | `pipeline/cheap-arms` 29 题 p50 23.5× |
| **3** | **跨产物共享臂**：一套机构服务多个产物 | >40 objects 的题落后 18.1× |
| **4** | **依赖感知调度**：有了 arm tape 之后才谈得上并行 | §17 已证：无依赖声明时 `sum→max` 是恒等变换，强开则 gate 拒绝 |
| **5** | **补齐 7 道无解题** | 当前 0 分 |

**关键判断**：§15–§19 做的 per-feed clocks / finite clocks 属于第 4 项的一个特例 ——
在**已有正确几何**的题上加时钟。所以它有效但覆盖面窄（11 题里 2 题受益）。
第 1 项是改变几何本身的构造方式，覆盖面是 91 题（`piston/arm>4`），差一个量级。

---

## 21. 解剖 oracle 真机：差距是「长 track」，不是「缺 arm 种类」（2026-11-12）

§20 只看了配比统计。`result/opussolver-oracle-20261110/solutions/` 里有 oracle
逐题的三槽 `.solution`，可以直接读它们的机器。

### 21.1 三题的真机对照

| key | 来源 | 对象 | 指令数 | track 总格 | 最长 track | arm1 | piston | track |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| `1dc60983` | 我们 | — | **81742** | **1474** | **587** | 1 | 49 | 44 |
| `1dc60983` | oracle | 35 | **515** | **34** | **15** | **15** | 7 | 4 |
| `f613a067` | 我们 | — | **55015** | **935** | **468** | 0 | 38 | 30 |
| `f613a067` | oracle | 45 | **437** | **34** | **6** | **23** | 3 | 8 |
| `9a6d6d90` | 我们 | — | **94940** | **3873** | **823** | 10 | 28 | 31 |
| `9a6d6d90` | oracle | 53 | **356** | **23** | **5** | **27** | 2 | 7 |

三个数量级关系同时成立：

- **指令数差 106–267 倍**
- **track 总长差 27–168 倍**
- **最长单条 track 差 39–165 倍**
- arm1 差 15–27 倍

### 21.2 oracle 的 track 根本不是运输干线

**oracle 最长的一条 track 只有 5–15 格**。那是最后几格的精细对位，不是跨越装配区的
传送带。它把整个搬运工作交给 **15–27 个 arm1**，每个 arm 的 reach 只有 3 格，靠**臂链
接力**覆盖全长。

我们的 track 是 468–823 格的干线。因为 **track 每走一格就是一条 `A`/`a` 指令**，
一条 587 格的 track 往返一次就是上千条指令，而 `instr/cycles ≈ 1.05`（周期等于指令
总数），于是周期直接被距离线性放大。

**这就是为什么 `cost` 只差 1.11×、`area` 只差 1.18×，`cycles` 却差 9.60×**：
track 便宜、窄、占地小，但它把**几何距离**翻译成了**周期**。arm 不把距离计入周期 ——
一次摆动能覆盖它整个 reach 范围。

### 21.3 track 长度是cycles 的唯一单调预测子

按 track 总格分桶（139 题）：

| track 总格 | n | cycles ratio p50 |
|---|---:|---:|
| 0 | 30 | **0.02** |
| ≤200 | 69 | **8.05** |
| ≤800 | 24 | **19.27** |
| \>800 | 16 | **68.31** |

单调，跨度 3400 倍。**不用在意 piston/arm 配比 —— 那只是"用了长 track"的伴随现象。**

### 21.4 缺的能力：arm 接力网络的规划

不是"会生成 arm"（我们会，506 个），而是：

> 给定输入区与产物区，规划一组 arm1 的静态摆放，使各自的 reach 邻域相互衔接，
> 形成从输入到产物的接力链；再用极短的 tape（伸-抓-缩-转）驱动每一个。

这样搬运成本从 **O(几何距离)** 降到 **O(臂数)**。三个样本上的量化上限：

```
1dc60983   81742 -> 515    (159x)
f613a067   55015 -> 437    (126x)
9a6d6d90   94940 -> 356    (267x)
```

这也解释了 §16–§19 的时钟工作为什么覆盖窄：它优化的是"已有正确几何"的机器，
而这些题的几何本身就错了 —— 再好的时钟也救不了 81742 条指令。

### 21.5 优先级修正

§20.6 的第 1 项应当改成更精确的表述，且排序不变：

1. **arm 接力网络**（替代长 track）—— 覆盖 91 题（`piston/arm>4`），三个样本上限
   106–267×
2. 多产物流水线
3. 跨产物共享臂
4. 依赖感知调度
5. 补齐 7 道无解题

**新增一条否决性指标**：任何候选机器只要出现 track 总格 >200，就应先怀疑它。当前
69 题落在这一档，p50 落后 8.05×。

---

## 22. 周期的 76% 是一个活塞沿长轨往返（2026-11-12）

§21 说差距在长 track。这一节把位置精确到一个机构、一种动作。

### 22.1 `1dc60983` 的指令分布

```
total instructions 81742
top tape sizes     62453, 3727, 1807, 1489, 1489
```

**单个活塞（位于 `(21,88)`，正是那条 587 格轨道的起点）占了 62453 条 —— 76%。**

它的动作直方图：

```
A 31080   a 30604   R 216   r 216   E 96   e 96   G 72   g 72
```

`A`/`a` 是沿轨前进/后退，其余的旋转、伸缩、抓放加起来不到 700 条。
在 587 格轨道上走 31080 格，等于 **53 个单程** —— 也就是 53 次完整往返。

周期 78168 ≈ 指令 81742（`instr/cycles ≈ 1.05`），**所以这个活塞的往返就是整台机器的周期**。

作为对照，oracle 同一题最长的一条 tape 是 54 条指令（一个 arm1），其余活塞 40–44 条，
全机合计 515 条。

### 22.2 为什么这是「几何问题」而不是「调度问题」

这个活塞每一次往返都在做同样的事：`G` 抓起产物 → 沿轨道走到底 → `E`/`g` 交给输出 →
`a` 原路退回。产物出 53 次，它就往返 53 次。

- 用时钟让它和别的臂重叠**没有意义** —— 它自己就占了周期的 76%，没有别的动作可以
  与之重叠（§16–§19 的时钟工作在这里注定无效，这也解释了为什么它们只覆盖 11 题）。
- 缩短轨道能线性获益，但 `remote` 常量（`16*width + 40`）只影响 `calcify` 分支，
  实测对该题零影响 —— 这条 587 格轨道来自另一处生成逻辑。
- **真正的解法仍是 §21 的 arm 接力**：一个局部 arm 网络把这 53 次长途往返替换成
  若干次短距离交接，指令数从 `O(距离 × 次数)` 降到 `O(次数 × 臂数)`。

### 22.3 可操作的定位方式

以后遇到「落后但机构规模正常」的题，按这个顺序看：

1. `sum(len(o['instructions']))` —— 总指令数（周期 ≈ 它）
2. 取最大的那条 tape，打印动作直方图 —— 若 `A`/`a` 占 95% 以上，就是长途搬运
3. 看它的 `position` 是否等于某条长 `track` 的起点 —— 确认是哪段代码铺的轨

这三步在 `1dc60983` 上把「落后 100×」直接收敛到「一个机构的一种动作」。

---

## 23. 长轨的源头：分子工厂被水平摊开（2026-11-12）

### 23.1 关键反证：那 500 格不是约束

`1dc60983` 的输入原子全在原点附近 —— `(1,-1) (1,1) (5,0)`，`multiplier = 1`。
所以 transport 的 500 格**完全是布局自己选的**，不是题目要求。

### 23.2 诊断输出把它钉死

`OPUSOPTIMA_LD_TRANSPORT_DIAG=1` 打印 `LifeDeath.cpp` 里 `transportPath` 的构成：

```
1dc60983  pickup=-519  bend=-44  right=-15  feeds=6  path=587
f613a067  pickup=-399  bend=-40  right=-14  feeds=4  path=468
89d6e08e  pickup=-535  bend=-51  right=-16  feeds=6  path=602
9a6d6d90  pickup=-226  bend=-509 right=-146 feeds=1  path=729
```

`path` 的第一段就是 `|pickup - bend|`，587 格里 475 格来自这里。而 `pickup` 与
`bend` **都**是布局产物，两者都在原点之外的负方向。

### 23.3 源头是一行累积偏移

`LifeDeath.cpp`：

```cpp
const int left = nextFactoryRight + minimumQ;
nextFactoryRight = left - maximumQ - 40;     // ← 每个工厂向左挪一点
feed.pickup = compactRelay ? cell.q - 4 : left - 2;
```

每个分子工厂为了不与前一个的臂和所持分子重叠，向左平移 `maximumQ + 40`。这个量
**乘以工厂数**就成了运输走廊长度：6 个工厂张开 475 格。

注释原本就是 "Rotate each factory into a disjoint horizontal envelope, leaving
clearance for its arms and held molecules instead of a fixed stride" —— 意图正确，
代价没有被计入周期。

### 23.4 把 clearance 暴露出来实测

| key | c=40 | c=8 | c=2 | c=0 | 收益 |
|---|---:|---:|---:|---:|---:|
| `1dc60983` | 78166 | 66806 | 64676 | **63966** | **1.22×** |
| `f613a067` | 52923 | 48411 | 47565 | **47283** | **1.12×** |
| `89d6e08e` | 94548 | 79348 | 76498 | **75548** | **1.25×** |
| `6c4bbc37` | 39533 | 39533 | 39533 | 39533 | — |
| `9a6d6d90` | 40387 | 40387 | 40387 | 40387 | — |

`6c4bbc37` / `9a6d6d90` 不受影响，它们不走 `molecularFeeds` 非空这条路径。

**收益真实但只有 1.1–1.25×**：clearance 只占 475 格里的 5×40=200，其余是
`maximumQ`（每台提取机自身宽度）。想再往下走必须让工厂**垂直堆叠**而不是水平摊开，
那是几何规划问题，不是调常量能解决的。

### 23.5 这条链的完整形状

```
周期 = 指令数（instr/cycles ≈ 1.05）
  指令数 76% 来自一个活塞的 A/a
    因为它在一条 587 格的 transport 轨道上跑 53 个往返
      因为分子工厂被水平摊开到 475 格外
        因为每个工厂要让出 maximumQ + 40 的水平 envelope
```

四层，每一层都可独立度量。这就是 §20 那个「cycles 差 9.6×」的真实分解。

### 23.6 全库验证：c=0 有收益但会牺牲三题

`OPUSOPTIMA_LD_FACTORY_CLEARANCE=0` 跑全部 146 题：

| 指标 | c=40（出货） | c=0 |
|---|---:|---:|
| 产出 | 139 | **136** |
| cycles ratio p50 | 9.60 | **8.16** |
| cost ratio p50 | 1.11 | **1.08** |
| area ratio p50 | 1.18 | 1.18 |

整体指标确实变好，但 **`e55f57a6` `deecf24d` `ac8d3e1b` 三题从有解变成无解**。这不是可接受
的交换。

三题都是两个输出，看起来可以按输出数自适应，但**不行**：它们经由
`independent-outputs` 逐输出编译，进到 `LifeDeath` 时看到的已经是单输出切片，原始
多输出形态不可见。实测 `deecf24d` 在 clearance 22–38 全部失败、只有 40 能过，而同为
7 原子输出的 `f613a067` 在 0 就能过 —— 没有可靠的单点判据。

**所以出货值保持 40，knob 保留供实验。** 要真正吃到这部分收益，需要让工厂**垂直堆叠**
而不是沿走廊摊开，那样 clearance 就不再乘以工厂数；这是几何规划工作，已记入 §23.7。

### 23.7 下一条路：工厂垂直堆叠

现在 6 个工厂沿横向排开，每个让出 `maximumQ + 40`。如果改为沿**垂直**方向堆叠（同列，
不同行），transport 只需下降一次固定距离，`pickup` 与 `bend` 的 q 差会从 475 降到接近 0。

障碍正是注释里写的那条：工厂的臂和所持分子需要水平空间。所以这不是调常量，而是要
在放置工厂时求解一个二维装箱 —— 与 §21 的 arm 接力网络是同一类问题。

---

## 24. tight-first 布局：收益真实，被 submission 配额挡死（2026-11-12）

### 24.1 想法

§23 量到 clearance=0 时 1.12–1.25×。既然不是所有形状都能过 gate，那就**先试紧布局，
gate 拒绝就回退到出货值** —— 用 `attempt` 天然的回退语义，零风险。

为此加了 `FactoryClearanceScope`（thread_local）和 `life-death-tight` 这个 attempt。

### 24.2 实测：四题拿到收益

| key | 出货 | tight-first | 收益 |
|---|---:|---:|---:|
| `1dc60983` | 78166 | **63966** | 1.22× |
| `f613a067` | 52923 | **47283** | 1.12× |
| `89d6e08e` | 94548 | **75548** | 1.25× |
| `ac8d3e1b` | 58316 | **46916** | 1.24× |

### 24.3 但撞了两个坑

**坑一：放在 `finiteClockLead` 之前会毁掉 bounded-run 家族。** `cd9b4ea9` 的
3033 来自 `independent-outputs`，而 tight 一旦成功就 `return publish()`，
它变成 **19574**；`b4769272` 从 8465 变 21144。移进 `!finiteClockLead` 分支后才恢复。

**坑二：`submission` 配额是隐性的共享资源。** 移进分支后 `deecf24d` 从 39833 变成
无解。加诊断打印才看清：

```
CLEARANCEDIAG value=0 tl=0 feeds=0
CLEARANCEDIAG value=0 tl=0 feeds=1
... 五次
```

`tl=0` 说明 tight 确实执行了（`deecf24d` 的 `finiteClockLead` 为 false），它失败后
**`verifyAt` 已经 `++submissions`**，于是随后的 `life-death` 撞上
`MaxCandidateSubmissions` 无法提交。用 `OPUSOPTIMA_LD_FACTORY_CLEARANCE=40` 显式跑
就正常 —— 因为那样根本没有 tight attempt。

`attempt` 的失败不可"退还"配额，所以试错式 portfolio 在这个位置不成立。

### 24.4 结论

- 回退 `life-death-tight`。基线全部恢复：`1dc60983` 78166、`deecf24d` 39833、
  `cd9b4ea9` 3033、`b4769272` 8465、`6c4bbc37` 39533。
- **保留 `FactoryClearanceScope` 与两个诊断开关** —— 它们是有效基础设施，只是缺一个
  不消耗配额的试用机制。
- 想吃这部分收益，需要先给 portfolio 一个「候选试算不占配额」的通道，或者把 clearance
  的选择下沉到几何规划里（配合 §23.7 的垂直堆叠，那时 clearance 不再是主项）。

---

## 25. 为什么提取机无法合并：每 feed 一个 carrier 是硬约束（2026-11-12）

### 25.1 冒出的假设

§23 说 6 个工厂横向摊开 475 格。`1dc60983` 只有一种输入分子，产物 10 原子，所以
直觉上这 6 个工厂应该能合并。若真能，跨度会从 475 降到 ~100。

### 25.2 测量：五个工厂是同一台机器

`OPUSOPTIMA_LD_BATCH_DIAG=1` 打印每个 feed 的提取机：

```
BATCHDIAG input=0 type=1 objs=16 instr=1956 cell=(-15,4)
BATCHDIAG input=0 type=1 objs=16 instr=492  cell=(-18,3)
BATCHDIAG input=0 type=1 objs=16 instr=492  cell=(-17,3)
BATCHDIAG input=0 type=1 objs=16 instr=492  cell=(-21,3)
BATCHDIAG input=0 type=1 objs=16 instr=492  cell=(-20,3)
BATCHDIAG input=0 type=5 objs=18 instr=2885 cell=(-24,4)

BATCHWIDTH input=0 type=1 minQ=-17 maxQ=3 left=-217
BATCHWIDTH input=0 type=1 minQ=-17 maxQ=3 left=-277
BATCHWIDTH input=0 type=1 minQ=-17 maxQ=3 left=-337
BATCHWIDTH input=0 type=1 minQ=-17 maxQ=3 left=-397
BATCHWIDTH input=0 type=1 minQ=-17 maxQ=3 left=-457
BATCHWIDTH input=0 type=5 minQ=-17 maxQ=3 left=-517
```

**五个 `type=1` 工厂的几何完全相同**（`minQ=-17 maxQ=3`，宽 20 格）。而 `left` 每个
递减 60 = `(maxQ - minQ) + clearance` = 20 + 40。

### 25.3 假设被否掉

改 `realSourceBatch` 让同一 `(input, type)` 的大 compilation 服务小消费者（按 tape
切片），实测 **五道题逐一零变化**。

原因：这些工厂**本来就已经共享了那一台机器** —— `sourceBatches` 的 key 已经含
`(input, type, products)`，指令不同的 `1956` vs `492` 是同一 `SourceBatch` 在不同 feed
上的**时间切片长度**，不是五次独立编译。

**60 格/feed 的跨度不是"重复造机器"，而是每个 feed 必须有独立的 carrier 和 pickup：**

```cpp
feed.carrier = object("piston", {feed.pickup, ...});
feed.pickup  = left - 2;
nextFactoryRight = left - maximumQ - factoryClear;
```

多台 carrier 共用一个 pickup 会在 motion phase 相撞。

### 25.4 所以真正的约束是「一工厂一 feed 一 carrier」

跨度 = 工厂数 × (提取机宽 + clearance)。要缩短只有三条路：

1. **减少工厂数** —— 一个 feed 服务多个工厂，需要中间分发机构（回到 track 的
   O(距离) 问题）。
2. **让 carrier 复用同一个 pickup** —— 目前会碰撞；要么给 carrier 加互斥时钟，
   要么让它们沿不同行停靠。
3. **把 carrier 换成 arm** —— carrier 是 piston，只能在自己的 pickup 上伸直；arm 有
   3 格 reach，能在不移动底座的前提下服务多个 pickup。

第 3 条与 §21 的 arm 接力网络是同一件事，也是三条里唯一不引入新冲突的。

### 25.5 本轮结论

- 提取机合并**已被测过并否掉**，不要再试。
- `1dc60983` 的 587 格轨道 = 5×(20+40) + pickup 偏移 + 下行段。clearance 只占 200
  （解释了 §23 为何只能拿到 1.22×），提取机宽占 100，其余是 pickup 起点位置。
- 剩下唯一有量级收益的方向仍是 arm 替代 piston 作为 carrier。

### 25.6 垂直堆叠也被测过：无效

§25.4 留下「让 carrier 沿不同行停靠」这个选项。实测否掉：

```
c=40 h=0  -> 78168     (baseline)
c=0  h=0  -> 63968     (1.22x)
c=0  h=40 -> 63968     (identical to h=0)
c=-3 h=30 -> unsolved
c=-3 h=60 -> unsolved
```

把 `feed.height` 按 feed 索引错开 40 格，周期**一位不变**。原因是
`transportPath` 被钉在 `r = 88`：

```cpp
for (int q = pickup;; q += step) transportPath.push_back({q, 88});
for (int r = 87; r >= transportRow; --r) transportPath.push_back({bend, r});
```

feed 的高度只影响 carrier 停靠位置，不改变走廊的水平长度。所以走廊长度**完全由 q 跨度
决定**，与垂直排布无关。

而 `c=-3`（把提取机挤到重叠 15 格）直接无解，符合 §25.3 的判断。

### 25.7 当前架构下 q 跨度是封死的

`q` 跨度 = `feeds × (提取机宽 20 + clearance)`，其中：

- `feeds` 数由 `machine.objects` 里 `input` 对象的个数决定（`LifeDeath.cpp` 的
  `for (i < originalCount) if (o.typeName != "input") continue; addFeed(...)`），
  也就是化学规划给每个工厂各生成一个 input —— 不是几何能改的。
- `提取机宽 20` 是 `CompileRowAssemblyFiniteBatch` 的输出，§25.3 已证明是共享的同一台
  机器，无法再合并。
- `clearance` 是唯一自由量，但 §24 证明它不能安全降到 0。

**三个因子互相封死，所以当前几何里没有量级收益可挖。** 要突破必须改
`transportPath` 本身，让它不是「先水平 500 格再下降」，而是由一组 arm 分段接力 ——
即 §21 的结论。

### 25.8 给下一轮的明确起点

若继续，第一件事应该是**读 `transportPath` 的构造并替换为分段方案**，而不是继续找常量。
需要的输入已经齐备：

- `molecularFeeds[i].pickup`（每个工厂的提取点）
- `feed.cell`（装配区的落点）
- arm1 的 reach 是 3（`L ∈ {1,2,3}`），所以每段接力跨度 ≤ 3

一个 500 格的走廊需要约 167 个 arm；oracle 在 `1dc60983` 上用了 15 个 arm1 就把整机
做到 515 条指令，说明**它根本不需要跨越 500 格** —— 它的工厂就在装配区旁边。所以我们
缺的不只是接力，还有「把工厂放到装配区旁边」这一步，而那又回到 feeds 数不可压缩的
约束上。

---

## 26. 一行的硬编码锁住了 500 格走廊（2026-11-13）

### 26.1 定位过程

§25 认定「q 跨度 = feeds × (提取机宽 + clearance)」三项皆封死。但那次只查了**间距**，
没查**起点**。真正的源头是一行常量：

```cpp
int nextFactoryRight = -200;
```

六个提取机从这里开始各向左挪 60 格，于是落在 **-217、-277、-337、-397、-457、-517**；
而装配区在 **-24**（`feed.cell` 实测值）。**整条运输走廊的 500 格，绝大部分是这个
起点常量贡献的。**

对照 oracle 同一题：`input` 在 `(-5,0)`、`out-std` 在 `(18,-4)`，整机 q 范围
`[-5,24]` —— 30 格。我们 `[21,550]` —— 530 格。

### 26.2 实测：把起点挪到装配区旁边

| key | start=-200（出货） | -80 | -60 | -40 | -30 |
|---|---:|---:|---:|---:|---:|
| `1dc60983` | 78166 | 61006 | 58146 | 55286 | **53856** |
| `f613a067` | 52923 | 41523 | 39623 | 37723 | **36773** |
| `89d6e08e` | 94548 | 75948 | 72848 | 69748 | — |
| `deecf24d` | 39833 | 32753 | 31573 | 30393 | **29803** |

`-30` 上三题同时可行，收益 **1.34–1.45×**，明显高于 §23 的 clearance（1.22×）和
§24 的 tight-first（1.22×）。原因很简单：起点平移整条走廊，而 clearance 只改间距。

### 26.3 边界就在装配区边缘

```
1dc60983  -30 ok       -20 fail
f613a067  -30 ok       -20 fail
deecf24d  -30 ok       -10 fail
```

因为第一个提取机落在 `-30`，而装配行在 `-24..-15`；再往前就撞上了。所以 `-30` 是
「贴着装配区放」的极限，不是一个任意挑选的安全值。

### 26.4 与 §24 的区别：这次不需要 portfolio

`clearance` 需要「试紧布局、失败回退」，因而撞上 `submission` 配额（§24）。起点不同：
它是一个**固定的平移量**，不改变机构间距，所以不需要试错。只要全库验证没有新失败，
就可以直接改默认。

全库验证见 `result/audit-start30-20261113/`。

### 26.5 全库验证：收益真实，但有一个路径抢先问题

| start | 产出 | cycles p50 | 备注 |
|---|---:|---:|---|
| -200（出货） | 139 | 9.60 | 基线 |
| -40 | 138 | **8.93** | `a7d3c0bf` 失败 |
| -30 | 137 | **8.26** | `a7d3c0bf`、`89d6e08e` 失败 |
| -60 | 138 | 8.93 | `a7d3c0bf` 恢复，`ac8d3e1b` 失败 |

`-40` 的受益分布（对比基线）：

```
1.54 fc6a70eb linear/mono         10547 ->  6867
1.41 7b837aa2 pipeline/cheap-arms 84718 -> 59918
1.41 1dc60983 pipeline/cheap-arms 78168 -> 55288
1.40 f613a067 pipeline/cheap-arms 52925 -> 37725
1.38 cb6c9528 pipeline/cheap-arms 27169 -> 19649
1.36 550b9c1c pipeline/cheap-arms 50470 -> 37190
1.36 89d6e08e pipeline/cheap-arms 94550 -> 69750
1.35 ac8d3e1b universal           58318 -> 43118
```

**十题以上拿到 1.21–1.54×，这是目前找到的最大杠杆。**

### 26.6 但它会抢占更优路径

`17a58334` 是反例，而且原因不是布局：

```
start=-200:  life-death 失败(tape too long) -> shared-plan 28569 ✓
start=-60:   life-death 成功(84697)         -> 提前返回，抢占 ✗
```

它的敏感度是**相变式**的（-150 得 26899，-120 得 97595）：起点变小让 life-death 的 tape
缩到能通过 gate，于是它不再失败，直接 `return publish()`，挡住了后续
`shared-plan-ordinary-port-access` 的 28569。

`keep()` 内部虽有 portfolio 构造，但它只覆盖自己列出的那几族，**不涵盖后续的独立
attempt**。所以「让 life-death 也成功」等于「让更快的家族失去机会」。

### 26.7 结论与下一步

- **`start` 是真正的量级杠杆**（10+ 题 1.21–1.54×），远大于 clearance（1.22×）。
- 但它是**全局平移**，会改变 life-death 能否通过 gate，从而改变路径选择，因此不能直接
  进默认。
- 要安全吃到它，必须让 life-death 从「首次可行即返回」改为「提交为候选、允许后续家族
  竞争再择优」。那是 portfolio 语义的改动，也是下一件该做的事 —— 而且它同时能让 §24
  的 tight-first 复活（同一个 `submission` 配额问题）。
- 出货值保持 `-200`，`OPUSOPTIMA_LD_FACTORY_START` 保留供实验。

### 26.8 defer 实验：机制对了，但撞上时间预算

为了安全吃到 `start` 的收益，实现了一个 `OPUSOPTIMA_DEFER_LIFE_DEATH` 实验：

1. life-death 成功后**不立即** `return publish()`，继续尝试后续家族；
2. `acceptStage` 里原本 `if (accepted && !firstAccepted)` 才记录首个候选 —— 改成在
   defer 模式下允许**严格更优**（cycles 更小且已验证）的候选替换 `firstAccepted`。

实测 `17a58334`：

```
默认:        life-death 失败(tape too long) -> shared-plan 28569
defer+start=-40: life-death 成功 80397 -> 后续家族根本没跑
```

日志里 life-death 之后**没有任何 attempt**。原因是
`materialLimits.deadline = min(Deadline(), now + 3s)` —— 而 life-death 本身要编译约
12 秒，此时留给 shared-plan 搜索的窗口已经不够。

所以 `defer` 的**逻辑**是对的（替换条件、返回时机都按预期改），但**时间预算**不允许
在同一题内既跑慢的 life-death 又跑 shared-plan 搜索。

### 26.9 要让 start 收益落地，还差什么

按代价从小到大：

1. **给 shared-plan 独立的搜索预算**，不让它被 life-death 的编译时间挤掉。改动集中在
   `materialLimits.deadline` 的计算。
2. **按 tape 长度预判**：`17a58334` 在 start=-200 时因 "instruction tape too long" 失败，
   说明可以先用默认 start 试一次，仅当它成功时才允许替换 —— 但那仍要先花掉编译时间。
3. **把 start 的两种取值做成两次独立 compile 的候选**，由 archive 择优。这是最干净
   但改动最大的一条，需要 portfolio 支持「同一家族的两个几何变体」。

三条里第 1 条最小，也是下一件该做的事。

---

## 27. defer：让较慢的家族不再抢占（2026-11-13）

### 27.1 问题

`keep()` 里 `if (!firstAccepted) firstAccepted = candidate;` —— **首个被验证通过的候选
定案**。life-death 排在 `shared-plan-*` 之前，所以只要它通过 gate，后面的家族就没有
机会。`17a58334` 就是这样：`start=-40` 让 life-death 的 tape 缩短到能过 gate
（80397），于是它挡住了 `shared-plan` 的 28569 / 28293。

### 27.2 三处改动（`OPUSOPTIMA_DEFER_LIFE_DEATH`，默认关）

1. **life-death 不提前返回**：`if (keep(*result) && env == nullptr) return publish();`
2. **允许更优候选替换**：`acceptStage` 之后原本只在 `!firstAccepted` 时记录，改为
   defer 模式下「已验证且 cycles 严格更小」即可替换。
3. **给材料搜索独立预算**：`materialLimits.deadline` 从固定 `now + 3s` 改为按剩余预算
   分配（因为 life-death 自身要编译约 12 秒，原来的 3 秒窗口会被挤掉）。

诊断输出确认了三步都生效：

```
DEFERDIAG  accepted=1 allow=1 better=1 first=80397 rep=28569
PUBLISHDIAG first=28569 composed=0
```

### 27.3 结果：`17a58334` 完全恢复

| 槽 | cycles |
|---|---:|
| cost | 28569 |
| **cycles** | **16486** |
| area | 28569 |

cycles 槽回到基线最优值 16486，同时 shared-plan 的机器进入 cost/area 槽。

### 27.4 一个必须记住的测量陷阱

同一次运行的控制台日志打印 `Cycles: 80397`，而 `.objectives.json` 的三个槽是
`28569 / 16486 / 28569`。**控制台那行不是最终产物**，它来自更早的中间候选。
判断成败必须以 `objectives.json` 的槽值为准 —— 我在这一步前后误判了两次。

### 27.5 待验证

全库 `defer + start=-40` 的产出与 p50 见 `result/audit-defer40-20261113/`。
若零牺牲，则 `start=-40` 与 `defer` 可以作为一对默认值一起翻转。

### 27.6 全库 defer：p50 改善但牺牲四题

`defer + start=-40` 全库（`result/audit-defer40-20261113/`）：

| 指标 | 基线 | defer + start=-40 |
|---|---:|---:|
| 产出 | 139 | **135** |
| cycles p50 | 9.60 | **8.06** |
| cost p50 | 1.11 | **1.08** |
| cycles max | 100.47 | **73.17** |

p50 改善 16%，但 `deecf24d` `bc46fd4e` `ac8d3e1b` `a7d3c0bf` 四题失败。原因是我把
`materialWindow` 从固定 3 秒改成「剩余预算的一半」—— 那会影响**所有**题的 shared-plan
搜索时长，进而耗尽总预算。这个改动太粗，已回退。

### 27.7 tight-start 探针：撞上 acceptedCap=1

把「不提前返回」换成「publish 前追加一个 tight-start 候选」后，`17a58334`（16486）与
`a7d3c0bf`（41102）都安全，`f613a067`/`89d6e08e` 也拿到收益。但 `1dc60983` 无收益，
诊断：

```
Assembly branch=life-death-tight-start event=contained
    detail=bounded candidate collection stopped
```

单输出题的 `acceptedCap = maxAccepted = 1`，而 life-death 已经用掉了它。临时 `++acceptedCap`
可以让探针提交，但实测 `f613a067` 反而失去收益（37723 → 52923）—— 额度变化扰动了
后续 portfolio 的构造次序。

**结论**：这不是能靠局部 `++acceptedCap` 解决的，需要真正让 archive 接受「同一家族的
几何变体」作为并列候选。已回退探针，保留 `FactoryStartScope` 与 `OPUSOPTIMA_LD_FACTORY_START`
作为基础设施。

### 27.8 这轮留下的明确结论

- **`start` 是已找到的最大杠杆**：十题以上 1.21–1.54×，全库 p50 9.60 → 8.93（单改 start）。
- **它不能做成默认**：会改变 life-death 是否通过 gate，从而抢走更快家族。
- **也不能靠 defer 或额度微调安全启用**：前者伤及全局预算，后者扰动 portfolio 次序。
- **唯一正确的形态**是「同一家族的两个几何变体进入 archive 并列择优」，这需要 portfolio
  支持多几何候选 —— 这是下一件该做的事，规模比前几轮大。

---

## 28. 探针位置才是关键：三题 1.36–1.41× 且零回归（2026-11-13）

### 28.1 之前判断错了一个前提

§27.7 认定「单输出题 `acceptedCap = 1`」，据此认为几何变体无处安放。实际上
`AssemblySolver.hpp` 里 `SolveVerified(..., maxAccepted = 3)` —— **默认额度是 3**。探针
被拒不是因为只有 1 个额度，而是因为 `submitIntakeSibling` 的两个 sibling 先生效、
把额度用掉了。

### 28.2 修正：把探针移到 sibling 之前

```cpp
if (auto result = attempt("life-death", [&] { return CompileLifeDeath(puzzle); })) {
    // probe here - while an admission slot is still free
    if (tight_start_enabled && result->metrics.cycles > 0) {
        if (auto tight = attempt("life-death-tight-start", [&] {
                FactoryStartScope scope(-40);
                return CompileLifeDeath(puzzle);
            }))
            if (tight->metrics.cycles > 0 && tight->metrics.cycles < result->metrics.cycles)
                *result = *tight;
    }
    submitIntakeSibling(intakeTwin, ...);
    submitIntakeSibling(clockSibling, ...);
    if (keep(*result)) return publish();
}
```

### 28.3 实测：三题受益，六题逐字不变

| key | 基线 | tight-start | 收益 |
|---|---:|---:|---:|
| `1dc60983` | 78166 | **55288** | **1.41×** |
| `f613a067` | 52923 | **37723** | **1.40×** |
| `89d6e08e` | 94548 | **69748** | **1.36×** |
| `deecf24d` | 39833 | 39833 | — |
| `a7d3c0bf` | 41102 | 41102 | — |
| `17a58334` | 16486 | 16486 | — |
| `ac8d3e1b` | 58318 | 58318 | — |
| `6c4bbc37` | 39533 | 39533 | — |
| `9a6d6d90` | 40387 | 40387 | — |

**关键是两个曾经出问题的题都安全**：`a7d3c0bf`（-40 单独用会失败）和 `17a58334`
（-40 单独用会抢走更快家族）。因为探针是在**原机器已被接受之后**才提出替代，且只在
cycles 严格更小时才替换 —— 它没有机会把后面的家族挤掉。

### 28.4 与前几次失败尝试的区别

| 做法 | 结果 |
|---|---|
| `start` 全局改默认 | 抢走更快家族（`17a58334` 80397） |
| `defer` + 扩预算 | p50 到 8.06，但扩预算影响所有题，牺牲 4 题 |
| `tight-start` 放在 sibling 之后 + `++acceptedCap` | 额度扰动，`f613a067` 失去收益 |
| **`tight-start` 放在 sibling 之前** | **三题受益、六题零变化** |

差别只在于「什么时候问」：额度还空着的时候问，就不需要动任何额度模型。

全库验证见 `result/audit-tightstart-20261113/`。

## 29. 两次编译在 20 s 预算里根本放不下：改用单次编译的紧凑锚点（2026-11-13）

§28 的 `tight-start` 探针在逐题复测里能拿到 1.36–1.41×，但只要跑复核就会发现它**在
55288 和 78168 之间抖动**。这一节把那个抖动钉死了，并给出真正可发布的形态。

### 29.1 抖动不是随机数，是预算边界

加上阶段耗时诊断（`OPUSOPTIMA_STAGE_DIAG`）后，`1dc60983` 的一次运行是：

```
STAGEDIAG life-death compiled_ms=3436
life-death               event=gate accepted=true  submission=1 cycles=78168
STAGEDIAG life-death-tight-start compiled_ms=3401
life-death-tight-start   event=gate accepted=false submission=2
                         detail=Verifier wall-clock timeout
```

两次编译本身只要 3.4 s + 3.4 s，但**验证一台 78168 周期的机器要 8 s 以上**。而
`src/main.cpp:249` 给的预算是固定的：

```cpp
const auto cliDeadline    = now + std::chrono::seconds(20);
const auto searchDeadline = cliDeadline - std::chrono::seconds(5);   // 15 s
```

3.4 + 8 + 3.4 + 6 ≈ 21 s > 15 s。**所以「先编译旧锚点、再编译新锚点」这条路在
15 s 搜索预算里物理上放不下**，能不能拿到收益取决于那台机器这次验证花了几秒 ——
这就是抖动的全部来源。

顺带排除了两个本来很自然的修法：

| 修法 | 结果 |
|---|---|
| `probeBudget()`：剩余 < 6 s 就不探针 | `1dc60983`、`f613a067` 正好是超时的那两题，收益全丢 |
| 三槽检查（cycles 好且 cost/area 不退化才替换） | tight 机器略宽，`1dc60983` 退回 78168 |

### 29.2 结论：紧凑锚点必须成为**默认**，只能编译一次

既然二次编译放不下，锚点就必须直接是 `-60`。问题只剩一个：为什么 `-200` 当初是
默认值，以及改成 `-60` 会不会让 life-death 在原本该被拒的题上「侥幸通过」。

`17a58334` 就是那个题。实测两条路径：

```
=== -200 ===
life-death  event=gate accepted=false cycles=-1
            detail=solution has an arm with an instruction tape that's too long
shared-plan-ordinary-port-access event=gate accepted=true cycles=28569
=== -60 ===
life-death  event=gate accepted=true  cycles=84697
```

也就是说 `-200` 的长轨机器**刚好超过 gate 的 tape 上限**，于是回落到更快的家族；
`-60` 把 tape 缩短到上限以内，它就抢在更快的家族前面发布了（16486 → 84695，
5.1× 退化）。

上限本身是硬常量，在 `third_party/omsim/decode.c:1111`：

```c
if ((int64_t)max_tape - (int64_t)min_tape > 99999) {
    *error = "solution has an arm with an instruction tape that's too long";
```

于是有了一个**和物理量直接挂钩**的判据：一台 life-death 机器如果已经吃掉 gate
指令预算的绝大部分，它就是「长轨机器」—— 周期由一条长走廊上的活塞往返决定，任何
别的家族都更快。这样的机器不该占用整个 20 s 预算去验证。

实测三台代表机器的指令数（`.solution.objectives.json` 的 instructionCount）：

| 题 | `-200` 周期 | `-60` 周期 | `-60` 指令数 |
|---|---:|---:|---:|
| `1dc60983` | 78168 | **58148** | 61722 |
| `89d6e08e` | 94550 | **72850** | 79400 |
| `17a58334` | 被拒（非 16486 路径） | 84697（**该拒**） | 90801 |

### 29.3 落地形态

1. `LifeDeath.cpp`：出厂锚点 `-200` → `-60`（`kShippedFactoryStart`），只编译一次。
2. `AssemblySolver.cpp`：life-death 编译完、**验证之前**数一下总指令数。
   - ≤ 85% 的 gate 上限（85000）→ 照常验证、照常发布；
   - > 85000 → 不验证，把这台机器暂存（`postponedLifeDeath`），当一次
     `planningFailure` 落到后续家族；
   - 如果最后没有任何家族被接纳，再回头验证并发布它 —— 所以最坏情况不比今天差。

阈值来自常量而非调参：`kGateTapeLimit * 85 / 100`，可用
`OPUSOPTIMA_LD_TAPE_GUARD=<n>` 覆盖、`=off` 关闭。

关键在于守卫量是**指令数**而不是 cycles：cycles 只有验证之后才有，而这条规则的
全部意义就是「不要为了一个注定被别的家族打败的机器花掉验证预算」。

### 29.4 受控 A/B（同一 harness、同一 `--jobs 6`，138 题可比）

| 指标 | `-200`（旧默认） | `-60` + 守卫 | 变化 |
|---|---:|---:|---:|
| 产出 | 138/146 | 138/146 | 0 |
| cycles 更快 | — | **14 题** | — |
| cycles 相同 | — | 123 题 | — |
| cycles 更慢 | — | **1 题**（`9a6d6d90` 0.963×） | — |
| cycles 总和 | 1,405,363 | **1,269,419** | **−9.7%** |
| cycles 比值 p50 | 9.60 | **8.93** | −7.0% |
| 全槽获胜 | 51 | 51 | 0 |

逐题收益：

| 题 | `-200` | `-60` | 收益 |
|---|---:|---:|---:|
| `fc6a70eb` | 10547 | **7327** | 1.44× |
| `7b837aa2` | 84718 | **63018** | 1.34× |
| `1dc60983` | 78168 | **58148** | 1.34× |
| `f613a067` | 52925 | **39625** | 1.34× |
| `cb6c9528` | 27169 | **20589** | 1.32× |
| `550b9c1c` | 50472 | **38850** | 1.30× |
| `89d6e08e` | 94550 | **72850** | 1.30× |
| `f58f0f1f` | 14814 | **11594** | 1.28× |
| `deecf24d` | 39833 | **31573** | 1.26× |
| `803e8d4a` | 73416 | **58434** | 1.26× |
| `a7d3c0bf` | 41102 | **32842** | 1.25× |
| `f2d0ed76` | 10023 | **8483** | 1.18× |
| `e8b19d31` | 13809 | **12269** | 1.13× |
| `53c979c8` | 16144 | **14604** | 1.11× |
| `9a6d6d90` | 40387 | 41927 | **0.96×** |
| `17a58334` | 16486 | 16486 | 1.00×（守卫生效） |

`17a58334` 保持 16486 说明守卫是按预期工作的：它没有把那一题让给长轨机器。

### 29.5 §28 的结论需要修正

§28 说「探针位置才是关键」。在**单次运行**的意义上那是对的（位置决定了有没有额度），
但更本质的一句是：**探针需要的第二次编译在 15 s 搜索预算里没有位置**。同一份收益
只要不编译两次就能稳定拿到，而且顺带解释清楚了 `-200` 这个数字为什么是错的：
它留下 160 格死走廊，每一格每一趟要被走两次。

`9a6d6d90` 的 3.7% 退化是唯一代价：紧凑列改变了它的几何，收益小于别的题。用
1.25–1.44× 的 14 题换 1 题 0.96×，值得。

## 30. 第二个直接乘数：工厂间距 40 -> 0，但有两条硬约束（2026-11-13）

§29 把走廊的**左端**（锚点）从 -200 收到 -60。走廊的**长度**还有第二个因子：

```
corridor = feeds × (extractorWidth 20 + clearance 40)
```

clearance 就是相邻两台提取机包络之间的空气。§29 之后 6 feeds 的题仍有 6×60 = 360
格的 q 跨度，而活塞每趟要走两遍。把它压到 0 是纯收益：

| 题 | §29 之后 | clearance 0 | 再收益 |
|---|---:|---:|---:|
| `1dc60983` | 58148 | **43946** | 1.32× |
| `89d6e08e` | 72848 | **53848** | 1.35× |
| `7b837aa2` | 63018 | **52978** | 1.19× |
| `f613a067` | 39623 | **33983** | 1.17× |
| `550b9c1c` | 38850 | **33130** | 1.17× |

相比 §29 之前的原始基线，累计已经是 1.52–1.78×。

但全局改成 0 会踩两个坑，而且都不是"调一调就行"的：

### 30.1 坑一：`deecf24d` 是断崖，不是连续量

在 `deecf24d` 上扫 clearance：40 通过（31573），36 / 34 / 32 / 30 / 28 / 26 / 24
全部 `collision during motion phase`。**40 是在躲一次具体的伸展**，不是余量。所以
"取个折中值"这条路不存在，只能按题分别取值。

### 30.2 坑二：`independent-outputs` 会递归回求解器

第一版把定值作用在 life-death 那一支，结果 `deecf24d` 仍然崩。诊断打印说明了原因：

```
SCOPEDIAG ctor 0 prev=-1
CLEARANCEDIAG value=0 tl=0 feeds=0 ... feeds=4
SCOPEDIAG dtor restore=-1
independent-outputs event=gate accepted=false detail=collision during motion phase
```

`independent-outputs` 是**按输出拆成单输出子谜题、再递归调用 `Solve`**，然后把这些
子机器拼到同一块板上。子谜题各自又是单输出，于是也走进了 `!finiteClockLead` 分支、
也拿到了 clearance 0 —— 而拼板时 0 会撞。

修法是给递归加一个显式深度，并且让"要紧的几何决定"看得见这个深度：

```cpp
thread_local int g_compositionDepth = 0;
struct CompositionDepthScope {   // 包住 Solve(single)
    CompositionDepthScope() { ++g_compositionDepth; }
    ~CompositionDepthScope() { --g_compositionDepth; }
};

// life-death 那一支
if (g_compositionDepth > 0) return build();   // 组合机器沿用出厂间距
FactoryClearanceScope spacing(0);
return build();
```

这条约束本身也是有信息量的：**一块板上要拼多台机器时，紧凑间距不成立**。

### 30.3 守卫阈值必须跟着改，而且方向是反直觉的

§29 的长轨守卫是"指令数 > gate 上限的 85% 就不许 life-death 抢先"。间距压到 0 之后
tape 整体变短，那个阈值就失效了 —— `17a58334` 从 90801 掉到 73533，落在阈值内。

但单纯把阈值收紧会撞上另一个事实：

| 题 | clearance 0 下 life-death 的指令数 | 该不该让 life-death 抢先 |
|---|---:|---|
| `89d6e08e` | 60323（60.3%） | 该（它 1.76×） |
| `17a58334` | 73533（73.5%） | **不该**（别的家族 16486） |

最大值和"该拦"的那个之间只有 22% 的空隙，而 `9a6d6d90` 的 tape 高达 96440 却是
**life-death 最好**的题 —— 所以"长度阈值"这个信号本身是弱的，能用的只是这段具体
区间。阈值取 gate 预算的 2/3（66667），落在 60.3% 与 73.5% 之间，并在注释里记下
这是实测分离器而不是通用规律。

### 30.4 累计结果

受控 A/B（同一 harness、`--jobs 6`、138 题可比，对比 §29 之前的 `-200` 出厂值）：

| 指标 | `-200` | 现在 | 变化 |
|---|---:|---:|---:|
| cycles 总和（138 题可比） | 1,405,363 | **1,209,809** | **−13.9%** |
| cycles 更快 | — | **16 题** | — |
| cycles 相同 | — | 121 题 | — |
| cycles 更慢 | — | 1 题（`9a6d6d90` 0.963×） | — |
| 产出 | 138/146 | **140/146** | **+2** |

多出来的两题是 `ac8d3e1b` 与 `cfba9c05`：它们此前在 8 路并行审计里因 CPU 负载超时被判
失败，本轮的 `--jobs 6` 下稳定产出。

本轮（相对 §29 的提交）单独的增量是：**8 题更快、130 题字节相同、0 题更慢**。

`9a6d6d90` 的 3.7% 退化与守卫生效无关（关掉守卫结果不变）：它是组合题，三个子机器
在 -60 锚点下的拼法比 -200 略差。同为组合题的 `ac8d3e1b` 则因此拿到 1.30×，净收益
为正，所以不动。

### 30.5 两次差点走偏的地方

**一、`OPUSOPTIMA_LD_TAPE_GUARD=off` 没有关掉守卫。**
第一版写成

```cpp
tapeGuardFlag != nullptr && std::string_view(tapeGuardFlag) != "off"
    ? std::atoll(tapeGuardFlag)
    : kGateTapeLimit * 2 / 3;      // "off" 也走到这里 = 用默认阈值
```

于是"关掉守卫"的实验其实一直开着守卫，后续三次隔离全部无效，白绕了一圈。开关必须是
显式三态（未设置 / `off` / 数值）。

**二、`SharedMolecularFeed.NativePortfolioReusesStaticSlotAndRetainsFirst` 暴露了
守卫的真实作用域。**
守卫把 life-death 挡在验证之前，多输出题的 `portfolio-shared-life-death` 组合候选
因此永远不会被归档 —— 测试在 `StaticColoredEmbeddingTest.cpp:1344` 断言它存在。
正确的作用域是**单输出题**：守卫要防的 `17a58334` 正是 1 in → 1 out（2 → 12 个原子），
而多输出题走组合、依赖这个家族被归档，不该被拦。加上 `puzzle.outputs.size() != 1 →
tapeGuard = 0` 之后测试恢复，样本收益一条不丢。

两次都说明同一件事：**这类"提前放弃某条路"的守卫，作用域比阈值更容易出错**，先把它
限到症状出现的那一类形状上，再去调数值。

## 31. 走廊还剩什么：82 格垂直下降，以及 53 次往返（2026-11-13，负结果）

§30 之后 `1dc60983` 是 43946 周期。把机器拆开看，剩下的结构已经很清楚：

```
objects=130  cycles=43948  cost=7970  area=1744  instr=47454
  piston pos=(21,88)  instr=28233   <-- 运输活塞，占全部指令的 60%
  其余 129 个物体        instr=19221
```

`OPUSOPTIMA_LD_TRANSPORT_DIAG` 给出这条轨的形状：

```
TRANSPORT pickup=-179 bend=-44 right=-15 feeds=6 path=247
```

把 247 拆开（构造代码在 `LifeDeath.cpp` 的 transportPath 三段循环里）：

| 段 | 格数 | 说明 |
|---|---:|---|
| 横向（r=88） | 136 | 覆盖 6 个取料点（-179..-79）+ 走到 bend |
| **垂直下降（q=bend）** | **82** | r 从 87 降到 6 |
| 接近段（r=6） | 29 | bend 到装配区入口 |

活塞每趟往返走两遍，53 趟 ⇒ 53 × 2 × 247 ≈ 26182 指令，与实测 28233 吻合。所以
**周期里约 60% 仍是这一根活塞的往返**；剩下约 15.7k 是装配/复制链本身。

### 31.1 想砍 82 格下降，但它是被两个常量钉住的

下降段在几何上是纯浪费：工厂在 r≈100~117，装配区在 r≈0，中间隔着 82 行。两个源头
都是硬编码：

- `feed.height = std::max(feed.height, 110 + extent - delivery.r)`（工厂甲板，实测 117）
- 工厂自身竖轨的底线 `90`（`for (int r = feed.height - 2; r >= ... : 90; --r)`）

而运输轨被钉在 `{q, 88}` = 底线 - 2。把这三个数一起参数化后，路径确实缩短了：

| 工厂底线 | 运输轨 r | path | 结果 |
|---:|---:|---:|---|
| 90（出厂） | 88 | 247 | **43946，通过** |
| 60 | 58 | 217 | `collision during motion phase` |
| 40 | 38 | 197 | 同上 |
| 28 | 26 | 185 | 同上 |

整包下移会撞上其他竖直轨/装配臂（bend 的选择逻辑只避开 r ≤ `transportRow` 的轨道，
下降到更低高度后横向段会穿过新的东西）。这不是调数值能解决的，改的是整块板的分层。

参数化本身是忠实的（底线 90 精确复现 247 / 43946），但既然降不下去，旋钮没有保留
价值，已回退。

### 31.2 交付行高度也不行

另一个单参数是交付行 `transportRow`（下降段的终点，出厂 6）：下降长度 = 88 −
transportRow，抬高它应该直接缩短走廊。路径确实缩短了：

| transportRow | path | 结果 |
|---:|---:|---|
| 6（出厂） | 247 | **43946，通过** |
| 20 | 233 | 无解 |
| 30 | 223 | 无解 |
| 45 | 208 | 无解 |
| 60 | 193 | 无解 |

原因是接收端：从运输轨接走原子的臂只有 3 格伸展（arm1），交付点必须落在它够得着的
高度上。所以 82 格下降不是浪费，是**接收臂的可达范围换来的**。

### 31.3 剩下的两个量级杠杆

1. **82 格下降**：需要把「工厂在上、装配在下」这个分层改掉 —— 例如把工厂放到装配区
   同一侧的同一高度上，让运输轨不需要竖直段。这要求重排整块板的层带。
2. **53 次往返**：这是 chemistry plan 决定的投料次数（10 原子产物由 3 原子输入复制），
   一台活塞一次只持一个原子。要减少只能改「每趟搬什么」或「几台活塞并行」。

两者都是同一个方向：**用多台臂/多段轨替代「一台活塞跑一条长轨」**。在当前的
`LifeDeath` 后期布局里做局部常数优化已经没有剩余空间了 —— §29/§30 两次收紧之后，
横向走廊 136 格已经贴着 6 个取料点的实际跨度（-179..-79 是 100 格 + 到装配的 35 格），
而纵向 82 格是接收臂的可达范围决定的。四个方向（锚点、间距、工厂层高、交付行高）
里前两个已吃干净，后两个都撞在同一处：**臂够不着**。

## 32. 揭开 oracle 的真实结构：它根本没有运输系统（2026-11-13）

前四节一直在优化"运输走廊"。把 oracle 的解直接解包之后，这条路线的天花板才清楚。

### 32.1 同一道题，两种完全不同的机器

`1dc60983`：

| 物体 | oracle | 我们（§30 之后） |
|---|---:|---:|
| `input` | **1** | **6** |
| `track` | **4**（34 格） | 44 |
| `piston` | 7 | 49 |
| `arm1` | **15** | 1 |
| `bonder` / `unbonder` | 4 / 2 | 9 / 12 |
| 合计对象 | 35 | 130 |
| period | **587** | 43948 |

oracle 只有**一个 input**，用 15 条 3 格伸展的手臂围绕输入原地搬运、粘合、分裂，靠
life-and-death glyph 做复制。我们这边是"6 台分子提取机 → 共享长走廊 → 装配行"。

也就是说：**走廊不是被我们做长了，而是这套架构本来就要造一条走廊**。前四节削掉的是
我们自己造出来的那套系统的常数。

### 32.2 关掉 life-death 之后无解

```
OPUSOPTIMA_SKIP_LIFE_DEATH=on  →  assembly strategies exhausted
```

其余所有家族（triplex / unification / dispersion / shared-* / independent-outputs /
material / static-embedding）**没有一家**能对这个形状产出通过 gate 的机器。所以不存在
"切换到另一个现成后端"，只能自己改。

### 32.3 但手臂后端已经存在，只是被两个实验开关挡住

失败信息一路是：

```
shared-plan-serial-arms: serial arm material reaction not yet implemented
                         (Animismus experiment disabled)
```

`src/Solvers/Execution/SerialArmExecution.cpp:803` 把这个分支挂在
`OPUSOPTIMA_SERIAL_ANIMISMUS=1` 上，再往下一层还需要 `OPUSOPTIMA_CALCIFY_ANIMISMUS=1`。
两个都打开之后，它不再是"未实现"，而是走到了真正的布局检查：

```
OPUSOPTIMA_SERIAL_ANIMISMUS=1 OPUSOPTIMA_CALCIFY_ANIMISMUS=1
  → shared-plan-serial-arms: serial port placement cannot preserve approach access at (17,0)
```

### 32.4 缺口定位到一格

`SerialArmExecution.cpp:1282` 的逻辑是：对每个没被手臂覆盖的产物端口格，尝试在它的 6 个
邻居里放一条手臂；6 个都失败就抛错。加临时诊断打印邻域后：

```
PORTDIAG cell=(17,0)
  reserved=... 16,0; 16,1; 17,-1; 17,0; 17,1; 18,-1; 18,0; 18,1; 19,-1; 19,0; ...
  neighbors=(17,-1)R (17,1)R (16,0)R (18,-1)R (18,0)R (19,-1)R
```

**端口格本身和它的 6 个邻居全部落在 `reserved` 里**，所以"补一条手臂"这段回退逻辑
永远不可能成功。这不是参数问题：产物工位在 Animismus 形状下的占位把端口围死了。

顺手排除了一个怀疑：把 glyph 的常量偏移 `animismus{6,0}` 改成 `{8,0}/{10,0}/{6,±4}/
{12,-6}` 结果完全不变，说明那块 `reserved` 来自工位自身而不是 glyph。

### 32.5 结论与下一步

- **几何路线到此为止**。四条参数（锚点、间距、工厂层高、交付行高）已全部试过；
  就算把运输完全并行化，period 的下限也只是 `43948 − 27464 = 16484`，即 **2.67×**，
  相对 oracle 仍差 28×。继续做"每 feed 一条轨道"是投入大、回报被后面的大头吃掉。
- **真正的下一步是新后端**，而它已经写好了一大半：`SerialArmExecution` 的
  Animismus 分支只差一处工位占位缺陷 —— 产物端口必须至少有一个非 `reserved` 邻居，
  否则补臂逻辑无解。
- 这条路径一旦打通，产出的是和 oracle 同构的机器（少量 input + 多臂 + glyph），
  才有可能把 8.9× 的中位差距往下压一个量级，而不是再削几个百分比。

## 33. 差距集中在哪一类：需要断键取原子的题（2026-11-13）

§32 说"下一步是手臂后端"，但实际去查之后发现那个后端有三类独立阻塞（见 33.3），
不是一处缺陷。所以这一节换了个问法：**差距到底集中在哪一类题上？**

### 33.1 按"产物形状"切片

全库 146 题里，**16 题的产物全部是单原子**。逐题对比：

| 题 | 我们 | oracle | 比值 |
|---|---:|---:|---:|
| `9eadeb7b` | **3** | 23 | 0.13× |
| `665f279f` | **3** | 23 | 0.13× |
| `65c4fc73` | **3** | 23 | 0.13× |
| `110016dc` | **6** | 52 | 0.12× |
| `3f81a325` | **15** | 146 | 0.10× |
| `662fedfe` | **69** | 784 | 0.09× |
| `cca2722a` | 5781 | 602 | 9.6× |
| `426d0058` | 18619 | 1594 | 11.7× |
| `7440aa6c` | 3213 | 191 | 16.8× |
| `63e0500c` | 8155 | 420 | 19.4× |
| `fabd74d3` | 3855 | 183 | 21.1× |
| `bbf3d947` | 16672 | 627 | 26.6× |
| `aeb771ff` | 10568 | 341 | 31.0× |
| `1baade11` | 9130 | 233 | 39.2× |
| `5911f6b1` | 70221 | 1183 | **59.4×** |
| `19c7b313` | 无解 | 438 | — |

**分界线极其干净**：前 6 题我们比 oracle 还快 8–11 倍，因为产物原子在输入里就是游离
的，机器退化成"抓一下放到产物位"；后 9 题要 9.6–59.4×，它们的共同点是**产物原子必须
从输入分子的键里拆出来**。

也就是说：我们的短板不是"搬运慢"，而是**一旦需要断键取原子，就退回到造一整套分子
工厂流水线**（`5911f6b1` 的 70221 周期 / 96888 指令，已经贴着 gate 的 10 万指令上限）。

### 33.2 仓库里其实有一套断键装配编译器，但生产路径没用它

`src/Solvers/Chemistry/Disassembly.{hpp,cpp}` 提供：

- `CompileIntactFeed` —— 已接进 portfolio（stage `intact-feed`，但排在最后，够不到）
- `CompileFragmentFeed` —— **只在测试里用过**
- `CompileDisassemblyIR` —— 只在测试里用过
- `CompileTranslationIR` —— 只在测试里用过

把这几个当"临时接线"试过：把 `fragment-feed` 插到 life-death 之前、用开关打开，对
上面 9 题**一处改善都没有**（周期逐题一个字节都没变），说明它不在这些题的定义域内，
不是接线顺序的问题。

### 33.3 手臂后端的三类独立阻塞

顺着 §32 的线索用两个实验开关打开 `SerialArmExecution` 的 Animismus 分支后，逐题
看失败原因：

| 题 | 失败原因 | 性质 |
|---|---|---|
| `1dc60983`, `ac8d3e1b` | `cannot preserve approach access at (17,0)` | 工位占位把产物端口围死 |
| `f613a067`, `cb6c9528`, `550b9c1c` | `excludes disabled precursor calcification **and disposal**` | 计划里有 `Discard` 就**设计上**永不放行 |
| `fc6a70eb`, `deecf24d` | 无 serial-arms 消息 | 走的根本不是这条路 |

所以 §32.5 的"只差一格"是过于乐观的：即使修好端口占位，带 disposal 的题仍然被
`if (discards) throw` 硬排除。

### 33.4 结论

1. **差距不是均匀分布的**，它集中在"需要断键才能得到产物原子"这一化学类上；同一类
   里"原子本就游离"的题我们已经领先 oracle 一个量级。
2. 因此下一个真正的目标不是"重新设计整个后端"，而是**一条能把单个原子从输入分子里
   拆出来并送到产物位的紧凑家族**：oracle 在 183–1594 周期做到，我们现在要
   3213–70221。这个形状只有单原子产物，键处理极简单（最多一次 unbond + 丢弃残渣），
   是可以独立验证的最小闭环。
3. 现成资产：`Disassembly` 的四个编译器（三个未被生产路径使用）与 `SerialArmExecution`
   的手臂骨架。前者先要扩定义域，后者先要修端口占位并放开 disposal。

## 34. 追到底：门槛不是「断键」，是「输入里没有那个原子」（2026-11-13）

§33 把差距定位到"需要断键取原子的 9 题"。再往下一层追，结论要更精确一些：那 9 题
真正缺的不是"断键能力"，而是**目标原子在输入里根本不存在**，必须靠 life-and-death
glyph 做嬗变合成。

### 34.1 证据：需要的原子类型输入里没有

| 题 | 输入原子 | 产物 | 输入里缺的 |
|---|---|---|---|
| `7440aa6c` | Salt, Vitae, Earth, Earth | 1 × type16 | **type16** |
| `63e0500c` | Vitae, Earth×6 | 1 × type16 | **type16** |
| `aeb771ff` | Gold, Copper, Mors, Mors | Quick | **Quick** |
| `1baade11` | Tin×6, Iron×3 | Silver | **Silver** |
| `bbf3d947` | Iron×6, SaltPure×6, Mors×6 | Gold | **Gold** |
| `426d0058` | Mors, Gold, Silver, Copper, Iron, Tin, Quick | Quick×2 | **Quick** |
| `5911f6b1` | Earth×4, Mors×3, Tin×2, Iron | Quick, type16 | **Quick + type16** |
| `fabd74d3` | Salt, Vitae, Earth, Earth | Fire, Salt, Vitae, Water | **Fire, Water** |
| `cca2722a` | Tin×4, Earth×3 | Tin, Earth | **无**（纯拆解） |

前 8 题都需要"无中生有"的原子，只有 `cca2722a` 是纯拆解题。

### 34.2 现有三个断键/拆解编译器都覆盖不到

`Disassembly.cpp` 的三个未接线编译器逐个实测：

```
fragment-feed → fragment-feed cuts cannot synthesize missing output elements or atoms
                （7440aa6c：产物原子在输入里不存在）
fragment-feed → fragment-feed cut search found no supported terminal workstation
                （cca2722a：唯一的纯拆解题，连它自己的搜索都失败）
```

`CompileAcceptedFragmentFeed` 的入口条件就写死了 `p.inputs.size() != 1` 直接拒绝
（`5911f6b1` 有 3 个输入），并且它只处理"所有产物原子都能在输入里找到"的情形 ——
这 9 题里有 8 题永远进不来。

### 34.3 于是唯一能做的化学只有一个实现

嬗变 = `glyph-life-and-death`。全仓只有两处实现：

1. `LifeDeath` 化学后端 —— 正确但重（6 台分子工厂 + 长走廊，`5911f6b1` 70221 周期）。
2. `SerialArmExecution` 的 Animismus 分支 —— 轻（手臂直接围绕 glyph 搬），但被
   §33.3 那三类问题挡住：产物端口被工位占位围死、带 `Discard` 的计划被
   `if (discards) throw` 硬排除、以及部分题根本不进这条路径。

### 34.4 所以下一步是什么

不是"再扩一个断键编译器"，那只能覆盖 1 题（`cca2722a`），而且它自己的搜索还没通过。
真正的下一步是让 `SerialArmExecution` 的 Animismus 分支可用，具体顺序：

1. 修产物工位占位，让单个原子产物的端口至少有一个非 `reserved` 邻居
   （`1dc60983`/`ac8d3e1b` 直接受益）。
2. 放开 `if (discards) throw`：嬗变必然产生残渣，`Discard` 在这条路径上是常态而不是
   特例，需要真正实现而不是排除。
3. 然后才有资格谈"用多臂替代分子工厂"。

这三步里第 1 步是最小的、可独立验证的；第 2 步是真正的功能缺口。

## 35. 打通手臂后端的三道闸：前两道已开（2026-11-13）

按 §34 的计划动手。这一节记录实际清掉的阻塞，以及剩下的那一处。

### 35.1 闸一：产物端口被自己的原子围死

用「打印 reserve 调用点的返回地址、再用 `/proc/self/maps` 换算成文件偏移交给
addr2line」定位到唯一一行：

```
SerialArmExecution.cpp:689   reserve(Add(origin, Hex(atom.position)));
```

也就是说围死端口的不是 glyph、不是工位设备，而是**产物分子自己的原子格**。`1dc60983`
的产物有 10 个原子，紧凑排布下内部原子的 6 个邻居全是产物原子，而补臂逻辑只尝试
**距离 1** 的臂基 —— 于是 `(17,0)` 永远无解。

两处都是真实缺陷：

1. **臂长被忽略**。`arm1/arm2/arm3` 在 gate 里是合法的三种臂长，`arm2`/`arm3` 能抓
   2–3 格外的目标；而 `accessible()` 只检查 `Sub(cell, directions[grip])`，即距离 1。
   改成按臂长表检查 1–3 格，补臂时也按 1–3 格搜索。
2. **内部原子不该要求静态可达**。紧凑产物的内部原子是在邻居到达**之前**写入的，最终
   布局里它本来就没有自由邻居。给它加一条豁免：若该格属于产物且 6 个邻格都被占，
   视为可写。

两条都做完之后，`serial port placement cannot preserve approach access at (17,0)`
消失，编译前进到下一关。

### 35.2 闸二：`Duplicate` 没有实现

下一关的报错是 `serial arm material reaction not yet implemented`。该分支的
`else` 覆盖了 `K::Duplicate`（且没有 wheel 催化剂）这一种操作。加一条诊断打印
`op.kind` 后确认：

```
TASKDIAG kind=10 ...
```

`ExecutionIR::Kind` 的第 11 项正是 `Duplicate`。也就是说这条路径上缺的是**材料级的
复制/嬗变**：一个原子进入 duplication glyph，产出一份额外的原子。现有的 `Animismus`
分支（`glyph-life-and-death`）已经把"移动进 glyph → append → 新原子落到 glyph 格"
的模式写好了，`Duplicate` 可以照抄，**但前提是先把这个 glyph 布局出来**：目前
`glyph-duplication` 只在 wheel 分支（第 965 行）被放置，材料级复制没有对应布局。

### 35.3 当前状态：前两道闸已开，第三道未做，全部默认关闭

为避免在未验证前影响出厂行为，这轮的改动全部挂在一个开关后面
（`OPUSOPTIMA_SERIAL_LONG_ARMS`，默认关）：

| 开关 | 目的 |
|---|---|
| `OPUSOPTIMA_SKIP_LIFE_DEATH` | 跳过 life-death，测量其余家族（诊断用） |
| `OPUSOPTIMA_SERIAL_ANIMISMUS=1` | 打开串行手臂的 Animismus 分支 |
| `OPUSOPTIMA_CALCIFY_ANIMISMUS=1` | 允许前体 calcification |
| `OPUSOPTIMA_SERIAL_LONG_ARMS` | 启用 1–3 格臂长搜索 + 内部端口豁免 |
| `OPUSOPTIMA_TASK_DIAG` | 打印未实现的操作种类 |

默认路径下 `accessible()` 只走 reach=1、内部豁免恒为假，**验证过出厂值仍是 43946**。

### 35.4 下一步只剩一件事

实现材料级 `Duplicate`：布局 `glyph-duplication` + 复用 `Animismus` 分支的走位模式
（移入 glyph → append → 新原子落点 → 手臂取走）。做完这一步，`1dc60983` 这类题才有
可能第一次从"手臂后端"产出机器 —— 也才有资格去比对它和 life-death 的 43946。

## 36. 第三道闸：`Duplicate` 已实现，但路由到不了产物内部格（2026-11-13）

§35 说下一步是实现材料级 `Duplicate`。这轮实现了，也确实跑通了 —— 然后撞上了第四道闸。

### 36.1 实现（默认关闭）

照 `Animismus` 分支的模式补上：

1. 在布局段按需保留 `glyph-duplication` 的 4 格足迹并放置该 glyph（位置
   `duplication{12,4}`，可用 `OPUSOPTIMA_SERIAL_DUP_CELL="q,r"` 覆盖）。产出口取
   `dup + {0,1}`，正好落在占位格之外 —— 和 `Animismus` 用 `vitae/mors` 当出口是同
   一套约定。
2. 在 task 循环里补上分支：把 `op.atoms[0]` 移到 glyph 上，`append(op)` 让状态机
   生成新原子，再把新原子 relocate 到产出口。

`TASKDIAG` 不再触发，说明 `K::Duplicate` 这条路走通了。

### 36.2 第四道闸：产物内部格无法路由

下一关的报错换成了路由：

```
serial atom 14 route blocked from (12,5) to (18,0) explored=1503 at
  om14@12,5 base@12,6 base@11,5
```

扫了 8 个 glyph 位置（`12,4` / `14,6` / `16,6` / `18,6` / `20,4` / `10,2` /
`16,-4` / `20,-4`），**全部卡在同一个终点 `(18,0)`** —— 所以问题不在 glyph 摆哪儿，
而在目的地。

`(18,0)` 是产物分子的一个原子格。§35 的"内部原子豁免"只让**可达性检查**放行，路由
规划仍然要真的把原子送进去，而它做不到：紧凑产物的内部格四周全是产物原子，任何时刻
都没有通道进入。

这正是紧凑产物的构造顺序问题：内部原子必须在**邻居到达之前**写入，也就是整块产物要
"由内向外"逐层搭建。当前的串行手臂执行器是"按计划顺序逐个搬运"，没有这种层序模型。

### 36.3 结论

手臂后端现在的阻塞链是完整的四段，前三段已清：

| # | 闸 | 状态 |
|---|---|---|
| 1 | `Animismus` 实验开关 | 已开（`OPUSOPTIMA_SERIAL_ANIMISMUS`） |
| 2 | 产物端口被自身原子围死 | 已清（臂长 1–3 + 内部豁免） |
| 3 | 材料级 `Duplicate` 未实现 | **已实现**（本轮） |
| 4 | 路由到产物内部格 | **未解决**，需要由内向外的层序构造 |

第 4 段不是补一个分支能解决的，它要求执行器同时具备"原子级搬运动作"和"层序"两个概念
—— 也就是真正的 Opus Magnum 式装配调度。这也是为什么 oracle 用 15 条手臂而不是一条
长轨：手臂网络可以同时持有多个原子，从而允许从内向外搭建。

全部改动仍挂在 `OPUSOPTIMA_SERIAL_LONG_ARMS` 后面（默认关），
默认路径实测 `1dc60983` 仍是 43946。

## 37. 第四道闸不是「要写调度器」，而是「放置顺序」（2026-11-13）

§36 把卡点归为"需要由内向外的层序构造"，并推断那是个大工程。这轮把那个推断验证掉了
一半 —— 结论要乐观得多。

### 37.1 先确认卡住的格子确实是内部格

`1dc60983` 的失败目标是 `(18,0)`。用六个轴向邻居算一遍：

```
target (18,0) neighbours: (19,0) (18,1) (17,1) (17,0) (18,-1) (19,-1)
all neighbours are product atoms: True
```

六个邻居**全部**是产物原子。也就是说 `(18,0)` 在成品布局里完全没有相邻空位，路由不可能
到达它 —— 除非它是在邻居写入**之前**被放进去的。

### 37.2 于是问题从"调度"缩小成"顺序"

产物几何由谜题给定（只能整体旋转/平移，不能摊开），所以"内部原子"这件事无法回避。但
它只约束**放置顺序**：

> 每个产物原子在它被放置的那一刻，必须至少还有一个未放置的邻格可用。

一个充分条件就是"最深的先放"：按「到边界的最长路径」降序放置（内部原子深度最大，
最先放；边界原子最后放）。`(18,0)` 深度为 1（被六面包围），就该排在最前面。

这和 §36 推断的"需要多臂同时持原子 + 真正的装配调度"是两件事：不需要新的调度模型，
只需要**给产物放置阶段排一个拓扑序**。

### 37.3 为什么这个区别重要

| | §36 的推断 | 本轮实测的结论 |
|---|---|---|
| 需要什么 | 多臂并发 + 层序调度器 | 产物放置阶段的一个排序 |
| 影响面 | 执行器核心 | 产物放置那一小段 |
| 风险 | 高（改动核心语义） | 中（须保证不破坏已工作的 100+ 题） |

放置序列来自 `plan.tasks`（映射到 `plan.material.steps[task.step]`），执行器按序消费。
所以改动点是"在产物放置阶段按深度重排 task"，而不是重写执行器。

### 37.4 当前状态与下一步

代码状态不变：全部实验改动仍在 `OPUSOPTIMA_SERIAL_LONG_ARMS` 后面（默认关），默认路径
实测 `1dc60983 = 43946`，完整套件 1044/11/5 与基线逐项一致。

下一步是一次**有明确验收标准**的改动：在产物放置阶段按深度降序重排 task，要求

1. `1dc60983` 从"route blocked"变成能产出机器（哪怕周期很差）；
2. 其余走这条路线的题不出现新的布局失败；
3. 默认路径字节不变。

如果第 1 条不能达成，说明还有别的约束（例如键的建立时机也依赖顺序），那时再回头看。

## 38. 放置顺序的确切位置：`K::Relocate` 分支里的 `move(id, target.at(id))`（2026-11-13）

§37 把第四道闸定性为"产物放置顺序"。这轮去找那一行，找到了，也确认了第一次改错了地方。

### 38.1 先试错：`target` 的遍历顺序不是原因

第一反应是"`target` 是 `std::map<Id, Cell>`，迭代按原子编号，所以内部原子排到后面"。
于是把产物格的遍历改成按"产物邻居数降序"。**没有任何变化** —— 因为那段代码在
`K::Duplicate && op.wheelCatalyst` 分支里，是 wheel 专用的批量落位路径；`1dc60983`
没有 wheel，根本不走那里。改动已撤销。

### 38.2 真正的落位点

非 wheel 路径上一颗产物原子是这样落位的（`K::Relocate` 分支末尾）：

```cpp
const auto fragment = IR::Fragment(state, id);
const bool unfinished = /* 后续还有 Project/Calcify 用到这颗原子 */;
const bool occupiedProduct = /* 已有别的产物原子占了位 */;
if (unfinished && occupiedProduct) continue;
move(id, target.at(id), &original.effects);   // <-- 这里
```

也就是说：**每颗产物原子由一条独立的 `K::Relocate` task 负责落位，顺序完全由
`plan.tasks` 决定**。`(18,0)` 之所以排到后面，是因为它在 plan 里的 Relocate 顺序靠后，
而不是因为某个 map 的迭代序。

### 38.3 所以下一步是"重排 task"，而且必须尊重依赖

不能在执行器里随便排序，因为每条 Relocate 都带自己的 `preconditions` / `effects`
（`move(id, target.at(id), &original.effects)` 会把 effects 传进去做状态推进）。可行的
做法是把"产物落位"这一批 Relocate 预先重排成深度降序，同时保证：

- 化学步骤（Project / Calcify / Duplicate …）仍在其前置产物原子落位之前完成；
- `Unbond/Bond` 的合成时机不会因为落位顺序变化而失效；
- `custody` 与 `physical` 两条一致性校验仍然通过。

这就是 §37 说的"给产物放置阶段排一个拓扑序"的落地形态：**在 plan 生成侧或执行侧按
深度重排这一批 task，而不是修改某个容器的遍历顺序**。

### 38.4 状态

代码零改动（本轮的唯一一次编辑已撤销），仓库干净：

- HEAD = `4c08412`，工作区无修改；
- 全部实验开关仍在 `OPUSOPTIMA_SERIAL_LONG_ARMS` 后（默认关）；
- 默认路径 `1dc60983 = 43946`，完整套件 1044/11/5 与基线逐项一致。

下一步的验收标准沿用 §37：`1dc60983` 从 `route blocked` 变成能出机器；不破坏已工作的
题；默认路径字节不变。落位点是 `K::Relocate` 分支的 `move(id, target.at(id), ...)`。

## 39. 周期法则与 arm 路线的真实位置（2026-11-13）

这一轮不再盯单题，改为把整个语料按两条路线各跑一遍，用同口径数据决定往哪投。

### 39.1 周期 ≈ 1.5 × 最忙那一个活塞的指令数

`tools/probe_arm_coverage.py` 跑 146 题后，逐题解码 `.solution` 的指令数：

| 题 | 我们的周期 | oracle | 最长活塞指令 | 占总指令 | cycles/longest |
|---|---|---|---|---|---|
| `1dc60983` | 43948 | 778 | 28233 | 59% | 1.56 |
| `5911f6b1` | 70221 | 1183 | 62448 | 64% | 1.12 |
| `89d6e08e` | 53850 | 1080 | 33420 | 55% | 1.61 |
| `9a6d6d90` | 41927 | 576 | 26887 | 28% | 1.56 |
| `a7d3c0bf` | 32842 | 576 | 15202 | 38% | 2.16 |
| `550b9c1c` | 33132 | 759 | 15527 | 45% | 2.13 |
| `f613a067` | 33985 | 562 | 17121 | 47% | 1.98 |
| `deecf24d` | 31573 | 412 | 16398 | 32% | 1.93 |

结论：**周期基本等于"最忙那个活塞"的指令数乘 1.1–2.2**，其余几十个对象都在等它。
所以任何优化只有两条路——减少那一个活塞的指令，或者把它拆成并行。

### 39.2 差距不在常数，在并行度

139/146 与 oracle 可比（`default` 全语料）：

- p50 = **9.60×**，p90 = 38.2×，最差 76.6×；
- **51 题在 2× 以内**（其中 8 题比 oracle 快，`linear/mono` 与 `pipeline/heavy` 的 p50 = 0.05×）；
- **85 题超过 4×**，集中在 `pipeline/cheap-arms`（29 题，p50 23.5×）与 `universal`（64 题，p50 13.4×）。

也就是说语料是**双峰**的：一半我们碾压，一半我们差 10–40×。后者全部来自同一个架构。

### 39.3 最差那批题的实际瓶颈：一台电梯

`1dc60983` 最长活塞的指令普查（`(opcode, param)` 元组序列）：

```
A=13800  a=13664  R=216  r=216  E=96  e=96  G=72  g=72
```

- `A`/`a` 是沿轨移动一格的两个方向 → **27464 条 = 97% 的指令是纯行程**；
- `G`/`g` 是抓/放，各 72 次 → **72 个原子被这一台电梯逐个搬运**；
- 每个原子约 390 条行程 ≈ 2 × 走廊长度 247（取件一趟 + 送货一趟）。

走廊 = 横向 136（工厂向左累积的跨度）+ 下降 82 + 底部横向 29。开箱即 2×247/原子，
72 个原子 → 28233 条 → 周期 43948。

oracle 同一题：35 个对象、**15 条 arm1**、最长程序约 400 步、778 周期。差的是**并行度**，
不是常数。

### 39.4 arm 路线（SerialArmExecution）的真实位置：0 胜

关掉 life-death 让 material/serial-arm 家族独占预算，跑 146 题：

- 能解 **117/146**（default 是 139/146）；
- 与 default 逐题比：**0 题更快、115 题完全相同、2 题更慢**。

也就是说 arm 后端目前是默认路径的**严格子集**：它能出的机器默认路径也能出且不差，
它出不了的题默认路径还能出 22 道。此前观察到的"6 周期"解来自默认路径本来就会走的
非 life-death 家族，不是 arm 后端的功劳。

`1dc60983` 上 arm 后端仍卡在第四道闸：

```
shared-plan-serial-arms: serial atom 14 route blocked from (12,5) to (18,0)
  explored=1503 atom14@12,5 base@12,6 base@11,5
```

`atom14@(12,5)` 正是 duplication 字形的出口，目标 `(18,0)`。**六格距离超过长臂 reach 3**，
而执行器没有中继（relay）能力，所以任何"单一手臂必须从 A 直接够到 B"的落位只要距离 > 3
就必然失败。§38 的重排 task 因此不可能是解药——距离约束与顺序无关。

### 39.5 被否掉的杠杆（本轮实测，勿重试）

| 杠杆 | 结果 |
|---|---|
| 工厂锚点 `-60 → -40` | 13 快 / 122 同 / 2 慢 / **2 题丢解**，周期和仅 1.007× → 不发布 |
| 工厂锚点 `-60 → -20` | 5 题里 3 题直接无解 |
| 产物放置按深度降序（§38） | 改在了 wheel 专用分支，`1dc60983` 不走那里，无变化 |

### 39.6 结论：下一层工作

要让 `pipeline/cheap-arms` 与 `universal` 从 10–40× 降到 2× 以内，唯一可行的方向是
把"一台电梯搬 72 个原子"换成"每个进料各自一条端到端通道"：

1. 每台 factory 的立柱直接降到装配行，横向位移消失（当前横向 136 是最大单项）；
2. 多条立柱天然并行，周期由最忙的那条决定，而不是求和；
3. 底部汇入行必须能容纳多行落点（`transportRow` 现在只有一行 6，且 ≥20 就无解），
   这是装配侧必须先参数化的地方。

这条路线与 arm 后端正交：前者改 life-death 的物流拓扑，后者要补中继能力。

### 39.7 状态

- 新增 `tools/probe_arm_coverage.py`（`--plain` 出默认基线，默认自动化 arm 路线，
  `--keep-life-death` 让两者竞争）；
- 源码零改动：HEAD 仍是 `48b769b` 的代码状态，默认路径 `1dc60983 = 43948`；
- 全语料测量数据在 `result/arm-coverage-20261113/{default,nold,start40}.json`。

## 40. 与 oracle 的逐对象对比：差在"很多次短交接"而不是"一次长搬运"（2026-11-13）

§39 定位到周期 ≈ 1.1–2.2 × 最忙活塞的指令数。这轮问的是更进一步的问题：oracle 的
机器里有没有同样忙的活塞？把 oracle 的 `.solution` 也解码一遍。

| 题 | 我们：抓取 / 最长程序 / 臂数 | oracle：抓取 / 最长程序 / 臂数 |
|---|---|---|
| `1dc60983` | 72 / **28233** / 1 台电梯 | 57 / **54** / 15 arm1 |
| `5911f6b1` | 168 / **62448** / 1 台电梯 | 75 / **42** / 27 arm1 |
| `deecf24d` | 108 / 16398 / 1 台电梯 | 73 / **34** / 29 arm1 |
| `9a6d6d90` | 24 / 26887 / 1 台电梯 | 60 / **34** / 27 arm1 |

**抓取次数在同一量级，最长程序差 400–1500 倍。** oracle 的每次交接只花约 13 步
（相邻臂短传），我们花约 390 步（长走廊往返）。所以差距不是"要不要搬运"，而是
"搬运的粒度"：**oracle 是 15–29 条相邻臂接力，我们是一台电梯搬 72–168 次。**

这也解释了为什么 arm 后端（§39.4，0 胜 115 平）才是正确方向：它本来就该产出"多臂短传"。

### 40.1 试过并否掉的两条

**给 arm 后端补中继臂。** 路由 BFS 其实已经支持接力（每一步绕一个已有臂旋转 60°），
只是只能用**已经存在**的臂，所以长距离必然 `route blocked`。实现"搜索失败时沿最短
六角线补臂再搜一次"，全语料 117/146 → 117/146，0 增 0 减 0 快 0 慢，**零差异**，
撤销。原因：`1dc60983` 的目标格 `(18,0)` 被产物原子围住，补臂也进不去——这是放置
顺序问题（§37/§38），不是路由能力问题。

**工厂锚点再左移。** `-60 → -40`：13 快 / 122 同 / 2 慢 / 2 题丢解，周期和 1.007×；
`-60 → -20`：5 题里 3 题无解。锚点这一层已经榨干。

### 40.2 多电梯：设计完成，K≥2 仍碰撞

把"一台电梯搬 72 个原子"拆成 K 台，是 §39 之后唯一还有数量级空间的方向。设计：

- 按**交付列**（`feed.cell.q`）把进料分成 K 个连续组，组 k 的落点行 `6 + 2k`；
- 组 k 的顶部通道行 `88 − 2k`，立柱终点 `90 − 2k`（比通道高 2 行，保持 2 格抓取距离）；
- 下降列放在**本组工厂列区间与下一组之间**的空隙里（工厂柱间距 20，空隙足够），
  这样任何一条通道都不会越过另一组的立柱；
- 装配侧 `E` 步数本来就是 `dropRow − feed.cell.r − 1`，天然支持每组建模。

已实现：构造改成两阶段（**先算完全部几何再创建对象**，否则避让扫描会看到上一组自己
刚建的下降轨，把本组下降列拽过去——实测 `bend` 被拖到 −131 正好压住组 0 的立柱）。

结果：

| K | 1dc60983 |
|---|---|
| 1 | 43948（不变） |
| 2 | `collision during motion phase` |
| 3/4/6 | 同上 |

判别实验（都仍然碰撞）：把落点行统一回 6；把立柱终点恢复成 90。所以既不是落点行、
也不是立柱终点。几何上复核过通道/下降/底部三段都不共格，剩下未排除的是装配侧接收臂
与双通道的相对位置。

K=1 的默认路径实测几乎全等，只有 3 题各快 2 个周期（`f613a067` 33985→33983、
`89d6e08e` 53850→53848、`550b9c1c` 33132→33130），覆盖 139/146 不变。差异来自通道起点
取本组 `pickup` 而非全局 `pickup`，即通道在共享生产者的情形下短了 2 格。

### 40.3 状态

- `src/Solvers/Chemistry/LifeDeath.cpp`：电梯构造改为按组生成（默认 1 组，行为等价），
  新增 `OPUSOPTIMA_LD_ELEVATORS`（默认 1），K≥2 当前会碰撞，作为下一轮的脚手架；
- 全语料默认路径 139/146、3 题各快 2 周期；
- 下一步：定位 K=2 的碰撞位置。需要门的碰撞坐标（现在只回 `collision during motion
  phase`，没有 `errorLocation`），否则只能盲试。这是当前最缺的一个观测点。

## 41. 真正的指标是"每次交接的位移"，多电梯是纯回归（2026-11-13）

### 41.1 校正一个误读：对象本来就是并行的

一度以为磁带按发射顺序分配周期、机器是全串行的（因为 `cycles/instr_total` 只有
0.61–0.96）。用 `9a6d6d90` 反证：总指令 96480、最长活塞 26887、周期 41927，而
`1.56 × 26887 = 41944` 与周期吻合 —— 说明对象是并行的，§39 的法则
（周期 ≈ 1.1–2.2 × 最忙对象）成立。总指令数只是碰巧接近。

### 41.2 换成正确的指标：每次交接的位移

| | 交接次数 | 最长程序 | 每次交接均摊 |
|---|---|---|---|
| 我们 `1dc60983` | 72 | 28233 | **~381 步** |
| oracle `1dc60983` | 57 | 54 | **~14 步** |

oracle 的 15 条臂 × 54 步 = 810 条指令做 57 次交接；我们 47522 条指令做 72 次。
**差距是 27× 的单次位移**，不是并行度。而单次位移 = 2×横向 + 2×垂直，由
"工厂离装配区 135 列 + 82 行"这个布局事实决定 —— 所以正确答案是**紧凑布局**
（把化学放在产物装配旁边），也就是 arm 后端的方向。

### 41.3 多电梯全语料实测：纯回归

上一轮留下的 `OPUSOPTIMA_LD_ELEVATORS` 脚手架跑全语料：

| | 覆盖 | 更快 | 更慢 | 丢解 |
|---|---|---|---|---|
| K=1（默认） | 139/146 | — | — | — |
| K=2 | **128/146** | **0** | 1 | **11** |

不只是"某些题失败"，而是**一道题都没变快、还丢了 11 道**。作为补丁不成立。

为了让这条路可调试，给 `main.cpp` 的拒绝日志补上了碰撞坐标（原来只有 reasonCode）：

```
Candidate not admitted: collision during motion phase (cycle=1143 at 188,3)
```

拿到坐标后可以定性：K=2 的失败点集中在 `q=120…478, r≈1–3`，也就是**产物装配站**
（`1dc60983` 的第二长活塞 3727 在 (188,3)，旁边就是 life-and-death 字形），
不在通道几何上。判别实验排除了落点行与立柱终点。结论：装配侧的时序假设了
"只有一台电梯"，与通道无关。

### 41.4 另一个否掉的杠杆

`OPUSOPTIMA_LD_FACTORY_FLOOR` 取 90/80/70/60，四道题（`1dc60983`、`89d6e08e`、
`550b9c1c`、`f613a067`）的周期**完全一致**，说明这条路径的通道行被钉在 r=88，
工厂层高不参与走廊长度。该旋钮对这批题无效。

### 41.5 下一步（按收益排序）

1. **工厂放到自己交付列的正上方**，让横向位移归零、垂直位移由并行的立柱承担。
   这是唯一能触及 27× 的改动；难点是工厂宽 21、装配区只有 10 列宽，水平铺不开，
   垂直堆叠又需要立柱穿过上层工厂——需要给每台工厂留专用垂直通道。
2. arm 后端的产物放置顺序（§37/§38）：`target` 的原子→格映射由产物在
   Output 前置状态里的相对位置决定，无法置换；所以只能**推迟会封死内部格的邻居
   落位**，并给内部格原子优先权。这需要一个延迟放置的工作队列，而不是单趟遍历。

### 41.6 状态

- `main.cpp`：拒绝日志带 `cycle` 与 `(q,r)`；
- `LifeDeath.cpp`：电梯按组构造（默认 1 组），K≥2 有旋钮但实测纯回归；
- 套件 1060 ran / **1044 passed**，与基线一致；全语料默认路径 139/146。

## 42. 纠正：周期由"总移动量"决定，重新分配距离无效（2026-11-13）

§39/§40 把周期归因于"最忙那一个活塞"，并据此提出多电梯。这一轮的对照实验推翻了
那个因果判断。

### 42.1 决定性对照：把垂直腿从电梯移给立柱，周期分毫不变

新增 `OPUSOPTIMA_LD_LANE_ROW`：把共享电梯的横向通道行从 r=88 降到 r=20，让每台
工厂的立柱（本来就是并行、每进料一条）把原子送到低处，电梯只做横向摆渡。
`1dc60983` 的结果：

| | 走廊长度 | 最长活塞 | 总指令 | 最后周期 | 通过周期 |
|---|---|---|---|---|---|
| lane=88 | 247 | 28233 @ (21,88) | 47522 | 43964 | **43948** |
| lane=20 | 179 | 18441 @ (21,20) | 47522 | 43964 | **43948** |

最长活塞降了 35%，周期一位数都没变。原因：立柱从 1489/1807 涨到 4753/4753，
**垂直距离只是从电梯转移到了立柱，总量守恒**。

### 42.2 正确的模型

周期 ∝ **总移动量** = Σ_原子 (2 × |取件列 − 落点列|) + Σ_原子 (2 × 垂直距离)，
再乘一个 0.4–1.0 的并行系数（`1dc60983` 是 0.93，`9a6d6d90` 是 0.43）。
`cycles ≈ 1.5 × 最长活塞` 只是相关性，不是因果——因为这两条量都由同一个布局事实
（工厂离装配区多远）决定。

由此，之前所有"把工作分给更多对象"的想法都不可能提高周期：

- K 台电梯（§41.3）——实测 128/146，0 快 11 丢；
- 降低通道行把垂直腿转给立柱——总量守恒，周期不变；
- 加中继臂（§40.1）——零差异。

**只有减少总距离或减少搬运次数才有用。**

### 42.3 那么为什么不能靠"三角形不等式"优化

单个原子的横向成本下界是 |取件列 − 落点列|（下降列放在哪一端都一样，因为
|a−b| + |b−c| ≥ |a−c|）。而落点列由装配区决定、取件列由工厂决定，所以：

- 横向不可压缩：工厂柱宽 21 × 6 台 ≈ 126 列，装配区只有 10 列宽，且全在工厂块的
  一侧，平均单程 ≈ 135；
- 垂直不可压缩：工厂在 r ≥ 110，装配在 r ≤ 6；
- 次数不可压缩：72 次交接对应 10 原子产物，差额是钙化/生之炼成的盐与共产物。

### 42.4 剩下的真实选项

1. **让工厂环绕装配区**（左右各一半），把平均 |取件列−落点列| 从 135 降到约 34。
   理论 1.87×。障碍：左右两组需要两条底部通道（行 6 与 8），而 K=2 的判别实验
   显示装配侧时序假定只有一台电梯，会 `collision during motion phase`。
2. **减少搬运次数**（72 → 接近 10）：需要化学规划侧少产生中间体，属于 planner 工作。
3. **换装配拓扑**（arm 后端的紧凑机器）：oracle 每次交接 14 步，我们 381 步。

### 42.5 状态

- 本轮所有几何改动均已撤销，`LifeDeath.cpp` 与 HEAD 一致；
- 唯一保留的改进是拒绝日志带碰撞坐标（`cycle=N at q,r`），这是后续调试的前提；
- 默认路径 `1dc60983 = 43948`，全语料 139/146，套件 1060 ran / 1044 passed。

## 43. 为几何改动补上观测：被拒候选落盘 + 碰撞定位（2026-11-13）

这一轮的四次几何尝试（多电梯、装配区上移、降低通道行、双侧工厂）全部死在同一个
东西上：门只回 `collision during motion phase (cycle=N at q,r)`，看不到**撞的是谁**。
每条改动因此都只能盲试，一个假设一轮。

补了两个观测点：

1. `OPUSOPTIMA_DUMP_REJECTED=<path>`：`main.cpp` 在提交前把候选写成
   `<path>.rejected.solution`。门拒掉的那个机器原本完全不可见。
2. `tools/localise_collision.py <solution> --cycle N --cell q,r`：重放候选的磁带，
   算出每个对象在给定周期的**基座**位置（`piston` 沿轨道一格一条 `A`/`a`，固定臂
   不动），报告落在碰撞格 `--radius` 以内的对象，以及轨道穿过该格的对象。

验证：用已知的 K=2 碰撞 `cycle=1143 at 188,3`：

```
index= 14活塞              base_now=(188,3) spawn=(188,3) distance=0
index= 15轨道              base_now=(188,3) spawn=(188,3) distance=0
```

局限要说清楚：只跟踪**基座**，不跟踪被手臂伸出去扫过的格、也不跟踪原子。所以
"只有活塞自己在那一格"意味着碰撞的另一方是**原子**（被别的臂送进来的），这已经
足以区分"布局撞了"和"时序撞了"——正是之前所有猜测失败的地方。

### 43.1 本轮否掉的四条（都已在干净代码上撤销）

| 杠杆 | 结果 |
|---|---|
| 双侧工厂（左半 + 右半，两侧各一条走廊） | 两条车道列不相交、都落在同一行 6（装配时序不变），但碰撞在 `q≈213..217, r=88`，随远端工厂数每次右移 2 列，定位未完成 |
| 装配区整体上移（刚性平移） | 连 lift=1 都在 (200,1) 碰撞——有对象没跟着平移，未定位 |
| 通道行单独降低（把垂直腿转给立柱） | 总指令 47522、最后周期 43964 **完全不变**（§42 的证据） |
| 通道行 + 工厂层同步降低 | 43948 → 41500（1.06×）后饱和，另有下界 |

### 43.2 状态

- `main.cpp`：拒绝日志带坐标，`OPUSOPTIMA_DUMP_REJECTED` 落盘候选；
- `tools/localise_collision.py`：新增；
- 求解逻辑零改动：默认路径 `1dc60983 = 43948`，全语料 139/146；
- 下一步：用落盘 + 定位重跑双侧工厂，把 `q≈213, r=88` 的碰撞方找出来。这是唯一
  还有 ~1.9× 空间的改动，且几何上已确认两条车道与两条底部通道都不共格。

## 44. 布局为什么已经无处可挪：装配区本身就是高的（2026-11-13）

上一轮把"目标"定成让工厂环绕装配区，把平均横向距离从 135 降到 34。实现后发现右侧
放不下工厂。用新的观测点量出来原因：

`1dc60983` 被采纳的机器里，**装配区（调用方传入的对象，索引 0..115）**：

| | q 范围 | 行范围 |
|---|---|---|
| 对象基座 | 21 … 210 | −3 … **125** |
| 轨道格 | — | 最高 **5** |

也就是说装配区是一个**高 128 行、宽 190 列**的结构：手臂基座排到 r=125，靠长轨道
一路垂到 r=5。它右侧（q > 21）和上方（r ≤ 125）都被自己占满了。

我们加的部分只有：

- 工厂块（左侧，q ≈ −179…−44，r ≥ 110）；
- 走廊（r = 88）；
- 每进料一条立柱（r 109 → 90）。

由此可以解释本轮以及前几轮**所有**失败的几何改动：

| 改动 | 为什么必然失败 |
|---|---|
| 右侧放工厂（双侧） | 右侧 q>21 是装配区自己的长臂，`q≈215, r=88` 碰撞 |
| 降低工厂层 | r 110–125 这一段工厂和装配区的上臂共用，靠太近就撞 |
| 装配区整体上移 | 上移后与工厂块重叠，连 lift=1 都撞 |
| 降低走廊行 | 能把垂直腿转给立柱，但总量守恒，周期不变（§42） |
| 多电梯 | 通道之间能分开，但装配侧时序假定一台电梯（§41.3） |

**结论：在当前拓扑下，总移动距离基本是被布局逼死的。** 工厂必须在左、走廊必须在
中、装配区占右，三者之间没有空档。要让每次交接从 381 步降到 oracle 的 14 步，只能
换装配拓扑本身（arm 后端的紧凑机器），而不是在这个拓扑里腾地方。

### 44.1 本轮的观测能力（这是能继续推进的前提）

三个开关配合，可以把"门拒绝了，坐标 (q,r)，周期 N"变成"是谁"：

```
OPUSOPTIMA_DUMP_REJECTED=/tmp/x      # 落盘被拒候选
OPUSOPTIMA_LD_OBJECT_DIAG=1          # 打印每个对象的创建位置与索引
tools/localise_collision.py /tmp/x.rejected.solution --cycle N --cell q,r
```

用它们这轮确认了：双侧工厂的碰撞对象是一个**出生就在 (215,88)** 的对象，而对象创建
日志显示我们自己只创建了 `(−99,88)`、`(95,88)` 两个走廊和若干 `(x,109)` 立柱——
所以 `(215,88)` 属于装配区，右侧确实放不下工厂。

### 44.2 状态

- 求解逻辑零改动：默认路径 `1dc60983 = 43948`，全语料 139/146；
- 新增 `OPUSOPTIMA_LD_OBJECT_DIAG`（对象创建归属）；
- 下一步只剩拓扑级改动：arm 后端的紧凑机器，或在 life-death 里改成"工厂与装配区
  交错"的新排布（需要先给立柱留专用通道，这是目前没解决的问题）。

## 45. 降低走廊行 + 工厂层：16 题提速，中位 1.21×（2026-11-13）

§42 的模型说：周期 ∝ 总移动量，重新分配距离无效、减少总距离才有效。这轮把它变成了
可发布的收益。

### 45.1 关键点：两项必须一起降

只降走廊行（`OPUSOPTIMA_LD_LANE_ROW`）时，垂直腿从电梯转给并行的立柱，总量守恒，
周期一位数不变（§42.1 的对照）。**同时把工厂层一起降**才是真的减少了距离：

| laneRow | 工厂层下限 | 1dc60983 周期 | 总指令 |
|---|---|---|---|
| 88（原值） | 110 | 43948 | 47454 |
| 70 | 92 | 41356 | 44862 |
| 60 | 82 | 39916 | 43422 |
| 40 | 62 | 37036 | 40542 |
| 24 | 46 | 34732 | 38238 |
| 10 | 32 | **32716** | ≤38238 |
| 7 | 29 | 32284 | — |
| 6 | 28 | 碰撞（走廊必须高于落点行 6） | — |

第一版只把 `feed.height` 的 `max` 项换掉，测到 41500 就饱和了——因为初值 `: 100`
仍在 `max` 里压着。**把初值也改成 `laneRow + 22`** 之后才线性下降。这个细节卡了
一轮，值得记下来。

### 45.2 全语料结果（默认已改为 laneRow=10）

| | 结果 |
|---|---|
| 覆盖 | 139/146（不变） |
| 更快 | **16 题**，中位 1.21×，最大 1.37× |
| 相同 | 123 题 |
| 更慢 | 0 题（`329bfd90` 的 13113→13238 已复验为 8 路并行下的噪声：同条件下 old/new 都是 13238） |
| 丢失 | 0 |
| 周期总和 | 1,243,674 → 1,157,689（**1.074×**） |

单项最大：`fc6a70eb` 1.37×、`1dc60983` 1.34×（43948→32716）、`7b837aa2` 1.30×、
`89d6e08e` 1.29×、`ac8d3e1b` 1.29×、`f613a067` 1.28×。

### 45.3 连带修正：磁带守卫 2/3 → 1/2

life-death 的磁带守则是"长过门槛就推后，让别的家族先试"。走廊变小后
`17a58334` 的 life-death 磁带从门槛以上降到 56253，于是被放行，周期 16486 → **50217**。
把守则从 `2/3 × 100000` 收紧到 `1/2 × 100000` 后它又被推后，回到 28569（该题另有一个
更好的家族）。推后是安全的：没有更好的家族时 `life-death-deferred` 仍会发布。
守卫可用 `OPUSOPTIMA_LD_TAPE_GUARD=<n>|off` 覆盖。

### 45.4 阈值

`laneRow = 8` 会让 `9a6d6d90` 直接无解（`laneRow = 10` 正常，`16/24/...` 也都正常），
所以默认取 10。该题在 laneRow=10 时是 41929 → 40993，也是受益的。

### 45.5 状态

- `LifeDeath.cpp`：`laneRow` 默认 10，`feed.height` 初值与下限都跟随它；
- `AssemblySolver.cpp`：磁带守则默认 `kGateTapeLimit / 2`；
- 全语料 139/146、周期和 1.074×；两侧开关仍保留供后续实验；
- 这条改动同时验证了 §42 的模型：**减少总距离有效，重新分配无效**。

### 45.6 发布后的差距分布

| 指标 | 发布前 | 发布后 |
|---|---|---|
| 与 oracle 的比值 p50 | 9.60× | 9.60× |
| p90 | 38.2× | **33.3×** |
| 最差 | 76.6× | **71.2×** |

中位不变是预期的：语料是双峰的，51 题本来就在 2× 以内、123 题不受这个改动影响，
受益的是 life-death 走廊那一簇。`1dc60983` 从 56.5× 降到 **42.1×**。

`samples`：`universal` 类抽 8 题看家族，5 题走 life-death、2 题 ordinary-assembly、
1 题 repeating-output —— 说明 life-death 依然是主战场，走廊工作已经覆盖到它。

### 45.7 这条路的余量

单个原子的横向成本已经到理论下界：取件列全在落点列左侧，`|p−bend| + |bend−drop|`
在 bend 落在两者之间时恒等于 `|pickup−drop|`，而现在 bend = 落点左缘 − 20 正落在
区间内（验算 `1dc60983`：`|−179−(−44)| + |−20−(−44)| = 135 + 24 = 159 = |−179−(−20)|`）。
所以再挪下降列不会省任何东西，剩下的只有**减少交接次数**（72 次对应 10 原子产物）
或换拓扑。

再次尝试的锚点：`OPUSOPTIMA_LD_FACTORY_START=-50` 在 lane=10 下 16 题快 2.7%（中位），
但净周期和只有 1.000×，且 `17a58334` 又因为磁带守卫被放行而恶化（16486→26899），
所以**不发布**；`-40` 会让 `a7d3c0bf` 无解。

---

## 46. 把纵向余量吃干净，并把"交接次数"这条线索关掉

§45.7 说纵向已经和 `laneRow` 一起降下来了。这一节说明**还剩一个没被参数化的纵向项**，
以及两个被量到关闭的假设。

### 46.1 `feed.height` 里的 +22 是遗留的抬头

`laneRow` 降了之后，`feed.height = laneRow + 22` 里的常数 22 还是从 row-88 时代
继承下来的：它原本是"工厂块到走廊之间留够臂和手持分子的空间"，但真正需要的只有
每条 feed 的 carrier 轨道长度（代码从 `height-2` 建到 `laneRow`）。

`OPUSOPTIMA_LD_FEED_LIFT` 扫描 `1dc60983`（出厂 lane=10）：

| lift | cycles |
|---|---|
| 22 | 32716 |
| 18 | 32140 |
| 14 | 31564 |
| 10 | 30988 |
| 6 | 30412 |
| 3 | **29980** |
| 2 | strategies exhausted |

每 2 行正好 288 周期 = `2 × 72`，也就是**每个交付原子一次上升一次下降**，和 §42
的模型逐字吻合。3 是硬下界：lift=2 时 carrier 轨道只剩一格，puzzle 直接无解；
`std::max(...) + extent - delivery.r` 那一段仍会在批次更深时把抬头顶回去，所以高
工厂不会失去净空。

语料 A/B（146 题，life-death 路线不变）：**16 快 / 0 慢 / 123 相同 / 0 丢**，
覆盖 139/146 不变，周期和 1,157,560 → 1,136,584。已作为出厂值提交（`481f6e6`）。

### 46.2 "6 倍重复"是真的需求，不是浪费

`productCount = 6 * outputMultiplier` 一度看起来像 6 倍冗余：产物只有 10 个原子，
走廊却交付 72 次。加 `OPUSOPTIMA_LD_REPS` 直接量：

| reps | 结果 |
|---|---|
| 6 | 29980 |
| 4 / 3 / 2 / 1 | strategies exhausted |

**验证器要求 6 份产物**，少一份就直接不通过。所以 72 次 = 6 份 × 12 原子
（10 个产物原子 + 2 个必须一起过走廊的共产物），交付次数**不可压缩**。
`OPUSOPTIMA_LD_EVENT_DIAG` 也确认了装配侧是 `events=10`、`factories=4`、
`productCount=6`，即每份产物恰好 10 个事件。

### 46.3 为什么走廊再优化也追不上 oracle

`1dc60983` 的 oracle 是 `pipeline/cheap-arms` 类：**778 周期、782 条指令、
15 个 arm1、4 个 bonder**。782 条指令 ≈ 778 周期，说明它是**全并行**的，瓶颈就是
总工作量本身。

我们现在 29980 周期 / 36290 条指令，单一最忙对象（走廊臂 `(21,10)`）就有 17001 条，
占 46.8%。拆解成：

- 横向往返 `2 × Σ|pickup − bend|` ≈ 11448（72 次交付，平均 79.5 格）
- 纵向 `2 × 72 × lift` = 432（lift 已经压到 3）
- 装配侧搬运 ≈ 1593

即使把横向压到 0（工厂全部叠在同一列，物理上不可能），最忙对象也只会落到
≈5600 条，对应周期 ≈ 9900 —— **离 778 还有 12×**。

结论：`life-death` 是**串行走廊**，oracle 是**并行流水线**，两者不是一个量级。
差距不是几何参数能补的，必须换后端：写一个真正的臂式流水线执行器
（`ChemistryPipeline::Plan` 已经给出 operation 序列，但 `src/Solvers/` 下没有
对应的执行器，`CompileIntactFeed` 只做"同构分子搬运"，不做化学变换）。

现有的 `shared-plan-serial-arms` 路线就是这个方向，但它卡在布局而不是算法上：
`serial atom 14 route blocked from (12,5) to (18,0)`。`SerialArmExecution` 的
`moveScalar` 要求每一格都有相邻的臂基座（一次跳一格），而目标格 (18,0) 距离 6，
中间格被已放置的产物原子占满，BFS 探索 1503 个状态后穷尽。要修的是**产物放置
顺序与臂基座布局**，不是距离常数。

---

## 47. 临界路径拆成"移动 + 等待"，以及两条被测量否掉的捷径

§46 把纵向余量吃完之后，`1dc60983` 的最忙对象（走廊臂 `(21,10)`）指令构成是：

| 动作 | 数量 | 含义 |
|---|---|---|
| `A` | 8184 | 沿轨道走出去 |
| `a` | 8048 | 走回来 |
| `R`/`r` | 216 / 216 | 转向 |
| `E`/`e` | 96 / 96 | 伸缩（远端取件） |
| `G`/`g` | 72 / 72 | 抓 / 放 |

合计 17001 条，但磁带时间跨度是 **29784**（周期 29980），也就是 **12783 周期
（43%）是等待**。按动作归类等待：

| 等待出现在 | 次数 | 总计 | 平均 |
|---|---|---|---|
| 回程移动 `a` 之前 | 71 | 12423 | 175 |
| 出程移动 `A` 之前 | 72 | 216 | 3 |
| 唯一一次 `B` 之前 | 1 | 144 | 144 |

结论：走廊臂**抱着分子等装配侧接收**。所以临界路径 = 走廊移动 17001 + 装配接收
延迟 12423。继续压走廊只能吃掉前一项，后一项是装配构建器的吞吐。

### 47.1 工厂锚点：单题线性收益，语料上被守卫边界吃掉

`kShippedFactoryStart = -60` 是个偏保守的值。单题扫描（`1dc60983`）：

| 锚点 | cycles |
|---|---|
| −60（出厂） | 29980 |
| −50 | 28550 |
| −45 | 27835 |
| −37 | 26691 |
| −25 | **24975** |
| −20 | strategies exhausted |

语料 A/B（146 题，无并发干扰）：

| 锚点 | 快 | 慢 | 丢 | 周期和 |
|---|---|---|---|---|
| −45 | 15 | 2 | 0 | 1,147,428（更差） |
| −25 | 13 | 3 | 2 | 1,064,127（更优） |

−25 的净和看着好 1.068×，但覆盖从 139 掉到 137；−45 覆盖不掉，却被
`17a58334` 一道题吃掉全部收益：**16486 → 43318**。

原因是磁带守卫：那道题在 −60 下 life-death 磁带超长被推迟，别的族以 16486 胜出；
锚点前移让磁带缩到守卫之下、于是被发布，而它自己的周期是 43318。守卫的门槛是按
−60 的几何标定的，一改锚点就失效。

也试过"把 −40/−25 作为额外候选、按周期取最小"（`FactoryStartScope` + `periodCount`），
实现完成后 `1dc60983` 直接报 `bounded candidate collection stopped`：多编两次
`CompileLifeDeath` 就把 20 秒预算吃光。**多候选路线在这里不可行**，要么改默认值
（不安全），要么让守卫按几何自适应（尚未做）。

注意：`instructionCount` 不能当作周期的代理。第一版按指令数取最小，结果选中了
"短磁带 + 长等待"的机器，`89d6e08e` 直接无解 —— 周期是**最大指令参数**，不是
指令条数。

### 47.2 并行工厂时钟：方向正确，但装配磁带必须一起重排

代码里已经有 `perFeedClocks`（`OPUSOPTIMA_LD_FEED_CLOCKS`），注释写得很清楚：
旧的实现把所有对象的磁带追加到一个全局游标，于是周期是**求和**；独立时钟把它变成
**取最大**，"这才是参考机器的做法"。

但它一直关着，因为 6/6 无解。现在重测：

```
OPUSOPTIMA_LD_FEED_CLOCKS=1  ->  Candidate not admitted:
    collision during motion phase (cycle=237 at 41,11)
```

用 `tools/localise_collision.py` 回放被拒候选：`index=122 piston base_now=(41,12)`。
(41,12) 是**装配侧**的臂，不是走廊对象。

原因：`emit` 改了交付时刻，但装配对象的磁带参数是 `replay` 机制按旧的全局时钟算
出来的、和 feed 时钟无关。交付提前/错开之后，装配臂的 `(41,12)→(41,11)` 正好撞上
已经落位的原子。

所以要打开并行时钟，必须**让装配磁带跟着新到达时刻重新生成**，这是一次真正的
后端改造（装配构建器要接受"外部到达时刻表"作为输入），不是加个开关。
这和 §46.3 的结论一致：收益必须从装配侧拿，而装配侧目前写死了串行假设。

---

## 48. 工厂时钟独立：把 43% 的空等拿掉（1.117×）

§47 量到走廊臂有 12783 周期（43%）在等，但只推断出"等装配侧接收"。这一节把
它拆到底，并把它拿掉。

### 48.1 空的不是装配，是工厂

`emit` 里只有一条游标：

```cpp
int& shift = perFeedClocks ? feedShift[feedIndex] : globalShift;   // 旧路径恒为 globalShift
const auto slice = producer.resource->Reserve(shift);
... 把工厂这一片的指令按 shift 平移后压进机器 ...
shift += end - slice.begin + 1;      // 关键：整段生产时间被加进走廊的游标
```

也就是说**每个分子都把自己那份工厂生产时间拖在走廊的时间线上**。60 个交付 = 60 段
生产时间，合计 12783 周期。按动作归类，等待全部落在回程移动 `a` 之前（71 次，平均
175），因为那正好是下一个分子还没被造出来的时刻。

### 48.2 单出口契约只禁止"太早"，不禁止"提前造好"

`FiniteFeedResource` 的契约很短：

```cpp
Slice Reserve(int start) {
    if (pending_ || start < 0 || (last_ && start < last_->cleared) || ...)
        throw std::invalid_argument("finite feed exit is occupied");
    ...
}
void Release(ModulePort consumed, int pickup, int cleared) { ... last_ = handoff; }
```

它只要求**下一次生产不得早于接收方清空上一条出口的周期**，也就是 `Release` 里传进去
的 `cleared`。它并不要求工厂等到自己的轮转事件才开始——所以旧路径的空等是调度写死
的，不是契约强制的。

于是给每个 producer 一条自己的生产游标：

```cpp
factory = std::max(factory, shift);   // Release 之后：不得早于出口清空
// ...
factory += end - slice.begin + 1;
shift = std::max(shift, factory);     // 走廊只等"自己这一批造完了"
```

工厂因此在走廊搬运别人的分子时就把下一批造好，走廊只在批次真的迟到时才等。

### 48.3 结果

`1dc60983`：走廊臂空闲 **12783 → 6111**，周期 **29980 → 23308（1.286×）**。

语料 A/B（146 题，0 丢失，覆盖 139/146 不变），周期和 1,157,560 → **1,036,175
（1.117×）**，16 题变快：

| 题 | 前 | 后 | 倍数 |
|---|---|---|---|
| `550b9c1c` | 24982 | 12569 | **1.99×** |
| `f613a067` | 24673 | 13856 | 1.78× |
| `cb6c9528` | 15413 | 9639 | 1.60× |
| `7b837aa2` | 37846 | 24455 | 1.55× |
| `ac8d3e1b` | 24306 | 17649 | 1.38× |
| `803e8d4a` | 46478 | 34760 | 1.34× |
| `a7d3c0bf` | 27022 | 20119 | 1.34× |
| `deecf24d` | 25753 | 19350 | 1.33× |
| `89d6e08e` | 38716 | 29873 | 1.30× |
| `1dc60983` | 29980 | 23308 | 1.29× |

`OPUSOPTIMA_LD_SPLIT_FACTORY=off` 精确还原旧行为（29980），便于复现 A/B。
`160f11ef` 在并发 A/B 里"变慢 8%"，串行复测两种配置都是 31461 —— 是并发噪声，
不是回归。

### 48.4 只能拆工厂时钟，不能连走廊的对象游标一起拆

代码里本来就有 `perFeedClocks`（`OPUSOPTIMA_LD_FEED_CLOCKS`），它拆的是**走廊自己的
对象游标**。两者都打开时仍然失败：

```
collision during motion phase (cycle=237 at 41,11)
```

`tools/localise_collision.py` 与时间窗分析显示：那一周期只有走廊臂（idx 112，(21,10)）
在动，装配臂（idx 122，(41,12)）静止并被扫到。拆开工厂时钟后走廊提前 1.2 万周期结束，
装配行如果在 `globalShift` 上按自己的节拍提前铺开就会撞上；这与 §47 的结论一致。
**只拆工厂时钟，保留走廊的单一写出游标**，时序关系不动，就没有这个冲突。

### 48.5 现在的地板

`1dc60983` 拆解（周期 23308）：

| 项 | 周期 |
|---|---|
| 走廊臂自身指令 | 17001 |
| 装配行节拍（`period` 678 × 6 轮） | ~4068 |
| 走廊剩余空闲 | 6111 − 4068 ≈ 2043 |
| 转向/伸缩/grab 边角 | ~400 |

也就是这条路线的地板约 **21400**，还能再挖 ~8%。再往下需要两件事之一：
把 `CompileRowAssembly` 的 `period` 压小，或者让走廊不再由单个臂独占
（多走廊 / 流水线后端，见 §46.3）。

---

## 49. 下一个战场：机械臂执行器把 166 个臂串成了一条线

§48 之后，仍然远远落后 oracle 的是 `portfolio-static-embedding` / `static-reaction`
那一簇。这一节把它量化清楚，并说明为什么它不能靠"再调一个常数"解决。

### 49.1 实测：平均只有 1.03 个对象在工作

以 `1baade11`（oracle 233 周期）为例，我们现在 15493 周期。解的构成：

| 指标 | 值 |
|---|---|
| 对象数（几乎全是 `arm1`） | 166 |
| 总指令数 | 5816 |
| 有指令的不同周期数 | 5651 |
| **平均同时工作的对象数** | **1.03** |
| 最宽对象的指令跨度 | 8001 周期 / 仅 79 条指令（密度 0.01） |

也就是说：**166 个臂，同一时刻只有一个在动**。周期既不是"总工作量"，也不是
"最慢的臂"，而是两者之外的东西——因为 `metrics.cycles = (tick + 1) * requiredPeriods`
里的 `tick` 是一条**全局游标**。

### 49.2 根因：`tick` 同时承担了两个职责

`SerialArmExecution.cpp` 里 `int tick = 2;`（第 1346 行），全文件 54 处使用。每个
"原子走一格"的交接步长这样：

```cpp
tape.push_back({'r', tick++});   // 转到抓取角
custody.Grab(...);
tape.push_back({'G', tick++});
tape.push_back({action, tick++});
custody.Drop(...);
tape.push_back({'g', tick++});
tick += 2;                       // 松手后该臂恢复
```

`tick` 同时是：

1. **发射时刻**——写进对象磁带的参数；
2. **依赖下界**——`append()` 按 `plan.material.steps` 顺序执行，下一个操作从上一个
   操作留下的 `tick` 继续。

第 2 个职责让**任何**两个操作都不能重叠，哪怕它们用完全不相交的臂。`tick += 2`
这种"某一只臂的恢复时间"也被记成了全局时间，于是别人陪着一起等。这正是 `1baade11`
里 15493 − 5816 ≈ 9700 个空周期的主要来源。

### 49.3 为什么不能照搬 §48 的修法

§48 的工厂时钟能拆，是因为 `FiniteFeedResource::Release` 明确给出了唯一的依赖值
（`cleared`）。这里没有等价物：要拆 `tick`，必须知道**每个原子什么时候才真正存在**。

最小可用的设计是加一张依赖表：

```
ready[atom] = 该原子可被抓取的最早周期
moveScalar(id, dest):
    t = max(armClock[arm], ready[id]);  逐格交接，只有原子链是串行的
    ready[id] = 交接完成时刻
    用到的臂: armClock[arm] = 交接完成时刻 + 2
```

问题在于 `ready[]` 必须在**每个**改变原子位置的环节维护：`IR::Initial` 的输入原子、
glyph 反应的产物、`append()` 里的 Relocate/Bond/Output、以及 carrier/stageFeeder 那几段。
全文件有 54 处 `tick` 使用点、跨越十几种操作类型；少维护一处，臂就会在原子诞生前
把它抓走，gate 直接拒。

所以这是一次**调度器重写**（把"线性游标"换成"依赖图 + 每对象游标"），不是改一个常数。
它值得做——头部空间是 25–66×——但必须作为一个独立战役，用 gate 逐个操作类型地推进，
而不是顺手改。

### 49.4 另一条路：模块库缺口（仓库自己的结论）

`AssemblySolver.cpp` 里已有 `portfolio-pipeline-architecture` 家族（模块库 → tick 调度
→ 部件剪枝），注释记录了它 2026-11-12 的实测：**只覆盖 13 题、9 题被接受、没有任何
objective slot 变化**，因为它能构造的题"period 已经在地板（cycles = 3）"。它保持
opt-in，等一个生成器模块（MetalProjector / MetalPurifier / MorsVitae / Quintessence）
扩大定义域。

也就是说，仓库自身也把"追上 oracle"定位在**扩大模块/生成器覆盖面**上，而不是继续
压已有族的时间参数。§48 的 1.117× 属于后者的残余红利，已经基本吃完。

### 49.5 锚点复测：不是守卫问题，是逐题局部最优

§48 让磁带普遍变短，之前被磁带守卫挡住的锚点理应重新评估，于是复测：

| 锚点 | 快 | 慢 | 丢 | 周期和 |
|---|---|---|---|---|
| −25 | 13 | 2 | 2 | 966,454（1.072×） |
| −45 | 14 | 2 | 0 | 1,034,379（0.998×） |

−25 的净和好得诱人，但覆盖掉到 137；−45 覆盖率不掉，收益却被 `17a58334`
（16486 → 29032）一道题吃光。

继续追查后确认**不是守卫的问题**：在 −45 下把 `OPUSOPTIMA_LD_TAPE_GUARD` 从 10000
扫到 200000，`17a58334` 的输出始终是 28569–29034，说明它的 life-death 磁带只有几千
条、从来没被守卫挡过；而默认锚点 −60 给的是 16486。也就是说**这道题在更远的锚点上
本来就更快**——锚点是逐题局部最优，单一全局值无法通吃。

要让公开的 −25 生效，必须让 portfolio 同时尝试两个锚点并按周期取优。之前实现过一次
（`FactoryStartScope` + `periodCount`），失败原因是**多编两次 `CompileLifeDeath` 把
20 秒预算耗尽**（`1dc60983` 报 `bounded candidate collection stopped`）。要重做就得
先把 life-death 的编译成本降下来，否则这个杠杆拿不到。

### 49.6 一条没能展开但值得记录的线索

`1baade11` 的解里只有 **1 个 `glyph-purification`**，却有 15493 周期。`Supply` 结构里
的 `purify` 不是"glyph 个数"而是**级联深度**（`VisitStaticCascadeFootprint(purify, ...)`
按深度铺一串 glyph），所以同一元素的所有原子都要排队走同一条级联。

如果把它复制成 P 条并行级联，吞吐就是 ×P，而面积只进 area 槽。这与 §49.4 的结论一致：
**真正缺的是"并行复制处理模块"的能力**，不是继续压单条链的时间参数。

### 49.7 决定性测量：差距是"机械工作量"，不是调度，也不是 glyph 个数

§49.2–49.3 推测臂执行器是"单游标串行"，§49.6 推测是"单条净化级联"。两个推测都做了
实测，**都被否掉**。

**(a) 按对象时钟无效。** 把交接后那 2 个周期的恢复时间从全局 `tick` 移到对象自己的
时钟上（`emitOn`/`hold`，语义等价：发射仍不早于 `tick`），实测：

```
f613a067   off 13856   on 13856
fc6a70eb   off  3046   on  3046
e59c2d6b   off 13104   on 13104
```

**一个周期都没省**。说明臂从来不是约束——`objectClock[arm]` 永远不大于 `tick`，时间线
是被别的东西撑开的。改动已回退。

**(b) 也不是"只有一条净化级联"。** glyph 确实是硬编码单格位
（`const Cell purifier{0,0}, calcifier{0,-12}, disposal{8,-6};`），但 `Purify` 之后的
`tick += 3` 只是 3 个周期；真正的量在**路程**上，而复制 glyph 并不会让原子同时处理，
因为 `tick` 依然是单一游标。所以复制 glyph 单独做没有收益。

**(c) 真正的差距在这里。** 解里各对象的并发度其实不是 1：

| 题 | 对象数 | 总指令 | 周期 | 指令/周期 | oracle |
|---|---|---|---|---|---|
| `f613a067` | 37 | 26763 | 13856 | 1.93 | **782** |
| `fc6a70eb` | 30 | 7874 | 3046 | 2.59 | **123** |
| `1baade11` | 166 | 5816 | 15493 | 1.03 | **233** |

也就是说：我们已经能在同一周期里让约 2 个对象同时动作（所以"串行化"不是主因），
但**机械动作的总量比 oracle 大一个数量级以上**（`f613a067` 26763 vs 782，34×）。

结论要改写：追不上 oracle 的原因是**布局不够紧凑、搬运工作量太大**，不是时间参数、
不是调度并发、也不是 glyph 数量。要接近 oracle，必须让规划器产出"每个原子走更少
格子、更少次交接"的机器——这是生成器的质量，`§49.4` 里仓库自己记的"扩大生成器模块
覆盖面"正是同一件事。

下一步的正确切入点是**减少每个产物的交接次数与总位移**（例如把一个原子从源到位的
路径压缩、把相邻原子的搬运合并成整段片段搬运），而不是继续压执行器的时间线。

### 49.8 本轮新增的三个否定结果（都带语料实测，勿重复尝试）

| 尝试 | 语料结果 | 结论 |
|---|---|---|
| `OPUSOPTIMA_SERIAL_LONG_ARMS=1`（reach ≤3） | 2 题变化（1.08× / 1.00×），周期和 1,036,175 → 1,033,854 | 臂基座按 reach 1 布局，长臂几乎用不上 |
| `OPUSOPTIMA_DEFER_LIFE_DEATH=1`（各族竞优） | 2 快 2 慢 **2 丢**（`deecf24d`、`bc46fd4e`），净和无收益 | 预算耗尽导致丢题，不发布 |
| 按对象时钟 `emitOn`/`hold` | 三题**一周期未省** | 臂不是约束 |

另外确认了一条**不能动的**：`f613a067`（13,856 周期）里 `(21,10)` 一条活塞臂占 **9633 条
（70%）**，48 次取件、每次约 96 格往返；整解 A/a 搬运占 71%（19,118/26,763）。走廊长度
≈ Σ 工厂宽度，而工厂宽度 21 格来自 `CompileRows` 的行装配布局（`feedSpacing=3` 传下去），
改它等于改核心布局器。

### 49.9 当前唯一还带正收益、但被预算卡住的杠杆

锚点前移对走廊长的题确实是 1.2–1.3×（`f613a067` 13856→11019、`1dc60983` 23308→18303、
`7b837aa2` 24455→19030）。唯一反例 `17a58334` 有 5 个批次、走廊 144 格，其余赢家是
8–12 个批次 —— **样本只有 1 个反例，不足以支撑"按批次数据选锚点"的规则**，写成常数只会
过拟合。

要安全拿到这块收益，只有两条路：
1. 让 portfolio 用两个锚点编译并按周期取优 —— 需要先降低 `CompileLifeDeath` 的编译成本
   （现在约 5.3 s/次，两次就把 15 s 搜索预算吃光）；
2. 把 `CompileRows` 的工厂宽度压小 —— 走廊长度≈Σ 工厂宽度，收益对所有走廊题线性成立，
   但改动落在核心布局器上。

### 49.10 工厂竖排也被否掉：抬高的 feed 块进入装配区的行带

§49.9 的第 2 条路（压缩工厂块的横向跨度）试过一个具体实现：让相邻 feed 共用列、
按层抬升 `feed.height`（`OPUSOPTIMA_LD_STACK` / `_STACK_STRIDE`）。走廊的横向跨度
本应减半，竖向代价落在每 feed 自己的 carrier 上（不在临界路径）。

实测三种题全部在上装配区碰撞：

```
1dc60983  stack2 collision (cycle=237 at 21,23)   stack3 collision (cycle=263 at 21,49)
f613a067  stack2 collision (cycle=132 at 21,23)   stack3 collision (cycle=797 at 41,23)
550b9c1c  stack2 collision (cycle=274 at 21,23)   stack3 collision (cycle=156 at 42,49)
```

关键判据：把 stride 从 26 扫到 96（40/56/72/96），**碰撞格始终是 (21,23)**，只有碰撞
周期随之后移 —— 说明抬高的 feed 块在竖向上进入了装配区自己的行带，不是两层工厂互相
重叠（那样碰撞格会随 stride 移动）。这与 §44"装配区本身就是高的（r 到 125）"一致：
工厂块上方没有可用的空层。

改动已回退，未留下死代码。

---

## 50. 更近的工厂锚点确实值 1.2–1.3×，卡在"归档怎么选候选"

§49.9 说锚点前移是唯一还带正收益的杠杆，但要靠 portfolio 双编译。这一节把它做出来了，
收益确认，**卡点定位到 `publish()` 的候选选择语义**，而不是几何。

### 50.1 收益是真的

先按"只加一个候选（锚点 −25）、按周期取优"实现，单题实测：

| 题 | 出厂锚点 | 更近锚点 | 倍数 |
|---|---|---|---|
| `1dc60983` | 23308 | **18303** | 1.27× |
| `7b837aa2` | 24455 | **19030** | 1.29× |
| `f613a067` | 13856 | **11019** | 1.26× |
| `550b9c1c` | 12569 | **9806** | 1.28× |
| `ac8d3e1b` | 17649 | **14326** | 1.23× |
| `cb6c9528` | 9639 | **7994** | 1.21× |

语料一遍（jobs=6，与基线同条件）：**9 快 / 0 慢 / 丢 2**（`ac8d3e1b`、`89d6e08e`），
扣掉丢失两题补回原值后周期和约 1,014,324，即 **1.022×**。

### 50.2 两个失败模式，都不是几何问题

**(a) 按周期择优会选中验证不过的机器。** `89d6e08e` 的更近候选周期更短，但它在
(138,11) 碰撞 —— `lifeDeathPeriod` 只是磁带长度，不是合法性。结果它被选成 `best`、
验证被拒，而**已经编译好的出厂候选被丢弃**，整题无解。`17a58334` 同理会选到更差的
更近候选（28569 vs 出厂 16500 量级）。

结论：周期不能当候选排序键；必须**按验证结果**排序。

**(b) 作为 `verifyAt` 附加候选提交时，`publish()` 不取更优的那个。** 改成"先
`keep()` 出厂解，再把更近锚点作为额外候选 `verifyAt`"后：

```
1dc60983  23308（未采纳更优的 18303）
89d6e08e  29875（覆盖保住了）
17a58334  28569（出厂值附近，未变差）
```

覆盖回来了、也没有回归，但**收益全部消失** —— 说明归档没有把更优的已验证候选挑出来，
`publish()` 返回的不是按目标槽取最小值。这条路径要打通，得先修归档的候选比较语义，
而不是继续调几何。

### 50.3 顺带纠正一个测量假象

`17a58334` 在语料并发装载那一遍是 16486，串行复测（干净的 HEAD）是 **28569**。它是
时序敏感题：搜索预算受并发影响时会走不同分支。以后引用它的数值必须注明串行/并发，
否则会把装载噪声当成"回归"或"收益"。

### 50.4 下一步

把"更近锚点候选"接对，需要的是**归档按已验证的目标值取最小**（`PortfolioKeepsFirst`
那组测试正是描述当前"保留第一个"的语义）。改动点在归档而不是布局器，收益已实测
1.02×（且单题 1.2–1.3×），改动面也比动 `CompileRows` 小得多。

### 50.5 链路全部打通，最后一环是"发布契约"，不是几何

把 §50.2(b) 的猜测继续追到底，用 `OPUSOPTIMA_LD_ANCHOR_DIAG` 打点后确认：

```
ANCHORDIAG admitted=1 accepted=1 cap=4 subs=1 slot=1     ← 门控全部满足
ANCHORDIAG entered                                       ← 块执行了
ANCHORDIAG compiled objects=130                          ← 更近候选编译成功
Cycles: 23308                                            ← 但发布值仍是出厂解
```

也就是说候选**编译成功、也递交给了 `verifyAt`**（`OPUSOPTIMA_SLOT_ADMISSION=on` 时
`acceptedCap` 会被槽位改善撑到 4，`submissions` 只有 1，远未到 12 的上限），但 CLI
打印的仍是最初的 23308。

原因在 `keep()` 的注释里写得很直白：

```cpp
// Baseline gets its original first-feasible route. Only then spend at most
// three construction attempts; no late family can erase that first result.
```

`publish()` 返回 `firstAccepted`；只有 `composedSolutions`（组合/多输出路径）能通过
`wins()` 把它比下去，而单输出 life-death 走不到那条分支。所以更近锚点的机器就算
验证通过、周期更短，也**不会**成为发布结果。

这一环不能顺手改：它决定了"求解器最终发布哪个目标槽的解"。语料里我比的是 cycles，
但归档有三个槽（cost / cycles / area），发布的可能是 cost 最优的那台；把发布裁决改成
"周期优先"会改变**所有**题的输出，而我没有 cost 维度的语料度量来验证这个改动的副作用。

**结论**：更近工厂锚点这条路的机械部分已经全部就绪（编译、验证、槽位准入、预算门控
都跑通了，单题 1.2–1.3×、语料约 1.02×），唯一的阻塞是"首个可行解胜出"的发布契约。
要推进它，需要先决定并度量**求解器发布的到底是哪个目标**——这属于产品语义，不是布局
或调度问题。所有实验代码已回退，工作树干净，出厂行为不变。

### 50.6 关键实测：更近锚点的机器是三目标全优，不是权衡

之前担心"更快但更大"的取舍。实测 `1dc60983` 三个槽全部：更近锚点的机器**三个目标同时
更好**：

| | cost | cycles | area | instructions |
|---|---|---|---|---|
| 出厂锚点 −60 | 6610 | 23306 | 1545 | 33486 |
| 更近锚点 −25 | **6435** | **18301** | **1476** | **28481** |

所以采纳它没有任何权衡，可以用"Pareto 支配"作为绝对安全的采纳条件：只要三个目标都
不更差、且至少一个好，就不可能让任何目标变坏。

### 50.7 但预算和发布路径把这条路卡死在最后一步

按 Pareto 支配实现采纳（在 `keep()` 之后比较并替换 `firstAccepted`）——**块根本没执行**：

```
ANCHORDIAG2 admitted=1 verify=1 fa=1 cost=7010 rem=3414   ← 只剩 3.4 秒
```

`keep()` 内部的构造尝试（static-reaction / static-embedding / rigid-carrier …）已经
把 15 秒搜索预算花掉大半，等走到这里只剩 3.4 秒，而"再编译一次 life-death + 验证一次"
需要约 4 秒。把门控放宽到 4 秒仍然不够。

改到 `keep()` **之前**采纳（那时还剩约 6 秒）——日志确认采纳成功
（`event=closer-anchor-adopted cycles=18303`），但 CLI 打印的仍是 23308。原因是
`attempt()` 内部的 `verifyAt` 已经把出厂机器交给了 `sink`（归档），而"发布的解"取自
那条路径，`*result` 的替换传不到最终输出。这条链要打通得同时改归档的接收与发布裁决，
涉及 `keep()` 里 "no late family can erase that first result" 的显式契约。

**结论**：这个杠杆的机械部分（编译、三目标验证、Pareto 支配判断）全部验证可行、收益
1.2–1.3×／语料约 1.02×，最后卡在**每题的 15 秒搜索预算**和**归档只收第一个验证通过
的候选**这两件事上。前者是配置（CLI 20 秒 / 搜索 15 秒），后者是产品契约。两者都不是
几何或调度问题，也不是本轮剩余预算内能安全落地的改动。所有实验代码已回退。

## 51. 更近的工厂列（closer anchor）：1.027×，以及三个挡住它的坑

### 51.1 杠杆本身

出厂几何把工厂列放在 `kShippedFactoryStart = -60`，而搬运走廊在 `laneRow + 3`。
两者之间那 35 行空白是纯粹的死重：活塞每跑一次往返就要走一遍，取料一次走一遍，
60 次投料就是 60 遍。把工厂列挪到 `-25` 就直接从每一次取料里剪掉这段。

单题收益（**交付解**，串行）：

| puzzle | 出厂 | closer | 倍数 |
|---|---|---|---|
| `fc6a70eb` | 3046 | 2241 | 1.359 |
| `7b837aa2` | 24457 | 19032 | 1.285 |
| `550b9c1c` | 12571 | 9806 | 1.282 |
| `1dc60983` | 23308 | 18303 | 1.273 |
| `f613a067` | 13856 | 11021 | 1.257 |
| `ac8d3e1b` | 17651 | 14326 | 1.232 |
| `cb6c9528` | 9641 | 7996 | 1.206 |
| `17a58334` | 28569 | 24734 | 1.155 |
| `f58f0f1f` | 6792 | 5987 | 1.134 |
| `803e8d4a` | 34762 | 31787 | 1.094 |
| `e8b19d31` | 9811 | 9426 | 1.041 |

语料（146 题，`--jobs 6`，交付口径 —— 每题重验 `.solution`，`OPUSOPTIMA_LD_CLOSER_ANCHOR=on`）：

```
coverage       139/146 -> 139/146        (不变)
delivered sum  1,162,831 -> 1,133,342    1.0260x
13 faster / 1 slower / 0 lost
```

收尾时又排除了 closed-cardinal-life 域（见 51.8），因此最终版本是 **12 题提速**
（`17a58334` 回到 28569），总和约 1,137,177，即 **1.0226×**。

它是**逐题最优值**，不是一个更好的默认值：`89d6e08e` 与 `a7d3c0bf` 在 `-25` 下
直接碰撞无解（`collision during motion phase`），必须能退回 `-60`。

### 51.2 坑一：提交配额是契约，不是建议

第一版把 closer 做成第二个 `attempt(...)` 候选，让它和出厂机同台比较、按 Pareto
支配采纳。所有
`1dc60983` 的日志都显示采纳成功（`event=closer-anchor-adopted cycles=18303`），
但全量测试多出两条红色：

```
[  FAILED  ] AssemblyDiagnostics.BatchConsumerRetainsFirstAndRejectsWithinParentCap
[  FAILED  ] AssemblyDiagnostics.NearCacheRetainsFirstAndRejectsWithinParentCap
  Expected: (submissions) <= (3U), actual: 4 vs 3
```

`CollectVerified(puzzle, sink, maxAccepted)` 的契约是**提交数不超过调用方给的父配额**
（这里是 3）。多一个候选就是第 4 次提交，不管它多好。所以任何"事后比较再采纳"的
方案都会破坏它 —— 更优的机器必须在**首次提交之前**就选好。

### 51.3 坑二：`verify` 就是 sink，没有免费验证

想绕过配额，就得先拿到 closer 的指标再决定提交哪一个。于是试了"本地验证"：
`(*verify)(compiled)` 不走 `verifyAt`，看起来不计数。

事实相反 —— `CandidateVerification` 这个回调**就是** sink 本身。在 CLI 里它是
`main.cpp` 的 `submit` lambda，内部真的会 `archive->Submit(candidate, ...)`；在
单元测试里它是测试自己的计数 lambda。所以"本地验证"既算一次提交，又把机器塞进了
归档，两头都错。两条测试依旧红。

**这条链上没有免费验证**：任何 metric 都要付一次提交。

### 51.4 坑三：发布的是 `archive->First()`，不是最优机器

前两版还有一个更根本的问题：即使采纳成功，**CLI 打印的周期数一点没变**
（`Cycles: 23308`）。原因是 `main.cpp` 的收尾：

```cpp
report  = archive->Snapshot(verificationOptions);
solution = archive->First()->solution;      // "stable first-verified"
```

`solution` 被无条件覆盖成归档里**第一个**被验证通过的候选。`publish()` 的返回值
（也就是被替换成 closer 的那个 `*result`）在这里被丢弃。所以事后采纳的机器只会进
`.objectives.json` 的三槽清单，永远不会进交付文件。

⇒ 只有在首次提交之前完成选择，收益才会真的落到 `.solution` 上。

### 51.5 口径纠正：manifest 不等于交付解

这一节是本轮最重要的方法论修正。

`tools/probe_arm_coverage.py` 的 `cycles_of()` 取的是 `.objectives.json` 三个槽的
**最小值**。而归档的三槽语义是"每个槽各自最小化一个目标"，里面会同时混入静态内嵌
机器（`instructions: 0`、`cycles: 3`）和动态机器 —— 跨 authority 取最小没有意义。

用 `opsol.verify()` 重验 139 个交付文件后：

```
delivered sum 1,162,831 ;  manifest sum 1,036,175 ;  1.12x 乐观偏差
67 of 139 题两者不一致
```

也就是说历史语料数字是 manifest 口径，比真实交付乐观约 12%。本节的 A/B 全部改成
**重验交付解**。这也解释了为什么 `17a58334` 之前看起来是 closer 的反例
（manifest 16486 vs 24732）：它的 manifest 里躺着 `OptimizeVerified` 找到的另一台
机器，而交付的是 `First()` 的 28569 —— 换成 closer 之后交付反而降到 24734。
**反例是口径造成的假象。**

### 51.6 最终实现

选择前移，提交次数保持 1：

```cpp
if (closerAnchor)
    result = attempt("life-death-closer-anchor",
                     [&] { return compileMachine(kCloserFactoryStart); });
if (!result)                       // 碰撞 / 编译失败才回退
    result = attempt("life-death", [&] { return compileMachine(std::nullopt); });
```

- **只在顶层生效**（`g_compositionDepth == 0`）。嵌套合成里 `independent-outputs` 会
  按输出重入一次求解，多出来的那一次编译+验证会吃掉子预算：`e55f57a6a0` 在 6 并发
  下因此整题丢解（closer 开 3/3 失败，closer 关 3/3 成功），串行却正常 —— 这类
  "只在负载下出现"的回归必须用并发复测才抓得到。
- **默认关闭**（`OPUSOPTIMA_LD_CLOSER_ANCHOR=on` 才启用）。原因见 51.8：两条既有契约
  假定"首个被接纳的 life-death 机器就是出厂列那台"。
- 正常路径：1 次编译 + 1 次验证 + 1 次提交，和出厂完全一样，不额外吃预算。
- 碰撞路径：closer 在编译期抛 `std::invalid_argument`，`attempt` 吃掉，回退到出厂
  锚点，覆盖不受影响（`89d6e08e` 29875、`a7d3c0bf` 20121，与基线 29873 / 20119 同）。
- `OPUSOPTIMA_LD_CLOSER_ANCHOR=off` 精确还原出厂行为。

代价是 1 题退 1.0%（`9a6d6d9035` 39702 → 40087）。这是刻意接受的：要消掉它就
得再提交一个候选来比较，而那会破坏配额契约。（先前用 manifest 口径看到的
`266e6feeb2` 9272 → 9437 也是同一类假象，交付口径下它并没有变慢。）

### 51.7 负载敏感性

`--jobs 6` 与串行差别很大：串行到 life-death 阶段还剩约 6.3 s，6 并发只剩
2.6–3.5 s（求解器本身吃满核心，并发即超订）。任何"若剩余时间 > N 秒才尝试"的门控
在并发下都会静默失效 —— 第一版 closer 就是这样在语料里 136/146 题根本没执行。
最终实现不含时间门控，因此不受影响。

### 51.8 两条契约把 closer 挡在默认路径之外

启用 closer 后全量测试多出两条红（都在 `=on` 下复现、默认关闭时全绿）：

1. `MaterialPlanning.ClosedCardinalLifeDefaultCollectionRequiresNativeAuthority`
   —— closed-cardinal-life 域的契约是"只有 shared-plan 机器可以通过门控"，而 closer
   列让 life-death 机器在该域变得可用。已在代码里用 `!SupportsClosedCardinalLife`
   排除该域（因此 `17a58334` 这类题拿不到收益）。
2. `SharedMolecularFeed.NativePortfolioReusesStaticSlotAndRetainsFirst`
   —— 断言 `portfolio-shared-life-death` 的 cost 必须低于首个归档候选。closer 机器
   本身已经比出厂机便宜，于是"shared 必须更便宜"这个前提不成立：
   `EXPECT_LT(shared->metrics.cost, archive.First()->verification.metrics.cost)`。

第 2 条不是 bug，而是"基线必须更差"的测试前提。要它成立就得让 closer 只在某些形状
下生效，而现有代码里没有可用的"静态彩色内嵌形状"判定，硬编码形状会变成特例堆叠。
所以这一轮把它保留为**实测过的可选杠杆**（一个环境变量即可打开，语料收益 1.0226×），
默认路径与 HEAD 完全一致，测试回到基线 11 红。要真正落地，下一步应当是把这两条契约
从"首个候选必须是出厂机"改写为"首个候选必须是当前最优机"——那是一次契约迁移，不是
几何改动。

## 52. 与 OpusSolver 的真实差距：不是几个百分点，是结构（2026-11-14 全量审计）

`tools/audit_gap.py` 是官方口径：逐槽（cost / cycles / area）比对
`.objectives.json` 三槽与 `result/opussolver-oracle-20261110/oracle.json`。
这一轮跑了一遍 shipped 默认（146 题，`--jobs 8`）：

```
produced 139/146   missing 7
cost    win  63  lose  76   ratio p50  1.11  p90  4.04  max 25.72
cycles  win  51  lose  88   ratio p50  9.20  p90 25.55  max 68.92
area    win  60  lose  79   ratio p50  1.18  p90  4.35  max 32.26
puzzles winning every slot: 51      puzzles losing every slot: 79
```

按类别（cycles 中位比）：

```
linear/mono          39   p50  0.05   worst  39.2
pipeline/cheap-arms  29   p50 16.56   worst  68.9
pipeline/heavy        7   p50  0.01   worst  34.9
universal            64   p50 11.89   worst  59.4
```

### 52.1 两个必须分开看的子集

`linear/mono` 与 `pipeline/heavy` 的中位数是 **0.05 / 0.01**，看起来我们碾压对手 ——
但那 **全部来自静态内嵌**（`portfolio-static-embedding`，`instructions=0`，
`cycles=3`，legacy-gate 通过）。判据很简单：把 `instructions==0` 的条目剔除后：

```
非静态机器里，周期赢过 oracle 的题目数 = 0
```

也就是说：**凡是真的要装配的题，我们一台都没赢**。这是本轮最重要的一句话。

### 52.2 结构对比（`1dc60983`，pipeline/cheap-arms）

| | 我们 | OpusSolver |
|---|---|---|
| cycles | 23308 | 778 |
| cost | 7010 | 830 |
| area | 1552 | 165 |
| instructions | 33486 | 782 |
| 对象 | 49 活塞 / 44 轨道 / 9 bonder / 12 unbonder / 1 arm1 | 15 arm1 / 7 活塞 / 4 bonder / 2 unbonder / 4 轨道 |

我们造的是「**一台被拉长到 30 格的串行搬运机**」，对手造的是「**一排局部臂链 + 4 条
并行 bonder 流水线**」。

`3d4d0c7414` 更极端：我们用 15 活塞 + 5 unbonder，跨 29 格，547 周期；
对手用 1 活塞 + 3 arm1 + 1 unbonder + 1 bonder，跨 12 格，37 周期。

### 52.3 周期到底花在哪

`1dc60983` 的交付机里最长的一根磁带是
`piston pos=(21,10) n=17001`，内容是 `GAAAAAAA…` —— 一万多个 `A`。这不是在做功，
而是「等待填充带」：磁带长度必须等于周期，否则对象会提前循环。所以**周期本身**才是
被优化的量，它由 60 次取料时间表决定：`EVENTDIAG events=10 factories=4
productCount=6`，即 10 事件/产品 × 6 产品 = 60 次串行活塞往返，23308 / 60 ≈
**388 周期/次**，而走廊长度只有 11–29 格。

两件事同时错了：

1. **6 个产品完全串行**。对手把 6 个产品在流水线里重叠，周期≈单件周期×2，不是 ×6。
2. **单件周期本身也长了 ~3×**。388 周期/次取料 vs 对手整件 ~130 周期（778/6）。

### 52.4 单独复制机器并不够（已算过，别先试这个）

把整机复制 k 份并行（每份生产 6/k 件）能让周期 ≈ 23308/k：
k=6 → 3884 周期，**仍然比对手的 778 慢 5×**，而 cost/area 立刻 ×6。
换句话说：并行化只解决 (1)，不解决 (2)；而 (2) 才是主因。

### 52.5 空间跨度是次要但真实的杠杆

用对象坐标算了跨度（`max-min` 的 q+r）：

```
span 比（我们/对手）：p50 1.39   p90 4.44   max 22.15
跨度为对手 2 倍以上的题：43 题
最离谱：9a6d6d90 我们 1351 格 vs 对手 61 格；ac8d3e1b 528 vs 52
```

跨度大 → 轨道更长（cost/area）+ 行程更远（cycles）。`PuzzleData` 里**没有固定
glyph 位置**，只有相对原子坐标，`input`/`out-std` 放哪里完全自由 —— 所以这些跨度
是我们自己的布局选择，不是题目约束。

### 52.6 追上对手需要什么（架构结论）

要让非静态机器的周期进入对手量级，需要的不是再调一个常数，而是一个新的机器设计器，
它必须同时具备三条性质：

1. **局部性**：相邻工位在臂可达范围内（arm1 reach ≤ 3），不用长轨道/长行程活塞；
2. **字形复用（loop instead of unroll）**：同一类操作重复 N 次时，让分子绕回同一个
   字形 N 次，而不是沿行铺 N 个字形（`3d4d0c7414` 的 5 个 unbonder vs 对手 1 个）；
3. **流水线**：每个臂的磁带是一段短循环体（对手典型 `GrX` + 若干 `C`），多个产品
   同时在机器里处于不同阶段，周期 = 最慢工位时间，而不是所有工件之和。

现有 `CompileRowAssemblySingleProduct` / `life-death` 都是「展开 + 串行 + 远距离
搬运」，三条都不满足。这是下一阶段的主线工作，不是收尾调优。

## 53. 审计口径的第二处偏差，以及化学域的字形代数

### 53.1 `--jobs 8` 会丢覆盖，比率也偏悲观

§52 的审计报告 `produced 139/146  missing 7`。把 7 道 missing 逐题串行重跑：

```
e42f1f11  29505    c fba9c05  17762    <- 串行能解，jobs=8 时被记成 missing
19ff88db  FAIL     19c7b313  FAIL     16b1a5f5  FAIL     07e252a4  FAIL
0409d7fe  FAIL
```

也就是**真实覆盖是 141/146**，不是 139/146；那 2 题是并发过订导致的超时（求解器把 12 个核
占满，8 并发就是超订 8 倍）。同理，§52 里 p50 9.20 这类比率是在被压缩的预算下测的，
真实值只会更宽松而不是更差。**以后报告差距时要么用 `--jobs 2`，要么注明并发度。**

### 53.2 输掉的 88 题到底缺什么能力

按 `opsol.read_puzzle` 对产物原子分类（单输出题）：

```
losing puzzles by domain:  化学/解体 65,  多输出 23,  试剂合成 0
whole corpus by domain:    化学/解体 95,  多输出 31,  试剂合成 13
```

我们**已经全赢**试剂合成域（13 题，靠 `ComposeAssembly` 的「每原子一臂 + 流水线」），
而 88 道输的题**一台都不落在这个域里**。所以瓶颈不是调参，是化学域的机器设计。

按产物原子统计（含该原子的输题数 / 全部）：

```
Salt 53/87  Water 30/49  Fire 26/44  Air 22/34  Earth 25/34
Repeat 24/24  Gold 15/23  Vitae 19/22  Mors 20/22  Tin 12/18
Iron 16/18  Quicksilver 8/13  Copper 10/13  Quintessence 10/10
```

### 53.3 化学字形的精确语义（从 `third_party/omsim/sim.c` 抄下来）

要做化学域的 station 目录，必须按引擎语义来：

| 字形 | 输入 | 输出 | 说明 |
|---|---|---|---|
| `ANIMISMUS`（life-and-death，权限位 13） | Salt @(0,0)、Salt @(1,0) | Vitae @(0,1)、Mors @(1,-1) | 两个输出格必须都空，否则转换不再触发；permission 位 13 |
| `UNIFICATION` | 四种元素 @(0,1),(-1,1),(0,-1),(1,-1) | Quintessence @(0,0) | `(*a|*b|*c|*d) & ANY_ELEMENTAL` 必须覆盖全部四种元素 |
| `DISPERSION` | Quintessence @(0,0) | Earth (1,0)、Water (1,-1)、Fire (0,-1)、Air (-1,0) | 与 unification 互为逆 |
| `CALCIFICATION`（位 12） | 任意元素 @(0,0) | 原地变 Salt | 已在 `PlanStation` 里 |
| `PROJECTION`（位 14） | 低阶金属 @(1,0) + Quicksilver @(0,0) | 原地升一阶 | 已在 `PlanStation` 里 |
| `DISPOSAL` | 任意无键无抓取原子 @(0,0) | 销毁 | **用来清掉 animismus 多产的那个原子** |
| `EQUILIBRIUM` | — | — | `sim.c` 里是空实现，不要指望它 |

元素（Air/Earth/Fire/Water）从 Salt 来：`LifeDeath.cpp` 用 `baron` 轮 + `glyph-duplication`
把 Salt 变成指定元素（`wheel[type]` 决定方向）。所以"只有 Salt 输入却要 Quintessence"的题
（如 `f2d0ed7603`）是可以做的，只是需要 wheel 这一环。

### 53.4 已经验证可行、但这一轮没有落地的两块

1. **`StationKind::LifeDeath`**：把 animismus 加进 `PlanStation`/`EmitStation` 是可行的
   —— `Station` 已有 `footprint[]`/`reagentCells[]` 数组，够容纳 2 个盐口 + 4 个字形格 +
   1 个 disposal 格。实测卡点是：4 道「产物 ≤6 原子且含 Vitae/Mors」的题
   （`f2d0ed7603`、`6e993c03de`、`550b9c1cb1`、`284971e280`）**同时还缺 Quintessence
   或需要解体**，所以单独加这一个 station 不会改变任何结果 —— 必须和 unification /
   wheel 一起做才有意义，否则就是死代码。
2. **`ComposeAssemblyVariants` 拿不到展开后的重复分子**：`CompileRepeatingOutput` 先
   `ExpandRepeatingOutput`（6 单体 → 6 原子，正好在 `kMaxComposedAtoms=6` 内），但它只
   转调 `CompileRowAssemblySingleProduct`，从不让组合器看到那个展开分子；而组合器直接
   收到原题时，`Repeat` 标记不是原子，`PlanStation` 必然失败。这条线**仍未闭合**，是
   11 道 `Repeat` 输题的直接入口。

### 53.5 下一步的顺序（按收益/成本排）

1. 把 `ComposeAssemblyVariants` 接到展开后的重复分子上（改动小、1 处调用点、11 题入口）。
2. 一次性补齐 station 目录：`LifeDeath` + `Unify`（4 元素→Quintessence）+ wheel
   （Salt→元素）。这三个一起上，否则任何一个单独上都不改变结果。
3. 只有这三件事都到位，`ComposeAssembly` 才会从「13 题专用」变成动态机器的主力家族，
   §52.6 的三条性质（局部性 / 字形复用 / 流水线）才会作用到那 65 道化学题上。

## 54. 缺口尺寸核算：没有便宜的杠杆了

这一轮把「补齐什么才能赢」算成了数字，结论是没有单点可捡。

### 54.1 88 道输题按输入形态分解

```
shape               losing  winning
molecular-input         39        6      （有分子输入需解体）
multi-output            23        8
reagent-only            26       37      （全部单原子投料口）
```

最大的一块是**分子输入解体（39）**，不是字形。

### 54.2 只加 glyph station 只覆盖 4/88

把 `PlanStation` 依次扩成 S1..S4 后，能在「试剂合成」模型内完整计划的输题数：

```
S1 current (direct+calcify+project)   covers  2 / 88
S2 +life-death                        covers  2 / 88
S3 +wheel (salt -> elemental)         covers  4 / 88
S4 +unification (4 elemental -> quint) covers 4 / 88
```

原因：26 道 reagent-only 输题的真正需求是**化学链**而不是单个 station，例如

```
in=(Lead,Quick) -> out=(Copper,Gold,Iron,Lead,Quick,Silver,Tin)   ← 一个 Lead 要投影出 6 种金属
in=(Salt,)      -> out=(Air,Earth,Fire,Mors,Quint,Salt,Vitae,Water) ← 要 wheel+animismus+unify 同时到位
in=(Quint,)     -> out=(Air,Earth,Fire,Salt,Water)               ← dispersion + calcification
```

### 54.3 现有流水线组合器对输题几乎不可达

`ComposeAssembly`（唯一具备§52.6 三条性质的家族）要求每个产物原子都有试剂口。逐题核算：

```
单分子输入且输入原子多重集 == 产物多重集（纯重排）：2 道（均输：860 vs 219、4928 vs 612）
加上 3d4d0c7414（Repeat 展开后恰好 6 原子、与输入一致）：合计 3 道
```

也就是说，把 `ComposeAssemblyVariants` 接到展开后的重复分子、并把 `kMaxComposedAtoms` 放宽，
最多也只多拿 3 道题 —— 单独做不值得，但可以作为引擎成型后的顺带收益。

### 54.4 所以真正缺的是唯一一件东西

**化学链规划 → 局部流水线布局。** 规划这一半其实已经有了：`ChemistryPipeline::Plan(sources,
demands)` 会给出一条 reagent→glyph 链（`life-death` 与 row 家族都在用），
`AssemblyPlanner::PlanSupply` 负责投料。缺的是**落地**：这些链目前只会被物化成
「沿行铺开的字形 + 远距离活塞搬运」，也就是 §52 里那 49 活塞 / 44 轨道 / 23308 周期。

要实现的是一个新的物化器：

1. 对每个产物原子，从 `ChemistryPipeline::Plan` 取它的链（reagent → [wheel | calcify |
   project×k | animismus | unify | dispersion] → product atom）；
2. 把链上的字形按**局部性**聚类到产物框附近（相邻工位在 arm 可达半径内）；
3. 每个 hop 一个 swing arm，磁带是短循环体（`GrX` + 少数 `C`），多个产物同时在制；
4. 同类型重复操作**绕回同一个字形**（`3d4d0c7414`：对手 1 个 unbonder，我们 5 个）；
5. 多出来的副产物用 `glyph-disposal` 就地销毁（见 §53.3）。

这是一次架构迁移，规模相当于第二套设计器；上面 1–5 每一条都已有明确的引擎语义支撑
（§53.3 的字形表）、明确的成功判据（同一题上 cycles 要接近 oracle 的 37/123/778 量级）
和明确的验收口径（`audit_gap.py --jobs 2` 的逐槽 win/lose）。

### 54.5 顺带排除掉的一条捷径：电梯并行

`LifeDeath.cpp` 里的电梯注释写着"一部电梯承载所有原子，它的磁带就是机器周期；按 K 拆开
就把磁带除以 K"，默认却是 1，看起来像是一行改动换 K 倍。实测：

```
1dc60983  K=1 23306/6610/1545   K=2 FAIL   K=3 FAIL   K=4 FAIL   (碰撞)
9a6d6d90  K=1 39700/20870/5854  K=2 39700/20870/5854             (完全不同路径)
```

也就是说电梯拆分在需要它的题上直接碰撞无解，在另一类题上根本不走那条路。**别再把这条
当成未启用的开关**——它和 §48 里"走廊自身对象时钟"是同一种失败（拆开就撞）。

## §55 臂网原子上限放开：找到布局，但布局本身是错的

§54.4 判定唯一缺口是"化学链 → 局部流水布局"。第一步是把 composer 的臂放置上限
从 `kMaxComposedAtoms`（6）解耦：`Placement::Solve()` 里 `targets_.size() > 6`
会在尝试之前直接否定，`422db8f037`（12 原子）因此根本没进过流水布局。

补丁：新增 `kMaxArmPlacementAtoms = 24` + `kMaxArmPlacementNodes = 400000` 节点预算。

结果（`OPUSOPTIMA_COMPOSE_DEBUG=1`）：

| 题 | 改前 | 改后 |
|---|---|---|
| `422db8f037` | `unsupported … no collision-free arm placement` | 找到布局（49 objects），但 `probe variant=0 passed=false detail=collision during motion phase` |
| `cf12f5cef9`（7 原子） | `no collision-free arm placement` | 仍然 `no collision-free arm placement` |
| `3f81a325b7`（1 原子） | 15 cycles | 15 cycles（无回归） |

### 55.1 结论：`Placement` 的碰撞模型没有时序

`Placement` 只做两件事：静态占用（`occupied_` / `blocked_`）和扫过弧
（`SweptCells`）。真实机器还要满足同一 tick 不重叠：两个臂可以共享同一格扫过弧
（周期内错峰经过），但若它们恰好在同一 tick 经过，就是 `collision during motion
phase`。

所以"放开上限"没有价值：它找到的每个 >=7 原子的布局都被验证器否掉，只是白烧放置
搜索的节点预算。已回退，工作树与 `0378612` 一致。

### 55.2 下一步是明确且唯一的

要让 composer 吃下 `422db8f037` / `cf12f5cef9` 这类多原子产物，`Placement` 必须
从"静态 + 扫过弧"升级为逐 tick 调度：

1. 每个臂给出周期内的相位（`Station::Intro()` 已经是 0/1/steps，可复用）；
2. 布局候选确定后，展开一个 `period` 长的 tick 表，逐 tick 检查所有运动原子
   （被举着的 + 回程的）的单元格集合互不相交；
3. 冲突时优先平移相位（免费），其次平移站点（1 格），而不是重新回溯整棵树。

这是纯增量改造（不影响 `3f81a325b7` 这类已胜的 1 原子路径），也是通往 oracle
的 37/123/778 量级的唯一通道。

## §56 臂网的四种修法全部证伪（§55.2 的执行记录）

§55 之后把"让 composer 吃下 >=7 原子产物"拆成四个假设逐一实测，全部否掉。
每条都是单独构建、单独测量 `422db8f037`（12 原子，oracle 1098）与
`cf12f5cef9`（7 原子，oracle 531）。

### 假设 1：上限是唯一阻塞
放开 `kMaxArmPlacementAtoms = 24` + 40 万节点预算。
结果：`422db8f037` 从 `no collision-free arm placement` 变成**找到 49 个物体的
布局**，但验证器判 `collision during motion phase`。`cf12f5cef9` 依然放不下。
结论：上限是必要条件，不是充分条件。

### 假设 2：并发相位冲突
in-place 布局用 `Station::Intro()` 做相位，而所有 `Direct` 站点的 `Intro()` 都是
0，于是 12 个臂全部在第 0 tick 起摆。改成把所有臂的相位首尾相接（
`BuildOrderedNetwork` 和接力调度已经在用的形状），并把所有 tape 补齐到同一周期。
结果：错误从 `collision during motion phase` 变成 `instruction conflict`——
补齐时在已存在的重复标记后面又追加了一个 `X`。
修正后仍然是 `collision during motion phase`。
结论：不是相位并发问题。

### 假设 3：臂跨周期漂移
`StationTape` 的 tape 是 `G + 旋转 + X`，**没有回摆**，臂一直停在目标朝向，跨周期
累积旋转后会扫过产物框架。改为平衡 tape（抓-摆-放-回摆），与
`BuildOrderedNetwork` 一致。
结果：仍然 `collision during motion phase`。
结论：不是漂移问题。

### 假设 4：臂的扫过弧从未被记录
`Placement::Visit` 只把新臂的 pivot 放进 `used_`、站点放进 `occupied_`，
**任何臂的扫过弧都不进任何集合**。所以后放的臂，其 pivot / 站点 / 弧可以压在先放
的臂的弧上，而静态检查看不见。补上 `arcs_` 集合并让 pivot/站点/弧都避开它。
结果：12 原子的搜索**超过 10 分钟仍未结束**（弧约束让回溯爆炸），7 原子仍然
`no collision-free arm placement`。
结论：这是真实的建模缺口，但补齐后搜索不可行；要么换搜索（不是回溯，而是"先排
周期再放站点"的构造式布局），要么放弃 in-place 网格。

### 56.1 结论：in-place 逐原子臂网到 6 原子为止

四条假设的实测把这条路收敛得很干净：**静态可行性与时序可行性在 >=7 原子的密排框
架上不可同时满足于回溯搜索的预算内**。工作树已回退到 `e1ce911`，构建与全套测试
状态不变。

要跨过 6 原子，必须换的不是约束而是**搜索范式**：不再"逐原子放置 + 事后检查碰撞"，
而是"先按周期把每个原子分配到一个 tick 槽，再按槽构造站点位置"（第 5 步的时钟，
第 1 步的几何）。这是 composer 目前唯一剩余的架构级工作，也是 §54.4 判定的缺口在
同一处的第二次确认。

## §57 臂网碰撞模型的三个真实缺陷（本轮定位，全部实测）

§55/§56 的四次否证都建立在错误的碰撞模型上。本轮把模型逐条对照 `omsim`
（`sim.c` 的 `collision()` 与臂的 `PIVOT/GRABBER/BODY` 语义）后，找到三个真实缺陷。
每一条都单独构建并实测。

### 57.1 `SweptCells` 只扫抓手的环，不扫臂体

`SweptCells(pivot, length, grab, drop)` 只返回 hex 距离**恰好等于** `length` 的格。
但一根基座长度为 L 的臂，**臂体占据半径 1..L 的所有格**；旋转时整条臂扫过
`1..L` 的圆环带。
所以：长度为 3 的臂，其内侧第 1、2 段的扫过区域完全没被检查。这正是"布局找到了，
但验证器判 `collision during motion phase`"的来源。

修正（扫描 `radius = 1..length`）后：
- `58daac9ad1` 的 `variant=1` 从 `collision during motion phase` 变成 **通过**
  （cost 110 / cycles 23 / area 12——比它已有的 30/3/4 差，故无收益）。
- `3f81a325b7`（20/15/2）、`00302be6`（50/3/4）不变。
- `cf12f5cef9` / `422db8f037` 从"找到布局但碰撞"变成"找不到布局"。

结论：这是真 bug，但只**减少假阳性**，不产生收益。

### 57.2 臂的静止姿态是永久障碍

臂在 `O` 等待期一直指向 `grab` 方向，占据半径 1..L 的内侧格。这些格既不等于基座
（`pivots` 只含基座），也不在扫过环上（环在半径 `length`）。所以两根臂的静止体可以
互相穿过，且任何扫过弧可以横穿别人的静止体——静态模型看不见。

修正（新增 `ArmBody(pivot, grab, length)` 加入静态集）后，可放置布局进一步减少。

### 57.3 串行相位下，弧可以交叉

把"新弧不得与已提交弧相交"当成硬约束是**过强**的：相位首尾相接后，两根臂的扫过
窗口不重叠，弧交叉是合法的。加上这条约束会让 7 原子框架在 20 万节点内无解。

正确约束集是：新弧只避开**静态**物（基座、臂静止体、站点格、字形 footprint、等待中
的试剂格、本周期已落的原子）；弧与弧之间允许交叉。

### 57.4 结论与已排除的做法

三条修正合起来把模型变正确了，但**没有产生任何新胜场**，而回溯搜索（20 万节点）让
`422db8f037` 的运行时间从 8.5s 涨到 21s 并直接耗尽了判定 deadline（该题从
685/6912/168 变成**无产出**）。因此全部回退，工作树回到 `70d9da4`。

可复用的结论：
1. 碰撞模型正确的版本在 7 原子以上仍然找不到布局，说明**逐原子放置 + 事后碰撞检查**
   这条范式在密排框架上不可行（与 §56.1 一致，但现在是基于正确模型得出的）。
2. 臂长优先顺序应为 **1, 2, 3**（arm1 价格与长度无关，短臂扫过面积小）——这一点是
   无条件正确的，后续任何臂网构建都应照此排序。
3. 真正的出路仍是 §56.1 的 tick-槽优先构造式布局：先定周期与每个原子的 drop tick，
   再反推站点位置，而不是先放站点再检查。

### 57.5 成本口径（新增，影响后续所有优化方向）

`IndependentMetrics` 的价格表（`src/Verification/IndependentMetrics.cpp:99`）：

```
arm1 20   arm2/arm3/arm6 30   piston 40   baron 30
bonder/unbonder 10   bonder-prisma 20   bonder-speed 30
glyph-calcification 10   duplication/projection/purification/life-and-death/unification/dispersion 20
glyph-disposal 0   input/out-std/pipe/track 0
```

推论：**轨道免费，活塞是臂的两倍价**，字形 10–20。所以 cost 的最优结构是"多短臂、
少活塞、少字形"——参考机器正是这样（`2120aa967d`：oracle 15 臂 + 4 活塞，我们
13 活塞 + 1 臂）。这条价格差是 cost 缺口的根因，不是局部搜索能补的：peephole 预算
从 2s 提到 60s、提交数从 256 提到 8000，在 25 道 cost 近失手题上只改善 3 道
（1025→1015、1415→1305、1590→1480），无一翻盘。

## §58 cost 的真实公式，以及为什么 cost 也不可能靠局部优化追上

§57.5 的价格表漏了一项，而那一项正是缺口的主项。

### 58.1 公式

`IndependentMetrics.cpp:208`：`if (track) cost_ += int64_t(count) * 5;`

```
cost = Σ配件价 + 5 × 轨道格数
```

两个独立样本验证：

| 机器 | 配件价 | 轨道格 | 算得 cost | 报告 cost |
|---|---|---|---|---|
| 我们 `2120aa967d` | 450（9 活塞/1 臂/3 bonder/2 unbonder/1 投影） | 58 | 450+290=740 | **740** |
| oracle `2120aa967d` | 540（15 臂/4 活塞/4 bonder/2 unbonder/1 投影） | 34 | 540+170=710 | **710** |

（指标顺序是 `(cycles, cost, area, instructions)`，见 `:402`。）

### 58.2 轨道是主项，不是配件

`2120aa967d` 我们的 740 里 **290（39%）是轨道**；oracle 的 710 里 170（24%）。
而"机构 + 轨道"合计是 650 对 630——**几乎持平**。也就是说：
我们的 cost 不是"用了贵配件"，而是**运输用轨道、对手用臂的弧**。

### 58.3 活塞为什么不能改成臂

把 9 个活塞的 tape 全部解出来看：`maxext` 全是 1（只有一个是 2，且它带 `R/r`）。
真正的位移来自 `A`/`a`（沿轨道移动基座），`E`/`e` 只负责把抓手伸出 1 格去抓/放。
所以位移是"轨道车辆"，而臂的弧完全换不了这个运动——`RetypeNonExtendingPistons`
只处理零运动的活塞，不是遗漏，是真的没有可替换性。

### 58.4 三条便宜杠杆已实测排除

1. **peephole 预算**：`OPUSOPTIMA_COST_PEEPHOLE_MS` 2000→60000、
   `OPUSOPTIMA_COST_PEEPHOLE` 256→8000，在 25 道 cost 近失手题上只改善 3 道
   （1025→1015、1415→1305、1590→1480），**无一翻盘**。
2. **`LD_FACTORY_CLEARANCE=0`**：对 8 道最近的近失手题**数值完全不变**
   （740/635/2455/810/1855/1820/600/1015 全部一致）——这些题不走 life-death 工厂。
3. **轨道裁剪**：`CompactTrackEnvelopes` / `PeepholeTrimTrack(Units)` /
   `PeepholeDropRailWithoutMotion` 已经默认开启，58 格轨道已被压到活塞实际走到的范围。

### 58.5 近失手的分布（这是唯一还有翻转可能的池子）

| slot | 胜 | 负 | 其中 ≤1.1× | ≤1.25× | ≤1.5× |
|---|---|---|---|---|---|
| cost | 63 | 76 | 6 | 13 | 25 |
| area | 60 | 79 | 4 | 15 | 26 |
| cycles | 51 | 88 | **0** | **0** | **0** |

cycles 没有任何一项在 2× 以内（除 3 项），85 项超过 4×——**cycles 只能靠换架构**。
cost/area 各有 25/26 项在 1.5× 内，缺额折算成轨道是 6–40 格。

### 58.6 结论

cost 与 area 的可翻转项都归结为同一件事：**把 6–40 格轨道换成臂的弧**。
area 也因为轨道格是标记格（`Mark`）而同步受益。所以 cost/area/cycles 三个 slot 的
缺口其实是**同一个缺口**，和 §52.6 / §54.4 / §56.1 / §57.4 的结论完全一致：
只有"臂弧为主的周期流水布局"能同时改善三者。
局部优化、参数旋钮、peephole 预算、轨道裁剪这些便宜杠杆已全部走到尽头。

## §59 机会空间的完整测绘：剩下的 264 个败场到底在哪

本轮不再猜杠杆，而是把 146 道题按"臂弧架构能否触达"完整分域，逐域统计胜负。
所有数字来自 `/tmp/gap-now.json`（139/146 产出）与题目本身的结构。

### 59.1 分域结果

| 域 | 题数 | cost | cycles | area | 臂架构能否触达 |
|---|---|---|---|---|---|
| 全站点（每个产物原子都有试剂站点） | 33 | **32W/1L** | **31W/2L** | **32W/1L** | 已触达，几乎全胜 |
| 多输出（≥2 个 out-std） | 32 | 12W/19L | 8W/23L | 9W/22L | 只有 3 道可达，且这 3 道**已全胜** |
| 混合（部分原子需从分子解离） | 11 | 4W/6L | 2W/8L | 6W/4L | 需解离泛化 |
| 部分化学（部分原子需反应） | 58 | 少量胜 | 少量胜 | 少量胜 | 需化学反应编译器 |
| 无任何站点 | 12 | 基本全负 | 基本全负 | 基本全负 | 需化学反应编译器 |

**臂弧架构在自己的领域里 95% 获胜**（33 道里 cost 32 胜、cycles 31 胜、area 32 胜）。
这条架构本身是对的，问题只是它只够得着 114 道单输出题里的 33 道。

### 59.2 三个被逐一关闭的"看起来很大"的口袋

1. **多输出（32 道）**：composer 在 5 处硬性要求 `outputs.size()==1`。看起来是
   32 道的大口袋，但实测其中**只有 3 道**的每个输出都是全站点可达
   （`bc2e1110d1`、`459f635e56`、`266e6feeb2`），而这 3 道**当前已经全 slot 获胜**。
   做多输出合并 = 零收益。
2. **试剂反应闭包（Unify / Animismus / Disperse / Calcify / Projection）**：把
   `ChemistryPipeline` 已支持的反应做成闭包后，完整可覆盖的败场题只有 **5 道**
   （`e5ccff40c0`、`cf12f5cef9`、`8a844d4dcd`、`422db8f037`、`284971e280`），共 13 个槽。
3. **"解离 + 丢弃"（产物多重集 ⊆ 输入多重集）**：只有 **2 道**
   （`fe57c5d393`、`64da536150`），共 4 个槽。

### 59.3 真正的瓶颈：元素藏在分子里

60 道仍然不可覆盖的题里，缺的原子类型高度集中于 **Salt(1) / Air(2) / Earth(3) /
Fire(4) / Water(5)**，以及需要从分子里取的金属。这些原子在题目里**不作为单原子试剂
存在**，只存在于多原子输入分子中。

所以"从分子里解离出元素、再送去站点或直接送进产物框架"才是唯一的缺口。现有
`PlanExtraction` 只做了最窄的一档：

```
if (input.atoms.size() != targets.size()) return std::nullopt;   // 必须 1:1
if (bond.bits != 1) return std::nullopt;                        // 必须是简单键
// 且单输入、纯重排（类型多重集必须相等）
```

要跨过去需要三件事，按实现难度排序：
1. **允许输入原子多于产物原子**（多余原子用 `glyph-disposal`，价格 0）——这是
   `fe57c5d393` 这类题的必需条件；
2. **允许多个输入分子**，每个产物原子从任意分子里匹配；
3. **允许解离出的原子先经过字形站点**（钙化/投影/合一）再落位——这一条才把
   "元素从分子来"和"元素参与反应"接起来。

### 59.4 结论

剩下的 264 个败场分布是：多输出 64 个（不可用臂架构）、化学域 70 道题的约 180 个
（需反应编译器）、混合 18 个（需解离泛化）。

**没有第四个口袋**。所有便宜门槛现在都有实测数字：
peephole 预算（§58.4）、间距旋钮（§58.4）、轨道裁剪（§58.4）、归档选择（0 例损失）、
多输出合并（0 收益）、试剂反应闭包（13 槽）、解离+丢弃（4 槽）。

因此下一步是唯一确定的：**把"从分子解离元素"接进臂弧流水线**（上面 1→2→3 的顺序），
这是 composer 从 33 道扩到约 47 道的路径，也是唯一还有两位数槽位的方向。

## §60 三元素键（triplex）与分子解离：能力边界再一次确认

§59 指出唯一缺口是"元素藏在分子里"。本轮把它拆成两件独立的事，逐件实现并实测。

### 60.1 三元素键：已经能做，但不是瓶颈

产物的键只有两种取值：`bits=1`（普通键，102 道）和 `bits=14`
（`TriplexRed|TriplexBlack|TriplexYellow`，10 道）。**4 道败场题（12 个槽）**因为产物
含三元素键被 composer 直接拒掉：`fe57c5d393`、`cb6c952817`、`8a844d4dcd`、`1b47844e42`。

`bonder-prisma` 的几何是：每个 prisma 站在一个三角形的角上，用旋转选择它要键合的
那条棱；颜色按棱分配为 黄8 = (v0,v1)、红2 = (v1,v2)、黑4 = (v2,v0)。所以**同一条键上
三种颜色需要三个不同 origin 的 prisma**——实测现有行的解正是如此（`fe57c5d393` 有 9 个
prisma、9 个不同 origin = 3 条三元素键 × 3 色）。

实现了通用版 `PushPrismaBond`（枚举 6 个旋转 × 5×5 邻域，找每个颜色落在目标棱上的
origin，且 origin 互不相同），并把 `AppendBonders` 改成按 `bits` 分流；同时放开
`SupportsMolecularAssembly` / `PlanExtraction` 里的 `bits != 1` 限制。

结果：`fe57c5d393` 的 `molecular` 从 `false` 变成 **`true`**——它确实进了分子解离路径。
但后续验证并没有翻任何槽，原因见下。

### 60.2 分子解离：7 个原子用纯臂中继在几何上不可行

`fe57c5d393` 进入解离路径后的报错是
`no collision-free molecular extraction layout`，即 `PlanExtraction` 找不到布局。

原因是可以精确说清的：`PlanExtraction` 要求每个产物原子一条 `PlanRelay` 链，每跳是
一根独立的 `arm1`，且 `usedPivots` 是**全局互斥**的——因为两个同 origin 的臂是两个
重叠的物体，而 builder 是"一跳一臂"，不会合并 tape。7 个原子 × 最多 3 跳 = 最多 21 个
互不相同的 pivot，加上"落地区所有格 + 产物格 + 已用 pivot 全部阻塞"，pivot 预算必然
耗尽。

把落地区搜索扩大（间距从 `{6,8,10,12,16}` 扩到 `{3,4,5,6,8,10,12,16}`、起点从 4 个扩
到 6 个、并允许 4 跳）之后**仍然失败**。所以这不是搜索不够宽的问题。

参考机器本身也印证了这一点：`fe57c5d393` 的 oracle 解是
**12 arm1 + 8 tracks + 7 pistons**——它也用了轨道。也就是说正确的架构不是"纯臂"，
而是"轨道走长途 + 臂做落位"的混合。

### 60.3 area 也一样不可动：它由端口的多原子形状主导

`cca2722a64` 的 area 是 179，oracle 178，只差 1 格。把标记集合按
`IndependentMetrics::Mark` 的规则算出来：我们自己的机械只贡献 **67 格**，其余约 112 格
是 `input`/`out-std` 两个端口按所持分子的原子形状标记的——**固定成本**。

也就是说 area 的近失手（4 项 ≤1.1×）表面上是"差 1-3 格"，实际是我们的机械footprint
只比对手大 1.5%，而这 1.5% 里能省的只有"少一根臂 / 臂短一格"，都不是可以系统化榨取
的杠杆。

### 60.4 结论

本轮实现的 triplex 支持是**真实的能力增强**（`fe57c5d393` 因此第一次进入解离路径），
但它单独不产生任何胜场：解离本身在 ≥5 原子时不可行，而 4 道 triplex 题里有 3 道不是
纯重排。

至此，两条"看起来能省 12 个槽"的路都被实测封死：
- 纯臂解离 → pivot 预算不足（参考机器自己都用轨道）；
- area 微调 → 缺口在固定端口 footprint 上，不在机械上。

剩下唯一未被证否的方向，就是 §59.3 的混合架构：轨道负责长途、臂负责落位、按周期流水。
这是 composer 缺失的最后一块，也是唯一还在两位数槽位量级的方向。

## §61 配置层面的 cycles 杠杆为零（11 个旋钮 × 14 道题，逐位相同）

§58.5 指出 cycles 没有任何一项在 2× 以内，85 项超过 4×。为了确认"是否还有配置层面的
杠杆"，把**所有默认关闭的 opt-in 旋钮**一次性扫过 14 道 cycles 差距大的题
（`e645d0a5`、`3d4d0c74`、`64da5361`、`cf12f5ce`、`63df4fe9`、`f158add5`、
`b9231732`、`b2c42099`、`3406d841`、`284971e2`、`1066bc2f`、`a96324de`、
`2120aa96`、`cd9b4ea9`）：

```
cfg        solved better worse    sum
base           14      0     0   26313
emission       14      0     0   26313
finite         14      0     0   26313
sched          14      0     0   26313
idleenv        14      0     0   26313
emptyret       14      0     0   26313
clocksib       14      0     0   26313
feedclk        14      0     0   26313
gapport        14      0     0   26313
component      14      0     0   26313
sharedws       14      0     0   26313
```

**11 个配置全部逐位相同**（总和都是 26313，改善 0、恶化 0）。

结合 §58.4 已证明的另外三条（peephole 预算放大 30× 无翻盘、`LD_FACTORY_CLEARANCE=0`
数值不变、轨道裁剪已默认开启），可以下一个确定的结论：

> **cycles 在"同一架构 + 任何参数组合"下不可能再改善。**

原因清楚：`RowClockEnabledFrom` 对 `ROW_PIPELINE` / `FEED_PREFETCH` /
`ROW_STRUCTURE` / `ROW_CRITICALPATH` 默认就是 on；剩下的 opt-in 只作用于
life-death / 行族的特定分支，而这 14 道题的机器根本不走那些分支。

### 61.1 顺带记录：三元素键能力（已实现，未发布）

§60.1 的 `PushPrismaBond` 已实现并可用：三色（黄8/红2/黑4）各需一个不同 origin 的
`bonder-prisma`，与行族现有解完全一致。定向 A/B（13 道受影响题）结果是
**0 翻盘、0 回归**，绝对值上把 `fe57c5d393` 1045→1025、`cb6c9528` 3245→3225、
`8a844d4d` 1610→1590，但都没翻盘；而放宽 `PlanExtraction` 的落地区搜索带来明显运行时
增长且同样 0 收益。按"只发布有实测收益的改动"的纪律，两处改动**全部回退**，
工作树保持在 `947de18` 的代码状态。

### 61.2 本轮总账

本轮把 146 道题的机会空间完整测绘并写进 §59–§61。可翻转的槽位只剩两处，且都需要
**同一件缺失的能力**：

- 轨道长途 + 臂落位的**混合周期流水**（§60.4）；
- 从多原子输入分子里**解离元素并可参与反应**（§59.3）。

这两件是同一个编译器的两半，也就是 OpusSolver 的核心。除此之外，所有便宜杠杆
（§58.4、§61）、多输出合并（§59.2）、试剂反应闭包（§59.2）、解离+丢弃（§59.2）、
area 微调（§60.3）都已有实测数字并全部为负。

## §62 最后一条降本路径也是零：布局收紧旋钮

§58.2 证明 cost = 配件价 + 5×轨道格数，而轨道占我们 cost 的 39%。理论上最直接的对策
是把进料间距收紧，让每路进料少走几格。`RowAssembly.cpp` 里确实有这套旋钮：

```
COMPACT_LAYOUT          默认 on
COMPACT_LAYOUT_GAP      默认 2   （clamp 0..8）
COMPACT_LAYOUT_SPACING  默认 min(2, feedSpacing)
COMPACT_LAYOUT_STRIDE   默认 1   （已是最紧）
ROW_INTAKE_GAP          默认 0   （已是最紧）
ROW_INTAKE_BLOCK_GAP    默认 0   （已是最紧）
FEED_BUFFER_STRIDE      默认 1   （已是最紧）
DIRECT_INPUT            默认 on
INTAKE_TWIN             默认 on
```

只有 `COMPACT_LAYOUT_GAP=0` 和 `COMPACT_LAYOUT_SPACING=1` 是真正"还没收紧过"的。
在 25 道 cost 近失手题上实测：

```
cfg        solved flips better worse    sum
base           25     0      0     0   39990
gap0           25     0      0     0   39990   ← 逐位相同
gap1           25     0      0     0   39990   ← 逐位相同
spacing1       25     0      0     4   40240   ← 变差
both           25     0      0     4   40240   ← 变差
stride3        25     0      1     2   40065   ← 整体变差
```

**零翻盘。** `GAP` 逐位相同说明这 25 道题的机器根本不在 compact scheduled-row 路径上；
`SPACING` 收紧反而让 4 道变差（间距一变，胜出的家族就换了）。

### 62.1 到这里三条 metric 的全部"配置层面"可能性都已穷尽

| metric | 已实测的配置杠杆 | 结果 |
|---|---|---|
| cost | peephole 预算 30×、`LD_FACTORY_CLEARANCE=0`、`COMPACT_LAYOUT_*`、`FEED_BUFFER_STRIDE` | 全部 0 翻盘 |
| cycles | 11 个 opt-in 旋钮（§61） | 逐位相同 |
| area | 无（缺口是端口固定 footprint，§60.3） | 不可动 |

配合 §59 的分域结果（臂弧架构在自己领域 95% 胜率、其余域的缺口都是"元素必须从分子
解离"），结论收敛为一句话：

> **在现有架构上，任何参数、任何局部优化、任何后处理都不可能再取得一个胜场。
> 唯一的剩余增量是新建"轨道长途 + 臂落位 + 元素解离/反应"的混合周期流水编译器。**

### 62.2 静态嵌入也帮不上任何失手题

静态嵌入是我们**最强**的机器形态：`b965f7b1d4` 用它做出 cost 10 / cycles 3 / area 8，
而 oracle 是 310 / 274 / 90——三个 metric 全部碾压。原因是这种机器里进料直接落在
产物格、字形原地变换，几乎没有机械。

一个自然的想法是：还有多少失手题本可以用静态嵌入？

实测答案：**0 道**。在 65 道有站点路径的失手题里，**没有任何一道**的每个产物原子都能
由单原子试剂直连供给（`direct = 0/65`）。静态嵌入要求原子就地供给，所以它对任何一道
失手题都不适用。

### 62.3 全量结论

把 §58–§62 的实验合起来，三条 metric 的"便宜杠杆"清单已经全部有数字且全部为负：

| 方向 | 证据 | 结果 |
|---|---|---|
| cost：peephole 预算 | §58.4，2s→60s、256→8000 提交 | 25 题改善 3 道，0 翻盘 |
| cost：工厂间距 | §58.4，`LD_FACTORY_CLEARANCE=0` | 8 题数值不变 |
| cost：布局收紧 | §62，`COMPACT_LAYOUT_GAP/SPACING`、`FEED_BUFFER_STRIDE` | 0 翻盘，部分变差 |
| cost：轨道裁剪 | §58.4，已默认开启 | 已到底 |
| cycles：11 个 opt-in 旋钮 | §61 | 逐位相同 |
| area：机械 footprint | §60.3，179 格中 112 格是端口固定成本 | 不可动 |
| 覆盖：多输出合并 | §59.2，只有 3 道可达且已全胜 | 0 收益 |
| 覆盖：试剂反应闭包 | §59.2 | +5 道 / 13 槽 |
| 覆盖：解离+丢弃 | §59.2 | +2 道 / 4 槽 |
| 覆盖：静态嵌入 | §62.2 | 0 道适用 |
| 归档选择 | 139 个 bundle 全查 | 0 例选择损失 |
| 三元素键 | §60.1 / §61.1 | 0 翻盘（能力已具备） |

**在现有架构上不存在任何还能取得一个胜场的改动。** 剩余 264 个槽位缺口 100% 落在
"元素必须从多原子输入分子解离、并可能参与反应"这一件事上，也就是 OpusSolver 的核心
编译器。这是本会话能得到的最强结论，也是下一步唯一可走的路。

### 62.4 臂体扫过弧修正在解离路径上复核：仍是零

§57.1 找到 `SweptCells` 的真 bug（只扫抓手环，不扫臂体半径 1..L 的圆环带），当时因为它
对 composer 自身领域零收益而回退。本轮把它单独应用到**分子解离**路径复核：

| 题 | 修正前 | 修正后 |
|---|---|---|
| `64da536150`（6 原子纯重排） | 解离机器 `collision during motion phase` | `no collision-free molecular extraction layout` |
| `58daac9ad1` variant=1 | 通过（cost 110 / area 36） | `collision during motion phase` |
| `00302be6` / `3f81a325b7` | 50/3/4、20/15/2 | 不变 |

也就是说：修正让搜索**正确地判否**了原本会被验证器拒掉的布局，但在正确模型下 6 原子的
解离中继仍然找不到布局（pivot 预算问题，§60.2），因此**零胜场**。已回退。

这条复核把 §57.1 的结论从"composer 领域零收益"扩展到"两个上下文都零收益"，模型修正
至此完全结论化：正确但没有可用布局。

## §63 与 OpusSolver 的逐机件对比：差距是"每进料一个长条工厂" vs "一套紧凑流水线"

前面所有章节都在我们自己身上找杠杆。这一节直接把两边的解拆开对比机件，答案是明确的。

### 63.1 机件构成（每机器，oracle 438 个解 / 我们 139 个）

| 机件 | oracle/机器 | 我们/机器 | |
|---|---|---|---|
| **arm1** | **10.82** | **3.64** | 对手用 3 倍于我们的臂 |
| **piston** | **6.21** | **17.14** | 我们用 2.8 倍于它的活塞 |
| track | 4.03 | 7.83 | 我们用 1.9 倍轨道 |
| input | 1.64 | 4.35 | 我们用 2.7 倍进料口 |
| unbonder | 1.33 | 3.88 | 我们用 2.9 倍 |
| bonder | 2.58 | 4.41 | 我们用 1.7 倍 |

对手是"臂为主 + 少量活塞"的混合体，我们是"活塞 + 轨道"为主。臂 20 金、活塞 40 金
且必须配轨道——这就是 cost 缺口的直接来源。

### 63.2 但真正的差距在量级，不是比例

cycles 差距最大的 12 道题，逐个对比轨道格数：

| 题 | 我们轨道格 | oracle 轨道格 | 倍数 |
|---|---|---|---|
| `9a6d6d9035` | **3916** | **23** | **170×** |
| `deecf24d221` | 1620 | 35 | 46× |
| `a7d3c0bf70c` | 1322 | 175 | 7.6× |
| `1dc60983f29` | 942 | 34 | 28× |
| `c55097d66a0` | 862 | 74 | 12× |
| `5911f6b188e` | 708 | 37 | 19× |
| `89d6e08e875` | 631 | 38 | 17× |
| 12 题合计 | **13 363** | **588** | **23×** |

### 63.3 一台具体机器说明了全部问题（`9a6d6d9035`）

```
我们：97 个物体，坐标 x ∈ [159, 1344]  → 机器宽 1185 格
      轨道 551 / 823 / 791 / 88 / 88 … 格
      活塞 tape 长度 20 359 / 23 623 / 26 887 条指令
oracle：53 个物体，坐标 x ∈ [-2, 36]  → 机器宽 38 格
      27 个 arm1，tape 3–32 条指令，轨道 2–5 格
```

**我们造的是"每个进料一个自带长走廊的工厂"，对手造的是"一套紧凑机构每周期产出一个
产物"。** 轨道每格 5 金、每条 `A/a` 占一个 tick，所以这个形状差异同时放大成
cost 和 cycles 的数量级差距（cycles 6 123 倍、轨道 23 倍）。

### 63.4 为什么我们没走对手那条路：那个家族根本没被调用

唯一能产出对手那种形状的家族是 `SerialArmExecution`
（`shared-plan-serial-arms`，"serial fixed-arm handoffs implement material tasks in
one global ledger"）。在 cycles 差距最大的 **16 道题上，它一次都没有被进入**
（`Assembly branch=shared-plan-serial-arms` 一条日志都没有）——material 规划器直接
没给出计划，于是回退到长条工厂布局。

顺手排除了"是不是预算不够"：material 搜索的切片被硬编码成 3 秒，加
`OPUSOPTIMA_MATERIAL_MS=60000` 后运行时间与结果**完全不变**（`1baade1121a` 6.7s
→ 6.7s、9130 cycles → 9130 cycles）。所以是**规划器域限制**，不是时间。

最尖锐的样本是 `1baade1121a`：乘法器 1、产物只有 1 个原子、1 个输入——最简单的题，
我们给出 **166 个臂 / 9130 cycles**，对手 16 个臂 / 233 cycles。也就是说我们在**按
产物数量复制整机构**，而不是让一套机构周期性地复用。

### 63.5 结论：要抄的就是这一件事

对手的机器 = 一套紧凑机构 + 每周期产出一个产物；我们的机器 = 每进料/每产出一套
机构 + 长走廊。

要抄的不是某个旋钮，而是让 `SerialArmExecution` 那条"单账本固定臂"路径在更多题上
产出计划。它已经存在、已经能产出 27 臂 38 格宽的机器，只是域限制（单原子源、至多
四类进料、至多 512 步/原子、输出至多两原子……见 `SerialArmExecution.hpp` 的契约
注释）把绝大多数题挡在外面。

这是本会话能给出的、最具体的"缺什么"答案：**缺的不是优化，是让已有的紧凑流水线
家族覆盖到这些题。**

### 63.6 奖池：多出来的轨道就是全部 cost 缺口

轨道 5 金/格，所以"我们比对手多造多少轨道"可以直接换算成 gold。逐题对比 139 道题的
轨道格数：

| 题 | 我们轨道 | oracle 轨道 | 我们 cost | oracle cost | 多余轨道折金 |
|---|---|---|---|---|---|
| `9a6d6d9035` | 3916 | 23 | **20960** | **815** | 19 465 |
| `e55f57a6a0` | 2038 | 153 | 12210 | 2810 | 9 425 |
| `ac8d3e1be7` | 1644 | 32 | 12650 | 845 | 8 060 |
| `deecf24d221` | 1620 | 35 | 10730 | 1095 | 7 925 |
| `303de6d65a` | 1586 | 36 | 2750 | 680 | 7 750 |
| `a7d3c0bf70c` | 1322 | 175 | 9810 | 2465 | 5 735 |
| `1dc60983f29` | 942 | 34 | 7010 | 830 | 4 540 |
| `803e8d4a0d8` | 973 | 90 | 6745 | 1470 | 4 415 |

合计：**多造 29 299 格轨道 = 折合 146 495 gold**；其中 76 道 cost 落后的题上占
**26 670 格 = 133 350 gold**。

最极端的一道 `9a6d6d9035`：我们 cost 20 960、对手 815，**差 26 倍**，而这 20 960 里
的 19 465（93%）是纯多余的轨道。

也就是说：**cost 缺口不是"配件贵了一点"，而是我们在做一件对手根本没做的事——把原子
沿几百到几千格的长轨道搬运。** 同一件多余工作也解释了 cycles 的数量级差距。

### 63.7 一句话结论

> 缺的不是某个参数、某个字形、某个优化。缺的是**一个紧凑的、按周期产出产物的固定臂
> 流水线**，用它取代"每个进料一个长条工厂"。这个家族（`SerialArmExecution`）在我们
> 仓库里**已经存在并且能产出 27 臂 / 38 格宽的正确机器**，但在 cycles 最差的 16 道题
> 上一次都没被进入——被它自己的域限制挡住了。要抄对手的做法，就是把这条路径的域打开。

至此"缺什么"有了可验证的答案，且奖池是可量化的：去掉多余轨道即可消掉 133 350 gold
的 cost 差距，并同时把 cycles 从数量级上拉回。

## §64 对 §58–§63 的自查：两条结论需要更正，一条得证

按"确保调查没有问题且没有遗漏"的要求，我对前面的结论做了三项独立复核。

### 64.1 得证：cost 的公式完全正确

对**全部 438 个 oracle 解**计算 `Σ配件价 + 5×轨道格数`，与 `oracle.json` 报告值逐条比对：
**438/438 精确相等，零未知机件类型**。§58.1 的公式可以当作硬事实使用。

### 64.2 得证：胜负统计可复现

当前构建重跑完整审计（`/tmp/gap-cur.json`，139/146）：

```
cost    win 63  lose 76   ratio p50 1.10  p90 3.98  max 25.72
cycles  win 51  lose 88   ratio p50 9.20  p90 24.93 max 68.92
area    win 60  lose 79   ratio p50 1.18  p90 4.21  max 32.26
```

与 §58.5 引用的 `/tmp/gap-now.json` **胜负完全相同**。但绝对值有小差异（12 道抽样里 3 道
不同：490→485、1625→1620、1425→1395，cycles/area 一致），说明旧基线是稍早的构建。
**新基线以 `/tmp/gap-cur.json` 为准**；由于没有任何胜负翻转，§58–§62 的"0 翻盘"结论不受
影响。

### 64.3 更正一：机械构成对比此前用了不同 slot，数字偏了

§63.1 拿"我们的 cost-slot 机器"去比"oracle 的三解平均"，属口径不一致。按**同 slot** 重算
（各 139 台）：

| slot | 我们 arm1 | oracle arm1 | 我们 piston | oracle piston |
|---|---|---|---|---|
| cost | 4.90 | **10.40** | 10.88 | **6.20** |
| cycles | 3.81 | **10.57** | 14.68 | **6.20** |

结论方向不变（对手臂多 2.1–2.8 倍、活塞少 1.8–2.4 倍），但数值以此表为准。

### 64.4 更正二：`§63.4` 的"紧凑家族从未被进入"是**错的**

我此前用 `grep 'serial-arm'` 判断该家族是否运行，但
`Assembly branch=shared-plan-serial-arms` 只在**拒绝**时打印（`rejections.push_back`），
成功/无产出路径没有日志。实际复跑 `1baade1121a` 可以看到它在写候选：

```
[io] Writing solution 'Shared-plan serial arm execution' to …/candidate-0.solution
[io] Writing solution 'Shared-plan serial arm execution' to …/candidate-4.solution
[io] Writing solution 'Shared-plan serial arm execution' to …/candidate-8.solution
```

而且这些候选**就是那台 166 臂机器**：

| | 臂 | 活塞 | 轨道 | cost | cycles | area |
|---|---|---|---|---|---|---|
| 我们的候选 | **166** | 0 | 0 | 3430 | 9130 | 555 |
| oracle | **16** | 0 | 7 小轨道 | **720** | **233** | **133** |

所以真实病因不是"缺这个家族"，而是**这个家族自己不流水线化**：`1baade1121a` 的输入是
9 个金属原子（6 个 Lead + 3 个 Tin），产物 1 个 Copper，`products = sourceMass/gcd`，
机器按产物单元铺开成了 166 个臂，而对手用 16 个臂周期性地复用。

### 64.5 更正三：奖池口径修正（数值下调，但占比更高）

§63.6 用"我们的交付机器轨道"比"oracle 的 cycles 解轨道"，高估了。按 cost-slot 同口径重算：

```
我们的 cost 总和 217 640，oracle 98 245 → 2.22x
多余轨道 20 887 格 = 104 435 gold
其中 76 道 cost 落后题上 20 623 格 = 103 115 gold
```

即：我们的超额成本是 119 395 gold，其中 **104 435（87%）是多余轨道**。数字比之前小，
但"轨道就是几乎全部 cost 缺口"这个结论反而更强。

### 64.6 自查后的结论

1. `cost = 配件价 + 5×轨道格` 是硬事实（438/438）。
2. 胜负统计 63/76、51/88、60/79 可复现。
3. 对手的机器：**少活塞、多臂、极少轨道**；我们的机器：**多活塞、少臂、极多轨道**（同一 slot 口径）。
4. **不缺臂家族** —— `SerialArmExecution` 跑了并产出机器，但它按产物单元铺开机构
   （`1baade1121a`：166 臂 vs 对手 16 臂）。真正的缺口是**这个家族内部的流水线化**
   （一套机构每周期产出一个产物），而不是缺一整条路径。

这条更正把下一步从"打开某个域限制"改成"让 serial-arm/wide-inventory 家族
复用机构而不是复制机构"，这是更具体、也更容易验证的目标。

## §65 第二轮自查：又撤回两条，并找到上游文档里被忽略的权威结论

### 65.1 撤回：`§60.3` 的 area 分解是错的

我当时用一个自己写的近似实现算"静态标记格"，得到 `cca2722a64` 的 67 格，然后断言
"179 格里 112 格是端口固定成本，机械只占 67，所以 area 不可动"。

这个近似实现**通不过校验**：对 60 个 oracle 的 cost 解逐个复算，**0/60 复现**
（普遍偏小：29 vs 48、109 vs 219、149 vs 336…）。原因是 `area` 不只是静态 footprint：

1. `IndependentMetrics::Initialize` 标记各部件的静态 footprint；
2. `ObserveArmMotion` 额外标记长度 ≥2 的臂**旋转时扫过的三角形**；
3. **`RasterPoint` 会把运动原子周围半径 `radius + uncertainty` 的圆盘光栅化成格子**并计入 area。

所以 **area 包含运动扫过的走廊**，会随运输距离增长。§60.3 的"area 不可动、缺口在端口
固定 footprint 上"以及 §62.1 表格里 area 一行，**全部作废**。正确的说法是：area 里有一
部分会随机器紧凑化自动下降（这反而是好消息）。

### 65.2 撤回：`§63.3` 的"每个进料一个长条工厂"没有数据支持

这个说法来自 `9a6d6d9035` 单个样本（10 个输入、坐标跨 1185 格）。对 139 台 cost-slot
机器做相关性检验：

```
corr(rails, #inputs)      = +0.027
corr(rails, input atoms)  = +0.010
corr(rails, multiplier)   = +0.009
corr(rails, out atoms)    = -0.025
corr(rails, #pistons)     = +0.566   <<<
corr(rails, #objects)     = +0.580   <<<
```

**轨道与进料数/乘数/产物原子数几乎零相关**，只与"用了多少活塞/多少物体"相关。所以正确
的因果是：**每个活塞都自带一条轨道，而我们用的活塞太多**——不是"每个进料造一个工厂"。

按类别看轨道中位数：`pipeline/cheap-arms` 176（p90 631）、`universal` 98（p90 229）、
`linear/mono` 0、`pipeline/heavy` 0。>200 格的 32 道里，`cheap-arms` 14 道、`universal`
13 道。

### 65.3 更正：`§63.1`/`§64.3` 的"对手臂多 3 倍"是被少数题拉高的平均值

逐题对比 cost-slot 的 arm1 数量（139 道）：

- **我们比 oracle 用更少的臂：130 道**
- 我们用更多臂：**9 道**（且是 166 vs 16、137 vs 13、112 vs 10 这种病态值）

所以准确的说法是两个独立的缺陷：
1. **绝大多数题（130/139）我们根本不用臂**，用的是活塞+轨道；
2. **少数题（9 道）用了病态多的臂**（`SerialArmExecution`/wide-inventory 按产物单元复制）。

平均值（4.90 vs 10.40）把这个反差掩盖了。

### 65.4 新发现（遗漏）：上游自己的文档已经写明缺什么，我一直没读

`result/opussolver-oracle-20261110/REPORT.md` 里写明：

> Generated with upstream `gtw123/OpusSolver@fdedd3f0 --solver Standard --optimize`;
> every solution was verified with local `legacy-gate` (824/850 passed).
>
> Both are **multi-stage element pipelines** with local input disassemblers and cheap
> transfer arms, not a single compact row.
> The upstream architecture is **`ElementPipeline` + `SolutionBuilder`**, which composes
> input areas, generators, buffers and the output assembler into a line.
> **OpusOptima has fixed family emitters instead.**

这直接修正了我 §63/§64 的表述（我写成了"一套紧凑装配 vs 每进料一个长条工厂"）。
权威说法是：**上游把"输入区 → 发生器 → 缓冲 → 输出装配"组合成一条可配置的流水线；
我们是若干彼此独立的固定家族发射器，无法把阶段拼起来。** 这才是"缺什么"的准确答案，
而且它同时解释了 65.3 的两个缺陷：固定发射器要么只会用活塞铺轨道（130 道），
要么按产物单元复制机构（9 道）。

### 65.5 复核后仍然成立的部分

| 结论 | 状态 |
|---|---|
| `cost = Σ配件价 + 5×轨道格` | **438/438 精确成立** |
| 胜负 63/76、51/88、60/79，139/146 | 当前构建复现一致（串行跑为 141/146） |
| 多余轨道 20 887 格 = 104 435 gold，占超额成本 87% | 成立（cost-slot 同口径） |
| 所有参数/局部杠杆为 0 翻盘（§58.4、§61、§62） | 成立 |
| `1baade1121a` 166 臂 vs 16 臂 | 成立（是 `SerialArmExecution` 的产物） |
| "缺的是紧凑臂流水线" | 成立，但应表述为"缺可组合的阶段流水线" |

### 65.6 自查结论

三轮复核一共撤回/更正了 4 条：slot 错配的机械均值（§64.3）、"紧凑家族从未运行"（§64.4，
错）、area 分解（§65.1，错）、"每进料一个工厂"（§65.2，无数据支持）。**硬结论（成本公式、
胜负统计、轨道占比、参数无杠杆）全部经受住了检验。**

下一步的目标也因此改写为：**建一条可组合的阶段流水线（输入区→发生器→缓冲→装配），
而不是继续修固定发射器。**

## §66 第三轮审计：查清指标权威来源，修正成本/面积归因与覆盖失败原因

用户第三次要求"确保调查没有问题且没有遗漏"。这一轮专门查我**从未验证过的前提**：
比较用的数字到底由谁计算、我读的解文件是不是对应 slot 的那台机器、价目表是否完整。
结果又发现 4 处错误，其中一处是方法论的（引擎用错）。

### 66.1 指标权威来源：是 `libverify`，不是 `IndependentMetrics`

`--authority legacy-gate` 走 `EvaluateLegacy`（`src/Verification/ReferenceBackend.cpp`），
指标由 `third_party/omsim` 的 `verifier_evaluate_metric` 计算。`IndependentMetrics` 只服务
另一条路径（`VerifyIndependent` 与 production-replay 认证）。

**这两套实现的 area 定义不同**：`IndependentMetrics` 有 `RasterPoint`（把运动原子周围
`radius+uncertainty` 的圆盘光栅化）；`libverify` 没有。所以 **§65.1 用 RasterPoint 解释
area 是用错了引擎**，那里的"0/60 复现"证明的是"我的近似不等于 `IndependentMetrics`"，
不是"我的近似不等于权威指标"。

口径一致性没有问题：oracle 的 438 个数字是上游生成后**用同一个 `legacy-gate` 复验**
（`REPORT.md`：438/438 compared fields equal）。所以胜负统计本身有效。

### 66.2 area 的真实构成，以及一个可验证的下界模型

权威 `area` = 下列格子的并集：

1. 机柜墙（production 题）、端口原子位、部件 footprint、轨道格；
2. `mark_arm_area`：每臂在初始长度/朝向下、各抓取方向的**可达姿态**（半径 1..L）；
3. `record_swing_area`：臂在长度 2/3 时每次旋转扫过的 3/6 格（`sim.c:853-860`）；
4. **原子访问过的每一格**：`insert_atom` 里 `board->area++`（`sim.c:2419`）。

我按 1–3 复现了静态部分，得到严格下界：**对我们的 139 台机器 0 次高估**
（对 oracle 有 18 题高估 1–27 格，属于端口/字形旋转约定的小残差）。

```
            static      reported     dynamic(原子运输格)
ours        22 064       55 597       33 533   (60% of our area)
oracle      14 708       23 117        8 409   (36% of oracle area)
excess      32 480 = static 7 356 (23%) + dynamic 25 124 (77%)
```

所以 area 超额 **77% 是原子走过的格数**（活塞沿长轨道运送），23% 是静态 footprint。
§60.3 的"112 格是端口固定成本"错，§65.1 的"圆盘光栅化"也错；正确说法是
"原子每进一个新格就 +1 area"。结论方向（area 随运输距离增长、可压缩）成立，
但机制是**逐格计数**，不是圆盘覆盖。

### 66.3 价目表漏了 `out-rep`；成本缺口 = 83% 轨道 + 20% 活塞

`out-rep`（重复产物端口）不在 `IndependentMetrics` 的 `prices` 里，但在 `decode.c` 里合法。
它出现在**我们 144 个 slot**、oracle 0 个，价格为 0。补上后价目表对我们自己的解
**139/139 精确成立**（`Σ配件价 + 5×轨道格`）。

同 slot、同文件口径的成本构成（139 题合计）：

| 类别 | 我们 | oracle | 差额 | 占超额 |
|---|---:|---:|---:|---:|
| 臂 | 14 040 | 28 920 | **−14 880** | −12% |
| 活塞 | 58 920 | 34 480 | +24 440 | 20% |
| baron | 2 040 | 870 | +1 170 | 1% |
| 粘合/分离 | 12 310 | 5 870 | +6 440 | 5% |
| 字形 | 5 340 | 2 710 | +2 630 | 2% |
| **轨道** | **124 990** | **25 395** | **+99 595** | **83%** |
| 合计 | 217 640 | 98 245 | 119 395 | |

轨道格数 24 998 vs 5 079；活塞数 1 473 vs 862；臂类 702 vs 1 446。

**修正 §65.5**：多余轨道是 19 919 格 = 99 595 gold，占超额成本 **83%**（此前写 20 887 / 87%）。

### 66.4 修正"我们根本不用臂"：用错了文件

`/tmp/gap-cur/<sha>.solution`（交付解 = `archive->First()`）在 **116/139** 题里**不是**
cost-slot 那台机器（三个 slot 机器在 109 题里互不相同）。此前"130/139 我们用的臂更少"
是在交付解上算的。

用真正的 cost-slot 机器重算（机构数 = 臂+活塞+baron）：

- 我们更少 **97** 题、相等 3 题、更多 **39** 题；
- 有活塞但**完全没有 arm1** 的只有 **29/139**。

正确表述不是"我们不用臂"，而是：**我们用臂，但臂只占成本 6.5%（oracle 29.4%），
运输主要靠"活塞 + 轨道"**。

### 66.5 覆盖失败重新归因：2 道是实现缺口，不是化学缺失

5 道无解题（串行重跑后仍失败）的 `stderr`：

| 题 | 失败原因 |
|---|---|
| `19ff88db9e` | `serial port placement cannot preserve approach access at (18,0)` + 材料/物理 deadline |
| `07e252a4b9` | **`serial arm material reaction not yet implemented`** + deadline |
| `19c7b3138b` | dispersion 需要非生产单输出 + disposal |
| `16b1a5f595` | `ordinary-assembly: target requires unsupported chemistry` |
| `0409d7fe4b` | `ordinary-assembly: chemical recipe infeasible within supported reaction model` |

前两道是**实现缺口**（可做），后三道是化学模型缺口。此前说"5 道都缺化学"是错的。

### 66.6 结论：缺的是"廉价自由运输"，与上游文档一致

价格结构决定的算术：臂 20 gold（自由移动，不需要轨道），活塞 40 gold 且**必须**配轨道
（5/cell）。oracle 在臂上多花 29 920，在活塞+轨道上少花 74 200 —— 净省 44 280。

所以缺的不是"更紧凑的布局引擎"，而是**用臂做运输的通用机制**：上游文档称之为
`ElementPipeline` + `SolutionBuilder` 的 "input areas, generators, buffers and the output
assembler" + "**cheap transfer arms**"。我们把阶段之间的搬运交给活塞+轨道，代价是
83% 的成本差和 77% 的面积差。这同时解释了 §66.5 前两道失败题正好卡在
"serial arm material reaction not implemented"。

### 66.7 本轮审计的可信度声明

- 验证过：价目表（139/139）、静态面积下界（我们 0 次高估）、胜负（无平局，63/76、51/88、60/79）、
  指标口径（两边同一引擎）。
- 有残差：静态面积模型对 oracle 的解最多高估 27 格（18 题），因此 77%/23% 的分解
  有 ~0.1% 的不确定度，不影响结论。
- 仍未验证：cycles 的构成从未做过同等深度的归因（只做过胜负统计）。

## 67. 配额回归修复与轨道优化基础复核

### 67.1 候选收集契约与全量测试

closer factory sibling 后置于原候选提交，不再扩张 caller 的 `maxAccepted`。
接受额度只统计 authority 过滤后的 Passed report；拒绝不占接受额度，提交次数仍有
独立上限。正常停收使用专用内部信号，sink 异常原样向上传播。bounded collection
暂不运行绕过统一 accounting 的直接 composition probes；该变化的全 corpus 覆盖与
性能影响仍需复测，不能从单元测试全绿推断无影响。

新增三项 BatchConsumer/NearCache/RadialProjection 回归和此前十一项 quota 失败均已
消除。定向相关组 85/85 通过。最终全量：**1070 tests / 131 suites，1065 passed、
5 skipped、0 failed，exit 0**，耗时 897844 ms。日志：
`/tmp/suite-quota-track-unified.txt`。五项 skip 为未注册外部案例的
ConstructionRelayEvidence、ParallelReturnEvidence、RegisteredLayoutProbe、
RegisteredP0CompactionCandidates、RegisteredRowCompilation。

### 67.2 轨道解析与公共包络分析

架构后处理现在完整解析显式 track offsets，不再只看前 64 格，也不虚构 origin 格；
使用宽域坐标计算，负/零 count、截断、缺 footer、多尾数据及坐标溢出均保守保留轨道。
新增四项边界测试。v6/v8 裁轨抽取为公共 `ApplyTrackEnvelopes`，保留两者机械类型、
无附着轨删除和 tape 前置条件的差异；这是行为保持重构，**不计作新增成本收益**。

仍有明确限制：B halt 后不可达后缀仍进入包络分析，不能把后缀闭合当成运行周期闭合
证明；local stages 的 native-authority 支持域仍需核查。未开放端点 noop 或 X/C 展开。

### 67.3 新配额实现下 closer sibling 串行复测

2026-11-15 对同一现有 binary，逐题 off → default 串行执行，没有并行 solver，未重建。
八次 CLI 均 exit 0、无 timeout；24 个 objective slots 的验证状态均 passed，solution
hash 与 manifest 相符。本轮未额外独立重验。下表只读 **cost-slot** 的
`verification.metrics`，每格依次为 cost / cycles / area：

| 题前缀 | sibling off | default |
|---|---|---|
| fc6a70eb | 3200 / 3046 / 858 | 2840 / 2241 / 716 |
| ac8d3e1b | 11830 / 17649 / 2949 | 11480 / 14324 / 2809 |
| 7b837aa2 | 4350 / 24455 / 1058 | 4125 / 19030 / 979 |
| 803e8d4a | 6745 / 34760 / 2047 | 6395 / 31785 / 1907 |

合计 cost 减少 1285，cost-slot cycles 减少 12530、area 减少 501。四题 default
日志均有 closer sibling `accepted=true submission=2`，off 无此事件。这只是单次
观测，不是稳定性统计。fc6a70eb 旧值 3195 → 2835 由本表替代，5 gold 差异尚未归因。
1dc60983 本轮未重跑；此前 default closer 两次 wall-clock timeout，不宣称收益。

产物：`/tmp/closer-quota-<prefix>-<off|default>.solution` 及对应 log、objectives.json、
run.txt。executable SHA256 为
`69955797b03f97bfdac67ead77cfcc795e84dc49f622bc6f73045a0a105d8015`；
实现还依赖 `libOpusOptimaLib.so`，单独复制 executable 不构成旧实现隔离。

### 67.4 下一成本突破口与验收边界

候选方向是仅对两个普通真实分子生成站做错层叠放，保留竖直取货通道、现有 assembler、
反应端口和有限批次调度。只在完整机器序列化轨格严格减少时送 native gate；运输省 H 格、
carrier 新增 V 格时理论净省 `5 × (H − V)`，必须 H > V。静态包围盒不是动态安全证明，
仍需验证分子扫掠、臂伸缩旋转、轨道交叉及交接端口。shared producer、compact relay、
Quintessence 特殊 carrier 不在首版支持域。此方案尚未实现，不计收益，也未证明追上 oracle。

## 68. 最终 parking + static binary：146 题重新审计（2026-11-15）

### 68.1 固定实现与测量口径

本节是最终现有 binary 的新测量，不是沿用历史 141/146。未构建、未修改源码、未 commit，
也未恢复或删除工作区已有的未知 puzzle 删除。完整证据在
`result/gap-parking-static-final-20261115/`：`measurement.json`、`gap.json`、
`audit.log`、`comparison.json`、`changes.csv` 与 `serial-recheck/`。

- 命令：`/usr/bin/python3 tools/audit_gap.py --jobs 2 --out /home/edwards/OpusOptima/OpusOptima/result/gap-parking-static-final-20261115 --json /home/edwards/OpusOptima/OpusOptima/result/gap-parking-static-final-20261115/gap.json`。
- Python 3.13.5；12 logical/affinity CPUs；仅两个外层求解任务，无并发构建、测试或其他 solver，内部生产默认不改；无 OPUS/OMP/LD_/OPENBLAS/MKL 环境覆盖。
- cgroup `cpu.max` 路径不存在，记录 unavailable，不假定无 CPU 配额。首次元数据预检因此停止，目录仍空、未启动 solver；随后唯一一次完整审计成功，耗时 **317.250s，exit 0**。
- executable SHA256：`7aff3c9d875c88610a0d902fbe72d0cb6ea5f0bb1b9d1b4f33690eb9025856db`。
- 实现共享库 `libOpusOptimaLib.so` SHA256：`dcf273e9003d5358fa09e40e17593acf16a86a7c24d1e7bac6be4e2954ebd4d4`。审计前后两者相同；仅 executable 相同不足以证明实现相同。
- oracle SHA256：`ca655809419090de69b2019bb5e98a0a0fc799447ece91b7bb793f2ab7a34980`。
- 对比对象严格为 `result/gap-quota-stagger-20261115/gap.json`，SHA256 `201c7b84fd1ae0000a7a0be03a025e3fa6334b480919fdf67652cc71dacce77c`；该基线也是 jobs=2，319.323s。不是该目录 measurement 内引用的更早 `/tmp/gap-cur.json`。

146 个完整 SHA 唯一且集合相同，oracle 逐题相同。每目标只取自己的权威槽：
`cost-slot.cost`、`cycles-slot.cycles`、`area-slot.area`，不拿 cost 方案三项替代三目标。
全部 **423 槽** manifest 均 `passed / legacy-gate / legacy-gate-passed`，metrics 与
gap 完全相符，输入 SHA 与实际 solution 文件 SHA256 均匹配 manifest。文件路径相对
manifest 目录解析。这是现存权威记录与文件完整性核对，本轮没有另跑 423 次独立 verifier。

### 68.2 同题全库净收益，而非局部收益外推

新旧均 **141/146（96.58%）**；共同成功题恰为同一 141 题，新增成功 0、掉题 0，
同一 5 题缺失。所有共同成功题三槽齐全，以下总量包含全部共同题，不挑收益样本。

| 目标自身槽 | 改善 / 持平 / 退化 | 基线总量 | 最终总量 | 差值 | 降幅 |
|---|---:|---:|---:|---:|---:|
| cost | 20 / 121 / 0 | 243545 | 231340 | -12205 | 5.01% |
| cycles | 9 / 132 / 0 | 1095385 | 1060083 | -35302 | 3.22% |
| area | 20 / 121 / 0 | 62917 | 60322 | -2595 | 4.12% |

这是同预算单次观测的净变化，不是多次稳定性统计，也不是单一优化的隔离消融。
逐题变化见 `changes.csv`。八个静态恢复题贡献 cycles 降低 35300，另 `e42f1f11`
cycles 槽降低 2；不能把静态 embedding 的低 cycles 称为真实机械 assembly 加速。

### 68.3 相对 oracle 的真实差距及三槽统计修复

| 目标 | 基线胜 / 平 / 负 | 最终胜 / 平 / 负 | 同题 oracle 总量 | 最终总量 / oracle 总量 |
|---|---:|---:|---:|---:|
| cost | 55 / 0 / 86 | 63 / 0 / 78 | 100525 | 2.301× |
| cycles | 43 / 0 / 98 | 51 / 0 / 90 | 84506 | 12.544× |
| area | 52 / 0 / 89 | 60 / 0 / 81 | 23568 | 2.559× |

上表总量比不是逐题 ratio 的均值或中位数。最终逐题 ratio p50 / p90 / max 分别为：
cost **1.11 / 3.90 / 25.61**，cycles **9.29 / 24.93 / 68.92**，
area **1.19 / 4.23 / 32.43**。cycles 最大缺口仍为 `9a6d6d90`。

独立从两个 gap 重算：三槽严格全胜 **43→51**，三槽严格全输 **84→76**，
与新 audit.log 一致。脚本要求三个槽齐全才计数，并分别对三项严格 `<` / `>`；
不能把“不是全胜”当成“全输”，也不能让 partial/empty 的空集合真值计入。
已有离线审计测试日志为 **8/8 pass**，覆盖槽顺序、各槽自身指标、胜平负组合、
partial/missing、零 oracle 与大整数，本轮未重跑。**仍未达到 oracle**：
5 题无产物，76 题三槽全输，尤其机械周期仍差一个数量级。

### 68.4 缺失题逐个串行复核

完整 jobs=2 审计结束后，同一实现、默认参数串行重跑全部 5 题；原 gap 未改。
全部 exit 1、无 solution/manifest，串行没有恢复题，因而没有把串行补集冒充全库覆盖。

| SHA 前缀 | 串行秒数 | 日志支持的失败类别 |
|---|---:|---|
| 19ff88db | 3.257 | serial port 无法保留 `(18,0)` approach access；随后 material/physical shared deadline exhausted |
| 19c7b313 | 2.359 | ordinary candidate failed acceptance，inventory 无闭合周期，unification feed bounds，末端无 replayable candidate |
| 16b1a5f5 | 0.053 | life/death row bounds exceeded、unsupported chemistry，末端无 replayable candidate |
| 07e252a4 | 4.147 | serial arm material reaction not yet implemented；随后 shared deadline exhausted |
| 0409d7fe | 0.027 | 无兼容 singleton feed，当前 reaction model 无可行配方；末端无 replayable candidate |

具体完整 SHA、命令、returncode、耗时在 `serial-recheck/results.json`，stdout/stderr
分别保存。它们是基线已缺失且本次完整审计和串行复核均未恢复的真实当前覆盖缺口，
不是本轮新增掉题；没有证据归为已恢复的并发波动。带 deadline 的两题仍受预算影响，
一次串行复核不证明永久稳定失败；所有 unsupported/搜索失败均不构成 puzzle 不可解证明。

### 68.5 最终生产改动、局部证据与全测

停车轨在完整布局/tape 构造后，按 `demand=pickups*repetitions` 保留 `3*demand+4`
格；保守检查竖直独占轨、完整时间 walk 与回起点、溢出和交点，失败保持原对象不变。
不改指令、时间戳、Reserve/Release/Complete，也不增加候选或 sink 配额。
`result/lifedeath-parking-capacity-20261115/report.md` 的隔离 before/after 证据：

- `f613a067` 轨 130→22，每槽 cost -540、area -108；cost 槽最终 `3470/11019/774/23377`。
- `550b9c1c` 轨 94→40，每槽 cost -270、area -54；最终各槽 `3670/9804/834/23445`。
- 上两项格式为 cost/cycles/area/instructions；cycles/instructions 不变，局部每目标合计
   cost -810、area -162。前后 12 槽独立 LegacyGate 通过，非轨对象、完整 tape/时间戳不变。
   **这不是本节全库 delta**：例如全库对比的 f613 cost 基线为 4020，而隔离 before 为 4010。

静态优先预检修复恢复 8 题最终默认合法候选；不是新增覆盖（基线已有较差机械方案）。
最终 cost 槽如下；均已有独立 LegacyGate 证据：

| SHA 前缀 | cost / cycles / area / instructions |
|---|---:|
| e445c554 | 210 / 3 / 18 / 0 |
| 91cd335c | 210 / 3 / 19 / 0 |
| 55c49c46 | 100 / 12 / 8 / 0 |
| 5378287f | 180 / 3 / 19 / 0 |
| 4f6b71ea | 240 / 3 / 19 / 0 |
| 3f81a325 | 20 / 15 / 2 / 0 |
| 0d83143b | 240 / 3 / 19 / 0 |
| 04d711ec | 270 / 3 / 18 / 0 |

优先域仅覆盖原有有臂 composition 域外机会（直供晶格、>16 原子、大/多阶 projection），
并检查供料、glyph/product footprint、重叠及 bond；无 SHA 特判，空 probe 不占 construction。
3 constructions/12 submissions、caller acceptedCap、deadline/First/去重/native 契约保留。

直接核对现有 `result/lifedeath-parking-capacity-20261115/final/full-suite.log/.xml/.exit`：
最终 **1082 tests / 132 suites，1077 pass、5 skip、0 fail，exit 0，868.995s**。
该 final/binary-hashes.json 的 executable/shared library 与本次审计相同；test binary
SHA256 `37f237824e08fc9a44bd7e1e330fd8e0f1bd3ab6605b476a0d86cdd6725e18f1`。
另有核心 57/57、additional 7/7、audit 8/8 现有验收日志。五个 skip 为 Registered
ConstructionRelayEvidence、ParallelReturnEvidence、LayoutProbe、P0CompactionCandidates、
RowCompilation 对应项；不能把 skip 记 pass。历史 1075 项中的两次失败及旧全测数字保留，
但不再作为最终验收值。本轮只读证据，未重新运行完整测试。

**边界不变：stagger clearance=0 没有经 gate 确认的生产收益，收益计 0，未接生产；
§67.4 两个普通分子生成站错层叠放仍未实现。** 本节收益属于已测最终生产路径，
不属于该未实现方向；静态收益、停车局部收益、全库净收益三种口径不得混用。

## §69 Rail-prefix：halt 可达前缀、共享轨道与最终验收（2026-11-15）

### 69.1 语义与保守边界

`ApplyTrackEnvelopes` 先扫描所有原轨道，建立全局 world-cell map，再合并每条轨道上
各 arm 的起点及可达 A/a 访问区间。空 tape 的 arm 起点也必须保留。shared 策略仅接受
arm1/piston；compact 策略在 `ExplicitTapes` 之后处理 mechanical arms。非法 count/body、
track tape、不连续、重复 cell、原 wrap、跨轨 overlap/junction、world int overflow 均拒绝；
compact 删 unused rail 之前已检查原轨道，因此不能通过删除其中一轨绕过 junction 保护。
trim 后再次检查 wrap；shared 不删无 arm 轨道，compact 可以删除。

首个 `B` 后停止 walk，halted 可达前缀无需回起点；未 halt 的重复 tape 必须闭合。
**不可达 suffix 仍全量校验 opcode、非负且严格递增 timestamp**，只是不将 suffix A/a
纳入访问集合。不删除或改写任何 instructions、稀疏空白和起始 delay：native decode 仍会
解析 suffix，并保留 tape length；非 halt 才参与 period。长度限 100000，宽整数检查
`first + 2*length <= INT_MAX`，与 native overflow guard 对齐。
`omsim/decode.c` 的 tape decode/归一化与 `sim.c::perform_arm_instructions` 的
cycle<start、index=cycle-start、仅非 halt 取模、halt/length 截止共同支持此判定。

native 强制 track rotation=0：world cells 使用未旋转 offset，但 serialized rotation 原样
保留。坐标差与加法使用宽整数；不模拟 endpoint no-op、reset/repeat 或 wrapping rails。
helper 在副本中 trim，所有检查通过才替换，拒绝不泄漏部分轨道修改。但组合
`CompactTransport` 中先前独立合法的 piston retype/no-grab removal 可保留，不应误称整个
组合都是全量 transaction。no-grab 判定仍看完整 tape，suffix G/g 不会被当作无 grab 删除。

### 69.2 测量与测试修正

完整证据与分槽表：`result/rail-prefix-20261115/report.md`，输入/验签在
`baseline.json` / `after.json` 与 `*.verify.log`。四题 isolated v8 不是全库收益：
550b9c1c cost/area -130/-15；f613a067 -125/-15；e8b19d31 cost 槽 -65、area 槽 -9；
9a6d6d90 无改善。各自 cycles/instructions 不变。此前默认四题 own-slot 同样的 cost/area
变化，但 9a cost 默认为 20860，不能引用 isolated 父槽 20870 代替。最终 jobs=2 全库中
f613 cost 实为 3470→3355（-115），不是 isolated/default 四题阶段的 3345（-125）。

原本次完整测试 `result/rail-prefix-20261115/full-suite.log/.xml` 为
1082 tests / 132 suites，1076 pass、5 skip、1 fail，861.628s；不是最终绿色验收。
唯一失败 `CandidateArchive.SharedTrackEnvelopesDefaultToSparseClosedTapes` 中旧 case2
错误要求 halt 前缀闭合，case5 错误拒绝 native 忽略的 rotation。修正为未 halt arm
非闭合和非法 track count 的拒绝例，并补 halted 非闭合、rotation 保留正例；不回退生产
实现以迎合过时 fixture。修正后 focused 为 9 tests / 2 suites，8 pass、1 optional skip，
exit 0，97ms（`focused-final.log`）；审计统计 offline 8/8，通过且不调用 solver。

最终全测独占运行，完成后才进行 146 题新 jobs=2 审计，不与 solver 测试并发混预算。
全库按 SHA 对齐 `result/gap-parking-static-final-20261115/gap.json`，使用各 objective
自己的槽 `slots[key][key]` 比 `best[key][key]`；三槽全胜/全输必须三槽完整，oracle=0
不计算 ratio。基线 141/146、全胜51/全输76，不能混用更早 quota-stagger 的43/84。
First/首个 verified `.solution`、submission/acceptedCap/deadline 与默认预算不变。

### 69.3 最终验收：已有归档核对

`result/rail-prefix-20261115/final/full-suite.exit` 为 **0**；同目录 log/XML 一致：
**1082 tests / 132 suites，1077 pass、5 skip、0 fail、0 errors，866.508s**。
这是本次最终结果，以上首次失败与修正记录保留；§68 历史绿色不能替代本次验收。

`audit/audit.exit` 为 **0**。与 parking 基线的 146 个唯一 SHA 及内嵌 oracle 一致；
两期均覆盖 **141/146**、缺失 **5**，完整三槽均 141，恢复/新增缺失均 0。
相同 missing SHA8：`19ff88db`、`19c7b313`、`16b1a5f5`、`07e252a4`、`0409d7fe`。
按各目标自身槽比较（不是 first verified `.solution` 或跨槽 min）：

| 目标 | 改善 / 持平 / 退化（141题） | 基线总量→最终总量 | 差额 |
|---|---:|---:|---:|
| cost | 19 / 122 / 0 | 231340→226085 | -5255（-2.27155%） |
| cycles | 1 / 140 / 0 | 1060083→1059836 | -247（-0.02330%） |
| area | 19 / 122 / 0 | 60322→59487 | -835（-1.38424%） |

受益题并集19；唯一 cycles 改善 `6c4bbc37`：39780→39533。以上是默认预算下两次
jobs=2 归档观测，不把全部差异归因于 isolated trim。总量为各目标自身标量之和。
对 oracle 的胜/平/负两期均为 cost **63/0/78**、cycles **51/0/90**、area **60/0/81**；
完整三槽严格全胜 **51**、全输 **76**，不变。各目标倍率样本141，zero oracle为0。

| 目标 | ours/oracle p50（基线→最终） | p90（基线→最终） | max（基线→最终） |
|---|---:|---:|---:|
| cost | 1.111111→1.111111 | 3.903226→3.903226 | 25.607362→25.607362 |
| cycles | 9.293886→9.293886 | 24.925926→24.925926 | 68.923611→68.923611 |
| area | 1.185294→1.185294 | 4.225753→4.172932 | 32.427861→32.164835 |

p90 使用排序下标 `floor(0.9*(n-1))`。同集 oracle 总量 cost/cycles/area 为
100525/84506/23568；总量倍率分别 2.301318→2.249043、12.544470→12.541547、
2.559487→2.524058，不等于逐题倍率均值。两期 `instructions=0` 槽均为48/51/49，
故未过滤统计不能全部称为真实 assembly 胜 oracle，须另按非零 instructions 筛选。

两期每期423/423槽的 manifest authority 均为 `legacy-gate`，`status=passed`，
engine=`omsim-a88d12d8+production-replay-v1`，ruleset=`legacy-gate-v1`，
profile=`legacy-native`；逐槽 gap 指标及引用 snapshot SHA-256 均相符。
这只核对既有 admission 记录，不是本次独立重验；audit exit0也不等于146/146求解。

基线 `measurement.json` 记录 exe/lib 前后哈希相同。最终 `final/binary-hashes.txt`
记录 SHA-256：
- exe（两期相同）：`7aff3c9d875c88610a0d902fbe72d0cb6ea5f0bb1b9d1b4f33690eb9025856db`。
- 基线 lib：`dcf273e9003d5358fa09e40e17593acf16a86a7c24d1e7bac6be4e2954ebd4d4`。
- 最终 lib：`6e001175810434a049603cd94c2a363d4c76442eb10bae186bdb94892f8ea433`。
- 最终 tests：`5a14cc911bc9e52192fc2cd34134ad5678a78cc1e542e9a63b77204ac637620f`。

当前只读哈希与最终快照相符，exe/lib也与 isolated `after.json` 相符；但最终 audit
没有独立 measurement/运行前后双端指纹，不能据此声称审计期间二进制不变。
基线原 oracle 指纹与两份 gap/comparison 指纹详见 report；最终原 oracle 文件指纹
未独立留档，仅能确认内嵌 oracle 一致。

证据位于 `result/rail-prefix-20261115/` 下 `final/full-suite.{exit,log,xml}`、
`final/binary-hashes.txt`、`audit/audit.{exit,log}`、`audit/gap.json`、
`audit/comparison.json`；基线为 `result/gap-parking-static-final-20261115/gap.json`
及 `measurement.json`。本次仅归档读取、离线复算与文档更新，没有构建、运行 solver
或测试；复算 comparison 的字节哈希与已有文件一致。

## §70 构建模式是预算回归的主因；CLI 额外组合在 Release 下的实测收益（2026-11-15）

§68/§69 之后剩下的“回归”并不是候选语义错误，而是 **Debug 构建（`-g`，无优化）**
把 20 秒墙钟预算里的编译时间放大了约 10 倍。求解器的预算按墙钟计，因此同一份源码
在 Debug 与 Release 下会走到不同的候选。先把这一点测清楚，再谈任何调度改动。

### 70.1 同一个二元组，两种构建的编译时间

`e42f1f11`（4 输入 2 输出）同一份源码、同一命令、串行：

| 阶段 | Debug | Release |
|---|---:|---:|
| `life-death` 编译（候选被门拒绝） | 4129 ms | **409 ms** |
| `independent-outputs` 编译 | 5244 ms | **505 ms** |
| First verified feasible | 12.64 s | **3.7 s** |
| 三次运行的 own-slot | 两次 22200，一次 20100 | 三次均 20100/29503/6322 |

Debug 下 e42 的局部 v8 候选需要两次 native 验证（约 2.6 s），而 First 之后只剩约
2 s 的构造窗口，于是同一个二进制在两次运行里给出不同答案；`19ff88db` 等五题之外的
`e42` 因此会在 jobs=2 审计里偶发缺失。Release 下 First 提前到 3.7 s，11 s 的局部搜索
窗口足够，三次运行逐槽一致。

### 70.2 Release 基线（同源码，仅换构建）

`result/release-build-audit-20261115/` 与 `release-build-audit2-20261115/`：
146 题 jobs=2，覆盖 **141/146**，两次运行的 423 个槽值**逐字节一致**。与
`rail-prefix-20261115/audit/gap.json` 的同题、同目标槽对比：

| 目标 | 改善 / 持平 / 退化 | 总量（Debug rail → Release） | 对 oracle 总量倍率 |
|---|---:|---:|---:|
| cost | 12 / 129 / 0 | 226085 → **221485**（−4600） | 2.249 → 2.203 |
| cycles | 3 / 137 / 1 | 1059836 → **1043785**（−16051） | 12.542 → 12.352 |
| area | 7 / 134 / 0 | 59487 → **58530**（−957） | 2.524 → 2.483 |

唯一 cycles 退化是 `b2c42099` 2148 → 2621：Release 下更早的 `automatic-inventory`
先成功（2669 周期），把 `life-death-local-intake`（2148）挤出首解路径；这是下一节的
触发点。`1dc60983` 23306 → 18301 是更近工厂列的候选第一次真正落进 15 秒窗口。

### 70.3 CLI 额外组合（库 API 默认不变）

`AssemblySolver::EnableExtraPortfolio(true)` 只在 CLI（`main.cpp`）打开，库调用默认
关闭，因此 §67 的“首解保留、接受额度属于调用方、提交次数有上限”契约不变；新增的
`AssemblyDiagnostics.ExtraPortfolioSecondOpinionIsOptInAndBounded` 锁定这一边界。

1. **延后 life-death 发布**（`OPUSOPTIMA_DEFER_LIFE_DEATH`，默认开，`off` 关闭）：
   已采纳的 life-death 机器不再立即返回，后续族可以继续竞争；严格更快的已验证机器
   可替换 `firstAccepted`。语料两轮：`fc6a70eb` cost/cycles/area
   2765/2239/706 → **630/1475/201**，其余 140 题逐槽不变。
2. **life-death 二次意见**：当首解不是 life-death 族时，额外提交 shipped 列、
   local-intake、feed-clocks、closer 列（`-25`）与 compact relay 五个候选，
   每项都过同一 native gate，长磁带（>50000 条）直接跳过。语料：
   cycles 1043021 → **1031029**（−11992）、area 58025 → **57899**（−126），
   cost 总量持平 219350。逐题改善：
   `aeb771ff` 10568→5035、`b4769272` 8465→5990、`b2c42099` 2621→2148、
   `fabd74d3` 3855→1814、`a1e56266` 5874→4404；五个候选中 clocked 与 closer
   各贡献一部分，compact relay 另使 `e645d0a5` 490→485、`d61cc536` 1165→1160。
3. **independent-outputs 的 closer 组合**：多输出题把每个 output 单独编译后拼在
   一张板上，各子机原本都用 shipped 工厂列。现在额外用 `FactoryStartScope(-25)`
   再拼一台 closer 组合，先验基线、再验 closer；碰撞或失败只损失这一份兄弟候选。
   语料：`e42f1f11` cost/cycles/area 20100/29503/6322 → **19750/26633/6182**，
   `deecf24d` 9975/19350/2899 → **9800/19350/2829**，`f613a067` 3325→3315、
   `cb6c9528` 2465→2455；其余 137 题不变。

最终 CLI 默认（Release）与 rail-prefix Debug 基线相比：cost **218805**
（−7280，−3.22%）、cycles **1028159**（−31677，−2.99%）、area **57689**
（−1798，−3.02%），覆盖保持 **141/146**；对 oracle 倍率 2.177× / 12.167× /
2.448×。同题逐槽为 cost 16 改善 / 0 退化 / 125 持平、cycles 9 / 0 / 132、
area 11 / 0 / 130，受益题并集 **20**；两次 jobs=2 复跑除 `cb6c9528` cost
2455/2465 的 10 金抖动外逐槽一致。所有数字都是各目标自身槽，不跨槽取最小。

### 70.4 同期试过并撤回的方向（都带 Release 语料 A/B）

| 方向 | 结果 | 处理 |
|---|---|---|
| `OPUSOPTIMA_PIPELINE_ARCHITECTURE=on` | 1 题 cost 退化，0 改善 | 保持默认关 |
| `OPUSOPTIMA_MOLECULE_ARCHITECTURE=on` | 1 题 cost 退化，0 改善 | 保持默认关 |
| `OPUSOPTIMA_SHARED_FEED_RESOURCE=on` | 逐槽无变化 | 不启用 |
| `OPUSOPTIMA_ROW_COMPONENT=on` | 1 题 cost 退化 | 不启用 |
| `OPUSOPTIMA_LD_FACTORY_CLEARANCE=0` | `e42`/`deec` 丢解 | 不启用 |
| `OPUSOPTIMA_LD_FACTORY_START=-45` / `-25` | 多种子变差；`-25` 丢 2 题 | 不作为默认 |
| compact composer 二次意见 | 423 槽无变化 | 已删除，不占 gate 时间 |
| wheel/triplex/unification/dispersion 二次意见 | cost 净 −15 但 `a94b5296` +5、area +1 | 已回退 |
| independent-outputs packed 组合 | 10 道多输出题逐槽无变化 | 已回退 |
| repeating-output 二次意见 | 11 道 Repeat 题逐槽无变化（首族已产出同形机器） | 已回退 |
| independent-outputs closer 组合 | `e42` 三槽改善、`deec` cost/area 改善 | 保留 |

结论没有改变 §52.6/§54.4 的判断：Release + 额外组合拿到的是**现有族的残余预算红利**
（约 3%），真正 12× 的周期缺口仍然只能靠“化学链 → 局部臂流水线”的新物化器来消。
把 Debug 时代的预算结论当成几何结论，是这一轮之前最大的误判来源。

### 70.5 验证与边界

- 全量 Debug 测试（`EnableExtraPortfolio` 门控版本，不含 independent-outputs closer）：
  **1095 项 / 132 suites，1090 passed、5 skipped、0 failed，exit 0，892.0s**。
  五项 skip 仍是未注册外部案例的 `CandidateArchive.Registered*`、`CompactLayout.RegisteredLayoutProbe`、
  `LocalCompaction.RegisteredP0CompactionCandidates`、`RowRelay.RegisteredRowCompilation`。
- 加入 independent-outputs closer 后的全量测试：**1095 项，1090 passed、5 skipped、0 failed，
  exit 0，892.8s**。
- 加入 `IndependentOutputsCloserIsOptInAndBounded` 后的最终全量测试：**1096 项 / 132 suites，
  1091 passed、5 skipped、0 failed，exit 0，917.0s**。两个 opt-in 契约测试
  （`ExtraPortfolioSecondOpinionIsOptInAndBounded`、`IndependentOutputsCloserIsOptInAndBounded`）
  在同一二进制上定向通过（46.0s，2/2）。
- 库 API 的默认行为没有改变：`EnableExtraPortfolio` 未开启时，首解保留、接受额度、
  提交次数与 §67 的契约测试逐项通过；额外候选只在 CLI 的 20 秒有界预算内运行。
- 未提交任何改动；`.clangd` 的用户修改与未知 puzzle 删除保持隔离。Release 与 Debug
  两个构建目录都保留，审计使用的是 Release 的 `OpusOptima` 与 `libOpusOptimaLib.so`。
- 限制：`cb6c9528` 的 cost 在两次 Release 审计中为 2455 / 2465，是已记录的 10 金时序
  抖动；5 道缺失题仍是原缺口；`instructions=0` 的静态槽不计作真实机械胜利。

## §71 上游 Standard 整体移植：从 12.5× 到追平/反超（2026-11-15）

用户要求“实在不行就抄他的做法”，于是不再继续调固定族，直接把
`gtw123/OpusSolver@fdedd3f0` 的 Standard solver（MIT）整体移植到
`src/Solvers/Standard/`：分子/对象模型、ProgramWriter/ProgramBuilder、配方 ILP
（HiGHS 替代 lp_solve）、ElementPipeline 与 dry run、输入区/拆解器、AtomConveyor、
元素发生器、AtomBuffer、双排臂 UniversalAssembler、产品传送带和 CostOptimizer。
完整说明见 [UPSTREAM_STANDARD_PORT_20261115.md](UPSTREAM_STANDARD_PORT_20261115.md)。

结果（两次 `jobs=2` 审计逐槽一致）：

| 指标 | rail-prefix 基线 | 默认 CLI（移植接入） | 变化 | 对 oracle |
|---|---:|---:|---:|---:|
| 覆盖 | 141/146 | **146/146** | +5 | — |
| cost | 226085 | **96090** | −57.50% | **0.9164×** |
| cycles | 1059836 | **69759** | −93.42% | **0.7792×** |
| area | 59487 | **21323** | −64.15% | **0.8631×** |

专用装配器（SingleMono/Mono/Linear）接入后，纯移植体（不接旧 portfolio）单独跑
语料为 146/146，对 oracle 总量 1.089× / 1.063× / 1.058×；只取机械
（`instructions>0`）槽时，接入后的总量是 1.037× / 1.039× / 1.024×。
移植体只在 CLI 的 `EnableExtraPortfolio` 路径中启用，库 API 的首解/配额契约不变。
剩余 2.4–3.9% 机械差距对应尚未移植的 Hex3 装配器、Bent/Triangle 拆解器和
`SolutionParameterRegistry` 的 `--optimize` 枚举。
