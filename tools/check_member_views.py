#!/usr/bin/env python3
"""member-view-refuses-temporaries: the lifetime rule for member accessors.

House style section 1: "A function that returns a borrow of an argument
refuses temporaries: delete its rvalue overload." A member accessor that
returns a view of one of its object's members borrows from `*this`, its
implicit argument, so `make_buffer().parts()` would hand out a view of a
temporary that ends with the statement. The rule: an inline member function
without parameters that returns a view (`std::span`, `std::string_view`,
`BorrowedSpan`, `BorrowedMutableSpan`, `BorrowedText`) of a data member
(its body names an identifier ending in `_`) is qualified `const&` (or `&`)
and has a deleted `&&` overload of the same name in the same file.

Virtual and static members are left out: a virtual accessor's ref
qualifier would have to change in every override, and a static one
borrows from no object. A view of static storage (`property_table()`)
names no data member and is left out too.

Run by tools/check_lifetime_rules.py after the harness's rules (the shim
there), on the same paths. A heuristic over source with comments and
literals masked, like the harness's rules; review still required. The rule
can move into template_harness's check_lifetime_rules.py once it has
settled (borrow sanitizer study, notes planning/borrow_sanitizer/).
"""
from __future__ import annotations
from dataclasses import dataclass
from pathlib import Path
import re

VIEW: str = r"(?:std\s*::\s*span\s*<[^;{}()]*>|std\s*::\s*(?:u8)?string_view|Borrowed(?:Mutable)?Span\s*<[^;{}()]*>|BorrowedText)"
ACCESSOR: re.Pattern[str] = re.compile(
    r"(?P<prefix>(?:\[\[nodiscard\]\]\s*)?(?:(?:inline|constexpr|virtual|static|friend)\s+)*)"
    r"(?P<ret>(?:typename\s+)?(?:[\w:]+\s*::\s*)?" + VIEW + r")\s+"
    r"(?P<name>[A-Za-z_]\w*)\s*\(\s*\)\s*(?P<quals>(?:const\s*)?(?:&&|&)?)\s*(?:noexcept\s*)?"
    r"(?P<override>(?:override|final)\s*)*(?P<end>\{|=\s*delete|;)")


@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    rule: str
    detail: str


def line_of(text: str, offset: int) -> int:
    result: int = text.count("\n", 0, offset) + 1
    return result


def body_of(text: str, opening: int) -> str:
    depth: int = 0
    index: int
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening:index + 1]
    return text[opening:]


def deleted_rvalue_names(text: str) -> set[str]:
    names: set[str] = set()
    match: re.Match[str]
    for match in ACCESSOR.finditer(text):
        if match.group("end").startswith("=") and "&&" in match.group("quals"):
            names.add(match.group("name"))
    return names


def check_member_views(path: Path, text: str) -> list[Finding]:
    """text: the file with comments and literals masked."""
    findings: list[Finding] = []
    deleted: set[str] = deleted_rvalue_names(text)
    match: re.Match[str]
    for match in ACCESSOR.finditer(text):
        name: str = match.group("name")
        prefix: str = match.group("prefix")
        if match.group("end") != "{":
            continue
        if re.search(r"\b(?:virtual|static|friend)\b", prefix) or match.group("override"):
            continue
        body: str = body_of(text, match.end("end") - 1)
        if re.search(r"\b[A-Za-z]\w*_\b", body) is None:
            continue
        qualified: bool = match.group("quals").rstrip().endswith("&")
        if qualified and name in deleted:
            continue
        missing: str = "a ref qualifier and a deleted && overload" if not qualified else "a deleted && overload"
        findings.append(Finding(str(path), line_of(text, match.start("name")), "member-view-refuses-temporaries",
                                name + "() returns a view of a member without " + missing))
    return findings
