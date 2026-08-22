# 发布准备

当前阶段只维护本地 Git，不推送远程，也不立即发布 PyPI。未来发布应让 PyPI 版本、
Git tag 和 GitHub Release 使用同一个语义版本，例如 `v0.9.0`。

## 本地检查

1. 确认 `CMakeLists.txt`、`pyproject.toml` 和 `include/eqmdsk/version.hpp` 版本一致。
2. 运行 C++、Python、模糊解析 smoke、类型检查和 stubtest。
3. 构建 C++ 安装包，在空目录中用 `find_package(eqmdsk CONFIG REQUIRED)` 编译 consumer。
4. 在没有 `extern/` 和虚拟环境的干净树中构建 sdist，再从 sdist 构建 wheel。
5. 检查归档不包含 `.venv`、构建目录、本地数据或缓存；MIT 许可证和 notices 必须存在。
6. 对 G/A/K/S 样本执行 parse→write→parse，并检查扩展字段和 AuxNamelist。

常用命令：

```console
uv build --wheel --sdist
uv venv build/release-venv
uv pip install --python build/release-venv/bin/python dist/eqmdsk-0.9.0-*.whl
build/release-venv/bin/python -c "import eqmdsk; print(eqmdsk.__version__)"
```

## CI、wheels 与 PyPI

普通 CI workflow 负责在不同操作系统和 Python 版本上运行测试、类型检查和源码构建；
`wheels.yml` 负责 cibuildwheel 矩阵，生成并测试各平台 wheel，同时生成 sdist。两者
都不应从 `main` 自动发布 PyPI。

未来可在精确的 `vX.Y.Z` tag 上手动触发发布 job：先上传同一批 wheel/sdist 到 PyPI，再
创建带有相同文件的 GitHub Release。推荐使用 PyPI Trusted Publisher，不在仓库保存长期
token。发布失败时不创建 GitHub Release，避免两边版本不一致。
