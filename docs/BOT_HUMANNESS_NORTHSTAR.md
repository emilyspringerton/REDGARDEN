# RED GARDEN — Bot Humanness Northstar

`status: design northstar; not implemented`

## 0. Why this is a separate document

`PING_SYSTEM_NORTHSTAR.md` (upstream Codex PR, 2026-09-02, reviewed and extended the same day)
specs the ping protocol itself: wire event, authority, human UI, and a bot contract that is
correctly scoped to *legality* — what a bot may emit or act on given its own perception. It
deliberately does not say anything about *feel* — how fast, how eagerly, how consistently a bot
should respond once an action is legal. That's a real, separate design surface with its own prior
art in this monorepo (`MISHRI`), its own failure modes (a mechanically perfect responder reads as
a bot even when every individual decision is legal), and its own hard constraint the ping doc's
`peers, not puppets` framing doesn't by itself resolve: REDGARDEN sims must stay **replay-
deterministic**, and `MISHRI`'s own techniques were built for a live, non-replayed, non-competitive
context that never had to worry about that. Splitting keeps the protocol doc protocol-shaped and
gives this real, different problem its own acceptance criteria instead of diluting both.

Scope: REDGARDEN's own arena bots (`apps/arena_bot`'s deterministic heuristic AI, the thing that
actually plays every live match today — see NORTHSTAR.md §25.1). The RL-trained policy
(`rl_policy_forward`/`team_rl_policy_forward`) is explicitly **out of scope for v0**: it's already
a learned, additive movement nudge layered on top of the heuristic bot, not the decision-maker a
ping needs to reach, and giving a neural net "humanness" is a different, much later problem (see
§9). Everything below is about the deterministic bot that already forms squads and claims nodes.

## 1. The core tension this document resolves

The ping doc's own §7 already gets the important thing right: a bot must never treat a human ping
as an order with guaranteed compliance, and every bot action stays bounded by its own legal
perception, role, and safety rules. What it doesn't specify is the one thing that makes a squad of
bots *feel* like teammates instead of a scripted response system once you actually watch them play
with pings live: **timing and consistency are themselves a tell.** A bot that emits `Danger`
the exact tick its threshold crosses, that has every squad member independently compute the exact
same speaker via `my_owner % squad_count` and fire in the same tick with zero variance, and that
reacts to an ally's `Attack` ping with 100% identical latency every time, is legally correct and
looks like a machine. A human teammate notices a call, has a beat of "wait, what," and then acts —
inconsistently, with visible personality. This document is about adding *that*, without touching
what a bot is allowed to know or do.

## 2. Prior art: MISHRI's real, shipped humanness layer

`MISHRI` (`src/humanness/HumannessLayer.ts`, `src/behavior/BehaviorOrchestrator.ts`,
`src/perception/PerceptionManager.ts`) is a real, working human-likeness layer for a Minecraft
bot, with a stated philosophy worth quoting directly because it's exactly the target feel: *"A
human is not a noisy machine. A human is a creature that hesitates, breathes, forgets, gets
distracted, and occasionally does things for no reason."* Concrete mechanisms actually shipped
there, cited by real name because they're the vocabulary the rest of this doc borrows:

- **Mood** — an 8-state enum (`neutral | curious | tired | bored | social | focused | startled |
  nervous`) that every other system reads from, cycling on its own timer and shifting on real
  triggers (`getStartled()` on taking damage).
- **Continuous internal state** — `energy`, `curiosity`, `socialEnergy`, `boredom`,
  `fatigueAccum`, each drifting on its own schedule and feeding back into mood and into the two
  core timing primitives below.
- **`delay(min, max)`** — the one real primitive everything else calls: a randomized wait,
  stretched by `fatigueAccum` and low `energy`. **`reactionDelay()`** branches on mood
  (`startled` → half the base range, faster; `tired`/low-energy → 1.5–2× slower) before calling
  `delay`. **`chatDelay()`** is explicitly separate and slower ("humans think, then type, then
  send"), scaled by `socialEnergy`.
- **APM throttle** — a hard cap on actions per rolling 60s window; once hit, the bot waits out the
  window plus jitter rather than instantly resuming at the cap boundary.
- **Imperfect compliance / attention** — `maybeTypo` (swap/delete/duplicate/adjacent-key), and the
  README's own "ignores messages" / mood-dependent talkativeness: the bot doesn't respond to
  everything, and what it does with an input isn't always exactly correct.
- **Unprompted, low-stakes "personality" behaviors** — `maybeDoubleTake`, `nervousLookAround`,
  `maybeScrollHotbar`, `maybeSneakPeek`: small, chance-gated actions with no functional purpose
  except reading as alive.
- **Utility-scored decision loop** (`BehaviorOrchestrator._decideBehavior`) — behaviors are scored
  and picked probabilistically, on a variable 3–18s cadence, not on a fixed tick — decisions
  visibly don't all land on the same rhythm.

## 3. What transfers, and the one thing that must change: determinism

Every mechanism above transfers as a *shape* — a delay curve, a mood-conditioned modifier, a
compliance-noise roll, a per-slot personality. **None of the code transfers**, because `MISHRI`
draws every one of those numbers from `Math.random()`/wall-clock timers, which is exactly right
for a live, unreplayed social bot and exactly wrong here: `PING_SYSTEM_NORTHSTAR.md` §9.4
requires a *replay determinism test*, and a bot whose ping-response timing depends on real-world
`Math.random()` calls made at unpredictable points in a variable-length frame produces a different
replay every time it's re-simulated.

This repo already has the right convention, established independently and for an unrelated
reason: `arena_game.c`'s item-curriculum stat blending (`arena_item_curriculum_blend_int`) needs
"jitter... reproducible across restarts," so it uses **a plain integer hash of its own real inputs
(item ids, slot index, a fixed constant) run through a Knuth multiplicative constant
(`2654435761u`), never `rand()`/`srand()`.** That's the pattern this document adopts wholesale:
every "random" humanness number a bot uses must be `hash(server_tick, owner_slot, purpose_tag) →
[0,1)`, not a PRNG call. Same input state always produces the same jitter, so a replay reproduces
bit-for-bit; a live match still looks fully random because `server_tick` and `owner_slot` differ
every time.

**Named, existing counterexample in this exact codebase, flagged not fixed here**: the synergy-
decay system (NORTHSTAR §25.3, `synergy_roll_tier` in `arena_game.c`) uses real `rand()` for its
weighted tier roll — a real, pre-existing gap against the same determinism bar this document is
holding itself to, presumably tolerable today because nothing has actually exercised replay
determinism against a synergy-decay-affected match yet. Don't copy that convention going forward;
don't fix it as part of this work either — it's a separate, already-shipped system and a separate
decision.

## 4. Per-bot temperament: a persistent trait, not per-call noise

A squad of bots that are humanized identically and independently re-rolled every tick just
becomes *uniformly* twitchy — still a tell, just a different one. Real teammates have consistent
personalities: the same ally is reliably the fast caller, another is reliably the last to react.

Give each bot slot a **temperament vector**, derived once per match, deterministically, from
`(match_seed, owner_slot, hero_id)` via the same hash-not-rand convention above — no learned
model, just a handful of stable scalars in `[0,1)`:

| Trait | What it modulates |
|---|---|
| `reaction_bias` | Shifts §5's reaction-delay range faster/slower for this bot specifically. |
| `emission_eagerness` | How close to the bare legal threshold (ping doc §7 "Emission") this bot pings — a low value waits for a clearer case; a high value calls it the instant it's legal. |
| `compliance_noise` | Chance this bot under- or over-reacts to an accepted teammate ping (§6). |
| `chattiness` | This bot's share of nominated-speaker ping volume, biasing (not overriding) squad speaker selection (§7). |

This is deliberately the cheapest possible version of "persona," on purpose: NORTHSTAR §25.2.2
already built the real prerequisite for a *learned* per-agent identity (`sim_get_obs_team`'s
agent-identity one-hot, feeding an eventual ROMA/RODE-style embedding). When that lands, the
learned embedding is the natural, better replacement for this hand-rolled temperament vector — an
obvious future wire-up, not a competing system. Ship the cheap deterministic version first; it's
enough to make the difference readable in a live match today.

## 5. Reaction and emission timing

Two separate delays, matching `MISHRI`'s own real `reactionDelay()`/`chatDelay()` split (a bot
"noticing," then "deciding to speak," are different beats):

**Emission delay** — once a bot's legal ping-emission threshold (ping doc §7) crosses, don't fire
immediately. Roll a deterministic delay in a base range (e.g. 300–900ms), modulated by:
- `temperament.reaction_bias` and `temperament.emission_eagerness` (§4);
- an urgency multiplier — a `Danger` triggered by a hero this bot just watched go from full health
  to critical in one hit gets `MISHRI`'s own `startled` treatment (roughly halve the delay); a
  quiet `On My Way` rotation call gets the full range.

**Consumption delay** — a bot doesn't re-plan its own utility the instant an accepted teammate
ping's event reaches it. Apply the same shape on the receiving side: a short "reading" delay
before the ping doc §7 "Consumption" utility adjustment actually takes effect, faster for a
high-urgency type (`Danger`, `Enemy Missing`) than a low-urgency one (`On My Way`), and faster for
a bot whose own local state already agrees with the call (matches `MISHRI`'s "startled = fast" —
corroborating evidence should never slow a bot down).

Both delays are hashes of `(server_tick_at_trigger, owner_slot, ping_type)` — never wall-clock,
per §3.

## 6. Imperfect compliance, not obedience

The ping doc's §7 already states a bot must not treat a human ping as a guaranteed order. This
section gives that a concrete mechanism instead of leaving it purely qualitative, directly
analogous to `MISHRI`'s "ignores messages" / imperfect-skills behavior, scoped so it can never
break a legality invariant:

- Roll `temperament.compliance_noise` (deterministic, §4) against the accepted ping's utility
  delta before applying it. A low-compliance bot dampens the adjustment (mirrors a `focused`-mood
  `MISHRI` bot finishing what it's doing instead of instantly redirecting); a high-compliance bot
  slightly amplifies it (mirrors an eager, `social`-mood response).
- This modulates **how strongly and how fast** a bot reweights its utility toward a called action.
  It must never let a bot's squad-to-node claim (`hero_squad_target_node`) become inconsistent
  with its squadmates', never grant or hide legal information, and never violate any safety/role
  rule the ping doc §7 already names as non-negotiable. Humanness is a coefficient on an
  already-legal decision, not a second decision system.
- A bot that's already strongly committed (mid-cast, low HP fleeing, holding a just-claimed node)
  should be *more* resistant to an incoming ping's pull than one that's idle between objectives —
  same "don't abandon a higher-priority commitment without a defined override rule" the ping doc
  §7 already requires, just given a real per-bot dial instead of an all-or-nothing rule.

## 7. Squad-level humanness

The ping doc's speaker-election idea ("a squad should normally nominate one speaker... the lowest
stable owner slot currently near the event") is correct as a *legality/dedup* mechanism — it stops
ten bots reporting the same fact — but computed with zero variance it produces something no real
squad does: every bot in the squad agreeing on the exact same speaker in the exact same tick,
forever. Two additions, both deterministic:

- **Nomination jitter** — the nominated speaker's own ping still fires (§5's emission delay
  already adds some natural spread), but bias `chattiness` (§4) so it isn't strictly always the
  lowest owner slot — occasionally (deterministically, per-match) a different squad member is the
  one who "gets there first," the way a real team's designated caller convention gets broken in
  practice.
- **Rare, bounded redundancy** — a non-speaker squad member occasionally still emits the same
  call, a beat later, mirroring a real ally who wasn't sure the first ping landed. Must stay inside
  the ping doc's own §5 spam-control budget (per-sender rate limit, clustering window) — this is
  flavor within the existing limits, not a reason to widen them.

## 8. What we explicitly do not import from MISHRI

Scoping this correctly matters as much as building it. Several real `MISHRI` features have no
honest analog here and should not be ported just because the source material has them:

- **No bot chat, typos, or free text.** The ping doc's own product boundary (§3) explicitly
  excludes free-form text/chat from v0. Pings are the bot's entire voice; there is no "typing"
  step to humanize.
- **No AFK simulation.** A `MISHRI` bot goes idle and logs off because it's simulating an
  unsupervised player. An arena bot mid-match has no honest equivalent — it always has a
  squad/role obligation.
- **No wrong-block/wrong-item-style mistakes applied to combat legality.** `MISHRI`'s "mines wrong
  blocks, fumbles inventory" is safe because it's a low-stakes single-player action. A REDGARDEN
  bot being tactically suboptimal is already true of the heuristic AI today and fine; this
  document must never make a bot violate game rules, damage math, or its own perception model in
  the name of "feeling human." Humanness governs presentation-layer timing and compliance
  *strength*, never simulation legality.
- **No mood-driven social behavior for its own sake** (`MISHRI`'s hotbar-scroll/sneak-peek/fidget
  have zero REDGARDEN analog — there's no idle-flavor slot in a real-time team fight).

## 9. Delivery plan

Sequenced to land after the ping system's own Bot v0 (`PING_SYSTEM_NORTHSTAR.md` §9.3), as an
additive wrapper around it, never a prerequisite for it:

1. **Deterministic hash utility.** One shared `bot_humanness_roll(server_tick, owner_slot,
   purpose_tag) -> [0,1)` function (or small family), following §3's convention exactly. Unit
   tested for reproducibility (same inputs → same output) and independence (different
   `purpose_tag`s don't correlate).
2. **Temperament.** Per-slot temperament vector computed once at match init from
   `(match_seed, owner_slot, hero_id)`. Logged per bot for inspection, same discipline
   `PING_SYSTEM_NORTHSTAR.md` §7 already asks of bot ping decisions.
3. **Emission/consumption delay.** Wrap the ping doc's Bot v0 emission/consumption paths with
   §5's delay model. A/B-able behind a flag (`--bot-humanness=off` reproduces Bot v0's mechanical
   behavior exactly, for training/evaluation runs that don't want the extra latency variance).
4. **Compliance noise + squad jitter.** §6 and §7, same flag-gated approach.
5. **Evaluation.** A replay-determinism test with humanness *on* (same match seed → identical
   replay, twice) — the one non-negotiable acceptance bar. Then a real human playtest, blind to
   which squad members are humanized, scored on the same axis `MISHRI`'s own README states as its
   own bar: *can a player watching tell the difference, from ping cadence alone?*
6. **Learned temperament (future, not this pass).** Once NORTHSTAR §25.2.2's real per-agent
   identity embedding exists, evaluate replacing §4's hand-rolled temperament vector with a
   projection of that learned embedding — humanness parameters that emerge from training instead
   of being hand-tuned, the same "training decides, not a hand-written rule" philosophy §25.2.2
   already applies to role discovery.

## 10. Acceptance criteria

- A shared, unit-tested deterministic roll utility exists and is the only source of "randomness"
  anything in this document touches — no `rand()`/`Math.random()`/wall-clock timing anywhere in
  the humanness path.
- Disabling humanness (`--bot-humanness=off` or equivalent) reproduces Bot v0's exact mechanical
  behavior — humanness is provably a pure additive timing/compliance layer, never a second
  decision system, never a source of a different *outcome*.
- A replay recorded with humanness enabled reproduces bit-for-bit on re-simulation from the same
  seed.
- No humanness roll ever causes a bot to violate a legality invariant already established in
  `PING_SYSTEM_NORTHSTAR.md` §6/§7 (fog of war, forged target/team, squad-to-node consistency,
  safety override rules).
- Per-bot temperament is stable for the life of a match (not re-rolled per ping) and visibly
  different bot-to-bot in logs.
- A blind playtest reviewer cannot reliably distinguish, from ping timing/cadence alone, which
  squad members are bots — the same bar `MISHRI`'s own README sets for itself, ported honestly
  rather than just asserted.

## 11. Open questions

1. Should `emission_eagerness`/`chattiness` be visible to the human squad (e.g., "this teammate
   calls early") or purely an internal presentation detail? This doc assumes purely internal for
   v0 — no new UI surface.
2. Does `apps/arena_bot`'s deterministic bot ever need to distinguish a *human* ally's ping from a
   *bot* ally's ping for compliance-noise purposes (real teammates might weight a human's call
   differently than a bot's)? Not addressed here; the ping wire event doesn't currently carry that
   distinction either (`PING_SYSTEM_NORTHSTAR.md` §8's `ArenaPingEvent` has no sender-is-bot flag).
3. Once §9.6's learned-temperament path lands, does `--bot-humanness=off` still need to exist for
   training/evaluation, or does the RL environment simply never wrap bots in this layer at all
   (bots inside `apps/arena_training/src/headless.c` never emit/consume pings today per this
   document's own §0 scope note)? Leaning toward the latter — flagged, not decided.
