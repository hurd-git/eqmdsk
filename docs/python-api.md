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
g.select_cocos(6)
g.to_cocos(11, from_cocos=6)
```

`g.cocos` 是可展开的 `CocosResult`，包含：

- `candidates`：根据当前 G-file 字段检测出的候选来源数组；
- `selected`：当前选择的来源，可能为 `None`，也可能是显式选择后不唯一的候选；
- `diagnostic`：检测器给出的诊断文本；
- `is_unique()`、`is_ambiguous()`、`has_match()`：对候选集合的状态判断。

`is_unique()` 和 `is_ambiguous()` 只根据 candidates 数组判断，不会因为 selected 被
显式设置而改变；因此候选有多个但已选择其中一个时，仍然是 ambiguous，同时
`selected` 可以正常用于转换。

G-file 初始化和读取完成后，内部 `_detect_cocos()` 根据 `CURRENT`、`BCENTR`、
`SIMAG`、`SIBRY` 和 `QPSI` 的符号构造新的 `CocosResult`：候选为空或多于一个时
`selected` 为 `None`，候选恰好一个时自动选择该唯一值。Python 调试时可以调用
`g._detect_cocos()` 获取当前字段的全新检测结果；它不会修改 `g.cocos`。

`g.select_cocos(source)` 不重新检测，也不执行数值转换。它只检查 `source` 是否在当前
`candidates` 中；合法时只更新 `selected`，候选数组、诊断文本和所有 G-file 字段都不变。
因此可以在多个候选之间反复选择；选择与当前 `selected` 相同的值时不做任何操作。
不在 candidates 中的值会抛出 `CocosError`。

`g.to_cocos(to_cocos, from_cocos=None, inplace=True)` 执行实际转换。来源规则是：
显式给出 `from_cocos` 就使用它，否则使用当前 `g.cocos.selected`；两者都没有时抛出
`CocosError`。显式来源可以不在 candidates 中，这是自动识别失败时的强制转换入口，但
仍必须是库支持的 COCOS 编号。转换只作用于经典 G-file 字段，不对扩展和 AuxNamelist
做未经确认的物理换算。

转换成功后，程序先对转换后的字段重新运行检测，得到新的 `candidates` 和
`diagnostic`，再把 `selected` 直接设置为 `to_cocos` 的目标值。因此 selected 与
candidates 是独立状态，selected 不要求属于新的 candidates。COCOS 状态不会写入文件；
重新读取文件时会重新检测。

## 规范写出和异常

保存遵循 `parse -> save -> parse` 语义等价，不承诺原始字节保真。注释、空格、换行、
数字格式、重复赋值和非标准穿插文本可能被规范化。

- `IOError`：打开、读取、写出或关闭失败；
- `ParseError`：输入截断或语法无效，带 `filename`、`line`、`column`；
- `ValidationError`：字段不满足标准尺寸、计数或数值要求；
- `FieldError`：未知字段、NamelistBlock 或变量；
- `CocosError`：COCOS 来源未选择、非法或不支持。

所有异常都继承 `eqmdsk.Error`。

## 公共类与方法参考

本节按成员、参数、返回值和用途说明稳定的 Python 接口。文件类和 namelist 类都由
C++ 核心持有实际数据；NumPy 数组通常是指向 C++ 存储的可写 view。

### GFile

| 成员 | 参数 | 返回值 | 用途 |
| --- | --- | --- | --- |
| GFile(path) | str 或 PathLike[str] | GFile | 立即读取完整 G-file |
| GFile.create(nw, nh) | 两个 int 网格尺寸 | GFile | 创建无路径、待填写字段的新对象 |
| g.cocos | 无 | CocosResult | 查看候选 COCOS、selected 与诊断 |
| g._detect_cocos() | 无 | CocosResult | 返回新的检测结果，不改变 g.cocos |
| g.select_cocos(source) | source: int | None | 只在 candidates 中选择来源 |
| g.to_cocos(to_cocos, from_cocos=None, inplace=True) | 目标、来源、是否原地 | GFile | 转换经典 G-file 字段 |
| g.copy() | 无 | GFile | 深复制字段、数组、路径和 AuxNamelist |
| g.save(path=None) | 可选输出路径 | None | 规范写回原路径或指定路径 |
| g.missing_fields() | 无 | list[str] | 列出缺失的必填字段 |

示例：

~~~python
g = eqmdsk.GFile("g067590.03300")
print(g["NW"], g["PSIRZ"].shape)
if g.cocos.selected is None and g.cocos.candidates:
    g.select_cocos(g.cocos.candidates[0])
if g.cocos.selected is not None:
    g.to_cocos(11)
else:
    g.to_cocos(11, from_cocos=5)
g.save("g.cocos11")
~~~

### AFile

| 成员 | 参数 | 返回值 | 用途 |
| --- | --- | --- | --- |
| AFile(path) | str 或 PathLike[str] | AFile | 立即读取完整 A-file |
| AFile.create() | 无 | AFile | 创建字段待填写的新对象 |
| a.header / a.footer | str 属性 | str | 编辑文件级前导/尾部文本 |
| a.copy() | 无 | AFile | 深复制字段、数组、header/footer |
| a.save(path=None) | 可选输出路径 | None | 规范写回 A-file |

~~~python
a = eqmdsk.AFile("a067590.03300")
print(a["SHOT"], a["BETAP"])
a["BETAP"] = 0.25
a.header = a.header.replace("01-Jan-00", "02-Feb-00")
a.save("a.edited")
~~~

### KFile、Namelist 和 NamelistBlock

| 成员 | 参数 | 返回值 | 用途 |
| --- | --- | --- | --- |
| KFile(path) | str 或 PathLike[str] | KFile | 立即读取完整 K-file |
| KFile.create() | 无 | KFile | 创建无路径的空 K-file |
| Namelist() | 无 | Namelist | 创建无路径、不能单独保存的内存 namelist |
| NamelistBlock(values=None) | 可选 mapping | NamelistBlock | 创建 C++ 持有的 block |
| n.keys()/b.keys() | 无 | list[str] | 返回 block/字段名称 |
| n.items()/b.items() | 无 | list[tuple] | 返回键值对快照 |
| n.update()/b.update() | mapping 或键值对 | None | 批量创建或替换 |
| del n[name] / del b[name] | 名称 | None | 删除 block 或字段 |
| k.copy()/n.copy()/b.copy() | 无 | 对应类型 | 深复制底层存储 |
| k.save(path=None) | 可选输出路径 | None | 写出 K-file |

~~~python
k = eqmdsk.KFile.create()
k["IN1"] = eqmdsk.NamelistBlock({"LIMITR": 60, "ITIME": 2})
k["IN1"]["LIMITR"] = 61
k["IN2"] = eqmdsk.NamelistBlock()
k["IN2"].update({"VALUE": 3.5})
del k["IN2"]["VALUE"]
k.save("created.k")
~~~

Namelist 标识符大小写不敏感，公开键统一为大写。NamelistBlock 不接受 None、嵌套
block 或二维数组；字段值必须是核心支持的标量、数值数组或字符串数组。

### SFile

| 成员 | 参数 | 返回值 | 用途 |
| --- | --- | --- | --- |
| SFile(path) | str 或 PathLike[str] | SFile | 读取标签和四列数据 |
| SFile.create(count) | count: int | SFile | 创建四个待填写数组 |
| s.copy() | 无 | SFile | 深复制标签和数组 |
| s.save(path=None) | 可选输出路径 | None | 写出标签和四列数据 |

~~~python
s = eqmdsk.SFile.create(2)
s["X"] = np.array([1.0, 2.0])
s["Y"] = np.array([3.0, 4.0])
s["DX"] = np.zeros(2)
s["DY"] = np.ones(2) * 0.1
s["TITLE"] = "created"
s.save("created.s")
~~~

X、Y、DX、DY 必须长度一致；长度、dtype 和有限值在 save() 时校验。

### 公共映射方法

| 调用 | 返回值 | 说明 |
| --- | --- | --- |
| obj[name] | 字段值或 block | 读取，未知名称抛出错误 |
| obj[name] = value | None | 创建或替换，类型由 C++ schema 校验 |
| name in obj | bool | 测试名称是否存在 |
| len(obj) | int | 字段或 block 数量 |
| obj.keys() | list[str] | 稳定顺序的公开名称 |
| obj.items()/obj.values() | list | 键值或值的快照 |
| obj.get(name, default) | 值或 default | 缺失时不抛错 |
| obj.update(other) | None | 批量赋值 |
| obj.setdefault(name, default) | 值 | 存在则返回旧值，否则插入 |
| obj.pop(name[, default]) | 被删除值 | 删除并返回 |
| del obj[name] | None | 删除字段或 block |
| obj.copy() | 同类型对象 | 深复制 C++ 存储 |
