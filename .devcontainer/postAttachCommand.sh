#!/bin/zsh

git config --global --add --bool push.autoSetupRemote true
git config --global --add safe.directory /home/vscode/app
git config --global --unset commit.template 2>/dev/null || true
git config --global commit.gpgSign false
git config --global core.fileMode false
git config --global fetch.prune true
git branch --merged | egrep -v '\*|develop|main|master' | xargs -r git branch -d || true

# -----------------------------------------------------------------------------
# Codex
#
# Codex reads credentials from auth.json, not from OPENAI_API_KEY, so the
# variable alone leaves the TUI sitting on its sign-in screen. Log in once, with
# the key piped over stdin so it never shows up in ps.
# -----------------------------------------------------------------------------
[ -n "$OPENAI_API_KEY" ] && command -v codex >/dev/null 2>&1 \
  && { codex login status >/dev/null 2>&1 \
    || printenv OPENAI_API_KEY | codex login --with-api-key; } || true
