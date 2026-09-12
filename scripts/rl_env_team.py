#!/usr/bin/env python3
"""
scripts/rl_env_team.py (NORTHSTAR §25.2.1) -- multi-agent extension of scripts/rl_env.py, wrapping
apps/arena_training/src/headless.c's team-mode ctypes API (sim_init_team/sim_step_team/
sim_get_obs_team/sim_get_done_team/sim_get_winner_team) for shared-parameter multi-agent PPO
training against a full team, not just 1v1.

Architecture: team_size agents on team A (heroes[0..team_size-1], being trained) vs. team_size
heuristic-driven agents on team B (heroes[ARENA_TEAM_SIZE..ARENA_TEAM_SIZE+team_size-1], stable,
non-circular opponent -- same reasoning rl_env.py's own module doc comment already gives for why
hero 1 in the 1v1 case can't be the live-training policy). ALL team-A agents share ONE arena_state
and advance together, one sim_step_team() call per tick -- not team_size independent physics
worlds.

Exposed as a Stable-Baselines3 `VecEnv` (not team_size separate gymnasium.Env instances wrapped in
SB3's own SubprocVecEnv) specifically because that's the wrong shape here: SB3's normal VecEnv
assumption is N independent environments with independent episode lifecycles, which is exactly
what N separate physics worlds would give you -- but a real team match's N agents share one match,
one win/loss outcome, one clock. Implementing team_size "slots" of ONE shared match AS an SB3
VecEnv (step_async/step_wait taking/returning N-stacked arrays, one real sim_step_team() call per
step_wait(), all N slots auto-reset together when the shared match ends) gets genuine
shared-parameter multi-agent PPO training out of SB3's existing, already-used PPO implementation
with zero new dependencies (no PettingZoo, no custom trainer) -- SB3's PPO already knows how to
learn one shared policy from a VecEnv's N parallel rollout streams; the only real change is that
those N streams come from one team match instead of N independent ones.

Reward: reuses scripts/rl_env.py's own compute_reward() per-agent, per-tick -- each team-A agent's
own 18-scalar self/foe-in-front block (the first ARENA_TRAINING_OBS_SIZE floats of its own
sim_get_obs_team() slice) has the identical layout compute_reward() already expects, and
sim_get_winner_team()'s 1=team-A/2=team-B convention already matches compute_reward's own
agent_owner=0 assumption exactly (every team-A agent "wins" when winner==1), so no reward-function
duplication is needed here.

NOTE ON VERIFICATION: gymnasium/stable-baselines3 (2.9.0) are installed in this environment as of
2026-08-10 and ArenaTeamVecEnv has been run for real via rl_train_team.py, including catching and
fixing the critical sim_step_team-calls-1v1-tick bug (see NORTHSTAR §25.2.1's own history) --
this is no longer spec-only. `python3 scripts/rl_env_team.py --smoke-test` still works without
gymnasium/SB3 for a ctypes-only sanity check when iterating on the C side in isolation.

Autocurriculum (§25.4, `autocurriculum=True`): team B's opponent is sampled once per episode from
`self.opponent_pool` (the permanent "heuristic" baseline plus a small, capped population of past
self-play checkpoints added live via `add_opponent_checkpoint()` as `rl_train_team.py`'s own
checkpoint-save loop produces them), biased via Prioritized Fictitious Self-Play (see
`_sample_opponent()`'s own doc comment) toward whichever opponent the CURRENT policy has been
losing to most. When the sampled opponent isn't the heuristic, team B's actions come from that
checkpoint's own loaded SB3 model, run against team B's OWN real perspective
(`sim_get_obs_team_any(..., my_team=1, ...)`) and applied via `sim_step_team_vs_actions` -- the
exact two C-level primitives this section's own bug-fix pass built as its prerequisite.

League mode (§25.4.1, rl_league.py, founder real-time citing a real AlphaStar league-training
video): `league_manager`/`league_role` are a real, additive, opt-in EXTENSION of the plain
autocurriculum above, addressing the "simple self-play -> cyclic dominance" failure mode the
video names. When `league_manager` is set, opponent sampling and checkpoint registration both
delegate to rl_league.py's own role-specific logic (sample_for_main/sample_for_main_exploiter/
sample_for_league_exploiter, LeagueManager's permanent cross-process registry) instead of this
class's own small, evicting, single-lineage `opponent_pool` -- see rl_league.py's own module doc
comment for the full design and rl_train_team.py's own `--league`/`--league-role` flags for how
three separate training processes actually run the three roles concurrently against one shared
`--league-dir`. `league_manager=None` (the default) is byte-for-byte the pre-existing
autocurriculum behavior above -- nothing about this class's own prior behavior changes when
league mode isn't requested.
"""

import argparse
import collections
import ctypes

from rl_env import (
    ARENA_HALF_EXTENT,
    ARENA_TRAINING_OBS_SIZE,
    DEFAULT_LIB_PATH,
    compute_reward,
)
from rl_league import (
    DEFAULT_STRUGGLE_WINDOW,
    HEURISTIC_ID,
    LeagueRole,
    sample_for_league_exploiter,
    sample_for_main,
    sample_for_main_exploiter,
)

# Noisy-gestalt alternating phased training (2026-08-10, founder: "ensure we are doing some of
# the new exotic training" -- NORTHSTAR §25.2.2, sourced from the CarePyre transcript's
# "Compositional Co-Adaptation" section). The transcript's own two-phase design, applied here for
# real (this was previously spec-only -- see rl_train_team.py's own "NOT yet trained here" note,
# now stale for this one piece):
#   Johnny phase: crank up a Synergy Reward so bots are heavily rewarded just for staying near a
#     living teammate -- "they will invent crazy, highly choreographed team setups. Win rate
#     drops, but team coordination skyrockets."
#   Spike phase: turn the Synergy Reward off entirely -- "the bots take the wild, highly
#     choreographed combo they just invented and strip out the fluff, refining it into a lethal,
#     meta-viable strategy." Reduces to the plain baseline reward from rl_env.py's compute_reward,
#     unchanged from before this feature existed.
# The transcript's own version computes synergy from a learned mutual-information objective
# requiring model internals SB3's stock PPO doesn't expose per-tick; this is a real, grounded,
# but simpler proxy computed straight from data already on the wire -- sim_get_obs_team's own
# teammate block (hp_frac, dx, dz, alive) already carries relative teammate position for exactly
# this reason -- a positional-proximity bonus for staying near a LIVING teammate, rather than the
# transcript's own attention-based mutual-info term. Simpler, but grounded in the same "reward
# genuinely interacting with each other" intent, and immediately runnable without a bigger
# architecture change.
REWARD_SYNERGY_PER_LIVING_TEAMMATE_NEARBY = 0.05  # small per-tick nudge, same order of magnitude
                                                    # as REWARD_ALIVE_PER_TICK in rl_env.py -- a
                                                    # constant presence incentive, not a
                                                    # damage/kill-scale reward that could dominate
                                                    # the real win-condition signal
SYNERGY_PROXIMITY_RADIUS = 8.0  # matches this file's sibling arena_game.h's own
                                  # ARENA_LANE_CREEP_XP_SHARE_RADIUS (8.0) -- "close enough to be
                                  # meaningfully fighting together," not just anywhere on the map
DEFAULT_GESTALT_PHASE_TICKS = 50_000  # ticks per phase (not SB3 timesteps exactly -- see
                                        # ArenaTeamVecEnv._global_tick's own doc comment) -- long
                                        # enough for a real Johnny-phase exploration window before
                                        # switching to Spike-phase refinement, short enough that a
                                        # single training run gets several alternations rather
                                        # than one Johnny phase that never actually resolves

# ARENA_TEAM_SIZE (2026-08-10): hand-synced copy of packages/simulation/arena_game.h's own real
# constant -- same "no direct C header access from Python" reasoning every other hand-synced
# constant in this file's sibling rl_env.py already documents for itself.
ARENA_TEAM_SIZE = 10

DEFAULT_TEAM_SIZE = 3  # NORTHSTAR §25.6 deliberately leaves the real first-run team size
                        # undecided -- 3v3 is a reasonable, small first target, easily overridden.

# Per-agent observation size: the same 18+2*ARENA_HERO_COUNT self/foe/hero-id-onehot block
# sim_get_obs's own layout already uses, PLUS (team_size-1)*4 teammate floats, PLUS a final
# team_size-long agent-identity one-hot (NORTHSTAR §25.2.2 role discovery prerequisite, added
# 2026-08-10 -- see headless.c's own sim_get_obs_team doc comment for the full reasoning: without
# this block the shared PPO policy has no signal for "which of my team_size copies am I," so it
# cannot differentiate behavior per slot even where that would score higher). Must match
# apps/arena_training/src/headless.c's own sim_get_obs_team() return value exactly. A function,
# not a constant, since it depends on team_size.
def team_obs_size(team_size):
    return ARENA_TRAINING_OBS_SIZE + (team_size - 1) * 4 + team_size


def load_team_lib(lib_path=None):
    """Loads the compiled shared library and sets up ctypes argtypes/restype for the team-mode
    functions specifically (see rl_env.py's own load_lib() for the 1v1 functions -- both can be
    called against the same loaded library, since headless.c exports both APIs additively)."""
    lib = ctypes.CDLL(lib_path or DEFAULT_LIB_PATH)
    lib.sim_init_team.argtypes = [
        ctypes.c_int, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
    ]
    lib.sim_init_team.restype = None
    lib.sim_step_team.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_int, ctypes.c_uint]
    lib.sim_step_team.restype = None
    lib.sim_get_obs_team.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_float)]
    lib.sim_get_obs_team.restype = ctypes.c_int
    lib.sim_get_done_team.argtypes = [ctypes.c_int]
    lib.sim_get_done_team.restype = ctypes.c_int
    lib.sim_get_winner_team.argtypes = [ctypes.c_int]
    lib.sim_get_winner_team.restype = ctypes.c_int
    # §25.4 autocurriculum prerequisite (headless.c, 2026-08-10) -- see ArenaTeamVecEnv's own
    # opponent-pool doc comment below for what these two drive.
    lib.sim_get_obs_team_any.argtypes = [
        ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_float),
    ]
    lib.sim_get_obs_team_any.restype = ctypes.c_int
    lib.sim_step_team_vs_actions.argtypes = [
        ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float),
        ctypes.c_int, ctypes.c_uint,
    ]
    lib.sim_step_team_vs_actions.restype = None
    return lib


# §25.4 autocurriculum: opponent-pool tuning constants.
MAX_CHECKPOINT_OPPONENTS = 5  # "a small population of past checkpoints" -- NORTHSTAR §25.4's own
                                # wording. Oldest checkpoint opponent is evicted (along with its
                                # win/loss stats) once a new one pushes the pool past this size;
                                # the permanent "heuristic" slot is never evicted.
PFSP_SHARPNESS = 2  # AlphaStar's own Prioritized Fictitious Self-Play weighting, f_hard(x) =
                      # (1-x)^p for win rate x against a given opponent -- the real, named
                      # technique NORTHSTAR §25.4's "bias toward whichever it's losing to most"
                      # description is describing. p=2 is AlphaStar's own paper default.
PFSP_MIN_WEIGHT = 0.01  # floor so an opponent the policy has fully solved (win rate -> 1, weight
                          # -> 0) doesn't drop out of rotation entirely -- NORTHSTAR §25.4 keeps
                          # the heuristic "plus" the checkpoint pool, not replaced by it.


try:
    from stable_baselines3.common.vec_env.base_vec_env import VecEnv
    import numpy as np
    from gymnasium import spaces

    class ArenaTeamVecEnv(VecEnv):
        """SB3 VecEnv wrapping ONE shared team match with `team_size` team-A agent "slots" --
        see this module's own doc comment for why VecEnv (not team_size independent
        gymnasium.Env instances) is the right shape here."""

        def __init__(self, team_size=DEFAULT_TEAM_SIZE, lib_path=None, dt_ms=16,
                     max_episode_ticks=4000, hero_ids_a=None, hero_ids_b=None,
                     noisy_gestalt=False, gestalt_phase_ticks=DEFAULT_GESTALT_PHASE_TICKS,
                     autocurriculum=False, league_manager=None, league_role=None):
            if team_size < 2 or team_size > ARENA_TEAM_SIZE:
                raise ValueError(f"team_size must be in [2, {ARENA_TEAM_SIZE}], got {team_size}")
            self.team_size = team_size
            self.lib = load_team_lib(lib_path)
            self.dt_ms = dt_ms
            self.max_episode_ticks = max_episode_ticks
            # Noisy-gestalt alternating phased training -- see this module's own doc comment
            # above REWARD_SYNERGY_PER_LIVING_TEAMMATE_NEARBY for the full design. _global_tick
            # counts real step_wait() calls across the WHOLE env lifetime (survives episode
            # resets, unlike self._tick which is per-episode) -- an env-tick proxy for SB3's own
            # global timestep counter, not an exact match to it (SB3 counts across n_steps
            # rollout boundaries with its own bookkeeping this env has no visibility into), but
            # close enough for "which phase are we in" purposes -- phase drift of a few hundred
            # ticks against SB3's own counter doesn't change the qualitative Johnny/Spike
            # alternation this feature is actually for.
            self.noisy_gestalt = noisy_gestalt
            self.gestalt_phase_ticks = gestalt_phase_ticks
            self._global_tick = 0
            self._last_logged_phase = None
            # Hero-per-slot: fixed unless the caller randomizes externally (unlike rl_env.py's
            # own randomize_heroes flag, not duplicated here yet -- NORTHSTAR §25.6 leaves this
            # open; the C API already accepts arbitrary hero_ids arrays, so adding per-episode
            # randomization later is additive, not a redesign).
            self.hero_ids_a = hero_ids_a or [0] * team_size  # default: everyone Unicorn
            self.hero_ids_b = hero_ids_b or [0] * team_size
            self._c_hero_ids_a = (ctypes.c_int * team_size)(*self.hero_ids_a)
            self._c_hero_ids_b = (ctypes.c_int * team_size)(*self.hero_ids_b)

            # §25.4 autocurriculum -- see _sample_opponent()'s own doc comment for the full
            # design. Pool slot 0 is always the permanent heuristic baseline (sentinel string
            # "heuristic", never a real checkpoint path); slots 1+ are past-checkpoint paths.
            self.autocurriculum = autocurriculum
            self.opponent_pool = ["heuristic"]
            self.opponent_wins = [0]    # team-A (current policy) wins while this opponent was B
            self.opponent_losses = [0]  # team-A losses while this opponent was B
            self._opponent_model_cache = {}  # checkpoint path -> loaded PPO model
            self._current_opponent_idx = 0

            # §25.4.1 league mode (rl_league.py) -- see this module's own doc comment above.
            # Real, deliberate design: league_manager being set takes priority over the plain
            # `autocurriculum` pool above (both being set at once shouldn't happen from
            # rl_train_team.py's own CLI, but if it did, league mode is the real, richer one).
            self.league_manager = league_manager
            self.league_role = league_role
            self._use_league = league_manager is not None
            self._league_wins = {}    # candidate id (HEURISTIC_ID or a league member id) -> wins
            self._league_losses = {}  # same keys -> losses
            self._current_opponent_key = HEURISTIC_ID  # HEURISTIC_ID or a league member id
            # Main Exploiter's own real struggle-tracking (rl_league.is_struggling_vs_main) --
            # only ever populated when league_role == MAIN_EXPLOITER; a rolling window of 1/0
            # outcomes from episodes that specifically challenged the CURRENT (freshest) Main,
            # not episodes spent climbing down through Main's history (a different matchup, see
            # rl_league.sample_for_main_exploiter's own doc comment).
            self._recent_vs_current_main = collections.deque(maxlen=DEFAULT_STRUGGLE_WINDOW)
            self._challenging_current_main_this_episode = False

            self._obs_size = team_obs_size(team_size)
            self._tick = 0
            self._prev_obs = None  # list of team_size np.ndarray, one per agent slot
            self._actions = None

            observation_space = spaces.Box(
                low=-1e4, high=1e4, shape=(self._obs_size,), dtype=np.float32
            )
            # Same [move_x, move_z, cast_q, cast_w, cast_r] shape as rl_env.py's own
            # ArenaTrainingEnv -- one action per agent slot, cast_* thresholded at >0 in
            # _apply_actions below, matching the 1v1 env's own convention exactly.
            action_space = spaces.Box(
                low=np.array([-ARENA_HALF_EXTENT, -ARENA_HALF_EXTENT, -1.0, -1.0, -1.0], dtype=np.float32),
                high=np.array([ARENA_HALF_EXTENT, ARENA_HALF_EXTENT, 1.0, 1.0, 1.0], dtype=np.float32),
                dtype=np.float32,
            )
            super().__init__(team_size, observation_space, action_space)
            self._do_reset()

        def _synergy_bonus(self, agent_obs):
            """Johnny-phase-only synergy proxy: sums REWARD_SYNERGY_PER_LIVING_TEAMMATE_NEARBY
            for every OTHER living team-A agent within SYNERGY_PROXIMITY_RADIUS, read straight
            off this agent's own teammate block in sim_get_obs_team's layout -- (team_size-1)
            slots of (hp_frac, dx, dz, alive) starting right after the shared 18+2*ARENA_HERO_COUNT
            self/foe block (see headless.c's own sim_get_obs_team doc comment for the exact
            layout this indexes into)."""
            bonus = 0.0
            for t in range(self.team_size - 1):
                base = ARENA_TRAINING_OBS_SIZE + t * 4
                alive = agent_obs[base + 3]
                if alive <= 0.5:
                    continue
                dx = agent_obs[base + 1]
                dz = agent_obs[base + 2]
                if (dx * dx + dz * dz) ** 0.5 <= SYNERGY_PROXIMITY_RADIUS:
                    bonus += REWARD_SYNERGY_PER_LIVING_TEAMMATE_NEARBY
            return bonus

        def add_opponent_checkpoint(self, path, generation=None):
            """§25.4 autocurriculum -- called from rl_train_team.py's own checkpoint-save loop
            each time a new checkpoint lands, so self-play opponents grow richer as training
            progresses instead of staying fixed at whatever existed when the env was constructed.
            No-op if autocurriculum is off AND league mode is off, so callers don't need to guard
            every call site.

            League mode (§25.4.1): registers `path` PERMANENTLY into the shared, cross-process
            `self.league_manager` under `self.league_role` instead of this class's own small,
            evicting local pool -- `generation` is required in this mode (rl_train_team.py's own
            checkpoint-save loop tracks and passes it; see rl_league.LeagueManager.register)."""
            if self._use_league:
                if generation is None:
                    raise ValueError("add_opponent_checkpoint: generation is required in league mode")
                self.league_manager.register(self.league_role, generation, path)
                print(f"[league] registered {self.league_role}/gen{generation}: {path}")
                return
            if not self.autocurriculum:
                return
            self.opponent_pool.append(path)
            self.opponent_wins.append(0)
            self.opponent_losses.append(0)
            # Evict the OLDEST checkpoint opponent (index 1 -- index 0 is the permanent heuristic
            # slot and is never evicted) once the pool exceeds MAX_CHECKPOINT_OPPONENTS real
            # checkpoints -- "a SMALL population," per NORTHSTAR §25.4's own wording.
            if len(self.opponent_pool) - 1 > MAX_CHECKPOINT_OPPONENTS:
                evicted_path = self.opponent_pool.pop(1)
                self.opponent_wins.pop(1)
                self.opponent_losses.pop(1)
                self._opponent_model_cache.pop(evicted_path, None)
                # _current_opponent_idx can't point past index 1 here (a fresh episode always
                # re-samples in _do_reset before any mid-episode add_opponent_checkpoint call
                # matters), so no index-shift bookkeeping is needed for it.
            print(f"[autocurriculum] opponent pool now: {self.opponent_pool} "
                  f"(added {path})")

        def _sample_opponent(self):
            """§25.4 autocurriculum: Prioritized Fictitious Self-Play (AlphaStar's own PFSP) --
            weight_i = (1 - win_rate_i)^PFSP_SHARPNESS, so opponents the current policy is
            currently LOSING to most get sampled most often, with a Beta(1,1)-smoothed win rate
            (untested opponents default to 0.5, not 0/1) so a freshly-added checkpoint gets a real
            shot before its stats exist, and a PFSP_MIN_WEIGHT floor so a fully-solved opponent
            (win_rate -> 1) never drops out of rotation entirely -- NORTHSTAR §25.4 keeps the
            heuristic baseline "plus" the checkpoint pool, not superseded by it. Returns the
            sampled index into self.opponent_pool."""
            weights = []
            for wins, losses in zip(self.opponent_wins, self.opponent_losses):
                win_rate = (wins + 1) / (wins + losses + 2)
                weights.append(max((1 - win_rate) ** PFSP_SHARPNESS, PFSP_MIN_WEIGHT))
            weights = np.array(weights, dtype=np.float64)
            probs = weights / weights.sum()
            return int(np.random.choice(len(self.opponent_pool), p=probs))

        def _sample_league_opponent(self):
            """§25.4.1 league mode: dispatches to rl_league.py's own role-specific sampler using
            THIS process's local win/loss stats (self._league_wins/self._league_losses -- same
            "each trainee's own local experience biases its own curriculum" architecture the
            plain autocurriculum pool above already uses, just keyed by league member id instead
            of pool index). Returns a candidate id (HEURISTIC_ID or a league member id) and also
            updates self._challenging_current_main_this_episode for Main Exploiter's own real
            struggle-tracking (see the constructor's own doc comment on
            self._recent_vs_current_main)."""
            self._challenging_current_main_this_episode = False
            if self.league_role == LeagueRole.MAIN_EXPLOITER:
                current_main = self.league_manager.latest_by_role(LeagueRole.MAIN)
                picked = sample_for_main_exploiter(
                    self.league_manager, self._league_wins_losses(),
                    list(self._recent_vs_current_main),
                )
                if picked is None:
                    return HEURISTIC_ID  # Main hasn't registered a single checkpoint yet
                if current_main is not None and picked == current_main.id:
                    self._challenging_current_main_this_episode = True
                return picked
            if self.league_role == LeagueRole.LEAGUE_EXPLOITER:
                return sample_for_league_exploiter(self.league_manager, self._league_wins_losses())
            # MAIN (the default/expected role when league mode is on) -- and any other/unset
            # role defensively falls back to Main's own whole-league sampling rather than crashing.
            return sample_for_main(self.league_manager, self._league_wins_losses())

        def _league_wins_losses(self):
            """(wins, losses) dict shape rl_league.pfsp_sample expects, built from this process's
            own two parallel dicts -- kept as two dicts rather than one dict-of-tuples so
            recording a win/loss (step_wait, below) is a single dict increment, not a
            read-tuple/rebuild-tuple/write-back round trip."""
            return {
                cid: (self._league_wins.get(cid, 0), self._league_losses.get(cid, 0))
                for cid in set(self._league_wins) | set(self._league_losses)
            }

        def _load_opponent_model(self, path):
            if path not in self._opponent_model_cache:
                from stable_baselines3 import PPO
                self._opponent_model_cache[path] = PPO.load(path)
            return self._opponent_model_cache[path]

        def _resolve_league_path(self, candidate_id):
            """League mode only: candidate_id is HEURISTIC_ID or a league member id (from
            _sample_league_opponent) -- resolves it to a real checkpoint path, or None for the
            heuristic sentinel. Falls back to the heuristic (logged, not silently) if the id
            can't be found -- should not happen given LeagueManager's own permanent, never-
            evicted registrations, but a real filesystem race (see LeagueManager.all_members's
            own doc comment) degrades safely here rather than crashing a long training run."""
            if candidate_id == HEURISTIC_ID:
                return None
            for member in self.league_manager.all_members():
                if member.id == candidate_id:
                    return member.path
            print(f"[league] WARNING: sampled id {candidate_id!r} not found in league registry, "
                  f"falling back to heuristic this episode")
            return None

        def _actions_to_flat(self, actions):
            """Shared [move_x, move_z, cast_q>0, cast_w>0, cast_r>0] flattening used for both
            team-A (from the policy being trained) and team-B (from a sampled opponent, when the
            opponent isn't the plain heuristic) -- same convention step_wait already applied to
            team A before §25.4 existed."""
            flat = (ctypes.c_float * (self.team_size * 5))()
            for i in range(self.team_size):
                a = actions[i]
                flat[i * 5 + 0] = float(a[0])
                flat[i * 5 + 1] = float(a[1])
                flat[i * 5 + 2] = 1.0 if a[2] > 0 else 0.0
                flat[i * 5 + 3] = 1.0 if a[3] > 0 else 0.0
                flat[i * 5 + 4] = 1.0 if a[4] > 0 else 0.0
            return flat

        def _read_all_obs(self):
            obs_list = []
            for i in range(self.team_size):
                buf = (ctypes.c_float * self._obs_size)()
                self.lib.sim_get_obs_team(i, self.team_size, buf)
                obs_list.append(np.array(buf[:], dtype=np.float32))
            return obs_list

        def _do_reset(self):
            self.lib.sim_init_team(self.team_size, self._c_hero_ids_a, self._c_hero_ids_b)
            self._tick = 0
            if self._use_league:
                # Same "once per EPISODE, not per tick" reasoning as the plain autocurriculum
                # branch below -- see that branch's own comment.
                self._current_opponent_key = self._sample_league_opponent()
            elif self.autocurriculum:
                # Opponent is sampled once per EPISODE, not per tick -- "the next episode's
                # opponent," NORTHSTAR §25.4's own wording -- so team B plays one coherent
                # opponent for the whole match rather than flickering between policies tick to
                # tick, which would give neither a fair evaluation nor a stable environment for
                # team A to learn against.
                self._current_opponent_idx = self._sample_opponent()
            obs = self._read_all_obs()
            self._prev_obs = obs
            return obs

        # --- VecEnv abstract interface ---

        def reset(self):
            obs = self._do_reset()
            return np.stack(obs, axis=0)

        def step_async(self, actions):
            self._actions = np.asarray(actions, dtype=np.float32)

        def step_wait(self):
            # ONE sim_step_team()/sim_step_team_vs_actions() call for the whole team this tick --
            # see this module's own doc comment for why this must be a single batched call, not
            # team_size individual ones.
            flat_a = self._actions_to_flat(self._actions)

            if self._use_league:
                opponent = self._resolve_league_path(self._current_opponent_key)
            else:
                opponent = self.opponent_pool[self._current_opponent_idx] if self.autocurriculum else "heuristic"
                opponent = None if opponent == "heuristic" else opponent
            if opponent is None:
                # Unchanged path -- byte-identical to pre-§25.4 behavior, including when
                # autocurriculum/league mode are both off entirely (opponent is always the
                # heuristic in that case).
                self.lib.sim_step_team(flat_a, self.team_size, self.dt_ms)
            else:
                # §25.4/§25.4.1: sampled opponent is a past self-play checkpoint (either the
                # plain autocurriculum pool or a league member) -- get team B's OWN perspective
                # via sim_get_obs_team_any (my_team=1), run it through that checkpoint's policy,
                # and drive team B with the real resulting actions instead of the fixed heuristic.
                model = self._load_opponent_model(opponent)
                obs_b = []
                for i in range(self.team_size):
                    buf = (ctypes.c_float * self._obs_size)()
                    self.lib.sim_get_obs_team_any(ARENA_TEAM_SIZE + i, 1, self.team_size, buf)
                    obs_b.append(np.array(buf[:], dtype=np.float32))
                actions_b, _ = model.predict(np.stack(obs_b, axis=0), deterministic=True)
                flat_b = self._actions_to_flat(actions_b)
                self.lib.sim_step_team_vs_actions(flat_a, flat_b, self.team_size, self.dt_ms)
            self._tick += 1
            self._global_tick += 1

            obs = self._read_all_obs()
            done = bool(self.lib.sim_get_done_team(self.team_size))
            winner = self.lib.sim_get_winner_team(self.team_size)
            truncated = self._tick >= self.max_episode_ticks
            episode_over = done or truncated

            # Noisy-gestalt: even phase index = Johnny (synergy reward ON), odd = Spike (OFF) --
            # see this module's own doc comment above REWARD_SYNERGY_PER_LIVING_TEAMMATE_NEARBY.
            in_johnny_phase = (
                self.noisy_gestalt
                and (self._global_tick // self.gestalt_phase_ticks) % 2 == 0
            )
            if self.noisy_gestalt:
                phase_name = "Johnny (synergy ON)" if in_johnny_phase else "Spike (synergy OFF)"
                if phase_name != self._last_logged_phase:
                    print(f"[noisy-gestalt] tick {self._global_tick}: entering {phase_name} phase")
                    self._last_logged_phase = phase_name

            rewards = np.zeros(self.team_size, dtype=np.float32)
            for i in range(self.team_size):
                rewards[i] = compute_reward(
                    self._prev_obs[i], obs[i], done, winner, agent_owner=0
                )
                if in_johnny_phase:
                    rewards[i] += self._synergy_bonus(obs[i])
            self._prev_obs = obs

            dones = np.full(self.team_size, episode_over, dtype=bool)
            infos = [{"winner": winner, "tick": self._tick, "TimeLimit.truncated": truncated}
                     for _ in range(self.team_size)]

            if episode_over:
                # §25.4 autocurriculum: record the outcome against whichever opponent was active
                # THIS episode before _do_reset() below samples the NEXT one. Only on a real
                # decided match (`done`, winner in {1, 2}) -- a truncated (timed-out) episode has
                # no real winner and would just add noise to the win-rate estimate PFSP samples
                # from.
                if self._use_league and done and winner in (1, 2):
                    won = winner == 1
                    key = self._current_opponent_key
                    if won:
                        self._league_wins[key] = self._league_wins.get(key, 0) + 1
                    else:
                        self._league_losses[key] = self._league_losses.get(key, 0) + 1
                    # Main Exploiter's own real struggle-tracking (rl_league.is_struggling_vs_main)
                    # -- only fed by episodes that specifically challenged the CURRENT Main, not
                    # ones spent climbing down through its history (see
                    # rl_league.sample_for_main_exploiter's own doc comment).
                    if self._challenging_current_main_this_episode:
                        self._recent_vs_current_main.append(1 if won else 0)
                elif self.autocurriculum and done and winner in (1, 2):
                    if winner == 1:
                        self.opponent_wins[self._current_opponent_idx] += 1
                    else:
                        self.opponent_losses[self._current_opponent_idx] += 1
                # All team_size slots terminate together (shared match outcome) -- reset the
                # whole match now and hand back FRESH initial observations for every slot, same
                # "auto-reset on done" contract SB3's own VecEnv expects from each sub-env,
                # just applied to all team_size slots at once since they share one episode.
                for info in infos:
                    info["terminal_observation"] = None  # filled in below, per-slot
                fresh_obs = self._do_reset()
                for i, info in enumerate(infos):
                    info["terminal_observation"] = obs[i]
                obs_out = np.stack(fresh_obs, axis=0)
            else:
                obs_out = np.stack(obs, axis=0)

            return obs_out, rewards, dones, infos

        def close(self):
            pass

        def get_attr(self, attr_name, indices=None):
            return [getattr(self, attr_name)] * self.team_size

        def set_attr(self, attr_name, value, indices=None):
            setattr(self, attr_name, value)

        def env_method(self, method_name, *args, indices=None, **kwargs):
            return [getattr(self, method_name)(*args, **kwargs)] * self.team_size

        def env_is_wrapped(self, wrapper_class, indices=None):
            return [False] * self.team_size

except ImportError:
    ArenaTeamVecEnv = None  # gymnasium/stable_baselines3 not installed -- see this module's own
                             # doc comment


def _smoke_test(team_size):
    """Runs without gymnasium/SB3 installed -- exercises load_team_lib()/sim_step_team()/
    compute_reward() directly via plain ctypes, mirroring rl_env.py's own _smoke_test()."""
    lib = load_team_lib()
    hero_ids_a = (ctypes.c_int * team_size)(*([0] * team_size))
    hero_ids_b = (ctypes.c_int * team_size)(*([0] * team_size))
    lib.sim_init_team(team_size, hero_ids_a, hero_ids_b)

    obs_size = team_obs_size(team_size)
    prev_obs = []
    for i in range(team_size):
        buf = (ctypes.c_float * obs_size)()
        lib.sim_get_obs_team(i, team_size, buf)
        prev_obs.append(list(buf))
    print(f"team_size={team_size}, obs_size={obs_size}")
    print("initial obs[0]:", prev_obs[0])

    total_reward = [0.0] * team_size
    done = False
    winner = 0
    flat = (ctypes.c_float * (team_size * 5))()
    for tick in range(3000):
        # Naive scripted policy: everyone advances toward map center and tries every cast --
        # enough to force real engagement (agents from both teams end up in range of each
        # other), not a real trained policy.
        for i in range(team_size):
            flat[i * 5 + 0] = 0.0
            flat[i * 5 + 1] = 0.0
            flat[i * 5 + 2] = 1.0
            flat[i * 5 + 3] = 1.0
            flat[i * 5 + 4] = 1.0
        lib.sim_step_team(flat, team_size, 16)

        cur_obs = []
        for i in range(team_size):
            buf = (ctypes.c_float * obs_size)()
            lib.sim_get_obs_team(i, team_size, buf)
            cur_obs.append(list(buf))

        done = bool(lib.sim_get_done_team(team_size))
        winner = lib.sim_get_winner_team(team_size)
        for i in range(team_size):
            total_reward[i] += compute_reward(prev_obs[i], cur_obs[i], done, winner, agent_owner=0)
        prev_obs = cur_obs
        if done:
            print(f"episode ended at tick {tick}, winner={winner}")
            break

    print("total reward per agent:", total_reward)
    print("done:", done, "winner:", winner)

    if ArenaTeamVecEnv is None:
        print("\ngymnasium/stable_baselines3 not installed -- ArenaTeamVecEnv itself was NOT "
              "exercised, only the raw ctypes team API above. See this module's own doc comment.")
    else:
        print("\ngymnasium/stable_baselines3 ARE installed -- running a real ArenaTeamVecEnv "
              "rollout too:")
        env = ArenaTeamVecEnv(team_size=team_size)
        obs = env.reset()
        ep_reward = np.zeros(team_size)
        for i in range(400):
            actions = np.array([env.action_space.sample() for _ in range(team_size)])
            actions[:, 0] = 0.0
            actions[:, 1] = 0.0
            obs, rewards, dones, infos = env.step(actions)
            ep_reward += rewards
            if dones[0]:
                print(f"VecEnv rollout ended at tick {i}, info={infos[0]}")
                break
        print("VecEnv rollout total reward per agent:", ep_reward)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--smoke-test", action="store_true",
                   help="run a scripted rollout against the compiled .so and print reward "
                        "totals -- works with or without gymnasium/SB3 installed")
    p.add_argument("--team-size", type=int, default=DEFAULT_TEAM_SIZE)
    args = p.parse_args()
    if args.smoke_test:
        _smoke_test(args.team_size)
    else:
        p.print_help()
