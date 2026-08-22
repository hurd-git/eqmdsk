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
| `TIME` | 平衡对应时间 |
| `JFLAG`, `LFLAG` | EFIT 状态或计算标志 |
| `LIMLOC` | 限制器位置标志 |
| `MCO2V` | 垂直弦线数量 |
| `MCO2R` | 径向弦线数量 |
| `QMFLAG` | Q 相关计算标志 |
| `NLOLD`, `NLNEW` | 旧、新版本或迭代相关计数 |
| `CHISQ` | 拟合残差或卡方量 |
| `RCENCM`, `RCNTR`, `ZCNTR` | 参考中心位置量 |
| `BCENTR` | 参考环向磁场 |
| `IPMEAS`, `IPMHD` | 电流测量和 MHD 电流量 |
| `AMINOR` | 小半径 |
| `ELONG`, `ELONGM` | 拉长率 |
| `UTRI`, `LTRI` | 上、下三角度相关量 |
| `VOLUME` | 等离子体体积 |
| `RCURRT`, `ZCURRT` | 电流中心坐标 |
| `QSTAR`, `Q95` | 安全因子指标 |
| `BETAT`, `BETAP`, `BETAPD` | 环向、极向及相关 beta 指标 |
| `LI` | 内部电感指标 |
| `GAPIN`, `GAPOUT`, `GAPTOP`, `GAPBOT` | 等离子体与边界/限制器的间隙 |
| `VERTN`, `SHEAR` | 垂直位置和剪切相关量 |
| `BPOLAV` | 平均极向磁场 |
| `S1`, `S2`, `S3` | EFIT 摘要剖面量 |
| `QOUT`, `SEPIN`, `SEPOUT`, `SEPTOP` | Q 值和分离点间隙相关量 |
| `SIBDRY`, `AREA` | 边界磁通和截面积 |
| `WMHD` | MHD 能量 |
| `ERROR` | EFIT 误差指标 |
| `QM`, `CDFLUX`, `ALPHA`, `RTTT`, `PSIREF` | 平衡拟合或磁通相关量 |
| `INDENT`, `DSEP`, `SEPEXP`, `SEPBOT` | 边界形状和分离点相关量 |
| `BTAXP`, `BTAXV` | 磁轴场量 |
| `AQ1`, `AQ2`, `AQ3` | 拟合系数 |
| `RM`, `ZM`, `PSIM` | 磁面或测量点位置/磁通量 |
| `TAUMHD`, `BETATD`, `WDIA`, `DIAMAG` | MHD、beta、能量和磁诊断量 |
| `VLOOP`, `TAUDIA`, `QMERCI`, `TAVEM` | 回路电压、时间常数和诊断量 |
| `NSILOP0`, `MAGPRI0`, `NFCOIL0`, `NESUM0` | 后续诊断数组的数量控制字段 |
| `RCO2V`, `DCO2V`, `RCO2R`, `DCO2R` | CO2/干涉测量弦线数据 |
| `CSILOP`, `CMPR2`, `CCBRSP`, `ECCURT` | 线圈、电流和诊断数组 |
| `PBINJ`, `RVSIN`, `ZVSIN`, `RVSOUT`, `ZVSOUT` | 注入和视线几何相关量 |
| `VSURF`, `WPDOT`, `WBDOT`, `SLANTU`, `SLANTL` | 表面、功率和边界相关扩展 |
| `ZUPERTS`, `CHIPRE`, `CJOR95`, `PP95` | EFIT 可选诊断扩展 |
| `DRSEP`, `YYY2`, `XNNC`, `CPROF`, `ORING` | 版本相关可选扩展 |
| `CJOR0`, `FEXPAN`, `QMIN`, `CHIMSE`, `SSI01`, `FEXPVS` | 电流、磁通和误差相关扩展 |
| `SEPNOSE`, `SSI95`, `RHOQMIN`, `CJOR99`, `CJ1AVE` | 分离点、Q 和电流相关扩展 |
| `RMIDIN`, `RMIDOUT`, `PSURFA`, `PEAK`, `DMINUX`, `DMINLX` | 中平面、压力和极值相关扩展 |
| `DOLUBAF`, `DOLUBAFM`, `DILUDOM`, `DILUDOMM`, `RATSOL` | 版本相关边界/刮削层扩展 |
| `RVSIU`, `ZVSIU`, `RVSID`, `ZVSID`, `RVSOU`, `ZVSOU`, `RVSOD`, `ZVSOD` |  |
| `CONDNO`, `PSIN32`, `PSIN21`, `RQ32IN`, `RQ21TOP`, `CHILIBT` |  |
| `LI3`, `XBETAPR`, `TFLUX`, `TCHIMLS`, `TWAGAP` |  |
