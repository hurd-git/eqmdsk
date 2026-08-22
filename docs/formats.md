# EFIT 格式与公开字段

eqmdsk 支持 EFIT 常见的 G、A、K、S 四类文件。调用者应显式选择对应的类，库不
根据文件名后缀猜测类型。四类文件在构造时完整读取，并通过字典式接口公开可解释
的字段。

## G-file（GEQDSK）

经典主体由 `NW`、`NH`、`NBBBS`、`LIMITR` 决定长度：20 个标量、四组长度为
`NW` 的 profile、`NH*NW` 个 `PSIRZ`、长度为 `NW` 的 `QPSI`、边界和限制器坐标。
`PSIRZ` 按 `(NH, NW)` 暴露。经典字段包括 `CASE`、几何/磁平衡标量、`FPOL`、
`PRES`、`FFPRIM`、`PPRIME`、`PSIRZ`、`QPSI`、边界计数和坐标。

EFIT 可能在主体后追加：

```text
KVTOR RVTOR NMASS
PRESSW PWPRIM       # KVTOR > 0
DMION               # NMASS > 0
RHOVN
KEECUR
EPOTEN              # KEECUR > 0
```

`IPLCOUT=2` 在完整长度匹配时公开为 `PCURRZ`、`CJOR`、`R1SURF`、`R2SURF`、
`VOLP`、`BPOLSS`。`IPLCOUT=1` 的四个整数头按固定 `I5` 列解析，随后保留
`RGRID`、`ZGRID`、可变长度的 `IPLCOUT_PREFIX` 和 `(NH, NW)` 的 `PCURRT`。
F-coil/E-coil 数量不写在 G-file 中，前缀不能安全命名为 `BRSP` 或 `ECURRT`。
无法分类的数值保留为 `UNPARSED_EXTENSION`，仍可查看和写回。扩展字段不盲目参与
COCOS 变换。

## AuxNamelist

G-file 尾部可能有 `&OUT1`、`&BASIS`、`&CHIOUT` 等 Fortran namelist。OMFIT 将
它们放入名为 `AuxNamelist` 的容器，文件中并没有字面量 `&AuxNamelist`。eqmdsk
采用同样的结构：

```python
g["AuxNamelist"]["OUT1"]["ISHOT"]
```

section 名称不硬编码，其他 EFIT 版本的附加 section 也可读取。写出只生成公开
投影的 canonical namelist，不保证注释和原始排版。应修改嵌套字段后调用 G-file
的 `write()`，不要单独把 AuxNamelist 当作普通 K-file 覆盖原文件。

## A-file

A-file 按固定 Fortran 记录组织摘要。`MCO2V`、`MCO2R`、`NSILOP0`、`MAGPRI0`、
`NFCOIL0`、`NESUM0` 等控制字段决定后续数组长度。读取器检查计数、记录和有限值；
写出器从公开字段重建标准记录。原始 header/footer 和内部计数不属于公开接口。

## K-file

K-file 是 Fortran namelist，section 不固定。`IN1` 常见但不保证存在，也可能有
`INWANT`、`INS`、`INSXR`、`EFITIN` 等。接口是 `k["IN1"]["LIMITR"]`；不允许
随意创建未知 section/变量。复杂索引和无法安全投影的语法不伪装成普通字段。

## S-file

S-file 可有 `XLABEL`、`YLABEL`、`TITLE` 三个标签，数据为等长的 `X`、`Y`、
`DX`、`DY` 四列有限实数。非标准穿插文本可读取但规范写出会丢弃。

## 路径和文本

C++ 使用字符串路径，Python 接受 `str` 和 `PathLike[str]`。`filename`、`CASE`、
标签和 K-file 字符串以 Python `str` 返回，不以 `bytes` 暴露。
