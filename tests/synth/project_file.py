"""A .chimeraProject as the tests write and read one (docs/project.md): a
zip of project.json, input.input (the input's entries, a line each) and
branches/<n>.input (each branch's, numbered from 1).

write() takes the project as one dict whose "inputs" (its own and each
branch's) hold their "entries" as a list, and read() gives it back the same
way, so a test can build or check a project as data. On the path the
witness puts in PYTHONPATH: import project_file."""
import json
import zipfile


def _lines(inputs):
    entries = inputs.pop("entries", []) if isinstance(inputs, dict) else []
    return "".join(e + "\n" for e in entries)


def write(path, doc):
    doc = json.loads(json.dumps(doc))
    parts = {"input.input": _lines(doc.get("inputs"))}
    if doc.get("inputs") == {}:
        del doc["inputs"]
    for n, branch in enumerate(doc.get("branches", []), 1):
        parts["branches/%d.input" % n] = _lines(branch.get("inputs"))
        if branch.get("inputs") == {}:
            del branch["inputs"]
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("project.json", json.dumps(doc, indent="\t") + "\n")
        for name, text in parts.items():
            z.writestr(name, text)


def read(path):
    with zipfile.ZipFile(path) as z:
        names = set(z.namelist())

        def entries(name):
            return z.read(name).decode("utf-8").splitlines() if name in names else []

        doc = json.loads(z.read("project.json"))
        doc.setdefault("inputs", {})["entries"] = entries("input.input")
        for n, branch in enumerate(doc.get("branches", []), 1):
            branch.setdefault("inputs", {})["entries"] = entries("branches/%d.input" % n)
    return doc
