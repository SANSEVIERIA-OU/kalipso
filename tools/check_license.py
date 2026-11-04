#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
"""Check the fixed company licence and the repository's release notice, offline."""
import argparse
import hashlib
import os
from pathlib import Path

EXPECTED_SHA256 = "134344bb2e5e1214fa519adc4a10691d6c69c12f66dcc97cd9341ebb90f4a773"


def check(root, repository=None):
    errors = []
    licence = root / "LICENSE"
    if licence.is_symlink() or not licence.is_file():
        errors.append("LICENSE must be a regular file containing the complete licence.")
    elif hashlib.sha256(licence.read_bytes()).hexdigest() != EXPECTED_SHA256:
        errors.append("LICENSE differs from the canonical SANSEVIERIA License 1.0.")
    notice = root / "NOTICE"
    if notice.is_symlink() or not notice.is_file():
        errors.append("NOTICE must identify this project and its separately licensed components.")
    else:
        text = notice.read_text(encoding="utf-8")
        if "SANSEVIERIA License 1.0" not in text:
            errors.append("NOTICE must identify SANSEVIERIA License 1.0.")
        if repository:
            expected = f"https://github.com/{repository}"
            # Require a complete URL token, not a prefix of another repository name.
            urls = {word.rstrip(".,;)") for word in text.split()}
            if expected not in urls:
                errors.append(f"NOTICE must identify {expected}; configure this project's notice.")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--repository", default=os.environ.get("GITHUB_REPOSITORY"))
    args = parser.parse_args()
    errors = check(args.root, args.repository)
    if errors:
        raise SystemExit("\n".join(errors))
    print("SANSEVIERIA License 1.0 and repository notice: PASS")


if __name__ == "__main__":
    main()
