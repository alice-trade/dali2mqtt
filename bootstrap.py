#!/usr/bin/env python3

"""
ESP-IDF Setup Utility.

Downloads standalone ESP-IDF archives (including git submodules), verifies hashes,
extracts them to a local project directory, and installs the required toolchains,
CMake, and Ninja without requiring manual configuration.
"""

import os
import sys
import json
import shutil
import zipfile
import tarfile
import hashlib
import platform
import argparse
import subprocess
import urllib.request
import urllib.error
from pathlib import Path

CLR_RESET = "\033[0m"
CLR_BOLD  = "\033[1m"
CLR_CYAN  = "\033[1;36m"
CLR_GREEN = "\033[1;32m"
CLR_YELLOW= "\033[1;33m"
CLR_RED   = "\033[1;31m"

def log_info(msg: str):
    print(f"{CLR_CYAN}[INFO]{CLR_RESET} {msg}")

def log_success(msg: str):
    print(f"{CLR_GREEN}[SUCCESS]{CLR_RESET} {msg}")

def log_warn(msg: str):
    print(f"{CLR_YELLOW}[WARN]{CLR_RESET} {msg}")

def log_error(msg: str):
    print(f"{CLR_RED}[ERROR]{CLR_RESET} {msg}", file=sys.stderr)


class EspIdfBootstrapper:
    GITHUB_API_RELEASES = "https://api.github.com/repos/espressif/esp-idf/releases"
    GITHUB_DL_URL = "https://github.com/espressif/esp-idf/releases/download"

    def __init__(self, args: argparse.Namespace):
        self.version = args.version
        self.custom_url = args.url
        self.expected_sha256 = args.sha256
        self.dest_dir = Path(args.dest).resolve()
        self.tools_dir = Path(args.tools_dir).resolve() if args.tools_dir else None
        self.targets = args.targets
        self.skip_tools = args.skip_tools
        self.is_windows = platform.system() == "Windows"

    def run(self):
        print(f"{CLR_BOLD}{CLR_CYAN}=== ESP-IDF Automated Bootstrapper ==={CLR_RESET}\n")

        download_url, archive_name = self._resolve_download_source()

        self.dest_dir.parent.mkdir(parents=True, exist_ok=True)
        archive_path = self.dest_dir.parent / archive_name

        if self._is_idf_installed():
            log_warn(f"ESP-IDF is already installed in: {self.dest_dir}")
            if not self._confirm_overwrite():
                self._run_toolchain_install()
                return

        self._download_file(download_url, archive_path)

        if self.expected_sha256:
            self._verify_sha256(archive_path, self.expected_sha256)
        else:
            log_info("No SHA-256 provided. Calculating archive fingerprint...")
            computed_hash = self._calc_sha256(archive_path)
            log_info(f"Archive SHA-256: {computed_hash}")

        self._extract_archive(archive_path)

        if archive_path.exists():
            log_info(f"Cleaning up temporary archive: {archive_path.name}")
            archive_path.unlink()

        if not self.skip_tools:
            self._run_toolchain_install()
        print(f"\n{CLR_BOLD}{CLR_GREEN}ESP-IDF setup finished successfully!{CLR_RESET}\n")
        self._print_usage_instructions()

    def _resolve_download_source(self) -> tuple[str, str]:
        if self.custom_url:
            log_info(f"Using manual URL: {self.custom_url}")
            filename = self.custom_url.split("/")[-1] or "esp-idf-manual.zip"
            return self.custom_url, filename

        tag = self.version if self.version.startswith("v") else f"v{self.version}"
        log_info(f"Targeting ESP-IDF release: {CLR_BOLD}{tag}{CLR_RESET}")

        try:
            req = urllib.request.Request(
                f"{self.GITHUB_API_RELEASES}/tags/{tag}",
                headers={"User-Agent": "ESP-IDF-Bootstrap"}
            )
            with urllib.request.urlopen(req, timeout=10) as resp:
                data = json.loads(resp.read().decode("utf-8"))
                for asset in data.get("assets", []):
                    name = asset.get("name", "")
                    if (name.endswith(".zip") or name.endswith(".tar.gz")) and "esp-idf" in name:
                        log_info(f"Discovered release asset: {name}")
                        return asset["browser_download_url"], name
        except Exception as e:
            log_warn(f"GitHub API query failed ({e}). Falling back to standard release asset URL.")

        ext = "zip" if self.is_windows else "tar.gz"
        asset_name = f"esp-idf-{tag}.{ext}"
        fallback_url = f"{self.GITHUB_DL_URL}/{tag}/{asset_name}"
        return fallback_url, asset_name

    def _download_file(self, url: str, target: Path):
        log_info(f"Downloading: {url}")
        log_info(f"Destination: {target}")

        try:
            req = urllib.request.Request(url, headers={"User-Agent": "ESP-IDF-Bootstrap"})
            with urllib.request.urlopen(req) as resp:
                total_size = int(resp.headers.get("Content-Length", 0))
                block_size = 1024 * 1024
                downloaded = 0

                with open(target, "wb") as f:
                    while True:
                        chunk = resp.read(block_size)
                        if not chunk:
                            break
                        f.write(chunk)
                        downloaded += len(chunk)
                        if total_size > 0:
                            percent = (downloaded / total_size) * 100
                            done_mb = downloaded / (1024 * 1024)
                            total_mb = total_size / (1024 * 1024)
                            bar_len = 30
                            filled = int(bar_len * downloaded // total_size)
                            bar = "=" * filled + ">" + " " * (bar_len - filled - 1)
                            sys.stdout.write(f"\r  [{bar}] {percent:5.1f}% ({done_mb:.1f} MB / {total_mb:.1f} MB)")
                            sys.stdout.flush()
                print()
        except urllib.error.URLError as e:
            log_error(f"Download failed: {e}")
            if target.exists():
                target.unlink()
            sys.exit(1)

    def _calc_sha256(self, path: Path) -> str:
        sha256 = hashlib.sha256()
        with open(path, "rb") as f:
            for block in iter(lambda: f.read(65536), b""):
                sha256.update(block)
        return sha256.hexdigest().lower()

    def _verify_sha256(self, path: Path, expected: str):
        log_info("Verifying archive SHA-256 integrity...")
        actual = self._calc_sha256(path)
        if actual != expected.strip().lower():
            log_error(f"Checksum mismatch!\n  Expected: {expected}\n  Actual:   {actual}")
            path.unlink()
            sys.exit(1)
        log_success("Checksum verified.")

    def _extract_archive(self, archive_path: Path):
        """Extracts zip or tar.gz and flattens directory structure into dest_dir."""
        log_info(f"Extracting archive to: {self.dest_dir} ...")

        temp_extract = self.dest_dir.parent / "_idf_unpack_tmp"
        if temp_extract.exists():
            shutil.rmtree(temp_extract)
        temp_extract.mkdir(parents=True, exist_ok=True)

        try:
            if archive_path.name.endswith(".zip"):
                with zipfile.ZipFile(archive_path, 'r') as zf:
                    zf.extractall(temp_extract)
            elif archive_path.name.endswith((".tar.gz", ".tgz")):
                with tarfile.open(archive_path, 'r:gz') as tf:
                    tf.extractall(temp_extract)
            else:
                log_error("Unsupported archive format.")
                sys.exit(1)

            subdirs = [d for d in temp_extract.iterdir() if d.is_dir()]
            root_src = subdirs[0] if len(subdirs) == 1 else temp_extract

            if self.dest_dir.exists():
                shutil.rmtree(self.dest_dir)
            shutil.move(str(root_src), str(self.dest_dir))
            log_success(f"Extracted ESP-IDF to: {self.dest_dir}")
        finally:
            if temp_extract.exists():
                shutil.rmtree(temp_extract)

    def _run_toolchain_install(self):
        """Runs the install script to fetch toolchains, cmake, and ninja."""
        log_info(f"Setting up toolchains for targets: {CLR_BOLD}{self.targets}{CLR_RESET}")

        env = os.environ.copy()
        if self.tools_dir:
            env["IDF_TOOLS_PATH"] = str(self.tools_dir)
            log_info(f"IDF_TOOLS_PATH set to: {self.tools_dir}")

        if self.is_windows:
            install_cmd = [
                "powershell.exe",
                "-NoProfile",
                "-ExecutionPolicy", "Bypass",
                "-File", str(self.dest_dir / "install.ps1"),
                self.targets
            ]
        else:
            install_script = self.dest_dir / "install.sh"
            install_script.chmod(0o755)
            install_cmd = ["/usr/bin/env", "bash", str(install_script), self.targets]

        log_info(f"Running command: {' '.join(install_cmd)}")
        try:
            subprocess.run(install_cmd, cwd=str(self.dest_dir), env=env, check=True)
            log_success("Toolchains, CMake, and Ninja installed successfully.")
        except subprocess.CalledProcessError as e:
            log_error(f"Installation failed with return code {e.returncode}")
            sys.exit(e.returncode)
    def _is_idf_installed(self) -> bool:
        marker = "export.ps1" if self.is_windows else "export.sh"
        return (self.dest_dir / marker).exists()

    def _confirm_overwrite(self) -> bool:
        answer = input(f"{CLR_YELLOW}Directory '{self.dest_dir}' already exists. Re-download and overwrite? [y/N]: {CLR_RESET}")
        return answer.strip().lower() in ("y", "yes")

    def _print_usage_instructions(self):
        if self.is_windows:
            activate_cmd = ". .\\export_env.ps1"
            build_cmd = ".\\build.ps1 app -t esp32c6 -b Release"
        else:
            activate_cmd = ". ./export_env.sh"
            build_cmd = "./build.sh app -t esp32c6 -b Release"

        print("To start developing, activate the environment in your shell:")
        print(f"  {CLR_BOLD}{CLR_CYAN}{activate_cmd}{CLR_RESET}\n")
        print("Then build your firmware:")
        print(f"  {CLR_BOLD}{CLR_CYAN}{build_cmd}{CLR_RESET}\n")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="ESP-IDF Setup Utility",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    parser.add_argument(
        "-v", "--version",
        default="v6.1",
        help="ESP-IDF release version to download (e.g. v6.1, v5.4, v5.3.2)"
    )
    parser.add_argument(
        "-u", "--url",
        default=None,
        help="Direct URL to standalone ESP-IDF archive (.zip or .tar.gz)"
    )
    parser.add_argument(
        "--sha256",
        default=None,
        help="Expected SHA-256 hash of the archive for security verification"
    )
    parser.add_argument(
        "-d", "--dest",
        default="./esp-idf",
        help="Target directory where ESP-IDF will be extracted"
    )
    parser.add_argument(
        "--tools-dir",
        default=None,
        help="Custom directory for toolchains (sets IDF_TOOLS_PATH). Default: ~/.espressif"
    )
    parser.add_argument(
        "-t", "--targets",
        default="esp32,esp32s3,esp32c6",
        help="Comma-separated target microcontrollers to install compilers for"
    )
    parser.add_argument(
        "--skip-tools",
        action="store_true",
        help="Only download and unpack ESP-IDF, skip running install.sh/install.ps1"
    )
    return parser.parse_args()


if __name__ == "__main__":
    cli_args = parse_arguments()
    installer = EspIdfBootstrapper(cli_args)
    installer.run()