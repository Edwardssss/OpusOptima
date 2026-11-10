# B1＋B2 证据与独立验解基础：静态 CONDUIT 关卡（2026-10-27）

本文记录 B1/B2 当时的能力边界；后续 B3–B6 工作补齐了显示边界字段证据，并扩展有界机械执行子域。下文关于非默认显示标志及化学／多臂等仍不可用的描述是历史状态，不代表当前代码。CONDUIT 销毁账目已按修正后的拆键半周期语义更正为 10。后续 B3–B6 已完成当前题库验收，见 [完成报告](ProductionB3B6Progress.md)；本文保留 B1/B2 当时尚未完成阶段 B 的历史状态。

## 交付边界

本轮完成的是：删除没有匹配资产而跳过的 Diatomic 资产探测测试；取得独立于 omsim 的原游戏数据流证据；建立不依赖求解器 IR 的有界生产执行验解；让原始静态 CONDUIT 经过双执行引擎和指标回写复验后发布。

这不是完整阶段 B 验解器，也不表示其余生产规则或机器均已支持。当前独立执行子域之外仍必须返回 `Unavailable`，不能因为外部裁判通过就全部解禁。

## B1：实际游戏证据与先前假设的纠正

### 来源与复现

本机安装的 Opus Magnum：Steam app **558990**，build **24114900**，depot **558993** manifest **2291295597581982695**。核对的 `Lightning.exe` SHA-256：

`fd804fadedbf1b991ed37f3ec7325b2160317f0585e3e908fb73f246a038368d`

[GameProductionEvidence.py](GameProductionEvidence.py) 只用 Python 标准库读取 PE/CLI 元数据和 IL 字节，不加载程序集、不调用游戏、不执行下载的检查工具、不修改游戏存档。字段引用按 IL 指令边界识别，不通过在操作数字节中搜索相同数值误判引用。记录所有 **4995** 个方法定义及 **2881** 个非空 IL 方法体；`Sim` 的 **49/49** 个方法均有 IL，被纳入检查。

可复现：

```sh
python3 -I tests/functional_test/GameProductionEvidence.py <本机Lightning.exe路径> --audit-production
python3 -I tests/functional_test/GameProductionEvidence.test.py
```

[ProductionGameEvidence.json](ProductionGameEvidence.json) 保存版本哈希、方法 token、方法 IL 哈希和引用位置；未包含游戏可执行文件或完整方法反编译正文。游戏版本哈希不同会拒绝沿用这份规则审查。六项独立脚本测试检查指令边界、前缀/switch、截断和版本锁定；测试不要求安装游戏，不引入新的“无资产就跳过”。

### vial 的实际角色

在这个已核对游戏版本中，生产 vial 记录并未成为输出需求、物料库存或碰撞对象：

- `Puzzle` 读取器 `0x06000e01` 将序列化位置、style 布尔值和 count 派生的视觉变体数组传入记录构造器 `0x06000743`。
- 记录数组字段 `0x04000414`，位置 `0x0400069e`，style `0x0400069f`，视觉变体数组 `0x040006a0`。
- 相关引用集中在构造、序列化、生产显示边界和 `SolutionEditorBase` 绘制方法 `0x060011b3`；绘制读取视觉变体的 tuple 元素并传入图形绘制调用。
- `Puzzle` 的 `0x06000dff` 把 vial 位置和视觉数组长度纳入 `Bounds2.UnionedWith`。没有发现这些 vial 字段在 `Sim` 中的直接读取。

所以，**“omsim 没有 vial 运行逻辑”不能再单独推出“游戏一定存在一个尚未执行的 vial 机械规则”**。此前保守地将所有 vial 候选一概标为不可用是能力边界保护，不是游戏语义结论。现在有了原游戏版本锁定的数据流证据，并对首个有界机械子域新增独立执行检查，才能有依据地接收该子域中的原始 vial 文件。

这项结论来自静态数据流审查，不是实际启动游戏后取得的 replay 证书；也不是对未来游戏版本、动态反射或全部生产行为的等价性证明。运行库不会仅凭这个判断接受任意机器，仍要求下面的两个执行引擎都通过。

### shrink 字段不能凭名字推断

原先称为 `shrinkLeft/shrinkRight` 的字段，在核对版本中参与的是 `Bounds2.Min/Max.X` 的计算及派生生产显示边界；没有取得“运行若干周期后房间会变小”的证据。派生边界的读者还包括 solution bounds 和编辑器/录制画面路径。

**不能继续仅凭本地命名设计动态收缩时序规则。** 本轮只接收两个字段均为 false 的静态子域；非默认标志仍明确 `Unavailable`，等待单独完成相关布局、显示边界与模拟约束的进一步交叉核实。其他生产后端的旧保护未批量移除。

## B2：独立执行子域与双引擎发布

新增 [StaticProductionVerifier.cpp](../../src/Verification/StaticProductionVerifier.cpp) 和对应头文件，属于 `verification` 而非 `solvers`。它直接接收文件解析结果，自己建立原子身份、完整批次、键图、机械持有、实际指令周期、生产室及导管状态，**不调用 ExecutionIR、求解器状态效果、FragmentCustody、机械路径证书或旧 HexSimulator**。

当前子域：

- version-3，静态 Large 房间、有限普通输出、普通键连通输入/输出；
- 最多一个固定单抓手机械臂，真实 G/g/R/r/O 指令；
- 原始身份与有序格保持的一对导管，完整自由片段释放后转移；
- 普通成键、拆键、独立原子 disposal、输入再生及每输出 `6 × multiplier` 完成；
- 原子守恒和事件 trace：获取、同身份导管转移、输出、销毁、剩余库存；
- 权限、室内足迹、墙体、输入与到达占用、离散时间/坐标/库存边界及旋转区间检查。

未覆盖的 chemistry、Repeat、disjoint feeds、track/pivot、isolation、多臂同步、多导管共享、非默认边界标志与跨导管边界新增键等返回 `Unavailable`。特别排除了会涉及 omsim 已知 conduit cloning 差异的跨边界成键子域。

这是独立执行基础，不是已经替代完整碰撞裁判：区间实现包含细分扫掠检查，没有声称对任意连续几何完成形式证明；外部裁判仍独立检查完整机械碰撞。文件解码可共享 IO 工具，但执行账本和规则实现独立；外部裁判自己的原始文件解析还提供了另一条解码检查路径。

发布流程对带 vial 或非默认边界字段的候选：

1. 原文件交给固定版本 omsim，检查机械、完成和实测指标。原有明确拒绝不被新引擎覆盖。
2. 通过后，再在同一受 wall-clock 限制的隔离子进程中运行独立静态生产执行。
3. 两引擎必须都通过，并且完成周期完全一致；不一致为验证错误，未知子域为 `Unavailable`，均不可发布。
4. 回填实测指标后对原文件执行同样的双引擎复验，最后原子发布。任何失败保留原输出并删除临时候选。

外部库仍锁定原 SHA-256：`0610b5841ee2a5cca673b4a2f585ab017a8d94fca1ec67fe79a789237625b8c2`。未修改库或删除生产元数据来通过测试。

## 首个端到端关卡：原始静态 CONDUIT

原题：`asset/puzzle/edge-cases/c544897394273089.puzzle`，SHA-256 前缀 **28ecbc47**；保留原 vial、双 Large 室和三格导管。

公开 CLI 发布文件：`result/static-conduit-dual-verified-20261027.solution`。

实测：**43 cycles / 4 instructions / 67 area / 60 cost**。独立引擎记录 **6** 次同身份导管转移、**6** 次输出和 **10** 个销毁原子（最终输出完成时另外两个拆下原子尚未销毁）；获取数量与输出/销毁/剩余库存守恒。两引擎完成周期均为 **43**，且回填指标后二验成功。

回归包括：

- 原始文件、公开 solver/CLI 发布和指标二验；
- 六种全局朝向、平移，以及两种 vial style、count 的合法视觉变体，独立执行与原外部裁判周期一致；
- 删除 pipe/unbonder/disposal/output、改变导管有序格、到达格被不同元素占用、周期预算不足等不通过；
- 非默认边界/未知指令/化学/跨导管成键仍不可用；负数或超过有界视觉计数的 vial 记录不能扩大认证子域；已有输出不会被不可用候选替换；
- 删除了 `DiatomicVerifierTest.AssetFixturesIfAvailable`，保留 Diatomic 求解器及旋转/元素/顺序等实际执行的核心测试。

## 最终回归和 fresh

当前完整回归：**556 项全部通过、0 失败、0 跳过**，包含证据读取器、独立执行器、静态 CONDUIT 公开 CLI，以及原三题和其他现有回归。

最终全新目录的[JSON 报告](../../result/local-b1-b2-conduit-certified-fresh-20261027/coverage.json)和[CSV](../../result/local-b1-b2-conduit-certified-fresh-20261027/coverage.csv)，对所有 **169** 个原始文件逐一运行相同五种后端，共 **845** 次，逐次外部进程限制仍为 **20 秒**。没有 `--failed-from`、没有历史结果拼接。

| 度量 | 上轮三题完成 | 本轮 B1/B2 基础 |
| --- | ---: | ---: |
| 原始文件 | 158/169 | **159/169（94.08%）** |
| 原文件 SHA-256 去重 | 138/146 | **139/146（95.21%）** |
| journal | 37/37 | **37/37** |
| 未取得可接收独立生产 profile 的文件 | 11 | **10** |
| 新增／回退 | — | **1／0** |

新增唯一原始文件为静态 `c544897394273089.puzzle`。该行明确记录 `verificationAuthority: libverify-and-independent-static-production`、`verificationCapability: verified-static-production-profile`，不再简单按“存在 vial”就一概贴不可用标签。未接受的生产实例仍保留未知 profile 状态，不将它们说成已解。

本轮新增通过来自**证据支持的验解能力接通已有候选**，不是增加逐题模板，也不是删除 vial 字段或恢复旧 pass bit。先前所有通过文件仍通过。剩余 **10 文件／7 个字节不同实例**仍都是生产题；其布局、物料、运输及独立执行 profile 的障碍没有被本轮自动解决。

复验（输出/扫描应使用全新的路径）：

```sh
cmake --build build -j 4 --target OpusOptimaTests OpusOptima
ctest --test-dir build --output-on-failure -j 2
ctest --test-dir build --output-on-failure -R 'IndependentStaticProduction|CliStaticVialConduit|GameProductionEvidenceReader'
node tests/functional_test/LocalCoverage.cjs result/<new-empty-directory>
```

**结论：用户指定的 B1＋B2 证据及有界独立验解基础、首个静态 CONDUIT 端到端关卡已完成；完整阶段 B 尚未完成。**
