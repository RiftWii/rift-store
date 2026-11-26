# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Cache the official 128x48 Homebrew Channel artwork for the offline preview."""

from concurrent.futures import ThreadPoolExecutor
from hashlib import sha256
import json
from pathlib import Path
import sys
from urllib.parse import quote, urlsplit
from urllib.request import urlopen


CATALOGUE = "https://riftwii.com/apps/apps.json"
HOSTS = {"riftwii.com", "riftwii.github.io"}
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def bounded_read(url, limit):
    with urlopen(url, timeout=12) as response:
        destination = urlsplit(response.url)
        if destination.scheme != "https" or destination.netloc not in HOSTS:
            raise ValueError("Unexpected download host")
        data = response.read(limit + 1)
    if len(data) > limit:
        raise ValueError("Download exceeds size limit")
    return data


def icon_filename(app_id):
    return sha256(app_id.encode("utf-8")).hexdigest() + ".png"


def fetch_icon(app, cache):
    icon = urlsplit(app["icon"])
    if icon.scheme != "https" or icon.netloc not in HOSTS or not icon.path.startswith("/apps/icons/"):
        raise ValueError(f"Unexpected icon URL for {app['id']}")
    path = cache / icon_filename(app["id"])
    if path.is_file():
        return
    url = "https://riftwii.com" + quote(icon.path, safe="/%")
    data = bounded_read(url, 128 * 1024)
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError(f"Icon for {app['id']} is not a PNG")
    pending = path.with_suffix(".tmp")
    pending.write_bytes(data)
    pending.replace(path)


def main():
    cache = Path(sys.argv[1])
    cache.mkdir(parents=True, exist_ok=True)
    catalogue = json.loads(bounded_read(CATALOGUE, 2 * 1024 * 1024))
    if catalogue["schema"] != 1 or len(catalogue["apps"]) > 1000:
        raise ValueError("Unexpected catalogue format")
    icons = [app for app in catalogue["apps"] if app.get("icon")]
    with ThreadPoolExecutor(max_workers=8) as workers:
        list(workers.map(lambda app: fetch_icon(app, cache), icons))
    print(f"Cached {len(icons)} official app icons")


if __name__ == "__main__":
    main()
