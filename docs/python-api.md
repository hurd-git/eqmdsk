# Python API

`eqmdsk` 是一个轻量的 EFIT G/A/K/S 文件读写库。Python 类直接调用共享的
C++17 实现，读取时一次性加载整个文件，写出时按照对应文件规范生成新的
标准文本。

## 打开和写出

构造函数接受文件名字符串，也接受实现 `os.PathLike[str]` 的对象。文件会在
构造时立即读取。`filename` 始终是字符串，不返回 `pathlib.Path`。

```python
import eqmdsk

g = eqmdsk.GFile("g123456.01234")
a = eqmdsk.AFile("a123456.01234")
k = eqmdsk.KFile("k123456.01234")
s = eqmdsk.SFile("s123456.01234")

g.write()                    # 写回原文件
g.write("copy.any-suffix")   # 写到指定路径
```

写出不会改变 `filename`。路径只用于读写和错误诊断，库不会根据后缀自动
判断文件类型或追加后缀。

## 映射行为

GFile、AFile、SFile 都像只包含标准字段的字典：

```python
print(g)                     # 显示全部字段和值
print(g.keys())
print(g.items())
current = g["CURRENT"]
g["CURRENT"] = 2.0
```

它们提供 `keys()`、`items()`、`values()`、`get()`、`len(file)`、`name in file`
和迭代。字段名使用规范中的大写形式。未知字段和错误字段访问会抛出
`eqmdsk.FieldError`，赋值只能替换已有字段，不能创建新的标准字段。

KFile 是嵌套映射，第一层是 namelist 节，第二层是节内变量：

```python
k = eqmdsk.KFile("k067590.03300")
print(k["IN1"])              # 显示 IN1 节的全部变量
limitr = k["IN1"]["LIMITR"]
k["IN1"]["LIMITR"] = 61
k.write("k.modified")
```

K-file 标识符按 Fortran 规则大小写不敏感；`k["in1"]["limitr"]` 与大写写法
等价。复杂的重复赋值、索引变量和不能安全投影为标准 Python 值的语法不会
进入公开映射，写出时只生成公开映射中的标准变量。

## 值类型

| C++ 字段 | Python 值 |
| --- | --- |
| logical | `bool` |
| integer | `int` |
| real | `float` |
| integer vector | 可写 `numpy.ndarray[int64]` |
| real vector/matrix | 可写 `numpy.ndarray[float64]` |
| string | `str` |
| string vector | `list[str]` |

数值数组是 C++ 存储的可写零拷贝 view。保持数组对象存活也会保持文件对象
存活。数组赋值必须保持原有形状；推荐直接修改 view：

```python
g["PSIRZ"][0, 0] = -0.25
s["Y"][0] = 42.0
```

## GFile COCOS

GFile 额外提供 `cocos`、`select_cocos()` 和 `to_cocos()`。当 `from_cocos` 为
`None` 时，库使用对象当前唯一或已选择的 COCOS；若来源不明确会抛出
`CocosError`。

```python
converted = g.to_cocos(11, from_cocos=5, inplace=False)
g.to_cocos(11)               # 对已 select_cocos() 的对象原地转换
```

## 规范写出

读取器会接受常见的空白、换行和 Fortran 指数变体。写出器不会保留原始空格、
注释、字段大小写、重复赋值历史、非标准尾部或二进制扩展区，而是从公开字段
重新生成规范文本。保证条件是：

```text
parse(input) -> write(output) -> parse(output)
```

得到的公开 keys、值、数组形状和类型语义一致。原始解析树、`RawSection`、
`header/footer`、KFile 的 `NamelistEntry` 等不属于最终 Python API。

## 异常

| 异常 | 含义 |
| --- | --- |
| `IOError` | 文件打开、读取、写出或关闭失败 |
| `ParseError` | 输入截断或语法无效；包含 `filename`、`line`、`column` |
| `ValidationError` | 修改后的标准字段无法按规范写出 |
| `FieldError` | 未知字段、节或变量 |
| `CocosError` | COCOS 来源缺失、歧义或不支持 |

所有异常都继承 `eqmdsk.Error`。
