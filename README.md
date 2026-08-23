# eqmdsk

`eqmdsk` 是一个轻量的 C++17 与 Python 库，用于读取、修改和写入 EFIT G、A、K、S 文件。它保留大型 EFIT 工具集中面向兼容性的文件读写能力，但不包含平衡分析、绘图、惰性加载、数据库、OMAS、MDSplus、SciPy 或其他框架集成，旨实现最小依赖、高速启动的同时，实现对于4种平衡文件的广泛支持和快速读写。对于一个129x129网格且含有附加信息的约1万行Gfile进行读写，其时间约为3.0ms和2.3ms，测试平台为 Ubuntu 24.04，不同平台可能不同。

## Python 快速开始

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

G-file 没有足够的元数据来区分全部 COCOS 约定，因此检测通常会返回多个候选。可以
从候选中选择一个来源，也可以在自动检测不可靠时显式给出来源进行强制转换：

```python
print(gfile.cocos.candidates)
converted = gfile.to_cocos(to_cocos=11, from_cocos=5, inplace=False)

gfile.select_cocos(5)  # 只选择来源，不改变 candidates 或文件字段
gfile.select_cocos(6)  # 可以在当前 candidates 中重新选择
gfile.to_cocos(11, from_cocos=6)
```

`select_cocos(source)` 只在 `source` 位于当前 `cocos.candidates` 时成功，并且只修改
`cocos.selected`。`to_cocos()` 省略 `from_cocos` 时使用当前 `cocos.selected`；如果
没有 selected，会抛出 `CocosError`。要绕过不可靠的自动检测，使用显式的
`from_cocos`；它不要求出现在当前 candidates 中，但必须是受支持的 COCOS 编号。参数名
使用 `from_cocos` 是因为 `from` 是 Python 保留关键字。

## 数组所有权

数值数组是指向 C++ 所有 Eigen 存储的可写 NumPy view，不存在隐藏转换或复制；
view 会保持对应文件对象或 K-file block 对象的生命周期。整数组赋值会替换底层数组，
长度和形状约束在 `save()` 时统一检查。C++ 侧调整容器尺寸时遵循普通 C++ 引用失效规则，
resize 后应重新获取 NumPy view。

标准字段类型严格区分整数、实数、字符串和数组类型。实数字段可以接收 Python `int`
并在 C++ 中规范化为 `double`；整数字段不接受浮点数。目标为实数数组时，整数 NumPy
数组可以安全转换为 `float64`，但 `float32`、字符串数组等不匹配的数组类型会被拒绝。

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

需要从零构造标准文件时，使用 `GFile.create()`、`AFile.create()`、`KFile.create()`
或 `SFile.create()`；字段填写顺序和最小可写示例见对应的 G/A/K/S 文件指南。

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
