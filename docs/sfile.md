# S-file 指南

```python
import eqmdsk

s = eqmdsk.SFile("s123456.01234")
print(s.keys())
s["Y"][0] = 42.0
s.write("s.modified")
```

S-file 可包含最多三个前导文本标签：`XLABEL`、`YLABEL`、`TITLE`。数据主体每行
包含四个有限实数，公开为等长数组 `X`、`Y`、`DX`、`DY`。空文件和只有标签的文件
也可以读取。

读取器会跳过常见的非标准穿插文本，以兼容不同 EFIT 生产程序；规范写出只包含标签
和四列数据，不复制穿插文本、内部 raw sections 或原始行排版。修改数组时必须保持
长度一致，写出前会进行有限值和长度校验。
