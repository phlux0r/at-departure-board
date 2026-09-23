#!/usr/bin/env python3
"""Probe AT's realtime API for vehicle occupancy (the AT Mobile "people" icon).

Answers three questions the docs cannot, in one run:

  1. Which realtime paths actually serve vehicle positions? The naming is not
     guessable - `/tripupdates` works but `/trip-updates` is a 404 - so this
     tries the candidates and reports what each one does.
  2. Does AT populate `occupancy_status` at all, and with which values? The
     field is optional in GTFS-Realtime and "experimental" in the spec, so
     being in the schema says nothing about being in the feed.
  3. What is the coverage - which routes and modes carry it, and what would
     it cost the board in bytes?

Nothing here runs on the board. It exists because docs/at-api-notes.md is
written from probes like this rather than from AT's documentation, which has
differed from the live API in five places so far.

    export AT_API_KEY=...            # or it reads src/secrets.h
    python tools/probe_occupancy.py
    python tools/probe_occupancy.py --tripid 1141170684-20260901160500-2
    python tools/probe_occupancy.py --save test/fixtures/vehiclepositions.json

The key is only ever sent to api.at.govt.nz, and is never printed.
"""

import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.request

BASE = "https://api.at.govt.nz"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Ordered so a total failure is diagnosable: if even the control 404s or 401s,
# the key or the subscription is the problem, not the path.
# GTFS-Realtime's OccupancyStatus, for reading the output without looking it
# up. The raw value is printed alongside, so a feed that returns strings, or
# numbering this does not expect, shows up as a mismatch rather than a wrong
# label quietly standing in for the truth.
OCCUPANCY = {
    0: "EMPTY",
    1: "MANY_SEATS_AVAILABLE",
    2: "FEW_SEATS_AVAILABLE",
    3: "STANDING_ROOM_ONLY",
    4: "CRUSHED_STANDING_ROOM_ONLY",
    5: "FULL",
    6: "NOT_ACCEPTING_PASSENGERS",
    7: "NO_DATA_AVAILABLE",
    8: "NOT_BOARDABLE",
}

CANDIDATES = [
    ("/realtime/legacy/tripupdates", "control - what the board already uses"),
    ("/realtime/legacy/", "combined feed - at-api-notes says it carries vehicle entities"),
    ("/realtime/legacy/vehiclepositions", "the likely one, by symmetry with tripupdates"),
    ("/realtime/legacy/vehicle-positions", "hyphenated, expected to 404 like trip-updates"),
]


def read_key():
    """Env first, then src/secrets.h - which is gitignored and already has it."""
    key = os.environ.get("AT_API_KEY")
    if key:
        return key, "AT_API_KEY"
    path = os.path.join(ROOT, "src", "secrets.h")
    try:
        with open(path) as f:
            m = re.search(r'#define\s+AT_API_KEY\s+"([^"]+)"', f.read())
    except OSError:
        m = None
    if m and m.group(1) != "your-key":
        return m.group(1), "src/secrets.h"
    sys.exit("no API key: set AT_API_KEY, or fill in src/secrets.h")


def get(path, key, params=None):
    """Returns (status, body_bytes, parsed_or_None)."""
    url = BASE + path + (("?" + params) if params else "")
    req = urllib.request.Request(url, headers={"Ocp-Apim-Subscription-Key": key})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            body = r.read()
            status = r.status
    except urllib.error.HTTPError as e:
        return e.code, e.read(), None
    except urllib.error.URLError as e:
        print(f"  ! {path}: {e.reason}")
        return None, b"", None
    try:
        return status, body, json.loads(body)
    except ValueError:
        return status, body, None


def walk(node, path=""):
    """Every (dotted path, value) in the document. The probe searches for
    occupancy rather than assuming where it sits: if AT nests it somewhere
    unexpected, an assumption here would report a false negative."""
    if isinstance(node, dict):
        for k, v in node.items():
            yield from walk(v, f"{path}.{k}" if path else k)
    elif isinstance(node, list):
        for i, v in enumerate(node):
            yield from walk(v, f"{path}[{i}]")
    else:
        yield path, node


def entities(doc):
    if not isinstance(doc, dict):
        return []
    ents = doc.get("response", {}).get("entity", [])
    return ents if isinstance(ents, list) else [ents]


def summarise(doc, body):
    """Occupancy coverage, and what kinds of entity are in the feed."""
    ents = entities(doc)
    kinds = {}
    for e in ents:
        for k in ("trip_update", "vehicle", "alert"):
            if isinstance(e, dict) and e.get(k) is not None:
                kinds[k] = kinds.get(k, 0) + 1

    occ_fields, values, by_route, on_trip = {}, {}, {}, {}
    for e in ents:
        found = None
        for p, v in walk(e):
            if "occupancy" in p.lower():
                occ_fields[p.split(".", 1)[-1]] = occ_fields.get(p.split(".", 1)[-1], 0) + 1
                if "status" in p.lower():
                    found = v
                    values[str(v)] = values.get(str(v), 0) + 1

        # ONLY vehicle entities. A trip_update carries trip.route_id too, and
        # counting those inflates the denominator with entities that could
        # never have reported occupancy in the first place - which makes every
        # route look far worse covered than it is.
        if not (isinstance(e, dict) and e.get("vehicle") is not None):
            continue
        route = None
        for p, v in walk(e):
            if p.endswith("trip.route_id"):
                route = v
                break

        # Split on whether the vehicle is on a trip at all. A bus between runs
        # has no route and reports no occupancy, and counting those against
        # coverage answers a question nobody asked: the board only ever shows
        # vehicles that are serving a trip it is watching.
        if route is None:
            on_trip["off"] = on_trip.get("off", 0) + 1
            if found is not None:
                on_trip["off_with_occ"] = on_trip.get("off_with_occ", 0) + 1
            continue
        on_trip["on"] = on_trip.get("on", 0) + 1
        if found is not None:
            on_trip["on_with_occ"] = on_trip.get("on_with_occ", 0) + 1
        seen, total = by_route.get(route, (0, 0))
        by_route[route] = (seen + (1 if found is not None else 0), total + 1)

    return {
        "bytes": len(body),
        "entities": len(ents),
        "kinds": kinds,
        "occupancy_fields": occ_fields,
        "values": values,
        "by_route": by_route,
        "on_trip": on_trip,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tripid", help="restrict to these trip ids (comma separated), "
                                     "as the board does; default is the whole feed")
    ap.add_argument("--route", help="only report these routes (comma separated short "
                                    "names or ids, e.g. 931,97R) - the full list is "
                                    "truncated, and what matters is your own stops")
    ap.add_argument("--save", metavar="FILE", help="write the first response that "
                                                   "carries occupancy, as a fixture")
    args = ap.parse_args()

    key, src = read_key()
    print(f"key from {src}, {len(key)} chars\n")
    params = f"tripid={args.tripid}" if args.tripid else None
    if params:
        print(f"filtered: {params}\n")

    saved = False
    reached = False
    for path, why in CANDIDATES:
        status, body, doc = get(path, key, params)
        if status is None:
            continue
        reached = True
        if status != 200:
            print(f"{path}\n  HTTP {status}  ({why})")
            if status in (401, 403):
                print("  -> key rejected, or not subscribed to the Realtime product")
            print()
            continue

        s = summarise(doc, body)
        print(f"{path}\n  HTTP 200  {s['bytes']:,} bytes  {s['entities']} entities  ({why})")
        print(f"  entity kinds: {s['kinds'] or 'none recognised'}")

        if not s["occupancy_fields"]:
            print("  occupancy: ABSENT - no key matching /occupancy/ anywhere in the feed")
        else:
            print(f"  occupancy fields: {s['occupancy_fields']}")
            print("  occupancy_status values:")
            for raw, n in sorted(s["values"].items()):
                try:
                    name = OCCUPANCY.get(int(raw), "not in the spec's enum")
                except ValueError:
                    name = "a string, not the spec's integer enum"
                print(f"    {raw:<4} x{n:<5} {name}")
            t = s["on_trip"]
            on, on_occ = t.get("on", 0), t.get("on_with_occ", 0)
            off, off_occ = t.get("off", 0), t.get("off_with_occ", 0)
            if on:
                print(f"  vehicles ON a trip:  {on_occ}/{on} report occupancy "
                      f"({on_occ / on * 100:.0f}%)  <- what the board would see")
            if off:
                print(f"  vehicles not on a trip: {off_occ}/{off} "
                      "(between runs; the board never shows these)")
            with_occ = sum(1 for seen, _ in s["by_route"].values() if seen)
            print(f"  routes with occupancy: {with_occ} of {len(s['by_route'])}"
                  "   (denominators below are VEHICLE entities only)")

            rows = sorted(s["by_route"].items())
            if args.route:
                # Match on the short name too: a route_id is "931-203", and the
                # number on the front of the bus is what anyone actually knows.
                wanted = [w.strip().lower() for w in args.route.split(",") if w.strip()]
                rows = [(r, v) for r, v in rows
                        if any(str(r).lower() == w or str(r).lower().startswith(w + "-")
                               for w in wanted)]
                missing = [w for w in wanted
                           if not any(str(r).lower() == w or str(r).lower().startswith(w + "-")
                                      for r, _ in rows)]
                if missing:
                    print(f"    (no vehicles running on: {', '.join(missing)})")
            shown = rows if args.route else rows[:15]
            for route, (seen, total) in shown:
                mark = "none" if not seen else f"{seen}/{total}"
                print(f"    {route:<16} {mark}")
            if not args.route and len(rows) > 15:
                print(f"    ... {len(rows) - 15} more routes (use --route to pick)")
            if args.save and not saved:
                with open(args.save, "wb") as f:
                    f.write(body)
                print(f"  saved to {args.save}")
                saved = True
        print()

    if not reached:
        sys.exit("nothing reached api.at.govt.nz - check the network, not the key")
    print("Paste what matters into docs/at-api-notes.md - that file is the record "
          "of what this API really does, and it is already right where AT's own "
          "docs are wrong.")


if __name__ == "__main__":
    main()
