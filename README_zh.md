# OpusOptima

用于解析、验证、渲染和求解 Opus Magnum 谜题文件的 C++20 工具包。默认 `assembly`
是有界候选组合；CLI 的额外 portfolio 会优先尝试上游 Standard 移植。

**网页演示：** https://edwardssss.github.io/OpusOptima/

## 当前状态

| 指标 | 数值 |
|---|---|
| 参考题库覆盖 | 146/146 字节去重题目 |
| cost / oracle | **0.8567×**（89,825 / 104,855） |
| cycles / oracle | **0.7420×**（66,427 / 89,524） |
| area / oracle | **0.8211×**（20,285 / 24,705） |
| 三项全胜 / 全负 | 55 / 0 |
| 外部 2401 题库抽样 | **300/300** |
| 完整 C++ 测试 | 1103 项：1098 通过、5 跳过、0 失败 |

两次最终审计的槽位完全一致（0/438 差异）。此前超时的 `GEN737` 现在约
12–14 秒完成。

## 构建

需要 C/C++20 编译器、CMake 3.20+、Ninja 和已初始化的子模块。

```sh
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j4
ctest --test-dir build --output-on-failure -j1
```

优化构建请使用独立目录并设置 `-DCMAKE_BUILD_TYPE=Release`。

## 运行

```sh
./build/OpusOptima --help
./build/OpusOptima -p asset/puzzle/easy/just-salt.puzzle
./build/OpusOptima -p asset/puzzle/campaign/ch1-and-prologue/P010.puzzle -s assembly
./build/OpusOptima verify --puzzle <puzzle> --solution <solution> --authority legacy-gate --json
```

省略 `-s` 即选择有界 `assembly` portfolio。默认输出到
`result/<puzzle-stem>-solution.solution`，日志写入 `log/opusoptima.log`。
`verify` 只复验已有解，不发布也不改写文件。

## 验证

发布权威是锁定版上游 [ianh/omsim](https://github.com/ianh/omsim)，由
`third_party/omsim` 子模块从源码构建，并隔离在 native worker 进程中。默认权限是
`legacy-gate`；`--authority independent` 只在其声明的有限子域内可用。

子模块缺失会让配置直接失败，不会静默关闭验证。`-DOPUSOPTIMA_ENABLE_LIBVERIFY=OFF`
只用于仅编译场景，此时默认发布路径会关闭（fail closed）。

## 浏览器

`wasm/` 是独立的 Emscripten target，包含 Standard 求解器、二进制 codec 和 omsim。
`web/` 是双语静态页面，支持：

- 上传、求解、验证和下载；
- 求解后立即显示只读机器布局，并播放 omsim 逐周期动画；
- 可视化构建原子、键、多端口和 production metadata；
- 高级文本/JSON 编辑，以及“冶炼金”（Refined Gold）示例。

Pages 部署见 `.github/workflows/pages.yml`。本地构建：

```sh
emcmake cmake -S wasm -B build-wasm -DCMAKE_BUILD_TYPE=Release
emmake cmake --build build-wasm --parallel 2
mkdir -p web/dist
cp web/index.html web/app.js web/i18n.js web/layout.js web/puzzle-builder.js \
   web/style.css web/example.puzzle web/dist/
cp build-wasm/opusoptima.js build-wasm/opusoptima.wasm web/dist/
```

网页使用进程内 omsim 验证，不是 native 隔离的 `legacy-gate` 发布事务。

## 目录

| 路径 | 作用 |
|---|---|
| `src/IO/` | Puzzle/solution codec、验证 worker、原子发布 |
| `src/Solvers/` | 规划、物理 lowering 和有界候选编译器 |
| `src/Solvers/Standard/` | 上游 Standard 求解器移植 |
| `src/Verification/` | 独立与参考验证后端 |
| `src/Core/`、`src/Engine/`、`src/Render/` | 坐标、模拟与渲染 |
| `wasm/`、`web/` | 浏览器求解器、编辑器和 Pages UI |
| `tests/`、`asset/`、`third_party/` | 测试、题库和依赖 |

## 许可证

项目代码采用 MIT 许可证。OpusSolver 来源说明见
[third_party/OpusSolver-LICENSE.txt](third_party/OpusSolver-LICENSE.txt)；omsim 由
`third_party/omsim` 子模块锁定，许可证见
[third_party/omsim/COPYING](third_party/omsim/COPYING)。
