"""The `// ic:` contract lines tools/ic_declarations.py writes into headers, kept across other generators."""


def keep_contract_lines(current: str | None, generated: str) -> str:
    """The generated text with the current file's `// ic:` contract lines kept.

    Those lines belong to tools/ic_declarations.py (IC3); a generator that
    rewrites a header holding them (abi_class_tables, property_codemod) keeps
    each group above the line it preceded, so the two generators never undo
    each other. A group whose line is gone is dropped, and the declarations'
    check then reports the header stale.
    """
    if current is None:
        return generated
    groups: list[tuple[list[str], str]] = []
    pending: list[str] = []
    line: str
    for line in current.split("\n"):
        if line.lstrip().startswith("// ic:"):
            pending.append(line)
            continue
        if pending:
            groups.append((pending, line))
            pending = []
    if not groups:
        return generated
    lines: list[str] = [line for line in generated.split("\n") if not line.lstrip().startswith("// ic:")]
    out: list[str] = []
    next_group: int = 0
    for line in lines:
        while next_group < len(groups) and groups[next_group][1] == line:
            out.extend(groups[next_group][0])
            next_group += 1
        out.append(line)
    kept: str = "\n".join(out)
    return kept
