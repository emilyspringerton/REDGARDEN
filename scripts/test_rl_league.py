#!/usr/bin/env python3
"""
scripts/test_rl_league.py -- real unit tests for rl_league.py's own pure logic (LeagueManager
persistence, PFSP weighting/sampling, Main Exploiter's struggle detection + reset cadence). No
gymnasium/stable-baselines3/compiled .so dependency -- everything here is plain Python + the
filesystem, runnable in any environment this repo's other scripts already assume.

Run: python3 scripts/test_rl_league.py
"""

import os
import random
import shutil
import tempfile
import unittest

from rl_league import (
    HEURISTIC_ID,
    LeagueManager,
    LeagueRole,
    is_struggling_vs_main,
    pfsp_sample,
    pfsp_weight,
    sample_for_league_exploiter,
    sample_for_main,
    sample_for_main_exploiter,
    should_reset_main_exploiter,
    win_rate,
)


class TestWinRateSmoothing(unittest.TestCase):
    def test_untested_opponent_reads_as_neutral_half(self):
        self.assertAlmostEqual(win_rate(0, 0), 0.5)

    def test_all_wins_approaches_but_never_reaches_one(self):
        self.assertLess(win_rate(100, 0), 1.0)
        self.assertGreater(win_rate(100, 0), 0.95)

    def test_all_losses_approaches_but_never_reaches_zero(self):
        self.assertGreater(win_rate(0, 100), 0.0)
        self.assertLess(win_rate(0, 100), 0.05)


class TestPFSPWeight(unittest.TestCase):
    def test_favor_hard_weights_a_losing_matchup_higher_than_a_winning_one(self):
        # Standard PFSP direction (Main/League Exploiter): bias toward whatever the trainee is
        # currently LOSING to most.
        losing_matchup = pfsp_weight(wins=1, losses=20, favor_hard=True)
        winning_matchup = pfsp_weight(wins=20, losses=1, favor_hard=True)
        self.assertGreater(losing_matchup, winning_matchup)

    def test_favor_easy_inverts_the_bias(self):
        # Main Exploiter's own "climbing down" direction: bias toward whatever it can ALREADY
        # beat, not whatever it's losing to.
        losing_matchup = pfsp_weight(wins=1, losses=20, favor_hard=False)
        winning_matchup = pfsp_weight(wins=20, losses=1, favor_hard=False)
        self.assertGreater(winning_matchup, losing_matchup)

    def test_never_drops_below_the_floor(self):
        # A fully-solved (or fully-lost, in the favor_hard=False case) matchup must never drop
        # out of rotation entirely -- NORTHSTAR §25.4's own "plus the pool, not superseded" floor.
        self.assertGreaterEqual(pfsp_weight(wins=1000, losses=0, favor_hard=True), 0.01)
        self.assertGreaterEqual(pfsp_weight(wins=0, losses=1000, favor_hard=False), 0.01)


class TestPFSPSample(unittest.TestCase):
    def test_empty_candidates_returns_none(self):
        self.assertIsNone(pfsp_sample([], {}))

    def test_hard_biased_sampling_favors_the_currently_losing_opponent_over_many_trials(self):
        rng = random.Random(7)
        candidates = ["easy", "hard"]
        stats = {"easy": (50, 1), "hard": (1, 50)}
        counts = {"easy": 0, "hard": 0}
        for _ in range(2000):
            counts[pfsp_sample(candidates, stats, favor_hard=True, rng=rng)] += 1
        self.assertGreater(counts["hard"], counts["easy"],
                            "standard PFSP should sample the currently-losing opponent far more often")

    def test_easy_biased_sampling_favors_the_currently_winning_opponent_over_many_trials(self):
        rng = random.Random(7)
        candidates = ["easy", "hard"]
        stats = {"easy": (50, 1), "hard": (1, 50)}
        counts = {"easy": 0, "hard": 0}
        for _ in range(2000):
            counts[pfsp_sample(candidates, stats, favor_hard=False, rng=rng)] += 1
        self.assertGreater(counts["easy"], counts["hard"],
                            "Main Exploiter's climb-down bias should sample the beatable opponent far more often")

    def test_unknown_candidate_defaults_to_neutral_stats_not_a_crash(self):
        rng = random.Random(1)
        result = pfsp_sample(["brand-new"], {}, rng=rng)
        self.assertEqual(result, "brand-new")


class TestLeagueManager(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rl_league_test_")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_register_then_all_members_round_trips(self):
        mgr = LeagueManager(self.tmp)
        m = mgr.register(LeagueRole.MAIN, generation=3, path="ckpt/main_3.zip")
        members = mgr.all_members()
        self.assertEqual(len(members), 1)
        self.assertEqual(members[0].id, m.id)
        self.assertEqual(members[0].role, "main")
        self.assertEqual(members[0].generation, 3)
        self.assertEqual(members[0].path, "ckpt/main_3.zip")

    def test_registrations_are_permanent_never_evicted(self):
        mgr = LeagueManager(self.tmp)
        for gen in range(10):
            mgr.register(LeagueRole.MAIN, generation=gen, path=f"ckpt/main_{gen}.zip")
        # Real, deliberate contrast with §25.4's own MAX_CHECKPOINT_OPPONENTS=5 eviction -- every
        # one of these 10 registrations must still be present.
        self.assertEqual(len(mgr.all_members()), 10)

    def test_members_by_role_filters_correctly(self):
        mgr = LeagueManager(self.tmp)
        mgr.register(LeagueRole.MAIN, 0, "a.zip")
        mgr.register(LeagueRole.MAIN_EXPLOITER, 0, "b.zip")
        mgr.register(LeagueRole.MAIN, 1, "c.zip")
        self.assertEqual(len(mgr.members_by_role(LeagueRole.MAIN)), 2)
        self.assertEqual(len(mgr.members_by_role(LeagueRole.MAIN_EXPLOITER)), 1)
        self.assertEqual(len(mgr.members_by_role(LeagueRole.LEAGUE_EXPLOITER)), 0)

    def test_latest_by_role_picks_the_highest_generation(self):
        mgr = LeagueManager(self.tmp)
        mgr.register(LeagueRole.MAIN, 0, "gen0.zip")
        mgr.register(LeagueRole.MAIN, 5, "gen5.zip")
        mgr.register(LeagueRole.MAIN, 2, "gen2.zip")  # registered out of order on purpose
        latest = mgr.latest_by_role(LeagueRole.MAIN)
        self.assertEqual(latest.generation, 5)
        self.assertEqual(latest.path, "gen5.zip")

    def test_latest_by_role_returns_none_for_an_empty_role(self):
        mgr = LeagueManager(self.tmp)
        self.assertIsNone(mgr.latest_by_role(LeagueRole.LEAGUE_EXPLOITER))

    def test_two_manager_instances_over_the_same_dir_see_each_others_registrations(self):
        # The real, load-bearing property for three separate rl_train_team.py processes sharing
        # one --league-dir: a second LeagueManager instance (a different process, in real use)
        # pointed at the same directory must see what the first one registered.
        mgr_a = LeagueManager(self.tmp)
        mgr_a.register(LeagueRole.LEAGUE_EXPLOITER, 0, "le_0.zip")
        mgr_b = LeagueManager(self.tmp)
        self.assertEqual(len(mgr_b.all_members()), 1)
        self.assertEqual(mgr_b.all_members()[0].path, "le_0.zip")


class TestSampleForMain(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rl_league_test_")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_heuristic_is_always_a_candidate_even_with_an_empty_league(self):
        mgr = LeagueManager(self.tmp)
        result = sample_for_main(mgr, {}, rng=random.Random(1))
        self.assertEqual(result, HEURISTIC_ID)

    def test_samples_from_registered_members_too(self):
        mgr = LeagueManager(self.tmp)
        m = mgr.register(LeagueRole.LEAGUE_EXPLOITER, 0, "le.zip")
        # Force the registered member to always win the weight race: give the heuristic a
        # terrible (currently-crushing) matchup for Main so PFSP favors the OTHER one instead.
        stats = {HEURISTIC_ID: (100, 0), m.id: (0, 100)}
        rng = random.Random(3)
        counts = {HEURISTIC_ID: 0, m.id: 0}
        for _ in range(500):
            counts[sample_for_main(mgr, stats, rng=rng)] += 1
        self.assertGreater(counts[m.id], counts[HEURISTIC_ID])


class TestSampleForLeagueExploiter(unittest.TestCase):
    def test_uses_the_same_whole_league_pool_as_main(self):
        tmp = tempfile.mkdtemp(prefix="rl_league_test_")
        try:
            mgr = LeagueManager(tmp)
            mgr.register(LeagueRole.MAIN, 0, "main0.zip")
            rng_a, rng_b = random.Random(9), random.Random(9)
            stats = {HEURISTIC_ID: (1, 1)}
            # Same seed, same candidate pool, same stats -- both functions should be able to draw
            # the exact same real member id (structurally identical sampling, per this module's
            # own doc comment on why League Exploiter isn't a different formula from Main).
            main_pick = sample_for_main(mgr, stats, rng=rng_a)
            le_pick = sample_for_league_exploiter(mgr, stats, rng=rng_b)
            self.assertEqual(main_pick, le_pick)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


class TestIsStrugglingVsMain(unittest.TestCase):
    def test_empty_history_is_not_struggling(self):
        # Main Exploiter's real default behavior is to challenge Main from the start, not to
        # assume it's already struggling before playing a single episode.
        self.assertFalse(is_struggling_vs_main([]))

    def test_mostly_losing_counts_as_struggling(self):
        self.assertTrue(is_struggling_vs_main([0] * 18 + [1] * 2))  # 10% win rate

    def test_mostly_winning_does_not_count_as_struggling(self):
        self.assertFalse(is_struggling_vs_main([1] * 18 + [0] * 2))  # 90% win rate

    def test_respects_a_custom_threshold(self):
        results = [1] * 4 + [0] * 6  # 40% win rate
        self.assertFalse(is_struggling_vs_main(results, threshold=0.3))
        self.assertTrue(is_struggling_vs_main(results, threshold=0.5))


class TestSampleForMainExploiter(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rl_league_test_")

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_no_main_checkpoint_yet_returns_none(self):
        mgr = LeagueManager(self.tmp)
        result = sample_for_main_exploiter(mgr, {}, recent_results_vs_main=[], rng=random.Random(1))
        self.assertIsNone(result)

    def test_not_struggling_always_challenges_the_freshest_main(self):
        mgr = LeagueManager(self.tmp)
        mgr.register(LeagueRole.MAIN, 0, "main0.zip")
        current = mgr.register(LeagueRole.MAIN, 1, "main1.zip")
        result = sample_for_main_exploiter(
            mgr, {}, recent_results_vs_main=[1, 1, 1, 1, 1], rng=random.Random(1)
        )
        self.assertEqual(result, current.id)

    def test_struggling_climbs_down_to_historical_main_biased_toward_beatable_ones(self):
        mgr = LeagueManager(self.tmp)
        easy = mgr.register(LeagueRole.MAIN, 0, "main0.zip")
        current = mgr.register(LeagueRole.MAIN, 5, "main5.zip")
        stats = {easy.id: (50, 1), current.id: (0, 50)}
        rng = random.Random(4)
        counts = {easy.id: 0, current.id: 0}
        for _ in range(500):
            picked = sample_for_main_exploiter(
                mgr, stats, recent_results_vs_main=[0] * 20, rng=rng  # 0% win rate -> struggling
            )
            counts[picked] += 1
        self.assertGreater(counts[easy.id], counts[current.id],
                            "a struggling exploiter should climb down toward the checkpoint it can already beat")


class TestShouldResetMainExploiter(unittest.TestCase):
    def test_generation_zero_never_resets(self):
        self.assertFalse(should_reset_main_exploiter(0, reset_every_n_generations=5))

    def test_resets_on_the_configured_cadence(self):
        self.assertFalse(should_reset_main_exploiter(4, reset_every_n_generations=5))
        self.assertTrue(should_reset_main_exploiter(5, reset_every_n_generations=5))
        self.assertTrue(should_reset_main_exploiter(10, reset_every_n_generations=5))

    def test_non_positive_cadence_disables_reset_entirely(self):
        self.assertFalse(should_reset_main_exploiter(100, reset_every_n_generations=0))
        self.assertFalse(should_reset_main_exploiter(100, reset_every_n_generations=-1))


if __name__ == "__main__":
    unittest.main()
