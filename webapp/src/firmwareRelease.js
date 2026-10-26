// Firmware releases are published to this repository; the device's OTA check uses the same one
// (GITHUB_API_URL in main/config.h)
export const FIRMWARE_REPO = "bennchong/frameway-firmware";
export const FIRMWARE_RELEASES_URL = `https://github.com/${FIRMWARE_REPO}/releases`;
export const FIRMWARE_LATEST_RELEASE_API = `https://api.github.com/repos/${FIRMWARE_REPO}/releases/latest`;
