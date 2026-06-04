---
name: github-actions-ci
description: Edit or debug this repo's GitHub Actions workflows (.github/workflows/deployment.yaml, integration.yaml). Load when changing matrix entries (python_version × base_image), the tag→version extraction logic, GHCR push behavior, workflow_dispatch inputs, or QEMU/buildx setup. Pinned action versions and the "latest tag only on default matrix entry" rule live here.
---

# CI workflows in this repo

Two workflow files. Both build the gamdl Docker image; their *triggers* differ.

| File | Triggers | What it does | Pushes to GHCR? |
|------|----------|--------------|-----------------|
| `deployment.yaml` | `push` to `master` (commented out — currently dormant), `workflow_dispatch` | Tagged release builds | yes |
| `integration.yaml` | `push` of `v*` tags, `pull_request` to master, `workflow_dispatch` | Same matrix build; verifies the Dockerfile still builds | yes, except on PRs |

The two files share most of the body. **Keep them in sync** when changing matrix axes or pinned action versions — there's no shared workflow yet.

## Matrix axes

```yaml
strategy:
  fail-fast: false
  matrix:
    python_version: ['3.10', '3.14']
    base_image: ['bookworm-slim', 'trixie-slim']
```

Four jobs per workflow run. `fail-fast: false` means a flaky trixie build doesn't kill the bookworm jobs.

**`latest` tag rule** — only the "default" matrix entry (3.10 + bookworm-slim) also tags as `latest`:

```yaml
type=raw,value=latest,enable=${{ matrix.python_version == '3.10' && matrix.base_image == 'bookworm-slim' }}
```

If you change the default Python or base image in the Dockerfile, **update this `enable:` predicate to match** or `latest` will silently disappear from GHCR.

## Tag → version extraction

```bash
if [ "${{ github.event_name }}" = "workflow_dispatch" ]; then
  TAG="${{ github.event.inputs.tag }}"
else
  TAG=${GITHUB_REF#refs/tags/}
fi
VERSION=${TAG#v}     # v3.7.3 → 3.7.3
echo "gamdl_version=${VERSION}" >> $GITHUB_OUTPUT
```

Constraints:

- `workflow_dispatch` requires the `tag` input (`required: true`). The input value is expected in `vX.Y.Z` form — the leading `v` is stripped.
- `push` event must be a tag push matching `v*`. Branch pushes are commented out in deployment.yaml; re-enabling them means `${GITHUB_REF#refs/tags/}` will yield `refs/heads/master`, which `${TAG#v}` won't fix. Add a branch arm if you re-enable.
- `pull_request` events skip the GHCR login (`if: github.event_name != 'pull_request'`) and `push: false` on the build step. Builds run; they just don't publish.

## Pinned action versions (do not bump casually)

| Action | Pin |
|--------|-----|
| `actions/checkout` | `@v5` |
| `docker/setup-qemu-action` | `@v3` |
| `docker/setup-buildx-action` | `@v3` |
| `docker/login-action` | `@v3` |
| `docker/metadata-action` | `@v5` |
| `docker/build-push-action` | `@v6` |

`docker/build-push-action@v6` changed the default `provenance` and `sbom` settings vs v5 — if a downgrade is needed for a reason, surface that explicitly.

## GHCR auth

```yaml
- uses: docker/login-action@v3
  with:
    registry: ghcr.io
    username: ${{ github.actor }}
    password: ${{ secrets.GITHUB_TOKEN }}
```

`secrets.GITHUB_TOKEN` is auto-provided. Repo settings must allow Actions to write packages (`permissions.packages: write` is already declared in the job — keep it).

## When you change Dockerfile build args

Synchronize the four places they appear:

1. `Dockerfile` — `ARG ... =default`
2. `compose.yaml` — `build.args.<NAME>`
3. workflow `build-args:` block
4. workflow `tags:` template (versions appear in the GHCR tag string)

Adding a new build arg? Also add a matrix axis or a hard-coded value in `build-args`. The metadata-action tag template needs updating too if the arg should appear in tag names.

## Cache backend

`cache-from: type=gha` / `cache-to: type=gha,mode=max`. `mode=max` exports cache for all stages (incl. the dotnet build stage). Don't switch to `mode=min` without measuring — the dotnet stage is the slow one.

## Retrieval

| Source | URL |
|--------|-----|
| GHA workflow syntax | `https://docs.github.com/en/actions/reference/workflow-syntax-for-github-actions` |
| GHA expressions | `https://docs.github.com/en/actions/learn-github-actions/expressions` |
| docker/metadata-action | `https://github.com/docker/metadata-action#readme` |
| docker/build-push-action | `https://github.com/docker/build-push-action#readme` |
| GHCR docs | `https://docs.github.com/en/packages/working-with-a-github-packages-registry/working-with-the-container-registry` |
