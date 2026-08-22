# Python API

`eqmdsk` 的 Python 类由共享的 C++17 核心实现。构造对象时立即读取整个文件，写出
时按照对应标准生成新文件。

## 打开和写出

构造函数和 `write()` 接受字符串或 `os.PathLike[str]`。`filename` 始终是 Python
`str`，不会返回 `Path` 或 `bytes`。

```python
g = eqmdsk.GFile("g123456.01234")
a = eqmdsk.AFile("a123456.01234")
k = eqmdsk.KFile("k123456.01234")
s = eqmdsk.SFile("s123456.01234")

g.write()
g.write("copy.any-suffix")
```

## 映射行为

G/A/S 对象是可展开的字典式对象，`print(g)`、`keys()`、`items()`、`values()`、
`get()`、`len(g)`、`name in g` 和迭代都只显示公开字段。已有字段可以替换，不能
删除或随意创建新字段。

K-file 是嵌套映射：

```python
print(k["IN1"])
k["IN1"]["LIMITR"] = 61
```

G-file 的附加 namelist 使用：

```python
out1 = g["AuxNamelist"]["OUT1"]
print(out1.keys())
out1["ISHOT"] = 67591
g.write("g.new")
```

没有尾部 namelist 时不会伪造 `AuxNamelist`，也不能直接给它赋新对象。

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

数值数组是 C++ 存储的可写 view。直接修改 view 不改变形状；整体赋值必须保持原有
长度或 `(NH, NW)` 形状。`PCURRT`、`PSIRZ` 等二维字段不会被扁平化。

## G-file COCOS

```python
converted = g.to_cocos(11, from_cocos=5, inplace=False)
g.select_cocos(5)
g.to_cocos(11)
```

省略 `from_cocos` 时使用对象已选择的唯一来源；来源歧义或未知会抛出 `CocosError`。
COCOS 转换只作用于经典 G-file 字段，不对扩展和 AuxNamelist 做未经确认的物理换算。

## 规范写出和异常

写出遵循 `parse -> write -> parse` 语义等价，不承诺原始字节保真。注释、空格、换行、
数字格式、重复赋值和非标准穿插文本可能被规范化。

- `IOError`：打开、读取、写出或关闭失败；
- `ParseError`：输入截断或语法无效，带 `filename`、`line`、`column`；
- `ValidationError`：字段不满足标准尺寸、计数或数值要求；
- `FieldError`：未知字段、section 或变量；
- `CocosError`：COCOS 来源缺失、歧义或不支持。

所有异常都继承 `eqmdsk.Error`。
