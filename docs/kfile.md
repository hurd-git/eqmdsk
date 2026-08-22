# K-file 指南

K-file 是 Fortran namelist 文件。section 和变量由具体 EFIT 版本决定，因此 `IN1`
只是常见 section，不是强制名称。

```python
import eqmdsk

k = eqmdsk.KFile("k067590.03300")
print(k.keys())
print(k["IN1"].keys())
print(k["IN1"]["LIMITR"])
k["IN1"]["LIMITR"] = 61
k.write("k.modified")
```

## 两层映射

section 和变量名按 Fortran 规则大小写不敏感：`k["in1"]["limitr"]` 与大写写法
等价。每个 section 是可展开的字典，支持 `keys()`、`items()`、`values()`、`get()`、
迭代、读取和替换已有变量。

| namelist 值 | Python 类型 |
| --- | --- |
| 单个整数、实数、逻辑、字符串 | `int`、`float`、`bool`、`str` |
| 多个整数 | `numpy.ndarray[int64]` |
| 多个实数或混合数值 | `numpy.ndarray[float64]` |
| 多个字符串 | `list[str]` |

带索引变量、空值、复杂表达式、无法确定长度的语法不会加入公开映射，也不会被猜测
成普通字段。重复 section 或变量采用最后一个可投影赋值。不能通过映射创建新的未知
section/变量；这避免 `k["IN2"] = 1` 这类 Python `dict` 副作用进入 EFIT 写出。

## 写出

写出器生成稳定的 `&SECTION`、`NAME = VALUE`、`/` 结构。输入注释、section 外文本、
重复赋值历史、原始大小写和排版不保留。写出后重新读取时，公开 section、变量、值和
数组形状应保持一致。

G-file 的 `AuxNamelist` 使用同一个 namelist 投影器，但它是 G-file 尾部的附加容器，
不等同于需要单独写出的普通 K-file；应通过 `g["AuxNamelist"]` 修改，再调用 G-file
的 `write()`。
