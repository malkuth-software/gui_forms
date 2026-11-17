#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
font_dir=$(CDPATH= cd -- "$script_dir/../assets/fonts" && pwd)
# The upstream files the bundled fallback subsets are cut from
# (tools/fallback_font_subsets.py write --sources <this directory>); not bundled themselves.
source_dir="$script_dir/../build/fallback-font-sources"
mkdir -p "$source_dir"
source_dir=$(CDPATH= cd -- "$source_dir" && pwd)
staging=$(mktemp -d "${TMPDIR:-/tmp}/gui-forms-fallback-fonts.XXXXXX")
trap 'rm -rf "$staging"' EXIT HUP INT TERM

noto_cjk_revision=f8d157532fbfaeda587e826d4cd5b21a49186f7c
noto_emoji_revision=9a5261d871451f9b5183c93483cbd68ed916b1e9
ofl_hash=6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2

fetch_verified() {
  url=$1
  expected=$2
  output=$3
  destination=${4:-$font_dir}
  curl --fail --location --silent --show-error "$url" --output "$staging/$output"
  actual=$(shasum -a 256 "$staging/$output" | awk '{print $1}')
  if [ "$actual" != "$expected" ]; then
    echo "SHA-256 mismatch for $output: $actual" >&2
    exit 1
  fi
  mv "$staging/$output" "$destination/$output"
}

fetch_verified \
  "https://raw.githubusercontent.com/notofonts/noto-cjk/$noto_cjk_revision/Sans/OTF/Japanese/NotoSansCJKjp-Regular.otf" \
  68a3fc98800b2a27b371f2fb79991daf3633bd89309d4ffaa6946fd587f375b5 \
  NotoSansCJKjp-Regular.otf "$source_dir"
noto_fonts=https://raw.githubusercontent.com/notofonts/noto-fonts/main/hinted/ttf
fetch_verified "$noto_fonts/NotoSansArabic/NotoSansArabic-Regular.ttf" \
  ceea25b464a656dc3b26849bab9356740401af62aedf1bfa8b7f0d9b75925b1b NotoSansArabic-Regular.ttf "$source_dir"
fetch_verified "$noto_fonts/NotoSansDevanagari/NotoSansDevanagari-Regular.ttf" \
  385e78e6359a9d88a0f243d53b1209d7548361ba2194e2b9ec779bcaa7e8949d NotoSansDevanagari-Regular.ttf "$source_dir"
fetch_verified "$noto_fonts/NotoSansBengali/NotoSansBengali-Regular.ttf" \
  6300c5370cd688b0641343de4c786de6d412bb6c578d129dae75e93a0322dcab NotoSansBengali-Regular.ttf "$source_dir"
fetch_verified "$noto_fonts/NotoSansGurmukhi/NotoSansGurmukhi-Regular.ttf" \
  bbde4d85fdfb998eff6921cb2c7a9a7924a1c95560a6aa9a06172530e4f596da NotoSansGurmukhi-Regular.ttf "$source_dir"
fetch_verified \
  "https://raw.githubusercontent.com/googlefonts/noto-emoji/$noto_emoji_revision/fonts/NotoEmoji-Regular.ttf" \
  415dc6290378574135b64c808dc640c1df7531973290c4970c51fdeb849cb0c5 \
  NotoEmoji-Regular.ttf
fetch_verified \
  "https://raw.githubusercontent.com/notofonts/noto-cjk/$noto_cjk_revision/Sans/LICENSE" \
  "$ofl_hash" OFL-NotoSansCJKjp.txt
fetch_verified \
  "https://raw.githubusercontent.com/googlefonts/noto-emoji/$noto_emoji_revision/fonts/LICENSE" \
  "$ofl_hash" OFL-NotoEmoji.txt
