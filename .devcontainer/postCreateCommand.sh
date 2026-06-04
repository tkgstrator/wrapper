#!/bin/zsh
set -e

REPO_ROOT="${CLAUDE_PROJECT_DIR:-/home/vscode/app}"
cd "$REPO_ROOT"

# -----------------------------------------------------------------------------
# git
# -----------------------------------------------------------------------------
git config --global --add --bool push.autoSetupRemote true
git config --global --add safe.directory "$REPO_ROOT"
git config --global --unset commit.template 2>/dev/null || true
git config --global commit.gpgSign false
git config --global core.fileMode false
git config --global fetch.prune true
git branch --merged 2>/dev/null \
  | grep -Ev '\*|develop|main|master' \
  | xargs -r git branch -d || true

# -----------------------------------------------------------------------------
# Apple Music APK (only fetched when missing AND APK_URL is provided)
#
# wrapper-v2 needs Apple Music Android native libs (libstoreservicescore /
# libmediaplatform / libandroidappmusic / libc++_shared) staged into
# rootfs/system/lib64/. tools/extract-libs.sh pulls them out of an .apk/.apkm
# bundle. This block grabs the bundle once if APK_URL is set in .env and the
# file is not already on disk; it does NOT touch lib64 itself - run
# tools/extract-libs.sh + tools/stage-system.sh manually when you want to
# (re)stage.
# -----------------------------------------------------------------------------
if [[ -f .env ]]; then
  set -a
  # shellcheck disable=SC1091
  source .env
  set +a
fi

mkdir -p .tmp

apk_url="${APK_URL:-}"
if [[ -n "$apk_url" ]]; then
  case "${apk_url,,}" in
    *.apkm*) artifact=".tmp/apple-music.apkm" ;;
    *)       artifact=".tmp/apple-music.apk"  ;;
  esac

  if [[ -f "$artifact" ]]; then
    echo "postCreate: $artifact already present, skipping download"
  else
    echo "postCreate: fetching Apple Music bundle from APK_URL -> $artifact"
    if curl -fSL --retry 3 --retry-delay 2 -o "$artifact.part" "$apk_url"; then
      mv "$artifact.part" "$artifact"
      echo "postCreate: downloaded $(du -h "$artifact" | cut -f1) to $artifact"
    else
      rm -f "$artifact.part"
      echo "postCreate: APK download failed; continuing without it" >&2
    fi
  fi
else
  echo "postCreate: APK_URL not set in .env, skipping Apple Music bundle fetch"
fi
