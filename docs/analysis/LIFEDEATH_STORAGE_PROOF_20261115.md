# LifeDeath dispersion storage 有限全局路由裁轨

## 实现与边界

- `LifeDeathStorage.hpp` 是独立证明，不复用 parking helper，不放宽旧 envelope overlap 拒绝。
- 仅记录 dispersion spare storage 的轨 ID；所有 output 对象过滤、平移并合并后，映射到最终序列化对象 ID，再尝试事务性裁剪。
- 按 native 的逆对象/逆轨点写入顺序重建全局 `A/a` 图；两个方向分别覆盖，包含端点零移动，轨 rotation 按 native 忽略。
- 优先 proposal：每条 selected storage 收集初始 base 位于该轨的所有 arm 在原全局图中首 `B` 前的实际 source/destination 连续包络；无初始用户则整轨保留。重叠初始 base 可属于多轨，不只看名义 own-arm。
- 保留原保守 proposal 作 fallback：所有包含任意 arm 访问世界点的 selected 轨均扩展包络。own proposal 完整全机 replay 失败才尝试保守 proposal；两者失败机器完全不变。
- 空 tape 的合法机械臂按 native 零长度 tape 语义静止，保守保留其基座；非空 tape 必须有 `B`。halt 不要求回起点。
- `B` 后 suffix 不执行但仍检查动作和严格递增时间戳，且原样保留。不支持 reset/repeat 或未证明循环。
- 对完整机器重新建候选全局图，逐 arm、逐移动要求原/候选 source 和 destination 相同，包含零移动。任何失败不提交 trial。
- 拒绝非法轨 body、重复点、非相邻轨段、loop、坐标或时间溢出、非法 arm body、未知动作、非 arm tape、ravari。没有已证明用户的轨保持原样。
- tapes、非轨对象、对象顺序、轨 origin/header/footer 不变；不增加候选或 verifier 提交，不修改 quota/deadline。native 验证仍是最终权威。

## 真实 default 测量

结果根目录：`result/dispersion-storage-20261115/`（指向 workspace artifacts）。

- `before/`：修改前真实 default；`after/`：初版，无收益；`after-fixed/`：空 tape 静止域修复后的真实 default。
- 每阶段六题串行，命令仅 `-p/-o`；未强制策略或预算。每个报告保存环境和 exe、lib、workers 的路径及 SHA-256，阶段内 before/after hashes 均一致。
- 三阶段各有 15 个 own-slot 独立 native 验证，全部 `passed`；不是用 legacy `.solution` 或 manifest 代替 native。
- `19c7b313` 三阶段均无有效 slot，是 baseline 已有失败，不计新增回归。

| puzzle | own-cost before → fixed | own-cycles before → fixed | own-area before → fixed |
|---|---:|---:|---:|
| 9a6d6d90 | 20860 → 18805 | 39700 → 39700 | 5854 → 5482 |
| 329bfd90 | 1535 → 1535 | 13113 → 13113 | 577 → 577 |
| 6c4bbc37 | 2350 → 2350 | 39533 → 39533 | 989 → 989 |
| 78a08a99 | 230 → 230 | 238 → 238 | 85 → 85 |
| c15dcf8f | 355 → 355 | 3991 → 3991 | 169 → 169 |

9a 三个 own-slot 分别逐对象审计：每个 slot 均只有 11 条轨的 cells 改为原 cells 的连续子片段，共删除 411 格；其他轨字段、所有非轨对象和全部 tapes 完全相同。周期和指令数均不变。

初版无收益的根因是构造机器仍含三个空 tape piston，初版统一拒绝，而最终后处理已移除这些闲置对象。native `decode.c` 对零指令 arm 直接跳过 tape 解码，`sim.c` 按零 tape 长度跳过执行，因此独立加入静止基座证明，而非放宽未证明循环。

## 验证记录

- 完整 `ninja -C build`：`build-fixed.log`。
- focused `LifeDeathTest.*:LocalCompaction.*`：21 项，20 passed、1 optional skipped，`focused-fixed.log`。
- 本轮全测：`full-suite.log`，最终状态以该日志为准，不引用旧 rail-prefix 全测。
- 编辑器检查：新 helper 无诊断；测试文件仍有 magic-number/style 及已有 nodiscard 诊断，实际构建成功。
- 所有既有工作区改动与未知删除保留，未提交。

## 审查修复与最终验证（追加，不替代首次结果）

本次证据目录：`result/dispersion-storage-review-20261115/`。

- 对照 native `sim.c:2188-2205`，逐轨点拒绝世界坐标 `(INT32_MIN, INT32_MIN)`，包括未选轨；不改变用于差值合法性的 `fits`。直接 origin 和非零 local offset 两种命中方式均验证事务拒绝，单轴 `INT32_MIN` 正对照仍可裁轨。
- 新增 partial overlap 的 `A/a` 独立方向覆盖及端点零移动、非零 origin/local offset 裁头、selected ID 越界/重复/非轨拒绝；拒绝路径均比较完整序列化不变。
- 重复 selected ID 原实现即拒绝，本次保留该安全契约，不改成隐式去重。
- 提取生产 `AppendLifeDeathObject` 接入点，在过滤后追加对象时记录最终 ID；窄域测试覆盖多个 output、前置对象和被过滤占位，但不声称直接执行了生产 skip predicate。
- 构建时发现新增测试的 `SCOPED_TRACE(tuple)` 编译错误，已改为显式消息；header 显式包含 `<utility>`。`build-all.log`、`build-required.log` 为失败尝试，不算成功证据。早先 `focused.log` 的 22 项通过来自旧测试二进制，同样不作为新改动证据。
- 修复后的完整 Ninja 构建：`build-final.log`、`build-final.exit`，真实 exit **0**。
- 修复后 focused：`focused-final.log`、`focused-final.xml`、`focused-final.exit`，**27/27 passed，exit 0**，包含新增 5 项。
- 首次全测 PID 2564362 已自然结束，日志显示 1080 passed、5 skipped；未恢复真实退出码，因此不记作 exit 0，也不作为本次最终二进制证据。

### 上一 storage 阶段最终串行证据链（已完成，仅证明旧代码）

- 一次性 wrapper：`final-validation.sh`，以原子目录锁防止重复启动；wrapper PID **2605234**，唯一新最终全测 PID **2605265**。
- 此链已自然结束，`full-suite.exit` 和 `audit.exit` 均为 **0**，三份二进制指纹相同；本轮等待 wrapper 2605234 自然结束后才运行 solver/build/tests。不作为新 own proposal 验收。
- 只有全测实际 exit 0 且指纹一致，才自动运行 `/usr/bin/python3 tools/audit_gap.py --jobs 2`；审计 PID、退出码、起止时间分别保存，结果为 `audit/gap.json`。
- 指纹：`binaries-before.txt`、`binaries-after-tests.txt`、`binaries-after-audit.txt`；包含 exe、共享库、测试和三个 worker 的路径、解析路径及 SHA-256。`inputs.sha256` 固定 audit/opsol、oracle 和 baseline；`environment.txt` 保存运行环境。
- 历史完整审计保留于该阶段 `audit/gap.json`，新阶段另行比较，不覆盖。

#### 上一阶段只读复核成绩（不作为 own-envelope 验收）

- 最终 full：**1090 项，1085 passed、5 skipped，`full-suite.exit=0`**；结束于 `2026-11-15T14:08:20+08:00`。audit：`audit.exit=0`，结束于 `2026-11-15T14:13:38+08:00`。
- `binaries-before.txt`、`binaries-after-tests.txt`、`binaries-after-audit.txt` 逐字节相同，三文件各自 SHA-256 均为 `946c6460ecb99a766d892d788cb2411dfe136edce1fb191e596b2cd5758bed5c`。这是旧阶段独立指纹，不与 own-envelope 新二进制混用。
- 对照 `result/rail-prefix-20261115/audit/gap.json`，两份均为 146 个唯一 SHA，题目集合及每题 oracle `best` 完全相同。覆盖均为 **141/146**，每种 slot 均 141 个、合计 423 个；没有新增或丢失覆盖。共同 missing：`19ff88db`、`19c7b313`、`16b1a5f5`、`07e252a4`、`0409d7fe`。
- 以下均取各自 slot 的自身目标 `slots[key][key]`，倍率为**共同覆盖 141 题的目标总量 / 同题 oracle `best[key][key]` 总量**，不是逐题倍率平均，也不以 legacy solution 代替 slot。

| 自身目标 | 改善 / 持平 / 退化 | rail-prefix 总量 | storage-review 总量 | 差值（后−前） | 同覆盖 oracle 总量 | rail-prefix 倍率 → storage-review 倍率 |
|---|---:|---:|---:|---:|---:|---:|
| cost | 1 / 138 / 2 | 226085 | 226140 | +55 | 100525 | 2.249043× → 2.249590× |
| cycles | 1 / 139 / 1 | 1059836 | 1059513 | −323 | 84506 | 12.541547× → 12.537725× |
| area | 1 / 139 / 1 | 59487 | 59327 | −160 | 23568 | 2.524058× → 2.517269× |

- 423 个 slot 合计：**3 改善、416 持平、4 退化**。按题统计（共同覆盖 141 题）：2 题改善、137 题全持平、2 题退化，没有改善/退化混合题；另 5 题共同 missing 不计持平。
- 全部自身目标变化：`9a6d6d90` cost **20870→18815**、area **5854→5482**；`a94b5296` cycles **14807→14482**；`e42f1f11` cost **20100→22200**、cycles **29503→29505**、area **6322→6534**；`cb6c9528` cost **2465→2475**。其余自身目标均不变。此为实际 default 审计差异，不把所有变化归因于裁轨。
- 审计 JSON SHA-256：rail-prefix `6e2e7676d67ae6affd0a9aea00921846bb5443bae514e840114b5b1c5b241b7c`；storage-review `5612f5b4b8b54c001970f6896dd11b1556940347d6df1b878d52bd006802886d`。前者与旧链 `inputs.sha256` 记录一致。

## Own-initial-user proposal（2026-11-15，独立阶段）

证据：`result/storage-own-envelope-20261115/`。保留源码快照、初始工作树 diff、旧链完成证据，未提交或恢复未知删除。

- 新增三项回归：外来跨轨访问仅在全机 replay 等价时可忽略；own proposal 改变 entrant 的方向/端点零移动时拒绝并保守 fallback；无初始用户 selected 轨整轨保留。完整序列化检查。
- 全量 Ninja `build-all.exit=0`，实际重建共享库和 CLI/tests；focused **30/30 passed，exit 0**。
- 真实 default 9a：本轮 before 自身目标 **18815 / 39700 / 5482**，after **18480 / 39700 / 5425**。cost 减少 **335**、area 减少 **57**，cycles 与 instructions（95276）不变。不能混用历史18805或预测18520。
- before/after 六个 own-slot 均分别 native passed；各测量阶段 exe/lib/workers 指纹不变，详见 measurement.json。
- `compare_structure.py` / `structure.json`：每槽所有 tapes 和非轨对象相同，全部 **91,136** 次实际 A/a source/direction/destination（含零移动）相同；B 后不执行，suffix 保留。
- cost/area 槽对象数均93→93，10条轨各为原连续子段，共少67格，其中storage 88→31少57格；其余10格属于最终默认输出差异，不把全335cost都归为单条storage收益。
- cycles 槽对象94→93：上述连续裁剪另有旧轨对象74（18格）不在新默认输出，共少85格；非轨对象/tapes/执行边仍相同。不声称该槽只有连续裁轨或稳定对象ID。
- 边界：不放宽 sentinel/overflow/unknown/no-halt、B suffix、全机有限 replay 和 trial 事务；不新增候选、提交或 quota。独立最终全测与后续146题jobs2审计由本目录一次性 `final-validation.sh` 执行，完成前不称最终通过。

### own-envelope 完成链复核：验证通过，但成绩验收失败

- 原目录 `result/storage-own-envelope-20261115/` 全部保留。`full-suite.exit`、`audit.exit`、`chain.exit` 均为 0；full 为 1093 项、1088 passed、5 skipped。三份 binary fingerprint 文件内容一致，文件 SHA256 均为 `a42b787ebafe29bee3fcd1e6386517bdc726166919cf283c61a29e5b6653b8aa`。
- 独立读取 own 与 `rail-prefix-20261115/audit/gap.json`：两边均 146 行，逐题 oracle 相同；共同有效 141 题。每项严格使用各自 `slots[key][key]`，不读 legacy、不跨槽取最小：

| 目标 | 改善/持平/退化 | rail 总和 | own 总和 | 差值 | 对应 oracle 总和 |
|---|---:|---:|---:|---:|---:|
| cost | 1/132/8 | 226085 | 227060 | +975 | 100525 |
| cycles | 1/137/3 | 1059836 | 1062840 | +3004 | 84506 |
| area | 1/137/3 | 59487 | 59495 | +8 | 23568 |

- 同样缺失的五题为 `19ff88db/19c7b313/16b1a5f5/07e252a4/0409d7fe`。423 个有效目标槽合计 3 改善、406 持平、14 退化，不能以测试通过称成绩无回归。
- `e42f1f11` 的 rail 三槽来自 `local-v8:parent=9368ec58f120b5f3791e377f79cea9ab472c10c385e1d8234fcdc34a8956baa4`，子候选 SHA256 为 `da2317a446300d5c63bda8b68d2a89b939465b624f420da641aeab6ae8c52f24`，cost/cycles/area = 20100/29503/6322。own 三槽却停留在同一个 parent（`independent-outputs`，22200/29505/6534）。继续核对 local-trials 后确认：子候选已经生成，两边 canonical SHA256 同为 `ff93e361337f9b1fc1946c8c6bedba8e0a4e1e83e97c5c8f62ef3878a61f9e71`；own 验证为 timeout / authority none / reasonCode worker-timeout / metrics null。不是候选未生成，也不是已证实裁轨使该子候选成绩变差。
- `cb6c9528` 的 cycles/area 槽仍为同一 closer-sibling hash `4f982bb667f9a5b3207adf8c4384315b52f83cb773013e090f4431823530d461`；cost-drop-glyph 从 `46d624d6133dcf136b6a382d252c781a70a978a0a30f3c892822a6c9c0e243cb`（2465）变为 `0db97f3fac19b9b1329ec20064258113fbb40664e9cf45fb31e0d85082195603`（2475）。需继续检查 local trace 与 deadline，不将这些证据直接归因为计时波动。
- 源码核对：storage ID 为空已有立即返回，不能声称无 storage 也执行全图 replay；非空时先全图 build/replay，随后即使 proposal 不缩短也先复制整机，两个 proposal 亦可能重复。后续修复只允许减少无效 proof 工作，保持 First、family、quota、预算和 native authority 不变。当前尚未完成修复后的验收链。

### storage replay 去重修复与三轮串行复核

- 全部原测量保留；新记录在 `result/storage-replay-fast-20261115/`，`before-1..3` / `after-1..3` 各按 e42/cb6/ac8/1dc 顺序串行 default CLI，仅增加 `OPUSOPTIMA_STAGE_DIAG=1 OPUSOPTIMA_DEFER_DIAG=1` 诊断环境。每个目标仍取其自身槽，不跨槽取最小。
- 仅修改 storage helper 的重复 proof 工作：一次合法原机 replay 收集实际执行的 `(source,direction)`，trial 重建全图后逐方向核对 destination；无可缩短 proposal 在复制前返回，own/fallback proposal 相同时不重复执行。空 storage 原有早退不变，无可能缩短的非空机仍需第一次扫描才能确定包络，不能声称已完全跳过原图构建。
- 等价性：合法有限且显式 B 停止的 tape 下，逐次 A/a 归纳；每个执行 source/direction 的 destination 与原图相同，则下一 base 相同，其余允许动作不移动 base。重复观察同一方向不增加约束；端点零移动仍核对，两个方向独立核对。保留原图/trial 的 overlap 写入顺序、sentinel/overflow/合法 suffix 检查与事务提交，native gate 不变。
- 未修改 First、family、quotas、MaxCandidateSubmissions、候选顺序、槽选择或任何 deadline。CLI 20s、search/archive 15s、已有 First 后结构 13s、首 pass 后 solver 13s、OptimizeVerified 15s 均保留。worker deadline 仍与 outerDeadline 取最小；timeout 不能据此改称未生成。
- build 后 focused 为 47 项、44 passed、3 skipped、exit 0（`focused.log`）。新增 8000 次重复双向边、第二次 no-op 字节不变、B 后非法 suffix 拒绝且字节不变测试；既有方向端点/重叠/保守 fallback 等测试保持。

下表为各自 cost/cycles/area 目标值（非同一文件跨槽最优）；cb6 的 cycles/area 槽保持 closer-sibling，cost 槽为 cost-drop-glyph。

| 题 | before 三轮（均相同） | after-1 | after-2 | after-3 |
|---|---|---|---|---|
| e42f1f11 | 22200 / 29505 / 6534 | 20100 / 29503 / 6322 | 同左 | 同左 |
| cb6c9528 | 2475 / 7994 / 604 | 2465 / 7994 / 604 | 2475 / 7994 / 604 | 2475 / 7994 / 604 |
| ac8d3e1b | 9580 / 17649 / 2529 | 9230 / 14324 / 2389 | 同左 | 同左 |
| 1dc60983 | 4530 / 23306 / 1143 | 4355 / 18301 / 1074 | 同左 | 4530 / 23306 / 1143 |

- 六份 measurement 的 `unchanged` 均 true；24 次 CLI exit 均 0，72 个目标槽独立 native JSON 均 status passed / legacy-gate。CLI SHA256 前后均为 `7aff3c9d875c88610a0d902fbe72d0cb6ea5f0bb1b9d1b4f33690eb9025856db`；实际修改位于动态库，lib SHA256 从 `ee6b16b2bff262b74c87eab756d5ffed9e045f466fe9d01b9d6179ca3554f5ad` 变为 `7867d117f86be512a4de68c1dca163f9f410c40a5ba28cf7a159406227b762ec`。各 worker 完整指纹保留在每份 measurement，不能只用 CLI hash 宣称实现没变。
- after 三轮 e42 三槽文件 hash 均恢复 rail 的 `da2317a4…`，同一 parent 的 local-v8；ac8 三槽均恢复 rail closer-sibling `d38d059b593688596269293a34ca9ec1d798ae1528fa247af60404c3dcf94236`。cb6 after-1 cost hash 恢复 `46d624d6…`，after-2/3 仍 `0db97f3f…`，cycles/area 始终 `4f982bb6…`。逐对象解析 after-1/2 的 cost 文件：分别 71/72 objects，唯一额外对象是 `bonder`，position=(102,0)，header=[1,5,0,0]，无 tape；其余对象（含 tape/track cells）相同。两个 local-trials 均执行完 rotate-60/variant3，末项 passed；现有 cb6 日志没有明确 timeout/deadline 记录，不能将多留此 glyph 的原因无证据断言为超时，也不能声称已稳定恢复。
- 阶段诊断只测整个 compile，不是 helper 独立计时：before-1 e42 First=14.199368s，life-death=4782ms，independent-outputs=5840ms；after-1 为 11.619291s、3812ms、4856ms。ac8 before-1 First=7.032737s / life-death=5685ms，after-1 为 6.338855s / 5121ms。不把这些差直接标成 helper 精确收益，也不据此证明所有退化仅由计时造成。
- 当时启动的最终链 `final-validation.sh`，wrapper PID=2766228，full PID=2766252；其最终结果已取得，见下节。未修改未知 puzzle 删除，未 commit。

## 后续验收：失败，不得宣称 storage 修复成功

- `result/storage-replay-fast-20261115` 的 full/audit/chain 虽均 exit0，coverage 实际由 141 降到 140，新增 missing e42；共同覆盖 own-slot 合计 cost +900、cycles +6164、area +49，11 项 cost 回退。因此该轮验收失败。
- 两套 jobs2 复现 `storage-concurrency-baseline-20261115`、`storage-concurrency-diagnostics-20261115` 各两轮，e42 均缺解；其余每套 18 个 own-slot 均独立 native passed，六项二进制指纹各自前后不变。串行结果不能替代 jobs2 验收。
- helper 独立诊断在这四题的 18 次调用全部 storage.empty 早退，1–7µs，graph/cell/instruction/move 计数为零。没有证据将这四题耗时归因于 storage graph replay。
- 当前构建为 Debug，未切换构建模式。进程 affinity 为 CPU 0–11，在线 CPU 数为 12；不能仅凭 load 5–6 宣称过载。cgroup 各层限制尚未全部核实。perf 因 paranoid=3 拒绝采样，未修改系统权限。

### worker 显式计时诊断（不是性能修复）

- `OPUSOPTIMA_CANDIDATE_DIAG=1` 时记录 worker 开始剩余 deadline、配置 timeout、结束耗时及结果；不改变预算、First、family、quota、slot 和 deadline。
- 完整裸 Ninja/all targets 已通过；focused `LifeDeathTest.*:CandidateArchive.*:LocalCompaction.*` 为 50 项、47 passed、3 skipped，1833ms。日志：`result/storage-worker-diagnostics-focused.log`。
- 一轮真实 default/jobs2：`result/storage-worker-timing-20261115`。e42 life-death compile 5772ms，worker 62901µs 返回 reference-rejected（motion collision cycle739 at83,26）；independent-outputs compile 7127ms，worker 开始仅余1790784µs，1792717µs后 worker-timeout。没有提高默认10000ms timeout，也没有延长 outer deadline。
- 此轮 e42 CLI exit1、三槽 missing；ac8=11390/17651/2711，cb6=2485/7994/604，1dc=4530/23306/1143。后三题共9个 own-slot 均独立 native passed，六项指纹前后不变。`acceptance.json` 明确 passed=false，测量脚本 exit1；不能将1dc单题波动当作整体改善。
- 测量脚本已改为仅记录明确 allowlist 的非敏感环境值；CLI失败、缺槽、native失败、指纹变化均产生非零验收结果。
- 尚无实际性能/coverage 修复；不启动新的 full/audit。下一步需要定位主体 compile 内部开销并实施语义保持优化，再完成同条件前后验证。