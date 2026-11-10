# 剩余组合执行链：阶段 A 验收记录（2026-10-26）

> **完成声明纠正：**本文件记录的旧测试与 155/169 扫描仍有效，但它们只证明一组有界子域，不足以证明原完整阶段 A 全部完成。尤其事后计划投影、单片段加 singleton 后备及限定生产反应模块不能等同于通用调度。后续三题实施的首次原题突破保留在 [RealTargetProgress.md](RealTargetProgress.md)，三题最终原始文件验证及最新范围见 [ThreeTargetAcceptance.md](ThreeTargetAcceptance.md)。新一轮三题目标与更宽泛的原阶段 A 全规则/任意组合要求不能混为同一个完成声明。

本记录面向维护求解器的开发者。阶段 A 是**有界、可独立验解的组合执行子域**，不等同于所有规则或任意可解题通用求解；阶段 B 的 vial、收缩规则及自研独立验解器均不在本轮交付中。合成 puzzle 必须写成真正的 `.puzzle`／`.solution` 文件，经原有外部验解器确认、回填实测指标，再对回填文件复验；物料 IR replay 和内部几何检查只作为额外保护。

## 基线与失败归因（A1）

详细的 11 个字节不同失败实例、权限、物料图、房间/导管规则、首个有证据的阻断层、阶段 B 边界及测试矩阵在 [RemainingCompositionA1.md](RemainingCompositionA1.md)。可信基线为 `result/local-chain-fragment-pivot-fresh-20261026/coverage.json`：155/169，字节去重 135/146，journal 37/37。不能把本轮代码的合成例计入这个分母，也不能把有界搜索耗尽表述成无解证明。

## 已接入的能力与复验锚点（A2—A6）

| 阶段 | 生产接入与真实机器正例 | 对应的保护和负例 |
| --- | --- | --- |
| A2 物料／物理分层 | `ExecutionPlan` 从真实 `ExecutionIR` 的已重放快照提取原子/批次、保留/修改的键掩码、共产品与催化剂来源、输出端口/周期序号、缓存生命周期和依赖；`LowerInventoryIR` 的 committed-step observer 记录已认证轨道与固定臂动作，原生产元数据由 `ProductionExecution` 单账本保留。`DisassemblyVerifierTest.SamePairMaterialChoosesRailOrFixedArmAfterRejection` 对同一物料需求实际验解两种机械实现。 | `ExecutionPlan.RejectsMutatedMaterialSnapshots`、`InventoryIR.ObserverReportsOnlyCommittedMaterialStepsAndPropagatesExceptions`；IR replay 不发布候选。 |
| A3 片段感知调度 | `SearchJointSupplyInventory` 在扁平化需求旁保留原输出图，在禁建键时要求同批刚性继承，尝试片段优先与可暴露进料两种排序，并在拒绝后枚举不同来源。`DisassemblyVerifierTest.AutomaticJoinsInheritedFragmentsFromSeparateFeeds`、`AutomaticIdenticalFragmentsRouteToDifferentColoredOutputs`、`AutomaticInheritedFragmentJoinsConvertedSecondFeed` 各有独立文件验解与指标二验。静态生产的目的室反应还先消费原生产谜题上的 `JointSupplyPlan`，再核对真实跨室账本的物料操作义务。 | `JointInventorySearch.RejectsMissingMaskAndNonRigidMandatoryGeometry`、`RejectFirstFragmentMappingAndAcceptSecondWithinSharedBudget`，以及原有普通/triplex/部分颜色掩码回归。非 Fire 边不靠不存在的 prisma 重建；不声称任意选择性删色可行。 |
| A4 替代机械 | 原有受认证轨道旋转/重抓取路径仍可用；新增由 `ConstraintLayout` 选择位置、`LowerInventoryIR` 执行真固定臂旋转与接合的后备。`FixedArmJoinsWideInheritedFragmentBeyondThreeRows` 的八原子产物所有朝向行跨度都超过三行，`FixedArmJoinsIndependentFeedsWithoutTrackOrPiston` 及 `FixedArmWideFragmentWithoutTrackOrPiston` 无轨道/活塞也双验。 | `FixedArmIntervals.RejectsInteriorSweptAtomWithFreeEndPoses` 拒绝端点空闲但扫掠相撞的动作；缺普通 bonder/抓取权限、错误输出端口或重叠占用均拒绝。原有 `ModuleHandoffTest` 拒绝同一出口时间窗冲突。最终非晶格碰撞仍以独立验解为准。 |
| A5 静态生产／导管 | `ExecutionIR::ForStaticProduction` 明确保留原生产规则；`ProductionExecution` 在源室取得完整批次，按真实导管身份/有序格搬运完整片段，分离释放、到达、取走与房间库存。已有刚性运输、无化学变化中继、无分期共产品也经过单账本检查。`ProductionTransport.BondedConduitFeedSplitsBeforeJointDispersion` 将带键供料拆开后在目的室完成四份反应共产品及第五份余料；`BondedConduitFeedReturnsBorrowedCatalystAcrossTwoOutputs` 支持四种四元催化剂，两个输出共同闭合周期。两类均完成真实双验、输出顺序/导管朝向变体及公开 solver 接入。 | `ProductionExecution` 的早取、重复交接、跨室错权、到达格占用、错导管、错掩码、序列复用与回滚测试；vial/收缩绝不通过移除元数据绕过。房间/运动局部检查不冒充游戏验解。 |
| A6 有界反馈 | 轨道物理错误可携带 IR 操作索引、片段原子、目标格及布局或状态预算类别；生产反应失败报告室/导管/机械模式/已知冲突格。`PhysicalCandidateKey` 去除对象顺序及指标变化造成的重复验证。`AutomaticRejectedFragmentSourceReplansToDistinctVerifiedMachine` 和 `FixedArmRejectsFirstLayoutAndKeepsPhysicalPermissions` 强制拒绝首台机器后更换真实决策并独立通过。 | 单独标记 `VerificationUnavailable` 与确定的独立拒绝；`InventoryIR.FailedPhysicalDeliveryCarriesAtomOperationAndTarget`、`ProductionTransport.JointConduitFeedbackRetainsLocalDecisionScope` 及已有回调异常、预算耗尽测试检查异常不被转换为无解证明。 |

## 历史全量检查（不代表 A7 全部要求完成）

**历史测量：以下有界组合子域已接入；当轮本地题库通过集合与可信基线相同，没有新增，也没有回退。** 使用当时的代码构建 `OpusOptimaTests` 和 `OpusOptima` 后，完整 CTest 登记 **527** 项：**526 通过、1 项可选资产 fixture 跳过、0 失败**。这一数字不是题库覆盖率。

最终全新目录的[完整 JSON 报告](../../result/local-composition-phase-a-accepted-fresh-20261026/coverage.json)与[CSV](../../result/local-composition-phase-a-accepted-fresh-20261026/coverage.csv)由以下命令生成；没有 `--failed-from`，没有拼接历史解。**复跑时须把结果路径换成另一个全新的空目录，脚本会拒绝覆盖旧解文件。**脚本对 **169** 个原始文件逐一尝试 `single-atom`、`diatomic`、`assembly`、`greedy`、`milp`，共 **845** 次，每次外部进程限 **20 秒**。成功标准为 CLI 正常退出、写出解文件并报告独立验解通过；CLI 发布路径还对实测指标回写后的文件二次验解。

```sh
cmake --build build -j 4 --target OpusOptimaTests OpusOptima
ctest --test-dir build --output-on-failure -j 2
node tests/functional_test/LocalCoverage.cjs result/local-composition-phase-a-accepted-fresh-20261026
```

| 度量 | 可信基线 | 最终 fresh | 新增／回退 |
| --- | ---: | ---: | ---: |
| 文件 | 155/169（91.72%） | **155/169（91.72%）** | 0／0 |
| 原文件字节的 SHA-256 去重 | 135/146（92.47%） | **135/146（92.47%）** | 0／0 |
| journal | 37/37 | **37/37** | 0／0 |
| 已知 vial 独立验解缺口 | 11 个文件 | **11 个文件**（8 个字节不同实例） | 未放宽保护 |

两次完整报告按文件路径比较，通过集完全一致。最终 14 个失败路径对应 11 个不同字节实例，按最早有证据的层和独立验解边界分别解释如下；**有限搜索失败均不是游戏无解证明**：

1. **生产特殊规则，11 个路径／8 个字节实例：**全部含 vial，本轮不能发表未获 vial 语义独立判定的解。CONDUIT 已产生片段化候选，独立报告为不可用；其余实例还存在目的室放置/导管路由、机械片段重叠或有界联合周期未闭合等各自的前置障碍，不能仅据终端库存错误归因。收缩规则同属阶段 B，未通过移除规则提高计数。
2. **非生产权限，1 个实例（序号 159）：**原有自动轨道先缺 bit 3；新固定臂后备验证了可行的受限组合，但该实例同时含两组三原子金属/水银料及八原子环，超出本轮有界片段接合与化学联动的已验范围。尚不能断言其物料不可行。
3. **非生产输出几何，2 个实例（序号 166、167）：**原轨道在六种朝向均先因产品范围受限；新固定臂宽片段模式可装配对应的通用测试形状，却不涵盖这两例完整的金属、triplex/有色键和链式反应历史。后续物料可行性仍待诊断。

上述机器只证明新增有界组合子域，不足以判定原阶段 A 的统一调度及所有组合要求全部完成，也不构成全题通过率承诺。阶段 B 的 vial/收缩语义与独立验解来源尚未实现，本报告没有将其标作阶段 A 已支持规则。
