#!/usr/bin/env python3
"""
scripts/rl_train_team.py (NORTHSTAR §25.2) -- trains REDGARDEN's arena bot AI via PPO against
scripts/rl_env_team.py's ArenaTeamVecEnv: shared-parameter multi-agent training for a full
team_size-a-side match, in place of scripts/rl_train.py's own 1v1-only training.

Same delivery pattern every other training script in this repo already established (config from
CLI args with env-var defaults, runs locally or on Colab, requires build/libarena_training.so to
already exist via scripts/build_training.sh).

Unlike scripts/rl_train.py, this script does NOT wrap the env in SubprocVecEnv/DummyVecEnv --
ArenaTeamVecEnv already IS a VecEnv (team_size parallel agent "slots" sharing one team match, see
that module's own doc comment for why), so it's passed to SB3's PPO directly. Training N separate
PARALLEL team matches at once (N * team_size total rollout streams instead of just team_size) is
real, valuable future depth this script doesn't attempt -- NORTHSTAR §25.6 doesn't resolve team
size for the first real run either, and stacking "how many matches in parallel" on top of that
same still-open question would be scope creep past what's actually been verified here.

NOTE ON VERIFICATION: the ctypes team-mode layer underneath it (via scripts/rl_env_team.py
--smoke-test) was run for real. A real end-to-end SB3 PPO run WAS performed 2026-08-10
(rl_team_checkpoints/ carries 4 real checkpoints up to step 86016) -- correcting this doc
comment's own earlier "has NOT been run end-to-end" claim, now stale. That run predates
--noisy-gestalt (below), which has not itself had a full run yet at the time this note was
written -- flagged, not faked, same posture every other training script in this repo uses.

--noisy-gestalt (2026-08-10, NORTHSTAR §25.2.2): the alternating Johnny/Spike phased training
schedule from the CarePyre transcript, now actually implemented (see scripts/rl_env_team.py's
own module doc comment for the full design).

--autocurriculum (2026-08-10, NORTHSTAR §25.4): PFSP-biased opponent-pool sampling (heuristic +
past self-play checkpoints, biased toward whichever the current policy is losing to most), also
now implemented -- see ArenaTeamVecEnv's own _sample_opponent()/add_opponent_checkpoint() doc
comments. Now HAS been exercised inside a real end-to-end model.learn() run -- correcting this
doc comment's own earlier "NOT yet exercised" claim, which had already gone stale once (a
2026-08-11 run, 500K timesteps, scored 75% vs. the fixed heuristic) and then stayed stale in
this comment through a second run (2026-08-14, 200K timesteps -- shorter, less converged --
scored 35%). Both real, both eval'd against the same fixed heuristic only, not yet against the
opponent pool itself; the lower second score is most likely under-convergence at half the prior
timestep budget, not a regression in the mechanism. Whoever runs this next: check EMILY/BACKLOG.md
and REDGARDEN/CHANGELOG.md for the current run count before assuming this note is still accurate.
Role discovery and synergy decay (§25.2.3) remain spec-only; this closes two of the three
remaining pieces, not all of them.

--league (§25.4.1, rl_league.py, founder real-time citing a real AlphaStar league-training
video): a real, additive extension of --autocurriculum addressing the "simple self-play produces
cyclic dominance" failure mode the video names -- one policy training only against its own past
selves can get better against one opponent while quietly getting worse against others, a real
rock-paper-scissors dynamic. --league runs ONE of three roles per process invocation
(--league-role main|main_exploiter|league_exploiter) against a shared, permanent, cross-process
--league-dir registry (rl_league.LeagueManager) -- running the full league means launching all
three roles as separate concurrent processes against the same --league-dir; see
scripts/run_league.sh for the real orchestration. MAIN_EXPLOITER additionally resets to a freshly
initialized network every --league-reset-every-n-generations checkpoint-save cycles (0 disables
resetting). See rl_league.py's own module doc comment for the full three-role design and the real,
named scope note on what "current Main" means across separate processes (the freshest registered
checkpoint, not a live in-memory model). NOT yet run end-to-end (needs three real, long, concurrent
training processes and real GPU/CPU time -- a real cost/founder-scheduling decision, not attempted
speculatively in the same pass that built the mechanism): rl_league.py's own pure logic is unit-
tested (scripts/test_rl_league.py), and this script's own wiring was reviewed directly, but "does
running the real three-role league measurably beat plain --autocurriculum" is exactly the kind of
open question this file's own history above already treats honestly for every prior feature
(noisy-gestalt, autocurriculum itself) -- not resolved here, named as the real remaining step.
"""

import argparse
import os
import sys


def parse_args():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--lib-path", default=os.environ.get("ARENA_TRAINING_LIB", None))
    p.add_argument("--team-size", type=int, default=int(os.environ.get("RL_TEAM_SIZE", 3)),
                   help="agents per side (2..10) -- NORTHSTAR §25.6 leaves the real first-run "
                        "size undecided; 3v3 is a reasonable, small default")
    p.add_argument("--total-timesteps", type=int,
                   default=int(os.environ.get("RL_TOTAL_TIMESTEPS", 500_000)))
    p.add_argument("--learning-rate", type=float, default=float(os.environ.get("RL_LEARNING_RATE", 3e-4)))
    p.add_argument("--n-steps", type=int, default=int(os.environ.get("RL_N_STEPS", 2048)),
                   help="PPO rollout length per agent slot before each policy update")
    p.add_argument("--batch-size", type=int, default=int(os.environ.get("RL_BATCH_SIZE", 64)))
    p.add_argument("--net-arch", type=int, nargs="+",
                   default=[int(x) for x in os.environ.get("RL_NET_ARCH", "64,64").split(",")])
    p.add_argument("--output-dir", default=os.environ.get("RL_TEAM_OUTPUT_DIR", "rl_team_checkpoints"))
    p.add_argument("--save-freq", type=int, default=int(os.environ.get("RL_SAVE_FREQ", 20_000)))
    p.add_argument("--eval-episodes", type=int, default=int(os.environ.get("RL_EVAL_EPISODES", 20)))
    p.add_argument("--noisy-gestalt", action="store_true",
                   default=os.environ.get("RL_NOISY_GESTALT", "0") == "1",
                   help="alternate Johnny (synergy reward on -- discover team combos) / Spike "
                        "(synergy reward off -- refine to meta) phases, NORTHSTAR §25.2.2. See "
                        "rl_env_team.py's own module doc comment for the full design")
    p.add_argument("--gestalt-phase-ticks", type=int,
                   default=int(os.environ.get("RL_GESTALT_PHASE_TICKS", 50_000)),
                   help="env ticks per Johnny/Spike phase when --noisy-gestalt is set")
    p.add_argument("--autocurriculum", action="store_true",
                   default=os.environ.get("RL_AUTOCURRICULUM", "0") == "1",
                   help="NORTHSTAR §25.4: sample team-B opponent per episode from a pool of "
                        "the heuristic plus past self-play checkpoints, biased (PFSP) toward "
                        "whichever the current policy is losing to most, instead of always "
                        "the fixed heuristic. Pool grows as this run's own checkpoints save.")
    p.add_argument("--league", action="store_true",
                   default=os.environ.get("RL_LEAGUE", "0") == "1",
                   help="NORTHSTAR §25.4.1: AlphaStar-style league training -- run ONE of "
                        "--league-role's three roles against a shared, permanent, cross-process "
                        "--league-dir registry instead of --autocurriculum's own small, "
                        "evicting, single-lineage pool. Overrides --autocurriculum when both "
                        "are set (league mode is the richer superset).")
    p.add_argument("--league-role", choices=["main", "main_exploiter", "league_exploiter"],
                   default=os.environ.get("RL_LEAGUE_ROLE", "main"),
                   help="which of the three real league roles THIS process trains -- run all "
                        "three as separate concurrent processes against the same --league-dir "
                        "for the actual league (see scripts/run_league.sh)")
    p.add_argument("--league-dir", default=os.environ.get("RL_LEAGUE_DIR", "rl_league"),
                   help="shared, permanent, cross-process checkpoint registry directory -- "
                        "every role's process must point at the SAME directory")
    p.add_argument("--league-reset-every-n-generations", type=int,
                   default=int(os.environ.get("RL_LEAGUE_RESET_EVERY", 5)),
                   help="MAIN_EXPLOITER only: resets to a freshly initialized network every N "
                        "checkpoint-save cycles (one generation = one --save-freq chunk). "
                        "<= 0 disables resetting entirely. Ignored for main/league_exploiter.")
    p.add_argument("--skip-export", action="store_true",
                   help="skip converting the trained policy to the embedded-C header format -- "
                        "note this header is NOT yet wired into any live consumer for team-mode "
                        "observations (see this file's own module doc comment); export is for "
                        "inspection/later-wiring, not a claim that it's live-consumable today")
    p.add_argument("--export-output", default=os.environ.get(
        "RL_TEAM_EXPORT_OUTPUT", "packages/common/rl_policy_weights_team.h"),
        help="deliberately a DIFFERENT filename from rl_train.py's own "
             "packages/common/rl_policy_weights.h -- a team-trained policy has a different input "
             "dimension (team_size-dependent) and must never silently overwrite the live 1v1 "
             "weights apps/arena_bot's real match bots already consult")
    return p.parse_args()


def make_env(lib_path, team_size, noisy_gestalt=False, gestalt_phase_ticks=50_000,
             autocurriculum=False, league_manager=None, league_role=None):
    def _init():
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from rl_env_team import ArenaTeamVecEnv
        return ArenaTeamVecEnv(lib_path=lib_path, team_size=team_size,
                                noisy_gestalt=noisy_gestalt,
                                gestalt_phase_ticks=gestalt_phase_ticks,
                                autocurriculum=autocurriculum,
                                league_manager=league_manager, league_role=league_role)
    return _init


def main():
    args = parse_args()

    from stable_baselines3 import PPO

    league_manager = None
    if args.league:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from rl_league import LeagueManager
        league_manager = LeagueManager(args.league_dir)
        print(f"[league] role={args.league_role} shared registry={args.league_dir} "
              f"(NORTHSTAR §25.4.1) -- run the other two roles as separate concurrent "
              f"processes against this SAME --league-dir for the real league; see "
              f"scripts/run_league.sh")

    vec_env = make_env(args.lib_path, args.team_size, args.noisy_gestalt,
                        args.gestalt_phase_ticks, args.autocurriculum,
                        league_manager=league_manager,
                        league_role=args.league_role if args.league else None)()
                                                           # ArenaTeamVecEnv IS the
                                                           # VecEnv -- no SubprocVecEnv/DummyVecEnv
                                                           # wrapper, see this file's own module
                                                           # doc comment

    os.makedirs(args.output_dir, exist_ok=True)

    policy_kwargs = dict(net_arch=args.net_arch)

    def _fresh_model():
        """Builds a brand-new PPO instance with freshly initialized weights, same hyperparameters
        -- MAIN_EXPLOITER's own real periodic reset (rl_league.should_reset_main_exploiter, video
        13:55) needs to actually discard learned weights, not just continue training the same
        network under a new name."""
        return PPO(
            "MlpPolicy",
            vec_env,
            learning_rate=args.learning_rate,
            n_steps=args.n_steps,
            batch_size=args.batch_size,
            policy_kwargs=policy_kwargs,
            verbose=1,
        )

    model = _fresh_model()

    print(f"Training team PPO: team_size={args.team_size} total_timesteps={args.total_timesteps} "
          f"net_arch={args.net_arch}")
    print("Reward function: scripts/rl_env.py's own compute_reward(), applied per-agent -- "
          "NORTHSTAR §25.2's own note on why the 1v1 reward function is directly reusable here.")
    print("Opponent: team_size heuristic-driven bots (stable, non-circular -- NORTHSTAR §25.2.1).")
    if args.noisy_gestalt:
        print(f"Noisy-gestalt ENABLED: alternating Johnny/Spike phases every "
              f"{args.gestalt_phase_ticks} env ticks (NORTHSTAR §25.2.2).")
    else:
        print("Noisy-gestalt disabled (pass --noisy-gestalt to enable, NORTHSTAR §25.2.2).")
    if args.league:
        print(f"League mode ENABLED: role={args.league_role}, shared registry={args.league_dir} "
              f"(NORTHSTAR §25.4.1, supersedes --autocurriculum's own single-lineage pool). "
              + (f"Resets to a fresh network every {args.league_reset_every_n_generations} "
                 f"generations." if args.league_role == "main_exploiter"
                 and args.league_reset_every_n_generations > 0 else
                 "Reset cadence not applicable to this role."
                 if args.league_role != "main_exploiter" else "Resetting disabled (--league-reset-every-n-generations <= 0)."))
    elif args.autocurriculum:
        print("Autocurriculum ENABLED: team-B opponent sampled per episode (PFSP-biased) from "
              "the heuristic plus this run's own growing checkpoint pool (NORTHSTAR §25.4).")
    else:
        print("Autocurriculum disabled -- team B is always the fixed heuristic (pass "
              "--autocurriculum to enable, NORTHSTAR §25.4).")
    print("NOT yet trained here: role discovery / synergy decay (NORTHSTAR §25.2.3) -- this is "
          "a shared-parameter baseline plus noisy-gestalt and/or autocurriculum/league when "
          "enabled, not the full research thread those pieces build toward.")

    checkpoint_path_template = os.path.join(args.output_dir, "ppo_arena_team_step_{}")
    timesteps_done = 0
    generation = 0
    while timesteps_done < args.total_timesteps:
        chunk = min(args.save_freq, args.total_timesteps - timesteps_done)
        # `before` is read from whichever model object is CURRENT at the top of this iteration --
        # after a Main Exploiter reset (below), that's a brand-new PPO whose own num_timesteps
        # starts back at 0, so this correctly rebases the delta instead of computing a negative
        # or stale value against the old (discarded) model's counter.
        before = model.num_timesteps
        model.learn(total_timesteps=chunk, reset_num_timesteps=False)
        # Same real bug rl_train.py's own doc comment already documents fixing (crediting only
        # the requested chunk silently undercounts, since .learn() can only stop at a rollout
        # boundary) -- reading the model's own authoritative counter (as a DELTA from `before`,
        # not the raw value -- a raw read breaks the moment a mid-run reset zeroes the model's
        # own counter, undercounting real total progress and potentially looping far longer than
        # --total-timesteps) for the same reason.
        timesteps_done += model.num_timesteps - before
        ckpt_path = checkpoint_path_template.format(timesteps_done)
        model.save(ckpt_path)
        print(f"Checkpoint saved: {ckpt_path}.zip ({timesteps_done}/{args.total_timesteps} timesteps) "
              f"[generation {generation}]")
        if args.league:
            # §25.4.1: permanent, cross-process registration -- see LeagueManager.register's own
            # doc comment for why this needs a real `generation` (Main Exploiter's own "current
            # Main" / "climb down through history" logic both key off it).
            vec_env.env_method("add_opponent_checkpoint", ckpt_path + ".zip", generation)
            if args.league_role == "main_exploiter":
                from rl_league import should_reset_main_exploiter
                if should_reset_main_exploiter(generation, args.league_reset_every_n_generations):
                    print(f"[league] Main Exploiter generation {generation}: resetting to a "
                          f"freshly initialized network (video 13:55).")
                    model = _fresh_model()
        elif args.autocurriculum:
            # Feeds this run's own growing checkpoint into the opponent pool -- NORTHSTAR §25.4's
            # "sample the next episode's opponent... plus the heuristic AI" self-play requirement.
            # env_method calls add_opponent_checkpoint() once on the shared env (see
            # ArenaTeamVecEnv.env_method's own doc comment for why "once, not team_size times" is
            # correct here).
            vec_env.env_method("add_opponent_checkpoint", ckpt_path + ".zip")
        generation += 1

    final_path = os.path.join(args.output_dir, "ppo_arena_team_final")
    model.save(final_path)
    print(f"Final model saved: {final_path}.zip")

    # Evaluation: real team-match episodes against the same heuristic opponent, win rate = the
    # fraction of episodes where team A (winner==1) won.
    print(f"\nEvaluating over {args.eval_episodes} team episodes...")
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from rl_env_team import ArenaTeamVecEnv
    eval_env = ArenaTeamVecEnv(lib_path=args.lib_path, team_size=args.team_size)
    wins = losses = draws = 0
    for ep in range(args.eval_episodes):
        obs = eval_env.reset()
        winner = 0
        done_this_ep = False
        while not done_this_ep:
            actions, _ = model.predict(obs, deterministic=True)
            obs, rewards, dones, infos = eval_env.step(actions)
            if dones[0]:
                winner = infos[0]["winner"]
                done_this_ep = True
        if winner == 1:
            wins += 1
        elif winner == 2:
            losses += 1
        else:
            draws += 1
    print(f"Eval results: {wins}W {losses}L {draws}D over {args.eval_episodes} episodes "
          f"({100.0 * wins / args.eval_episodes:.1f}% team win rate vs. the heuristic bot team)")

    header_path = None
    if not args.skip_export:
        from export_rl_policy_to_c import extract_layers_from_sb3_policy, write_c_header_from_layers
        layers = extract_layers_from_sb3_policy(model)
        header_path = os.path.join(args.output_dir, "rl_policy_weights_team.h")
        # Distinct guard_name/model_name/symbol_prefix from rl_train.py's own defaults -- this
        # header must never collide with the live 1v1 one (RL_POLICY_WEIGHTS_H /
        # RL_POLICY_MODEL / rl_policy_forward) if both ever get #included in the same
        # translation unit. symbol_prefix="TEAM_" is the other half of that fix (found live
        # while wiring this up -- see export_rl_policy_to_c.py's own doc comment on
        # write_c_header_from_layers): without it, OBS_SIZE/ACTION_SIZE/MOVE_TARGET_RANGE and
        # rl_policy_forward() itself were hardcoded regardless of guard_name/model_name, a real
        # duplicate-symbol compile error the moment both headers are included together.
        write_c_header_from_layers(layers, header_path,
                                    guard_name="RL_POLICY_WEIGHTS_TEAM_H",
                                    model_name="RL_POLICY_TEAM_MODEL",
                                    symbol_prefix="TEAM_")
        print(f"\nExported (inspection only, NOT wired into a live consumer yet): {header_path}")
        print(f"Deliberately not git-synced to {args.export_output} automatically -- unlike "
              f"rl_train.py's own --skip-git-sync default-on behavior, promoting a team-trained "
              f"policy into anything live is a real design decision (what consumes a "
              f"team-shaped input vector? NORTHSTAR §25.5 doesn't resolve this) that shouldn't "
              f"happen as a side effect of a training run.")

    print("=" * 60)
    print(f"DONE. Final policy: {final_path}.zip")
    extras = []
    if args.noisy_gestalt:
        extras.append("noisy-gestalt phased training (§25.2.2)")
    if args.autocurriculum:
        extras.append("PFSP autocurriculum opponent sampling (§25.4)")
    suffix = " with " + " + ".join(extras) + "." if extras else "."
    print(f"This is VS0's shared-parameter baseline (NORTHSTAR §25.2.1/§25.5){suffix}")
    print("Role discovery and synergy decay (§25.2.3) are specified but not implemented by this script.")
    print("File a completion Apple and mark the relevant EMILY/BACKLOG.md item done.")
    print("=" * 60)


if __name__ == "__main__":
    main()
