# OpusSolver Standard 移植：从 12.5× 到追平/反超（2026-11-15）

## 结论

本轮把上游 `gtw123/OpusSolver@fdedd3f01a238b941ceba8bace7ea35cbd3b8c41`
的 **Standard solver 整体移植**到 `src/Solvers/Standard/`（MIT，保留出处），并在
CLI 的 `EnableExtraPortfolio` 路径中作为第一个候选接入。结果是语料指标的数量级
变化，而不是又一次局部调参：

| 指标 | 移植前基线（rail-prefix） | 移植后默认 CLI | 变化 | 对 oracle 总量 |
|---|---:|---:|---:|---:|
| 覆盖 | 141/146 | **146/146** | +5 | — |
| cost | 226,085 | **96,090** | **−57.50%** | **0.9164×** |
| cycles | 1,059,836 | **69,759** | **−93.42%** | **0.7792×** |
| area | 59,487 | **21,323** | **−64.15%** | **0.8631×** |

两次独立的 `jobs=2` 全语料审计（`result/upstream-standard-specialized-20261116/`
与 `result/upstream-standard-specialized2-20261116/`）**438 个槽值逐字节一致**。
作为纯移植体（不接旧 portfolio、不做本地优化）单独跑语料：**146/146**，对 oracle
总量 cost **1.089×**、cycles **1.063×**、area **1.058×**；也就是说，单靠这份移植
已经把原先 2.25× / 12.54× / 2.52× 的差距压到个位数百分比。

## 1. 移植了什么

上游是 C#/.NET。移植保持它的坐标、旋转、元素顺序和程序语义，模块几何逐字对应：

| 上游 | 移植文件 | 内容 |
|---|---|---|
| `Puzzle/Molecule.cs`, `Solution/GameObject.cs`, `Arm`, `Glyph`, `Track` | `StandardModel.hpp/.cpp` | `Vec2=(X,Y)=(q,r)`、六方向 0..5、`Rot` 逆时针 60°、元素逻辑序（Salt, Air, Fire, Water, Earth, Quick, Lead→Gold, Mors, Vitae, Quint, Repeat）、分子归一化/旋转/Repeat 展开、对象父子树 |
| `Solver/ProgramWriter.cs`, `Solver/ProgramBuilder.cs`, `Solution/Program.cs` | `StandardProgram.hpp/.cpp` | 每臂 fragment、最小时间平移、reset 时间计算、Repeat/PeriodOverride |
| `Solver/Recipe*.cs` | `StandardRecipe.hpp/.cpp` | 反应集合、使用额度、整数线性规划（用 vendored HiGHS 替代 lp_solve，scale 1..6 + cardinal/全放宽） |
| `Solver/ElementPipeline.cs`, `ElementGenerators/*` | `StandardPipeline.hpp/.cpp` | dry run 的 `Consume/Generate/PassThrough`、Input/Output、MultiStack/SingleStack buffer、Projector/Purifier/Animismus/Quintessence/Disperser/VanBerlo 逻辑 |
| `Solver/Standard/Input/*` | `StandardInputModules.cpp` | Simple/Complex input area、Linear/Universal/Mono 拆解器、AtomConveyor |
| `Solver/Standard/*Generator.cs`, `AtomBuffer.cs` | `StandardGenerators.cpp` | Salt、Salt-NoPass、Projector、Purifier、MorsVitae、Quintessence、Disperser、VanBerlo、AtomBuffer 的几何与磁带 |
| `Solver/Standard/Output/Universal/*`, `ProductConveyor`, `SimpleOutputArea` | `StandardOutputModules.cpp` | 上下双排臂装配区、bonder 阵列、逐行 BondProgrammer、产品传送带 |
| `Output/MoleculeAssemblerFactory`, `SingleMonoatomic/Monoatomic/LinearAssembler` | `StandardOutputModules.cpp` | 按产物形状选择专用装配器（单原子/多单原子/直线链），其余落回 Universal；Hex3 仍未移植 |
| `Solver/CostOptimizer.cs` | `StandardSolver.cpp` | 删除未用臂、无伸缩活塞转臂、裁掉未用轨道 |
| `Solver/PuzzleSolver.cs` + `SolutionGenerator.cs` | `StandardSolver.cpp` | 端到端生成、fragment 合并、转成 `io::SolutionData` |

入口：`standard::CompileUpstreamStandard(puzzle)`；独立 CLI 名为
`-s upstream-standard`。

## 2. 接入方式（CLI 额外组合，库 API 不变）

`AssemblySolver::SolveCandidates` 在 `EnableExtraPortfolio` 打开时（`main.cpp` 打开）：

1. 先编译并提交 `upstream-standard`，记为 `firstAccepted`；
2. 立即跑一次既有的首解后结构 portfolio（静态 embedding/composition/rigid carrier 等）
   与 life-death 二次意见；
3. `keep` 对 upstream 首解返回 `false`，于是固定族链继续跑，把旧族的更便宜机器
   （例如 `fe57c5d3` cost 925→335、`b2c42099` 870→320）也送进归档；
4. CLI 的接受额度在 `maxAccepted` 之外多给 4 个位置，避免 upstream 与旧族互相挤掉；
5. 归档仍按 cost/cycles/area 各自取最小，最终发布 `archive->First()`（upstream）。

库 API（未调用 `EnableExtraPortfolio`）完全保持原契约：首解保留、接受额度属于
调用方、提交次数上限不变；因此 §67 的配额/首解测试不需要改写。

## 3. 逐槽结果

`result/upstream-standard-hybrid-20261115/gap.json`，146 题、每指标自身槽：

| 指标 | 胜 | 平 | 负 | p50 | p90 | max |
|---|---:|---:|---:|---:|---:|---:|
| cost | 65 | 52 | 29 | 1.00 | 1.27 | 2.36 |
| cycles | 59 | 52 | 35 | 1.00 | 1.16 | 1.75 |
| area | 64 | 49 | 33 | 1.00 | 1.17 | 1.85 |

三道全胜 54 题、三道全负降至 **25** 题。总量反超 oracle 的来源要诚实拆开：

- `instructions=0` 的静态机器只占 cost 5.4%、cycles 0.4%、area 2.0%；
- **只取 `instructions>0` 的机械槽**，对同题 oracle 的总量比是
  cost **1.0373×**、cycles **1.0385×**、area **1.0242×**（n=98/95/97）。
  即机械部分已进入 oracle 的 **2.4–3.9%** 以内，剩余总量优势来自静态合法解与本地裁轨。

## 4. 移植中修掉的真实问题

- `UniversalAssembler::GetConnectedAtoms` 的 BFS 忘记在入队时标记 `seen`，在环上
  无限入队，1dc60983 在输出第 7 个原子时 `bad_alloc`。修复后 146/146 全部完成。
- 早期版本把 `Molecule::RotateBy` 的成员函数与自由函数同名，以及 Purifier 的
  `Purifier&` 无法在 C# 式重绑定处重新赋值；均已按上游语义修正。
- 上游 `AtomConveyor`/`ComplexInputArea` 的输出 access point、装配区上下臂、
  `AtomicBuffer` 的 wait 语义都按源码逐行对齐，生成的机器第一次过 native gate。

### 验证

- 纯移植体：146/146 全部经 CLI 的 `legacy-gate` 验证后写出 objectives。
- 接入后的全量 Debug 测试（`tests/unit_test/AssemblySolverTest.cpp` 含
  `UpstreamStandardFamilyIsCliOptIn`、`ExtraPortfolioSecondOpinionIsOptInAndBounded`、
  `IndependentOutputsCloserIsOptInAndBounded`）：**1097 项 / 132 suites，
  1092 passed、5 skipped、0 failed，exit 0，917.2s**（最终源码；
  专用装配器版本的另一次完整运行为 927.7s，同样 0 failed）。
- 两次 `jobs=2` 审计逐槽一致，438/438 槽 manifest authority 均为 `legacy-gate`、
  `status=passed`、engine `omsim-a88d12d8+production-replay-v1`。
- 曾尝试在 upstream 候选之前提交 recipe scale 1..6、放宽配方与反序构建的变体；
  由于它们占满接受额度（`canConstruct` 要求 `acceptedCount < 3`），静态 portfolio
  与旧成本族不再运行，总量反而退化到 cost **1.036×**、cycles **1.046×**、
  area **1.017×**。已按“无收益即回退”撤销，保留单候选 + portfolio + 旧链的混合。

## 5. 还没移植的上游部分（下一步的确定性收益）

1. **Hex3Assembler**：中心原子 + 三条支链的产物形状；当前回退到
   `UniversalAssembler`。这类题在语料里不多，但仍有可测的 cost/area 余量。
2. **NonLinear3Bent / NonLinear3Triangle 拆解器**：当前非直线分子一律走
   `UniversalDisassembler`，个别形状的机械 cost/cycles 仍有差距。
3. **`SolutionParameterRegistry` + `--optimize` 枚举**：上游会枚举配方 scale、
   元素顺序、臂长、首个/其它产物顺序等并并行验证；当前只取第一个可行配方。
   这是机械差距里最可能被完全消掉的一块。
4. **Production 题**：上游本身不支持，移植也直接拒绝；与 oracle 语料一致。

专用装配器（`SingleMonoatomicAssembler`、`MonoatomicAssembler`、
`LinearAssembler`）已按上游 `MoleculeAssemblerFactory` 接入，使纯移植体的
总量从 1.105/1.069/1.076 降到 1.089/1.063/1.058，并让 96/86/92 个槽与 oracle
逐值相同。

## 6. 复现

```sh
cmake -S . -B artifacts/build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build artifacts/build-release -j 4

# 纯移植体
artifacts/build-release/OpusOptima -s upstream-standard -p <puzzle> -o /tmp/x.solution

# 默认 CLI（assembly + EnableExtraPortfolio）
OPUSOPTIMA_BIN=artifacts/build-release/OpusOptima \
  /usr/bin/python3 tools/audit_gap.py --jobs 2 \
  --out result/upstream-standard-hybrid-20261115 \
  --json result/upstream-standard-hybrid-20261115/gap.json
```

证据：最终 `result/upstream-standard-refactor-20261116/`（与 specialized 审计逐槽
相同）、`result/upstream-standard-specialized-20261116/` 与
`upstream-standard-specialized2-20261116/`（两次一致的验收），以及
`result/upstream-standard-hybrid-20261115/`、`upstream-standard-integrated-20261115/`
（演进对照）和 `result/upstream-standard-standalone-20261115/slots.json`（纯移植体）。
所有解法都由 CLI 的 `legacy-gate` native 验证后才写入 objectives；审计的 438 槽均有
`passed / legacy-gate / legacy-native` 记录。

## 7. 出处与许可

移植自 `gtw123/OpusSolver`，commit
`fdedd3f01a238b941ceba8bace7ea35cbd3b8c41`，MIT License。每个移植文件头注明
来源；未复制上游的 C# 工程或运行时，只复制了算法与几何常量到本仓库的 C++ 实现。
