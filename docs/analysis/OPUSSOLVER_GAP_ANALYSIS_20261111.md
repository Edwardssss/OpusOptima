# 对比性能测试与针对性能力分析（2026-11-11）

原生权威：`legacy-gate`（`omsim-a88d12d8+production-replay-v1`）。
数据：`result/bench-inplace-lattice-20261111`（146 题，control = `OPUSOPTIMA_COMPOSE=off`，variant = 默认）。
参考：`result/opussolver-oracle-20261110`（OpusSolver `gtw123/OpusSolver@fdedd3f0` 生成的 146×3 参考解）。
复现：`python3 tools/bench_corpus.py --control OPUSOPTIMA_COMPOSE=off --jobs 8`。

---

## 1. 总体：落后的是 cycles，cost/area 是次生问题

136/146 题产出解。逐槽位（对我们产出的 136 题）：

| 指标 | 胜参考 | 输参考 | 中位比值（我方/参考） | p75 |
|---|---:|---:|---:|---:|
| cost | 56 | 80 | **1.20×** | 2.92× |
| **cycles** | **46** | **90** | **10.38×** | **19.42×** |
| area | 54 | 82 | 1.18× | 2.73× |

按参考解的架构分类（中位比值 cost/cycles/area）：

| class | n | 胜（c/cyc/a） | cost | cycles | area |
|---|---:|---|---:|---:|---:|
| `universal` | 62 | 14 / 13 / 14 | 1.37 | **14.37** | 1.20 |
| `pipeline/cheap-arms` | 28 | 7 / 3 / 8 | **2.42** | **22.97** | **2.28** |
| `linear/mono` | 39 | 31 / 26 / 28 | 0.41 | 0.05 | 0.17 |
| `pipeline/heavy` | 7 | 4 / 4 / 4 | 0.63 | 0.01 | 0.09 |

结论：单原子/线性题我们全面领先（静态组合/送料机）；差距集中在
`universal` + `pipeline/cheap-arms`（90 题，占产出的 66%），cycles 差 14–23 倍。

## 2. 结构归因：我们是“串行活塞/轨道输送”，参考是“并联 arm1 网络”

对 90 道落后题（有解）的发布机做机制画像：

| 量（中位） | 我方 | 参考 |
|---|---:|---:|
| `arm1` 数量 | **0**（86/90 台 <5） | **10**（最高 29） |
| `piston` 数量 | 14 | 2–3 |
| `track` 格数 | 143 | 6–8 |
| 机制成本构成 | **piston+track ≈ 100%** | arm1 为主 |
| 总指令数 | 3 310 | ~700 |
| 周期（最长磁带） | 2 661 | ~100 |

典型个案（我方 vs 参考）：

| 题 | 我方 cycles/cost/area | 参考 | 我方机制 | 参考机制 |
|---|---|---|---|---|
| `f613a067` | 54410 / 6405 / 1627 | 562 / 850 / 169 | 38 piston + 30 track，55 015 instr | 23 arm1 + 3 piston，437 instr |
| `89d6e08e` | 98420 / 8235 / 2181 | 1080 / 940 / 190 | 50 piston + 44 track，101 100 instr | 11 arm1 + 10 piston，2752 instr |
| `6e993c03` | 5384 / 650 / 144 | 216 / 695 / 161 | 10 piston + 5 track，1348 instr | 20 arm1 + 3 piston，148 instr |

成本模型（`src/Verification/IndependentMetrics.cpp`）：`arm1=20`、`piston=40`、
`track=5/格`、`bonder/unbonder=10`、glyph 10–20、input/output 0。
所以长轨道 + 多活塞同时抬高 cost 和周期：一条 150 格的馈线 = 750 cost，
而它的磁带长度直接决定整机周期。

**根因**：我们的多阶段题走“单条串行馈线 + 长活塞轨道”，每个原子约 50 个动作；
参考是“每原子 2–5 个动作”的 arm1 交接网络，不同臂的片段互相重叠。

## 3. 差距覆盖域（按输入/输出形状切分 100 道未全胜题）

| 形状 | 题数 | 代表 | 需要的能力 |
|---|---:|---|---|
| 多原子输入 + 需要化学 | **41** | `fe57c5d3`,`f613a067`,`f5922439` | 就地拆解 + 化学流水线（arm1 网络） |
| 单原子输入 + 需要化学 | **28** | `f58f0f1f`,`f158add5`,`e5ccff40` | 纯化/统一/生灭链（含缓冲） |
| 多原子输入 + 多输出 + 化学 | 15 | `fc6a70eb`,`e42f1f11`,`deecf24d` | 拆解 + 并行产物装配 |
| 单原子输入、无化学（大产物） | 5 | `e445c554`,`4f6b71ea` 等 | 大格点装配（本轮已解决） |
| 多原子输入 + 多输出 + multiplier>1 + 化学 | 4 | `ce508e51`,`5911f6b1` | 同上 + 周期平衡 |
| 单原子输入 + 多输出 + 化学 | 4 | `c15dcf8f`,`9a6d6d90` | 多产物流水线 |
| 多原子输入 + multiplier>1 + 化学 | 2 | `aeb771ff`,`63e0500c` | 拆解 + 化学 |
| 其它 | 1 | `e645d0a5` | — |

另有 10 题**完全无解**（`e42f1f11`、`cfba9c05`、`ac8d3e1b`、`2637c0fe`、`1dc60983`、
`19ff88db`、`19c7b313`、`16b1a5f5`、`07e252a4`、`0409d7fe`），全部是多原子输入。
它们需要“拆解 + 余料处理 + 6 输出周期”（见 §5 的停放问题）。

## 4. 上游 OpusSolver 的关键机制（代码级复核）

| 机制 | 实现 | 我们缺什么 |
|---|---|---|
| 相对时间片段 | `ProgramWriter.NewFragment()`；组件只写自己臂的指令序列；`WriteGrabResetAction = G + … + X`，`AdjustTime(-1)` 让复位与下一动作重叠 | 我们的 `write()` 推进全局游标；`ProgramBuilder` 目前只做审计，不做排程 |
| 逐臂最小位移排程 | `ProgramBuilder.AddFragment`（**逆序**合并）+ `CalculateTimeShift`：只检查**同一臂**重叠，复位时长 = `|Δext| + (3-\|Δrot−3\|) + |Δpos|` | 正是“短磁带 + 跨臂并行”的来源；我们没启用 |
| 元素流水线 | `ElementPipeline`：Input → Purify → Project → Disperse → Salt → Animismus → Unify → Buffer → Output，每个生成器一个 `AtomGenerator`（自带臂/缓冲），`Consume/Generate` 逐元素推进 | 我们的化学阶段串行挂在同一条馈线上 |
| 就地拆解 | `LowCost/Input/DiatomicDisassembler`、`LinearDisassembler`、`AtomConveyor`：源旁 unbond + 逐原子交给共享臂 | 我们只有活塞长轨 + disposal |
| 大产物装配 | `UniversalAssembler` + `AssemblyArea`（上/下臂）+ `BondProgrammer` + `ProductConveyor`，顺序焊接 + 独立产物输送 | 行机只会“一次一个原子沿行列推进” |
| 成本收敛 | `CostOptimizer`：删无指令臂/轨道格、把不发伸缩指令的 piston 改成 arm1 | 我们有 remove-idle-arms 等局部变换，没有 piston→arm1（历史实测会破 tape 语义，需配合片段重写） |

## 5. 结论：两个必须先补的通用能力

### A. arm1 传递网络 + 逐臂相对排程（C1+C3）

- 覆盖：§3 的 41+15+4+2 = 62 题（多原子输入）以及 28 题化学链的传输段。
- 验收口径：把 `CompileDisassemblyIR`/`CompileTranslationIR` 的 `Relocate`
  在 ≤3 格时降级为 `arm1` 的 `G + r… + X`（3–5 tick 片段），用
  `ProgramBuilder`（已有实现 + 14 项测试）做真正排程；先用
  `19c7b313`/`GEN` 类与 `f613a067` 作 fixture，要求 cycles 至少降到参考的 3× 内、
  不丢现有胜场。
- 风险：跨臂碰撞需要全局校验（`AuditMachineSchedule` 已能重放）；调度器替换
  必须保留旧发射器作回退。

### B. 余料停放（parking）而不是 disposal（GEN 类）

- `19c7b313`/`07e252a4`/`1baade11` 无 disposal 权限（bit 17），参考机用传送带
  把每周期 8 个余料原子推到队列里（实测原子数 17→25→33→49）。
- 我们的 IR `K::Discard` 只会生成 `glyph-disposal`；需要新增“Relocate 到停放区”
  的语义，并让周期内所有旧余料一起前移（整串平移）。

## 6. 本轮已落地的针对性提升

1. **glyph 工位**（`d9e8760`）：钙化/投影工位 + 无臂静态布局，覆盖单原子输入化学题。
2. **大格点原位布局**（本次）：>6 原子的直连键合产物（格点）没有可行臂布局，
   改为“输入区落在产物格 + bonder 并行 + out-std”的无臂机。

146 题 A/B（control = compose off）：

| | any-win | cost | cycles | area | all-three |
|---|---:|---:|---:|---:|---:|
| before | 55 | 53 | 43 | 51 | 43 |
| after | **63** | **61** | **51** | **59** | **51** |

`improved=9 regressed=0`。大格点 5 题从 `4641–5153 cycles / 640–700 cost / 139–159 area`
降到 **3 cycles / 180–240 cost / 18–19 area**（参考 1111–1251 / 500–570 / 125–138）。
契约测试 61/61 通过。

## 7. 下一步顺序（按覆盖/成本比）

1. C1+C3 的 arm1 拆解-传递内核（62 题的上限，改动最大）。
2. 停放语义 + 6 输出周期（10 道无解题）。
3. 单原子输入的纯化/统一链（28 题，可复用静态工位 + 多次穿越）。
4. 多输出流水线（23 题）。

每步验收：native `legacy-gate` 通过、146 题零回归、三槽独立记账。

## 8. 成本差距的实测定位（2026-11-11 第二轮）

以 146 题 oracle 为参照，把 `cost` 槽逐题对比（`result/bench-peephole4-20261111`）：

| 我们的 cost / 参考 cost | 题数 |
|---|---:|
| ≤ 1.00（胜） | 61 |
| 1.00–1.10 | 7 |
| 1.10–1.30 | 10 |
| 1.30–2.00 | 17 |
| 2.00–5.00 | 26 |
| > 5.00 | 15 |
| 未产出 | 10–16（随并行负载抖动） |

**成本构成（gate `solution_file_cost` 口径）**：track 5/hex、piston 40、arm1 20、
bonder/unbonder 10、prisma 20、baron 30、calcification 10、其余 glyph 20、端口 0。

逐题分解得到的稳定结论：

| 题 | 我们 | 参考 |
|---|---|---|
| e59c2d6b | 13 piston / 116 hex | 8 piston / **15 arm1** / 49 hex |
| 18644c2c | 16 piston / 91 hex | 6 piston / **12 arm1** / 37 hex |
| d7ad2f77 | 11 piston / 78 hex | 5 piston / **8 arm1** / 21 hex |
| 63e0500c | 2 piston / 9 unbonder / 71 hex / 4 baron | 4 piston / **13 arm1** / 3 unbonder / 19 hex |

也就是说：**同一道题，参考机用 8–24 个 `arm1` 完成搬运，我们用 2–4 倍轨道 +
2 倍活塞**。`arm1` 定长 20 且旋转搬运与距离无关（半径 R 的臂可服务半径 R 的整个
六边形环），而我们所有搬运都是「活塞骑轨道」，成本 = 40 + 5·hex。

灾难级（>5×）的 15 题进一步暴露两类几何浪费：

1. **进料线过长**：`expandedFeeds` 把每个 source 往左排 `2*span+12` 个格，
   再由一根共享 courier 轨道横跨全部距离（9a6d6d90 出现 823 hex 的单根轨道，
   占该机 93% 成本）。
2. **按产物复制整条产线**：3 个产物 → 3 条完整 row 机（x≈304/753/1340），
   参考用一套网络同时供三个出口。

结论：**成本差距不是局部冗余，而是搬运范式差异**。局部编辑器（删除冗余字形/
机构、缩短未被遍历的轨道端点、换成同 tape 的便宜机构）最多回收 2–5%，无法翻越
1.3× 以上的差距。

### 本轮已落地的成本优化（`8d8066a`）

`LocalOptimizer` 在变体队列之后新增 gate 定价的 peephole 阶段：

- 删除无人站立/无人沿其驱动的轨道、全空 tape 机构、冗余 bonder/unbonder/glyph；
- 同位置/同旋转/同 tape 换成更便宜机构（`bonder-prisma→bonder`、
  `arm2/3/6`/`baron→arm1`、无伸缩活塞→`arm1`）；
- 按 tape 的 `A`/`a`（沿轨道 ±1 格）算出机构真正遍历的区间，一次性裁掉区间外的
  轨道格。

每个提案都必须先过 native gate 才可能进槽，因此该阶段对 cost 单调、绝不牺牲
cycles/area；默认 256 次提交、2000 ms 时间片、预留 3 s 发布余量、96 对象上限
（`OPUSOPTIMA_COST_PEEPHOLE` / `OPUSOPTIMA_COST_PEEPHOLE_MS` 可调）。

实测：成本恒定下降（86277833 1705→1485、44598f0a 1745→1705、deecf24d
13255→13185、e59c2d6b 1035→1020 等），`regressed=0`；但槽位胜负仍为 61/75 ——
因为 3–15% 的回收不足以翻越 1.05–1.3× 的差距。

### 要真正打平成本，必须做的那件事

新增 **arm1 搬运内核**（与 `SerialArmExecution` 对齐）：

1. 给「从 A 到 B 运输」求枢轴 P，使 `dist(P,A) = dist(P,B)`，用一次 swing 搬运
   （成本 20，与距离无关）；
2. 工位/端口按同心六边形环布置，一个臂服务整环（参考机的 15 个 arm1 就是环上
   的换手点）；
3. 多输出共用一张网络，禁止按产物复制产线；
4. 分子输入改成「环上逐原子提取」，不再为每个原子列配活塞+竖轨。

只有在 1–4 落地后，`track/hex` 与 `piston` 的占比才会降到参考机水平，`cost` 槽
才有机会从 61/75 翻到 ≥ 平手。

## 9. arm1 搬运内核落地情况（2026-11-11 第三轮，`9e2e276`）

### 已落地

`third_party/omsim` 的臂几何（`sim.c: record_swing_area` + `decode.c` 指令位）：
一只 `arm1` 的 **size ∈ {1,2,3}**（其它值 `abort`），抓取点只能是
`pivot + L*step(d)` 这 6 格（半径 L 的六边形环的**顶点**，不是整圈 6L 格），
旋转把持物沿该环推进。因此「一次 swing」= 20 成本、位移 L 或 2L 格、与距离无关。

在 `ComposeAssembly` 中实现为内核：

| 步骤 | 实现 |
|---|---|
| ① 枢轴求解 | `SwingCandidates(target)` 枚举所有 `(pivot, L, grab, drop)`，使 `target − L*step(drop) = pivot`、`pivot + L*step(grab)` 为抓取格；按「少旋转优先、L 大优先、朝西优先」排序 |
| ② 一臂服务整环 | `AxisRingDirection(cell,pivot,L)` 取代原来的 `HexDistance == 3` 判定（原判定会把臂够不到的格子当成环上格），环布局改为在 `(pivot, L)` 上搜索「环上产物原子数最多」 |
| ③ 共享网络 | 环布局用**一只臂**服务整环的多个产物原子（每原子一次 grab→rotate→drop），而不是每原子一只机构 |
| ④ 分子输入提取 | 规划器已实现（`PlanRelay` 3 跳内 BFS + 一键 per-bond unbonder + 逐原子中继），但**未通过 native 校验**：串行中继的 tape 每周期重复时臂朝向不回位，`collision during motion phase cycle=14` |

### 语料实测

- 纯「单分子输入 → 同原子重排单产物」题：全库只有 **1 道**（64da5361，我们已
  155 vs 参考 460 胜）。其余分子输入题都带元素转换（lead→gold、vitae→mors），
  不属于重排族，所以 ④ 的**理论上限只有 1 题**，即使打通也不会改变胜负。
- 146 题 A/B（`result/bench-arm-swing-20261111`）：槽位不变（cost 61 / cycles 51 /
  area 59 / any-win 63），3 道 10–60 的成本抖动来自构造期时限，不是结构回退。
- 结论：①②③ 让合成器**能**摆放以前会被拒的形状（多输入小产物、半径 1/2 环、
  120° 摆臂），但在当前 146 题库上没有可测量的槽位收益；真正的成本差距仍来自
  参考机的「元素流水线」形态（见 §8）。

### ④ 要继续需要补的一件事

中继 tape 必须在每周期把臂的朝向转回抓取方向（`G, r^k, R^k, X` 或等价），并让
每个中继站在上一站落地后的下一拍抓取；否则第 2 周期起抓取格漂移，`cycle=14`
碰撞。`SerialArmExecution` 里已有同构的排程代码，可作为对齐基准。

## 10. 分子输入提取：行打包（2026-11-11 第四轮）

### 碰撞根因（代码级）

`collision.c` 把每个静止原子建成半径 29 px 的圆、把运动原子建成沿圆弧扫掠的
chain collider；六边形中心距 82 px，而持物原子始终落在离枢轴**恰好 L 个六边形
距离**的圆上。因此一次 swing 扫过的是该半径环上的**全部 18 个格心**（L=3），
不只是 6 个轴向顶点。旧规划器只排除 `SwingCandidates` 的顶点与落点，于是三条臂
的对角弧会从 `(1,0)` 这样的**非顶点格**上穿过——正是共享产物行第二颗原子停放的
位置，报 `collision during motion phase` @cycle 20 `[1,0]`。

### 修复

- `SweptCells(pivot, length, grab, drop)`：用与 `collision.c: to_xy` 相同的像素
  变换（82 px/列、71 px/行）把弧内所有格枚举出来，端点（抓取格、落点）除外。
- `PlanRelay` 新增两条硬约束：弧内任何格不得是已停放原子/产物格、也不得是任何
  已放置臂的枢轴（含本链前序跳的枢轴，按 BFS 路径计算）。
- `PlanExtraction` 把每条链的枢轴累积进 `usedPivots` 供后续链复用。

### 结果

- `ComposeGlyphStationTest.MolecularExtractionVerifiesNatively` 转绿（原 `DISABLED_`），
  native 报告 `passed 170 cycles / cost 120 / area 43`；解里第三条链改为
  `(-5,1) → (-4,0) → (2,0)`，末跳弧绕开整条产物行。
- 去掉 `OPUSOPTIMA_MOLECULAR_EXTRACTION` 开关，提取器**接回默认路径**。
- 全库可服务题仍只有 1 道（`64da5361` UNWINDING）：A/B 槽位完全一致
  （155/860/34），零回退；它已被行家族以 155 vs 参考 460 拿下 cost 槽。

## 11. 试剂缓冲打包：feed 轨道的成本杠杆（2026-11-11 第五轮）

### 几何事实

缓冲槽停放在蛇形进料器的上排，每个槽把共享 feeder 轨道两侧各拉长一个 stride 单位
（`5 + 2*feedReach`），所以 stride 每多 1 就多付 `5 cost/缓冲`。老几何用 stride 3
（两个停放原子之间留一个完整进料周期），但真正决定抓取时刻的是**feeder 时钟**，
不是几何间距：排程由 tape 逐拍给出，缓冲之间靠时刻错开而不是靠格距。

### 落地

`RowAssembly.cpp` 新增 `bufferStride`（窄进料默认 **1**，宽进料 `expandedFeeds`
固定 3；`OPUSOPTIMA_FEED_BUFFER_STRIDE=3` 全局恢复老几何）。`compactLayout` /
`compactBuffers` 分支保持原值不变。

宽进料必须保留 3：宽抽取的共享 courier 把 slot 距离当作**让位间隙**，打包后两个
停放原子会在运动相撞（`DisassemblyVerifierTest.WideExtractionInventoryBuffers`
`collision during motion phase cycle=388 at=-30,4`）。

### 146 题 A/B（`result/bench-feed-stride-narrow-20261111`，`--jobs 6`）

```
control produced=136/146 any-win= 63 cost= 61 cycles= 51 area= 59 all-three= 51
variant produced=136/146 any-win= 65 cost= 63 cycles= 51 area= 59 all-three= 51
changed: improved=12 regressed=3 unchanged=121
```

三条 `REGRESS` 全部是并行负载噪声，隔离重测各 2 次、两模式逐位相同：
`f5922439` 625/6303/126、`f2d0ed76` 3575/10881/1032、`cb6c9528` 4735/27169/1257；
三者 `bufferCount == 0`，stride 不可能影响其机器字节。真实口径
**improved=12、regressed=0**。

| 题 | 类 | cost | cycles | area |
|---|---|---|---|---|
| e59c2d6b ABLATIVE CRYSTAL | pipeline/cheap-arms | 1030 → **880** | 15072 → 13104 | 295 → 244 |
| deecf24d | universal | 12915 → 12295 | 41162 → 40242 | 3682 → 3482 |
| a7d3c0bf | pipeline/heavy | 12015 → 11375 | 43030 → 41102 | 3244 → 3052 |
| 803e8d4a | pipeline/cheap-arms | 9530 → 9410 | 74776 → 73414 | 2942 → 2900 |
| 89d6e08e | pipeline/cheap-arms | 8235 → 7795 | — | 2181 → 2173 |
| 86277833 | universal | 1235 → 1175 | 10565 → 9797 | 297 → 279 |
| fe57c5d3 | pipeline/cheap-arms | 1085 → 1055 | 5048 → 4928 | 251 → 241 |
| 44598f0a | universal | 1445 → 1395 | 13307 → 12731 | 372 → 360 |
| be50ec6b | universal | 1050 → 1035 | 9011 → 8819 | 243 → 237 |
| fabd74d3 | linear/mono | 600 → 590 | 3999 → 3855 | 265 → 257 |
| cca2722a | universal | 580 → 540 | 6085 → 5781 | 193 → 179 |

### 契约与全量测试

`result/full-suite-20261111/gtest.log`：1036 个用例，12 个失败。逐条复核后
**11 个在 pristine HEAD 上同样失败**（`AssemblyDiagnostics.*` 8 条 +
`AssemblyVerifierTest.*` 2 条 + `RigidCarrierTest.NoPlacementPreservesDownstreamOpportunity`，
均为 `bounded candidate collection stopped` 的配额/时限敏感用例），
`DisassemblyVerifierTest.WideExtractionInventoryBuffers` 是本轮引入又修好的
（改为宽进料不打包）。**本轮净新增失败 = 0**。新增 `FeedBufferStrideTest.*`
两个用例（结构不变量 + native 双验）全绿。

### 与参考的差距

`e59c2d6b` 的 cost 已从 1.025× 降到 **0.876×**（880 vs 1005），但 cycles 仍是
13104 vs 1170、area 244 vs 215。参考机用 8 活塞 + 15 `arm1` + 49 hex；我们仍是
9 活塞 + 110 hex。**cost 差的骨架仍是「活塞骑轨道 vs arm1 摆臂」**，本节只回收了
缓冲侧的几何浪费。

### 为什么本轮没有直接上 arm1 中继

窄进料（`span ≤ 2` 的源）不是「每原子列一活塞+竖轨」，而是一条**按拍驱动的蛇形
feeder**：`feedData` 把每个 slot 的抓放时刻写进 1729 拍的 tape，活塞沿一根共享
rail 往返。把这段 rail 换成 2–3 只 `arm1` 等于重写相位规划器（上游
`ProgramWriter` + `ProgramBuilder` 的 per-arm 相对片段合并），而不是替换一个机构：
同一台机器的其余 12 条 tape 都以 feeder 的拍号对齐，任何偏移都要整体重算。
这正是 `result/solver-row-pipeline-20261109/REPORT.md` 里「further gains need a
phase-planner rewrite」的同一结论。`expandedFeeds`（每原子列一活塞+竖轨）分支
才是可以局部替换的形态，但它只覆盖 `span > 2` 的源，`e59c2d6b` 不在其中。

## 12. 近身差距的逐题归因：缺的是 `arm1`，多的是 hex（2026-11-11 第六轮）

数据：`result/bench-feed-stride-narrow-20261111`（默认配置），逐题解析 cost 槽
机器并与 `result/opussolver-oracle-20261110/solutions/<sha>_cost.solution` 对比
机构构成（`p` 活塞数、`a` arm1 数、`h` 轨道格）。

| 题 | 类 | 我们 cost | 参考 | 比值 | 我们 p/a/h | 参考 p/a/h |
|---|---|---:|---:|---:|---|---|
| 2120aa96 | pipeline/cheap-arms | 740 | 710 | 1.04 | 9/1/58 | 4/15/34 |
| bb847c75 | universal | 2455 | 2295 | 1.07 | 39/0/171 | 37/4/139 |
| 18644c2c | pipeline/cheap-arms | 810 | 755 | 1.07 | 11/0/62 | 6/12/41 |
| 526f899e | universal | 1855 | 1720 | 1.08 | 28/0/141 | 27/6/98 |
| b7b0160e | universal | 1820 | 1680 | 1.08 | 27/1/134 | 26/7/90 |
| fe57c5d3 | pipeline/cheap-arms | 1025 | 925 | 1.11 | 8/0/89 | 7/12/57 |
| d7ad2f77 | universal | 600 | 540 | 1.11 | 7/2/44 | 5/8/24 |
| 0f87ae0d | universal | 1665 | 1465 | 1.14 | 28/0/103 | 25/4/71 |
| 44b4ecd1 | universal | 1090 | 955 | 1.14 | 9/1/92 | 4/24/33 |
| 172055db | universal | 1755 | 1510 | 1.16 | 27/0/129 | 24/3/92 |
| 63e0500c | linear/mono | 780 | 650 | 1.20 | 2/1/72 | 4/13/24 |
| a1e56266 | universal | 750 | 620 | 1.21 | 9/0/66 | 6/9/28 |
| b2c42099 | universal | 755 | 605 | 1.25 | 7/0/67 | 2/18/17 |
| 5e131b3a | universal | 2095 | 1655 | 1.27 | 28/1/177 | 26/5/89 |

结论是**一致的单一形态差**：活塞数几乎相同（7–39 vs 2–37），差距全部落在
「我们 0–2 只 `arm1` vs 参考 3–24 只」+「我们 44–177 hex vs 参考 17–139 hex」。
也就是说这 14 道不是搜索不足，而是**同一个 `arm1` 搬运网络没接进通用行**。

### 为什么不能把现有机构「就地」换成 arm1（几何结论）

arm1 的一次 swing 把原子从环上一个顶点搬到另一个顶点，位移恒为
`L * (step(d) − step(g))`，而 `|step(d) − step(g)| ∈ {1,2}`，所以**位移一定是沿 6 个
轴向之一、长度为 L 或 2L（L ≤ 3）**；同时持物原子扫过半径 L 的**整圈**。两条推论：

1. **1 格宽的竖直提升做不成 arm1**：同一列上相距 m 的两格只能靠 180° 旋转
   （|k|=3），而弧必然向两侧鼓出 L 格，撞相邻列。宽进料里「每原子列一活塞+竖轨」
   因此无法逐列替换（`expandedFeeds` 的竖轨是唯一可选形态的结论要修正：
   可替换的是**整条进料**，不是单列）。
2. **蛇形 feeder 的长度是延时线**：`distance = 5 + 2*(−feedOffset − c.q)` 用轨道
   格数给每个 slot 制造相位差；参考机用臂的旋转拍数制造同样的相位差。这就是
   §11「缓冲 stride 是唯一松弛量」的根因——再压格数就会改时序。

### 下一步的真实形态（relay feed）

范围锁定在 **`direct=0 && compact=0` 的普通行**（`OPUSOPTIMA_DEBUG_INTAKE=1` 可打印
`[intake] ... direct=0 compact=0`）：`44b4ecd1`、`b2c42099` 等近身负场都在这里。
需要的新生成器：

1. 用**斜向 swing 链**把每个源原子送进机器（每跳沿轴向 L 或 2L，扫掠按 `SweptCells`
   校验，`ComposeAssembly` 的 `PlanRelay` 已是现成实现）；
2. 关掉蛇形 feeder 与其 1729 拍 tape，用「第 k 站第 t 拍抓」的相位表替代延时线
   （等价于上游 `ProgramWriter` + `ProgramBuilder` 的 per-arm 相对片段合并）；
3. 每个候选仍先过 native gate，因此可分批落地：先 1 道原型、再按类铺开。

只要第 2 步的相位表复用现有 `time` 游标（动作按程序序串行写入），tape 长度自然
就是周期，不需要重算其他 12 条 tape——这是把工作从「全机重排」压到「进料段重写」
的关键，也是本轮把 `SweptCells`/`PlanRelay` 先落地的原因。

## 13. 放宽取料（M1）落地与 M2 的确切断点（2026-11-11 第七轮）

### 结论先说

`RowIntakeModeScope(2)`（放宽取料）已接入默认路径，但**只能作为兄弟候选**，不能替换
普通机：放宽后蛇形 feeder 被停到棋盘外（`feedArm`/`feedTrack` 移到
`(-feedOffset, -20)`，`feedData = {1,0,0,0,2,0,0,0,1,0}`），一旦该机被 gate 拒绝，
整个 `life-death` stage 就没了。用「替换」形态实测：14 题三槽全胜，
31 题直接停止产出。

落地形态：

- `RowAssembly.hpp` 新增 `RowIntakeModeScope`（0=跟随环境、>0=强制取料、<0=强制蛇形、
  `2`=放宽取料：允许在 `compactLayout` 之外、最多 8 个源）。
- `LifeDeath.hpp` 新增 `CompileLifeDeathLocalIntake`：在 `RowIntakeModeScope(2)`
  内复用同一套 `CompileLifeDeathGeometry`。
- `AssemblySolver` 的 `submitIntakeSibling`：普通机先经 `attempt` 入档，随后把放宽机
  作为**额外候选**提交；`CandidateArchive::Submit` 按目标槽取最小，所以更差的兄弟永远
  拿不走槽位，编译抛异常或 gate 拒绝也只是丢掉兄弟。
- 兄弟先过一遍 `LocalTrial(·, 8)` 再提交：下游 `OptimizeVerified` 以
  `archive.Slots()` 为父本，未打包的兄弟会分摊优化预算（`160f11ef` cost 2040→2050）。
- 作用域限定 `outputs.size() == 1`：蛇形是一条共享轨道，多输出机每个产物复制一条产线，
  在那里重建兄弟只花掉验证预算（`journal8/P102` 17150→17970）。

### 146 题 A/B（`result/bench-intake-twin3-20261111`，`--jobs 6`）

```
control produced=136/146 any-win= 65 cost= 63 cycles= 51 area= 59 all-three= 51
variant produced=136/146 any-win= 65 cost= 63 cycles= 51 area= 60 all-three= 51
changed: improved=5 regressed=4 unchanged=130   ← 并行负载噪声
```

9 道「变化」题逐题串行隔离、每模式 3 次，全部逐位稳定：

| 题 | 关 | 开 |
|---|---|---|
| `b2c42099` | 755 / 4346 / 217 | **635 / 2727 / 164** |
| `63df4fe9` | 580 / 3032 / 157 | **500 / 1993 / 125** |
| 其余 7 道 | — | 与关完全一致 |

即外部并行跑出的 4 条 `REGRESS` 全部是负载噪声。**真实口径：improved=2，
regressed=0**。37 道 journal 资产题全量对比：只有 `P070`（= `b2c42099`）变化，
其余逐位相同。

### 为什么 14 道只兑现了 2 道——M2 的确切断点

历史「14 题三槽全胜」是**替换**形态测出来的；改成兄弟形态后，其余 12 道机器逐位相同。
逐题查 `life-death-local-intake` 的拒绝原因：

```
b4769272 :: life/death pickup schedule unsupported   (历史 6930 -> 1065)
cd9b4ea9 :: life/death pickup schedule unsupported   (历史 7610 -> 1620)
其余 10 道 :: ACCEPTED，但机器与普通机无差别（普通机已被后续优化追平）
```

断点在 `LifeDeath.cpp:783`。共享分子工厂的取料时刻是从**蛇形 feeder 的 tape** 反推的：

```cpp
const auto tape = machine.objects[feedArm].instructions;
for (auto ins : tape)
    if (ins.action == 'G') {
        int position = 0;
        for (auto prior : tape) { if (prior.action=='A') ++position;
                                  if (prior.action=='a') --position; }
        for (int f = 0; f < factories.size(); ++f)
            if (position == factories[f].distance)                 // ← feeder 行程
                events.push_back({ins.parameter - factories[f].distance - 8, f});
    }
if (events.empty() || events.front().time < 0)
    Reject("life/death pickup schedule unsupported");
```

`factories[f].distance = 5 + 2 * (-feedOffset - cell.q)`，也就是「工厂沿 feeder 轨道
的格号」。放宽取料把 feeder 轨道截成 2 格，工厂就再也对不上 `position`，
`events` 为空 → 整个 stage 拒绝。这正是 §12 说的「相位耦合」的具体代码位置：

**M2（站间摆臂交接）的最小可验证切片 = 把共享工厂的交付点从「feeder 轨道格号」
改成「取料轨道的列号」**，即让 `factories[].distance` 由取料臂的
`directMinColumn..-feedOffset` 列区间表达，`events` 从取料臂 tape 的 `G` 时刻反推。
只有这一步过了，`b4769272`（4.7× cycles）与 `cd9b4ea9`（6.7× cycles）这两块最大的
近身收益才能进档。

其余 10 道不兑现的原因不同：普通机在这几轮优化（缓冲打包 `f77ec3f`、扫掠安全
`884a3f`、`LocalTrial` 顺序）之后已经追平了当年放宽机的水平，属于**已经吃掉的收益**，
不是损失。

### 附：compact-layout gap 的收缩实测（否决）

`compactGap` 决定 `feedOffset = width + compactGap`，也就是进料银行到源站的距离，
默认 2。试 `OPUSOPTIMA_COMPACT_LAYOUT_GAP=1`（`result/bench-gap1-20261111`）：

```
control produced=140/146   variant produced=139/146
changed: improved=6 regressed=2 unchanged=131
```

逐题串行隔离后：`f7ea45f6`、`a94b5296`、`160f11ef` 三条「改进」在隔离下**完全消失**
（并行噪声），只有 `6e993c03` 是真的（640/5384/144 → 635/5383/141）；而两条回退都是
真的：

| 题 | gap=2 | gap=1 |
|---|---|---|
| `b0171cdf` | 885 / 5490 / 325 | 925 / 7380 / 346 |
| `329bfd90` | 1535 / 13113 / 577 | 1570 / 15966 / 600 |

`b0171cdf` 的 cycles 反而涨 34%，说明 gap 压到 1 之后该行的相位不再成立，被 gate 拒绝后
落到更差的其他族；同时 `produced` 掉 1。**净负，否决**，默认保持 2。

### 附：共享 life/death 进料资源的实测（仍保持 opt-in）

`OPUSOPTIMA_SHARED_FEED_RESOURCE=on`（`result/bench-shared-feed-20261111`）：

```
control produced=140/146   variant produced=140/146
changed: improved=4 regressed=0 unchanged=135
  improve 7b837aa2 cost:6605->5735; cycles:88450->86266; area:1766->1533   ← 隔离后仍成立
  improve f7ea45f6 / a94b5296 / 53c979c8                                  ← 隔离后消失
```

`7b837aa2` 是真实收益（cost −13%、area −13%），但把默认翻成 on 之后，资产语料**无法
判定**：同一套 37 题对比跑两次，每次跳出来的题都不一样——一次是 `journal9-p/P106`
1075 → 1095（单进程无争用时两者完全一致 1015），另一次是 `journal3/P068` 885 → 880
（方向还相反）。即残余差异是负载噪声，不是系统性收益或损失。

结论：**只有一道题的真实收益，不足以为它翻默认**，保持 `=on` opt-in；等 M2 打通后
重新评估（那时组合预算的分配会变）。

结论：这一轮的 4 个候选杠杆里，只有「放宽取料兄弟候选」是可度量、零回退、可提交的；
compact gap 收缩净负、共享工厂收益过窄、`Lifetime`/多输出共享网络仍受 M2 断点阻塞。

## 14. M2–M5 的落地设计与状态（2026-11-11 第七轮收口）

### 为什么四个里程碑要按这个顺序，以及各自的真实前置

先把结论摊开：**M2 是唯一的必要前置**。证据有三条，都是本轮实测出来的：

1. **10 道无产出题全部是 arm1 机器**（参考机构：3–9 活塞 + 8–27 `arm1` + 3–10 轨道）：

   | 题 | 类 | 参考 p/a/t/b | 参考 cost/cycles/area |
   |---|---|---|---|
   | `e42f1f11` | universal | 8/21/7/3 | 1030 / 833 / 201 |
   | `cfba9c05` | cheap-arms | 9/25/10/5 | 1250 / 719 / 250 |
   | `ac8d3e1b` | universal | 6/21/6/3 | 845 / 670 / 175 |
   | `2637c0fe` | universal | 3/24/9/3 | 880 / 619 / 190 |
   | `1dc60983` | cheap-arms | 7/15/4/4 | 830 / 778 / 165 |
   | `19ff88db` | cheap-arms | 4/8/5/7 | 665 / 1237 / 232 |
   | `19c7b313` | linear/mono | 5/14/5/1 | 710 / 438 / 182 |
   | `16b1a5f5` | universal | 6/17/3/3 | 880 / 1431 / 213 |
   | `07e252a4` | cheap-arms | 7/27/10/6 | 1230 / 641 / 291 |
   | `0409d7fe` | cheap-arms | 6/14/3/4 | 845 / 1271 / 219 |

   所以 **M5（余料停放）单独做没有产出**：这些题根本不缺停放位，缺的是搬运网络。
   M5 只对「有化学转换 + 余料必须 disposal」的题有效，而那些题现在是有产出的。

2. **分类的差距全部落在 arm1 上**（146 题，默认配置，逐槽位比值中位数）：

   | class | n | cost | cycles | area |
   |---|---:|---:|---:|---:|
   | `pipeline/cheap-arms` | 29 | 1.97× | **24.3×** | 2.41× |
   | `universal` | 64 | 1.37× | **14.3×** | 1.20× |
   | `linear/mono` | 39 | **0.34×** | **0.12×** | **0.17×** |
   | `pipeline/heavy` | 7 | **0.63×** | **0.01×** | **0.09×** |

   我们已经赢下 `linear/mono` / `pipeline/heavy`；差距 100% 集中在需要 arm1 网络的
   两类。**M4（多输出共享网络）只降 cost 不产 arm1**，单独做也打不开这两类。

3. **M3（逐臂相对排程）是任何 arm1 行的必要条件**：一台机器的 12 条 tape 以同一
   拍号对齐，把活塞换成 arm1 必须让每条臂的片段独立锚定。`AuditMachineSchedule` 只
   做审计（`RowAssembly.cpp`，`ScheduleAudit`），要变成 emitter 才能承载 arm1 行。

### M2 的最小可验证切片（唯一入口）

断点在 `LifeDeath.cpp:783`：共享分子工厂的交付时刻是从**蛇形 feeder tape** 的
`A`/`a` 累计位移反推的，`factories[].distance = 5 + 2*(-feedOffset - cell.q)`。
取料臂每个原子往返一次（净位移恒为 0），无法表达这个单调位移，所以放宽取料带不了
工厂。

要打通，需要三步（按依赖排序）：

1. **工厂出口改行**：`addFeed` 目前把分子通道的落点放在 `{x, 3}`（`feed.cell`），
   而取料臂站在第 3 行、只能向第 0–2 行伸缩（`reach = 3 - slot.row`）。出口必须
   下移到第 2 行（`{x, 2}`），并相应移动 `glyph-life-and-death`/`glyph-calcification`
   与共享运输轨（`transportRow`、`transportPath`）的落地段。
2. **相位表换基准**：`events` 改为从取料臂 tape 的 `G` 时刻反推，`factories[].distance`
   改成取料臂的列偏移（`cell.q - directMinColumn`），`distance + 8` 的提前量保留。
3. **请求数校验**：`feed.expectedRequests` 与
   `eventsPerProduct * repetitions` 的一致性检查（`LifeDeath.cpp:786-796`）要按新
   基准重算，否则会 `shared feed request count differs from finite demand`。

预期收益（历史「替换」形态实测）：`b4769272` 6930/23367/1923 → 1065/8465/·，
`cd9b4ea9` 7610/20422/1962 → 1620/3033/·。这两道是当前最大、最确定的近身收益。

### M3 的形态

`ProgramBuilder` 已有 `ProgramFragment{arm, instructions, anchor}` 与按臂合并
（`RowAssembly.cpp` `AuditMachineSchedule` 里已经在用），把 `CompileRowsWithRetimingPolicy`
的 `write()`/`move()` 从「共享 `time` 游标 + 逐臂 append」改成「逐臂片段 + 合并时
只允许缩短片段之前的空档」，`SweptCells`/`PlanRelay`（`ComposeAssembly.cpp`）已是
现成的 arm1 扫掠校验。这一步不改变现有机器字节（审计当前 `displaced=false` 的机器
必须保持逐位一致），是安全的重构型前置。

### M4 / M5 的形态

- **M4**：`independent-outputs` 目前按产物各造一整台机器再平移拼接
  （`AssemblySolver.cpp:1917` 起）。多输出共享网络的正确形态是「一份机构 + 每产物
  一条出料支线」，收益只在 cost/area，不产 arm1——排在 M2/M3 之后。
- **M5**：`boundedWaste` 分支已经是「余料停车轨」而不是 glyph-disposal
  （`RowAssembly.cpp` `if (disposal && boundedWaste)`），缺的是把它对
  `LeftWaste`/`Purify` 类也打开；但如上所述它不解决当前 10 道无产出题。

### 本轮实际交付

- `c1f4963` 放宽取料兄弟候选（M1）：2 题三槽全胜、144 题逐位一致、37 道资产题仅
  `P070` 变化；全量 1039 用例 1023 通过，失败与 pristine HEAD 同一批 11 条。
- `b36549d` 两处否决的实验记录（compact gap=1、共享工厂默认开启）。

### M2 的目标域量化（`result/domain-compose-20261111`）

把 162 道语料题各跑一遍、逐题抓 `Assembly branch=... detail=...`，统计所有 stage 的
拒绝原因：

```
produced=155  unproduced=7
 115  portfolio-static-reaction  :: static purification requires an ordinary singleton metal target
  51  portfolio-static-embedding :: Unsupported: atom supply
  27  portfolio-static-reaction  :: Unsupported: header or permissions
  24  portfolio-static-embedding :: Unsupported: header or permissions
  22  portfolio-static-embedding :: Unsupported: atom supply or duplicate cell
  12  life-death-local-intake    :: life/death pickup schedule unsupported  ← M2 的唯一目标
  12  (其余各自 1–9 条)
```

**这 12 道就是 M2 的全部可见收益域**（单输出、取料机器已编译成功、只差工厂相位）：

```
1dc60983 284971e2 53c979c8 550b9c1c 7b837aa2 8a844d4d
be4a3de2 cb6c9528 e5ccff40 f2d0ed76 f58f0f1f f613a067
```

`b4769272` / `cd9b4ea9` 不在其中：它们是双输出，被 `submitIntakeSibling` 的单输出护栏
直接跳过。要覆盖它们，单输出护栏要先按 M2 的新几何重新评估。

`compose-assembly found no collision-free arm placement` 只有 2 道，所以「把组合式
arm1 网络做宽」（M4 的候选路线）在现有语料上几乎无域可吃——**M4 不应优先于 M2**。

## 15. 行流水线成为默认：周期最大单点杠杆（2026-11-11 第八轮）

### 先看参考机到底赢在哪

用 `tools/solution_probe.py` 解析 `fe57c5d3` 的两台 cost 槽机器：

| | 机构 | hex | period | cycles |
|---|---|---:|---:|---:|
| 我方 | `piston=9 unbonder=15 bonder-prisma=9` **`arm1=0`** | 123 | 822 | 4928 |
| 参考 | `piston=7 arm1=12 unbonder=3 bonder-prisma=3` | 57 | 602 | **612** |

我方 `cycles / period = 4928 / 822 ≈ 6.0`，即**一个产品一个周期**；参考机
`612 ≈ 602`，即**一个周期内流水出完 6 个产品**。参考机的 12 条臂各跑自己的 tape
（`G:18` 表示一个周期 18 次抓取），并行推进；我方 9 只活塞挤在同一条 `time` 游标上
串行。**差距的主因是并行度，不是单步成本**。

### 落地

`CompileRowsWithRetimingPolicy` 里本来就有三级独立时钟，但**默认全关**：

- `feedPrefetch`：进料器自己的绝对时钟，可以在上下臂收尾时预取下一颗；
- `rowPipeline`：每次放手只等下料格空闲的精确拍，而不是整条装配时钟；
- `assemblyClocks`：上臂的回收/再抓/回程尾巴与传送带空程各用独立时钟。

三者全部翻成默认开（`!flag || flag != "off"`），主开关是
`OPUSOPTIMA_ROW_ASSEMBLY_CLOCKS=off`。仅这一步就是本仓库至今最大的周期杠杆。

### 146 题 A/B（`result/bench-clocks-guarded-20261111`，`--jobs 6`）

```
control produced=140/146 any-win=65 cost=63 cycles=51 area=60
variant produced=140/146 any-win=65 cost=63 cycles=51 area=60
changed: improved=26 regressed=1 unchanged=112   ← 唯一回退隔离后完全一致，是噪声
```

隔离抽样（每题每模式 3 次，全部逐位稳定）：

| 题 | 关 | 开 |
|---|---|---|
| `bdaa166d` | 1420 / 23072 / 556 | — / **13388** / — |
| `284971e2` | 905 / 3656 / 194 | — / **2348** / — |
| `0f6f9d67` | 1090 / 12005 / 278 | — / **8135** / — |
| `53c979c8` | — / 19419 / — | — / **16144** / — |
| `e5ccff40` | — / 11878 / — | — / **8272** / — |
| `b2c42099` | 635 / 2727 / 164 | 635 / **2148** / 164 |
| `63df4fe9` | 500 / 1993 / 125 | 500 / **1543** / 125 |

周期降幅 20%–42%，cost/area 全部不动。

### 轮系家族必须保持串行（真实碰撞）

翻默认后 `WheelAssemblyTest.MolecularSupplyNormalizationVerifiesTwice` 报
`collision during motion phase cycle=1858 cell=-17,0`：轮系家族把行机器按固定偏移
整体平移，并从行 tape 的活塞位置反推自己的节拍（`obj.position == {-offset, 3}` 找
`feedArm`），流水线改变 tape 结构后这套推导失配。新增 `RowClockScope(-1)`
（`RowAssembly.hpp`）在 `WheelAssembly.cpp` 的两处行编译外面强制串行。

代价：improved 从 31 降到 26，换掉一个真实的 native 碰撞——这个交换是对的。

### 契约测试跟着改（不是放宽，是改基准）

`FeedPrefetch` / `RowPipeline/RowAssemblyClocks/ClockedScheduledRow` 四条本来断言
"不设环境变量就是旧串行机"，现在断言"默认是流水线机、`=off` 逐位还原串行机"。
`RowEmptyResetRetiming` 与 `ComponentRowHandoff` 的 retiming/交接优势在流水线下已被
取代（两机相等），改为对**串行基准**比较，claim 本身保留。

### 流水线落地后的差距（146 题，默认配置，逐槽位比值中位数）

| class | n | cost | cycles | area |
|---|---:|---:|---:|---:|
| `linear/mono` | 39 | **0.34×** | **0.12×** | **0.17×** |
| `pipeline/heavy` | 7 | **0.63×** | **0.01×** | **0.09×** |
| `pipeline/cheap-arms` | 29 | 1.97× | **23.69×** | 2.41× |
| `universal` | 65 | 1.37× | **13.76×** | 1.20× |
| **ALL** | 140 | **1.10×** | **9.75×** | **1.18×** |

全量周期中位数从本会话开始时的 **10.38×** 降到 **9.75×**。我们仍然全面赢下
`linear/mono` 与 `pipeline/heavy`；差距依旧 100% 在两类 arm1 机器上。

### 本轮同时否决的三个杠杆

1. **`bufferedSupply` 不参与流水线**（`!bufferedSupply` 条件）：试着放开后，
   `fe57c5d3` / `2120aa96` / `18644c2c` / `e59c2d6b` / `a7d3c0bf` 的开关两态**逐位
   相同**——它们被别的条件（`expandedFeeds`/`custody`/`owners` 等）挡在外面，放开这个
   标志是空操作。已回退，保留原护栏。
2. **`OPUSOPTIMA_SLOT_ADMISSION=on`**（`result/bench-slot-admission-20261111`）：
   `improved=3 regressed=1`，唯一回退 `a94b5296` 是 cost/area 更好、cycles 差 2% 的
   混合结果。收益过小，不翻默认。
3. **`compact layout gap=1`**（前文附录）：净负。

### 结论：平手不可能靠调参达成

`cheap-arms` 23.7× 与 `universal` 13.8× 的周期差不是"调度没优化好"，而是**机构形态**：
参考机用 8–27 只 `arm1` 并行搬运（`fe57c5d3` 的 12 条臂各自一条 tape、一个周期抓 18
次），我们用 9 只活塞串行在一条时间线上。流水线已经把同一条时间线上的重叠榨干
（20–42%），剩下的是**并行机构的数量差**，只能靠 M2/M3（arm1 网络 + 逐臂排程）解决。

## 16. arm1 行网络：域量化与一个证明级的否定（2026-11-12）

### 先量化域，再动手

新增 `tools/puzzle_probe.py`（`.puzzle` 读取器，映射 `BinaryParser::ParsePuzzle`），
对 146 道题逐题算两件事：产物元素是否**只有**单原子端口、以及是否存在于多原子输入里。

```
single-output / non-production: 114
  产物存在某个元素既无单原子端口、又在分子里的（"可中继"）: 53
  其中 losing 两类: pipeline/cheap-arms 25/29、universal 18/65
```

于是实现了第三条生成路径 `BuildRelayedStations`：产物原子由输入分子的 unbonder 释放，
臂链（`PlanRelay` + `SweptCells`）搬到站点格，站点臂再放到产物框上；每条臂一条独立
tape，天然流水。布点改为**节点预算的回溯**（`kComposedNodeBudget`、上限 20 原子），
并把 `composeOnline` 的「≤6 原子」门槛拆掉。

### 实测结果：0 道被接受

146 题全量跑一遍，`compose-assembly event=gate accepted=true` **一条都没有**；
拒绝原因是 `no collision-free relay station layout` / `no collision-free arm placement`。

`fe57c5d3` 的逐原子诊断（`OPUSOPTIMA_DEBUG_COMPOSE=1`）：

```
[relay] atom 0 target=(0,0) ok=0 pivot=30 station=0 arc=60
[relay] atom 1 target=(-1,1) ok=25 pivot=20 station=4 arc=41
[relay] atom 2 target=(-1,0) ok=30 pivot=20 station=4 arc=36
```

`(0,0)` 那颗原子**一个可行解都没有**：长度 1 的 6 个候选枢轴全落在产物格上，
长度 2/3 的 60 条扫掠弧全部穿过产物框。

### 为什么这是结构性的，不是搜索不够

`arm1` 的一次 swing 由「枢轴在距目标 L 处、抓取格在距枢轴 L 处、扫掠弧取半径 L 环上的
一段」定义。若某产物原子的**六个邻格全是产物原子**：

- `L = 1`：枢轴必须正好是这 6 个邻格之一 → 全部被占 → 无解；
- `L = 2, 3`：弧要穿过距枢轴 L 的环，而该环与密集产物框相交 → 必然碰撞。

对 114 道单输出题做这个判定：

```
产物中存在「六邻全占」原子的题: 33 / 114
（含 fe57c5d3、b2c42099、cb6c9528、e59c2d6b、bb847c75、cf12f5ce …）
```

**结论：纯 arm1 网络在数学上覆盖不了这些产物框。** 参考机之所以在 12 只 `arm1` 之外
还带 **8 条轨道 + 7 只活塞**，正是因为内部原子必须靠轨道/活塞落位。所以 M2/M3 的正确
形态是**臂 + 轨道的混合布点器**（interior 用 track+piston，外圈用 arm1 并行），而不是
"每原子一只臂"。

这一轮把「纯臂网络」这条路完整走到头并证明它不通，同时把域量化、`Placement` 的节点预算
与 `.puzzle` 读取器留了下来；实验代码已全部回退（零接受、零 gate 拒绝，留着只是风险面）。

## 17. 内外向臂网络（inside-out arm network）：实现 + 全语料 A/B（2026-11-12）

§16 证明了「静态布点」无法放置「六个邻居全是产物原子」的原子。这一轮把那个证明的推论
落实成机器：**帧不必在被放置时就是满的，只要在更早的节拍里不做满**。于是按「已占邻居数」
从多到少排序，内部原子先落位（此时帧是空的），后续摆动只与**已经落位的原子**、已占用的
站台格、已占用的旋转中心冲突。

### 17.1 实现（`ComposeAssembly.cpp`，~150 行）

- `InteriorFirst(targets)`：按已占邻居数降序（稳定排序）给出放置顺序。
- `ReagentPortFor(puzzle, type, calcify)`：单原子端口，或可钙化的 Salt。
- `BuildOrderedNetwork(puzzle, targets, types)`：
  1. 每个产物原子必须有**独立试剂端口**（否则要分子内的 freed atom，需要中继链）；
  2. 按 `InteriorFirst` 逐个用 `SwingCandidates` 试摆，只检查 `placed / stations / pivots`
     （产物格本身不算障碍）；
  3. **平衡带**（`G → 旋转 → g → 反向旋转`，与 `StationTape` 的不平衡形态相反），每个
     原子占一个错开的 `tick`，周期 = 最终 `tick`；
  4. `out-std` + `AppendBonders`。
- `SupportsOrderedNetwork(puzzle)` 暴露在 `ComposeAssembly.hpp`，`AssemblySolver.cpp` 的
  `composeOnline` 以「家族链什么都没产出」为前提接入。
- 变体顺序：**放在最后**，且只在 `variants.empty()` 时才构建。这样它绝不会顶掉任何既有
  变体，只能补上「所有既有形态都验证失败」的空位。
- `OPUSOPTIMA_ORDERED_NETWORK=off` 用于 A/B；`OPUSOPTIMA_COMPOSE_DEBUG=1` 打印每个变体
  的验证结果与度量。

### 17.2 域与验证结果（162 题）

`ordered=true` 的 29 题里：**27 题构建出的机器通过原生验证**，2 题
`did not complete within cycle limit`（`07d7fe2f`、`36344f51`），1 题
`solution contains an instruction conflict`（`58daac9a` 的 variant 0，随后 variant 1 通过）。
失败都被 `continue` 吞掉，不影响家族。

### 17.3 全语料 A/B（control = `OPUSOPTIMA_ORDERED_NETWORK=off`）

`result/bench-ordered-network-20261112/rows.json`，139 对可评分题：

```
control any-win= 65 cost= 63 cycles= 51 area= 60
variant any-win= 65 cost= 63 cycles= 51 area= 60
changed=2  improved=1  regressed=1
  improve a94b5296 cost 1690->1680 area 635->629
  regress fc6a70eb cost 5910->5920
```

两处增量串行 3× 隔离后都是负载噪声：`a94b5296` 两侧都是 `(1680, 629)` 三连一致；
`fc6a70eb` 三连 `(5890, 1713)`、`(5890, 1713)`、`(5900, 1713)`——单次 5900 是同一条路径
在并行负载下的抖动（多出来的两次验证消耗了 deadline，偶尔让较弱的候选被发布）。

**结论：网络是有效但零收益的兜底形态。** 原因是域本身：`ordered=true` 等价于
`SupportsComposedAssembly` 的试剂端口域减去帧密度假设，而这个域上的既有一臂/环组/静态
布点已经覆盖，网络只能在它们全失败的空位上补机器，而那些空位在语料上并不缺机器（家族的
行/轮/三合链已经在生产）。真正缺机器的 `fe57c5d3` 一类是**纯分子输入**（`Air/Fire/Salt`
都只存在于那一个分子里），它们需要的是**中继链**（freed atom 从分子落地位置摆到站台），
而中继链与站台臂争夺同一批旋转邻居——这正是 §16 量化的 12 臂 + 8 轨 + 7 活塞的来源。

### 17.4 状态

保留（默认开启、纯兜底、全语料零回退、104 项定向测试全绿）：它给 29 题补上了可验证的
机器，扩展了 `compose-assembly` 的域。未做的部分是**中继链**：那是下一轮唯一还没被证伪
的杠杆。

全套基线同时被修正。此前记录的是「11 项既有失败」，本次在**干净树**（`git stash` 掉全部
改动后重跑 1039 项）确认实际是 **15 项**：

```
AssemblyDiagnostics.{ComponentRequiresExactOptInAndSharesQuota,
  IneligibleCompactPreservesBaselineCandidatesAndQuota,
  StaticEmbeddingDefaultExplicitOffQuotaFirstAndDeadline,
  StaticReactionDefaultOffDomainQuotaAndFirst,
  SingletonChainUsesRemainingSlotAndRetainsFirstAndStatic,
  PortfolioDeduplicatesAndPreservesFirst,
  PortfolioRetainsFirstWhenAlternativesRejectOrBudgetExpires,
  PortfolioNeverExpandsCallerAcceptedCap}
AssemblyVerifierTest.{PortfolioArchivesDistinctRealStructures,
  PortfolioSemanticTransformsAndPermissionBoundary}
LinearInputArea.DirectIntakeIsDefaultAndOffRestoresFeeder
RigidCarrierTest.NoPlacementPreservesDownstreamOpportunity
ScheduleAudit.CompiledScheduledRowIsSchedulable
ScheduledRowCompanion.{IntakeCandidatesExposeTheFeederCompanion, FeederModeHasNoCompanion}
```

多出来的 4 项是**同套件内 `setenv` 泄漏**造成的顺序相关失败：它们在 `--gtest_filter` 里
单独跑全部通过（`LinearInputArea.*` / `ScheduleAudit.*` / `ScheduledRowCompanion.*` 一起跑
才失败），与本次改动无关（干净树上同样失败）。
