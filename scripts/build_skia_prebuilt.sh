#!/usr/bin/env bash
# Build a sk_capi-shaped Skia CPU prebuilt tarball (as-needed; not every Scuzz release).
#
# Produces: dist/skia-<triple>-cpu.tar.gz containing libsk_capi.a (+ fonts/ copy).
#
# Env:
#   SCUZZ_SKIA_BRANCH   Skia git branch/tag (default chrome/m131)
#   SCUZZ_SKIA_TRIPLE   output triple (default from scripts/skia_triple.sh)
#   SCUZZ_SKIA_WORK     work directory (default /tmp/scuzz-skia-build)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TRIPLE="${SCUZZ_SKIA_TRIPLE:-"$("$ROOT/scripts/skia_triple.sh")"}"
BRANCH="${SCUZZ_SKIA_BRANCH:-chrome/m131}"
WORK="${SCUZZ_SKIA_WORK:-/tmp/scuzz-skia-build}"
OUT_DIR="${ROOT}/dist"
ASSET="skia-${TRIPLE}-cpu.tar.gz"
SHIM="${ROOT}/crates/ffi-skia/src/sk_capi_skia.cpp"
BRIDGE="${ROOT}/crates/ffi-skia/src/sk_capi_skia_bridge.c"

echo "build_skia_prebuilt: triple=${TRIPLE} branch=${BRANCH} work=${WORK}"

mkdir -p "${WORK}" "${OUT_DIR}"
SKIA="${WORK}/skia"
if [[ ! -d "${SKIA}/.git" ]]; then
  echo "==> cloning Skia (${BRANCH})"
  git clone --depth 1 --branch "${BRANCH}" https://skia.googlesource.com/skia.git "${SKIA}"
else
  echo "==> reusing Skia checkout at ${SKIA}"
fi

cd "${SKIA}"
echo "==> sync deps"
# Skip emsdk — CPU sk_capi prebuilt does not need WASM toolchains.
GIT_SYNC_DEPS_SKIP_EMSDK=1 python3 tools/git-sync-deps

FONT_DIR="${WORK}/fonts"
mkdir -p "${FONT_DIR}"
FONT_TTF="${FONT_DIR}/DejaVuSans.ttf"
if [[ ! -f "${FONT_TTF}" ]]; then
  echo "==> fetching DejaVuSans.ttf"
  if [[ -f /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf ]]; then
    cp -f /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf "${FONT_TTF}"
  else
    curl -fsSL -o "${FONT_TTF}" \
      "https://cdn.jsdelivr.net/npm/dejavu-fonts-ttf@2.37.3/ttf/DejaVuSans.ttf"
  fi
fi
test -s "${FONT_TTF}"

# Generate embedded font bytes for deterministic Headless text.
FONT_INC="${WORK}/scuzz_embedded_font.c"
xxd -i -n scuzz_embedded_font "${FONT_TTF}" >"${FONT_INC}"

# Hermetic CPU build. Force FreeType + custom fontmgr on all hosts (including
# Darwin, where Skia defaults to CoreText and omits the freetype2 ninja target)
# so sk_capi_skia.cpp's SkFontMgr_New_Custom_* path links.
GN_ARGS=(
  'is_official_build=true'
  'is_component_build=false'
  'skia_use_system_expat=false'
  'skia_use_system_icu=false'
  'skia_use_system_libjpeg_turbo=false'
  'skia_use_system_libpng=false'
  'skia_use_system_libwebp=false'
  'skia_use_system_zlib=false'
  'skia_use_system_harfbuzz=false'
  'skia_use_freetype=true'
  'skia_use_system_freetype2=false'
  'skia_enable_fontmgr_custom_embedded=true'
  'skia_enable_fontmgr_custom_empty=true'
  'skia_use_fontconfig=false'
  'skia_enable_gpu=false'
  'skia_use_gl=false'
  'skia_use_metal=false'
  'skia_enable_pdf=false'
  'skia_enable_svg=false'
  'skia_use_x11=false'
)

# Skia's macOS GN defaults to Intel; set arm64 for Apple Silicon triples.
case "${TRIPLE}" in
  aarch64-apple-darwin|aarch64-*-darwin*) GN_ARGS+=('target_cpu="arm64"') ;;
  x86_64-apple-darwin|x86_64-*-darwin*) GN_ARGS+=('target_cpu="x64"') ;;
esac

echo "==> gn gen"
bin/gn gen out/Static --args="${GN_ARGS[*]}"
echo "==> ninja skia (+ freetype2/harfbuzz)"
ninja -C out/Static skia freetype2 harfbuzz

PKG="${WORK}/pkg"
rm -rf "${PKG}"
mkdir -p "${PKG}/fonts" "${PKG}/obj"
cp -f "${FONT_TTF}" "${PKG}/fonts/DejaVuSans.ttf"

echo "==> compile sk_capi shim + bridge + embedded font"
c++ -std=c++17 -O2 -fPIC -c "${SHIM}" -o "${PKG}/obj/sk_capi_skia.o" \
  -I"${SKIA}" \
  -DSK_RELEASE \
  -DSCUZZ_SKIA_EMBEDDED_FONT

cc -O2 -fPIC -c "${BRIDGE}" -o "${PKG}/obj/sk_capi_skia_bridge.o" \
  -I"${ROOT}/crates/ffi-skia/include"

cc -O2 -fPIC -c "${FONT_INC}" -o "${PKG}/obj/scuzz_embedded_font.o"

echo "==> pack libsk_capi.a"
cp -f "${SKIA}/out/Static/libskia.a" "${PKG}/libskia.a"
# Also pull companion static libs produced by the Skia build into the fat archive.
for dep in freetype2 harfbuzz zlib png jpeg skcms; do
  if [[ -f "${SKIA}/out/Static/lib${dep}.a" ]]; then
    cp -f "${SKIA}/out/Static/lib${dep}.a" "${PKG}/lib${dep}.a"
  fi
done

SHIM_OBJS=(
  "${PKG}/obj/sk_capi_skia.o"
  "${PKG}/obj/sk_capi_skia_bridge.o"
  "${PKG}/obj/scuzz_embedded_font.o"
)
ARCHIVES=( "${PKG}/libskia.a" )
for dep in freetype2 harfbuzz zlib png jpeg skcms; do
  if [[ -f "${PKG}/lib${dep}.a" ]]; then
    ARCHIVES+=( "${PKG}/lib${dep}.a" )
  fi
done

# Prefer merging everything into one fat archive for simple linking.
# Darwin: Apple libtool merges .a/.o without GNU ar -M (unsupported by BSD ar).
# Linux: extract objects and use GNU ar (MRI script when the object count is large).
if [[ "$(uname -s)" == "Darwin" ]]; then
  libtool -static -o "${PKG}/libsk_capi.a" "${ARCHIVES[@]}" "${SHIM_OBJS[@]}"
else
  COMBINED="${PKG}/combine"
  rm -rf "${COMBINED}"
  mkdir -p "${COMBINED}"
  (
    cd "${COMBINED}"
    for archive in "${ARCHIVES[@]}"; do
      ar x "${archive}" 2>/dev/null || ar -x "${archive}"
    done
    cp -f "${SHIM_OBJS[@]}" .
    objs=( ./*.o )
    if ((${#objs[@]} > 500)); then
      {
        echo "CREATE ${PKG}/libsk_capi.a"
        for o in ./*.o; do echo "ADDMOD $o"; done
        echo "SAVE"
        echo "END"
      } | ar -M
    else
      ar rcs "${PKG}/libsk_capi.a" ./*.o
    fi
  )
  rm -rf "${COMBINED}"
fi

rm -f "${PKG}"/libskia.a "${PKG}"/libfreetype2.a "${PKG}"/libharfbuzz.a \
  "${PKG}"/libzlib.a "${PKG}"/libpng.a "${PKG}"/libjpeg.a "${PKG}"/libskcms.a
rm -rf "${PKG}/obj"

{
  echo "skia_branch=${BRANCH}"
  echo "triple=${TRIPLE}"
  echo "built=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} >"${PKG}/VERSION"

tar -C "${PKG}" -czf "${OUT_DIR}/${ASSET}" libsk_capi.a fonts VERSION
echo "build_skia_prebuilt: wrote ${OUT_DIR}/${ASSET}"
ls -lh "${OUT_DIR}/${ASSET}"
