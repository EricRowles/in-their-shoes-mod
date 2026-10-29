"""
Package Mod Script for In Their Shoes
Creates a standalone zip archive ready for installation via Mod Organizer 2,
Vortex, or manual extraction into the Skyrim Special Edition Data folder.
"""

import os
import re
import sys
import zipfile
import argparse
import subprocess

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CMAKE_LISTS = os.path.join(REPO_ROOT, "SKSEPlugin", "CMakeLists.txt")


def get_version():
    """The plugin version, from project(... VERSION x.y.z) in SKSEPlugin/CMakeLists.txt."""
    with open(CMAKE_LISTS, encoding="utf-8") as f:
        match = re.search(r"project\s*\([^)]*?\bVERSION\s+([0-9.]+)", f.read(), re.DOTALL)
    if not match:
        sys.exit(f"Error: no project VERSION found in {os.path.relpath(CMAKE_LISTS, REPO_ROOT)}")
    return match.group(1)

def get_manifest():
    """Returns the list of relative file paths to include in the package."""
    manifest = [
        "InTheirShoes.esp",
        os.path.join("SKSE", "Plugins", "InTheirShoes.dll"),
    ]
    
    scripts_dir = os.path.join(REPO_ROOT, "scripts")
    if os.path.isdir(scripts_dir):
        for f in sorted(os.listdir(scripts_dir)):
            if f.endswith(".pex"):
                manifest.append(os.path.join("scripts", f))
                
    source_dir = os.path.join(REPO_ROOT, "source", "scripts")
    if os.path.isdir(source_dir):
        for f in sorted(os.listdir(source_dir)):
            if f.endswith(".psc"):
                manifest.append(os.path.join("source", "scripts", f))

    readme = "README.md"
    if os.path.exists(os.path.join(REPO_ROOT, readme)):
        manifest.append(readme)

    return manifest

def build_if_needed():
    """Runs build_and_deploy.py to ensure the DLL and PEX scripts are fresh."""
    build_script = os.path.join(REPO_ROOT, "tools", "build_and_deploy.py")
    if os.path.exists(build_script):
        print("Ensuring latest build and compiled scripts...")
        res = subprocess.run([sys.executable, build_script, "--no-deploy"], capture_output=False)
        if res.returncode != 0:
            print(f"Error: Build failed with exit code {res.returncode}")
            sys.exit(res.returncode)

def create_package(output_zip: str, skip_build: bool = False):
    if not skip_build:
        build_if_needed()

    manifest = get_manifest()
    
    # Verify required files exist
    missing = []
    for rel_path in manifest:
        full_path = os.path.join(REPO_ROOT, rel_path)
        if not os.path.exists(full_path):
            missing.append(rel_path)
            
    if missing:
        print(f"Error: The following required files are missing:\n" + "\n".join(f" - {f}" for f in missing))
        sys.exit(1)

    out_dir = os.path.dirname(output_zip)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    print(f"\nCreating mod package: {output_zip}")
    with zipfile.ZipFile(output_zip, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for rel_path in manifest:
            full_path = os.path.join(REPO_ROOT, rel_path)
            # Normalize path for zip file (forward slashes)
            arcname = rel_path.replace("\\", "/")
            zf.write(full_path, arcname=arcname)
            file_size = os.path.getsize(full_path)
            print(f"  + [{file_size:8d} bytes] {arcname}")

    total_size = os.path.getsize(output_zip)
    print(f"\nPackage created successfully: {output_zip} ({total_size / (1024*1024):.2f} MB)")
    return output_zip

def main():
    parser = argparse.ArgumentParser(description="Package In Their Shoes as an installable mod zip.")
    parser.add_argument(
        "-o", "--output",
        default=os.path.join(REPO_ROOT, "dist", f"InTheirShoes-v{get_version()}.zip"),
        help="Path to output zip file (defaults to dist/InTheirShoes-v<version>.zip)"
    )
    parser.add_argument(
        "--no-build",
        action="store_true",
        help="Skip rebuilding DLL and recompiling Papyrus scripts"
    )
    args = parser.parse_args()
    create_package(args.output, skip_build=args.no_build)

if __name__ == "__main__":
    main()
