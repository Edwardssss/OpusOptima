# 上游 Standard：`--optimize` 枚举、配方子集与特殊形状移植（2026-11-16）

## 结论

本轮把上游 `--optimize` 的确定性部分接进 CLI 的 upstream 候选，并补上
`Hex3Assembler`、`NonLinear3Bent`、`NonLinear3Triangle` 三种形状。146 题语料
保持 **146/146**，机械槽（`instructions>0`）总量首次三项都低于 oracle：

| 指标 | 移植前基线 | 本轮前 | 本轮后 | 对 oracle |
|---|---:|---:|---:|---:|
| cost | 226,085 | 96,090 | **89,825** | **0.8567×** |
| cycles | 1,059,836 | 69,759 | **66,429** | **0.7420×** |
| area | 59,487 | 21,323 | **20,285** | **0.8211×** |

146/146 覆盖，`438/438` 槽由 `legacy-gate` 验证；机械槽三项也全部低于
oracle（见 §3.3/§3.4）。最终证据见 §3.4。

## 1. `--optimize`：配方、参数与配方子集

上游 `PuzzleSolver.Solve(generateMultipleSolutions)` 会：

1. `RecipeGenerator.GenerateRecipes(true)` 枚举每个可行的 product scale、cardinal
   放宽和全放宽配方；
2. 再用 `IncludeUneededReactions=true` 的生成器补一轮；
3. 对每个配方取 `SolutionGenerator.GetAvailableParameters()`；Standard 只有
   `ReverseProductBuildOrder`，且仅对多产物题存在；
4. 每个配方 × 参数生成一台机器，由 Runner 按 cost/cycles/area 各取最优。

移植后：

- `RecipeBuilder::GenerateRecipes(true)` 现在真的返回 **所有** 可行 scale/放宽配方，
  不再在第一个可行 scale 处提前返回；
- `RecipeGenerator` 增加 `includeUnneededReactions`，并按上游在 Cardinal/Salt
  阶段补 VanBerlo/Calcification 反应；
- `CompileUpstreamStandardVariants` 按上游顺序编译配方 × build order，并按
  **序列化字节** 去重；
- 新增 **配方子集** 枚举：对多输入题，LP 的第一个最优解常用多余输入（例如
  `f5922439` 用 2 个输入，而 oracle 只用 1 个），把 reagent 集合限制到
  size 1..min(3, N-1) 的子集再解 LP，能找回更便宜的可行配方；
- 这些候选作为 **bonus archive entries** 提交：消耗 `MaxCandidateSubmissions`
  的提交额度，但 **不** 增加 accepted 计数，因此结构 portfolio 和固定族链的
  accepted 容量与之前完全相同。

选择策略：

| 题型 | 默认编译的配方集合 | 上限 |
|---|---|---:|
| 多产物 | 前 3 个 scale + 前 2 个子集配方，各两种 build order | 8 |
| 单产物 | 默认、recipe 1/2/7 + 前 2 个子集配方 | 6 |

`OPUSOPTIMA_STANDARD_OPTIMIZE=off` 关闭整个 bonus 枚举；
`OPUSOPTIMA_STANDARD_SUBSET=off` 只关闭配方子集（保留上游配方枚举）。

## 2. 特殊形状

### 2.1 NonLinear3Bent / NonLinear3Triangle

按上游逐行移植：

- Bent：3 个原子、`Size=3`，三原子呈折线；3 步循环的 grab/middle/output 臂；
- Triangle：3 个原子、`Size=2`，按两条键/三条键分成两种 3 步循环；
- 工厂选择顺序与上游一致：单原子 → `Height==1` 线性 → Bent → Triangle →
  Universal；
- 元素输入顺序改用 `GetElementInputOrder`，并在计算顺序前调用
  `PrepareReagentMolecule` 旋转到对应朝向；
- 线性 3 原子分子必须先走 `Height==1` 分支（这是本轮发现并修掉的覆盖回退：
  `f7ea45f6`/`a7d3c0bf`/`5911f6b1` 曾被误判为 Bent 而拒绝）。

资产题 A/B（同一 CLI，`legacy-gate` 验证）：

| 题目 | Universal（`=off`） | 专用拆解器 |
|---|---|---|
| `nonlinear-3 bent 1` | 140 / 420 / 75 | **91 / 185 / 45** |
| `nonlinear-3 triangle 1` | 121 / 460 / 78 | **113 / 175 / 49** |

### 2.2 Hex3Assembler

移植 `Output/Hex3` 的 `AssemblyArea`、`CenterAtomMoleculeBuilder`、
`MissingCenterAtomMoleculeBuilder` 和输出臂布局：

- 产物元素顺序取 builder 的 `GetElementsInBuildOrder`；
- 工厂在“非全单原子、非全线性、无 triplex、全部 `IsProductCompatible`”时选择
  Hex3；
- 资产题 A/B：

| 题目 | Universal（`=off`） | Hex3 |
|---|---|---|
| `hex with and without center 1` | 713 / 460 / 94 | **302 / 180 / 63** |
| `multiple hex both` | 1187 / 475 / 107 | **514 / 200 / 73** |

`OPUSOPTIMA_STANDARD_HEX3=off` 关闭 Hex3；
`OPUSOPTIMA_STANDARD_SPECIAL_DISASSEMBLERS=off` 关闭 Bent/Triangle。

## 3. 测量

所有数字来自 `tools/audit_gap.py --jobs 2`，每槽由 `legacy-gate` 验证。
最终证据：`result/upstream-standard-final-clean-20261116/`（`--jobs 2`，
与 `result/upstream-standard-final-clean2-20261116/` 逐槽一致，0/438 diff；
也与加入 early-exit 前的 `upstream-standard-subset-20261116/` 逐槽一致）。

### 3.1 阶段对照

| 阶段 | cost | cycles | area | 对 oracle cost/cycles/area |
|---|---:|---:|---:|---|
| 移植后基线 | 96,090 | 69,759 | 21,323 | 0.9164 / 0.7792 / 0.8631 |
| + 配方/参数枚举 | 95,705 | 68,955 | 21,219 | 0.9127 / 0.7702 / 0.8589 |
| + Hex3/三角拆解 | 91,155 | 67,020 | 20,638 | 0.8693 / 0.7486 / 0.8354 |
| + 单产物配方集 | 90,605 | 66,657 | 20,411 | 0.8641 / 0.7446 / 0.8262 |
| + 配方子集 | 89,825 | 66,429 | 20,285 | 0.8567 / 0.7420 / 0.8211 |

### 3.2 逐槽（子集枚举前）

| 指标 | 胜 | 平 | 负 | p50 | p90 | max |
|---|---:|---:|---:|---:|---:|---:|
| cost | 68 | 75 | 3 | 1.00 | 1.00 | 1.57 |
| cycles | 63 | 79 | 4 | 1.00 | 1.00 | 1.05 |
| area | 69 | 72 | 5 | 1.00 | 1.00 | 1.38 |

三指标全胜 55 题，全负 1 题（`e42f1f11`，随后由配方子集修到
1030/801/201，oracle 1030/791/201）。

### 3.3 机械槽与类别（子集枚举前）

- 机械槽（`instructions>0`）：cost **0.9747×**、cycles **0.9921×**、
  area **0.9794×**（n=98/95/97）。
- 类别总量：

| 类别 | n | cost | cycles | area |
|---|---:|---:|---:|---:|
| linear/mono | 40 | 0.5784 | 0.4369 | 0.5131 |
| pipeline/cheap-arms | 33 | 0.9412 | 0.9627 | 0.9360 |
| pipeline/heavy | 7 | 0.8310 | 0.3914 | 0.6912 |
| universal | 66 | 0.8867 | 0.7055 | 0.8516 |

### 3.4 配方子集审计（最终）

`result/upstream-standard-final-clean-20261116/gap.json`，`--jobs 2`，146 题
（两次 clean 审计逐槽一致）：

| 指标 | 我们 | oracle | 比值 | 胜/平/负 | p50 | p90 | max |
|---|---:|---:|---:|---:|---:|---:|---:|
| cost | 89,825 | 104,855 | **0.8567** | 75/70/1 | 0.997 | 1.000 | 1.008 |
| cycles | 66,429 | 89,524 | **0.7420** | 65/77/4 | 1.000 | 1.000 | 1.034 |
| area | 20,285 | 24,705 | **0.8211** | 70/73/3 | 1.000 | 1.000 | 1.011 |

- 三指标全胜 **55** 题，三指标全负 **0** 题；
- 机械槽（`instructions>0`）：cost **0.9657×**、cycles **0.9887×**、
  area **0.9733×**（n=98/95/97）；
- 类别总量：

| 类别 | n | cost | cycles | area |
|---|---:|---:|---:|---:|
| linear/mono | 40 | 0.5784 | 0.4369 | 0.5131 |
| pipeline/cheap-arms | 33 | 0.9283 | 0.9624 | 0.9320 |
| pipeline/heavy | 7 | 0.8310 | 0.3914 | 0.6912 |
| universal | 66 | 0.8794 | 0.7007 | 0.8438 |

- 最差单题：cost `cfba9c05` 1.008×，cycles `1b47844e` 1.034× / `e42f1f11`
  1.013×，area `2637c0fe` 1.011×；其余全部 ≤1.01 或持平。
- 两个原先的最大外点：`f5922439` 480/585/161 → **305/608/117**（oracle
  305/608/117，cycles 还快 23），`e42f1f11` 1205/858/236 →
  **1030/801/201**（oracle 1030/791/201）。


### 3.5 按原计划 4–7 项的对应结果

- **4）pipeline/cheap-arms 成本**：33 题总量 cost **0.9283×**、cycles
  **0.9624×**、area **0.9320×**，三项都已低于 oracle；剩余只是逐题小负。
- **5）universal cycles**：66 题总量 cost **0.8794×**、cycles **0.7007×**、
  area **0.8438×**；cycles 最差只剩 `e42f1f11` 1.013×、`160f11ef` 1.001×，
  其余全部持平或更好。
- **6）单解三指标 Pareto**：任一单槽的三项总量都低于 oracle：
  cost 槽 0.8567/0.9466/0.8308，cycles 槽 0.8940/0.7420/0.8435，
  area 槽 0.8636/0.7943/0.8211。逐题看，71/146 题存在一个槽同时三指标
  不劣于 oracle 且至少一项严格更好；如果赛制要求每题只提交一个解，按目标
  选槽即可保持总量全胜。
- **7）更大语料**：reference corpus 之外的导入题覆盖抽样见 §3.6。

### 3.6 更大语料抽样

从 `gtw123/OpusSolver@fdedd3f` 的 `test/puzzles`（2401 个文件，2378 个不同题）
中按固定步长取 **120 道非 reference 语料**，在 Release CLI 默认配置下运行，
每次 `-p ... -o ...`，30 s 超时：

- **120/120 产出并通过验证（100%）**；
- 与历史 `result/opussolver-fdedd3f-full-20261028/coverage.json` 的同文件结果
  对照：老 solver 通过 113/120，本轮 **improved 7、regressed 0**；
- 历史全语料（2401 题）assembly 覆盖为 2273/2401（94.67%），本轮抽样的 100%
  说明上游 Standard 候选补上了大部分 “assembly strategies exhausted” 题。

这是覆盖抽样，不是对全部 2401 题重新跑分；完整导入语料的成本审计仍建议在
独立批次中做（见 §4 的同一命令，把 `OPUSOPTIMA_CORPUS` 指向导入目录）。

### 3.7 测试与确定性

- 全量 Debug 测试：**1103 tests / 133 suites，1098 passed、5 skipped、
  0 failed，exit 0，1067.5 s**（新增 `StandardVariantTest` 6 项）。
- 两次独立的最终二进制 clean 审计逐槽一致（**0/438 diff**），并且与加入
  early-exit 前的子集审计也逐槽一致；每个 `.objectives.json` 的 **438/438**
  槽都是 `status=passed`、`authority=legacy-gate`、
  `engine=omsim-a88d12d8+production-replay-v1`。
## 4. 复现

```sh
cmake -S . -B artifacts/build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build artifacts/build-release -j 4

OPUSOPTIMA_BIN=artifacts/build-release/OpusOptima \
  /usr/bin/python3 tools/audit_gap.py --jobs 2 \
  --out result/upstream-standard-subset-20261116 \
  --json result/upstream-standard-subset-20261116/gap.json
```

单题 A/B：

```sh
# 关闭配方 bonus 枚举
OPUSOPTIMA_STANDARD_OPTIMIZE=off ...
# 关闭配方子集
OPUSOPTIMA_STANDARD_SUBSET=off ...
# 关闭 Hex3 / Bent+Triangle
OPUSOPTIMA_STANDARD_HEX3=off ...
OPUSOPTIMA_STANDARD_SPECIAL_DISASSEMBLERS=off ...
```
