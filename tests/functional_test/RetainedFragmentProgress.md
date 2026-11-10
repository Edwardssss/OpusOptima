# 保留片段与化学前驱推进记录（2026-10-26）

> 历史记录：本文件保留第二题突破及 157/169 的当时状态。后续已接通 WARP 分阶段轮模块并发布原题；最新结果和完成边界见 [ThreeTargetAcceptance.md](ThreeTargetAcceptance.md)。不改写下列旧测量。

## 当前状态：第二个真实题突破，整体计划仍未完成

在上一轮 PHILOSOPHER’S CATALYST 通过的基础上，**BICRYSTAL TRANSCEIVER 的原始文件现已通过公开 CLI、独立验解和指标回写后二验**。WARP FUEL 也已有闭合且可重放的完整物料周期，但尚无机械发布结果；不能将它计为通过。

## BICRYSTAL 的实际缺口与修复

不是单纯扩大可达范围，也不是仅重试坐标：

1. `SerialFragmentMotion` 搜索完整刚性片段的真实旋转及串行换抓点，状态区分片段位置和朝向；每一步有明确机械臂基座、抓取原子、姿态与旋转指令，拒绝/耗尽/证书异常不混淆。
2. 源片段的继承边通过来源链追溯到完整输入批次。不同批次的等价供料姿态只有在**原子标签、完整键图及掩码的刚性对称性**被证明一致时，才复用物理端口；不是根据题名或 ID 分支。
3. 尚需投射的片段不能提前进入已有产品的常开 bonder 区，否则会先与其他片段合并，之后无法再单独搬去反应。调度会推迟这类交付，用实际反应后的新身份完成最终搬运。
4. 源料尚占据输入足迹时，空出的同一足迹格可以用于提取；最后一个原子仍在足迹内移动也会阻止再生。离开足迹后不假定能穿过已再生的输入。
5. 要取走被分支余料围住的金属时，先将余料真实搬入缓存；净化站复用前先清空出口处的中间产物。缓存候选失败可事务式回滚，再试其他槽位。
6. 物料周期完整闭合、所有机械臂返回起始姿态后，机器重复同一受验周期，不再为有限六产品重复构建相同物理 IR。未执行动作的臂不序列化。

原始发布文件：`result/bicrystal-shared-fragment-20261026.solution`。定向公开 CLI 使用原 **20 秒**外部限制通过，实测 **41346 cycles / 4910 instructions / 711 area / 4470 cost**。原题 `asset/puzzle/workshop-versions/w2946687073.puzzle` 未修改。

## 已确认的 WARP 化学前驱

固定版本外部验解器 `sim.c` 的 Calcify 与 Duplicate 使用原位元素标签转换，不要求被转换端点无键；新语义没有推广到 Purify/Animismus/Unify/Disperse 等消耗型反应。最小真实机器 `BondedCalcificationAndWheelDuplicationRetainColoredStar` 已经独立双验：四 Fire 彩键星保持掩码，把中心钙化为 Salt，再用真实固定轮的 Water 催化剂转为 Water。

共享表示新增：

- `ExecutionIR` 的带键 Calcify/Duplicate 以新端点身份保留原坐标和所有邻接键。
- `WheelCatalyst` 是显式设备引用，包含设备身份、所读四元元素及固定轮姿态，不创建、借用、消费或丢弃虚假的输入原子；设备姿态约束和可消耗库存分离。
- `ExecutionPlan::Task` 保留设备引用，读写集合覆盖整个受影响片段。
- `MaterialPlanning` 可以生成 Fire 装配前驱，先按 Fire–Fire 规则建彩键，再插入保键 Calcify/轮 Duplicate，最后在**原题**上输出和重放。缺许可、错误催化元素和改变固定设备姿态均拒绝。
- 联合需求排序把必须继承的片段作为连续组，先供应不需转化的端点，再完成金属升级，避免被反应中途未交付的同批原子堵住补料。整数配方候选与前驱/直接模型共享总状态预算。

原 WARP FUEL 在默认 **4096** 共享状态上限内找到闭合物料周期，本次为 **21 状态、1 个候选**，完整原题图重放通过。普通目标规则、Fire 才能创建彩键的限制，以及完整输入批次/余料闭合要求都没有放宽。

**剩余机械障碍：**这个物料周期仍需要把 Fire 彩键子装配的建键、钙化、轮复制和最终搬运分阶段绑定到真实模块。轮的实体占用使得不能将完整十原子产品直接放在同一复制站。现有串行后备对此明确报告 `needs a staged wheel-module binding`，不会把纯 IR 成功发布为解。

## 验证边界

完整回归：**543 项，542 通过、1 项可选资产 fixture 跳过、0 失败**。新回归覆盖原始 BICRYSTAL、原始 PHILOSOPHER’S CATALYST、刚性片段路径、拒绝证书/预算/异常、端点保键转换、轮设备身份与姿态、前驱阶段顺序和权限。

物理路径构建使用 `AppendFromTail` 逐步核对初态/尾快照并重新计算每个原子操作，避免每条指令都重放全部历史的二次成本；**整个程序在候选结束时仍执行完整 Replay 和 RequireComplete**，独立机械验解不变。篡改旧快照仍会在最终重放被拒绝。

## 当前代码完整 fresh 测量

最终报告：[coverage.json](../../result/local-retained-fragment-final-fresh-20261026/coverage.json)，[coverage.csv](../../result/local-retained-fragment-final-fresh-20261026/coverage.csv)。169 个原始文件均重新运行同一五种后端，共 845 次，每次外部尝试仍限 20 秒；没有使用失败子集、拼接历史通过或修改原题。

| 度量 | 上一轮真实突破 | 本轮 fresh |
| --- | ---: | ---: |
| 原始文件 | 156/169 | **157/169（92.90%）** |
| 按原始文件 SHA-256 去重 | 136/146 | **137/146（93.84%）** |
| journal | 37/37 | **37/37** |
| vial 独立验解缺口 | 11 个文件 | **11 个文件，保护不变** |
| 新增／回退 | — | **1／0** |

本轮新增唯一原题为 `w2946687073.puzzle`。对初始可信 155/169 基线，累计新增两个非生产原题、回退零。当前通过的两个目标的实测指标：

| 原题 | cycles | instructions | area | cost |
| --- | ---: | ---: | ---: | ---: |
| PHILOSOPHER’S CATALYST | 19539 | 2304 | 509 | 3080 |
| BICRYSTAL TRANSCEIVER | 41346 | 4910 | 711 | 4470 |

上一轮 PHILOSOPHER’S CATALYST 的有限展开机器与本轮闭合周期机器具有不同指标；旧报告仍保留历史身份，不伪称所有指标单调改善。本轮没有优化最优成本或周期的承诺。

唯一仍失败的非生产原题为 `w2946687209.puzzle`（WARP FUEL）；另外 11 条失败路径仍含 vial。WARP 的物料重放成功和转换子步骤机器成功都不计作整题成功，剩余分阶段轮模块绑定未实现，整体计划仍不能标为全部完成。

复验命令（扫描结果路径需换为新的空目录）：

```sh
cmake --build build -j 4 --target OpusOptimaTests OpusOptima
ctest --test-dir build --output-on-failure -j 2
node tests/functional_test/LocalCoverage.cjs result/<new-empty-directory>
```

