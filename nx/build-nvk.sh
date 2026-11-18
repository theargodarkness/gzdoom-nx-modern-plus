#!/usr/bin/env bash
# Builds the Vulkan driver for the Nintendo Switch (Mesa NVK for Horizon, from
# https://github.com/NaGaa95/mesa-switch) as one static libvulkan.a.
#
# Run as root inside devkitpro/devkita64, with host packages already installed
# (see .github/workflows/nvk.yml). Follows mesa-switch's own build-switch.sh
# and CI workflow.
#
#   bash nx/build-nvk.sh <mesa-switch checkout> <output dir>
#
# The output directory receives lib/libvulkan.a, lib/pkgconfig/vulkan.pc and
# include/vulkan, laid out like a portlibs prefix.
set -euo pipefail

MESA="$(cd "$1" && pwd)"
OUT="$(mkdir -p "$2" && cd "$2" && pwd)"

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITA64="$DEVKITPRO/devkitA64"
export PATH="$DEVKITA64/bin:$DEVKITPRO/tools/bin:${CARGO_HOME:-/root/.cargo}/bin:$PATH"
# rustc-switch-wrapper.sh only cross-compiles when this is set.
export MESA_SWITCH_RUST_TARGET=aarch64-unknown-linux-gnu
export LC_ALL=C TZ=UTC
PORTLIBS="$DEVKITPRO/portlibs/switch"
SPIRV_PREFIX="${SPIRV_PREFIX:-/opt/spirv-tools}"

step() { echo; echo "=== $* ==="; }

cd "$MESA"
git config --global --add safe.directory "$MESA" || true

step "Cross-compilation wrappers"
mkdir -p /usr/local/libexec
install -m 755 bindgen-switch-wrapper.sh /usr/local/libexec/bindgen
install -m 755 rustc-switch-wrapper.sh /usr/local/libexec/rustc
install -m 644 bindgen-atomic-shim.h /usr/local/libexec/bindgen-atomic-shim.h
install -m 644 src/nouveau/headers/nv_device_info.h "$PORTLIBS/include/"

step "LLVM 15 for the native shader tools"
llvm_config=/usr/bin/llvm-config-15
libdir="$("$llvm_config" --libdir)"
version="$("$llvm_config" --version)"
major="${version%%.*}"
[ -e "$libdir/clang/$version" ] || ln -sfn "$major" "$libdir/clang/$version"
[ -e "$libdir/clang/$major" ] || ln -sfn "$version" "$libdir/clang/$major"
printf "[binaries]\nllvm-config = '%s'\n" "$llvm_config" > /tmp/llvm-native.txt

step "Stub archives for Rust std (dl, rt, util)"
if [ ! -f "$PORTLIBS/lib/libdl.a" ]; then
	tmp="$(mktemp -d)"
	echo 'void __mesa_switch_stub_lib(void) {}' > "$tmp/stub_posix.c"
	aarch64-none-elf-gcc -c "$tmp/stub_posix.c" -o "$tmp/stub_posix.o"
	for l in dl rt util; do
		aarch64-none-elf-ar rcs "$PORTLIBS/lib/lib${l}.a" "$tmp/stub_posix.o"
	done
fi

step "Native host tools (mesa_clc, vtn_bindgen2)"
export PKG_CONFIG_PATH="$SPIRV_PREFIX/lib/pkgconfig"
export CMAKE_PREFIX_PATH="$SPIRV_PREFIX"
meson setup builddir-native \
	--native-file /tmp/llvm-native.txt \
	--buildtype=release \
	-Dvulkan-drivers= -Dgallium-drivers= -Dshader-cache=enabled -Dplatforms= \
	-Dglx=disabled -Degl=disabled -Dopengl=false -Dgles1=disabled -Dgles2=disabled \
	-Dtools=[] -Dllvm=enabled -Dmesa-clc=enabled -Dmesa-clc-bundle-headers=enabled \
	-Dprecomp-compiler=enabled -Dinstall-mesa-clc=true
ninja -C builddir-native src/compiler/clc/mesa_clc src/compiler/spirv/vtn_bindgen2
export PATH="/usr/local/libexec:$MESA/builddir-native/src/compiler/clc:$MESA/builddir-native/src/compiler/spirv:$PATH"
unset PKG_CONFIG_PATH CMAKE_PREFIX_PATH

step "Configure NVK for the Switch"
meson setup builddir-switch \
	--cross-file switch_cross_file.txt \
	--default-library=static \
	--prefix="$PORTLIBS" --libdir=lib \
	--buildtype=release -Doptimization=2 -Db_lto=false -Db_ndebug=true \
	-Dvulkan-drivers=nouveau -Dgallium-drivers= -Dgallium-rusticl=false \
	-Dplatforms=switch \
	-Dglx=disabled -Degl=disabled -Dopengl=false -Dgles1=disabled -Dgles2=disabled \
	-Dvideo-codecs= -Dshader-cache=enabled -Dxmlconfig=enabled -Dexpat=enabled \
	-Dtools=[] -Dllvm=disabled -Dshared-glapi=disabled -Dshared-llvm=disabled \
	-Dmesa-clc=system -Dprecomp-compiler=system -Dcpp_rtti=false -Dbuild-tests=false \
	-Dnvk-build-id="$(git rev-parse HEAD)"

step "Build"
ninja -C builddir-switch

step "Stage"
rm -rf /tmp/nvk-stage
meson install -C builddir-switch --destdir /tmp/nvk-stage
cp -a "/tmp/nvk-stage$PORTLIBS/." "$OUT/"
for pc in "$OUT"/lib/pkgconfig/*.pc; do
	sed -i 's|^prefix=.*|prefix=${pcfiledir}/../..|' "$pc"
done
git rev-parse HEAD > "$OUT/MESA_COMMIT"
find "$OUT" -maxdepth 3 | sort | head -n 60
ls -la "$OUT/lib/libvulkan.a"
