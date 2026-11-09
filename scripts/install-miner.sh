#!/usr/bin/env bash
# Install this firmware on a StarMiner that is already joined to Wi-Fi.
#
# On a new PC:
#   git clone --recurse-submodules --branch main --single-branch \
#     https://github.com/satoshiware/minerOS.git
#   cd minerOS
#   ./scripts/install-miner.sh 192.168.1.50
#
# The miner must already be on the network. The update is refused while the
# miner is in its setup access point. After the install, open the printed
# address, hard-refresh, and set the pool host, port, user, and password.
# Split stays off until that switch is turned on.
#
# Override the source with REPO_URL and BRANCH. The company repo publishes
# this firmware on main.

set -euo pipefail

REPO_URL="${REPO_URL:-https://github.com/satoshiware/minerOS.git}"
BRANCH="${BRANCH:-main}"
ASSUME_YES=0
MINER_IP=""

usage() {
  echo "Usage: $0 [--yes] <miner-ip>" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --yes|-y) ASSUME_YES=1; shift ;;
    -h|--help) usage ;;
    *)
      if [[ -n "$MINER_IP" ]]; then
        usage
      fi
      MINER_IP="$1"
      shift
      ;;
  esac
done

if [[ ! "$MINER_IP" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Give the miner IP address, for example 192.168.1.50" >&2
  usage
fi

for cmd in git docker curl; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "This PC needs $cmd." >&2
    exit 1
  fi
done

if ! docker info >/dev/null 2>&1; then
  echo "Docker is installed but not running." >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -f "$SCRIPT_DIR/../CMakeLists.txt" && -f "$SCRIPT_DIR/../version.txt" ]]; then
  SRC="$(cd "$SCRIPT_DIR/.." && pwd)"
else
  SRC="${SRC_DIR:-$HOME/minerOS}"
  if [[ ! -f "$SRC/CMakeLists.txt" ]]; then
    echo "Cloning $BRANCH from $REPO_URL"
    git clone --recurse-submodules --branch "$BRANCH" --single-branch "$REPO_URL" "$SRC"
  else
    echo "Updating $SRC"
    git -C "$SRC" fetch origin "$BRANCH"
    git -C "$SRC" checkout "$BRANCH"
    git -C "$SRC" pull --ff-only origin "$BRANCH"
    git -C "$SRC" submodule update --init --recursive
  fi
fi

if [[ ! -d "$SRC/components/libsecp256k1/libsecp256k1/.git" && ! -f "$SRC/components/libsecp256k1/libsecp256k1/CMakeLists.txt" ]]; then
  git -C "$SRC" submodule update --init --recursive
fi

echo "Miner:  http://$MINER_IP"
echo "Source: $SRC ($BRANCH)"
if [[ "$ASSUME_YES" -ne 1 ]]; then
  read -r -p "Build and install the firmware and the web page on this miner? [y/N] " answer
  if [[ ! "$answer" =~ ^[Yy]$ ]]; then
    echo "Cancelled."
    exit 0
  fi
fi

if ! docker image inspect espminer-build >/dev/null 2>&1; then
  echo "Building the compiler image. The first time takes a while."
  docker build -t espminer-build "$SRC/.devcontainer"
fi

echo "Building esp-miner.bin and www.bin"
docker run --rm \
  -v "$SRC":/workspaces \
  -w /workspaces \
  -e GIT_CONFIG_COUNT=1 \
  -e GIT_CONFIG_KEY_0=safe.directory \
  -e GIT_CONFIG_VALUE_0=/workspaces \
  espminer-build idf.py build

FW="$SRC/build/esp-miner.bin"
WWW="$SRC/build/www.bin"
if [[ ! -f "$FW" || ! -f "$WWW" ]]; then
  echo "The build did not produce both install files." >&2
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

echo "Checking the miner is on Wi-Fi"
if ! curl -fsS --max-time 5 "http://$MINER_IP/api/system/info" >/dev/null; then
  echo "Cannot reach http://$MINER_IP. Join the miner to Wi-Fi first. Updates are refused in setup-AP mode." >&2
  exit 1
fi

echo "Installing firmware"
curl -fS --max-time 300 -X POST \
  -H "Content-Type: application/octet-stream" \
  --data-binary @"$FW" \
  "http://$MINER_IP/api/system/OTA"
echo
echo "Waiting for the reboot"
sleep 8
wait_for_miner

echo "Installing the web page"
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
echo "Installed. Open http://$MINER_IP and hard-refresh (Command+Shift+R)."
echo "On the Pool page, set the host, port, user, and password, then Save."
echo "Split stays off until you turn it on and restart."
if command -v python3 >/dev/null 2>&1; then
  curl -fsS --max-time 5 "http://$MINER_IP/api/system/info" \
    | python3 -c 'import json,sys; d=json.load(sys.stdin); print("Version:", d.get("version"), " Page:", d.get("axeOSVersion"))' \
    || true
fi
