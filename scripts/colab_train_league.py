#!/usr/bin/env python3
"""
scripts/colab_train_league.py -- Colab driver for REDGARDEN's real AlphaStar-style PFSP league
training (NORTHSTAR §25.4.1, scripts/run_league.sh's own local equivalent), run on Colab's free
GPU instead of this box's own CPU. Founder real-time: "...begin work on pure pure pure RL auto
curiculum PFSP fast play fast forward league play via a colab skrip."

Same reusable-bootstrap pattern scripts/colab_train.py already established for the unsupervised
GPT-2 pretrain stage (S170-194): the notebook cell mounts Drive, git-pulls this repo, and calls
this file -- all real training logic lives here in git, so a fresh Colab run always executes the
current version with no re-pasting. Reused directly, not reinvented.

What this launches: the SAME three concurrent roles (main/main_exploiter/league_exploiter)
scripts/run_league.sh already runs locally, just pointed at a Drive-backed --league-dir/
--output-dir so checkpoints -- and the checkpoint git-lfs history scripts/checkpoint_git_sync.py
already wires into every rl_train_team.py save, unconditionally, on by default, S533 -- survive
a Colab session reset instead of living in the ephemeral container filesystem. Building
libarena_training.so (scripts/build_training.sh) is the one real prerequisite this driver has to
do itself that run_league.sh assumes is already built locally.

Config is read from environment variables (set by the notebook cell), same convention
colab_train.py's own parse_args() uses, so this also runs standalone for local smoke-testing:
    RL_TOTAL_TIMESTEPS=2000 RL_SAVE_FREQ=1000 python3 scripts/colab_train_league.py
"""

import os
import subprocess
import sys
import time

ROLES = ("main", "main_exploiter", "league_exploiter")


def sh(cmd, **kw):
    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True, **kw)


def ensure_build_tools():
    # Colab images already ship gcc; this is a real, cheap no-op there. Best-effort (not
    # `check=True`) since a locally-run smoke test with no apt/root access shouldn't fail here --
    # scripts/build_training.sh's own gcc invocation is the real, load-bearing check.
    subprocess.run(["apt-get", "install", "-y", "-qq", "build-essential"], check=False)


def pip_install():
    try:
        import stable_baselines3  # noqa: F401
        import gymnasium  # noqa: F401
        return
    except ImportError:
        pass
    subprocess.check_call([sys.executable, "-m", "pip", "install", "-q",
                            "stable-baselines3", "gymnasium"])


def build_training_lib(repo_dir):
    sh(["bash", "scripts/build_training.sh"], cwd=repo_dir)


def configure_checkpoint_remote(ckpt_dir, role, remote_url, ssh_key_path):
    """Wires scripts/checkpoint_git_sync.py's own per-directory git-lfs repo up to a real
    remote, if the founder has configured one (CHECKPOINT_GIT_REMOTE/CHECKPOINT_GIT_SSH_KEY env
    vars) -- otherwise every checkpoint still gets a real local git-lfs history on Drive (S533's
    own "push is a no-op until origin is configured" design), just not pushed anywhere.

    Each of the three roles gets its OWN branch (named after the role) on the shared remote --
    three independently-`git init`'d repos pushing their own "main"/"master" to the same remote
    branch would collide (unrelated histories, non-fast-forward), so this checks out a
    role-named branch before scripts/checkpoint_git_sync.py's own lazy `_ensure_repo` (which
    no-ops once `.git` already exists) ever runs. Same SSH-key-over-Drive pattern
    scripts/git_sync_utils.py already established for the GPT-2 weights export.
    """
    if not remote_url or not ssh_key_path or not os.path.exists(ssh_key_path):
        return
    os.makedirs(ckpt_dir, exist_ok=True)
    if not os.path.isdir(os.path.join(ckpt_dir, ".git")):
        subprocess.run(["git", "init"], cwd=ckpt_dir, check=True)
        subprocess.run(["git", "checkout", "-b", role], cwd=ckpt_dir, check=True)
    os.chmod(ssh_key_path, 0o600)
    result = subprocess.run(["git", "remote", "get-url", "origin"], cwd=ckpt_dir,
                             capture_output=True)
    remote_cmd = ["git", "remote", "set-url" if result.returncode == 0 else "add",
                  "origin", remote_url]
    subprocess.run(remote_cmd, cwd=ckpt_dir, check=True)
    # GIT_SSH_COMMAND is read from the environment at push time by checkpoint_git_sync.py's own
    # subprocess.run calls (inherited through the rl_train_team.py subprocess this script spawns
    # below) -- set process-wide here so every background sync thread in every role's process
    # picks it up, no per-call plumbing needed.
    os.environ["GIT_SSH_COMMAND"] = (
        f"ssh -i {ssh_key_path} -o StrictHostKeyChecking=accept-new "
        f"-o UserKnownHostsFile=/content/.ssh_known_hosts")


def main():
    repo_dir = os.environ.get("REPO_DIR", os.getcwd())
    drive_folder = os.environ.get("DRIVE_FOLDER", "/content/drive/MyDrive/redgarden-league")
    league_dir = os.path.join(drive_folder, "rl_league")
    team_size = os.environ.get("RL_TEAM_SIZE", "3")
    total_timesteps = os.environ.get("RL_TOTAL_TIMESTEPS", "500000")
    remote_url = os.environ.get("CHECKPOINT_GIT_REMOTE")
    ssh_key_path = os.environ.get("CHECKPOINT_GIT_SSH_KEY",
                                   "/content/drive/MyDrive/.ssh/id_ed25519")

    ensure_build_tools()
    pip_install()
    build_training_lib(repo_dir)

    os.makedirs(drive_folder, exist_ok=True)

    procs = []
    for role in ROLES:
        out_dir = os.path.join(drive_folder, f"rl_league_checkpoints_{role}")
        configure_checkpoint_remote(out_dir, role, remote_url, ssh_key_path)
        log_path = os.path.join(drive_folder, f"rl_league_{role}.log")
        log_f = open(log_path, "w")
        p = subprocess.Popen(
            [sys.executable, "scripts/rl_train_team.py",
             "--league", "--league-role", role, "--league-dir", league_dir,
             "--team-size", team_size, "--total-timesteps", total_timesteps,
             "--output-dir", out_dir, "--skip-export"],
            cwd=repo_dir, stdout=log_f, stderr=subprocess.STDOUT,
        )
        procs.append((role, log_path, log_f, p))
        print(f"[league] {role}: pid {p.pid} -> {log_path}")

    print(f"[league] all three roles launched, league_dir={league_dir} "
          f"(persists on Drive across Colab session resets)")
    print(f"[league] tail the *.log files above to watch progress; checkpoints git-lfs-sync "
          f"automatically (scripts/checkpoint_git_sync.py, on by default) into each "
          f"rl_league_checkpoints_<role>/ dir on Drive as they save")

    # Same "one role dying means the group should stop" posture run_league.sh's own `wait -n`
    # already established -- a Colab cell has no supervising systemd unit to relaunch it, so
    # this just surfaces the failure loudly (non-zero exit) instead of silently hanging.
    try:
        while True:
            time.sleep(30)
            statuses = [p.poll() for _, _, _, p in procs]
            if any(s is not None for s in statuses):
                for (role, log_path, _, _), status in zip(procs, statuses):
                    state = "running" if status is None else f"exited({status})"
                    print(f"[league] {role}: {state}")
                if all(s == 0 for s in statuses):
                    print("[league] all three roles completed successfully")
                    return
                sys.exit(1)
    finally:
        for _, _, log_f, p in procs:
            if p.poll() is None:
                p.terminate()
            log_f.close()


if __name__ == "__main__":
    main()
