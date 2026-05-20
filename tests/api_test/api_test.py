import requests
import os
import itertools
import re
import logging
from concurrent.futures import ThreadPoolExecutor, as_completed

DOWNLOAD_IMAGES = False  # True: download and store images  False: log results only

#All possible parameter values
GOES16_SECTORS = ["FD", "CONUS", "AK", "HI", "PR", "GS"]  # GOES-East
GOES18_SECTORS = ["FD", "AK", "HI", "NP", "PNW", "PSW", "WUS", "EEP"]  # GOES-West
#GOES19_SECTORS = ["FD", "CONUS", "AK", "HI", "PR", "GS"]  #GOES-East

COMPOSITE_PRODUCTS = [
    "GEOCOLOR", "AirMass", "DayConvection", "DayLandCloudFire",
    "DayNightCloudMicroCombo", "Dust", "FireTemperature", "Sandwich",
]
BAND_PRODUCTS = [f"{i:02d}" for i in range(1, 17)]  # "01" to "16"

BASE_URL    = "https://cdn.star.nesdis.noaa.gov"
OUTPUT_DIR  = os.path.join(os.path.dirname(__file__), "api_test")
LOG_FILE    = os.path.join(os.path.dirname(__file__), "api_test.log")
MAX_WORKERS = 12

SECTORS_BY_SAT = {"16": GOES16_SECTORS, "18": GOES18_SECTORS}
#SECTORS_BY_SAT = {"16": GOES16_SECTORS, "18": GOES18_SECTORS, "19": GOES19_SECTORS}

logging.basicConfig(
    filename=LOG_FILE,
    filemode="w",
    level=logging.CRITICAL,
    format="%(asctime)s  %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)


def latest_band_url(satellite, sector, band):
    if sector == "FD":
        dir_url = f"{BASE_URL}/GOES{satellite}/ABI/FD/{band}/"
    else:
        dir_url = f"{BASE_URL}/GOES{satellite}/ABI/SECTOR/{sector.lower()}/{band}/"

    try:
        r = requests.get(dir_url, timeout=10)
        if r.status_code != 200:
            return None
        #filenames format: 20261291350_GOES16-ABI-FD-02-10848x10848.jpg
        files = re.findall(r'(\d+_GOES\d+-ABI-[^"]+\.jpg)"', r.text)
        if not files:
            return None
        return dir_url + sorted(files)[-1]
    except requests.RequestException:
        return None


def fetch(satellite, sector, product):
    is_band = re.match(r'^\d{2}$', product) is not None

    if is_band:
        url = latest_band_url(satellite, sector, product)
        if url is None:
            return (satellite, sector, product, 404, "no listing")
    else:
        if sector == "FD":
            url = f"{BASE_URL}/GOES{satellite}/ABI/FD/{product}/latest.jpg"
        else:
            url = f"{BASE_URL}/GOES{satellite}/ABI/SECTOR/{sector.lower()}/{product}/latest.jpg"

    if DOWNLOAD_IMAGES:
        filename = f"GOES{satellite}_{sector}_{product}.jpg"
        dest = os.path.join(OUTPUT_DIR, filename)
        try:
            r = requests.get(url, stream=True, timeout=20)
        except requests.RequestException as e:
            return (satellite, sector, product, "ERR", str(e))
        if r.status_code != 200:
            return (satellite, sector, product, r.status_code, "skipped")
        with open(dest, "wb") as f:
            for chunk in r.iter_content(chunk_size=8192):
                f.write(chunk)
        kb = os.path.getsize(dest) / 1024
        return (satellite, sector, product, 200, f"{kb:.0f} KB  -> {filename}")
    else:
        try:
            r = requests.head(url, timeout=20)
        except requests.RequestException as e:
            return (satellite, sector, product, "ERR", str(e))
        return (satellite, sector, product, r.status_code, url)


if __name__ == "__main__":
    if DOWNLOAD_IMAGES:
        os.makedirs(OUTPUT_DIR, exist_ok=True)

    combos = []
    for sat, sectors in SECTORS_BY_SAT.items():
        for sector, product in itertools.product(sectors, COMPOSITE_PRODUCTS + BAND_PRODUCTS):
            combos.append((sat, sector, product))

    total = len(combos)
    print(f"Testing {total} combinations ({MAX_WORKERS} parallel)...\n")
    ok = fail = 0
    with ThreadPoolExecutor(max_workers=MAX_WORKERS) as pool:
        futures = {pool.submit(fetch, *c): c for c in combos}
        for i, future in enumerate(as_completed(futures), 1):
            sat, sec, prod, status, msg = future.result()
            label = f"GOES{sat} {sec:<6} {prod:<25}"
            if status == 200:
                ok += 1
                print(f"[{i:3}/{total}] OK    {label}  {msg}")
                logging.critical("SUCCESS  GOES%s %s %s", sat, sec, prod)
            else:
                fail += 1
                print(f"[{i:3}/{total}] SKIP  {label}  {status}")
                logging.critical("FAILED   GOES%s %s %s", sat, sec, prod)
    if DOWNLOAD_IMAGES:
        print(f"\nDone — {ok} saved, {fail} skipped -> {OUTPUT_DIR}")
    else:
        print(f"\nDone — {ok} available, {fail} skipped -> {LOG_FILE}")
