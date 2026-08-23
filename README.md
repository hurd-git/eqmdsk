# Eqmdsk

`eqmdsk` 是一个轻量易用的 C++17 与 Python 库，用于读取、修改和写入 EFIT G、A、K、S 文件。它保留大型 EFIT 工具集中面向兼容性的纯文件数据读写能力，但不包含进一步操作，如平衡分析、绘图、惰性加载、数据库、OMAS、MDSplus、SciPy 或其他框架集成。`eqmdsk` 旨在保留最小依赖、高速启动的同时，实现对4种平衡文件的广泛支持和快速读写。对于一个129x129网格且含有附加信息的约1万行Gfile进行读写，其时间约为3.0ms和2.3ms，测试平台为 Ubuntu 24.04，不同平台可能不同。

## Python 快速开始

### 文件读写

```python
import eqmdsk

gfile = eqmdsk.GFile("g123456.01234")  # 构造时立即读取完整文件
print(gfile.keys())
print(gfile["PSIRZ"].shape)             # (NH, NW)：行为 Z，列为 R
print(gfile.get("AuxNamelist"))          # 固定存在，可能为空或包含 OUT1/BASIS 等 block

gfile["CURRENT"] = 1.2e6
gfile["PSIRZ"][0, 0] = -0.25           # 可写的零拷贝 NumPy view
gfile.save()                            # 写到原始路径
gfile.save("result.with-any-suffix")   # 使用调用者给出的路径
```

本库不提供公共 `load()` 或 `read()` 包装函数。构造 `GFile`、`AFile`、`KFile` 或
`SFile` 时会读取完整文件；`save()` 会一次性完成序列化与写入。

标准字段使用规范的 EFIT 大写名称。K-file 是两层字典，NamelistBlock 和变量查询遵循
Fortran 标识符的大小写不敏感规则：

```python
kfile = eqmdsk.KFile("k123456.01234")
assert kfile["xlim"] is not None
print(kfile.keys())
print(kfile["IN1"]["XLIM"])
```

### Cocos 转换

G-file 没有足够的元数据来区分全部 COCOS 约定，因此检测通常会返回多个候选。可以使用 select_cocos 确定候选，并使用 to_cocos 进行转换。当显式指定`from_cocos`时，to_cocos 也可以进行强制转换：

```python
print(gfile.cocos.candidates)
gfile.select_cocos(5)  # 如果不在 candidates 中，会抛出错误。
# 只会改变 gfile.selected，不会修改其它数据，用于 cocos 不确定时手动分配 cocos
print(gfile.selected)  # 查看当前的 cocos
gfile.to_cocos(to_cocos=11)  # 将当前的 cocos 转换到目标 cocos

converted = gfile.to_cocos(to_cocos=11, from_cocos=5, inplace=False)
# 也可以输入 from_cocos 进行强制转换。关闭 inplace 可以维持旧 gfile 不变，返回一个新的转换后的 gfile
```
更多详细信息可在[GFile文档](https://github.com/hurd-git/eqmdsk/blob/main/docs/gfile.md)和[Python-API](https://github.com/hurd-git/eqmdsk/blob/main/docs/python-api.md)中查阅。

### 从零创建文件

需要从零构造标准文件时，使用 `GFile.create()`、`AFile.create()`、`KFile.create()` 或 `SFile.create()`；字段填写顺序和最小可写示例见对应的 G/A/K/S 文件指南。

以 Gfile 为例，可以使用类方法`GFile.create(nw, nh)`创建一个空的 Gfile 文件，通过这种方式创建的对象没有路径，在保存时需要显示指定路径。保存的必要条件是必填字段都已填写，并且尺寸是合法的。下面的例子创建一个最小的 `3 x 2` 网格 G-file：

```python
import numpy as np

g = eqmdsk.GFile.create(3, 2)
g["CASE"] = "minimal"
for name in ("RDIM", "ZDIM", "RCENTR", "RLEFT", "ZMID", "RMAXIS", "ZMAXIS",
             "SIMAG", "SIBRY", "BCENTR", "CURRENT"):
    g[name] = 1.0
for name in ("FPOL", "PRES", "FFPRIM", "PPRIME", "QPSI"):
    g[name] = np.zeros(3)
g["PSIRZ"] = np.zeros((2, 3))
g["NBBBS"] = g["LIMITR"] = 0
for name in ("RBBBS", "ZBBBS", "RLIM", "ZLIM"):
    g[name] = np.empty(0)

g.save("created.g")
```

保存前可以用 `g.missing_fields()` 检查是否还有未填写的必填字段。

## 读写性能

CI 在 Ubuntu、macOS arm64 和 Windows 上使用相同的完整文件基准测试，分别测量构造时的
解析、`save()` 写入和再次构造时的重新解析。测试使用 Python 3.14，G-file 为确定性的
172 x 172 合成网格（约 494 KB），S-file 约 700 KB；每个平台先预热 5 次，再测量 15 次，
表中为中位数，单位为毫秒。

| 平台 | 文件 | 解析 | 写入 | 重新解析 |
| --- | --- | ---: | ---: | ---: |
| Ubuntu | G | 2.170 | 1.984 | 1.976 |
| Ubuntu | A | 0.174 | 0.095 | 0.177 |
| Ubuntu | K | 0.631 | 2.022 | 0.804 |
| Ubuntu | S | 3.820 | 8.244 | 3.388 |
| macOS arm64 | G | 3.973 | 2.513 | 3.959 |
| macOS arm64 | A | 0.209 | 0.157 | 0.279 |
| macOS arm64 | K | 0.999 | 1.845 | 3.423 |
| macOS arm64 | S | 4.534 | 14.799 | 4.689 |
| Windows | G | 9.844 | 3.087 | 8.023 |
| Windows | A | 0.416 | 0.266 | 0.381 |
| Windows | K | 2.071 | 4.859 | 2.743 |
| Windows | S | 13.538 | 34.390 | 13.563 |

结果表明，常用 G-file 的解析和写入在所有平台都保持毫秒级；macOS arm64 的 G-file
解析约为 Ubuntu 的 1.8 倍，Windows 的解析较慢，但仍未达到影响整文件读写工作流的数量级。
A-file、K-file 的文件规模较小，时间容易受到 runner 调度和临时文件系统影响。不同机器、
编译器、Python 版本和文件系统会产生正常波动，因此这些数值用于跨平台回归观察，不作为
固定硬件性能承诺。完整 JSON 报告可在对应的 [CI 运行](https://github.com/hurd-git/eqmdsk/actions/runs/32627280722)
的 `performance-*` artifacts 中下载；本地可运行 `benchmarks/benchmark_io.py` 重复测试。

```python
view = gfile["PSIRZ"]
assert view.flags.c_contiguous and view.flags.writeable
```

## 错误与规范写出

公共异常包括 `Error`、`IOError`、`ParseError`、`ValidationError`、`FieldError`
和 `CocosError`。Python 的 `ParseError` 实例还提供 `filename`、`line`、`column`。

读取器接受常见的 EFIT/Fortran 排版变体，并识别经典 G-file 主体、标准 EFIT 数值
扩展、`IPLCOUT` 数据和尾部 `AuxNamelist`。无法安全解释的数值扩展保留为
`UNPARSED_EXTENSION`，不会猜测物理含义。writer 从公开字段重新生成规范文本；
注释、原始空格和重复赋值历史不会逐字复制。保证的是解析、规范写出、重新解析后
公开 keys、值、数组形状和类型语义一致。

更多内容参见[文档索引](docs/README.md)和[Python API 指南](docs/python-api.md)，
以及 [G-file](docs/gfile.md)、[A-file](docs/afile.md)、[K-file](docs/kfile.md)、
[S-file](docs/sfile.md) 独立指南。[兼容性契约](docs/compatibility.md)定义了精简
schema 与稳定性边界。

## C++ 使用

```cpp
#include <eqmdsk/eqmdsk.hpp>

eqmdsk::GFile file("g123456.01234");
auto& psi = std::get<eqmdsk::DoubleMatrix>(file.at("PSIRZ"));
psi(0, 0) = -0.25;
file.save("result");
```

安装后，可通过 CMake 使用导出的静态库：

```cmake
find_package(eqmdsk 0.9 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE eqmdsk::eqmdsk)
```

Eigen 是公共头文件接口的一部分，消费项目必须能够找到 `Eigen3::Eigen`。静态库属于
源码 SDK 构建产物，应使用消费项目的工具链和兼容的 Eigen 3.4+ 重新构建。项目不承诺
跨工具链或跨 Eigen 版本的二进制 ABI。Python wheel 有意不安装 C++ 静态库、头文件
或 CMake package。

## 开发构建

本地开发时可在 `extern/eigen` 和 `extern/pybind11` 中提供 Eigen 与 pybind11。
这些源码树有意排除在 Git 和源码发行包之外，也支持使用系统安装。两者均不存在时，
Python 构建可以获取经过版本固定和校验的 Eigen；纯 C++ 构建需要显式允许网络获取。

```console
uv venv --python 3.12
uv pip install --python .venv/bin/python -e '.[test]'
.venv/bin/python -m pytest
```

仅构建 C++：

```console
cmake -S . -B build \
  -DEQMDSK_BUILD_PYTHON=OFF \
  -DEQMDSK_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix build/prefix --component Development
```

普通 CMake 配置默认仅构建 C++；scikit-build-core 的 Python 包构建会自动启用扩展。
只有设置 `EQMDSK_FETCH_DEPENDENCIES=ON` 时才允许下载缺失的 Eigen，回退版本已固定
并校验 checksum。

默认测试完全离线。另有一组 checksum 固定、MIT 许可的 G/A 兼容性语料，可显式获取：

```console
.venv/bin/python tests/public_data/fetch.py --output build/public-data
EQMDSK_PUBLIC_FIXTURE_DIR=build/public-data \
  .venv/bin/python -m pytest tests/python/test_public_fixtures.py
```

运行 `benchmarks/benchmark_io.py` 可重复测试整文件解析、写入、重新解析所需时间和
进程峰值 RSS；参考结果记录在 [benchmarks/RESULTS.md](benchmarks/RESULTS.md)。

长时间解析器 fuzz 测试见 [tests/fuzz/README.md](tests/fuzz/README.md)，发布验证流程
见 [docs/releasing.md](docs/releasing.md)。

## 许可证

eqmdsk 使用 MIT 许可证。构建期依赖和公共头文件依赖保留各自许可证，详见
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
