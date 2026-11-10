# OpusSolver 架构复研与能力差距分析（2026-11-10）

原生权威：`legacy-gate`（`omsim-a88d12d8+production-replay-v1`）。
上游源码：`gtw123/OpusSolver@fdedd3f0`（本地 `/tmp/opusup`）。
数据：`result/opussolver-oracle-20261110`（146 题 × 3 目标 = 438 参考解）、
`result/solver-generalization-track-envelope-default-integrated-20261109/inputs`（146 个同源输入）。

---

## 1. 上游架构再确认（五层）

| 层 | 关键类型 | 职责 | 证据 |
|---|---|---|---|
| 布局 | `SolverComponent` / `AtomGenerator.Parent` | 组件图；子组件放在父组件 `OutputPosition` 上，几何由连接关系推导 | `Standard/SolutionBuilder.CreateAtomGenerators`：`atomGenerator.Transform.Position = parentGenerator.AtomGenerator.OutputPosition` |
| 流水线 | `ElementPipeline`、`ElementGenerator`、`ElementBuffer` | 输入→纯化→投射→盐→生灭→输出 的链式生成器，每个生成器自带臂与缓冲 | `Solver/ElementPipeline.cs` |
| 输入 | `SimpleInputArea`、`ComplexInputArea`、`LinearDisassembler`、`AtomConveyor` | 源旁就地拆解，短轨/就地旋转，逐原子交给共享线；每输入独立臂 | `Standard/Input/*` |
| 输出 | `UniversalAssembler` + `AssemblyArea` + `BondProgrammer` + `ProductConveyor` | 上下臂阵列 + 序列化 bonder 工位 + 独立产物输送 | `Standard/Output/Universal/*` |
| 时间 | `ProgramWriter`（片段）+ `ProgramBuilder`（排程） | 组件只写“相对时间片段”；`ProgramBuilder` 逆序合并，逐臂计算最小位移使各臂指令不重叠 | `Solver/ProgramWriter.cs` / `Solver/ProgramBuilder.cs` |

三条与被测机器直接相关的实现细节：

1. `ProgramBuilder.CalculateTimeShift` 只检查**同一臂**上是否重叠，因此不同臂之间可以任意重叠——这是“免费并行”的来源。
2. `Instruction.Reset` 的时长按“展开时间”计算（`CalculateResetTime`：`|Δextension| + (3-|Δrotation-3|) + |Δposition|`），配合 `AdjustTime(-1)` 让复位与后续动作重叠；`Grab+Reset` 是原子交接惯用法。
3. 上游自己的模型也把伸缩上限写死为 **3**（`Math.Min(extension + 1, 3)`），与我们 2026-11-10 实测的“活塞最大 3 格”一致。
4. 指令字母表（`IO/SolutionWriter.GetInstructionCode`）：`A/a` 前进/后退（活塞轨道），`E/e` 伸缩，`R/r` 旋转，`p/P` 枢转，`G/g` 抓/放，`X` 复位，**`C` = Repeat（零时间，磁带压缩）**，`O` = PeriodOverride。据此才能正确解读参考机。

---

## 2. 量化对比（同输入、同权威）

### 2.1 P088（`WAKEFULNESS POTION`）

| 指标 | 我们（unified） | 参考 | 比值 |
|---|---:|---:|---:|
| cycles | 5082 | 1205 | 4.2× |
| cost / area | 810 / 201 | 755 / 230 | 0.93× / 1.14×（我们更好） |
| 臂数 | 11（全 piston） | 18（12 arm1 + 6 piston） | 1.6× |
| 最长臂磁带 | 855 tick | 205 tick | 4.2× |
| 全部臂磁带之和 | 9405 | 3168 | 3.0× |
| 动作总数（文件） | 1965 | 714 | 2.8× |
| `p/P` 枢转 | 0 | 6 | — |
| 取料动作/原子 | ~50（feeder 849 / 17 原子） | ~3.4（输入臂合计 57 / 17 原子） | **~15×** |

利用率的粗算：`Σ磁带 / 周期` = 9405/847 ≈ 11.1（=臂数），参考 3168/200 ≈ 15.8（<18）。
**两台机器都是“满载”的**：差距不是“我们在等待”，而是“每根臂在同样的时间里做了几倍的事”。

### 2.2 `pipeline/cheap-arms`（33 题，30 题产出解）

| 指标（中位数） | 我们 | 参考 | 比值 |
|---|---:|---:|---:|
| 每产物周期（cycles/6） | 2781 | 130 | **21×** |
| 活跃臂数 | 12.5 | 21.5 | 0.58× |
| 每臂磁带 tick | 11604 | 200 | **58×** |
| 最长磁带 tick | 9802 | 252 | 39× |
| 总动作数 | 15286 | 714 | **21×** |

### 2.3 `universal`（抽 13 题）

| 指标（中位数） | 我们 | 参考 | 比值 |
|---|---:|---:|---:|
| 每产物周期 | 1050 | 100 | 10.5× |
| cost | 1730 | 585 | 3.0× |
| 臂构成 | 21.5 piston + 11.8 track + **1 arm1** | **12 arm1** + 5 piston | — |
| 动作构成 | `A/a`(轨道移动) 80% | `A/a`(arm1 伸缩) 53%，`E/e` 29%，`X` 3% | — |
| 枢转 `p/P` | 0 | 0（该类不用，其它类用） | — |

> 字母语义按臂类型解码：同一字母在 `arm1` 与 `piston` 上含义不同（见 `SolutionWriter.GetInstructionCode`
> 与 `Puzzle/Instruction.cs`）。我们机器里 `A/a` 全部是**活塞轨道移动**；参考机里绝大部分 `A/a` 是
> **arm1 的伸缩**，所以“动作构成”一列只能作为数量级参考，不能逐字对比。

三类的结论一致：**我们在“单位原子动作数”和“臂数量/成本结构”两个维度同时落后，而不是调度并行度落后。**

---

## 3. 结论：我们到底缺什么“通用能力”

按 ROI 从高到低：

### C1. 紧凑传递臂网络（arm1 链 + 抓取-复位）——最直接的原因
参考机 12–21 根臂里大半是 `arm1`（成本 20，纯旋转+伸缩），原子在相邻臂之间以 2–5 个动作交接；我们行机里 `arm1` 数量≈0，一切靠 `piston`（成本 40）在长轨道上滑动，单次取料 50 个动作。
**代价**：动作数 ×10~15，cost ×2~3，面积也被轨道吃掉。
**缺的能力**：用 `arm1` 做传递、用 `G…X`（抓取-复位）交接原子、必要时用 `p/P` 枢转——即“传送带网络”的生成器。

### C2. 组件图 + 自动布局
上游几何由组件连接推导（`Transform.Position = parent.OutputPosition`），所以每个组件都能紧贴上下游、轨道极短；我们是按 puzzle family 手写一整套大几何，输入被推到远处，长轨不可避免。
**缺的能力**：组件抽象（带输入/输出接点）+ 由连接图求坐标的放置器。

### C3. 相对时间片段 + 逐臂排程（ProgramBuilder 等价物）
上游组件只写“相对片段”，最终由 `ProgramBuilder` 逆序合并、逐臂求最小位移。它既是 C1 的前提（短磁带必须能交错），也让跨产物流水（上一个产物还在收尾、下一个原子的取料已开始）成为可能。
**注意**：单看 P088，我们的臂利用率已接近 100%，所以仅换调度器不会带来 4×；但没有它，C1 的短磁带无法编排。

### C4. 元素流水线（ElementPipeline + ElementBuffer）
化学阶段（纯化/投射/钙化/生灭/统一/无色）在上游是独立组件，各自带臂和缓冲，天然并行；我们的 SupplyPlan 把化学全部塞进共享取料器/输送器，阶段之间串行。
**证据**：`universal` 类里我们 5.3 个 input、6.2 个 unbonder、1.5 个 disposal 都挂在同一条 feeder 上。

### C5. 架构级多方案搜索
上游对同一题会尝试不同 `SolutionType` × 参数（`SolutionParameterRegistry`）并保留每个目标最好的机器，甚至另有 `LowCost/*` 求解器专门做 cost；我们是在**一个**架构上做参数/局部压缩。当前 146 题里我们三槽同机的比例很高，说明缺少“换架构换目标”的能力。

---

## 4. 为什么不能靠调参补上

- gap/stride/spacing、目标旋转、轨道尾裁、piston→arm1、删除 bonder/unbonder、单上臂 shuttle、lower 预定位，都已 native 实测并记录为无效或回退。
- 本轮的本地取料实验（`OPUSOPTIMA_DIRECT_INPUT`）证明：**只把取料变便宜，瓶颈立刻转移到行装配的产物转移**——因为行本身仍是一条串行长磁带。
- 数据也支持这一点：`Σ磁带/周期` 已接近 1.0×臂数，说明机器已被现有结构“塞满”，没有可回收的空闲。

---

## 5. 建议的下一步（按增量成本排序）

1. **先做“arm1 传递网络”的最小闭环**（C1，单点收益最大）：
   在现有行机左侧，用 1 根 `arm1` 取代 intake 活塞的一段行程（取料→交接），保持 arm1 只在 2–3 格内工作；
   验收：`just-salt` 与 P088 变体周期再降 ≥20%，cost 不升。
2. **再做“相对片段 + 逐臂排程”内核**（C3）：
   把 `write()` 从“推进全局游标”改为“写入当前臂的相对片段”，由排程器在各臂之间只做**非重叠**约束；
   这是后续所有短磁带机器的前置条件。
3. **最后做“组件图 + 自动布局”**（C2）与**流水线化学**（C4）：
   先只覆盖 narrow ordinary row + 单输入/双输入线性源，保持现有 machine 作为 fallback。
4. 每步都必须：native `legacy-gate` 通过、不丢已有 55 个胜场、20 s 预算内、三槽独立。

---

## 6. 复现命令

```sh
# 我方与参考解结构画像（本报告数据）
python3 - <<'PY'
import sys; sys.path.insert(0,'/tmp')
from psol_lib import parse
for p in ('<our>.solution', 'result/opussolver-reference-20261110/P088-Standard.solution'):
    _,o = parse(p)
    arms=[x for x in o if x['type'] in {'piston','arm1','arm2','arm3','arm6'}]
    print(p, len(arms), max(x['instr'][-1][1]+1 for x in arms if x['instr']),
          sum(x['instr'][-1][1]+1 for x in arms if x['instr']))
PY
```
