# Python API

`eqmdsk` 的 Python 类由共享的 C++17 核心实现。构造对象时立即读取整个文件，写出
时按照对应标准生成新文件。

Python 对象是便于交互式调试和 PyCharm 展开的映射 facade；实际字段存储、NamelistBlock
生命周期、解析、校验和序列化均由 C++ 完成。稳定的外部接口是本文列出的文件类、
`Namelist`、`NamelistBlock`、字段映射操作、`save()` 和异常。对象上的 `_core`、
`_assign`、`_erase` 等下划线成员仅用于内部桥接和调试，不属于发布后的兼容承诺。

所有文件对象、`Namelist` 和 `NamelistBlock` 都提供 `.copy()`。它返回独立的 C++ 深
拷贝：标量、字符串、数组、block、G-file 的 `AuxNamelist` 以及 A-file 的 header/footer
都不会与原对象共享可变存储。复制后的文件保留原对象的路径属性；调用 `save(path)`
时仍可指定新的输出路径。

## 打开和写出

G/A/K/S 文件对象的构造函数和 `save()` 接受字符串或 `os.PathLike[str]`。每个文件
对象都暴露三个路径属性：`filename` 是文件名，`path` 是输入路径的相对/绝对形式，
`abspath` 是规范化的绝对路径。三者始终以 Python `str` 返回，不返回 `Path` 或
`bytes`。独立 `Namelist` 没有路径属性，也没有公共保存方法；它只能作为内存中的
namelist 映射使用，内容需要复制到 `KFile` 或 `GFile` 后由文件对象保存。

如果输入只有文件名，例如 `"g.dat"`，则 `path` 为 `"./g.dat"`；输入
`"data/g.dat"` 时保留该相对路径；输入绝对路径时 `path` 保持绝对。`abspath` 始终
是绝对路径。`filename`、`path` 和 `abspath` 都是只读属性，不提供对象内重命名操作。
创建空对象且没有 path 时，三个属性为空字符串，必须给 `save(path)` 显式目标。

```python
g = eqmdsk.GFile("g123456.01234")
a = eqmdsk.AFile("a123456.01234")
k = eqmdsk.KFile("k123456.01234")
s = eqmdsk.SFile("s123456.01234")

g.save()
g.save("copy.any-suffix")
```

对于 `create()` 创建的空对象，`filename`、`path` 和 `abspath` 都为空，
调用无参数 `save()` 会抛出 `ValidationError`；必须使用 `save(path)` 指定写出目标。
显式 `save(path)` 只影响本次写出，不会改变对象的三个路径属性。

从零创建请使用显式工厂，不能用不存在路径的构造器代替。字段填写、缺失字段报告
和各类文件的最小示例见对应的 [G-file](gfile.md)、[A-file](afile.md)、[K-file](kfile.md)
和 [S-file](sfile.md) 指南。

A-file 的文件级 `header` 和 `footer` 也是可读写的 Python `str`；它们由 C++ 核心
持有并随 `save()` 写回，不会出现在字段 `keys()` 中。G/S/K 的头部语义已经映射为
标准字段、标签或 NamelistBlock，不额外提供 raw header/footer 属性。

## 映射行为

G/A/S 对象是可展开的字典式 facade，`print(g)`、`keys()`、`items()`、`values()`、
`get()`、`len(g)`、`name in g` 和迭代都只显示公开字段。C++ 核心拥有权威字段存储；
C++ 同时维护各格式的字段 schema、必填/可选缺失状态和增删规则，Python 映射只负责
值转换和调试展示。标准字段可以替换，必填字段删除后恢复为 `None`，可选扩展字段删除
后从集合移除；新增字段必须属于该格式允许的标准或扩展字段。

K-file 是嵌套映射。block 使用 `NamelistBlock` 赋值创建，字段和 block 的
增删直接使用映射操作：

```python
print(k["IN1"])
k["IN1"]["LIMITR"] = 61
k["IN2"] = eqmdsk.NamelistBlock({"VALUE": 1})
del k["IN2"]
```

G-file 的附加 namelist 使用：

```python
out1 = g["AuxNamelist"]["OUT1"]
print(out1.keys())
out1["ISHOT"] = 67591
g.save("g.new")
```

无论输入文件尾部是否实际包含 namelist，读取后的 `GFile` 都固定包含一个
`AuxNamelist`。没有尾部内容时，它是空的 `Namelist`；`GFile.create()` 也同样初始化
空对象。`AuxNamelist` 的公开类型就是 `Namelist`，可以像普通 `Namelist` 一样修改，
但只能随所属 G-file 写出，不能单独保存。

独立 `Namelist` 的内容可以复制到文件对象后保存：

```python
nml = eqmdsk.Namelist()
nml["IN1"] = eqmdsk.NamelistBlock({"LIMITR": 60})
k = eqmdsk.KFile.create()
k.update(nml)
k.save("created.k")
```

## C++ 对象关系

`eqmdsk::Namelist` 是一个真实的、无路径的 Fortran namelist 对象，自己负责 block、
字段、解析和规范序列化。`eqmdsk::KFile` 公有继承 `Namelist`，在此基础上增加文件
路径和默认写回行为。`eqmdsk::GFile` 则持有一个 `Namelist` 实例作为 `AuxNamelist`：
它不是 `KFile`，也没有 `_AuxNamelist` 子类或另一层通用核心包装。Python 绑定直接把
该实例暴露为 `eqmdsk.Namelist`；只有保存时要求通过所属 G-file，避免产生没有 G-file
主体的独立尾部输出。

## 值类型

| C++ 类型 | Python 类型 |
| --- | --- |
| `bool` | `bool` |
| `std::int64_t` | `int` |
| `double` | `float` |
| `std::string` | `str` |
| `IntVector` | 可写 `numpy.ndarray[int64]` |
| `DoubleVector` | 可写 `numpy.ndarray[float64]` |
| `DoubleMatrix` | 可写 `numpy.ndarray[float64]` |
| `StringVector` | `list[str]` |

数值数组是 C++ 存储的可写 view。直接修改 view 会修改对象；整体赋值会替换底层数组，
长度和 `(NH, NW)` 形状等约束统一在 `save()` 时校验。`PCURRT`、`PSIRZ` 等二维字段
不会被扁平化。

标准字段的类型在 C++ schema 中严格定义。实数字段接受 Python `int` 并自动转换为
`double`；整数字段不接受浮点值。对于实数数组，`float64` 是标准类型，整数 NumPy
数组可以转换为 `float64`，但 `float32`、字符串数组和其它不匹配类型会在赋值时拒绝。

## G-file COCOS

```python
converted = g.to_cocos(11, from_cocos=5, inplace=False)
g.select_cocos(5)
g.to_cocos(11)
```

省略 `from_cocos` 时使用对象已选择的唯一来源；来源歧义或未知会抛出 `CocosError`。
COCOS 转换只作用于经典 G-file 字段，不对扩展和 AuxNamelist 做未经确认的物理换算。

## 规范写出和异常

保存遵循 `parse -> save -> parse` 语义等价，不承诺原始字节保真。注释、空格、换行、
数字格式、重复赋值和非标准穿插文本可能被规范化。

- `IOError`：打开、读取、写出或关闭失败；
- `ParseError`：输入截断或语法无效，带 `filename`、`line`、`column`；
- `ValidationError`：字段不满足标准尺寸、计数或数值要求；
- `FieldError`：未知字段、NamelistBlock 或变量；
- `CocosError`：COCOS 来源缺失、歧义或不支持。

所有异常都继承 `eqmdsk.Error`。
