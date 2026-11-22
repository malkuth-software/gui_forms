#!/usr/bin/env python3
"""Moved to template_harness: harness/ic_declarations.py (harness.lock pins the commit).

The behavioural constraint declarations' generator (IC; docs/ic_declarations.md
in template_harness): run it with tools/ic/project.toml, from this checkout's
root, to write or check the header contract lines, the entry checks, the
member-manifest column, the fuzz blocks and the proof obligations of every
*.ic.toml declaration. A forwarding shim, as tools/check_house_style.py is.
"""
import harness_link

if __name__ == '__main__':
    harness_link.run_tool('ic_declarations')
else:
    harness_link.forward_import('ic_declarations')
