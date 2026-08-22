# A-file 指南

```python
import eqmdsk

a = eqmdsk.AFile("a067590.03300")
print(a["SHOT"], a["BETAP"], a["RCO2V"].shape)
a["BETAP"] = 0.25
a.write("a.modified")
```

A-file 是 EFIT 输出的平衡摘要，记录由 Fortran 固定格式组成。控制字段包括
`SHOT`、`TIME`、`JFLAG`、`LFLAG`、`LIMLOC`、`MCO2V`、`MCO2R`、`QMFLAG`、
`NLOLD`、`NLNEW`。`MCO2V`、`MCO2R` 以及磁探针、线圈和等离子体电流相关计数决定
弦线和响应数组长度；修改计数时必须同步修改数组。

读取器兼容 EFIT 常见的控制记录和可选记录，并对截断、数组长度和有限值进行校验。
缺失或损坏的冗余 `TIME` 记录按兼容规则回退，不把内部记录计数、原始 header/footer、
raw sections 或 Fortran 解析对象暴露给 Python。

写出器按当前公开字段重建标准 A-file。它不保证原始空格、注释、可选记录顺序或字节
级保真；保证重新读取后公开字段的语义一致。标准字段使用大写名称且不提供别名。
