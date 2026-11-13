#!/usr/bin/env python3
"""Generate re/ppc (the recompiled game code) from the user's own game files.

Portable (Linux/Windows) version of the BUILDING.md steps 1 and 3. It needs:
  --game-dir    folder holding the player's own Default.xex and the Title Update 3 package
                (the file TU_174Q22P_0000008000000.00000000000O2, anywhere under the folder)
  --recompiler  the built XenonRecomp binary (tools/build_xenonrecomp.cmd, or the CI workflow)
  --ppc-context XenonRecomp's XenonUtils/ppc_context.h

Nothing it writes is meant to be committed: re/ppc and kernel_stubs.gen.cpp are derived
from the game and stay out of the public repository (.gitignore already excludes them).

Steps: stage Default.xex -> find and extract TU3 (Default.xexp) -> XenonRecomp (applies the
patch itself) -> gen_missing_functions until no `// ERROR` is left -> unpack the patched image
-> debug_probes -> check_recomp_acceptance -> gen_kernel_stubs.
"""
import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "port" / "tools"
sys.path.insert(0, str(TOOLS))

TU3_NAME = "TU_174Q22P_0000008000000.00000000000O2"
TITLE_ID = "0x4E4D0859"
TU3_VERSION = "0.0.3.2"


def run(cmd, **kw):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, cwd=kw.pop("cwd", ROOT), **kw)


def find_tu3(game_dir: Path) -> Path:
    """The title-update package for TTT2 that targets 0.0.3.2 (TU1/TU2 may sit beside it)."""
    from stfs import STFS, parse_xex_patch_descriptor
    seen = []
    for path in sorted(p for p in game_dir.rglob("*") if p.is_file()):
        if path.stat().st_size < 0x10000:
            continue
        try:
            pkg = STFS(str(path))
            if pkg.title_id != 0x4E4D0859:
                continue
            for entry in pkg.read_file_table():
                if entry["is_dir"] or not entry["name"].lower().endswith(".xexp"):
                    continue
                info = parse_xex_patch_descriptor(pkg.read_file(entry)) or {}
                seen.append((path.name, info.get("target_version")))
                if info.get("target_version") == TU3_VERSION:
                    return path
        except Exception:
            continue
    sys.exit(f"Title Update 3 (version {TU3_VERSION}) not found under {game_dir}. "
             f"Title updates seen: {seen or 'none'}")


def extract_xexp(tu3: Path) -> Path:
    from stfs import STFS
    out = ROOT / "re" / "extracted_tu" / TU3_NAME
    out.mkdir(parents=True, exist_ok=True)
    pkg = STFS(str(tu3))
    for entry in pkg.read_file_table():
        if not entry["is_dir"] and entry["name"].lower().endswith(".xexp"):
            (out / entry["name"]).write_bytes(pkg.read_file(entry))
    xexp = out / "Default.xexp"
    if not xexp.exists():
        sys.exit("TU3 package has no Default.xexp")
    return xexp


def error_count() -> int:
    n = 0
    for f in (ROOT / "re" / "ppc").glob("ppc_recomp.*.cpp"):
        n += len(re.findall(rb"// ERROR [0-9A-F]{8}", f.read_bytes()))
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game-dir", required=True, type=Path)
    ap.add_argument("--recompiler", required=True, type=Path)
    ap.add_argument("--ppc-context", required=True, type=Path)
    args = ap.parse_args()

    xex = next(iter(sorted(args.game_dir.rglob("Default.xex"))), None)
    if not xex:
        sys.exit(f"Default.xex not found under {args.game_dir}")
    (ROOT / "re").mkdir(exist_ok=True)
    shutil.copyfile(xex, ROOT / "re" / "Default.xex")

    tu3 = find_tu3(args.game_dir)
    print(f"TU3 package: {tu3.name}")
    extract_xexp(tu3)

    toml = ROOT / "re" / "recomp" / "tekken.toml"
    for attempt in range(1, 8):
        shutil.rmtree(ROOT / "re" / "ppc", ignore_errors=True)
        (ROOT / "re" / "ppc").mkdir(parents=True)
        run([args.recompiler, toml, args.ppc_context])
        errors = error_count()
        print(f"pass {attempt}: {errors} `// ERROR` sites", flush=True)
        if errors == 0:
            break
        run([sys.executable, TOOLS / "gen_missing_functions.py"])
    else:
        sys.exit("`// ERROR` sites remain after 7 passes")

    patched = ROOT / "re" / "recomp" / "Default_patched_tu3.xex"
    if not patched.exists():
        sys.exit("XenonRecomp did not write the patched image (Default_patched_tu3.xex)")
    run([sys.executable, TOOLS / "xex_unpack.py", patched])
    if not (ROOT / "re" / "decrypted" / "Default_patched_tu3.bin").exists():
        sys.exit("patched image was not unpacked")

    run([sys.executable, TOOLS / "debug_probes.py"])
    run([sys.executable, TOOLS / "check_recomp_acceptance.py"])
    run([sys.executable, TOOLS / "gen_kernel_stubs.py"])
    files = len(list((ROOT / "re" / "ppc").glob("ppc_recomp.*.cpp")))
    print(f"done: {files} generated sources in re/ppc")


if __name__ == "__main__":
    main()
