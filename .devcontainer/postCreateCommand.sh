#!/bin/zsh

sudo chown -R vscode:vscode /home/vscode/app/.venv
sudo chown -R vscode:vscode /home/vscode/.cache

uv sync
uv venv /home/vscode/app/.venv
uv pip install gamdl
