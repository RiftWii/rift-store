# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Copy the pinned RiftWii core into the build directory."""

import argparse
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory


REVISION = "94990481c5b12b37b9a6768541f67763fe1db6ba"
REPOSITORY = "https://github.com/KakarottoCake/Riftwii.git"
FILES = (
    "Makefile.bearssl", "vendor-bearssl", "include/riftwii", "wii/core/nanduse.cpp",
    "wii/core/nanduse.hpp", "wii/online/entropy.cpp", "wii/online/entropy.hpp",
    "wii/online/tls.cpp", "wii/online/tls.hpp", "wii/online/tlsroots.c",
    "wii/online/httpmem.cpp", "wii/online/httpmem.hpp", "wii/online/online.hpp",
    "wii/online/netsock.cpp", "wii/online/netsock.hpp", "wii/online/netconfig.cpp",
    "wii/online/netconfig.hpp", "plugins/appstore/storedata.cpp", "plugins/appstore/storedata.hpp",
    "plugins/common/rwpruntime.hpp", "src/net/applist.cpp", "src/net/appcatalog.cpp",
    "src/net/http.cpp", "src/net/dnsquery.cpp", "src/data/sha256.cpp", "src/data/textlines.cpp",
    "src/fs/zipread.cpp", "src/art/pngdecode.cpp", "src/art/themepack.cpp", "src/art/qrcode.cpp",
    "tools/app_list_check.cpp",
)


def extract(git, root, destination):
    import io
    import tarfile

    archive = subprocess.check_output([git, "-C", str(root), "archive", REVISION, "--", *FILES])
    with tarfile.open(fileobj=io.BytesIO(archive)) as source:
        source.extractall(destination, filter="data")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--riftwii", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--git", default="git")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    probe = subprocess.run([args.git, "-C", str(args.riftwii), "cat-file", "-e", REVISION + "^{commit}"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if probe.returncode == 0:
        extract(args.git, args.riftwii, args.output)
    else:
        with TemporaryDirectory() as directory:
            subprocess.run([args.git, "init", "-q", directory], check=True)
            subprocess.run([args.git, "-C", directory, "fetch", "--depth=1", REPOSITORY, REVISION], check=True)
            extract(args.git, directory, args.output)
    (args.output / "revision").write_text(REVISION + "\n", encoding="ascii")


if __name__ == "__main__":
    main()
