# 工作区整理、Review 与下一步建议（2026-10-27）

## 第三轮普通源片段保留收尾（2026-11-06）

已接入持续同ID/普通键源片段的可选机械子解，父解先验、原共享3秒/20秒与三槽/首解策略不改。真实双二聚体从 cost/cycles/area 1230/7830/218 降至 890/4743/204；旋转平移、反序、混合键及输出坐标变换通过。修复输出常开bonder提前粘连和再生feed运动障碍遗漏。留出双弯曲三聚体仍拒绝；完整CLI已有automatic-inventory更优，启禁新机制三槽相同，不能声称全局收益或unseen覆盖。见[第三轮实际证据](../result/solver-general-capabilities-round3/report.md)。

本机制完成即停止扩展。后续里程碑使用该目录固定 puzzle/parent/retained、共享预算archive接口和消融开关复验，再做整体覆盖/对标；不扩大profile认证，不继续为留出失败调参数。

## 第二轮构造期运输流水化收尾（2026-11-05）

已完成窄 scalar relay custody 流水化：不同且已对齐接收臂在发送后窗口提前抓取，任务/材料依赖及输入输出相位不改，完整legacy gate后进三槽。两真实有限结构各两布局，离线四配对cycles改善0.123%/1.323%，cost/area不变；生产式共享3秒 parent-first 补证仅较快输入获益，较慢输入父解耗尽预算正确拒绝子解。初始8次production材料IR拒绝、构建及单测失败均保留，不称unseen或对标打平。见[第二轮证据与限制](../result/solver-general-capabilities-round2/report.md)。无跨任务独立装配、片段或共产品新能力。

该轮提出的片段缺口现由上方第三轮有限实现收尾；共产品新机制本轮未扩展。完整覆盖/双方对标留主会话里程碑，不在本轮跑全162。

## 首轮通用关键路径收尾（2026-11-05）

已收尾末端空手 arm1 回位并行化（有条件替代 local-v4，真实 gate 后入三槽）；不新增片段/共产品构造。3个真实已见输入 cycles 改善4.83%–6.41%，cost/area不变。继承29个成功旋转候选、3个共享20s进程失败；本轮只补7个缺失项，合计36成功候选/18对（3题各6布局旋转，不是unseen）。单题CLI cycles槽19503→18289，cost/area不变。原注册heldOut称谓、旧hash缺失与失败均保留限制，详见[首轮完整继承与补证报告](../result/solver-general-capabilities-round1/report.md)。原legacy/profile/2-10-20和24门检上限不变。

当时下一机制为**运输/流水化调度**，现由上方第二轮窄实现收尾。完整最终对标仍未执行，不以已见题或合成回归声称外部泛化。

## 最新获批四步（取代下方历史执行优先级，历史保留）

1. **泛化构造反馈/能力与失败分类**：材料计划接受回调内进行机械 lowering 和布局/裁判接受；拒绝后在共享剩余预算内继续材料候选，首个真正可行解稳定返回。见[第1步执行报告](../result/solver-material-feedback-step1/report.md)。
2. **候选验解与三代表档案**：求解中保留恰好三个目标代表槽 cost / cycles / area，允许同一真实已验解占多个槽，不强制三解不同；“最低”仅限当前已发现验证通过候选，不承诺全局最优。本步已实施，见[第2步报告](../result/solver-objectives-step2/report.md)。
3. **少量互补构造家族**：以语义能力而非题名/hash 路由，不硬编码对手答案。已实施有界普通装配 portfolio（串行行布局/共享工作站/供料间距），见[第3步内部报告](../result/solver-portfolio-step3/report.md)。
4. **局部优化与预算调度**：已核实有界单轮邻域、真实门检三槽与共享截止；pair/chain开发fixture分别重现area 13→12、23→20，人工劣化回归重现三主目标改善。保留 legacy/profile 和 2/10/20 默认预算；合作式截止并非硬实时20秒保证。详见[第4步收尾证据](../result/solver-local-optimization-step4/report.md)。

四步完成后再统一重测覆盖与真实 OpusSolver 对标；旧已见题不称 unseen。
旧独立认证开发继续暂停；固定 omsim 第三方、legacy 保护、显式旧后端兼容继续保留，assembly 仍推荐入口。
本包完成第1–4步限定实现；第4步采用最多3父解×8个确定性局部候选与13/15/20秒阶段截止（首解前保留15秒），见[真实局部收益、失败与调度限制](../result/solver-local-optimization-step4/report.md)。尚未执行最终完整对标，由主会话接续。三槽采用稳定先到 tie，-o 固定首个已验解；详见中英文 README 与各步报告。

## 当前获批主线（2026-11-05）

**四线有界交付已完成当前累积集成验收**，见[最终报告](../result/solver-mainlines-final-20261105/report.md)：
新跑enabled 782/782、disabled 503/503 CTest、Node 46/46；首次混合编译器LTO失败保留记录，
匹配GCC后完整重建通过。external16当前显式/默认各4/16且等价，仍12失败；local146不变和
双方质量差距引用历史配对，未声称新全量对标、超过OpusSolver或完成更广独立认证。

用户已批准按 1→4 完成后统一汇报；以下取代历史认证路线的执行优先级：

1. **第三方 omsim 依赖收敛**：保留现有 binary，锁来源、原文 COPYING、Linux x86-64 平台和 SHA-256；不声称源码到二进制可重现。保留 worker 隔离、legacy gate 和必要 production 补验。
2. **真实 OpusSolver 公平对标**：固定 `gtw123/OpusSolver@fdedd3f01a238b941ceba8bace7ea35cbd3b8c41`；同机器、输入 hash、20秒总墙钟和同 legacy gate 裁判。standard/lowcost/optimize 分开，本地169通过不是竞争结果。见[执行包](../result/solver-baseline-20261105/report.md)。
3. **按真实差距改进求解器**：由配对成功/超时、四指标、Pareto及外部128历史失败分类选择有界修复；不改产品2/10/20预算掩盖失败。
4. **默认入口与用户工作流**：基于真实成绩选择默认求解路径，保留模式和失败边界，再统一验收汇报。

主线1–2有界实施包已完成、停供内部验收：[真实报告](../result/solver-baseline-20261105/report.md)。本地共同支持域132题：132 vs130；另4题对手不支持、10题production独有。外部历史失败固定16题 before 为0 vs16；四指标共同成功130对中对手Pareto支配104对。

主线3有界实际求解改进包已完成、停供监督：[修复与真实配对报告](../result/solver-mainline3-20261105/report.md)。通用单原子 cardinal 经 calcification 供 Salt、再 wheel duplication 的机械接线补齐 GEN985 类失败；固定 external16 本方0→4/16，对手仍16/16，新增四解的四指标仍全被对手支配。本地146/146、四指标逐题不变，关联77测试通过。未改默认、legacy/profile或2/10/20预算。

主线4默认入口包已完成、停内部验收：[入口与兼容报告](../result/solver-mainline4-20261105/report.md)。核实旧 CLI 实为 single-atom，现省略 `-s` 与无参 API 统一为 assembly；显式后端保留，历史 `default` 仍为 greedy 兼容别名并明确标注。未改变验解权威、profile或2/10/20预算，无上游 fallback/.NET产品依赖。定向构建与入口回归结果见报告；外部12/16失败与解质量差距保留。四个有界包均已实施，但**最终同源集成仍由主会话接续，未提前统一向用户汇报，不宣称旧认证计划完成。**

**旧 R1–R5 独立验证器全域认证/证明主线暂停，未完成。** 现有独立模块及历史报告全部保留；下方历史完成状态只适用于有界切片，不代表旧计划完成。新对标本地集合含开发已见 holdout，不称未见；共同支持域与 production 独有域分开。


## R4 当前新增切片累计集成（2026-11-05，完成，停供监督）

本次同版本 enabled／disabled **完整构建（-j2）及完整 CTest（-j1）** 分别通过 **778/778、501/501**；现有 Node 发布保护最小组合 **25/25**，均无失败／跳过。CLI、worker 协议／进程及失败原子性保护已包含在完整 CTest，未额外重复此前定向集合。283个 src／tests／CMake 输入执行前后 SHA256 一致，各配置构建后的关键产物保持稳定；真实编译警告保留，无修复、无依赖安装。详见 [本次报告](../result/r4-integration-20261105/report.md) 及其原始日志、退出码和哈希链。

这是**当前新增 R4 功能切片的累计集成**，补齐此前局部包未执行的全工程类别，不改写历史证据，也不是全计划 R5 最终验收。本次完成不代表历史偶发失败根因已修复。

随后获批的唯一缺失类别已在全新目录完成：[169题×五后端845扫描报告](../result/r4-fresh845-20261105/report.md)。沿用hash匹配的现有产物，无构建／安装／并行测试，314.399秒、exit0；固定输入hash一致，真实 **169/169、byteDistinct146/146、journal37/37**，候选 **170成功／675既有拒绝**，显式timeout／Unavailable均0（675拒绝未输出verification JSON，不能推断opt-in可用）。相对固定2026-11-03基线成功新增／回退0、共同成功170对四指标变化0；CLI／shared lib／workers／runner与输入前后hash一致。两个opt-in未扫描，不算覆盖。未重跑2254或18场景，非最终R5。扫描包停供监督；下一步需明确批准R1业务身份／生命周期／跨getter出边的生产接线闭合目标、判据与范围（或明确另选R2），不自动重回周边fixture循环。

**R1范围决策已停供授权：** [权威计划的有限范围决策](IndependentVerifier.md#r1有限范围决策本轮只读取证不授权实现)选择`arm1`（`d2d3c702`、95d、注册索引4），不再以claw-pivot代替普通臂类别。三个直接字段清单只证e1公开、bf2私有唯一setter、11a3公开且唯一字段赋值；不证列表成员生命周期或A非干扰。当前约束没有可保证一次实现闭合的R1子域：仅可由用户选择扩大实际生产effect/别名证明范围，或明确批准带公开写入权限、初始化和线程/重入边界的条件化managed契约；后者不得冒称全域认证。未实现新门，无源码/构建/测试/fixture变更，详细边界及停止条件见权威计划。

**仍未收敛到只剩实机：** R1实体身份／生命周期及跨getter出边证据、R2连续语义／面积／四指标及有效成本配对、完整R3资源／setup／取消资格、F完整足迹／时序／竞争与认证接线仍未闭；正式认证仍依赖R1→R2→所用R3资格。受限Repeating不证明永久运行或完整批准域，sampling_identity_bound=false、两个opt-in不授予认证、默认legacy/profile及2/10/20预算不变。最终R5仍须等待批准域收敛，本包停供监督。

## R3 显式 opt-in 有界资源实测（2026-11-05，停供监督）

[完整报告与原始记录](../result/r3-bounded-resource-20261105/report.md)：六个严格有界场景各三轮，
构建及测量串行、build最多-j2。最大合法逆序X tape/span40001真实转换并Completed；
工作门饱和图（work65536）及最大组件8192真实Checked，另测Repeating整机入口成功。
成功API最大观测81.354ms（本机Debug），相邻tape/graph明确拒绝独立统计，不计性能收益。
RSS保留但第2/3轮受启动器继承高水位污染；非游戏/worker性能，不作三样本置信推断。
足以结束本次新增模块R3局部实测包，**不足以证明R3全域最坏时间或D性能资格**；
initial validation/sort/rawSource/整机复制/vector/setup中途取消仍未证，未观测到>2s setup反例。
不重跑已通过双配置9/9，不扩域/增预算，不开放A/B，不改legacy/profile/2-10-20；停供监督。

## R3 有限 tape 合作取消包（2026-11-05，停供监督）

已用域内GrCC+可控过期deadline复现转换忽略预算（red），再将显式展开入口原2秒
总deadline传入转换，给C复制/X扫描/物化等循环逐步检查并返回ResourceUnavailable，
不发表部分tape、不重置预算。原末尾Timeout覆盖与CycleLimit保持，默认入口未变。
Enabled/disabled各串行-j2构建并通过9/9最小相关测试（44/37ms）；未重跑2254或身份类别。
静态槽界、真实red/green命令及未覆盖的复制/setup/单次库操作/Repeating逐边取消见
[详细记录](IndependentVerifier.md#r3-有界实际预算包有限-tape-转换取消2026-11-05)。
不声称已测出域内必超2秒，也不声称R3全过；保持legacy/profile/2-10-20及认证关闭。

## R4 同动态门接线行为对照验收（2026-11-05）

本轮补齐唯一缺口：冻结2254输入、当前动态门/profile与2/10/20预算不变。接线前由原会话9条精确 edit 倒序撤回构造，不使用旧HEAD或reset；只改变隔离源码的 `IndependentVerifier.hpp` 与 `StaticProductionVerifier.cpp`，保留其余历史修改。逆补丁 SHA256：`70a9e872f573738f8a2ea093107fa3cd3be904191b6797330f3fbf332e00818b`。

持久包：[comparison.json](../result/r4-same-gate-comparison-20261105/comparison.json)、[原编辑](../result/r4-same-gate-comparison-20261105/historical-edits.json)、[before.patch](../result/r4-same-gate-comparison-20261105/before.patch)、[来源](../result/r4-same-gate-comparison-20261105/provenance.json)、[构建输入](../result/r4-same-gate-comparison-20261105/build-inputs.json)。克隆当前对象，使用原 Ninja 精确编译/归档/链接命令串行构建（不超过-j2），Debug/GCC/libverify配置保持；仅隔离rpath及worker绝对路径重定向。`build-before.sh`、`routing-build.sh` 两次 exit 0。初次发现克隆对象硬编码原worker路径，所得运行明确作废，保存于 `discarded-original-worker-routing-run`；重编未改源码的 SolutionVerifier 路径宏后才产生权威 `before-run`，未混入对照。原build及历史报告未覆盖。

原after四binary及runner SHA均匹配，但旧报告未记传递依赖hash，不能追溯证明完整环境相同。因此必要新增**同门回归类别** `after-run`（使用既有已验收after二进制，不重跑38/36定向类别），与before之间13项已解析依赖hash一致，HiGHS/spdlog隔离副本逐字节相同，libverify保持同一文件。历史2254也另存 `historical-comparison.json`，不替代当前环境配对。

命令均成功：隔离cwd运行同一 `node /home/edwards/OpusOptima/tests/functional_test/VerificationCorpus.cjs <包>/before-run --frozen result/verifier-hardening-baseline-20261029/manifest.json --cohort all --diagnostics --workers 4`；原cwd用同选项输出 `<包>/after-run`；`python result/r4-same-gate-comparison-20261105/compare.py` exit 0。manifest和每条输入hash、runner、host、workers、2s replay/10s worker/11s进程配置全部相同。实际日志在包内保留。

**结果：2254/2254精确配对，root Passed→Passed 2254，shadow Unavailable→Unavailable 2254；status/reasonCode/detail、authority/engine/rules/profile、errorCycle/location及四指标全部零差异。** 2254组root四指标可比且相同；shadow指标均null，不能宣称语义指标验证通过。1820条仍为动态轨迹/area未认证；其余434条为Repeat/unknown 26、track bound 158、wall 10、tape bound 235、arm bound 4、transform bound 1。没有timeout或进程错误变异；时间诊断不作语义通过或性能结论。原历史1019/801/434仅跨策略背景。

**本冻结域、同配置的接线行为保持验收已闭合**；不扩展为R4/R5全域认证，不改变R1缺边、`sampling_identity_bound=false`、Repeating或认证关闭状态。已有38/38与36/36没有重跑，无无关tests、安装、委派、游戏/存档或提交。隔离无关对象/cache已清理；验收源码快照、before二进制/依赖、补丁、逐条报告及来源命令保留。


面向维护者；本记录区分已完成的整理、确认修复的问题和仍未解决的边界。

## R4 第一有界功能接线包（2026-11-05，停供监督）

独立 opt-in API：`ReplayExpandedFiniteMachine(puzzle, raw, cycleLimit=150000)`；返回 `ExpandedFiniteReplayReport`，资格恒 false，不产生认证 Passed／authority。仅接现有 FiniteTapeExpansion，未接 Repeating，不新增文件入口。raw 快照（含原顺序、索引与四指标声明）、展开 slots／origins／计数、功能执行观测及转换失败 code／rawOrdinal／slot 分开保存。旧 Replay／独立认证／生产补验和发布调用者不 fallback；legacy-gate 策略不变。

总2秒起点包括快照、展开、复制、scope/setup和执行；阶段检查及最终超时覆盖不允许超时 Completed。转换／复制不能抢占，非严格2秒返回承诺；没有增加2/10/20预算。R1 `sampling_identity_bound=false`、认证关闭不变。enabled 定向 **38/38**，disabled **36/36**，均 `-j2` 构建成功；新 Conflict fixture 曾失败，纠正为锁定冲突例后真实复验通过。

本次重新执行既有冻结 runner：`node tests/functional_test/VerificationCorpus.cjs result/r4-finite-wiring-20261105 --frozen result/verifier-hardening-baseline-20261029/manifest.json --cohort all --diagnostics --workers 4`，exit0。[本次报告](../result/r4-finite-wiring-20261105/report.json)：2254行、legacy Passed2254、shadow Unavailable2254（全部 `independent-profile-uncertified`），无基础设施错误／非零CLI退出／未分类超时／mismatch；二进制和输入哈希锁定，最终二进制仍匹配。本次不是历史1019通过结果，也不是认证或生产等价证明。

本有界包接线、定向回归及冻结2254运行已完成；**当时同配置接线前后行为对照尚未闭合（现已由本文开头同门对照包补齐）**，`mismatches=[]` 不捕获 Passed→Unavailable，不能作为行为保持验收。原结果不覆盖。全工程suite、游戏／目标机验收、R1缺边、Repeating与认证开放仍未完成。未提交、委派、启动游戏或读写存档。精确命令、日志和预算局限见 [IndependentVerifier 本包实测记录](IndependentVerifier.md#r4-本包实际验证记录2026-11-05)。


## 1. 本轮整理

- 自有 C++ 按仓库 `.clang-format` 使用 clang-format 19 统一排版；独立 C 验解适配器也统一风格，不格式化第三方源码或上游 API 头文件。
- 中英文 README 改为当前状态、构建、使用、验收边界和导航；旧开发长文不再留在代码树内，需要时从 git 历史检索（`docs/history/README.md`、`docs/history/README_zh.md`）。本地早期设计草案明确标记为历史设想，不把 TPS／并发目标当作实测。
- 六个新生产后端的接收、异常传播和实测指标接线收敛到 `AssemblySolver` 的同一局部适配器，保持策略顺序与原异常语义。
- CMake CLI 回归通过统一函数登记，保留原测试名称、输出路径及 20 秒外部限制。
- 复用 `HexFrame::Compose`／`ToWorld`，去掉重复 frame 组合与旋转实现。
- 独立扫掠缓存同一采样时刻的底座／wheel 位置、原始房间及原子位置容器；移动对象仍使用原采样数、阈值和检查，不共用求解器效果或证书。
- 题库扫描每个 puzzle 只读取一次，用同一 Buffer 计算原始哈希和结构。

没有创建提交，也没有删除历史扫描、实验输出或用户的既有报告。

## 2. Review：已确认并修复

| 问题 | 修复及回归重点 |
| --- | --- |
| 缓存 slot 复用缺少对上一轮 Retrieve 的依赖 | 新 Store 依赖上一轮取走操作；物理计划重排不能覆盖仍占用的 slot |
| 候选去重忽略部件顺序 | 保留部件序列；重叠 track／有序 glyph 的不同执行机器不再被错误合并 |
| 生产反应产物把绝对目的坐标当成相对平移 | 用目的坐标减当前坐标；保键转换不再把端点及其邻居重复位移 |
| parked 化学可能激活错误的同类键合端点 | lowering 约束实际持有／转换的 operand 身份，拒绝错误接触后继续搜索 |
| 催化剂约束排除了用于 unification 的 cardinal 中间库存 | 将最终 Quintessence 需求纳入中间 cardinal 上界；保留权限和明确 wheel 能力选择 |
| 独立执行错误旋转 track 局部偏移 | 按 native 的 track 解码约定处理，非零序列化 rotation 有差分回归 |
| compact 搜索耗尽了后备策略开始前的 deadline | compact 与后备布局各自获得其声明的预算，不改变外部 20 秒限制 |
| solution 声明的部件数未读完即可在 EOF 成功返回 | 必须读取所有声明部件，完整尾部对象被截去也会报错 |
| 关闭 libverify 的测试路径仍无条件依赖 POSIX 头文件／PID | 测试使用可移植 PID helper，并整理条件编译；未实际执行 Windows/MSVC 验收 |

完整 CTest 已通过 **622/622，零失败、零跳过**；比较脚本 **12/12**、游戏证据读取器 **8/8** 通过。格式校验、独立 C 适配器编译及文档本地链接检查均通过。

整理与修复后的全新目录扫描保持 **169/169 原始文件、146/146 字节不同题目、journal 37/37**；仍为五种后端、845 次尝试、每次 20 秒，相对 B3–B6 基线新增 0、回退 0。证据：[完整扫描](../result/local-cleanup-review-fresh-20261027/coverage.json)、[基线比较](../result/local-cleanup-review-fresh-20261027/comparison.json)、[完整回归](../result/local-cleanup-review-fresh-20261027/regression.log)、[构建与验证来源](../result/local-cleanup-review-fresh-20261027/provenance.json)。历史验收报告没有改写为本轮结果。

## 3. 明确保留的问题

### `fork` 后直接执行 C++ 验解逻辑（后续里程碑 1 已修复）

以下保留 review 当时的问题说明；现已使用独立 exec worker 解决继承锁问题，默认验收权威仍不切换，验收见本文末尾里程碑 1。

review 当时的 Linux worker 使用 `fork` 而不 `exec`。如果另一个线程在 fork 时持有日志等互斥锁，子进程会继承锁状态，再调用文件解析／日志路径可能等待至超时。父进程的失败关闭和超时保护仍在，但这不是线程安全的并发验解 API。

整理 review 当时没有用一个父进程互斥锁或“暂时关日志”的局部补丁冒充解决；它们不能排除其他线程持锁。串行 CLI 是当前已测路径。完整修复应使用独立可执行 worker（例如 `posix_spawn`／exec），并进行构建接线、结果协议、子进程回收和并发压力测试。

### 暂未进行的更大整理

重复的测试 puzzle 编码器和临时文件管理可以收敛到测试支持层，但涉及更多 fixture 的独立性及恶意字节测试，不在这轮顺带改造。验解函数的嵌套结构也没有仅为阅读样式大范围重写。

仍有既有编译 warning，主要是负例中故意忽略 `nodiscard` 返回值和省略聚合字段；本轮不宣称全工程零 warning。

## 4. 下一步建议

### 第一优先：可靠的执行边界与可复现基线

先实现 exec-based 验解 worker，再考虑同进程多线程或并行验解。验收需要：并发日志压力下不死锁、worker 崩溃／超时能回收、协议截断与错误版本失败关闭、已有输出不被破坏、原题与指标后二验不回退。

同时冻结一份可分发的基线：源码 revision、编译选项、可执行文件／验解库哈希、原题哈希、成功率、逐题求解／验解耗时和输出指标。当前工作区有大量未提交源码，稳定发布前应由维护者审核后分批提交，而不是继续把功能堆到一个无法定位回退的大变更中。

### 第二优先：真正未见题上的泛化评估

169/169 证明本地题库已经覆盖，不证明任意新题可解。现有题库已用于开发，不能再把其字节拆分 holdout 说成未见测试。

另选未用于开发的外部题库，在调参前冻结集合及统一的后端／时间配置；将改名、旋转、局部坐标变化等语义同构题作为同一组，避免仅按字节去重泄漏。沿用公平比较规程，对生产题使用一致的验解能力与明确边界，不因外部裁判未实现某条规则就当作通过。

以失败类别驱动下一轮通用化：分子图／物料周期、房间／导管布局、运动可达性、always-on glyph、未知规则及预算分别计量，优先扩大公共规划机制，不继续靠增加题名或过窄图形分支追本地覆盖数字。

### 第三优先：时延余量与解质量

在可靠性和未见题基线建立后，再优化搜索耗时与 cost/cycles/area/instructions。先找接近 20 秒的实例，区分物料搜索、布局、机械 lowering 和验解耗时；保持同一外部限制和原题字节，不靠延长时间抬高成绩。

解质量应报告可比较的多个指标或 Pareto 集，而不是把一个总分当作全部目标。每次优化都保留真实机器双验和零回退检查。

**建议执行顺序：独立 worker／可复现性 → 未见题泛化 → 搜索稳定性与指标优化；不建议现在直接并行化现有验解路径或继续宣称“通用求解器已完成”。**

## 5. OpusSolver 题库导入与自研验证器建议

已将 `gtw123/OpusSolver` 锁定提交 `fdedd3f01a238b941ceba8bace7ea35cbd3b8c41` 的
`test/puzzles` 并入常规题库 `asset/puzzle`：属于既有分组的文件合入 `campaign/`、`easy/`、
`edge-cases/`、`journal/`、`tourney-*/`、`workshop-versions/`，新增分组
`24hour-1-puzzles/`、`24hour-1-sample/`、`24hour-1-test/`、`custom/` 并列放置。
共 2401 个路径、2378 个字节不同文件；142 个哈希与原库重叠，2236 个字节不同文件不在原库
（其中 165 个与原库文件逐字节相同，无同名不同内容冲突）。
逐题 Git blob／SHA-256 核对在导入阶段完成；各分组数量、重叠统计与上游版本汇总记入
README，逐文件哈希清单保留在导入工作记录中，不再随代码树分发（题目本身已是普通题库
文件，且有锁定版本可重新推导）。
只提取了 puzzle 和根许可证，没有执行上游脚本或构建上游代码；上游根目录的 MIT 许可不等于
独立确认每个游戏／社区题目的权利归属。新增字节也不等于已经完成语义同构去重的真正未见题。

完整测量使用同一五种后端、每次 20 秒，四个互不重叠的文件分片分别运行独立 CLI 进程；
所有分片结束后核对 2401 个文件和 12005 次尝试，重新计算整体、字节去重、新增题、重叠题和
各目录覆盖率，不复用旧的通过结果，不在测量中改动求解器。它不是与 OpusSolver 本身的同机成绩比较，
且并发进程负载与旧的串行扫描不同。

完整测量已结束，见[外部题库报告](../result/opussolver-fdedd3f-full-20261028/coverage.json)及
[运行来源](../result/opussolver-fdedd3f-full-20261028/provenance.json)：

| 口径 | 通过／总数 | 覆盖率 |
| --- | ---: | ---: |
| 上游全部文件 | 2273/2401 | 94.67% |
| 上游字节去重 | 2250/2378 | 94.62% |
| 不在原库的字节不同文件 | 2108/2236 | 94.28% |
| 与原库重叠的文件 | 165/165 | 100% |
| 与原库重叠的字节哈希 | 142/142 | 100% |
| 24hour-1-puzzles | 935/1000 | 93.50% |
| 24hour-1-test | 946/1000 | 94.60% |
| 24hour-1-sample | 97/100 | 97.00% |
| custom | 130/136 | 95.59% |

其余目录全部通过。128 个失败均在新增字节集合中，assembly 路径 5 个进程超时，123 个被
求解／验解流程拒绝，不能由末条错误推断唯一根因或题目无解，具体文件见[失败清单](../result/opussolver-fdedd3f-full-20261028/failures.json)。全部 2273 个通过文件的 assembly
后端均通过；其他后端没有新增 portfolio 通过文件。所有结构记录都解析成功，测量约 18.35 分钟。

求解器、共享库和验解库在扫描前后哈希不变；新题结果不是从旧报告补齐。这是一份冻结求解器后的
外部题库基线，不是在这些新题上边调参边得到的最佳成绩；但上游目录名 `test` 和“字节未出现”本身
仍不能证明语义层面的统计独立 holdout。

### 不必从二进制逆向开始

`libverify.so` 来自公开的 [ianh/omsim 锁定源码](https://github.com/ianh/omsim/tree/a88d12d8d100784aa525ee90259255b869bcba38)，
包含 parse/decode、simulation、collision、verifier 和 steady-state 等模块。技术上可以反汇编／
反编译 `.so`，但有完整源码时这样会丢失类型、命名和设计意图；只有排查发布二进制与源码行为不一致时
才值得优先做二进制层检查。

上游 COPYING 声明为 “this software belongs to a future without copyright. please use it however you'd like.”，
不是 MIT 文本；移植／分发应保留来源、锁定提交和原许可，不应笼统改标为本项目 MIT。
版本记录也不构成源码与既有发布二进制逐字节一致的可复现构建证明。

三条路线应分清：自编译／维护源码 fork 可快速摆脱不透明预编译文件，但仍是同一算法；照译后与原库比较
也可能继承相关错误，不能据此宣称独立正确。长期的自研裁判应扩展现有独立执行模型，源码只作规则参考和差分 oracle。

### 建议的首批可验收工作

1. 先落地 exec-based worker、结果协议及并发／超时／崩溃回收测试，解决目前 fork 继承锁的边界。
2. 以现有 `StaticProductionVerifier` 为起点，扩展普通有限输出，不复用求解器效果或物理证书；
   不把旧 `HexSimulator` 改名当新裁判，也不信任 solution 中声明的 metrics。
3. 补独立 cost／area／instruction 计算，以及解析或保守区间的连续碰撞；提高采样数不等于完成连续证明。
4. 对 repeat/reset、隐式 disjoint links、循环／歧义 track、协作运动和 conduit cloning 分别认证；
   原库已知未实现／与游戏不同的行为必须补游戏侧证据，不能只追求与 omsim 一致。
5. 先 shadow 运行，记录完成状态、周期、指标和首个事件分歧；同时使用未见机器、故意破坏机器、
   临界碰撞和畸形文件测试。只有已认证子域才逐步切换发布权威，未知继续 `Unavailable`，native 保留作测试参考。

本次题库测量只进行了题库导入／测量和路线分析，没有在测量期间移除 `libverify.so` 或切换验解权威。

## 6. 独立验证器里程碑 1：worker 与依赖隔离已验收

获批开发计划的第一批交付已经完成，不代表里程碑 2–6 全部实现：

- `OpusOptimaVerificationCore` 单独编译 codecs、基础日志／几何与有界生产重放，不链接 solver 或旧 `HexSimulator`；独立 worker 只依赖该 core。
- 主 CLI／主库／`OpusOptimaVerifyWorker` 不链接 `libverify.so`；可选 `OpusOptimaReferenceWorker` 和明确的 reference 测试保留该依赖，默认仍为 `legacy-gate`。
- 父进程改为 `posix_spawn`／exec，不在 fork 后运行带锁 C++；原 puzzle 预解析移入 worker。受信绝对 worker 路径不经 PATH/CWD 搜索，参数不经 shell。
- 专用 FD 3 使用有版本、长度、枚举及 65536 字节总上限的显式 little-endian 协议，不传 struct 内存，不混用日志 stdout。父进程同时监督退出和读取，能处理大于 pipe 容量的响应。
- 使用进程组终止和回收、FD RAII 及 pidfd 退出事件；检查缺 worker、崩溃、挂起、异常退出、截断／超大响应、错误 authority、描述符泄漏和继承日志锁。
- 保留临时候选／指标回填／二验／rename；第二次指标或 provenance 变化也拒绝发布，失败保留原输出。单次验证 10 秒、发布总预算 20 秒与 CLI 外围剩余预算共同约束，不在第二遍加总时间。
- 补齐旧测试中遗漏的 reference-only 条件编译，修正 verification-core 经静态 spdlog 链入 shared 库时的 PIC 配置；无库测试不依赖删除现有库。无库构建显式使用一致 GCC C/C++ 工具链，避免环境 Clang/GCC LTO 混用。

完整验收：reference-enabled **634/634**，reference-disabled **370/370**，均零失败、零跳过；新的原题 fresh 为 **169/169**、字节去重 **146/146**、journal **37/37**，五后端共 **845 次**、每次仍设置 20 秒、回退 0。

证据：[完整扫描](../result/verifier-m1-legacy-fresh-20261028/coverage.json)、[基线比较](../result/verifier-m1-legacy-fresh-20261028/comparison.json)、[enabled 回归](../result/verifier-m1-legacy-fresh-20261028/regression-enabled.log)、[disabled 回归](../result/verifier-m1-legacy-fresh-20261028/regression-no-native.log)、[二进制依赖及哈希](../result/verifier-m1-legacy-fresh-20261028/provenance.json)。现有 `asset/libverify.so` 哈希不变。

**关键边界：**独立 worker 当前只执行既有有界生产重放；即使重放完成，完整指标及连续碰撞尚未认证，仍返回 `Unavailable / independent-profile-uncertified`，不发布、不冒充自研裁判已完成。无库配置中的传输／协议测试通过，也不意味着无库机器认证已经交付；Windows 与安装／迁移布局仍不在首版验收范围。

下一步进入里程碑 2：泛化独立执行模型及普通有限输出，保持上述发布资格保护，之后再补完整四指标、连续碰撞和 capability 认证。

## 7. 里程碑 2–6 的首个有界独立链路

首版链路已实现，当前认证目标是 `finite-ordinary-v1 / finite-82x71-v1`，不是所有原游戏规则。
实现、API、命令、能力界限和数值模型注意事项见[独立验证器说明](IndependentVerifier.md)。

- 泛化现有执行模型到普通有限输出，明确 `ReplayCompleted` 与可发布 `Passed` 的区别；
- 独立 cost／area／instructions／cycles，运动面积有完整观察接口，临界归属无法确定不通过；
- 采用 outward interval、Taylor 余项界、线性解析与保守细分的连续碰撞内核，名义模型无需
  native 判定即可执行，但这不自动证明其与原游戏／native 所有浮点细节等价；
- 新增只读 `verify --authority independent|legacy-gate|shadow --json`、显式来源及能力矩阵；
- 发布在关闭参考裁判的构建中真实通过，并验证错误／未知候选不覆盖旧输出；默认仍为 legacy-gate；
- 修复 shadow 在并发发布时可能读到其他目标文件的 provenance 竞态，使用私有已接收值快照；
- 明确超出长臂／磁带／坐标界限为不可用，不当作机器违法；静止无磁带 wheel 的重复编辑器编号
  不再造成误拒绝；隐式闭合 track 继续归入未认证循环规则。

最后两套完整回归：**enabled 662/662、disabled 393/393**，零失败／跳过；脚本 **19/19**、
游戏证据读取器 **8/8**。默认 gate 的新 [169 文件 fresh](../result/verifier-m2-m6-certified-legacy-fresh-20261029/coverage.json)
仍是 **169/169、字节去重 146/146、journal 37/37**，845 次、每次 20 秒、回退 0。

冻结机器 [差分](../result/independent-finite-certified-shadow-20261029/report.json) 覆盖 **2254** 个不同
puzzle＋solution 组合：**1019 通过、801 按原预算超时、434 不可用**，状态／四指标差异为 **0**。
所有输入与二进制哈希前后核对，没有重求解或改写文件；原来的求解失败不是坏机器标签。
[验证来源及依赖证据](../result/independent-finite-certified-shadow-20261029/provenance.json)记录最终二进制、
无 native 依赖、enabled／disabled 回归与已知限制。

生产墙体／导管的独立连续认证、Repeating 输出、repeat/reset 展开、disjoint 隐式链接、循环／歧义
轨道、未证协作运动和 cloning 尚未开放；数值模型与原游戏的进一步证据、以及 801 个执行超时的
性能工作仍是下一轮目标。不能把首版子域发布说成全游戏裁判替代，也不能偷偷用 native 挽救
independent 的未知或超时结果。


### Row structure round 5：已完成的有限硬件候选

见 [报告](../result/solver-row-structure-round5/report.md)。按需求省略 ordinary narrow-feed escape工作站，或保留并缩短两格轨；原parent与三槽独立发布保持。`OPUSOPTIMA_ROW_STRUCTURE=off`可关闭。固定三例同源串行off/on真实cost/area赢家：P022 755/156→745/154，P021 595/136→535/125，留出P025 635/141→575/130；cycles槽及首解SHA保持。最终四项针对性测试通过；适用width/height≤16、feedReach≤64，完整legacy gate为发布前提。不推广到wide/shared/material或未证工作站共享，不扩大本轮搜索。
