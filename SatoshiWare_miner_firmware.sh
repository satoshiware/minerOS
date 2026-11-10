#!/usr/bin/env bash
# Upload the shared firmware files to one miner already on Wi-Fi.
#
# Put this script in the same folder as esp-miner.bin and www.bin, then:
#   ./SatoshiWare_miner_firmware.sh 192.168.38.62
#
# The next miner is the same command with that miner's IP.
# The computer needs curl. This script installs curl when it is missing.
# The miner must already be on the network. The update is refused while the
# miner is in its setup access point. After the install, open the printed
# address, hard-refresh, and set the pool host, port, user, and password.
# Split stays off until that switch is turned on.

set -euo pipefail

MINER_IP=""

usage() {
  echo "Usage: ./SatoshiWare_miner_firmware.sh <miner-ip>" >&2
  echo "Example: ./SatoshiWare_miner_firmware.sh 192.168.38.62" >&2
  echo "Run it from the folder that contains esp-miner.bin and www.bin." >&2
  exit 1
}

if [[ $# -ne 1 || "$1" == "-h" || "$1" == "--help" ]]; then
  usage
fi

MINER_IP="$1"
if [[ ! "$MINER_IP" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "The miner IP must look like 192.168.38.62" >&2
  usage
fi

ensure_curl() {
  if command -v curl >/dev/null 2>&1; then
    return
  fi
  echo "curl is missing. Installing it."
  if [[ "$(uname -s)" == "Darwin" ]]; then
    if command -v brew >/dev/null 2>&1; then
      brew install curl
    else
      echo "Install curl, then run this again." >&2
      exit 1
    fi
  elif command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update && sudo apt-get install -y curl
  elif command -v dnf >/dev/null 2>&1; then
    sudo dnf install -y curl
  elif command -v yum >/dev/null 2>&1; then
    sudo yum install -y curl
  elif command -v pacman >/dev/null 2>&1; then
    sudo pacman -S --noconfirm curl
  else
    echo "Install curl, then run this again." >&2
    exit 1
  fi
  if ! command -v curl >/dev/null 2>&1; then
    echo "curl is still missing." >&2
    exit 1
  fi
}

ensure_curl

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -f "$PWD/esp-miner.bin" && -f "$PWD/www.bin" ]]; then
  FW="$PWD/esp-miner.bin"
  WWW="$PWD/www.bin"
elif [[ -f "$SCRIPT_DIR/esp-miner.bin" && -f "$SCRIPT_DIR/www.bin" ]]; then
  FW="$SCRIPT_DIR/esp-miner.bin"
  WWW="$SCRIPT_DIR/www.bin"
else
  echo "Put esp-miner.bin and www.bin in this folder, then run the script again." >&2
  exit 1
fi

wait_for_miner() {
  local i
  for i in $(seq 1 90); do
    if curl -fsS --max-time 3 "http://$MINER_IP/api/system/info" >/dev/null 2>&1; then
      return 0
    fi
    sleep 2
  done
  echo "The miner at $MINER_IP did not come back." >&2
  return 1
}

echo "Miner: http://$MINER_IP"
echo "Firmware: $FW"
echo "Web page: $WWW"

echo "Checking the miner is on Wi-Fi"
if ! curl -fsS --max-time 5 "http://$MINER_IP/api/system/info" >/dev/null; then
  echo "Cannot reach http://$MINER_IP. Join the miner to Wi-Fi first. Updates are refused in setup-AP mode." >&2
  exit 1
fi

echo "Installing firmware on $MINER_IP"
curl -fS --max-time 300 -X POST \
  -H "Content-Type: application/octet-stream" \
  --data-binary @"$FW" \
  "http://$MINER_IP/api/system/OTA"
echo
echo "Waiting for the reboot"
sleep 8
wait_for_miner

echo "Installing the web page on $MINER_IP"
curl -fS --max-time 300 -X POST \
  -H "Content-Type: application/octet-stream" \
  --data-binary @"$WWW" \
  "http://$MINER_IP/api/system/OTAWWW"
echo

echo "Restarting so the new page is the one that loads"
curl -fS --max-time 30 -X POST "http://$MINER_IP/api/system/restart" >/dev/null || true
sleep 8
wait_for_miner

echo
echo "Installed on http://$MINER_IP"
echo "Hard-refresh that page (Command+Shift+R)."
echo "On the Pool page, set the host, port, user, and password, then Save."
echo "Split stays off until you turn it on and restart."
