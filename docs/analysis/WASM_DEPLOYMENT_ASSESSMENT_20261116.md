# WebAssembly 部署评估：求解器上网页还差什么（2026-11-16）

## 结论

**可行**。求解器核心是可移植的 C++20，没有线程依赖；HiGHS 自身支持 Emscripten
（`EMSCRIPTEN_HTML` 选项），spdlog 也能编译到 WASM。真正的缺口不在算法，而在
**入口层、验证层和展示层**：

- 当前 CLI 的求解链路依赖 Linux 文件系统、`posix_spawn` 验证 worker 和
  `libverify.so`；这些不能原样跑在浏览器里；
- `omsim` 已经提供 `verifier_create_from_bytes()`（见
  `third_party/omsim/verifier.h`），所以验证可以改成**进程内字节验证**，不需要
  子进程；
- 解析/写回已经具备字节 API：`BinaryParser::ParsePuzzle(span)` 和
  `BinaryWriter::WriteSolution()`；缺的是求解、验证、渲染的稳定 C ABI / embind；
- 前端展示缺 **solution renderer / 逐周期播放**；现有 `SvgExporter` 只画 puzzle，
  `HexSimulator` 能跑完整解但没有逐帧快照 API。

## 现有可复用资产

| 能力 | 现状 |
|---|---|
| puzzle 字节解析 | `src/IO/BinaryParser.cpp`，`ParsePuzzle(std::span<const std::byte>)` |
| solution 字节写出 | `src/IO/BinaryWriter.cpp`，`WriteSolution()` |
| Standard 快速求解 | `standard::CompileUpstreamStandard(puzzle)`；实测 `1dc60983` 单题约 **46 ms** |
| 完整 portfolio | `AssemblySolver::CollectVerified(puzzle, sink, 3)`；CLI 默认质量来自它 + 本地优化 |
| 验证 | `libverify`（omsim）+ worker 进程；omsim 有 `verifier_create_from_bytes` |
| puzzle 预览 | `SvgExporter::BuildPuzzleSvg()`（纯字符串，无文件依赖） |
| 求解模拟 | `engine::HexSimulator::Run(puzzle, solution, state)`（完整跑，无逐帧 API） |
| 日志 | `spdlog`；`LogManager::Init()` 才建文件 sink，库路径可跳过 |

Standalone Standard 的质量是 1.089× / 1.063× / 1.058×；完整 portfolio + 本地搜索
是 0.857× / 0.742× / 0.821×。网页可以先给“快速模式”（几十毫秒），再给“完整模式”
（数秒，放 Web Worker）。

## 还差的链路

1. **WASM 构建目标**：新增 Emscripten target，只链接求解器/IO/render/omsim 的
   可移植部分；排除 `main.cpp`、`SolutionVerifier.cpp`（`posix_spawn`）、
   `CandidateArchive.cpp`（文件归档）、worker 主程序、tests 和 Linux-only
   `libverify.so` 目标。`HiGHS` 用 `HIGHS_NO_DEFAULT_THREADS=ON` 等选项裁掉
   不需要的模块。
2. **C ABI / embind**：至少四个函数：
   - `solve(puzzleBytes, mode) -> solutionBytes + metrics`
   - `verify(puzzleBytes, solutionBytes) -> report`
   - `parsePuzzle/puzzleSvg(puzzleBytes) -> JSON/SVG`
   - `parseSolution(solutionBytes) -> JSON`（对象、位置、磁带、指标）
   目前 `CompileUpstreamStandard` 是 C++ API，`AssemblySolver` 需要 sink；
   没有面向 JS 的稳定入口。
3. **进程内验证**：把 omsim 的 C 源文件直接链进 WASM，使用
   `verifier_create_from_bytes` 包装成 `VerificationReport`。生产题（13/2401）
   还依赖 `StaticProductionVerifier` 的**文件路径**接口，需要字节化或暂时在网页
   上明确拒绝。浏览器内验证没有 Linux 进程隔离，权威性要重新声明。
4. **内存内 portfolio**：CLI 的 `CollectVerified` 依赖调用方 sink；需要实现一个
   内存 sink（按 cost/cycles/area 保留三槽），代替 `CandidateArchive` 的文件
   目录/发布流程。`OptimizeVerified` 若也要进 WASM，需要把 archive 换成内存
   结构。
5. **用户自定义输入/输出**：
   - 仅“上传 `.puzzle` + 下载 `.solution`”：只需要解析器，已经具备；
   - 若要在网页里**编辑**输入/输出，需要 puzzle 编辑器、权限位/字形/臂型校验
     和一个 puzzle 编码器。`src/IO` 目前只有 parser，没有生产版 `WritePuzzle`；
     现有写入逻辑只在若干 unit test 的局部 helper 里。
6. **前端展示**：需要
   - puzzle SVG 已有；
   - solution 对象列表/画布渲染（arm/glyph/track/input/output）；
   - 逐周期播放：给 `HexSimulator` 增加 snapshot/step API，或前端按磁带模拟；
   - metrics、验证状态、错误位置、下载按钮。
7. **浏览器运行时**：求解放 Web Worker（避免卡 UI）；设置 deadline、cycle/
   instruction/内存上限；提供取消；`ALLOW_MEMORY_GROWTH`；如果引入 pthreads
   还要 COOP/COEP，建议第一版保持单线程。
8. **部署与安全**：静态托管 + `application/wasm` MIME、缓存；用户上传文件在
   worker 内解析并限制大小；对 parser 做 fuzz/边界测试；若走服务端 API，另加
   速率限制和任务超时。Production 题和未知规则应给明确错误，而不是空结果。

## 两条落地路线

**A. 混合（最快、保权威）**

浏览器只做 UI/SVG/下载；求解和 `legacy-gate` 验证仍走现有原生 CLI 的服务端封装
（HTTP/WebSocket）。不需要 WASM 求解，1–2 天可打通上传→求解→展示。

**B. 纯 WASM（离线/隐私/零后端）**

1. 先接 standalone Standard + 字节解析/写回 + puzzle SVG：上传、快速求解、下载；
2. 再接 omsim 字节验证 + 完整 portfolio 的内存 sink；
3. 最后做 solution 播放器和自定义 puzzle 编辑器。
预计第 1 步是几天量级，完整链路是 1–2 周量级的工程，不是算法问题。


## GitHub Pages 可行性

**纯 WASM 方案可以只用 GitHub Pages**：它是静态文件托管，正好承载
`index.html + JS + .wasm + assets`；WASM 求解、omsim 字节验证和前端渲染都在
浏览器里完成，不需要服务器进程。

限制：

- Pages **不能**运行当前的 native CLI、`OpusOptimaVerifyWorker`、
  `posix_spawn` 或 Linux `libverify.so`；这些必须换成 WASM/进程内实现，或者把
  验证放到外部后端。
- Pages 不提供自定义 HTTP 响应头，因此不要依赖 `SharedArrayBuffer`/pthreads/
  COOP-COEP；第一版保持单线程 Web Worker。
- 站点有约 1 GB 的发布上限（WASM + HiGHS + assets 通常远低于此）；带宽是软限制。
- 公有仓库可直接用 Pages；私有仓库的 Pages 取决于账户计划。
- 自定义域名和 HTTPS 支持；用 GitHub Actions 的
  `configure-pages/upload-pages-artifact/deploy-pages` 发布 `dist/`。

如果保留 native `legacy-gate` 作为权威验证，Pages 只能放前端，求解/验证要放到
自建服务器或容器/Serverless 后端（CORS 配置好），这就不是“只用 Pages”了。
## 验收建议

- 同一批 146 题在 WASM 与原生 CLI 上逐槽指标一致（至少 standalone 模式）；
- `verify()` 对 438 个已验证槽返回与 native `legacy-gate` 相同的 status/metrics；
- 浏览器 worker 内单题预算可控（快速模式 <1 s，完整模式可取消）；
- 上传随机/畸形文件不崩溃、不超内存；
- 展示层能回放至少一个多臂解，且下载的 `.solution` 能被原生 gate 接受。
