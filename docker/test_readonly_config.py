"""Exercise scripted-image startup with mocked mount and external commands.

Run with Python, bash and jq. A real read-only Docker mount is tested in docker/test.sh.
"""

import json
import os
import pathlib
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
BASH = shutil.which("bash")
JQ = shutil.which("jq")
if not BASH or not JQ:
    raise SystemExit("These shell integration tests require bash and jq on PATH")


def unix(path):
    s = pathlib.Path(path).resolve().as_posix()
    return "/" + s[0].lower() + s[2:] if os.name == "nt" else s


def put(path, text):
    path.write_text(text, encoding="utf-8", newline="\n")
    path.chmod(0o755)


def run_variant(
    label,
    mode,
    update,
    force=False,
    tar=True,
    config=True,
    default_speed=True,
    repeat=False,
    transit_force=False,
    command="build_tiles",
    archive_setting="True",
    serve=True,
):
    with tempfile.TemporaryDirectory(prefix="valhalla-ro-") as tmp:
        root = pathlib.Path(tmp)
        scripts = root / "scripts"
        fixture = root / "custom"
        binpath = root / "bin"
        for p in (scripts, fixture, binpath):
            p.mkdir()
        (fixture / "transit_tiles").mkdir()
        if tar:
            (fixture / "valhalla_tiles.tar").write_bytes(b"existing archive")
        else:
            (fixture / "valhalla_tiles").mkdir()
            (fixture / "valhalla_tiles/tile.gph").write_bytes(b"existing tile")
        (fixture / "some.pbf").write_bytes(b"fixture-not-read-because-no-build")
        initial = b'{"mjolnir":{"tile_dir":"original", "concurrency":42},"additional_data":{}}\n'
        if config:
            (fixture / "valhalla.json").write_bytes(initial)
        for name in ("helpers.sh", "configure_valhalla.sh", "docker-entrypoint.sh"):
            raw = (ROOT / "docker/scripts" / name).read_text(encoding="utf8")
            raw = raw.replace(". /valhalla/scripts/helpers.sh", '. "${TEST_SCRIPTS}/helpers.sh"')
            raw = raw.replace(
                "/valhalla/scripts/configure_valhalla.sh ", '"${TEST_SCRIPTS}/configure_valhalla.sh" '
            )
            raw = raw.replace('CUSTOM_FILES="/custom_files"', 'CUSTOM_FILES="${TEST_ROOT}"')
            raw = raw.replace('GTFS_DIR="/gtfs_feeds"', 'GTFS_DIR="${TEST_ROOT}/gtfs_feeds"')
            put(scripts / name, raw)
        put(
            binpath / "findmnt",
            '#!/usr/bin/env bash\n[[ "$MOCK_MOUNT" == fail ]] && exit 1\n[[ "$MOCK_MOUNT" == ro ]] && echo ro,relatime || echo rw,relatime\n',
        )
        put(
            binpath / "sponge",
            '#!/usr/bin/env bash\necho sponge >> "$TEST_LOG"\nif [[ "$MOCK_MOUNT" == ro ]]; then cat >/dev/null; echo readonly >&2; exit 1; fi\ncat > "$1"\n',
        )
        put(
            binpath / "valhalla_service",
            '#!/usr/bin/env bash\nif [[ "${1:-}" == --version ]]; then echo test-version; else echo service >> "$TEST_LOG"; fi\n',
        )
        put(
            binpath / "valhalla_build_config",
            '#!/usr/bin/env bash\necho config >> "$TEST_LOG"\nif [[ "${1:-}" != --output ]]; then echo \'{"mjolnir":{},"additional_data":{}}\'; fi\n',
        )
        put(binpath / "valhalla_build_tiles", '#!/usr/bin/env bash\necho tiles >> "$TEST_LOG"\n')
        put(binpath / "valhalla_build_extract", '#!/usr/bin/env bash\necho extract >> "$TEST_LOG"\n')
        bash_env = root / "bash-env.sh"
        put(
            bash_env,
            """curl() {
  echo curl >> "$TEST_LOG"
  local out=""
  while (( $# )); do if [[ "$1" == -o || "$1" == --output ]]; then out="$2"; shift; fi; shift; done
  if [[ -n "$out" && "$out" != /dev/null ]]; then echo '{}' > "$out"; fi
}
find() {
  if [[ "$*" == *'-exec chmod'* ]]; then
    echo chmod >> "$TEST_LOG"
    [[ "$MOCK_MOUNT" != ro ]]
  else
    command find "$@"
  fi
}
""",
        )
        log = root / "calls.log"
        env = os.environ.copy()
        env.update(
            {
                "PATH": unix(binpath) + ":" + unix(pathlib.Path(JQ).parent) + ":/usr/bin:/bin",
                "TEST_ROOT": unix(fixture),
                "TEST_SCRIPTS": unix(scripts),
                "TEST_LOG": unix(log),
                "MOCK_MOUNT": mode,
                "BASH_ENV": unix(bash_env),
                "path_extension": "",
                "tileset_name": "valhalla_tiles",
                "traffic_name": "",
                "force_rebuild": "True" if force else "False",
                "build_admins": "False",
                "build_time_zones": "False",
                "build_transit": "Force" if transit_force else "False",
                "build_elevation": "False",
                "use_tiles_ignore_pbf": "True",
                "update_existing_config": str(update),
                "use_default_speeds_config": str(default_speed),
                "server_threads": "1",
                "tile_urls": "",
                "build_tar": archive_setting,
                "serve_tiles": str(serve),
                "default_speeds_config_url": "https://example.invalid/speeds",
            }
        )
        proc = subprocess.run(
            [BASH, unix(scripts / "docker-entrypoint.sh"), command],
            env=env,
            text=True,
            capture_output=True,
            timeout=20,
            check=False,
        )
        if repeat and proc.returncode == 0:
            again = subprocess.run(
                [BASH, unix(scripts / "docker-entrypoint.sh"), command],
                env=env,
                text=True,
                capture_output=True,
                timeout=20,
                check=False,
            )
            if again.returncode != 0:
                raise AssertionError((again.stdout, again.stderr))
        calls = log.read_text().splitlines() if log.exists() else []
        after = (
            (fixture / "valhalla.json").read_bytes() if (fixture / "valhalla.json").exists() else None
        )
        print(
            label,
            "exit",
            proc.returncode,
            "calls",
            calls,
            "hashfile",
            (fixture / "file_hashes.txt").exists(),
        )
        print(proc.stdout[-600:], proc.stderr[-300:])
        return (
            proc.returncode,
            calls,
            initial,
            after,
            (fixture / "file_hashes.txt").exists(),
            proc.stdout + proc.stderr,
        )


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    for update in (False, True):
        c, calls, initial, actual, hashfile, _ = run_variant(
            "read-only repeated startup", "ro", update, repeat=True
        )
        check(
            c == 0 and calls == ["service", "service"] and initial == actual and not hashfile,
            "Read-only startup changed files or called a writer",
        )
    c, calls, initial, actual, hashfile, output = run_variant(
        "read-only directory tiles", "ro", True, tar=False
    )
    check(
        c == 0 and calls == ["service"] and "Skipping archive creation" in output,
        "Read-only directory tiles did not start without archive creation",
    )
    c, calls, initial, actual, hashfile, output = run_variant(
        "read-only forced build", "ro", False, force=True
    )
    check(
        c != 0 and not calls and "Cannot build tiles" in output and initial == actual,
        "Read-only build was not rejected",
    )
    c, calls, initial, actual, hashfile, output = run_variant(
        "read-only transit build", "ro", False, transit_force=True
    )
    check(
        c != 0 and not calls and "Cannot build transit data" in output and initial == actual,
        "Read-only transit build was not rejected before changes",
    )
    c, calls, initial, actual, hashfile, output = run_variant(
        "read-only explicit archive", "ro", False, command="tar_tiles", tar=False
    )
    check(
        c != 0 and not calls and "Cannot build a tile archive" in output,
        "Explicit archive creation on a read-only mount was not rejected",
    )
    c, calls, initial, actual, hashfile, output = run_variant(
        "read-only forced archive", "ro", False, archive_setting="Force"
    )
    check(
        c != 0 and not calls and "Cannot build a tile archive" in output,
        "Forced archive creation on a read-only mount was not rejected",
    )
    c, calls, initial, actual, hashfile, output = run_variant(
        "read-only archive without service", "ro", False, serve=False, tar=False
    )
    check(
        c != 0 and not calls and "Cannot build a tile archive" in output,
        "Archive-only startup on a read-only mount was not rejected",
    )
    c, calls, initial, actual, hashfile, output = run_variant(
        "read-only missing config", "ro", False, config=False
    )
    check(
        c != 0 and not calls and "preconfigured valhalla.json" in output,
        "Read-only startup without config was not rejected",
    )
    c, calls, initial, actual, hashfile, _ = run_variant("writable without update", "rw", False)
    check(
        c == 0
        and initial != actual
        and "config" not in calls
        and "sponge" in calls
        and "curl" in calls
        and "tiles" in calls
        and "service" in calls,
        "Writable startup did not preserve path and default-speed updates",
    )
    writable_config = json.loads(actual)
    check(
        writable_config["mjolnir"]["tile_dir"].endswith("/custom/valhalla_tiles")
        and writable_config["mjolnir"]["concurrency"] == "1",
        "Writable startup did not set the configured paths and concurrency",
    )
    c, calls, initial, actual, hashfile, _ = run_variant("writable with update", "rw", True, repeat=True)
    check(
        c == 0
        and initial != actual
        and calls.count("config") == 2
        and "sponge" in calls
        and calls.count("curl") == 2
        and calls.count("tiles") == 1
        and "service" in calls,
        "Writable config updates did not run",
    )
    c, calls, initial, actual, hashfile, _ = run_variant(
        "writable forced build", "rw", False, force=True
    )
    check(
        c == 0 and calls.count("tiles") == 2 and "extract" in calls and "service" in calls,
        "Writable forced build did not run its build and archive stages",
    )
    check(
        "config" not in calls
        and json.loads(actual)["mjolnir"]["tile_dir"].endswith("/custom/valhalla_tiles"),
        "Writable forced build did not retain its path overrides without merging defaults",
    )
    c, calls, initial, actual, hashfile, _ = run_variant(
        "writable new config", "rw", False, config=False
    )
    check(
        c == 0 and actual is not None and "config" in calls and "service" in calls,
        "Writable startup did not create its missing config",
    )
    c, calls, initial, actual, hashfile, output = run_variant("mount detection failure", "fail", False)
    check(
        c != 0 and not calls and "Cannot determine mount options" in output,
        "Mount detection failure was not reported",
    )
    print("All shell integration checks passed")


if __name__ == "__main__":
    main()
