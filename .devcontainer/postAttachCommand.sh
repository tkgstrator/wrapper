#!/bin/zsh

git config --global --add --bool push.autoSetupRemote true
git config --global --add safe.directory /home/vscode/app
git config --global --unset commit.template
git config --global commit.gpgSign true
git config --global core.fileMode false
git config --global fetch.prune true
