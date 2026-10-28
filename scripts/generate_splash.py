#!/usr/bin/env python3
"""Generate OOBE splash screen EPDGZ files for embedding in firmware.

Creates SVG splash screens by display resolution (not per-board), converts
to PNG via rsvg-convert, then uses process-cli to dither and produce EPDGZ.

Currently generates two variants:
  - 800x480   (landscape: Waveshare PhotoPainter 7.3", reTerminal E1002, XIAO EE04)
  - 1200x1600 (portrait: XIAO EE02)

Dependencies:
  rsvg-convert (librsvg2-bin on Ubuntu, librsvg on macOS)
  node + process-cli (npm ci in process-cli/)
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

# FrameWay brand colours
BACKGROUND_COLOR = "#809178"  # sage green
FOREGROUND_COLOR = "#ffffff"

# FrameWay mark in a 512x512 box: a frame left open at the bottom right, where a leaf sits
MARK_FRAME_PATH = (
    "M 221 405 H 147 Q 117 405 117 375 V 137 Q 117 107 147 108.7 "
    "L 335 119.3 Q 365 121 365 151 V 225"
)
MARK_FRAME_STROKE = 42
MARK_LEAF_PATH = (
    "M 241 426 C 247 310 299.2 256.9 415 249 C 409 364.9 356.8 418 241 426 Z"
)

# Screen size variants to generate
SCREEN_SIZES = {
    "800x480": (800, 480),
    "1200x1600": (1200, 1600),
}

# Layout parameters per orientation (all values are fractions of width or height)
LAYOUT = {
    "landscape": {
        "icon_y": 0.25,  # icon vertical center (fraction of height)
        "icon_size": 0.28,  # icon size (fraction of min(width, height))
        "title_size": 0.06,  # title font size (fraction of height)
        "subtitle_size": 0.035,  # subtitle font size (fraction of height)
        "qr_y": 0.62,  # QR code top edge (fraction of height)
        "qr_size": 0.195,  # QR code size (fraction of min(width, height))
    },
    "portrait": {
        "icon_y": 0.30,  # icon vertical center (fraction of height)
        "icon_size": 0.25,  # icon size (fraction of min(width, height))
        "title_size": 0.035,  # title font size (fraction of height)
        "subtitle_size": 0.016,  # subtitle font size (fraction of height)
        "qr_y": 0.55,  # QR code top edge (fraction of height)
        "qr_size": 0.195,  # QR code size (fraction of min(width, height))
    },
}

# Path to process-cli
PROCESS_CLI = os.path.join(os.path.dirname(__file__), "..", "process-cli", "cli.js")


def generate_screen_svg(
    width: int, height: int, title: str, subtitle: str, qr_label: str
) -> tuple:
    """Generate a screen SVG with the FrameWay mark, a title and a single centered QR
    placeholder that the firmware fills at runtime.
    Returns (svg_content, qr_x, qr_y, qr_size)."""
    is_landscape = width > height
    L = LAYOUT["landscape" if is_landscape else "portrait"]

    icon_scale = min(width, height) * L["icon_size"]
    icon_x = width / 2
    icon_y = height * L["icon_y"]

    title_y = icon_y + icon_scale * 0.55
    title_size = max(18, height * L["title_size"])
    subtitle_size = max(10, height * L["subtitle_size"])

    qr_size = int(min(width, height) * L["qr_size"])
    qr_x = (width - qr_size) // 2
    qr_y = int(height * L["qr_y"])

    label_y = qr_y + qr_size + height * (0.06 if is_landscape else 0.03)
    label_size = max(12, height * (0.030 if is_landscape else 0.018))

    # Text is solid rather than translucent, and small text is semibold, so it stays
    # readable once the background is dithered
    svg = f"""<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" width="{width}" height="{height}">
  <!-- Background -->
  <rect x="0" y="0" width="{width}" height="{height}" fill="{BACKGROUND_COLOR}"/>

  <!-- FrameWay mark -->
  <g transform="translate({icon_x - icon_scale * 0.5}, {icon_y - icon_scale * 0.5}) scale({icon_scale / 512})">
    <path d="{MARK_FRAME_PATH}" fill="none" stroke="{FOREGROUND_COLOR}"
          stroke-width="{MARK_FRAME_STROKE}" stroke-linejoin="round"/>
    <path d="{MARK_LEAF_PATH}" fill="{FOREGROUND_COLOR}"/>
  </g>

  <!-- Title -->
  <text x="{width / 2}" y="{title_y + title_size * 1.2}" text-anchor="middle"
        font-family="sans-serif" font-size="{title_size}" font-weight="600"
        fill="{FOREGROUND_COLOR}">{title}</text>
  <text x="{width / 2}" y="{title_y + title_size * 1.5 + subtitle_size * 1.8}" text-anchor="middle"
        font-family="sans-serif" font-size="{subtitle_size}" font-weight="600"
        fill="{FOREGROUND_COLOR}">{subtitle}</text>

  <!-- QR code placeholder (white background, firmware fills at runtime) -->
  <rect x="{qr_x - 4}" y="{qr_y - 4}"
        width="{qr_size + 8}" height="{qr_size + 8}"
        rx="4" fill="white"/>
  <text x="{width / 2}" y="{label_y}"
        text-anchor="middle" font-family="sans-serif" font-size="{label_size}" font-weight="600"
        fill="{FOREGROUND_COLOR}">{qr_label}</text>

</svg>"""
    return svg, qr_x, qr_y, qr_size


def generate_splash_svg(width: int, height: int) -> tuple:
    """Generate splash screen SVG. Returns (svg_content, wifi_qr_x, wifi_qr_y, wifi_qr_size)."""
    # Bold "Frame" and regular "Way", as in the FrameWay wordmark
    title = '<tspan font-weight="700">Frame</tspan><tspan font-weight="400">Way</tspan>'
    return generate_screen_svg(
        width,
        height,
        title,
        "Your Life, at a Glance",
        "Scan to connect WiFi",
    )


def svg_to_png(svg_path: str, png_path: str, width: int, height: int) -> bool:
    """Convert SVG to PNG using rsvg-convert."""
    rsvg = shutil.which("rsvg-convert")
    if not rsvg:
        print("ERROR: rsvg-convert not found.")
        print("  Ubuntu: sudo apt-get install librsvg2-bin")
        print("  macOS:  brew install librsvg")
        return False

    try:
        subprocess.run(
            [rsvg, "-w", str(width), "-h", str(height), svg_path, "-o", png_path],
            check=True,
            capture_output=True,
        )
        return True
    except subprocess.CalledProcessError as e:
        print(f"ERROR: rsvg-convert failed: {e.stderr.decode()}")
        return False


def _get_process_cli_base(width: int, height: int) -> list:
    """Return base process-cli command args with orientation detection."""
    node = shutil.which("node")
    if not node:
        raise RuntimeError("node not found")

    if not os.path.exists(PROCESS_CLI):
        raise RuntimeError(
            f"process-cli not found at {PROCESS_CLI}\n  Run: cd process-cli && npm ci"
        )

    orientation = "portrait" if height > width else "landscape"
    return [
        node,
        PROCESS_CLI,
        "--placeholder--",  # input path placeholder
        "-d",
        f"{width}x{height}",
        "--orientation",
        orientation,
        "--scale-mode",
        "cover",
    ]


def png_to_epdgz(png_path: str, output_dir: str, width: int, height: int) -> bool:
    """Convert PNG to EPDGZ using process-cli."""
    try:
        cmd = _get_process_cli_base(width, height)
        cmd[2] = png_path  # replace placeholder
        cmd.extend(["-o", output_dir, "--format", "epdgz"])
        subprocess.run(cmd, check=True)
        return True
    except RuntimeError as e:
        print(f"ERROR: {e}")
        return False
    except subprocess.CalledProcessError as e:
        print(f"ERROR: process-cli failed: {e}")
        return False


def generate_setup_complete_svg(width: int, height: int) -> tuple:
    """Generate setup-complete screen SVG. Returns (svg_content, qr_x, qr_y, qr_size)."""
    return generate_screen_svg(
        width,
        height,
        "Setup Complete!",
        "Scan the QR code to get started",
        "Scan to open web interface",
    )


def generate_meta_header(
    qr_x: int,
    qr_y: int,
    qr_size: int,
    setup_qr_x: int,
    setup_qr_y: int,
    setup_qr_size: int,
    output_path: str,
):
    """Generate C header with QR position metadata for both screens."""
    content = (
        "// Auto-generated splash screen metadata\n"
        "// Do not edit manually\n"
        "\n"
        "// OOBE splash screen - WiFi QR code position\n"
        f"#define SPLASH_WIFI_QR_X {qr_x}\n"
        f"#define SPLASH_WIFI_QR_Y {qr_y}\n"
        f"#define SPLASH_WIFI_QR_SIZE {qr_size}\n"
        "\n"
        "// Setup complete screen - web UI QR code position\n"
        f"#define SETUP_COMPLETE_QR_X {setup_qr_x}\n"
        f"#define SETUP_COMPLETE_QR_Y {setup_qr_y}\n"
        f"#define SETUP_COMPLETE_QR_SIZE {setup_qr_size}\n"
    )
    with open(output_path, "w") as f:
        f.write(content)


def main():
    # Import board dimensions from boards.py
    sys.path.insert(0, os.path.dirname(__file__))
    from boards import BOARD_DIMENSIONS

    parser = argparse.ArgumentParser(description="Generate OOBE splash screen EPDGZ")
    size_group = parser.add_mutually_exclusive_group(required=True)
    size_group.add_argument(
        "--board",
        choices=BOARD_DIMENSIONS.keys(),
        help="Board name (looks up display resolution from boards.py)",
    )
    size_group.add_argument(
        "--size",
        choices=SCREEN_SIZES.keys(),
        help="Display resolution directly (e.g. 800x480)",
    )
    parser.add_argument(
        "--output-dir",
        default=os.path.join(os.path.dirname(__file__), "..", "main", "splash_data"),
        help="Output directory for EPDGZ, PNG preview, and metadata",
    )
    parser.add_argument(
        "--svg-only",
        action="store_true",
        help="Only generate SVG (skip EPDGZ conversion)",
    )
    args = parser.parse_args()

    if args.board:
        w, h = BOARD_DIMENSIONS[args.board]
    else:
        w, h = SCREEN_SIZES[args.size]

    os.makedirs(args.output_dir, exist_ok=True)

    # Generate both splash screens
    screens = [
        ("splash", generate_splash_svg),
        ("setup_complete", generate_setup_complete_svg),
    ]

    all_meta = {}

    for name, gen_func in screens:
        print(f"\n=== {name} ({w}x{h}) ===")

        svg_content, qr_x, qr_y, qr_size = gen_func(w, h)
        all_meta[name] = (qr_x, qr_y, qr_size)
        print(f"  QR placeholder: x={qr_x} y={qr_y} size={qr_size}")

        if args.svg_only:
            svg_path = os.path.join(args.output_dir, f"{name}.svg")
            with open(svg_path, "w") as f:
                f.write(svg_content)
            print(f"  SVG: {svg_path}")
            continue

        with tempfile.NamedTemporaryFile(suffix=".svg", delete=False) as tmp_svg:
            tmp_svg.write(svg_content.encode())
            tmp_svg_path = tmp_svg.name

        png_path = os.path.join(args.output_dir, f"{name}.png")

        try:
            if not svg_to_png(tmp_svg_path, png_path, w, h):
                sys.exit(1)
            print(f"  PNG: {png_path}")

            print(f"  Converting to EPDGZ via process-cli...")
            if not png_to_epdgz(png_path, args.output_dir, w, h):
                print(f"  ERROR: Failed to generate EPDGZ")
                sys.exit(1)

            epdgz_path = os.path.join(args.output_dir, f"{name}.epdgz")
            print(f"  EPDGZ: {epdgz_path}")

            # Clean up thumbnail generated by process-cli
            thumb_path = os.path.join(args.output_dir, f"{name}.jpg")
            if os.path.exists(thumb_path):
                os.unlink(thumb_path)

        finally:
            os.unlink(tmp_svg_path)

    # Write metadata header
    splash_meta = all_meta["splash"]
    setup_meta = all_meta["setup_complete"]
    meta_path = os.path.join(args.output_dir, "splash_meta.h")
    generate_meta_header(
        splash_meta[0],
        splash_meta[1],
        splash_meta[2],
        setup_meta[0],
        setup_meta[1],
        setup_meta[2],
        meta_path,
    )
    print(f"\n  Metadata: {meta_path}")


if __name__ == "__main__":
    main()
