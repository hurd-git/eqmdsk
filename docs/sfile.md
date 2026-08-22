# S-file 指南

## 文件简介

S-file 是简洁的四列数据文件，可选包含 `XLABEL`、`YLABEL`、`TITLE` 三个前导文本
标签。它用于保存等长的观测或诊断数据，不包含 G-file 的网格和物理扩展。

## 读取结果

构造 `SFile` 会完整读取文件。数据主体公开为等长的 `X`、`Y`、`DX`、`DY` NumPy
数组，标签以 Python `str` 返回。空文件和只有标签的文件也可以读取；读取器会跳过
常见的非标准穿插文本，内部 raw 数据和解析状态不会暴露。

文件路径通过只读的 `filename`、`path`、`abspath` 暴露，规则见 [Python API](python-api.md)。

## 字段表

| 类别 | 字段 |
| --- | --- |
| 必填数据 | `X`, `Y`, `DX`, `DY` |
| 可选文本 | `XLABEL`, `YLABEL`, `TITLE` |
| 非法 | 其它顶层字段、raw 行对象和内部解析状态 |

四个数据数组必须长度一致，元素必须是有限实数。除此之外的字段名会被
`FieldError` 拒绝。

S-file 没有独立的 raw footer；文件开头的三个可选文本行已经分别映射为
`XLABEL`、`YLABEL`、`TITLE`，因此直接编辑这些字段即可。

## 调用和保存

```python
import eqmdsk

s = eqmdsk.SFile("s123456.01234")
print(s.keys())
s["Y"][0] = 42.0
s["TITLE"] = "modified"
s.save("s.modified")
```

`s.copy()` 返回独立的 S-file 副本；修改副本的数据数组不会影响原对象。

数组 view 由 C++ 核心所有，修改元素会直接修改对象；整体赋值会替换底层数组，四个
数据数组的长度一致性在 `save()` 时校验。
规范写出只包含标签和四列数据，不复制穿插文本、raw 数据或原始行排版。

## 从零创建和注意事项

```python
import numpy as np
import eqmdsk

s = eqmdsk.SFile.create(2)
s["X"] = np.array([1.0, 2.0])
s["Y"] = np.array([3.0, 4.0])
s["DX"] = np.array([0.1, 0.1])
s["DY"] = np.array([0.2, 0.2])
s["TITLE"] = "created"
s.save("s.new")
```

`SFile.create(count)` 创建四个长度为 `count` 的必填数组，初始未填写
字段在 Python 侧显示为 `None`。保存前必须完成四个数组；可选标签可以省略。普通
构造器只表示读取已有路径，保存时 writer 会检查长度、类型和有限值。

`X`、`Y`、`DX`、`DY` 是实数数组，标准 dtype 为 `numpy.float64`；整数数组可以转换为
实数数组，`float32` 或其它不匹配 dtype 会在赋值时拒绝。

## 字段含义

| 字段 | 含义 |
| --- | --- |
| `X` | 第一列数据，通常表示横坐标或 R 坐标 |
| `Y` | 第二列数据，通常表示纵坐标或测量值 |
| `DX` | `X` 的误差、宽度或不确定度 |
| `DY` | `Y` 的误差、宽度或不确定度 |
| `XLABEL` | X 轴标签文本 |
| `YLABEL` | Y 轴标签文本 |
| `TITLE` | 文件或数据集标题 |
