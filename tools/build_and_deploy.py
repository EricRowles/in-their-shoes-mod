"""
Build and Deploy Script for InTheirShoes
Builds the C++ SKSE plugin (MSVC x64 Release) and compiles all Papyrus scripts (.psc -> .pex),
then deploys the artifacts to the mod folders listed in .env.

Machine-specific paths are read from .env in the repo root (not committed). Copy .env.example to .env
and fill it in.
"""

import os
import subprocess
import shutil
import sys

# Paths
REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
ENV_FILE = os.path.join(REPO_ROOT, ".env")
ENV_KEYS = ("VCVARS_BAT", "SKYRIM_DIR", "PAPYRUS_IMPORTS", "DEPLOY_TARGETS")
CMAKE_BUILD_DIR = os.path.join(REPO_ROOT, "SKSEPlugin", "build", "release-msvc")

SCRIPTS_SRC = os.path.join(REPO_ROOT, "source", "scripts")
SCRIPTS_PEX_OUT = os.path.join(REPO_ROOT, "scripts")
DLL_SRC = os.path.join(CMAKE_BUILD_DIR, "InTheirShoes.dll")
DLL_REPO_DEST = os.path.join(REPO_ROOT, "SKSE", "Plugins", "InTheirShoes.dll")


def load_env():
    """Reads KEY=VALUE lines from .env. Values set in the environment take precedence."""
    values = {}
    if os.path.exists(ENV_FILE):
        with open(ENV_FILE, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                key, value = line.split("=", 1)
                values[key.strip()] = value.strip().strip('"')
    for key in ENV_KEYS:
        if os.environ.get(key):
            values[key] = os.environ[key]
    return values


def require(env, key):
    value = env.get(key, "")
    if not value:
        print(f"Error: {key} is not set. Copy .env.example to .env in the repo root and fill it in.")
        sys.exit(1)
    return value


def path_list(value):
    return [p.strip() for p in value.split(";") if p.strip()]


ENV = load_env()


def build_cpp():
    print("\n=== [1/3] Building C++ Plugin (InTheirShoes.dll) ===")
    vcvars_bat = require(ENV, "VCVARS_BAT")
    if not os.path.exists(CMAKE_BUILD_DIR):
        print(f"Error: Build directory not found: {CMAKE_BUILD_DIR}")
        sys.exit(1)

    path = os.environ.get("PATH", "")
    new_path = os.pathsep.join([p for p in path.split(os.pathsep) if "mingw" not in p.lower() and "winlibs" not in p.lower()])
    env = os.environ.copy()
    env["PATH"] = new_path

    cmd = f'call "{vcvars_bat}" && cmake --build "{CMAKE_BUILD_DIR}" --config Release'
    res = subprocess.run(cmd, shell=True, capture_output=True, text=True, env=env)
    if res.returncode != 0:
        print("C++ Build Failed!")
        print(res.stdout)
        print(res.stderr)
        sys.exit(res.returncode)
    print("C++ Plugin build successful!")

    os.makedirs(os.path.dirname(DLL_REPO_DEST), exist_ok=True)
    if os.path.exists(DLL_SRC):
        shutil.copy2(DLL_SRC, DLL_REPO_DEST)
        print(f"Copied DLL to: {os.path.relpath(DLL_REPO_DEST, REPO_ROOT)}")


def compile_papyrus():
    print("\n=== [2/3] Compiling Papyrus Scripts (.psc -> .pex) ===")
    skyrim_dir = require(ENV, "SKYRIM_DIR")
    compiler_exe = os.path.join(skyrim_dir, "Papyrus Compiler", "PapyrusCompiler.exe")
    flags_file = os.path.join(skyrim_dir, "Data", "Source", "Scripts", "TESV_Papyrus_Flags.flg")
    imports = [SCRIPTS_SRC] + path_list(require(ENV, "PAPYRUS_IMPORTS"))

    os.makedirs(SCRIPTS_PEX_OUT, exist_ok=True)
    import_arg = ";".join(imports)

    psc_files = [f for f in os.listdir(SCRIPTS_SRC) if f.endswith(".psc")]
    failed = []
    for f in psc_files:
        psc_path = os.path.join(SCRIPTS_SRC, f)
        cmd = [
            compiler_exe,
            psc_path,
            f"-f={flags_file}",
            f"-i={import_arg}",
            f"-o={SCRIPTS_PEX_OUT}"
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"[FAIL] {f}")
            print(res.stdout)
            print(res.stderr)
            failed.append(f)
        else:
            print(f"[OK]   {f}")

    if failed:
        print(f"\nPapyrus compilation failed for: {', '.join(failed)}")
        sys.exit(1)
    print("All Papyrus scripts compiled successfully!")


def deploy():
    print("\n=== [3/3] Deploying to Mod Folders ===")
    targets = path_list(ENV.get("DEPLOY_TARGETS", ""))
    if not targets:
        print("DEPLOY_TARGETS is empty in .env; skipping deployment.")
        return

    for target in targets:
        os.makedirs(target, exist_ok=True)

        dll_dir = os.path.join(target, "SKSE", "Plugins")
        os.makedirs(dll_dir, exist_ok=True)

        old_dll = os.path.join(dll_dir, "InTheirShoes.dll")
        if os.path.exists(old_dll):
            try: os.remove(old_dll)
            except: pass

        if os.path.exists(DLL_SRC):
            shutil.copy2(DLL_SRC, os.path.join(dll_dir, "InTheirShoes.dll"))
            print(f"Deployed DLL -> {dll_dir}")

        pex_dir = os.path.join(target, "scripts")
        os.makedirs(pex_dir, exist_ok=True)
        for f in os.listdir(SCRIPTS_PEX_OUT):
            if f.endswith(".pex"):
                shutil.copy2(os.path.join(SCRIPTS_PEX_OUT, f), os.path.join(pex_dir, f))
        print(f"Deployed PEX scripts -> {pex_dir}")

        src_dir = os.path.join(target, "source", "scripts")
        os.makedirs(src_dir, exist_ok=True)
        for f in os.listdir(SCRIPTS_SRC):
            if f.endswith(".psc"):
                shutil.copy2(os.path.join(SCRIPTS_SRC, f), os.path.join(src_dir, f))
        print(f"Deployed PSC sources -> {src_dir}")

        esp_src = os.path.join(REPO_ROOT, "InTheirShoes.esp")
        if os.path.exists(esp_src):
            try:
                shutil.copy2(esp_src, os.path.join(target, "InTheirShoes.esp"))
                print(f"Deployed ESP -> {target}")
            except PermissionError:
                print(f"ESP locked by another process (skipping copy): {target}")

    print("\nDeployment complete!")


if __name__ == "__main__":
    build_cpp()
    compile_papyrus()
    deploy()
