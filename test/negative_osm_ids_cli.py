#!/usr/bin/env python3
"""Exercise the patched build-tiles CLI with fresh fixtures from negative_osm_ids."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run(command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=45, check=False)
    print(f"$ {command!r}\nexit={result.returncode}\n{result.stdout}{result.stderr}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture-generator", type=Path, required=True)
    parser.add_argument("--build-tiles", type=Path, required=True)
    args = parser.parse_args()
    generator = args.fixture_generator.resolve(strict=True)
    builder = args.build_tiles.resolve(strict=True)

    with tempfile.TemporaryDirectory(prefix="valhalla-negative-osm-cli-") as tmp:
        root = Path(tmp)
        fixtures = root / "fixtures"
        generated = run([str(generator), "--generate-cli-fixtures", str(fixtures)])
        if generated.returncode != 0:
            raise AssertionError("Fixture generation failed")

        def check(name, stage, expected=None, input_uri=None):
            tile_dir = root / (name + "-tiles")
            config = root / (name + ".json")
            config.write_text(json.dumps({"mjolnir": {"tile_dir": str(tile_dir),
                                                        "concurrency": 4,
                                                        "id_table_size": 1000}}), encoding="utf-8")
            source = input_uri or str(fixtures / (name + ".osm.pbf"))
            result = run([str(builder), "--config", str(config), "--end", stage, source])
            output = result.stdout + result.stderr
            if expected is None:
                if result.returncode != 0 or "Finished with 1 routable ways" not in output:
                    raise AssertionError(f"Positive control failed: {name}")
            elif (result.returncode != 1 or "Tile building failed: Negative OSM " + expected not in output
                  or "renumber OSM IDs and references" not in output):
                raise AssertionError(f"Expected graceful negative-ID failure: {name}")

        check("positive", "parsenodes")
        check("negative_way", "parseways", "way ID -1")
        check("negative_ref", "parseways", "way node reference ID -2")
        check("negative_node", "parsenodes", "node ID -1")
        check("negative_relation", "parserelations", "relation ID -1")
        check("negative_member", "parserelations", "relation member ID -2")
        check("negative_many", "parseways", "way node reference ID -2")
        if sys.platform.startswith("linux") and shutil.which("curl"):
            # libosmium uses curl as a subprocess for file: URLs, without a network request here.
            # PBF data blocks use libosmium's default zlib. The small input may be read ahead.
            check("negative_many_url", "parseways", "way node reference ID -2",
                  (fixtures / "negative_many.osm.pbf").as_uri())
        else:
            print("Skipping file: URL check: Linux and curl are required")


if __name__ == "__main__":
    main()
