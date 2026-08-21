# K-file guide

K-file 对外表现为两层字典。第一层键是 namelist 节名，第二层键是节内变量
名：

```python
import eqmdsk

k = eqmdsk.KFile("k067590.03300")
print(k.keys())
print(k["IN1"].keys())
print(k["IN1"]["LIMITR"])
k["IN1"]["LIMITR"] = 61
k.write("k.modified")
```

节名和变量名按照 Fortran 标识符大小写不敏感。每个节对象提供
`keys()`、`items()`、`values()`、`get()`、`len()`、迭代和索引读写，因此调试
器可以直接展开 `k["IN1"]` 查看全部公开变量。

## 投影规则

读取器会识别常见的 Fortran namelist 写法，并把最终有效赋值投影为简单值：

| 输入值 | 映射值 |
| --- | --- |
| 单个整数、实数、逻辑或字符串 | `int`、`float`、`bool`、`str` |
| 多个整数 | 可写 `numpy.ndarray[int64]` |
| 多个整数/实数 | 可写 `numpy.ndarray[float64]` |
| 多个字符串 | `list[str]` |

带索引的变量、空值、复杂值、原始语法和无法安全投影的逻辑向量不会加入
公开映射。它们不会伪装成普通字段，也不会在规范写出时重新生成。

重复节或重复变量采用最后一个可投影赋值。`keys()` 只返回公开映射中的节和
变量，因此它代表写出结果，而不是输入文件的语法树。

## 写出规则

KFile 写出所有公开节和变量，使用稳定的 `&SECTION`、`NAME = VALUE`、`/`
格式。输入文件中的注释、节外文本、重复赋值顺序、原始大小写和排版不保留。
写出后重新读取，公开映射的键和值应保持一致。

```python
before = {name: dict(section.items()) for name, section in k.items()}
k.write("k.output")
after = {name: dict(section.items()) for name, section in eqmdsk.KFile("k.output").items()}
assert before.keys() == after.keys()
```

不支持通过映射创建新的节或变量；访问不存在的键会抛出 `FieldError`。
