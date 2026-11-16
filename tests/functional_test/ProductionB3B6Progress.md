# B3–B6 完成验收：有界生产子域与剩余原题（2026-10-27）

本文保留阶段完成时的 614 项回归及其 fresh 验收。后续工作区整理、Review 修复和最新的 622 项回归／零回退扫描见仓库 README。

## 完成口径

本阶段以**保留原始 puzzle、公开 CLI 生成真实机器、两个执行引擎通过、完成周期一致、实测指标回填后二验和原子发布**为生产题完成标准。物料 IR、合成变体和实验 prototype 均不单独计入题库覆盖。

这里的“完成”针对已列明的 B3–B6 工作及当前 169 文件题库，不是任意未来生产题、所有游戏规则或连续几何的完备求解证明。没有删除未知机械子域的 `Unavailable` 保护，没有修改原题权限或生产元数据。

## B3：独立执行与物理规划接通

独立执行器直接读取原始 puzzle 与序列化 solution，自己执行机械指令、原子身份／键图、输入再生、化学、抓取、导管、库存和输出；不调用求解器 `ExecutionIR`、调度结果、物料效果、路径证书或旧模拟器。

扩展路径经固定 native omsim 差分，修复了磁带起点／周期、抓取状态锁存、piston／track 端点 no-op、多重成键几何、刚拆键原子的半周期反应资格及同时移动底座碰撞等缺陷。差分覆盖七种化学 glyph、多抓手、独立时序多臂、正负磁带偏移、停止磁带、pivot、静态／转动 wheel 和故意碰撞负例。

新物理能力包括：

- 同一输出端口的不同**产品副本身份**进入继承边映射，避免跨成品错误保留输入键；
- wheel 催化剂显式绑定设备／元素／方向，支持 cardinal→Salt→wheel 转换的真实新原子身份，不虚造物料催化剂；
- `ChemicalPlanner::PlanWithWheelCatalysts` 显式建模设备催化剂；旧物料后端的 `Plan` 不会因此选择无法物理执行的 wheel 配方，避免非生产题超时回退；
- 紧凑室内反应站复用、有限缓存、可旋转 piston／track 运输，以及 always-on 成键后的持有运动约束；
- 显式物理 wheel 绑定与接触检查，原有调用未声明设备时仍拒绝 wheel lowering；
- 静止碰撞对只检查一次，移动对仍保持 64 个扫掠采样；静止距离不随采样时间变化，不改变碰撞阈值或 native 必要性。

原始 CONDUIT 仍为 **43 cycles / 4 instructions / 67 area / 60 cost**。拆键半周期语义修正后，其完成时是 **10 个已销毁原子**，另外两个拆下原子仍在库存中；先前的 12 个销毁记录已更正。六次输出、六次同身份导管转移和完整物料守恒仍成立。

## B4：按原游戏证据纠正“动态收缩”前提

锁定游戏程序集 SHA-256：

`fd804fadedbf1b991ed37f3ec7325b2160317f0585e3e908fb73f246a038368d`

schema-2 [ProductionGameEvidence.json](ProductionGameEvidence.json) 固定以下事实：

- `Puzzle::06000dff` 左标志分支对 `Bounds2.Min.X` 加 **33.0f**，右标志分支对 `Bounds2.Max.X` 减 **33.0f**，随后写入 derived bounds `04000415`；
- `Solution::0600117f` 的全部直接消费者为编辑器画面中心路径 `060007a6` 和录制画面路径 `0600129e`，均记录方法 token、IL SHA-256 及引用位置；
- 49 个 Sim 方法没有读取两个标志、派生 bounds 或相关 bounds 方法。

因此，这个固定版本的字段是**显示边界内缩／裁切标志，不是随周期缩小生产房间**。没有实现推测的收缩周期，也没有把 33 像素转换成六角格限制。四种标志组合保留在文件中，实际房间墙体、库存及机械运动继续独立检查。

证据读取器 **8/8** 通过；从本机原游戏重新生成的审计结构与保存 JSON 完全相同。CONDUIT 的旋转／平移、两种 vial style 和四种标志组合，以及化学／导管 profile 变体，均检查双引擎周期和元数据保留。

这是**版本锁定的静态 IL 数据流证据**，不是启动原游戏所得 replay 证书，也不覆盖未来版本或任意动态反射。

## B5：七个字节不同实例的真实机械后端

所有新后端都按分子图、权限、房间几何、导管拓扑及局部 frame 匹配；不按题名分支，不读取预制 solution、缓存部件表或运输磁带。已生成机器均走原题公共验解／发布路径。

### 房间内装配与紧凑化学

[ProductionAssembly.cpp](../../src/Solvers/Production/ProductionAssembly.cpp) 枚举房间、输出 frame、piston 底座及供料接触，使用真实长度／朝向姿态 BFS 和成键顺序搜索。未使用的导管仍保留身份、有序格及端点所属房间，避开所有实际释放位置。它解决 CONDUIT BUG，不依赖 conduit cloning。

[AutomaticSupply.cpp](../../src/Solvers/Planning/AutomaticSupply.cpp) 新增紧凑室内域搜索，复用一个投射站和一个钙化站、有限缓存及短轨 piston，实际执行 Artificial Ore 的两产品库存周期。既有非生产后端保持原时间预算；紧凑生产布局单独使用四秒有界预算，公开 CLI 仍受相同 **20 秒**外部限制。

### 有限共产品库存

[ProductionFiniteStock.cpp](../../src/Solvers/Production/ProductionFiniteStock.cpp) 用真实导管把 AETHER REACTOR 的 Salt 共产品存入两个 SmallWider 室；库存动作由占用／墙体／底座约束下的 BFS 重新生成。最后一次入库可在不同终止臂姿态结束，无需不必要的复位。原题完成时 **acquired=91、remaining=31、disposed=0**，其中 24 个 Salt 实际缓存，另有已再生的完整下一批 7 原子。

这说明有限输出需求不必强制使用无限闭合物料周期；但残料必须有真实位置、容量和机械搬运，不能当作隐式销毁。

### 环形处理与直接完整供料覆盖

[ProductionRingProcessing.cpp](../../src/Solvers/Production/ProductionRingProcessing.cpp) 识别完整六元环及两个三原子目标图，使用六抓手统一处理、真实 wheel 转换和 piston／track 搬运，解决 PROBE MODULE。wheel 与主环转动错开以避免碰撞；真实机械计划包含额外合法转换，不冒充最小物料配方。

[ProductionDirectFeed.cpp](../../src/Solvers/Production/ProductionDirectFeed.cpp) 搜索两个完整供料图对目标顶点的不交覆盖、逐位置转换、继承边拆除／重建和静态 wheel 域，解决 DWARVEN FIRE WINE。原题无需运动指令；实际使用一次钙化及两次复制，不是最小配方的一次复制。

### 多室反应与片段装配

[ProductionPetalAssembly.cpp](../../src/Solvers/Production/ProductionPetalAssembly.cpp) 在一个小室生成 Water–Vitae 花瓣，经原始双格导管运输；另一小室用真实 wheel 生成 Earth，经单格导管运输；中央 Large 室用 arm3、piston 和短轨装配七原子产品。EMERGENCY ANTIDOTE 完成时 **acquired=78、remaining=4、disposed=32**，记录 **24 次真实导管转移**。

[ProductionDistributedPurification.cpp](../../src/Solvers/Production/ProductionDistributedPurification.cpp) 绑定四个完整输入图、两个输出图和实际导管树，重新计算四个房间内的拆分、运输、中继和中央装配 BFS。两个产品的周期消耗每种输入一批、五次净化，不用 wheel 或销毁。不同房间的空臂复位后缀可并行，同一臂的任务仍顺序，Overloaded 完成时 **acquired=60、remaining=15、disposed=0**。

### 原题公开 CLI 指标

| 字节不同实例 | Cycles | Instructions | Area | Cost |
| --- | ---: | ---: | ---: | ---: |
| CONDUIT BUG | 335 | 28 | 53 | 80 |
| Artificial Ore | 740 | 222 | 44 | 155 |
| AETHER REACTOR | 238 | 426 | 85 | 270 |
| PROBE MODULE | 187 | 36 | 68 | 340 |
| DWARVEN FIRE WINE | 3 | 0 | 64 | 190 |
| EMERGENCY ANTIDOTE | 228 | 42 | 87 | 330 |
| Overloaded | 813 | 318 | 125 | 400 |

其中 Wine、Antidote、Overloaded 各有一个字节相同的 workshop 文件；七个不同实例对应十个新增通过文件，不计作十个独立算法突破。

## B6：发布保护、负例与完整回归

局部布局搜索在相同预算内交错尝试不同朝向，不再先把同一朝向的所有站距耗尽后才尝试其他朝向；旧的宽片段库存回归已加入重复运行检查。最终扫描前发现的 CONVERSION Z-ORDER 和两个 HIGH GLOSS FINISH 文件超时已修复为显式 wheel 物理能力选择，并新增三个原题 20 秒 CLI 回归；没有通过提高外部限制或合并旧结果掩盖回退。

所有新原题及重复 workshop 文件都有公开 CLI 回归，采用 **20 秒**外部限制。变体覆盖输入／输出局部旋转和平移、原子／键／端口顺序、房间／导管重排、合法有序导管 frame、题名变化、显示元数据及 multiplier。破坏机器的负例包括删除化学 glyph、pipe、轨道、库存臂、输出等；被拒绝候选不得覆盖已有输出。拒绝回调和基础设施异常仍不能返回未经接受的候选。

完整最终 CTest：**614/614 通过，0 失败、0 跳过**。见[完整回归日志](../../result/local-b3-b6-final-accepted-fresh-20261027/regression.log)和[构建日志](../../result/local-b3-b6-final-accepted-fresh-20261027/build.log)。另外使用最终代码重新对七个不同原题机器逐一双验，见[独立重验日志](../../result/local-b3-b6-final-accepted-fresh-20261027/original-dual-recheck.log)。

未知 part／instruction／chamber、Repeat／未展开 repeat/reset、disjoint feeds、循环或歧义轨道、未建立的协作曲线运动，以及 conduit cloning／跨导管边界新增键等仍失败关闭或返回 `Unavailable`。原子、部件、周期、坐标和视觉元数据边界均保留。

独立扫掠仍是采样，不宣称任意连续几何的形式证明；**native 连续碰撞裁判始终是发布必要条件**。任一引擎拒绝、不可用、超时或周期不一致，都不能发布。外部库 SHA-256 保持：

`0610b5841ee2a5cca673b4a2f585ab017a8d94fca1ec67fe79a789237625b8c2`

## 最终 fresh 验收与复现

最终完整 fresh 已通过：

| 口径 | 最终结果 |
| --- | ---: |
| 原始文件 | **169/169，100%** |
| 按原文件字节去重 | **146/146，100%** |
| journal | **37/37，100%** |
| 相对 B1/B2 的 159 文件起点 | **新增 10，回退 0** |
| 相对用户给出的 158 文件基线 | **新增 11，回退 0** |
| 完整 CTest | **614 通过，0 失败、0 跳过** |
| 仍未通过的原始文件 | **0** |

[基线比较与七个不同原题的验解来源／指标](../../result/local-b3-b6-final-accepted-fresh-20261027/comparison.json)已单独重算：169 行、845 次尝试、每行原始文件 SHA-256、所有发布文件均核对成立；本阶段新增的十个生产文件全部具有 `libverify-and-independent-static-production` 验解来源。

因此，**B3、按已确认真实语义落实的 B4、剩余实例攻克 B5、发布保护与最终 fresh 验收 B6 均已完成**。这不包含猜测的动态收缩规则，不表示未知规则自动通过，也不宣称任意新题可解。

完整 [JSON](../../result/local-b3-b6-final-accepted-fresh-20261027/coverage.json)／[CSV](../../result/local-b3-b6-final-accepted-fresh-20261027/coverage.csv)保留每次尝试的日志、原文件 SHA-256、验解来源和发布指标。扫描使用全新目录，完整回归结束后单独运行，对 169 个原始文件逐一执行同一五种后端，共 845 次，每次外部限制 20 秒；不使用 `--failed-from`，不拼接历史结果，也不把合成测试加入分母。

```sh
cmake --build build -j 4 --target OpusOptimaTests OpusOptima
ctest --test-dir build --output-on-failure -j 1
python3 -I tests/functional_test/GameProductionEvidence.test.py
python3 -I tests/functional_test/GameProductionEvidence.py <Lightning.exe> --audit-production
node tests/functional_test/LocalCoverage.cjs result/<new-empty-directory>
```
