"""Locate the repository's optional Python packages and .NET runtime."""
from pathlib import Path

_repo = Path(__file__).resolve().parents[2]
TOOLS = _repo / '.tools'
if not TOOLS.is_dir() and _repo.parent.name == '.worktrees':
    TOOLS = _repo.parents[1] / '.tools'
