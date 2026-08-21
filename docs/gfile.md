# G-file guide

```python
import eqmdsk

g = eqmdsk.GFile("g067590.03300")
print(g)
print(g["NW"], g["PSIRZ"].shape)
g["CURRENT"] = 2.0
g.write("g.modified")
```

GFile 提供标准 GEQDSK 标量、profile、网格和边界字段。读取器接受固定宽度
或空白分隔的 header、CRLF、Fortran `D` 指数和常见旧式指数；写出器统一使用
标准固定宽度实数记录。

`cocos` 返回检测结果。来源明确时可以调用 `to_cocos(target, from_cocos=source)`；
也可以先 `select_cocos(source)` 后省略来源。`inplace=False` 返回新的 GFile。

G-file 输入中的前导文本、header 后缀和标准区域以外的扩展数据不会进入公开
映射，也不会由规范 writer 原样复制。保证的是写出后重新读取的标准字段语义。
