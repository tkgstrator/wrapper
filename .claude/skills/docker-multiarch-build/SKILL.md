---
name: docker-multiarch-build
description: Build, edit, or debug this project's multi-arch Docker image. Load when touching Dockerfile / compose.yaml / docker buildx, when changing GAMDL_VERSION / PYTHON_VERSION / BASE_IMAGE / N_M3U8DL_RE_VERSION build args, or when CI image builds fail. Covers the dotnet build-stage that compiles N_m3u8DL-RE, the uv runtime stage, and TARGETARCH / cache-from gha specifics.
---

# Multi-arch Docker build for gamdl

This image bakes together three things that don't naturally cohabit:

1. **N_m3u8DL-RE** — a .NET 10 native binary. Cross-compiled in a `dhi.io/dotnet:10-sdk` stage to `linux-x64` or `linux-arm64` based on `TARGETARCH`.
2. **ffmpeg** — apt-installed, required only when `download_mode = nm3u8dlre`.
3. **gamdl** — Python CLI, installed system-wide via `uv pip install --system` on top of `ghcr.io/astral-sh/uv:python${PYTHON_VERSION}-${BASE_IMAGE}`.

The Dockerfile is two stages. Don't collapse them — the dotnet SDK image is hundreds of MB you don't want in the runtime.

## Build args (single source of truth)

| Arg | Default | What it controls |
|-----|---------|------------------|
| `GAMDL_VERSION` | `3.7.3` | The `gamdl==X` pinned in `uv pip install --system`. Bump this when gamdl releases. |
| `N_M3U8DL_RE_VERSION` | `v0.5.1-beta` | Git tag checked out before `dotnet publish`. |
| `PYTHON_VERSION` | `3.10` | Selects `ghcr.io/astral-sh/uv:python${PYTHON_VERSION}-${BASE_IMAGE}` tag. CI matrix: `3.10`, `3.14`. |
| `BASE_IMAGE` | `bookworm` (Dockerfile) / `bookworm-slim` (compose) | Debian variant. CI matrix: `bookworm-slim`, `trixie-slim`. |
| `TARGETARCH` | auto from buildx | `arm64` → `linux-arm64`, `amd64` → `linux-x64`. Anything else fails the build. |

**Watch out:** Dockerfile defaults `BASE_IMAGE=bookworm` but compose.yaml passes `bookworm-slim`. CI matrix uses the `-slim` variants. The non-slim default in Dockerfile is fine for ad-hoc builds but produces a much bigger image — prefer passing the slim variant explicitly.

## Building locally

```bash
# Single-arch (your host arch), fast iteration
docker compose build gamdl

# Multi-arch (push-only — buildx can't load multi-platform into local daemon)
docker buildx build \
  --platform linux/amd64,linux/arm64 \
  --build-arg GAMDL_VERSION=3.7.3 \
  --build-arg PYTHON_VERSION=3.10 \
  --build-arg BASE_IMAGE=bookworm-slim \
  -t local/gamdl:test \
  --push .   # or --output type=registry,name=...
```

For local single-arch testing of arm64 from an amd64 host (or vice versa), buildx will use QEMU. Slow but correct.

## The C# 14 patch

Watch this line:

```dockerfile
RUN sed -i 's/nameof(this\.\([a-zA-Z]*\))/nameof(\1)/g' \
    src/N_m3u8DL-RE.Parser/StreamExtractor.cs
```

This works around a C# 14 breaking change: `nameof(this.X)` is no longer allowed inside attributes. If you bump `N_M3U8DL_RE_VERSION` to a newer upstream that already fixed this, **remove the sed** — otherwise it silently no-ops and you won't notice until grep tells you. If upstream rewrites the file structure, the regex stops matching; verify by running `grep -n 'nameof(this' src/N_m3u8DL-RE.Parser/*.cs` after `git clone`.

## Cache strategy

- apt: `--mount=type=cache,target=/var/lib/apt,sharing=locked --mount=type=cache,target=/var/cache/apt,sharing=locked`. Layers stay slim; cache speeds repeat builds.
- NuGet: `--mount=type=cache,target=/root/.nuget/packages`. Same idea for `dotnet publish`.
- CI uses GitHub Actions cache backend: `cache-from: type=gha` / `cache-to: type=gha,mode=max`. Don't switch to `type=registry` without coordinating — it changes who pays the storage.

`--mount=type=cache` caches are **not** layer caches. They persist across builds but don't make it into the final image. Don't rely on a file present at build time being present at runtime unless it was `COPY`d or `RUN`-created outside the cache mount.

## Cross-compile gotchas

- `dotnet publish -r linux-arm64` from an amd64 host works because dotnet has full cross-compile support. **Do not** wrap this stage in QEMU emulation — let dotnet do it natively. The Dockerfile is already correct.
- `clang` is installed as the AOT compiler/linker (`-p:CppCompilerAndLinker=clang`). If a build fails with "clang: not found", apt install failed silently — check the apt step's exit code.
- `TARGETARCH` is set by buildx automatically when `--platform` is passed. Without `--platform`, it falls back to host arch.

## When something fails

| Symptom | Likely cause |
|---------|--------------|
| `Unsupported arch: <something>` | Built without buildx or with an arch outside arm64/amd64 |
| `nameof(this...)` still in error log | Upstream changed file layout; sed didn't match. Re-derive the patch. |
| `uv pip install --system gamdl==X.Y.Z` fails on resolve | gamdl pinned a version not on PyPI yet, or python_version constraint mismatch |
| `apt-get update` fails on trixie | Debian mirror flake or trixie security repo not yet provisioned; retry or pin to bookworm-slim |
| Image runs but `N_m3u8DL-RE --version` says "not found" | dotnet build silently exited 0 without producing the binary; check the publish stage logs |

## Retrieval

Don't trust baked-in knowledge for buildx flags or dockerfile syntax:

| Source | URL |
|--------|-----|
| Dockerfile reference | `https://docs.docker.com/reference/dockerfile/` |
| buildx CLI | `https://docs.docker.com/reference/cli/docker/buildx/build/` |
| Cache backends | `https://docs.docker.com/build/cache/backends/` |
| Multi-platform | `https://docs.docker.com/build/building/multi-platform/` |
| docker/build-push-action | `https://github.com/docker/build-push-action#readme` |
