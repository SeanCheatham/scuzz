#!/usr/bin/env python3
"""Build a local macOS app bundle with its non-system libraries."""

from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile


def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def libraries(binary):
    rows = run("otool", "-L", str(binary)).splitlines()[1:]
    return [row.strip().split(" (compatibility version", 1)[0] for row in rows]


def bundle(exe, dest, name, version, identifier, width, height, scale):
    if not name or Path(name).name != name or name in (".", ".."):
        raise ValueError("package name must be one file name")
    if dest.is_symlink() or (dest.exists() and not dest.is_dir()):
        raise ValueError("package output must be a directory")
    dest.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".macos-", dir=dest.parent) as temp:
        stage = Path(temp) / "host"
        app = stage / (name + ".app")
        contents = app / "Contents"
        main = contents / "MacOS" / name
        frameworks = contents / "Frameworks"
        main.parent.mkdir(parents=True)
        frameworks.mkdir()
        (contents / "Resources").mkdir()
        shutil.copy2(exe, main)
        copied = {}
        names = {}

        def embed(source, target, is_library=False):
            for dependency in libraries(source):
                if dependency.startswith(("/System/Library/", "/usr/lib/")):
                    continue
                path = Path(dependency)
                if not path.is_absolute() or path.suffix != ".dylib":
                    raise ValueError("unsupported library path: " + dependency)
                path = path.resolve(strict=True)
                if path == source.resolve():
                    continue
                if path not in copied:
                    filename = Path(dependency).name
                    if filename in names and names[filename] != path:
                        raise ValueError("library file name collision: " + filename)
                    names[filename] = path
                    library = frameworks / filename
                    copied[path] = library
                    shutil.copy2(path, library)
                    license_file = path.parent.parent / "LICENSE.txt"
                    if license_file.is_file():
                        shutil.copy2(license_file,
                                     contents / "Resources" / (library.stem + "-LICENSE.txt"))
                    run("install_name_tool", "-id", "@loader_path/" + filename,
                        str(library))
                    embed(path, library, True)
                relative = "@loader_path/" if is_library else "@executable_path/../Frameworks/"
                run("install_name_tool", "-change", dependency,
                    relative + copied[path].name, str(target))

        embed(exe, main)
        info = {
            "CFBundleExecutable": name,
            "CFBundleIdentifier": identifier,
            "CFBundleDisplayName": name,
            "CFBundleName": name,
            "CFBundlePackageType": "APPL",
            "CFBundleShortVersionString": version,
            "CFBundleVersion": "1",
            "NSHighResolutionCapable": True,
            "NSAppTransportSecurity": {"NSAllowsLocalNetworking": True},
            "NSLocalNetworkUsageDescription": "Connect to app services on your local network.",
            "ScuzzUI": {"width": int(width), "height": int(height), "scale": float(scale)},
        }
        (contents / "Info.plist").write_bytes(plistlib.dumps(info))
        for library in copied.values():
            run("codesign", "--force", "--sign", "-", "--timestamp=none", str(library))
        run("codesign", "--force", "--sign", "-", "--timestamp=none", str(app))
        run("codesign", "--verify", "--deep", "--strict", str(app))
        previous = Path(temp) / "previous"
        if dest.exists():
            dest.rename(previous)
        try:
            stage.rename(dest)
        except OSError:
            if previous.exists():
                previous.rename(dest)
            raise
    print("scuzz package ok (macOS) -> " + str(dest / (name + ".app")))


if __name__ == "__main__":
    try:
        bundle(Path(sys.argv[1]).absolute(), Path(sys.argv[2]).absolute(), *sys.argv[3:])
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print("macOS package fails: " + str(error), file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError):
            print(error.output, file=sys.stderr)
        sys.exit(1)
