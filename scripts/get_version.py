#!/usr/bin/env python3
"""
Centralized version detection for ESP32 PhotoFrame project.

This script provides a single source of truth for version detection across:
- CMake builds (local development)
- GitHub Actions CI/CD
- Web flasher manifest generation

Usage:
    python3 get_version.py [--type stable|dev]

Options:
    --type stable    Get stable version (latest git tag or GitHub release)
    --type dev       Get dev version (dev-<commit-hash>)

If --type is not specified, auto-detects based on current git state:
    - If on a tag: returns the tag version
    - Otherwise: returns dev-<commit-hash>
"""

import argparse
import json
import subprocess
import sys
import urllib.request

# Firmware releases are published here rather than to this repository
FIRMWARE_REPO = "bennchong/frameway-firmware"


def get_latest_github_release():
    """Get the latest release version from GitHub API."""
    try:
        api_url = f"https://api.github.com/repos/{FIRMWARE_REPO}/releases/latest"
        with urllib.request.urlopen(api_url, timeout=5) as response:
            data = json.loads(response.read())
            return data["tag_name"]
    except Exception as e:
        print(f"Warning: Could not fetch from GitHub API: {e}", file=sys.stderr)

    return None


def get_git_tag():
    """Get the latest git tag."""
    try:
        result = subprocess.run(
            ["git", "describe", "--tags", "--abbrev=0"],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.strip()
    except Exception:
        pass
    return None


def get_commit_hash():
    """Get the short commit hash."""
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            capture_output=True,
            text=True,
            check=True,
        )
        return result.stdout.strip()
    except Exception:
        return None


def get_stable_version():
    """Get stable version (latest tag or GitHub release)."""
    # Method 1: Try git tag
    version = get_git_tag()
    if version:
        return version

    # Method 2: Try GitHub API
    version = get_latest_github_release()
    if version:
        return version

    # Method 3: Fallback to dev version
    commit = get_commit_hash()
    if commit:
        return f"dev-{commit}"

    return "unknown"


def get_dev_version():
    """Get dev version (dev-<commit-hash>)."""
    commit = get_commit_hash()
    if commit:
        return f"dev-{commit}"
    return "dev-unknown"


def get_auto_version():
    """Auto-detect version based on git state."""
    # Check if we're exactly on a tag
    try:
        result = subprocess.run(
            ["git", "describe", "--tags", "--exact-match"],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.strip()
    except Exception:
        pass

    # Not on a tag, return dev version
    return get_dev_version()


def main():
    parser = argparse.ArgumentParser(
        description="Get version for ESP32 PhotoFrame project"
    )
    parser.add_argument(
        "--type",
        choices=["stable", "dev", "auto"],
        default="auto",
        help="Version type to retrieve (default: auto)",
    )

    args = parser.parse_args()

    if args.type == "stable":
        version = get_stable_version()
    elif args.type == "dev":
        version = get_dev_version()
    else:  # auto
        version = get_auto_version()

    print(version)
    return 0


if __name__ == "__main__":
    sys.exit(main())
