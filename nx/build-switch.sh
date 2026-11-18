#!/usr/bin/env bash
# Builds GZDoom for the Nintendo Switch. Run inside a devkitA64 container, from the repository root:
#
#   docker run --rm -v "$PWD":/gzdoom -w /gzdoom devkitpro/devkita64:latest bash nx/build-switch.sh
#
# Environment:
#   HAVE_VULKAN=ON|OFF   Vulkan needs a Mesa NVK libvulkan.a in portlibs, which devkitPro's own
#                        packages don't have. OFF (default) builds the OpenGL / GLES renderers only.
#   BUILD_DIR            Output directory (default: build-switch)
#
# Result: $BUILD_DIR/dist/switch/gzdoom, ready to copy to the SD card's /switch folder.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HAVE_VULKAN="${HAVE_VULKAN:-OFF}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-switch}"
DEPS_DIR="$BUILD_DIR/deps"
HOST_DIR="$BUILD_DIR/host"
JOBS="$(nproc)"
SNDFILE_VERSION=1.2.2
ZMUSIC_VERSION=1.3.0

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export PATH="$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$PATH"
PORTLIBS="$DEVKITPRO/portlibs/switch"
TOOLCHAIN="$DEVKITPRO/cmake/Switch.cmake"
POLICY="-DCMAKE_POLICY_VERSION_MINIMUM=3.5"

step() { echo; echo "=== $* ==="; }

mkdir -p "$DEPS_DIR"

step "1/5 Host tools (lemon, re2c, zipdir)"
cmake -S "$ROOT/tools/host" -B "$HOST_DIR" -DCMAKE_BUILD_TYPE=Release $POLICY
cmake --build "$HOST_DIR" -j"$JOBS"

step "2/5 libsndfile $SNDFILE_VERSION"
if [ ! -f "$PORTLIBS/lib/libsndfile.a" ]; then
	if [ ! -d "$DEPS_DIR/libsndfile-$SNDFILE_VERSION" ]; then
		curl -L --fail -o "$DEPS_DIR/libsndfile.tar.xz" \
			"https://github.com/libsndfile/libsndfile/releases/download/$SNDFILE_VERSION/libsndfile-$SNDFILE_VERSION.tar.xz"
		tar -xJf "$DEPS_DIR/libsndfile.tar.xz" -C "$DEPS_DIR"
	fi
	cmake -S "$DEPS_DIR/libsndfile-$SNDFILE_VERSION" -B "$DEPS_DIR/libsndfile-build" $POLICY \
		-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX="$PORTLIBS" \
		-DBUILD_SHARED_LIBS=OFF -DBUILD_PROGRAMS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF \
		-DENABLE_EXTERNAL_LIBS=ON -DENABLE_MPEG=OFF -DENABLE_CPACK=OFF \
		-DENABLE_PACKAGE_CONFIG=OFF -DINSTALL_PKGCONFIG_MODULE=OFF
	cmake --build "$DEPS_DIR/libsndfile-build" -j"$JOBS"
	cmake --install "$DEPS_DIR/libsndfile-build"
else
	echo "already installed"
fi

step "3/5 ZMusic $ZMUSIC_VERSION"
if [ ! -f "$PORTLIBS/lib/libzmusic.a" ]; then
	if [ ! -d "$DEPS_DIR/ZMusic" ]; then
		git clone --depth 1 --branch "$ZMUSIC_VERSION" https://github.com/ZDoom/ZMusic.git "$DEPS_DIR/ZMusic"
		# No glib (so no bundled FluidSynth) and no dynamic loader on the Switch.
		git -C "$DEPS_DIR/ZMusic" apply "$ROOT/nx/patches/zmusic-$ZMUSIC_VERSION-switch.patch"
	fi
	cmake -S "$DEPS_DIR/ZMusic" -B "$DEPS_DIR/zmusic-build" $POLICY \
		-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX="$PORTLIBS" \
		-DBUILD_SHARED_LIBS=OFF -DZMUSIC_NO_FLUIDSYNTH=ON \
		-DDYN_SNDFILE=OFF -DDYN_MPG123=OFF
	cmake --build "$DEPS_DIR/zmusic-build" -j"$JOBS"
	cmake --install "$DEPS_DIR/zmusic-build"
else
	echo "already installed"
fi

step "4/5 GZDoom (HAVE_VULKAN=$HAVE_VULKAN)"
cmake -S "$ROOT" -B "$BUILD_DIR/gzdoom" $POLICY \
	-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DNINTENDO_SWITCH=ON -DCMAKE_BUILD_TYPE=Release \
	-DNO_OPENMP=ON -DNO_GTK=ON \
	-DHAVE_VULKAN="$HAVE_VULKAN" -DHAVE_GLES2=ON \
	-DIMPORT_EXECUTABLES="$HOST_DIR/ImportExecutables.cmake" \
	-DZMUSIC_INCLUDE_DIR="$PORTLIBS/include" \
	-DZMUSIC_LIBRARIES="$PORTLIBS/lib/libzmusic.a"
cmake --build "$BUILD_DIR/gzdoom" -j"$JOBS"

step "5/5 Packaging"
OUT="$BUILD_DIR/dist/switch/gzdoom"
G="$BUILD_DIR/gzdoom"
rm -rf "$BUILD_DIR/dist"
mkdir -p "$OUT/iwads" "$OUT/pwads"
for f in gzdoom.nro gzdoom_launcher.nro gzdoom.pk3 brightmaps.pk3 lights.pk3 game_support.pk3 game_widescreen_gfx.pk3; do
	src="$(find "$G" -maxdepth 2 -name "$f" | head -n 1)"
	[ -n "$src" ] || { echo "ERROR: $f was not built"; exit 1; }
	cp "$src" "$OUT/"
done
for d in soundfonts fm_banks; do
	src="$(find "$G" -maxdepth 2 -type d -name "$d" | head -n 1)"
	if [ -n "$src" ]; then cp -r "$src" "$OUT/"; fi
done
[ -d "$OUT/soundfonts" ] || { mkdir -p "$OUT/soundfonts"; cp "$ROOT"/soundfont/*.sf2 "$OUT/soundfonts/" 2>/dev/null || true; }
[ -d "$OUT/fm_banks" ] || cp -r "$ROOT/fm_banks" "$OUT/fm_banks"
cp "$ROOT/nx/gzdoom.ini" "$OUT/gzdoom.ini.default"
ls -la "$OUT"
echo "Done: copy the 'switch' folder in $BUILD_DIR/dist to the root of the SD card."
