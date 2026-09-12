#!/usr/bin/env python3
"""
scripts/rl_league.py -- AlphaStar-style league training, a real, additive extension of NORTHSTAR
§25.4's existing PFSP autocurriculum (see §25.4.1 for the full design doc).

Founder real-time direction, citing a real, named source (a YouTube explainer of DeepMind's own
AlphaStar league, timestamps 13:01-13:55): §25.4's existing autocurriculum is plain PFSP against
ONE policy's own growing checkpoint pool -- exactly the "simple self-play" the video names as
vulnerable to CYCLIC DOMINANCE (a rock-paper-scissors dynamic: training against your current self
makes you better against that one opponent while quietly getting worse against others). AlphaStar's
own real fix is a LEAGUE of three distinct agent roles trained simultaneously, each contributing
permanent, never-evicted checkpoints back into one shared pool:

  - MAIN: trains against the whole league (PFSP-weighted, biased toward whatever it currently
    loses to most) -- pushes the skill frontier forward, same real objective §25.4's own single
    policy already has, just sampling from a shared multi-lineage pool instead of only its own
    history.
  - MAIN_EXPLOITER: has ONE job -- find and break Main's CURRENT weaknesses. Challenges Main's
    freshest checkpoint directly; if it can't win consistently (no learning signal, nothing to
    exploit yet), it "climbs down" to Main's own historical checkpoints instead, biased toward
    ones it can ALREADY beat (the opposite bias from standard PFSP) to build up real skill before
    re-challenging the current Main. Periodically resets to a freshly initialized network --
    prevents the exploiter itself from converging onto one narrow trick and forces it to
    rediscover exploits from scratch.
  - LEAGUE_EXPLOITER: same PFSP-weighted whole-league sampling as Main, but a SEPARATE lineage --
    its whole purpose is finding weaknesses that persist across the ENTIRE league (not just
    Main's current self), so nothing here ever holds a permanent blind spot as a group.

Real, honest, necessary scope note on "current Main": this repo trains each role as a SEPARATE
process (see rl_train_team.py's own --league-role flag and run_league.sh's own orchestration) --
there is no single shared in-memory model across processes. "Current Main" therefore means
Main's own most-recently-REGISTERED checkpoint on disk, re-read from the shared, file-based
LeagueManager registry each time it's needed, not literally the exact in-memory weights of a
live-training process at that instant. This is the real, honest tradeoff of a checkpoint-based
multi-process league (not a hack -- any file-based league without extra IPC would work this way),
named directly rather than silently smoothed over.

Every registered checkpoint is PERMANENT -- unlike §25.4's own existing MAX_CHECKPOINT_OPPONENTS=5
eviction, nothing here is ever removed from the league. Per-opponent win/loss stats used for PFSP
weighting stay LOCAL to whichever process is doing the sampling (same architecture §25.4's own
ArenaTeamVecEnv.opponent_wins/opponent_losses already uses -- each trainee's own experience against
an opponent is what should bias ITS OWN curriculum, not a globally-shared, harder-to-keep-correct
stat); only the REGISTRY of which checkpoints exist is shared across processes, via one small JSON
file per registration under <league_dir>/members/ -- append-only, so no locking is needed even
with three real concurrent writers (a new registration is always a brand-new file, never an
overwrite of another process's).
"""

import dataclasses
import enum
import glob
import json
import os
import random
import time
import uuid


class LeagueRole(str, enum.Enum):
    MAIN = "main"
    MAIN_EXPLOITER = "main_exploiter"
    LEAGUE_EXPLOITER = "league_exploiter"


# Same real AlphaStar PFSP constants NORTHSTAR §25.4 already established for the single-pool
# case (rl_env_team.py's own PFSP_SHARPNESS/PFSP_MIN_WEIGHT) -- reused here as the one, shared,
# canonical implementation rather than a second copy of the same math. See
# rl_env_team.py's own import of this module for how the existing single-pool path now delegates
# here instead of keeping its own duplicate.
PFSP_SHARPNESS = 2
PFSP_MIN_WEIGHT = 0.01

HEURISTIC_ID = "heuristic"  # sentinel matching §25.4's own permanent baseline convention


def _role_str(role):
    """Normalizes a LeagueRole (or a plain string) to its real "main"/"main_exploiter"/
    "league_exploiter" value. Real, found-live gap: `str(LeagueRole.MAIN)` is "LeagueRole.MAIN",
    not "main" -- Enum.__str__ wins over the str mixin's own __str__ despite `class LeagueRole
    (str, enum.Enum)`. Every place that needs role as a plain string (persisted JSON, filename
    construction, equality against a caller-supplied plain string) goes through this instead."""
    return role.value if isinstance(role, LeagueRole) else str(role)


def win_rate(wins, losses):
    """Beta(1,1)-smoothed win rate -- an untested opponent (0 wins, 0 losses) reads as a neutral
    0.5, not 0.0, so a freshly registered checkpoint gets a real sampling chance before any stats
    exist against it. Identical smoothing to rl_env_team.py's own pre-existing _sample_opponent."""
    return (wins + 1) / (wins + losses + 2)


def pfsp_weight(wins, losses, sharpness=PFSP_SHARPNESS, min_weight=PFSP_MIN_WEIGHT, favor_hard=True):
    """AlphaStar's own f_hard(x) = (1-x)^p weighting when favor_hard=True (bias toward whatever
    is currently WINNING against this trainee least often, i.e. hardest for the trainee) -- the
    real, standard PFSP direction Main and League Exploiter both use.

    favor_hard=False inverts it to x^p -- MAIN_EXPLOITER's own real "climbing down" behavior
    (§25.4.1): when it can't beat the current Main at all, bias toward Main's OWN historical
    checkpoints it can ALREADY beat, to build up real skill before re-challenging, rather than
    continuing to sample opponents that give it zero learning signal. This inverted direction is
    this module's own reasonable interpretation of the video's "climbs down through Main's
    historical checkpoints" description -- the source describes the BEHAVIOR, not an exact
    formula, so this is named as an interpretation, not a verbatim reproduction of DeepMind's own
    unpublished-here pseudocode.
    """
    x = win_rate(wins, losses)
    base = (1 - x) if favor_hard else x
    return max(base ** sharpness, min_weight)


def pfsp_sample(candidate_ids, local_wins_losses, favor_hard=True, rng=None):
    """Samples one id from candidate_ids, weighted by pfsp_weight() computed from
    local_wins_losses (a dict id -> (wins, losses); missing ids default to (0, 0), i.e. an
    untested 0.5 win rate). Returns None for an empty candidate list -- callers decide the
    fallback (rl_env_team.py's own single-pool path always has at least the heuristic sentinel,
    so this should never actually return None there).
    """
    if not candidate_ids:
        return None
    rng = rng or random
    weights = [
        pfsp_weight(*local_wins_losses.get(cid, (0, 0)), favor_hard=favor_hard)
        for cid in candidate_ids
    ]
    return rng.choices(candidate_ids, weights=weights, k=1)[0]


@dataclasses.dataclass
class LeagueMember:
    id: str
    role: str
    generation: int
    path: str
    created_at: float


class LeagueManager:
    """Shared, permanent, append-only, cross-process checkpoint registry -- see this module's own
    doc comment for why append-only-per-registration needs no locking even under real concurrent
    writers (three separate rl_train_team.py --league processes, one per role)."""

    def __init__(self, league_dir):
        self.league_dir = league_dir
        self.members_dir = os.path.join(league_dir, "members")
        os.makedirs(self.members_dir, exist_ok=True)

    def register(self, role, generation, path):
        """Permanently adds one checkpoint to the league. Never overwrites or evicts an existing
        entry -- the real, deliberate difference from §25.4's own MAX_CHECKPOINT_OPPONENTS=5
        eviction (video: "every frozen copy of an agent is kept in the league forever")."""
        role = _role_str(role)
        member = LeagueMember(
            id=f"{role}_{generation}_{uuid.uuid4().hex[:8]}",
            role=role,
            generation=generation,
            path=path,
            created_at=time.time(),
        )
        out_path = os.path.join(self.members_dir, member.id + ".json")
        # Write to a temp name then rename -- rename is atomic on the same filesystem, so a
        # concurrent all_members() glob/read from another process never observes a half-written
        # file (the real, minimal reason this design needs no explicit lock).
        tmp_path = out_path + f".tmp{os.getpid()}"
        with open(tmp_path, "w") as f:
            json.dump(dataclasses.asdict(member), f)
        os.replace(tmp_path, out_path)
        return member

    def all_members(self):
        """Re-reads every registered member from disk on every call -- deliberately not cached:
        checkpoint registrations are infrequent (once per --save-freq chunk, not per tick/episode),
        so re-globbing a small directory is cheap, and this is the one mechanism that lets three
        separate processes see each other's newly-registered checkpoints at all."""
        members = []
        for fp in sorted(glob.glob(os.path.join(self.members_dir, "*.json"))):
            try:
                with open(fp) as f:
                    data = json.load(f)
                members.append(LeagueMember(**data))
            except (json.JSONDecodeError, OSError, TypeError):
                # A malformed/partial file should never be possible given the atomic rename above,
                # but a real filesystem hiccup (e.g. a read racing a delete) degrades to "skip this
                # one entry," not a crash -- there will be a next all_members() call.
                continue
        return members

    def members_by_role(self, role):
        role = _role_str(role)
        return [m for m in self.all_members() if m.role == role]

    def latest_by_role(self, role):
        """The single freshest (highest-generation) member of a role, or None if that role has no
        registered members yet -- MAIN_EXPLOITER's own real "current Main" (see this module's own
        doc comment on why that means the freshest checkpoint, not a live in-memory model)."""
        candidates = self.members_by_role(role)
        if not candidates:
            return None
        return max(candidates, key=lambda m: (m.generation, m.created_at))


def sample_for_main(league, local_wins_losses, rng=None):
    """MAIN's own real opponent curriculum: PFSP-weighted (favor_hard, the standard direction)
    over EVERY registered league member (any role, any generation) plus the permanent heuristic
    baseline -- generalizes §25.4's own single-lineage pool to the whole multi-role league."""
    candidate_ids = [HEURISTIC_ID] + [m.id for m in league.all_members()]
    return pfsp_sample(candidate_ids, local_wins_losses, favor_hard=True, rng=rng)


def sample_for_league_exploiter(league, local_wins_losses, rng=None):
    """LEAGUE_EXPLOITER's own real opponent curriculum -- structurally identical sampling to
    MAIN's (whole-league, PFSP-weighted, standard hard-biased direction): the real distinguishing
    feature isn't a different formula, it's that this trains a SEPARATE policy lineage (its own
    checkpoints register under role=league_exploiter), so the league gains a second, independent
    line of attack against whatever the whole group is currently weak to -- exactly the video's
    own "prevents blind spots" framing."""
    return sample_for_main(league, local_wins_losses, rng=rng)


DEFAULT_STRUGGLE_WINDOW = 20  # episodes -- matches rl_train_team.py's own DEFAULT --eval-episodes
                                # order of magnitude; enough episodes for a real win-rate read
                                # without waiting an excessive number of matches to notice
DEFAULT_STRUGGLE_THRESHOLD = 0.3  # win rate vs. current Main below this over the last
                                    # DEFAULT_STRUGGLE_WINDOW episodes counts as "struggling" --
                                    # a real, tunable v0 choice, not derived from the source video
                                    # (which names the BEHAVIOR, not a numeric threshold)


def is_struggling_vs_main(recent_results, threshold=DEFAULT_STRUGGLE_THRESHOLD):
    """recent_results: an iterable of 1 (won) / 0 (lost) outcomes, most-recent-last, already
    windowed by the caller (see DEFAULT_STRUGGLE_WINDOW). An empty history is NOT struggling --
    MAIN_EXPLOITER should challenge the current Main from the very start, matching the video's
    own "has the sole job of finding and breaking the current weaknesses of the Main agent" as
    the real default behavior, not the fallback."""
    results = list(recent_results)
    if not results:
        return False
    return (sum(results) / len(results)) < threshold


def sample_for_main_exploiter(league, local_wins_losses, recent_results_vs_main, rng=None):
    """MAIN_EXPLOITER's own real, two-mode curriculum (§25.4.1, video 13:20/13:51):
      - Not struggling (the common case): always challenges Main's CURRENT (freshest registered)
        checkpoint directly -- its one real job.
      - Struggling (win rate vs. current Main below DEFAULT_STRUGGLE_THRESHOLD over the last
        DEFAULT_STRUGGLE_WINDOW episodes, or no Main checkpoint exists to challenge yet): "climbs
        down" through Main's own historical checkpoints, PFSP-weighted toward ones it can ALREADY
        beat (favor_hard=False -- see pfsp_weight's own doc comment for why this is inverted from
        the standard direction) rather than the hardest ones, to build up real skill before
        re-challenging the current Main.
    Returns None only if Main has registered zero checkpoints at all yet (nothing to challenge or
    climb through) -- callers should fall back to the permanent heuristic in that case, the same
    real "there's always at least the heuristic" floor §25.4's own single-pool design already has.
    """
    current_main = league.latest_by_role(LeagueRole.MAIN)
    if current_main is None:
        return None
    if not is_struggling_vs_main(recent_results_vs_main):
        return current_main.id
    main_history = [m.id for m in league.members_by_role(LeagueRole.MAIN)]
    return pfsp_sample(main_history, local_wins_losses, favor_hard=False, rng=rng)


def should_reset_main_exploiter(generation, reset_every_n_generations):
    """MAIN_EXPLOITER's own real periodic full reset (video 13:55: "every few generations, the
    Main Exploiter resets to a randomly initialized neural network") -- generation 0 never resets
    (nothing has been learned yet to reset away from), and reset_every_n_generations <= 0 disables
    resetting entirely (a real, explicit opt-out, not a divide-by-zero risk)."""
    if reset_every_n_generations <= 0:
        return False
    return generation > 0 and generation % reset_every_n_generations == 0
