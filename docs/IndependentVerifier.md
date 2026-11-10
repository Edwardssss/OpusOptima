# 独立验证器里程碑 2–6：首个认证子域与发布链路

> **验证器来源变更（2026-11-10）**：外部验解器不再以预编译 `asset/libverify.so` 引入，
> 而是以上游 [ianh/omsim](https://github.com/ianh/omsim) 的 `third_party/omsim` 子模块
> 锁定提交 `a88d12d8d100784aa525ee90259255b869bcba38`，并由 CMake 用上游 `Makefile` 的
> `libverify.so` 目标参数（`-O2 -std=c11 -shared -fpic` + `-lm`）从六个 C 源文件构建。
> 导出符号集与退役预编译库一致；此前被拒的机器（`collision during motion phase`、周期
> 1798、格点 `[12,1]`）仍被同一周期同一格点拒绝，通过机器的指标逐项相同。
> **下文所有 `asset/libverify.so` 的 `sha256sum` 记录都是针对该退役预编译库的历史证据，
> 保留不改写**；当前构建不再产生该文件，来源锚点改为子模块提交。

> **当前入口（2026-11-05）**：旧 R1–R5 全域认证/证明主线暂停且未完成；现有独立模块保留，不继续扩认证。本页以下是历史有界切片证据，不是全域完成声明。当前获批1–4主线见[权威计划](ReviewAndNextSteps.md)。产品继续使用 worker 隔离的第三方 legacy gate 及必要 production 补验。


## R4 当前新增切片累计集成（2026-11-05，完成，停供监督）

本次同版本 enabled／disabled **完整构建（-j2）及完整 CTest（-j1）** 分别通过 **778/778、501/501**；现有 Node 发布保护最小组合 **25/25**，均无失败／跳过。CLI、worker 协议／进程及失败原子性保护已包含在完整 CTest，未额外重复此前定向集合。283个 src／tests／CMake 输入执行前后 SHA256 一致，各配置构建后的关键产物保持稳定；真实编译警告保留，无修复、无依赖安装。详见 [本次报告](../result/r4-integration-20261105/report.md) 及其原始日志、退出码和哈希链。

这是**当前新增 R4 功能切片的累计集成**，补齐此前局部包未执行的全工程类别，不改写历史证据，也不是全计划 R5 最终验收。未重测已验收 R3 的18场景、未重跑2254、未执行新845扫描；845是否启动交主会话决定。本次完成不代表历史偶发失败根因已修复。

**仍未收敛到只剩实机：** R1实体身份／生命周期及跨getter出边证据、R2连续语义／面积／四指标及有效成本配对、完整R3资源／setup／取消资格、F完整足迹／时序／竞争与认证接线仍未闭；正式认证仍依赖R1→R2→所用R3资格。受限Repeating不证明永久运行或完整批准域，sampling_identity_bound=false、两个opt-in不授予认证、默认legacy/profile及2/10/20预算不变。最终R5仍须等待批准域收敛，本包停供监督。

## R4 受限 Repeating 接线契约（2026-11-05，待验收）

显式 `ReplayRepeatingFiniteMachine` 仅执行普通非生产机器，至少一个 Repeating 输出；不展开 C/X，不新增文件入口，不授予认证。每个 puzzle 输出索引恰绑定一个输出端口（Repeating 用 out-rep，普通用 out-std），整个调用固定 raw 模板、frame 与 marker displacement；调用内不接受替换。模板先经 checker 的有限轴向非 surg 域验证。每端口 maxMonomers 固定6，目标6×倍率；不是永久运行证明。普通输出继续消费，Repeating 输出独立非消耗高水位，不能互相替代。

锁源 decoder 在 raw 局部(0,0)选择 center；sim consume_output 从该 world 格唯一原子取完整 molecule。接线同样仅取该唯一锚定连通分量（无原子为空），所有原子及键经 frame.Local 刚体逆变换；不搜索其他候选、不截目标大小。六向旋转及整数平移保持类型、六邻接和普通键位。断开片段不合并。多输出允许，但索引重复绑定或遗漏均 Unavailable；每次检查读取当时真实库存。checker 域外及工作门失败均 Unavailable，不猜测匹配。

单一外围2秒 deadline 从入口构造前开始，覆盖 setup、完整提取及观察和报告；各阶段检查非抢占，65536只是保守比较工作门，不是全库存成本资格或耗时证明。bad_alloc/length_error（包括错误报告分配）由外围转换为无部分成功的资源 Unavailable，结构化 resourceUnavailable 不依赖分配错误字符串。默认 ReplayFiniteMachine、VerifyIndependent、发布路径及 legacy/profile/2-10-20 不变。certificationEligible 恒 false。

## R4 受限 Repeating 接线实测（2026-11-05，限定包完成，待监督验收）

实现文件：`src/Verification/IndependentVerifier.hpp`、`src/Verification/StaticProductionVerifier.cpp`；机械测试：`tests/unit_test/IndependentFiniteReplayTest.cpp`、`tests/unit_test/StaticProductionVerifierTest.cpp`。没有 opt-in 文件入口；原文件/发布入口仍走原 strict 路径。

最小 enabled/disabled 命令均为 `cmake --build <build> --target OpusOptimaTests -j2` 后执行 `<build>/OpusOptimaTests --gtest_filter='IndependentFiniteReplay.Repeating*:IndependentStaticProduction.FiniteRepeatingPrefixRequiresIntermonomerBonds'`；build 分别为 `build`、`build/verifier-m1-no-native-20261028`。最终 enabled **6/6**、disabled **5/5**（后者不编译 native reference 测试）。日志在 [本包目录](../result/r4-repeating-wiring-20261105/)：`build-extra-final.log`、`tests-extra-final.log`、`build-disabled-extra.log`、`tests-disabled-extra.log`；生产三个目标构建见 `build-production-final.log`。

真实 raw 机械执行覆盖：1/3/5/6/7 链长度；重复观察不累加；同 run 轨道搬运先增长后缩短而高水位不退；六向刚体变换、错位/错材料/断键；独立多端口；混合普通输出消费（6个普通产物被消费，6链库存仍留存）；puzzle 原子/键与 solution 输入不变；普通 replay/独立认证仍拒绝；raw C/X 和域外模板不开放。native reference 六单体完成、七单体要求不完成及跨单体断键对照保留，新 opt-in 六单体阈值对照通过。锁源 `omsim-sim-reference.c` 的 `consume_output`/`check_repeating_output` 不检查 held，也不消费 Repeat，故 held 轨道 fixture 不借普通消费语义。

本包另有精确13条原编辑逆补丁基线（不是下节旧 raw C/X 基线）：[来源](../result/r4-repeating-same-gate-20261105/provenance.json)、[逆补丁](../result/r4-repeating-same-gate-20261105/before.patch)、[逐对比较](../result/r4-repeating-same-gate-20261105/comparison.json)。隔离 before 串行构建；before/after 均以 frozen manifest、`--cohort all --diagnostics --workers 4` 运行 `tests/functional_test/VerificationCorpus.cjs`，再执行 `python3 result/r4-repeating-same-gate-20261105/compare.py`。**2254 root passed→passed、2254 shadow unavailable→unavailable**；2254 root metrics 记录逐字段可比一致，2254 shadow null metrics 不冒称指标等价；状态、authority、detail、四指标、错误位置等逐行一致，无 timing/infrastructure 行。shadow 1820条仍为动态轨迹/area未认证，其余434条域拒绝。当前成对环境闭合、binary/runner/input及动态依赖hash已核验；历史完整环境闭合仍为 false，不作追溯声明。此后仅增加测试及本文档，生产源码未变。

失败修复：首次保存 before 二进制路径有误，改为上述保留全部既有 dirty 修改的精确逆编辑隔离基线；一次歧义 edit 未改文件后用唯一上下文修复。新增混合测试最初误预期完成后再生成一枚普通输入，真实完成点立即返回，修正库存/获取期望为6/12后双构建通过；未改生产行为。保留失败日志 `tests-extra.log`。

剩余：监督验收；未做资源异常故障注入、无限运行或全库存耗时资格验证，未授予认证。2秒非抢占 deadline、65536保守工作门、普通/发布拒绝与 legacy/profile/2-10-20 均不扩大；不重跑无关已完成类别，不提交或操作游戏存档。

## R4 同动态门接线行为对照验收（2026-11-05）

本轮补齐唯一缺口：冻结2254输入、当前动态门/profile与2/10/20预算不变。接线前由原会话9条精确 edit 倒序撤回构造，不使用旧HEAD或reset；只改变隔离源码的 `IndependentVerifier.hpp` 与 `StaticProductionVerifier.cpp`，保留其余历史修改。逆补丁 SHA256：`70a9e872f573738f8a2ea093107fa3cd3be904191b6797330f3fbf332e00818b`。

持久包：[comparison.json](../result/r4-same-gate-comparison-20261105/comparison.json)、[原编辑](../result/r4-same-gate-comparison-20261105/historical-edits.json)、[before.patch](../result/r4-same-gate-comparison-20261105/before.patch)、[来源](../result/r4-same-gate-comparison-20261105/provenance.json)、[构建输入](../result/r4-same-gate-comparison-20261105/build-inputs.json)。克隆当前对象，使用原 Ninja 精确编译/归档/链接命令串行构建（不超过-j2），Debug/GCC/libverify配置保持；仅隔离rpath及worker绝对路径重定向。`build-before.sh`、`routing-build.sh` 两次 exit 0。初次发现克隆对象硬编码原worker路径，所得运行明确作废，保存于 `discarded-original-worker-routing-run`；重编未改源码的 SolutionVerifier 路径宏后才产生权威 `before-run`，未混入对照。原build及历史报告未覆盖。

原after四binary及runner SHA均匹配，但旧报告未记传递依赖hash，不能追溯证明完整环境相同。因此必要新增**同门回归类别** `after-run`（使用既有已验收after二进制，不重跑38/36定向类别），与before之间13项已解析依赖hash一致，HiGHS/spdlog隔离副本逐字节相同，libverify保持同一文件。历史2254也另存 `historical-comparison.json`，不替代当前环境配对。

命令均成功：隔离cwd运行同一 `node /home/edwards/OpusOptima/tests/functional_test/VerificationCorpus.cjs <包>/before-run --frozen result/verifier-hardening-baseline-20261029/manifest.json --cohort all --diagnostics --workers 4`；原cwd用同选项输出 `<包>/after-run`；`python result/r4-same-gate-comparison-20261105/compare.py` exit 0。manifest和每条输入hash、runner、host、workers、2s replay/10s worker/11s进程配置全部相同。实际日志在包内保留。

**结果：2254/2254精确配对，root Passed→Passed 2254，shadow Unavailable→Unavailable 2254；status/reasonCode/detail、authority/engine/rules/profile、errorCycle/location及四指标全部零差异。** 2254组root四指标可比且相同；shadow指标均null，不能宣称语义指标验证通过。1820条仍为动态轨迹/area未认证；其余434条为Repeat/unknown 26、track bound 158、wall 10、tape bound 235、arm bound 4、transform bound 1。没有timeout或进程错误变异；时间诊断不作语义通过或性能结论。原历史1019/801/434仅跨策略背景。

**本冻结域、同配置的接线行为保持验收已闭合**；不扩展为R4/R5全域认证，不改变R1缺边、`sampling_identity_bound=false`、Repeating或认证关闭状态。已有38/38与36/36没有重跑，无无关tests、安装、委派、游戏/存档或提交。隔离无关对象/cache已清理；验收源码快照、before二进制/依赖、补丁、逐条报告及来源命令保留。


## 当前验收总表与收敛决策（2026-11-04；现行执行入口）

**结论：尚未收敛到只剩实机。** 本表及下列五包取代历史各节的“下一包”调度建议，不覆盖历史失败、停止出口或验证记录。阶段编号沿用权威计划的五阶段；本文原“里程碑”编号不与之混用。本次仅整理既有证据，未重新运行审计、构建或测试；测试数量、保守拒绝和只读 review 无高置信 bug 均不等于生产语义通过。

| 阶段 | 真实已完成 | 尚可离线实现／关闭 | 依赖的规则证据 | 确需实机观察（不替代证明） | 外部假设／当前验收 |
|---|---|---|---|---|---|
| 1 性能测量 | 冻结队列、五轮比较、慢例串行与内存记录已落盘 | 在真实合格域取得 D 的有效成本配对，定位剩余 setup／内循环成本 | 双方 Passed、四指标同口径；Unavailable 不算收益 | 目标运行环境的实际耗时、内存与中断表现 | 旧样本／机器观测不是全域资源界；测量包完成，资格成本未过 |
| 2 规则与认证前置 A/B/C/D | receiver／wrapper／字段局部门、有限义务账本；C 的候选完整性与误差条件性论证 | 业务ID→实体身份、生成／移除与跨getter稳定性；连续数值、面积／四指标和总门；有效配对 | 锁定程序集与规则源，实体／pair exclusions 全覆盖，数值及算术前提 | 游戏实际对象／调用时序、分派与参考结果的特定实例核对 | 线程／重入／反射／native干扰及 Math／浮点环境未全封闭；A/B/D未通过，C亦非全域等价 |
| 3 资源 E | 磁带双门、轨长0/1/256/257与部分拓扑、内部弦有限差分 | tape/tracks/arms/coordinates 的完整边界、解析／setup成本与中断路径；不扩profile的实现可先做 | 资源上界、拓扑与溢出规则；正式扩展须先过D | 目标机长输入／取消／内存表现 | 规则上界不能替代墙钟保证；未正式开放 |
| 4 墙体／受限导管 F | 六室型静态单格、projection两格足迹、释放捕获／同周期到达及守恒的限定功能 | 完整运动足迹与连续空间时序、竞争／占用／glyph／键交互，认证接线 | 有序格、身份、迁移守恒与锁源半周期规则；依赖A/B/C及对应资源资格 | 游戏生产室／迁移可见事件与边界实例 | 限定功能已实现≠完整功能域或连续认证；生产墙体仍Unavailable |
| 5 repeat/reset/repeating G | FiniteTapeExpansion、FiniteRepeatingGraph纯模块；双配置定向与分配失败验证；集中只读review未报高置信bug | raw→展开执行及来源／指标接线；Repeating完整材料快照、非消耗高水位、多端口／倍率与有限完成状态 | copy/reset时序与成本口径、跨单体键／异种材料图、输出匹配及完成规则；不能用普通output消耗计数代替 | 游戏实际reset／输出时机及有限完成实例 | 模块契约域不等于批准支持域；未接生产，不证明永久运行／稳态 |
| 最终集成 H | 已有历史回归与无库链路 | 在上述实现稳定后做一次同版本、双配置、入口／发布／失败原子性集成验收 | 所有新增资格与provenance一致；不拼接历史运行 | 最后执行明确列出的目标机验收清单 | 尚未执行最终验收；不能写“仅余实机” |

### 最多五个剩余工作包（依赖不是执行先后假定）

| 包 | 完成标准 | 真实依赖及关键路径 |
|---|---|---|
| R1 源绑定身份与生命周期（合并A/B） | 从明确业务ID绑定描述、实例、采样成员及pair角色；覆盖生成／移除／排除；为跨getter正常／异常／finally出边给出同owner非干扰证据。未覆盖类或出边不认证 | **关键路径起点**；固定源和明确调用域。业务命名与稳定性分别交账，ID解出不自动关闭A |
| R2 连续语义与有效成本（合并C/D） | 连续碰撞／离散面积／四指标各自闭合，满足算术前提；真实入口确有双方Passed且四指标一致的有效性能配对，语义和成本门分别通过 | **R1→R2关键路径**；不以双方Unavailable或静态帧小域收益冒充运动域成本 |
| R3 有界资源资格（E） | tape/tracks/arms/coordinates 的资源、拓扑、溢出、setup与取消边界满足既有预算；新增覆盖与profile资格明确 | **R2→正式开放R3**；离线实现可提前，但不能以新资源绕过D |
| R4 生产执行与认证接入（合并F/G） | 阶段4完整批准域的足迹／连续时序与认证一致；阶段5模块进入真实执行，保存非消耗输出状态及原始指标／来源，有限完成正确；raw与文件入口、失败原子性、无库路径一致 | 对应规则实现可与R1/R2并行；**认证须R1/R2及所用R3资格**。两阶段分别验收，不因一个模块完成合并放行 |
| R5 一次最终集成与目标机验收（H） | 统一版本验证全部批准域及不支持边界，保存真实结果／未决假设，再执行实机清单；剩余只有逐项可观察事项才允许称“只剩实机” | **R1/R2/R3/R4→R5**；最终全量验证在实现收敛后，不是当前动作 |

### R4 第一有界功能接线包 API 契约（2026-11-05；认证关闭）

实现前固定：新增独立命名的 `ReplayExpandedFiniteMachine(puzzle, raw, cycleLimit=150000)`，调用本身即显式 opt-in；不复用认证开关，不改变 `ReplayFiniteMachine`、`VerifyStaticProduction`、`VerifyIndependent[Files]` 或发布调用者。只调用现有 `ExpandFiniteArm1Tape` 域，不接 Repeating／图 checker，不新增文件入口。
返回独立 `ExpandedFiniteReplayReport`：`execution` 只用 Completed 等功能结果，`certificationEligible` 恒 false；原始声明指标、原始指令顺序／索引与 armObject、展开 slots／逐格 origins 分开保存。cycles／outputs／events 是展开机器的执行观测，expandedLength／staticInstructions 是展开磁带计数；不生成原输入四指标认证，不信任声明 metrics。转换错误保留完整 code／rawOrdinal／slot 和类别 detail；失败不执行半展开。调用总计共用现有2秒 replay预算，包含原始快照、转换、复制与 setup，转换／setup后及返回前检查同一 deadline；不提高10秒 worker／20秒外层预算。输入不变。冻结2254使用既有命令与冻结清单重新执行；未完成前不得称本包验收完成。R1 `sampling_identity_bound=false` 不变。

### R4 本包实际验证记录（2026-11-05）

本包功能接线、定向回归及冻结2254运行已完成；**当时同配置接线前后行为对照尚未闭合（现已由本文开头同门对照包补齐）**，不能以 `mismatches=[]` 或全 Unavailable 代替行为保持验收。原报告保留不覆盖；**不是 R4／R5 整体认证验收**，R1 身份缺边及 `sampling_identity_bound=false` 保持不变。

- enabled：`cmake --build build --target OpusOptimaTests OpusOptima OpusOptimaVerifyWorker OpusOptimaReferenceWorker -j2` 成功；最后仅重建测试目标。`build/OpusOptimaTests --gtest_filter='FiniteTapeExpansion.*:IndependentFiniteReplay.*:IndependentStaticProduction.FiniteRepeatResetReferenceMetricsDoNotAuthorizeRawTapes:IndependentStaticProduction.OfflineExpansionLockedDecoderInstructionDifferential'` **38/38**。日志：`build/r4-finite-build.log`、`build/r4-finite-build-final.log`、`build/r4-finite-tests-final.log`。
- disabled：沿用 `build/verifier-m1-no-native-20261028`，`cmake --build build/verifier-m1-no-native-20261028 --target OpusOptimaTests -j2`，相同测试程序使用 `--gtest_filter='FiniteTapeExpansion.*:IndependentFiniteReplay.*'` **36/36**。日志：`build/r4-finite-disabled-build-final.log`、`build/r4-finite-disabled-tests-final.log`。enabled 的两个 native 差分测试不计入 disabled 分母。
- 新接线覆盖单独 C、单独 X、C+X 成功，乱序／非零起点的原 ordinal/index/copySite、四项 raw 声明序列化保留、输入不变、展开计数与执行 cycles 分离、默认三个入口拒绝、Malformed／OutsideDomain／BoundExceeded／Conflict 原子失败及完整错误坐标保留、执行 CycleLimit。最初 Conflict fixture 实际可展开，测试如实失败；改为现有纯转换已锁定的 `rrrXG` 冲突，最终以上结果全部通过。
- 本次真实冻结命令：`node tests/functional_test/VerificationCorpus.cjs result/r4-finite-wiring-20261105 --frozen result/verifier-hardening-baseline-20261029/manifest.json --cohort all --diagnostics --workers 4`。见[本次报告](../result/r4-finite-wiring-20261105/report.json)及 `build/r4-frozen2254.log`。UTC `2026-11-05T01:11:18.737Z` 至 `01:12:37.360Z`，**2254/2254**，CLI exit 全0、legacy-gate Passed 2254、shadow 全2254为 `independent-profile-uncertified`／Unavailable、基础设施错误0、未分类超时0、报告 mismatches 空。不是旧1019通过数字，也不把全部不可用当等价证明；本次没有性能权威结论。脚本、输入、CLI／lib／两 worker 哈希保存在报告中，执行前后及最终再查一致；测试目标重建未改变这些二进制。
- 隔离追踪：`IndependentWorkerMain` 仍调用 `VerifyIndependentFiles`；`ReferenceBackend` 的生产补验仍调用 `VerifyStaticProductionFiles`。新函数在 src 只有声明／定义，无 CLI、worker、发布或旧认证调用者，未新增文件解码入口，strict codec 路径未改。legacy-gate 不变；不声称 legacy-gate 自身拒绝 C/X，保持 raw 拒绝的是原独立入口和独立资格。
- 预算：快照、纯转换、展开机器复制、CheckScope/setup、执行均从同一起点计入原2秒，返回超时不会保留 Completed；setup 在 CheckScope 前后检查，执行沿用循环预算。纯转换及容器复制不是可抢占操作，阶段结束才能观察 deadline，**不保证严格2秒内返回**，不新增 worker 入口或更改既有10/20秒保护。`totalMicros` 是总耗时，非原输入认证指标；若要求硬实时中断／该新 API 独立进程隔离，仍待后续授权，不以加预算替代。

未运行全工程 suite、目标机／游戏验收；未接 Repeating、不扩大纯转换域、不改 legacy-gate、不接图 checker、未提交。本节是本次有限功能包的证据；下文较早“首包不接线”仅指当时纯转换阶段，不撤销本节独立 opt-in 接线，也不开放认证。

### 本次授权有界身份包：已落盘片段，R1未验收

`tests/functional_test/CurrentBusinessIdentityEvidence.json` 是固定安装程序集实际离线读取的快照；
`GameProductionEvidence.py --audit-current-business-identity` 仅接受既定程序集 SHA256、资源长度／SHA256，复用
`current_grid_string`，并锁定注册、查询、成功分支、直接setter／effect窗口、metadata及EH记录。
未加载程序集执行方法，未读取存档，未启动游戏，未改变profile或2/10/20预算。

实际解码 `uint32(-757873037)=d2d3c673`，资源记录偏移59879、flags `4000000a`，文本为 **`claw-pivot`**。
`a48@1464`的e1写入、`962`注册索引9，以及`11bb@342→dc6→local16→11a6@389`的同一局部引用传递已可重复核对。
`dc9`把描述参数存入bf2；`11a6`在向this.11a3 Add原参数前调用11a7及dec，
后者经df1遍历bf7并调用6d7／子实例位置setter，**不能假定无副作用**。

**准确停止出口（不是离线不可能定理）：**资源解码不等于77c全部wrapper正常返回证明；无输入不能确定ReadString选择，
注册查询的唯一性／Maybe成功及初始化时间仍未闭合；dc6构造和11a7／dec前置effect未闭；
没有从插入后到111f采样时的同一owner／描述／成员稳定性证据。
尤其`claw-pivot`初始e7空，并不能直接进入类别1/2：1152还依赖另一getter事件的f1.Count；
bf7角色及后续反序列化生命周期effects均不由初始空数组推出。快照故明确
`sampling_identity_bound=false`、`certification_eligible=false`，逐项保存6条缺边。
A的111c/1122/1123非干扰独立未闭。本包推进的是资源和参数传递的可复验片段，**未交付同一采样候选／pair绑定，未通过R1**；
不继续滚动追加callee，交主会话决定下一有限范围。

验证命令（仓库根目录；均exit 0；无构建／全应用运行）：
```sh
python3 -B tests/functional_test/GameProductionEvidence.py '/home/edwards/.local/share/Steam/steamapps/common/Opus Magnum/Lightning.exe' --audit-current-business-identity > tests/functional_test/CurrentBusinessIdentityEvidence.json
python3 -B tests/functional_test/GameProductionEvidence.test.py EvidenceReaderTests.test_business_identity_bounded_windows EvidenceReaderTests.test_business_identity_rejects_unlocked_source
python3 -B tests/functional_test/GameProductionEvidence.test.py
python3 -B - <<'PY'
import json, runpy
from pathlib import Path
x = runpy.run_path('tests/functional_test/GameProductionEvidence.py')
m = x['Metadata']('/home/edwards/.local/share/Steam/steamapps/common/Opus Magnum/Lightning.exe')
assert x['current_business_identity_audit'](m) == json.loads(Path('tests/functional_test/CurrentBusinessIdentityEvidence.json').read_text())
print('fixed-source business identity snapshot: PASS')
PY
```
定向2项通过，完整EvidenceReader 142项通过，真实快照一致；定向逐字节变异拒绝及非锁源拒绝不等于生产语义证明。

### R1有限范围决策（本轮只读取证，不授权实现）

**选择普通单臂 `arm1`，不是claw-pivot。** 固定源a48@442输入`d2d3c702`，资源解码为arm1，@452写e1，@612发布95d；976注册索引4。@574..592构造含一个03c1值的e7，故它比初始e7为空的962更适合作为普通臂类别1/2的必要映射目标；这仍只证明描述形状，不把类别1/2预先命名为底座/抓手。29个e1初始化字面量的资源解码文本互异；这不是运行时77c返回、注册无别名/无改写或查询唯一成功证明。业务输入应明确为ReadString得到`arm1`且查询成功，不能凭无输入的11bb断言已选中它。

**本轮限额恰为三个直接声明/写入清单**（各2897个有RVA方法均解码，无失败）：e1为public非readonly，29处stfld全在a48；bf2为private非readonly，唯一stfld为dc9@2；11a3为public非readonly，唯一stfld为119f@17。无这三字段的直接取址记录。仅bf2具字段封装，且setter/显式构造调用仍须管住；11a3唯一赋值绝非成员唯一写入清单，公开List可经别名Add/Remove/Clear及元素替换。e1公开也不能借“唯一定义”推全时不变。本轮没有第四清单、递归callee或程序集执行。

最小义务链固定为：a48/b51及77c的arm1返回与注册976唯一性 → a4a/a4b/a4c、11bb成功分支 → dc6/dc9建立实例描述 → 11a6/11a7/dec插入原引用 → **同一owner的11a3在插入至111f取样间的成员插入、移除、替换与描述稳定** → 11dc/11a4、1152、dc8/df2/df3、11f2及111f@547..784的同一候选/子成员来源。现有清单不能给出该区间全部集合修改者，dc6和插入前effects也未闭；不递归它们。命名身份可在一次采样实际取得的描述上单独成立，不推出“它就是先前插入的实例”；跨调用生命周期是另一个有限时间区间命题，不能要求业务名证明无限未来。A的08bd两次08b5及1117→111c/1122/1123仍是独立阻塞，不因arm1命名完成而消失。

**结论：当前批准强度下，没有已精确可关闭、可承诺一次实现交付R1绑定的子域；原“唯一下一实际实现目标”须降回待授权目标，不能保证按当前约束完成。** 字段封装＋完整直接写入清单，只有在明确且适用的managed执行契约下才能导出有限不变式。契约必须覆盖公共字段/集合别名的写入权限、构造/初始化时机、库/外部调用effects、异常/finally、重入及并发；“无反射/动态写入、unsafe/native、外部线程”均非现成事实，即使排除这些，公开List的普通managed修改仍须闭合。

仅有两条实质路线，均须用户决定：**①保留实际生产全强度**，授权针对上述时间区间的完整修改者/别名及A非干扰证明范围；先交可穷尽的effect边界，未能列全即停，不承诺当前三清单预算内完成、不滚动追callee。**②明确批准条件化支持域**，规定初始化后注册/ID冻结、指定owner独占成员修改、所列调用对描述和列表的effect契约及线程/重入边界；再做有限条件证明，必须输出每项假设和未覆盖路径，不得冒称实际全域R1闭合。未经批准不能以此实现新门或放行认证。任何后续包遇到重复ID、注册替换、dc9重设描述、列表Remove/重加/替换、异常或重入改写即应拒绝或报告缺边；超固定方法/effect边界即停止，不追加相似fixture。R1/R2离线缺口不归实机，R4/fresh845不重跑，legacy/profile/2-10-20及认证关闭不变。

### A/B停止链复判与唯一下一实际目标（历史决策；以上述有限范围决策为准）

历史“本包到此停止”是**当时授权／范围出口，不是离线不可解定理**。已知而尚未利用完的有限路径是：
`06000a48@1454`的整数`-757873037`经`0600077c`写入`04000962.040009e1`，
注册数组`04000976[9]`及`ReadString → 06000a4a`查询，随后`060011bb`构造`03ef`、
`060011a6`插入`040011a3`。可做锁源、有界的解码／序列化绑定，而非继续猜半径、混淆名或数组形状。
**遗漏的是可有限尝试的推进方法，不是已取得语义结论**：目前未证解码闭包是否纯且有限、ID唯一选择、Maybe成功分支实例及e7索引角色；不读取存档、不执行程序集也不能凭空补这些事实。
A还另欠`111c/1122/1123`出边对当前owner的effect封闭；解码成功不解决它，未知callee也不能直接改名“需实机”。

**唯一下一实际实现目标：R1中的“固定源业务ID解码→反序列化实例→碰撞候选身份绑定门”。**
不是再做fixture／通用审计：在既有EvidenceReader及现有证据中实现一个仅接受锁定解码规则、注册查询、成功分支与同一实例插入形状的有限识别门，交付一个明确业务类型到候选身份／pair角色的可复验绑定，消除B的一条真实未知身份边；不只交付字符串值或更多字节清单。不得自动置全局certification_eligible。e7索引若涉及该角色也必须绑定；不涉及则明确为何无需它，不能借962初始空数组推断其他抓手角色。

**最小待选决策：允许重开上述已停止的有界链（含实际所需的0600077c、060011bb、060011a6及直接前置effect切片），但不授权全程序alias闭包、不扩大支持域、不新增未证effect公理。**
本决策包不追代码闭包，也不声称该门一定可闭合；后续应先固定有限切片清单与规则可表达性再实现。若需超出切片，报告确切缺边后停止，不能不断追加callee。
这只是恢复原证明义务所需的范围授权，不是要求用户接受较弱证明。若不授权，R1/R2关键路径仍阻塞；可并行的R3/R4实现不是替代完成路径。只有另行明确批准封闭调用域契约或收缩支持域，才可改变这一状态；不得静默采用。

本轮不修改legacy-gate、profile或replay/verification/CLI-publication的2/10/20秒预算，不运行游戏／存档／全量测试，不提交。下文均保留为证据与历史记录。

## 交付边界

本轮已实现独立执行、指标模型、连续碰撞内核、差分及无库发布链路，但当前
**`finite-ordinary-v1` / `finite-82x71-v1` 的认证资格仅限静态运动帧**。
旋转、平移、piston、pivot 及 wheel 转动的 reference 轨迹和面积等价性尚未证明，
认证入口返回 `Unavailable`，不能以有界重放完成或模型回归通过替代认证。生产墙体／导管、
Repeating 输出等尚未认证的组合仍不可用，**全局默认仍为 `legacy-gate`**。
阶段 2 的前置证据未关闭，阶段 3–5 不标记完成。

独立模式不调用 native 补齐分数或挽救结果；未入域、无法证明、超时与真正通过分别记录。
首个子域的通过并不表示已经移除整个项目对参考裁判的需求。

## 里程碑 2：统一执行与普通有限输出

[StaticProductionVerifier.cpp](../src/Verification/StaticProductionVerifier.cpp) 中的执行模型现在明确区分
普通棋盘和生产室环境，原生产入口保留旧 gate 的采样重放；新
[IndependentVerifier.hpp](../src/Verification/IndependentVerifier.hpp) 提供：

- `ReplayFiniteMachine`：普通／原有生产子域的有界重放，返回 `Completed` 等执行结果，不是发布授权；
- `VerifyIndependent`／`VerifyIndependentFiles`：只有规则、完整指标和连续判定均通过，才返回
  `Passed` 且 authority 为 `Independent`。

执行仍自己维护批次／原子身份、键、持有状态、输入再生、化学、磁带、同时运动和每输出
`6 × outputMultiplier` 完成条件，不调用求解器 IR、状态效果、调度或证书。

重放 API 的 trace 记录 cycle、执行 phase、对象索引和原子 ID，增加抓取／释放／运动／反应事件，
最多保留 4096 个事件并显式标记截断，事件 cycle 从 0 开始，完成周期数仍按实际完成时刻计数。现有 native C ABI 不提供完整事件轨迹，因此本轮没有声称
已完成 native 逐事件／状态差分。

## 里程碑 3：四项独立指标

[IndependentMetrics.cpp](../src/Verification/IndependentMetrics.cpp) 独立计算 cycles、cost、area、instructions：

- 不读取声明 metrics 来决定完成或分数；零指令机器合法；
- 面积包含静态部件、端口／导管／轨道、机械臂 shaft／swing、真实原子及运动 raster 的并集；
- disposal 的面积足迹是七格，反应接触仍只有中心；track 的序列化 rotation 不旋转其偏移；
- 统计真实非空动作，排除 `O`、空格和 `B`，但 halt 后序列化动作仍参与静态指令数量；
- 通过完整 motion frame 的观察接口记录扫掠面积，不能只观察端点；临界 raster 归属无法确定时，
  `Conclusive()` 不成立，不能独立发布。`Conclusive()` 只是指标模型的必要条件，不能证明
  动态 reference 轨迹／面积等价。`CertificationEligible()` 额外要求自初始化以来没有原子运动或
  机械臂姿态变化；后续静态覆盖清空临界格也不能恢复资格。`Finish()` 保留诊断指标，不代表认证。
  认证入口使用此资格门，并另以静态运动帧 gate 限制资格。

价格、计数及 raster 口径参考锁定 omsim 源码事实，但实现没有链接 native，也没有复用求解器估分。
原生差分测试覆盖静态足迹、六种室型、各朝向／长度、片段超出臂长的旋转、piston／track、
halt、多抓手、wheel 和伪造声明分数。

## 里程碑 4：连续碰撞内核

[ContinuousCollision.cpp](../src/Verification/ContinuousCollision.cpp) 对调用方提供的轨迹
`center + rotation(start-center, angle*t) + translation*t` 做判定：

- outward-rounded long-double 区间；sin/cos 使用含显式余项界的 Taylor 包围；
- 相对线性运动的解析最小距离、相同刚性运动的距离不变性、速度上界 broad phase；
- 复杂相对运动使用有限节点／深度的保守细分；没有固定采样即可返回 Separated 的捷径；
- 擦边、预算耗尽、非法数值、fast-math／不适合的算术环境返回 `Uncertain`，不默认通过。

认证目前只接收静态运动帧：先检查材料、六个 wheel 原子和所有底座的初态整数格占位唯一性，
并拒绝底座互换；不同静态格的最小间距 sqrt(41²+71²) 大于最大半径和 58。
任何材料旋转／平移、底座／臂长／方向变化或 wheel 转动均返回 `Unavailable`。
连续内核可验证传入轨迹，但尚未用来认证动态 reference 轨迹与面积映射。

### 内核的包围论证与验证边界

以下论证针对输入 double 所表示的精确实数轨迹，不包括调用方把游戏状态转换为轨迹时的误差：

1. 在 IEC 559、round-to-nearest、渐进下溢且没有 fast-math 的前提下，每个基本运算结果
   向两侧 `nextafter` 扩张。乘法取四个端点积的包围，平方单独处理跨零区间，除法仅用于
   严格为正的分母区间。因此组合运算包含对应的实数结果；非法输入或不支持的算术环境不认证。
2. sin/cos 各累加 32 项。sin 到 63 次，cos 到 62 次，但 cos 的 63 次系数为零，
   两者均可视为到 63 阶的 Taylor 多项式。64 阶导数绝对值至多为 1，故统一加入
   `|angle*t|^64/64!` 余项；该上界的逐次乘除也向上扩张，不依赖 libm 三角函数取样。
3. 单条轨迹速度的欧氏范数不超过 `|angle|*||start-center||_1 + ||translation||_1`。
   两条轨迹速度界之和 `V` 是相对运动的 Lipschitz 常数。对时间段中点 `m`，取向上包围的
   `h=max(m-low,high-m)`；若中点距离平方下界严格大于 `(clearance+V*h)` 平方上界，
   则整段均分离。只有所有子段都证明分离才返回 `Separated`。
4. 线性路径包围解析最近时刻，刚性同运动使用距离不变性；broad phase 使用初距与总可达距离。
   `Collision` 需要某个时刻的距离平方上界严格小于 clearance 平方下界，即严格侵入证据。
   接触边界、不能证明的子段、细分预算耗尽均为 `Uncertain`；clearance 为零的特例仅表示
   不存在负距离侵入，不能解释为接触安全。`PointAt` 的区间中点近似不是认证接口。

圆周解析 oracle 使用距圆心恒为 32 的轨迹，覆盖角度 ±1／±8、原点及坐标 1e9 边界，
验证 clearance 24 的实际细分分离、clearance 40 的碰撞、交换对称和零节点预算的不确定性。
新增测试后成功构建，连续碰撞定向集 **15／15 通过（0.31 秒）**。这为模型回归提供证据，
不是对算术实现的穷尽证明，也不证明集成 clearance 或 area raster 的误差包围。

随后增加 `Enclose(trajectory, begin, end)`，返回整个闭时间段的理想轨迹坐标包围，
而不是有限采样的 min/max。审阅依据如下：

- 输入 double 作为精确实数参数提升到 long double；端点满足 `0 <= begin <= end <= 1`。
  最近舍入的非负加法再除以二得到的计算中点仍在闭区间内（允许舍入到端点），不会溢出。
  不要求它等于精确数学中点。
- 两次减法各用 `nextafter(+infinity)` 外扩，故 `max(m-begin,end-m)` 的计算上界覆盖
  到两个端点的真实距离；包括相邻浮点时间、次正规时间及零宽区间。
- 上述速度界同时支配每个坐标导数的绝对值。因此中点坐标区间逐轴加上
  `[-V*h,+V*h]`（乘法和最终加法均外扩）覆盖整个时间段；Taylor 余项覆盖中点三角函数。
- 证明依赖 IEEE 基本运算的最近舍入、渐进下溢，以及编译器保留逐操作外扩语义；
  不支持破坏这些语义的重结合优化。当前运行时拒绝非最近舍入及 x86 DAZ/FTZ，
  编译时拒绝 `__FAST_MATH__`。这些检查不是对任意编译器选项或非 x86 控制寄存器的全面认证。

历史实际联合验证为连续碰撞 **17／17** 加 ReferenceMotion **9／9**，共 **26／26**
（约 52ms）；这是此前运行的记录，不是本次证据登记时重新执行的结果。
新增相邻／次正规时间区间回归后，实际执行
`cmake --build build --target OpusOptimaTests -j2 && build/OpusOptimaTests --gtest_filter='ContinuousCollision.*:ReferenceMotion.*'`：
构建成功，连续碰撞 **18／18** 加 ReferenceMotion **9／9**，共 **27／27**（25ms）。
这是已执行运行的证据登记，不是登记时重跑；编辑器诊断审阅仅见命名、magic number、复杂度等静态风格提示，
未将其表述为零诊断，也不以回归通过替代算术或游戏映射证明。
该 API 没有接入动态认证，也不包围 reference 的 float 运算、整数 snap 或游戏运动。

**数值模型限制必须保留：**内核只证明传入轨迹；锁定 reference 的 `0.125` 是时间步上限，
不是像素接触排除带或空间误差界。reference 先更新整数末态，再以末态 grab position 与 atom offset
反插值；现有初态欧氏旋转不等价（十格半径反例的末点误差约 0.140831 像素）。
动态轨迹、浮点步进与面积误差的完整包围尚未证明，因此动态认证保持 `Unavailable`。
这里公布 ruleset/model 来源，不把它叫全游戏形式验证，也不据此切换全局默认。
生产墙体及其运动／舍入语义尚未取得连续认证，故该类仍为 `Unavailable`。

### 锁定 reference 的末态运动诊断（非认证）

[ReferenceMotion.hpp](../src/Verification/ReferenceMotion.hpp) 与
[ReferenceMotion.cpp](../src/Verification/ReferenceMotion.cpp) 实现末态整数几何的 float 反插值诊断，
区分 Swing／Pivot／Track／Piston，并将 piston 属性独立于运动类型保存；这避免遗漏旋转或轨道运动上的
piston 分支。诊断保留 reference 的运算顺序，但未证明跨编译器位等价，也不验证整个末态是否合法。
非有限进度、冲突字段、越界坐标、退化 piston 和超出样本预算均返回无结果；默认内部采样计数预算为
65536，不通过放宽预算隐藏无法处理的轨迹。

内部采样计数保留 reference 的 `log/pow` 步长分类，不将数学上的整数阈值冒称为 libm
位等价替代。分类后的步长须是 `2^-k`，其中 `3 ≤ k ≤ 22`；在 IEEE 二进制且有效位至少
23 位的 float 上，从第一步到 1 的每个 `j / 2^k` 均可精确表示，逐次相加也精确，因此
内部样本数直接计算为 `2^k - 1`，再检查原有预算，无须线性迭代。无法确认这些条件时拒绝。
本次执行 `cmake --build build --target OpusOptimaTests -j2` 后运行
`build/OpusOptimaTests --gtest_filter='ReferenceMotion.*'`，**10／10 通过，187 ms**；
新增回归在全部 cadence 转换附近及坐标上界，将直接计数与原始 float 逐次累加比较，
检查预算恰好足够和少一个的边界。此计数论证不证明七格空间覆盖或连续碰撞安全。

当前实现还会在六个诊断 API 入口链检查浮点环境：拒绝非 nearest rounding、SSE DAZ／FTZ、fast-math 和不符合要求的 IEEE 格式。RAII 回归分别覆盖 DAZ、FTZ 及组合，并恢复调用者控制字。最新重建后运行 `ReferenceMotion.*:ContinuousCollision.*`，**30／30 通过（12＋18），178 ms**；原文 C 差分重新执行得到 **48 个末态不变量、187349 次逐位比较、211 个预期奇异端点拒绝**。Node 门控回归无外部 header 时为 **3 pass／1 skip**，提供锁定 header 时为 **4 pass**。这些结果不认证跨平台 libm 或游戏等价性。

原文差分另外抽取了 snap、七格候选顺序及严格距离谓词。最新执行通过 **30093 次 snap、90279 次 raster、187349 次运动逐位比较、48 个末态不变量及 211 个预期奇异端点拒绝**。空间样本包含 30000 个确定性随机点，以及正负 41／44 的恰好值与两侧 `nextafter`；另覆盖原点、非零 y 的半格残差边界及正负 43.999，每个额外中心取 x／y 各自相邻浮点的九点组合。每点比较 15／20／29 三种半径下的数量、坐标及顺序。这是有限样本回归，不是全域证明；约简布局不验证 ABI，使用 host libm、O0、禁 FP contraction。raster oracle 不执行 `mark_used_area`、`REMOVED`、真实 board 状态或连续面积。最新定向 `ReferenceMotion.*:ContinuousCollision.*:IndependentMetrics.*` 为 **54／54 通过，505 ms**；三个非 nearest 模式均覆盖六个诊断 API 的拒绝路径。

另有独立临时 C fixture，直接抽取同一双哈希锁源中的 collider append、bbox 与 board 距离谓词，严格使用 `-Wall -Wextra -Werror` 编译。实跑覆盖同批不互检、commit 后 prefix 检查、bbox 排除与禁用更新、15／20／29 半径切线相邻浮点、检查计数、碰撞锁存及 location 更新。board 分支以可控 `mark_used_area` stub 覆盖七格访问顺序、`REMOVED` 抑制 board 碰撞、最后命中位置、空 location，以及 board 标记不抑制 prefix 碰撞（计数 70＋1）。完整差分重跑成功，以上运动／snap／raster 计数保持不变。这里 append fixture 的调度为手写，面积存储是 stub；不证明本地 all-pairs 等价、真实 board 生命周期、连续面积或游戏等价性。

同一脚本还直接提取并执行锁源完整 ordinary collision scheduler，最新实跑 **101 个有限场景通过**：固定底座、墙、producer、移动底座、移动原子五类的 15 个无序类别对，各有重叠／分离控制（30 例），加 15 个批次／采样场景、40 个 board 标志／location 场景、8 个 board 锁存／最后 location 场景及 8 个移动 prefix location 场景。后者覆盖五类 collider × 回调返回 `0`／`VALID`／`REMOVED`／`VALID | REMOVED` × 有／无 location；断言只有 `REMOVED` 位抑制碰撞（`0` 仍碰撞）、检查数、面积调用数及 location 写入／保持哨兵。验证固定与移动底座逐个 commit、墙／producer 同批排除、同 movement 原子排除而跨 movement 检查，以及三个内部样本的 cursor／length／bbox 重置。双移动底座三样本的检查数为 423，面积调用数为 8：x=-61.5／-41／-20.5 时每底座分别访问 1／2／1 格，不能假设每样本标记数相同。每个类别对均断言碰撞结果、检查数与面积调用数。这些测试使用约简布局、`NO_CHAIN` 和人工可配置面积 stub；原 45 个场景仍用空 collision location 和返回 `REMOVED` 的 stub，chain 路径一旦到达即 abort。不验证真实 board 存储、chain、完整 ABI、整体 scheduler 等价或实际 float 全区间安全，也不改变生产认证门控。完整运动／snap／raster 差分重新通过，计数保持不变。

原文差分 driver 是依赖临时 C oracle 的独立程序，CMake 明确将其排除在 GTest 源集合之外；它仍由下述 Node 入口临时编译和清理。

此前实际执行 `build/OpusOptimaTests --gtest_filter='ReferenceMotion.*'`，**9／9 通过**；
覆盖双向旋转／伸缩、末态平移、pivot、piston 组合属性、非法字段与资源边界。
这两次单元测试是诊断公式和拒绝路径的回归结果，不是与游戏或锁定 C 实现的执行差分。

另已实际执行 [ReferenceMotionDifferential.cjs](../tests/functional_test/ReferenceMotionDifferential.cjs)
及其 [C++ driver](../tests/functional_test/ReferenceMotionDifferential.cpp)：
`node tests/functional_test/ReferenceMotionDifferential.cjs <locked-collision.c> <locked-sim.h>`。
两个参数须指向外部保存的原始源码：`collision.c` 的 SHA256 必须为
`a2df55f5a12ec8a107c6408e44d7e4373447b6741a8f59259829c2f6f96558b5`，
`sim.h` 的 SHA256 必须为
`198a3c4a74bc94fe1595e0159881797d450f387e92598191b77cbec3e8864a22`；
缺少参数或哈希不符会在提取和编译前拒绝。无需把第三方完整源码复制入仓库。
脚本提取普通原子中心计算的原文语句及权威 `movement_type` 枚举，显式映射全部四种机制。
结构体仍为约简布局，**不是完整 ABI 验证**。分别以 C11／C++20、
`-O0 -ffp-contract=off -Wall -Wextra -Werror` 编译，链接当前 host libm，结束时清理临时目录。
`CC`／`CXX` 可分别指定单个编译器可执行路径，不支持带参数的命令字符串。

门控回归入口为 `node --test tests/functional_test/ReferenceMotionDifferential.test.cjs`，
无需编译器或外部源码即可检查缺参数、错误 header 在编译前拒绝，以及已记录证据的计数／哈希／文档一致性。
另设置 `OPUSOPTIMA_REFERENCE_MOTION_HEADER=<locked-sim.h>` 可执行独立的错误 source SHA 拒绝测试；
未设置时该项明确跳过，设置了错误 header 则测试失败，不能充当 source 拒绝的覆盖。
这些门控测试不重新执行 C 差分，也不是独立语义认证。

增强版真实结果为 **187349 次原子中心逐位比较通过，211 个预期奇异端点被拒绝**，退出码 0。
共 20840 组运动，每组 9 个时间点：20000 组确定性随机运动，加上
四机制 × 11 个 baseRotation × 3 种变化 × 6 个方向的 792 组结构化运动，
以及四机制 × 6 个方向 × 正反变化的 48 组运动学一致末态样本。
后者从长度 2 的初态按整数旋转／平移构造末态：Swing 绕 base，Pivot 绕抓取点，
Track 同步更新 base，Piston 更新伸缩长度与抓取点。另以方向索引推导预期末态，
独立于构造用的旋转／平移 helper，检查全部 **48／48** 组的 base、抓取点、原子、
局部臂长、方向与 base→抓取点几何关系；driver 同时断言比较／拒绝总数及计数守恒。
它们不构成完整游戏合法性验证。
唯一允许的奇异拒绝为 piston 属性、grabber offset `(1,0)`、extension `1` 且 `t=0`。
此前 186917 次比较的结构化版及 179789 次比较的随机版属于较早执行，不与本次相加。
这些是有限样本、当前 host libm 下的原文语句提取差分；约简结构体／布局 shim 不等于完整上游 ABI，
输入也不保证是合法游戏末态。未验证完整 reference 程序、chain、base collider、cadence、
连续包围或跨平台位等价；额外测试的端点不代表它们属于 reference 内部采样循环。

该模块不接入动态 `Passed`，不提供连续包围、七格覆盖证明、面积等价或游戏认证；
上述证明缺口以及阶段 2 的游戏观察缺口仍保留。

## 里程碑 5：显式 provenance、shadow 与全库差分

能力矩阵在 [IndependentCapabilities.hpp](../src/Verification/IndependentCapabilities.hpp)，不按题名或哈希白名单接收。
当前范围与资源上界：

| 项目 | 范围 |
| --- | --- |
| puzzle | version 3，非生产，有限 `out-std` |
| 分子 | 连通，1–64 原子，无 Repeat |
| 输入／输出 | 各 1–16 个，multiplier 1–8 |
| 部件／机械臂 | 1024／128；臂长 1–3 |
| 动作 | 非认证回放支持抓取／释放、旋转、piston、单抓手 pivot、非循环且不歧义的 track、idle／halt；认证只接收静态运动帧 |
| 化学 | 已实现的成键／拆键及七类转换 glyph，含 wheel 催化剂 |
| track | 每条最多 256 个有序、唯一且相邻的格 |
| 磁带位置 | -20000…20000；归一化由自研代码执行 |
| part 位置 | q、r 和 hex radius 均在声明的 1024 上界内 |
| 库存／周期 | 8192 个当前原子／最多 150000 cycles；独立执行内部 wall-clock 仍为 2 秒，外层多个引擎共享 10 秒验解 deadline |

超出子域的长磁带／长臂／坐标不是机器非法的证据，返回不可用。静止且无磁带的 wheel 可重用
编辑器编号，不将其误认为冲突的活动机械臂。

资源防护在分配前执行：二进制列表按剩余字节及最小条目尺寸检查必要结构界，文件长度必须能由
`size_t`、`streamsize` 和字节缓冲区表示；这不是固定文件大小预算，也不能替代逐字段 EOF 检查。
机械臂和输入生成分别在分配前检查 128／8192 上界；库存回归使用带合法单键的 64 原子连通输入，
验证恰好 8192 原子可执行而下一组生成不可用。磁带归一化复用 map 节点，按偏移方向重键以免覆盖
尚未处理的指令；正负起点均保留多臂相对相位。原始磁带 vector 和全量文件字节仍可能同时驻留，
这些结构检查及节点复用不构成全流程固定内存上界；解析时间仍单列，不计入随后开始的验证 deadline。

面积 metrics API 每帧共用 8320（8192 + 128）条待处理 sweep 上限，原子运动与底座移动共同计数；
零运动观察不消耗配额，下一帧重新计数。超限令 metrics 失效，必须重新 Initialize 后才能继续。
这是 API 局部资源保护：当前认证 replay 在观察之前拒绝动态运动，实际仅提交静止观察，
因此不将该保护称为已证实可达的 replay 漏洞修复，也不证明全流程固定内存或未来动态／wheel
profile 配额充分；动态面积仍不具备认证资格。

### CLI

```sh
# 只验解，不求解、不回写 puzzle 或 solution：
./build/OpusOptima verify --puzzle P --solution S --authority independent --json
./build/OpusOptima verify --puzzle P --solution S --authority legacy-gate --json
./build/OpusOptima verify --puzzle P --solution S --authority shadow --json

# 显式独立发布；不支持的规则不回退 native：
./build/OpusOptima -p asset/puzzle/easy/just-salt.puzzle -s single-atom \
  --authority independent -o result/independent-salt.solution
```

JSON 包含 status、authority、engineVersion、rulesetVersion、profile、reasonCode、权威四指标及
可选 shadow 观察。退出码分别为通过 0、拒绝 1、不可用 2、预算 3、基础设施／格式错误 4。
独立模式不能被日志子串误标成 native-backed；覆盖 runner 现在要求显式 JSON provenance，缺失或
畸形来源即使出现旧的 `Valid: YES` 文本也不能计为通过。

Shadow 不改变 primary authority。发布先完成权威两遍验解，再提交；shadow 使用**重新序列化的私有
已接收值快照**，不重新打开可能被其他 publisher 替换的目标路径，观察失败不回滚合法 primary 输出。
已有快照竞态、错误 profile／authority 和 provenance 变化的故障注入测试。

### 冻结机器差分

[VerificationCorpus.cjs](../tests/functional_test/VerificationCorpus.cjs) 对原题库和导入题库中已有成功
solution 的 **2254 个不同 puzzle＋solution 哈希组合**重新执行，不重新求解、不回写输入。
使用四个独立进程，shared 10 秒验解 deadline，记录前后文件／二进制哈希。

[最终差分报告](../result/independent-finite-certified-shadow-20261029/report.json)：

| 独立结果 | 数量 |
| --- | ---: |
| 通过，四指标与 reference 相同 | **1019** |
| 按原执行预算超时 | **801** |
| 明确不可用／未入域 | **434** |
| reference 成功却独立明确拒绝 | **0** |
| 已通过机器的周期／四指标不一致 | **0** |

不可用项为 Repeat 26、track 上界 158、生产墙体未认证 10、磁带位置上界 235、臂数上界 4、part
坐标上界 1。没有按失败实例删白名单；规则界限公开，超时没有算成通过。这是验证器子域支持测量，
**不是 2401 个 puzzle 的新求解覆盖率**。原来的 128 个求解失败没有被当作坏机器标签。

## 下一阶段：证据与超时收敛（进行中）

历史 2254 对报告保持不变；[冻结清单](../result/verifier-hardening-baseline-20261029/manifest.json)
保存原报告 SHA256、四个构建产物哈希、全量输入及旧 801 超时队列。该目录还保留旧 worker、库和相关源码，
不把当前重跑结果覆盖到历史报告。

[数值证据契约](../tests/functional_test/VerifierNumericalEvidence.json) 区分名义模型、上游源码、游戏静态 IL
和游戏实际观察。[主网格资产证据](../tests/functional_test/MainGridAssetEvidence.json) 已通过锁定程序集的静态资源解码，
将四个纹理加载参数直接对应到四张 82×71 PNG，并追踪等尺寸包装、尺寸 getter、主网格初始化及运动／面积消费者；
此结论只适用于记录的程序集和资产哈希，不是实际游戏观察。可用 `GameProductionEvidence.py GAME --audit-grid-assets` 重现。
字节证据读取器在缺少 `#~`／`#-` metadata table stream 时明确拒绝；这不是完整 PE／CLI 格式验证。
读取器要求 IL 的 tiny／fat 方法头及声明的方法体完整落在同一节的文件支持区间内；
拒绝截断、跨 raw section、virtual-only RVA 和不足三 word 的 fat 头，避免切片静默截断生成不完整证据。
指令扫描使用显式 opcode 白名单，拒绝保留的单字节／FE 编码，并检查所有 operand 类别的截断；
`FE 19` 按 Mono 的 `no.` 编码消费一个 flag 字节（现代 .NET runtime 表将该编码标为 unused）。
全编码边界回归只固定读取器支持的编码与 operand 长度，不验证 flag 值、prefix 组合、分支目标、栈或 token 语义，
也不表示对应运行时支持执行该指令。这些无游戏依赖的合成回归只验证读取完整性，不证明 IL 语义或游戏等价性。

普通 collider 的独立入口 [ReferenceColliderOracle.cjs](../tests/functional_test/ReferenceColliderOracle.cjs)
在验证外部 collision 源与 header 的 SHA256 后，提取真实普通插入、坐标映射及矩形函数，
与原创 C fixture 的期望比较。直接编译运行已通过 8 组场景、40401 个格点 round-trip 和 2 个有符号半格 tie；
覆盖显式 cursor 分组、严格相切、七格标记、REMOVED 与包围盒旁路。board 为合成 stub，header 仅作哈希锁，
不验证 ABI；cursor 由 fixture 手动推进，不验证外层 collider 分类、chain、游戏等价性或连续空间覆盖。
另有独立的 ordinary outer-loop oracle（`tests/functional_test/ReferenceOuterLoopOracle.cjs`）：使用同一双哈希锁源及真实 sim.h，运行抽取的 collision 外层循环，
在真实插入函数前记录 collider／cursor。39 组有限场景（含六种固定类别无序配对、三种运动原子／固定类别配对，以及运动底座对固定底座、墙体、生成原子、运动原子和另一运动底座的五种配对）覆盖固定底座 prefix、墙体／生成原子不推进 prefix、墙体／生成原子不推进 prefix、
同 movement 排除／不同 movement 互检、track 末态反插值、移动底座、track／piston 的 increment=1 端点排除及 swing／pivot／piston 中点。
board 为合成单格 lookup，覆盖静止原子接触及位置、REMOVED 排除和相邻格分离，不是完整真实 board 实现。
另覆盖半径和为 58 的严格相切及相邻 progress float 两侧、increment=.3f 的三次采样与末端排除；track fixture 明确设置末态 absolute_grab_position。
另有八组普通调用边界回归：WHEEL_ATOM、GRABBED_ONCE、GRABBED mask 及 WHEEL_ATOM|GRABBED 四种标志分别用于静止／运动原子，检查几何不变性，并复核静止原子的 REMOVED 优先排除。这不证明上游 wheel／grabber 分类或生命周期。
chain 路径显式 abort；未覆盖上游 wheel／grabber 分类或完整 pair 矩阵。
该子进程复用原创资格检查并在自身进程独立检查浮点环境，另以五种 rounding／FTZ／DAZ 注入验证拒绝（不支持的注入返回 78）。
父 runner 的末行 JSON 在独立 `outerLoop` 字段保存 39 组场景及每种注入的实际 exit code／outcome；77 为 rejected，78 为 injection-unavailable，不能混称拒绝成功。该字段显式保持 `certificationEligible=false`，父级 insertion scope 不变。
不继承另一进程的资格化结论，不证明编译器／libm 正确性、连续覆盖或游戏等价，也不扩大认证域。
另有 20000 个整数中心（各坐标在 ±262144 内）及 1125 个针对半格、亚格与相邻 float 的中心；
每中心枚举 query 周围 25 格，对半径 15／20／29 检查严格接触是否落入七格。
边界中心取 axial anchors −1024／−1／0／1／1024 的 25 个组合，叠加 5 个偏移，再取两轴各 3 个相邻 float。
这些是有限回归，不是一般覆盖证明；原点与格 (0,2) 在半径 136 时重现七格遗漏，说明不能任意扩大半径。
这不是两个独立实现的差分，不能据此升级动态认证。
入口在任何几何测试前检查 IEC 559、binary32／binary64 参数、`FLT_EVAL_METHOD=0`、
round-to-nearest、渐进下溢与 ties-to-even 探针；SSE 平台还检查 MXCSR 的 FTZ／DAZ 位。
编译固定使用 `-O0 -ffp-contract=off`。每次运行还注入 upward／downward／toward-zero，
以及 SSE 平台的 FTZ／DAZ，要求在执行几何前以 77 拒绝；非 SSE 平台明确报告后两项注入不可用。
这只是可检查算术前提的资格化，不是编译器／libm 全域正确性证明，特别不证明 sqrt 全域正确舍入。

七格查询另有以下**条件性几何引理**，不是外层资格化：
设输入中心两轴绝对值 ≤262144，候选格的精确 `to_xy` 两轴绝对值 ≤262400，
半径 ≤29；使用 IEEE binary32、round-to-nearest、无 contraction／FTZ／DAZ，
并按锁源运算顺序求值，且假定 libm sqrt 正确舍入到 binary64（上述资格化探针不证明此假设）。此域中整数／半整数的 `to_xy` 乘积精确，
逆映射 `u=x/82-(0.5*y)/71`、`v=y/71` 各自绝对误差 <0.001；
`w=fl(-u-v)` 的残差 `|u+v+w|<0.001`。这些界分别来自每次除法／减法的半 ulp，
而非随机样本。三个初始 round 残差均 ≤0.5，因此 rounded 整数之和只能为 −1／0／1。
若和为 0，修正不改变整数；若为 ±1，最大绝对残差必与该和同号
（反号项绝对值若最大，其余两项至多抵消到 0.5，不可能达到至少 0.999）。
锁源严格比较选择一个最大残差修正；若最大者为 w，则返回的 u／v 不变，
等效于只修正 w。故修正后三个轴的误差均 <1.001；加逆映射误差后，
相对于精确 axial 坐标三轴误差均 <1.003。

七格以外的整数格在 u／v／w 至少一轴相差 ≥2，因此该轴与精确输入相差 >0.997。
三轴线性泛函分别为 `x/82-y/142`、`y/71`、`-x/82-y/142`，
其欧氏范数均 <1/70，故遗漏格的真实欧氏距离 >69.79。
另一方面，若锁源 float 距离 <58，则真实距离 <59：两轴减法各误差 ≤0.03125；
平方／求和非负，距离 <58 强制两轴 float 差绝对值 <59；
此小域平方与求和误差各 <0.001、sqrt 转 float 误差 <0.00001，
与减法误差合并仍小于 1。因此这两个条件不可能同时成立。
该引理只覆盖上述坐标／半径／算术前提下的 board 查询，不证明外层一定满足前提、
collider 分类完整、任意半径、连续轨迹或游戏等价；认证门控不变。

可重现：

```sh
node tests/functional_test/ReferenceColliderOracle.cjs LOCKED_COLLISION_SOURCE LOCKED_SIM_HEADER
OPUSOPTIMA_REFERENCE_COLLISION_SOURCE=LOCKED_COLLISION_SOURCE \
OPUSOPTIMA_REFERENCE_MOTION_HEADER=LOCKED_SIM_HEADER \
node --test tests/functional_test/ReferenceColliderOracle.test.cjs
```

未提供外部锁定输入时，Node 测试明确跳过相关源测试，不把缺失输入算作通过。

整数末态另由 [ReferenceSnapOracle.test.cjs](../tests/functional_test/ReferenceSnapOracle.test.cjs)
锁定完整 `sim.c`（SHA256 见 evidence）和同一真实 header，抽取方向归一化、方向基及位置变换，
以 UBSan 执行 59535 次有界变换：5 个基点、恒等平移及全部 6 个轴向单位平移（包括 `(0,-1)`）、旋转 −10…10、相对坐标各 −4…4。
每次检查逆变换和轴向六角距离守恒。还复现 `(1,0)` 旋转到 `(0,1)` 后，
82×71 嵌入的平方长度从 6724 变为 6722；因此不能把整数旋转视为该嵌入下的精确欧氏旋转。
本轮真实编译及证据契约测试 3/3 通过、无跳过；这是有限末态回归，不覆盖 movement 分类、键旋转、
board 重新插入、wheel/grabber 生命周期、全段插值或游戏等价，动态认证仍关闭。

同一双哈希锁源另有多组 UBSan 回归：
[ReferenceCycleOracle.test.cjs](../tests/functional_test/ReferenceCycleOracle.test.cjs)
抽取真实 producer/visibility 函数，以 192 个 flags/type 组合检查可见性，以 258 次入队检查扩容、重复保留及 half-cycle 分流。
真实 `initial_setup` 回归覆盖 5 个 table-size 输入与 60 个 wheel 原子的坐标、元素、抓取标志及输入边界调用顺序；
grid、area 与输入依赖仍是事件替身，不证明实际输入生成或完整 board 生命周期。
另抽取真实 `flag_blocked_inputs` 与 `spawn_inputs`，用 1024 个 IO 类型、双位置占据标志和旧 BLOCKED 组合，
检查 BLOCKED 清除、VALID/REMOVED 条件、短路 lookup、被阻塞不插入及先原子后 disjoint bond；另检查空 IO。
另有 64 个共享位置的双输入案例：统一判阻后逐输入生成，spawn 不重新 lookup，每个输入先原子后 bond，
受控插入回写后下一轮重新判阻。lookup/insert/bond 仍为替身，共享位置写入覆盖替身槽，
不证明真实网格 overlap 栈、碰撞处理或完整 producer 时序。
另在真实 grid/reset harness 接入相同的输入函数，新增 8 例 array/hash × 输入身份顺序 × 空/REMOVED 初始槽，
验证两个输入各两个原子的真实两层 overlap、双向 disjoint bonds、第二轮阻塞、逐层移除及重新生成时面积保留。
这八例使用两个不同的 from 键，不覆盖重复键语义，也不是完整输入、碰撞或游戏等价证明。
另以完整锁源 `sim.c`（而非抽取函数）运行 6 例 instruction/deferred movement 生命周期：
ARM 双向旋转、PISTON 单原子与普通键双原子伸长、VAN_BERLO 双向旋转，使用真实 grid、抓取/释放、wheel 创建、
延迟移动/重插、临时标志清除及 board 析构，UBSan 检查通过。
同一 harness 分别运行恒定返回 false 的受控 collision callback，以及哈希锁定的完整 `collision.c`；
后者重复六例无碰撞生命周期，并新增一例 ARM 旋转被 cabinet wall 阻挡的碰撞断言，均通过 UBSan。
双原子活塞还检查单个 movement 携带两个原子、双向普通键保持、释放后身份/抓取标志及远处旁观原子不变。
替身模式不证明真实碰撞，锁源模式也只是参考实现的采样检测，不是独立连续碰撞认证。
两模式均未调用 `initial_setup`；不证明完整周期调度、一般 chain/disjoint 运动、wheel/glyph 交互或四指标/游戏等价。
上述有限证据均登记源与 header 双哈希，保持 `certification_eligible=false`，不构成指标或游戏等价证明。
该 harness 也抽取真实 `run`，检查起始 half-cycle 1/2 × 三种 overlap 情形共 6 例的调用顺序及完成状态；
各 phase 仅记录事件，并不证明内部语义或真实 overlap 清除。
该 harness 还抽取真实 `schedule_flag_reset_if_needed` 和 `reset_temporary_flags`，不替换这两个函数，
以 UBSan 验证 64 个普通原子与 4 个 overlap 原子的队列扩容、重复调度、临时位清除、永久位保留及重置幂等性。
抽取边界对缺失、重复及异常尾部均拒绝。另接入真实网格 lookup／insert／rehash，
以 64 个 array 原子及 64 个 hash 原子经历四轮强制扩容，逐一验证 reset 队列指向当前网格地址、
原子值与面积保留，以及最终临时位清除。另抽取真实原子引用结构与 removal 函数，
验证两个交错三层 overlap 栈的逐层移除、overlap 数组压缩、reset 与 removed 槽重插入，面积保持为 2。
另以 128 个 array/hash 格验证真实 used-area 标记函数：首次标记计面积、重复标记不增面积，
并区分 VISITED 与 REMOVED 的 overlap 计数；这不覆盖运动面积栅格化。
真实 `move_atom` 另以 128 个普通原子验证移动缓冲区增长、快照、旧槽 MOVED／movement index、
延迟重插入后显式调度临时位 reset 及面积保留；两层 overlap 移动验证幸存原子不会被误标记为 MOVED。
另以三个 chain 原子验证碰撞 bucket 与 movement list 的摘链／重连；真实 `move_atoms` BFS
覆盖三个普通键原子与一个 disjoint 目标、排除一个 recent-only 邻居、迁移双向 disjoint bond、
相同移动去重及冲突移动报告。有限断言也检查平移／旋转中心 equality 和首个 collision 保留。
这些不是一般 chain 运动、任意 BFS 拓扑、完整 grab/drop 或运动认证的证明。
同一 harness 另将锁源 collision 普通插入函数与真实 grid／reset 拼接，
以 UBSan 检查 3042 个有限案例（array/hash 两个原点、三种半径、13×13 整数偏移及空／占用／移除三种状态）。
独立整数平方距离谓词核对逐格面积标记，并核对碰撞位置、检查计数与插入长度。
该回归要求通过 binary32／binary64、round-to-nearest、subnormal 及 SSE FTZ／DAZ 资格检查；
注入三种非默认舍入及 FTZ／DAZ 后必须拒绝执行（非 SSE 平台可显式报告后两项不支持）。
这些检查不证明连续运动栅格化、完整四指标或游戏等价，也不解除动态认证门控。
这些检查只覆盖有界扩容后的指针重建、代表性键位及移除重插场景，
不证明一般 grab/drop 或 deferred reinsertion。
本轮 collider、cycle/reset、visibility、snap 四组联合 18/18 通过，失败与跳过均为零；动态认证保持关闭。

整数端点 oracle 另抽取真实 `rotate_bond_bits`／`rotate_bonds`，对五组键位分别枚举全部 64 种掩码及 −10…10 的旋转，共 6720 例；逐位独立构造期望值，检查逆旋转及所有非键位保留，使用 UBSan。此枚举不是所有跨组联合掩码的穷尽，也未覆盖 deferred reinsertion、完整抓放生命周期或连续运动。真实 deferred 路径在碰撞检查之后重新插入原子，并处理可见 chain 原子与 disjoint bond；端点和键位回归不能替代这些路径的验证。
[ReferenceVisibilityOracle.test.cjs](../tests/functional_test/ReferenceVisibilityOracle.test.cjs)
抽取真实局部引用类型及三个可见性谓词，检查 10 种 mechanism × 256 种 flags 组合共 2560 例，
覆盖 removed/moved 臂优先级、wheel 可见性及有键/被抓取的输入排除。
lookup 和坐标变换为固定桩，只选一个普通键位、recent 键位和抓取计数代表；不覆盖全部 flags 域、
真实网格、wheel 创建或 grab/drop 生命周期。两组均登记非认证证据，不关闭运动、指标或游戏等价义务。
运行方式与下方相同，将测试名换成对应测试即可。

```sh
OPUSOPTIMA_REFERENCE_SIM_SOURCE=LOCKED_SIM_SOURCE \
OPUSOPTIMA_REFERENCE_MOTION_HEADER=LOCKED_SIM_HEADER \
node --test tests/functional_test/ReferenceSnapOracle.test.cjs
```

碰撞分类／pair filtering、reference 时间步与真实空间误差包围、面积误差公式等义务仍未全部关闭，
不能把已有静态证据升级为游戏观察或全游戏认证。尚未开放资源扩展 profile，生产与 repeat 子域继续受原门槛约束。

执行器现已在异常路径保留 cycle／phase、已完成输出、库存高水位和有界最近事件；计数默认启用，
`verify --diagnostics` 额外采集 phase、raster、collision 时间。worker 协议 v3 保持 65536 字节响应上限，
最多传送 64 条事件、每条 8 个原子 ID，并显式标记截断。硬 kill 没有返回现场时不补造诊断。

冻结诊断入口使用原 CLI／worker，不求解、不改变规则或预算：

```sh
node tests/functional_test/VerificationCorpus.cjs result/NEW-DIAGNOSTIC-DIRECTORY \
  --frozen result/verifier-hardening-baseline-20261029/manifest.json --diagnostics
node --test tests/functional_test/VerificationCorpus.test.cjs
```

默认仅重跑旧 801；`--cohort all` 选择冻结全量，`--workers 1` 支持串行慢例复核，默认仍为四 worker。
runner 校验源报告、队列成员及每个输入的哈希，保存当前二进制和脚本哈希、主机信息、逐例结果与归因，
输出必须是全新目录。返回的 replay 内部超时、worker 父进程 kill、CLI runner kill 分开记录；
连续接触证明不确定单独归类，**不把所有 `Uncertain` 都冒称节点预算耗尽**。

[801 项首轮诊断报告](../result/verifier-timeout-diagnostic-20261029/report.json) 已完整产生：
801 项全部为返回现场的内部 replay 预算耗尽，未出现父进程 kill、基础设施错误或明确拒绝，
不是 801 项都已获得证明。累计 phase 时间中 motions 为 1145.06 秒，spawn 为 203.32 秒，glyphs 为 207.39 秒；
motions 内 raster 为 26.67 秒、collision 为 194.76 秒，其余成本仍须进一步归因，不能仅凭差额认定某个函数是热点。
该诊断运行不替代无计时的三轮规范配对对比。

区间内核已改用向上包围的 `max(mid-low,high-mid)` 覆盖非精确中点，并在区间整体分离判定前拒绝退化中点；
非 nearest 舍入与 x86 DAZ／FTZ 环境返回 `Uncertain`。14 项内核测试、24 项 Node 脚本测试及 14 项 evidence
测试通过。数值加固后的 enabled／disabled CTest 分别为 **675／406 项全部通过**；
[2254 对完整差分](../result/verifier-numerical-hardening-full-20261029/report.json) 为 **929 通过、891 内部超时、434 不可用**，
没有明确拒绝或通过例四指标不一致。该单轮相较历史多 90 个超时，不能据此宣称无性能回退或阶段 2 已收敛；
后续重复测量与热点优化仍须保留并解释该结果。

诊断计时不是规范成绩；phase 时间包含其 raster／collision 子任务，不能相加当作互斥成本。
`collisionMicros` 仅在启用计时时累计实际执行的碰撞检查：认证路径的静态重叠及运动资格检查，
或非认证 replay 的离散采样检查（含墙检查及其位置准备）；异常退出也累计。
它不是连续碰撞内核耗时：当前执行器没有调用该内核，动态轨迹仍不具备认证资格。
每个检查作用域取整至微秒，短检查可以为零。
新增 `observeMicros` 单独累计静态足迹观察（包括 wheel 足迹构造），仅在显式启用计时时采集；
`snapshotMicros` 累计运动前原子及机械快照构造，`fragmentMicros` 与 `heldMicros` 分别累计片段和持有查询。
持有查询包含其调用的片段查询，因此后二者是 inclusive 时间，不能相加为互斥成本；
每次调用取整至微秒，短查询可以为零。这些值也包含在调用它们的 phase 时间内。
worker 协议相应升级至 v4，旧版本帧会被拒绝，
父进程与 worker 必须来自同一构建。该诊断改动不改变冻结历史报告，也不表示新的性能收益。
完成例 CLI p95 也包含 primary reference 工作，不等于独立 replay p95。
### 运动快照优化与配对结果

机械运动快照只复制本周期姿态、持有状态和动作，不再复制整条只读磁带；没有省略碰撞、面积或时序检查。
[五轮全量配对报告](../result/verifier-snapshot-comparison-five-rounds-20261102.json) 使用相同 2254 对输入、四 worker、原预算，
十份报告的共同完成集为 908 对，未观察到明确拒绝、非超时状态或四指标变化（`semanticChanges: []`）。
实际执行顺序为 before 1–3、after 1–5、before 4–5，跨日期分块，**不是严格交错实验**；机器噪声和执行顺序限制因果归因。
[旧三轮报告](../result/verifier-snapshot-comparison-20261029.json) 原样保留，其共同集为 910 对；共同集不同导致前三轮 p95 与下表略有不同。

| 轮次 | before 通过／超时 | after 通过／超时 | 共同完成 CLI p95（毫秒）before → after | 共同完成 replay p95（微秒）before → after | 恢复／新增超时 |
| --- | --- | --- | --- | --- | --- |
| 1 | 929／891 | 1199／621 | 1770 → 1221 | 1731229 → 1189754 | 270／0 |
| 2 | 952／868 | 1164／656 | 1692 → 1312 | 1653308 → 1269006 | 212／0 |
| 3 | 955／865 | 1137／683 | 1707 → 1381 | 1668844 → 1340890 | 183／1 |
| 4 | 980／840 | 1290／530 | 1631 → 1066 | 1593848 → 1031311 | 310／0 |
| 5 | 965／855 | 1254／566 | 1646 → 1135 | 1605852 → 1092435 | 289／0 |

每轮不可用均为 434；第三轮新增超时保留，完整超时成员、输入报告哈希与执行顺序均保存在报告中。
以上 p95 使用 nearest-rank，仅覆盖相同的共同完成集，不能代表超时项的完成成本。
五轮比较报告的 `memory` 与 `serialSlowCases` 仍为 `not-collected`；以下结果来自独立串行报告，不混入五轮统计。

[六个关键慢例串行复核](../result/verifier-snapshot-serial-20261029/report.json) 选择冻结超时中解文件最大的三例及 baseline 完成中最慢三例，
交替 AB／BA／AB 各三轮，另跑一对诊断计时；24 行无探针错误或已完成配对四指标变化。
下表仅计非诊断三轮；每侧每例仅三个样本，nearest-rank p95 即最大值，不作总体分位数推断。

| 输入索引 | 三轮状态 before／after | probe elapsed p95（毫秒）before → after | replay p95（微秒）before → after | worker 峰值 RSS（KiB）before 范围／after 范围 |
| --- | --- | --- | --- | --- |
| 146 | passed／passed | 1905 → 1803 | 1883292 → 1778582 | 13264–13328／13264–13324 |
| 188 | timeout／timeout | 2024 → 2022 | 2003742 → 2000741 | 13324–13328／13260–13324 |
| 345 | passed／passed | 1813 → 702 | 1795206 → 681942 | 13232–13264／13264–13264 |
| 392 | timeout／timeout | 2031 → 2030 | 2002207 → 2000789 | 13264–13264／13264–13328 |
| 1852 | passed／passed | 1665 → 1108 | 1649048 → 1090424 | 13264–13328／13260–13324 |
| 2136 | timeout／timeout | 2024 → 2030 | 2002882 → 2000877 | 13260–13264／13264–13324 |

持续超时三例的耗时被 2 秒 replay 预算截断，不是完成用时，也不证明加速。
内存采用每次新父进程的 Linux `getrusage(RUSAGE_CHILDREN).ru_maxrss`；非诊断每侧 18 次观测的总范围为
before **13232–13328 KiB**、after **13260–13328 KiB**。诊断第四对不计入上述范围或 p95。
这些值仅表示已测慢例的 worker 峰值，既不是整个 profile 的内存上界，也没有证明 RSS 改善。

### 串行复核的协议与样本资格

当前串行工具接口为：

```sh
node tests/functional_test/VerificationSerial.cjs OUTPUT BEFORE_PROBE AFTER_PROBE BEFORE_WORKER AFTER_WORKER MANIFEST BEFORE_REPORT
```

两侧 probe 各自必须与其共享库及 worker 的 ABI／协议匹配，不能用当前 probe 代替旧构建 probe。
报告 schema 为 2，记录四个根 ELF 及 `ldd` 解析出的绝对路径依赖 SHA256；这不是对运行时 `dlopen` 依赖的完整证明。
测量结束重新检查这些文件、manifest、baseline、脚本和所选 puzzle／solution 哈希；变更使运行失败，
保留已落盘逐行观察，但不生成最终报告。已有输出目录或文件不覆盖。
双侧状态、真实退出码／signal、显式错误、RSS 和完成例四指标均为硬检查；
只有非诊断、双侧 Passed 且 cycles／cost／area／instructions 一致的配对才有性能资格。
双侧 timeout／unavailable／cycle-limit 只是未完成观察，第四轮诊断也不进入完成成本统计。
存在硬错误退出 1；无硬错误但零合格完成配对退出 2，不能因 `errors: []` 宣称性能验证通过。

[子任务计时旧串行报告](../result/verifier-subtask-serial-20261103/report.json) 的两侧协议不匹配；
[protocol4 重测报告](../result/verifier-subtask-serial-protocol4-20261103/report.json) 的 before 全部为协议错误，
after 首轮六例全部因动态轨迹／面积未认证而 Unavailable，不能作为同工作量完成样本或性能收益证据。
两份报告原样保留，不与上述历史快照测量混合。
恢复隔离旧版 probe 后的 [ABI 恢复重测报告](../result/verifier-subtask-serial-recovered-20261103/report.json)
包含 24 个配对观察：before 为 16 次 timeout、8 次 Passed；after 为 24 次 Unavailable
（`independent-profile-uncertified`）。全部 24 对状态变化，合格完成配对为 0，
`performanceConclusion` 为 `invalid`，runner 退出 1；提前拒绝动态轨迹／面积不构成性能收益。
旧版 probe 的 10 个布局断言仅覆盖所用结构布局，不是完整 ABI 等价证明。
复核报告记录的四个根 ELF 及其依赖共 14 个文件，当前 SHA256 全部匹配；
该复核不覆盖任意运行时 `dlopen`，也不把旧报告重新解释为有效性能结论。
串行及比较脚本定向测试为 21／21（含四个 CLI 子场景）：覆盖零完成资格、显式 payload 错误、
非法 RSS、输入／机器／ELF 在测量期间变更及逐行结果持久化；这些合成 fixture 不证明验证器游戏语义。

合格第四／第五轮已完成，四构建产物组内一致、2254 对输入完整且无基础设施错误。
历史排除项原样保留，不拼接半轮、不计入比较：
- `verifier-numerical-before-round4-20261029` 和 `verifier-numerical-before-round4-retry-20261102`：权限／worker 启动错误；前者还混用了 reference worker。
- `verifier-numerical-before-round4-devices-20261102` 和 `verifier-numerical-before-round5-devices-20261102`：虽无基础设施错误，但混用了 after reference worker。
- `verifier-numerical-before-round5-all-binaries-20261102`：中断，缺少最终报告。

截至本次记录，五轮噪声复核与串行／RSS汇总已完成，但阶段 2 仍未完整验收：剩余高成本子任务尚未全部逐项定位；
阶段 1 的浮点／规则与实际观察义务未关闭，因此阶段 3–5 不开放，不以性能改善代替语义证据。
先前轮次新增回归 enabled／disabled 为 **676／407 项全部通过**；该计数不代表后续资源加固后的最新验收。
资源加固后 enabled 完整构建成功，完整 CTest **687／688 通过**；唯一失败
`CliMaterialCapabilityRegression_edge-cases_c654382114940518` 在首次 assembly 求解阶段未能于既定 20 秒内发布解，
单项重跑同样失败。旧冻结报告 `local-b3-b6-complete-fresh-20261027/coverage.json` 中同题 assembly 已记录
`process-timeout`；这是历史超时证据，尚不能单独证明当前改动对耗时没有影响。disabled 完整构建及 CTest **419／419 通过**。
replay／parser 定向回归 **33／33 通过**，Node 脚本 **27／27 通过**；enabled 全套中的 evidence reader 和独立 CLI 测试通过。
先前 evidence **15 项**、两配置 CLI 各 **6 项**通过记录仍属于原轮次，不冒充本轮内部用例计数。
[先前全新原题扫描](../result/verifier-hardening-original169-fresh-20261102/coverage.json)
严格执行 169 文件 × 五后端共 **845 次**、每次 20 秒限制：169／169 文件和 146／146 字节不同题目有成功候选，
相对首版冻结的原题扫描没有成功文件回退；该扫描仍走默认 legacy-gate，不代表独立 profile 的全规则覆盖。
[资源加固后全新扫描](../result/local-independent-offline-fresh-20261102/coverage.json) 已完成同样 **845 次**调用：
**168／169 文件、journal 37／37、145／146 字节不同题目**有成功候选。两轮 169 个输入 SHA256 全部一致，
唯一成功状态回退为 `edge-cases/c654382114940518.puzzle` 的 assembly：Passed → process-timeout；
上轮成功指标为 cycles 10771、instructions 16514、area 1753、cost 6405。所有两轮共同 Passed 候选的四指标均无变化。
独立工作目录中的 20 秒 CLI trace 以 exit 124 复现；8 秒 GDB 单点采样位于 `SearchSupplyInventoryCore`
的 Store／Discard 递归分支。该采样只定位当时执行位置，不证明 vector 搬迁是瓶颈，也不证明本轮资源加固造成超时；
当时这一题的覆盖回退仍未解决；后续修复与验收见下节，不作为全阶段完成验收。
隔离测量已修正为绝对路径只读绑定全部四个构建产物、独立设备挂载及仅输出目录可写；
最终第五轮恢复报告完整退出并通过比较器一致性检查，不再有待运行的补测。
`legacy-gate` 默认、2／10／20 秒限制及原历史报告均不变。

## 库存搜索修复与最新离线验收（2026-11-03）

上述材料能力超时已通过库存搜索的必要条件剪枝恢复：Projection 必须有未排除且 recipe 配额非零的
Quicksilver feed；金属可达性只从允许的原料播种，并按已启用且配额允许的 Projection／Purification
向高等级金属传播。该检查忽略数量及顺序，只提前拒绝不可达目标，不把可达性当作可执行性证明。
缺水银、排除水银、零原料配额、零 Projection 配额及多级净化均有回归；合法 Projection 使用实际搜索
所得计划构建执行 IR 并 replay。目标 assembly 恢复正常首解约 **1.59 秒**，不是无候选快速拒绝。

[修复后全新扫描](../result/local-independent-metal-reachability-fresh-20261103/coverage.json)
已完成 **169 文件 × 五后端＝845 次**调用：**169／169 文件、journal 37／37、146／146 字节不同题目**
有成功候选，production 无成功候选数为 0。输入文件及 SHA256 与两份基线一致；相对资源加固后扫描，
仅上述 assembly 从 timeout 恢复 Passed；相对首版扫描成功文件集合不变，共同 Passed 候选四指标不变。
目录名称中的 independent 不改变实际 authority：本轮仍为默认 **legacy-gate**，不是独立全规则认证。

最新 enabled 完整 CTest **690／690 通过，553.78 秒**，
[完整日志](../result/local-independent-metal-reachability-fresh-20261103/validation/enabled-ctest.log)
已持久保存；该次使用此前成功构建的产物，并未在 CTest 前另行重建。
disabled 重建后完整 CTest **421／421 通过，26.63 秒**。新增执行 IR 回归所在轮次定向 **4／4 通过**；
独立 Node **27／27** 是先前资源加固轮次的结果，不冒充新一轮执行。

保留中间轮次 enabled **689／690、293.61 秒**的失败记录：
`ProductionRouting.IndependentlyVerifiesReusedMolecularConduitStreams` 曾在约 2103 ms 返回空 machine；
后续单项通过，最新完整套件也通过，但**根因仍未归因**，不能声称已修复或已证明为偶发噪声。
没有为此修改 production 实现或扩大预算。

阶段 2 的游戏观察／浮点适用域证据仍未关闭，阶段 3–5 的 profile 资格仍受其前置约束；
production 连续认证及 repeat/reset／Repeating 也尚未交付。以上是离线回归与覆盖恢复，不是所有阶段完成。

## 里程碑 6：独立发布与无库配置

同目录临时候选 → 独立验解 → 回填独立指标 → 复验 → rename 已可用于受支持子域。第二遍要求
metrics 和 profile／engine／ruleset provenance 重现；任一失败保留旧输出，未知规则不可发布。
父进程也拒绝未登记的独立 profile、错误 authority 或缺少来源的 Passed 响应。

独立 worker／父库／主 CLI 不链接 `libverify.so`，独立 worker 不链接 solver／HiGHS；参考适配器仅在
可选 reference worker 与明确的 oracle 测试中。关闭参考裁判的完整构建也能独立验解、发布与拒绝，
没有删除、改名或修改现有 `asset/libverify.so`。

```sh
cmake -S . -B build/verifier-m1-no-native-20261028 -G Ninja \
  -DCMAKE_C_COMPILER=/usr/bin/gcc -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
  -DOPUSOPTIMA_ENABLE_LIBVERIFY=OFF -DBUILD_TESTING=ON
cmake --build build/verifier-m1-no-native-20261028 -j 4
ctest --test-dir build/verifier-m1-no-native-20261028 --output-on-failure -j 1
```

完整回归：启用参考裁判 **662/662**、关闭参考裁判 **393/393**，均零失败／跳过；脚本测试 **19/19**、
游戏证据读取器 **8/8**。CLI 测试涵盖只读 JSON、伪造声明分数、无库真实发布、未知动作、截断文件、
共享预算、shadow 不提升旧 gate 及拒绝时旧输出保护。

## 参考运动门控后的离线验收

本轮 enabled 完整 CTest **709／709 通过，439.45 秒**；disabled 完整重建成功后，
完整 CTest **440／440 通过，41.28 秒**。disabled 旧构建曾出现 **420／421**，
不能把该失败删除或算入重建后结果。CLI 回归已按实际资格边界更新：静态指标
为 3／20／3／0，动态独立验解拒绝且保持输入只读，拒绝发布不覆盖已有文件，
动态 shadow 的 authority 为 `none`。没有修改生产拒绝逻辑来迁就旧断言。

全部五个 Node 测试文件使用锁定 reference header 执行：**31／31 通过，0 失败、0 跳过，
8910.707079 ms**；这不是重新执行完整 C 差分。byte-only 游戏证据读取器回归
**15／15 通过，0.006 秒**，测试不需要安装或执行游戏。
参考运动／理想连续碰撞定向 **30／30** 及原文提取差分的适用域保持不变。
以上结果是上述历史验收之后的新一轮记录，不替换首版或库存修复轮次的数字，
也不是阶段 2–5 或新的 169×5 扫描验收。

来源调查发现本机安装的 assembly SHA256 为
`ee4d1740609cbc09b5eb52244acc63da32f9e3cc280309049ceb7b708d20e1c5`，
与已审计版本 `fd804fadedbf1b991ed37f3ec7325b2160317f0585e3e908fb73f246a038368d`
不同。三个 audit 保持旧哈希门控；此前 constructor 追踪在哈希检查处停止，
没有得到可用于新版本的 IL 证明。没有启动游戏、执行 assembly 或操作存档。
不能移植旧 token、直接更新锁值或以现有回归通过替代来源重建。

随后新增独立的 `--audit-current-colliders`，结果持久化于
`tests/functional_test/CurrentColliderEvidence.json`；它不替代上述三个旧版本 audit。
当前版本只读 metadata／IL 检查涵盖 collider 常量与插入模板、局部 pair 报告、
虚槽声明、报告存储／访问器、Maybe 存储／转换和 binary32／binary64 数值调用链。
结果始终为 `partial-byte-evidence-only`、`certification_eligible: false`。
这些字节证据不证明运行时分派、所有位置／分类来源、消费者与异常语义、连续覆盖或指标等价。
新增持久证据全结构一致性回归，仅归一化 constructor 参数显示名称与 namespace，
不忽略 token 或其余证据字段；此前读取器 **44／44 通过，0.698 秒，exit 0**。
随后纳入空值转换 `0x06000d66`、对应 MemberRef `0x0a0006ba` 和报告
HasValue 访问器 `0x06001283` 的声明／IL 模板，增加五种篡改情形。
重新只读运行真实 assembly CLI 并同步持久 JSON 后，读取器
**46／46 通过，0.625 秒，exit 0**。synthetic 测试本身不依赖安装游戏。
两方法的 fat header 分别锁定为 `133002001000000060010011`、
`133001000f00000033020011`，局部签名分别为 `07011300`、
`070115118f78011287f8`；逐字节篡改覆盖 maxstack、InitLocals、locals token、
code size 及 MoreSects（拒绝额外 EH 段），另覆盖局部签名和 header 解析边界。
其他方法的 header／locals／EH 尚未全部纳入该锁定。
追加 implementation、flags、signature、owner 边界篡改以及 StandAloneSig row
重绑定回归后，历史结果为 **48／48，0.755 秒，exit 0**。
随后增加条件 reset 方法 `0x06001293` 及前置 `0x0600128e`、abstract 槽
`0x060011e1`、抛异常 helper `0x06001295` 的 metadata／IL 锁定。
`1293` header 为 `03300200c500000000000000`；偏移 6 的分支可在 8 提前返回，
偏移 10 的 abstract virtual 调用及其失败 helper 可阻止偏移 85 的空报告赋值。
因此这不是无条件清空；前置调用、分派、异常构造和下游消费者仍未证明。
拒绝回归覆盖四方法的 metadata／body、abstract RVA 和全部 12 个 header 字节；
真实只读 CLI 输出已重新持久化，完整 synthetic 回归 **50／50，0.805 秒，exit 0**。
这些观察不能当作完整方法语义证明，也不替换此前 15／25 项历史记录。
随后接入泛型报告 wrapper `0x0200021f` 的五个方法、八个 MemberRef、三个
TypeSpec、存储／screen 字段及 mscorlib 完整身份检查，并增加 metadata／body
篡改回归。首次 50 项运行因持久 JSON 尚未同步出现 1 项失败；新增测试首次
52 项运行暴露 synthetic 索引变更访问不存在字典项的问题，已改为篡改对应
blob／string 内容。重新只读生成真实程序集证据后，最终 **52／52，1.498 秒，exit 0**。
wrapper 证据仅绑定声明和方法字节，不证明外部库行为、初始化、运行时分派或异常语义。
动态资格、默认 gate 和预算均未改变。

Maybe wrapper 的局部门控进一步核对六个方法及转换消费者的 owner、
两个存储字段的 field owner、动态定位且唯一／无约束的类型 GenericParam，
并拒绝这七个方法的方法级泛型参数、额外 EH、不同 tiny／fat header 或 locals。
Metadata 建立 owner map 前检查 TypeDef 的 MethodList／FieldList：起点必须为 1，
边界在对应表的合法范围内且单调不减；允许空类型的相等边界，拒绝倒退覆盖、
越界及无人归属的成员。`#~`／`#-` 均拒绝非空 FieldPtr、MethodPtr、
ParamPtr、EventPtr 和 PropertyPtr 表：当前 reader 不解释这些间接索引，不能
将其误当作直接成员顺序。独立合成 PE／CLI 构造器回归覆盖两种 stream 的
方法／字段合法范围、倒退／零／遗漏首成员／越界范围及五种非空指针表；
另有 helper 与 owner 篡改回归。新增回归后 EvidenceReader CTest **1／1 通过，
11.91 秒，总计 11.94 秒**。
这只加强声明和字节绑定，不证明转换异常依赖、消费者或碰撞失败语义，
也不将当前程序集提升为可认证版本。

碰撞方法的局部字节门另检查显式 branch／switch 目标必须落在指令起点，
并绑定 `111f` 的 pair-loop 字节窗口及 collider List 的 get_Item／get_Count
成员身份。它们仍不证明完整 CFG、求值栈或生成路径完备性。
Metadata 的异常区段读取支持 small 和 fat EH：分别验证 8 位／24 位区段长度、
12／24 字节 clause、RVA 四字节对齐、原始文件 backing、指令边界、
catch／filter／finally／fault 的 flags 与 extra，并支持混合串联。
只读解析当前程序集的 `111c` 得到 9 个 fat finally clause，`111f` 得到
4 个 small finally clause；这不证明 Dispose 分派、异常控制流或生命周期语义，
也尚未将这些区段纳入生产认证门。新增 fat 格式、长偏移／长度、混合串联、
截断及畸形输入回归包含 268／65548 字节合法 fat 区段，后者覆盖 24 位
长度的第三个字节。另将 `111f` 与 `1122` 的采样入口、步长字段读取、浮点递增及
`ble` 回边绑定到显式字节门，持久 JSON 同步记录对应偏移和字节；
`1122` 另绑定完整方法体 SHA256，原有证据字段保持不变。
两种布局的逐字节 mutation 与截断回归检查该门；这不证明步长为正且有限、浮点递增
必然取得进展、循环体效应或步长更新时序，不能作为生命周期完成证据。
`111b` 现另绑定完整方法体 SHA256 与偏移 873–941 的步长表达式尾部字节，
条件表达式为 `Min(0.125, 0.25 / Pow(2, Round(Log(local2, 2))))` 再转 binary32。
即使假定正 Int32 输入、数学函数返回预期指数 `k`、nearest-even binary32 加法且
字段不变，正有限步长也不足以证明循环前进：步长为 `2^-max(3,k+2)`，当
`k >= 22` 时，在 `t = 2^(22-k) <= 1` 处加半个 ULP 会舍回原值。
例如 `local2 = 2^30`、步长 `2^-32`、`t = 2^-8`；只有条件化的
`k <= 21` 足以保证该等距采样过程经过 1 时仍能前进。回归独立检查
`k = 22..31` 的舍入停滞与 `k = 0..21` 在 1 处的前进，以及尾部逐字节
mutation、截断和 audit 完整体哈希门。此反例不证明实际输入可达、运行时 Math
精度、运行时成员解析、完整 CFG、异常或字段生命周期；这些仍是离线证明缺口。
外围 audit 已严格绑定 System.Math 的 Log、Round、Pow、double Min 和 Int32 Max
成员名称、签名、父类型及程序集身份；metadata 身份不证明运行时解析或精度。
`111b` 的显式正常控制流另记录 local2 在 623／805 的写入及 797／882 的读取，
623 初始化可达且支配这两次正常路径读取，没有其他 local2 地址访问。
该图将 leave 直接连接目标、绕过 finally，不构成异常路径支配、值域或终止证明。
新增回归覆盖 switch 默认落空与分支、回环、leave 绕过处理器、全部 local 编码和非法初始化点。
`111b` 另严格绑定单一区段内六条有序 finally 的 flags、try／handler 偏移与长度及 extra；
36 个 clause 字段变异和 5 类区段结构变异均被拒绝。另有受限、上下文敏感的
finally CFG：正常 leave 按内到外执行处理器，异常展开替换旧 continuation；分支、
fallthrough、保护区边界及 endfinally 位置均检查，未知结构拒绝。在此模型下，623
初始化支配 local2 读取（真实体 309 个状态，合成 fixture 966 个状态）。这不是完整
CLI 栈／类型验证、callee 效应、值域、生命周期或终止证明。
步长字段 `04001138` 的直接 token 引用另扫描全部 2897 个非零 RVA 方法，2897 个
完整解码、无失败，精确绑定六处引用（构造器和 111b 各一写入，111f／1122 各两读取）。
额外写入、取地址、缺失引用和不完整 IL 均拒绝；扫描包含不可达尾部，不将静态清单
误称为可达性、alias、反射、外部代码或生命周期证明。合成 fixture 为 61／61 方法。
距离方法 `0xcb4` 与 `get_ImpliedS` `0xca8` 绑定完整 IL、签名、owner 区间、
坐标字段、Abs 成员及精确 implementation／method flags；四种 flags 单比特变异均拒绝。
在额外前提 `|Q|, |R| <= B = 268435455` 下，`|S| <= 2B`，三项绝对差之和
至多 `8B <= Int32.MaxValue`，各中间量可表示且分子为偶数，因此等于数学 hex 距离。
调用者满足该域尚未证明。unchecked 反例 `(2^30, 0)` 到 `(0, 0)` 的数学距离为
`2^30`，Int32 累加溢出后却返回 `-2^30`；标准 mscorlib 合约下 Abs(Int32.MinValue)
另会抛异常。这些是本地算术模型，不是 Mono 执行证明，也不证明游戏可达性。
步长字段另绑定 private 实例 R4 metadata、字段与构造器共同 owner、managed 实例
`.ctor` 身份及 `this / ldc.r4 1 / stfld` 精确窗口。EH-free 正常控制流检查初始化
store 可达并支配所有可达正常返回，拒绝跳入值窗口中部、绕过 store、未知前缀及
不支持的控制流。该检查不证明调用副作用、publication、reentrancy、alias、反射、
外部写入或完整字段生命周期；不能据此开放动态认证。
当前离线 collider audit 与持久 JSON 保持一致，状态为 `partial-byte-evidence-only`，
认证资格仍为 false。receiver 门另外绑定四个方法的共同 owner、managed 实例无参
metadata，禁止参数地址逃逸或赋值，检查六个 `ldarg.0` 窗口的两端指令边界及内部
branch／EH handler／filter 入口；只证明单次入口 this 身份，不证明跨调用对象身份、
stack、callee 副作用或完整生命周期。新增 metadata 和跨窗口边界反例均已通过。
本轮首次完整回归运行 134 项，两个新增边界 subtest 因测试构造错误失败（19.229 秒）；
改用足够长的 NOP 合成 body 后，完整 EvidenceReader unittest **134／134 通过，
17.821 秒**。上轮 134／134、16.202 秒为独立历史运行，不合并计数。
本轮审计及测试文件均无语法错误；两文件 Pylance 诊断仅有 severity 4 的未使用变量
提示，无错误／警告。Pylance references 已核实 receiver helper 接入正式 audit。
这仅关闭 receiver 局部门及其回归验证，不关闭阶段2的生命周期、空间等价或成本义务。

### 工作包 A：阶段2有限义务矩阵（2026-11-04）

此表是有限验收账本，不把字节绑定、局部语义或条件推断升级为全调用域结论。
证据均限于 SHA256 `ee4d1740609cbc09b5eb52244acc63da32f9e3cc280309049ceb7b708d20e1c5`
的离线程序集。下面的方法号为 MethodDef，位置为 IL 字节偏移。

| 命题 | 证据 | 缺口 | 依赖 | 关闭标准 | 状态 |
|---|---|---|---|---|---|
| 04001138 的直接字段引用恰为两写四读 | JSON `step_field_references`，2897 个有 RVA 方法完整扫描 | 不覆盖 alias、反射、外部修改 | 固定程序集及解码器 | 完整直接 token 清单匹配 | 已关闭（仅字节清单） |
| 110a 正常返回前在入口 this 上初始化为 R4 1 | `step_field_initialization`、`step_constructor_normal_flow`、`step_entry_receivers`；110a@55..66 | 不证构造期间未逸出或后续值稳定 | 构造器 metadata、局部 CFG | 初始化 store 支配正常返回且 receiver 绑定 | 已关闭（局部） |
| 111b 的局部更新及读取入口绑定 | 既有 store dominance、finally CFG、receiver 六窗口 | callee effect、值域、执行终止不由 CFG 给出 | 既有局部门 | store/return 和窗口身份各自满足既定门 | 已关闭（局部，不是生命周期） |
| 1116 同一入口对象先更新后主采样 | 新 JSON `step_lifecycle.primary_order`；1116@51 ldarg.0、@52 call111b、@57 ldarg.0、@58 call111f | 不证到达51、111b正常返回或无重入 | 六方法完整 IL、managed 实例签名、无 EH | 正常到达51且更新正常返回时，两次接收者相同、顺序固定 | 已关闭（条件性调用顺序） |
| 1117 次采样使用其入口 this | `step_lifecycle.secondary_entry`；1117@31..37 调用1122 | 本入口无直接111b；此前更新及构造来源未知 | 1117完整 IL及metadata | 绑定入口接收者，不推断此前更新 | 已关闭（单入口）；此前更新阻塞 |
| 工厂新对象到提取结果保持身份 | `step_lifecycle.identity`；1114完整IL/header/locals/三个finally、generic wrapper六方法及九个唯一MemberRef绑定 | 不证逸出、纯度、终止、跨getter稳定性 | managed CLI局部/值类型语义；正常经过newobj56及提取59；无unsafe/外部栈改写 | 实际转换/构造保存原引用、提取读取payload；factory local0单store且不取址 | 已关闭（条件性引用身份，仅到08b4@59结果） |
| 111f类别1/2正常局部生成计数与来源 | `category12_local_generation`；547..784双索引循环及635/696返回事件、656/748插入点 | 缓存委托、Where/ToArray、callee effects及实体身份未证 | 下文列出的正常返回/有限数组/无外部修改前提 | 每候选出现位置1项category1，每bf7索引1项category2；位置事件不合并 | 已关闭（条件性编号局部规则）；实际游戏计数阻塞 |
| 候选发布字段跨采样稳定 | `step_lifecycle.publication_candidate`；08b6@2为040004d8唯一直接写入，08b5@1唯一直接读取；08b4@64为setter唯一直接入口；下文「A：getter间最小effect域」记录08bd及1117一层callee | 正常路径最早未闭合根为1117@26→111c；111c@0 newobj0a0004d6、@7 call110c及后续调用/回调effects未证；1122、1123亦有未证出边 | wrapper身份；固定程序集；同一owner的调用/异常清理/重入非干扰及外部写入假设 | 在明确封闭域内证明所有可能执行出边不写该owner的040004d8，或证明返回引用等价；直接token清单不替代effects | 阻塞（一层边界已定位；不扩大追踪） |
| 全部入口获得已初始化且已更新对象 | 主/次入口和候选发布链已定位 | 另有0c7e@2094、1253@6144调用1114；128a@387/@484调用1117、@626调用1116；1290@50、1291@47调用1116，未追溯来源 | 前两行未证身份/稳定性及完整入口域 | 有限调用域与各入口前置条件成立 | 阻塞；不启动开放式全程序追踪 |
| 步长对认证输入域保证数值/进度 | 既有表达式与binary32停滞反例；JSON局部算术证据 | local2域、Math解析、浮点环境与采样误差界未闭合 | 生命周期、C数值规则 | 明确输入域及误差/进度证明，外部依赖逐条列出 | 阻塞（C；运行时解析属外部假设） |
| B：有限实体/配对入口账本 | 下文「B：有限实体/配对覆盖账本」；既有JSON完整离线重审计相等；111f五个直接Add、1122有限入口及0..3局部pair表 | 账本仅登记候选，不证明类别到原子/底座/wheel/抓手的映射、生成/移除完整性 | 固定程序集及既有证据工具；不依赖A稳定性已关闭 | 有限入口、证据分级、对应实现、缺口和最小反例需求逐项登记 | 已完成（准备包；不是B语义关闭） |
| B：实体语义、生成/排除覆盖完整 | 上述账本及既有ordinary oracle有限fixtures | 类别映射、上游集合成员资格、重复/遗漏、生成/移除时序及排除正当性均未闭合 | A身份/时序；下一有界实体子命题 | 每类正反例与规则来源、明确遗漏处理；未知类不认证 | 阻塞（仅完成首个准备包） |
| C：空间运动与四指标等价 | 既有阶段2局部字节证据 | 误差、面积及指标口径未闭合；本包不推进 | A/B及C数值规则 | 对应边界反例与规则证明 | 阻塞（C，未执行） |
| 阶段2资格与成本通过 | 持久JSON仍partial、certification_eligible=false | 无完整语义门及双方Passed有效成本配对 | A–C与D | 语义/成本分别验收，不计Unavailable收益 | 阻塞（D，未执行） |

**发现与边界。** 新 `collider_step_lifecycle` 仅绑定1116、1117、08b4、08b5、08b6、08bd
六方法完整IL、owner、签名、flags和无EH；1114只绑定metadata及两个精确窗口，
排除窗口内部branch/handler/filter入口，不声称证明整个工厂。08b4把工厂结果存入
wrapper局部变量，调用 `0a000286` 提取后才交给setter；`0a0005ac` 的名称即便为
op_Implicit，也不能推出保持身份。这是**最小离线未证边**，不是“必须实机”。
解析wrapper实现与局部数据流可作为下一次有界任务；本包不继续追踪。
跨getter稳定性需要先限定callee/alias域，否则停止，不泛化为CLI验证器。
并发、反射/外部修改、运行时解析另列为外部执行假设；特定实机fixture只能观察或证伪，
不能关闭全输入命题。本轮未启动游戏，未验证任何实际执行事实。

**本包验证。** 新增两个定向unittest（正例及六方法receiver/token/owner/flags/signature/EH、
工厂窗口/分支入口和发布字段变异），并适配完整持久快照对比：合成有RVA方法由61增至68，
生产仍2897；新字段参与完整相等比较，不剔除生命周期证据。
命令为 `/usr/bin/python3 tests/functional_test/GameProductionEvidence.test.py -k step_lifecycle`
及 `-k persisted_current_collider`；分别2/2及1/1通过。首次快照测试因旧计数61失败，
更新为实际68后通过。JSON由既有 `--audit-current-colliders` 离线重生成。
历史134项运行不算本包新增验证；本包未重跑全应用或改变profile/预算。
本轮两Python文件经 `ast.parse` 均通过语法检查；Pylance syntax/diagnostic 工具均因
`http://localhost:41131/stream` transport初始化失败，故未取得本轮Pylance诊断，
不援引历史诊断作为通过结果。上述四个目标文件在既有工作树均为未跟踪文件；
本包仅在其现有内容上追加/定向修改，未覆盖或还原其他既有更改。

当前版本网格资产另存于 `tests/functional_test/CurrentGridAssetEvidence.json`，
不替换历史 `MainGridAssetEvidence.json`，且 `certification_eligible` 保持 false。
`GameProductionEvidence.py --audit-current-grid-assets` 绑定当前程序集、字符串资源、
loader／flip／premultiply 方法及四张 82×71 PNG 的 SHA256；检查唯一的
`IMG_Load`／`SDL2_image.dll` 导入身份以及 `atom.lighting` 条件选择器。
路径为空、绝对路径、父目录逃逸、同名 PSD 存在、PNG 头／尺寸／哈希变化均拒绝。
这不是通用 PNG 校验器，也不证明 native 解码实现：仍显式依赖
`IMG_Load` 与 `SDL_ConvertSurfaceFormat` 保持 IHDR 尺寸的外部假设。
managed 尺寸传递链现已增加六个 leaf 和六个 wiring 方法的完整 IL 门控，
以及 15 个字段／6 个方法的 owner、flags、signature 和方法 implementation flags 检查；
这些仅绑定声明身份和条件 managed 数据流，不证明运行时解析、分派、外部构造器或完整初始化。
flip／premultiply 另由有限 byte-array IL 解释器检查：36 个含 padding 的行翻转输入，
以及 alpha／channel 的全部 65,536 组合。Array.Copy、尺寸 getter 和紧密四字节布局
仍是显式模型假设；不是 native 像素布局、任意尺寸或全调用栈的证明。
新增 19 类篡改拒绝、两个真实编码字符串记录及截断拒绝、current／历史 CLI
路由与 Content 根路径、外来程序集拒绝和持久证据版本隔离回归，均不需要安装游戏。
最新 focused CTest 为 **1／1**，内部 unittest **73／73，3.872 秒**，
CTest 总计 **4.03 秒，exit 0**。真实程序集仅只读解析，资产证据已重新生成并回读一致，
`certification_eligible` 仍为 false。像素回归还覆盖逐字节变异、截断、追加、非法分支、
未初始化局部变量、栈错误、未知调用／字段、数组越界、除零和解释步数耗尽。
这些回归证明读取器的检查与版本隔离，不升级游戏等价性或动态认证资格。

初始化离线 inventory 另绑定查询方法 `06000ad1` 的声明、67 字节 IL、locals、
泛型参数及 small finally section；选定引用身份覆盖 13 个 MemberRef、8 个 TypeRef、
6 个 TypeSpec、2 个 AssemblyRef 和 1 个 MethodSpec，并持久化至
`tests/functional_test/CurrentInitializationInventory.json`。独立 synthetic fixture
检查引用各列变异，查询回归检查 body/header/blob 的逐字节变异、截断及追加，
以及 owner、重复泛型参数和 finally clause 各字段变异。它们不是完整引用闭包，
不证明本地 Maybe 转换、库行为、别名写入、初始化时序或终止性；认证资格仍为 false。

阶段 2 面积审查新增一个可复现的**连续圆弧覆盖反例**：原子从轴坐标
`(2,7)` 绕原点旋转 60°，当前 cadence 为 32 段，考察格心 `(-7,10)`。
该格心至 double 欧式圆弧的最短距离约为 `57.568804`，小于半径 `58`；
但至全部 31 个内部采样点的最短距离约为 `58.503522`，大于当前
`58 + 0.321808` 的采样包围。起终点也不是该格。因此即使单个采样点的
边界包围正确，仍不能推导连续轨迹覆盖。新增解析回归验证距离、采样遗漏及
动态认证门；当前 enabled 配置面积回归 **15／15，0.39 秒，exit 0**。
这不是游戏面积规则反例：游戏可能使用离散 raster 时相，尚未证明该规则应按
连续圆弧计格；不能直接扩大半径或改变名义面积来冒充语义修复。

阶段 2 增加 `CheckBoundedSeparation` 作为独立的误差组合接口：调用者必须提供
覆盖闭时间段（含端点）的两物体欧式位置误差界，内核对
`minimumDistance + errorA + errorB` 逐步向上舍入，再向 double 上方取界，
仅当理想轨迹在整个区间严格分离时证明实际轨迹分离。非零误差时，膨胀半径内
的碰撞只返回 `Uncertain`，不误报实际碰撞；零误差完全复用原接口。
非法误差、不支持的算术环境、膨胀阈值超过既有范围或证明预算耗尽均拒绝认证。
该接口尚未接入动态认证，也不产生或证明游戏／reference 的误差界。
新增四组回归覆盖双物体误差、重叠带、圆弧、交换对称、预算、非法／极大误差、
subnormal 边界和非默认舍入；连续碰撞定向 CTest **22／22，0.46 秒，exit 0**。

阶段 2 新增诊断接口 `BasePositionErrorBound`，只包围 `BaseAt` 相对于精确
82×71 基座轨迹的位置误差，不接入认证。其目标量对所有 binary32
`t ∈ [0,1]` 同时成立；Track 的理想逐轴位置是 `B - T*(1-t)`，其余机制是 `B`。
坐标仍限制在 ±2²⁰，整数／半整数映射 `82*(u+v/2), 71*v` 在 binary64
中精确。接口另要求 binary64 radix 2、53 位有效位和 `FLT_EVAL_METHOD == 0`，
并复用 nearest、非 fast-math、无 DAZ/FTZ 及字段合法性门控。

令映射后的 binary32 坐标为 `b, d`，`u=2^-24`、`η=float denorm_min`。
`q=fl(1-t)` 位于 `[0,1]`，且 `|q-(1-t)| ≤ u`；
`p=fl(d*q)` 满足 `|p| ≤ |d|`（端点 `d` 可表示且 nearest 舍入单调），
其乘法误差不超过 `u*|d|+η`。最终减法误差不超过
`u*(|b|+|d|)+η`。因此逐轴总误差由
`|B-b| + |T-d| + u*(|b|+3*|d|) + 3η` 包围。
允许乘减融合时，融合舍入直接以 `|b|+|d|` 为界，仍被同一式子覆盖。
固定基座仅需映射误差。映射差是小整数／半整数，binary64 中精确；
非负求和和乘法每步用 `nextafter(+∞)` 上取界，最后两轴 L1 界支配欧式界，
不依赖 `sqrt` 的正确舍入。极值、subnormal 进度、非法字段及浮点环境测试
仅是回归；全进度主张依据上述运算界，不依据有限采样。
这个界**不覆盖 AtomAt、三角函数、活塞归一化、游戏轨迹、动态面积或碰撞认证**。

`TrackAtomPositionErrorBound` 另提供更窄的全原子诊断界：仅允许合法、
非活塞、零底座角的 Track。其精确目标是 `B+G+O-T*(1-t)`，其中
`G` 为 grabberOffset 的 82×71 映射，`O` 为 atom 与 absoluteGrabPosition
之差的映射；不假定这些字段满足额外布局关系。差坐标可达 ±2²¹。
接口复用基座界的环境与字段门控，并通过 volatile 正负零实参检查
`cos(±0)=1, sin(±0)=0`。该检查是当前 host libm 零角路径的前提，
不声称任意角 libm 的正确舍入，也不改变 AtomAt 的实现。

令 `S=|B|₁+|T|₁+|G|₁+|O|₁`。包括差坐标在内，整数转 float 精确；
`u+v/2` 的半整数分子至多 `3*2²¹ < 2²⁴`，因此乘 `.5` 和前置加法
也精确，即使相消也不会丢失误差项。ToXY 仅最后乘 82／71 可能舍入，
故可以使用映射后坐标的绝对值，而不必用相消前的坐标绝对值。
零角乘以 1 或 0 及其加减精确，AtomAt 在此域逐轴约简为
`fl(fl(fl(b-fl(d*fl(1-t)))+g)+o)`，小写为 ToXY 的舍入值。
映射后的 B、G、O 到输出分别至多经过 4、3、2 次非精确舍入；
T 项包含映射、`1-t`、乘法和三个加减，至多 6 次。
将表达式展开为这四个映射输入项，nearest 模型
`fl(x)=x*(1+δ)+ε, |δ|≤u, |ε|≤η` 给出相对项
`γ16*S`，`γ16=16u/(1-16u)<32u`。坐标域保证无溢出。
ToXY 的非零结果为绝对值至少 1 的整数，不产生下溢；`1-t` 对 binary32
`t∈[0,1]` 不是零就是至少 `2^-24`，也不产生下溢。
只需计入两轴各一次乘法与三次加减的绝对项（共 8 项），各项之后
至多经过三个舍入因子，故其和不超过 `8*(1+u)^3*η < 32η`。
因此返回上取界 `32u*S+32η`，用 L1 支配欧式距离。
S 是此坐标域内可精确表示的非负整数，binary64 求和精确；
最终乘加用 nextafter 上包围。允许收缩时，非平凡融合只可能将
`b-d*q` 的乘减合并：其模型为 `(b-d*q)*(1+δ)+ε`，展开后的
B 与 T 系数仍分别是已有舍入因子的子积，路径长度不增加，且少一个
独立绝对误差项。其余可融合乘法的因子均为 0 或 1，数值精确，
与后继加减融合不会增加误差项。因此同一 `32u*S+32η` 界覆盖这两种路径；
此论证不允许 fast-math 重结合或超额精度求值。
极值、双倍差坐标、抵消和代表进度回归不能替代上述全区间推导。
新增独立字段回归遍历五个 Hex 字段各五种极值／抵消位置（3125 组），
每组检查五个精确二进制进度（15625 个位置）；目标使用 long double
直接计算未舍入表达式，不复用被测 ToXY。字段越界回归覆盖全部五个
Hex 字段的两轴正负边界外输入，以及非法 rotation／pistonExtension；
DAZ／FTZ 拒绝后另检查清除两位可恢复成功。
此前 ReferenceMotion 定向回归为 18／18（0.56 秒）。随后新增
`TrackBoundsComposeWithContinuousSeparation`，把双方 Track 位置误差同时
传入连续分离：覆盖远离轨迹、原本有间隙但误差膨胀后不可认证的轨迹，
以及相向并在末端相交的轨迹。膨胀内核不把可能接触升级为真实碰撞。
最近串行 `cmake --build build --target OpusOptimaTests -j2` 成功，
`ctest --test-dir build --output-on-failure -R '^(ReferenceMotion|ContinuousCollision)\.'`
43／43 通过（1.16 秒，包含 x87 single／double 精度模式，以及三种非 nearest 舍入模式下三个入口的拒绝和环境恢复检查）；这是诊断路径回归，不是生产语义或全量验收。
此接口未接入生产认证，**不关闭旋转／活塞、游戏映射或动态面积义务**。

### 连续分离内核的数学契约

以下结论只针对 `ContinuousCollision` 声明的理想实数轨迹，不针对游戏的
浮点插值、末端 snap 或离散 raster。输入 double 被视为精确实数；
`angle=0` 时为 `start+translation*t`，否则为
`center+R(angle*t)*(start-center)+translation*t`，`t∈[0,1]`。
碰撞表示距离严格小于阈值，分离证明采用严格大于；相切允许返回不确定。
零阈值不存在严格内含的碰撞，单独返回分离。

区间端点使用 long double，每个加、乘、除的结果向两侧分别移动一个
`nextafter`；负号精确，乘法取四个端点积，平方跨零时下界为零。
在 nearest、渐进下溢、无 fast-math 的 IEEE 二进制环境中，这些操作
包围精确实数结果。环境还要求 long double 的有效位及指数范围覆盖
输入 double，确保单点转换精确；不支持的环境拒绝认证。
另拒绝 `FLT_EVAL_METHOD != 0`，并以 volatile 的 `1+epsilon` 运算检查
运行期精度没有低于 long double 的声明精度。x87 控制字可在不改变
存储格式及 `fegetround()` 的情况下切到 24／53 位；一格 long double
外扩不足以覆盖这种舍入。glibc x86 回归分别设置这两种模式，验证
包围、普通分离及带误差分离均拒绝，恢复控制字后包围恢复成功。
该回归复现的是环境拒绝缺口，不是错误距离或错误分离的数值反例。
输入范围与深度限制使中间量保持有限，极小平方下溢也通过外扩覆盖，
不能据此证明虚假的严格间隙。此契约不授权会破坏区间运算的编译变换。

三角函数不依赖 host libm：用区间递推构造 sin 的 63 阶与 cos 的
62 阶多项式；后者等于 cos 的 63 阶 Taylor 多项式，因为奇次系数为零。
递推中的正整数除数精确可表示。对任一区间中的实数角 x，Taylor
余项绝对值不超过 `|x|^64/64!`，因为两函数的 64 阶导数绝对值不超过 1。
程序逐步向上计算 `M^64/64!`，M 为角区间的绝对值上界，并向多项式区间
两侧加入余项。区间依赖只会扩大结果，不改变包含性。

速度界取 `|angle|*|start-center|₁+|translation|₁`，由旋转保欧式长度及
L1 支配 L2 得到全时间 Lipschitz 界。`Enclose` 在可表示中点求区间位置，
以向上包围的两侧最大时间跨度乘速度界，沿两轴扩大；不要求舍入中点
恰好为数学中点。双方相对运动用两速度界之和。全区间粗筛以初始距离
严格大于阈值加总速度判分离；子区间则以中点距离严格大于阈值加可达
半径判整段分离。所有平方比较均使用距离下界与阈值上界。

线性特例的精确最近时刻是 `clamp(-dot(offset,velocity)/|velocity|²,0,1)`。
仅在速度平方下界为正时作区间除法；clamp 单调，故夹紧后的区间包含
真实最近时刻。该时间区间的距离下界严格大于阈值上界即可证明全段分离；
碰撞则另在一个真实候选时刻计算距离上界，严格内含才报告。
同速度的线性轨迹，以及同中心／角度／平移的刚体轨迹，距离恒定。
一般曲线的碰撞结论同样需要单个实际中点的距离上界严格小于阈值下界。
细分的两个闭区间覆盖父区间；只有全部区间均证明分离才返回分离。
预算耗尽、深度用完或中点不可再分都返回不确定，不作为无碰撞证据。

`CheckBoundedSeparation` 要求调用者的非负误差界在所有时刻（含端点）
成立。由三角不等式，理想轨迹间距大于 `distance+errorA+errorB` 即可
推出实际轨迹间距大于 distance；阈值的求和与转回 double 均向上包围。
膨胀阈值内的理想碰撞不能推出实际碰撞，因此降为不确定；双方误差都为
零时保留原内核语义。Track 组合回归只连接理想连续运动与 binary32
reference 位置误差，不证明游戏全连续轨迹误差，也不开放动态认证。

阶段 2 仍有离线义务：collider 插入完整性、游戏／reference 轨迹映射、
真实空间误差及七格覆盖、面积 raster 包围和四指标规则。
只有实际游戏近接触等观察明确属于实机工作；前述离线义务尚未关闭，
因此阶段 3–5 的资格扩展继续阻塞，动态认证保持 `Unavailable`。

### C 首包：连续数值／离散面积义务矩阵

本表取代“数值／面积整体待证”的笼统状态，历史测试数不作为本包验证。
“本地已证”只指给定输入和算术模型，不指游戏等价；A 生命周期与 B 实体映射保持阻塞，
B 的停止出口仍有效，不续追 ID、描述初始化、11f4 或 A effects。

| 命题／层次 | 已有证据 | 缺口、依赖及关闭标准 | 状态 |
|---|---|---|---|
| `BaseAt` 全 binary32 进度误差；受限 Track `AtomAt` 误差（本地连续数值） | 上述两个误差界推导，坐标 ±2²⁰；Track 限非 piston、零 baseRotation | 不能外推至实数进度或游戏轨迹；游戏对应另见映射行 | 本地已证，诊断接口未接认证 |
| 一般 rotation/piston 误差（本地数值） | `ReferenceMotion::AtomAt` 运算序及输入门 | 非零角 libm、角度舍入、piston sqrt/归一化的统一位置界未给出；须逐运算包围全域，不能用经验常数 | 离线可推进；平台 libm 另需资格 |
| 连续分离及误差组合（本地数学） | 上节 `ContinuousCollision` 区间／Taylor／Lipschitz 推导；输入坐标绝对值 ≤1e9、角度 ≤8 | 调用者须提供闭时间段含端点的真实误差与完整配对；不能从膨胀内碰撞推出真实碰撞 | 内核已证；生产接入阻塞 |
| cadence（离散时间） | `InteriorSampleCount` 对已接受 dyadic 步长精确计数；host log/pow 决定分档 | 尚缺跨运行库的分档边界对应及游戏进度／采样时相映射；须绑定实际取样生命周期 A | 局部计数已证；分档与映射待证 |
| 七格候选完整性（离散空间） | `BoardAreaCells` 固定中心加六邻居，严格阈值 44/49/58；下述 C 第二包给出一般二维全接受域排除界与对抗回归 | 结论针对同一 float 谓词；不等于精确圆盘，逐步 binary32／sqrt 等额外执行前提见证明，运行期门未全部检查 | **一般二维本地条件性命题关闭；非游戏认证** |
| `IndependentMetrics::RasterPoint`（离散空间） | C 第三包独立推导逆投影矩形对实际 double 筛选的全接受域排除界；直接边界／拒绝回归 | 候选完整性不等于精确圆盘或最终面积正确；逐步 binary64 前提未由此接口运行门完整检查，详见下文 | **本地条件性全域命题关闭；非游戏认证** |
| 动态面积：同一采样点坐标包围 | C 第五包核对实际 `U=.05+(turns?.03*maximumRadius:0)+16*epsilon*(|C|₁+|P|₁+|D|₁)`；给出小坐标零角 Track 条件性包围及合法大坐标抵消反例 | `maximumRadius` 是同帧旋转 hex 半径，不是圆盘半径；通用公式不支配 ReferenceMotion 的独立字段尺度，旋转项仍无推导 | **受限本地点包围已证；全接受域命题被反例否定；未接认证** |
| 动态面积：采样间覆盖／游戏映射 | `EndMotions` 仅内部 dyadic 采样；既有 `(2,7)→(-7,10)` 连续圆弧遗漏反例 | 点包围不推出连续覆盖；游戏计格时相、reference→game 对应及浮点圆盘判定仍未闭合 | 连续覆盖推论已被既有反例否定；游戏离散语义待证 |
| 四指标定义 | `IndependentMetrics::Initialize/Finish`：规则表成本、展开后计数指令、格集合面积、调用者传入 cycles；静态来源已记录 | 各规则表与游戏定义、Replay 完成周期／生成移除时序须分别绑定；不能把四个本地整数叫游戏四指标已证 | 本地规则实现已有；生产映射依赖 A/B |
| reference／Replay → 生产实体及生命周期 | `StaticProductionVerifier::CertifiedMotions` 保留动态拒绝门；现有 A/B 表与停止出口 | 完整实体、配对排除、生成／移除和采样对象顺序未闭合；须有对应身份及生命周期证据 | A/B 阻塞，非本包工作 |
| 外部执行前提 | nearest、非 fast-math、渐进下溢等由本地门检查；连续内核另检查 long double 实际精度 | 游戏运行时 Math 与 host libm、实际浮点环境及 native 资产尺寸传递仍需分别资格；有限运行观察不证明全域 | 外部假设；不是所有本地缺口都需实机 |

**本包唯一关闭命题：水平轴有界七候选完整性。** 对满足现有算术环境门且
`FLT_EVAL_METHOD=0` 的逐步 binary32 运算，中心 `c=(x,0)`，`x` 为有限 binary32、
`|x|≤2²⁶`，半径限 `15/20/29`；目标集合是所有 `|u|,|v|≤2²⁰` 格点中，
按 `BoardAreaCells` 同一 float 坐标／距离筛选所得集合，不是精确欧式圆盘或游戏面积。
前提还包括正确舍入的 sqrt（本轮仅验证 host libm 的所测输入，不以回归证明该库全域正确舍入）。
证明候选不遗漏，不改筛选规则、不证明一般二维七格充分：

1. 写 `a=RN(x/82)`，`|a-x/82|≤1/32`（本域商的最大 float 间距为 1/16）。
   `v=0,w=-a`，两非零残差相等，严格 tie 分支不改 `u`，故 snapped 为 `(s,0)`，
   `|s-x/82|≤1/2+1/32=17/32`。本域 `|s|<818402`，中心及六邻居均在有效坐标域内。
2. 任意 `v≠0` 的格点，其 `|RN(71*v)|≥71`；平方、加非负平方、sqrt 和转 float
   的单调性保证算出距离至少 71，超过最大严格阈值 58。因此所有命中格都在 `v=0` 行。
3. 该行若 `|u-s|≥2`，精确横距至少 `82*(2-17/32)=120.4375`。
   全有效行域内 `RN(82*u)` 误差至多 4，减法舍入误差保守至多 8，故计算 `|dx|>100`；
   此处平方与 sqrt 无溢出／下溢，其正常数相对舍入界 `2^-24` 不可能降至 58。
   故命中格仅可能为 `(s-1,0),(s,0),(s+1,0)`，都在七候选内。
   这同时处理了合法域内七候选之外的任意远格，不靠有限扫描充当全域证明。

此前测试仅有 tie、格心与接触样例，没有这个明确子域的完备性推导。
新增 `ReferenceMotion.SevenCandidatesCoverHorizontalAxisWithinBoundedDomain` 只回归该命题：
在半径接触／snap tie `±24/33/38/41`、零、`±2²⁶` 及相邻 float 点，对三半径与扩大窗口比较。
扩大窗口用于回归遗漏，不是全域 oracle；全域排除由以上推导承担。
生产代码未改，`IndependentMetrics` 不使用此七格算法；不得将本结果推广至其动态面积。

**本包验证（非历史结果）。** `cmake --build build --target OpusOptimaTests -j2` exit 0，
仅重新编译本测试并链接。随后运行：

```sh
./build/OpusOptimaTests --gtest_filter='ReferenceMotion.SevenCandidatesCoverHorizontalAxisWithinBoundedDomain:ReferenceMotion.SnapUsesStrictResidualTieOrder:ReferenceMotion.BoardRasterUsesStrictContactAndReferenceOrder:ReferenceMotion.RasterRejectsUnsupportedInputsAndEnvironment'
```

结果 **4/4、0 ms、exit 0**。无构建／回归失败、无修复轮次；未运行全应用或全套测试。
下一有界建议：仅证明 `y≠0` 时理想 cube snap 残差域到最近被排除格的距离下界，
再单独登记 binary32 snap 误差裕量；若下界不足 58，先提供最小反例，不直接扩大候选或开放认证。
A/B 阻塞、动态拒绝、legacy-gate、profile 和 2/10/20 秒预算均不变；本包完成即停。

### C 第二包：一般二维七候选完整性（条件性全域证明）

**目标与执行前提。** 令 `L=2²⁰`，合法格集合 `H={(i,j): |i|,|j|≤L}`，
精确几何映射 `T(i,j)=(82i+41j,71j)`，实际逐步 float 映射记为 `Tf`。
对每个使现有 `BoardAreaCells(c,r,cells)` 成功的有限 binary32 中心 `c`，
`r∈{15,20,29}`，令 `s=SnapToHex(c)`，`N(s)` 为中心加实现中六偏移。
定义 `F(c,r)` 为 **H 中全部格**按实现同一 `Tf`、float 减／平方／加、
`float(sqrt(double(...))) < r+29` 命中的集合。本包证明 `F(c,r)⊆N(s)`，
因此实际输出恰为 F（顺序仍由实现决定）。不主张 F 等于精确欧式开圆盘。
域没有改为小坐标或只接受测试中的输入；Snap 成功但任一候选非法的输入不属于
BoardAreaCells 成功域，不能混淆两个接口的接受域。

以下 RN 为逐步 binary32 nearest-even、渐进下溢，无 contraction／重排／额外精度；
`FLT_EVAL_METHOD=0`，double 为 binary64，sqrt 对 binary64 正确舍入，转 float 正确舍入，
`std::round` 遵守半数远离零。这是本地**条件性**数学结论：现有运行门只检查
非 fast-math、IEEE float/double、float 精度、nearest 及 SSE FTZ/DAZ；未检查全部
编译器／sqrt 前提。当前 Debug 编译参数没有显式禁止 contraction，故本次执行通过
不能升级为所有构建已满足这些前提，更不是游戏运行库资格证明。

**先处理二维 snap，不借正六边形定理。** 设实现逆投影为 `q=(u,v,w)`，
精确逆投影为 `Q=(U,V,W)=(x/82-y/142,y/71,-x/82-y/142)`。
由接受门 `|u|,|v|≤L-1, |w|≤2L-2`，最后一步 `w=RN(-u-v)` 有
`ρ=u+v+w`、`|ρ|≤1/16`（精确和绝对值小于 2²¹，其最大半 ulp 为 1/16）。
初始整数 `n_i=round(q_i)`、真实残差 `e_i=q_i-n_i` 满足 `|e_i|≤1/2`；
残差减法本身精确（非零整数附近由 Sterbenz，round 到零时直接精确）。
因此整数 `S=n_u+n_v+n_w=ρ-Σe_i` 只能是 -1、0、1。
实现的 strict 比较恰选一个最大绝对残差轴（tie 顺序 u、v、w 与代码相同）；
将该轴整数改成其余两轴整数的负和，含默认分支隐式修正 w。
若 S=0，所有轴残差仍≤1/2。若 S=1，则 `Σe_i≤-15/16`：最大绝对残差
不可能为非负，否则另外两项各不小于它的负值，和至少 -1/2，矛盾。
故被选残差为 `-m`，且 `m≥(1-ρ)/3≥5/16`，修正后残差为 `1-m≤11/16`；
S=-1 对称。因此输出 cube 整数 `s*=(s.u,s.v,-s.u-s.v)` 满足
`max_i |q_i-s*_i|≤11/16`，包括所有二维 tie，不假定标准六边形的欧式最近点性质。

在理想精确逆投影下 `ρ=0`，同一论证把修正轴残差界收紧为 `2/3`；
七格外轴差至少 `4/3`，以下真实82/71泛函给出的理想欧氏排除距离为 `284/3`。
因此理想二维 snap 先有独立裕量，下面才将逆投影／坐标／筛选误差扣入；
不把标准正六边形最近点定理套到本格基。

**再纳入全域逆投影误差。** 写 `a=RN(x/82)`、`b=RN(RN(y/2)/71)`，
代码确为 `u=RN(a-b)`，不能换算成另一运算序。
由 v 的门及其舍入原像，`|y/71|≤L-1+1/32`，`|v-V|≤1/32`。
乘 1/2 除 subnormal 外精确，subnormal 误差至多半个最小 float；
故 `|b-y/142|<1/32` 且 `|b|≤L/2`。
再由 u 的舍入原像 `|a-b|≤L-1+1/32` 得 `|a|<3L/2`，
所以 `|a-x/82|≤1/16`，`|u-(a-b)|≤1/32`。
于是保守地 `|u-U|<1/8, |v-V|<1/16`，并由 ρ 得 `|w-W|<1/4`。
组合上一步得到统一 `max_i |Q_i-s*_i|<15/16`。
这些是对完整接受域的舍入原像界，不从有限 anchor 推断。

**82/71 几何排除界。** 任意 H 中但不在 N(s) 的整数格 h，其 cube 差
`h*-s*` 至少一轴绝对值≥2：三个整数和为零且各轴≤1的向量恰是零和六邻居。
该轴有 `|h*_i-Q_i|>17/16`。
三个精确线性泛函为 `X/82-Y/142, Y/71, -X/82-Y/142`，
其欧氏算子范数分别为 `sqrt(1/82²+1/142²),1/71,同前`，均≤1/71
（`71²/82²+1/4<1`）。故 `||T(h)-c||₂>71·17/16=75.4375`。
这使用真实非正六边形格基，排除了任意远格，不限于扫描窗口。

**实际 float 命中反向包围。** 全合法 H 上，`i+j/2` 精确（半整数、幅度≤3L/2），
两次乘法的绝对舍入误差各≤4：`|82i+41j|<2²⁷, |71j|<2²⁷`。
因此 `||Tf(h)-T(h)||₂≤8`（保守 L1 界）。若 float 距离 d<58，令
`dx=RN(Tf.x-x),dy=RN(Tf.y-y)`，`z=RN(RN(dx²)+RN(dy²))`。
平方／求和非负且全域无溢出；正常项相对误差≤ε=2⁻²⁴，subnormal 项
用最小 float 的绝对误差补偿。由正确舍入 sqrt 及转 float，d<58 推出
`sqrt(z)<58+2⁻¹⁷`，继而
`dx²+dy² ≤ z/(1-ε)² + 4·2⁻¹⁴⁹ < 59²`。
故两计算差都<59，其减法舍入原像误差每轴≤2⁻¹⁹；于是
`||Tf(h)-c||₂<60`，最终 `||T(h)-c||₂<68`，与 75.4375 排除界矛盾。
阈值44、49只会进一步缩小命中集合。此界有超过7的裕量，不将精确接触与
float 接触视为等价，也不依赖精确圆盘成员资格。

**必要对抗回归与本包实测。** 新增
`ReferenceMotion.SevenCandidatesCoverGeneralTwoDimensionalAdversaries`：13个 anchor
覆盖原点、混合符号、±L附近及大数抵消，11个二维偏移覆盖斜向 cube tie、
三个阈值附近的3:4方向和非正六边形位置；中心两轴分别取相邻 float，三半径，
共3861个成功调用。与 snap±4 的合法格窗口按同一 float 谓词比较；失败诊断打印
hexfloat 中心、半径、snap、漏格和距离。窗口仅用于找遗漏，以上全域不等式才是证明。

本包实际命令（没有借用首包4项结果）：
```sh
cmake --build build --target OpusOptimaTests -j2
./build/OpusOptimaTests --gtest_filter='ReferenceMotion.SevenCandidatesCoverGeneralTwoDimensionalAdversaries'
```
构建重新编译本测试并链接，exit 0；新测试 **1/1，7 ms，exit 0**，3861调用全部接受且无遗漏。
另用 Python 打印范数 `hypot(1/82,1/142)=0.01408241222450679 < 1/71=0.014084507042253521`
及计数3861，仅作算术核对，不作证明。未运行全套测试、游戏或存档操作。
生产实现与接受域未改；当前 BoardAreaCells 调用仅本地单测及 functional reference
差分，后者 oracle 也用七候选，不充当本证明；IndependentMetrics 不复用该结论。
无遗漏反例、无行为修复或认证放行；A/B、动态面积、legacy/profile及预算不变，本包止于此命题。

### C 第三包：RasterPoint 逆投影矩形候选完整性

**唯一命题及接受域。** 此处不使用七候选结论。令 `L=2²⁰`，合法格为
`H={ (q,r): |q|,|r|≤L }`，`R=RN(radius+uncertainty)`。对通过 RasterPoint
数值、坐标和百万候选门的输入，矩形包含 H 中所有按该实现 double 距离谓词
可能进入任一写入分支的格。不把异常返回解释为成功的空结果；集合容量异常可能
发生在部分写入后，生产调用方 `EndMotions` 的 `InvalidateOnFailure` 会使收集失效。
已有集合的 contains 条件只能抑制写入，不扩大距离集合。本命题不是最终集合／面积
正确性，不证明 uncertainty 常数、动态采样时相或游戏语义；A/B 仍阻塞。

**算术前提（原充分条件；必要性与本地资格见紧接本证明后的核对包）。** 以下 RN 为逐步 IEEE binary64 nearest-even，渐进下溢、正确舍入
基本运算及 floor/ceil，无重排、contraction 或额外精度；int 至少32位。
接口本身未完整检查这些环境条件，故为**条件性全接受域证明**，不是无条件平台资格。
当前 Debug 参数为 `-g -std=gnu++20` 及警告项，无 fast-math，也未显式禁止 contraction；
一次运行不能资格化所有构建。以下宽松界有意远离最后一位，不依赖扫描。

1. **先从接受门反推有限大小，不能先假设中心小。** 写
   `A=RN(y/71)`、`B=RN(RN(x/82)-A/2)`，`bR=floor(A)`、`bQ=floor(B)`。
   半径非负、有限且 R≤L；extent 至少2，所以坐标门给出 `|A|,|B|<L`。
   除以2除次正规边界外精确（边界误差至多η，亦被下述界吸收）；由舍入原像界反推 `|RN(x/82)|<2L`，从而
   `|x|<2²⁸, |y|<2²⁷`。这也覆盖负数、抵消、signed zero 和极小数。
   标准绝对／相对误差界给出
   `|A-y/71|<10⁻⁸`、`|B-(x/82-y/142)|<10⁻⁸`。
   例如各商与逆投影差绝对值均小于2²¹，单步半 ulp 不超过2⁻³²，
   三步误差之和仍远小于10⁻⁸；渐进下溢的绝对误差更小。
2. **实际浮点命中推出稍膨胀的几何界，而非宣称两者等价。** 对任意 H 中的格，
   `T=(82q+41r,71r)` 的半整数及整数中间值均精确可表示，故格点映射无舍入。
   精确坐标差绝对值小于2²⁹，每轴减法误差≤2⁻²⁵。
   令计算差为 d、计算平方和为 S，`u=2⁻⁵³, η=2⁻¹⁰⁷⁴`。
   非负平方／加法舍入满足
   `S≥(1-u)²||d||²-3η`，而 `RN(R*R)≤(1+u)R²+η`。
   所以 `S≤RN(R*R)` 蕴含 `||d||≤R+10⁻⁶`（包括 R=0 的下溢情形），
   再加两轴减法误差有 `||T-(x,y)||≤R+10⁻⁵`。
   内分支也受此界约束：当 uncertainty<radius 时，舍入的非负
   `radius-uncertainty≤R`，平方单调；严格内命中必是外圆盘谓词命中。
   因此不遗漏严格内边界、非严格外接触或 radius=uncertainty 的分支差异。
3. **逆投影与整数余量。** 精确几何直接给出
   `|r-y/71|≤(R+10⁻⁵)/71` 和
   `|q-(x/82-y/142)|≤(R+10⁻⁵)c`，其中 `c=1/82+1/142<.02`。
   这只是保守 L1 投影，不需正六边形或最近格定理。
   extent 计算中常数相加、乘法和除法在 R≤L 时的绝对误差均小于10⁻⁸；
   正确 ceil 因而保证 `extentR≥R/71-10⁻⁸`、`extentQ≥Rc-10⁻⁸`。
   结合前两步，所有命中满足 `|r-A|<extentR+10⁻⁵`、
   `|q-B|<extentQ+10⁻⁵`。
   反之对任一整数坐标 z，若在 `[floor(a)-e,floor(a)+e+1]` 外，
   则 `|z-a|≥e+1`（低侧实际严格大于），矛盾。这一整格余量同时吸收
   floor 的负数方向、ceil 临界舍入及大坐标误差，涵盖所有合法远格，不靠有限窗口。
4. **溢出及资源拒绝。** R≤L 使两个 extent<22000，转换 int 安全；坐标门使
   端点均在 [-L,L]，循环末端自增、宽度及64位宽度乘积均不溢出。
   全合法域映射与上述差／平方远小于 binary64 溢出界；NaN/inf、负半径或负
   uncertainty、和溢出、逆投影／坐标越界被拒绝，非成功枚举。
   百万候选门在循环前拒绝超量矩形；集合容量门并非缩减候选再成功返回。
   资源拒绝不构成遗漏反例，也不被本证明改成成功。

**缺口与必要回归。** 原测试仅经 EndMotions 检查大坐标 uncertainty 增加工作量，
没有直接测试 floor/ceil 外围、接触分支或前置拒绝。本包在 header 只加 friend 测试入口，
未改生产算法。`RasterRectangleCoversFloatingFilterBoundaryAdversaries` 对9组原点、
负数、接触、混合符号及±L附近抵消中心的双向 nextafter，共18次调用，与合法±32窗口
按同序 double 谓词比较 area/uncertain 两集合；另断言精确外接触只进 uncertain。
窗口只是回归，不是全域证明。`RasterRectangleRejectsNumericCoordinateAndResourceOverflow`
覆盖非有限／负输入、有限和溢出、R=L及越界、巨大中心、两轴正负坐标拒绝，
并验证边缘可接受36候选、R=30000接受而31000因候选超限拒绝；不扩大资源预算。

本包实际验证：
```sh
cmake --build build --target OpusOptimaTests -j2
./build/OpusOptimaTests --gtest_filter='IndependentMetrics.RasterRectangle*:IndependentMetrics.WorkCountersMeasureRasterWorkAndResetWithoutChangingArea:IndependentMetrics.LargeCoordinateUncertaintyExpandsRasterCandidateCoverage'
```
最终构建 exit 0；本轮 **4/4、808 ms、exit 0**，其中新增2项。首次新增2项也通过
（785 ms）；补充有限加法溢出及R=L资源拒绝后重新构建并执行上述4项。
无失败／修复轮次；首次构建有既有 StaticProductionVerifier 缺成员初始化警告，未修改无关代码。
无生产行为、接受域或profile变化，故未触发语义变更的冻结2254对；未运行全套、游戏或存档。
候选完整性条件性关闭；精确圆盘等价、最终面积正确性和运行环境资格未关闭。
下一有界建议仅审计 RasterPoint 浮点环境前提的实际构建／运行门覆盖，不重开七候选，
不扩展动态时相、四指标、A/B、legacy-gate、profile或2/10/20预算。本包到此停止。

### C 第四包：RasterPoint 算术前提资格核对（2026-11-04）

**结论与停止点。** 原证明的逐步 RN 条件在本机所查两个 Debug 配置及新建探针进程中
满足；不是生产调用线程的运行时保证，也不是跨平台资格。更重要的是，当前认证路径
不执行 RasterPoint：`StaticProductionVerifier.cpp:840–886` 的 `CertifiedMotions`
先拒绝任何动态原子／臂，再传入静止观察；`ObserveAtomMotion:261–280` 仅动态时
AddSweep，`ObserveArmMotion:283–313` 仅底座移动时 AddSweep；BeginMotions 清空 sweeps，
所以 EndMotions 的 sweep 循环为空。worker `IndependentWorkerMain.cpp:4–6` 进入
VerifyIndependentFiles 并不因此获得其他诊断 API 的算术门。直接 IndependentMetrics
动态调用及 friend 单测可达 RasterPoint，但它们不检查环境；不能把
ReferenceMotion／ContinuousCollision 的局部门误说成此处的支配门。本包不开放动态认证。

**先减前提，再评资格。** 以下放宽只证明矩形包含“同一实现浮点谓词可能写入”的格，
不保持逐位结果／集合身份，也不证明精确圆盘等价。令 `v=2⁻⁵²`、`δ=2⁻¹⁰²²`，
用弱于 RN 的逐运算界 `|fl(z)-z|≤v|z|+δ`，要求有限值语义、单调基本运算及
不低于 binary64 的有效精度／指数范围；固定标准舍入方向均满足此界。

- nearest-even **不是此命题必要条件**。在原大小反推中以 v 替换 u，
  `|A-y/71|`、`|B-(x/82-y/142)|` 仍各 `<10⁻⁸`：大小 `<2²¹` 的每步误差
  `<2⁻³¹`，三步加传播 `<2⁻²⁹`。每轴差 `<2²⁹` 的舍入误差 `<2⁻²³`。
  非负平方和下界改为 `(1-v)²||d||²-3δ`，阈值上界 `(1+v)R²+δ`；
  比例平方根相对增量 `<4v`，`4vL<10⁻⁹`，下溢项贡献 `<4√δ<10⁻¹⁵⁰`。
  因此依旧可取 `||d||≤R+10⁻⁶`，加两轴差误差仍 `<R+10⁻⁵`。
  extent 中不足十次常数／乘除舍入的总绝对误差 `<10vL<3×10⁻⁹<10⁻⁸`。
  全部仍被最后的一整格余量吸收。无需修改当前 rounding 状态。
- 渐进下溢 **不是必要条件**。对于本段这些有界、无 tiny 分母的运算，FTZ 每次
  清零的绝对误差 `<δ`，DAZ 把每个次正规输入改零的误差 `<δ`；把上式的
  `3δ` 保守替换为 `32δ` 即可，`√(32δ)<10⁻¹⁵⁰`。DAZ 对中心／半径的
  输入替换也仅贡献常数个 δ；half 下溢不再“精确”，但已被该界覆盖。
  这不宣称 FTZ/DAZ 对其他算法或当前集合结果无影响。
- “绝无额外精度／contraction” **也不是必要条件**，但不等于任意实现均许可。
  中间精度至少53位、指数范围至少 binary64 的有界扩展及最终 binary64 存储，
  每步至多再一次 binary64 舍入；即使用 `2v` 和双倍 δ，上述逆投影误差
  `<2⁻²⁸<10⁻⁸`、几何余量仍 `<10⁻⁵`。比较右侧重算半径若保留扩展精度，
  与存储 R 差 `<2vL+2δ`，仍远小于 `10⁻⁶`。FMA 只移除现有乘加的一次
  舍入，大小界不增；例如 `dx²+dy²` 一次 FMA 的下界仍可用原两层乘积界。
  格点映射中整数／半整数运算在这些精度下依然精确。
- **不据此放行任意 reassociation／fast-math**：只融合原运算树已有乘加可用上界；
  任意变换的运算次数、除法近似、有限值检查消除没有这些保证。
  当前无重排是充分的可核查条件；不要求为了此命题额外加 `-ffp-contract=off`。
- binary64 格式可用明确满足以上精度／范围及语义的实现替代，不要求品牌／ABI；
  不能降成 binary32：仅差值误差项 `2⁻²⁴·2²⁹=32` 已超整格余量，现有证明
  无法资格化。floor/ceil 要求它们对实际有限参数返回数学整数取整值（整数在域内
  可精确表示），不依赖运行时 rounding 方向；无需认证一般 libm 的正确舍入。
  `int≥32位` 也是过强充分条件：本函数所有端点／自增／宽度均可用有符号
  `digits≥22` 安全覆盖（`2L+1<2²²`），extent `<22000`，乘积在 uint64_t 中。
  本包不把这一放宽移植到该类其他整数计算；现有 `1<<20` 自身也要求足够宽 int。

**简洁资格矩阵。** “本地满足”仅指所查配置／探针；外部条件不靠它推定。

| 前提 | 真正需要的条件／可放宽项 | 编译时证据 | 运行时／调用路径／平台状态 |
|---|---|---|---|
| binary64 | ≥53有效位、足够指数范围、有限值与单调基本算术；本包用binary64资格 | 两配置 g++14.2、x86-64、SSE2；double 53位，指数[-1021,1024]、IEC559 | 本地满足；其他ABI／编译器未验证；RasterPoint无格式门 |
| nearest | 非必需；以上 v 误差界足够 | 默认 `-frounding-math` disabled，不能据此保证任意动态模式优化语义 | 新进程 nearest=1、tie_even=1；实际调用线程无检查／恢复保证 |
| 下溢 | 非必需严格渐进；仅本段可容常数个δ的FTZ/DAZ | `__DBL_HAS_DENORM__=1` 不等于控制字保证 | 探针MXCSR=0x1fa2，FTZ/DAZ均关、gradual=1；未注入模式或认证其他平台 |
| 额外精度 | 至少binary64的有界扩展可容；降低精度不可容 | `FLT_EVAL_METHOD=0`、`-mfpmath=sse` | 本地无x87额外精度；其他执行模型未验证 |
| contraction | 原树FMA可容，不需要一律禁止 | `-ffp-contract=fast`，但 `-mfma` disabled；非“显式禁止” | 其他CPU FMA不是自动反例；未验证其构建 |
| reassociation | 保留有界运算树／有限值检查；任意fast-math不资格 | associative/unsafe/finite-only disabled，无fast-math；CMake未强制保留这些设置 | 不支持从当前配置泛化用户注入flags或Release／LTO |
| floor/ceil | 域内精确数学取整，且转换不越界 | 实现用std::floor/ceil；工具链／libm语义是外部契约 | 探针负floor=-1、正ceil=1只作smoke，不是libm全域证明 |
| int宽度 | 本函数digits≥22充分；≥32位非必要 | sizeof(int)=4；探针int_digits=31；uint64_t乘积 | 本地满足；窄int目标不在资格内 |

**最小缺口与唯一优先建议。** 当前认证无 RasterPoint 调用，故此处没有需立刻修复的
生产行为缺口，也没有复现到耦合遗漏 bug；不新增平台框架、认证门或编译flag。
剩余仅两项：(1) 可达动态／直接入口无本地运行环境契约，当前进程探针不证明其线程状态；
(2) 未查构建／平台不能继承本机资格，尤其fast-math、低精度与不符合取整语义的工具链。
**优先最小落地是保留本段弱化后的数值契约与“不在当前认证路径”结论，不改代码。**
只有主会话另行决定开放动态调用时，才把对应实际构建是否满足该契约作为开放条件；
不要求先扩充nearest／FTZ／FMA防御门，也不以继续调查游戏Math或A/B作为本包产物。

**实际命令／结果（可复现记录）。** 仅只读源码／配置和一个独立小探针；没有工程构建、
没有重跑前包四项测试、没有安装、游戏或存档操作。读取命令包括
`sed -n '1,150p' /home/edwards/.claude/plans/shimmying-jingling-hollerith.md`、
`sed -n '1105,1205p' docs/IndependentVerifier.md`，以及上述源码位置、
`CMakeLists.txt:1–90`；`grep -nE 'RasterPoint|ffast-math|ffp-contract|frounding-math|Ofast|fast-math'`
的窄文件搜索，`find build -maxdepth 4` 定位已有配置。
配置提取实际为以下Python（两配置均Debug，LIBVERIFY分别ON/OFF，所查三个源参数一致）：
```sh
python3 - <<'PY'
import json
for d in ['build', 'build/verifier-m1-no-native-20261028']:
    print('BUILD', d)
    for a in json.load(open(d + '/compile_commands.json')):
        if a['file'].endswith(('IndependentMetrics.cpp', 'ReferenceMotion.cpp',
                               'ContinuousCollision.cpp', 'libverify.cpp')):
            print(a)
    print('\n'.join(s.strip() for s in open(d + '/CMakeCache.txt')
          if s.startswith(('CMAKE_BUILD_TYPE:', 'CMAKE_CXX_COMPILER:',
                           'CMAKE_CXX_FLAGS', 'OPUS'))))
PY
uname -sm
c++ --version | head -1
```
结果Linux x86_64、Debian g++14.2.0-19；实际编译 `/usr/bin/g++`，共同参数为
`-DFMT_SHARED -DSPDLOG_COMPILED_LIB -DSPDLOG_SHARED_LIB`、源码／Core／IO／Verification／
spdlog include、`-g -std=gnu++20 -fPIC -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-sign-compare -Wno-invalid-offsetof`。
CMake全局CXX_FLAGS为空，Debug=-g；未把缓存的Release=-O3当成已验证Release。
有效查询与探针命令如下（不修改现有对象；探针用同一编译器／语义参数，省略无关宏、include和警告）：
```sh
printf '' | /usr/bin/g++ -g -std=gnu++20 -fPIC -dM -E -x c++ - \
  | grep -E '__DBL_(MANT_DIG|MAX_EXP|MIN_EXP|HAS_DENORM)__|__FLT_EVAL_METHOD__|__SIZEOF_INT__|__SSE2__|__FAST_MATH__|__FINITE_MATH_ONLY__'
printf '' | /usr/bin/g++ -g -std=gnu++20 -fPIC -x c++ -Q --help=optimizers -S -o /dev/null - \
  | grep -E 'fp-contract|associative-math|finite-math-only|rounding-math|unsafe-math|excess-precision'
printf '' | /usr/bin/g++ -g -std=gnu++20 -fPIC -x c++ -Q --help=target -S -o /dev/null - \
  | grep -E 'mfma |mfpmath=|march='
/usr/bin/g++ -g -std=gnu++20 -fPIC -x c++ -o build/raster-environment-audit-probe - <<'CPP'
#include <cfenv>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <limits>
#include <xmmintrin.h>
int main() {
    using D = std::numeric_limits<double>;
    volatile double tiny = D::denorm_min(), one = 1., half = 0x1p-53;
    volatile double gradual = tiny * 1., tie = one + half;
    std::printf("iec559=%d radix=%d digits=%d min_exp=%d max_exp=%d int_digits=%d eval=%d nearest=%d mxcsr=0x%x gradual=%d tie_even=%d floor=%g ceil=%g\n",
        D::is_iec559, D::radix, D::digits, D::min_exponent, D::max_exponent,
        std::numeric_limits<int>::digits, FLT_EVAL_METHOD,
        std::fegetround() == FE_TONEAREST, _mm_getcsr(), gradual == tiny,
        tie == one, std::floor(-.25), std::ceil(.25));
}
CPP
./build/raster-environment-audit-probe
status=$?
rm -f build/raster-environment-audit-probe
exit "$status"
```
有效探针exit 0，输出 `iec559=1 radix=2 digits=53 min_exp=-1021 max_exp=1024
int_digits=31 eval=0 nearest=1 mxcsr=0x1fa2 gradual=1 tie_even=1 floor=-1 ceil=1`。
宏另有FINITE_MATH_ONLY=0、SSE2=1，未定义FAST_MATH；优化器／target结果见矩阵。
保留失败事实：首次探针误将警告项拆为 `-Wno-invalid offsetof`，链接报找不到
`offsetof`，运行文件不存在，exit127；不是数值失败。首次 `-Q --help` 未指定语言产生
C语言警告；随后指定C++但漏输入报 `no input files`；最终以上stdin查询成功。
首次搜索带空type被rg拒绝后改窄cpp搜索；glob未列出被忽略build，改find定位；
搜索不存在的 `src/IO/libverify.cpp`／`Verifier*.cpp` 报无文件后使用实际头文件与worker。
这些失败不作为通过证据。生成探针已删除；仅更新本文档，不更改任何既有未提交／未跟踪工作。
未验证Windows、ARM、32位／x87、其他编译器／libm、Release／LTO或线程环境变更。

### C 第五包：动态面积 uncertainty 的点误差契约与边界

本包仅作数学／消费者审计，不修改生产行为、认证门、profile 或预算；不重开 A/B，
不延续第四包的环境资格调查。以下 `H(q,r)=(82q+41r,71r)`、`|·|₁` 为像素坐标 L1 范数，
`u=2^-24`、`η=2^-149`。点误差、采样间扫掠、reference→game 是三个不同命题。

**实际表达式及消费者。** `IndependentMetrics::EndMotions` 中逐 sweep 的量为

```
U = .05 + (sweep.turns ? .03 * maximumRadius : 0.)
        + 16 * numeric_limits<float>::epsilon() * (|C|₁ + |P|₁ + |D|₁)
```

其中 `P=H(position)`、`C=H(center)`、`D=H(translation)`；`maximumRadius` 从 1 起，
取同帧所有旋转 sweep 相对中心的最大 hex 距离，不是 `sweep.radius`（原子58／底座49），
也不是逐 sweep 单独计算。零角平移没有 `.03` 项，但仍计入传入 center 的尺度。
同一个 maximumRadius 决定 `steps=2^max(3,2+round(log2(maximumRadius)))`；
只取 `t=i/steps, 1≤i<steps`。double 中心为 `C+R(angle)(P-C)+Dt`，
没有调用 ReferenceMotion 或其误差界。邻接注释只说像素网格各向异性和 reference float
运算可能改变边界归属：这说明它是边界缓冲的实现意图，不构成 `.05`、`.03` 或
系数16的推导，更没有 reference→game 保证。不能把它解释成已经证明的采样间位移界。

`RasterPoint` 用 `radius-U` 严格内圆调用 `Mark`，`radius+U` 闭外圆产生 uncertain；
边界带里仅名义严格圆内的格也加入 area。故 area 是名义采样圆及直接标记的并集，
不是已证明的上下界计数；uncertain 可同时含 area 内、外的格。`Mark` 会删除已有
uncertain，所以不确定集合不是永久运动记录。`Finish` 只要求初始化且帧关闭，
无论是否 Conclusive 都返回 area 大小；`Conclusive` 仅表示当前无 raster 歧义。
`CertificationEligible=Conclusive&&!observedMotion`，不能用清空 uncertain 恢复动态资格。
生产中 `StaticProductionVerifier::CertifiedMotions` 先拒绝动态，完成报告仅在 certify
分支取 `Finish` 和 `CertificationEligible`；最终 `VerifyIndependent` 还要求
`metricsConclusive && continuousCertified`，否则返回 Unavailable，且不发布独立 metrics。
因此当前公式影响直接动态诊断／差分面积，不是当前认证路径可利用的动态安全漏洞；
非 certify replay 也不通过这条 Finish 分支发表测量。修改其语义仍应由主会话决策。

**一个真正但很窄的本地点包围。** 取合法非 piston Track、baseRotation=0，且
base、translation、grabberOffset、atom、absoluteGrabPosition 的每个 hex 分量均在
`[-1,1]`。令 `B=H(base)`、`T=H(translation)`、`G=H(grabberOffset)`、
`O=H(atom-absoluteGrabPosition)`，明确要求指标 sweep 满足
`turns=0, center=(0,0), D=T, P=B+G+O-T`，且同帧没有旋转 sweep。
这些是额外输入关联，不由两个接口自动保证。该域包含既有
`DifferentialLinearPistonAndTrackMotionAndHaltedTapeAccounting` 的小坐标 Track 平移，
不是新增 profile，也不声称差分 fixture 已建立游戏映射。

前提沿用已有局部误差界的 IEEE binary32／binary64、nearest、渐进下溢、无 fast-math、
FLT_EVAL_METHOD=0，并要求零角 sin=0、cos=1（包含实际使用的有符号零）。每个上述小
hex 的 `|H|₁≤194`，offset 的界为388，所以
`S=|B|₁+|T|₁+|G|₁+|O|₁≤970`。已有 Track 定理给出相对精确
`B+G+O-T(1-t)` 的欧氏误差 `≤32uS+32η`；其向上舍入接口结果也远小于 `.002`。
这里 steps=8，P、D 都是小整数像素坐标，乘以 i/8 后至多三个二进制小数位，
零角的整条 double 中心求值无舍入误差，正好等于上述精确目标。因此每个实际取样时刻
`||AtomAt(m,t)-rasterCenter(t)||₂ < .002 < U`。不是把经验常数改名为证明：
独立推导的界严格小于代码现有正余量，只在列出的受限域成立。
同域的 base sweep 取 P=B-T、D=T、C=0，已有 Base 界的 L1 上界
`u*(194+3*194)+6η < .000047`，同理被 U 支配。端点由其他直接标记处理，
本命题不证明那些标记的几何语义。

此结论确实给出每个采样点的真实坐标球包围；三角不等式还给出**精确实数圆盘**的
半径 `r-U` 内／`r+U` 外关系，但不消除 `RasterPoint` 实际 double 平方比较的边界
舍入义务，不把 Conclusive 升为精确几何证书。也不填补任意相邻 t 之间的覆盖，
更不把本地 AtomAt 对应为游戏轨迹；已有连续弧反例不在本包重复记为新发现。

**全接受域的点包围否定：独立字段抵消。** 即使零角且有上面的精确 P/D 关联，
现有公式也不覆盖所有合法 ReferenceMotion 输入。令 `L=2^20`，全部 r=0，取

```
Track; isPiston=false; baseRotation=rotation=pistonExtension=0
base=(L-1,0); translation=(1,0); grabberOffset=(1,0)
atom=(0,0); absoluteGrabPosition=(L,0)
metric position=(-1,0); center=(0,0); translation=(1,0); turns=0
```

末态 B+G+O=0，初态 P=-82，t=1/2 是实际七个内采样点之一，独立 double 中心为-41。
逐步 binary32 中 `ToXY(base).x=85983152`（精确值85983150），
减去41后为85983112，加82后为85983192，再加 `O.x=-85983232` 得 `AtomAt.x=-40`。
y=0，所以实际点误差为1；此时 `U=.05+16*2^-23*(82+82)`，即
`0.050312805175781253`，严格小于1。每个输入字段及指标初末态都在接口坐标门内；
`absoluteGrabPosition=base+grabberOffset` 也成立。这不是声称当前受限生产 profile
能产生该长偏移片段，只是否定通用接口接受域的包围推论。现有 Track 界不会失效：
其 S 计入独立 B/G/O，指标尺度却被端点抵消，两个 `32u` 系数相同没有帮助。

**实际 C++ 最小有界验证（2026-11-04）。** 持久回归位于
`tests/unit_test/ReferenceMotionTest.cpp` 的
`ReferenceMotion.TrackFieldCancellationExceedsEndpointScaleUncertainty`。
直接调用 `BaseAt`／`AtomAt`，得到 `BaseAt(1).x=85983152`、
`BaseAt(.5).x=85983112`、去掉 atom offset 的 `AtomAt(.5).x=85983192`，
原输入 `AtomAt(.5)=(-40,0)`。与此前 Python 运算序核对一致，但此处通过依据是实际
C++ 测试，不是 Python。测试检查各独立字段在门内、零角／非 piston、抓取位置恒等式，
并用真实 `BasePositionErrorBound`／`TrackAtomPositionErrorBound` 检查误差仍被包围；
未抵消 `S=171966546`，返回 Track 界不小于 `32uS`。

同一回归显式核对 `P=B+G+O-T`、`D=T` 及 y 关联；按当前 EndMotions 零角公式计算的
**诊断对照**名义中心为 `(-41,0)`、`U=0.050312805175781253`，实际误差 `1>U`。
这没有读取私有 RasterPoint 的中心／U，也不声称观察到错格或最终面积错误。
真实 `IndependentMetrics` 初始化、`ObserveAtomMotion` 与 `EndMotions` 均接受该 sweep，
实际 rasterPoints=7（t=4/8 在采样序列中），动态 `CertificationEligible` 仍为 false。
这仍不证明受限生产 profile 可达性、连续覆盖或 reference→game 映射。

执行命令及结果（现有 Debug build；无失败）：

```
cmake --build build --target OpusOptimaTests -j2
# exit 0；编译 ReferenceMotionTest.cpp 并链接测试目标
./build/OpusOptimaTests --gtest_filter='ReferenceMotion.TrackFieldCancellationExceedsEndpointScaleUncertainty:ReferenceMotion.BaseBoundCoversRoundingAndCoordinateExtremes:ReferenceMotion.TrackAtomBoundCoversWholeExpression:ReferenceMotion.TrackAtomBoundCoversIndependentFields:ReferenceMotion.TrackBoundsComposeWithContinuousSeparation:ReferenceMotion.UnsupportedRoundingFailsClosed:ReferenceMotion.*Underflow*:IndependentMetrics.MotionCertificationCannotBeRestoredByClearingRasterAmbiguity'
# exit 0；8/8 通过（*Underflow* 命中 NonGradualUnderflowFailsClosed）
```

本次仅追加测试和更新本文档；未改生产代码／uncertainty／认证／profile／契约 API，
legacy-gate 与 2-10-20 预算不变；未运行全应用、冻结2254、游戏或存档操作。
既有未提交／未跟踪修改保留。证据足以建议进入显式点契约设计，不代表已经实现或批准。

**有界设计决策：仅推荐现有测试 fixture 层的配对点契约，生产暂不接入（本轮不实施）。**
按上文“证据与超时收敛”的资格门，本契约只比较本地数值模型，不补 A/B 生命周期、实体／游戏对应。
直接调用证据止于 `StaticProductionVerifier.cpp::CertifiedMotions`、
`IndependentMetricsTest.cpp` 的 Observe 调用及 `ReferenceMotionTest.cpp` 的抵消反例，不继续全程序追踪。
生产的同名 `Motion` 只有 `center/translation/turn`；`before` 有原子位置，
`ArmMotionSnapshot` 有 base/length/direction/grips/action 等，但没有完整
`reference_motion::Motion` 的独立 B/G/O、末态抓取点及其配对来源。
`CertifiedMotions` 在 Observe 前已拒绝动态原子／臂，只上报静止位置和前后相同臂姿态。
`IndependentMetrics::Sweep` 仅留 position/center/translation/turns/radius，不能反推完整 Motion。
线性 Track/piston 指标测试虽有人工前后姿态，也没有 reference Motion 的可验证字段关联。
因此生产直接调用链**不持有本契约所需的完整配对数据**；禁止根据终点或臂 pose 重建独立 S。
已有实际消费者是 `ReferenceMotionTest.TrackFieldCancellationExceedsEndpointScaleUncertainty`：
同一 fixture 显式构造完整 Motion 和被 Observe 接受的 sweep，并检查 P/D 恒等式。
其价值是把已记录的 `error=1>旧U≈.050313` 固定为总包围回归入口，而不是造无人消费的通用接口。

**最小输入与拒绝。** 如主会话批准，契约只落在该测试文件的局部断言／helper，
输入完整末态 Motion、显式配对的 sweep 和采样索引 `(i,n)`；不扩展公共头文件或 Sweep。
限定 Track、非 piston、baseRotation/rotation/pistonExtension 全零，各 reference 字段和
sweep position/center/translation 各轴均在既有 `±2^20` 域，turns=0。
用至少 int64 的精确整数运算检查 `O=atom-absoluteGrabPosition`、
`position=base+grabberOffset+O-translation`、`sweep.translation=translation`；
经 H 映射即 `P=B+G+O-T, D=T`。抵消 fixture 还显式检查末态抓取点等于 base+grabberOffset，
这些只是 fixture 数据关系，不宣称已还原生产实体身份。
仅接纳 `3≤n≤16, 1≤i<2^n` 的 `t=i/2^n`，对应 EndMotions 的实际内部采样；
若需核对某帧 cadence，n 须由该帧全部 sweep 的 maximumRadius 规则确定，不能只看本 sweep。
缺字段／配对来源、不满足整数关系、越域、非有限值、未知 cadence 或不同进度，一律报告契约不可用，
不返回零界、不影响既有 metrics 诊断结果。拒绝旋转、piston 和未经限定的任意 t。

**最终点包围不是旧 U。** 目标是同一点 `||AtomAt(m,t_f)-N(sweep,t_d)||₂≤E_point`，
其中 N 必须按 EndMotions 的实际 double 运算顺序定义，不能悄悄换成另一条求值路径。
必须分解并上取整 `E_point≥E_reference+E_nominal+E_time`：
`E_reference` 是现有 `TrackAtomPositionErrorBound` 对精确
`F(t_f)=B+G+O-T(1-t_f)` 的界，保留未抵消
`S=|B|₁+|G|₁+|O|₁+|T|₁` 及 `32uS+32η`；Base 界和旧 `.05+…` 均不能替代它。
在本限定域，Hx/Hy 为绝对值小于 `2^27` 的整数，P−C 小于 `2^28`；
零角 cos=1/sin=0 后，N 的整数加减精确，D·t 及最终和的 dyadic 分子少于 46 位，
故 binary64 的逐步求值精确，可取 `E_nominal=0`（不是一般 double 误差为零）。
`i/2^n` 在 binary32 和 binary64 中均精确，要求传入的 t_f、t_d 与此有理数相等，
故本域 `E_time=0`；不能由一次强制转换或游戏时间名义相同推定该条件。
若以后放宽时间输入，至少还需上取整 `||T||₂·|t_f-t_d|`，
以及相对选定精确进度的各自表示／配对误差；本推荐不支持该扩展。
因此受限域可复用 reference 返回界作为最终数值半径，**理由是另两项在明确条件下为零**，
不是直接把旧 bound 当 U 或改写生产 uncertainty。
环境沿用 reference 已检的 radix-2 binary32/64、nearest、FLT_EVAL_METHOD=0、
无 fast-math、SSE 无 FTZ/DAZ，并要求 nominal 路径也满足它们、正负零三角值及既定求值语义；
不满足或无法核验即不可用，不声称普遍 libm／游戏保证。

**最小变更与验收出口（待批准，非本次验证）。** 本轮只更新本契约段；后续若实施只改
`ReferenceMotionTest.cpp`，增加局部配对断言及病例，不改 ReferenceMotion/IndependentMetrics
API、旧 U、输出、profile 或生产调用者。正例覆盖小坐标、各独立字段边界、双轴及强抵消，
共同 dyadic t 的首末内部点和 `.5`；逐点检查总包围，保留 `error>旧U` 和动态资格为 false 的反例。
反例覆盖缺 Motion／配对、P/D 不符、非法字段、t 表示或 cadence 不符、环境不符、旋转和 piston；
均应明确不可用，不能以零界通过。本轮文档-only，不构建、不重跑测试；上段历史 8 项结果不是新验证。
未证事项仍是实际游戏字段／时间对应、A/B 生命周期及动态资格；不推导连续采样间覆盖、
旋转／piston、RasterPoint 圆盘判定或最终面积／四指标正确性。legacy-gate、2/10/20 预算和诊断行为不变。
本设计包到此停止。主会话监督决定：暂不实施测试限定契约；生产调用链缺少配对数据，既有真实抵消回归已覆盖关键否定，再新增测试专用helper暂不能关闭生产义务。保留设计与反例，暂停该分支。后续转入阶段3磁带资源的离线准备，仅核实边界、归一化和资源约束，不开放profile，不把阶段2标记通过。

## E／阶段3：磁带位置首个离线准备包（未开放）

本包仅静态追踪 `ParseSolutionFile → ParseInstructions → CheckScope → Instruction`，并核对直接使用的 writer、metrics 和现有测试；不执行语料、探针、构建或测试，不以历史测试数替代本次结果。A/B 生命周期及实体覆盖、阶段2语义／成本总门 D **仍未通过，正式扩展阻塞**；按收敛计划 E 可独立准备规则，不改变 `finite-ordinary-v1`、legacy-gate、认证资格或 2/10/20 秒预算。历史报告的磁带不可用 **235** 仅是上述冻结报告的归因，本次没有重计，不能当作当前可新增覆盖数。

### 实际边界与待证矩阵

| 层／命题 | 当前精确证据与结论 | 状态／缺口 |
| --- | --- | --- |
| codec 的原始位置与条目数 | `BinaryParser.hpp:88–91` 的位置为 signed int32；`BinaryParser.cpp:362–370,373–385` 只要求 count 非负且 `count <= Remaining()/5`，在 reserve 前检查；每条先读 i32 位置再读 action。`BinaryWriter.cpp:98–101` 同序写回，未施加 ±20000。`ParseSolution:453–465` 的对象数也是字节结构界，不是 profile 配额 | ±20000 不在 parse 层；完整字节足够时 INT32_MIN/MAX 位置均可进入 vector，之后才被 scope 拒绝。结构检查不保证磁带总内存或解析时间 |
| replay scope 与实际双重位置门 | `IndependentCapabilities.hpp:25` 声明 `maxTapePosition=20000`；`StaticProductionVerifier.cpp:535–542` 先在 allowOrdinary 分支对越界返回 Unsupported／Unavailable，随后另有独立硬编码 ±20000，越界或重复键 Rejected。`VerifyIndependent:1440–1446` 使用 allowOrdinary=true；旧生产直接入口仍使用 false | 实际不一致点是 profile 值与硬编码两个来源：只改 profile 不能扩展，甚至会把新范围改判 Rejected；生产专用路径当前不走相同 Unavailable 分类。这是现状，不是本包修复对象 |
| 局部归一化及周期 | `StaticProductionVerifier.cpp:582–601`：每臂 `s=min(position)`，键变为 `p-s`；非 B 磁带贡献 `max-min+1`，B 磁带不贡献 period。正 s 按升序、负 s 按降序 extract/reinsert，节点复用，不创建跨度大小的数组 | 当前 s,p∈[-20000,20000]，归一化键∈[0,40000]，周期候选∈[1,40001]；减法及 +1 均安全，重键不覆盖待处理节点。空臂 start=0，不参与局部 period |
| 全局相位与执行 | `StaticProductionVerifier.cpp:700–708,797–801`：在**所有臂（含空带）** start 中取最小 first，再 `start-=first`；period 至少1；`cycle<start` 或 halted 则 idle，否则查 finite 的 elapsed 或 elapsed%period。`Run:142` 的 cycle 从0到 limit-1，profile limit≤150000 | 当前全局 start∈[0,40000]，实际计算 elapsed∈[0,149999]，安全；共同平移仅在无空带改变锚点等条件下保持相位，不能一概把任意平移视为等价。稀疏大位置不等于大量指令，也不保证在周期限内执行到 |
| 指令数与存储 | 唯一整数键使每臂至多40001节点，128臂至多 **5,120,128** 节点（scope `505–506,540–542`）；跨度本身不分配空槽。`IndependentMetrics.cpp:166–189` 另建位置 set、统计动作 G/g/R/r/P/p/E/e/A/a，O/B/空格不计；B之后动作仍计。metrics 直接 API 不施加磁带位置 profile | 完成 scope 后的节点数有界，不等于 parse 前资源有界。原始 vector、replay map、metrics 当前臂 set 可同时存在；条目数≠指标指令数≠跨度。重复位置可在 vector 中大量重复，不能借 map 的唯一性推导 parse 配额 |
| 文件／时间／总内存 | `BinaryParser.cpp:233–256,470–478` 整文件读取，只检查 size_t/streamsize/vector 可表示性；`VerifyIndependentFiles:1504–1518` 先解析再创建执行。`StaticProductionVerifier.cpp:1340–1341,326–329` 的2秒 deadline 不含解析；磁带插入／重键循环没有逐项 Budget 检查 | **未证固定总内存和 setup 最坏耗时**；大 raw vector 在 scope 之前已分配，字节缓冲解析时与 vector 共驻。已有保护不足以支持“可任意提升位置上界”的结论；本包未发现当前 ±20000 域内的归一化溢出 bug，无经运行确认的新反例 |

若直接放开至 int32 全域，单臂位置 `{INT32_MIN, INT32_MAX}` 会使 `max-min+1` 及重键超出 int；两臂分别取这两个 start 会使全局 start 差超出 int；即使只放宽到对称 M，周期表达式也要求 `2*M+1<=INT_MAX`（32位 int 时 M≤1073741823）。这是**移除现有保护后的风险输入**，当前被位置门提前拒绝，不能报告为现行可达溢出。B 只免除周期计算，不免除重键／相位风险。

### E 有界设计：单一位置来源与原位前置检查（2026-11-04，历史设计；已按下节批准实施）

> 本节保留批准前设计快照，其中“未覆盖”“尚不存在”“未执行”“仅设计”等均描述当时状态，不是当前结论。当前实施、两轮实际运行及剩余缺口见“E 原位双门实施验收”；设计中的范围限制继续有效。

**唯一推荐：** 保持 `finiteOrdinaryProfile.maxTapePosition=20000` 为唯一数值来源；只在 `CheckScope` 的**原机械臂、原指令循环位置**计算一次 `outsideTapePosition = p < -finiteOrdinaryProfile.maxTapePosition || p > finiteOrdinaryProfile.maxTapePosition`，先执行 `allowOrdinary_ && outsideTapePosition` 的 Unsupported 门，再执行 `outsideTapePosition || !arm.tape.emplace(p, action).second` 的 Rejected 门。保留两个 throw 的类型、detail、短路顺序及后面的 action switch。无需增加另一常量、修改 profile/header 或统一入口状态。此处“前置”仅指每条指令在 map 插入及本臂归一化前的位置检查，不是整机预扫描、解析前检查或 setup 前检查。

依据 `StaticProductionVerifier.cpp:134–136,421–542,573–601,1343–1361`：setup 先 `CheckScope`（其内部先处理环境／部件、构造本臂，再按原始 vector 顺序插入 map、验证动作、最后重键），随后才初始化 metrics。不可把全磁带范围预扫提到 CheckScope 开头、部件循环外或解析器中；否则较晚的越界可能抢先于较早的权限、重复键、未知动作或部件错误，改变状态与 detail。非机械部件携带的 instructions 当前不走此门，不能借统一来源新增验证。保留 `allowOrdinary=false` 对普通无 productionInfo 输入首先 Unavailable 的格式门；下表位置结论均以此前门已通过为前提。

| 直接入口／证据位置 | 当前行为（此前门通过时） | 提议结构与现有测试覆盖／缺口 |
| --- | --- | --- |
| `VerifyIndependent`（`StaticProductionVerifier.cpp:1437–1502`） | `ExecuteReplay(true,true)`；位置越界 Unavailable／`independent-profile-uncertified`，重复键 Rejected／`independent-machine-rejected`；未知动作 Unavailable。productionInfo 输入先被认证门挡住，不到位置门 | 不动 wrapper／metrics 顺序。`IndependentFiniteReplay.CurrentTapePositionGateDistinguishesBoundsFromInt32Extremes`（`IndependentFiniteReplayTest.cpp:180–205`）覆盖 ±20000、±20001、int32极值及稀疏跨度；`ProfileBoundsAreUnavailableRatherThanInvalidMachineVerdicts:279–306` 另覆盖30000。未覆盖混合错误先后 |
| `ReplayFiniteMachine`（同文件 `1391–1436`） | `ExecuteReplay(true,false)`；越界映射为 ReplayOutcome::Unavailable，重复键 Rejected，未知动作 Unavailable；无认证 metrics 初始化 | 同一原位双门，无新增预扫。`TapeNormalizationPreservesActionsForPositiveAndNegativeStarts:165–179`、`EmptyTapeAnchorsPositiveButNotNegativeLocalStarts:206–220` 覆盖边界内归一化／空带相位；`CompletionRequiresEveryPortAndRejectsWrongInstructionsAndPermissions:221–240` 覆盖未知动作和权限。极值状态未在此直接入口单测 |
| `VerifyStaticProduction`（同文件 `1365–1379`） | `ExecuteReplay(false,false)`；位置越界及重复键均 Rejected；未知动作仍 Unavailable，不得套用 ordinary 的位置 Unavailable | 保留 false 分支与 detail。`IndependentStaticProduction.DifferentialTapeOffsetsNormalizeAgainstTheEarliestArm`、`DifferentialIndependentArmsKeepOffsetsAndIgnoreHaltedTapeLength`（`StaticProductionVerifierTest.cpp:325–361`）经文件 wrapper 覆盖相位／B；`UnknownRulesAndInstructionsStayUnavailable:490–526` 直接覆盖未知动作。未覆盖此入口的 ±20001／极值／重复位置 |
| `VerifyIndependentFiles`（同文件 `1503–1530`） | 先解析 puzzle、solution，再调 VerifyIndependent；解码异常 Error／`independent-file-error`，可解析的机械臂越界沿用 Unavailable | 不改读取、catch、parse timing；上节 codec 极值往返并非此 wrapper 状态证据，此位置极值文件入口仍缺定向覆盖 |
| `VerifyStaticProductionFiles`（同文件 `1380–1390`） | 解析后调 VerifyStaticProduction；解码异常 Error、detail 前缀 `independent file decoding failed:`；可解析且此前门通过的越界 Rejected | 不改 wrapper／参数求值结构。上述两项 Differential 经此入口；没有文件位置极值或混合错误测试，不声称覆盖 |
| `BinaryParser::ParseSolution`／`ParseSolutionFile`（`BinaryParser.cpp:362–385,453–478`）；`BinaryWriter`（`BinaryWriter.cpp:98–101`） | signed int32位置保留；count结构门后 reserve，文件先整读；不受位置profile限制，格式错误抛 ParseError，codec本身没有VerificationStatus | 完全不接入位置门。`SolutionParseUnitTest.PreservesSignedInt32TapePositionsThroughCodec:75–92`、`RejectsInstructionCountsAndByteTruncationAtTheActualRead:93–121`（`SolutionParseTest.cpp`）覆盖内存codec往返／截断，不证明文件预算 |
| `IndependentMetrics::Initialize`（`IndependentMetrics.cpp:92–95,166–189`） | 无位置profile门；机械臂重复位置／未知动作抛 invalid_argument；非机械部件不按磁带计数。直接API不是独立认证入口 | 不接入位置门。`IndependentMetrics.StaticCostsAreaAndTapesNeverTrustDeclaredMetrics:299–316` 与 `UnknownProfilesAndIncompleteMotionFramesCannotProduceMetrics:427–440`（`IndependentMetricsTest.cpp`）分别覆盖计数及重复／未知动作；位置极值直接调用未单测 |

**错误优先级兼容性：** 每条先位置，再插入／重复键，再动作／权限；按 vector 原顺序逐条、逐部件执行。单条越界且动作未知：true 为位置 Unavailable，false 为位置 Rejected；范围内重复键且第二条动作未知：重复键 Rejected；较早范围内未知动作、较晚越界／重复键：先未知动作 Unavailable；较早重复键、较晚越界：先重复键 Rejected。仅看最终同为 Unavailable 不足，必须保留原 detail。范围内未知动作已见上述现有测试；这些混合错误组合目前仅由 `535–580` 的静态控制流推导，**没有新增运行证据**。

**最小实施文件与验收（留给主会话决定，不在本包执行）：** 生产只改 `src/Verification/StaticProductionVerifier.cpp:535–542` 的谓词复用；`IndependentCapabilities.hpp:25` 原值不动。只在既有 `IndependentFiniteReplayTest.cpp`／`StaticProductionVerifierTest.cpp` 增加各自命名为 `TapeGatePreservesEntryStatusAndErrorOrder` 的有界用例：复用现有fixture，分别断言 true 的两个直接入口、false入口和两个文件wrapper的 ±20000／±20001／int32极值、重复键与上述混合错误状态及原detail；文件wrapper另断言截断仍Error及原reason/detail，不要求复现未指定的文件参数求值顺序。不加压力生成器或未来范围测试。codec／metrics源码、header、预算及策略均不改。

批准实施后最小命令（仓库根目录；**本设计未构建、未执行**）：

```sh
cmake --build build --target OpusOptimaTests -j2
TMPDIR="$PWD/build" build/OpusOptimaTests --gtest_filter='IndependentFiniteReplay.TapeGatePreservesEntryStatusAndErrorOrder:IndependentStaticProduction.TapeGatePreservesEntryStatusAndErrorOrder:IndependentFiniteReplay.CurrentTapePositionGateDistinguishesBoundsFromInt32Extremes:IndependentFiniteReplay.TapeNormalizationPreservesActionsForPositiveAndNegativeStarts:IndependentFiniteReplay.EmptyTapeAnchorsPositiveButNotNegativeLocalStarts:IndependentStaticProduction.DifferentialTapeOffsetsNormalizeAgainstTheEarliestArm:IndependentStaticProduction.DifferentialIndependentArmsKeepOffsetsAndIgnoreHaltedTapeLength'
```

新测试名仅是实施验收约定，当前尚不存在；须先加入测试再用此filter，不能把未匹配当通过。现有20项通过仅引用下节已记录的构建／测试日志；本包不重跑。其他直接API保持不动，其现有codec／metrics回归证据不扩写成极值wrapper或混合错误覆盖。

**解析前资源政策单列，不纳入推荐实现：** 固定文件字节上限会拒绝目前仍可完整读取／解析的文件（含不参与执行的内容）；每部件或全solution raw条目配额会计入重复键、非机械部件指令，可能排除此前可接受输入，或把原Rejected／未知动作Unavailable提前改判为资源Unavailable。即使40001唯一键／臂是现行位置域的推论，也不是raw vector配额；不可在reserve前据此新增默认限制。提前按文件大小／count拒绝还会抢先于后续截断的原Error。给解析／setup新增deadline或工作量上限同样可能将现行可完成输入改为Timeout／Unavailable，且检查顺序须另行定策。这些都需要用户明确批准接受域与状态变化；本包撤回上版具体候选数值，不推荐任何新默认配额或扩域。

保留已有 `count <= Remaining()/5` 与可表示性检查不改变域，但不能推导固定解析资源界。只统一位置来源也**不等于setup耗时／总内存证明**：原始vector已存在，前面部件已构图，成功scope的map节点界不限制解析字节／raw重复项，也不覆盖allocator与metrics set峰值。无需也不得借本方案宣称E资源义务闭合；不扩展其他规则，A/B/D仍阻塞，正式扩展不开。


### E 最小现行规则测试包（2026-11-04，离线准备，不开放范围）

以下保留上一测试包的实际执行记录（不是本设计包的新运行）：当时仅修改 `SolutionParseTest.cpp`、`IndependentFiniteReplayTest.cpp` 及本文；新增4个小测试、扩充1个现有参数组合，没有修改生产入口、profile、硬编码、codec接受域、legacy-gate或2/10/20秒预算。当时的候选资源数值未经批准，现已由上节行为保持设计取代，没有成为测试政策或验收条件；A/B及D仍阻塞正式范围扩展。

- `SolutionParseUnitTest.PreservesSignedInt32TapePositionsThroughCodec`：通过真实 writer → parser → writer，逐项比较位置和动作，再比较完整字节；同一小磁带包含 INT32_MIN/MAX、±20000、±20001。现行通用codec保留这些signed int32原始位置，并不执行±20000门。
- `SolutionParseUnitTest.RejectsInstructionCountsAndByteTruncationAtTheActualRead`：使用既有字节builder构造单臂；负count得到 `Negative instruction count`，INT32_MAX及count=1但仅余0–4字节得到 `Invalid instruction count`；余5–8字节时完整条目通过count门，但缺失／截断的4字节footer得到 `Unexpected EOF while reading i32`。这区分结构count门与后续读取失败，不把原有metrics/object count测试冒称instruction count证据。
- 扩充 `IndependentFiniteReplay.TapeNormalizationPreservesActionsForPositiveAndNegativeStarts`：沿用真实四动作搬运机，把start=-20000和19997加入已有±1/±4；动作键分别触及-20000和+20000，仍为完整6输出、23周期，原始输入位置不变。无复制归一化算法作为oracle。
- `IndependentFiniteReplay.CurrentTapePositionGateDistinguishesBoundsFromInt32Extremes`：沿用静态机器，经 `VerifyIndependent` 的单点B在±20000均Passed、3周期；±20001及INT32_MIN/MAX均Unavailable，detail为 `tape position exceeds the finite profile bounds`。同一fixture的稀疏 `B@-20000,r@20000`（跨度40000）Passed、3周期、指标指令数1，B后的动作仍计入静态指标、不执行。
- `IndependentFiniteReplay.EmptyTapeAnchorsPositiveButNotNegativeLocalStarts`：直接复用四动作机器，添加一个远离工作区的空带臂，无大生成器。start=-4/0/+4均完成6输出，周期分别23/23/27；空带start=0使正偏移不能整体消去，负偏移仍可消去。仅报告观测行为，不把任意共同平移当作等价。

本次实际执行（仓库根目录，组合单次运行；退出码均0）：

```sh
cmake --build build --target OpusOptimaTests -j2 > build/stage3-tape-minimal-build.log 2>&1
TMPDIR="$PWD/build" build/OpusOptimaTests --gtest_filter='SolutionParseUnitTest.*:IndependentFiniteReplay.TapeNormalizationPreservesActionsForPositiveAndNegativeStarts:IndependentFiniteReplay.CurrentTapePositionGateDistinguishesBoundsFromInt32Extremes:IndependentFiniteReplay.EmptyTapeAnchorsPositiveButNotNegativeLocalStarts:IndependentFiniteReplay.HaltedLongTapeKeepsStaticCountsWithoutExecutingLaterMotion:IndependentFiniteReplay.ArmCountBoundIsCheckedBeforeConstructingTheNextTape:IndependentStaticProduction.DifferentialTapeOffsetsNormalizeAgainstTheEarliestArm:IndependentStaticProduction.DifferentialIndependentArmsKeepOffsetsAndIgnoreHaltedTapeLength:IndependentMetrics.StaticCostsAreaAndTapesNeverTrustDeclaredMetrics:IndependentMetrics.DifferentialLinearPistonAndTrackMotionAndHaltedTapeAccounting' > build/stage3-tape-minimal-tests.log 2>&1
```

构建仅重编两个测试源并链接既有runner；首次组合运行20项全过（45ms）；发现既有metrics文件fixture默认使用系统临时目录后，以 `TMPDIR="$PWD/build"` 将其文件落点约束到仓库内，重新实际运行上述同一组合，**20测试／4 suites全部通过（36ms）**。首轮该既有helper意外使用系统临时目录，不能宣称首轮满足文件落点限制；最终日志为仓库内重跑结果。包含本次新增／扩充用例及复用的多臂相位、B后计数、arm count、metrics及差分证据，非借用历史通过。没有发现本次运行可复现的新bug，故没有改变行为。

未覆盖：非B最大跨度40000的完整周期执行、40001密集唯一键／40002条目、重复raw超量与总5,120,128节点、最坏setup耗时／峰值内存、文件入口位置极值状态及旧生产专用入口的越界分类；未做5百万节点压力测试。字节截断仅经内存codec入口；现有差分用例虽经过文件解析，不代表新增位置极值已覆盖文件入口。不声称结构count检查证明解析资源有界，亦不声称稀疏测试测出了分配复杂度。未来位置／文件资源政策仍须单独批准。

**交主会话决定：** 上节已收敛为唯一的原位双门／位置常量单源方案及未来验收命令；本设计包仅更新本文，不实现、不构建、不重跑上述20项，不增加配额或扩域。legacy-gate、profile和2/10/20秒预算不变；A/B/D继续阻塞，正式扩展不开。到此停止。

### E 原位双门实施验收（2026-11-04，保持行为，不开放阶段3）

主会话批准后实施。生产增量仅 `src/Verification/StaticProductionVerifier.cpp` 原指令循环的双门：一次计算 `outsideTapePosition`，两门共享 `finiteOrdinaryProfile.maxTapePosition`（仍为20000），原位置、异常类型、detail、短路及插入／动作检查顺序不变。未提前预扫，未改 header、profile、codec、metrics、预算或文件／raw配额；A/B/D仍阻塞，不声称E资源义务闭合。

测试增量仅在 `tests/unit_test/IndependentFiniteReplayTest.cpp` 与 `tests/unit_test/StaticProductionVerifierTest.cpp` 各增加一个无条件发现的 `TapeGatePreservesEntryStatusAndErrorOrder`。ordinary复用静态单原子fixture及已有just-salt文件，production复用Conduit fixture；生产正常带分别整体平移-20000与19993，使最小位置-20000、最大位置20000，保留原动作，无大型fixture。覆盖 ±20000通过，±20001和int32两极值在两个true直接入口Unavailable、false入口Rejected，以及两个文件wrapper的相同状态／detail；ordinary认证入口另断言reasonCode。覆盖越界兼未知动作、重复兼未知动作、较早未知后越界／重复、较早重复后越界，逐项锁定原detail。两个文件wrapper分别以有效puzzle和一字节solution断言原解码Error／detail（ordinary另验reason），不假定两个坏文件的解析顺序。

**实际顺序：** 先只添加上述两项测试，在原生产双门上构建并运行下列组合；7项／2 suites全部通过（35ms）。然后才改生产谓词，再构建并以完全相同filter运行；7项／2 suites全部通过（33ms）。两轮日志均实际出现两项新增测试的 `[ RUN ]` 与通过结果，不是空filter或历史引用。两轮命令均为：

```sh
cmake --build build --target OpusOptimaTests -j2
TMPDIR="$PWD/build" build/OpusOptimaTests --gtest_filter='IndependentFiniteReplay.TapeGatePreservesEntryStatusAndErrorOrder:IndependentStaticProduction.TapeGatePreservesEntryStatusAndErrorOrder:IndependentFiniteReplay.CurrentTapePositionGateDistinguishesBoundsFromInt32Extremes:IndependentFiniteReplay.TapeNormalizationPreservesActionsForPositiveAndNegativeStarts:IndependentFiniteReplay.EmptyTapeAnchorsPositiveButNotNegativeLocalStarts:IndependentStaticProduction.DifferentialTapeOffsetsNormalizeAgainstTheEarliestArm:IndependentStaticProduction.DifferentialIndependentArmsKeepOffsetsAndIgnoreHaltedTapeLength'
```

两轮构建退出0；保留既有dangling-else及缺失成员初始化编译警告，未借本任务修复。运行输出分别保留在仓库 `build/tape-gate-baseline.log`、`build/tape-gate-refactored.log`。新增文件fixture使用build内PID命名solution并由RAII删除；验证结束确认无残留 `*tape-gate-*.solution`。既有差分用例受同一build内TMPDIR约束。未观察到与设计不同的行为，不重跑冻结2254。

**剩余资源缺口：** 此次补齐上节历史缺口中的位置极值文件状态、旧生产专用入口分类及混合错误／文件截断证据；未测非B最大跨度40000的完整周期、40001密集唯一键／40002条目、重复raw超量、总5,120,128节点、最坏setup耗时或峰值内存，未进行5百万节点压力测试。有限通过不证明解析／setup资源有界；不增加文件大小限制、raw配额或deadline。保留上节历史失败和未实施事实，不把旧20项证据计为本次运行。完成即停，供监督验收。

## 尚未交付的全规则能力

首版实现了里程碑 2–6 的有界链路，不宣称整个原游戏验证器已经被替代：

- 生产墙体／导管的独立连续认证、Repeat／Repeating 输出、未展开 repeat/reset、disjoint 隐式链接、
  循环／歧义 track、未建立的协作曲线运动及 conduit cloning 仍不可用；
- 超时机器仍需性能工作，不能用延长预算或回退 native 冒充独立支持；
- 首版仍为 Linux/POSIX build-tree，未宣称 Windows、安装迁移或所有浮点平台通过；
- 默认发布仍是 legacy-gate；只有更多规则证据和认证范围达到明确门槛，才单独讨论默认切换。

这些限制是后续逐规则认证任务，而不是由本地 100% 求解或与参考裁判一致推导出的完备性结论。


#### A：wrapper 身份有界子包（2026-11-04）

`step_lifecycle.identity` 精确绑定 TypeDef03de 的 System.ValueType 基类、唯一无约束
泛型参数、TypeSpec0038/0039/00cb、bool/payload字段、d5f/d60/d61/d62/d64/d65的
完整IL/签名/flags/tiny header/无EH，以及九个MemberRef的唯一声明解析。
名称仅用于metadata解析，不赋予op_Implicit任何语义。d65调用d64，其正常分支将
原arg0和true交给d5f，后者将原arg2存入payload；d62正常分支读取payload。
空值拒绝/异常路径不返回结果，不证明调用必成功或终止。

工厂完整365字节、header、local signature和三个finally均绑定；local0仅在61 store，
从不取地址，因此callee即使修改/逸出对象也不替换caller局部引用。08b4@33保存
值类型wrapper，@36地址调用只读d60，46/51静态调用只接收bool/string而非wrapper地址；
@59正常提取同一payload。所需局部推理没有开放式callee effects。
仍保留跨getter稳定性、次入口先前更新/来源、并发/反射/运行时外部假设三个未证边。
本包不证明无逸出、无副作用或完整生命周期，不推进B–H。

本轮真实离线刷新JSON：fixture有RVA方法69、生产2897；仍partial-byte-evidence-only，
certification_eligible=false。新增20类反例覆盖六实现、MemberRef父项/歧义、字段owner、
泛型、实例化、local0覆盖/取址/绕过、EH/header/locals和中间调用签名。

本轮验证命令（不是复用旧通过）：

```sh
/usr/bin/python3 tests/functional_test/GameProductionEvidence.test.py EvidenceReaderTests.test_step_lifecycle_bounded_claims EvidenceReaderTests.test_step_lifecycle_mutations_rejected EvidenceReaderTests.test_step_wrapper_identity_adversarial_bindings
/usr/bin/python3 tests/functional_test/GameProductionEvidence.py '/home/edwards/.local/share/Steam/steamapps/common/Opus Magnum/Lightning.exe' --audit-current-colliders > tests/functional_test/CurrentColliderEvidence.identity.json && mv tests/functional_test/CurrentColliderEvidence.identity.json tests/functional_test/CurrentColliderEvidence.json
/usr/bin/python3 tests/functional_test/GameProductionEvidence.test.py EvidenceReaderTests.test_persisted_current_collider_evidence_matches_audit
/usr/bin/python3 tests/functional_test/GameProductionEvidence.test.py
```

定向3项、快照1项、完整137项全部通过（完整套13.648s）。另用/usr/bin/python3调用
ast.parse(Path(...).read_text())分别检查GameProductionEvidence.py及其.test.py，均通过；
json.loads读取真实JSON确认partial状态及认证false。Pylance工具不可用，未安装工具，未跑应用回归。
首次fixture缺TypeRef228，扩表修复；实际077c的implflags=8（NoInlining），改为绑定真实值；
旧factory-branch反例先触发unsupported opcode，完整身份门前移到窗口解码前修复。
修复后重跑上述验证全通过。下一建议仅界定08bd两getter间最小effect域，若需开放式effects
则记录最小未证边即停，不自行进入B–H。

#### A：getter 间最小 effect 域（2026-11-04，有界阻塞出口）

**命题及检查边界。** 仅问同一次 `060008bd` 正常执行中，@79 和 @97
两次 `060008b5` 返回的引用是否相同；@102 的1116发生在第二次读取之后，
不属于本命题的干扰区间。离线只读同一SHA256程序集；本轮没有重复wrapper身份证明。
检查08bd完整108字节、1117完整65字节、其九个不同直接callee（各至一层，
包括只在抛出路径使用的077c/09dc）、getter/setter和唯一setter调用方法08b4。
为列出直接字段引用及必要入口，遍历2897个有RVA的MethodDef的**编码token操作数**，
全部可解码；这是非递归引用清单，不是遍历调用图。未读取下表边界外callee的body。

**有限调用/写入表。** 方法号为MethodDef；偏移十进制；调用目标另标完整token。

| caller / 位置 | 已读的直接证据 | 对040004d8的结论 / 边界 |
|---|---|---|
| 08b5@0..6 | `02 7b d8040004 2a`；@1 ldfld040004d8 | 纯读取入口this，无调用、无EH |
| 08b6@0..7 | `02 03 7d d8040004 2a`；@2 stfld040004d8 | 唯一直接字段写入者；private实例方法（flags129），无调用、无EH |
| 08b4@64 | call060008b6；@59 wrapper提取的引用身份沿用上一包结论，不在本包重做栈证明 | 全2897方法中setter token仅此一处；不证明构造器不可对已有对象显式调用 |
| 08e8@170、12d7@780 | 均newobj060008b4 | 构造器token仅这两个直接引用，通常分配新owner；未追其caller/body，不证明反射、委托或重入不可达 |
| 08bd@79→@97 | @78 ldarg.0、@79 call08b5、@84 callvirt1117、@89 ldarg.0、@90 ldc.i4.0、@91 stfld040004d9、@96 ldarg.0、@97 call08b5 | 两getter入口为同一this；区间唯一caller直接store是**另一**字段040004d9；1117 effects仍须证明 |
| 1117@1→1111 | 1111七字节 `02 7b 36110004 2a` | 只读Sim.04001136，无调用/EH；返回false走下行抛出分支，正常采样需返回true |
| 1117@13→077c、@18→09dc | 077c长1871字节、54个调用/构造、2个EH clause；09dc八字节、@2 call0a00022b System.Exception..ctor | 错误字符串/异常构造不赋予纯度；1117@23 throw；仅在1117正常返回且无外部异常恢复时排除此支，异常清理effects不泛化为安全 |
| 1117@26→111c | receiver为入口Sim、arg1为false；111c长8026字节、9个EH clause；189 call、191 callvirt、83 newobj及9 ldftn | 正常采样最早未闭合effect根；44 stfld、8 stsfld、9 ldflda（均非040004d8），不能据此推出无副作用 |
| 1117@32→1122 | 355字节、1个EH clause；14 call、12 callvirt、1 newobj；无直接字段store/取址 | 仍有27个未证调用/异常清理出边；例如@65 callvirt060011dd、@91 callvirt060011f2，未据名称或未见store认定调用纯度 |
| 1117@38→1123 | 68字节、1个finally；@6 callvirt0a0005ae、@16 call0a0005af、@24 call0a0005b1、@44 call0a0005b3、@61 constrained callvirt0a000006 IDisposable.Dispose；@31/@37 stfld04000c2a/04000c2b | 只列出直接字段写入；枚举、提取、释放的effects未封闭，不以fixture或方法名代替证明 |
| 1117@45→110d | 七字节 `02 7b 30110004 2a` | 只读04001130，无调用/EH |
| 1117@52→110e | 八字节 `02 03 7d 30110004 2a` | 仅写Sim.04001130，无调用/EH |
| 1117@59→1112 | 八字节 `02 03 7d 36110004 2a` | 仅写Sim.04001136，无调用/EH |

**最小未证边与所需证据。** 正常true分支在1117@26调用111c。该callee最前沿即
`111c@0 newobj0a0004d6`（泛型MemberRef构造）、@7 call0600110c，随后@17
callvirt0a00007c GetEnumerator；@85 call06001126、@122 constrained
callvirt0a000006 Dispose、@129 call06001127也是明确未闭合出边。
本轮只识别这些调用点，不解析/递归进入这些实现。111c完整IL的SHA256为
`730e6a613e97e21a78bdf453c1f7b10a687e4fad1b4c225de666a85d8c004b57`，
1122为`fbb45a6b06347b39c01e81953cc306acb4ced20f8bbd6bf4afe6de4acff2b0a8`；
这是复查定位指纹，不是新增自动审计门。

关闭实际稳定性至少需要上述正常执行、类型初始化、委托/虚调用及finally释放出边对
**当前08bd入口owner的040004d8**的非干扰证据，或一个明确、可验收的封闭输入/调用域
及各出边effect契约；同时须排除外部线程、反射/unsafe/native写入和同owner重入写入。
“唯一setter只由构造器调用”并不足以证明这些条件：未证明构造器/间接调用不能在既有
owner上运行，也未证明callee无反射、别名或外部写入。不能仅把缺失契约当作前提加入
门就宣布稳定性已关闭。即使111c单独关闭，1122与1123的出边仍待独立证据。
因此本包仅关闭**直接token清单及caller局部无相关写入**，保留跨getter稳定性阻塞，
没有生成伪反例或把“可能干扰”声称为实际发生的反例；到此停止A扩张。

**验证及持久化。** 实际执行`/usr/bin/python3 - <<'PY' ... PY`只读探针：通过
`importlib.util.spec_from_file_location`载入现有GameProductionEvidence.py，以
`Metadata(固定Lightning.exe路径)`、`instructions`、`field_reference_inventory`
检查上述方法/引用，再用`hashlib.sha256`核对程序集及body。字段完整清单恰为
08b5@1 ldfld、08b6@2 stfld；2897/2897解码成功，setter入口1、构造器入口2；
未执行程序集。仅改本文及义务矩阵；不改脚本、测试或JSON，不刷新快照，沿用真实
JSON的partial/认证false状态而不声称本轮重新审计JSON。文档-only未跑测试/构建；
Pylance不可用，未安装或运行替代诊断。不跑应用、不启动游戏、不操作存档、不提交；
默认/profile/2–10–20秒预算未变。

**下一独立B包建议（不执行）。** 最小只做实体/配对的有限覆盖账本：以已有
111f/1122的采样候选和现有实体相关证据为输入，逐项列实体种类、候选生成/配对入口、
排除规则及对应方法/偏移，标出缺失种类与边界反例需求；不追A的111c调用闭包，
不扩到空间误差、四指标、成本或全游戏，也不把A阻塞当作已解除。交主会话验收后再定范围。

### B：有限实体/配对覆盖账本（首个准备包）

**命题与出口。** 本包关闭的是“有限候选入口及缺口已登记”，不是实体覆盖或阶段2资格。
复用 `GameProductionEvidence.py` 和 `CurrentColliderEvidence.json`，固定程序集SHA仍为
`ee4d1740609cbc09b5eb52244acc63da32f9e3cc280309049ceb7b708d20e1c5`。
下文偏移均为十进制IL字节偏移，方法/字段token为十六进制；不把JSON的插入**窗口起点**
当成newobj偏移。D=直接字节事实，R=非空、正常执行等前提下的局部规则解释，
U=未经证明的语义对应。类别编号、半径、相似坐标形状及混淆方法名不能单独证明实体身份。
A的wrapper引用身份证明仅到08b4@59；跨getter稳定性仍阻塞，不继续追111c调用闭包。

#### 1. 有限生成/采样入口索引

| 入口 | D：已有绑定及本轮读取事实 | R：可作的有限解释 | U/停止边界 |
|---|---|---|---|
| 111f（1518字节；body SHA256 `cdf648043cb42967de598182228416c68e6138223a70128137ef0f7b4b64416e`） | @7读步长04001138，@13跳condition，@18开始body，@1446更新、@1455条件、@1461回边；直接Add0a0005cf恰@265/500/656/748/931 | 正常表达式 `t=field; while(t<=1){body;t+=field;}`；五个语法插入点而非五个实体或固定数量 | 步长进度、迭代次数、所有上游成员及effect不由此证明 |
| 111f类别0两路径 | 第一来源链@37→11dd、@47→07f4，后@74..129取状态，@185→0d81后枚举；category0窗口@256，newobj113d@260，Add@265。另一来源@325→11dc、@330→11a4，filter1151委托@349，@366/371泛型调用；@416→0dc8、@421读040009f1并枚举；窗口@491，newobj@495，Add@500 | 两路径均用113d构造类别0/半径29；不能据此称“全部原子”或“wheel” | 11dd/07f4无IL body；11a4一层实现从040011a3枚举并@28调用0ddf，停止于该出边；泛型筛选/物化及上游分类未证明 |
| 111f类别1/2路径 | @553→11dc、@558读040011a3；filter1152委托@577，@594/599泛型调用；@635→11f2结果local25；类别1窗口@642、位置04000382@646、newobj@651、Add@656；@663读local24的04000bf7，再逐元素@696→11f2，类别2显式初始化@703..753、位置04000382@724、半径04001140=24、Add@748 | 类别1半径20；类别1/2取相同字段token，但来自不同求值/对象；04000bf7数组长度控制类别2枚举 | 同字段不证同位置或同实体；数组成员是否抓手、底座或wheel尚未建立。11dc无IL body；不追其实现/派发域 |
| 111f类别3路径 | @785→110c、@790→11a1、@795读04000c52、@802泛型调用；@813读0400048d、@837/842读0400044d/0400034f并枚举；显式初始化窗口@881..936，位置为静态04000449加local34经0810@907变换，半径04001141=20，Add@931 | 独立类别3参与下表局部pair；不是“类别3不碰撞” | 尚未证明是墙、底座或其他实体；110c/11a1/泛型/变换的跨层语义不展开 |
| 1122（355字节；body SHA256 `fbb45a6b06347b39c01e81953cc306acb4ced20f8bbd6bf4afe6de4acff2b0a8`） | @7同一步长，@13跳condition，body@18，更新@294、条件@303、回边@309；@19→110c、@24→11a4，@49→0df2且@54条件跳过；@91→11f2，@99→0dce，@107/134→0dc8，@112读040009e8、@121读04000c17，@254读04000bf7；@228调用1124；本方法无直接Add0a0005cf | 候选次采样/位置检查入口，不是已证第二个collider生成器；0dce一层仅读04000bf5 | 不把无直接Add提升为无间接生成/无副作用；1124一层仍有调用出边，未解析其下游或报告语义；1122亦不提供A非干扰契约 |
| 111c（只引用已有快照） | `radius15_insertions`七个类别0/半径15窗口@2197/2729/3352/3861/4726/5260/6254，对应Add@2236/2768/3391/3900/4765/5299/6293 | 既有生成相关候选 | 不重新深入、不证明“生成原子”、完整生成/移除时序或A稳定性 |

113d一层构造器@2/9写category/position，分支选择半径（category==1为20，其余29）；
TypeDef0499字段04001149/4a/4b分别承载类别/位置/半径，enum049d混淆名不作语义依据。
筛选复用JSON `generation_filters` 的既有完整body：1151@1→0df3，
1152@1→0df2、@9→0df3；正常输入表达式分别为
`Count(040009f1 of getter0dc8(arg1))>0`、
`Length(040009e7 of getter0dc8(arg1))!=0 OR 上述Count>0`（短路）。
0dc8@1读04000bf2；这些既有getter/谓词证据只支持字段级非空规则，
不支持把“有数组/集合”译成“有抓手/wheel”。本轮未为此泛化IL工具或递归展开新闭包。

#### 2. 四类实体的覆盖与最小反例需求

以下独立位置均为 `src/Verification/StaticProductionVerifier.cpp`，是**现有模型行为**，
不是已经证明与游戏编号相同。独立实现中的明确类型名不反向命名游戏混淆字段。

| 实体 | D/R：已有覆盖与独立入口 | U：游戏对应、排除与缺证 | 下一最小正/反例需求（本包未执行） |
|---|---|---|---|
| 原子 | 独立Spawn@768–790把输入加入atoms_；CertifiedMotions@838–868将before中每个原子加入瞬时占位。诊断@1040–1065有atom/atom、atom/base、atom/wheel；游戏111f两类别0构造路径及111c既有半径15窗口是候选，不是身份结论 | 缺两类别0来源的成员资格、是否重复/漏掉静止/运动/新生/持有原子；半径15与29不证生成时序。独立诊断不动pair后续sample跳过，不等于游戏局部category排除；REMOVED/持有语义不由编号给出 | 单个普通静止原子及相同原子进入运动/生成候选集合；同一身份同时落两来源时应去重还是双插入；REMOVED与有效原子仅差一个状态位的排除对照 |
| 底座 | 独立机械解析@500–528覆盖arm1/2/3/6、piston、baron；认证@847–852插入每个old.base；@855–859检查两底座交换；诊断@1026–1035 base/base、@1051–1057 atom/base。游戏类别1位置04000382/半径20为候选 | 本轮有界身份调查确认local24为TypeDef03ef，11f2返回TypeDef0256，但仍无040011a3成员资格及返回位置到独立base的身份桥（见下节）；不能因半径20认定类别1=底座。filter1152可能排除空集合对象，其业务正当性未知 | 固定一个上游候选、空/仅040009e7/仅040009f1/两者都有四状态，确认类别1生成次数；两重合候选与单候选排除对照。只证入口成员规则，不追发布生命周期 |
| wheel | 独立baron→wheel@507、grabbers=0@510；Wheels@756–766生成六个点并拒绝重叠，Spawn@768–776用这些点阻止输入生成；认证@850–852插入六点；诊断@973–986生成oldWheels，@1058–1065只检查atom/wheel | 类别0第二来源040009f1可能相关，但wheel身份、六成员、状态和重合处理均未证；诊断该块无独立wheel/base或wheel/wheel循环，不表示游戏允许这些pair。认证瞬时统一占位反而会拒绝这些重合；其相对游戏是否过强仍未知 | 单wheel六位置与普通arm同构输入；wheel/wheel、wheel/base、wheel/原子各一重合及一分离，明确“应排除”还是“应拒绝”；静止与旋转只登记分类，不求运动误差 |
| 抓手 | 独立GrabDrop@803–835：wheel跳过，G时按grip位置捕获atom Id，g释放并reset；Held@734–744按所持片段判定；@940–963用grips推导原子运动并检查协作；不另生成抓手碰撞圆。游戏111f类别2从04000bf7逐元素生成/半径24是候选，1122也读同字段 | 本轮确认04000bf7为同型03ef子对象数组，0dc6按描述数组040009e7创建并赋父引用/两个值字段；这仍不能证明它是抓手（见下节）。空抓手、持有同一片段、多抓手共同持有、wheel附件及排除时序都缺桥接；不得把“独立没有抓手圆”当作游戏排除已证 | 空/单元素04000bf7对照，分辨其实际类型与身份；相同位置空/持有两态、两个抓手持同一原子/不同原子、wheel与普通arm对照；只有证明映射后才讨论新增独立碰撞类 |

历史ordinary reference入口是 `ReferenceOuterLoopOracle.cjs:17–64` 的锁源提取与fixture拼接，
不是Lightning执行。其 `ReferenceOuterLoopOracleFixture.c:167–215` 覆盖六个固定类无序pair
及三种moving/fixed，`:216–239`覆盖五种moving-base pair，`:241–273`覆盖四种
WHEEL_ATOM/GRABBED_ONCE/GRABBED组合的静止/运动路径及静止REMOVED优先排除。
这些是已有synthetic one-cell board边界：chain路径在oracle中abort；标志不改该入口几何
不证明上游wheel/抓手已被正确分类、移除或全部配对。本包不重跑历史39组或137项。

#### 3. 局部配对表及排除含义

D：111f@978..1015前缀建立i=0、j=i+1并通过get_Item0a0005de取两项；
@1015..1395是已有 `pair_report`，@1395..1446后缀递增j/i并用get_Count0a0005e2限界。
@1031 distance与@1038/1045半径和比较，`bge.un`跳@1395；相等和unordered均跳过，
本包不作空间数值等价推导。只在通过此前筛选时，0..3域的局部报告选择如下：

| 无序类别对 | 局部报告选择 | IL证据 |
|---|---|---|
| 0/0 | 选中 | @1160/1169类别比较，@1176取04001123，@1181→0d3f |
| 1/1 | 选中 | @1195/1205类别比较，@1213取04001124，@1218→0d3f |
| 0/1 | 选中 | presence locals39/40组合，@1234取04001125、@1239→0d3f |
| 0/3 | 选中 | presence locals42/39组合，@1255取04001126、@1260→0d3f |
| 2/3 | 选中 | presence locals42/41组合，@1276取04001127、@1281→0d3f |
| 0/2、1/2、1/3、2/2、3/3 | 此局部report不选中 | 上述分支均不匹配；local43=false、local44=null，跳过report |

R：presence locals39..42分别表示“任一为0/1/2/3”；JSON绑定对称性，因此这五项选择
不因i/j交换改变。D：@1327/1351检查类别3，类别3的位置不加入report坐标列表；
@1390调用11e8；后缀@1422另调用1124处理i的位置。**类别3不列坐标≠不参与碰撞**，
“未选局部报告”≠在所有入口/所有时刻允许相交。未知类别不在此有限0..3表的证明域，
不能据本表认证。11e8、1124下游、间接插入、异常/回调和生成/移除时序均未封闭。

独立认证@838–868把原子、所有底座、wheel六点统一放入initialPositions，不按这些
游戏编号排除；诊断@965–1068则只显式遍历base/base、atom/atom、atom/base、atom/wheel，
在sample>0时略过双方静止pair。两者都不能直接替换上表。最小pair反例需求是
在**已建立来源身份**后，对十个无序编号对各给两项、交换顺序，并对“选中/未选中”
各保留一例；再将同身份重复插入与两身份同位置分开。未建立身份前仅能测试编号规则，
不能宣布wheel/抓手排除正确。

#### 4. 本轮实际探针、验证及下一有界建议

实际在项目根运行下列只读命令（未执行程序集、未覆盖JSON）：

```sh
python3 -B - <<'PY'
import importlib.util, json, hashlib
s = importlib.util.spec_from_file_location('e', 'tests/functional_test/GameProductionEvidence.py')
e = importlib.util.module_from_spec(s); s.loader.exec_module(e)
p = '/home/edwards/.local/share/Steam/steamapps/common/Opus Magnum/Lightning.exe'
m = e.Metadata(p); a = e.current_collider_audit(m)
print('audit_equal', a == json.load(open('tests/functional_test/CurrentColliderEvidence.json')))
print('sha256', hashlib.sha256(open(p, 'rb').read()).hexdigest())
print('status', a['status'], 'eligible', a['certification_eligible'])
for mid in [0x111f, 0x1122]:
    b = m.body(mid); print(f'{mid:04x}', len(b), hashlib.sha256(b).hexdigest())
PY
```

结果exit0：`audit_equal True`，SHA匹配，`partial-byte-evidence-only`、eligible `False`；
两body长度/指纹如上。另实际用同一Metadata、`instructions(body)`列出111f/1122的
非空token指令偏移，用 `references(mid)` 和 `body(mid)`检查必要一层callee
11dc/11dd/11a4/07f2/07f4/0dce/0dc8/0df2/0df3/1151/1152/113d/1124；
11dc/11dd/07f2/07f4返回空body，按边界记录而非补造实现。较大的首次输出被截断，
后续用有限token清单、短body及指定窗口重读，不以截断内容充当证据。
未发现需要修正的直接耦合证据错误；脚本、JSON及生产代码均不改。
这是文档-only包，不跑构建、测试或应用回归，不借历史通过结果；仅做文档diff检查。
不改profile/legacy-gate或2/10/20秒预算，不启动游戏、不操作存档、不提交。

**本次有界实现：111f 类别1/2正常局部生成（条件性，不是游戏动态验证）。**

`GameProductionEvidence.py::category12_local_rule` 精确识别547..784窗口（547补入入口receiver，
784为779长回边后的边界），复用IL解码与分支解析；拒绝窗口改字节、外部跳入内部和重叠EH。
从初始化0、步进1、signed `< conv.i4(array.Length)` 的两个索引循环提取递推，
在下述前提下，已筛选候选数组的每个**出现位置**i执行一次category1 Add@656；
该次捕获的04000bf7数组每个索引j执行一次category2 Add@748。
故category1=N，category2=Σ长度；这是有限循环模板的条件性推导，不是静态Add站点数冒充运行次数。
category1位置仅绑定该次635返回值的04000382、半径来自113d category==1的20字面量；
category2位置仅绑定该次696返回值的04000382、半径来自04001140=24。
两次位置调用、不同候选及同token字段读取不合并为同一对象。相同引用重复索引仍分别计数，
同坐标不同身份也不去重。窗口门只识别这个闭合模板，不推广为通用IL解释器。

**所有计数前提（R，尚不是已证callee effects）：** 正常从547进入并退出784；
调用、分配和Add均正常返回且无重入；Where/ToArray提供有限有序的合格出现位置数组，
缓存非空委托确实绑定1152；1152的首次getter之e7长度非零或另一次getter之f1 Count>0
按短路求值，不假定两次getter同对象；数组及相关receiver非空，数组长度≤Int32.MaxValue；
未读取成员不被callee改变，捕获引用、循环locals和目标local2 list不被外部effects改写；
每次显式Add恰好追加一个值，已审计半径值保持不变。
本包**没有**关闭11dc返回域、缓存委托历史、Where/ToArray语义绑定、11f2 effects及list外部修改；
它们是从本局部条件定理升级实际游戏计数的最小缺边，不继续开放式追踪。
一层离线字段签名04000bf7=`061d128fbc`、04000382=`06119388`仅说明数组/位置类型，
不能证明实体身份；仍只称类别1/2，不称底座/抓手已确认。A跨getter稳定性仍阻塞。

`category12_synthetic_trace` 是显式供应已筛选候选、每次返回位置及成员快照的**合成递推测试器**，
不执行游戏、不调用callee。新增三项测试从窗口提取的初值/步进/类别运行递推，覆盖数组0/1/2、
不同候选、重复成员及候选、相同坐标不同身份；filter测试从1152模板读取短路真分支，
覆盖仅e7、仅f1、均空以及两个getter来自不同对象的反例；窗口篡改/插入点/来源/回边/EH拒绝测试
验证fail-closed。半径及来源事件由既有构造器/静态字段门和精确窗口共同约束，
合成测试通过不表示实际游戏行为通过。

实际命令与结果（本包，无历史137项借用）：
- `python3 -B -I tests/functional_test/GameProductionEvidence.test.py EvidenceReaderTests.test_category12_local_counts_and_sources EvidenceReaderTests.test_category12_local_rule_rejects_mutations`：2项通过。
- `python3 -B -I tests/functional_test/GameProductionEvidence.test.py EvidenceReaderTests.test_category12_synthetic_filter_observations`：1项通过。
- `python3 -B tests/functional_test/GameProductionEvidence.py '/home/edwards/.local/share/Steam/steamapps/common/Opus Magnum/Lightning.exe' --audit-current-colliders > tests/functional_test/CurrentColliderEvidence.json`：离线刷新成功。
- `python3 -B -I tests/functional_test/GameProductionEvidence.test.py EvidenceReaderTests.test_persisted_current_collider_evidence_matches_audit`：1项快照通过。
- 最终再次离线刷新后，`python3 -B -I tests/functional_test/GameProductionEvidence.test.py`：140项通过，12.940秒，包括新三项与快照。
- 无测试失败；审阅中修正category1来源描述为构造器20字面量，随后重新刷新并完整测试。
  一次文本搜索因空type参数失败，改用有界读取；不影响验证。Pylance不可用，未声称诊断通过。

真实JSON新增`category12_local_generation`，保持`partial-byte-evidence-only`及
`certification_eligible=false`；fixture补齐真实局部窗口，未引入真实游戏执行。
未构建/运行全应用、未启动游戏、未碰存档、预算、profile或legacy-gate，未提交。
**下一有界建议（已被本轮监督指令替代）：** 原建议是缓存委托/Where metadata门；本轮不实施该建议，改做下述B身份调查。

### B：有界身份调查（本轮；文档-only阻塞出口）

**目标与结论。** 调查111f的local24、04000bf7元素、11f2返回位置能否对应底座/抓手/其它实体。
上包140项只验证局部模板与合成递推，**不是实际游戏计数或实体对应**。
本轮能关闭类型及直接构造关联的字节事实，不能关闭任何实体命名子命题；保持类别1/2编号结论并停止。
未新增门、测试、JSON或公共audit，未进入111c闭包，未做完整alias分析。
以下程序集仍为账本固定SHA；所有偏移为十进制IL字节偏移。

| 对象/边 | D：本轮直接证据 | R：有限解释 | U：最小缺失关联/停止点 |
|---|---|---|---|
| 111f local24 | 040011a3签名`061512813d01128fbc`的元素类型是TypeDef03ef；111f@614..619从筛选数组取该元素到local24；04000bf7归属03ef，签名`061d128fbc`即03ef[] | 候选与附件元素是**同一声明类型**，不是两个按类型天然区分的“底座/抓手”类 | 缺040011a3中03ef实例的业务成员资格及其描述对象到明确机制种类的绑定；同型不证相同角色、相同实例或全部覆盖 |
| 04000bf7直接赋值入口 | 全部可解码MethodDef中对该字段的直接stfld/stsfld仅0dc6@85；03ef的`.ctor`0dc6@69..85按构造参数TypeDef0379的040009e7.Length创建03ef[]；@94..177每索引以静态04000962、父04000bfc值递归newobj0dc6@105，local2为新子对象；@124经0dcb写子04000bf3，@152经0dcd写子04000bf4，@164写子04000bf6，@170..177存入数组 | 正常构造路径可见“描述数组驱动的同型子对象、父引用及两值字段”：0dc9@2写描述04000bf2；0dd5@2写布尔04000bfc；0dcb/0dcd@2分别写04000bf3/4；0dc8仅读bf2，0dd4仅读bfc。这些小callee均只读必要一层 | 040009e7是TypeDef03c1值数组，但其索引含义未绑定抓手布局；静态04000962描述的机制身份、子值字段的业务含义均未证。唯一直接字段写入口不排除数组元素后改、别名、反射或外部代码，不作运行时不变式 |
| 11f2返回位置 | 方法签名`2003128958128fbc1193880c`：receiver为04b6、输入03ef/04e2/float32、返回0256；0256字段04000382为04e2值。@0..6以输入03ef调用0ddc得local0；@7..35以静态04000449、11dd→07f2结果的04000c16及0600080f产生local1；@36..40调用11f3，@45..50以**原输入**作key调用0a00066c get_Item，@51返回 | 11f3@0创建字典、@11调用11f4填充、@16返回字典；TypeSpec01e0=`151280f902128fbc128958`绑定Dictionary<03ef,0256>。0ddc@0..30沿04000bf6的nullable式父链取值直至无父；可解释为由关联链上端启动、再按原输入选结果的计算形状 | 11f3的一层已经到11f4出边，**不再深入11f4**；11dd与07f2无IL body。缺字典中对应key的0256如何写04000382、该值代表本体中心/附件端点/其它点的规则。因此11f2不是已证“底座位置getter”，更未证纯度、稳定性或两次返回相等 |
| 锁定reference端 | 同revision `a88d12d8d100784aa525ee90259255b869bcba38`、同双SHA的collision.c:555–564由solution->arms[i].position插入静止base，:591–604由position/movement/progress插入运动base；sim.h:191–215声明mechanism.type/position/direction_u/v/movement。collision.c:565–569墙体也用armBaseRadius | reference端base身份由**来源字段和分支**支持，不只半径；已有outer-loop fixture只人工设置这些字段（:167–215），不读取生产03ef对象 | 没有生产03ef/0379实例→reference mechanism.type/position的键或构造对应表，也没有040009e7索引→明确抓手的关联。两个源码各自有位置/父子形状不足以组成桥 |

0dc6完整body SHA256=`64788d6df2c2d2e916641009e32c240abbe9723f612c00b8e994502362af6c65`；
11f2=`d7ca6ea7658a218e3a965b32a681267c04540b59cdff36c48a8a2ca4638317b7`；
11f3=`0a9d634d32583b77f895b53d5ad7cd1bb4ef8e96ef64b35a813ecaba05e633f9`。
混淆名字、半径、同型递归、旋转调用名和相似几何都没有用来命名实体。
父链解释不包含终止性或无环证明；构造中新数组关系不提升为采样时的完整对象图。

**完整只读复现探针**（仓库根执行；无游戏加载、无文件写入；网络只获取既有锁定reference原文）：

```sh
python3 -B - <<'PY'
import hashlib, importlib.util, urllib.request
s = importlib.util.spec_from_file_location('e', 'tests/functional_test/GameProductionEvidence.py')
e = importlib.util.module_from_spec(s); s.loader.exec_module(e)
m = e.Metadata('/home/edwards/.local/share/Steam/steamapps/common/Opus Magnum/Lightning.exe')
assert hashlib.sha256(m.data).hexdigest() == 'ee4d1740609cbc09b5eb52244acc63da32f9e3cc280309049ceb7b708d20e1c5'
for typ in (0x256, 0x379, 0x3c1, 0x3ef, 0x4b6, 0x4e2):
    r, n = m.rows[2][typ-1:typ+1]
    print('TYPE', hex(typ), 'extends', hex(r[3]), 'methods', r[5], n[5]-1)
    for fid in range(r[4], n[4]):
        print('FIELD', hex(0x04000000 | fid), m.blob(m.rows[4][fid-1][2]).hex())
for mid in (0x111f, 0xdc6, 0xdc8, 0xdc9, 0xdd5, 0xdcb, 0xdcd, 0xdd4,
            0xddc, 0x11f2, 0x11f3, 0x11dd, 0x7f2, 0x80f):
    r = m.rows[6][mid-1]; b = m.body(mid)
    print('METHOD', hex(mid), 'owner', hex(m.owner[mid]), 'sig', m.blob(r[4]).hex(),
          'RVA', r[0], 'length', len(b), 'sha256', hashlib.sha256(b).hexdigest())
    for offset, opcode, token in e.instructions(b):
        if mid != 0x111f or 547 <= offset < 784:
            print(offset, hex(opcode), None if token is None else hex(token))
for mid, row in enumerate(m.rows[6], 1):
    if row[0]:
        for offset, opcode, token in e.instructions(m.body(mid)):
            if token == 0x04000bf7 and opcode in (0x7d, 0x80):
                print('DIRECT_BF7_STORE', hex(mid), offset)
for token in (0x0a00066c, 0x0a00066d):
    r = m.rows[10][(token & 0xffffff)-1]
    print('MEMBER', hex(token), hex(r[0]), m.string(r[1]), m.blob(r[2]).hex())
print('TYPESPEC_01e0', m.blob(m.rows[27][0x1e0-1][0]).hex())
root = 'https://raw.githubusercontent.com/ianh/omsim/a88d12d8d100784aa525ee90259255b869bcba38/'
for name, sha, ranges in (
    ('collision.c', 'a2df55f5a12ec8a107c6408e44d7e4373447b6741a8f59259829c2f6f96558b5', ((555,570),(591,605))),
    ('sim.h', '198a3c4a74bc94fe1595e0159881797d450f387e92598191b77cbec3e8864a22', ((191,215),))):
    b = urllib.request.urlopen(root + name, timeout=30).read()
    assert hashlib.sha256(b).hexdigest() == sha
    print(name, 'SHA256', sha); lines = b.decode().splitlines()
    for start, end in ranges:
        for i in range(start-1, end): print(i+1, lines[i])
PY
```

**本轮结果与停止条件。** 上述探针组成部分已离线读取成功，reference下载两次均通过原双SHA；
完整复现命令亦本轮执行成功（exit 0）。本地reference源码未随仓库提供，故只读取得同revision原文后内存验SHA，未下载其它版本。
没有执行测试/构建（文档-only），没有借用历史140项作本轮通过结果；Pylance未提供，诊断不可用。
A跨getter稳定性、实际计数、实体全覆盖、阶段2资格均仍未解决；legacy-gate/profile及2/10/20秒预算不变。
**后续有界包的停止决策见下节。** 原建议已由最后一次锚点调查执行；未闭合身份，不再建议继续深入11f4或其它callee。

### B：最后一个业务身份锚点（文档-only停止出口）

**回答：不能在本包边界内闭合。** 复用`initialization_array_publication`后，取得静态描述04000962的直接初始化、一个序列化类型查询入口060011bb，以及一个候选关联入口060011a6的字节证据；但没有把明确业务类型ID的实际值绑定到该描述，更没有绑定040009e7索引角色。以下为新增矩阵，不替代上节的A/计数阻塞；全部偏移为十进制IL字节偏移。

| 命题 | 已证D（固定程序集的字节/metadata） | 推断R（不升级为认证） | 未证U / 精确缺边 |
|---|---|---|---|
| 一个生产0379描述 | 06000a48@1448 newobj06000b51；@1454压入整数-757873037，@1459调用0600077c，@1464写040009e1；@1475、1480经0600077c/06000759后写040009e2@1485；@1518发布到04000962。既有publication模板验证成功：@6530起建0379[29]，@6612取04000962，@6617存索引9，@6789发布04000976 | 040009e1是查询所用的ID候选字段，不是因为其混淆名字；初始化中另一个文本/LocString路径不能充当类型映射 | 未得到0600077c(-757873037)的实际业务ID值及其与源锁定机制类别的关联；未执行该方法，也不继续解析其闭包。索引9是**描述注册数组索引**，不是e7索引 |
| 该描述的040009e7 | 06000b51@22..29创建03c1[0]并写e7；06000a48@1448..1518给此新描述的直接初始化没有再写e7。全2897个有RVA方法均解码成功：e7直接store仅a48@592/786/992/1234/1423及b51@29；前五处不在962的新对象初始化段 | 对此直接构造/发布片段，可说962描述的初始e7为空，故没有可在该片段中命名的索引；0dc6仍只是按描述长度建同型子数组 | 不能由空数组推断任意采样时刻仍空，或把其它描述的e7索引命名为抓手。缺“某明确ID描述的某索引→业务角色”实际关联；直接stfld清单不排除数组修改、alias或外部行为 |
| 一个明确的业务ID入口 | 060011bb@306调用System.IO.BinaryReader.ReadString（MemberRef0a000506，签名20000e），@311把结果传06000a4a；后者@8存查询参数到04000977，@13读注册数组04000976。该方法、既有query绑定及publication可复用，不增加delegate/Where门 | 这是序列化字符串到描述查询的实际关联入口，优于单独观察业务字符串。11bb@318的Maybe式调用后，@342..345用local15及false构造03ef，@389转候选关联入口 | 尚未证明输入序列化ID等于962的040009e1实际值；Maybe分支的实际命中与local15实例身份未建立。未据既有局部query门声称完整选择语义/唯一性/生命周期，也没有读取任何存档来填补这些事实 |
| 一个03ef候选关联入口 | 060011a6@0..2先把receiver及arg1交060011a7；@7..9把arg1/arg2交06000dec；@14..21读取receiver.040011a3后以arg1调用MemberRef0a0004d7 Add（签名2001011300）。11bb@389调用此入口；040011a3唯一直接字段store仍119f@17 | 若前置调用正常返回，这段把同一传入对象交给候选集合Add，构成局部插入形状；不是仅凭同型猜身份 | 不能证明该arg1就是962描述的03ef实例，更不能证明采样111f时仍为同一成员。060011a7/06000dec的效果、异常和移除、集合别名/跨getter顺序仍未知；本包**不深入**它们，也不审计11f4 |

**范围及出口。** 未闭合完整命题，因此没有添加精确门、测试、JSON或公共audit，也不修改运行行为；只更新现有文档。业务字符串、半径、数组形状及混淆名字均不作为类型命名证据。0dc6构造与11f2按原key返回的既有事实不变，仍不能称“底座/抓手”。**明确建议暂停B身份链，转C中独立的数值/面积义务分解**：先分开连续碰撞数值误差/适用域与离散七格面积/四指标规则，把需要实体身份的映射标为阻塞，不借C绕过B。不是再深入一个callee的授权。本包完成即停止等监督验收；A跨getter生命周期、实际生成计数、实体全覆盖与阶段2资格继续阻塞，legacy-gate/profile及2/10/20秒预算不变。

**本包完整只读复现探针**（仓库根执行，唯一外部输入为离线Lightning.exe；无游戏、网络、存档或程序集执行；没有写文件）：

```sh
python3 -B - <<'PY'
import hashlib, importlib.util
s = importlib.util.spec_from_file_location('e', 'tests/functional_test/GameProductionEvidence.py')
e = importlib.util.module_from_spec(s); s.loader.exec_module(e)
m = e.Metadata('/home/edwards/.local/share/Steam/steamapps/common/Opus Magnum/Lightning.exe')
assert hashlib.sha256(m.data).hexdigest() == 'ee4d1740609cbc09b5eb52244acc63da32f9e3cc280309049ceb7b708d20e1c5'
p = e.initialization_array_publication(m.body(0xa48))
assert p['field_tokens'][9] == '04000962'
print('PUBLICATION', p['start_offset'], p['publication_offset'], p['template_index'])
print('EXISTING_QUERY_BINDINGS', e.initialization_query_bindings(m)['scope'])
r = e.field_reference_inventory(m, (0x04000962, 0x040009e7, 0x040011a3))
print('INVENTORY', r['decoded_methods'], r['complete'], r['decode_failures'])
for f in ('04000962', '040009e7', '040011a3'):
 print('STORES', f, [(x['method_token'], x['offset']) for x in r['references']
                       if x['field_token'] == f and x['opcode'] in ('007d', '0080')])
for mid, ranges in ((0xa48, ((1448,1523),(6530,6537),(6608,6618),(6789,6799))),
                    (0xb51, ((22,34),)), (0xa4a, ((0,36),)),
                    (0x11bb, ((305,400),)), (0x11a6, ((0,27),))):
 b = m.body(mid)
 print('METHOD', f'0600{mid:04x}', 'owner', hex(m.owner[mid]), 'length', len(b),
       'sha256', hashlib.sha256(b).hexdigest())
 for o, op, t in e.instructions(b):
  if any(lo <= o < hi for lo, hi in ranges):
   print(o, f'{op:04x}', '' if t is None else f'{t:08x}')
print('DESCRIPTOR_INIT_BYTES', m.body(0xa48)[1448:1523].hex())
print('ID_DECODER_INPUT', int.from_bytes(m.body(0xa48)[1455:1459], 'little', signed=True))
for token in (0x0a000506, 0x0a0004d7):
 row = m.rows[10][(token & 0xffffff)-1]
 print('MEMBER', hex(token), hex(row[0]), m.string(row[1]), m.blob(row[2]).hex())
PY
```

**本包结果。** 完整命令执行exit 0：PUBLICATION=`6530 6789 9`；INVENTORY=`2897 True []`；
962直接store仅`06000a48@1518`；e7直接store为上表六处；11a3直接store仅`0600119f@17`；ID_DECODER_INPUT=`-757873037`。
方法body的`length / SHA256`如下（不是业务语义证明）：

- 06000a48：6800 / `2977b87c820b4c753cb90c5ffbb3de7d88b7bdaa29be75e5c3e46dc4c0ca41a5`
- 06000b51：83 / `0ac268ddfcf7de1344355b886bd1cdc0f24baa33128245911080954c00722e13`
- 06000a4a：36 / `222bcbeb8d4f4fe6ff974be79890d64020e2c8bb9bd4b5df058ee4451c660136`
- 060011bb：893 / `a48598d23be67ff58f5e355ac6070580988d0d662eead4c240e6b609de49735d`
- 060011a6：27 / `bff587a124f9acce5b3cef15e10543f8f3adfc61e8216bd20578f099f774a23f`

文档-only，未运行构建、测试、快照或完整EvidenceReader；这些均不作为本包通过结果，不借用旧140项。
Pylance工具不可用，诊断未运行。既有未提交/未跟踪工作原地保留，无提交、游戏启动或存档操作。

## E／阶段3：开轨道首个有界离线准备包（未开放）

**范围与结论：** 仅静态追踪 `ParseSolutionFile → ParseSolutionObject → CheckScope → Motions`，旁读直接 metrics 消费点和必要测试。磁带最小行为保持重构已完成，本包不继续磁带，不修改生产代码、profile、legacy-gate 或 2/10/20 秒预算。收敛计划 `shimmying-jingling-hollerith.md:28–34,72–76` 允许 E 提前准备，但 A/B/D 仍阻塞正式扩展；历史 tracks **158** 只是历史不可用分类，本包没有新增入域／通过／超时统计。以下都是**源码推导，不是运行验证**，未确认新的现行 bug。

### 当前拓扑／资源矩阵

位置均为本包读取时的文件行号；`SP` 指 `src/Verification/StaticProductionVerifier.cpp`。

| 层／命题 | 现行保护与准确分类 | 剩余证据／成本门 |
| --- | --- | --- |
| parse 结构及分配 | `src/IO/BinaryParser.cpp:373–397`：轨道 count 为 i32，负数或大于 `Remaining()/8` 抛 ParseError；逐对读 i32 offset，最后另读必需 i32 尾字。该结构门未预留尾字，尾字不足由实际读取拒绝。`453–478` 对象数按剩余字节/30 检查后 reserve；**不是**1024部件或256格配额 | parse 不检查相邻、唯一、环、绑定或坐标 profile。count=0 可解码，后由 scope 判 Unavailable；字节完整的257格也先解析。`ReadAllBytes:233–256` 整文件读取，仅可表示性界，无固定文件字节上限；解析时原始字节和 objects/bodyData 共驻 |
| 入口共用与错误映射 | `SP:1344–1436,1438–1528`：`VerifyStaticProduction` 用 `(ordinary=false,certify=false)`，`ReplayFiniteMachine` 用 `(true,false)`，`VerifyIndependent` 用 `(true,true)`，共用 CheckScope/Motions。Unsupported→Unavailable，Rejected→Rejected，invalid_argument 在认证入口→Unavailable、非认证入口→Error，其余 std::exception→Error；文件解码失败为 Error，独立文件入口 reason=`independent-file-error`。独立 scope 拒绝/不可用分别为 `independent-machine-rejected` / `independent-profile-uncertified` | 先决门不可忽略：旧生产入口不接普通 puzzle；独立认证入口 production 输入先在 `SP:425–426` 返回 Unavailable，不到轨道检查。ReplayFiniteMachine 只报告执行 outcome，不授予认证 |
| 长度／部件／臂 | `IndependentCapabilities.hpp:19–26` 为1024 parts、128 arms、坐标1024；`SP:486–506,646–650` 实施部件和臂数门。每轨 **1…256格** 是 SP 内硬编码，**不是** profile 独立字段；0、负数、257等均 Unsupported，先于逐格坐标检查。轨道权限 bit3、非空 tape 则 Rejected | 所有轨道均在 scope 检查，即使没有臂使用。不能只改 profile 声称放宽轨长，也不提议任何新数值上限 |
| 坐标／算术 | `SP:491–500,653–659`：part origin 的 q、r 及 hex Radius 均≤1024；普通入口越界 Unsupported，旧生产路径 Rejected（rotation非法也走同一门）。offset 仅 q、r 各在±1024，否则 Rejected；**没有 offset Radius≤1024 或 world Radius≤1024 的门**。position=origin+offset，忽略序列化 rotation；普通 Room 恒0（`SP:338–345`），不另限 world 格 | 当前可推 world 的 q/r∈[-2048,2048]、Radius≤3072；两 world 点差 q/r≤4096、和≤8192，`Add/Sub/Radius`（`SP:30–45`）在此路径无 int 溢出风险。此推导不是全程序坐标证明。生产 Room 检查存在但本包不扩审墙体 |
| 相邻／唯一／连通／顺序 | `SP:660–665` 逐格要求与前格 hex距离1（否则 Rejected），再线性 find 先前格（重复则 Unsupported）。因此接收的序列唯一、按文件顺序连通，不排序、不按几何重建。相邻重复首先触发距离0的 Rejected，非相邻重复若前一步合法则触发 looping Unavailable | `SP:666–667` 仅在长度>2时检查首尾相邻并返回 looping Unavailable；**没有非连续内部格相邻（弦）、整张几何图度数／分支检查**。因此“序列开路径”不自动等于“几何诱导图无环／无歧义”；未查游戏图语义，不将此缺口宣称真实bug |
| 跨轨重叠／臂绑定／运动 | `SP:918–937` 仅当本周期动作为 A/a，按**旧底座世界坐标**遍历所有轨道 find；找不到→Rejected `track instruction without a track`；命中两条→Unsupported `ambiguous overlapping track reset/motion`。唯一命中后 A索引+1、a索引-1，端点钳制为no-op，底座和物料用同一translation；不是按尾字／臂号永久绑定 | scope 不全局拒绝轨道重叠，也不预绑定臂；未执行 A/a 的重叠、未到达的未来歧义不会仅因此报错。不能把延迟查找说成全机拓扑证明，亦不涉及 reset 行为。本包不改变此策略 |
| metrics 不是拓扑门 | `SP:135–137` 先完成 scope 才初始化认证 metrics；`IndependentMetrics.cpp:202–213` 自身允许count≤4096、检查body长度、逐格计5成本和世界足迹（track rotation=0），不验证相邻／唯一／臂绑定 | 4096是 metrics 直接API界，不能当 replay 长度界或新增覆盖。独立认证仍须其余语义／面积／连续认证资格，不把通过轨道scope当Passed |

### 静态复杂度与总资源边界（非耗时实测）

令 T 为轨道数，Lᵢ为第 i 条长度，S=ΣLᵢ，A为机械臂数，Aₘ为本周期执行 A/a 的臂数。在**完成 scope 的当前域**，T+A≤1024，A≤128，Lᵢ≤256，所以 S≤262144；若A=128，S≤896×256=229376（还未扣除必需端口等其他部件）。这是路径存储元素上界，不是全进程内存字节配额。

- `SP:651–668` 每轨 vector 保留 O(Lᵢ) Cell，线性去重累计至多 Lᵢ(Lᵢ−1)/2 次既有格比较；普通输入 scope 轨道部分 O(ΣLᵢ²)，额外路径存储 O(S+T)。全1024条256格的比较上界为 **33,423,360**；生产 Room 另带房间查询因子，本包不展开。
- `SP:921–928` 每次 A/a 扫描所有路径，最坏 O(S+T)，每周期轨道查找 O(Aₘ(S+T))，全C周期 O(C·A(S+T))；没有轨道索引缓存。按A=128及S≤229376，可得每周期最多 **29,360,128** 次格比较的宽松上界（其他部件还会降低它）。这不是整次 Motions/验证器复杂度，未计原子、碰撞及metrics，不能据此声称在预算内完成。
- 原始 bodyData 的 O(S) 存储与 replay paths 共驻；metrics 足迹另存集合（逐格计入集合的实现见 `IndependentMetrics.cpp:202–213`）。畸形／越界输入在 scope 前可以有任意更大的、受文件字节结构约束的 raw对象和格数，或直接API携带多余bodyData；因此不能将 S 上界推广为 parse 前固定总内存界。整文件解析工作随输入字节增长，完整文件缓冲与解析结果可共驻。
- `SP:326–329,1340–1341` deadline为2秒，建立在解析之后；`Run:135–147` setup之后才到周期 Budget。轨道逐格和逐臂扫描无循环内 Budget。故当前静态元素界不证明 setup 最坏墙钟／内存成本，也不能把“2秒预算”说成解析及每个内循环均可即时打断。D的有效双方Passed／四指标一致成本配对仍缺，不做性能收益声明。

### 已有测试证据与唯一最小下一目标

仅阅读，**本包未执行这些测试**：`StaticProductionVerifierTest.cpp:329–357` 有两格轨道端点no-op及忽略序列化rotation的差分用例；`457–464` 和 `IndependentFiniteReplayTest.cpp:341–355` 有底座同时交叉拒绝；后者 `357–371` 有首尾相邻三格环Unavailable；`StaticProductionVerifierTest.cpp:586–591` 有offset INT_MAX拒绝；`IndependentMetricsTest.cpp:606–646` 有线性轨道运动指标差分。所读这些文件没有1/256/257格的直接长度边界测试，未将已有用例名称或断言当成本次通过结果。

**唯一建议下一有界目标（待主会话批准）：补当前长度门的表驱动边界测试，不修改生产实现。** 使用现有 Ordinary/Machine fixture，在远离机器的起点放直线轨道，count依次为0、1、256、257，offset=(i,0)，所有格坐标保持当前范围；以 `ReplayFiniteMachine` 对照无轨道基线：1/256应保留基线完成结果，0/257应Unavailable且detail精确为 `independent track bound exceeded`。只验证现行scope边界，不评估长轨性能、不统计新覆盖、不顺带添加泛化图框架或开放政策。此目标填补明确的现行边界证据缺口；内部弦／多轨重叠的全局规则证据与有效成本配对继续列门，不随此测试关闭。

本包实际仅执行读取／定位命令：`git status --short`（并对三个目标路径复查状态）、`sed -n` 读取收敛计划及上述源码区间、`rg -n 'track|Track|轨道' tests/unit_test/{StaticProductionVerifierTest,IndependentFiniteReplayTest}.cpp src/IO/BinaryParser.cpp src/Verification/IndependentVerifier.hpp`、对 `SolutionParseTest.cpp` / `IndependentMetricsTest.cpp` 定位直接测试，以及工具 view/grep/glob。早期若干组合定位命令因未存在的 `VerificationCodec.*`、`WorkerProtocolTest.cpp` / `Core/HexCoord.hpp` 路径或空type参数报错，随后改读实际文件；不计作验证。变更仅追加本节文档；无探针、构建、测试、游戏／存档操作、提交或委派。出口为**具有具体缺失证据的阻塞结论＋一个最小现行规则测试目标**，停止供监督验收。


### 现行轨长0／1／256／257边界回归（2026-11-04，离线完成）

本包落实上节待执行目标，仅新增 `IndependentFiniteReplay.TrackLengthBoundPreservesEntryOutcomeAndOriginalDetail` 与本记录；生产实现、legacy-gate、profile、2／10／20秒预算不变。先核对 `IndependentFiniteReplayTest.cpp`、`StaticProductionVerifierTest.cpp`、`IndependentMetricsTest.cpp`、`SolutionParseTest.cpp`：已有两格端点no-op／rotation、交叉底座、三格首尾闭环和线性指标用例，不重复新增；此前没有本表的直接长度边界回归。

复用真实 `Ordinary()/Machine()` 与公开 `ReplayFiniteMachine`／`VerifyIndependent`，轨道起点(10,10)，offset=(i,0)，无轨道基线先断言Completed。每格同时验证内存对象及 `BinaryWriter::WriteSolutionFile → BinaryParser::ParseSolutionFile` 往返对象；另断言解码轨道body与原始body相等，避免文件入口漏掉count或尾字。这里文件路径覆盖实际solution编解码后调用公开验证入口，**不是**新增 `VerifyIndependentFiles` 全文件wrapper测试。

| count | replay断言 | 独立认证入口断言 |
| --- | --- | --- |
| 0、257 | Unavailable；原detail严格等于 `independent track bound exceeded` | Unavailable；同一原detail；reason=`independent-profile-uncertified` |
| 1、256 | 保持无轨道基线Completed、原detail、cycles、outputs、acquired／disposed／remaining | 保持基线Unavailable与原detail `dynamic reference trajectories and area are not certified`，reason亦一致；**不把scope通过冒称认证Passed** |

**完整验证命令与结果（工作目录仓库根）：**

```sh
cmake --build build --target OpusOptimaTests -j2 > build/track-bound-build.log 2>&1
```

首次exit=1：新增测试的 `{&machine, &decoded}` 同时含mutable／const指针，initializer_list推导冲突；仅将局部decoded声明改为非const，修复测试编译，不修改生产代码。

```sh
cmake --build build --target OpusOptimaTests -j2 > build/track-bound-build-retry.log 2>&1
./build/OpusOptimaTests --gtest_filter='IndependentFiniteReplay.TrackLengthBoundPreservesEntryOutcomeAndOriginalDetail:IndependentFiniteReplay.ProfileBoundsAreUnavailableRatherThanInvalidMachineVerdicts:IndependentFiniteReplay.SimultaneouslyCrossingBasesAreRejectedBeforeUnrelatedOutputCompletes:IndependentStaticProduction.DifferentialPistonAndTrackEndInstructionsAreNoOps:IndependentStaticProduction.DifferentialTracksIgnoreSerializedRotation:IndependentStaticProduction.CrossingTrackBasesCannotPassWhileUnrelatedOutputCompletes:IndependentMetrics.DifferentialLinearPistonAndTrackMotionAndHaltedTapeAccounting' > build/track-bound-tests.log 2>&1
```

两条exit=0，实际匹配7项，7/7通过（14ms）。发现既有metrics fixture默认使用系统临时目录，随后最终命令显式设TMPDIR为build；未修改共享fixture。新增solution始终在build并由RAII清理。补强认证基线的精确status/detail断言后最终重建复验：

```sh
cmake --build build --target OpusOptimaTests -j2 > build/track-bound-build-final.log 2>&1
TMPDIR="$PWD/build" ./build/OpusOptimaTests --gtest_filter='IndependentFiniteReplay.TrackLengthBoundPreservesEntryOutcomeAndOriginalDetail:IndependentFiniteReplay.ProfileBoundsAreUnavailableRatherThanInvalidMachineVerdicts:IndependentFiniteReplay.SimultaneouslyCrossingBasesAreRejectedBeforeUnrelatedOutputCompletes:IndependentStaticProduction.DifferentialPistonAndTrackEndInstructionsAreNoOps:IndependentStaticProduction.DifferentialTracksIgnoreSerializedRotation:IndependentStaticProduction.CrossingTrackBasesCannotPassWhileUnrelatedOutputCompletes:IndependentMetrics.DifferentialLinearPistonAndTrackMotionAndHaltedTapeAccounting' > build/track-bound-tests-final.log 2>&1
```

最终构建exit=0；7项实际执行、7/7通过（15ms），含1项新边界测试（4个count×内存／解码×replay／认证入口），其余6项为紧邻现行行为回归，不报全量覆盖或性能收益。最终辅助solution／puzzle均已清理；失败与成功命令结果已登记于本节，本包build日志随收尾清理。没有运行额外lint／全套测试或实机。定位时曾误查不存在的 `IndependentVerifierTest.cpp` 和通配目标，后改读实际 `IndependentFiniteReplayTest.cpp`；这些定位失败不算验证失败。

**下一真正可实施离线包：最小拓扑反例，而非继续文档审计。** 复用同一fixture，构造四格有内部弦但首尾不相邻的序列 `(0,0),(1,0),(0,1),(0,2)`，及两条共享格的轨道；分别覆盖无A/a、A/a在唯一旧底座、A/a在共享旧底座、由唯一位置进入共享格后下一周期A/a。保存精确status/detail和失败周期（若公开事件可观察），对照现行延迟绑定；内部弦／静止重叠当前未全局禁止，不能凭测试假定游戏不允许，也不能直接称真实bug。若需确立与游戏语义差异，先使用已有离线reference证据；无充分证据则保持未证边，不启动游戏或越权改策略。

本包未发现生产bug；失败仅为已修复的新增测试编译错误。只关闭当前轨长边界证据缺口；唯一／相邻／首尾闭环拒绝、A/a按旧底座绑定及多轨歧义拒绝的生产策略不变，内部弦与跨轨重叠仍无全局证明。A／B／D与阶段4／5尚未完成，阶段2成本配对和其他离线准备未关闭，**不能声称只剩实机测试**。保护原有未提交／未跟踪工作，无提交、委派、游戏启动或存档操作；本包完成后停止供主会话验收。

### 阶段3轨道最小拓扑真实入口包（现行行为回归）

仅在 `IndependentFiniteReplayTest.cpp` 新增5项测试，通过公开 `ReplayFiniteMachine` 与 `VerifyIndependent` 运行现有普通Salt fixture；未修改生产语义。轨道为序列 `p0=(0,0),p1=(1,0),p2=(0,1),p3=(0,2)`，存在非连续几何邻接 p0–p2，但首尾不相邻；共享轨为 `[(0,1),(1,1)]`（两轨同偏移）。几何邻接不替代序列边，跨轨共享格也不等同于两个底座重合。

**预期来源及证明域：** 本包从 `StaticProductionVerifier.cpp` 的 CheckScope、Instruction、Motions、CertifiedMotions、Run 推导预期，而非读取实际输出当oracle。CheckScope仅检查相继格、重复格和首尾闭环等既有门；Motions仅在 A/a 时按旧底座查找唯一轨，索引±1并在端点夹取。普通输出目标为6，失败report.cycles为零基失败周期，diagnostics.completedCycles仅在整周期完成后增加。没有本包锁源reference差分证据，以下全部是有限执行器**现行行为回归，不认证游戏规则正确性**：

- `InternalChordAndStationaryTrackOverlapAreNotGlobalTopologyGates`：两轨偏移(10,10)，单个额外底座静止于共享格(10,11)，普通生产臂在远处执行原四步序列。Replay Completed，detail=`independent finite execution completed every ordinary output`，outputs={6}，cycles=23（释放在2、6、…、22）。这只说明不存在全局内部弦／交叠拒绝门；VerifyIndependent在普通生产臂首次旋转周期1触发动态认证门，并非认证交叠。
- `InternalChordUsesSequenceNeighborsAndBothEndpointNoOps`：生产臂朝向3，抓取相对偏移(-1,0)的Salt，输出在(-1,2)。磁带 `G,a,A,A,A,A,g,a,a,a,a`，周期11；正向持物Motion事件严格在每轮偏移2、3、4，端点a/A无Motion，释放在6+11k，第六输出周期61，故Completed/outputs={6}/cycles=62。反向三步回到p0，末端a保持p0，下一轮才能再次抓取并完成；禁止几何弦捷径的推导不依赖事件中的不可见底座坐标。认证入口在首次实际平移周期2停止。
- `SharedOldBaseRejectsBothTrackDirectionsBeforeCompletion`：共享旧底座执行A、a均为Unavailable，detail=`ambiguous overlapping track reset/motion`，cycles=0、completedCycles=0、outputs={0}；在第二条轨首端执行a本应端点no-op，也必须先过唯一归属门。两个入口均保持此detail，认证reasonCode=`independent-profile-uncertified`、errorCycle=0。detail虽含reset，本包未执行reset。
- `UniqueOldBaseMayEnterSharedCellButNextTrackActionIsAmbiguous`：分别从p1执行A、从p3执行a，第0周期唯一归属可移动到共享p2；第1周期同一指令才歧义，Replay Unavailable/上述detail/cycles=1、completedCycles=1、outputs={0}。仅一个远处底座在轨上，不是底座碰撞。认证入口在第0周期平移时先返回Unavailable/detail=`dynamic reference trajectories and area are not certified`，errorCycle=0，不声称它执行到后续歧义。
- `EnteringSharedTrackCellWithAnotherBaseIsACollisionNotAmbiguity`：唯一旧底座从p1执行A，另一个底座静止p2；Replay Rejected/detail=`independent simultaneous arm-base collision`、cycles=0、outputs={0}。这是实际底座相撞，与静止轨道重叠对照；认证动态门仍先返回Unavailable/动态detail/errorCycle=0，不能据此认证无碰撞。

**完整执行记录（本包独立验证，未复用或重跑旧7项）：** 工作目录为仓库根；所有构建和运行均显式将TMPDIR限定现有build，未写辅助solution／puzzle。

```sh
TMPDIR="$PWD/build" cmake --build build --target OpusOptimaTests -j2 > build/track-topology-build.log 2>&1
TMPDIR="$PWD/build" ./build/OpusOptimaTests --gtest_filter='IndependentFiniteReplay.InternalChordAndStationaryTrackOverlapAreNotGlobalTopologyGates:IndependentFiniteReplay.InternalChordUsesSequenceNeighborsAndBothEndpointNoOps:IndependentFiniteReplay.SharedOldBaseRejectsBothTrackDirectionsBeforeCompletion:IndependentFiniteReplay.UniqueOldBaseMayEnterSharedCellButNextTrackActionIsAmbiguous:IndependentFiniteReplay.EnteringSharedTrackCellWithAnotherBaseIsACollisionNotAmbiguity' > build/track-topology-tests.log 2>&1
```

首次构建exit=0；首次运行exit=1，4/5通过，持物测试Rejected/detail=`atom overlaps arm base`：初稿朝向0，p1底座撞到输入新生Salt(1,0)。这是fixture真实输入几何冲突，不是轨道耦合bug；改朝向3并把输入／输出移至(-1,0)/(-1,2)，保留相同轨道及预推磁带／周期。第二次按上述构建命令尝试exit=1（误用不存在的SolutionObject.body，正确字段为bodyData）；该次用`&&`串接的测试未运行，随后打印的是上次测试日志，不作为新验证。修正字段后第三次按上述命令以`&&`串接构建／测试，构建exit=0，测试exit=0，实际5/5通过（4ms）。期间一次grep工具因空type参数报定位错误，改用限定IO路径的rg确认声明，不计测试结果。未新增依赖、未运行全套或lint；日志结果在此登记后删除本包两个build日志。

本包未发现生产耦合bug；保护已有修改，仅新增测试及本文档段落。未扩reset／重复／profile／256上限，legacy与2/10/20预算不变，无委派、提交、游戏启动或正式存档操作。A/B/D仍阻塞正式扩展，阶段4/5和其他离线准备不因这5项回归而关闭。**下一独立可实施目标：** 在已有锁源离线reference harness内仅复用本包朝向3的内部弦持物fixture，建立是否能完成及完成周期的差分证据；先核查reference是否接受该编码，若没有可靠证据则记录未证，不扩生产语义或启动游戏。本包到此停止。

### 有界离线内部弦 reference 差分（2026-11-04）

本包承接上述目标，不重开拓扑审计。已检查上述五项轨道测试、`StaticProductionVerifierTest.cpp` 的 native 差分入口和 functional `VerifierTest.cpp` 的 `ExternalVerifier.KnownSolutionsComplete`。现有 `DifferentialPistonAndTrackEndInstructionsAreNoOps` 已覆盖端点 no-op，`DifferentialTracksIgnoreSerializedRotation` 已覆盖轨道序列不受序列化朝向影响，但均不是内部弦持物朝向3 fixture，故保留原测试而仅新增 `IndependentStaticProduction.DifferentialInternalChordHeldSaltOrientationThree`。没有新增验证框架或生产策略。

先核查 `asset/libverify.README`：锁定 release target revision `a88d12d8d100784aa525ee90259255b869bcba38`；`sha256sum asset/libverify.so` 返回 `0610b5841ee2a5cca673b4a2f585ab017a8d94fca1ec67fe79a789237625b8c2`，退出0。另以以下命令复核（退出0）：

```sh
sha256sum -c <(printf '%s  asset/libverify.so\n' '0610b5841ee2a5cca673b4a2f585ab017a8d94fca1ec67fe79a789237625b8c2')
ldd asset/libverify.so
./build/OpusOptimaTests --gtest_list_tests
```

`ldd` 退出0、无缺失库；测试列表退出0，已有 dependent native tests 真实存在，`HAS_LIBVERIFY` 已启用。本轮不重新下载工具；revision provenance 取自既有 README，实际执行对象以本地库 SHA-256 锁定，不把 movable release tag 当作新的源码核验。

**最小改动与结果：** 复用既有 `WritePuzzle`，仅允许无 `productionInfo` 时写普通 puzzle 标记并返回，原 production 编码路径不变；新测试将同一普通 puzzle／solution 写入 `build/independent-chord-reference-<pid>.{puzzle,solution}`，经现有 parser 回读，再分别调用 `ReplayFiniteMachine` 与既有 libverify API，二者周期上限均100。固定保留输入 `(-1,0)`、输出 `(-1,2)`、arm 朝向3、四点有序路径 `(0,0),(1,0),(0,1),(0,2)` 与原11周期磁带，不以 production puzzle 替换普通 fixture。

| 观测项 | 本地 finite replay | 锁源 reference |
| --- | --- | --- |
| 完成 | `Completed`，输出计数 `{6}` | cycles 求值无 error，完成 |
| 完成周期 | 62 | 62 |
| cost / area / instructions | finite report 不提供这些指标，不作双边相等断言 | 40 / 7 / 11，逐项求值无 error |
| 独立认证 | `Unavailable`，errorCycle=2，`dynamic reference trajectories and area are not certified` | 非 reference 执行失败 |

reference 接受该编码，故无需无弦对照。指标以 gtest `RecordProperty` 记录；不伪造 reference 输出计数接口，也不将单例完成周期一致解释为连续轨迹／面积认证、所有拓扑或游戏本体等价。

**实际验证命令与退出码：**

```sh
TMPDIR="$PWD/build" cmake --build build --target OpusOptimaTests -j2 > build/chord-reference-build.log 2>&1
TMPDIR="$PWD/build" ./build/OpusOptimaTests --gtest_filter='IndependentStaticProduction.DifferentialInternalChordHeldSaltOrientationThree:IndependentStaticProduction.DifferentialPistonAndTrackEndInstructionsAreNoOps:IndependentStaticProduction.DifferentialTracksIgnoreSerializedRotation:ExternalVerifier.KnownSolutionsComplete:IndependentFiniteReplay.InternalChordAndStationaryTrackOverlapAreNotGlobalTopologyGates:IndependentFiniteReplay.InternalChordUsesSequenceNeighborsAndBothEndpointNoOps:IndependentFiniteReplay.SharedOldBaseRejectsBothTrackDirectionsBeforeCompletion:IndependentFiniteReplay.UniqueOldBaseMayEnterSharedCellButNextTrackActionIsAmbiguous:IndependentFiniteReplay.EnteringSharedTrackCellWithAnotherBaseIsACollisionNotAmbiguity' --gtest_output=xml:build/chord-reference-results.xml > build/chord-reference-tests.log 2>&1
```

构建退出0；定向运行真实匹配9项（五项原轨道回归、两个既有 dependent、一个新增差分、一个 functional reference）全部通过，退出0，23ms。无构建或测试失败、无失败后改换 oracle；构建仍报告既有 helper 第243行 dangling-else warning，本包未修改该处。调查中有对不存在路径的只读搜索报错，改用现有路径，不计作 reference 失败。无新增生产代码、无依赖安装。fixture 由 RAII 删除，核对 XML 后删除本轮三份日志/XML：

```sh
rm -- build/chord-reference-build.log build/chord-reference-tests.log build/chord-reference-results.xml
```

清理退出0；本轮生成文件均限 build，已有 build 文件及未提交工作保留。不改256/profile、legacy-gate或2/10/20预算，不重开A/B、不跑长轨性能，不启动游戏或操作存档，不提交、不委派。**本包已取得且仅取得这个具体 fixture 的完成／周期 reference 差分证据，到此停止；下一建议转阶段4既有墙体／导管独立准备，不再无限细化拓扑，本包不执行阶段4。**

### 阶段4首包：墙体／受限 conduit 义务与有序格拒绝（2026-11-04）

**仅准备与一个测试主题，不开放 production。A/B 空间映射前置仍未过，D 未闭；下表的现行功能不是连续认证。** 实际入口均在 `src/Verification/StaticProductionVerifier.cpp`，functional 为 `VerifyStaticProduction`，认证为 `VerifyIndependent`；后者在 Setup 以 `production-wall-profile-uncertified` 保持 `Unavailable`，包括本包的错序负例。

| 六室型 | 现行离散 Room 参数 (radius, stretch) | 静态点／连续足迹未闭义务 |
|---|---|---|
| Small | (1,0) | 独立规则支持的墙边内／上／外点与顶点、伸展接缝；完整原子圆足迹 |
| SmallWide | (1,1) | 同上，不能把多个中心六角格并集当成连续运动证明 |
| SmallWider | (1,2) | 同上 |
| Medium | (2,0) | 同上 |
| MediumWide | (2,1) | 同上 |
| Large | (3,0) | 同上；现有 conduit fixture 使用两 Large 室，不代表覆盖六型 |

`Room:338` 和室型构造 `438–480` 做格内／墙格及室间重叠检查；`InsidePoint:348` 是现有带余量的静态点判定。`Motions:975–1052` 仍为现行采样墙检查，不证明全区间。`Boundaries:1300` 检查原子、wheel、机械底座的部分占用冲突；机械 shaft／旋转扫掠、端口完整足迹、面积与全部指标仍须单独建立证据。**最小缺失规则**是六型连续墙边的独立定义及原子／机械／wheel／端口足迹和切触语义；本包不照抄 InsidePoint 公式制造 oracle。

| 受限 conduit 义务 | 现行入口／功能 | 本包边界与剩余义务 |
|---|---|---|
| 身份／有序格／室归属 | Setup `625–694`：ID、计数、逐索引 path、去重、成对不同室 | 本包只补同格集合的纯置换拒绝；未知配置不因此放行 |
| 端口／生成／isolation | Setup `696–700` 拒绝同室 input/output isolation；Spawn `769` 遇占用跳过生成 | Run `140–175`：初始 Spawn/Boundaries；每周期 GrabDrop/Capture→Spawn→glyph consume→outputs→motion→Spawn→glyph produce→outputs→Boundaries。尚无本包独立时序认证 |
| 释放／预留 | Capture `1084`：drop 后未 held/reserved 的完整 fragment，在入口 shape 内才捕获 | 双端竞争、取走与生成的完整时序仍未闭；不扩展 cloning |
| 迁移／到达／守恒 | PipeGlyph `1111`：保留 atom IDs／内部键，移除源、到达占用拒绝、Transfer；跨边界键 Unsupported | 旧原例有限守恒控制仍通过；不是所有冲突／连续足迹／全指标认证 |
| 认证拒绝 | Setup `425–426` production 总拒绝门 | A/B 未过，D 未闭；未知组合仍 Unavailable，不按通过数登记 profile |

**唯一新增主题**：`tests/unit_test/StaticProductionVerifierTest.cpp:51` 的 `ConduitCellPermutationsPreserveSetsButNotOrderedFootprints`。复用现成三格 CONDUIT，穷举 5 个非恒等排列 × source／destination／both 三个端点 mask，共15个负例及1个原序正控制；所有置换保持格集合、计数、ID、位置和 puzzle 不变。functional 内存及文件入口均要求准确拒绝，认证入口全部保持 Unavailable。已有 `ProductionTransport.IndependentlyVerifiesReorderedPairConduitCells` 改的是 puzzle 允许的顺序并构造匹配机器，不覆盖这里的机器单独错序。

独立预期来自 omsim 的 conduit cabinet 规则（本地 reference `build/production-upstream-decode.c:497` 逐索引比较，置 `CONDUIT_ALTERED`），并用**已锁定实际 binary**逐例验证 `conduit violations`／`cabinet violations` 为1，原序为0且完成周期43一致。`asset/libverify.README` 锁定 revision `a88d12d8d100784aa525ee90259255b869bcba38`；本轮 `sha256sum asset/libverify.so` 实得 `0610b5841ee2a5cca673b4a2f585ab017a8d94fca1ec67fe79a789237625b8c2`。这里只主张 reference 规则差分，不主张游戏实机认证，也不为 build 内源码补造来源 hash。

**实际验证与失败修正：** 首轮执行下面同一构建及三项 filter，构建成功、组合退出1；新主题错误地要求所有置换 `cycles` 查询产生 `verifier_error`。单项复现命令 `build/OpusOptimaTests --gtest_filter='IndependentStaticProduction.ConduitCellPermutationsPreserveSetsButNotOrderedFootprints'` 退出1。reference 将 cabinet 违规单独暴露，可能仍模拟完成；这是测试 oracle/API 用法错误，不是已发现的生产行为 bug。改为查询独立的 violation metrics（`build/omsim-verifier-reference.c:1052–1066`），未改生产代码；新增断言加大括号消除新引入的 dangling-else 警告。最终实际命令：

```sh
cmake --build build --target OpusOptimaTests -j2 > build/phase4-build.log 2>&1 && build/OpusOptimaTests --gtest_filter='IndependentStaticProduction.ConduitCellPermutationsPreserveSetsButNotOrderedFootprints:IndependentStaticProduction.OriginalConduitExecutesWithoutSolverIRAndConservesEveryAtom:IndependentStaticProduction.ConduitMutationsAndOccupancyDoNotPass' > build/phase4-conduit-order.log 2>&1
```

最终退出0，3/3通过（31ms）；本机 `HAS_LIBVERIFY` 已启用，16组 reference 检查实际执行。构建仅存既有 `Differential` 单行 if 的 dangling-else 警告，未修无关代码。测试 solution RAII 清理；本包日志用 `rm -- build/phase4-build.log build/phase4-conduit-order.log` 清理。

**关闭的仅是**三格固定 fixture 上两端任意纯错序不能靠同格集合绕过现行拒绝的有限回归缺口。**未关闭**六型静态点规则、连续 footprint、完整导管生命周期、A/B/D 与 production 认证。下一实际实现目标：监督确认独立墙边／足迹规则后，先实现六型静态边界 case 表（边内／切触／外侧、角点、伸展接缝）接入真实 Room/InsidePoint 测试入口；连续运动单独后续，不以静态通过代替。本包到此停供验收，不进入该实现、不继续轨道。

### 阶段4有界实际包：六室型静态单格墙规则（2026-11-04）

**只验证离散格占用，不验证完整 InsidePoint、连续切触或扫掠；production 认证仍为 Unavailable。** 本轮重新读取上述义务、`Room`／`InsidePoint` 及既有墙体回归。独立来源没有提供本包可证明的连续点阈值，因此主动降级为单格 glyph 静态规则，不把 radius／stretch 或 InsidePoint 公式充作 oracle。

**来源和资格先行：** `asset/libverify.README` 固定 omsim revision `a88d12d8d100784aa525ee90259255b869bcba38`；实际 `asset/libverify.so` SHA-256 仍为 `0610b5841ee2a5cca673b4a2f585ab017a8d94fca1ec67fe79a789237625b8c2`。从该 revision 的 raw URL 下载到 build 的 `decode.c`、`sim.c`、`verifier.c` 分别与既有参考副本 `cmp` 相同，SHA-256 分别为：

- decode.c：`e9357b75e655def905e088c11ee0757876fa0b8866a97b07af860c8052b57f62`
- sim.c：`a7e23da18c225ec24b2337ed691fcc0a468bc27c7b87d4b8dc6644c10a08491f`
- verifier.c：`39afee16919834251c0db642d29357525c2e9cb8deaf9761ac5f045acada17a7`
- 同 revision sim.h：`198a3c4a74bc94fe1595e0159881797d450f387e92598191b77cbec3e8864a22`，`CABINET_MAP_SIZE=32`。

这些 hash 资格是锁源规则差分，不是源码重建 binary 或游戏完整等价认证。独立语义来自 `decode.c:294–306` 六型显式 interior／wall 坐标表；`440–462` 按室位置平移后，interior 标记为室号、wall 为255、未登记格为0；`506–523` 将非 conduit glyph 的变换后足迹落在0或255记为 `GLYPH_OUTSIDE_CABINET`。`sim.c:1887` 明定 calcification 足迹仅 `(0,0)`，不涉及原子圆切触。`verifier.c:1052–1060` 的 `cabinet violations` 对任意 cabinet flag 返回1，无 flag 返回0；本包直接查询该指标并检查 `verifier_error == nullptr`，不以 cycles 成功推定无违规。

**唯一新增测试主题：** `IndependentStaticProduction.SixRoomStaticGlyphCellsMatchLockedCabinetRules`。数据为固定 literal 坐标，不由被测半径公式生成。每型六个 interior 角格、六个 wall 角格、六个更外侧格；SmallWide／SmallWider／MediumWide 另各两个伸展接缝 interior 格。在原点及 `(-7,4)` 两个室位置重复，合计 **228组＝84内格正例＋72墙格负例＋72外格负例**。这不是全部墙格穷举，也不主张墙格中心就是连续墙面切触点。每例真实内存及序列化文件 functional 入口必须一致；负例要求准确的 `glyph crosses a room boundary`，认证入口全部要求 `Unavailable / production-wall-profile-uncertified`。无 arm、conduit 或运动；另一个固定 Large 室中重叠 salt input/output 仅作静止完成控制，与被测 glyph 隔离。既有 `ProductionTransport.PlansSixChamberGeometriesAndRejectsWallIntersections` 合并验证六型室格数量与重叠拒绝。

**实际失败、修复及执行数：** 首次构建成功，单项测试退出1；228次 reference 调用全部执行，84个预期内格的 cabinet 指标为1。最小原因是测试完成控制放在 `(20,0)`，超出独立 `sim.h` 的 cabinet map `[-16,15]`，连 Large 控制室本身未登记；不是已确认的 Room 算法差异。仅把控制室及 input/output 移到 `(9,0)`，所有控制室格均在 map 内，未修改生产算法。首轮 shell 尾随 `tail` 掩盖了进程退出码，但日志明确 `1 FAILED TEST`，未算通过。首轮单项命令为 `build/OpusOptimaTests --gtest_filter='IndependentStaticProduction.SixRoomStaticGlyphCellsMatchLockedCabinetRules'`。修正后实际完整命令：

```sh
cmake --build build --target OpusOptimaTests -j2 > build/phase4-wall-build.log 2>&1 && build/OpusOptimaTests --gtest_filter='IndependentStaticProduction.SixRoomStaticGlyphCellsMatchLockedCabinetRules:ProductionTransport.PlansSixChamberGeometriesAndRejectsWallIntersections' > build/phase4-wall-combined.log 2>&1
```

最终组合退出0，**2/2通过（97ms）**；`build/build.ninja` 明确 `-DHAS_LIBVERIFY=1`，不是 skip。最终228次 reference 实际执行；含首轮失败运行，本轮共 **456次** native create／cabinet metric 检查，不额外查询 cycles，不重复上一 conduit 16组／3项。构建只有既有 `Differential` dangling-else 警告。本测试 puzzle／solution 由 RAII 删除；本轮四个下载副本及三份 build 日志清理，保留既有参考文件及其他修改。

**关闭范围与下一目标：** 仅关闭上述固定六型角格／外格／伸展接缝单格占用回归；InsidePoint 完整性、连续 footprint、全 conduit 生命周期、A/B/D 与 production 认证均未闭，legacy／profile／2-10-20 预算未动。下一真正可实施的有限目标是同一锁源的一个多格 glyph（例如 animismus）静态足迹：固定少量位置／朝向，明确逐足迹格的内／跨墙预期，经真实入口与 cabinet 指标差分；仍不引入连续运动或开放 profile。本轮到此停止。

### 阶段4有界实际包：projection 两格静态跨墙足迹（2026-11-04）

**仅测试／文档，不改生产策略或认证。** 新增 `tests/unit_test/StaticProductionVerifierTest.cpp` 中 `IndependentStaticProduction.ProjectionTwoCellFootprintMatchesLockedCabinetRules`。选择明确两格的 `glyph-projection`，没有扩展到全 glyph 或连续圆足迹。

**独立依据：** 沿用 `asset/libverify.README` 锁定 revision `a88d12d8d100784aa525ee90259255b869bcba38`。本轮实际复核 binary 与现有参考文件的 SHA-256：`asset/libverify.so` 为 `0610b5841ee2a5cca673b4a2f585ab017a8d94fca1ec67fe79a789237625b8c2`；`build/production-upstream-decode.c` 为 `e9357b75e655def905e088c11ee0757876fa0b8866a97b07af860c8052b57f62`；`build/production-upstream-sim.c` 为 `a7e23da18c225ec24b2337ed691fcc0a468bc27c7b87d4b8dc6644c10a08491f`；`build/omsim-verifier-reference.c` 为 `39afee16919834251c0db642d29357525c2e9cb8deaf9761ac5f045acada17a7`。没有重新下载或声称源码重建 binary。独立 footprint 来自 `sim.c:1901–1907` 的 PROJECTION `(1,0),(0,0)`；`sim.c:30–36` 和 `decode.c:912` 给出方向变换；`decode.c:294–306` 的 literal 室格表给出 membership；`506–523` 检查每个 footprint 格是否为0／255；`verifier.c:1052–1060` 提供真实 `cabinet violations` 查询。生产 `GlyphCells`／`Room` 仅作被测对象，没有调用或复制为 oracle。

**固定10例（5通过／5跨墙拒绝）：** 坐标均为室原点 `(0,0)` 的绝对格。

| 室型 | anchor | rotation | 第二格 literal | 预期 |
| --- | --- | --- | --- | --- |
| Small | (1,0) | 0 | (2,0) | wall，拒绝 |
| Small | (1,0) | 1 | (1,1) | wall，拒绝 |
| Small | (1,0) | 2 | (0,1) | 两格在内 |
| Small | (1,0) | 3 | (0,0) | 两格在内 |
| Small | (1,0) | 4 | (1,-1) | 两格在内 |
| Small | (1,0) | 5 | (2,-1) | wall，拒绝 |
| Small | (0,1) | 0 | (1,1) | wall，拒绝 |
| SmallWide | (0,1) | 0 | (1,1) | 伸展接缝，两格在内 |
| SmallWide | (1,1) | 0 | (2,1) | wall，拒绝 |
| SmallWider | (1,1) | 0 | (2,1) | 第二伸展接缝，两格在内 |

所有 anchor 都在内，旋转改变第二格的正负例可捕获只检查 anchor／忽略旋转；两组相同坐标仅变室型的对照隔离伸展接缝。该 glyph 两格相邻，在这些房型内格的相邻非内格就是 wall，无法在保持 anchor 在内时让第二格越过 wall 到更远的 exterior；本包不伪造该不可达组合，也不重复上一包单格 exterior 网格。

fixture 无 arm、conduit、tape、运动或测试区原子；在 `(9,0)` 的另一 Large 室内放重叠 salt input/output 作静止完成控制，和 projection 完全隔离。全部房间（含墙）与部件均在 reference map `[-16,15]` 内。每例实际调用内存 `VerifyStaticProduction` 和序列化 `VerifyStaticProductionFiles`，负例精确要求 `glyph crosses a room boundary`，两入口 status/detail 一致；`VerifyIndependent` 始终要求 `Unavailable / production-wall-profile-uncertified`。每例创建 native verifier，直接查询 `cabinet violations`（内例0，跨墙例1）并要求 `verifier_error == nullptr`，不查询 cycles 来推定无违规。

**执行及精确计数：** `build/build.ninja` 实际含 `-DHAS_LIBVERIFY=1`，新增主题不是 skip。执行命令如下，两次组合均退出0（首次8ms，最终9ms），均为 **2/2通过**，未重跑六室228例：

```sh
cmake --build build --target OpusOptimaTests -j2 > build/phase4-projection-build.log 2>&1 && build/OpusOptimaTests --gtest_filter='IndependentStaticProduction.ProjectionTwoCellFootprintMatchesLockedCabinetRules:IndependentStaticProduction.DifferentialEveryChemicalGlyphOnDirectFeeds' > build/phase4-projection-test.log 2>&1
```

实际 shell 在组合后保存 `$?`，打印有限结果后 `exit "$result"`，没有通过 tail 覆盖失败码。首轮新增单行 `if` 遇 gtest 宏产生 dangling-else 警告，补花括号后重建并运行相同最小组合；最终只剩既有 `Differential` 的 `:311` 警告。每次新增主题10次 native create／cabinet metric／error 检查，紧邻既有 chemistry 项7次 native create／cycles／error 检查；本包两次合计 **34次 native create，其中20次 cabinet metric 检查＋14次 cycles 检查**。后14次不是墙无违规证据。新增主题每轮还实际各执行10次内存 functional、文件 functional、认证 Unavailable 检查。没有生产差异，不需要策略修改或最小差异报告。fixture 文件经 RAII 删除，两份本轮 build 日志清理；保留原有参考副本及已有修改。

**未证明：** 连续点／圆足迹／扫掠、所有 glyph、全房型多格组合、完整游戏等价及 production 独立认证仍未证明；legacy、profile、2-10-20预算均不变。下一建议转向**受限 conduit 一次释放到一次到达的具体生命周期**：固定单分子／单端点对，检查释放后源占用消失、到达周期目标占用与事件顺序，以及单个目标阻塞反例；先锁源生命周期规则，不继续增加 glyph 表。本包完成即停。





### 阶段4有界生命周期包：仅释放触发捕获、同周期到达（2026-11-04）

本包只增加 `ConduitCapturesOnlyOnReleaseAndArrivesThatCycle`，不改生产算法。现有原例43周期／6次迁移守恒和占用拒绝测试没有独立的持有／释放时序预期；`ProductionExecution.SerializesRepeatedConduitCustodyAndReservesArrivalCells` 是显式 ledger 调用，不是实际磁带 replay。复用既有 HAS_LIBVERIFY writer/API harness，不扩墙体表。

**独立预期：** 锁源 omsim `a88d12d8d100784aa525ee90259255b869bcba38` 的 `sim.c::fill_conduits` 只在 half-cycle 1 选择 `BEING_DROPPED && !GRABBED`，`apply_conduit` 在第一半周期消费、第二半周期产生。最小两 Large 室、单格端点、单原子 feed／output、静止 arm，无键、无其他反应。三例仅改变释放：始终持有，或零基周期 d=2／3 释放；磁带 G@0、g@d、O@(P-1)，P=d+2。因此释放前没有 Transfer／Output；始终持有40周期仍未完成；正例 Transfer 与 Output 均在 d+kP（k=0..5），完成周期 d+5P+1，即23／29。期望先由规则与字面磁带计算，不从 replay 输出生成 oracle。

实际断言持有前缀 acquired=remaining=1、outputs=disposed=0；完成时 acquired=7、outputs=6、remaining=1、disposed=0，6次迁移／输出的周期和 AfterMotion 阶段，以及 Acquire→Transfer→Output atom ID 一致。后者**仅为独立 replay 的公开事件证据**，native cycles 不证明 ID 稳定。内存与文件入口结果一致；三例认证入口仍为 `Unavailable / production-wall-profile-uncertified`。

验证实际命令（每条均保存并返回真实退出码，日志不覆盖失败）：

```sh
TMPDIR="$PWD/build" cmake --build build --target OpusOptimaTests -j2
TMPDIR="$PWD/build" build/OpusOptimaTests --gtest_filter='IndependentStaticProduction.ConduitCapturesOnlyOnReleaseAndArrivesThatCycle'
TMPDIR="$PWD/build" build/OpusOptimaTests --gtest_filter='IndependentStaticProduction.ConduitCapturesOnlyOnReleaseAndArrivesThatCycle:IndependentStaticProduction.OriginalConduitExecutesWithoutSolverIRAndConservesEveryAtom:IndependentStaticProduction.ConduitMutationsAndOccupancyDoNotPass:IndependentStaticProduction.ConduitCellPermutationsPreserveSetsButNotOrderedFootprints:ProductionTransport.IndependentlyCompletesSixProductsIncludingIsolation' --gtest_output=xml:build/phase4-release-results.xml
```

构建退出0；首次单测1/1通过、退出0；组合5/5通过、退出0，XML 确认新增项 `reference_cases=3`。HAS_LIBVERIFY **实际运行**：新增主题每轮3次 native create／cabinet metric／cycles／error 检查，两轮合计6次；持有例周期上限报错，释放例 native cycles=23／29，无错误。实际 `sha256sum asset/libverify.so` 为 `0610b5841ee2a5cca673b4a2f585ab017a8d94fca1ec67fe79a789237625b8c2`，与锁源 README 一致。无失败复现、无生产耦合 bug、无需算法修复；构建仅保留既有 Differential:311 dangling-else 警告。自建 fixture 由 RAII 删除，本包日志／XML验证后清理。

**关闭范围：** 仅此静止单原子、单端点对的持有不捕获和释放周期捕获→迁移→到达输出／守恒。目标已占用拒绝仍只有既有负例，不新增独立时序证明；双端竞争、其他 glyph 干预、cloning、跨界新增键、完整 pipeline 和连续认证均未关闭。legacy／profile／2-10-20预算不变。此包完成即停；下一目标转向**有限阶段5规则准备／实现前置**（先列锁源规则与最小证据义务，不默认扩展 conduit 或开放 production）。


### 阶段5首包：有限 repeat/reset 规则与拒绝边界（离线，未开放展开）

本包不改生产实现。来源为 README 锁定的 omsim revision
`a88d12d8d100784aa525ee90259255b869bcba38`；实际复核 libverify SHA256
`0610b5841ee2a5cca673b4a2f585ab017a8d94fca1ec67fe79a789237625b8c2`。
本地锁源 `production-upstream-decode.c` SHA256
`e9357b75e655def905e088c11ee0757876fa0b8866a97b07af860c8052b57f62`，
`omsim-verifier-reference.c` SHA256
`39afee16919834251c0db642d29357525c2e9cb8deaf9761ac5f045acada17a7`。
以下“reference规则”不等于独立实现能力，更不等于 A/B/D 认证。

| 对象／边界 | 锁源规则与证据范围 | 当前独立入口 |
|---|---|---|
| 原始磁带／显式展开磁带 | 原始是 solution 序列化的 action/index 对；reference decode 在仿真前消解 C/X，展开磁带含显式动作及空位。strict codec 原样保留 C/X，不替调用者展开。 | C、X 均 Unavailable；不是 codec 解析失败，也不是成功验证。已允许显式动作按原 profile 验证。 |
| 索引／负偏移 | reference 按 index 排序，以 min 建局部 tape，再按所有臂最早 start 归一化。C 的源起点另有 `last_repeat=max(last_repeat,-min_tape)`，不能把带 C 的负偏移视为任意平移等价。原始 span>99999／长度算术溢出被拒绝。 | 位置须在 [-20000,20000]，重复 index 拒绝；超界在相应 profile 下沿用原有 Rejected／Unavailable gate 顺序。单臂无 C 的负偏移 X 本包有 reference 对照；负偏移 C 只有源码规则，未实测、不开放。 |
| B halt | 首个 B 终止此臂，halt tape 不贡献全局循环 period；非 halt 臂共同确定 period。 | 支持 B，一次性执行并停止；已有 offset/halt 回归保留。B 后静态 instructions 仍可计数，但 executed instructions 在 B 截止，二者不可混用。 |
| X reset | decode.c:1170–1280 按 reset_from 到 X 前的机械状态生成：必要 drop、过长活塞缩回、反转归位、轨道归位、过短活塞伸回；空 reset 也占一个空位（影响 repeat）。 | 全部拒绝 X。本包仅实测无轨 arm1 单次 swing 后 X→R；活塞／轨道／空 reset 仅源码规则，未认证。重叠轨 reset 明确排除。 |
| 相邻 C 块 | 排序后连续 C 条目（不要求坐标紧邻）复制同一 `[last_repeat,last_end)`，不是递归倍增；后续普通条目重置源起点与 reset_from。若 last_end 超过 C 位置，reference 拒绝 reset 展开重叠。 | 全部拒绝 C。本包实测 C@4、C@8 各复制同一4格块；其他重叠、间隔、负起点只列规则，不声称已验证。 |

**指标口径：** `instructions` 是 decode.c:1371 的展开后非空、非 B 槽位数；
`executed instructions` 是 reference.c:500–526 在测量截止前至少执行过的非空槽位数，
遇 B 截止，不是跨所有循环的动态总执行次数。`parsed instructions` 返回 solution 文件
solved 元数据中的声明分数（parse_solution_byte_string），不是 raw 条目数，不能拿它作展开 oracle。
本包 raw 条目数4／6、手工条目数4／12由字面 fixture 可见；不查询或认证 parsed 分数。
cycles 是实际完成周期；cost／area 未新增对照，也未建立任何四指标完整认证证据。

**唯一新增正反主题：**
`FiniteRepeatResetReferenceMetricsDoNotAuthorizeRawTapes` 使用单原子搬运、单臂、无轨道：
`G,r,g,X` 在 offset=-3，及 `G,r,g,X,C@4,C@8` 整体 offset=0／7。
手工预期固定写为 `G,r,g,R` 或其三份，不调用或重写展开算法作 oracle。
锁源 reference 六次 create（3对 raw/manual）全部在100周期上限内完成：cycles=23，
展开 instructions／executed instructions 分别4／4或12／12，error=null。
显式版在独立 in-memory／strict-file 两入口完成且 outputs={6}；raw 两入口均
Unavailable，另有单独 C、单独 X 的两入口拒绝断言，以免首个 X 掩盖 C gate。
六个 raw/manual 的 VerifyIndependent 均仍为 `production-wall-profile-uncertified`；
显式完成和 raw 拒绝均不构成认证通过。

**实际验证：** 首次 `cmake --build build --target OpusOptimaTests -j2` 退出1，
原因是新增测试误用了不存在的 completedCycles 字段，改为 cycles，并修正新代码结构化绑定复制警告。
之后两次 -j2 构建均退出0；首次组合与加入独立 C/X 断言后的最终组合均4/4通过、退出0。
最终过滤器为新主题加 `TapeGatePreservesEntryStatusAndErrorOrder`、
`DifferentialTapeOffsetsNormalizeAgainstTheEarliestArm`、
`DifferentialIndependentArmsKeepOffsetsAndIgnoreHaltedTapeLength`。
XML 每轮记录新增主题 reference_cases=6，两轮合计12次新增 native fixture 执行；
既有两个 differential 主题每轮另有4+2次 native 执行，组合合计每轮12次、两轮24次。
每个新增 native fixture 实际查询 instructions／cycles／executed instructions 三指标。
最终构建仅保留既有 Differential:311 dangling-else 警告。fixture 经 RAII 删除，
本包日志／XML复核后清理，TMPDIR 指向 build，无游戏或存档操作。

**下一可实施目标（本包不实施）：** 无轨、单 arm1、有限 C/X 的预展开 IR，先固定
原始条目数／展开长度／索引算术资源上界、源码区间和空槽保留、错误次序及两个入口一致性；
先排除负偏移 C、活塞、所有轨道与重叠 reset，单独补正反例后才考虑扩大域。
Repeating 无限输出／材料图仅保留依赖：placeholder、单体序列、跨单体键、材料身份与有限计数证据，
本包不展开调查。阶段2／4仍未关闭，不得称只剩实机；A/B/D、legacy、profile、2/10/20预算均不变。
本包没有发现或修复生产 bug，完成即停供监督验收。

### 阶段5离线有界转换（已实施；未接生产）

**交付边界。** 本轮实现仅做离线纯转换，不调用 reference、时钟、文件、游戏或认证入口；
不改 CheckScope／VerifyIndependent 的 raw C/X 拒绝，不增 profile。域固定为恰好一个 arm1、
初始长度1–3／方向0–5、无任何 track（包括未连接轨道）、无活塞、无其他机械臂／wheel、
无 Repeat 原子／Repeating 输出；含 C 时 raw 最小 index 必须非负。上述排除是转换器的保守域，
不是新 profile 批准。非机械对象只接受现有有限 scope 已认识的类型且不得带 tape；未知对象、
未知动作、缺失／非法 arm body 状态失败关闭。普通可接受动作仅 `G g R r P p O 空格 B`；
A/a、E/e 不因无轨／无活塞而当作 no-op。几何、材料、权限和可完成性仍须由原验证器判断；
转换器只检查相关对象形状、现有动作 permission 表（含 raw C/X 及生成动作），不宣称解合法。

**实际交付文件。**

- 新增 `src/Verification/FiniteTapeExpansion.hpp/.cpp`，使用现有 `io::PuzzleData`、
  `io::SolutionData`、`io::ArmInstruction`；不改 strict codec，不向材料／库存
  `src/Solvers/Execution/ExecutionIR.hpp` 塞机械语义，也不引入第二套执行框架。
- 新增 `tests/unit_test/FiniteTapeExpansionTest.cpp`；在现有
  `tests/unit_test/StaticProductionVerifierTest.cpp` 增加转换结果消费与 raw gate 回归。
- `CMakeLists.txt` 将新 cpp 加入 `OPUSOPTIMA_VERIFICATION_CORE_SOURCES`，由其已有
  REMOVE_ITEM 避免主库重复编译；unit test 由现有 CONFIGURE_DEPENDS glob 收录。
- 本文维护契约／fixture；上述代码已实现，不改 legacy、profile 或2/10/20策略。

实际公开类型（命名空间 `verification`；字段不是认证报告）：

```cpp
struct TapeOrigin {
    enum class Kind { Hole, Raw, Reset, Copy } kind;
    std::optional<std::size_t> rawOrdinal; // 原输入 vector 序号；Reset 为 X
    std::optional<std::int32_t> rawIndex;
    std::optional<std::int64_t> copySite, sourceSlot; // 最近一次 C 的 raw index／局部源槽
    std::optional<std::uint8_t> resetStep; // drop、逐次反转或空 reset 的生成序号
    // Copy 保留源的 leaf rawOrdinal/rawIndex/resetStep；不建递归来源树
};
struct ExpandedFiniteTape {
    std::size_t armObject;
    std::optional<std::int32_t> rawStart; // 空 tape 为 null
    std::int64_t rawEntries, rawSpan, expandedLength, staticInstructions;
    std::optional<std::int64_t> haltSlot;
    std::vector<io::ArmInstruction> slots; // 稠密，parameter = rawStart + 局部槽
    std::vector<TapeOrigin> origins;      // 与 slots 一一对应
};
enum class TapeExpansionError { Malformed, OutsideDomain, BoundExceeded,
                                Conflict, UnknownState, ResourceUnavailable };
struct TapeExpansionFailure {
    TapeExpansionError code;
    std::optional<std::size_t> rawOrdinal;
    std::optional<std::int64_t> slot;
};
using TapeExpansionResult = std::variant<ExpandedFiniteTape, TapeExpansionFailure>;
TapeExpansionResult ExpandFiniteArm1Tape(const io::PuzzleData&, const io::SolutionData&);
```

只输出目标臂 tape 和旁路来源，不复制整个 solution。立即消费者是新增测试中的**现有**
`VerifyStaticProduction`／`ReplayFiniteMachine` 及 `IndependentMetrics`：复制 fixture solution，
只替换 `objects[result.armObject].instructions=result.slots`；strict writer 按 vector 长度写 count，
bodyData 的历史声明 count 不作实际条数。声明 metrics 不改、不作 oracle。未来通过集成门后，
生产预处理可原样调用此函数并使用同一替换步骤，然后进入原 CheckScope，而不是重写测试 helper。
本轮／首包实现均不接这条生产线；认证入口不能偷偷替换 raw 输入或把显式版 Passed 归给 raw。
该模块以现有消费者已经接受的 action/index 对作为输出，来源侧车不参与执行。

**索引、占位与计数。** 保留 raw vector 的 ordinal，排序副本按 int32 index 升序；重复 index
不合并。非空时 `s=min(index), L=max(index)-s+1`，局部槽 `n=int64(index)-s`；展开槽 k 的
绝对 index 为 `s+k`，单臂实际执行 start 归一为0（不是在序列化输出里删除 s）。初始长度 L，
即使只有空源 C、尾 C 或无动作也不缩短。内部槽区分未写入 Hole 与已写入的空白：O 和 raw
空格都占已写入槽；O 保留原 action，生成的空 reset 使用空格。输出把 Hole 物化为空格而非 O，
避免引入 O 的权限；来源仍能区分三者。所有槽包括首尾 Hole 都输出，因此 map 的 min/max、
空槽等待和非 halt period 完整保留。空 tape 输出空 vector、L=0、无 halt，消费者原 period 下限1不变。

`rawEntries` 包括 C/X/O/空格/B；`rawSpan=L` 与条数不同；`expandedLength` 是完整最终槽长，
不是动作数；`staticInstructions` 仅数最终 `G g R r P p`，含 B 后动作，排除 B/O/空白/Hole。
`executed instructions` 不由转换器推算，它属于运行截止时的覆盖槽位指标；既有 reference
口径和 cycles／cost／area 均须另验。首个 raw B 的局部位置就是 haltSlot；转换不在 B 截断，
B 后 X 也照常扫描和展开，B 在符号扫描中无状态变化。halt tape 不贡献 period，现有消费者
保留下来的 B 使其按原实现停止。为避免 decode 的 raw halt 元数据与 map 执行不一致，
**任何 C 冻结源区间含 B 一律 OutsideDomain**，不复制 B、不猜测复制 halt 语义；这同样不代表
新 profile 批准。普通 B 后仍允许无 B 源的后续 C 块，且必须保留静态尾部。

**可直接编码的算法。** 内部槽含 action、written 位和上述来源；普通/X 用 setSlot：只要目标
written 就 Conflict，包括原来是 O／空格、同值或 X 覆盖 C 的结果。C 按锁源 memcpy 语义覆盖，
包括用 Hole 清除旧槽；连续 C 的目的段可互相覆盖，后者胜出，不误套 setSlot。每次复制附上
当前 C 的位置与对应源槽，leaf 来源沿用源槽；复制 Hole 也有 Copy 来源且 written=false。

```text
检查域、raw count／索引界、重复、全部 raw opcode／permission；空 tape 直接返回。
分配 2*L 槽，length=L；last_end=last_repeat=reset_from=0。
按排序条目扫描：
  普通：setSlot(n, action, Raw)；last_end=n+1；记录首个 raw B。
  C run（排序连续，坐标可有间隔）：
    冻结 [last_repeat,last_end)，源长=max(0,last_end-last_repeat)，零长不写；含 B 则拒绝。
    对 run 内每个 C：若 last_end>n 则 Conflict；复制冻结源到 n，
      length=max(length,n+源长)；不更新 last_end，不递归复制刚生成的块。
    若存在下一非 C 条目 q：last_repeat=reset_from=q 的局部槽。
  X：从已知初态 grab=false, rotation=0 扫描 [reset_from,n) 的展开槽；
    G/g 设置 grab；r/R 分别 rotation++/--；P/p、B、O、空格、Hole 不改变这两状态；
    任何其余状态／动作 UnknownState，不生成“猜测 reset”。
    若 reset_from>=n 则扫描为空，不能以无符号减法下溢；写入仍需过 setSlot。
    while rotation>3: rotation-=6；while rotation<-3: rotation+=6。
    先按 grab 生成 g，再对正 rotation 逐次生成 R、对负 rotation 逐次生成 r；
    +3/-3 保留原符号，不用 [0,5] 模数抹平平局。
    无生成动作也写一个空格；逐槽 setSlot，保留 X ordinal 与 step。
    reset_from=last_end=生成后游标；length=max(length,游标)。
每次地址／end 计算先检查界再访问；最终裁至 length，检查输出位置域，计算计数与 halt。
任一步失败丢弃整个局部结果，不返回半条 tape；不修正 raw 输入。
```

这里 X 的“已知初态”是锁源每段的机械符号初态，不是持久化全局 arm 旋转，更不是分子身份、
实际是否抓住原子或材料连通性。连续 X 使用上次生成后 reset_from；C run 后源切换也同步它。
不能因 P/p 看似不转底座就删除其动作，不能生成活塞／轨道 reset，也不能把未知状态清零。

**资源与错误顺序。** 全部 index 差、`2*L`、复制 end、`s+k`、rotation 及计数在 int64 内先算，
检查非负、`size_t` 容量和 int32 可表示性后才窄化／分配。用现有 `maxTapePosition=20000`：
raw 位置 ∈[-20000,20000] ⇒ L≤40001、唯一 raw 条目≤40001、锁源工作槽≤2L≤80002；
实现仍逐次检查生成／复制 end≤2L，违反则 BoundExceeded，不越界、不扩容兜底。还要独立检查
最终每个绝对位置仍在现有 ±20000 内；80002 只是工作容量推导，**不是**新获批执行长度。
含 C 且 s≥0 时进一步 L≤20001；不复用 reference 的99999作为新 profile 上限。
排序 O(N log N)，槽／来源 O(L)，逐次扫描／复制最坏 O(N·L)，由上述界导出，不能宣称线性；
不增加任意条数、操作数或时间阈值，也不改现有 cycleLimit≤150000 与2/10/20策略。
生产若需更快算法／预算资格，仍须 D 门；离线纯函数不自行执行 verifier timeout 策略。

固定顺序为对象／初态结构 → 域排除 → raw count／位置界 → 排序重复 → 全 raw 动作与权限 →
按排序展开的冲突／状态／端点检查 → 最终输出位置界。Malformed 用于缺字段、非法方向／长度、
重复；OutsideDomain 用于明确排除和未知 opcode／对象；UnknownState 用于内部状态不可解释；
Conflict 用于 setSlot 已占用或 C 与 reset 源尾重叠；BoundExceeded 用于现有界／安全算术；
分配失败 ResourceUnavailable。codec ParseError 在调用前处理，不混为 C/X 错误。
这些不是认证 verdict：后续接入口时 OutsideDomain／BoundExceeded／UnknownState／资源失败
只能失败关闭为 Unavailable；Malformed／Conflict 的 Rejected 还须保持既有入口 gate 顺序。
本轮不实现映射、不改变现有 raw C/X 返回原因或权限错误优先级。

**字面 fixture 与验收（本轮最小组合已运行，范围见下）。** 下表省略 index 时从0连续；
`_` 为 Hole、`s` 为显式空格，预期必须手写，不能用转换算法生成 oracle。

| raw | 唯一预期／拒绝 |
|---|---|
| 既有 `G r g X` offset=-3；`G r g X C@4 C@8` offset=0/7 | 分别 `G r g R`、三份 `G r g R`；保留原 start，复用既有搬运结果23周期作对照 |
| `G@0 C@2 C@5` | `G _ G _ _ G`，run 两次均复制 `[0,1)`，不倍增；raw=3、span=6、length=6、静态=3 |
| `C@0`；`X@0`；空 tape | 分别一格 Hole、一格生成空白、零格；前两者 period=1，来源不同 |
| `G r X`；`r r r X`；`R R R X` | `G r g R`；三 r 后三 R；三 R 后三 r，验证 drop 优先及±3平局 |
| `r X X`；`r X C@2 G@4 X@5` | `r R s`；`r R r R G g`，验证连续 reset 与 C 后 reset_from 切换 |
| `O@0 C@2`；`s@0 C@2` | 首尾保留 O 或 s，中间 Hole；静态=0，writer／权限／来源不能混淆 |
| `B@0 r@1 X@2` | `B r R`，halt=0、静态=2、executed=0；B 后不裁剪，周期按现有 halt 规则 |
| `r r r X@3 G@4`；`r r r X@3 C@4` | 前者普通写碰到 reset 生成槽，后者 last_end>C位置，均 Conflict |
| `G@0 C@1 C@2 g@3` | `G G G g`；另用 `G r C@2 C@3` 断言复制目的段重叠后者胜出：`G r G G r` |
| 重复 index（含 C run 内）；负起点 C；C 源含 B；任意轨道／活塞／第二臂／Repeating | 按上述顺序失败，不返回部分输出；负起点纯 X 仍可接受 |
| unknown action／未知对象／缺 arm state；A/a/E/e | 未知不得变空白，域外不得假装完成 reset；分别核对分类 |
| `r@19999 X@20000`；`G@20000 C@20001`；raw int32 极值 | 首者生成 R 仍在20000可接受；后两者 raw 越现有界，分配前拒绝 |
| `r@19998 r@19999 X@20000`；`X@-20000 O@20000` | 首者展开到20001拒绝；后者 L=40001／2L=80002，保留全跨度，不缩尾；测安全算术和来源界 |

另断言 permission 缺失、reset 写入已物化空白、C 覆盖普通／空槽及其后普通再写的冲突，所有
成功结果无 C/X、slots／origins 等长、输入不变、序列化往返稳定；相同 raw 在 in-memory 与
strict-file 解码后转换得到同一结果／分类，严格 codec 不代替转换。C run 先统一检查重复与
permissions，避免锁源 run 内跳过这些检查的行为成为独立模块漏洞。

差分分两层：①纯字面槽／来源／计数断言无需 native；②锁定现有 reference 二进制与 decode
身份后，对支持域内 raw、手写显式、转换显式三者查询 error、instructions、executed instructions、
cycles；可完成搬运 fixture 再比较 outputs 和 cost／area，保留 B 后静态指标及 halt 专门样例。
不完成的最小槽 fixture 只比较 decode／截止口径，不把超时当 Passed。拒绝 fixture 即使
reference 接受也只证明本模块更保守，不倒推扩域。显式结果送现有实际消费者，raw 继续同时
断言原内存／strict-file 拒绝与 VerifyIndependent uncertified；模块成功不计认证、性能收益。
reference 差分只支持锁源规则一致性，不等于游戏认证，未知／Unavailable 不记有效配对。

**生产集成前的硬门。** A 必须补齐生命周期／身份／更新发布顺序、可变性和重入证据；B 必须
闭合原子／底座／wheel／抓手、生成移除时序、pair exclusions 及未知实体失败关闭的覆盖；
D 必须汇总 A–C 语义门，并有合格双方 Passed、四指标一致的有效性能配对和既有预算资格。
只有这些门满足并由主会话明确批准后，才允许考虑前述生产预处理接线和独立 profile 决策；
本转换域排除、reference 对齐和测试可达均不能绕过它们。阶段2／4仍未完成，绝非只剩实机。
**本轮实际最小验收与剩余门。** 已交付上述三份新文件，并修改 CMake 与现有
`StaticProductionVerifierTest.cpp`；生产入口没有调用本模块，唯一调用者为测试。
`ExpandFiniteArm1Tape` 为纯函数，失败 variant 不携带半结果；输出保留稠密显式槽与旁路来源。
结构检查只覆盖展开所需形状，不替代端口、几何、材料及生命周期验证。

锁源复核使用 `asset/libverify.README` 固定的
`a88d12d8d100784aa525ee90259255b869bcba38` 的 `decode.c`，以及现有二进制
SHA256 `0610b5841ee2a5cca673b4a2f585ab017a8d94fca1ec67fe79a789237625b8c2`。
逐段核对 C run 不更新 last_end、源冻结与重叠目的覆盖、Hole 清除、X 的符号状态和±3平局、
B 不裁剪静态尾部。未发现需要改变契约的锁源反例；negative C、复制 B、未知动作及 run 内
重复／权限预检是有意保守差异，不能据 reference 接受而扩域。首次新增测试曾把
`G_XC` 长度误写为7（实际字面 `G gG g` 长6），把 Hole 来源位置误写为5（应为6）；
已修正 fixture 并通过回归，没有据错误 oracle 修改算法。补强测试时缺 `<algorithm>` 的编译失败
亦已修复后重建。

实际命令（所有 build 最多 `-j2`）：

```sh
cmake --build build --target OpusOptimaTests -j2
build/OpusOptimaTests --gtest_filter='FiniteTapeExpansion.*:IndependentStaticProduction.FiniteRepeatResetReferenceMetricsDoNotAuthorizeRawTapes:IndependentStaticProduction.OfflineExpansionLockedDecoderInstructionDifferential'
cmake --build build/verifier-m1-no-native-20261028 --target OpusOptimaTests -j2
build/verifier-m1-no-native-20261028/OpusOptimaTests --gtest_filter='FiniteTapeExpansion.*'
```

Enabled 最终6/6通过（4个纯转换、2个 native 测试），disabled 最终4/4通过；
HAS_LIBVERIFY 实际创建并运行 **54** 个实例（9个搬运，15组 raw／手写／转换共45个），
不是仅统计编译的测试数量。搬运三组各三版本比较 instructions、executed instructions、cycles、
cost／area；显式两版本送实际内存／文件消费者均完成23周期、outputs={6}，raw 两入口仍
Unavailable（`unexpanded repeat/reset or unknown independent instruction`），三版本的
VerifyIndependent 仍为 `production-wall-profile-uncertified`。只把显式完成归给显式结果。
其余15组比较静态指标与10周期截止下 cycles／executed instructions／错误文本；包含±3平局、
重叠覆盖与 `BrX` 静态2／executed0，不把截止或错误记作完成。原生差分的手写显式第三版本
不是用转换器生成 oracle。纯测试覆盖来源叶、最近 C/sourceSlot、Hole 清除后普通重写、
raw/输出位置与40001跨度、40002条目／1025部件拒绝、重复优先级、权限和域排除；
可序列化形状的失败样例经 writer/parser 后维持分类，成功样例往返槽／来源稳定且输入未改。

未运行历史2254／全套、游戏／存档、连续映射或性能资格；未注入 allocator failure，
UnknownState 为防御分支未强制触发；未单独跑 lint／诊断工具。enabled/disabled 均只验证测试
目标及其模块依赖，不声称所有应用目标重建。没有修改任何现有入口、raw gate、profile、
legacy 或2/10/20预算；构建包含模块不等于生产接线。A/B/D、游戏规则及连续映射仍未证，
阶段5不可称全部完成。构建内本轮 scratch 已清理，保留既有 build 树和其他未提交修改；
不提交，至此停止供主会话审查。

### 2026-11-04 有限展开有界实际验证包：分配失败与失败原子性

**成本与隔离选择。** 先复读 FiniteTapeExpansion.hpp/.cpp、四个纯转换测试及上一节实现记录，
检索 tests 的 bad_alloc／operator new／allocation failure 工具；现有 worker/resource probe 只有
进程隔离和资源观测，没有可复用的逐点分配失败工具。因此仅在 BUILD_TESTING 下新增
`FiniteTapeAllocationTest` 独立可执行及同名 CTest，链接现有 verification core；没有生产 hook、
接口或算法修改，也不引入 allocator 框架。该单线程进程替换普通 new/new[]，固定32项无分配
trace，仅在转换调用的 RAII 窗口内 armed，按调用序号精确抛一次 bad_alloc，退出窗口即关闭。
输入构造、序列化快照、断言和恢复调用均不在注入窗口；绝不链接进共享 OpusOptimaTests，
不依靠环境变量或全局无界失败。当前五类容器均用普通对齐分配；未声称覆盖未来 aligned
allocator 或外部 malloc。没有发现需修改的直接耦合生产 bug。

**实际点与断言。** 每个 fixture 先观测成功分配序列并核对手写槽串，再对每个实际分配
序号逐一失败；失败必须为 variant 的唯一 failure 分支、code=ResourceUnavailable，且
rawOrdinal/slot 均空，无成功槽／来源可见。检查停止于失败序号，没有失败后的内部继续分配。
每次失败后对 solution 全部 writer 可表达字段逐字节比较，核对 puzzle 所有字段（分子和
productionInfo 在本包为空），再正常调用并比较成功结果的全部元数据、槽和每项来源，且再次
检查输入。不是只测试第一次分配，也不是用转换结果代替字面动作 oracle。

| fixture / 手写展开 | 实际分配序列及请求字节（本机64位 Debug） | 注入次数/配置 |
|---|---|---:|
| unsorted `g@2 G@0` → `G g` | order 16；dense tape 480；slots 24；origins 216 | 4 |
| `GrX` → `GrgR` | order 24；dense 480；slots 32；origins 288 | 4 |
| `GrCC` → `GrGGr`（重叠 C run） | order 32；dense 640；frozen source 160；slots 40；origins 360 | 5 |
| `G@0 C@2 g@4 C@6` → `G G g g` | order 32；dense 1120；两次 source 各80；slots 56；origins 504 | 6 |
| `C@0` → 单空格 | order 8；dense 160；slots 8；origins 72；空 source 无分配 | 4 |
| 空 raw → 空 tape | 无分配 | 0 |

每个配置一轮 **23次实际 bad_alloc 注入**，含两个独立 C run 的两个源快照及已经成功 reserve
slots 后 origins reserve 失败的清理；没有异常泄漏或半结果，恢复结果完全相同。输出 reserve
后的 push_back 元素为无动态分配值类型，move 返回 variant 不再分配；本构建实测无额外点。
length_error catch 是现有防御路径，本包没有伪造它或声称实际触发；容器 max_size 超限和
UnknownState 也未强制触发。

**现有资源门的位置与静态上界。** 不改20000政策。对象结构／域检查及 C+负位置排除在前，
raw count>40001 和任意 raw 位置越 ±20000 在 order 分配之前拒绝；rawSpan 并无独立的
40001阈值门，而是由合法端点推导。order 分配后检查重复／权限，dense 分配前检查2*span
可表示性，展开中 checkEnd 限制工作端点；最终绝对位置检查在展开后、slots/origins reserve
之前。因此最终 expanded 越界不是“所有分配之前拒绝”。新增 probe 实测40002 raw条、
+20001及-20001位置均0次内部 new即 BoundExceeded；`r@19998 r@19999 X@20000`
在 order+dense 两次分配后 BoundExceeded，没有输出分配；已有最大跨度
`X@-20000 O@20000` 成功，rawSpan=expandedLength=40001，4次分配。未造巨型压力。

有效 raw N≤40001、span≤40001、dense capacity≤80002、最终成功输出≤40001。
本机构建 sizeof(size_t)=8、ArmInstruction=8、TapeOrigin=72；由实际 dense 请求480/6可见
Slot=80。无 C 时同时存活的最大请求 payload 上界为
`40001*8 + 80002*80 + 40001*(8+72) = 9,920,248 bytes`（约9.46 MiB）。
含 C 时非负位置使 N/span≤20001、capacity≤40002；一次 source≤capacity，且同一时间只
存在一个 source、输出 reserve 前所有 source 已析构。因此 source 阶段保守上界
`20001*8 + 2*40002*80 = 6,560,328 bytes`；输出阶段上界
`20001*8 + 40002*80 + 20001*(8+72) = 4,960,248 bytes`。
这些是当前标准库请求的容器元素 payload 静态上界，不含输入对象、分配器/异常/栈开销、
vector 对象、链接库或进程 RSS；不是跨 ABI 的硬内存配额，也不证明最坏运行时间资格。
2*span工作边界仍由既有 checkEnd 保证，本包未另造可达的工作容量越界样例。

**完整执行命令与结果。** 构建最多-j2；没有 reference 类别重跑：

```sh
cmake --build build --target FiniteTapeAllocationTest OpusOptimaTests -j2 > build/finite-allocation-enabled-build.log 2>&1
ctest --test-dir build -R '^FiniteTapeAllocationTest$' --output-on-failure -V
build/OpusOptimaTests --gtest_filter='FiniteTapeExpansion.*'
cmake --build build/verifier-m1-no-native-20261028 --target FiniteTapeAllocationTest OpusOptimaTests -j2 > build/finite-allocation-disabled-build.log 2>&1
ctest --test-dir build/verifier-m1-no-native-20261028 -R '^FiniteTapeAllocationTest$' --output-on-failure > build/finite-allocation-disabled-ctest.log 2>&1
build/verifier-m1-no-native-20261028/OpusOptimaTests --gtest_filter='FiniteTapeExpansion.*' > build/finite-allocation-disabled-pure.log 2>&1
build/FiniteTapeAllocationTest
```

所有命令 exit 0；enabled/disabled 各 CTest **1/1**、纯测试 **4/4**通过。enabled独立
可执行额外直接运行一次以显示完整逐点输出，故本包合计 **69次实际注入**（enabled两轮46，
disabled一轮23；不是69个不同分配点）。两配置的独立目标不依赖 HAS_LIBVERIFY，适用性一致。
两构建日志检索 warning:/error: 无匹配；未运行独立 lint、sanitizer、全套或应用构建。
上一包enabled6/6、disabled4/4、54 native结果仅沿用记录，本包未重跑那两个native测试。
日志及共享纯测试产生的短期文件在build下，结束清理本包四份日志；保留目标和既有build树。

**停止建议。** 就现有明确限定的离线有限展开接口，字面语义、来源、资源边界和本包实际
分配失败／恢复已形成足够的有界验收闭环，建议离线模块验收收敛并停止继续增加工具测试，
交监督审查；这不等于生产认证或性能资格。未接生产raw门，未改legacy/profile/2-10-20，
未启动游戏、触碰存档、提交或嵌套委派。防御分支及生产接线仍明确不在本包证明范围。

### 阶段5：Repeating有限前缀的跨单体键／计数证据（单一正反主题）

本包只修改已有测试及本文，不改生产、legacy、profile或2/10/20预算，不继续扩充已收敛的
有限C/X工具测试。已阅读收敛计划、上文Repeat/Repeating记录、strict puzzle/solution codec、
StaticProductionVerifier范围门、现有Repeating展开测试和锁源reference对应规则。
阶段2/4 A/B/D仍未通过；不是“只剩实机”，不开放Repeating认证。

**fixture与字面预期。** 复用StaticProductionVerifierTest中的WritePuzzle、严格solution writer
和native harness。新增`FiniteRepeatingPrefixRequiresIntermonomerBonds`：非production、倍率1，
raw输出仅Salt@(0,0)、Repeat@(1,0)及一条普通键；字面输入则是六个真实Salt@(q,0)，q=0..5，
五条相邻普通键。输入与out-rep同原点，一条远离材料的空抓取／释放手臂带只用于给reference
提供有限周期，不搬运或生成材料，不调用solver或本地ExpandRepeatingOutput生成oracle。
反例仅删除(2,0)--(3,0)跨单体键：仍是相同位置、相同六个Salt，分成两个三原子片段，
不能把六元素multiset视为同一六单体聚合物。严格codec逐个断言材料类型／坐标、每条键的
bits及端点、raw Repeat及其位置／键、倍率和out-rep端口身份；不是只检查元素计数。

四类身份严格分开：raw Repeat是重复位移标记，不是第二种实际材料；真实材料是六个Salt；
本主题一单体只有一个Salt，五条实键均跨单体边界；完成计数是已验证单体前缀×倍率，
不是raw原子数、输入次数或消耗次数。锁源`decode.c`的repeat_molecule（约611–728）与
out-rep解码（约1017–1067）将placeholder移到末尾、展开实际材料并补边界键，倍率来自
output_scale；`sim.c:1717–1810`按位置、材料类型及REAL_BONDS逐前缀匹配，只有成功才增加
number_of_outputs。本包排除其已注释的surg polymer复杂边界。

**锁源及观测。** 使用asset/libverify.README锁定的omsim
`a88d12d8d100784aa525ee90259255b869bcba38`，本轮核对库SHA-256
`0610b5841ee2a5cca673b4a2f585ab017a8d94fca1ec67fe79a789237625b8c2`；
本地锁源decode SHA-256 `e9357b75e655def905e088c11ee0757876fa0b8866a97b07af860c8052b57f62`，
sim SHA-256 `a7e23da18c225ec24b2337ed691fcc0a468bc27c7b87d4b8dc6644c10a08491f`。
通过verifier.c:1089–1115的有限`product N cycles`查询，不查询throughput或稳态指标。
20周期上限下：正例`product 6 cycles = 1`且无error，`product 7 cycles = -1`并报
`solution did not complete within cycle limit`；断键反例对6和7均为相同有限失败。
这给出有限六单体阈值完成及七单体阈值未完成证据，不声称公开API直接返回了输出计数。
输入不被输出消耗且静止，故不会因等待更久生成第七个材料原子；仍不外推无限完成／稳态。

两个fixture的VerifyIndependent与VerifyIndependentFiles均正确Unavailable。正例detail为
`independent profile excludes Repeat/unknown elements`；断键反例先触发
`independent disjoint-input links not yet established`，不能把后者冒充已到达Repeat门。
没有观察到生产bug，也没有修改生产门来迎合fixture。

**实际命令、失败与修复。** 构建始终最多-j2：

```sh
cmake --build build --target OpusOptimaTests -j2 > build/finite-repeating-build.log 2>&1
build/OpusOptimaTests --gtest_filter=IndependentStaticProduction.FiniteRepeatingPrefixRequiresIntermonomerBonds > build/finite-repeating-test.log 2>&1
build/OpusOptimaTests --gtest_filter='IndependentStaticProduction.FiniteRepeatingPrefixRequiresIntermonomerBonds:RepeatingOutputTest.*' --gtest_output=xml:build/finite-repeating-results.xml > build/finite-repeating-test.log 2>&1
```

首次build成功，单测试失败（exit 1）：两次反例入口detail断言错误地期待Repeat门，实际先触发
不连通输入门。修复测试预期，未修改生产；首次四次native查询均实际执行且预期通过。
之后重建并跑最小组合7/7通过；补倍率assert后再次同命令重建及7/7通过。
最终XML记录reference_cases=4，非skip；本包共三轮各4次，即12次实际native metric查询，
不是12种fixture。仅补充一组正反主题，既有六项纯Repeating测试作回归，不运行旧native
solver资产测试、C/X测试或全套。build有两处既有dangling-else warning（Differential及
OfflineExpansionLockedDecoderInstructionDifferential），未新增warning、未运行lint／sanitizer。
fixture由RAII删除；本包build日志和XML核对后删除，保留所有既有修改与build树。

**本包停止／下一有界实际目标。** 本包足以作为一个有限跨单体键拒绝／计数主题，停供监督。
下一包只做同一有限线性Repeating前缀的异种材料位置身份对照（例如Salt/Fire有序二原子单体，
保持最终元素multiset但交换身份的反例），先给字面坐标／键／有限阈值，再观察锁源reference；
不扩大到surg、彩键、稳态、多输出倍率或整个Repeating实现。若要生产接线，仍需单独设计
限定输出规则、非消耗前缀计数状态与认证门，不能复用普通output的消耗计数来直接开放profile。

### 阶段5：有限Repeating异种材料位置身份对照（离线，不开放支持）

本包仅新增 `FiniteRepeatingPrefixRequiresOrderedHeterogeneousMaterials` 一组正反对照，
复用提取出的 `FiniteRepeatingPrefixFixture()`；旧六Salt断键测试只作为共享fixture的回归。
不修改生产、legacy、profile或2/10/20预算，不扩展材料框架。

**先定规则与预期，再查reference。** 继续锁定omsim
`a88d12d8d100784aa525ee90259255b869bcba38`，本地decode、sim、verifier源文件SHA256分别为
`e9357b75e655def905e088c11ee0757876fa0b8866a97b07af860c8052b57f62`、
`a7e23da18c225ec24b2337ed691fcc0a468bc27c7b87d4b8dc6644c10a08491f`、
`39afee16919834251c0db642d29357525c2e9cb8deaf9761ac5f045acada17a7`；
`asset/libverify.so` SHA256仍为
`0610b5841ee2a5cca673b4a2f585ab017a8d94fca1ec67fe79a789237625b8c2`，本包实际复核全部四项。
`decode.c:611–728,1017–1067` 按Repeat相对原点位移复制材料并补跨单体键；
`sim.c:1717–1810` 按位置定位模板、比较原子类型与REAL_BONDS，前缀首次不匹配即停止增加计数；
边界外键按规则屏蔽，非普通消耗输出。`verifier.c:1089–1115` 提供有限product阈值查询。
因此不是用reference输出反推预期，也不依赖multiset判等。

模板为Salt@(0,0)、Fire@(1,0)、Repeat@(2,0)，普通键0--1、1--2，位移(2,0)。
正例输入六个单体Salt@(2k,0)、Fire@(2k+1,0)，k=0..5，所有相邻位置q--q+1
（q=0..10）都有bits=1键，含全部五条跨单体键。反例唯一改动是交换第三单体
@(4,0)/@(5,0)的类型；两例均六Salt、六Fire，位置、十一条键、倍率1、output、机器和时钟均不变。
首两单体仍正确，避免把反例变成提前无匹配或不连通主题。预先给定有限预期与实际结果一致：

| 输入 | product 2 cycles | product 3 cycles | product 6 cycles |
| --- | --- | --- | --- |
| 六个Salt/Fire | 1，无错误 | 1，无错误 | 1，无错误 |
| 第三单体Fire/Salt | 1，无错误 | -1，cycle limit | -1，cycle limit |

每次查询cycle limit=20，失败错误全文为 `solution did not complete within cycle limit`。
它只证明该有限窗口未到阈值，不能解释成全域不可能、稳态或永久运行。
strict puzzle codec逐项检查材料类型/坐标及键bits/端点，包括Repeat模板；solution codec逐项
检查对象类型/位置/bodyData和指令action/parameter。正反两例direct与files入口均仍为
Unavailable，detail均为 `independent profile excludes Repeat/unknown elements`，不是提前
`independent disjoint-input links not yet established` 门；未开放raw Repeating支持。

**实际验证与失败解释。** 构建命令为 `cmake --build build --target OpusOptimaTests -j2`。
首次exit 1仅因新测试误写指令字段opcode/index，按现有结构改为action/parameter。
之后构建均exit 0。首轮7项组合exit 1，native六查询全部执行且结果符合预期；失败仅为
fixture旧bodyData指令数量0与实际两条指令不一致。现有writer明确由instructions.size()
写入该字段，因此仅将新测试fixture的该字段规范为2，未改模板预期、机器指令或生产。
修正后相同7项组合exit 0（7/7，reference_cases=6）。补显式utility include后最终运行：

```sh
cmake --build build --target OpusOptimaTests -j2 > build/finite-repeating-material-build.log 2>&1
timeout 60s build/OpusOptimaTests --gtest_filter='IndependentStaticProduction.FiniteRepeatingPrefixRequiresOrderedHeterogeneousMaterials:IndependentStaticProduction.FiniteRepeatingPrefixRequiresIntermonomerBonds:RepeatingOutputTest.*' --gtest_output=xml:build/finite-repeating-material-results.xml > build/finite-repeating-material-test.log 2>&1
```

最终build/test退出码均0，8/8通过、无skip；XML分别记录旧测试4、新测试6次查询。
本包新主题三轮各6次，加最终旧回归4次，共22次实际native metric查询，不是22种fixture。
每轮测试进程wall上限60秒，每次native查询20周期；未执行throughput、游戏、全套或额外native
资产测试。构建仍只有两处既有dangling-else warning，未运行lint/sanitizer。
fixture由RAII删除，本包日志/XML核对后删除，原有build锁源和工作区修改保留。

**阶段5缺口与停止方案。** 本包补足有限材料位置反例，不代表独立验证器已有raw模板有限图
语义、非消耗前缀计数状态或认证接线；阶段2/4原有A/B/D缺口亦未消除，不能声称只欠实机。
下一项只允许实现下述纯离线有限图契约；旧“逐位置比较连通分量”的概要不足以直接编码，
以下精确定义取代它。本轮仅更新本文，不实现、不构建、不重跑上述8项，完成即停供主会话批准。

#### 下一有界实现前置契约：离线有限Repeating图检查器

**证据与范围。** 规则依据仍为上述锁定revision的源码，不从有限metric结果猜规则：
`decode.c:611–765,1017–1067`（marker末置、方向键复制、跨单体补键、行边界），
`sim.c:46–51,1649–1663,1717–1810`（polymer坐标、ignored区域、逐单体匹配和计数）。
本轮直接阅读这些锁定源码；未执行reference。已有两个有限fixture是消费者／验收证据，
`solvers::ExpandRepeatingOutput`是被对照对象，不可充当oracle；它的固定六次、坐标byte范围
及AssemblyPlanner上限不是本工具的资源政策。源码足以支持以下明确的非surg子域；不声称
所有单Repeat模板均落在其中。域外直接停止该调用，不能降级为猜测式checker。

**唯一API草案（批准后才建文件）。** 放在`opusoptima::verification`，不接受puzzle、solution、
worker、reference或可变profile参数；所有坐标均在同一输出局部坐标系，原点固定(0,0)，
调用者负责已建立的刚体变换，本模块不猜方向／平移、不自动找最佳匹配。

```cpp
struct FiniteRepeatingGraphRequest {
    const io::Molecule& rawTemplate;
    const io::Molecule& component;
    core::HexCoord displacement;
    std::size_t maxMonomers;
};
struct RepeatingGraphCell { std::int64_t q, r; };
enum class RepeatingGraphDifferenceKind { MissingAtom, AtomType, BondMask, UnexpectedAtom };
struct RepeatingGraphDifference {
    RepeatingGraphDifferenceKind kind;
    std::size_t monomerCount; // 首个失败的阈值，1-based
    RepeatingGraphCell cell;
    std::optional<RepeatingGraphCell> neighbor; // 仅BondMask
    std::optional<core::AtomType> expectedType, actualType;
    std::uint8_t expectedBits{}, actualBits{}, allowedBits{};
};
struct FiniteRepeatingGraphChecked {
    std::size_t provenPrefix; // [0,maxMonomers]，不是累计输出数
    std::optional<RepeatingGraphDifference> firstDifference;
};
enum class RepeatingGraphFailureCode { Malformed, OutsideDomain, ResourceUnavailable };
struct FiniteRepeatingGraphFailure {
    RepeatingGraphFailureCode code;
    std::string detail; // 稳定原因文字；无部分成功／前缀
};
using FiniteRepeatingGraphResult =
    std::variant<FiniteRepeatingGraphChecked, FiniteRepeatingGraphFailure>;
FiniteRepeatingGraphResult CheckFiniteRepeatingGraph(const FiniteRepeatingGraphRequest&);
```

不提供expected图公共API、策略插件或第二种分子表示；内部仅需int64坐标索引与六方向bits。
成功分支包括**无匹配**（prefix=0且difference存在）；prefix=n时difference为空，只表示该
有限上限全通过，不能叫完整／无限输出，不能断言第n+1失败。其他成功有且仅有firstDifference
指向prefix+1。重复同一快照返回同一结果，不写输入，不记录／消耗材料，无输出倍率参数。

**输入结构、域与失败优先级。** 先无分配检查数量／可表示性，再完整结构检查，再域检查，
最后匹配；资源预检超限优先返回OutsideDomain，不承诺为超大对象扫描所有Malformed。
在资源界内，任一结构错误优先于域错误，均不得返回一个“已证”部分结果。

* Malformed：任一分子原子坐标重复；未知AtomType数值；键端点不存在、自环、非六邻接、
  无向重复键（含反向重复）、bits=0或含0x0f外位、Normal与彩键混合；n=0；位移为零或
  与唯一marker坐标不一致。所有距离计算先提升int64，禁止调用可能int溢出的HexCoord::s／Add。
* OutsideDomain：空模板／无真实材料、不是恰好一个Repeat、component含Repeat；合法彩键；
  下述轴向／非surg／连通条件不成立；现有资源上限不足。component为空是合法无匹配快照，
  不属于空模板；非空component必须是一个完整连通分量，不能将多个分量拼凑证明前缀。
  原子身份限现有16种中的15种真实材料，全部精确比较，不实现VARIABLE_OUTPUT通配。
* component表示完整的某一个候选材料连通分量，包括通过边界连接的剩余材料，不是裁成
  前n单体的诱导子图；断键后应由调用者按连通分量分别调用。其他无关分量不在本次参数中。
  结构合法但缺原子／断键／类型错位／多余连接都由比较给出无匹配或较短前缀，不当作Malformed。
  例如断键仍保持整体连通的环形反例可直接比较；分裂的链则分别比较，不能传成伪component。
* ResourceUnavailable只用于bad_alloc／length_error等容器资源失败，结果variant失败原子性；
  不捕获未知内部错误冒充无匹配。已有资源门超界是OutsideDomain，不是此分支。

**可直接判定的非surg子域。** displacement=d=w*e，w为正整数，e为六个单位hex方向之一；
其他斜率属于OutsideDomain而非非法游戏模板。令f=(-e.r,e.q+e.r)，定义
`u(p)=f.r*p.q-f.q*p.r`、`v(p)=-e.r*p.q+e.q*p.r`。这正是锁源变换，轴向时行列式为1，
`u(d)=w>0,v(d)=0`；不需要浮点／除法求逆。每个真实材料行v的最小／最大u记L[v]/R[v]，
要求从min_v至max_v每行都有真实材料，且`R[v]-L[v]<w`；真实材料必须含(0,0)。
raw模板（含marker）连通，下面生成的两个单体真实图也须连通。由行宽条件，平移副本
互不重叠，外侧单体必在该行有限包络外，不存在surg的行交错；连续占用行使锁源空行哨兵
和跨空行插值无需实现。这里是有意收窄的可检查域，不以两个fixture代表一般非surg。
两个单体连通保证每个更长前缀连通；无需试算无界副本。还要求周期真实顶点的任何几何
六邻接只发生在同副本或相邻副本：枚举raw真实点p、q和六方向t，对
`p+t-q=j*d`的整数解检查`|j|<=1`，否则OutsideDomain。该有限检查不可仅由行宽条件推断。
原始坐标负值、内部孔洞、分支并非自动拒绝，但仍须满足上述条件与逐位置比较。marker不要求为vector末项。**实施前锁源纠正（2026-11-04）：** decode.c:1032–1039
不是稳定移除marker，而是将首个marker与末项交换；例如`[Repeat,A,B]`变为`[B,A,Repeat]`。
补键循环依赖此顺序，故内部先精确执行该交换，再遍历真实材料；不可保留原排列冒充锁源。
对域内互反签名，移动marker仍可同义，但不以此取代交换规则。

**marker、方向键和期望图。** 令m为真实材料数。marker只声明位移，既不替换成一种真实
原子，也不按marker的类型复制；从顶点集剔除它。对每条raw无向键建立两端的六方向bits；
复制每个真实材料的类型与全部方向bits到`p+i*d`。marker自身方向bits不复制，但真实材料
指向marker的方向bits**必须保留**：它们可能在下一副本接到真实原子，而非简单删掉整条键。
在两个副本中按**副本序、各副本raw原顺序**遍历真实原子i<j，对六邻接j执行
`bits[j,t+3] |= bits[i,t]`，这与锁源补键的方向、顺序相同；取第二副本各真实材料的
方向签名并平移回模板坐标。不能改成无条件双向OR或自动补齐所有相邻原子的边。
本域额外要求该签名按周期坐标查找后每条实键互反且bits一致，否则OutsideDomain；这是
可检查的域限制，不是修改锁源规则。上述副本邻接门明确保证只涉及相邻副本，故检查
前／后各一层邻居足够，不调用现有六次展开函数。
若任何非零方向键目标在周期真实顶点集中不存在，或补边后产生不支持的bits，返回
OutsideDomain，不能凭空创造原子。重复实键规范化为一个无向边，其bits按规则并集；
这不豁免输入raw重复键的Malformed。生成n个副本的期望顶点后，仅在两端都存在时输出
内部无向边；末端悬向未来的方向签名保留在内部模板，不写成悬空MoleculeBond。
重叠原始顶点是Malformed；不同副本重叠是OutsideDomain（行宽门已排除，但仍需防御断言）。
component重叠坐标也绝不能通过“后一项覆盖前一项”吞掉。

**边界mask精确定义。** 对阈值k≥1，锁源ignored谓词在本域化为：
`I_k(p) = min_v<=v(p)<=max_v && (u(p)<L[v(p)] || u(p)>R[v(p)]+(k-1)*w)`。
行范围以外**从不ignored**；行包络内的空洞也不ignored。不能把“期望图中不存在”当成ignored。
对每个期望原子p的每个hex方向t，令q=p+t；允许mask为`A_k(p,t)=0x0f`当I_k(q)，否则0。
比较`((actualBits ^ periodicExpectedBits) & (0x0f ^ A_k)) == 0`。
本域实际只允许普通键，所以ignored侧“存在普通键”与“无键”都通过；类型比较永不屏蔽。
一条额外键只有其**在域内端点所指的另一端**属于ignored才允许，不是整个边界原子任意免检。
其余额外键（包括指向孔洞、上下越行）、缺失内部键／跨单体键均失败；缺失向未来／左边界
的键允许。ignored区域的真实原子身份、数量、键无需匹配，但其在component中的存在和
原始键仍须结构合法、且完整连通。component中每个非ignored原子须唯一对应期望顶点；
额外非ignored原子失败，每个期望顶点须存在且类型一致。因此不能只比multiset、总数量、
诱导边或要求整个分量大小恰等于k*m。锁源的ignorable_atoms余额检查在本域等价于这一双射。

**最大已证前缀与计数。** 必须依次判k=1..n，首个失败立即停，返回此前连续成功的k-1；
即使更大k可能匹配也不得跳过失败。无前态的工具只证明当前快照从1开始的连续前缀，
不是锁源跨周期历史计数的替代。锁源从`1+number_of_outputs/outputs_per_repetition`起查，
成功置`number_of_outputs=k*outputs_per_repetition`，不remove_molecule，等待不重复累加。
未来集成必须另存每输出端口的已证历史高水位、倍率和起查阈值；不能把每次本API结果相加、
不能在普通output消费后套用，更不能因本次从1失败而抹掉历史高水位。有限fixture倍率1的
product2/3/6仅是阈值证据；无计时／历史输入的API不返回完成cycle或已消费输出数。

**资源来源与int64安全。** 只读`finiteOrdinaryProfile.maxMoleculeAtoms=64`（raw含marker）、
`maxInventory=8192`（component顶点和n*m期望真实顶点各自的界），不添加新policy常量。
先用`n<=maxInventory/m`避免乘法溢出；因此m≥1时n≤8192。键先用hex简单图度≤6推导
`E<=3V`做无分配数量预检，后检查唯一端点／邻接；超过只是域外，不按超大键表分配。
两副本／边界签名工作量≤常数*m，是从定义推导的工作空间，不是提高inventory政策。
不挪用maxTapePosition=20000、maxPartCoordinate=1024、150000周期、AssemblyPlanner或
solver预算限制材料坐标，也不把puzzle byte坐标限制套到运行材料快照。输入HexCoord的int
在受支持现有32位int平台先提升int64；位移及每次乘加、变换、邻居、行界全用int64检查，
内部不窄化为HexCoord。n≤8192、各输入分量int32使`p+i*d`绝对值<2^45，轴向变换系数
仅-1/0/1，行界／差值<2^47，远小于int64；仍对size_t和容器max_size作显式检查，不依赖
溢出后再检查。不支持更宽int平台时须编译期约束或走检查算术，不靠默默截断。

**算法、确定性与有限成本。** 在资源／结构／域检查后，以排序坐标表建raw/component索引，
生成周期签名及n*m期望顶点和内部边；按k递增应用上述行mask并比较。不得每次通过后删除
component，或裁去“多余”原子。差异先查缺失顶点，再查类型，再按顶点(q,r)和固定方向序
`(1,0),(0,1),(-1,1),(-1,0),(0,-1),(1,-1)`查方向bits，再查额外非ignored顶点；同类按坐标
字典序，缺失／额外类型optional按存在性填写。BondMask中的expectedBits为周期签名，
actualBits为实际方向bits，allowedBits为该方向的0或0x0f；不是经过mask的伪装原值。
不要求失败对象提供所有差异或分量搜索框架。设T=raw顶点数、V=component顶点数、P=n*m，
简单图E=O(T+V+P)。副本邻接域检查O(T²)；排序和生成O((T+V+P)log(T+V+P))；逐阈值最多扫描V与k*m个期望顶点、
各六邻居并二分查找，故保守总时间O(T²+(T+V+P+n*V+m*n²)log(T+V+P))，
空间O(T+V+P)，n,V,P≤8192、T≤64，均有限；不是实时／最坏墙钟资格。不新增全局cache、
timeout或更大默认预算，暂不为渐进优化加框架。

**准确改动落点与最小验收（下一包，尚未执行）。** 只新增
`src/Verification/FiniteRepeatingGraph.hpp/.cpp`、`tests/unit_test/FiniteRepeatingGraphTest.cpp`，
在`CMakeLists.txt`的`OPUSOPTIMA_VERIFICATION_CORE_SOURCES`登记实现一次；测试由现有
`UNIT_TEST_SOURCES`的CONFIGURE_DEPENDS glob自动收录，不另加重复源项。更新本文实施结果。
不得修改`StaticProductionVerifier`门、`IndependentCapabilities`数值、solver展开或worker协议。
手写图先于任何reference断言，最小组合为：

1. 单Salt／位移1，n=1与6；marker不在末项也同义；手写六Salt五键得prefix6、n=7得6。
2. Salt/Fire／位移2，手写十二原子十一键；第三单体交换得prefix2和类型差异；相同multiset
   不得通过。六方向中的反向轴验一个，非轴向位移显式OutsideDomain。
3. 断跨单体键后的两真实分量分别查，最大只3；另用保持连通的手写图删一条必需键查BondMask，
   加一条非ignored连接也查BondMask。不以传入断开的伪component代替断键负例。
4. 首尾向ignored续接／不续接都允许，证明无消耗；ignored侧额外原子允许，包络孔洞或越行
   额外原子不允许；同一坐标类型错位必失败。验mask逐方向，不用图生成器产生预期。
5. 原始重复坐标／重复反向键／缺端点为Malformed；合法非surg域外（行交错、空行、彩键、
   多marker）为OutsideDomain；空component得prefix0；n*m在8192及越界、int32极值提升
   无溢出、分配失败无半结果。副本重叠须拒绝，不靠map覆盖。
6. 复用现有`FiniteRepeatingPrefixRequiresIntermonomerBonds`及
   `FiniteRepeatingPrefixRequiresOrderedHeterogeneousMaterials`的字面模板／材料数据，
   离线结果对照既有锁源阈值表；保留它们现有reference与Unavailable断言，不新增native查询，
   不宣称锁源直接返回了prefix。未来实际实现再按最小测试目标验收，本包不重跑原8项。

**集成前仍需什么。** 本离线实现即使通过，仅是有限fixture对照／未来输出判定的图内核。
阶段2/4原定义的A/B/D仍须各自按原验收关闭：A的wrapper身份、getter间effect域和生命周期
稳定性；B的实体类别映射、成员／配对、生成／移除及排除覆盖；D的完整语义资格门与双方
Passed有效成本配对（不能计Unavailable收益）。这里不改名、缩减或用图测试替代，C的
空间运动／四指标口径也没有因本工具而关闭。还需独立认证输出坐标变换与中心分量选择、抓持／
输出时序、完整材料快照、跨周期非消耗高水位状态、倍率与多端口计数、有限完成cycle，以及
raw入口／capability fail-closed映射。尤其无状态prefix不等于普通output的消耗事件，不能
复用其remove／消费计数。legacy/profile/2-10-20预算保持不变，不接生产、不开放支持。
文档契约到此停止；无新增fixture、实现、构建、游戏／存档操作、提交或嵌套委派。

### 2026-11 有限 Repeating 图检查器实施结果（批准后的限定实现）

上述“文档契约到此停止”保留为历史设计状态；本次批准后新增
`src/Verification/FiniteRepeatingGraph.hpp/.cpp`、
`tests/unit_test/FiniteRepeatingGraphTest.cpp`，实现登记到verification core，单测由既有glob收录。
仅复用既有独立进程`tests/worker/FiniteTapeAllocationTest.cpp`做分配失败验证，未新增测试框架。
没有生产调用、profile／资源常量、solver展开或raw认证入口变更；legacy／2-10-20预算不变。

实现前对照锁源发现marker移除次序不能稳定删除：decode.c:1032–1039先将首个marker与末原子
交换。因此已先纠正本契约再实现；最小证据为`[Repeat,A,B] -> [B,A,Repeat]`，真实原子次序
为`[B,A]`。双副本按此次序做i<j单向OR，再采用第二副本签名；不是生成器oracle。

结果是从k=1起连续成立的有限`provenPrefix`及首差，既不是累计输出数、已完成周期，也不是
native历史阈值状态。支持域仍严格限于上述单marker、普通键、轴向位移、逐行包络不重叠且
无空行、原点真实原子、连通及互反周期签名等条件；不是全部单Repeat、surg、变换搜索或
无限稳态。结构／资源／域失败均不返回部分prefix，输入只读且重复快照一致。

手写图覆盖十五种真实原子、六轴、同质／异种顺序、缺原子、保持连通的缺键、额外键、
ignored首尾连接／不连接、包络孔洞／越行额外材料、类型不可mask、坐标／键结构错误及域／
资源拒绝、int32极值提升到int64、8192库存与越界。对断开的六原子链提交两真实分量，
原点分量最多prefix3，另一分量为0，不将断图伪装成单component。额外材料负例在键检查阶段
即可首差BondMask，不能强求后续UnexpectedAtom优先。

首次enabled选择运行9项（7项新测试加2项既有锁源fixture）为8通过、1失败：新测试误将
`Salt(0,0)—Fire(0,1)—Repeat(1,0)`视为无周期目的地。该斜marker键实际指向下一副本Salt，
确有互反签名；纠正负例预期为合法域内空材料prefix0，没有放宽checker。另修复range-loop
复制告警，失败对象以move返回，分配异常使用短detail避免恢复路径再分配。

实际验证命令与结果（构建均至多-j2）：

- `cmake --build build --target OpusOptimaTests -j2`成功；首次执行过滤器
  `FiniteRepeatingGraph.*:IndependentStaticProduction.FiniteRepeatingPrefixRequiresIntermonomerBonds:IndependentStaticProduction.FiniteRepeatingPrefixRequiresOrderedHeterogeneousMaterials`
  得到上述8/9；修正后`./build/OpusOptimaTests --gtest_filter='FiniteRepeatingGraph.*'`为7/7。
- `cmake --build build --target FiniteTapeAllocationTest -j2`及完整运行成功：checker成功路径25个
  分配点逐点施加持续bad_alloc，均原子返回ResourceUnavailable，恢复不再分配，重试prefix2；
  原有有限tape的23次注入及边界检查也仍通过。
- 复用既有`build/verifier-m1-no-native-20261028`（`OPUSOPTIMA_ENABLE_LIBVERIFY=OFF`）：
  `cmake --build build/verifier-m1-no-native-20261028 --target OpusOptimaTests FiniteTapeAllocationTest -j2`
  成功；该目录下执行相同新测试过滤器为7/7，完整分配runner同样通过25个checker注入点。
- 本次实际reference查询共10次：同质fixture4次、异种fixture6次。两既有fixture均通过，
  direct／file raw Repeating入口的Unavailable断言保持通过；没有新增native查询，也没有让
  reference直接输出prefix。只作这两个字面fixture的有条件消费者／阈值差分，不泛化证明。
  公共行为未改变，未运行2254项全集；无游戏／存档操作、提交或嵌套委派。

设T为raw原子数、V为材料原子数、m为真实单体原子数、n为请求上界、P=mn≤8192，
有序容器实现的保守时间上界为
`O(T² + (T+V+P+nV+mn²) log(T+V+P+1))`，额外空间`O(T+V+P)`。
所有坐标平移／包络运算用int64，先做除法式库存界检查才乘n*m；没有无限扩展。
至此限定模块实施验证结束，仍须监督及前述A/B/D与生产集成资格验收，不据此开放支持。



### R4 下一独立有限包：opt-in Repeating 非消耗进度层（非认证）

本包先固定调用边界：新增 `FiniteRepeatingProgress`，显式构造有界端口配置后，
`Observe(port, rawTemplate, component, displacement)` 实际调用现有有限图 checker；不接受
可伪造的外部 prefix，不保存分子或逐次历史。每端口只保存配置与历史最大 provenPrefix，
计数为 `highWaterMonomers * outputMultiplier`，绝不将多次 prefix 相加；较短／重复快照不
撤销进度，任何失败（包括预算耗尽）均不更新状态。沿用 `io::Molecule` 完整连通分量，
输入只读，不走普通 `ConsumeOutputs`、不删材料、不增加 emitted 或普通输出事件。

规则重新核对本地锁源 `build/omsim-{decode,sim}-reference.c`，SHA256分别为
`e9357b75e655def905e088c11ee0757876fa0b8866a97b07af860c8052b57f62`、
`a7e23da18c225ec24b2337ed691fcc0a468bc27c7b87d4b8dc6644c10a08491f`：
`decode.c:1058` 的 outputs_per_repetition=output_scale，`:1314` 的默认目标为6*scale；
`sim.c:1780–1801` 从历史计数/倍率+1检查并赋值，不累加快照，也不消耗材料。
故配置显式接收已知的有限 targetMonomers（1..6），默认6；倍率沿现有有限域1..8，
目标输出阈值为 targetMonomers*倍率。1..6仅为有限阈值功能，不泛化为任意游戏目标；
已有reference六／七阈值和异种2/3/6证据仅证明各次有限查询，不证明跨快照历史层。

资源：固定最多16端口；配置 maxMonomers 非零且不超过 maxInventory=8192，checker继续
按实际模板真实材料数检查总顶点界；无历史／快照集合。另在进入非抢占式 checker 前限制
`maxMonomers * (component原子数 + rawTemplate原子数 * maxMonomers) <= 65536`，
超出返回 ResourceUnavailable，不修改状态；8192库存上限不等于保证接受全部8192前缀检查。
自显式构造起共享一个总2秒截止期，可传外围既有截止期并取两者较早者；
检查前后均把超时作为失败，提交状态前最后检查；不为每端口或快照重置预算。
这不是硬实时抢占保证。同一端口的 rawTemplate、输出局部坐标系及 displacement 必须
由调用者保持稳定；不同模板须建立新组件，不允许混用同端口历史。
只建立这一层状态，不实现机械Repeating replay。下一明确接线位置是未来非认证
`ReplayFiniteMachine` 的 Run 输出观察阶段：从 inventory 提取完整连通分量并刚体变换为输出
局部坐标后调用 Observe，再另行映射专用 repeating 进度；目前 Run 的 report.outputs 是
消耗式计数，不能直接冒充此状态。此次完全不改该路径、VerifyIndependent、worker发布、
legacy/profile/2-10-20；raw Repeating 默认仍应 Unavailable。不重跑已收敛2254或tape认证类别。

本包实施验证：新增独立组件并登记 verification core；在既有真实有限链 fixture 上新增4项
消费者测试，覆盖增长／重复／缩短／空快照、异种材料有限阈值、端口隔离、倍率、原子与键
逐项不变、错误／域外／超时／工作量拒绝的失败原子性及默认 raw Repeating Unavailable。
边界试验发现8192链的原checker非抢占计算可远超2秒，已在本组件增加上述保守工作门，
同一边界现快速返回 ResourceUnavailable、历史保持零；未修改原checker或默认执行路径。
最终 enabled `build` 与 disabled `build/verifier-m1-no-native-20261028` 各以
`cmake --build <dir> --target OpusOptimaTests -j2` 构建，执行
`OpusOptimaTests --gtest_filter='FiniteRepeatingProgress.*:FiniteRepeatingGraph.*'`，
各11/11通过（43/44ms）。未新增reference查询；锁源赋值规则是历史层的设计依据，
消费者测试不是一次reference查询对历史状态的证明。未接完整机械执行／生产认证，
本限定组件完成后停供监督，不代表R4完成。


## R3 有界实际预算包：有限 tape 转换取消（2026-11-05）

本包只修复显式 `ReplayExpandedFiniteMachine` 接受域中的转换循环取消缺口；
不声称 R3 全过，不扩认证或 profile。已读 R3、两条显式入口、Replay Budget、
Repeating progress 工作门及图检查实现。选择 tape：原转换没有内部 deadline，
入口只在转换/复制后检查总时钟。可控已过期 deadline 的域内 `GrCC` 探针在 red
阶段仍返回成功 tape，首个 failure-variant 断言失败（0ms）；这是合作取消契约
缺口的确定性复现，**不是**域内运行必然超过2秒的实测证明。

`ExpandFiniteArm1Tape` 现在接受可选外围 steady-clock deadline；无参旧调用不启用
取消。显式执行入口传入原 started+2s，不重置预算、不增加配额。初始结构/域验证
（含排序、重复索引、权限）之后先检查；外层指令、C源扫描/每复制站点/每复制槽、
X历史扫描/旋转归一、最终 slots/origins materialize 每槽及返回前检查。
使用与原 expanded 入口及 Replay Budget 相同的严格 `now > deadline`，抛既有
ResourceUnavailable；局部 tape 销毁，failure variant 不携带部分 tape，执行不开始。
入口既有末尾 Timeout 覆盖保持，故实际总deadline耗尽仍为 Timeout，而非 CycleLimit；
cycleLimit=1 仍由原执行逻辑报告 CycleLimit。初始 malformed/域错误保留优先级，
后续 Conflict/BoundExceeded 等可能被已经到期的取消抢先，此为新增合作取消语义。
原 rawSource 快照及“成功转换后执行超时仍保留完整展开观测”的语义未改。

静态工作量：接受域 raw<=40001、rawSpan<=40001、内部capacity<=80002槽；
每个C冻结源/站点复制与每个X历史扫描各最多capacity槽，站点/指令数<=40001。
故可用40001*80002作为各类嵌套槽访问的保守乘积界，绝非声称这一乘积都能实际
达到；新检查将这些嵌套循环分为逐槽合作取消片段。X旋转归一最多约capacity/6+1步，
也逐步检查；生成动作最多4条。检查之间仍可能有一次最多80002 Slot的vector
构造/销毁、reserve或其他分配；初始排序及验证前扫描也不抢占。没有硬实时保证。

另一入口静态观察（本包未改）：Repeating Fragment 每次完整 bonds扫描前已有Budget，
库存<=8192，单轮最多3*8192条键，增长轮加终止轮保守<=8193；不是逐边取消。
Observe前后有deadline，progress已有65536工作门，实际接线maxMonomers=6；
图prefix每轮三遍expected（邻接一遍每点6方向）加一遍actual，raw<=64、
component<=8192，周期几何对照<=63*63*6，双副本签名<=126 choose 2。
这些界不等于裸checker任意配置或全部库存都能2秒完成。

验证（离线、串行构建，未与资源测量并行）：
- red命令：`cmake --build build --target OpusOptimaTests -j2` 后执行
  `build/OpusOptimaTests --gtest_filter=FiniteTapeExpansion.ExpiredEnclosingDeadlineCancelsCopyWithoutPublishingTape`；
  build成功、测试如上失败，随后加入真实检查修复，不遗留忽略deadline的占位代码。
- green对 `build` 和 `build/verifier-m1-no-native-20261028` 分别运行
  `cmake --build <dir> --target OpusOptimaTests -j2`，然后
  `<dir>/OpusOptimaTests --gtest_filter='FiniteTapeExpansion.*:IndependentFiniteReplay.ExplicitExpansionExecutesRawWithoutAuthorizingIt:IndependentFiniteReplay.ExplicitCopyPreservesUnorderedOffsetSource:IndependentFiniteReplay.ExpansionCategoriesRetainFailureAndNeverExecute:IndependentFiniteReplay.ExpansionFailureNeverExecutesPartialTape'`。
  enabled 9/9（44ms）、disabled 9/9（37ms）。日志在
  `build/r3-tape-budget-{green-build,green-tests,disabled-build,disabled-tests}.log`。
- 新测试同时覆盖过期C/X/普通/空tape、未传deadline/未来deadline成功、重复index
  在过期时仍Malformed。过期测试只动态证明首次取消点，不冒称中途每个点均被触发；
  其余循环取消间隔是代码级界。既有golden/错误分类/失败不执行/有限CycleLimit定向回归通过。

未覆盖：rawSource/整机复制、setup其余循环、初始验证扫描/排序/单次分配的内部取消，
Repeating逐边取消与裸图checker全配置；本包没有实际极值计时或全域最坏耗时证明。
默认 Replay/VerifyIndependent 未接入新deadline，因此不重跑同门2254、身份fixtures或
旧Repeating类别。未改legacy/profile/2-10-20及认证关闭状态；完成这一包后停供监督。

### R3 后续实际有界资源观察（2026-11-05，停供监督）

[报告、probe源码、原始三轮数据与环境/hash](../result/r3-bounded-resource-20261105/report.md)。
既有文件worker/串行语料工具不调用两个opt-in，故最小独立probe链接现有Debug core；
没有新benchmark框架、游戏测量或冻结语料重复计时。先串行-j2构建，再六场景各三进程。

- 最大合法tape：逆序X@20000..-20000共40001条/span40001，真实展开40001槽并执行，
  Completed/output6；API min/median/max=63.718/69.661/71.435ms。
- Repeating直接图工作：raw64、7680原子链、maxMonomers8，work65536恰饱和，
  Checked/highWater8，56.873/59.094/66.258ms；8192原子最大组件、maxMonomers7，
  work60480，Checked/highWater7，55.666/58.582/81.354ms。
  这是门饱和与最大组件见证，不是全域最重拓扑证明，也不是整机8192原子处理证据。
- Repeating显式整机入口：input64、raw output2，Completed/output6/acquired64/remaining64，
  1.436/1.567/3.919ms，certificationEligible仍false。
- 相邻拒绝单列：tape40002条BoundExceeded（无展开/执行）0.957..2.062ms；
  graph work65544明确ResourceUnavailable（无图checker/highWater仍0）0.008..0.011ms。
  拒绝不是成功或性能收益。全部18进程符合预期，无超时，未重跑合作取消双配置9/9。

API计时含入口复制/setup，fixture另计；父进程墙钟仅后两轮可得。RSS是进程高水位，
后两轮受Python启动器继承高水位污染，不可作为API内存增量或收益。三样本不作p95置信。
本机成功API最大观测81.354ms，没有>2s API/setup实际反例；但没有setup分段仪表，
初始验证/sort/rawSource/整机复制/vector/setup中途检查缺口仍在，不能证明全域最坏墙钟。
不因未见反例而修算法或扩大许可域；其他主机/配置/拓扑/单次分配仍未证。

这些证据足以结束本次新增模块R3局部实测工作，**不等于R3全局通过或D性能资格通过**，
不代替R4/R5剩余资格项，不重新开放A/B。legacy/profile/2-10-20与原预算未变；
独立临时binary取hash后清理，保留报告与可审计数据，停供监督。


## R4 当前功能切片全新845扫描（2026-11-05，停供监督）

[独立新包报告](../result/r4-fresh845-20261105/report.md)：使用累计集成hash匹配的现有CLI／共享库／workers，不构建、不安装，原LocalCoverage串行169题×五后端845次、314.399秒、exit0。输入逐文件hash与固定2026-11-03基线一致，真实fixture169/169、byteDistinct146/146、journal37/37；170候选成功、675既有solver拒绝、外层timeout0、显式Unavailable0（675无verification JSON，不代表全能力可用）。共同成功170对四指标、成功集合、provenance均无变化，产物／runner／输入前后hash一致。

实际authority/profile仍legacy-gate/legacy-native，2/10/20预算与原solver选项不变；两个显式opt-in未扫描，不能据此算覆盖或认证。本包只补当前功能切片缺失类别，不是最终R5；未重跑2254／18场景。R1身份／生命周期／跨getter出边、R2连续语义／面积／四指标成本配对、完整R3全边界／setup／取消性能资格、R4/F完整足迹／时序／竞争与认证接线仍未闭，不能称只剩实机。下一步需监督明确批准R1生产接线闭合包的范围与完成判据（或明确另选R2），不自行扩大fixture循环。
