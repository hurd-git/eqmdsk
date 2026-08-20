import ast
from pathlib import Path

import eqmdsk


def test_pep561_files_are_installed_and_stub_exports_match_runtime():
    package = Path(eqmdsk.__file__).resolve().parent
    stub = package / "__init__.pyi"
    marker = package / "py.typed"

    assert stub.is_file()
    assert marker.is_file()
    assert marker.read_bytes() == b""

    tree = ast.parse(stub.read_text(encoding="utf-8"), filename=str(stub))
    stub_all = None
    declarations = set()
    for node in tree.body:
        if isinstance(node, (ast.ClassDef, ast.FunctionDef, ast.AsyncFunctionDef)):
            declarations.add(node.name)
        elif isinstance(node, ast.AnnAssign) and isinstance(node.target, ast.Name):
            declarations.add(node.target.id)
        elif (
            isinstance(node, ast.Assign)
            and any(
                isinstance(target, ast.Name) and target.id == "__all__"
                for target in node.targets
            )
        ):
            stub_all = ast.literal_eval(node.value)

    assert stub_all == eqmdsk.__all__
    assert set(stub_all) <= declarations | {"__all__"}
