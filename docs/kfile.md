# K-file 指南

## 文件简介

K-file 是 Fortran namelist 文件。它由任意数量的 `NamelistBlock` 组成，block 和变量
由具体 EFIT 版本决定，因此 `IN1`、`IN2` 只是常见名称，不是固定 schema。

## 读取结果

构造 `KFile` 会完整读取文件，返回一个嵌套映射：外层键是 block 名，内层值是
`NamelistBlock`。block 和变量名遵循 Fortran 规则大小写不敏感，访问结果统一使用
大写键。支持的值类型如下：

K-file 没有独立的文件级 header/footer；文件中的 namelist block 就是公开结构。
文件路径同样通过只读的 `filename`、`path`、`abspath` 暴露。

| namelist 值 | Python 类型 |
| --- | --- |
| 单个整数、实数、逻辑、字符串 | `int`、`float`、`bool`、`str` |
| 多个整数 | `numpy.ndarray[int64]` |
| 多个实数或混合数值 | `numpy.ndarray[float64]` |
| 多个字符串 | `list[str]` |

带索引变量、空值、复杂表达式和无法确定长度的语法不会加入公开映射，也不会被猜测
成普通字段。重复 block 或变量采用最后一个可投影赋值。

## 字段和 block 规则

K-file 没有固定的必填字段表，也没有 block 白名单。block 名由输入文件或调用者决定，
创建新 block 使用 `NamelistBlock` 赋值：

```python
k["IN2"] = eqmdsk.NamelistBlock()
k["IN2"]["ITIME"] = 2
```

直接读取不存在的 block，例如 `k["IN2"]["ITIME"] = 2`，不会隐式创建，异常为：
`FieldError: unknown namelist block: IN2`。先赋一个 `NamelistBlock` 即可创建。

`NamelistBlock` 比 G/A/S 的固定字段映射宽松：

| 项目 | 规则 |
| --- | --- |
| 字段名 | 不与标准字段表比较，允许不同 EFIT 版本的变量和扩展变量；名称不能为空 |
| 大小写 | 访问和写出统一为大写，`block["itime"]` 等价于 `block["ITIME"]` |
| 标量值 | 支持整数、实数、逻辑值和字符串 |
| 数组值 | 支持一维整数/实数数组以及字符串数组；二维数组不属于 Namelist 值 |
| 缺失值 | 不支持 `None`；删除字段使用 `del block["NAME"]` |
| 嵌套值 | 字段值不能再是字典或另一个 `NamelistBlock` |
| 非法值 | 不支持的对象、三维以上数组、空字段名会被拒绝 |

## 调用和保存

```python
import eqmdsk

k = eqmdsk.KFile("k067590.03300")
print(k.keys())
print(k["IN1"]["LIMITR"])
k["IN1"]["LIMITR"] = 61
k["IN2"] = eqmdsk.NamelistBlock({"VALUE": 3.5})
del k["IN2"]["VALUE"]
k.save("k.modified")
```

`k.copy()` 返回独立的 K-file 副本；其中的 block、字段和数组不会与原对象共享。

字段和 block 的增删、替换、`update()` 都直接转发到 C++ 核心。Python facade 不维护
另一份 namelist 存储。写出器生成稳定的 `&BLOCK`、`NAME = VALUE`、`/` 结构，不保留
注释、block 外文本、重复赋值历史、原始大小写和排版。

G-file 的 `AuxNamelist` 本身就是一个 `Namelist` 实例，使用与独立 `Namelist` 相同的
block 和字段接口；它只因依附于 G-file 而不能单独保存，必须随所属 G-file 写出。它
不是 `KFile`，也不是包装通用核心的额外对象。

## 从零创建和注意事项

```python
k = eqmdsk.KFile.create()
k["IN1"] = eqmdsk.NamelistBlock()
k["IN1"].update({"LIMITR": 60, "ITIME": 2})
k["IN2"] = eqmdsk.NamelistBlock({"VALUE": 3.5})
k.save("k.new")
```

独立的 `Namelist` 和 `NamelistBlock` 也可以调用 `.copy()`，得到不共享底层字段存储的
副本。

`KFile.create()` 创建空的 K-file，不要求任何预定义 block。普通构造器只读取已有路径；
不存在的 block 不会通过索引自动创建。`NamelistBlock` 的字段没有标准白名单，但仍须
使用 C++ 核心支持的标量、数组或字符串数组类型，并且字段名不能为空。

## 字段含义

K-file 由不同 EFIT 版本和不同运行配置生成，没有一张跨版本可靠的固定字段含义表。
下面只列出常见名称；未列出的动态字段含义留空，不代表字段非法。

| 名称 | 常见含义 |
| --- | --- |
| `IN1` | 常见的 EFIT 输入 namelist block |
| `IN2` | 版本相关的第二输入 block |
| `OUT1` | 常见的 EFIT 输出附加 block |
| `BASIS` | 常见的基准/配置附加 block |
| `CHIOUT` | 常见的诊断输出附加 block |
| `LIMITR` | 限制器点数或限制器相关控制量 |
| `ITIME` | 时间索引或时间步相关控制量 |
| `ISHOT` | 放电号 |
| `KFFCUR` | EFIT 电流拟合/计算控制量 |
| `FITDELZ` | 拟合网格或垂直步长相关控制量 |
| `XLIM`, `YLIM` | 限制器或边界坐标数组 |
| 其它 block 名 | 含义由具体 EFIT 版本决定 |
| 其它字段名 | 含义由所属 block 和具体 EFIT 配置决定 |
