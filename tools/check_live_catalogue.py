# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Check the current store feed with Rift Store's native parser."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

from fetch_icons import CATALOGUE, bounded_read


RIFTWII_REVISION = "94990481c5b12b37b9a6768541f67763fe1db6ba"
CATALOGUE_LIMIT = 2 * 1024 * 1024
PARSER_FILES = (
    "include/riftwii/applist.hpp",
    "include/riftwii/jsonreader.hpp",
    "tools/app_list_check.cpp",
)
STORE = Path(__file__).resolve().parents[1]


def read_catalogue(path):
    if path is None:
        return bounded_read(CATALOGUE, CATALOGUE_LIMIT)
    with path.open("rb") as source:
        data = source.read(CATALOGUE_LIMIT + 1)
    if len(data) > CATALOGUE_LIMIT:
        raise ValueError("Catalogue exceeds the 2 MiB limit")
    return data


def check(riftwii, git, compiler, data):
    build = STORE / "build"
    build.mkdir(exist_ok=True)
    with TemporaryDirectory(prefix="catalogue-check-", dir=build) as directory:
        work = Path(directory)
        for name in PARSER_FILES:
            spec = f"{RIFTWII_REVISION}:{name}"
            source = subprocess.run([git, "-C", str(riftwii), "show", spec],
                                    check=True, stdout=subprocess.PIPE).stdout
            path = work / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(source)

        executable = work / ("app_list_check.exe" if os.name == "nt" else "app_list_check")
        subprocess.run([compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-Iinclude", str(STORE / "source/catalogue_parser.cpp"), "tools/app_list_check.cpp",
                        "-o", executable.name], cwd=work, check=True)
        subprocess.run([str(executable)], cwd=work, input=data, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--riftwii", required=True, type=Path, help="local RiftWii Git checkout")
    parser.add_argument("--catalogue", type=Path, help="local apps.json instead of the live feed")
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"), help="host C++ compiler")
    parser.add_argument("--git", default="git", help="Git executable")
    args = parser.parse_args()
    try:
        check(args.riftwii.resolve(), args.git, args.cxx, read_catalogue(args.catalogue))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Catalogue check failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
