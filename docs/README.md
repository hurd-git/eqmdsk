# eqmdsk 文档

`eqmdsk` 是一个轻量的 EFIT G/A/K/S 文件读写库。项目取 OMFIT 的兼容性和
`eqdsk` 的轻量性：完整读取文件、提供清晰的字典式字段并按标准写出，不计算
平衡性质，也不连接 OMAS、MDSplus、数据库或其他服务。

## 文档导航

- [G-file](gfile.md)：经典 GEQDSK、EFIT 数值扩展、`IPLCOUT`、`AuxNamelist`；
- [A-file](afile.md)：控制记录、固定记录和计数驱动的数组；
- [K-file](kfile.md)：Fortran namelist 的两层字典接口；
- [S-file](sfile.md)：标签和四列数据；
- [Python API](python-api.md)：类型、映射、写出和异常；
- [兼容性](compatibility.md)：平台、依赖、资源限制和边界；
- [发布准备](releasing.md)：本地构建和未来发布流程。

## 设计原则

1. 构造时一次性读入，写出时一次性生成；不使用懒加载、内存映射或缓存。
2. 保证 `parse -> save -> parse` 的字段语义一致，不保证原始字节排版一致。
3. 只有在记录长度和控制字段能够安全确定含义时才拆分扩展；无法确定的数值放入
   `UNPARSED_EXTENSION`，绝不猜测物理含义。
4. 标准 G/A/S 字段使用规范大写名称且不提供别名；K-file 标识符遵循 Fortran 的
   大小写不敏感规则。
5. C++ 和 Python 共享同一套解析、校验和写出逻辑。
