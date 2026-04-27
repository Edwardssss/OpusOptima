<div align="center">
  <h1>🔬 OpusOptima</h1>
  <p><em>用于解析、渲染和求解 Opus Magnum 谜题文件的 C++20 工具包</em></p>

  <p>
    <img src="https://img.shields.io/badge/标准-C%2B%2B20-blue?logo=c%2B%2B" alt="C++20">
    <img src="https://img.shields.io/badge/构建-CMake%20%7C%20Ninja-green?logo=cmake" alt="Build">
    <img src="https://img.shields.io/badge/许可证-MIT-yellow" alt="License">
    <img src="https://img.shields.io/badge/状态-alpha-red" alt="Status">
  </p>
</div>

---

## 📖 概述

**OpusOptima** 是一个用于处理 [Opus Magnum](https://www.zachtronics.com/opus-magnum/)（Zachtronics 出品的炼金术主题谜题游戏）谜题文件的库与工具集。它提供：

- **解析** — 完整支持 `.puzzle` 二进制文件（版本 3）的读取，包括生产类谜题数据
- **渲染** — ASCII 预览与 SVG 导出，完美呈现六边形分子结构
- **求解** — 集成 [HiGHS](https://highs.dev/) 线性规划求解器，通过两种求解后端（基于 A* 的贪心规划器 + MILP 原子匹配）生成合法解

## ✨ 功能特性

### 已实现

| 功能 | 状态 |
|---|---|
| `.puzzle` 二进制格式解析器 | ✅ |
| `.solution` 二进制格式解析器 | ✅ |
| `.solution` 二进制格式写入器 | ✅ |
| 六边形网格坐标系 | ✅ |
| 分子与原子数据结构 | ✅ |
| 化学键类型处理（普通、三键） | ✅ |
| 机械臂指令解码（全类型：arm1-6、piston、baron） | ✅ |
| 轨道/管道/雕文（全部 9 种）对象支持 | ✅ |
| 支持ASCII 渲染puzzle | ✅ |
| 支持 SVG 导出puzzle文件渲染结果 | ✅ |
| 全指令集模拟 | ✅ |
| 碰撞检测（基于预计算扫描表 LUT） | ✅ |
| 确定性模拟引擎 | ✅ |
| A\* 寻路算法 | ✅ |
| GreedyPlanner 求解器（贪心原子分配 + 直接路径规划） | ✅ |
| RecipeMilp 求解器（基于 HiGHS MILP 的原子匹配） | ✅ |
| 可根据结果生成 `.solution` | ✅ |
| spdlog 日志输出 | ✅ |
| 支持模拟器验证求解结果 | ✅ |

### 未来计划

- [ ] 多臂协同求解（piston/baron/track）
- [ ] 编译为 WebAssembly，实现网页端使用
- [ ] 优化算法，逼近Pareto边界
- [ ] 提高算法并行度，从而提升求解速度

---

## 🏗️ 项目结构

```
OpusOptima/
├── CMakeLists.txt
├── .clang-format
├── .clangd
├── .gitignore
├── LICENSE
├── project.md
├── analyse_file.md
├── src/
│   ├── main.cpp
│   ├── Core/
│   │   ├── CoreConstants.hpp
│   │   ├── HexGrid.hpp
│   │   ├── LogManager.hpp
│   │   ├── LogManager.cpp
│   │   ├── MachineState.hpp
│   │   └── PuzzleSpecs.hpp
│   ├── Engine/
│   │   ├── CollisionLUT.cpp
│   │   ├── CollisionLUT.hpp
│   │   ├── HexSimulator.cpp
│   │   └── HexSimulator.hpp
│   ├── IO/
│   │   ├── BinaryParser.cpp
│   │   ├── BinaryParser.hpp
│   │   ├── BinaryWriter.cpp
│   │   ├── BinaryWriter.hpp
│   │   └── libverify.h
│   ├── Render/
│   │   ├── AsciiRenderer.cpp
│   │   ├── AsciiRenderer.hpp
│   │   ├── SvgExporter.cpp
│   │   └── SvgExporter.hpp
│   └── Solvers/
│       ├── AStar.hpp
│       ├── BaseSolver.hpp
│       ├── BaseSolver.cpp
│       ├── GreedyPlanner.cpp
│       ├── GreedyPlanner.hpp
│       ├── RecipeMilp.cpp
│       └── RecipeMilp.hpp
├── tests/
│   ├── unit_test/
│   ├── functional_test/
│   └── benchmark_test/
├── asset/
│   ├── puzzle/
│   ├── solution/
│   └── libverify.so
└── third_party/
    ├── spdlog/
    ├── HiGHS/
    ├── googletest/
    └── benchmark/
```

---

## 🚀 快速开始

### 前置要求

- **CMake** ≥ 3.20
- 支持 C++20 的编译器（GCC 11+、Clang 14+、Apple Clang 14+）
- Ninja（推荐）或 Make

### 构建

```bash
git clone https://github.com/Edwardssss/OpusOptima.git
cd OpusOptima

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 运行测试

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build
cd build && ctest --output-on-failure
```

### 运行基准测试

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build
./build/OpusOptimaBenchmarks
```

---

## 🧪 使用示例

### CLI 使用

```bash
# 使用默认求解器 (GreedyPlanner) 求解谜题
./build/OpusOptima -p asset/puzzle/easy/easy-conduit.puzzle

# 指定求解算法（已注册名称："GreedyPlanner"、"greedy"、"RecipeMilp"、"milp"）
./build/OpusOptima -p asset/puzzle/easy/easy-infinite-product.puzzle -s greedy

# 列出所有可用求解器
./build/OpusOptima --list-solvers

# 自定义输出路径
./build/OpusOptima -p asset/puzzle/easy/just-salt.puzzle -o my-solution.solution
```

**默认输出目录**：解文件写入 `result/<谜题名>-solution.solution`。
日志记录在 `log/opusoptima.log`，包含模块级详情（solver、io、simulator、validator）。

### SVG 输出样例

SVG 渲染器会生成带颜色编码的原子（Salt、Air、Earth、Fire 等）、化学键以及简洁卡片式布局的分子图——适合嵌入文档或分享。

---

## 🧩 puzzle 文件格式

OpusOptima 解析 Opus Magnum 使用的私有二进制 `.puzzle` 格式。主要字段：

| 字段 | 类型 | 说明 |
|---|---|---|
| 版本号 | `int32`（小端） | 期望值：`3` |
| 名称 | C# varint 前缀字符串 | 谜题显示名称 |
| 创建者 Steam ID | `uint64` | 作者标识 |
| 权限位 | `uint64` | 分享设置 |
| 输入分子 | `Molecule` 列表 | 起始反应物 |
| 输出分子 | `Molecule` 列表 | 目标产物 |
| 输出倍率 | `int32` | 分数倍率 |
| 生产信息 | 可选块 | 生产类谜题的额外数据 |

每个 `Molecule` 由原子（类型 + 六边形坐标）和化学键（类型 + 起点/终点坐标）组成。

---

## 🧾 solution 文件格式

OpusOptima 同样解析二进制 `.solution` 格式（格式标签 `7`）。主要字段：

| 字段 | 类型 | 说明 |
|---|---|---|
| 格式标签 | `int32`（小端） | 期望值：`7` |
| 谜题名称 | C# varint 前缀字符串 | 对应谜题的名称 |
| 解法名称 | C# varint 前缀字符串 | 用户命名的解法名 |
| 指标 | `(类型, 值)` int32 对列表 | 周期、费用、面积、指令数 |
| 对象数量 | `int32` | 放置的对象数 |
| 对象 | 可变长度列表 | 每个对象含类型、位置、5-int32 头及类型相关体 |

### 支持的对象类型

全部 **23 种**来自真实数据的对象类型均已支持，各自采用自描述二进制结构：

| 分类 | 类型 | 体结构 |
|---|---|---|
| 指令型 | `arm1`–`arm6`、`piston`、`baron` | 5-int32 头（ic 在索引 [3]）+ N×5 字节指令 |
| 轨道/管道 | `track`、`pipe` | 头（segCount 在索引 [4]）+ 可变长坐标段 |
| 固定大小 | `input`、`output`、`out-std`、`out-rep`、`bonder`、`bonder-speed`、`bonder-prisma`、`unbonder`、`glyph-*`（9 类） | 恰好 5-int32 头（20 字节） |

解析器已通过**4,400+ 个真实解法文件**和**169+ 个谜题文件**的验证，数据来源包括全部游戏战役关卡、锦标赛存档与社区仓库。

---

## 🛠️ 依赖项

| 库 | 用途 | 位置 |
|---|---|---|
| [spdlog](https://github.com/gabime/spdlog) | 高速、仅头文件的日志库 | `third_party/spdlog` |
| [HiGHS](https://highs.dev/) | 高性能线性/整数规划求解器 | `third_party/HiGHS` |
| [Google Test](https://github.com/google/googletest) | 单元与功能测试 | `third_party/googletest` |
| [Google Benchmark](https://github.com/google/benchmark) | 微基准测试 | `third_party/benchmark` |

所有依赖均 vendored 在 `third_party/` 目录下，由 CMake 自动构建，无需手动安装。

项目根目录提供了 `.clang-format`、`.clangd`、`.gitignore` 等配置文件，用于保持代码风格一致和静态分析集成。

---

## 📄 许可证

本项目采用 MIT 许可证发布——详见 [LICENSE](LICENSE) 文件。

*Opus Magnum 是 Zachtronics LLC 的商标。本项目与 Zachtronics 无关，亦未经其认可。*

---

## 📚 致谢

本项目的灵感来源于gtw123的[OpusSolver](https://github.com/gtw123/OpusSolver)，项目中用于测试的`.puzzle`和`.solution`文件全部来源于[omsim](https://github.com/ianh/omsim)库，在此感谢所有开发者的付出。