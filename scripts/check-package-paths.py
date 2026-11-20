"""Reject machine-specific paths in staged release files, including binary debug records."""
import argparse
from pathlib import Path


def path_patterns(roots):
    patterns = set()
    for root in roots:
        path = root.resolve()
        if path == Path(path.anchor):
            raise ValueError("Specify a directory below the filesystem root")
        for value in {str(path), path.as_posix(), str(path).replace("\\", "\\\\")}:
            for encoding in ("utf-8", "utf-16-le"):
                patterns.add(value.encode(encoding).lower())
    return patterns


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--private-path", type=Path, action="append", default=[])
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    roots = [repo, Path.home(), *args.private_path]
    if repo.parent.name == ".worktrees":
        roots.append(repo.parents[1])
    patterns = path_patterns(roots)
    if not (args.package / "manifest.json").is_file():
        parser.error("Expected an unpacked release directory")
    leaks = []
    for file in args.package.rglob("*"):
        if file.is_file():
            content = file.read_bytes().lower()
            if any(pattern in content for pattern in patterns):
                leaks.append(str(file.relative_to(args.package)))
    if leaks:
        raise SystemExit("Local path in release files: " + ", ".join(leaks))


if __name__ == "__main__":
    main()
