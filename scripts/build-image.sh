#!/usr/bin/env bash
# build-image.sh [CHECKOUT_DIR] [TAG] — Cloud Build the game-server image from a checkout's tracked files.
# Stable: scripts/build-image.sh /home/fatbaby/redgarden-stable   R&D: scripts/build-image.sh /home/fatbaby/REDGARDEN
set -euo pipefail
SRC="$(cd "${1:-$(dirname "$0")/..}" && pwd)"
TAG="${2:-$(git -C "$SRC" rev-parse --short HEAD)}"
NAME="${IMAGE_NAME:-redgarden}"   # stable: IMAGE_NAME=redgarden-stable, R&D: IMAGE_NAME=redgarden-rnd
PROJECT="${PROJECT:-project-d24a71e9-2daf-4b2d-917}"
CTX="$(mktemp -d)"; trap 'rm -rf "$CTX"' EXIT
git -C "$SRC" ls-files -z -- . ':!rl_team_checkpoints*' ':!docs2' ':!notebooks' ':!minmax.PNG' | (cd "$SRC" && xargs -0 tar cf -) | tar xf - -C "$CTX"
cp "$(dirname "$0")/../ops/docker/redgarden.Dockerfile" "$CTX/Dockerfile"
gcloud builds submit "$CTX" --project "$PROJECT" --tag "us-central1-docker.pkg.dev/$PROJECT/emily/$NAME:$TAG"
echo "$NAME:$TAG"
