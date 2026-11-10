# 真实未解题突破记录（2026-10-26）

> 历史里程碑：本文记录首次 155→156 的真实原题突破。后续 BICRYSTAL 已独立发布、WARP 已闭合物料周期，当前状态与剩余机械边界见 [RetainedFragmentProgress.md](RetainedFragmentProgress.md)。下列旧测量不改写为新结果。

## 状态：部分突破，原实施计划未全部完成

上一轮 155/169 的扫描与合成能力测试仍是历史证据，但据此宣布完整阶段 A 完成是不准确的。本轮以三个真实非生产未解题为目标，目前已独立发布 PHILOSOPHER’S CATALYST；BICRYSTAL TRANSCEIVER 仅完成可重放物料周期，WARP FUEL 仍在当前物料搜索域内耗尽。不得把后两者记为成功，也不得宣称六步计划全部完成。

## 已确认的语义和实现

固定版本 `ianh/omsim`（提交 `a88d12d8d100784aa525ee90259255b869bcba38`）[sim.c:511–519](https://github.com/ianh/omsim/blob/a88d12d8d100784aa525ee90259255b869bcba38/sim.c#L511-L519) 的投射规则检查被消耗的水银无键、无隐含断连键且未被抓取，没有禁止金属端带键；[transform_atom:145–149](https://github.com/ianh/omsim/blob/a88d12d8d100784aa525ee90259255b869bcba38/sim.c#L145-L149) 只改元素标签。核对的 `sim.c` 原文 SHA-256 为 `a7e23da18c225ec24b2337ed691fcc0a468bc27c7b87d4b8dc6644c10a08491f`。

- `ExecutionIR::Project` 在金属带键时保留全部相对坐标和邻接键掩码，以新 ID 替换金属端；水银必须为独立原子。其他尚未核实的带键反应保持拒绝。
- `FragmentCustody::ProjectHeld` 与 `PhysicalInventory::ReactHeld` 事务式重绑定持有端，消费停放的水银；证书拒绝不改变库存、持有权、停车槽或指令。
- `InventorySearch`/`SupplyExecutionIR` 支持在投射过程中保留必须继承的金属片段，明确重映射边、片段朝向与来源身份；不再只允许 `Take + None` 的原元素直送。
- `ExecutionPlan` 的 Project 读写集合包含整个被触及片段，防止端点变型绕开其他成员的依赖。

独立机械回归 `InventoryProjectionPreservesBondedMetalAndInheritedColor` 分别保留普通键和 triplex，完成真实文件验解、指标回写、二验；单测另覆盖掩码 1/2/6/8/14、带键水银拒绝、权限与回滚。

## 物料域与机械域分离

新增 `SearchMaterialExecution` 不要求 track/piston，也不使用轨道暴露顺序作为全局过滤条件。它在共享状态/候选预算和合法周期中寻找经 IR 重放确认的闭合物料计划。候选仅代表支持的化学模型下守恒闭合，不能当作机器发布。

本轮诊断（默认共享 4096 展开状态）：

| 原题 | 物料结果 | 当前机械状态 |
| --- | --- | --- |
| `w2868339730` PHILOSOPHER’S CATALYST | Found，12 状态，1 个完整候选 | 共享计划驱动的串行固定臂机器通过原始文件独立双验和 CLI 发布 |
| `w2946687073` BICRYSTAL TRANSCEIVER | Found，35 状态，1 个完整候选；含保键投射、净化和余料处置 | 当前串行执行器明确拒绝需要搬运的非重建彩键片段；宽片段交接仍需实现 |
| `w2946687209` WARP FUEL | Exhausted，4096 状态，尚无完整候选 | 不能推断物料无解；所需带键四元转化/合法前驱历史尚未贯通 |

原始 fixture 未修改；算法没有题名或 workshop ID 分支。原题路径只在回归测试和这份报告中用于选择输入。

## 第一个真实机器突破

`SerialArmExecution` 事前消费 `ExecutionPlan::tasks`，在一个物理账本中执行完整批次获取、实际拆键、投射、库存停车、多个普通键接合和输出。模块选择固定臂格局及端口接近方向，搜索真实旋转与串行释放/抓取交接，不用物料 Relocate 伪造机械平移。每个物料周期核对库存闭合；生成有限六产品机器。没有访问 puzzle 名称/ID决定算法。

PHILOSOPHER’S CATALYST 通过原始权限集运行，不需要 track 或 piston：

- 原题：`asset/puzzle/workshop-versions/w2868339730.puzzle`
- 独立发布文件：`result/trackless-metal-ring-pruned-20261026.solution`
- 实测：**19193 cycles / 13128 instructions / 392 area / 2460 cost**。
- `PublishVerifiedSolution` 对原始题独立验解，写回实测指标后二验，再原子发布；定向运行使用未放宽的 20 秒外部进程限制通过。

初版放置了许多未使用机械臂，虽通过验解但超过原 20 秒进程限制；删除未执行任何动作的臂并重新编号后重新双验，才使用上面的发布结果。没有放宽裁判或扫描时限。

## 回归与未完成事项

完整 CTest：**536 项，535 通过、1 项可选资产跳过、0 失败**。中间出现过 `WideExtractionInventoryBuffers` 的布局时间片耗尽；在能够合法重建键时先尝试单原子提取、必须继承片段时仍优先保留片段，保持原共享时间/状态预算后，完整回归通过。没有按测试名或题目调整阈值。

当前串行后备仍有明确边界：保留彩键的大分子搬运未接入，复杂带键四元化学未贯通，任务顺序仍以已重放的串行合法顺序为主，没有完成所有计划中的有界调度重排。以上是实际剩余工作，不是“不可修复”结论。阶段 B 的 vial/收缩保护不变。

## 当前代码完整 fresh 测量

报告：[coverage.json](../../result/local-real-target-breakthrough-fresh-20261026/coverage.json)，[coverage.csv](../../result/local-real-target-breakthrough-fresh-20261026/coverage.csv)。对 169 个原始文件重新运行 `single-atom`、`diatomic`、`assembly`、`greedy`、`milp`，共 845 次，逐次外部进程限 20 秒；使用全新目录，没有使用 `--failed-from`，没有拼接历史通过。

| 度量 | 上轮可信基线 | 本轮 fresh |
| --- | ---: | ---: |
| 原始文件 | 155/169 | **156/169（92.31%）** |
| 重新按原文件 SHA-256 去重 | 135/146 | **136/146（93.15%）** |
| journal | 37/37 | **37/37** |
| 已知 vial 验解缺口 | 11 个文件 | **11 个文件，保护不变** |
| 新增／回退 | — | **1／0** |

新增唯一原题为 `asset/puzzle/workshop-versions/w2868339730.puzzle`，assembly 后端正常退出并独立发布，实测指标为 19193/13128/392/2460（cycles/instructions/area/cost）。此前所有通过文件仍然通过。剩余非生产失败为 `w2946687073` 与 `w2946687209`；此外仍有 11 个含 vial 的失败路径。本轮 156/169 是新增无轨金属环原题的真实通过，与更早误计未核实 vial 候选的旧 156/169 **不是同一个通过集合**。

结论：取得一个真实原题突破，但三题目标和原整体组合执行计划未全部完成。
