# G-file 指南

## 文件简介

G-file 是 EFIT/GEQDSK 的平衡文件，包含网格尺寸、平衡标量、剖面、二维磁通矩阵、
边界和限制器坐标。文件可能在经典主体后追加 EFIT 数值扩展和 `AuxNamelist`。
eqmdsk 只负责完整读取、规范写回和字段语义保持，不计算平衡面性质。

## 读取结果

构造 `GFile` 会立即读取整个文件。`PSIRZ` 暴露为形状 `(NH, NW)` 的可写 NumPy
数组；`NW`、`NH`、计数和标量是 Python 标量；字符串字段是 `str`。无法安全拆分的
扩展数值放入 `UNPARSED_EXTENSION`，解析器内部的 header、raw 数据和布局对象不会
暴露。

文件路径通过只读的 `filename`、`path`、`abspath` 暴露，规则见 [Python API](python-api.md)。

经典主体由 `NW`、`NH`、`NBBBS`、`LIMITR` 决定长度：20 个标量、四组长度为 `NW`
的 profile、`NH*NW` 个 `PSIRZ`、长度为 `NW` 的 `QPSI`，再出现边界和限制器坐标。
读取器兼容固定宽度或空白分隔 header、CRLF、Fortran `D` 指数及常见省略指数写法。

## 字段表

`NW`、`NH` 由文件头提供，创建时由 `GFile.create(nw, nh)` 提供。其余必填字段必须
在保存前填写；边界数组长度由 `NBBBS` 和 `LIMITR` 决定。

| 类别 | 字段 |
| --- | --- |
| 必填头和标量 | `CASE`, `NW`, `NH`, `RDIM`, `ZDIM`, `RCENTR`, `RLEFT`, `ZMID`, `RMAXIS`, `ZMAXIS`, `SIMAG`, `SIBRY`, `BCENTR`, `CURRENT` |
| 必填剖面和网格 | `FPOL`, `PRES`, `FFPRIM`, `PPRIME`, `PSIRZ`, `QPSI` |
| 必填边界 | `NBBBS`, `LIMITR`, `RBBBS`, `ZBBBS`, `RLIM`, `ZLIM` |
| 可选 EFIT 扩展 | `KVTOR`, `RVTOR`, `NMASS`, `PRESSW`, `PWPRIM`, `DMION`, `RHOVN`, `KEECUR`, `EPOTEN` |
| 可选 IPLCOUT 扩展 | `IPLCOUT`, `IPLCOUT_NW`, `IPLCOUT_NH`, `IPLCOUT_ISHOT`, `IPLCOUT_ITIME`, `RGRID`, `ZGRID`, `IPLCOUT_PREFIX`, `PCURRT`, `PCURRZ`, `CJOR`, `R1SURF`, `R2SURF`, `VOLP`, `BPOLSS` |
| 可选兼容保留值 | `UNPARSED_EXTENSION` |
| 非法 | 不在以上列表中的 G-file 顶层字段，以及内部 header、raw、计数和布局对象 |

`AuxNamelist` 是 G-file 持有的一个实际 `Namelist` 实例，不是 G-file 数值字段，也
不是额外的包装类。它包含动态的 `NamelistBlock`，其 block 名不限制为 `OUT1`、`BASIS`、
`CHIOUT`。Python 侧 `type(g["AuxNamelist"]) is eqmdsk.Namelist`；C++ 侧
`g.aux_namelist()` 返回 `eqmdsk::Namelist*`。

G-file 没有独立的可编辑 raw footer；第一行头部的语义由 `CASE`、`NW`、`NH` 和内部
标准控制值组成，尾部附加内容通过 `AuxNamelist` 映射编辑。

## 调用和保存

```python
import eqmdsk

g = eqmdsk.GFile("g067590.03300")
print(g["NW"], g["PSIRZ"].shape)
g["CURRENT"] = 2.0
g["AuxNamelist"]["OUT1"]["ISHOT"] = 67591
g.save("g.modified")
```

`g.copy()` 返回独立的 G-file 副本，包括字段数组和 `AuxNamelist`；修改副本不会影响
原对象。

标准字段和已知扩展字段直接通过映射赋值。删除经典必填字段时，它仍保留在 Python
映射中并恢复为 `None`，同时进入 `missing_fields()`；删除可选扩展字段才会从底层
字段集合移除。未知字段会抛出 `FieldError`。

数组整体赋值只替换底层数组，长度和形状统一在 `save()` 时校验。`RBBBS`、`ZBBBS`、
`RLIM`、`ZLIM` 的长度分别必须与 `NBBBS`、`LIMITR` 相等；`PSIRZ`、`PCURRT` 必须
与 `(NH, NW)` 网格一致。`RGRID`、`ZGRID` 不能直接赋值，而是由 `RLEFT`、`RDIM`、
`ZMID`、`ZDIM` 自动计算。

扩展字段只保证读写语义，不自动参与 COCOS 转换。COCOS 使用方式如下：

```python
converted = g.to_cocos(11, from_cocos=5, inplace=False)
g.select_cocos(5)
g.select_cocos(6)
g.to_cocos(11, from_cocos=6)
```

`g.cocos` 是一个只读的 `CocosResult` 对象，调试器可以直接展开查看
`candidates`、`selected`、`diagnostic` 以及 `is_unique()` 等状态。文件读取完成后，
eqmdsk 会根据文件中的符号信息检测可能的来源约定；当信息不足时可能得到多个候选值。
内部 `g._detect_cocos()` 会返回当前字段对应的全新检测结果，不改变 `g.cocos`。检测
结果的组装规则是：候选为空或多于一个时 `selected` 为 `None`，候选恰好一个时
`selected` 自动取该唯一候选。
`is_unique()`、`is_ambiguous()` 和 `has_match()` 只依据 candidates 数组判断；即使
在多个候选中显式选择了一个，候选集合仍然属于 ambiguous 状态。

`g.select_cocos(source)` 只在 `source` 位于当前 candidates 时成功，不重新检测，也不
执行转换。调用成功后只更新 selected：

- `g.cocos.candidates` 保持不变；
- `g.cocos.selected` 变为 `source`；
- `g.cocos.diagnostic` 保持不变；
- `CURRENT`、`BCENTR`、`PSIRZ` 等文件字段保持不变。

可以在多个候选之间反复调用 `select_cocos()`；如果选择值与当前 selected 相同，则
完全不做任何操作。不在当前 candidates 中的值会抛出 `CocosError`。

如果 `source` 不在自动检测出的候选集合中，`select_cocos()` 会抛出 `CocosError`，并
保留原检测结果供诊断。实际转换使用：

```python
g.to_cocos(to_cocos=11, from_cocos=5, inplace=True)
```

省略 `from_cocos` 时使用当前 `g.cocos.selected`；如果 selected 为空则报错。显式传入
的 `from_cocos` 可以不在 candidates 中，这是自动识别不可靠时的强制转换入口，但必须
是支持的 COCOS 编号。转换完成后，程序先对转换后的字段重新检测 candidates 和
diagnostic，再把 selected 直接设为 `to_cocos`；selected 不要求属于新的 candidates。
COCOS 选择信息不写入 G-file 文件内容，重新读取文件时会重新检测。

`AuxNamelist` 不能单独保存，必须修改后调用所属 G-file 的 `save()`。这是保存归属的
限制，不改变它本身是 `Namelist` 实例这一事实。无论原始文件尾部是否有 namelist，
读取后的 G-file 都固定包含一个 `AuxNamelist`；没有尾部内容时它为空。
`GFile.create()` 也会自动包含一个空的 `AuxNamelist`，可以直接向其中添加 block。

## 从零创建和注意事项

普通构造器只表示读取已有文件；创建空对象使用显式工厂：

```python
import numpy as np

g = eqmdsk.GFile.create(3, 2)
g["CASE"] = "minimal"
for name in ("RDIM", "ZDIM", "RCENTR", "RLEFT", "ZMID", "RMAXIS", "ZMAXIS",
             "SIMAG", "SIBRY", "BCENTR", "CURRENT"):
    g[name] = 1.0
g["FPOL"] = np.zeros(3)
g["PRES"] = np.zeros(3)
g["FFPRIM"] = np.zeros(3)
g["PPRIME"] = np.zeros(3)
g["PSIRZ"] = np.zeros((2, 3))
g["QPSI"] = np.zeros(3)
g["NBBBS"] = 0
g["LIMITR"] = 0
g["RBBBS"] = np.empty(0)
g["ZBBBS"] = np.empty(0)
g["RLIM"] = np.empty(0)
g["ZLIM"] = np.empty(0)
g.save("created.g")
```

创建对象的标准字段初始值在 Python 侧显示为 `None`，`NW`、`NH` 除外；保存前可用
`missing_fields()` 和 `missing_optional_fields()` 检查状态。writer 会校验字段类型、
数组形状、计数和有限值，并规范生成固定宽度数值记录，不保证原文排版保真。

G-file 的整数控制字段（如 `NW`、`NH`、`NBBBS`、`LIMITR`）不接受浮点赋值；实数标量
可以接受 Python `int` 并自动转成 `float`。实数数组通常使用 `numpy.float64`，整数数组
可以转换为实数数组，其他 dtype 需要显式转换。

## 字段含义

| 字段 | 含义 |
| --- | --- |
| `CASE` | 平衡或放电的标识文本 |
| `NW` | R 方向网格点数 |
| `NH` | Z 方向网格点数 |
| `RDIM` | 计算区域在 R 方向的尺寸，单位 m |
| `ZDIM` | 计算区域在 Z 方向的尺寸，单位 m |
| `RCENTR` | 真空环向磁场 `BCENTR` 对应的 R 坐标，单位 m |
| `RLEFT` | 矩形计算区域的最小 R 坐标，单位 m |
| `ZMID` | 计算区域中心的 Z 坐标，单位 m |
| `RMAXIS` | 磁轴 R 坐标，单位 m |
| `ZMAXIS` | 磁轴 Z 坐标，单位 m |
| `SIMAG` | 磁轴处的极向磁通，单位 Wb/rad |
| `SIBRY` | 等离子体边界处的极向磁通，单位 Wb/rad |
| `BCENTR` | `RCENTR` 处的真空环向磁场，单位 T |
| `CURRENT` | 等离子体电流，单位 A |
| `FPOL` | 极向电流函数，在磁通网格上的 `F = R·B_t`，单位 m·T |
| `PRES` | 等离子体压力，在均匀磁通网格上给出，单位 Pa |
| `FFPRIM` | `F F'(ψ)`，其中 `F = R·B_t`，在均匀磁通网格上给出，单位 `(m·T)^2/(Wb/rad)` |
| `PPRIME` | `P'(ψ)`，在均匀磁通网格上给出，单位 `Pa/(Wb/rad)` |
| `PSIRZ` | 矩形 R-Z 网格点上的极向磁通，单位 Wb/rad |
| `QPSI` | 从磁轴到边界的均匀磁通网格上的 q 值 |
| `NBBBS` | 等离子体边界点数 |
| `LIMITR` | 限制器轮廓点数 |
| `RBBBS`, `ZBBBS` | 等离子体边界点的 R/Z 坐标，单位 m |
| `RLIM`, `ZLIM` | 周围限制器轮廓点的 R/Z 坐标，单位 m |
| `KVTOR` | 环向旋转开关 |
| `RVTOR` | 环向旋转特征大半径，单位 m |
| `NMASS` | 质量密度开关 |
| `PRESSW`, `PWPRIM` | 旋转/扩展压力及其导数 |
| `DMION` | 离子质量密度相关剖面 |
| `RHOVN` | 均匀极向磁通网格上的归一化环向磁通 |
| `KEECUR` | `EPOTEN` 扩展的控制开关，具体语义依 EFIT 版本而定 |
| `EPOTEN` | 与 `KEECUR` 配套的扩展剖面数据，具体物理含义依 EFIT 版本而定 |
| `IPLCOUT` | IPLCOUT 输出模式 |
| `IPLCOUT_NW`, `IPLCOUT_NH` | IPLCOUT 数据网格尺寸 |
| `IPLCOUT_ISHOT`, `IPLCOUT_ITIME` | IPLCOUT 对应的放电号和时间索引 |
| `RGRID`, `ZGRID` | IPLCOUT 网格坐标 |
| `IPLCOUT_PREFIX` | 无法进一步安全命名的 IPLCOUT 前缀数据 |
| `PCURRT` | IPLCOUT 的 R-Z 网格环向电流密度数据 |
| `PCURRZ` | IPLCOUT 模式扩展数据，具体含义依 EFIT 版本而定 |
| `CJOR`, `R1SURF`, `R2SURF`, `VOLP`, `BPOLSS` | IPLCOUT 模式扩展量 |
| `UNPARSED_EXTENSION` | 无法安全判断标准含义的数值扩展 |
| `AuxNamelist` | G-file 尾部附加 namelist 的容器 |
