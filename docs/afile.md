# A-file guide

```python
import eqmdsk

a = eqmdsk.AFile("a067590.03300")
print(a)
print(a["SHOT"], a["BETAP"], a["RCO2V"].shape)
a["BETAP"] = 0.25
a.write("a.modified")
```

AFile 支持 EFIT 控制记录、固定四实数记录、弦线和响应数组，以及标准可选
记录。数组长度由 `MCO2V`、`MCO2R` 和响应计数控制；修改计数时必须同步
修改对应数组。

读取器对损坏或缺失的冗余 `TIME` 记录采用标准兼容回退：控制记录、第三条
header 记录、最后使用 `0.0`。写出器根据当前公开字段生成规范 A-file，原始
header/footer 和可选记录计数属性不对外暴露。
