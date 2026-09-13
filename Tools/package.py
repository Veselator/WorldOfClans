# -*- coding: utf-8 -*-
"""
package.py - builds World of Clans and packs it into a zip a player can simply unpack and run.

    python Tools/package.py                     build Release, pack everything
    python Tools/package.py --no-build          pack the Release build that is already there
    python Tools/package.py --maps Test         ship only these maps (folder names)
    python Tools/package.py --include-generated also ship Maps/Generated_* worlds
    python Tools/package.py --name WoC-demo     name of the folder and of the zip

The result is Dist/<name>.zip holding one folder:

    <name>/
      WorldOfClans.exe
      msvcp140.dll, vcruntime140.dll, vcruntime140_1.dll   (so no redistributable is needed)
      Config/   Sprites/   Sound/   Shaders/ (*.spv only)
      Maps/     <- players drop their own map folders here
      Saves/    <- empty; the game writes saves here

The game finds its data by looking for Config/game.json next to the .exe, so the folder
can be unpacked anywhere and the .exe renamed at will.
"""
import argparse
import datetime
import glob
import os
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Data folders copied whole. Shaders are handled separately: only the compiled SPIR-V ships.
DATA_FOLDERS = ["Config", "Sprites", "Sound"]
# The player's own preferences are not part of the game: without the file the defaults
# from game.json are used, which is what a fresh install should see.
SKIP_FILES = {os.path.join("Config", "settings.json")}
RUNTIME_DLLS = ["msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"]


def log(message):
    print("[package] " + message)


def find_vs():
    """Visual Studio install folder, via vswhere, or the common default."""
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if os.path.exists(vswhere):
        out = subprocess.run([vswhere, "-latest", "-products", "*", "-requires",
                              "Microsoft.Component.MSBuild", "-property", "installationPath"],
                             capture_output=True, text=True).stdout.strip()
        if out:
            return out.splitlines()[0]
    for candidate in glob.glob(r"C:\Program Files\Microsoft Visual Studio\*\*"):
        if os.path.exists(os.path.join(candidate, "MSBuild", "Current", "Bin", "MSBuild.exe")):
            return candidate
    return None


def build(vs):
    msbuild = os.path.join(vs, "MSBuild", "Current", "Bin", "MSBuild.exe")
    log("building Release with " + msbuild)
    result = subprocess.run([msbuild, os.path.join(ROOT, "PPaTU_Lr2.vcxproj"),
                             "-p:Configuration=Release", "-p:Platform=x64", "-m", "-v:m"])
    if result.returncode != 0:
        sys.exit("[package] the build failed; nothing was packed")


def runtime_dlls(vs):
    """The C++ runtime DLLs from the Visual Studio redistributable folder, newest first."""
    pattern = os.path.join(vs, "VC", "Redist", "MSVC", "*", "x64", "Microsoft.VC*.CRT")
    folders = sorted(glob.glob(pattern), reverse=True)
    for folder in folders:
        found = [os.path.join(folder, name) for name in RUNTIME_DLLS]
        if all(os.path.exists(path) for path in found):
            return found
    return []


def copy_tree(source, target, relative_root):
    for folder, _, files in os.walk(source):
        for name in files:
            full = os.path.join(folder, name)
            relative = os.path.relpath(full, relative_root)
            if relative in SKIP_FILES:
                continue
            destination = os.path.join(target, relative)
            os.makedirs(os.path.dirname(destination), exist_ok=True)
            shutil.copy2(full, destination)


def main():
    parser = argparse.ArgumentParser(description="Pack World of Clans for distribution.")
    parser.add_argument("--no-build", action="store_true", help="use the existing Release build")
    parser.add_argument("--maps", nargs="*", help="map folders to ship (default: all but generated)")
    parser.add_argument("--include-generated", action="store_true", help="also ship Maps/Generated_*")
    parser.add_argument("--name", default="WorldOfClans", help="folder and zip name")
    args = parser.parse_args()

    vs = find_vs()
    if not vs:
        sys.exit("[package] Visual Studio was not found")

    if not args.no_build:
        build(vs)

    exe = os.path.join(ROOT, "Build", "Release", "PPaTU_Lr2.exe")
    if not os.path.exists(exe):
        sys.exit("[package] " + exe + " does not exist - build Release first")

    dist = os.path.join(ROOT, "Dist")
    stage = os.path.join(dist, args.name)
    if os.path.exists(stage):
        shutil.rmtree(stage)
    os.makedirs(stage)

    # --- the program ---------------------------------------------------------------------
    shutil.copy2(exe, os.path.join(stage, "WorldOfClans.exe"))
    dlls = runtime_dlls(vs)
    if dlls:
        for dll in dlls:
            shutil.copy2(dll, stage)
        log("C++ runtime bundled: " + ", ".join(os.path.basename(d) for d in dlls))
    else:
        log("WARNING: C++ runtime DLLs not found; players will need the VC++ Redistributable x64")

    # --- data ----------------------------------------------------------------------------
    for folder in DATA_FOLDERS:
        source = os.path.join(ROOT, folder)
        if os.path.isdir(source):
            copy_tree(source, stage, ROOT)

    os.makedirs(os.path.join(stage, "Shaders"), exist_ok=True)
    for spv in glob.glob(os.path.join(ROOT, "Shaders", "*.spv")):
        shutil.copy2(spv, os.path.join(stage, "Shaders"))

    maps_root = os.path.join(ROOT, "Maps")
    shipped = []
    for entry in sorted(os.listdir(maps_root)):
        full = os.path.join(maps_root, entry)
        if not os.path.isdir(full) or not os.path.exists(os.path.join(full, "Map.json")):
            continue
        if args.maps is not None and entry not in args.maps:
            continue
        if args.maps is None and entry.startswith("Generated_") and not args.include_generated:
            continue
        copy_tree(full, stage, ROOT)
        shipped.append(entry)
    os.makedirs(os.path.join(stage, "Maps"), exist_ok=True)
    os.makedirs(os.path.join(stage, "Saves"), exist_ok=True)
    log("maps: " + (", ".join(shipped) if shipped else "none"))

    with open(os.path.join(stage, "Maps", "README.txt"), "w", encoding="utf-8") as note:
        note.write(
            "Тут лежать карти. Кожна карта - окрема тека з файлами:\n"
            "  Map.json, Terrain.png, HeightMap.png, Trees.png\n"
            "  (MapObjects.json і Minimap.png - за бажанням)\n"
            "Щоб додати карту, просто скопіюйте її теку сюди. Гра знайде її сама.\n"
            "Власні карти зручно робити у вбудованому редакторі (\"Редактор карт\").\n")

    # --- the archive ---------------------------------------------------------------------
    stamp = datetime.date.today().strftime("%Y-%m-%d")
    archive = os.path.join(dist, "%s-%s.zip" % (args.name, stamp))
    if os.path.exists(archive):
        os.remove(archive)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for folder, dirs, files in os.walk(stage):
            relative_folder = os.path.relpath(folder, dist)
            if not files and not dirs:
                zf.writestr(relative_folder.replace(os.sep, "/") + "/", "")   # keep empty Saves/
            for name in files:
                full = os.path.join(folder, name)
                zf.write(full, os.path.relpath(full, dist))

    size = os.path.getsize(archive) / (1024 * 1024)
    log("done: %s (%.1f MB)" % (archive, size))


if __name__ == "__main__":
    main()
