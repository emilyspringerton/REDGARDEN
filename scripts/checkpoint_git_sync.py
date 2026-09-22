#!/usr/bin/env python3
"""
scripts/checkpoint_git_sync.py -- real, per-directory git-lfs "model repository" for RL
checkpoints, same shape as IDUNA's internal/modelgit.Syncer (EMILY/BACKLOG.md S531, IDUNA
`46d579f`) applied to REDGARDEN's own local `rl_team_checkpoints*/` output dirs. Founder
real-time: "...model repositories etc (model checkins to git pls)."

Why a separate script, not IDUNA's own Go package: REDGARDEN's checkpoints are plain local
files under `.gitignore`d dirs (`rl_team_checkpoints_*/`) written by these Python training
scripts, not rows in an IDUNA-owned SQL checkpoint store -- there is no HTTP/SQL layer here for
a Go syncer to hook into. Same real behavior, ported to fit this repo's own shape: each
checkpoint output directory becomes its OWN git repo (independent of REDGARDEN's own repo,
exactly like IDUNA's SHANKPIT/DEADWEIGHT/BRAWLPIT model repos are each the GAME's own sibling
repo, not IDUNA's), git-lfs-tracks `*.zip` (the only real checkpoint filetype SB3's
`model.save()` produces here), and commits+pushes on every new checkpoint, fire-and-forget.

On by default, per-directory opt-out via env var (same "Disabled, not Enabled" zero-value
default IDUNA's `modelgit.Syncer` uses): REDGARDEN_CHECKPOINT_GIT_DISABLED=1 skips sync
entirely (e.g. for CI smoke-test runs that write throwaway checkpoints nobody wants committed).

No remote is configured here -- unlike IDUNA's model repos, none of REDGARDEN's checkpoint dirs
has an existing sibling GitHub repo to push to (checked directly: they're gitignored precisely
because nobody wanted this large-binary history inside REDGARDEN's own repo, and no other repo
in this monorepo currently exists to hold it either). Real, honest scope: this gives every
checkpoint directory a REAL local git+lfs history (deterministic, byte-identical to any other
git-lfs repo, `git log`-able, restorable) -- `push()` is a no-op unless `git remote get-url
origin` already resolves, so wiring a remote later (a new dedicated repo, founder call) needs
zero code changes here, just `git remote add origin <url>` inside the checkpoint directory.
"""
from __future__ import annotations

import os
import subprocess
import threading


def _run(cmd, cwd):
    subprocess.run(cmd, cwd=cwd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def _ensure_repo(ckpt_dir: str) -> None:
    if not os.path.isdir(os.path.join(ckpt_dir, ".git")):
        _run(["git", "init"], ckpt_dir)
    gitattrs = os.path.join(ckpt_dir, ".gitattributes")
    if not os.path.exists(gitattrs) or "*.zip" not in open(gitattrs).read():
        _run(["git", "lfs", "install", "--local"], ckpt_dir)
        _run(["git", "lfs", "track", "*.zip"], ckpt_dir)
        _run(["git", "add", ".gitattributes"], ckpt_dir)
        _run(["git", "commit", "-m", "chore: git-lfs track *.zip checkpoints", "--allow-empty"], ckpt_dir)


def _has_remote(ckpt_dir: str) -> bool:
    result = subprocess.run(
        ["git", "remote", "get-url", "origin"],
        cwd=ckpt_dir, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    return result.returncode == 0


def _sync(ckpt_dir: str, ckpt_filename: str, message: str) -> None:
    try:
        _ensure_repo(ckpt_dir)
        _run(["git", "add", ckpt_filename], ckpt_dir)
        _run(["git", "commit", "-m", message, "--allow-empty"], ckpt_dir)
        if _has_remote(ckpt_dir):
            _run(["git", "push"], ckpt_dir)
    except subprocess.CalledProcessError as exc:
        print(f"[checkpoint_git_sync] sync failed for {ckpt_dir}/{ckpt_filename}: {exc}")


def sync_checkpoint_async(ckpt_path_with_ext: str, message: str) -> None:
    """Fire-and-forget git-lfs commit (+push if a remote is configured) of one checkpoint file,
    same non-blocking shape as IDUNA modelgit.Syncer.SyncBlob's own `go` call. `ckpt_path_with_ext`
    is the real file path SB3 wrote (e.g. ".../ppo_arena_team_step_122880.zip")."""
    if os.environ.get("REDGARDEN_CHECKPOINT_GIT_DISABLED", ""):
        return
    ckpt_dir = os.path.dirname(os.path.abspath(ckpt_path_with_ext))
    ckpt_filename = os.path.basename(ckpt_path_with_ext)
    threading.Thread(
        target=_sync, args=(ckpt_dir, ckpt_filename, message), daemon=True,
    ).start()
