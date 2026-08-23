import ast
from pathlib import Path

import eqmdsk


def test_pep561_stub_is_installed_and_exports_match_runtime():
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


def test_stub_contains_declarations_but_no_documentation_strings():
    package = Path(eqmdsk.__file__).resolve().parent
    stub = package / "__init__.pyi"
    tree = ast.parse(stub.read_text(encoding="utf-8"), filename=str(stub))

    documented_nodes = []
    for node in ast.walk(tree):
        if isinstance(node, (ast.Module, ast.ClassDef, ast.FunctionDef)):
            if ast.get_docstring(node, clean=False) is not None:
                documented_nodes.append(getattr(node, "name", "<module>"))

    assert documented_nodes == []


def test_public_runtime_docs_come_from_the_implementation_module():
    gfile_doc = eqmdsk.GFile.__doc__ or ""
    assert "Required fields:" in gfile_doc
    assert "Optional fields:" in gfile_doc
    assert "Reading and editing:" in gfile_doc
    assert "COCOS detection, selection, and conversion:" in gfile_doc
    assert "Creating a new G-file:" in gfile_doc
    assert "Documentation:" in gfile_doc

    assert "Required fields:" in (eqmdsk.AFile.__doc__ or "")
    assert "Creating a new A-file:" in (eqmdsk.AFile.__doc__ or "")
    assert "Creating a new S-file:" in (eqmdsk.SFile.__doc__ or "")
    assert "Block and field rules:" in (eqmdsk.KFile.__doc__ or "")

    for public_type in (
        eqmdsk.GFile,
        eqmdsk.AFile,
        eqmdsk.SFile,
        eqmdsk.KFile,
        eqmdsk.Namelist,
        eqmdsk.NamelistBlock,
    ):
        assert public_type.__init__.__doc__ == public_type.__doc__
    assert "COCOS detection" in (eqmdsk.CocosResult.__doc__ or "")
