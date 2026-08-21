# S-file guide

```python
import eqmdsk

s = eqmdsk.SFile("s123456.01234")
print(s)
s["Y"][0] = 42.0
s.write("s.modified")
```

SFile 支持最多三个前导文本标签 `XLABEL`、`YLABEL`、`TITLE`，以及等长的
`X`、`Y`、`DX`、`DY` 数组。每个数据行必须有四个有限实数；空文件和只有标签
的文件也是合法输入。

非标准穿插文本可被读取器跳过，但不会由规范 writer 复制。写出结果只包含
标签和四列标准数据，重新读取后数组和标签语义保持一致。
