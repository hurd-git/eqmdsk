# Formats and public fields

eqmdsk 支持 EFIT G/A/K/S 四种文件。四个 Python 类都在读取时加载完整文件，
通过字典式接口暴露标准字段，并按规范重新生成输出。库不提供自动格式检测，
调用者应显式选择 `GFile`、`AFile`、`KFile` 或 `SFile`。

## GFile

公开字段包括：

- `CASE`、`NW`、`NH`；
- 几何和磁平衡标量 `RDIM`、`ZDIM`、`RCENTR`、`RLEFT`、`ZMID`、`RMAXIS`、
  `ZMAXIS`、`SIMAG`、`SIBRY`、`BCENTR`、`CURRENT`；
- `FPOL`、`PRES`、`FFPRIM`、`PPRIME`、`QPSI` 一维数组；
- `PSIRZ` 二维数组；
- `NBBBS`、`LIMITR` 及边界和限制器坐标数组。

写出器检查数组长度、网格尺寸、边界数量和有限数值，并生成标准 GEQDSK
数字记录。COCOS 检测与转换只在 GFile 上提供。

## AFile

AFile 公开 EFIT 标准控制字段、固定四实数记录、弦线数组、响应数组和最多
十五组可选标准记录。控制字段包括：

```text
SHOT TIME JFLAG LFLAG LIMLOC MCO2V MCO2R QMFLAG NLOLD NLNEW
```

`MCO2V`、`MCO2R`、`NSILOP0`、`MAGPRI0`、`NFCOIL0`、`NESUM0` 控制对应数组
长度。写出器会根据当前字段重建标准 header、控制记录和数据记录；原始
header/footer、记录数量属性和其他排版信息不属于公开 API。

## KFile

KFile 是两层映射：`kfile[section][variable]`。节和变量名大小写不敏感，
重复赋值采用最后一个有效值。复杂 namelist 语法不会进入公开映射。写出器
生成稳定的 Fortran namelist 文本。

## SFile

SFile 的标准字段为可选的 `XLABEL`、`YLABEL`、`TITLE` 和四个等长数组
`X`、`Y`、`DX`、`DY`。每一行数据必须包含四个有限实数。写出器输出可选标签
和四列标准数据，忽略输入中的非标准穿插文本。

## 路径和文本

C++ 接口使用 UTF-8/本机字符串路径；Python 构造和写出接受字符串或
`PathLike[str]`，`filename` 返回 `str`。标准文本字段在 Python 中返回 `str`，
不返回 `bytes`。
