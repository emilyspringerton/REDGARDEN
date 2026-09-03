# RED GARDEN — Ping System Northstar

`status: product and protocol northstar; not implemented`

*Authored upstream via a Codex PR (`garyredg/codex/write-northstar-document-for-ping-system`,
merged 2026-09-02) without this monorepo's own session context. Reviewed the same day: the
protocol/product design below holds up and is largely unchanged. §7 has been extended to ground
the bot contract in REDGARDEN's actual existing bot code (not just the abstraction it already
correctly described) and to split out the deeper "how should a bot's ping behavior actually
**feel**" question into its own document — see
[`BOT_HUMANNESS_NORTHSTAR.md`](BOT_HUMANNESS_NORTHSTAR.md), which also carries a real replay-
determinism constraint this document's own §9.4 already implicitly requires but didn't spell out
for bot-side timing.*

## 1. The job

RED GARDEN needs a **fast, spatial, team-only communication system** for the arena. It must let
humans and bots coordinate in the same match without stopping to type, opening a menu, or assuming
voice chat. The reference bar is League of Legends' ping language: an intent is placed on the world,
is visible and audible to the squad, and expires before it becomes UI clutter.

This is not chat with icons. A ping is a short-lived, structured tactical assertion:

> **who** is signaling, **what** they mean, **where/whom** they mean it about, and **when** the
> information is relevant.

The same authoritative event model serves human input, deterministic bot policy, replays,
spectators, and future training data. Bots must never receive hidden information through pings;
they may only emit or act on pings whose inputs are legal under their own perception model.

## 2. North-star experience

In a crowded 10v10 fight, a player sees an ally mark a wounded enemy, hears one clear confirmation,
and immediately understands “focus this target.” A bot in the same squad sees the same public event
and may decide to converge, peel, or decline based on its role and local state. No one needs to read
a sentence, and no individual needs to spam commands to make the team move together.

The system succeeds when:

- A human can make the common call in **one deliberate input**, while moving and casting.
- An ally can identify the **meaning, source, and location/target** in under a glance.
- A bot can use a ping as one bounded signal among perception, role, squad assignment, and safety
  rules—not as a magical global commander override.
- Repeated or stale signals are suppressed rather than escalating audio and visual noise.
- A match replay can answer what was called, by whom, and whether the team responded.

## 3. Product boundaries

### In scope

- Team-only tactical pings for humans and bots in the arena's multiplayer/team modes.
- Ground, hero, objective/node, jungle-camp, structure, and ally-target pings.
- A radial input surface plus direct shortcut path; mouse/keyboard is the v0 baseline, with
  controller/touch parity designed rather than assumed.
- Server-authoritative validation, replication, replay recording, rate limiting, and client display.
- Bot emission and consumption through the exact same wire event as humans.

### Explicitly not v0

- Free-form text, voice, direct messages, or all-chat.
- Enemy-visible pings, taunts, or a system designed to harass an opponent.
- A fully autonomous “obey every ping” behavior tree or a substitute for squad AI.
- Hidden-information scouting via a remote ping target.
- Post-match analytics, moderation tooling, localization polish, or custom player ping wheels.

Those may be worthwhile later, but they must not delay a compact, legible tactical vocabulary.

## 4. Shared tactical vocabulary

The first wheel is intentionally small. Every ping has one unambiguous gameplay meaning and one
consistent color, icon, sound family, map marker, and bot-facing enum. Do not overload one icon with
both “help me” and “attack here.”

| Ping | Human meaning | Valid targets | Bot interpretation (advisory) |
|---|---|---|---|
| **On My Way** | I intend to rotate here. | Ground, allied hero, objective | Adds a short-lived arrival-intent signal; may avoid duplicating the rotation. |
| **Assist Me** | I need allied presence here. | Ground, self/ally, objective | Raises local support priority; never forces an unsafe path. |
| **Attack / Focus** | Commit pressure to this target or area. | Visible enemy, objective, ground | Raises target utility if target is legal and reachable. |
| **Danger / Retreat** | Do not enter or continue this area. | Ground, visible enemy, objective | Raises avoidance/retreat utility; cancels only lower-priority intents. |
| **Enemy Missing** | A known enemy is no longer accounted for. | Last known visible enemy location | Records last-seen warning, not current enemy truth. |
| **Hold / Defend** | Protect this allied objective or lane. | Allied node, structure, ground | Raises defensive squad assignment utility. |

**Target-aware rendering is mandatory.** “Attack” on a hero identifies that hero; “Defend” on a
node identifies the node; a ground ping shows a world position. A ping that cannot identify its
referent is not actionable enough to ship.

### 4.5 System-generated objective alerts — resolving S189-03 ("team awareness of Kings")

`EMILY/BACKLOG.md` S189-03: "also my team is unaware of the 4 kings" — S188-01's rendering fix
made a live King visible to whoever is already looking at it, but nothing tells the rest of the
team it exists at all. That card's own note left the mechanism undecided pending "a founder
call" on whether this is the ping system's job. **Founder call, made here**: yes — reuse this
system's own wire event and presentation grammar rather than inventing a third alert mechanism,
but as a clearly distinct, **system-generated** category, not a 7th player-pingable type:

- **Trigger**: the exact real moment `arena_tick_kings` (`packages/simulation/arena_game.c`)
  sets `k->active = 1` — a King's first spawn (`ARENA_KING_SPAWN_DELAY_MS`, 1:00) or any later
  respawn (`ARENA_KING_RESPAWN_MS`) — is currently silent. The server broadcasts a real event at
  that instant, to both teams (a King is neutral, contestable by either side — unlike a player
  ping, this one is never team-scoped).
- **Shape**: the same `ArenaPingEvent` wire struct (§8), with `sender_owner` set to a reserved
  sentinel value meaning "the server itself," never a real hero slot — client rendering must be
  able to tell a system alert apart from a teammate's own call (a distinct icon/color and a real
  announcer-style audio cue, matching Dota's own "Roshan has spawned" precedent, not the
  teammate-ping sound). `target_kind`/`target_id` identify the specific camp; `world_x_mm`/`_z_mm`
  give its real position for the minimap marker once a minimap exists (`GFD-RENDER-NORTH`'s own
  "procedural minimap is the biggest visual gap" note — this alert's marker is real, buildable
  scaffolding for that work, not blocked on it: the same fixed, always-known camp positions
  (`arena_camp_position`) can drive a temporary on-screen directional indicator or HUD banner
  first, upgraded to a real minimap dot later without changing this event's own shape).
- **Not** a ping a player or bot can suppress, mute per-sender, or mistake for a tactical call —
  it has no sender to mute, and §5's spam-control budget doesn't apply to it (it fires at most
  once per real spawn, already rate-limited by the game's own real spawn timers).
- Does not replace S189-01's own separate damage-log ask, and doesn't require it either — a real,
  standalone win on its own.

## 5. Human interaction and presentation

### Input

- **Primary path:** hold the configured ping key, aim, release toward a radial segment. Release on
  the center emits the context-sensitive default: Attack on a visible enemy; Assist on an ally or
  allied objective; otherwise On My Way.
- **Fast path:** separate rebindable keys emit the four highest-frequency calls—On My Way, Assist,
  Attack, Danger—at the cursor/crosshair target without opening the wheel.
- **Safety:** pings are client-predicted for responsiveness but remain pending until accepted by
  the server. A rejected ping never appears to teammates.
- **Accessibility:** every icon has a distinct shape and label, sounds differ by cadence as well as
  pitch, and no meaning relies on red/green color distinction. Hold duration, wheel size, and
  sound volume are configurable.

### World, minimap, and HUD

- World markers are anchored to the ground or entity, face the local camera, and decay over
  **3.0 seconds**. A target ping follows its valid target; if the target dies, it leaves a short
  ground marker at the final location and expires normally.
- The minimap shows the same marker and direction cue for off-screen pings. It must not reveal a
  location outside the local player's legitimate vision.
- The kill/ability HUD is not a second ping feed. At most one concise banner (“ALLY: DANGER”) is
  shown at a time, and only for high-urgency calls.
- Audio is scoped: a player hears their own ping immediately; teammates hear an accepted ping once.
  Nearby urgent danger may be spatialized, but map-wide audio must remain readable in a 10v10 fight.

### Spam control

- A sender may emit at most **3 pings per 4 seconds** and **8 per 20 seconds**. The server is the
  authority for both windows.
- Equivalent pings from the same sender within **1.0 second** refresh the existing marker rather
  than create another marker or sound.
- A teammate's matching ping within **0.75 seconds** clusters at the same marker and increments a
  small agreement count; it does not stack audio.
- Each receiver may mute a teammate's pings for the current match. Muting removes their visual and
  audio events locally, but does not change server state or bot behavior.
- The client keeps only the most recent three visible markers per teammate and prioritizes Danger,
  Assist, and target-specific Attack over travel intent when the screen is crowded.

## 6. Authority, visibility, and information integrity

The arena server validates every ping before distributing it. The client never tells teammates
that a ping is valid merely because it drew a local cursor marker.

1. Authenticate the sender as an active, alive participant in the match and derive their team from
   server state—never from a client-provided team field.
2. Enforce enum, coordinate bounds, target identity, sender cooldown, and match-phase rules.
3. For a target ping, confirm that the target exists and is a legal relation for that type.
4. For an enemy-specific ping, require that the sender currently has legitimate visibility of that
   enemy. `Enemy Missing` is the exception: it uses the sender's server-recorded last-seen position
   and timestamp, never a fresh hidden position.
5. Replicate only to active players and bots on the sender's team; replay/spectator policy is
   applied separately and never leaks live fog-of-war information.

Pings do **not** create vision, update bot omniscience, or bypass fog of war. A recipient can see a
teammate's warning marker because team coordination is the point; they cannot infer an exact hidden
enemy state beyond the explicitly shared last-seen assertion and its age.

## 7. Bot contract: peers, not puppets

Existing arena bots already form deterministic squads and select separate node objectives. Pings
add an explicit, shared coordination channel above that behavior—not a replacement for it.

**Grounded against the real code, not just the abstraction** (`apps/arena_bot/src/main.c`): every
bot independently computes the same squad partition via `my_owner % squad_count`
(`hero_squad_count`), and `hero_squad_target_node` deterministically assigns each squad the
nearest still-unclaimed node in ascending squad-id order — this is exactly the mechanism a "one
speaker per squad" ping-emission rule should key off (e.g. the lowest owner-slot member of a
squad, by this same `my_owner % squad_count` partition, is the natural default nominee), and it's
also exactly why speaker election needs the jitter `BOT_HUMANNESS_NORTHSTAR.md` §7 adds — computed
with zero variance, every bot resolves the identical nominee on the identical tick, which is
correct for legality/dedup but reads as mechanical the moment a human is watching. The RL-trained
policy (`rl_policy_forward`/`team_rl_policy_forward`, NORTHSTAR §25.1) is an additive movement
nudge layered on top of this same deterministic heuristic bot, not a separate decision-maker — v0
ping emission/consumption belongs entirely to the heuristic layer; the RL policy does not need to
know pings exist yet.

**Determinism constraint, stated explicitly here because §9.4 depends on it**: any timing,
eagerness, or compliance variance layered onto bot ping behavior (nominate-speaker jitter,
reaction latency, imperfect compliance — see `BOT_HUMANNESS_NORTHSTAR.md`) must be derived from a
deterministic hash of match state (`server_tick`, `owner_slot`, a purpose tag), never from
`rand()`/wall-clock timing — the same convention `arena_game.c`'s item-curriculum stat blending
already established for reproducible-across-restarts jitter. A bot's ping timing is presentation,
not simulation state, but it still has to replay identically or the "replay determinism test" this
document's own §9.4 calls for doesn't hold once bots start pinging.

### Emission

A bot may emit a ping only when its own legal observation crosses a clear threshold, such as:

- it begins rotating to a squad target (`On My Way`);
- it is outnumbered or has low survivability near a contested objective (`Assist Me` or `Danger`);
- it sees a reachable, high-value enemy whose squad could plausibly follow (`Attack / Focus`);
- it loses direct perception of a recently observed enemy (`Enemy Missing`);
- its commander/squad logic prioritizes holding an allied node (`Hold / Defend`);
- **it decides to commit to a King (NORTHSTAR §22/Jungle Camps, `ArenaKing`)** — founder,
  real-time: "if a bot wants to take on a king they can send out a ping to group up before going
  in." A King is boss-scale (500 HP, ~2x a camp minion's damage — real risk to a lone hero), so a
  bot should call `Assist Me` at the King's own camp position *before* committing to the pull,
  the same real "call for backup on a big objective" convention every MOBA jungle already
  follows — not a new ping type, the existing vocabulary already covers it exactly. Applies
  symmetrically to a human player: nothing here is bot-only, this is just the bot decision rule
  for when to use the button a human already has.

Bot pings use the same server budget as humans, plus a conservative per-bot policy cooldown. A
squad should normally nominate one speaker (for example, the lowest stable owner slot currently
near the event) so ten bots do not independently report the same fact.

### Consumption

The bot's decision layer receives accepted teammate pings as bounded features: type, age,
distance, source relation/squad, target validity, and local agreement count. It may adjust utility
or choose a corroborating action only when:

- the action is legal under its own vision and pathing constraints;
- the signal is fresh (normally no older than 3 seconds);
- it does not abandon a higher-priority survival, objective, or explicit squad commitment without
  a defined override rule; and
- independent local evidence or multiple teammate signals support an expensive rotation.

For v0, bots should **not** treat a human ping as an order with guaranteed compliance. That prevents
one mistaken click from collapsing stable squad assignments and gives the eventual learned policy a
clear problem: estimate when a teammate's tactical assertion is worth following.
`BOT_HUMANNESS_NORTHSTAR.md` §6 gives this a concrete mechanism (a per-bot, deterministic
compliance-noise coefficient on the utility adjustment below) rather than leaving it purely
qualitative — worth building once Bot v0 (mechanical, unconditional-per-rule compliance) is
proven, not before.

### Response count scales with synergy decay — the real, original design intent (S189-02)

This is the founder's own original ping-system spec (EMILY/BACKLOG.md S189-02, predating this
document's own upstream Codex authoring, which had no visibility into it): "if i ping the whole
team shouldnt come over... especially if im winning im assuming my cohesion goes down only 1 or 2
teamers come to a ping instead of more... considering it when we are behind as a comeback
mechanism." Concretely: **how many teammates actually converge on a ping is itself part of the
existing synergy-decay comeback mechanic** (NORTHSTAR §25.3, `arena_state.synergy_tier[team]`,
already shipped) — not a new, parallel system.

Applies to bot consumption, not human free will (a human teammate can always choose to walk toward
a ping regardless; this governs the bot side of "how many respond"):

- The convergence-utility bonus a bot's own utility scoring assigns to "go toward this accepted
  Assist/Attack/Danger ping" is scaled by the *inverse* of the ping sender's team's current
  `synergy_tier` — tier 0 (full cohesion) gives the full bonus (most in-range, otherwise-idle bots
  cross the utility threshold to converge); tier `ARENA_SYNERGY_TIER_COUNT - 1` (fully decayed,
  the tier a winning team drifts toward) gives a heavily dampened bonus, naturally leaving only
  the one or two closest/highest-existing-utility bots crossing threshold — the real "1 or 2
  teamers" outcome the founder specified, produced by scaling an existing utility term, not a
  hard-coded responder cap.
- This is a direct, mechanical extension of an already-shipped system: no new state, no new
  server-side timer — `synergy_tier` already re-rolls every `ARENA_SYNERGY_ROLL_INTERVAL_MS` (8s),
  weighted toward higher (more decayed) tiers the further ahead that team is. A losing team stays
  near tier 0 more often, so more bots converge on its own pings — the comeback mechanic applies
  to ping-responsiveness the same way it already applies to move speed/CDR (§25.3's own real
  buff), without inventing a second lever.
- Bounded, not absolute: a bot's own higher-priority survival/objective/explicit-squad-commitment
  override (this section's own bullet list above) still applies first. Synergy decay changes how
  *tempting* a ping is, never whether an already-critical bot abandons a fight to go sightsee a
  low-priority call.

## 8. Network and simulation shape

Use a small, versioned event rather than encoding pings as client UI state:

```c
typedef enum ArenaPingType {
    ARENA_PING_ON_MY_WAY,
    ARENA_PING_ASSIST,
    ARENA_PING_ATTACK,
    ARENA_PING_DANGER,
    ARENA_PING_ENEMY_MISSING,
    ARENA_PING_DEFEND
} ArenaPingType;

typedef struct ArenaPingEvent {
    uint32_t sequence;       /* server-assigned, monotonic per match */
    uint32_t server_tick;
    uint8_t sender_owner;
    uint8_t team;
    uint8_t type;
    int16_t target_kind;     /* ground, hero, node, camp, structure */
    int16_t target_id;       /* -1 for ground */
    int32_t world_x_mm;
    int32_t world_z_mm;
    uint16_t ttl_ms;
} ArenaPingEvent;
```

The actual wire representation belongs in `packages/common/protocol.h`, with client requests and
server-accepted events kept distinct. The server assigns the sequence and tick; clients may include
a local request nonce only to reconcile their own prediction. Snapshots should carry currently live
events or an ordered ping-event stream with loss recovery, so a late-joining spectator/replay can
reconstruct markers without inventing state.

Ping creation is a simulation-adjacent event: it is authoritative, timestamped, replayable, and
team-scoped, but it must not alter combat state. Keep validation in the server/network boundary and
give bots a sanitized accepted-event view, rather than letting renderer input mutate `ArenaGame`.

## 9. Delivery plan

1. **Protocol and authority:** define request/event types, server validation, recipient filtering,
   deterministic rate-limit tests, and replay serialization. No UI until forged target/team/hidden
   information requests are proven rejected.
2. **Human v0:** radial wheel, four fast keys, world marker, minimap marker, sound, accessibility
   labels, mute, and crowded-screen clustering. Exercise in both 3v3 and 10v10.
3. **Bot v0:** legal-observation emission rules, squad speaker election, accepted-event feature
   adapter, and conservative utility adjustments. Log decisions for inspection. Purely mechanical —
   fixed thresholds, zero timing variance, unconditional-per-rule compliance. **Bot v1** (see
   `BOT_HUMANNESS_NORTHSTAR.md`, sequenced after this step is proven) wraps it with deterministic
   reaction/emission latency, per-bot temperament, and imperfect compliance, flag-gated so Bot v0's
   exact mechanical behavior stays reproducible for training/evaluation.
4. **Evaluation:** scripted multi-client test, replay determinism test, packet-loss/reordering test,
   and a human playtest focused on time-to-comprehension and noise.
5. **Learned coordination:** only after the baseline is observable and stable, expose sanitized ping
   history to the team-RL environment and compare against the non-ping policy. Do not claim the
   system improves coordination until the evaluation shows it.

## 10. Acceptance criteria

- A human can issue every v0 type with a radial input and the four frequent types with one key.
- An accepted ping is visible only to the sender's team, is target-aware, appears in world and
  minimap views, plays no duplicate sound, and expires predictably.
- Forged enemy visibility, target, team, sequence, coordinate, and rate-limit requests are rejected
  server-side and covered by automated tests.
- A bot uses only accepted, team-visible events and emits no more than one equivalent squad call per
  event window under normal conditions.
- Existing squad/node behavior continues to work with ping consumption disabled; a ping must be an
  additive coordination layer, not a hidden dependency.
- Replays preserve accepted pings in order without exposing live fog-of-war data to an unauthorized
  viewer.
- A 10v10 stress run remains legible: no marker pile-up, no unbounded event queue, and no audible
  ping storm.

## 11. Open decisions before implementation

1. **Fog model:** team-shared vision is deferred elsewhere; confirm whether a teammate's enemy ping
   is intentionally visible even when the recipient lacks local vision, and specify the last-seen
   age display. This document assumes yes for coordination, without revealing live tracking.
2. **Controller/touch mapping:** define an equally fast radial gesture that does not interfere with
   camera or ability targeting before mobile support is claimed.
3. **Spectators:** decide delayed full-information spectator behavior versus same-team/restricted
   views; do not accidentally make a live spectator a scouting channel.
4. **Social safety:** decide whether match-level mute is sufficient for v0 and what persistent
   reporting/blocking policy belongs to account infrastructure later.
5. **Training ownership:** determine whether pings enter learned bot observations as raw events,
   compressed intent features, or a learned communication channel. Start with the deterministic,
   auditable feature adapter in §7.
6. **RL-policy ("vector brain") ping awareness:** the founder's own original spec explicitly
   flagged this as unresolved ("both heuristically and in the vector brain... i dunno"), not a
   firm requirement — this document commits only to the deterministic heuristic bot (§7's
   Emission/Consumption, and the synergy-decay response-count mechanic) for v0. Whether/how the
   trained RL policy (`rl_policy_forward`/`team_rl_policy_forward`, NORTHSTAR §21/§25) should ever
   observe pings is a real, separate, later decision — don't build it blind.

## 12. Measure the promise

Instrument accepted/rejected/muted pings, type, target kind, duplicate clustering, recipient count,
bot follow/decline outcome, and match phase. Aggregate measures should include ping rate by team
size, time from ping to first allied response, agreement rate, stale-event rate, and mute rate.

Metrics are diagnostic, not a mandate to maximize ping volume. The desired outcome is **fewer,
clearer signals that improve squad decisions**. If a change increases pings but not comprehension,
response quality, or match health, it failed the north star.
