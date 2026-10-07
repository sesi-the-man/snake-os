#!/usr/bin/env bash
# Builds the bootable SD card image. Meant to run inside the Docker container
# (see docs/BUILDING.md), but works on any Linux host with Buildroot dependencies.
set -euo pipefail

# The build runs as root inside the Docker container. Some host tools Buildroot
# compiles (e.g. tar) refuse to configure as root unless this is set.
export FORCE_UNSAFE_CONFIGURE=1

usage() {
  cat <<USAGE
Usage: ./build.sh [options]

  (no options)     Clean build of images/snake_os.<version>.pi0.img
  --no-clean       Reuse the previous build output (faster, less reliable)
  --legal-info     Also collect licenses and source code for everything in the
                   image into images/snake_os.<version>.legal-info.tar.gz
  -h, --help       Show this help
USAGE
}

CLEAN=1
LEGAL=0
for arg in "$@"; do
  case "$arg" in
    --no-clean)   CLEAN=0 ;;
    --legal-info) LEGAL=1 ;;
    -h|--help)    usage; exit 0 ;;
    *)            echo "Unknown option: $arg"; usage; exit 1 ;;
  esac
done

cd "$(dirname "$0")"
REPO="$(pwd)"
OUT="${OUTPUT_DIR:-/output}"
IMAGES="${REPO}/images"

if [ ! -f buildroot/Makefile ]; then
  echo "The buildroot submodule is missing. Run: git submodule update --init"
  exit 1
fi

# Version comes from the git tag (e.g. v1.1), falling back to the commit hash.
VERSION="$(git -c safe.directory="*" describe --tags --always --dirty 2>/dev/null || echo dev)"
echo "Building version ${VERSION}"

if [ "${CLEAN}" = "1" ]; then
  rm -rf "${OUT}"
fi
mkdir -p "${OUT}" "${IMAGES}"

export PATH="/usr/lib/ccache:${PATH}"
make BR2_EXTERNAL="${REPO}/br2-external" O="${OUT}" -C buildroot snake_pi0_defconfig
make -C "${OUT}"

NAME="snake_os.${VERSION}.pi0"
cp -f "${OUT}/images/sdcard.img" "${IMAGES}/${NAME}.img"

if [ "${LEGAL}" = "1" ]; then
  make -C "${OUT}" legal-info
  tar -C "${OUT}" -czf "${IMAGES}/snake_os.${VERSION}.legal-info.tar.gz" legal-info
fi

cd "${IMAGES}"
sha256sum "${NAME}.img" > "snake_os.${VERSION}.sha256.txt"
echo
cat "snake_os.${VERSION}.sha256.txt"
echo "Done: images/${NAME}.img"
