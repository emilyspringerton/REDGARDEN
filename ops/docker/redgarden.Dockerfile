# REDGARDEN game-server image (matchmaker + arena_server + arena_bot + bot-pool script).
# Context = `git ls-files` of one checkout (scripts/build-image.sh). Same image runs every
# container of the redgarden-* pods; the pod spec (EMILY/gitops/specs/redgarden-*.pod) picks the command.
FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends gcc libc6-dev libsdl2-dev libgl-dev libglu1-mesa-dev make \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . /app
RUN bash scripts/build.sh

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends bash procps ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=build /app/build/red_garden_matchmaker /app/build/red_garden_arena_server /app/build/red_garden_arena_bot /app/build/
COPY scripts/run_bot_pool.sh /app/scripts/run_bot_pool.sh
