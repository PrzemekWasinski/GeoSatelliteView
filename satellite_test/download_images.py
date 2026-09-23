#!/usr/bin/env python3
"""Download sample imagery from GOES, EUMETView, Himawari, GIBS and Copernicus."""
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
from urllib.parse import urlencode
import xml.etree.ElementTree as ET

from PIL import Image


HERE = Path(__file__).resolve().parent
BASE_URL = "https://cdn.star.nesdis.noaa.gov"
EUMETVIEW = "https://view.eumetsat.int/geoserver/wms"
GIBS = "https://gibs.earthdata.nasa.gov/wms/epsg4326/best/wms.cgi"
COPERNICUS = "https://sh.dataspace.copernicus.eu/process/v1"
TOKEN_URL = "https://identity.dataspace.copernicus.eu/auth/realms/CDSE/protocol/openid-connect/token"
PROVIDERS = ("goes", "eumetsat", "himawari", "nasa_gibs", "copernicus")
# A small, editable selection for new sources; GOES still uses every C++ entry.
WMS_SOURCES = {
    "eumetsat": (EUMETVIEW, (-65, -65, 65, 65), [
        "mtg_fd:rgb_geocolour", "mtg_fd:rgb_truecolour", "mtg_fd:ir105_hrfi",
        "mtg_fd:rgb_cloudphase", "mtg_fd:rgb_dust", "msg_fes:rgb_airmass",
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


def read_config(path):
    source = re.sub(r'/\*.*?\*/|//[^\n]*', '', path.read_text(), flags=re.S)
    entries = ENTRY.findall(source)
    if not entries or len(entries) != source.count('"Satellite"'):
        raise ValueError(f"Could not parse every satellite entry in {path}")
    return list(dict.fromkeys(entries))


def goes_test_configs(path):
    # The production configuration may also contain non-GOES providers.
    configs = [entry for entry in read_config(path) if entry[0].startswith("GOES")]
    for satellite, sectors in GOES_SECTORS.items():
        # Existing regional entries supply the product set for each satellite.
        reference_sector = "AK" if satellite == "GOES18" else "PR"
        products = [product for sat, sector, product in configs
                    if (sat, sector) == (satellite, reference_sector)]
        for sector in sectors:
            configs.extend((satellite, sector, product) for product in products)
        configs.extend((satellite, "CONUS", product) for product in GOES_CONUS_PRODUCTS)
    return list(dict.fromkeys(configs))


def image_url(satellite, sector, product):
    if sector == "FD":
        return f"{BASE_URL}/{satellite}/ABI/FD/{product}/1808x1808.jpg"
    if sector == "CONUS":
        return f"{BASE_URL}/{satellite}/ABI/CONUS/{product}/1250x750.jpg"
    return f"{BASE_URL}/{satellite}/ABI/SECTOR/{sector.lower()}/{product}/latest.jpg"


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
def wms_times(endpoint, timeout):
    url = endpoint + "?" + urlencode(dict(service="WMS", version="1.3.0", request="GetCapabilities"))
    root = ET.fromstring(fetch_bytes(url, timeout))
    layers = {}
    for layer in root.findall(".//{*}Layer"):
        name = layer.findtext("{*}Name")
        if name:
            for dimension in layer.findall("{*}Dimension"):
                if dimension.get("name") == "time":
                    layers[name] = dimension.get("default", "")
    return layers


def wms_jobs(provider, args):
    endpoint, bbox, layers = WMS_SOURCES[provider]
    # Dry runs stay entirely offline. Actual requests pin the advertised latest time.
    times = {} if args.dry_run else wms_times(endpoint, args.timeout)
    if args.limit:
        layers = layers[:args.limit]
    jobs = []
    for layer in layers:
        if not args.dry_run and layer not in times:
            raise ValueError(f"Layer missing from API capabilities: {layer}")
        when = times.get(layer, "")
        if args.date and provider == "nasa_gibs":
            when = args.date.isoformat()
        width = args.size
        height = max(1, round(width * (bbox[3] - bbox[1]) / (bbox[2] - bbox[0])))
        params = dict(service="WMS", version="1.3.0", request="GetMap", layers=layer,
                      styles="", crs="CRS:84", bbox=",".join(map(str, bbox)),
                      width=width, height=height, format="image/jpeg")
        if when:
            params["time"] = when
        jobs.append(job(provider, layer.split(":")[0] if provider == "eumetsat" else provider,
                        "region" if provider == "himawari" else "overview", layer,
                        endpoint + "?" + urlencode(params), when or "server default"))
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
    parser.add_argument("--providers", nargs="+", choices=PROVIDERS, default=list(PROVIDERS))
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
                configs = goes_test_configs(args.config)
                if args.limit:
                    configs = configs[:args.limit]
                entries = [job(provider, *entry, image_url(*entry)) for entry in configs]
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
                if entry.get("setup_failed"):
                    totals["failed"] += 1
            continue
        output.mkdir(parents=True, exist_ok=True)
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
    print(f"Done: {totals['saved']} saved, {totals['failed']} failed, {totals['skipped']} skipped.")
    # Missing credentials are visible and yield a distinct incomplete-run status.
    return 1 if totals["failed"] else (2 if totals["skipped"] else 0)


if __name__ == "__main__":
    sys.exit(main())
