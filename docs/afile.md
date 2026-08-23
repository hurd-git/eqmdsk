# A-file 指南

## 文件简介

A-file 是 EFIT 输出的平衡摘要文件，使用 Fortran 固定记录保存控制量、摘要量、诊断
量和计数驱动的数组。它适合交换分析结果，不是 G-file 的替代品。

## 读取结果

构造 `AFile` 会完整读取文件。整数、实数、逻辑和字符串以 Python 标量返回；记录数组
以 NumPy view 返回，字符串字段是 `str`。文件控制记录之前的文本可通过 `header`
编辑，已识别可选记录之后的生产者自定义尾部可通过 `footer` 编辑；两者都是 Python
`str`，并由 C++ 核心持有。读取器兼容常见 EFIT 控制记录和可选记录，对截断、数组
长度和有限值进行校验。

文件路径通过只读的 `filename`、`path`、`abspath` 暴露，规则见 [Python API](python-api.md)。

## 字段表

`MCO2V`、`MCO2R`、`NSILOP0`、`MAGPRI0`、`NFCOIL0`、`NESUM0` 决定后续弦线、响应、
磁探针、线圈和等离子体电流数组的长度。修改计数时必须同步修改对应数组。

| 类别 | 字段 |
| --- | --- |
| 必填控制字段 | `SHOT`, `TIME`, `JFLAG`, `LFLAG`, `LIMLOC`, `MCO2V`, `MCO2R`, `QMFLAG`, `NLOLD`, `NLNEW` |
| 必填摘要字段 | `CHISQ`, `RCENCM`, `BCENTR`, `IPMEAS`, `IPMHD`, `RCNTR`, `ZCNTR`, `AMINOR`, `ELONG`, `UTRI`, `LTRI`, `VOLUME`, `RCURRT`, `ZCURRT`, `QSTAR`, `BETAT`, `BETAP`, `LI`, `GAPIN`, `GAPOUT`, `GAPTOP`, `GAPBOT`, `Q95`, `VERTN`, `SHEAR`, `BPOLAV` |
| 必填剖面和诊断 | `S1`, `S2`, `S3`, `QOUT`, `SEPIN`, `SEPOUT`, `SEPTOP`, `SIBDRY`, `AREA`, `WMHD`, `ERROR`, `ELONGM`, `QM`, `CDFLUX`, `ALPHA`, `RTTT`, `PSIREF`, `INDENT`, `RSEPS`, `ZSEPS`, `SEPEXP`, `SEPBOT`, `BTAXP`, `BTAXV`, `AQ1`, `AQ2`, `AQ3`, `DSEP`, `RM`, `ZM`, `PSIM`, `TAUMHD`, `BETAPD`, `BETATD`, `WDIA`, `DIAMAG`, `VLOOP`, `TAUDIA`, `QMERCI`, `TAVEM` |
| 必填计数和数组 | `NSILOP0`, `MAGPRI0`, `NFCOIL0`, `NESUM0`, `RCO2V`, `DCO2V`, `RCO2R`, `DCO2R`, `CSILOP`, `CMPR2`, `CCBRSP`, `ECCURT` |
| 可选记录和诊断 | `PBINJ`, `RVSIN`, `ZVSIN`, `RVSOUT`, `ZVSOUT`, `VSURF`, `WPDOT`, `WBDOT`, `SLANTU`, `SLANTL`, `ZUPERTS`, `CHIPRE`, `CJOR95`, `PP95`, `DRSEP`, `YYY2`, `XNNC`, `CPROF`, `ORING`, `CJOR0`, `FEXPAN`, `QMIN`, `CHIMSE`, `SSI01`, `FEXPVS`, `SEPNOSE`, `SSI95`, `RHOQMIN`, `CJOR99`, `CJ1AVE`, `RMIDIN`, `RMIDOUT`, `PSURFA`, `PEAK`, `DMINUX`, `DMINLX`, `DOLUBAF`, `DOLUBAFM`, `DILUDOM`, `DILUDOMM`, `RATSOL`, `RVSIU`, `ZVSIU`, `RVSID`, `ZVSID`, `RVSOU`, `ZVSOU`, `RVSOD`, `ZVSOD`, `CONDNO`, `PSIN32`, `PSIN21`, `RQ32IN`, `RQ21TOP`, `CHILIBT`, `LI3`, `XBETAPR`, `TFLUX`, `TCHIMLS`, `TWAGAP` |
| 文件级文本 | `header`, `footer`；不属于字段映射，但可读写并随文件保存 |
| 非法 | 不在以上列表中的 A-file 顶层字段，以及内部记录、raw 数据和格式辅助对象 |

`header` 和 `footer` 是文件级文本，不属于字段映射，因此不出现在 `keys()` 中。

可选字段可以缺失、修改或删除。删除必填字段时，字段恢复为 `None` 并重新出现在
`missing_fields()`。

## 调用和保存

```python
import eqmdsk

a = eqmdsk.AFile("a067590.03300")
print(a["SHOT"], a["BETAP"], a["RCO2V"].shape)
a["BETAP"] = 0.25
a.save("a.modified")
```

`a.copy()` 返回独立的 A-file 副本，包括字段数组以及 `header`、`footer` 文本。

写出器根据当前公开字段重建标准 A-file，并将当前 `header`、`footer` 原样放回对应
位置；不保证字段记录的原始空格、注释、可选记录顺序或字节级保真。修改计数字段后
若数组长度不匹配，`save()` 会抛出 `ValidationError`。

A-file 的整数控制字段和实数记录字段严格区分；实数标量允许 Python `int` 自动转为
`float`，实数数组建议使用 `numpy.float64`。整数数组可转换为实数数组，其他不匹配的
数组 dtype 会在赋值时拒绝。

```python
a.header = a.header.replace("01-Jan-00", "02-Feb-00")
a.footer = "producer-specific text\n"
a.save("a.edited")
```

## 从零创建和注意事项

```python
source = eqmdsk.AFile("a067590.03300")
created = eqmdsk.AFile.create()
for name in created.missing_fields():
    created[name] = source[name]
for name in created.missing_optional_fields():
    if name in source:
        created[name] = source[name]
created.save("a.copy")
```

`AFile.create()` 的标准字段在 Python 侧初始为 `None`。必填记录应连续、完整地填写；
可选四元记录只能从第一组开始连续提供，不能只填写中间一组。普通构造器只读取已有
路径，不能用不存在的路径隐式创建文件。编辑 `header` 时应保留标准的多行结构和
第二行 I7 `SHOT` 位置；编辑 `footer` 不影响字段校验，但内容会原样追加到规范记录后。

## 字段含义

| 字段 | 含义 |
| --- | --- |
| `SHOT` | 放电号 |
| `TIME` | 平衡对应时间，单位 ms |
| `JFLAG` | 错误标志；OMFIT 约定 `JFLAG=0` 表示错误 |
| `LFLAG` | 错误标志；OMFIT 约定 `LFLAG>0` 表示错误 |
| `LIMLOC` | 等离子体构型代码：`IN`、`OUT`、`TOP`、`BOT` 表示限制器构型，`SNT`、`SNB`、`DN`、`MAR` 表示不同偏滤器构型 |
| `MCO2V` | 垂直 CO2 密度弦线数量 |
| `MCO2R` | 径向 CO2 密度弦线数量 |
| `QMFLAG` | 轴上 `q(0)` 标志；`FIX` 表示约束，`CLC` 表示浮动 |
| `NLOLD`, `NLNEW` | 之前版本和当前版本的 `WRITE` 语句数量 |
| `CHISQ` | 来自磁探针、磁通环、Rogowski 线圈和外部线圈的总拟合 χ² |
| `RCENCM` | 真空场 `BCENTR` 对应的大半径，单位 cm |
| `RCNTR` | 几何中心的大半径，单位 cm |
| `ZCNTR` | 几何中心的 Z 坐标，单位 cm |
| `BCENTR` | `RCENCM` 处的真空环向磁场，单位 T |
| `IPMEAS` | 测得的等离子体环向电流，单位 A |
| `IPMHD` | 拟合得到的等离子体环向电流，单位 A·turn |
| `AMINOR` | 等离子体小半径，单位 cm |
| `ELONG` | 等离子体边界拉长率 |
| `ELONGM` | 磁轴处拉长率 |
| `UTRI` | 上三角度 |
| `LTRI` | 上三角度（OMFIT 的现有描述如此记录；具体上下定义依 EFIT 版本） |
| `VOLUME` | 等离子体体积，单位 m³ |
| `RCURRT` | 电流形心的大半径，单位 cm |
| `ZCURRT` | 电流形心的 Z 坐标，单位 cm |
| `QSTAR` | 等效安全因子 `q*` |
| `Q95` | 归一化极向磁通 95% 处的 q 值 |
| `BETAT` | 环向 beta，单位 % |
| `BETAP` | 以依据安培定律定义的平均极向磁场 `BPOLAV` 归一化的极向 beta |
| `BETAPD` | 抗磁极向 beta |
| `LI` | 使用依据安培定律定义的平均极向磁场归一化的内部电感 |
| `GAPIN` | 等离子体内侧间隙，单位 cm |
| `GAPOUT` | 等离子体外侧间隙，单位 cm |
| `GAPTOP` | 等离子体顶部间隙，单位 cm |
| `GAPBOT` | 等离子体底部间隙，单位 cm |
| `VERTN` | 当前电流中心处的真空场指数 |
| `SHEAR` | 包含归一化极向磁通 95% 的位置处的磁剪切 |
| `BPOLAV` | 依据安培定律定义的平均极向磁场，单位 T |
| `S1`, `S2`, `S3` | Shafranov 边界线积分 |
| `QOUT` | 等离子体边界处的 q 值 |
| `SEPIN` | 外部第二分离面的内侧间隙，单位 cm |
| `SEPOUT` | 外部第二分离面的外侧间隙，单位 cm |
| `SEPTOP` | 外部第二分离面的顶部间隙，单位 cm |
| `SIBDRY` | 等离子体边界处的极向磁通，单位 Wb/rad |
| `AREA` | 等离子体截面积，单位 cm² |
| `WMHD` | 等离子体储能，单位 J |
| `ERROR` | 平衡收敛误差 |
| `QM` | 轴上安全因子 `q(0)` |
| `CDFLUX` | 计算得到的抗磁磁通，单位 V·s |
| `ALPHA` | Shafranov 边界线积分参数 |
| `RTTT` | Shafranov 边界线积分参数 |
| `PSIREF` | 参考极向磁通，单位 V·s/rad |
| `INDENT` | 等离子体边界凹陷量 |
| `DSEP` | 偏滤器构型下为正的最小间隙；限制器构型下为负，绝对值表示到外部分离面的最小距离，单位 cm |
| `SEPEXP` | 分离面径向扩展，单位 cm |
| `SEPBOT` | 外部第二分离面的底部间隙，单位 cm |
| `BTAXP` | 磁轴处的环向磁场，单位 T |
| `BTAXV` | 磁轴处的真空环向磁场，单位 T |
| `AQ1` | `q=1` 磁面的等效小半径，单位 cm；未找到时为 100 |
| `AQ2` | `q=2` 磁面的等效小半径，单位 cm；未找到时为 100 |
| `AQ3` | `q=3` 磁面的等效小半径，单位 cm；未找到时为 100 |
| `RM` | 磁轴处的大半径，单位 cm |
| `ZM` | 磁轴处的 Z 坐标，单位 cm |
| `PSIM` | 磁轴处相关的边界极向磁通，单位 Wb/rad |
| `TAUMHD` | 能量约束时间，单位 ms |
| `BETATD` | 抗磁环向 beta，单位 % |
| `WDIA` | 抗磁等离子体储能，单位 J |
| `DIAMAG` | 抗磁量；OMFIT 未提供进一步描述 |
| `VLOOP` | 测得的回路电压，单位 V |
| `TAUDIA` | 抗磁能量约束时间，单位 ms |
| `QMERCI` | Mercier 稳定性判据；当 `q(0) > QMERCI` 时表示稳定 |
| `TAVEM` | 磁测量和 MSE 数据的平均时间，单位 ms |
| `NSILOP0`, `MAGPRI0`, `NFCOIL0`, `NESUM0` | 后续诊断数组的数量控制字段 |
| `RCO2V` | 垂直 CO2 密度弦线的路径长度，单位 cm |
| `DCO2V` | 垂直 CO2 弦线的线平均电子密度 |
| `RCO2R` | 径向 CO2 密度弦线的路径长度，单位 cm |
| `DCO2R` | 径向 CO2 弦线的线平均电子密度 |
| `CSILOP` | 计算得到的磁通环信号，单位 Wb |
| `CMPR2` | OMFIT 未提供描述的磁探针相关数组 |
| `CCBRSP` | 计算得到的外部线圈电流，单位 A |
| `ECCURT` | 测得的 E 线圈电流，单位 A |
| `PBINJ` | 中性束注入功率，单位 W |
| `RVSIN` | 真空室内侧命中点的大半径，单位 cm |
| `ZVSIN` | 真空室内侧命中点的 Z 坐标，单位 cm |
| `RVSOUT` | 真空室外侧命中点的大半径，单位 cm |
| `ZVSOUT` | 真空室外侧命中点的 Z 坐标，单位 cm |
| `VSURF` | 未计算，始终为零 |
| `WPDOT` | 未计算，始终为零 |
| `WBDOT` | 未计算，始终为零 |
| `SLANTU` | 到外侧上部限制器的间隙，单位 cm |
| `SLANTL` | 到外侧下部限制器的间隙，单位 cm |
| `ZUPERTS` | OMFIT 未提供描述 |
| `CHIPRE` | 压力约束的总 χ² |
| `CJOR95` | 归一化极向磁通 95% 处的归一化磁面平均电流密度 |
| `PP95` | 归一化极向磁通 95% 处的归一化 `P'(ψ)` |
| `DRSEP` | 单零构型下到外部第二分离面的外侧径向距离，单位 cm；SNT 为正、SNB 为负，默认值 40 cm |
| `YYY2` | Shafranov Y2 电流矩 |
| `XNNC` | 垂直稳定性参数，即归一化到临界指数的真空场指数 |
| `CPROF` | 电流剖面参数化参数 |
| `ORING` | 等离子体与倾斜表面之间的间隙，单位 cm |
| `CJOR0` | 归一化轴向磁面平均电流密度 |
| `FEXPAN` | X 点处的磁通扩张 |
| `QMIN` | 最小安全因子 `q_min` |
| `CHIMSE` | MSE 的总 χ² |
| `SSI01` | 归一化极向磁通 1% 处的磁剪切 |
| `FEXPVS` | 外侧下部真空室命中点处的磁通扩张 |
| `SEPNOSE` | X 点与 `ZNOSE` 处外部磁力线之间的径向距离，单位 cm |
| `SSI95` | 归一化极向磁通 95% 处的磁剪切 |
| `RHOQMIN` | `q_min` 处的归一化半径，即归一化体积的平方根 |
| `CJOR99` | 归一化极向磁通 99% 处的归一化磁面平均电流密度 |
| `CJ1AVE` | 等离子体外侧 5% 归一化极向磁通区域内的归一化平均电流密度 |
| `RMIDIN` | Z=0 处的内侧大半径，单位 m |
| `RMIDOUT` | Z=0 处的外侧大半径，单位 m |
| `PSURFA` | 等离子体边界表面积，单位 m² |
| `PEAK` | 中心压力与平均压力之比 |
| `DMINUX` | 上 X 点到限制器表面的最小距离，单位 cm |
| `DMINLX` | 下 X 点到限制器表面的最小距离，单位 cm |
| `DOLUBAF` | 分离面外侧支腿到上挡板的距离，单位 cm |
| `DOLUBAFM` | Rmax 处分离面到上挡板的距离，单位 cm |
| `DILUDOM` | 分离面内侧支腿到上穹顶的距离，单位 cm |
| `DILUDOMM` | Rmin 处分离面到上穹顶的距离，单位 cm |
| `RATSOL` | Rmin 和 Rmax 处外部磁力线到分离面距离（1 cm）的比值 |
| `RVSIU` | 上部真空室内侧打击点的大半径，单位 cm |
| `ZVSIU` | 上部真空室内侧打击点的 Z 坐标，单位 cm |
| `RVSID` | 下部真空室内侧打击点的大半径，单位 cm |
| `ZVSID` | 下部真空室内侧打击点的 Z 坐标，单位 cm |
| `RVSOU` | 上部真空室外侧打击点的大半径，单位 cm |
| `ZVSOU` | 上部真空室外侧打击点的 Z 坐标，单位 cm |
| `RVSOD` | 下部真空室外侧打击点的大半径，单位 cm |
| `ZVSOD` | 下部真空室外侧打击点的 Z 坐标，单位 cm |
| `CONDNO` | 条件数 |
| `PSIN32` | `q=3/2` 磁面处的归一化极向磁通 |
| `PSIN21` | `q=2` 磁面处的归一化极向磁通 |
| `RQ32IN` | `q=3/2` 磁面的最小大半径，单位 cm |
| `RQ21TOP` | `q=2` 磁面最大 Z 处的大半径，单位 cm |
| `CHILIBT` | Li 光束的总 χ² |
| `LI3` | IMAS 定义的 `li`：`2/R0/μ0²/Ip² * ∫(Bp² dV)` |
| `XBETAPR` | OMFIT 未提供描述 |
| `TFLUX` | OMFIT 未提供描述 |
| `TCHIMLS` | OMFIT 未提供描述 |
| `TWAGAP` | OMFIT 未提供描述 |

OMFIT 的 `desc` 还列出 `BETAN` 和 `FLUXX`。其中 `BETAN` 是 OMFIT 读取后根据
其它字段计算出的归一化 beta 派生量，不是 A-file 的固定原始记录；`FLUXX` 在该
解析流程中也没有作为标准记录读取。eqmdsk 不执行这类额外分析，因此二者不属于
当前公开字段。
