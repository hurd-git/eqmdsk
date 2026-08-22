# G-file 指南

```python
import eqmdsk

g = eqmdsk.GFile("g067590.03300")
print(g["NW"], g["PSIRZ"].shape)
g["CURRENT"] = 2.0
g["AuxNamelist"]["OUT1"]["ISHOT"] = 67591
g.write("g.modified")
```

## 经典 GEQDSK 主体

header 的最后三个整数是 `IDUM`、`NW`、`NH`。主体随后固定出现 20 个标量、四组
`NW` profile、`NH*NW` 个 `PSIRZ`、`NW` 个 `QPSI`，再出现 `NBBBS`、`LIMITR` 和
两组坐标。`PSIRZ` 按文件顺序转换为 C 连续的 `(NH, NW)` 数组。

读取器接受固定宽度或空白分隔 header、CRLF、Fortran `D` 指数以及常见的省略指数
标记写法。写出器统一生成固定宽度 E16.9 数值记录，并在打开目标文件之前完成
尺寸、计数和有限值校验。

## EFIT 数值扩展

经典限制器之后可能出现 `KVTOR/RVTOR/NMASS`。`KVTOR > 0` 时紧跟 `PRESSW/PWPRIM`，
`NMASS > 0` 时紧跟 `DMION`，然后是 `RHOVN`、`KEECUR` 和条件性的 `EPOTEN`。
eqmdsk 根据控制值和 `NW` 读取数组，并按相同顺序写出。

之后的 `IPLCOUT` 相关数据存在版本差异：

- 模式 2 在完整长度匹配时公开为 `PCURRZ`、`CJOR`、`R1SURF`、`R2SURF`、`VOLP`、
  `BPOLSS`；
- 模式 1 的 `NW/NH/ISHOT/ITIME` 头按四个 `I5` 列解析，`RGRID/ZGRID` 和可变长度
  前缀公开，末尾 `PCURRT` 为 `(NH, NW)`；
- F-coil/E-coil 数量不在 G-file 中，前缀无法可靠命名时不猜测；
- 不能安全匹配的数值放入 `UNPARSED_EXTENSION`。

扩展字段只保证读写语义，不自动参与 COCOS 转换。修改后写出器会重新检查数组形状
和有限值。

## AuxNamelist

G-file 尾部以 `&OUT1`、`&BASIS` 等 section 开始时，eqmdsk 将所有可投影 namelist
放入 `g["AuxNamelist"]`。section 名称不限制为三个已知名字，其他 EFIT 版本的附加
section 也能兼容。原始注释、重复赋值和排版不保留；写出结果是稳定的 canonical
namelist。

## COCOS

`g.cocos` 返回检测结果。来源明确时调用 `g.to_cocos(target, from_cocos=source)`；
也可以先 `select_cocos(source)`，再省略来源。检测结果不唯一且没有显式来源时抛出
`CocosError`。COCOS 只转换经典字段，扩展和 AuxNamelist 保持原数值语义。
