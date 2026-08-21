# eqmdsk

`eqmdsk` 是一个轻量的 C++17 与 Python 库，用于读取、修改和写入 EFIT G、A、K、
S 文件。它保留大型 EFIT 工具集中面向兼容性的文件读写能力，但不包含平衡分析、绘图、
惰性加载、数据库、OMAS、MDSplus、SciPy 或其他框架集成。

`0.9.0` 版本的 C++ 与 Python 公共接口共享同一套 C++ 实现。

## Python 快速开始

```python
import eqmdsk

gfile = eqmdsk.GFile("g123456.01234")  # 构造时立即读取完整文件
print(gfile.keys())
print(gfile["PSIRZ"].shape)             # (NH, NW)：行为 Z，列为 R

gfile["CURRENT"] = 1.2e6
gfile["PSIRZ"][0, 0] = -0.25           # 可写的零拷贝 NumPy view
gfile.write()                            # 严格写回原始路径
gfile.write("result.with-any-suffix")   # 严格使用调用者给出的路径
```

本库不提供公共 `load()` 或 `read()` 包装函数。构造 `GFile`、`AFile`、`KFile` 或
`SFile` 时会读取完整文件；`write()` 会一次性完成序列化与写入。

标准字段使用规范的 EFIT 大写名称，并区分大小写。唯一例外是 K-file 的直接映射、
变量和 section 查询，因为 Fortran namelist 标识符不区分大小写。K-file 的通用
`fields` 属性仍暴露规范大写名称的 `FieldMap`，因此其中的键必须精确使用大写：

```python
kfile = eqmdsk.KFile("k123456.01234")
assert kfile["xlim"] is not None
print([section.name for section in kfile.sections])
print(kfile.entry("IN1", "XLIM").values)
```

G-file 没有足够的元数据来区分全部 COCOS 约定，因此检测通常会返回多个候选。可以
在转换时显式给出来源，或者先从已检测候选中选择来源：

```python
print(gfile.cocos.candidates)
converted = gfile.to_cocos(to_cocos=11, from_cocos=5, inplace=False)

gfile.select_cocos(5)  # 之后可以省略 from_cocos
gfile.to_cocos(11)
```

`from_cocos=None` 时，`to_cocos()` 使用对象当前唯一或已显式选择的 COCOS；只有该
来源仍不明确时才抛出 `CocosError`。原始检测结果保存在 `error.result` 中。参数名
使用 `from_cocos` 是因为 `from` 是 Python 保留关键字。

## 数组所有权

数值数组是指向 C++ 所有 Eigen 存储的可写 NumPy view，不存在隐藏转换或复制；
view 会保持对应文件对象或 `FieldMap` 的生命周期。整数组赋值会复制到现有存储中，
且必须保持原有 shape 和长度。C++ 侧调整容器尺寸时遵循普通 C++ 引用失效规则，
resize 后应重新获取 NumPy view。

```python
view = gfile["PSIRZ"]
assert view.flags.c_contiguous and view.flags.writeable
```

## 错误与原始内容保留

公共异常包括 `Error`、`IOError`、`ParseError`、`ValidationError`、`FieldError`
和 `CocosError`。Python 的 `ParseError` 实例还提供 `filename`、`line`、`column`。

不属于已知 schema 的输入会在不猜测其含义的前提下保留：

- G-file 的前导内容、header 后缀和扩展尾部；
- A-file 的 header 和 footer；
- K-file 的顺序、重复 entry、注释、索引/raw 值、section 原始拼写、终止符以及
  namelist 块外文本；
- S-file 的行间和尾部文本。

`extra_header`、`extension_tail`、`header`、`footer`、`RawSection.data` 等
Python opaque 属性均为 `bytes`，因此嵌入的 NUL 和非 UTF-8 数据不会被解码或丢失。

`raw_sections` 是这些保留区域的只读视图。未修改文件写出后重新解析必须保持语义
一致；只有在不需要把原始文本变成第二套可编辑状态时，才要求字节完全一致。

更多内容参见[文档索引](docs/README.md)、[Python API 指南](docs/python-api.md)，
以及 [G-file](docs/gfile.md)、[A-file](docs/afile.md)、[K-file](docs/kfile.md)、
[S-file](docs/sfile.md) 独立指南。[格式与字段参考](docs/formats.md)和
[兼容性契约](docs/compatibility.md)定义了精简 schema 与稳定性边界。

## C++ 使用

```cpp
#include <eqmdsk/eqmdsk.hpp>

eqmdsk::GFile file("g123456.01234");
auto& psi = std::get<eqmdsk::DoubleMatrix>(file.at("PSIRZ"));
psi(0, 0) = -0.25;
file.write("result");
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
