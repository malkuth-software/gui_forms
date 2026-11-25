#!/bin/sh
# Measure what the compiler caches give a newcomer and an application, cold
# against restored, on macOS or Linux. For the integrator's batch build host.
#
#   sh gui_forms/tools/measure_cache_bootstrap.sh ROOT SCENARIO [ARGS]
#
# ROOT is an empty scratch directory on the build host (several GB per
# scenario); every scenario makes its own workspace ROOT/<name>, so each one
# also proves that a cache works at another workspace path. It prints one
# summary line per scenario and keeps the full logs in ROOT:
#
#   <name> rc=<exit> seconds=<build time> hits=<n> misses=<n>
#
# Scenarios, in the order the cache-bootstrap report runs them:
#
#   newcomer NAME HARNESS_URL
#       The real bootstrap from the template_harness at HARNESS_URL (a clone
#       URL or path; its default branch), with --no-smoke, then the smoke build
#       (ci_build.py --install-only) in the environment it wrote
#       (.ci/local-build-env.sh). On a machine whose compiler or macOS SDK
#       differs from CI's the bootstrap's restore step says so, and the
#       restored cache cannot hit (M4, 2026-11-24: 8 of 1273 hits).
#   cold NAME REV
#       gui_forms at REV, an empty compiler cache, the same environment.
#   seeded NAME REV SEED_WORKSPACE
#       As cold, with the compiler cache copied from SEED_WORKSPACE (a
#       workspace an earlier scenario built). With the seed's own REV it
#       measures a restore at another path (M4: 1265 of 1266 hits); with a
#       newer REV, "last stable to latest" (run blast-radius after it).
#   blast-radius OLD_WORKSPACE NEW_WORKSPACE
#       Which changed inputs made NEW's objects miss OLD's cache
#       (tools/cache_blast_radius.py): not a build.
#   app-intree NAME SEED_WORKSPACE|-
#       Pocket Reference (main) compiling GUI.Forms at its pin in its own
#       tree, as its CI does; the compiler cache copied from SEED_WORKSPACE,
#       or empty with -. Shows whether GUI.Forms' cache serves an app.
#   app-sdk NAME SDK_WORKSPACE
#       Pocket Reference (main) linking the SDK SDK_WORKSPACE installed
#       (.build/native-<platform>/gui-forms-sdk), packed and extracted under
#       NAME as gui-forms-sdk-<platform>.tar.gz would be: the release asset
#       an app downloads instead of compiling GUI.Forms.
#
# Environment: JOBS (default 6); GUI_FORMS_URL (default
# https://github.com/malkuth-software/gui_forms) and POCKET_REFERENCE_URL;
# on macOS clang 22.1.8 from Homebrew's llvm@22 is put first on PATH. The
# cold, seeded and app scenarios take the macOS runtime from CI's newest
# published cache archive (downloaded once into ROOT/downloads), so their
# objects are comparable with each other; the newcomer scenario restores or
# builds its own runtime as the bootstrap does.
#
# A suggested batch on the build host (about an hour at JOBS=6 on the M4):
#   R=<scratch>; T=gui_forms/tools/measure_cache_bootstrap.sh
#   sh $T $R newcomer newcomer <harness clone URL with claude/cache-bootstrap as default branch>
#   sh $T $R cold stable <stable release commit>
#   sh $T $R seeded stable-elsewhere <the same commit> $R/stable
#   sh $T $R seeded latest-from-stable <main commit> $R/stable
#   sh $T $R blast-radius $R/stable $R/latest-from-stable
#   sh $T $R app-intree app-cold -
#   sh $T $R app-intree app-seeded $R/newcomer
#   sh $T $R app-sdk app-sdk $R/newcomer
set -eu

root=$1
scenario=$2
shift 2
mkdir -p "$root"
root=$(cd "$root" && pwd -P)
jobs=${JOBS:-6}
gui_forms_url=${GUI_FORMS_URL:-https://github.com/malkuth-software/gui_forms}
pocket_reference_url=${POCKET_REFERENCE_URL:-https://github.com/malkuth-software/pocket_reference}
case "$(uname -s)" in
  Darwin)
    platform=macos-arm64
    PATH="$(brew --prefix llvm@22)/bin:$PATH"
    SDKROOT=$(xcrun --show-sdk-path)
    export SDKROOT
    ;;
  Linux)
    case "$(uname -m)" in aarch64|arm64) platform=linux-arm64 ;; *) platform=linux-x64 ;; esac
    ;;
  *) echo "measure_cache_bootstrap.sh: macOS or Linux only (Windows numbers come from the field tester)" >&2; exit 2 ;;
esac
export PATH

# The workspace environment the bootstrap writes (template_harness
# build_environment.py), for scenarios that do not run the bootstrap.
workspace_environment() {
  workspace=$1
  export CCACHE_DIR="$workspace/.ccache" CCACHE_BASEDIR="$workspace" CCACHE_COMPILERCHECK=content
  export CCACHE_SLOPPINESS=pch_defines,time_macros,locale CCACHE_MAXSIZE=5G
  export CMAKE_C_COMPILER_LAUNCHER=ccache CMAKE_CXX_COMPILER_LAUNCHER=ccache CMAKE_OBJCXX_COMPILER_LAUNCHER=ccache
  export CC=clang CXX=clang++ OBJCXX=clang++
  export GUI_FORMS_STX_PACKAGE_DIR="$workspace/.ci/stx-package"
  export HARNESS_WORKSPACE="$workspace" HARNESS_PROJECT="$workspace/template_harness/projects/gui_forms.toml"
  TMPDIR="$(cd "${TMPDIR:-/tmp}" && pwd -P)/"
  export TMPDIR
  runtime=$(ls -d "$workspace"/.build/toolchain/*/ 2>/dev/null | grep -v '\.unverified/$' | head -n 1 || true)
  if [ -n "$runtime" ] && [ -z "${GUI_FORMS_LLVM_RUNTIME:-}" ]; then
    export GUI_FORMS_LLVM_RUNTIME="${runtime%/}" HARNESS_LLVM_RUNTIME="${runtime%/}"
  fi
}

# CI's newest published cache archive for this platform, once.
published_cache() {
  archive="$root/downloads/gui-forms-cache-$platform.tar.gz"
  if [ ! -f "$archive" ]; then
    mkdir -p "$root/downloads"
    tag=$(python3 -I -c "import json,sys,urllib.request
releases=json.load(urllib.request.urlopen('https://api.github.com/repos/malkuth-software/gui_forms/releases?per_page=100'))
names=[r['tag_name'] for r in releases if r['tag_name'].startswith('build-') and any(a['name']==sys.argv[1] for a in r['assets'])]
print(names[0])" "gui-forms-cache-$platform.tar.gz")
    curl -sSLf -o "$archive" "https://github.com/malkuth-software/gui_forms/releases/download/$tag/gui-forms-cache-$platform.tar.gz"
  fi
  echo "$archive"
}

# gui_forms at REV and template_harness at its harness.lock pin in WORKSPACE,
# and CI's macOS runtime from the published cache. On Linux set
# GUI_FORMS_LLVM_RUNTIME to a built runtime (the newcomer scenario's
# .build/toolchain/<name>, for instance) before the other scenarios.
checkout() {
  workspace=$1
  revision=$2
  mkdir -p "$workspace"
  [ -d "$workspace/gui_forms" ] || git clone -q "$gui_forms_url" "$workspace/gui_forms"
  git -C "$workspace/gui_forms" checkout -q --detach "$revision"
  pin=$(sed -n 's/^commit = "\(.*\)"/\1/p' "$workspace/gui_forms/harness.lock")
  [ -d "$workspace/template_harness" ] || git clone -q https://github.com/malkuth-software/template_harness "$workspace/template_harness"
  git -C "$workspace/template_harness" checkout -q --detach "$pin"
  if [ "$platform" = macos-arm64 ] && [ ! -d "$workspace/.build/toolchain" ]; then
    tar -xzf "$(published_cache)" -C "$workspace" .build/toolchain
  fi
}

# Runs the command with the cache statistics zeroed, then prints the summary line.
measured() {
  name=$1
  workspace=$2
  shift 2
  ccache -z >/dev/null
  start=$(date +%s)
  set +e
  "$@" > "$root/$name.build.log" 2>&1
  status=$?
  set -e
  end=$(date +%s)
  ccache --print-stats > "$root/$name.ccache.txt"
  hits=$(awk '$1 == "direct_cache_hit" || $1 == "preprocessed_cache_hit" { n += $2 } END { print n + 0 }' "$root/$name.ccache.txt")
  misses=$(awk '$1 == "cache_miss" { print $2 }' "$root/$name.ccache.txt")
  echo "$name rc=$status seconds=$((end - start)) hits=$hits misses=$misses"
}

smoke_build() {
  python3 gui_forms/tools/ci_build.py --install-only --jobs "$jobs"
}

case "$scenario" in
  newcomer)
    name=$1
    harness_url=$2
    workspace="$root/$name"
    rm -rf "$root/$name.harness"
    git clone -q "$harness_url" "$root/$name.harness"
    start=$(date +%s)
    python3 -B "$root/$name.harness/harness/bootstrap.py" --workspace "$workspace" --project gui_forms \
      --platform "$platform" --no-smoke > "$root/$name.bootstrap.log" 2>&1
    end=$(date +%s)
    echo "$name bootstrap seconds=$((end - start))"
    grep '^warning: ' "$root/$name.bootstrap.log" || true
    cd "$workspace"
    . ./.ci/local-build-env.sh
    export HARNESS_WORKSPACE="$workspace" HARNESS_PROJECT="$workspace/template_harness/projects/gui_forms.toml"
    measured "$name" "$workspace" smoke_build
    ;;
  cold|seeded)
    name=$1
    revision=$2
    workspace="$root/$name"
    checkout "$workspace" "$revision"
    if [ "$scenario" = seeded ] && [ ! -d "$workspace/.ccache" ]; then
      cp -R "$3/.ccache" "$workspace/.ccache"
    fi
    workspace_environment "$workspace"
    cd "$workspace"
    measured "$name" "$workspace" smoke_build
    ;;
  blast-radius)
    python3 -B "$(dirname "$0")/cache_blast_radius.py" "$1" "$2" ".build/native-$platform/gui-forms"
    ;;
  app-intree|app-sdk)
    name=$1
    workspace="$root/$name"
    mkdir -p "$workspace"
    [ -d "$workspace/app" ] || git clone -q "$pocket_reference_url" "$workspace/app"
    pin=$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["commit"])' \
      "$workspace/app/third_party/gui-forms.lock.json")
    checkout "$workspace" "$pin"
    workspace_environment "$workspace"
    cd "$workspace"
    if [ "$scenario" = app-intree ]; then
      if [ "$2" != - ] && [ ! -d "$workspace/.ccache" ]; then
        cp -R "$2/.ccache" "$workspace/.ccache"
      fi
      measured "$name" "$workspace" sh -c "cmake -S app -B .build/app -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=$workspace/gui_forms/cmake/llvm22.cmake \
        -DPOCKET_REFERENCE_TOOLKIT_SOURCE_DIR=$workspace/gui_forms \
        && cmake --build .build/app --target pocket-reference --parallel $jobs"
    else
      archive="$root/gui-forms-sdk-$platform.tar.gz"
      tar -czf "$archive" -C "$2/.build/native-$platform" gui-forms-sdk
      echo "$name sdk-archive bytes=$(wc -c < "$archive")"
      rm -rf "$workspace/sdk" && mkdir "$workspace/sdk" && tar -xzf "$archive" -C "$workspace/sdk"
      measured "$name" "$workspace" sh -c "cmake -S app -B .build/app -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=$workspace/gui_forms/cmake/llvm22.cmake \
        -DCMAKE_PREFIX_PATH=$workspace/sdk/gui-forms-sdk -DPOCKET_REFERENCE_DOCS=$workspace/gui_forms \
        && cmake --build .build/app --target pocket-reference --parallel $jobs"
    fi
    ;;
  *)
    echo "measure_cache_bootstrap.sh: unknown scenario $scenario" >&2
    exit 2
    ;;
esac
