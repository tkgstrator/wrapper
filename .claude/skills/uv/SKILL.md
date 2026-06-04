---
name: uv
description: Astral uv (Python package & project manager). Load before running uv sync/add/remove/run/lock/pip/venv/tool/python, editing pyproject.toml, or building this project's Docker image (which uses ghcr.io/astral-sh/uv:python$PYTHON_VERSION-$BASE_IMAGE and `uv pip install --system`). Biases toward retrieval from docs.astral.sh over pre-trained knowledge.
---

# uv

This project is managed by **uv**. `pip`, `pip-tools`, `virtualenv`, `pyenv`, `poetry`, `pdm`, `pipx` — uv replaces all of them. Don't reach for those tools unless retrieval explicitly says to.

## Prefer retrieval over baked knowledge

uv ships breaking-ish CLI changes regularly. Before running anything non-trivial, fetch the docs:

| Source | URL | Use for |
|--------|-----|---------|
| uv docs root | `https://docs.astral.sh/uv/` | Concepts, project layout |
| CLI reference | `https://docs.astral.sh/uv/reference/cli/` | Exact flags & subcommands |
| Settings reference | `https://docs.astral.sh/uv/reference/settings/` | `[tool.uv]` in pyproject |
| Concepts → projects | `https://docs.astral.sh/uv/concepts/projects/` | sync/lock/run semantics |
| Docker integration | `https://docs.astral.sh/uv/guides/integration/docker/` | `uv pip install --system`, multi-stage |
| pep 723 scripts | `https://docs.astral.sh/uv/guides/scripts/` | Inline `# /// script` headers |

Use WebFetch on those URLs when in doubt — don't guess flags.

## Project layout in this repo

- `pyproject.toml` — single source of truth. `[project].dependencies` is currently empty (the runtime payload is `gamdl`, installed in the Docker image).
- `uv.lock` — committed. **Always keep it in sync with `pyproject.toml`.**
- `.venv/` — local venv at `/home/vscode/app/.venv`. Created in `postCreateCommand.sh`.
- Dockerfile pins `uv` via the `ghcr.io/astral-sh/uv:python${PYTHON_VERSION}-${BASE_IMAGE}` base image and installs gamdl via `uv pip install --system "gamdl==${GAMDL_VERSION}"` — system Python, **not** the project venv.

## Commands you actually want

| Want to... | Command |
|------------|---------|
| Install everything from `pyproject.toml` + `uv.lock` | `uv sync` |
| Install incl. dev/optional groups | `uv sync --group dev` / `--all-groups` |
| Add a runtime dep & update lockfile | `uv add <pkg>` |
| Add a dev-only dep | `uv add --dev <pkg>` (legacy) **or** `uv add --group dev <pkg>` (PEP 735) |
| Remove a dep | `uv remove <pkg>` |
| Refresh lockfile only (no install) | `uv lock` |
| Upgrade one pinned dep | `uv lock --upgrade-package <pkg>` |
| Upgrade everything | `uv lock --upgrade` |
| Run a command inside the project env | `uv run <cmd>` (auto-syncs) |
| Run a oneshot script with inline deps | `uv run --script path.py` |
| Spawn a tool ephemerally (like pipx) | `uvx <tool>` |
| Install a tool persistently | `uv tool install <tool>` |
| Manage Python interpreters | `uv python install 3.12` / `uv python pin 3.12` |

`uv pip ...` exists as a drop-in pip emulation. **Don't reach for it in this project's working tree** — use `uv add` / `uv sync` so `uv.lock` stays authoritative. `uv pip install --system` is fine inside the Dockerfile (no project venv there).

## Cardinal rules

1. **Never edit `uv.lock` by hand.** Let `uv lock` / `uv add` / `uv sync` regenerate it.
2. **Don't activate `.venv` in tool calls.** Each Bash invocation is a fresh shell — `source ... && cmd` works for one call but doesn't persist. Prefer `uv run <cmd>` or call `/home/vscode/app/.venv/bin/<tool>` directly.
3. **`UV_LINK_MODE=copy`** is set in this devcontainer (bind-mounted home) — don't override to `hardlink` or installs fail across filesystems.
4. **Don't commit `.venv/`**. It's recreatable from the lockfile.
5. **Python version** for the project is `>=3.10` (per `pyproject.toml`). Docker matrix builds against 3.10 and 3.14 — keep dependencies compatible with both.
6. **Lockfile diff in PRs:** if you change `pyproject.toml`, run `uv lock` (or `uv sync`) and commit `uv.lock` in the same commit. CI will fail if they drift.

## Common foot-guns

- `uv run python -c "..."` re-syncs before running. Use `--no-sync` if you've already synced and want speed.
- `uv add foo` without a version pins a compatible range; `uv add 'foo==1.2.3'` pins exact.
- `uv sync --frozen` honors the lockfile and refuses to update it — use this in CI / Docker for reproducible installs.
- `uv sync --locked` errors if the lockfile is stale w.r.t. pyproject.toml — good early signal in CI.
- Inside the Docker image, `uv pip install --system` writes to `/usr/local/lib/python.../site-packages`. Do **not** also create a venv there; pick one.
- `uv venv /path` recreates the venv from scratch and silently nukes the existing one — `postCreateCommand.sh` does this on every container rebuild.

## When you change deps

```bash
uv add <pkg>            # updates pyproject.toml AND uv.lock AND .venv
git add pyproject.toml uv.lock
```

If you only edited `pyproject.toml` by hand:

```bash
uv lock                 # refresh uv.lock
uv sync                 # install into .venv
```

If CI complains the lockfile is out of date, run `uv lock` locally and commit.
