#!/usr/bin/env python3
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
from datetime import date, datetime, timedelta, timezone
from functools import lru_cache
import json
import math
import os
from pathlib import Path
import re
import sys
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen
from urllib.parse import urlencode, urljoin, urlsplit
from html.parser import HTMLParser
from itertools import product as combinations
import xml.etree.ElementTree as ET

from PIL import Image


HERE = Path(__file__).resolve().parent
BASE_URL = "https://cdn.star.nesdis.noaa.gov"
EUMETVIEW = "https://view.eumetsat.int/geoserver/wms"
GIBS = "https://gibs.earthdata.nasa.gov/wms/epsg4326/best/wms.cgi"
COPERNICUS = "https://sh.dataspace.copernicus.eu/process/v1"
TOKEN_URL = "https://identity.dataspace.copernicus.eu/auth/realms/CDSE/protocol/openid-connect/token"
PROVIDERS = ("goes", "eumetsat", "himawari", "nasa_gibs", "copernicus")
# EUMETSAT is restricted to these three selected layers.
WMS_SOURCES = {
    "eumetsat": (EUMETVIEW, (-65, -65, 65, 65), [
        "mtg_fd:rgb_dust", "msg_fes:rgb_airmass", "mtg_fd:rgb_truecolour",
    ]),
    "himawari": (GIBS, (80, -60, 180, 60), [
        "Himawari_AHI_Band13_Clean_Infrared", "Himawari_AHI_Air_Mass",
        "Himawari_AHI_Band3_Red_Visible_1km",
    ]),
    "nasa_gibs": (GIBS, (-180, -90, 180, 90), [
        "MODIS_Terra_CorrectedReflectance_TrueColor",
        "MODIS_Aqua_CorrectedReflectance_TrueColor",
    ]),
}
FIELDS = ["provider", "satellite", "sector", "product", "requested_time", "url",
          "filename", "status", "error", "downloaded_at_utc"]
ENTRY = re.compile(
    r'\{\s*"Satellite"\s*,\s*"([A-Za-z0-9_]+)"\s*\}\s*,\s*'
    r'\{\s*"Sector"\s*,\s*"([A-Za-z0-9_]+)"\s*\}\s*,\s*'
    r'\{\s*"Product"\s*,\s*"([A-Za-z0-9_]+)"\s*\}'
)
# Fixed NOAA STAR ABI views. Meso locations move, so they are not listed here.
GOES_SECTORS = {
    "GOES18": (
        "PNW", "PSW", "WUS", "AK", "CAK", "SEA", "NP", "HI", "TPW", "TSP", "EEP",
    ),
    "GOES19": (
        "NR", "UMV", "CGL", "NE", "SR", "SP", "SMV", "SE", "EUS", "CAN",
        "NA", "CAR", "GA", "PR", "TAW", "EEP", "MEX", "CAM", "NSA", "SSA",
    ),
}
GOES_CONUS_PRODUCTS = ("GEOCOLOR", "AirMass", "Dust", "DayNightCloudMicroCombo",
                       "FireTemperature", "Sandwich", *(f"{band:02d}" for band in range(1, 17)))
GOES_IMAGE_SIZE = (500, 500)
GOES_EXTRA_SECTORS = ("GM", "GS", "PACUS", "US", "NUS", "CUS", "SUS", "NEX", "SAX")
GOES_EXTRA_PRODUCTS = (
    "DayConvection", "DayLandCloud", "DayLandCloudFire", "DaySnowFog",
    "DayCloudPhaseDistinction", "NighttimeMicrophysics", "SimpleWaterVapor",
    "DifferentialWaterVapor", "SO2", "Ash", "CloudTop", "CloudTopHeight",
    "CloudTopTemperature", "RainRate", "Aerosol", "GLM", "Fire", "NaturalColor",
    "TrueColor", "GeoColor", "RGB", "18", "17",
)


def read_config(path):
    source = re.sub(r'/\*.*?\*/|//[^\n]*', '', path.read_text(), flags=re.S)
    entries = ENTRY.findall(source)
    if not entries or len(entries) != source.count('"Satellite"'):
        raise ValueError(f"Could not parse every satellite entry in {path}")
    return list(dict.fromkeys(entries))


class DirectoryLinks(HTMLParser):
    def __init__(self):
        super().__init__()
        self.links = []

    def handle_starttag(self, tag, attrs):
        if tag == "a":
            self.links.extend(value for key, value in attrs if key == "href" and value)


@lru_cache(maxsize=512)
def directory_names(url, timeout):
    """Read immediate directory children only; never crawl image archives."""
    # NOAA does not expose a root index; do not issue this known-invalid request.
    if url.rstrip("/") == BASE_URL.rstrip("/"):
        return ()
    try:
        parser = DirectoryLinks()
        parser.feed(fetch_bytes(url, timeout).decode("utf-8", errors="replace"))
        names = []
        for href in parser.links:
            target = urljoin(url, href)
            if urlsplit(target).netloc != urlsplit(url).netloc or not target.startswith(url):
                continue
            relative = target[len(url):].rstrip("/")
            if href.endswith("/") and re.fullmatch(r"[A-Za-z0-9_-]+", relative):
                names.append(relative)
        return tuple(dict.fromkeys(names))
    except (OSError, URLError, ValueError) as error:
        print(f"Discovery unavailable for {url}: {error}; using probe candidates", file=sys.stderr)
        return ()


def goes_test_configs(path, discover=False, timeout=5, discovery_budget=20,
                      extra_products=()):
    """Probe all product candidates for GOES19 full disk, independently of keep.csv."""
    products = set(GOES_CONUS_PRODUCTS) | set(GOES_EXTRA_PRODUCTS) | set(extra_products)
    products.update(("Airmass", "DMW", "DerivedMotionWinds", "DayCloudPhase",
                     "DayCloudType", "NightMicrophysics", "SnowIce", "RocketPlume",
                     "Vegetation", "LST", "SST", "TPW"))
    if path.exists():
        products.update(product for satellite, _, product in read_config(path)
                        if satellite.startswith("GOES"))
    if discover:
        products.update(directory_names(f"{BASE_URL}/GOES19/ABI/FD/",
                                        min(timeout, discovery_budget)))
    return [("GOES19", "FD", product) for product in sorted(products)]


def image_url(satellite, sector, product):
    if sector == "FD":
        return f"{BASE_URL}/{satellite}/ABI/FD/{product}/1808x1808.jpg"
    if sector == "CONUS":
        return f"{BASE_URL}/{satellite}/ABI/CONUS/{product}/1250x750.jpg"
    return f"{BASE_URL}/{satellite}/ABI/SECTOR/{sector.lower()}/{product}/latest.jpg"


def goes_urls(satellite, sector, product):
    """Try direct, regional and mesoscale layouts, including static-size aliases."""
    base = f"{BASE_URL}/{satellite}/ABI"
    prefixes = [image_url(satellite, sector, product).rsplit("/", 1)[0]]
    if sector not in ("FD", "CONUS"):
        prefixes.append(f"{base}/{sector}/{product}")
    if sector in ("M1", "M2", "MESO1", "MESO2"):
        number = sector[-1]
        prefixes.extend(f"{base}/{view}/{product}" for view in
                        (f"MESO/M{number}", f"MESO{number}", f"M{number}"))
    sizes = (("1808x1808", "5424x5424", "10848x10848", "678x678") if sector == "FD"
             else ("1250x750", "2500x1500", "5000x3000") if sector in ("CONUS", "PACUS")
             else ("600x600", "1200x1200", "2400x2400", "300x300", "1000x1000"))
    return list(dict.fromkeys([image_url(satellite, sector, product),
                              *(f"{prefix}/{filename}.jpg" for prefix in prefixes
                                for filename in ("latest", *sizes))]))


def job(provider, satellite, sector, product, url, requested_time="", **extra):
    name = re.sub(r"[^A-Za-z0-9_-]", "_", f"{satellite}_{sector}_{product}")
    return dict(provider=provider, satellite=satellite, sector=sector, product=product,
                url=url, requested_time=requested_time, filename=name + ".jpg", **extra)


def fetch_bytes(url, timeout, data=None, headers=None):
    request = Request(url, data=data, headers={
        "User-Agent": "GeoSatelliteView-satellite-test/2.0", **(headers or {})})
    with urlopen(request, timeout=timeout) as response:
        return response.read()


def resize_goes_image(path):
    resampling = getattr(Image, "Resampling", Image)
    with Image.open(path) as source:
        source.load()
        resized = source.convert("RGB").resize(GOES_IMAGE_SIZE, resampling.LANCZOS)
    resized.save(path, format="JPEG", quality=85, optimize=True)


@lru_cache(maxsize=4)
def wms_catalog(endpoint, timeout):
    url = endpoint + "?" + urlencode(dict(service="WMS", version="1.3.0", request="GetCapabilities"))
    root = ET.fromstring(fetch_bytes(url, timeout))
    catalog = {}

    def visit(layer, inherited_time="", inherited_styles=()):
        when = inherited_time
        styles = list(inherited_styles)
        for dimension in list(layer.findall("{*}Dimension")) + list(layer.findall("{*}Extent")):
            if dimension.get("name", "").lower() == "time":
                when = dimension.get("default", "") or when
        styles.extend(style.findtext("{*}Name") for style in layer.findall("{*}Style"))
        styles = tuple(dict.fromkeys(style for style in styles if style))
        name = layer.findtext("{*}Name")
        if name:
            catalog[name] = (when, styles)
        for child in layer.findall("{*}Layer"):
            visit(child, when, styles)

    for layer in root.findall("./{*}Capability/{*}Layer"):
        visit(layer)
    return catalog


def wms_times(endpoint, timeout):
    return {name: when for name, (when, _) in wms_catalog(endpoint, timeout).items()}


def wms_jobs(provider, args):
    endpoint, bbox, seeds = WMS_SOURCES[provider]
    catalog = {}
    if not args.dry_run and not args.no_discovery:
        try:
            catalog = wms_catalog(endpoint, args.discovery_timeout)
        except (OSError, URLError, ValueError, ET.ParseError) as error:
            if provider != "eumetsat":
                raise
            print(f"EUMETSAT discovery unavailable: {error}; using probe candidates", file=sys.stderr)
    layers = list(seeds)
    jobs = []
    for layer in dict.fromkeys(layers):
        when, styles = catalog.get(layer, ("", ()))
        if args.date and provider == "nasa_gibs":
            when = args.date.isoformat()
        width = args.size
        height = max(1, round(width * (bbox[3] - bbox[1]) / (bbox[2] - bbox[0])))
        for style in (("",) if provider == "eumetsat" else dict.fromkeys(("", *styles))):
            params = dict(service="WMS", version="1.3.0", request="GetMap", layers=layer,
                          styles=style, crs="CRS:84", bbox=",".join(map(str, bbox)),
                          width=width, height=height, format="image/jpeg")
            if when:
                params["time"] = when
            entry = job(provider, layer.split(":")[0] if provider == "eumetsat" else provider,
                        "region" if provider == "himawari" else "overview", layer,
                        endpoint + "?" + urlencode(params), when or "server default")
            if style:
                # Styles are independent configurations and need distinct files.
                suffix = style.encode("utf-8").hex()
                entry["filename"] = entry["filename"][:-4] + "_style_" + suffix + ".jpg"
            jobs.append(entry)
            if args.limit and len(jobs) >= args.limit:
                return jobs
    return jobs


def copernicus_jobs(args):
    end = args.date or datetime.now(timezone.utc).date()
    start = end - timedelta(days=30)
    interval = {"from": start.isoformat() + "T00:00:00Z", "to": end.isoformat() + "T23:59:59Z"}
    payload = {
        "input": {"bounds": {"bbox": args.bbox, "properties": {
            "crs": "http://www.opengis.net/def/crs/OGC/1.3/CRS84"}},
            "data": [{"type": "sentinel-2-l2a", "dataFilter": {
                "timeRange": interval, "mosaickingOrder": "mostRecent"}}]},
        "output": {"width": args.size, "height": args.size,
                   "responses": [{"identifier": "default", "format": {"type": "image/jpeg"}}]},
        "evalscript": '//VERSION=3\nfunction setup(){return {input:["B02","B03","B04"],'
                      'output:{bands:3}};}\nfunction evaluatePixel(s){return [2.5*s.B04,2.5*s.B03,2.5*s.B02];}',
    }
    entry = job("copernicus", "Sentinel2", "bbox", "TrueColor", COPERNICUS,
                f"{start}/{end} (mostRecent mosaic)", data=json.dumps(payload).encode())
    if args.dry_run:
        return [entry]
    client_id = os.environ.get("CDSE_CLIENT_ID")
    secret = os.environ.get("CDSE_CLIENT_SECRET")
    if not client_id or not secret:
        entry["skip"] = "Set CDSE_CLIENT_ID and CDSE_CLIENT_SECRET to download Sentinel-2 imagery"
        return [entry]
    token = json.loads(fetch_bytes(TOKEN_URL, args.timeout, urlencode({
        "grant_type": "client_credentials", "client_id": client_id,
        "client_secret": secret}).encode(), {"Content-Type": "application/x-www-form-urlencoded"}))
    entry["headers"] = {"Content-Type": "application/json", "Authorization": "Bearer " + token["access_token"]}
    return [entry]


def download(entry, output, timeout):
    # Missing aliases are normal during exhaustive probing. Network failures are
    # retried by download_one, but do not multiply them by every URL variant.
    for url in entry.get("candidate_urls", (entry["url"],)):
        row = download_one({**entry, "url": url}, output, timeout)
        if row["status"] != "failed":
            return row
        if not row.pop("missing_alias", False):
            return row
    return row


def download_one(entry, output, timeout):
    url, filename = entry["url"], entry["filename"]
    destination = output / filename
    temporary = destination.with_suffix(".jpg.part")
    row = {key: entry.get(key, "") for key in FIELDS}
    row.update(status="failed", error="")
    if entry.get("skip"):
        row.update(status="skipped", error=entry["skip"])
        return row
    for attempt in range(3):
        try:
            request = Request(url, data=entry.get("data"), headers={
                "User-Agent": "GeoSatelliteView-satellite-test/2.0", **entry.get("headers", {})})
            with urlopen(request, timeout=timeout) as response:
                if response.status != 200:
                    raise ValueError(f"Unexpected HTTP status {response.status}")
                start = response.read(3)
                if start != b"\xff\xd8\xff":
                    raise ValueError("Response is not a JPEG")
                with temporary.open("wb") as image:
                    image.write(start)
                    while chunk := response.read(64 * 1024):
                        image.write(chunk)
            if entry["provider"] == "goes":
                resize_goes_image(temporary)
            temporary.replace(destination)
            row["status"] = "saved"
            row["error"] = ""
            row["downloaded_at_utc"] = datetime.now(timezone.utc).isoformat()
            return row
        except (OSError, URLError, ValueError) as error:
            temporary.unlink(missing_ok=True)
            row["error"] = str(error)
            if isinstance(error, HTTPError) and error.code not in (429, 500, 502, 503, 504):
                row["missing_alias"] = error.code in (404, 410)
                break
            if isinstance(error, ValueError) or attempt == 2:
                break
            time.sleep(2 ** attempt)
    return row


def positive_int(value):
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be at least 1")
    return number


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=HERE.parent / "config/satellites.cpp")
    parser.add_argument("--output", type=Path, default=HERE / "images")
    parser.add_argument("--workers", type=positive_int, default=4)
    parser.add_argument("--timeout", type=positive_int, default=60, help="socket timeout in seconds")
    parser.add_argument("--discovery-timeout", type=positive_int, default=5,
                        help="short timeout for directory/WMS discovery")
    parser.add_argument("--discovery-budget", type=positive_int, default=20,
                        help="total GOES discovery time budget in seconds")
    parser.add_argument("--no-discovery", action="store_true", help="immediately probe candidates without fetching catalogs")
    parser.add_argument("--goes-products", nargs="+", default=[], help="additional case-sensitive product names to cross-probe")
    parser.add_argument("--eumetsat-namespaces", nargs="+", default=[])
    parser.add_argument("--eumetsat-products", nargs="+", default=[])
    parser.add_argument("--eumetsat-layers", nargs="+", default=[], help="additional exact namespace:layer names")
    parser.add_argument("--providers", nargs="+", choices=PROVIDERS, default=["goes"])
    parser.add_argument("--limit", type=positive_int, help="first N images PER provider")
    parser.add_argument("--size", type=positive_int, default=1024,
                        help="new API image width (max 2500); GOES is always saved at 500x500")
    parser.add_argument("--date", type=date.fromisoformat, help="YYYY-MM-DD for GIBS daily imagery and Sentinel search end")
    parser.add_argument("--bbox", type=float, nargs=4, default=[-0.5, 51.3, 0.3, 51.8],
                        metavar=("WEST", "SOUTH", "EAST", "NORTH"), help="Sentinel region; defaults to London")
    parser.add_argument("--dry-run", action="store_true", help="list URLs without downloading or writing files")
    args = parser.parse_args()
    west, south, east, north = args.bbox
    if not all(map(math.isfinite, args.bbox)) or not (-180 <= west < east <= 180 and -90 <= south < north <= 90):
        parser.error("bbox must satisfy -180 <= WEST < EAST <= 180 and -90 <= SOUTH < NORTH <= 90")
    if args.size > 2500:
        parser.error("--size must be at most 2500")
    totals = dict(saved=0, failed=0, skipped=0)
    for provider in dict.fromkeys(args.providers):
        output = args.output / provider
        print(f"Preparing {provider}: {output.resolve()}", flush=True)
        try:
            if provider == "goes":
                configs = goes_test_configs(args.config, discover=not args.dry_run and not args.no_discovery,
                                            timeout=args.discovery_timeout, discovery_budget=args.discovery_budget,
                                            extra_products=args.goes_products)
                if args.limit:
                    configs = configs[:args.limit]
                entries = [job(provider, *entry, image_url(*entry), candidate_urls=goes_urls(*entry)) for entry in configs]
            elif provider == "copernicus":
                entries = copernicus_jobs(args)
            else:
                entries = wms_jobs(provider, args)
        except (OSError, URLError, ValueError, ET.ParseError, KeyError) as error:
            # One API being down must not stop the other sources.
            entries = [job(provider, "", "", "setup", "", skip=str(error))]
            entries[0]["setup_failed"] = True
        if args.dry_run:
            for entry in entries:
                print(f"{provider}/{entry['filename']}: {entry['url']} {entry.get('skip', '')}")
                for alternate in entry.get("candidate_urls", [])[1:]:
                    print(f"  fallback: {alternate}")
                if entry.get("setup_failed"):
                    totals["failed"] += 1
            continue
        output.mkdir(parents=True, exist_ok=True)
        print(f"Probing {len(entries)} {provider} configurations", flush=True)
        with (output / "report.csv").open("w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=FIELDS)
            writer.writeheader()
            with ThreadPoolExecutor(max_workers=args.workers) as pool:
                futures = {pool.submit(download, entry, output, args.timeout): entry for entry in entries}
                for index, future in enumerate(as_completed(futures), 1):
                    row = future.result()
                    if futures[future].get("setup_failed"):
                        row["status"] = "failed"
                    totals[row["status"]] += 1
                    writer.writerow(row)
                    handle.flush()
                    print(f"[{provider} {index}/{len(entries)}] {row['status'].upper()} {row['filename']} {row['error']}", flush=True)
                    if row["status"] == "saved":
                        print(f"  Saved to: {(output / row['filename']).resolve()}", flush=True)
    print(f"Done: {totals['saved']} saved, {totals['failed']} failed, {totals['skipped']} skipped.")
    # Missing credentials are visible and yield a distinct incomplete-run status.
    return 1 if totals["failed"] else (2 if totals["skipped"] else 0)


if __name__ == "__main__":
    sys.exit(main())
