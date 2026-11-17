# OpusOptima

> **当前方向**：第 1–6 项已完成。146 道字节去重参考题上，当前求解器总量为 OpusSolver oracle 的 cost **0.857×**、cycles **0.742×**、area **0.821×**；55 题三项全胜，0 题三项全负。第 7 项外部验证：全新 300 题抽样已 **300/300** 通过；此前超时的 `GEN737` 约 12–14 秒完成。2401 题全量扫描单独记录。独立认证路线继续暂停；固定 omsim、legacy 保护、显式旧后端和 2/10/20 默认预算保留。旧已见题不称 unseen。现有 omsim binary 仅锁 hash，不声称源码可重现，勿从移动 release URL 替换。

用于解析、渲染和求解 Opus Magnum 谜题文件的 C++20 工具包。

[English](README.md)

## 当前状态

旧验收 gate 的最新验收为 **169/169 个原始文件**、字节去重 **146/146**，其中
journal **37/37**。该次扫描对每个文件重新运行五种后端，共 **845 次尝试，每次限
20 秒**，没有拼接历史结果；对应完整回归 **662 项通过，零失败、零跳过**，题库通过集合
与 B3–B6 基线一致。

这些是当前本地题库的实测结果，不保证任意新题可解、指标全局最优或与原游戏完全等价。

- [完整覆盖报告](result/verifier-m2-m6-certified-legacy-fresh-20261029/coverage.json)
- [基线比较](result/verifier-m2-m6-certified-legacy-fresh-20261029/comparison.json)
### 参考题库结果（1–6 项）

146 道字节去重参考题的最终审计总量为 cost **89,825**（oracle 的 0.8567×）、
cycles **66,427**（0.7420×）、area **20,285**（0.8211×）。逐槽胜/平/负为
cost 75/70/1、cycles 66/76/4、area 70/73/3；55 题三项全胜，0 题三项全负。
仅机械槽（`instructions>0`）为 0.9657× / 0.9887× / 0.9733×；
pipeline/cheap-arms 为 0.9283× / 0.9624× / 0.9320×；universal 为
0.8794× / 0.7007× / 0.8438×。第 6 项中，71/146 题存在至少一个单解在三项上
都不劣于 oracle 且至少一项严格更好。两次 `final3`/`final4` 审计槽位完全一致
（0/438 差异）。外部 2401 题库单独统计：300 题抽样已 **300/300** 通过（[结果](result/external-300-20261116/results.json)）；
此前超时的 `GEN737` 原因是空臂路线搜索耗时，该搜索现已受共享 deadline 限制，
约 12–14 秒完成。

- [B3–B6 实现、证据与能力边界](tests/functional_test/ProductionB3B6Progress.md)

## 仓库结构与开发流程

仓库只跟踪源码。构建目录、日志与实测报告都在仓库之外的 `../artifacts/` 下，通过
`build/`、`log/`、`result/` 三个符号链接暴露，因此 `git status` 只会显示源码、测试、
题库和工具。

- 本 README 是唯一的项目文档，不再维护独立的 `docs/` 目录。
- [`tools/README.md`](tools/README.md) — 测量脚本

```sh
cmake -S . -B build && cmake --build build -j4
env -C . ./build/OpusOptimaTests          # 必须在仓库根目录运行整套测试
python3 tools/bench_corpus.py             # 改动默认值前必须跑的题库 A/B
python3 tools/probe_layout.py --count 12  # 发射器层 intake 与 feeder 对比
```

## 受限连通行候选

普通 RowAssembly 有一个有界的连通行供给／交接候选，覆盖直接单原子供给与完整的
一至两行水平链（宽度 2–8）。它把已连通的一行作为单件搬运，而不是独立并行原子。
`OPUSOPTIMA_ROW_COMPONENT=on` 才会显式替换 clearance 组合槽位；未设置、`off` 与无法
识别的取值都保持原有 clearance 槽位和共享配额。该候选沿用既有配额和完整验解。这是
实验机制，不主张能改善真实组合的胜出结果。机制与适用范围见
[机制说明](result/solver-component-row-round7/mechanism.md)，实测见
[第 7 轮证据](result/solver-component-row-round7/report.md)。

## 外部 OpusSolver 题库

上游锁定提交 `fdedd3f01a238b941ceba8bace7ea35cbd3b8c41`（`gtw123/OpusSolver` 的
`test/puzzles`）的 2401 个题目文件现已**并入常规题库** [`asset/puzzle/`](asset/puzzle/)：
其中属于既有分组的文件合入 `campaign/`、`easy/`、`edge-cases/`、`journal/`、
`tourney-2019/`、`tourney-2020/`、`tourney-2021/`、`workshop-versions/`，新增分组
`24hour-1-puzzles/`、`24hour-1-sample/`、`24hour-1-test/`、`custom/` 并列放在其旁。
其中 2236 个文件为新增字节，165 个与原库文件逐字节相同，不存在同名不同内容的冲突，
因此只有一个题库目录、所有工具也只接受一个 `--corpus` 路径。

导入时逐文件核对 Git blob 哈希（上游版本 `fdedd3f01a238b941ceba8bace7ea35cbd3b8c41`，
2401 个路径、2378 个字节不同文件、142 个哈希与原库重叠、2236 个新字节）；导入过程
没有执行上游代码（后续配对对标实际执行了上游工具）。各分组数量与逐文件哈希清单保留
在导入工作记录里而不放进代码树：有锁定版本与上述汇总即可重新推导，题目本身现在就是
普通题库文件。

| 口径 | 通过／总数 | 覆盖率 |
| --- | ---: | ---: |
| 全部导入文件 | 2273/2401 | 94.67% |
| 导入题库字节去重 | 2250/2378 | 94.62% |
| 不在原库的字节哈希 | 2108/2236 | 94.28% |
| 与原库重叠的字节哈希 | 142/142 | 100% |

[完整报告](result/opussolver-fdedd3f-full-20261028/coverage.json)包含 12005 次尝试：每个文件五种
后端、每次 20 秒、四个独立 CLI worker。扫描中二进制冻结，不调参、不复用历史通过结果。
外部测量发生在 worker 改造前，保留为冻结基线，不代表每个后续二进制都已重跑此库。
这是本项目在上游题库上的覆盖测量，不是与 OpusSolver 同机对比；字节新增也不代表语义去重
或真正未见的 holdout。导入的题目来自 `gtw123/OpusSolver`（MIT 许可，版权归其作者），
它们是数据而非代码，因此不在旁边额外保留许可副本。

## 构建

可信发布路径使用 Linux/POSIX 进程隔离以及上游 [native verifier FFI](https://github.com/ianh/omsim)，
它由锁定版本的 `third_party/omsim` 子模块**从源码构建**，不再随仓库携带预编译 `.so`。
依赖：C/C++20 编译器、CMake 3.20+、Ninja 或其他生成器、`third_party/` 下的子模块；
题库/基准脚本需要 Node.js 20+，游戏证据测试需要 Python 3。

```sh
git submodule update --init --recursive   # 取回锁定版本的 omsim
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j 2
ctest --test-dir build --output-on-failure -j 1
```

CMake 用上游 `Makefile` 中 `libverify.so` 目标的同一组参数
（`-O2 -std=c11 -shared -fpic` 并链接 `-lm`）编译六个 omsim C 源文件，然后链接进
`OpusOptimaReferenceWorker`。可复现锚点是子模块提交：

- 锁定版本 `a88d12d8d100784aa525ee90259255b869bcba38`（`third_party/omsim`）；
- 导出符号集与已退役的预编译库完全一致；
- 行为由测试套件与题库基线锁定：此前被判定为 `collision during motion phase`、周期 1798、
  格点 `[12,1]` 的机器现在仍在同一周期同一格点被拒，通过的机器指标逐项相同。

子模块缺失时配置阶段直接报错并给出 `git submodule update --init third_party/omsim`
指令，而不是静默关闭验解；`-DOPUSOPTIMA_ENABLE_LIBVERIFY=OFF` 仍可用于只编译的场景——
此时默认 `legacy-gate` 发布路径走失败即拒绝，独立 profile 只能在其支持的有限子集内发布。

## 运行

在仓库根目录执行：

```sh
./build/OpusOptima --list-solvers
./build/OpusOptima --help
./build/OpusOptima -p asset/puzzle/easy/just-salt.puzzle --authority independent
# 只验解刚生成的文件，不重新求解或改写：
./build/OpusOptima verify --puzzle asset/puzzle/easy/just-salt.puzzle \
  --solution result/just-salt-solution.solution --authority independent --json
./build/OpusOptima -p asset/puzzle/campaign/ch1-and-prologue/P011.puzzle -s assembly
./build/OpusOptima -p asset/puzzle/workshop-versions/w2591419339.puzzle \
  -s assembly -o result/artificial-ore.solution
```

省略 `-s` 默认使用推荐的 `assembly` 有界组合策略，与显式 `-s assembly` 及 C++ API
`BaseSolver::Create()` 一致。`single-atom` 保留不改变化学的单原子运输；`diatomic` 保留
普通键双原子运输；`greedy` 和 `milp`（`mip`）仍是实验性候选生成器，不保证完整机器。
显式后端及类名别名始终选择其自身，未知名称失败，不静默替换。
兼容例外：历史 **`-s default` / `Create("default")` 仍是 greedy 别名**，不同于省略参数；
推荐路径请用 `assembly`。当前源码没有应用 GUI 求解选择入口需要同步。
OpusSolver 仅作对标工具，不是自动 fallback，产品运行不要求 .NET。

推荐仅指有用的有界覆盖，不代表解质量领先：[主线3](result/solver-mainline3-20261105/report.md)
固定 external16 从0到4通过，仍有 **12/16 失败**（对手16/16），新增四解均被对手支配；
原共同成功130对中，对手支配104、本方支配0、26互有取舍。本地146成功与四指标逐题不变。
见[入口验收报告](result/solver-mainline4-20261105/report.md)。
默认输出为 `result/<puzzle 文件名去掉后缀>-solution.solution`，日志写入
`log/opusoptima.log`。

## 能力与验收边界

- 版本 3 puzzle 解析、solution 解析／写入、六角坐标、普通键／triplex 掩码，以及
  ASCII 预览和 SVG 渲染工具。
- 完整原料批次、保留片段、化学共产品、催化剂借还、多输出与有限库存的有界物料规划。
- 显式设置 `OPUSOPTIMA_FINITE_RETENTION=1` 后，assembly 可尝试有界的六产品行装配，
  组合四元素／Quicksilver 单原子双源、Animismus 与一种静态四元素轮身份
  （Air、Earth、Fire 或 Water）。目标决定轮姿态，每产品多余的一颗 Vitae 必须进入声明的
  有限库存；沿用共享候选／deadline 限额，必须取得真实 native 报告才能接受。
  此有限共产品路径尚不支持多种轮元素混用或任意 mixed graph 装配。
- 普通单原子 wheel 装配还可组合直接供料、钙化得到的 Salt 与多种衍生四元素。
  此混合 Salt 路径保留原始输入索引，必须取得真实 native 接受，解档案也执行同一准入约束；
  不要求启用有限保留选项。供料轨道只为实际使用的轮站预留长度，保持原有站间距与交接余量。
- Static embedding combines genuine singleton feeds, calcification, static wheel
  duplication and exact colored bonds between final Fire endpoints. Every induced
  Fire–Fire edge must be requested, and prisms cannot touch wheel footprints.
  Placement shares the existing deadline and 576 checks. Each output retains the
  sixteen-atom, thirty-six-edge and radius-three bounds, with at most two outputs.
  The family runs once after a first solution or before material search as a fallback;
  actual native acceptance and first-solution retention remain mandatory, and exact
  `OPUSOPTIMA_STATIC_EMBEDDING=off` disables both routes. Direct ordinary products
  and singleton identity outputs need no chemical permissions. Unused molecular
  inputs remain allowed; supplies reference genuine singleton input indices only.
  Ordinary products can also use bounded purification with permission 15 and a
  genuine lower-metal singleton feed. Direct feeds, adjacent purification and
  adjacent projection keep their priority. Otherwise the nearest supplied lower
  metal supports a triangular cascade of depth two to five, using the first original
  input index on ties. Depth `n` uses `n+1` real dispensers and `n(n+1)/2` glyphs
  (at most 15), emitted downstream/rightmost first. Six orientations about the actual
  target product cell share the existing 576 checks and caller deadline. The entire
  source/intermediate triangle reserves private cells outside every other output
  cell and tile. Purification also accepts stable Salt calcified from genuine
  cardinal singleton feeds with permission 12, alongside direct ordinary neighbors.
  Calcifiers occupy only Salt targets, outside private source/intermediate cells;
  Salt waits stably for the cascade root. One-step projection also accepts these
  calcified neighbors, with isolated Mercury outside every output and private tile.
  Native inputs regenerate before glyphs each half-cycle; emitted calcifiers and
  metal glyphs precede all ordinary bonders, and outputs consume after glyphs.
  Projection products therefore finish before another Mercury regeneration.
  Wheels, colored bonds and purification/projection mixtures remain unsupported:
  projection can upgrade even bonded metal waiting for delayed purification.
  Private triangle placement still rejects enclosed purification targets. Tiles
  cannot share intermediate stock. Whole-puzzle native admission, portfolio slots,
  shared placement checks, caps and deadlines remain unchanged.
- 刚性分子覆盖的每个源／目标最多十六原子，最多八输入、两输出，原有半径八边界不变。
  搜索中检查真实继承键、剩余成键权限和至少使用一个完整分子的条件；
  首个不可行覆盖不会阻断后续覆盖，所有输出仍共用 20,000 节点上限和调用者 deadline。
  覆盖块按每原子节省的继承键成本减去所需钙化／断键成本稳定排序；这是有界搜索启发式，
  不保证最低成本。有断键权限时，可在保留全部源原子的刚性覆盖中切除多余源键并补充目标键；
  切除边不得属于目标边，跨输出原子占地重叠会在构造前拒绝。
- 普通行装配复用原有第一个构造槽位执行有依赖约束的空臂复位排程；无可并行复位时
  保留原串行构造，直接传递调用者 deadline，不分配新时长。
  `OPUSOPTIMA_ROW_RETIMING=off` 保留串行模式，其他变体仍使用剩余构造槽位。
- 新增仅限 native 的闭合宽金属路线：单个 2–12 原子金属输入、旋转后最小高度四行、
  单金属输出且禁止丢弃。由质量守恒推导完整批次周期，要求严格 IR 闭合，
  使用稀疏源接力布局；旧 serial API 的默认布局不变。
  搜索前计入共享 construction 限额，保留节点、提交及外层 deadline；
  规划和构造共享三秒子截止时间，验解仍使用调用方既有验证预算。
- 严格闭合库存搜索对全金属、禁止丢弃的题目使用铅等价质量上界，
  排除累计引入质量超过目标总量的新原料批次；含非金属输入/输出或允许丢弃时不启用，不增加搜索预算。
- 分子供料组合使用显式有限批次切片和容量一的出口预留。末片包含清理尾段，
  直到接收臂实际搬离原子才释放出口；未完成批次和重叠预留均拒绝。
  此接口保持原布局和 tape，不证明运动扫掠安全，也不据此直接共享实体工厂。
- `OPUSOPTIMA_SHARED_FEED_RESOURCE=on` enables a bounded post-first trial sharing
  one finite extraction factory between the two salt inlets of an Animismus site.
  It requires a native-accepted LifeDeath parent, one or two ordinary or existing
  colored outputs, and
  genuine multiatom Animismus feed; singleton inputs remain independent. Sharing stays
  within each output, with joint demand compiled once and checked exactly. The
  existing transport layout and native gate remain. It replaces the static embedding
  slot only when an output requires Vitae/Mors without a same-type genuine singleton
  input, which the static emitter cannot derive. Bounded derived Gold-through-Tin
  suppliers and requested synthetic Quintessence use original puzzle inputs and the
  same deadline. Cardinal suppliers preserve whole molecules and original input
  indices; a registered usable real Quintessence source suppresses the shared synthetic
  probe. Salt unification retains singleton-salt validation and molecular restrictions.
  Colored outputs retain Fire endpoints, permission 11, and nonempty color masks
  contained in 14. The existing row compiler uses ordinary proxy bonds and original
  labels with feed spacing three and the unchanged deadline. Single-product and
  compact sharing remain unsupported. The trial
  adds no construction slot and carries the enclosing deadline through bounded
  lowering. It is off by default and may consume verification time needed by later
  candidates.
- 原有局部空闲臂优化在空 tape 删除未改变机器时，可移除完全静态机器中的 no-op 占位臂；
  对无 reset/repeat 隐式展开的活动程序，也可移除全部指令时间不超过保留程序末端的 no-op 臂。
  决定周期长度的占位臂、轮子和未知指令程序仍保留，仍须通过解档案的复验才能更新目标槽位。
  八个局部试探全部保留，独立 60 度旋转放到调度和结构试探之后；显式 shared-bonder 模式保留原顺序。
- 固定臂、piston、track、交接与生产室／导管物理编译；已覆盖紧凑反应站、环形处理、
  完整供料覆盖、分布式净化等受支持结构。多流路由在独占布局所需的最少 chamber／conduit
  数量超过容量时直接剪枝，沿用既有共享搜索预算。
- 不消费求解器 IR、调度结果或碰撞证书的独立有界生产执行器。

默认旧 gate 仍写临时候选、隔离进程验解、回填实测指标、复验并原子替换目标。
显式独立模式使用同一发布事务，但指标完全自算；失败保留已有输出，内部 `HexSimulator` 不是裁判。

independent／reference 两个 exec 型 worker 保持依赖隔离，主 CLI、父库和独立 worker 不链接
`libverify.so`。首个 `finite-ordinary-v1` 子域现已接通普通有限输出、四项独立指标及区间认证的
名义运动，可在关闭参考裁判的配置中发布。生产墙体／导管等未认证组合仍不可用，**全局默认
仍为 legacy-gate**；见下文验证器章节及
[本轮验收](result/independent-finite-certified-shadow-20261029/provenance.json)。

带 vial 或非默认显示边界字段的生产候选还必须通过独立生产执行，且两引擎完成周期一致。
锁定原游戏版本的 IL 证据将 vial 和本地命名的两个“shrink”字段确定为显示元数据，
并没有证实房间随周期缩小的规则；未知执行子域仍返回 `Unavailable` 或被拒绝。

普通有限完成要求每个输出达到 `6 × outputMultiplier` 份。验解只认证完成前的运行，
不保证完成后无限期无碰撞；旧生产 gate 仍要求 native 连续碰撞判定，其采样重放不等于新独立
区间证明。后者认证的是明确声明的名义规则模型，不是完整原游戏或所有平台浮点实现等价性证明。

冻结机器差分覆盖 **2254 个 puzzle＋solution 组合**：**1019 独立通过、801 超时、434 不可用**，
状态／四项指标差异为 **0**。未知和超时不计作通过；这是验证器子域支持率，不是导入题库的新求解覆盖率。

## 复现验收

```sh
ctest --test-dir build --output-on-failure -j 1
python3 -I tests/functional_test/GameProductionEvidence.test.py
node --test tests/functional_test/FairBenchmark.test.cjs
node --test tests/functional_test/LocalCoverage.test.cjs
node tests/functional_test/LocalCoverage.cjs result/<新的空目录>
# 单独测量导入题库：
node tests/functional_test/LocalCoverage.cjs result/<另一个新的空目录> \
  --corpus asset/puzzle
```

每次完整扫描使用全新目录，测试或扫描期间不要重建共享库。合成 fixture 不进入原题覆盖率
分母；`--failed-from` 只用于失败子集诊断，不能当作完整覆盖报告。

与其他求解器比较时，使用[公平比较规程](tests/functional_test/FairBenchmark.md)，记录
字节去重的开发／保留集及对称验解口径。现有题库已经影响开发，其 holdout 不是真正未见过
的外部测试集。

## 代码与文档导航

下表即代码导航。求解数据流为 `main` → puzzle parser → assembly/portfolio → candidate archive → native gate；`src/Solvers/` 保存各能力家族及其共享的 planning/lowering 代码。

| 位置 | 职责 |
| --- | --- |
| [src/Core/](src/Core/) | 坐标、分子／部件数据、共享 frame 变换 |
| [src/IO/](src/IO/) | 文件编解码、验解子进程、原子发布 |
| [src/Solvers/](src/Solvers/) | 物料计划、物理 lowering、有界候选编译 |
| [src/Verification/](src/Verification/) | 独立生产执行 |
| [src/Engine/](src/Engine/)、[src/Render/](src/Render/) | 内部模拟与渲染工具 |
| [tests/unit_test/](tests/unit_test/) | 契约、变换、负例和真实机器 fixture |
| [tests/functional_test/](tests/functional_test/) | 原题 CLI 回归、题库脚本与规则证据 |
| [result/](result/) | 生成解、不可变实验报告和日志 |
| [third_party/](third_party/) | 依赖与上游许可证，不纳入本项目格式化 |

以下为历史验收记录，不是当前能力规格：

- [B1/B2 规则证据与初始独立执行](tests/functional_test/ProductionB1B2.md)
- [三个非生产目标](tests/functional_test/ThreeTargetAcceptance.md)
- [保留片段阶段](tests/functional_test/RetainedFragmentProgress.md)
- [首次真实目标突破](tests/functional_test/RealTargetProgress.md)
- [阶段 A 故障矩阵](tests/functional_test/RemainingCompositionA1.md)
- [阶段 A 验收](tests/functional_test/RemainingCompositionAcceptance.md)

## 求解器结构与不变量

数据流为 `io::PuzzleData`（二进制解析）→ `AssemblySolver` 规划与有界候选编译器 →
`CandidateArchive` 验证/发布 → native `legacy-gate`。上游 Standard 移植位于
`src/Solvers/Standard/`，由 CLI extra-portfolio 首先尝试，并保持库 API 的首解与配额契约。
重构必须保持：puzzle 字节是只读输入；规划证书与独立验证分离；只发布已验字节；保留首解；
归档每个目标槽只保留最小值；不自动回退到 OpusSolver；默认权限仍是 `legacy-gate`。
四项指标由锁定 omsim gate 测量，`HexSimulator` 不是裁判。

## WebAssembly 与 GitHub Pages

网页构建放在独立的 `wasm/` 或 `web/` target，只复用可移植的 solver/IO/render 代码，
不修改 CLI 默认、文件归档或 native worker 路径。纯 WASM 可以只用 GitHub Pages 托管
（静态文件、单线程 Web Worker、无自定义 COOP/COEP 头）；但 Pages 不能运行 native CLI、
`posix_spawn` 验证器或 `libverify.so`，需要这些时让 Pages 只做前端，求解/验证放到
自建容器或服务器后端。首个目标已经放在 `wasm/`，双语静态页面在 `web/`：单线程模块包含上游 Standard、
二进制 codec 和 omsim，通过字节 C ABI 暴露 `opusoptima_solve` / `opusoptima_verify` / `opusoptima_describe` / `opusoptima_trace` / `opusoptima_make_puzzle` / `opusoptima_make_puzzle_json`，
由 `.github/workflows/pages.yml` 发布 `web/dist`。本地构建：

```sh
emcmake cmake -S wasm -B build-wasm -DCMAKE_BUILD_TYPE=Release
emmake cmake --build build-wasm --parallel 2
mkdir -p web/dist
cp web/index.html web/app.js web/i18n.js web/layout.js web/puzzle-builder.js web/style.css web/dist/
cp build-wasm/opusoptima.js build-wasm/opusoptima.wasm web/dist/
```

首次使用请在 **Settings → Pages → Build and deployment → Source** 选择
**GitHub Actions**，然后推送到 `main` 或手动运行
`Deploy browser solver to GitHub Pages` workflow。页面使用进程内 omsim 检查，
而不是 native 隔离的 `legacy-gate` 发布事务，并显示机器布局，回放 omsim 的
逐周期物理轨迹（原子、键、机械臂和碰撞驱动的运动）及逐臂 tape 时间线。
基础编辑器通过 `opusoptima_make_puzzle` 支持原子和键；高级 JSON 编辑器通过
`opusoptima_make_puzzle_json` 支持多个输入/输出端口和 production metadata
（chambers、conduits、vials、shrink/isolation）。可视化构建器可在六边形网格上
编辑任意原子、键和端口，并用表单编辑 production；页面支持 EN/中文切换。
另有独立的机器布局编辑器，
可在六边形网格上摆放 arm1/piston/baron/track 和 glyph，并保存为 `.layout.json`；
它是规划/预览格式，不是 `.puzzle` 字段，因为游戏 puzzle 格式本身不存储机械布局。
带 native 权威的后端仍属后续工作。

## 开发约定

自有 C++ 使用 `clang-format-19` 和仓库的 [.clang-format](.clang-format)。不格式化
第三方代码或改写历史生成报告。规划、执行或验解的变更需要对应正反例；验收路径的变更还需
原题 CLI 和全量 fresh 检查。求解器计划证书与独立验解必须保持分离。

首个独立发布子域已可用；下一步在原预算内降低执行超时、补强原游戏／数值模型证据，并逐项
认证生产空间、重复输出和其他排除规则，再讨论默认切换。本地 100% 求解覆盖不是通用验证器
或通用求解器的完备性证明。

## 四主线当前集成验收（2026-11-05）

第三方锁、公平基线、cardinal→Salt→wheel补给及assembly CLI/API默认的有界交付
已通过[当前累积验收](result/solver-mainlines-final-20261105/report.md)：enabled 782项、
disabled 503项CTest及46项Node测试。固定external16从0到4，仍12失败；当前省略/显式
assembly结果等价。本地146不变是历史逐题事实，不是本轮新扫描；这不是外库全量覆盖、
独立认证完成或质量超越OpusSolver。

## 许可证

项目代码采用 [MIT 许可证](LICENSE)。依赖和 [OpusSolver 来源说明](third_party/OpusSolver-LICENSE.txt)
分别遵守各自条款；外部验解器即上游 [ianh/omsim](https://github.com/ianh/omsim)，以
`third_party/omsim` 子模块锁定并由其自身源码构建，其许可原文见
[third_party/omsim/COPYING](third_party/omsim/COPYING)。

## 已验三目标档案（第2步）

`-o` 固定发布**首个验证通过候选**，不随目标 winner 波动。成功时
`<output>.objectives.json` 指向独占同级 bundle 内不可变代次命名的三个完整 solution 文件。每槽仅最小化本指标，主目标相等稳定先到，
没有隐藏次目标权重；允许重复 hash。无可行候选时 API 三槽为空，不生成新公开输出。
每条记录保存完整字节/SHA-256、输入 SHA-256、实际全部指标、authority/rules/profile/engine
及策略来源，不能拼接不同解指标。

portfolio 调用 `io::CandidateArchive::Submit(solution, strategy)` 作为真实 gate，
通过 `Slots()`/`First()` 取得不可变已验记录。接纳时私有验解归档，`Publish(options)`
再验证去重三代表及首解后最终发布。CLI 使用 `Snapshot(options)`：接纳首解立即发布三槽（允许重复），
后续已验改进发布新快照；核对原输入、authority、精确已验字节和截止，不重复验解。
快照引用不可变代次文件，后续中断不破坏旧 manifest。reject/timeout/unavailable 不清空旧槽；仅最终 Publish 封存档案。
`AssemblySolver::CollectVerified` 保留原首可行路径，然后对普通有限单输出最多尝试三次：
紧凑串行 piston/track、适配单原子供料的共享工作站、增加供料间距的布局。
按能力过滤；忽略名字和预测指标后按完整序列化结构去重，再送真实 gate。
最多12次提交、默认3次接纳，因此不保证执行最后变体；这是有界构造而非局部优化。
round4–6的retiming、structure-neighborhood与critical-path家族共用上述三次构造尝试（含构造失败），不追加接纳配额；早期baseline变体优先。拒绝消耗提交配额，结构重复不消耗。非普通输出首解通过后停止fallback链，不继续昂贵构造。
耗尽后可能抛异常，调用方保留 archive，不把未验返回值当成功。显式后端不变，禁用 OpusSolver fallback。

CLI 仍共享原20秒：前15秒构造/接纳，保留原截止前5秒用于发布；后续超时保留已发布快照，不将未验候选当成功。
单写者下 manifest rename 是三槽可见性提交点，`-o` 另一次 rename，**不是多文件事务**。
同步 manifest rename 失败尝试回滚旧主文件；不保证崩溃恢复、并发写、回滚自身 I/O 失败或 fsync 持久性。
消费者一次读取 manifest 后按引用读 bundle。保留 bundle（含失败私有档案），不引入清理框架。
见[第2步证据与限制](result/solver-objectives-step2/report.md)与
[第3步有限 portfolio 证据](result/solver-portfolio-step3/report.md)。

Assembly 随后快照三目标槽（最多3个不同已验父解），各做8个确定性局部假设：
紧凑运输（把不伸缩的 piston 改为 arm1、删除从不抓取的机械臂、把轨道裁到臂实际
访问的格子）、压缩最大共同空档、删无指令机械臂、整体±60°旋转、折半并删空臂组合、
裁去轨道首端/末端。`OPUSOPTIMA_COMPACT_TRANSPORT=off` 精确恢复旧队列顺序（首个
假设为时间索引折半）；未设置或 `on` 均启用含紧凑运输的默认队列。
local-v4 适用时改为同步末端空手 arm1 回位（保持各臂旋转序列，
共同提前 reset），否则保留旧 rotate-300；`OPUSOPTIMA_SERIAL_RETURN=1` 可作旧 v4 消融。
After both existing return strategies decline, v4 prepositions empty arm1 rotations only on explicit tapes with common O markers. Existing strategy priority and fixed parents are preserved; every candidate requires native admission.
Within the existing v5 slot, useful return compaction followed by empty-rotation prepositioning starts from the original parent. Both transforms must apply and prepositioning must vacate an occupied global column, otherwise the exact legacy timing-division/idle-arm-removal trial remains. The shared v4 return policy respects existing flags; serial-return mode suppresses this composition. Only the final composite reaches native admission. The trace distinguishes `compact-returns-and-preposition-empty-rotations` from legacy `timing-divide-2-and-remove-idle-arms`; variant IDs, priorities, fixed parents and gate limits remain unchanged.
这不是片段保留/共产品构造或通用流水线；见[首轮真实消融与限制](result/solver-general-capabilities-round1/report.md)。
绝不视为等价改写；每个不同候选完整 gate（含权限）通过才接纳。
忽略名字/预测指标的序列化字节去重父解、空变换和候选；子解不回填，最多24次门检。
三指标各自严格改善、tie稳定先到，`-o`仍保首解。trace记录父hash/实测指标、候选
canonical hash、变换编号和真实判定。首可行前仍给15秒；首解后结构截止改为13秒，
局部接纳截止15秒，最终发布预留5秒。进行中的门检共用15秒截止，因此局部2秒是调度
目标而非保证最少时长；显式后端不变。见[第4步实测与限制](result/solver-local-optimization-step4/report.md)。

### 实验性有限批次消费者路由

`--experimental-shared-batch-consumer` 默认 **OFF**，限 assembly solver，需同时
开启 `--experimental-shared-assembly`。仅在封闭双输入普通/Animismus 混合域，
增加一个完整 legacy gate 候选：末尾真实源前体直接送首反应，省去存取绕路。
不改六份输出、身份账、缓冲几何或原首解/三目标父槽；不支持的末尾分配或
验证失败保父，不假设多臂无冲突并行。测量对照两侧均开启
`--experimental-shared-scheduling --experimental-shared-buffer-relay`。
证据和限制见 `result/solver-mixed-batch-consumer/report.md`。

静态反应站候选在 assembly 已验首解后默认有界启用；显式设置
`OPUSOPTIMA_STATIC_REACTION=off` 可关闭消融，其他失败实验保持默认 OFF。
The legacy matcher preserves the serialized layouts for complementary Vitae/Mors
products with opposing Water neighbors and an optional handed Salt branch. If it
rejects, the same static reaction slot can embed two connected ordinary outputs,
each with exactly one complementary life atom and otherwise genuine singleton
direct feeds. A genuine singleton Salt feed supplies Animismus. At most eight
singleton inputs, 16 atoms and 36 ordinary adjacent edges per output, and radius
three from each life atom are supported. The life atoms remain on the real glyph
ports; at most 36 rotation pairs are checked. Both output cell sets must be
disjoint and avoid the reagent cells. Only requested bonds are emitted, without
arms, disposal or additional derived chemistry. Ordinary arm, bonder, Animismus
and output permissions are required. The fallback shares the caller deadline and
existing construction/submission/acceptance caps. Its distinct stage requires the
candidate's own actual LegacyGate authority. Legacy calcified Water support,
first-result retention and the other backends remain unchanged. There is no puzzle
name/hash dispatch or unbounded grid search.
收益、边界及默认启用建议见 `result/solver-static-reaction-integration/report.md`。

The same static reaction slot supports ordinary v3, nonproduction, multiplier-one
puzzles with one singleton metal output, one to eight genuine singleton metal
inputs and purification permission 15. It selects the nearest supplied lower metal
(first input index on ties), supporting one to five purification steps. Depth `n`
uses `n+1` dispensers and `n(n+1)/2` glyphs, at most 15. Adjacent reagent cells
produce the next row at `(q,r+1)`; downstream/rightmost glyphs have priority and
the root output is `(0,n)`. There are no arms or instructions. Depth one preserves
its previous bytes. Molecular and nonmetal feeds remain unsupported. Every
candidate requires original-puzzle native completion and its own LegacyGate
authority; the first solution, shared caps and deadline remain unchanged.

有限静态嵌入候选默认启用，显式 `OPUSOPTIMA_STATIC_EMBEDDING=off` 关闭。
仅普通 v3、倍率 1、无生产信息：至多 8 个 Salt/元素单原子原料，
1–2 个产品，每个至多 8 原子/12 普通邻接边，连通唯一格点，
相对某原子半径不超过 3，且必须实际需要元素→Salt 钙化。
检查 arm/bonder/calcification 权限；有界旋转/局部平移，非任意网格搜索。
仅首个已验解之后尝试，共享构造/提交上限、完整原题 gate 和三槽 Snapshot；
缺少 native 库不能绕过验证。旧精确 P032 默认路径、其他失败实验 OFF、
20 秒预算和 first-verified 均不变。8 个真实匹配不代表整体打平。
本次默认准入与三方测量见 `result/solver-static-embedding-default/report.md`。

刚性载体在 `OPUSOPTIMA_RIGID_CARRIER` 未设置时默认尝试，仅精确 `off` 关闭。与 static embedding 一致，其余字符串（含 `on`、`unknown`、空串及 `OFF`）均允许尝试。仍要求 native worker 以及首个 Passed 父候选具有 LegacyGate 权威；共享构造/唯一提交/接受上限（3/12/3）、deadline、first 和三目标 slot 保护不变。有限 placement preflight 复用 compiler 的 header/graph 守卫，确定不支持（含全 singleton 源）才在构造扣额前跳过；Unknown 仍须原构造额度与 native gate，仍可能耗时或挤掉后继机会。缺少 bonder/calcification 权限本身不足以拒绝已带完整键的分子。

The rigid-carrier preflight shares the existing one/two-port six-rotation/translation matcher (at most 1,152 anchors). A conservative source/target size/type check can also admit a static partition as Unknown without enumerating partition placements or covers. The same charged slot supports two to four original output ports, retaining the v3, nonproduction, multiplier-one, eight-input, sixteen-atom and radius-eight limits. Every selected genuine bonded source is completely partitioned into disjoint rigid outputs with exact atom types and inherited requested edges, with at least one batch supplying multiple ports; permitted real unbonders cut every remaining edge. There is no chemistry, new bonding, retention or disposal. Independent regions regenerate whole source batches only after all assigned pieces clear, preserving the original six-product goal at each port. Existing one/two-port whole-source covers remain preferred whenever every port has a legal tile. Partition recursion and every pose attempt share 20,000 nodes and the caller deadline, with no second cover search after exhaustion. The explicit `portfolio-static-molecule-partition` stage requires actual LegacyGate authority before publication, including legacy-primary Shadow; Independent mode and native-disabled builds cannot admit it. Shared construction/submission/accepted limits, first retention and objective slots remain unchanged.

The bounded radial-wheel candidate is enabled when `OPUSOPTIMA_RADIAL_WHEEL` is unset or exactly `on`. Explicit `off` and every unrecognized value (including an empty string or different case) disable it. Admission remains limited to ordinary v3, nonproduction, multiplier-one puzzles with one three-to-seven-atom Salt hub product, two to six Salt/cardinal radial leaves, one to eight genuine singleton Salt feeds, exact ordinary hub edges, unique positions and all required permissions. The closed one-product period, native original-puzzle gate, first candidate, static archive slots, construction/submission/acceptance caps and deadlines remain unchanged. The measured opportunity concerns this limited domain and does not establish improvement on arbitrary unseen puzzles.

The mutually exclusive metal variant in the same budgeted radial slot has limited default admission. Unset or `OPUSOPTIMA_RADIAL_PROJECTION=on` enables it; `off`, empty and unrecognized values disable it. Admission requires ordinary nonproduction v3, multiplier-one puzzles, genuine singleton inputs, one supplied hub with two to six ordinary radial metal leaves, a common supplied lower-or-equal metal and Mercury, and optionally one singleton metal output. Actual source coordinates and output indices are preserved; unused ordinary inputs are allowed. Life atoms, colored/nonradial bonds and other output forms are refused. Arm, bonder, grab, rotation, pivot, wait (bit 25) and projection permissions are required. One projection glyph receives at most five individually transported Mercury doses per held metal atom. The singleton outlet is separate from projection and bonding. The closed period restores all poses without residual doses or intermediate stock. The candidate requires its own native original-puzzle gate and retains first-result priority, separate metric slots, construction/submission/acceptance caps and deadlines. Hardware savings can increase cycles. This limited rollout uses the validated seen corpus; it does not prove all unseen opportunities are free of regressions.
