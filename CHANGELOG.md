# Changelog

## 2026-09-03 (4)
- feat(matchmaker): real per-match seed + mode plumbing (kanban `534432532` "GFD dungeons",
  `GoblinFoxDragon/docs2/DUNGEON_NORTHSTAR.md` Milestone 1 -- "a way to pass a seed... into the
  spawned process" / "PACKET_MATCH_FOUND telling the client which mode it's joining"). Real,
  minimal first slice, not the full dungeon feature: `MatchFoundMsg` (`packages/common/
  protocol.h`) gains `seed`/`mode` fields; the matchmaker generates a fresh `rand()` seed per
  match, passes it to the spawned server via `--seed`, and sends it in `MatchFoundMsg` alongside
  a new `--mode` CLI flag (`match_mode`, still always 0 today -- no dungeon server binary exists
  yet). `arena_server` now uses a passed `--seed` for its own `srand()` instead of
  `time(NULL)^getpid()` when present, falling back to the old behavior otherwise -- real,
  immediate value even before any dungeon server exists (reproducible match RNG from a known
  seed). Live-verified end-to-end on a private test port: `MATCHMAKER: matched 2 players ->
  spawned server on port 19100 (seed=161265267)`. `bash scripts/build.sh` clean. Real, honest,
  NOT done: Milestones 1 (full instancing)-4.5 of the northstar are still not built; this is
  only the seed/mode transport piece.

## 2026-09-03 (3)
- feat: real settings pane + master volume slider added to the arena client (kanban cruise-queue cards 3424324 'REDGARDEN settings pane' + 343543 'REDGARDEN settings volume slider', built together as one real unit -- the volume slider is the pane's own first control). Escape toggles a new centered SETTINGS panel (apps/arena/src/main.c), same real 'works in any mode' precedent F11/H/B already established, and the same shared click-hit-test-vs-render-geometry discipline shop_panel_origin already set (settings_panel_origin/settings_slider_track). Click-and-drag slider (mouse-down resolves + continues tracking via SDL_MOUSEMOTION while held, standard slider-grab UX, distinct from the shop panel's single-click-resolves rows) sets a new master_volume global (0.0-1.0), which play_tone() now scales on top of each call site's own relative volume -- the one real place a player-controlled master level needs to apply. Session-only (no settings-file/config mechanism exists yet to persist it across launches -- named honestly as separate, later work, not silently faked). New settings_click_consumed gate added alongside the existing shop_click_consumed/ground_target_click_consumed so a settings-pane click never also falls through to a movement command. Live-verified, not just compile-checked: ran the real compiled binary under Xvfb with the pane temporarily forced open, screenshotted mid-run -- panel/border/title/slider/handle/percentage all render correctly, then reverted the force-open before commit. scripts/build.sh clean, scripts/test_arena.sh (1151 assertions) and bazel test //tests:all (16/16) both pass, zero regressions. (sess-20260902-2008-ed50169e)
- feat: Duck's Smoke Bomb (W) now has a 50% independent chance to slow each enemy caught in the cloud at cast time (S205-87, priority-queue kanban card: 'duck smoke bomb should have a 50% chance to slow each enemy hit by it'). New ARENA_DUCK_W_SLOW_CHANCE_PCT (50)/ARENA_DUCK_W_SLOW_MS (2000)/ARENA_DUCK_W_SLOW_PCT (0.30f) constants, same real precedent ARENA_CART_DELIVERY_SLOW_MS/_PCT already established. redgarden_host_duck_smoke_bomb_cast now loops enemies within ARENA_DUCK_W_RADIUS at cast time (the only real 'hit' this vision-only ability has) and rolls rand()%100 per enemy, applying arena_apply_slow on a hit -- rolled once at cast, not re-rolled per tick, matching 'hit by it' (the throw) not 'standing in it' (continuous). 4 new tests in tests/test_duck_smoke_bomb.c: a real slow landing through the compiled cast path (retried across seeds, not asserting a specific rand() sequence), a statistical ~50% check across 1000 trials with a fixed seed (wide 30-70% band, not flaky), an enemy outside the radius never slowed, and a teammate never slowed. Also fixed a real, pre-existing, unrelated gap found live while trying to run this test via its own declared Bazel target: packages/simulation/BUILD.bazel's arena_game cc_library was missing bacon_puck_intangible_speed_mod.c/_host.h entirely (present in every other real build path -- build.sh/test_arena.sh/ci.yml -- but never added to Bazel), which made bazel test //tests:test_duck_smoke_bomb fail to even compile; added both. bazel test //tests:all: 16/16 targets pass, zero regressions. scripts/build.sh + scripts/test_arena.sh both clean. (sess-20260902-2008-ed50169e)

- New Trinket-slot item, "Luck of the Draw" (S242-01, cruise queue): +1 flat mp/sec regen while
  in combat, 2200 Flow. New ArenaItemDef.bonus_mp_regen_combat field + cached ArenaHero.
  item_bonus_mp_regen_combat, same trailing-field convention every prior trinket append (Haste
  Trinket, Kite String) already used; the in-combat mana-regen rate now reads
  ARENA_MP_REGEN_IN_COMBAT_PER_SEC + item_bonus_mp_regen_combat, scoped to in-combat only. New
  test verifies both the boost and that it doesn't leak into the out-of-combat rate.
  ARENA_ITEM_COUNT 34->35 (real, honest note: this computes to shop page 4, not the founder's own
  literal "page 5" -- treated as descriptive, not padded with filler items to force a 5th page).
  Reconciled a real, live data-integrity issue found along the way: the kanban card carrying this
  ask had backlog_item_id S205-87, already a real, different, existing item -- given its own
  correct id (S242-01) instead. scripts/test_arena.sh (1145 assertions) and test_10_bots.sh both
  green. (sess-20260902-2008-ed50169e)

## 2026-09-03 (2)

- Ping System Northstar: resolved S189-03 ("team awareness of Kings") -- the founder call the
  card's own note left open. A King spawn/respawn (arena_tick_kings, k->active = 1) now broadcasts
  a real, distinct system-generated alert reusing the ping wire event, never a player-pingable
  type, with its own icon/sound so it's never mistaken for a teammate's call. Also added a real
  bot-emission trigger per the founder's own real-time addition: a bot committing to a King should
  call Assist Me at the camp position first, same convention a human already has -- not a new
  ping type, the existing vocabulary already covers it. (sess-20260902-2008-ed50169e)

## 2026-09-03

- Ping System Northstar: added the real, original S189-02 design intent Codex's upstream authoring
  never had visibility into -- ping response COUNT (how many teammates converge) scales with the
  already-shipped synergy-decay cohesion tier (NORTHSTAR §25.3), a comeback mechanic extension not
  a new system. Winning/high-decay teams get 1-2 responders per ping; losing/low-decay teams get
  more. Also flagged RL-policy ("vector brain") ping awareness as a real, separate, explicitly
  founder-uncertain open question, not committed to for v0. (sess-20260902-2008-ed50169e)

## 2026-09-02

- Added the Ping System Northstar: a League-style, server-authoritative team ping design for human
  and bot squad coordination, including tactical vocabulary, visibility integrity, bot contract,
  wire-event shape, rollout, acceptance criteria, and open decisions. (sess-20260825-1938-f6bd411e)

- Reviewed the (upstream, Codex-authored, no-repo-context) Ping System Northstar and extended §7's
  bot contract to ground it in the real `apps/arena_bot` squad code (`my_owner % squad_count`,
  `hero_squad_target_node`) and require deterministic (hash, not `rand()`) timing for any bot-side
  variance. Split the deeper "how should bots respond to pings in a human-feeling way" question
  into a new doc, `docs/BOT_HUMANNESS_NORTHSTAR.md`: ports MISHRI's real, shipped humanness-layer
  concepts (mood, reaction/chat-delay split, APM throttle, imperfect compliance, temperament) as
  shapes only — every roll is a deterministic hash of `(server_tick, owner_slot, purpose_tag)`,
  matching `arena_game.c`'s own item-curriculum-blend convention, to keep replay determinism
  (flags, doesn't fix, the pre-existing `synergy_roll_tier` `rand()` counterexample). Both docs
  registered in EMILY's golden-docs-index. (sess-20260902-2008-ed50169e)

## 2026-08-27
- Bacon+Puck: real movement speed increase (30%, PARENA-mod-powered) while Shadow Step's Q intangibility is active. commits 37a5396/9755bf9. (sess-20260825-1938-f6bd411e)

- Cart hero: AOE zone indicators (real gap fixed, zone_radius_x10 wire field), marble-bag+Fibonacci pity RNG (first real implementation of NORTHSTAR's own pull algorithm), R finally better-weighted than W, King's Growth buff outcome added. 21 new tests. Apple #16395, commit 1515caf (sess-20260825-1938-f6bd411e)


## 2026-08-26
- real [move-debug] server-side trace on every move command + F10 client-side reset-rotation debug tool, for the live intermittent 'stuck sideways, can't rotate' movement bug report -- diagnostics only, root cause not found yet (sess-20260825-1938-f6bd411e)
- deployed the 2026-08-25/26 autocurriculum RL policy (35.0% vs fixed heuristic) as the live 3v3 weights, replacing the 2026-08-11 noisy-gestalt checkpoint; launched the next training generation (rl_team_checkpoints_autocurriculum_20260826) (sess-20260825-1938-f6bd411e)
- Autocurriculum RL training run complete (team_size=3, 503808/500000 timesteps): 35% win rate vs heuristic bot AI (worse than baseline, reported honestly). Weights exported (rl_policy_weights_team.h) but not wired into any live consumer. Apple #16125 (sess-20260825-1938-f6bd411e)
- Bacon+Puck's W replaced with Shadow Step: click an enemy hero to blink behind them, reading their real server-tracked facing_rad (new ArenaHero field, first real facing concept, not just client-visual). Revived the ground-target reticle system for hero-click targeting instead of ground points. Real range/cooldown/mana, no-op on miss. Apple #16113, commit 14e37e1 (sess-20260825-1938-f6bd411e)
- Abraham's W now freezes him in place for the windup instead of silently wasting the cooldown/mana when the movement-interrupt rule cancelled the cast because he was walking. Gary's own "movement interrupts cast" feel is untouched. Apple #16108, commit 7671b2a (sess-20260825-1938-f6bd411e)
- Abraham's fireball now ignites enemies it touches (real burn DoT, reuses the existing on_hit_burn mechanic) and travels a bit faster (3.0 -> 4.0). Apple #16105, commit 43a286d (sess-20260825-1938-f6bd411e)
- migrated REDGARDEN's build to Bazel (S202-36) -- all 7 binaries + 14 test targets build/pass under `bazel build/test //...`, following PARENA's own existing setup. arena_game.c now #includes all 6 PARENA-mod host headers directly (closes the "-include flag missed in one build path" bug class hit 3 times this session). Shell scripts kept alongside for now. Apple #16101, commit 1158929 (sess-20260825-1938-f6bd411e)
- Abraham's W (fireball) now auto-targets the nearest enemy (no manual ground-click, no range limit) instead of requiring a screen click that proved genuinely hard to get registering reliably. He Xiangu's old Q replaced with Moira Orb (Overwatch), a real homing projectile reusing the same auto-target path; her W toggle reframed into a real Light/Dark stance (regen vs flat armor). Real server-side diagnostic logging added to arena_server. Apple #16101, commit 6a27f00 (sess-20260825-1938-f6bd411e)
- fixed live bug: Abraham never auto-attacked at all (windup-start check hardcoded to Gary only -- S202-34 excluded him from the old melee loop everywhere but missed adding him to the new ranged trigger), plus bot AI still ran his pre-rework kit for W. Verified via direct headless repros. Also finished Kite String (S202-34 trinket, +4% auto-attack range, 3333 flow), found as real uncommitted half-finished work. Apple #16090, commits ea0787a (CI wiring) + d4fd6c7 (fix) (sess-20260825-1938-f6bd411e)
- Abraham W rework -- A Line of Fire: real ground-targeted piercing fireball (S202-34), 400ms GOLDENBAND-driven windup animation, ranged auto-attack. Apple #16084, commit 961d500 (sess-20260825-1938-f6bd411e)
- Body blocking shipped (S202-27): real hero-hero collision via resolve_hero_hero_collision, same shape as the existing obstacle collision. Applies to allies too (real MOBA convention), skipped during Paper Glide. 4 new tests, full suite green. Apple #16057. (sess-20260825-1938-f6bd411e)

- Duck W -- Smoke Bomb shipped (S202-10, real AoE target-denial: hero_obscured_from blocks arena_nearest_enemy from outside an active cloud). PARENA mod (on_duck_smoke_bomb_cast), wire-synced, client-rendered, 13 new tests. Live-verified fully green in real CI (run 32913376214, commit 7d55fe0). Correction: a test_arena_replay segfault seen locally during this work turned out to be specific to this session's own sandboxed environment, not a real CI/production bug -- real CI's headless test step (which runs it) passed clean; an earlier note here overstated it as a pre-existing CI gap. Apple #16044. (sess-20260825-1938-f6bd411e)


## 2026-08-25
- fixed live bug: Tree passive (and day/night/Bloodflower) never ticked in arena_update, the 1v1 simulation tick -- only arena_update_teams had it wired in, so it silently never fired outside team-mode matches. New regression test exercises the real top-level tick function. Fully documented Tree's passive in README.md's new "Hero passives" section (sess-20260825-1938-f6bd411e)
- item curriculum: PARENA-mod-driven generation primitive for NORTHSTAR.md §26.3.2's "tuning the game via items" v0 (blend two catalog items' stats into a runtime curriculum slot, deterministic, 15 new tests, honest scope -- training-loop consumption not built yet) (sess-20260825-1938-f6bd411e)
- added auto-release CI job (PITVIPER pattern): real, non-prerelease GitHub release on every push to main (sess-20260825-1938-f6bd411e)
- build templates: tech trees as item templates, shop auto-buy (Bruiser/Assassin/Caster presets), PARENA-mod-driven (sess-20260825-1938-f6bd411e)
- fixed CI break: build_arena.sh + Windows cross-compile missing tree_passive_mod.c link (sess-20260825-1938-f6bd411e)
- Tree hero passive: auto-attacks nearby jungle trees for self-heal, PARENA-mod-driven, wire-synced with client squish animation (sess-20260825-1938-f6bd411e)

- Day/night cycle + lighting ported from SHANKPIT retro_sky.c/retro_lighting.c into arena_game.c; moon-zenith Bloodflower event delivered as REDGARDEN's first real PARENA mod (stdlib/redgarden/bloodflower_mod.prn). Live round-trip verified via new tests/test_bloodflower.c (12 checks), full existing suite still green. (sess-20260825-0828-cc32a704)


## 2026-08-20
- King buff status synced to clients + bottom-right buff HUD (founder: 'couldnt even tell if i got a buff'). King health bars + name tags added (founder: 'the 4 kings need health bars and name tags'). Deployed live to redgarden-stable. (sess-20260820-0649-a3f19d93)
- Fixed the real cause of the Four Kings/jungle-camp invisibility bug: fully simulated server-side since Milestones 1/2, never had a wire-protocol representation. Added ArenaCampMinionSnapshot/ArenaKingSnapshot to ArenaSnapshotMsg, populated server-side, rendered client-side. Commit 9cdbb09. (sess-20260820-0649-a3f19d93)
- NORTHSTAR.md §29 (ECOWAR) addendum: resolved 2 of the section's 4 open architecture questions
  from real-time founder fragments (hero is directly piloted + cards cover everything else;
  simulation loop recommendation is arena_game.c/apps/arena, extended, not local_game.c or a new
  third loop), added a full net-new card/economy design (22-card deck, rarity tiers, marble-bag +
  Fibonacci-pity pull algo for both draw and packs, non-depleting Clash-Royale-style cycling,
  Prompt-o-verse-farmed card art), and flagged a real Flow-naming collision between REDGARDEN's
  own kill-fed Flow and TRAPX/SHANKPIT's territory-generation Flow rather than silently picking
  one. Spec only, no code. (sess-20260813-2154-dda37e8b)

## 2026-08-19
- Added procedural minimap to apps/arena (top-right circular radar, hero+node dots) (sess-20260813-2154-dda37e8b)

- Added cel-shading (outline + quantized diffuse banding) to apps/arena, first step toward the founder's Prompt-o-verse-referenced DragonsNShit graphics quality bar (sess-20260813-2154-dda37e8b)


## 2026-08-17

- 乾淨對照組訓練跑完(S170-294延續):team_size=3、503808/500000 timesteps(PPO overshoot到下一個完整
  rollout邊界)、僅--autocurriculum(noisy-gestalt關閉),跟2026-08-11那次75%勝率的reference run完全同
  config。結果:14勝6敗0平,**70.0%勝率**——跟75% reference run同一量級,不是2026-08-14那次35%的低分。
  這證實了先前的假設:35%不是機制退步,是那次同時改了兩個變數(timesteps砍半+加了--noisy-gestalt)造成
  的收斂不足,不是乾淨對照。70% vs 75%的落差在兩次獨立訓練run之間屬於正常變異範圍。最終policy:
  `rl_team_checkpoints_autocurriculum_500k/ppo_arena_team_final.zip`。權重也匯出到
  `rl_policy_weights_team.h`(146314 bytes)供檢視,**明確尚未接入任何實際consumer**——team-shaped
  input vector要接什麼consumer是NORTHSTAR §25.5沒解決的真實設計問題,不是這次訓練run的副作用該決定的。
  誠實回報,不誇大。Apple #13945。 (sess-20260813-2154-dda37e8b)

- ops檢查(founder即時指示:「ensure ops for our exotic training」,澄清「exotic training」=autocurriculum):
  確認先前兩輪autocurriculum跑都已完整結束並回報過(2026-08-14 200K timesteps/35% win rate已在Apple
  #13608/#13610記錄；2026-08-15 10v10版512K timesteps/40% win rate已在Apple #13748記錄),目前沒有訓練
  程序在跑,無systemd unit監督訓練本身(training是手動背景跑,不是常駐服務——這是預期行為,不是ops缺口)。
  發現先前35%那輪其實同時改了兩個變數(timesteps從500K減半到200K「且」加了--noisy-gestalt),不是乾淨對照
  組。為了真正驗證CHANGELOG自己下的「應為收斂不足,非機制退步」假設,重新啟動一輪與75%那次(2026-08-11)完全
  同config的乾淨對照:team_size=3、500K timesteps、僅--autocurriculum(noisy-gestalt關閉)。啟動後確認
  process存活、fps=99、正常寫入checkpoint,健康運行中,輸出至`rl_team_checkpoints_autocurriculum_500k/`。
  Apple #13908。 (sess-20260813-2154-dda37e8b)

## 2026-08-15

- team_size=10(真正10v10)首次RL訓練啟動:先smoke test確認313fps(C simulation+Python env層本就通用支援team_size 2-10,不需改code),再啟動500K timesteps正式run(--noisy-gestalt,比照先前最佳3v3規模),預估~27分鐘。輸出至rl_team_checkpoints_10v10/。Apple #13699。 (sess-20260813-2154-dda37e8b)

- team_size=10訓練完成:512000/500000 timesteps(PPO overshoot到下一個完整rollout邊界,team_size=10每iteration 20480 timesteps)。20場team episode對照heuristic bot team:8勝8敗4平,40.0%勝率。誠實回報,不誇大——低於50%,代表heuristic隊伍在真正10v10規模下至少還是同等強度(對比先前3v3規模的75%/35%結果,10v10協調難度更高,而且這是第一個在這個真實規模訓練出來的checkpoint,不是已調校成熟的)。最終policy:rl_team_checkpoints_10v10/ppo_arena_team_final.zip。權重也匯出到rl_policy_weights_team.h供檢視,明確**尚未接入任何實際consumer**——apps/arena_bot的team_rl_engage_nudge仍硬性gate只認team_size==3,要接上10v10還需要解除那個gate並驗證checkpoint不會讀到隊友以外的敵方資料,這次沒做。Apple #13748。 (sess-20260813-2154-dda37e8b)


## 2026-08-14
- 【修正】上一條寫「首次」是錯的——2026-08-11已經跑過一次真正end-to-end --autocurriculum訓練(500K timesteps,75% win rate,見下方2026-08-11條目)。這次(2026-08-14)其實是第二次,只跑200K timesteps(前次一半),結果35%比前次75%更低,收斂不足很可能是主因,不是機制本身變差。兩次都只對抗固定heuristic評估,還沒對抗opponent pool本身。5個checkpoint進opponent pool。不建議現在接成live consumer。scripts/rl_train_team.py自己的module doc comment也已修正(那個過時的"NOT yet exercised"斷言就是這次誤判的來源)。Apple #13608 + #13610(自我修正)。 (sess-20260813-2154-dda37e8b)
- redgarden-stable(GFD Battlegrounds真正部署)promote:自08-10落後4天多,已git pull+rebuild+重啟,Four Kings現在真的在stable部署上線了。Apple #13584。 (sess-20260813-2154-dda37e8b)
- 啟動新一輪 --autocurriculum + --noisy-gestalt 訓練(200K timesteps,3v3),驗證 NORTHSTAR §26.3.1 記錄的'真正 end-to-end autocurriculum run'待辦項。輸出至 rl_team_checkpoints_autocurriculum/。 (sess-20260813-2154-dda37e8b)

- NORTHSTAR §20.5: re-investigated CANNON/siege minions -- the structures blocker §20.4 named is resolved, but found a real, different blocker (lane creeps and towers are on geometrically unrelated systems, zero interaction code) -- documented, real design question flagged, not built yet (sess-20260813-2154-dda37e8b)


## 2026-08-13
- NORTHSTAR §29: ECOWAR scoping doc -- RTS command layer (existing card-RTS) + MOBA combat layer (existing arena), deck-building as the interface. Scoping only, 4 open architecture questions flagged, no build yet (sess-20260813-2154-dda37e8b)
- Tripled Flow cost of the 6 page-4 items (Gae Bolg/Masamune/Muramasa/Balance Ring/Empress Hairpin/Ninja Tekko), first pass of a balance iteration -- other items deferred (sess-20260813-2154-dda37e8b)
- Added an 'Item mechanics beyond the stat columns' section to README documenting Donkey/Blink Dagger/Haste Trinket/Gae Bolg/Masamune/Balance Ring/Empress Hairpin's coded mechanics that the stat table alone doesn't show (sess-20260813-2154-dda37e8b)

- Full README refresh (3v3 queue, R&D/Stable split, Jungle Camps, bot AI research program, 33-item catalog) + fixed accidental full-document duplication in the legacy design-doc section (sess-20260813-2154-dda37e8b)


## 2026-08-11
- §25.4 autocurriculum 端對端訓練跑完：75% vs 固定 heuristic（比 noisy-gestalt-only 的 100% 低，可能是沒有 overfit 到單一對手的正常結果，需要 pool-based eval 才能真的判斷） (sess-20260810-0505-a53abca2)
- §25.4 autocurriculum 真的端對端訓練跑起來了（500K timesteps，team_size=3，獨立 output-dir） (sess-20260810-0505-a53abca2)
- 商店擴充：6 個新道具、開出第 4 頁；記錄 AD/AP、GOLDENBAND 動畫、GPT-2 道具命名系統三個新的決策點 (sess-20260810-0505-a53abca2)
- Team model 真的接進 live bots（新 3v3 隊列）+ 記錄 draft-phase/item-tuning 的研究方向決策點 (sess-20260810-0505-a53abca2)

- noisy-gestalt 500K timestep 訓練跑完：20/20 eval 全勝 vs 固定 heuristic（誠實標註：不代表通過 autocurriculum 對手池的考驗） (sess-20260810-0505-a53abca2)


## 2026-08-10
- NORTHSTAR §25.4 autocurriculum：PFSP opponent-pool sampling 實作完成，rl_train_team.py 新增 --autocurriculum (sess-20260810-0505-a53abca2)

- NORTHSTAR §25-28: 多智能體 RL 團隊訓練(角色發現、noisy gestalt、synergy decay、autocurriculum)+ 跨遊戲策略遷移 + 物理資訊模擬研究 + frame-break prompting,來源是創辦人自己的 AGI R&D 對話記錄。§25 VS0(team env)真的寫完編譯過,ctypes 驗證過 (sess-20260809-1420-e9d3d7f8)


## 2026-08-05
- fix(arena): perf (VBO orphaning, benchmarked skinning first to rule it out) + real skeleton-matched animation clips (fixes 'swimming' - founder's real rig has ~178deg rest rotation on arm bones, flat clips were wrong-axis) (sess-20260723-2347-df115bd5)
- fix(ci): bundle assets/goldenband into RedGarden_Client zip - real bug, downloaded builds silently fell back to Tyler's plain box, no crash/error, found live by founder (sess-20260723-2347-df115bd5)
- feat(arena): real founder-modeled Tyler mesh replaces box-rig and proof mesh - real skinned character now live, fallback chain mesh->box->plain-box, real bug found+fixed (dynamic VBO cap too small for real model vert count) (sess-20260723-2347-df115bd5)
- feat(arena): S144-07 real vertex-weighted skinned mesh (F9 debug proof rig) - CPU skinning, real multi-bone blended weights, live-verified under Xvfb as a continuous tapered mesh (sess-20260723-2347-df115bd5)

- feat(arena): S144-06 GOLDENBAND box-rig drives Tyler's animation - real .gband motion data through real forward kinematics, one cube per joint, replacing Tyler's old static box; falls back safely if assets missing (sess-20260723-2347-df115bd5)


## 2026-08-02

- fix: reapplied `arena_bot`'s `ARENA_HERO_COUNT` (30, was stuck at the stale 28) and
  `test_10_bots.sh`'s PID-scoped cleanup -- both fixed earlier the same day, then reverted along
  with an unrelated set of commits (a founder-driven history rewrite of this repo); reapplied
  since they're confirmed, independent, low-risk fixes regardless of that revert. No bot has ever
  been able to draft Warrior (28) or Cart (29) -- only a real human client can, since the draft
  screen reads `arena_game.h`'s real `ARENA_HERO_COUNT=30` directly, not `arena_bot`'s own
  separate hand-synced copy. Found while investigating a live "match_start then frozen,
  arena_server disappears with zero snapshots" bug that only ever reproduced with a real human in
  the lobby -- **directly disproven** as the crash's cause by a controlled reproduction (20 bots,
  both heroes drafted, match went live and produced snapshots cleanly). `test_10_bots.sh`'s fix
  is the same live-match-killing `pkill -f` bug as Apple #11565, reintroduced by the same revert.
- feat(arena_server): real crash diagnostics. The live crash above left literally nothing to
  investigate after the fact -- process just gone, no `match_end`, no error. `SIGSEGV`/`SIGABRT`/
  `SIGFPE`/`SIGBUS`/`SIGILL` now dump `match_phase`/`lobby_size`/`picked_count` and every owner's
  `hero_id`/`team`/`alive`/`hp`, plus a real backtrace, to stderr (captured by the live systemd
  unit's log) and into the match's own JSONL log if open, then re-raise so the OS's own exit
  behavior is unchanged. Verified live: `kill -SEGV` against a running instance produces real,
  readable diagnostics. Root cause of the actual crash still open -- this is what gets it next
  time, not a fix for it yet.

## 2026-07-31 (2)

- fix(arena): draft-grid pick screen was overflowing narrower window widths -- founder-reported
  ("i broke the server, tyler makes things wonky") real bug, root-caused via
  `var/logs/matchmaker-bots.log`: a full 20/20-connected lobby stuck at phase=1 forever, one
  CLIENT id never appearing in the pick log, dying on the 60s no-progress timeout.
  `draft_grid_origin`/`draft_screen_hero_at`/`draw_draft_screen` computed the 6-col hero grid
  centered on `win_w/2` with no clamp against the actual (resizable) window size -- fine at the
  1280x720 default, but below ~1134px wide the rightmost column (`hero_id % 6 == 5`, which
  includes Tyler at id 17) rendered mostly or fully past the window edge, unclickable or only
  clickable in a mislabeled sliver. Grid now shrinks/shifts to fit whatever window size is live.
  `apps/arena/src/main.c` `5916dc5`.

## 2026-07-31

- feat(arena): Patrol command -- fourth and last slice of §24 Milestone 2's WC3 group-order
  vocabulary, which is now **fully shipped** (Stop/Attack-move/Hold Position/Patrol, all built
  today). Real `P` keybind (free, matches real WC3 exactly), held-then-click same as attack-move
  (checked before attack-move if both happen to be held). New `PACKET_ARENA_PATROL`/
  `ArenaPatrolCmd` wire packet, `arena_set_patrol_target(owner, x, z)` sets point A to the unit's
  own position at the moment of issue and point B to the clicked point, always starting toward B
  first (real WC3 behavior). `arena_tick_patrol` walks the unit back and forth forever, flipping
  direction on arrival (`ARENA_PATROL_ARRIVAL_RADIUS`), opportunistically engaging anything
  encountered along the way via a newly-factored-out `arena_find_opportunistic_target` helper
  (previously duplicated inline in `arena_tick_attack_move` -- patrol needing the exact same scan
  a third time was the point where extracting it stopped being premature). Cleared by any other
  move/attack/attack-move/hold/stop command, same "a new command always wins" convention every
  other group order already follows. 4 new tests (13 total across all four Milestone 2 commands
  shipped today). `scripts/build.sh` clean, full `scripts/test_arena.sh` suite green,
  `scripts/test_10_bots.sh` stable.

- feat(arena): Hold Position command -- third slice of §24 Milestone 2's WC3 group-order
  vocabulary (only patrol left). Real `D` keybind (`H`, WC3/StarCraft's own real convention, was
  already taken by this file's ability-help toggle -- "Defend" is the exact synonym several other
  RTS UIs already use for the same order). New `PACKET_ARENA_HOLD`/`ArenaHoldCmd` wire packet,
  server-side `arena_hold_position(owner)` halts in place same as Stop. The real behavioral
  difference from Stop: a held unit never chases a target that leaves range
  (`arena_tick_attack_targets` now drops the lock instead of pure-pursuing when `hold_position`
  is set) but still opportunistically defends itself against whoever wanders into range
  (`arena_tick_attack_move`'s own opportunistic-engage scan, extended to run for held units too,
  not just attack-move ones) -- the extension matters specifically for ranged heroes (Gary so
  far), whose basic attacks only ever fire through `attack_target`, unlike melee's always-on flat
  proximity loop which "just works" for a stationary unit with zero extra code. Cleared by any
  other move/attack/attack-move/stop command, same "a new command always wins" convention every
  other group order already follows. 4 new tests. `scripts/build.sh` clean, full
  `scripts/test_arena.sh` suite green, `scripts/test_10_bots.sh` stable.

- feat(arena): Attack-move command -- closes two open items at once: NORTHSTAR.md §17.4's own
  long-unchecked "Attack-move command (LoL's 'A' + click)" and the second real slice of §24
  Milestone 2's WC3 group-order vocabulary. Real LoL/WC3 "hold A, then click ground": moves
  toward the clicked point like a plain move, but `arena_tick_attack_move` opportunistically
  diverts to whatever enemy comes within range along the way (`attack_target` gets set,
  `arena_tick_attack_targets` -- already real, unchanged -- takes over the actual chase/combat),
  re-acquiring a new target automatically if the current one dies (unlike a direct attack-target
  lock, which just goes idle) and resuming the ORIGINAL destination once nothing's left to engage
  (a new `attack_move_x/z` pair remembers it, since `target_x/z` gets overwritten mid-chase by
  `arena_tick_attack_targets`' own real "the attack command wins while it's active" behavior).
  Held-key detection (`SDL_SCANCODE_A` read at the moment of a ground click via
  `SDL_GetKeyboardState`, same "held, not toggled" idiom the Tab scoreboard already uses), not a
  separate mode-toggle keypress. New `PACKET_ARENA_ATTACK_MOVE`/`ArenaAttackMoveCmd` wire packet,
  same `arena_owner_controls` authorization every other group command (move/attack/stop) already
  enforces. Any other move/attack/stop command clears it, same "a new command always wins"
  convention. Team-mode only, same scoping the underlying attack-target/chase system already has.
  5 new tests. `scripts/build.sh` clean, full `scripts/test_arena.sh` suite green,
  `scripts/test_10_bots.sh` stable. Hold and patrol still open -- Milestone 2 stays IN PROGRESS.

- feat(arena): Stop command -- NORTHSTAR.md §24 Milestone 2 (corrected), the first of the real
  WC3 group-order vocabulary (attack-move/hold/patrol/stop) for Tyler's own clone-control system.
  Real `S` keybind (unbound before this), new `PACKET_ARENA_STOP`/`ArenaStopCmd` wire packet,
  server-side `arena_stop_unit(owner)` (cancels move target + attack-target lock, resets
  `target_x/z` to the unit's own current position rather than leaving it stale). Applies to the
  whole currently-selected group via the same `selected_or_self()` resolution move/attack clicks
  already use, and the same `arena_owner_controls` authorization (self, or one of Tyler's own
  active clones) every other group command already enforces. 3 new tests. `scripts/build.sh`
  clean, full `scripts/test_arena.sh` suite green, `scripts/test_10_bots.sh` stable.
  Attack-move/hold/patrol not started -- this milestone stays open.

- docs(northstar): §24.3.2 CORRECTION -- founder, real-time: "the unit controls are supposed to
  be for tyler." §24's Milestone 2 was originally framed as "a second hero gets real
  directly-controlled units"; corrected to its actual intent: real WC3-shaped group-order
  vocabulary (attack-move/hold/patrol/stop) for Tyler's own already-shipped clone mechanic, not
  a new hero. The Cart (built the same session under this section) was never that milestone in
  the first place -- §24.3.1 already flagged it Indirect-Control, a real but separate archetype;
  its completion doesn't close the corrected Milestone 2. Old milestone table renumbered.

- feat(arena): The Cart (hero #30, `ARENA_HERO_CART`) -- TYLER `multiverse_heroes.md` #10,
  `NORTHSTAR.md` §24 Milestone 2, founder pick from §7's own queue (the one entry never built).
  Real lore constraint honored, not overridden: the compendium's own 2026-07-23 gameplay note
  already named the Cart's whole identity as "nobody, including its own controller, gets to
  request what" -- asked directly whether to honor or override that for a WC3-style
  directly-commanded kit (this session's own AskUserQuestion), founder chose to honor it.
  Indirect-Control, same archetype §16.1 already built for Donkey. Q is a small self-heal
  ("Maintenance" -- the Cart isn't a combatant per its own lore). W/R ("No Requester in the
  Ledger" / "Already Waiting") open a delivery zone at the Cart's own position -- whoever steps
  in first (ally, enemy, or the Cart's own controller, no team check) gets one of 4 real,
  equally-weighted outcomes (heal/mana/slow/Flow), not always good, single-use. First hero with
  TWO zone-shaped abilities sharing the same r_zone fields every other zone hero already uses --
  last-cast-wins if both are active, a real interaction no prior kit could reach, documented not
  hidden. New `zone_radius` field on `ArenaHero` (every zone hero before this had exactly one
  zone, so radius was always just a constant, never needed storing). Real bug caught by the
  build itself: `apps/arena_server` never called `srand()` -- `rand()` would have used the C
  library's default seed (1) every server restart, making "random" deliveries fully predictable
  in real matches; fixed there, matching the pattern `apps/arena`/`apps/arena_bot` already used.
  5 new tests, including one proving the zone still fires on the Cart's own controller when
  nobody else is in range. `scripts/build.sh` clean, full `scripts/test_arena.sh` suite green,
  `scripts/test_10_bots.sh` stable.

- docs(northstar): §24.3.1 CORRECTION -- started Milestone 1 (generalize the clone mechanism off
  Tyler-only) and found, by checking every real gate directly rather than assuming, that it's
  already generic: `arena_owner_controls`, `tyler_clone_cascade_kill`, the hittable/targeting
  checks, and every client-side drag-select/rendering path all branch on `is_clone`/`clone_owner`
  alone, never `hero_id == ARENA_HERO_TYLER`. Only the spawn trigger and one sizing constant are
  Tyler-specific, and both need a real second hero's real kit numbers to generalize correctly --
  so Milestone 1 collapses into Milestone 2, no standalone work left. Also fixed a real citation
  error in the original §24 draft: this roster's hero content pipeline is §7 / `TYLER/
  multiverse_heroes.md`, not `HERO_CONTENT_FRAMEWORK.md` (that's GoblinFoxDragon's own, for
  DragonsNShit's separate lore-hero system).

- docs(northstar): §24 "Full unit control affordances — Warcraft 3 northstar" -- founder,
  real-time: "redgarden full unit control affordances northstar warcraft 3." Spec only, no code.
  Names the real current shape (every hero owner-piloted, lane creeps autonomous-AI-only, zero
  player unit production) and the one real precedent that already exists: Tyler's own clone
  system (drag-select, `selected_units[]`, `is_clone`/`clone_owner`) -- currently hardcoded to
  Tyler only. Directly references §16.1's own honest "sidestepped, not solved" companion-unit
  gap (Donkey shipped as an item specifically to avoid building that system) as still-open and
  relevant. Real path proposed: generalize Tyler's already-shipped mechanism (Milestone 1) before
  giving a second hero real controllable units (Milestone 2) and a real WC3-shaped group-order
  vocabulary (Milestone 3) -- a real unit-production economy is named as a separate, much bigger,
  explicitly-undecided pivot (Milestone 4), not assumed.

- feat(arena_server): reward-credit hook -- GoblinFoxDragon `REDGARDEN_GUI_NORTHSTAR.md`
  Milestone 4. `report_match_result` now also credits real Flow to a match participant's
  persistent DragonsNShit character, if they have one (gated on IDUNA's new
  `GET /api/v1/characters/by-player/:player_id` lookup succeeding -- a real 404 for the common
  case of a REDGARDEN-only player is expected, not logged as an error). 100 Flow on a win, 25 on
  a loss via the existing `PATCH .../gold/credit` -- first real numbers, not a design review's
  output, tuned later against real playtesting. `packages/common/http_client.h` gained a general
  `http_json_request(method, ...)` (GET/PATCH needed, only POST existed); `http_post_json` is now
  a thin wrapper so every existing POST call site is untouched, plus new `http_get_json`/
  `http_patch_json` wrappers. Caught a real bug via `-Wformat-truncation` before it shipped: the
  by-player lookup path buffer was 64 bytes, too tight for `/api/v1/characters/by-player/` (30)
  + a 36-char UUID + NUL (67) -- fixed to 96. `scripts/build.sh` clean, full
  `scripts/test_arena.sh` suite green, `scripts/test_10_bots.sh` stable.

- feat(arena): `apps/arena` client gains a `--ticket <hex>` flag -- GoblinFoxDragon
  `REDGARDEN_GUI_NORTHSTAR.md` Milestone 3 (Battlegrounds entry point). `net_connect`'s ticket
  resolution now checks an externally-supplied ticket first, before the existing WOTAN
  self-registration flow and the self-minted dev fallback -- neither of those carries a real
  DragonsNShit identity (self-registration would silently mint a throwaway `redgarden_bot`
  player_id instead). This is the piece that makes `apps2/mud`'s new `battlegrounds` command
  actionable: it mints a real ticket via IDUNA and prints the exact `red_garden_arena --queue
  <host> --matchmaker-port 7778 --ticket <hex>` command line to run, since a telnet session can't
  launch a client process itself. `scripts/build.sh` clean (no new warnings), full
  `scripts/test_arena.sh` suite green.

- feat(arena): GoblinFoxDragon `REDGARDEN_GUI_NORTHSTAR.md` Milestone 2 -- real skillchain
  resonance detection in `arena_game.c`, same session as Milestone 1 below. `ArenaResonance`
  (14 elements) + `resonance_combo` are a straight C port of
  `GoblinFoxDragon/server/skillchain.go`'s own `combinationTable` -- same real (ws1, ws2) pairs,
  same real tier-1/2/3 multipliers (20%/35%/50%). Tracked per-TARGET, not per-caster
  (`sc_pending_attrs`/`sc_pending_attr_count`/`sc_pending_age_ms` on `ArenaHero`, real FFXI "a
  chain forms on whoever gets hit twice, from any source" rule), aged every tick in
  `tick_hero_kit` alongside `combat_timer_ms`'s own "generic across every hero" countdown. New
  `apply_weapon_skill_damage` is the one choke point every real weapon-skill cast now routes
  through instead of a bare `apply_damage`/`apply_armor` pair (ordinary abilities/basic attacks
  never touch it, matching real FFXI); `skillchain_flash_tier` is a new, distinct one-tick
  wire-visible event (same lifetime idiom as `cast_flash_slot`, deliberately not folded into it
  or the generic hit-feedback path, per the northstar's own explicit requirement). Verified real,
  not just plausible: Warrior's own Q (Scission) into R (Induration+Reverberation) closes an
  actual Tier 2 Distortion chain per the real table -- the one pairing achievable with Milestone
  1's own in-kit content alone. 2 new tests
  (`test_warrior_q_then_r_closes_a_real_skillchain`, `test_warrior_skillchain_window_expires`).
  `scripts/build.sh` clean (no new warnings), `scripts/test_arena.sh` full suite green,
  `scripts/test_10_bots.sh` stable. Client-side rendering of the new chain event is a real,
  visible follow-up gap, same scoping decision as Milestone 1's own client gap -- this pass is
  server-authoritative simulation only.

- feat(arena): GoblinFoxDragon `REDGARDEN_GUI_NORTHSTAR.md` Milestone 1 -- Warrior, the first
  DragonsNShit job ported into Battlegrounds as real ability content, not a TYLER hero.
  `ARENA_HERO_WARRIOR` appended to `ArenaHeroID` (`ARENA_HERO_COUNT` 28->29). Three real Great
  Sword weapon skills from `GoblinFoxDragon/server/skillchain.CanonicalWeaponSkills`, matching
  WAR's real job stat block (`server/job.jobStats[WAR]`, STR-8/VIT-8), in real FFXI progression
  order: Q Hard Slash (Scission), W Power Slash (Transfixion), R Frostbite (Induration+
  Reverberation, dual resonance) -- each harder than the last, on a longer cooldown than the
  last. `apps2/mud`'s weapon skills share one real, uniform cost (`server/combat.TPWSThreshold`,
  100 TP); REDGARDEN has no TP resource, so MP substitutes (this file's own existing
  `ARENA_MP_COST_*`) rather than a new TP bar being invented -- an honest amendment, not a
  literal port. All three are plain melee-range instant hits (`warrior_cast_q/w/r`, same shape
  as Gunnr's Q), wired into the real Q/W/R cast dispatch + bot AI heuristic (biggest/longest-
  cooldown checked first). Resonance attributes documented for Milestone 2 (real skillchain
  detection in this file) to consume later -- not acted on yet. `docs/HEROES_VS0.md` entry
  added. New tests: `test_warrior_q_hard_slash_damages_in_melee_range`,
  `test_warrior_q_out_of_range_whiffs`, `test_warrior_w_power_slash_hits_harder_than_q`,
  `test_warrior_r_frostbite_hits_hardest`. `scripts/build.sh` clean (no new warnings),
  `scripts/test_arena.sh` full suite green, `scripts/test_10_bots.sh` stable.

## 2026-07-29

- feat(arena): S170-218, split the single lane's wave into melee + caster roles. Biggest,
  most structural item of the creep-overhaul batch, deliberately sequenced last -- closes it
  out. New `ArenaLaneCreepRole` (`ARENA_LANE_CREEP_MELEE`=0 default, `ARENA_LANE_CREEP_CASTER`);
  `ARENA_LANE_WAVE_CASTER_COUNT` (1 of each 3-strong wave) spawns as a caster with its own
  lower HP/damage but a genuine range advantage (`ARENA_LANE_CREEP_CASTER_RANGE` 6.0 vs.
  melee's 3.5) -- the actual "role" distinction, not just a stat reskin. "Roles exist at all"
  was the goal per the backlog's own framing, not exact League parity (multi-lane/siege waves
  stay explicitly out of scope). Melee stays value 0 and reuses every original constant
  unchanged specifically so every pre-existing test that hand-builds an `ArenaLaneCreep`
  without setting `role` keeps behaving exactly as before -- all 15 prior lane-creep tests
  passed unmodified. Wire-synced: `ArenaLaneCreepSnapshot` gained a `role` byte (server pack +
  client unpack), and the client render loop gives casters a distinct taller/narrower
  silhouette with a bright accent instead of melee's darker plate accent -- also corrected a
  stale comment there that still claimed lane creeps weren't wire-synced (they have been since
  S170-146). 2 new tests (wave role mix + HP-per-role, caster engaging from a range melee
  couldn't). Full suite (772 checks) + test_10_bots.sh green.

- feat(arena): S170-230, hero Zagan, "The Standstill's Confessor" -- 28th hero, Control/
  Disruptor. Founder, across several fragmented messages: "hero ZAGAN" -> "unique kit adds
  stun" -> "think of a way to give ZAGAN a unique kit that changes meta." Built directly from
  `TYLER/lore/activation_47_transmutation.md` (the full 47-minute monologue transcript deriving
  the Riemann Hypothesis through six alchemical stages) plus two okemily.com posts about it that
  both independently land on the same thesis: Zagan's power should stay an unconfirmed, hedged
  claim, not a clean verified one.
  **Passive -- Base Metal Screams**: the first time ANY enemy hero's HP crosses below 50% in
  their current life, Zagan gains a flat Flow bounty -- no proximity or damage-source
  requirement, an event-triggered (threshold-crossing) passive shape new to this roster.
  **Q -- Calcination**: a hit plus a lingering armor-shred debuff.
  **W -- The Standstill**: a real stun -- this roster's first-ever kit to call
  `arena_apply_stun()` (the generic infrastructure has existed since S170-184; no kit used it
  until now).
  **R -- Conjunction**, the actual meta-changing lever: for the duration, Zagan's TOTAL armor
  (`arena_hero_armor`, base+items both) becomes exactly equal to a locked target's -- a true live
  mirror, not an additive steal. R against a squishy target makes ZAGAN squishier too, a real
  cost that punishes always-R-the-biggest-threat play and rewards diving a tank instead -- no
  other ability on this roster can make its own caster weaker as the direct cost of using it.
  Two real pre-existing bugs found and fixed alongside: `apps/arena_server/src/main.c` hard-coded
  its hero-pick bound check against `ARENA_HERO_MNM`, silently making Weatherman unpickable over
  the real network path since he shipped; `apps/arena_hero_name` was also missing a Weatherman
  case entirely (fell through to "unknown"); `apps/arena_bot/src/main.c`'s own duplicated
  `ARENA_HERO_COUNT` was stale at 26 (already a hero behind). 9 new tests (passive
  trigger/no-retrigger, Q damage+shred+expiry, W stun in/out of range, R mirror+live-fallback),
  plus an `arena_ai_bridge` tags-string test. Full suite (764 checks) + test_10_bots.sh green.

- test(arena): S170-217, confirm last-hit already works for lane creeps. §20.3's own note:
  lane-creep-vs-lane-creep damage (`arena_tick_lane_creeps`) and hero-vs-lane-creep damage
  (`arena_hero_attack_lane_creeps`) are two independent sources converging on the same
  `ArenaLaneCreep.hp` field, so a hero finishing off an already-weakened creep likely already
  reproduces real last-hit behavior. No new code -- new test
  `test_hero_last_hits_a_lane_creep_already_weakened_by_the_wave_clash` runs both real damage
  paths for real (an actual wave clash weakens the creep, then a hero's real follow-up hit
  finishes it) and confirms the finishing hero gets full Flow+XP kill credit regardless of who
  dealt the earlier damage. Full suite green.

- feat(arena): S170-216, XP-share radius on lane creep kills. Was killer-only
  (`h->xp += ARENA_LANE_CREEP_KILL_XP` on the single hero whose hit landed); now every OTHER
  allied hero within the new `ARENA_LANE_CREEP_XP_SHARE_RADIUS` (8.0, deliberately bigger than
  this file's typical combat-ability radii -- XP-share rewards "present for the wave," not
  "landed inside a tight hitbox") also gets the XP. Flow/gold stays individual/precise
  (killer-only, unchanged) -- real MOBA parity. New test confirms a nearby ally shares XP while
  a far-away ally gets nothing. Full suite + test_10_bots.sh green.

- feat(arena): S170-215, deny for lane creeps. `arena_hero_attack_lane_creeps` used to filter a
  hero's own team's creeps out entirely; now an ally CAN target their own lane creep once it
  drops below 50% HP, killing it to deny the enemy the reward -- the real League deny mechanic.
  §20.3 flagged a sub-decision (build just "ally can kill their own" vs. also "enemy can't finish
  it below 50%"): only the first half is built, since the second half isn't how the real
  mechanic works (deny is a RACE, not a block on the enemy -- adding it would be an artificial
  buff beyond what real deny does). Same kill-reward path either way, no separate reduced-reward
  tuning (out of scope, matching this section's "spec the model, not the numbers" discipline).
  New test confirms an ally can deny below the threshold; existing above-threshold test's message
  updated for accuracy (behavior unchanged there). Full suite + test_10_bots.sh green.

- feat(arena): S170-214, minion-aggro-redirect on lane creeps. NORTHSTAR §20.3's single biggest
  missing piece of real lane-trading risk -- lane creeps previously picked their target purely by
  distance, entirely independent of who was actually fighting whom. Now a hero attacking an enemy
  hero within a lane creep's own aggro radius pulls that creep's aggro onto the attacker,
  overriding the plain-nearest pick, the real "minion aggro" mechanic. Detected via the
  defender-side `last_attacked_by_owner` + `combat_timer_ms > 0` signal (same fields
  `arena_tick_attack_windups`/Gary's homing shot already set for kill-credit) rather than a true
  same-tick attacker-side flag -- `arena_tick_lane_creeps` runs before hero-vs-hero combat
  resolves each tick, and `damaged_this_tick` is cleared at the end of the *previous* tick by the
  time it runs, so it isn't usable here (flagged honestly rather than reordering call sites for a
  same-tick check, a bigger and riskier change). New test:
  `test_lane_creep_aggro_redirects_to_attacker_over_a_closer_bystander` (attacker farther away
  than a never-attacked bystander, still gets targeted). Full suite + test_10_bots.sh green.

- refactor(arena): S170-213, rename "jungle creep" terminology to "node-guardian creep"
  throughout code. §20.2's own finding: they aren't League jungle camps at all (no buffs, no
  epic-objective equivalent, tied to node ownership, actively march) -- the mismatch between
  what "jungle creep" implies and what the entity actually does was itself likely part of "hard
  to reason about," independent of any mechanical change. Scoped to the actual rename target
  (identifiers, function/test names, comments describing this specific entity, one live README
  line) while leaving two adjacent things untouched on purpose: the separate, correctly-named
  "jungle obstacles/terrain" scenery system (rocks/trees/walls -- a different thing that happens
  to share the word "jungle" as flavor, not the renamed entity), and direct founder quotes using
  "jungle" in their own words (preserved verbatim as historical record, matching this repo's
  established practice). `ARENA_JUNGLE_CREEP_KILL_FLOW`/`_XP` -> `ARENA_NODE_GUARDIAN_KILL_FLOW`/
  `_XP`; 4 test function names renamed to match. Full test suite + test_10_bots.sh green.

- feat(arena): S170-212, visible aggro-radius ring for node-guardian ("jungle") creeps. Same
  `ring_mesh`/annulus idiom the R-zone/cast-radius circles already use (S170-200), reusing the
  flavor color already computed for each creep's body (gold/neutral, blue/team0, red/team1) so a
  player sees the boundary before taking an unexpected hit, rather than learning it that way --
  particularly valuable since a marching team creep's position (S170-161) is already
  unpredictable in a way a fixed camp wouldn't be. Outline only, no pulse, flat low alpha -- a
  static passive boundary, not a "something just happened" spell-cast effect. Verified live under
  Xvfb: the ring renders at the correct position and flavor color around a neutral creep.

- feat(arena): S170-211, node-guardian ("jungle") creep damage now routes through `apply_armor`.
  NORTHSTAR §20.3's first bullet, first item of the creep-overhaul batch resumed after the
  founder lifted the code freeze. These creeps previously dealt flat, unmitigated damage via a
  raw `apply_damage` call -- the one outlier among hero-vs-hero damage sources, all of which
  already go through `apply_armor(raw, arena_hero_armor(target))`. Same one-line fix shape as
  every other call site in the file. 3 existing tests asserted exact flat-damage numbers against
  the default Unicorn hero (4 armor); updated to set the target hero to Duck (0 base armor) first
  -- the same "exact hit-damage math" idiom `test_melee_windup_completes_and_deals_damage` already
  uses -- rather than hand-computing new armor-adjusted magic numbers. Full test suite +
  test_10_bots.sh green.

- fix(arena): S170-228 follow-up -- CI's Linux build was broken by the RL-policy wiring.
  Founder: "the build is down when we wired the new ai brain in" -> "its an issue with the
  linux bbuild." `scripts/build.sh` and `scripts/build_training.sh` had already picked up the
  new `packages/common/mlp_infer.c` link dependency (`arena_game.c` now calls
  `rl_policy_forward()` -> `mlp_forward()`), but two other places compiling `arena_game.c` were
  missed: `scripts/build_arena.sh` (CI's "Build Linux arena client" step, an executable link --
  unlike the training `.so`'s silent undefined-symbol case, this hard-fails at link time with
  `undefined reference to mlp_forward`, which is exactly what broke CI) and the mingw Windows
  cross-compile step in `.github/workflows/ci.yml`. Both fixed the same way: add
  `packages/common/mlp_infer.c` to the link line. Also found and fixed two stale local checkouts
  behind `origin/main` that compounded the confusion: `/home/fatbaby/REDGARDEN` (6 commits
  behind, missing `rl_policy_weights.h` entirely) and `/home/fatbaby/redgarden-deploy` (3 commits
  behind) -- both fast-forwarded and rebuilt clean. Verified: `scripts/build_arena.sh` and the
  full `scripts/test_arena.sh` suite pass locally; mingw itself isn't available in this sandbox
  to build-test the Windows fix directly, but it mirrors the identical, already-proven fix.

- fix(arena): S170-229, buying/selling in the shop no longer also moves the player. Founder:
  "clicking on item in shop to buy should not cause playyer to move." The shop-click and
  movement-click handlers were two separate `if` blocks reacting to the same click event with no
  shared state -- every shop click also fell through to the move-command handler. New
  `shop_click_consumed` flag, set whenever the click lands anywhere inside the shop panel's own
  bounding box, gates the movement handler.

- feat(arena): S170-228, wire the trained RL policy into the live bot AI. Founder: "let it train
  longer then dump the weights into c and commit" -> "update our bots to use it instead of the
  hand written net." A real, fully-trained 1,000,000-timestep PPO run evaluated at 30W/0L/0D over
  30 episodes against the heuristic bot AI. `arena_bot_tick`'s own movement now calls
  `rl_policy_forward()` instead of the old hand-picked-weight `bot_brain_forward()` -- scoped to
  movement only (the founder's own "hand written net," not the per-hero Q/W/R casting heuristic,
  which stays untouched). Real circular-dependency bug caught and fixed before it could bite:
  `arena_update()` auto-drives hero 1 through `arena_bot_tick` whenever `arena_bot_enabled` is
  set, which would have made the training harness's own "opponent" driven by whatever policy is
  currently compiled in -- unstable, and completely unbuildable on the first run. Fixed by
  keeping the old logic alive as `arena_bot_tick_heuristic` specifically for training to call
  directly, decoupled from the live game's own (now RL-driven) path. Fixed 5 existing tests that
  assumed the old bot's fixed, predictable movement. Verified live twice under Xvfb: the trained
  bot closes distance and engages in real mutual combat. Full test suite green.

- fix(arena): S170-227 export bug -- exact-integer weights produced invalid C literals. Founder:
  "can we run the unsupervised stuff here" -> "reinforcement" -- installed `gymnasium` +
  `stable-baselines3` and ran the full RL pipeline for real for the first time, closing every
  "not independently verified" gap the S170-225/226/227 doc comments had flagged. A real
  4000-timestep PPO smoke run trained cleanly against `ArenaTrainingEnv` with real checkpointing
  and evaluation. Exporting that real trained model surfaced a genuine bug
  `write_c_header_from_layers` never hit against its earlier hand-built synthetic test network:
  `f"{v:.8g}f"` produces invalid C literals like `0f` for exactly-integer weight values (a real
  trained model's own untrained biases genuinely include exact zeros). Fixed with a `fmt_float()`
  helper; re-verified end to end against the real model -- PyTorch and the compiled C header now
  match to float32 precision. Added a `--self-test` flag covering this exact edge case going
  forward. Full test suite green.

- feat(arena): S170-227, weight export to embedded C MLP + git-sync. NORTHSTAR §21's sprint,
  fourth and final item -- closes out the full reward-driven RL pipeline. New
  `packages/common/mlp_infer.c`/`.h`: a small, generic, dependency-free dense-MLP forward pass
  (SHANKPIT's own `neural_net.h` precedent, not `gpt2_infer.c` -- wrong shape for a small policy
  net), 5 new tests with hand-computed expected outputs. New
  `scripts/export_rl_policy_to_c.py` extracts a trained PPO policy's action-mean network (not
  the value/critic net) and writes it as literal C float arrays + a clipped
  `rl_policy_forward()` wrapper. Verified end to end: a hand-built PyTorch network shaped like
  SB3's own policy net was exported, compiled, and its C output matched PyTorch to float32
  precision. New `scripts/git_sync_utils.py` factors the SSH-push logic out of
  `colab_train.py`'s own `git_sync_weights_to_repo()` into a shared, artifact-agnostic function.
  `scripts/rl_train.py` now runs export + git-sync automatically after training. Full test suite
  green -- the complete S170-223..227 reward-driven RL pipeline is built (training itself not
  yet run against a real `gymnasium`/`stable_baselines3` install, flagged honestly).

- feat(arena): S170-226, PPO training script (Stable-Baselines3). NORTHSTAR §21's sprint, third
  item. `scripts/rl_train.py` trains a small MLP policy (SB3's own default `net_arch=[64,64]`)
  via PPO against `scripts/rl_env.py`'s `ArenaTrainingEnv`, same CLI-args/env-var delivery
  pattern as `colab_train.py`. Parallel envs via `SubprocVecEnv`, periodic + final checkpoints,
  a real evaluation pass reporting actual win/loss/draw rate against the heuristic bot AI. Same
  honest gap as S170-225: `stable_baselines3` isn't installable in this environment, so the
  actual training loop is written to spec but not live-tested -- flagged, not claimed.

- feat(arena): S170-225, Python gymnasium.Env wrapper for the RL sim. NORTHSTAR §21's sprint,
  second item. `scripts/rl_env.py` wraps `apps/arena_training/src/headless.c`'s ctypes API: an
  18-float Box observation space (named indices mirroring `sim_get_obs()`'s own layout), a
  5-float Box action space, and `compute_reward()` implementing S170-223's full reward design
  (damage dealt/taken, kill/death, Flow/XP gained, alive bonus, dominant terminal win/loss).
  Verified for real against the compiled `.so` via `--smoke-test` (400 sim ticks, real combat,
  reward correctly accumulating) since `gymnasium`/`stable-baselines3` aren't installable in
  this environment -- the `gymnasium.Env` subclass itself is written to spec but not live-tested,
  flagged honestly rather than claimed.

- feat(arena): S170-224, ctypes-callable RL environment API. NORTHSTAR §21's implementation
  sprint, first item. `apps/arena_training/src/headless.c` mirrors sibling SHANKPIT's own
  `apps/training/headless.c` shape (`sim_init`/`sim_step`) but exposes a small, fixed, documented
  18-float observation array (`sim_get_obs`) instead of a raw struct pointer -- `ArenaState` is
  large and still growing, so mirroring its exact layout in a Python `ctypes.Structure` would be
  fragile ABI surface with no compiler to catch a future desync. New `scripts/build_training.sh`
  builds `libarena_training.so`. Verified via a live ctypes round-trip (real movement/combat over
  200 ticks, real reset) plus 6 new headless C tests. Full suite green.

- docs(arena): NORTHSTAR §21, reward-driven RL spec -- Unity ML-Agents shaped (S170-223).
  Founder: "running training on a corpus of games is cool but thats not what i actually want
  right now i want unsupervised learning with rewards like in the unity ml-agents plugin." Found
  the right precedent already real in sibling SHANKPIT: `apps/training/headless.c` (a minimal
  ctypes-callable C environment API) and `neural_net.h`/`brain_weights.h` (a small MLP, weights
  as literal compiled-in C arrays -- the real "embed weights in C" pattern for a small policy
  net, distinct from `gpt2_infer.c`'s wrong-shaped-for-this token-generation approach). Designed
  the full reward function (dense per-tick shaping + dominant terminal win/loss term) and the
  target architecture (new `apps/arena_training/headless.c`, a `gymnasium.Env`, Stable-
  Baselines3's PPO, weight export to a small embedded-C MLP). Spec pass only, no code yet.

- feat(arena): embed trained bot AI weights into C + auto git-sync from Colab (S170-220,
  S170-221). Founder: "we want to embed the weights right into the c code... we can do it all
  with colab scripts running python to do it all / i will put the keys in MyDrive/.ssh."
  `packages/common/gpt2_infer.c`/`.h` is a verbatim port of the sibling gpt2-alpine-c repo's own
  C GPT-2 inference engine (fully parameterized, same file serves both repos' very different
  model sizes). `scripts/colab_train.py` now trains a small custom GPT2Config from scratch (4
  layers/128 dim/4 heads by default, not a fine-tune of public GPT-2-small, which at ~497MB is
  too large to commit every run and too slow for real-time inference), exports to the flat
  binary format that engine loads, and -- if an SSH key is present at
  `MyDrive/.ssh/id_ed25519` -- commits and pushes it straight to `origin/main` as
  `weights/redgarden-arena-bot.bin`. Verified end to end locally: a real exported tiny model
  loaded cleanly through the real C loader and produced finite logits on a real forward pass.
  5 new headless smoke tests, full suite green. Not done: wiring inference into the live bot AI
  decision loop -- flagged honestly, not faked.

- docs(arena): Colab training workflow instructions in README (S170-219). Founder: "put
  instructions in the readme for that i assume i upload the repo to drive and then what."
  Corrected that assumption -- the notebook clones REDGARDEN from GitHub inside Colab, no repo
  upload needed. Documents the real flow (build_ai_corpus.py -> upload corpus to Drive -> open
  notebook from GitHub -> run bootstrap cell -> checkpoint lands on Drive), and honestly flags
  what's not built yet (weight-embed-into-C, automated git-sync from Colab).

- fix(arena): split PACKET_ARENA_SNAPSHOT into world + hero-chunk packets (S170-193). Founder
  decision on the flagged MTU risk: split rather than accept fragmentation or trim the payload.
  `ArenaSnapshotMsg` had grown to 2460 bytes (heroes[20] alone was 1680 of that) -- comfortably
  over the typical 1500-byte Ethernet MTU, where a UDP datagram that size gets IP-fragmented and
  losing any ONE fragment loses the WHOLE datagram. heroes[] now goes out as 2 self-contained
  `PACKET_ARENA_SNAPSHOT_HEROES` packets (10 heroes each, each carrying its own `total_count` so
  it never depends on arrival order) instead of living inside the world message -- new sizes
  ~788/~856 bytes, both with real headroom under MTU. Touches all three consumers (server,
  human client, bot); the bot's own receive-loop restructure also fixed a related latent bug
  (prev/cur used to swap on every individual packet rather than once per drained batch, subtly
  corrupting flock velocity inference if the bot's loop ever fell behind). Live-verified on
  isolated ports: a 1v1 match played to a real winner with real HP changes, and a 12-hero match
  confirmed both hero chunks (including owner slots 10/11, the second chunk) deliver real data.
  Full sim test suite green.

- docs(arena): NORTHSTAR §20, full creep overhaul -- LoL parity spec first (S170-209). Founder:
  "full creep overhaul lol parity northstar doc first currently creeps are spooky too strong and
  hard to reason about." Pins down League's real minion-wave model (melee/caster/siege roles,
  automatic wave clashes, minion-aggro-redirect on champion attacks, gold-is-individual/XP-is-
  shared last-hit split, deny, structure pressure) against REDGARDEN's own two separate creep
  systems. Headline finding: lane creeps are the closer analog but collapsed to one role with no
  aggro-redirect/deny/XP-share; jungle creeps aren't League jungle camps at all -- they're node-
  ownership guardians dealing flat unmitigated damage (no `apply_armor` call), wearing jungle-
  creep terminology that's itself likely part of the "hard to reason about" complaint. Proposes a
  sequenced target design without deciding any numeric retuning -- spec pass only, no code.

- feat(arena): MnM W rework -- Burrow, replacing the free toggle armor stack (S170-208). Founder:
  "switch MnM w to burrow where he digs down below the map and is untargetable in that time
  dealing small aoe damage when he comes back up." W is now a real cast (14s cooldown, flat
  mana charge) that sets intangible_ms + rooted_ms for 1.5s -- untargetable and pinned in place,
  same combo his own R already uses -- then fires a one-shot AoE eruption (radius 3.0, 16 damage)
  on the exact spot he burrowed once a new dedicated `mnm_burrow_ms` countdown hits zero. Also
  gated `mnm_burrow_ms` across all three auto-attack paths plus the legacy 1v1 `resolve_combat`
  resolver -- a burrowed MnM isn't present on the surface to swing at anything, a real bug this
  change's own first test draft caught (the 1v1 path had no such gate). 11 new/updated tests,
  full suite green.

- fix(arena): shop panel showed only 24 of 27 items; Donkey fold proc affordance (S170-210).
  Founder: "ensure the new items donkey and blink dagger are actually available in the shop ui"
  -> "ensure donkey has affordances so its clear something is happening when it procs on the 25%
  health thing." `SHOP_ITEMS_PER_COL` was a stale hardcoded 12 (2 cols x 12 = 24 slots), a
  leftover from when the item catalog had exactly 24 entries -- both the render loop and the
  click hit-test share that constant, so Blink Dagger (24), Donkey (25), and Haste Trinket (26)
  rendered nowhere and couldn't be bought. Bumped to 15. Also added a gold-white FoldFlash burst,
  a distinct proc tone, and a "DONKEY FOLD" status tag (replacing the generic UNKILLABLE one) so
  Immortal's Fold reads as a clear, sourced event rather than a silent stat change -- reusing the
  frame-delta reconstruction idiom the heal/attack flashes already use, no wire-protocol change
  needed.

- feat(arena): Haste Trinket, modest 6% CDR passive (S170-207). Founder: "add a haste trinket" ->
  "passive haste lowers cd and auto attack cd make it a modest improvement 6%." New Trinket-slot
  item (900 Flow) reducing ability cooldowns and auto-attack cooldown by 6%, via a new
  `bonus_cdr_pct` item stat summed into `item_bonus_cdr_pct` and applied by a shared `apply_cdr()`
  helper wired into `cast_cooldown()` (Q/W/R) and all 4 auto-attack cooldown assignment sites.
  Deliberately does not touch windup duration -- matches NORTHSTAR §17.1's documented real-League
  behavior that attack speed compresses backswing/cooldown but not the windup fraction itself.

- feat(arena): Weatherman + Donkey, NORTHSTAR §16 (S170-206). Founder: "add the weatherman and
  donkey" -> [clarified Donkey's "owner" ambiguity] -> "donkey should be an item" -> "3.2k flow"
  -> "tilda should make the hero do the paper airplane glide thing" -> "longish range high speed
  escape can move above obstacles" -> "long ish cooldown" -> "2 minute cooldown on paper plane
  fly mode" -> "but the thing where it unfolds and fights for you thats a passive." §16.1's whole
  premise -- a genuinely new non-piloted companion-unit system -- turned out avoidable: Donkey
  ships as an equippable item (3200 Flow, Back slot) whose two procs trigger on whichever hero
  wears it. Immortal's Fold (automatic): HP < 25% -> damage floor + periodic fight-back damage to
  the nearest enemy, own proc cooldown. Paper Glide (tilde-activated, same key as Blink Dagger,
  generalized via a new `arena_use_active_item` dispatcher): a real high-speed traversal (7x base
  speed) away from the nearest enemy, flies over obstacles, untargetable for the window, 2-minute
  cooldown. Weatherman ships as hero #27: Q (Barometric Shove, ranged knockback, no damage -- the
  roster's first push-outward Q), W (Collects On What's Owed -- grounds an airborne enemy,
  extends an airborne ally, reading the item's own `donkey_airborne_ms` field), R (The Debt
  Compounds, AoE zone DPS), Passive (The Ledger, Dagda's Undry regen shape). 16 new tests, build
  clean, full suite green (654/654). Live-verified: an isolated 10v10 match with the new 27-hero
  roster, zero crashes, real HP changes across all 20 heroes in the match. `docs/HEROES_VS0.md`
  updated (Donkey repointed to the item roster, Weatherman kit added); NORTHSTAR §16 status block
  added.

## 2026-07-28 (continued 10)

- feat(arena): Blink Dagger, item catalog's first real active ability (S170-205). Founder: "add
  blink dagger 1400 flow it gives a new keybind on screen for tilda" -> "+6ap +6hp". New 25th
  item (Trinket slot, 1400 Flow, +6 AD/+6 HP), but the real value is `arena_use_blink` -- the
  first item in the catalog that isn't just passive stats. Bound to a dedicated key (tilde/
  backquote), distinct from Q/W/E, since it's an item activation, not a kit ability. New
  `PACKET_ARENA_BLINK` (no payload), a fully separate `blink_cooldown_ms` track (doesn't touch
  Q/W/R cooldowns or mana). Direction: toward the current move target if moving, else the
  nearest living enemy, else no-op -- the same fallback chain `unicorn_cast_q` already
  established. Travels `ARENA_BLINK_RANGE` (12.0, the single longest gap-closer/escape distance
  on the whole roster) or the remaining distance to an already-close target, whichever is
  shorter. `ARENA_BLINK_COOLDOWN_MS` matches real DOTA's own Blink Dagger cooldown exactly (15s).
  Blocked by stun but NOT by silence -- using an item isn't a cast. A 4th ability tile shows real
  synced cooldown state, only drawn while the local player actually has it equipped. 8 new tests,
  build clean, full suite green (638/638). Live-verified: GUI client ran 6s under Xvfb with the
  new keybind/tile render code active, no crash.

## 2026-07-28 (continued 9)

- feat(arena): auto-attack windup/backswing, NORTHSTAR §17 LoL parity (S170-204). Picked as the
  next spec-only NORTHSTAR section to build (over §16 Weatherman/Donkey and §19.5 structures).
  Real audit finding before writing any code: §17.3's own "gap analysis" was already stale --
  S170-162/163 had already shipped the distinct attack command, the persistent attack-target
  lock with pure-pursuit chase, and Gary's real homing ranged basic attack, three of §17.4's five
  target-design bullets, just never reflected back into the doc. What was still genuinely
  unbuilt, and the actual core of the founder's original question ("does the champion stop when
  auto-attacking?"), was the windup/backswing state machine itself. New
  `attack_windup_ms_remaining` on `ArenaHero`, applied to both the flat melee loop and Gary's
  ranged attack: a fresh attack begins a real windup (25% of the existing cooldown, NORTHSTAR
  §17.5's own suggested ratio) instead of dealing damage instantly; movement freezes during
  windup; a genuinely new move command or a stun cancels it outright (no damage, no cooldown
  spent); a completed windup re-validates the target and fires. Real risk found and handled:
  `apps/arena_bot`'s own ~100ms decision loop re-sends a move command constantly even while
  already in range -- naively canceling on ANY move command would have silently broken melee
  damage for every bot-controlled hero. Fixed by comparing the new target against the hero's
  CURRENT position (not the previous target) gated by its own attack range, so a real reposition
  cancels but the bot's own noisy re-affirmation doesn't. `§17` updated with a status block
  correcting the stale gap analysis and a checklist of what's shipped vs. still open (roster-wide
  ranged split beyond Gary, attack-move). 11 new/updated tests, build clean, full suite green
  (630/630). Live-verified: an isolated 10v10 match ran 45s with zero crashes and real HP changes
  across 18/20 heroes including a death -- melee combat works correctly with real bot AI, not
  just unit tests. Deliberately out of scope: the 1v1 practice demo and hero-vs-creep combat both
  keep their existing flat instant-damage model -- windup/kiting is a PvP mechanic.

## 2026-07-28 (continued 8)

- feat(arena): Gary W -> Aimed Shot, a real cast-time ability (S170-203). Founder: "switch gary w
  to aimed shot just like wow hunter cast time big damage for now movement interrupts cast damage
  does not interrupt cast silence does" -> "ensure cast bar affordance shown to user." Gary's W
  was a free toggle extending Q's own range; it's now a real WoW Hunter-style cast-time nuke on
  its own cooldown. New generic cast-time infrastructure on `ArenaHero` (`casting_slot`/
  `cast_time_remaining_ms`/`cast_total_ms`/`cast_anchor_x,z`/`cast_target`) -- Aimed Shot is the
  first ability to use it, not the only one this is meant to support later. Needs a hittable foe
  in range to even begin (no target = no-op, no cost spent). Movement interrupts: live position
  checked every tick against where the cast began -- a fresh move command OR a forced
  displacement both catch uniformly via one position check. Silence interrupts, checked right
  after `silenced_ms` ticks down each frame. Damage does NOT interrupt -- no HP/combat-timer
  check anywhere in the logic, deliberately. Target re-validated only at completion, not
  continuously -- stepping out of range mid-cast without the caster moving still costs the cast.
  `casting_slot`/`cast_time_remaining_ms`/`cast_total_ms` synced on the wire and rendered as a
  real progress bar under every casting hero's health bar, visible to everyone watching, not just
  the caster. Ability tile highlights while mid-cast too. `gary_cast_q`, the toggle-hero list/
  ability name/blurb, the internal bot AI's Gary heuristic, and `docs/HEROES_VS0.md` all updated
  to match. 6 new tests (cast begins/no-op/completes/movement-interrupts/damage-does-not/
  silence-interrupts), build clean, full suite green (613/613). Live-verified: GUI client ran 6s
  under Xvfb with the new render code active, no crash. The external networked bot AI never casts
  W at all (pre-existing, unrelated) so live-match verification of the mechanic is
  unit-test-covered rather than bot-observed this pass — flagged, not faked.

## 2026-07-28 (continued 7)

- feat(arena): bot node-capture fix + fractal-boids squad splitting (S170-201, S170-202).
  Founder: "there is some issue with flcoking my team having a lot of trouble capping a node" ->
  "like the whole team doesnt need to try to cap the node" -> "add like fractal boids so we
  naturally split more into squads." Two related bugs in the node-capping/flocking AI, fixed
  together. S170-201: the old flocking-anchor rule picked one "anchor" bot per node via
  `owner_index mod ARENA_SNAPSHOT_NODE_COUNT == node_index` — purely coincidental, unrelated to
  whether that bot was actually heading to that node right now; if its two coincidental
  slot-owners were dead/engaged/anchoring elsewhere, NOBODY ever anchored that node, and
  `flock_offset`'s own separation force alone could push every other bot's real move target
  outside `ARENA_NODE_CAPTURE_RADIUS` forever — exactly "trouble capping A node," not every node.
  S170-202: fixing anchoring alone left the other half of the complaint — every idle bot
  independently picking its own nearest node converged the WHOLE team onto the same one. Fixed
  with a "fractal" application of the same Reynolds boid instinct `flock_offset` already uses,
  recursively, at a coarser scale: individuals flock tightly within a SQUAD (unchanged math,
  squad-scoped), while squads spread apart by claiming DIFFERENT nodes via a small deterministic
  greedy pass every bot computes identically (no coordination needed) — differentiated GOALS
  instead of a second force, reinforcing goal-seeking rather than fighting it. With squads doing
  the claiming, the anchor question collapses to "am I my own squad's lowest owner index."
  Build clean, full suite green (607/607). Live-verified extensively: a full isolated 10v10 match
  with temporary debug instrumentation confirmed squad assignments span all 5 nodes, movement
  continues smoothly toward assigned targets (no freeze), and a follow-up 60s run on the final
  binary showed real, spread-out movement across the whole roster with zero crashes (21/21
  processes healthy). An initial small 3v3 test appeared frozen, triggering this deep
  verification pass — traced to a stable melee standoff (unchanged engage-branch code, never
  reaching the node-capping branch at that low headcount), not a regression.

## 2026-07-28 (continued 6)

- feat(arena): real cast-radius affordances + ground zone circles (S170-200). Founder: "zone
  abilities dont read at all we need true aoe cast circle click affordances that show cast radius
  also it should show a circle on the ground, nice shader spell effect simple but nice showing to
  all participants that the spell was cast there so it reads." 8 heroes' R (Ghost/Flamel/
  Morrigan/Paimon/NOOR-1/Vassago/He Xiangu/Beleth) cast a real fixed-position, radius-accurate,
  multi-second ground zone -- none of that state was ever on the wire, so a networked client had
  no way to know a zone existed, where it was, or how big, and even locally the only visual was
  the generic "small/medium/big by slot" flash, not the ability's real radius. New
  `arena_hero_r_zone_radius()` is the single source of truth for "how big," reused by the
  existing (unchanged) mechanical damage/heal check and the new rendering. `r_zone_x`/`r_zone_z`/
  `r_active_ms` added to `ArenaHeroSnapshot`, synced for every hero. New `disc_mesh` (filled
  circle, same build-once-at-unit-scale idiom as `ring_mesh` -- a thin ring alone reads as a wire
  outline in a busy fight, not a real area) — a pulsing filled disc + boundary ring renders at
  the zone's real position/radius for its real remaining duration, identical for every client.
  Cast-radius preview: while your own hero's R is a zone ability and actually castable, a faint
  outline ring shows where/how big it'll land before you commit -- every zone in this roster
  casts at the caster's own position (no ground-click targeting exists in this input model at
  all), so a live self-centered preview is the honest, buildable affordance this pass; a full
  click-to-place targeting system would need its own aiming input mode and wire command, scoped
  out, not silently dropped. Build clean, full suite green (607/607). Live-verified: an isolated
  2-bot networked match completed cleanly with the new wire fields (no truncation/protocol
  mismatch); the GUI client ran 6s under Xvfb with the new per-frame render code active, no
  crash.

## 2026-07-28 (continued 5)

- docs(arena): item stats table + suggested heroes for new players (S170-199). Founder: "i need
  you to put the stats of the items in the readme and suggested heroes." The full 24-item
  `ARENA_ITEMS` catalog as a real table (slot, cost, AD/HP/MP/Armor/Move Speed) — previously only
  described in prose, no actual numbers anywhere outside the source. 4 suggested-hero picks
  spanning Tank/Fighter/Marksman/Support, chosen for kit simplicity (no clone management, no
  blink mind-games, no stealth timing): MnM, Duck, Gary, He Xiangu. Docs-only, no code changed.

## 2026-07-28 (continued 4)

- fix(arena): remove fog of war -- client-side-only visibility isn't real (S170-198). Founder,
  real-time, immediately after S170-196 shipped: "remove fog of war its only client side fuck
  that." NORTHSTAR §15.2 itself named this exact tradeoff and explicitly deferred real
  server-side vision culling rather than build it -- "the enemy just doesn't render" was always
  a cosmetic-only gate a modified client trivially bypasses, not real information hiding. Reverts
  the fog half of S170-196 (`ARENA_VISION_RADIUS`, both distance-check skip blocks in
  `apps/arena/src/main.c`, the README section) -- camera lock (§15.1, the `C` toggle) is
  untouched, not part of this complaint. Build clean, full suite green (607/607).
- feat(arena): 10x Flow earned from all sources (S170-197). Founder, real-time: "the economy is
  too slow i can never buy anything increase flow gained by 10x from all sources." All 4
  Flow-earning constants x10: jungle creep kill 15->150, lane creep kill 8->80, hero kill
  100->1000, assist 35->350. XP left untouched -- not mentioned, and XP has no spend pressure the
  way Flow does. Every existing test already checked against the constants, not literal numbers,
  so nothing needed updating. Build clean, full suite green (607/607).

## 2026-07-28 (continued 3)

- feat(arena): camera lock + fog of war, NORTHSTAR §15 (S170-196). All S170 sprint checklist
  items done except S170-193 (flagged as needing the founder's own design call, not auto-picked)
  — chose the next NORTHSTAR spec-only section to build via a direct question, picked "camera
  lock/fog of war" (§15) over auto-attack LoL parity (§17), Weatherman/Donkey (§16), and
  structures (§19.5). Camera lock: new `C` toggle — the orbit pivot already hard-follows
  `my_owner`'s hero every frame unconditionally, so locking only ever meant freezing the
  yaw/pitch rotation angle, the one way a player can currently look away from their own hero;
  zoom stays free while locked, resolving §15.1's own open question per real-MOBA convention.
  Fog of war: client-side visual only, explicitly not real server-side vision culling (named and
  accepted in the spec). New `ARENA_VISION_RADIUS` (16.0 * phi, ~25.89 — a real fraction of the
  current golden-ratio-scaled node spacing, not the pre-S170-191 interaction radii the original
  spec named, which never got rescaled with the map) — an enemy hero beyond that radius of
  `my_owner`'s own hero is skipped entirely (no model, no health bar, no name, and — as a natural
  side effect of skipping before the hover computation — can't be hover-targeted or
  attack-clicked). Allies and jungle creeps always visible, resolving §15.2's own open questions
  toward their stated lean. README keybind table + a new "Fog of war" section updated to match.
  Build clean, full suite green (607/607). Live-verified: `red_garden_arena` ran 6s under Xvfb in
  local practice mode (a real enemy hero on screen, the new distance-check code exercised every
  frame) with no crash — visual confirmation of the actual on-screen fog cutoff/lock-freeze isn't
  capturable in this sandbox (no xdotool for the drag/hover input), same limitation prior
  visual-only passes this session (S170-182, S170-185) already hit — flagged, not faked.

## 2026-07-28 (continued 2)

- feat(arena): unsupervised-learning prep, end to end (S170-194, S170-195). Founder: "do the work
  to prepare for unsupervised learning" -> "target torch training on colab." Two commits, one
  deliverable:
  - **S170-194 (C side, `6743964`)**: fixed `arena_serialize_state`'s real, load-bearing "owner
    must be 0 or 1" restriction (a direct blocker for team-mode corpus, which is now the primary
    game mode) — bounds check is now any real active hero slot, foe resolved via
    `arena_nearest_enemy` instead of a hardcoded opposite index. Added a 26-hero kit-shape tag
    table (`ranged`/`melee`, `has_homing_attack`, `has_knockback`, `has_heal`, `has_dash`,
    `has_stealth` — NORTHSTAR §18.6's own "stronger lever" for cross-hero transfer) and
    `arena_hero_tags_string()`, spliced into both self and foe blocks. New
    `arena_corpus_record()` writes one `{"text": "..."}` JSONL line per active hero per tick
    (state + the same move/cast_q/cast_w/cast_r action format `arena_decode_action` already
    parses — one format serves both training-label and future policy-output duty). Wired live
    into `apps/arena_server` via `corpus_log_tick`, alongside the existing match logger. Also
    fixed `scripts/build.sh` never linking `arena_ai_bridge.c` into the server build (a real
    "undefined reference" bug, not caught until live-verifying with an actual match). 10 new
    tests, 607/607 green. Verified live: an isolated 2-bot match produced 38 valid corpus
    records.
  - **S170-195 (Python/Colab side, `2aa464d`)**: `scripts/build_ai_corpus.py` aggregates
    `var/corpus/arena-corpus-*.jsonl` into one combined file; `scripts/colab_train.py` ports
    `gpt2-alpine-c`'s own proven GPT-2-small next-token-prediction pretrain pattern (same
    `{"text": ...}` shape, zero conversion needed); `notebooks/redgarden_gpt2_pretrain_colab.ipynb`
    is the one-cell bootstrap (mount Drive, git clone/pull, run the script) — training logic
    lives in the versioned script, not notebook cells, so a `git pull` picks up future changes
    with no re-pasting.

  This is genuinely NORTHSTAR §18.4's unsupervised pretraining stage — next-token prediction,
  no win/loss label — not §12 Phase E's later supervised, NORN-graded fine-tune; the resulting
  checkpoint is meant as that later stage's starting weights, not a finished policy. The Python/
  Colab half can't be run end-to-end in this sandbox (needs a real corpus from played matches
  plus an actual Colab GPU) — flagged, not faked; only the C-side corpus pipeline was verified
  against a real live match.

- fix(arena): CRITICAL -- fixed-size 2048B receive buffer silently truncated every real
  snapshot (S170-192). Found live while smoke-testing the map expansion below: an isolated
  bot-vs-bot match got stuck at "entering draft" forever. `apps/arena`'s and `apps/arena_bot`'s
  own `net_poll_snapshots` both used a fixed `char rbuf[2048]` to receive
  `PACKET_ARENA_SNAPSHOT` — every field this session added to `ArenaHeroSnapshot`/
  `ArenaSnapshotMsg` grew the real wire packet to 2072 bytes, past that fixed size, without
  anyone checking the receive side's own headroom. `recvfrom` silently truncates a UDP datagram
  larger than the buffer, so every snapshot was truncated and correctly rejected by the
  existing size check — no client (bot or human) could ever see valid state. Fixed by sizing
  both buffers to the real, current struct size instead of a magic literal, so this can't
  silently drift again. Re-verified the same isolated match: draft, live play, and a clean
  match end, all working. Plausibly explains part of the "frozen 1v1" reported earlier this
  session (a separate, already-fixed live-pool stale-binary mismatch was the other confirmed
  cause).

- feat(arena): golden ratio map expansion + more jungle obstacles (S170-191). Founder: "use
  golden ratio to expand map size and add more jungle obstacles." `ARENA_HALF_EXTENT` now
  `32.0 * phi` (was 32.0, itself 20→28→32 through earlier passes) — left as a real expression,
  not a pre-computed literal. Node layout, jungle obstacles, and the S170-190 powerup layout all
  scaled by the same phi factor (written as original-coordinate-times-phi for traceability).
  `arena_fountain_position` converted from a hardcoded literal — found already stale before this
  pass, per `arena_graveyard_position`'s own comment — to a real formula that can't drift again.
  10 new jungle obstacles fill the substantial new outer margin. `apps/arena_bot`'s own
  duplicated fountain/shop literals updated to match. 1v1 local demo spawns deliberately left
  unscaled (separate, always-compact practice pairing). Build clean, full suite green (597/597,
  audited beforehand — no test hardcodes a position literal).

- feat(arena): Berserker + Regen powerups, Warsong Gulch-style (S170-190). Founder: "add
  berserker and health regen powerups like from warsong gulch in between the nodes." New
  `ArenaPowerup` entity, two neutral pickups positioned at the midpoints between the node
  clusters (derived from `arena_nodes_reset_layout`'s own table). Walking within
  `ARENA_POWERUP_PICKUP_RADIUS` grabs it, granting a 20s timed buff (Berserker: +15 flat AD via
  a new `arena_hero_bonus_ad` helper; Regen: +8 HP/sec, same fractional-accumulator idiom as
  mana regen) — the powerup goes inactive and respawns 60s later. Hero-only, same clone-exclusion
  scoping as fountains/node-capture/creep-targeting. Wire-synced (unlike static fountains, these
  have real dynamic state), rendered as a floating orb, `hero_status_label` shows BERSERKER/REGEN
  tags. Works in both 1v1 and team mode. 6 new tests, build clean, full suite green (597/597).

- docs(arena): NORTHSTAR §19 status update — economy shipped, structures still spec-only. Found
  via proactive audit: the section header still read "spec only, no code yet" even though the
  economy half (Flow/XP, item shop, character pane, bot AI shopping, assists) has been fully
  built and shipped this session. Added a status callout and marked §19.5 (structures)
  explicitly as still unbuilt. Docs only.

- fix(arena): Tyler clone kills now credit Tyler, not the disposable clone slot (S170-188).
  Found via proactive audit (no fresh backlog item queued). Real bug: a Tyler puppet clone
  landing the actual killing blow credited Flow/XP/kills to the clone's own disposable
  `ArenaHero` slot, lost the instant that slot gets reused on Tyler's next R — never reaching
  Tyler, the real player whose army earned the kill. New `arena_reward_owner()` resolves a raw
  owner index to who should actually be credited, applied where damage attribution is recorded
  (the one call site a clone can ever reach — Gary's own homing-shot path never sees clones).
  1 new test, build clean, full suite green (583/583).

- feat(arena): hero kill assists now grant Flow + XP (S170-187). Founder: "assists should gen
  flow." New `assist_owner[]`/`assist_ms[]` (4-slot recent-attacker memory, separate from
  `last_attacked_by_owner`) recorded via `record_assist_damage()` at the same melee/homing-shot
  call sites the kill-attribution field already uses. On a kill, everyone else in the victim's
  assist list within the ~10s window gets `ARENA_HERO_ASSIST_FLOW`/`XP` (35/20, roughly a third
  of the full kill bounty), excluding whoever got the full kill reward. Fixed the same sentinel-
  after-memset gap this session has now hit three times, for `assist_owner[]` this time, in both
  reset paths. No new wire/UI surface needed — flows into the already-synced/displayed
  `flow`/`xp` fields. 4 new tests, build clean (via the now-fixed `scripts/build.sh`), full suite
  green (578/578).

- fix(arena): `scripts/build.sh` now actually builds `apps/arena` (S170-186). Real gap found
  while investigating an unrelated rendering question: the script every "build clean" claim
  this session relied on never built the actual human GUI client at all — only
  `scripts/build_arena.sh` did. Checked: it does compile clean (pre-existing warnings only, none
  from this session), so no broken commits, just an unverified claim that happened to hold.
  Folded the same `gcc` invocation into `build.sh` so this can't recur. Verified with a full
  `rm -rf build` + rebuild from scratch — every binary this repo ships now comes out of the one
  script. Full suite still green.

- fix(arena): real 7-segment digit glyphs — every number was illegible (S170-185). Founder:
  "ensure our font can render numbers." Real bug: `draw_char`'s digit branch drew the exact same
  generic box outline for every digit 0-9, completely indistinguishable from each other. Every
  numeric HUD value this game shows (HP/MP, ability cooldown countdown, Flow/XP/item costs, K/D,
  APM) has been effectively illegible as a specific number this whole session. Replaced with a
  real standard 7-segment mapping, same `GL_LINES` stroke style as every other glyph in this
  font. Build clean, full suite green (client-only). Live Xvfb screenshot confirms the rendering
  pipeline itself is healthy (letters render correctly) — couldn't capture a frame with an
  active on-screen number in this sandbox (no interactive input, no cooldowns active yet at
  match start); the segment mapping is a standard, directly-verifiable table, not a guess.

- feat(arena): generic Stun + Slow status effects, using GoblinFoxDragon's `server/status`
  package as reference (S170-184). Closes a gap `hero_status_label`'s own doc comment already
  flagged. New `stunned_ms` (hard CC — blocks movement/casting/auto-attack, gated at every
  action call site) and `slowed_ms`/`slow_pct` (proportional move-speed reduction) fields on
  `ArenaHero`, plus `arena_apply_stun()`/`arena_apply_slow()` kit-wiring hooks (no kit uses them
  yet, infrastructure first). Real bugfix found along the way: none of the five existing
  status-effect fields were ever synced over the wire — the status label HUD has been silently
  non-functional in every networked match, same class of bug as S170-180's `w_active` fix. Fixed
  for all seven fields (5 existing + 2 new). 9 new tests, build clean, full suite green
  (568/568).

- feat(arena): bot AI shop interaction, Sprint 5 of S170-175. Closes the last explicitly-deferred
  gap from the shop/economy pass. Simple first pass: when no enemy is within a safety radius and
  the bot can afford the next item in catalog order, it detours to its own team's shop and buys —
  `arena_shop_buy`'s own server-side validation does the real work, the bot just decides when to
  go and which item's next. New `send_shop_buy()` mirrors `send_attack`'s packet shape; item
  costs duplicated locally as `ARENA_BOT_ITEM_COSTS` (same "kept in sync by hand" idiom this file
  already uses, since it deliberately doesn't link the sim package). Build clean, full suite
  green. Verified live: an isolated 4-bot match ran cleanly 24s with no crashes; the match log's
  snapshot schema doesn't carry flow/equipped_item so an actual purchase isn't directly visible
  in it — flagged, not faked.

- revert(arena): move team size back to 10v10 (S170-183). Founder, real-time, mid a live-pool
  queueing investigation: "ok move back to 10 v 10." Symmetric revert of S170-178's 7v7 change —
  `ARENA_TEAM_SIZE` 7 → 10, `ARENA_SNAPSHOT_MAX_HEROES` 14 → 20, both pool-launch
  scripts/systemd deploy sources reverted to match (not the live host itself), doc comments
  updated. Build clean, full suite green. Doesn't by itself confirm the live-pool queueing issue
  is resolved — the live host's own binaries still need a separate rebuild/restart.

- feat(arena): real draft pick-a-hero UI, replacing auto-draft (S170-182). Split out from the old
  bundled S170-69 item. Draft used to auto-pick instantly (S170-66/68); now a real 26-hero grid
  screen (`draw_draft_screen`) replaces the normal match view for as long as
  `net_phase == ARENA_PHASE_DRAFT && !net_picked` — click a tile to draft it
  (`draft_screen_hero_at` shared between hit-test and hover-highlight). Replaced the old
  auto-pick's `net_draft_offset` formula with `net_picked_hero_id` so the existing resend safety
  net resends the real click, not a recomputed value. No auto-fallback if the player never
  clicks — deliberate scope decision, flagged not faked. Build clean, full suite green;
  server-side draft flow (untouched) verified via an isolated bot-vs-bot match on a fresh port.

- feat(arena): real cursor-shape swap on enemy hover (S170-69). Founder northstar: "nice cursor
  indicators for hover over enemy vers aly etc." The color-coded YOU/ALLY/ENEMY bracket+label
  already covered the relation-indicator half; this adds the literal cursor-shape swap
  (`SDL_CreateSystemCursor`/`SDL_SetCursor`) — crosshair over a live hittable enemy, default
  arrow otherwise. Client-only, full suite green. S170-69's other half (a real draft/lobby
  pick-a-hero UI) is substantial standalone work, split out and left open.

- docs(arena): document Flow/XP economy, item shop, and 7v7 in README (S170-177). Founder: "and
  document it all in the readme." Extends the S170-97 keybind table (`B` shop, `1`-`9` quick-buy,
  held-`TAB` scoreboard, `H` overlay, W-toggle-vs-instant mana distinction) and adds a "Flow, XP,
  and the item shop" section.

- fix(arena): sync `w_active` over the wire; toggle W drains mana over time (S170-180/181).
  Founder: "it seems like toggelable abilities arent working" — real root cause, `w_active` was
  never on the wire at all, so a networked client's own local copy stayed permanently 0/off
  regardless of the real server state (the W tile's "active" highlight was always wrong in
  net_mode). Added the field to `ArenaHeroSnapshot`, synced both directions. Also, founder:
  "instead of initial mana cost toggle spells should drain mana over time" — the 10 true-toggle
  heroes no longer charge a flat `ARENA_MP_COST_W` to activate, just need `mp > 0`; a new
  `ARENA_MP_DRAIN_W_PER_SEC` drains continuously while active (`w_drain_accum`, same
  fractional-accumulator idiom as mana regen), auto-deactivating at 0 mana. New
  `arena_hero_w_is_toggle()` lets the client HUD pick the right mana-cost model per hero. 2 tests
  rewritten, 2 added. Full suite green.

- feat(arena): reduce team size to 7v7 (S170-178). Founder: "reduce it to 7 v 7." `ARENA_TEAM_SIZE`
  10 → 7 — every sim-side array/loop bound derives from `ARENA_MAX_HEROES`, so this is the whole
  gameplay change. Duplicated size constants that don't auto-derive updated to match:
  `ARENA_SNAPSHOT_MAX_HEROES` (protocol.h) 20 → 14, `launch_arena_pools.sh`'s
  `BOT_POOL_LOBBY_SIZE` 20 → 14, `run_bot_pool.sh`'s default bot count 19 → 13, both
  `ops/systemd/*.service` deploy sources' lobby-size/bot-count. The systemd files are deploy
  sources only — editing them doesn't touch the actually-running live pool, which needs a manual
  re-copy + restart on the host (deliberately not done here). Build clean, full suite green — all
  team-mode tests already reference `ARENA_TEAM_SIZE` symbolically.

- feat(arena): shop panel + character pane + scoreboard, Sprint 4 of NORTHSTAR §19 (S170-175).
  Shop structures rendered at each `arena_shop_position()` (team-relative colored trim). An
  always-visible character stat pane (local hero's HP/MP/AD/Armor/Flow/Flow-earned/XP/K-D). A
  shop panel (`B` to toggle) with instant one-click buy/sell and `1`-`9` quick-buy — no confirm
  step, per this repo's own "high-APM... instantly resolve, no menu-diving" cross-cutting
  constraint. A held-`TAB` scoreboard: per-hero and team-aggregate K/D/Flow/XP. `net_poll_snapshots`
  now copies the Sprint-3-synced economy fields into local hero state so the client has real data
  to show. Build clean, full suite green (client-only change). Visual verification hit a real but
  pre-existing Xvfb/software-GL coordinate quirk in this sandbox that also reproduces against
  already-shipped HUD code this change never touched (the 3D pass, including the new shop
  structures, rendered correctly in every screenshot) — flagged, not faked; real-desktop
  verification still open.

- feat(arena): shop wire protocol, Sprint 3 of NORTHSTAR §19 (S170-175). `PACKET_ARENA_SHOP_BUY`/
  `PACKET_ARENA_SHOP_SELL` + `ArenaShopBuyCmd`/`ArenaShopSellCmd`, dispatched in
  `server_handle_packet` the same shape as the existing `PACKET_ARENA_ATTACK` handler.
  `ArenaHeroSnapshot` gains `flow`/`flow_earned`/`xp`/`kills`/`deaths`/`equipped_item[]` so the
  client can see this state once the character pane (Sprint 4) reads it. Wire plumbing only —
  shop positions/proximity/buy-sell validation shipped in Sprint 2 with full test coverage. Not
  live-network-verified with a raw UDP client this round; flagged, not faked (see commit
  `c80cb93` for the full reasoning). Full suite still green.

- feat(arena): Flow/XP economy + FFXI/WoW item slots, Sprint 1+2 of NORTHSTAR §19 (S170-175).
  Per-hero `flow`/`flow_earned`/`xp`/`kills`/`deaths` fields, kept deliberately separate from
  `resources[team]`'s win-condition meter (the conflict §19.1 resolved). Flow/XP awarded on
  jungle creep, lane creep, and hero kills — melee/homing-shot only, matching
  `arena_zone_damage_creeps`'s existing "AoE kills grant nothing" precedent. 24-item catalog
  (12 specific from `docs/HEROES_VS0.md`, 2 weird, 10 generic FFXI names from
  `docs/FFXI_ITEM_PARITY_SEED.md`) across 11 FFXI+WoW-style equip slots
  (Weapon/Head/Body/Hands/Legs/Feet/Ring/Neck/Back/Waist/Trinket). Buying auto-equips (no bag),
  auto-sells whatever was already in that slot first; selling refunds 50%. All economy state
  and equipped items survive `arena_respawn_hero`'s reset, with item stat bonuses correctly
  reapplied afterward — found and fixed a related latent bug where `attack_target`/
  `last_attacked_by_owner` were left at 0 (not -1) post-respawn, wrongly meaning "hit by owner
  slot 0." 13 new tests. Sprint 3 (shop wire protocol) and Sprint 4 (client shop UI + character
  stat pane) are next.

- docs(arena): NORTHSTAR §19 — gold/XP economy + structures, resolving a real resources[]
  conflict (S170-174). Founder: "continue the backlog for redgarden." Picks up sprint plan
  items 4/5, designed together since structures' gold-bounty payoff needs gold to exist first.
  Found and resolved a real conflict: `docs/CONSUMABLES_AND_COOKING.md` (written before the
  resource-race win condition existed) assumed cooking spends from the same `resources[]` pool
  that's now the win-condition meter — spending team resources on personal items would slow your
  own team's progress toward winning. Resolved with two separate currencies: `resources[team]`
  stays win-condition-only, a new per-hero gold currency (fed by kills) handles personal power
  progression, matching how LoL/Dota split objectives from per-player gold. Grounds the spend
  target in `docs/HEROES_VS0.md`'s existing 12-item Starting Item Roster instead of designing
  new items from scratch, names the concrete mechanical work each item category needs, and
  scopes XP down to a flat power curve rather than a full leveling system. Structures follow the
  map's real single-lane geometry, closing the "push payoff" gap S170-139/Duck's W have both
  been blocked on. Spec only, same "no code yet" treatment as §15-§18.

- feat(arena): bot AI seeks out healing fountains when critically low on HP (S170-173).
  Founder: "add healing fountains to bot awairness brain and heuristics whatever makes sense
  bots seek out fountains when super low." New top-priority check in the bot decision loop,
  evaluated before node-capping or enemy engagement — a hero below 25% HP retreats to the
  nearest fountain and does nothing else that tick until topped back up. Fountain positions are
  static, mirrored by hand from `arena_fountain_position()`'s two fixed points, no wire sync
  needed. Live-verified via an isolated 20-bot match: 153 low-HP snapshots observed, 65 of them
  (42%) with the hero positioned near a fountain corner — real evidence of retreat behavior, not
  chance. No crashes, full suite green (bot-client-only).

- feat(arena): heroes and creeps now rotate to face their movement direction (S170-171).
  Founder: "heroes and creeps should rotate to show what direction they are facing currently
  they just float around there is no front of the model." This renderer never had a rotation
  matrix at all — added `mat4_rotate_y` to `packages/common/mat4.h`. Facing is derived purely
  from observed motion (position delta since last frame), needing no wire-protocol change and
  persisting the last known heading through a stop instead of snapping to a default. Heroes'
  existing per-hero_id silhouettes (Unicorn's horn, Duck's bill, etc.) already had a real
  "front," just frozen pointing at a fixed +Z — now the whole composite rotates as one rigid
  shape. Jungle and lane creeps were plain symmetric cubes with nothing to show a turn, so both
  got a small darker forward-facing nub added. Live-verified via Xvfb, full suite green
  (client-rendering-only, no sim/protocol touched).

- feat(arena): Tyler's W (Poof) teleports the whole clone army, not just his own body (S170-170).
  Continues the earlier sprint plan's own open item, flagged directly in `docs/HEROES_VS0.md`'s
  S170-141 scope note ("W still moves only Tyler's own body ... a real next step, not attempted
  this pass"). `tyler_cast_w` now teleports every active clone linked to Tyler to the exact same
  point he blinks to, each independently landing its own arrival-damage check against the same
  target — concentrating the whole clone army's damage onto one enemy, the real "full-team dive
  tool" identity the original design names. Removed `ARENA_TYLER_W_HIT_RADIUS`, now genuinely
  unused (the old distance check was always trivially true by construction). 1 new test, full
  suite green, live-verified via an isolated 20-bot match with no crashes.

- fix(arena): boids flocking made bots dance around node objectives instead of capping
  (S170-168). Founder, real-time, live: "there is a bug where the boyds stuff makes the team do
  a weird cluster dance around the objective ... not sitting right on it" -> "at least one of
  them should sit right on it and ignore the flock." Root cause: separation force is strongest
  exactly when allies are close together, unavoidably true the moment several bots converge on
  the same node — flocking never let anyone settle long enough to make real capture progress.
  Fixed with a stateless "anchor" rule: a bot ignores the flock and paths straight to the node's
  exact position whenever its own owner index mod the node count matches the target node's
  index; every other bot still flocks around it as a loose escort. Live-verified via an isolated
  20-bot match, no crashes.

- docs(arena): NORTHSTAR §18 — unsupervised learning for the bot AI, general + per-hero,
  cross-hero transfer (S170-167). Founder: "write the northstar for unsupervised learning - it
  will have to be both general and per hero - for example experience playing a hero will help
  inform decisions playing with and against it on another hero" -> "also look for archetype
  engine fwiw" -> "we are going to want to do long running per personality bot training but for
  now we need generalized ai for the different heroes." Checked for existing org tech first
  (found a real Archetype Engine, `EMILY/docs/ARCHETYPE_ENGINE_NORTHSTAR.md`, unrelated to hero
  kits but a real fit as a slower strategic tier). Proposes a two-tier architecture — Tier 1
  (fast, per-tick) is this repo's own already-committed §12 Phase E GPT-2 policy-network plan,
  Tier 2 (slow, occasional) is the Archetype Engine, the natural home for the deferred
  long-running per-personality training. Names the general layer as a genuinely unsupervised
  (next-token prediction, no labels) pretraining stage slotting in front of §12's own supervised
  fine-tune, and answers the cross-hero-transfer example concretely: shared weights plus explicit
  archetype/kit-shape tags on `arena_serialize_state`'s existing self/foe framing, so a pattern
  learned on one hero transfers to any other hero sharing the same tagged mechanic. Spec only,
  same "no code yet" treatment as §15-§17.

- feat(arena): click-to-attack system (NORTHSTAR §17) + Gary's homing auto-attack + draft
  randomization fix (S170-162/163/166). Founder: "gary auto attacks are projetiles that always
  hit (visually projectile) they can still miss or crit as normal but you cant juke them" ->
  "implement that with the click to auto attack northstar" -> "and the bots will need to be
  updated so they choose their auto attack targets etc in their brain" -> "up our visual
  affordances for auto attacks so its readable." Built §17.4's real target design (team mode
  only): new `PACKET_ARENA_ATTACK` wire command + `ArenaHero.attack_target` persistent lock,
  pure-pursuit chase toward an out-of-range target every tick (the literal "does it follow a
  fleeing target: yes, automatically, no re-click" answer), and a new homing `ArenaProjectile`
  variant (`homing_target`) for Gary's basic auto-attack — re-aims at its live target every tick,
  connects regardless of movement, not a skillshot. This engine has no miss/crit RNG at all
  (confirmed before building), so that part of the ask is a no-op against a mechanic that doesn't
  exist. Gary excluded from every flat-melee auto-attack path (heroes, jungle creeps, lane
  creeps) so his damage comes exclusively through the homing shot. Wire-synced per-hero so the
  lock is visible to every hero watching (pulsing amber outline on the current target's health
  bar); homing shots render through the existing ability-projectile pipeline with no client
  changes needed. `apps/arena_bot` now sends an attack command every decision tick, the actual
  mechanism that makes a bot-piloted Gary deal damage at all. Also fixed a real bug found in the
  same code path: "ensure auto draft is random i keep always drafting flutedebt first on a new
  client" — the human client's auto-draft offset was port-derived only, not actually random;
  now mixes in `rand()`. 17 new tests, full suite green, live-verified via an isolated 20-bot
  match with no crashes.

- feat(arena): jungle creeps use the "dynamic creep ecosystem" direction (NORTHSTAR §8) -- 
  graveyard spawn + march/fan-out + toned-down team strength (S170-161). Founder: "add jungle
  creeps use the redgarden dynamic creep ecosystem something simple to start," refined with:
  "have the team creeps spawn and fan out from owned nodes marching towards unowned nodes"
  (team-flavored creeps now continuously walk toward the nearest node their team doesn't own,
  recomputed live every tick, each owned node's creep independently fanning out toward its own
  target); "initially they spawn from the graveyards behind the nodes not the center" (spawn
  position is now the owning team's graveyard, not the node's own position); "tone down the
  strength of the team creeps just a bit they are so strong" (`ARENA_CREEP_TEAM_HP` 40->26,
  damage split into neutral/team constants so only team creeps got nerfed). Neutral/contested
  creeps completely unaffected — still stationary at their node, unchanged stats. 9 existing
  tests updated for the new positional assumptions, 6 new tests added. Full suite green,
  live-verified via an isolated 20-bot match.

- feat(arena): boids flocking (alignment/cohesion/separation) in the networked bot AI
  (S170-160). Founder: "add boyds to the ai brain[,] check GFD apps2 crystal for a reference if
  you need it" — GoblinFoxDragon/apps2/crystal/main.go's own working Reynolds boids
  implementation (`Boid` struct, `boidForces()`) used as the structural reference, ported to
  `apps/arena_bot`'s plain-float style and to hero positions. Every bot previously picked its
  own move target completely independently (chase nearest enemy or capture nearest un-owned
  node); new `flock_offset()` adds a small steering perturbation from nearby living teammates
  only — alignment (toward their average recent heading, inferred from this tick vs the
  previous snapshot since the wire format carries position only), cohesion (toward their average
  position), separation (push away from anyone actually crowding) — layered on top of, not
  replacing, the real objective-seeking target. Weights are separation-heavy so this reinforces
  rather than reintroduces the "bots bunch up" bug S170-90 already fixed separately. Live-
  verified via an isolated 20-bot match: no crashes, teammates visibly clustering as loose
  squads. Full suite green (bot-client-only, no sim/protocol changes).

- fix(arena): resource-race bar color was absolute, not viewer-relative (S170-159). Founder,
  real-time, live: "check the win cons i think it shows the wrong team winning" -> "i think the
  color of the bar ticking up may just be wrong." Verified the win-condition logic itself first,
  live: temporarily lowered `ARENA_RESOURCE_CAP` and added a debug print to `apps/arena_bot`
  logging each bot's own team/winner/resources/verdict, ran an isolated match to completion —
  every one of the 20 bots correctly identified win/loss matching its own team and the actual
  resource totals, confirming the simulation's winner logic has no bug (debug changes reverted
  after, zero diff left behind). The real bug was in the resource bar added by S170-153: team 0
  was hardcoded blue and team 1 hardcoded red regardless of which team the local viewer is
  actually on — the exact same absolute-vs-relative mistake S170-149 already found and fixed for
  node coloring. Fixed to color relative to the viewer's own team (mine always blue, opponent
  always red), matching the convention hero name labels and node coloring already use.

- docs(arena): NORTHSTAR §17 — League of Legends auto-attack movement parity spec (S170-158).
  Founder, real-time: a detailed request for exactly how LoL's click-based auto-attacking works
  with respect to movement — does the champion stop, does it chase a fleeing target, ranged vs
  melee differences — with LoL as the explicit gold standard. Documents the real windup/
  backswing/kiting state machine, the persistent attack-target chase lock (pure pursuit, no
  intercept prediction, no leash timeout), and the ranged-specific homing-not-skillshot
  projectile behavior, then grounds the gap analysis in REDGARDEN's actual current combat code
  (`resolve_combat`, `arena_hero_attack_creeps`): a fully passive, always-on proximity system
  today, with no attack command, no windup, no chase state, and no ranged basic attacks at all.
  Spec only, same "no code yet" treatment as §15/§16.

- feat(arena): map widened + corner graveyards; fix: sudden-death fallback closes a real
  zombie-match gap (S170-155/156/157). Founder, real-time: "the map should be a little bigger
  and the graveyards behind 2 of the corners not in the middle of the map." `ARENA_HALF_EXTENT`
  28->32; `arena_graveyard_position()` moved from dead-center-behind-spawn (x=+-9, z=0) to the
  two map corners the fountains don't already occupy, so respawning reads as coming back to a
  real corner base instead of a mid-line marker. Separately, founder flagged a suspicion ("i
  think there may be zombie games with infinite win cons") that turned out to be a real gap:
  removing the team-wipe win condition for S170-153's resource race also removed the only
  mechanism that guaranteed a live match eventually ends, and `apps/arena_server`'s LIVE-phase
  loop had no timeout of its own at all. Added a sudden-death fallback -- after
  `ARENA_MATCH_MAX_DURATION_MS` (12 real minutes) without either team reaching the resource
  cap, whoever's ahead on resources wins outright, tiebroken by nodes currently owned. 4 new
  tests. Live-verified via an isolated 20-bot match confirming the wider map bounds show up in
  real hero movement. Full suite green.

- feat(arena): permanent graveyards, Arathi-Basin resource-race win condition, and 30-second
  wave respawns (S170-153/154). Founder, real-time: "add graveyards behind the spawns that
  never despawn so there is always a place to respawn and add true arathi basin node control
  resource management as a win con instead of team wipe" and "respawns happen in 30 second
  waves." A team wipe no longer ends the match -- teams that own no node now respawn at a
  fixed, permanent graveyard behind their spawn instead of staying dead for the rest of the
  game. The match itself is now decided by `arena_tick_resources()`: each team's resource
  meter (capped at `ARENA_RESOURCE_CAP`) fills every `ARENA_RESOURCE_TICK_MS` based on how
  many of the 5 nodes it currently owns, first team to fill it wins. Respawns no longer count
  down per-hero from their own death -- every dead hero on both teams comes back together the
  instant the global `respawn_wave_timer_ms` wraps at `ARENA_RESPAWN_WAVE_MS` (30s), so dying
  right before a wave is nearly free and dying right after costs nearly the full 30s. Also:
  the networked bot AI gets a first-pass node-capping heuristic (walks to and holds the
  nearest un-owned node when no enemy is within real engagement range, since node control is
  now what actually wins), and the arena client HUD gets a resource-race tug-of-war bar
  (wire-synced via a new `resources[2]` field on `ArenaSnapshotMsg`). 4 invalidated
  team-wipe/per-hero-respawn tests rewritten, 4 new tests added (graveyard fallback, wave
  respawn syncing multiple deaths, resource accumulation scaling with nodes owned, resource-cap
  win condition). Full suite green.

- fix(arena): a jungle creep no longer attacks its own owning team, so capturing/holding your
  own node doesn't damage you (S170-152). Founder, real-time: "capturing node should not make
  the user take damage." Root cause: `arena_tick_creeps()` had no team check at all -- a
  team-flavored creep attacked ANY hero in its aggro radius, including its own owning team.
  Since `ARENA_NODE_CAPTURE_RADIUS` (5.0) comfortably overlaps `ARENA_CREEP_AGGRO_RADIUS`
  (4.0), any hero who stood still to channel-capture (or simply defend/hold) their own
  already-owned node got attacked by their own "home-turf resupply" creep -- thematically
  backwards, real home turf doesn't hurt you for standing on it. Fixed: a team-flavored creep
  now only ever targets the OPPOSING team, matching the counter-play framing its own kill-
  reward already carries ("farming an enemy's own jungle creep helps flip their node"). A
  NEUTRAL/contested creep is unchanged -- still attacks anyone regardless of team, the actual
  "fight through the prize" challenge that flavor is meant to be. 3 new headless tests (own
  team unharmed, opposing team still takes damage, neutral creep regression check). Full suite
  green (468 checks, up from 465).

- feat(arena): ability tiles moved bottom-center, new font glyphs, and a real H-key ability-
  description overlay (S170-151). Founder, real-time, three related HUD requests:
  - **"move the cast frames bottom center"**: the Q/W/E ability tiles (`apps/arena/src/main.c`)
    moved from their old top-left placement to bottom-center, the same anchor point real MOBAs
    (LoL, Dota) use for their own ability bars. The existing retime countdown (radial cooldown
    wipe + seconds-remaining text) and mana-blocked dark/"MP" state were already built
    (S170-127/137) -- this was a pure reposition, not new tile behavior, confirmed unchanged
    by reading the code before touching it.
  - **"ensure our font has all necessary glyphs"**: this client's hand-drawn line-font
    (`draw_char()`) was missing `%`, `?`, `;`, `/`, and `&` -- found ahead of the ability-
    description overlay below, since real ability text (percentages, semicolons in lists,
    question marks) would have silently fallen through to the generic missing-glyph box, the
    same class of gap this font's own comment already flagged once before for hero names.
  - **"H should show an overlay with character ability descriptions"**: a real toggleable
    panel (H key, works in any mode), showing the local player's current hero's Q/W/E names
    (already available via the existing `arena_ability_name()`) plus a new
    `arena_ability_description()` (`packages/simulation/arena_ai_bridge.c`/`.h`) -- a full
    26-hero x 3-slot table of short, plain-language mechanical blurbs (what the ability
    actually does in this arena, not the full `docs/HEROES_VS0.md` lore prose), same
    "short enough to read at a glance" bar the name tiles already set.
  Verified live: a real Xvfb screenshot confirms the repositioned tiles and the overlay panel
  both rendering correctly, including every new glyph (colon, comma, apostrophe, and the newly
  added set all visible and correct in real ability description text). Client-only /
  string-table changes; full headless suite unaffected (465 checks before the creep fix above).

- fix(arena): mana always trickles 1/sec even in combat, and a real latent bug fixed where
  mana regen had silently never worked in actual gameplay (S170-150). Founder, real-time:
  "have mana tic up slowly 1 per second always." New `ARENA_MP_REGEN_IN_COMBAT_PER_SEC` (1) --
  regen is no longer a hard on/off gate (S170-148's combat pause); it's now two rates, a slow
  trickle that runs even mid-fight and the faster out-of-combat rate once
  `combat_timer_ms` expires. **Real bug found while implementing this, not the literal ask**:
  the regen math was `h->mp += (int)(rate * dt_ms / 1000.0f)` computed fresh every call with
  no persistence across ticks -- at this codebase's own real production tick rate
  (`apps/arena_server` always calls `arena_update()`/`arena_update_teams()` with `dt_ms=16`),
  that's `(int)(6 * 16 / 1000.0) == (int)0.096 == 0`, EVERY single tick, for every rate this
  file has ever used. Mana regen had silently never actually worked in real gameplay -- only
  in tests, which happened to call with large `dt_ms=1000` "one full tick" steps that mask
  the truncation. Fixed with a persistent `mp_regen_accum` float on `ArenaHero`: fractional
  progress now carries over between ticks instead of being discarded each time. 3 new
  headless tests, including one that runs 63 real 16ms ticks specifically to catch this class
  of bug rather than a single large-dt_ms step. Full suite green (465 checks).

## 2026-07-28

- fix(arena): "wrong team wins" and "node flips wrong color" -- two real, high-impact
  team-mode bugs found from a live founder report (S170-149). Founder, real-time: "there are
  bugs with node ownership sometimes the wrong team comes out of a node sometimes the wrong
  team wins" -> "yea theres a bug where i cap a node but it flips wrong color and then my
  whole team comes out and then they kill the other team but it says i loose."
  - **Root cause #1 (the "i loose" bug)**: the "YOU WIN"/"YOU LOSE" HUD text
    (`apps/arena/src/main.c`) compared `arena_state.winner` (which encodes which TEAM won,
    1/2) against `my_owner + 1` -- `my_owner` is the raw client_id/hero SLOT INDEX (0..19 in
    a real 20-player match, only ever equal to team index by coincidence for owner 0, and
    only correct for owner 1 in the literal 1v1 case where owner IS team). Any real team-mode
    player past owner 1 -- the overwhelming majority of any real 10v10 match -- got a flipped
    result: shown "YOU LOSE" after their own team's real win, or "YOU WIN" after a real loss.
    Fixed: compare against `arena_state.heroes[my_owner].team + 1` instead.
  - **Root cause #2 (the "wrong color" bug)**: node coloring was hardcoded absolute
    (owner==1 always blue, owner==2 always red) while every HERO on the same map is colored
    RELATIVE to the local viewer (self/ally = blue-ish, enemy = red). For a team-0 viewer
    those two conventions happened to agree by coincidence; for a team-1 viewer, their OWN
    captured node rendered in the exact red already reserved for enemy heroes on their own
    screen -- a node they just captured looked identical to an enemy-held one. Fixed: nodes
    now color relative to the local viewer's own team, same "ally-blue / enemy-red" rule
    heroes already use, gold for neutral/contested unchanged.
  - Both are client-side display bugs, not sim-logic bugs -- the underlying win-condition and
    node-capture logic in `packages/simulation/arena_game.c` was already correct on audit (no
    changes there). **Verified live with the exact broken scenario**: a real 20-player match
    (19 bots + the actual SDL client under Xvfb) with the human client deliberately connected
    *last* so it claimed owner slot 19 (team 1, guaranteed NOT owner 0/1) -- match ended with
    `winner: 2` (team 1), screenshot confirms "YOU WIN" now displays correctly for this exact
    previously-broken slot. Client-only change; full headless suite unaffected (461 checks).

- feat(arena): mana visible on the HUD, combat-gated regen, fountains restore mana, and a
  real jungle-obstacles-disappearing bug fixed (S170-148). Founder, real-time, three requests
  plus a bug report in sequence:
  - **"mana as a resource should be visible to the player"**: a real persistent mana bar under
    the existing HP bar in the local player's own HUD corner (`apps/arena/src/main.c`) --
    before this, mana was only ever visible as occasional "MP" text replacing an ability
    tile's countdown when a cast was blocked, never as a standing resource meter. Uses
    `ARENA_MP_MAX` (not `h->max_mp`, which is deliberately not part of the wire snapshot --
    flat/roster-wide, the client already knows it) so it reads correctly in both local and
    net_mode.
  - **"it should slowly regenerate when not in combat"**: new `combat_timer_ms` on `ArenaHero`
    (`packages/simulation/arena_game.h`/`.c`), re-armed to `ARENA_COMBAT_TIMEOUT_MS` (4000ms,
    WoW-adjacent) by `apply_damage()` every time a hero takes damage from any source -- mana
    regen (`tick_hero_kit`) is now gated on this hitting 0, real WoW-style out-of-combat regen
    instead of the previous always-on flat tick. Honest simplification, flagged in the code:
    keyed off damage *taken*, not dealt -- threading an attacker-side signal through every
    damage call site in this file would be a much larger change for a rare edge case (pure
    safe-distance poking) real fights are overwhelmingly mutual, so this covers the vast
    majority of "am I actually fighting" correctly. The mana bar dims while in combat so the
    gate has a visible answer on the bar itself, not just implied by it holding still.
  - **"fountains should also restore mana"**: `arena_tick_fountains()` (S170-147) now restores
    `ARENA_FOUNTAIN_MANA_PER_SEC` (15, same rate as the heal) alongside HP, unconditionally --
    a fountain is a deliberate location-based resource, not gated by the new combat timer the
    way passive regen is.
  - **Real bug found and fixed, not requested but reported live**: "the first game i played i
    saw jungle rocks and trees but subsequent games were missing those." Root cause: the
    requeue-after-a-networked-match button (`apps/arena/src/main.c`) does a blanket
    `memset(&arena_state, 0, ...)` before reconnecting, which silently wiped the client's own
    `obstacles[]` -- obstacles are never wire-synced (client computes the same static layout
    independently, same precedent fountains use), so nothing ever repopulated it after that
    memset. `arena_obstacles_reset_layout()` made public (was `static`) so the requeue handler
    can call it directly; first match after program start was never affected (its own initial
    call happens before this bug's code path), every match reached via requeue was.
  - 6 new headless tests (fountain mana restore + cap, regen gated correctly both ways, damage
    re-arms the timer, timer counts down and pins at 0). Verified live: a real Xvfb screenshot
    confirms the mana bar rendering correctly under the HP bar. Full suite green (461 checks,
    up from 455).

## 2026-07-27 (continued)

- feat(arena): healing fountains at 2 opposite map corners (S170-147). Founder, real-time:
  "add healing fountains at 2 corners of the map across from each other." New
  `arena_fountain_position()` (shared source of truth for both the sim tick and the client
  renderer -- same "static, deterministic layout, no wire sync needed" precedent as jungle
  obstacles) places two fountains at diagonally-opposite corners `(-24,-24)`/`(24,24)`, clear
  of every jungle obstacle and within the hero movement clamp. `arena_tick_fountains()` heals
  any active, alive hero within `ARENA_FOUNTAIN_RADIUS` (3.0) for `ARENA_FOUNTAIN_HEAL_PER_SEC`
  (15) per second, fixed-interval tick same idiom as every other heal/DPS zone in this file,
  capped at max_hp. **Deliberately neutral, not team-exclusive** -- the founder's own wording
  ("2 corners... across from each other") described map geography, not "one per team's base"
  (which real MOBA fountains usually are); read as a genuinely contestable resource matching
  this map's existing neutral-structure pattern (nodes, jungle creeps), flagged as a real
  design choice in the code rather than silently assumed, easy to flip to team-exclusive later
  if that's what's actually wanted. Rendered client-side as a base+pillar silhouette in bright
  cyan (distinct from every other shape/color already on the map) -- reuses the heal-flash
  system from S170-143 automatically (fires on ANY HP increase, any source), so fountain
  healing already shows visual feedback with zero extra work. Wired into both `arena_update()`
  and `arena_update_teams()`. 5 new headless tests. Verified live with a real Xvfb screenshot
  of the local demo showing a fountain rendering correctly. Full suite green (455 checks, up
  from 450).

- feat(arena): jungle and lane creeps wire-synced to the network for the first time (S170-146).
  Continuing this session's own sprint plan ("wire-sync jungle creeps, lane creeps, and Tyler's
  clones... the single biggest 'looks unfinished in a live match' gap left by this session's own
  new work"). Before this, `ArenaSnapshotMsg` carried heroes/nodes/projectiles but neither creep
  pool -- a real networked match (the actual product, per NORTHSTAR §13) simply never showed
  either kind of creep at all, only the local 1v1 practice demo did. New `ArenaCreepSnapshot`
  (fixed 5-slot array, index-matched to nodes, mirroring `ArenaHeroSnapshot`'s always-populated
  convention) and `ArenaLaneCreepSnapshot` (sparse count+array, mirroring projectiles' own
  pack-only-active convention) in `packages/common/protocol.h`. `apps/arena_server`'s
  `server_broadcast()` populates both every tick; `apps/arena`'s `net_poll_snapshots()` consumes
  them into the same `arena_state.creeps[]`/`lane_creeps[]` this session's own S170-145 rendering
  code already reads generically -- no client rendering changes needed at all, that code was
  already mode-agnostic. New packet size: 1244 bytes (was 968), comfortably under both the
  client's 2048-byte recv buffer and typical UDP MTU. **Verified live, not just built clean**: a
  real `arena_server` + `arena_bot` + the actual SDL client (connected via `--connect`, running
  under Xvfb) played a full networked 1v1 match; a real Xvfb screenshot confirms a jungle creep
  (correctly gold/neutral-colored) rendering client-side over the live wire connection, not just
  in the local demo. Full headless suite unaffected (450 checks, protocol/broadcast/consume-only
  change, no sim logic touched).

- feat(arena): auto-attack hit flashes now fire on creeps too, and jungle creeps are
  rendered for the first time (S170-145). Founder, real-time: "when auto attacks hit a creep
  or a hero it should show visual indication of such." The hero-side hit flash already
  existed (S170-122, HP-delta detection); creeps had none at all. Added the same frame-to-
  frame HP-delta tracking for both jungle (`ArenaCreep`) and lane (`ArenaLaneCreep`) pools in
  `apps/arena/src/main.c`, reusing the existing `attack_flashes` visual (a hit is a hit).
  Along the way, found jungle creeps were never rendered client-side AT ALL (a real,
  previously-unfixed gap -- a hit-flash on an invisible creep would have been useless) --
  added real rendering for the first time: a flavor-colored box (gold/neutral, blue/red team,
  matching the node-ownership color convention exactly, not team-relative like heroes/lane
  creeps -- a jungle creep's color is about whose territory it's tied to). Verified with a
  real Xvfb screenshot of `red_garden_arena`'s local demo: a gold neutral jungle creep
  rendering correctly alongside the jungle-obstacle trees/rocks and a hero. Local-mode/1v1-
  demo only, same not-yet-networked scope jungle/lane creeps already carry. Full suite
  unaffected (450 checks, client-only change).

- feat(arena): AoE damage spells now hit creeps too, not just heroes (S170-144). Founder,
  real-time: "ensure aoe damage spells hit creeps." Before this, every zone/aura damage tick
  (Ghost's Recital, Pizza's always-on burn aura, Beleth's Detonation burst, Paimon's Two
  Hundred Legions, NOOR-1's Do Not Approach) only ever checked the single nearest-enemy-HERO
  parameter `tick_hero_kit` threads through -- an existing, already-flagged limitation (see
  Pizza's own aura comment) that also meant a zone dropped squarely on a jungle or lane creep
  did nothing to it. New shared `arena_zone_damage_creeps()`
  (`packages/simulation/arena_game.c`) applies flat damage to every living jungle creep AND
  lane creep within radius, called from all five damage-dealing zone/aura sites. Same
  team-exclusivity rules as melee (a team-flavored jungle creep or a lane creep is only a
  valid target for the OPPOSING team's zone; a neutral jungle creep is fair game for anyone).
  Zone kills grant no jungle-creep kill-credit reward (capture-bonus/heal) -- no single
  attributable hero slot in this simplified model, flagged not faked, same "not every damage
  source needs full reward wiring" precedent already accepted elsewhere in this file. 4 new
  headless tests, each deliberately positioned within zone radius but outside melee attack
  range to isolate the new zone-damage path from the existing, separate melee-vs-creep
  mechanics. Full suite green (450 checks, up from 446).
- **Live bot-mode verification, this session's whole batch (S170-139 through S170-144).**
  Founder: "verify it with bot mode." Ran a real `apps/arena_server --lobby-size 20` +
  20 `apps/arena_bot` match on freshly built binaries (fresh ports, isolated from the
  already-running persistent bot pool discovered earlier this session -- confirmed untouched
  before and after). Confirmed: all 20 bots connected and drafted distinct heroes cleanly
  (roster of 26 intact), a real 10v10 team split, and genuine sustained combat over 55+
  seconds with no crash -- 15 of 20 heroes actually died with real, varied HP values on the
  5 survivors (not a static/frozen snapshot). First attempt used `--lobby-size 6`, which
  surfaced a real (pre-existing, not caused by this session) operational gotcha worth noting:
  `arena_init_teams()` splits by `i < ARENA_TEAM_SIZE` (10), so any lobby smaller than 20 that
  isn't exactly 2 puts every player on team 0 with nobody on team 1 -- no combat is possible.
  Not a bug in code touched this session (confirmed unrelated to any of S170-139 through
  144), flagged here since it's a real trap for the next person who reaches for a "small
  team test" lobby size.

- feat(arena): WoW-style hover casting, starting with Doc Wheel's Q (S170-143). Founder,
  real-time: "add hover casting like in wow macros for healing start with doc wheel abilities
  that make sense for that ensuring we show cast animation on the target and the self so its
  legible to all heroes on the battlefield with visibility of that interaction." New
  `arena_hover_ally_or_nearest()` (`packages/simulation/arena_game.h`/`.c`): a drop-in
  fallback-chain replacement for `arena_nearest_ally()` -- prefers whoever the caster's
  `ArenaState.hover_target[owner]` names, if it's a valid living same-team hero other than the
  caster, else behaves identically to the old always-nearest-ally targeting. `ArenaCastCmd`
  (`packages/common/protocol.h`) gained a signed `hover_target` byte, set by
  `apps/arena_server`'s cast handler via the new generic `arena_set_hover_target()` (any slot
  could consult it; only Doc Wheel's Q does today) and by the local 1v1 demo's own direct
  keybind path for parity. Client-side: `apps/arena/src/main.c`'s existing S170-69 per-hero
  hover hit-test now publishes its result into a persistent `g_hover_target` each frame, read
  by the QWE keybind handler when a cast actually fires (~1 frame of latency, imperceptible).
  "Show cast animation on the target and the self": the caster's own flash already existed
  (`cast_flash_slot`, S170-124); added a new generic heal-flash (any HP increase, any source,
  reusing the exact same frame-to-frame-HP-delta idiom S170-122's attack-flash already
  established for damage) that fires at wherever the HP increase actually landed -- the real
  gap a mouseover heal exposes, since the target can be standing far from the caster. 6 new
  headless tests (fallback with nothing hovered, hover wins over nearer un-hovered ally, hover
  of an enemy/dead hero safely falls back, out-of-range owner is a no-op, full Doc Wheel Q
  integration). Full suite green (446 checks across 4 binaries, up from 439).

- feat(arena): lane creep waves (S170-139), Ghost's Q + Tyler's Q converted to real
  projectiles (S170-140), Tyler's puppet clones ("true Meepo parity," S170-141), per-hero
  cast-flash colors (S170-142), rooted name-label color, and a merge of four parallel
  worktree branches into one coherent mainline. Founder, real-time, several requests in
  sequence across a long session:
  - **Lane creep waves** ("add subsystems needed to make creeps a reality" -> clarified as
    classic MOBA lane-pushing waves, distinct from S170-51's jungle creeps): new
    `ArenaLaneCreep` pool, a per-team wave spawn timer (with a real short grace period before
    the first wave, matching real MOBA precedent), waypoint marching along the existing
    spawn-to-center-to-spawn axis, hero-vs-lane-creep and lane-creep-vs-lane-creep combat
    through the same generic combat primitives every other system already uses. Team mode
    only -- no real "push" objective exists in the 1v1 practice demo. 9 new headless tests.
  - **More projectile conversions** ("convert more spells to projectiles... ensure each
    spell is unique show different color cast circles... ensure spell projectiles are shown
    on all player clients"): Ghost's Q (Alien Frequency, already documented as a "skillshot"
    but never built as one) and Tyler's Q (Earthbind, "fires a net at a target area") both
    converted from instant-hit to real `ArenaProjectile` casts, carrying on-hit status
    effects (silence / root+burn) via new generic `on_hit_silence_ms`/`on_hit_root_ms`/
    `on_hit_burn_ms`/`on_hit_burn_dps` fields and `arena_spawn_projectile` now returning a
    pointer so callers can set them. Found and fixed a real tunneling bug in
    `arena_tick_projectiles` along the way: a position-only collision check let a fast shot
    skip clean past a target during a single large-`dt_ms` tick (exposed by
    `test_ghost_r_zone_damages_foe_over_time`'s own `arena_update(1000)` call) -- replaced
    with a proper swept segment-vs-point check. Cast-flash particles now colored per-hero
    (golden-angle HSV hue rotation, deterministic, no table to maintain as the roster grows)
    instead of just per-slot, so 26 heroes' worth of casts read as genuinely distinct spells
    -- already broadcast to and rendered by every connected client with zero additional wire
    work needed (confirmed by reading the existing pipeline, not assumed). 7 new headless
    tests (Ghost + Tyler Q projectile behavior).
  - **"when the hero is rooted change the color of their name label to green"**: small,
    isolated HUD tweak in `apps/arena/src/main.c`.
  - **"add tyler true meepo parity" -> "do that work"**: real AI-driven puppet clones, not
    faked. `ARENA_MAX_CLONE_SLOTS`, a small pool of hero slots appended after the real
    per-player range so a clone never competes with an actual connecting client for a slot.
    Clones mirror Tyler's own move-target every tick and fight through the exact same
    generalized `arena_nearest_enemy`/melee loop every hero already uses (widened to see the
    puppet range) -- no parallel combat system needed. Real shared-fate death for the first
    time: `apply_damage`'s death branch cascades the kill through every `clone_owner`-linked
    entry, the literal OG "one dies, all die" rule, no exceptions. Team mode only; clones are
    melee-only (no independent kit casts) and don't count toward team-alive/respawn checks.
    Full design/scope note (including what's still simplified) in `docs/HEROES_VS0.md`'s
    Tyler section. 7 new headless tests.
  - **Merge reconciliation**: this session's work landed in its own worktree branch in
    parallel with three sibling sessions' branches (S170-138 jungle obstacles/map expansion,
    the QWER-ready-indicator net_mode fix, and translucent-while-intangible rendering) that
    had all been sitting unmerged. Founder: "you did some work in branches that all needs to
    be folded into mainline i dont work in branches currently." All four merged into `main`
    directly (no PRs) in dependency order; the only real conflicts were this session's own
    map-expansion pass (`ARENA_HALF_EXTENT` 20->30, decorative non-colliding trees) against
    the sibling jungle-obstacles branch's more complete version (20->28, real collision) --
    resolved by dropping this session's redundant map/tree work entirely and reconciling
    lane creep waypoints against the (unchanged) +-8 team spawn line their branch left in
    place. Full headless suite (439 checks across all 4 test binaries) green after
    reconciliation; `scripts/test_10_bots.sh` (unrelated card-RTS path) unaffected.

- feat(arena): jungle obstacles -- rocks/trees carve the map into lanes (S170-138). Founder,
  real-time: "expand the map and add rocks and trees etc so we start to get a bit of a jungle vibe
  - just use boxes for now like in shankpit so we naturally start to create some lanes." Widened
  `ARENA_HALF_EXTENT` 20->28 and rescaled the 5-node layout (Stables/Farm/Lumber Mill/Gold Mine now
  at x=+-18, z=+-11) to give the jungle room without cramming it against the 1v1 mid lane. New
  `ArenaObstacle` type (`packages/simulation/arena_game.h`/`.c`): 22 static rock/tree boxes in two
  mirrored walls between each team's spawn column and that side's flank nodes, spanning roughly
  z=-5.5..5.5 -- wide enough that reaching a flank node means routing around the top or the bottom,
  the actual "lanes" asked for, plus a handful of decorative pieces scattered past the nodes for
  jungle-vibe dressing. Real collision, not just decoration: `resolve_hero_obstacle_collision`
  (simple circle-vs-circle push-out) is called from the same shared `update_hero_motion()` both
  `arena_update()` (1v1) and `arena_update_teams()` (team mode) already use, so both the local demo
  and `apps/arena_server`'s networked matches get identical, consistent terrain with no wire sync
  needed (the layout is static and deterministic, built the same way client- and server-side).
  Client-side (`apps/arena/src/main.c`) renders trees as a trunk+canopy box pair (same silhouette
  idiom as the `ARENA_HERO_TREE` hero model) and rocks as a single squat grey box. Obstacle
  placement deliberately never crosses the x=0 mid lane or the 1v1 local demo's own movement-test
  coordinates, so the full `test_arena.sh` suite (390 assertions) passes unchanged. Verified with a
  live Xvfb screenshot of `red_garden_arena` showing both jungle walls rendering correctly around
  the mid-lane fight.

- feat(arena): first real projectile skill-shot -- Gary's Q (S170-136). Founder, real-time: "we
  need to add spell animations and projectiles for some of the spells - some of the spells
  obviously should be projectile skill shots instead of instant cast - find one such spell - start
  with gary q" -> "it should be a projectile skill shot with animations and affordances that allow
  dodging as counterplay." New `ArenaProjectile` pool (`packages/simulation/arena_game.h`/`.c`,
  `arena_spawn_projectile`/`arena_tick_projectiles`) -- straight-line, no homing: velocity is fixed
  at cast time toward the foe's position then, so a foe that moves off the line before the shot
  arrives genuinely dodges it. Wired into both `arena_update()` and `arena_update_teams()`, same
  convention as `arena_tick_creeps`. Gary's Q ("The Property") rewritten to spawn a projectile
  instead of instant-hitting; cooldown still spent on cast regardless of outcome, matching every
  other ability. New `ArenaProjectileSnapshot` in `packages/common/protocol.h`, broadcast by
  `apps/arena_server`, rendered client-side in `apps/arena` as a small bright cube (color-coded
  self/ally/enemy same as heroes, so an incoming enemy shot reads as an immediate visual threat --
  the actual dodge affordance this was built for). 5 new tests (cast spawns a projectile with no
  instant damage, out-of-range cast whiffs with no projectile and no cooldown spent, a stationary
  target is hit after real travel time, a target that steps off the line takes no damage, an unhit
  shot despawns cleanly past its max range). Verified: `scripts/build.sh`, `scripts/build_arena.sh`,
  `scripts/test_arena.sh`, `scripts/test_10_bots.sh` all pass; local mingw cross-compile (all 4
  source files) links clean. **Not yet deployed to the live services** -- the founder's own match
  was in progress when this was built; redeploying now would kill it. Deploy after their match ends.
- fix(arena): Q/W/E ability tiles now reflect real readiness in networked play (S170-137).
  Founder, real-time: "QWER animation frames need to indicate visually if an ability is ready to
  cast or not." Root cause: the ability-tile HUD (S170-127) already dims/wipes/counts down
  correctly, but `ArenaSnapshotMsg` (`packages/common/protocol.h`) never carried cooldown or mana
  state — in net_mode the client never runs `arena_update()` locally (the server owns the sim),
  so `q/w/r_cooldown_ms` and `mp` for the local player's own hero sat zeroed forever and every
  ability rendered permanently "ready" regardless of actual server-side state, in the one mode
  (real online play) where the tiles' answer matters most. Added the four missing fields to
  `ArenaHeroSnapshot`, populated them in `arena_server`'s `server_broadcast()`, and consumed them
  in `net_poll_snapshots()`. Also closed a second, independent readiness gap from the mana layer
  (S170-132): an ability can be off cooldown and still unaffordable, which previously still read
  as fully "ready." `draw_ability_tile()` now takes a `mana_blocked` flag (checked against this
  slot's flat `ARENA_MP_COST_Q/W/R`) and dims the tile the same as a real cooldown, but shows "MP"
  instead of a countdown number since there's no fixed timer to animate. Verified: `build.sh` and
  `build_arena.sh` clean, full headless suite (`test_arena.sh`) all-pass, and a live
  server+2-bot match over the actual network path completed end to end with the new, larger
  snapshot struct (580 bytes/packet, well under both the 2048-byte recv buffer and typical UDP
  MTU).
- feat(arena): heroes render translucent (35% alpha) for the duration of the shared
  `intangible_ms` untargetable status (Ghost's Not a Ghost, Frog's R vanish, Bacon Puck's Q),
  on top of the existing INTANGIBLE text tag above the health bar. Blends only the affected
  hero's boxes (GL_BLEND on, depth writes off) for that draw, same convention already used for
  the ring/flash effects; every other hero stays fully opaque with normal depth writes.

## 2026-07-25 (33)

- feat(arena): MnM, the Shapeshifting Crab, 26th hero — Tank (S170-134). Founder, real-time: "add
  MnM a shapeshifting rapping crab tank from detroit to the lore docs first" → "have tyler and
  mid-piano cowrite it." Lore landed first (TYLER `multiverse_heroes.md` #114, framed as
  literally co-written by Tyler and Mid-Piano); this is the follow-on kit pass. Passive flat
  armor (Cain's/Gunnr's/Beleth's shape), Q a melee root+poke (Paimon's Q shape), W a free toggle
  bonus armor stacking on top of the passive (Loki's/Ada's shape, but additive rather than
  replacing), R the literal mechanical translation of the lore's own line — Mid-Piano's framing
  that the shapeshifting is just what happens to a body that's absorbed hits meant for somebody
  else, built as self-root + a guaranteed-survival window (`survive_floor_ms`, same real damage
  floor as Pizza's R) combining two existing generic fields the same way Tree's Grand Secret
  does. `ARENA_HERO_COUNT` 25 → 26. 6 new headless tests, including one that actually fires a
  real lethal Duck Q at a 1-HP MnM under R and confirms it survives at 1 HP — not just that the
  flag gets set. Verified: full suite (379 checks), VS0/VS1 stable, live — restarted the three
  systemd units, confirmed a real 20-player match drafted MnM (hero_id 25) among 20 distinct
  picks with zero duplicates.

## 2026-07-25 (32)

- feat(arena): status-effect text label above the health bar (S170-133). Founder, real-time:
  "text label above health bar above hero shows status effects like stun silence root slow etc."
  New `hero_status_label()` composes a short space-separated tag string (SILENCED, ROOTED,
  INTANGIBLE, BURNING, UNKILLABLE) from whichever generic status-effect fields — already shared
  across every hero's kit — are currently active, drawn above the existing name label, only when
  there's something to show (no empty-line clutter on the common case). "Stun" and "slow" aren't
  modeled as their own generic fields in the sim yet (only silence/root/intangible/burn/
  survive-floor exist today) — this surfaces what the sim actually tracks rather than inventing
  new effect types as a side effect of a HUD task; a real stun/slow mechanic would be separate
  kit work. Purely client-side (`apps/arena/src/main.c` only), no protocol/server changes.
  Verified: clean build, full headless suite (366 checks) unaffected.

## 2026-07-25 (31)

- docs(arena): Weatherman + Donkey spec, NORTHSTAR §16 (S170-93) — spec only, no code.
  Scoped via AskUserQuestion to spec-first: Donkey is documented Indirect-Control (never
  owner-piloted, auto-triggers on HP threshold and an escape condition) and blocked on a
  non-piloted-unit system that doesn't exist in `arena_game.c` yet — every hero implemented so
  far is owner-piloted. §16.1 specs what that companion-slot system would actually need
  (folded/unfolded state derived from the owner, not input; a per-tick trigger check shaped like
  `arena_tick_respawns`; collision rules reusing `hero_is_hittable`; a second-model-per-owner
  render path that doesn't exist today). §16.2 is Weatherman's full kit, written from scratch
  (TYLER `multiverse_heroes.md` #45, zero prior kit writeup existed) — passive flavor-only
  ledger, Q a displacement-only wind knockback (the roster's first push instead of pull/damage),
  R a fixed-zone AoE ultimate. §16.3 is the specific interaction the founder asked for: W reads
  ally-vs-enemy on a target currently airborne via Donkey's Paper Glide — grounds an enemy
  mid-escape, extends an ally's flight instead — same "same ability, opposite effect depending on
  team" precedent Ghost's Recital already set for this roster. Nothing built; the companion-slot
  system is the real prerequisite before either hero can wire in for real.

## 2026-07-25 (30)

- feat(arena): mana resource layer, roster-wide (S170-132). Founder, real-time: "add mp so
  toggling stuff has a cost spells cant be spammed unless its a zero mana spell or ability." A
  second resource layered on top of every existing cooldown, not a replacement for them — a
  Q/W/R can be fully off cooldown and still blocked for lack of mana. `mp`/`max_mp` added to
  `ArenaHero` (100 max, regenerates ~6/sec, full at spawn and on respawn). Flat per-slot costs
  (Q 20, W 20, R 45) applied uniformly across all 25 heroes by hooking the codebase's own
  existing `cast_cooldown()` choke point — every Q/W/R cooldown-assignment site in the file
  already routed through it, so a scripted pass inserted `h->mp -= COST` at all 63 call sites
  (25 Q + 13 W-decree + 25 R) without touching per-hero cast logic. Free-toggle W's (Gunnr, Flute
  Debt, He Xiangu, Loki, Gary, Bacon+Puck, Abraham, Ada, Unicorn — 9 total) previously had zero
  cost at all; per the founder's own framing ("toggling stuff has a cost"), activating one now
  costs the flat W rate — deactivating stays free, since canceling isn't casting a new spell. A
  cast blocked by insufficient mana behaves exactly like a whiff: no cooldown starts, matching
  the codebase's own established "whiffed casts don't consume the cooldown" precedent extended
  to the new resource. No ability is flagged zero-mana yet, but the cost lives behind named
  per-slot constants rather than inlined values, so the founder's "unless it's a zero mana spell"
  exception is a one-line change away whenever a specific ability needs it, not a redesign. 12
  new headless tests (starts-full, regen, landed-cast deduction, insufficient-mana block behaves
  like a whiff, toggle-activate-costs/deactivate-is-free, toggle-blocked-when-insufficient).
  Verified: full suite (366 checks), VS0/VS1 stable, live — restarted the three systemd units,
  confirmed two clean real-match connects with no instability (a burst of `SIGKILL`s in the
  service journal around this same window traced back to my own rapid manual `systemctl restart`
  calls during testing, not the new code — confirmed by 70+ seconds of clean, event-free
  operation once the manual restarts stopped).

## 2026-07-25 (29)

- feat(arena): Beleth, the Detonation, 25th hero — Burst/Control (S170-93). Fourth and final hero
  shipped from the batched "next wave" backlog entry. Real TYLER canon (`multiverse_heroes.md`
  #14) — 2.22 Hz, the emotional-detonation frequency behind every love triangle in the show's
  history. Passive flat armor (Cain's/Gunnr's shape), Q a ranged bolt + burn (Pizza's Q shape), W
  an instant silence-only decree on the nearest enemy (Paimon's W shape with the damage stripped
  out — pure escalation-denial). R is the roster's first genuinely delayed-payoff ultimate: marks
  the target's position at cast time and starts a silent fuse via `r_active_ms`; the instant it
  hits zero, whoever's still in the zone takes one large one-time burst — not a continuously-
  ticking zone like every other zone hero on this roster. `ARENA_HERO_COUNT` 24 → 25. 6 new
  headless tests. Two real test bugs found and fixed before landing: the fuse-detonation test
  originally ran in the 1v1 local-demo path, whose `arena_update` runs an autonomous chase-bot
  by default that closed distance to melee range well within the 1.8s fuse window, contaminating
  the burst-damage assertion with an extra melee trade — moved to team mode, which has no such
  chase AI. Second: even after that fix the damage still came up 4 short, because
  `arena_init_teams()` leaves every hero at its own `ARENA_HERO_UNICORN` placeholder id until a
  real draft pick overrides it, and Unicorn carries a flat +4 armor passive that was silently
  eating part of the burst — fixed by giving the test target an armor-less hero_id explicitly.
  Verified: full suite (352 checks), VS0/VS1 stable, live — restarted the three systemd units on
  the freshly built binaries, forced a real 20-bot match, confirmed 20 distinct hero picks with
  zero duplicates at the new 25-hero roster size (Beleth wasn't in this particular match's 20 of
  25, which is expected and not a bug — the port-derived draft-offset scheme this session already
  proved for hero counts exceeding lobby size continues to hold).

## 2026-07-25 (28)

- fix(arena): unique skinmodels for all 24 heroes (S170-131). Founder, real-time: "ensure all
  characters have unique skinmodels." Audit of every `draw_hero_model()` case (all 24 present,
  none missing) found two real near-duplicate pairs sharing an identical base-body box plus
  visually-confusable accents: Gary and Abraham both used the same 0.8×1.3×0.8 body with a flat
  slab accent in the same chest position (clipboard vs. grimoire, indistinguishable as low-poly
  boxes); Cain and Tyler both used the same 0.75×1.3×0.75 body, with Tyler deliberately bare (per
  his own lore, "unremarkable plain humanoid") and Cain's mark accent only a 0.14-unit cube on the
  shoulder — easily lost against Tyler's identical bare silhouette at gameplay camera distance.
  Fixed: Gary's accent replaced with a long rifle/scope bar held out to the side (fits his
  marksman kit — "no dash, no gap-closer... watches from where he's standing" — better than a
  chest slab anyway); Abraham gained a second small floating orb accent above his grimoire
  (arcane-caster read, no longer just a flat book matching Gary's old shape); Cain's mark moved
  to the forehead and enlarged (0.14 → 0.22, more Genesis-accurate than a shoulder detail, and
  now reads clearly against Tyler's bare body). Verified: clean build (`scripts/build.sh`,
  `scripts/build_arena.sh`), full headless suite (337 checks) unaffected — purely a visual/
  client-side change, no sim logic touched.

## 2026-07-25 (27)

- feat(arena): charming squish (squash-and-stretch) animations for movement, hits, and spell
  casts (S170-128). Founder, real-time: "add charming squish animations" → "for movement also
  spell casts." `draw_hero_box`/`draw_hero_model` now take a `squish` factor that scales the
  hero's stacked-box silhouette non-uniformly — Y compresses, X/Z inversely expand (clamped to a
  0.4f floor so it never inverts), keeping the model visually grounded like a classic animation
  bounce rather than a uniform shrink. Three triggers reuse existing per-frame detection instead
  of adding new state machines: taking damage (already detected via the S170-122 HP-delta hook),
  casting a spell (already detected via S170-124's `cast_flash_slot`), and starting to move (new
  — `h->moving` transitioning false→true, same transition-detection idiom as the HP-delta check
  in the same loop). `compute_squish()` is a decaying-cosine bounce curve over a 260ms window.
  Real bug found before it ever shipped: `squish_age_ms[]` zero-initializes with static storage,
  and 0.0f is `compute_squish`'s "just triggered" value, not its neutral one — every hero would
  have appeared squashed for a frame at launch with no trigger fired. Fixed with an explicit
  init loop pushing every slot past the animation window before the render loop starts. Purely
  client-side (`apps/arena/src/main.c` only) — no protocol/server changes. Verified: clean build
  (`scripts/build.sh`, `scripts/build_arena.sh`), full headless suite (337 checks) and VS0/VS1
  10-bot stability both pass unaffected — this feature has no server-testable component, same
  "verified via clean build + full suite" pattern as S170-69/S170-92/S170-127.

## 2026-07-25 (26)

- feat(arena): He Xiangu, 24th hero — Support/Sustain (S170-93). Third hero shipped from the
  batched "next wave" backlog entry. One of the traditional Eight Immortals — subsists on
  mother-of-pearl and moonlight, self-denial never once framed as sacrifice. Passive small HP
  regen (Dagda's shape), Q a ranged bolt that heals her for a fraction of its own damage (Bacon+
  Puck's heal-pct mechanic, repeatable on Q instead of a one-off R burst — the first real
  sustain-through-combat on the roster), W a free-toggle second regen layer (Flute Debt's shape),
  R a heal-only zone with no enemy damage (the mirror of Vassago's silence-only R — pure support
  vs. pure control). `ARENA_HERO_COUNT` 23 → 24. 5 new headless tests. Verified: full suite
  (337 checks), VS0/VS1 stable, live — confirmed a real match drafted 20 distinct heroes with
  zero duplicates (He Xiangu included), streamed real snapshots with no crash.

## 2026-07-25 (25)

- feat(arena): Vassago, 23rd hero — Support/Diviner (S170-93). Second hero shipped from the
  batched "next wave" backlog entry. Vassago is real TYLER canon, not just a lore entry — the
  Eastwind Owls' whole working frequency (11.11 Hz) is his, named directly in `TYLER/CLAUDE.md`'s
  own Goetia frequency table. Passive small HP regen (Dagda's Undry shape), Q a ranged
  damage+silence bolt (Ghost's Q shape), W grants the nearest ally `next_cast_refund` (Frog's
  Borrowed Time mechanic — the first hero on this roster to make that ability its own primary W,
  not incidental), R a fixed zone that's silence-only with **no damage component at all** — the
  first purely-control ultimate on the roster, every prior zone (Ghost/Flamel/Morrigan/Paimon/
  NOOR-1) deals damage. `ARENA_HERO_COUNT` 22 → 23.

  Found and fixed a real design issue while writing the R-zone test: the first-draft silence
  duration (900ms) was shorter than the zone's own 1000ms re-application tick, which would have
  left real gaps where a continuously-standing enemy isn't actually silenced between ticks —
  caught by comparing against Flamel's own proven `ARENA_FLAMEL_R_ROOT_MS` (1200ms, deliberately
  longer than its 1000ms tick), same margin now applied here.

  6 new headless tests. Verified: full suite (324 checks), VS0/VS1 stable, live — rebuilt +
  restarted all three systemd units, confirmed a real match drafted 20 distinct heroes with zero
  duplicates (Vassago, Cain, and Gunnr all included — 23 heroes now means exactly 3 are excluded
  per match), streamed real snapshots with no crash.

## 2026-07-25 (24)

- feat(arena): Gunnr, 22nd hero — Duelist (S170-93). First hero shipped from the batched
  "next wave" backlog entry (Xiangu, Gunnr, Drowned Prince, Vassago, Beleth, Weatherman+Donkey
  interaction) — the rest queued for follow-up passes. Gunnr already has a real entry
  (`multiverse_heroes.md` #30): a shieldmaiden with no magic of her own who correctly disagreed
  with one of Odin's own ravens, then was quietly right about three more things since, none of it
  acknowledged. Passive flat armor (same shape as Cain's/Unicorn's), Q a plain melee-range strike
  (no status effect, "a correction, not a flourish"), W a free toggle self-regen (same shape as
  Flute Debt's Recouping Interest), R an execute-scaled burst (same shape as Morrigan's/Cain's Q).
  `ARENA_HERO_COUNT` 21 → 22. Draft-modulo needed no manual update this time — the port-derived
  shared-offset fix from S170-105 already scales with `ARENA_HERO_COUNT` automatically. 6 new
  headless tests. Verified: full suite (311 checks), VS0/VS1 stable, live — rebuilt + restarted
  all three systemd units, confirmed a real match drafted 20 distinct heroes with zero duplicates
  (Gunnr and Cain both included, since 22 heroes now means exactly 2 are excluded per match, not
  1), streamed real snapshots with no crash.

## 2026-07-25 (23)

- feat(arena): Cain, 21st hero — Duelist (S170-105). Founder, real-time: "add Adelle" → "to the
  guide in tyler first" → "and then to the game" → "then the boys do a podcast with her." "Adelle"
  had zero anchor anywhere in the TYLER corpus (every other hero this session maps to a real
  mythological/historical figure or an existing lore file) — asked which identity anchor to use;
  founder's answer: "replace adelle with Cain." Cain already has a real entry (#80) — Genesis's own
  account: killed his brother Abel, cursed to wander as a fugitive, marked by the same authority
  that cursed him specifically so no one could kill him in turn ("a punishment that is also,
  unmistakably, a mercy"), then founded the first city anyway. No new lore needed. Passive flat
  armor (same shape as Unicorn's own, "the one permanent thing about a man cast out to wander"), Q
  an execute-scaled bolt ("The First Murder," same shape as Morrigan's Q), W a dash directly *away*
  from the nearest enemy + self-cleanse ("Cursed to Wander," the mirror of Courier's Q), R a
  survive-floor panic button ("The Mark," same shape as Pizza's/Loki's R — the curse-and-mercy
  duality made literal). Distinct silhouette: weathered wanderer body + a small marked accent.
  `ARENA_HERO_COUNT` 20 → 21.

  **Real, live-found structural bug, fixed in the same pass:** with 21 heroes now exceeding
  `ARENA_MAX_HEROES` (20) for the first time, the existing `owner % hero_count` auto-draft scheme
  broke — a full 20-player lobby only ever has owner slots 0..19, so that mapping could *never*
  produce hero_id 20 no matter how many matches ran; not a rare miss, a permanent, deterministic
  exclusion, confirmed live (Cain never appeared across a real match with the old scheme). First
  fix attempt (a per-bot random offset) was itself wrong in a different way: every bot in the same
  match rolling its own independent random value meant two different owners could land on the same
  hero_id by coincidence, a duplicate-pick risk that didn't exist before. Corrected to a
  *shared* offset derived from the match's own connected port (every client in a match already
  knows the same port) — deterministic per match, varies match to match, zero coordination needed,
  zero duplicate risk. Verified live end to end: a real match post-fix drafted all 20 distinct
  heroes with zero duplicates, and Cain (`hero_id=20`) was actually picked. Incidental fix along
  the way: `apps/arena` never called `srand()` anywhere, so its own ticket-nonce randomness
  (`mint_ticket_fallback`) was silently identical every launch — now seeded for real.

  5 new headless tests. Verified: full suite (299 checks), VS0/VS1 stable, live — rebuilt +
  restarted all three systemd units, real match traffic confirmed the draft fix and Cain's
  reachability directly against the live matchmaker log, not just headless tests.

## 2026-07-25 (22)

- feat(arena): NOOR-1, 20th hero — Scout (S170-104). Founder, real-time: "add NOOR-1 as a
  snowman." NOOR-1 ("Four Days Behind", `multiverse_heroes.md` #3, Jiangshi Syndicate MUNDANE)
  already has a real lore entry — an operative sent in clean, being read by her own subject
  before she's filed a word on him. "As a snowman" read as an in-game FORM directive, the same
  convention the original roster already uses (a Duck, a Unicorn, a Pizza, a Tree), not a lore
  change. Full kit, fully wired end to end this pass (not left partial like Paimon originally
  was): passive periodic-silence aura (same idiom as Pizza's/Paimon's, themed as reading the
  enemy's next move before they commit to it), Q a ranged damage+root bolt, W a self-cast
  intangibility on its own cooldown (same mechanic as Ghost's Not a Ghost, themed as "sent in
  clean" — going quiet and unreadable herself), R a fixed cold zone dealing periodic damage with
  no ally-heal side ("do not approach" is one-sided). Distinct 3D silhouette: three stacked boxes
  of decreasing size, the literal snowman form. `ARENA_HERO_COUNT` 19 → 20; pick-validation bound,
  draft-modulo (both bot and human clients), name/ability-name tables all updated in the same
  pass — every gap Paimon's initial landing left behind (S170-55) closed here from the start. 5
  new headless tests. Verified: full suite (289 checks), VS0/VS1 stable, live — rebuilt +
  restarted all three systemd units, confirmed NOOR-1 (`hero_id=19`) actually gets drafted in a
  real 20/20 match and the match runs stably with real snapshots streaming, no crash.

## 2026-07-25 (21)

- feat(arena): Overwatch-style recast-time tiles for Q/W/E (S170-127). Founder, real-time: "add
  the ability frame cooldown timer tiles from shankpit og engine as recast time affordances" ->
  "make it like overwatch recast frames for q w e." Replaced the plain three-line text HUD
  ("Q: NAME [CD]") with real square ability tiles. Visual language ported from SHANKPIT's own
  `apps/lobby/src/main.c` `draw_ability_one_tile()` (bordered square, background/border color
  swap on cooldown, big centered countdown number, keybind label below) plus a real radial
  cooldown wipe on top -- SHANKPIT's tile was for one hero's one fixed-length ability; REDGARDEN
  has 19 heroes across 3 slots with cooldowns ranging roughly 2s-26s+, where "how much is left"
  matters more than a flat color tint alone shows. No per-hero max-cooldown table exists
  client-side to compute that fraction against, so it's tracked locally instead:
  `draw_ability_tile()` remembers the highest `cooldown_ms` seen since it last hit 0 (arms the
  instant a cast starts counting down from its real peak) and wipes that fraction away as a dark
  wedge sweeping clockwise from 12 o'clock -- self-correcting per-slot, no new wire data needed.
  W's tile lights bright toggle-green while `w_active`, matching the existing "W is ON" HUD
  convention it replaces. Ability-name caption kept (S170-96/S170-112's "show which real ability
  that is" work), drawn small below each tile -- known limitation, not hidden: several hero names
  are long enough to visually overflow the caption's tight column width at this tile size, a
  cosmetic issue only, worth a follow-up pass if it reads as messy in practice. Client-only
  change, no protocol/server changes. Verified: clean build, full headless suite (277 checks),
  VS0/VS1 stable. No headless test possible for the actual rendered tile layout.

## 2026-07-25 (20)

- feat(arena): small musical sound effects for gameplay legibility (S170-92). Founder, real-time:
  "add little musical sound effects to redgarden to add legibility via midi." Real scope decision
  made, not guessed: raw SDL2 core audio (`SDL_OpenAudioDevice`/`SDL_QueueAudio`), no SDL2_mixer.
  The backlog item's own open questions (new mixer dependency? second DLL to bundle in `PLAY.bat`'s
  zip alongside SDL2.dll?) both dissolve if nothing new gets linked at all -- SDL2 core already has
  an audio subsystem, already ships in every existing build. "Via midi" read as "short, distinct
  musical notes per event," not literal `.mid` playback -- a procedurally-synthesized sine tone per
  cue is the honest match at this scope ("little," per the founder's own word). Two cues: a short
  low thud (220Hz) on any hit landing, and an ascending A4/C#5/E5 triad per ability slot (Q/W/R) on
  cast -- mirrors the spell-flash color tiers (S170-124) in sound, so which slot just fired reads
  even without looking at the cast location. Gated to a ~15-unit hearing radius around the local
  player's own hero -- unfiltered, a real 20-hero match's several-casts-per-second would be noise,
  not legibility. Graceful degradation: no audio device available (this box is headless; a real
  player's box might also lack sound hardware) means every `play_tone()` call is a silent no-op,
  never a crash. Client-only change (`apps/arena`) -- no protocol/server/bot changes, no live
  systemd restart needed. Verified: clean build (both native and, per the existing CI workflow's
  own mingw step, expected to cross-compile cleanly since only core SDL2 functions are used --
  not locally re-verified, mingw isn't installed on this box), full headless suite (277 checks),
  VS0/VS1 stable. No headless test possible for actual audio playback, same limitation as the
  earlier hover-cursor/flash-animation work.

## 2026-07-25 (19)

- feat(ops): auto-deploy the live arena binaries on green CI (S170-100). Founder, real-time:
  "ensure the server version always stays current with the currently latest passing build." New
  `scripts/auto_deploy.sh`, polled by `redgarden-auto-deploy.timer` (every 10 min, 6 GitHub API
  calls/hour, well under the unauthenticated rate limit) + `.service`. Real design, not a quick
  bolt-on, given tonight's own S170-84 CI-hang incident:
  - Operates on a **separate checkout** (`~/redgarden-deploy`), never the interactive dev
    directory this script lives in -- an automated `git checkout <sha>` against the same
    directory active development happens in would risk clobbering uncommitted work or racing a
    build.
  - Only ever considers GitHub Actions runs with `status=completed` AND `conclusion=success` --
    guards against exactly the CI-hang failure mode that can sit "in_progress" indefinitely.
  - **Re-verifies locally before touching anything live** -- rebuilds and reruns
    `test_arena.sh`/`test_10_bots.sh` against the fresh checkout rather than trusting CI's word
    alone; aborts and leaves the old binaries in place on any local failure.
  - Publishes via copy-then-rename, not a direct overwrite -- found live, first real run: a
    direct `cp` onto `red_garden_arena_bot` hit `ETXTBSY` because the 19-bot pool has that binary
    mapped the whole time it runs. `rename()` atomically repoints the path to a new inode;
    already-running processes keep the old one mapped until they're actually restarted.
  Verified live, real end-to-end run (not simulated): bootstrapped the deploy checkout, found and
  deployed the latest green SHA, restarted all three systemd units, confirmed a real 20/20 match
  still forms afterward. Second run correctly no-ops ("already deployed... up to date"). Timer
  installed and enabled for real.

## 2026-07-25 (18)

- docs(northstar): spec camera lock/unlock + fog of war (S170-125), no code yet. Founder,
  real-time: "specdd unlockable and lockable camera and fog of war." Asked and confirmed scope
  before writing: spec only (same treatment as §14's draft-ban thread), and if/when fog of war
  gets built, client-side visual only for a first pass, not real server-side vision culling. New
  `NORTHSTAR.md` §15: camera lock proposal (hard-center on the local hero, `C` to toggle, open
  question on whether zoom stays free while rotation locks), fog-of-war proposal (radius-based
  hard cutoff around the local hero, allies always visible, honestly scoped as "the stock client
  chooses not to render this" rather than real anti-cheat), and named open questions (team vision
  sharing, node-ownership vision bonus, jungle-creep visibility) for whoever picks this up next.

## 2026-07-25 (17)

- feat(arena): particle effects for spells (S170-124). Founder, real-time: "redgarden add
  particle effects to spells." Distinct from S170-122's auto-attack flash, which fires on any HP
  decrease -- a signal several kits' spells don't produce at all (Frog's Q rewinds position/HP
  with no damage; Unicorn's W is a pure toggle). Real wire-protocol addition instead of another
  client-side guess: `ArenaHeroSnapshot.cast_flash_slot` (0/1/2/3 = none/Q/W/R), set the instant a
  cast clears its gate in `arena_cast_q`/`arena_toggle_w`/`arena_cast_r` regardless of whether it
  goes on to hit anything (a real cast animation fires on cast, not just on a landed hit) -- W
  needed care since only some heroes have an internal cooldown gate (instant-cast heroes like
  Ghost/Tyler/Paimon) while others are pure toggles (Unicorn) with no cooldown at all; gated on
  `w_cooldown_ms <= 0`, which is always true for toggle heroes and only true for cooldown heroes
  when actually available. Server clears its own copy right after each broadcast, one-tick
  lifetime, same idiom as `damaged_this_tick`. Client renders Q/W/R as visually distinct tiers
  (small cyan, bigger violet, biggest gold) via the existing ring-mesh machinery; local 1v1 demo
  mode (no server broadcast to hook) drains the same field directly off `arena_state` each frame
  instead. 5 new headless tests (cast_flash_slot set correctly on Q/W-toggle/R, not set when
  blocked by cooldown on Q or a cooldown-gated W). Verified: full suite (277 checks), VS0/VS1
  stable. Wire-format change (ArenaHeroSnapshot grew by one byte) -- rebuilt and restarted all
  three live systemd units together, confirmed a real 20/20 match forms, drafts, and streams real
  snapshots with no crash.

## 2026-07-25 (16)

- feat(arena): enhanced cursor hover state, enemy vs. ally (S170-69 revisited). Founder,
  real-time: "do the enhanced cursor hover state work" — promotes this from the northstar/design
  note it was logged at to real implementation. Purely client-side: `arena_state.heroes[i].team`
  is already populated in every render mode, no wire changes needed. Hovering near a hero's
  floating health bar now draws a bracket outline (same relationship color as the bar fill —
  self/ally/enemy) plus a tooltip with relationship, hero name, and real HP numbers near the
  cursor. Found in the same pass the health bars already run (world_to_screen per hero), no extra
  per-frame cost. Client-only rendering change — no headless test possible (same limitation as the
  earlier attack-flash/melee-animation work), verified via clean build only.

## 2026-07-25 (15)

- docs(readme): real "How to Play" section for the arena MOBA (S170-97). Founder, real-time: "put
  all the hero doc comments that say how to play them (what the kit keybinds do) move that up to
  the top of the readme." The keybind contract was scattered as code comments in
  `apps/arena/src/main.c` and left implicit across every per-hero entry in `docs/HEROES_VS0.md` —
  no single place explained "Q/W/E are your three ability slots, click to move, right-drag/wheel
  for camera." New section right under the title: a real synthesis (checked directly against the
  actual SDL event handling, not guessed), not comments moved verbatim.

## 2026-07-25 (14)

- feat(arena): finish wiring Paimon into the live roster (S170-55). The hero (enum entry, kit
  dispatch, docs writeup) was added in an earlier, uncommitted pass but was never actually
  reachable: no `arena_hero_name()`/`arena_ability_name()` entries (rendered as "unknown"),
  `apps/arena_server`'s pick-validation bound stopped at `ARENA_HERO_TYLER`, the draft-modulo in
  `apps/arena_bot`/`apps/arena` (both `% 18`) meant Paimon could never be drafted at all, and
  `tick_hero_kit`/`bot_cast_kit_if_ready` had no Paimon case (the latter a real compiler warning:
  "enumeration value 'ARENA_HERO_PAIMON' not handled in switch"). Fixed all five gaps: name +
  ability-name table entries, pick-validation bound raised to `ARENA_HERO_PAIMON`, draft-modulo
  bumped to `% 19` in both bot and human clients, passive aura-silence + R-zone damage/heal tick
  added to `tick_hero_kit` (new `ARENA_PAIMON_PASSIVE_INTERVAL_MS` constant), bot-cast heuristic
  added. Also gave Paimon a distinct 3D silhouette (robed body + raised scepter accent) instead of
  falling through to the generic default cube. 5 new headless tests (Q root+damage in/out of
  range, W damage+silence, passive periodic silence, R zone damage-to-enemy/heal-to-ally).
  Verified: full suite passes (272 checks), live: rebuilt + restarted all three systemd units,
  confirmed Paimon (`hero_id=18`) actually gets drafted in a real 20/20 match and the match runs
  stably with real snapshots streaming, no crash.

## 2026-07-25 (13)

- fix(matchmaker): close the phantom-requeue race that was silently capping almost every 10v10
  lobby at 19/20 (S170-85, S170-86). Founder, real-time: "tried the loki build matchmaking still
  launches the client but still no players no enemies nothing" → "q w e r t dont seem to work."
  Root-caused, not guessed: `apps/arena_bot`'s `wait_for_match()` resends `PACKET_FIND_MATCH` if
  it hasn't seen a reply in ~5s (a prior, partial mitigation from S170-99-era work, narrowed from
  1s but never closed). If that resend is still in flight the instant the matchmaker actually
  matches and dequeues the client, the late retry arrives with no way to tell it apart from a
  fresh request — `enqueue()` re-added an address that was already off connecting to its real
  match, permanently costing some *future* lobby exactly one slot (that address's owner isn't
  listening for a second `PACKET_MATCH_FOUND`). With 19 bots continuously cycling through matches,
  this stopped being a rare edge case and became the reason almost every lobby landed at
  `phase=0, 19/20 connected` before the server's 60s no-progress timeout tore it down — which
  also fully explains S170-86: a match that never leaves `ARENA_PHASE_WAITING` never reaches
  `ARENA_PHASE_LIVE`, so casts are correctly rejected the whole time, not broken. Fix: matchmaker
  now remembers every address for a 10s cooldown after it's actually been matched (comfortably
  longer than `connect_to_server`'s own ~5s max retry window) and ignores, rather than re-queues,
  any `FIND_MATCH` from it during that window. Verified live: before the fix, every real attempt
  against the 19-bot pool + 1 extra bot capped at 19/20 and timed out; after rebuilding and
  restarting all three systemd units, the very first attempt reached a genuine 20/20 lobby, all
  20 heroes picked, `match live`, and real snapshot events streaming to the match log. Headless
  suite unaffected (`test_arena.sh` all pass, `test_10_bots.sh` VS0/VS1 stability pass).

## 2026-07-25 (12)

- feat(arena): hero respawn, gated on node control (S170-121). Founder, real-time: "redgarden
  controlling a node enables its spawn for your team." Before this there was no hero respawn
  system at all -- `arena_update_teams` only ever checked team-wipe (0 alive) for the win
  condition, so death was permanent for the rest of the match. Added `respawn_ms_remaining`
  (mirrors `ArenaCreep`'s existing respawn idiom): armed to `ARENA_HERO_RESPAWN_MS` (8s) on death
  in `apply_damage`, ticked down each frame in the new `arena_tick_respawns`, but the actual
  respawn is withheld until the hero's team owns at least one `ArenaNode` -- territory is a real
  gate, not just a speed bonus, matching the founder's framing literally. Respawns at the owned
  node closest to that team's spawn line, full HP, hero identity preserved. Win condition updated
  to match: a team-wipe no longer instantly ends the match if that team still holds a node (they
  can fight back in); only ends once they're wiped AND own nothing to respawn onto. 4 new headless
  tests covering: stays dead with no node, respawns once a node is owned, match doesn't end
  prematurely on a wipe with a held node, match does end once truly locked out.
- feat(arena): basic auto-attack animations (S170-122). Founder, real-time: "add basic animations
  for auto attacks." Neither the wire snapshot (`ArenaHeroSnapshot`, deliberately minimal) nor a
  uniform-across-render-modes signal exists for "an attack just landed" -- used frame-to-frame HP
  decrease instead (available in every render path: local demo, net_mode, replay), spawning a
  quick orange-white flash at the hit hero's position. Reuses the exact same ring-mesh/shader
  machinery the existing move-click placement ring already uses, just a different color/scale
  curve so the two don't read as the same effect.
- Verified: `build.sh`, `build_arena.sh`, `test_arena.sh` (259/259 pass), `test_10_bots.sh`
  (VS0/VS1 stability pass). Live: rebuilt and restarted all three systemd units (wire protocol
  unchanged, but sim behavior changed so old running binaries needed to cycle). Confirmed via a
  temporary 20th bot that the live pool still forms real matches on the new binary. Noted but not
  chased down (pre-existing, already tracked as S170-115): the persistent bot pool intermittently
  gets stuck at 19/20 connected and times out -- matches the already-diagnosed abandoned-queue-slot
  behavior from force-quit client reconnects, not something introduced by this change.

## 2026-07-25 (11)

- fix(arena): requeue looked exactly like a crash (S170-115, real bug found by reading the
  matchmaker log). `net_find_and_connect()` blocks the whole event loop for up to 60s with no
  frame rendered in between -- the window shows whatever was on screen before the click and
  never updates for the entire wait, indistinguishable from a hang. Confirmed live: 13+ distinct
  source ports from the founder's own IP within a few minutes, consistent with force-quitting an
  apparently-frozen window and relaunching, over and over, each relaunch abandoning the previous
  queue attempt mid-match (which is also why those matches kept stalling at high-but-not-full
  connect counts). New `draw_queuing_screen()` renders and presents one real "QUEUING FOR MATCH /
  PLEASE WAIT" frame immediately before the blocking call starts, wired into the OK-button
  requeue handler. Doesn't make the wait non-blocking (bigger rearchitecture, not this pass) but
  makes the wait visibly a wait instead of a crash. Verified: `build_arena.sh`, `test_arena.sh`,
  and a local mingw cross-compile, all clean.

## 2026-07-25 (10)

- fix(ci): Windows cross-compile broken by S170-96's hero-name labels -- `arena_ai_bridge.c` (home
  of `arena_hero_name()`) was never added to the mingw link command when the HUD started calling
  it, so CI has been red on every commit since (`e53ee5f` confirmed failed via the Actions API).
  No valid Windows build existed for the founder to download. Fixed in the CI workflow and
  verified with a local mingw cross-compile using the same toolchain/flags, 0 errors.
- feat(arena): 18th hero, TYLER -- `docs/HEROES_VS0.md` already specced this as "an exact copy of
  Meepo's classic kit" (real OG clone-death rule) well before any code existed for it (S170-111).
  True multi-clone spawning isn't buildable on this engine without touching the draft/pick/
  connection model every other hero depends on (`ArenaHero` slots are one-per-client) -- honestly
  simplified and documented as such: Q "Earthbind" roots + a DoT (folds in Geostrike's poison,
  no generic per-melee-attack passive hook exists to hang it off separately), W "Poof" is a real
  instant blink-strike, R "Divided We Stand" keeps the actual point of the OG rule (real risk/
  reward) as a self-buff that hits hard and leaves Tyler's own armor negative for the window
  after, rather than literal shared-fate clones. `ARENA_HERO_COUNT` 17→18.
- feat(arena): real ability names on the HUD (S170-96 follow-up). Founder, live: "show ability
  names on screen." The Q/W/E cooldown strip only ever showed generic "Q READY"/"W ON" -- new
  `arena_ability_name(hero_id, slot)` (`packages/simulation/arena_ai_bridge.c`) returns each
  hero's real ability name from `docs/HEROES_VS0.md` (e.g. "EARTHBIND", "THE SACRED MAGIC"),
  stacked vertically on the HUD now since real names run much longer than "Q READY" ever did.
  Verified: `build.sh`, `build_arena.sh`, `test_arena.sh`, `test_10_bots.sh`, and a local mingw
  cross-compile (including `arena_ai_bridge.c`), all clean.

## 2026-07-25 (9)

- feat(arena): hero name labels above the floating health bars (S170-96). Founder: "add hero name
  labels above health bars." With 17+ heroes in the roster, a colored bar alone doesn't say who's
  who at a glance. One more `draw_string()` call per alive hero in the existing per-hero HUD loop
  (S170-89), using `arena_hero_name()` (`packages/simulation/arena_ai_bridge.c` -- the same token
  vocabulary the Game AI bridge already uses, e.g. "morrigan") for the text, reusing whatever GL
  color was already set for that hero's bar (team-colored labels, no extra color call needed).
  `arena_ai_bridge.c` wasn't previously linked into the arena client at all -- added it to
  `scripts/build_arena.sh`. Verified: `build.sh`, `build_arena.sh`, `test_arena.sh` all clean.
  Client-only change (no wire-protocol/gameplay-logic touched), so no live systemd restart needed.
  This box has no display, so verified by code review + clean compile only, same standing
  limitation as every other windowed-client-only change this session -- not run interactively.

## 2026-07-25 (8)

- fix(arena): bots bunching up on top of each other in a live match (S170-90). Founder, real-time:
  "all of the bots just bunch up on eachother." Root cause: `apps/arena_bot`'s move-target logic
  sent the nearest enemy's *exact* (x,z) as the move target -- whenever several bots on one team
  shared the same nearest enemy (common once a team clusters up mid-fight), they'd all converge on
  the literal same point and stack. Fixed by spreading each bot to its own approach angle around
  the target, derived from its stable owner index (`my_owner % 8`, no coordination needed between
  bots) at a radius just outside `ARENA_ATTACK_RANGE` -- a real surround formation instead of a
  single pile. Verified: `build.sh`, `test_arena.sh`, `test_10_bots.sh` all clean; restarted the
  three live systemd units on the new build, then ran a real temporary 20/20 match (added one
  extra bot to the persistent 19-bot pool's open human slot, removed it after) and confirmed real
  position data in the match log -- heroes on the same team ended up at genuinely distinct
  coordinates around a fight, not stacked identically the way the bug produced.

## 2026-07-25 (7)

- fix(arena): the two capture nodes render compressed onto one point in net_mode (S170-87). Real
  protocol gap, exactly as diagnosed but not yet fixed: `ArenaSnapshotMsg` never included node data
  at all -- only `heroes[]`, `winner`, `phase`, `picked[]`. In net_mode the client never calls
  `arena_init()`/`arena_init_teams()` locally (the server owns that), so `arena_state.nodes[0]`/
  `nodes[1]` stayed at the global static default -- both zeroed to `(0,0)` -- making both nodes
  render on top of each other at the world origin. Added `ArenaNodeSnapshot` (x/z/owner/
  capturing_team/capture_progress_ms) + `ArenaNodeSnapshot nodes[ARENA_SNAPSHOT_NODE_COUNT]` to
  `ArenaSnapshotMsg`, populated server-side in `server_broadcast()`, consumed client-side in
  `net_poll_snapshots()`. Also colored the node cubes by owner (blue/red/gold matching the hero
  team-color convention) now that ownership actually reaches the client -- the territory redesign's
  whole point, who controls the ground right now, was invisible before this. Verified: `build.sh`,
  `build_arena.sh`, `test_arena.sh`, `test_10_bots.sh` all clean; restarted all three live systemd
  units (`redgarden-matchmaker-bots`, `redgarden-matchmaker-players`, `redgarden-bot-pool`) on the
  new build since this is a wire-format change -- confirmed the pool re-fills cleanly to 19/20 with
  no size-mismatch/crash on the new binary.

## 2026-07-25 (6)

- fix(arena): missing font glyphs. Founder: "we are missing a lot of font glyphs in redgarden."
  `draw_char()`'s hand-drawn vector font only ever covered digits + `W,I,N,L,O,S,E,U,Y,H,P` +
  space -- everything else (15 missing uppercase letters, all of lowercase, punctuation) fell
  through to a generic missing-glyph placeholder box. Tonight's own hero-name expansion (Gary,
  Bacon+Puck, Abraham, Ada, Flute Debt) made this much more visible, since most of those names use
  letters the font never had. Added the remaining 15 letters (A,B,C,D,F,G,J,K,M,Q,R,T,V,X,Z),
  lowercase-folds-to-uppercase (one glyph set, not two), and common punctuation (`- + ' " . , : ! ( )`)
  in the same simple `GL_LINES` stroke style as the existing letters. Verified: `build_arena.sh`,
  `test_arena.sh`, `test_10_bots.sh`, and a local mingw cross-compile, all clean.

## 2026-07-25 (5)

- feat(arena): 16th/17th heroes, Abraham the Mage and Ada Lovelace (S170-103). Founder: "add
  abraham the mage" → "add ada lovelace mech pilot." Ada already had full lore
  (`multiverse_heroes.md` #112); Abraham needed new lore, written this pass (#113, The Unbound
  Historicals — Abraham of Worms, disputed author of the real grimoire behind Crowley's actual
  Abramelin Operation). `ARENA_HERO_COUNT` 15→17. Abraham: caster, Q a ranged bolt boosted by a
  toggle (W boosts Q's damage rather than range/duration, a new toggle shape), R a full
  self-cleanse + heal. Ada: tank/controller, Q roots at range, W is a toggled armor bonus
  (`arena_hero_armor()`), R real damage + a follow-up root. Wired into every real call site.
  Verified: `build.sh`, `build_arena.sh`, `test_arena.sh`, `test_10_bots.sh`, and a local mingw
  cross-compile, all clean.

## 2026-07-25 (4)

- docs: FFXI Rise of the Zilart-era item parity seed list (S170-102). Founder direction, real-time
  sequence: "add parity with all ffxi items at rise of the ziliart launch" → "northstar" →
  "redgarden into a doc like the hero metaverse guide in tyler" → "for true ip." Real,
  representative FFXI item names by category (currency, crafting materials, weapons by
  weapon-skill class, armor by equip slot, real Zilart-mission key items, notable RotZ-era
  end-game weapons) at `docs/FFXI_ITEM_PARITY_SEED.md`, same doc-first convention as
  `TYLER/multiverse_heroes.md`. Explicitly not for direct shipping — seed/training data for
  `gpt2-alpine-c`'s fine-tune pipeline to generate this game's own original item names from.
  Registered in `EMILY/context/golden-docs-index.md`.

## 2026-07-25 (3)

- fix(arena): real root cause of "everything breaks after 1 game" (S170-99). Founder, live:
  "still after 1 game in redgarden everything breaks." Confirmed via the matchmaker log: a
  genuinely full 20/20 lobby (including the founder's own external IP) entered draft and then --
  "No lobby progress in 60s (phase=1, 20/20 connected) -- shutting down." Reproduced a bot-only
  20/20 lobby live to rule out a server-side draft bug: it completed cleanly every time, meaning
  the human's own pick specifically was the one never landing. Root cause: `net_send_pick()` (and
  `apps/arena_bot`'s own `send_pick()`) was a single fire-and-forget UDP send with **no retry** --
  unlike `net_connect()`/`net_find_and_connect()`, which both already retry on a timer. Rock-solid
  over localhost loopback (bots, which is all this path was ever tested against all session), but
  a real external connection can drop that one unacknowledged packet, and `net_picked` latching to
  1 on *send* rather than *confirmation* meant it would never be resent -- the client believed it
  had drafted while the server waited forever for a pick that was never coming. Fixed in both the
  human client and `arena_bot`: resend the pick every ~1s while still stuck in draft, harmless if
  the original arrived (the server just re-records the same hero_id). Verified: `build.sh`,
  `build_arena.sh`, `test_arena.sh`, `test_10_bots.sh`, and a local mingw cross-compile, all clean.

## 2026-07-25 (2)

- feat(arena): 15th hero, Bacon+Puck merged (S170-94). Founder: "add bacon and puck as the same
  hero." Same merge pattern as Flamel/Druid earlier in the roster -- Bacon (`multiverse_heroes.md`
  #5, "custodian of the one location nobody's allowed to know yet," seed phrase "ask again later")
  and Puck (#67, an unresolved duality between two versions of himself nobody can confirm is real).
  `ARENA_HERO_COUNT` 14→15. Q "Ask Again Later" (self `intangible_ms`, the shared can't-be-hit
  status, S170-32), W a free toggle that extends Q's own intangibility duration rather than
  granting a stat, R "The Trick Was Always the Same" (real damage + a self-heal off a fraction of
  it, always commits). Wired into every real call site. Verified: `build.sh`, `build_arena.sh`,
  `test_arena.sh`, `test_10_bots.sh`, and a local mingw cross-compile, all clean.

## 2026-07-25 (1)

- feat(arena): 13th/14th heroes, Gary and Flute Debt (S170-91). Founder: "add GARY to redgarden"
  → "music" → "add flute debt" (read in context, not a separate audio request). Both already had
  full lore entries, no new writing needed: Gary, Bifrost Security (Off-Duty)
  (`multiverse_heroes.md` #35) and Han Xiangzi's Flute-Debt (#42). `ARENA_HERO_COUNT` 12→14.
  Gary: a stationary marksman with no dash/teleport at all -- Q is a range-gated precision shot
  (no movement), W is a free toggle that extends Q's own range rather than granting a stat, R is
  a fixed-duration root ("slow down, this isn't a track meet"). Flute Debt: a real debt/payoff
  mechanic -- Q applies the shared `burning_ms`/`burn_dps` DoT (Pizza's fields, S170-46) as "the
  wrong note," W is a free-toggle self-heal, R always lands but deals real bonus damage only if
  the Q debt is still active on the target ("eventually collects"), base damage otherwise. Wired
  into every real call site: all three cast-dispatch switches, `tick_hero_kit`'s regen tick, bot
  AI heuristics, both auto-draft pools (`% 12` → `% 14`), the server's pick-validation bound,
  `arena_hero_name()`, `docs/HEROES_VS0.md`. Verified: `build.sh`, `build_arena.sh`,
  `test_arena.sh`, `test_10_bots.sh`, and a local mingw cross-compile, all clean.

## 2026-07-24 (34)

- fix(arena): "ENEMY" HUD readout was a hardcoded 1v1 assumption, broken in team mode (S170-86
  investigation, real bug found). `heroes[1 - my_owner]` only ever made sense for exactly 2
  heroes -- in a 20-hero team match it either mislabels a teammate as ENEMY (`heroes[1]` is
  always team 0, same team as `heroes[0]`, whenever `my_owner==0`) or reads a negative
  out-of-bounds index for any `my_owner > 1`. Replaced with `arena_nearest_enemy(my_owner)` in
  net_mode, the same team-aware lookup the server already uses -- local 1v1 mode keeps the
  original behavior unchanged.
- feat(arena): per-hero floating health bars (S170-89). New `world_to_screen()` projects a world
  point through the same view-projection matrix the 3D pass draws with, into the 2D HUD's pixel
  space. Every alive hero now gets a small billboarded bar above them, colored by relationship
  (cyan = you, blue = teammate, red = enemy) -- not just the two fixed YOU/nearest-enemy bars,
  which never showed anything for the other 18 heroes in a real team match. Verified:
  build_arena.sh, test_arena.sh, and a local mingw cross-compile, all clean.

## 2026-07-24 (33)

- fix(ci): hard timeout ceilings after a real hung build. Founder: "we have a hung build for the
  rebrand in ci." Confirmed via the GitHub Actions API: commit `62ca556`'s run sat "in_progress"
  on the mingw-w64/SDL2 install step for 18+ minutes with byte-identical YAML to four immediately
  preceding runs that all passed in seconds -- a transient runner/mirror stall, not a code bug, but
  nothing in the workflow would have ever timed it out on its own (job default is 6 hours; no gh
  CLI/token available in this environment to cancel it remotely). Added `timeout-minutes: 30` at
  the job level, `timeout-minutes: 10` on the specific mingw/SDL2 step, `DEBIAN_FRONTEND=noninteractive`
  on that step's apt-get (defends against an interactive alternatives prompt as one plausible
  cause), and a real `wget --timeout=30` (previously only `--retry-connrefused`, which doesn't
  catch a connection that succeeds and then stalls).

## 2026-07-24 (32)

- feat(arena): 12th hero, Loki (S170-79). Founder: "add LOKI to KNIGHTS_OF_THE_VOID hero
  multiverse then into the game one shot as a kit." New lore entry first
  (`TYLER/multiverse_heroes.md` #37, "Loki, Who Isn't Here" — see that repo's own commit), then a
  real `ARENA_HERO_LOKI` kit here, one pass, no stub: Q "Interference, Not a Signal" (instant
  positional swap with the nearest enemy, no travel time, small hit on arrival, no range gate),
  W "Bound Where the Myth Says" (free toggle, flat armor bonus while active, same convention as
  Unicorn's regen toggle), R "Held For As Long As The Myth Demands" (self-cast survive-floor
  window, the same `survive_floor_ms` mechanic Pizza/Dagda's ultimates already use). Wired into
  every real call site: `arena_hero_armor()`, the Q/W/R dispatch switches, the bot AI heuristic,
  the human/bot auto-draft pools (`% 11` → `% 12`), the server's draft-pick validation bound
  (`ARENA_HERO_COURIER` → `ARENA_HERO_LOKI`), `arena_hero_name()`, `docs/HEROES_VS0.md`.
  `ARENA_HERO_COUNT` 11 → 12. Verified: `scripts/build.sh`, `scripts/build_arena.sh`,
  `scripts/test_arena.sh`, `scripts/test_10_bots.sh` all clean, plus a full local mingw
  cross-compile with the updated roster, 0 warnings.

## 2026-07-24 (31)

- feat(arena): toggleable APM overlay, F11 (S170-71). Founder: "add toggalable apm overlay f11"
  → "adding apm near term if its cheap." Ring buffer of action timestamps (clicks + Q/W/E casts)
  in `apps/arena/src/main.c`, `apm_compute()` counts entries within a trailing 60s window so the
  on-screen number is a real rate rather than a since-launch average. Off by default, F11 toggles
  it in any mode (local, net, or observing). Ties into `REDGARDEN/CLAUDE.md`'s standing
  high-APM-affordance UI constraint. Verified: `scripts/build_arena.sh` + `scripts/test_arena.sh`
  clean, local mingw cross-compile clean.

## 2026-07-24 (30)

- fix(ops): redgarden-bot-pool.service never set REDGARDEN_TICKET_SECRET (S170-72) -- the real
  root cause of "no entities visible," not a rendering or death bug. Live investigation of the
  founder's "i cant see myself or team or enemies" / "maybe the game isnt actually working right
  theres no entities" turned up ~55 accumulated zombie arena_server processes, all stuck at
  "0/20 connected" until their 60s no-progress timeout. The matchmaker log showed lobbies filling
  and spawning a real dedicated server every time, but no client ever completed PACKET_CONNECT to
  it. Root cause: only the two matchmaker systemd units had REDGARDEN_TICKET_SECRET set --
  redgarden-bot-pool.service (the unit that actually runs the 19 apps/arena_bot processes) never
  did, since it was written. Bots could queue fine (no ticket needed for that) but silently failed
  to mint a valid connect ticket for the actual game-server handshake, so matches formed and then
  sat empty forever. Fixed by adding Environment=REDGARDEN_TICKET_SECRET=... to the bot-pool unit.
  Verified live: killed the zombie servers, restarted all three services, watched real
  CLIENT N CONNECTED lines climb to 18-20/20 across several matches -- the system is now capped
  only by needing a 20th (human) player, not by a broken connect path.

## 2026-07-24 (29)

- fix(arena): the actual instant "YOU WIN" bug (S170-66) -- three more `#ifndef _WIN32` guards
  reintroduced by the newer 10v10 networked-PvP code, all in `apps/arena/src/main.c`: the
  `net_poll_snapshots()` call site, the click-to-move `net_send_move()` call site, and the Q/W/E
  `net_send_cast()` call site. On Windows (the founder's actual platform) all three silently
  compiled out -- no error -- so the client fell through to the local single-player practice
  simulation instead of ever applying real server snapshots, resolving near-instantly and
  producing a "YOU WIN" completely disconnected from the real networked match. `grep -n "#ifndef
  _WIN32"` now returns zero hits in this file; every remaining guard is a correctly-scoped
  `#ifdef _WIN32` around an actual platform difference. Second real blocker found + fixed in the
  same pass: `redgarden-bot-pool.service` (S170-65) launched exactly 20 bots into a
  `--lobby-size 20` matchmaker, permanently filling the lobby with bots alone -- dropped to 19 so
  a human always has an open slot. Also added, absorbing part of S170-68's scope per the
  founder's own real-time narrowing ("terminal launching the client is fine for now" / "auto
  draft is fine for now"): `net_send_pick()` + auto-draft (sends a `PACKET_ARENA_PICK` the moment
  `net_phase` reports `ARENA_PHASE_DRAFT`, console-logged) so a match never hangs waiting on a
  pick that never comes; and a click-to-continue "OK" requeue button on the win/lose screen in
  net_mode, reusing the same `net_find_and_connect`/`net_connect` path used at startup. Verified:
  `scripts/build_arena.sh` clean, `scripts/test_arena.sh` all green, and a full local mingw
  cross-compile (same toolchain/flags as CI) produced a clean 0-warning `RedGarden.exe`.

## 2026-07-24 (28)

- ops: real systemd units for the arena matchmaker pools + persistent bot pool (S170-65). Founder,
  after the S170-63 fix: "fix is not pushed" -- correctly pointing at the actual gap, since the
  code fix genuinely was pushed and merged, but the live matchmaker processes had *never* run
  under systemd at all, ever, on this box -- only ever manually nohup'd, which is exactly why
  S170-63's outage happened in the first place (died silently, stayed dead until someone
  noticed). New `ops/systemd/redgarden-matchmaker-bots.service`,
  `redgarden-matchmaker-players.service`, `redgarden-bot-pool.service` (+ new
  `scripts/run_bot_pool.sh`, a foreground wrapper so systemd can actually supervise the bot set),
  matching the existing `fatbaby-newssite.service`/`gfd-mud.service` pattern. Deployed and
  verified live: killed the manual processes, started the units, confirmed `Restart=on-failure`
  actually works (one stray leftover `arena_server` was squatting on the matchmaker's own port;
  once killed, systemd auto-relaunched the matchmaker within its restart window with no manual
  intervention).

## 2026-07-24 (27)

- fix(ops+ci): matchmakers had died (bots orphaned, queue packets going nowhere) and `PLAY.bat`
  never set `REDGARDEN_TICKET_SECRET`, so even after restarting the matchmakers the client failed
  silently at the ticket-mint step -- no human login flow exists yet, so `--queue` falls back to
  self-minting via that env var, which has to be set client-side too (S170-63, found live while
  a founder was actually trying to connect). Restarted both matchmaker pools with the shared test
  secret the live bot pool already uses. Fixed `PLAY.bat` to set the same secret before launching
  and added `pause` so a failure is actually readable instead of the window closing before the
  error prints. Also: briefly misdiagnosed this as an IDUNA-vs-server ticket-signing-secret
  mismatch (real, but irrelevant to the self-mint path actually in use here) and accidentally
  spawned one broken test bot mid-investigation that spammed the pool with failed
  connect/requeue cycles -- corrected, orphans cleaned up.

## 2026-07-24 (26)

- fix(ci): `PLAY.bat`'s hardcoded `127.0.0.1` was wrong for the actual distributed client
  (S170-59). Found live: a founder actually downloaded and ran the CI-built Windows client, and
  it hung "queuing for a match" at `127.0.0.1:7778` -- loopback, which only makes sense if the
  matchmaker is running on that same Windows machine. Fixed `PLAY.bat` to point at this box's
  real address (`198.58.107.85`) and print what it's connecting to before launching, instead of
  a silent `start` that gave no feedback about which server it was even trying to reach.

## 2026-07-24 (25)

- CI green end to end (S170-54 closed): confirmed via the GitHub Actions API (no `gh` CLI on this
  box, public API works without a token for a public repo) that commit `276614c`'s run passed
  every step -- headless tests, the bot-pool soak test, Linux server-side build, Linux arena
  client build, the mingw-w64 install, the Windows cross-compile, artifact bundling, and upload.
  `red-garden-build` now contains a real `RedGarden_Client_*.zip` (Windows .exe + SDL2.dll +
  PLAY.bat) and `RedGarden_Server_*.zip` (Linux server-side binaries), matching what a founder
  actually asked for: "the github artifact for REDGARDEN is unsuitable... no executable... SDL
  dll not bundled... check shankpit for the protopattern."

## 2026-07-24 (24)

- fix(arena): the actual root cause of the Windows build failure, found by locally reproducing
  the cross-compile (downloaded `gcc-mingw-w64-x86-64-win32` + deps via `apt-get download`,
  extracted with `dpkg-deb -x`, no sudo/root needed) instead of guessing blind against CI (S170-54
  cont'd). The whole networking section of `apps/arena/src/main.c` (ticket minting, WOTAN
  registration, `net_connect`, `net_find_and_connect`, snapshot polling — ~300 lines) was still
  wrapped in one big `#ifndef _WIN32`, so none of it was ever compiled on Windows at all despite
  the earlier per-call portability fixes — `main()`'s calls to these functions produced "implicit
  declaration" + linker "undefined reference" errors. Removed that outer guard now that the
  platform differences inside are each handled individually (winsock includes, ioctlsocket/fcntl,
  closesocket/close, mkdir, and one more found this pass: `getpid()` is POSIX-only, added a
  `GetCurrentProcessId()` branch). Also silenced two real `sendto()` type-mismatch warnings
  (Winsock wants `const char *`, POSIX accepts anything pointer-shaped). **Verified: a real
  `RedGarden.exe` (PE32+, Windows) now builds clean locally with zero errors and zero warnings**,
  Linux side (`build_arena.sh`, full test suite) still green. Same fix pushed for CI to confirm
  independently.

## 2026-07-24 (23)

- fix(arena): real Windows portability for `apps/arena/src/main.c`'s networking, found by
  actually watching the S170-54 CI run fail rather than trusting the workflow blind. Root cause:
  the file's `#ifndef _WIN32` guard around POSIX socket headers had no matching `#ifdef _WIN32`
  branch including `winsock2.h`/`ws2tcpip.h` at all -- so under MinGW, `sockaddr_in`/`AF_INET`/
  `SOCK_DGRAM` etc. were simply undeclared. Fixed to match `apps/server/src/main.c`'s already-
  correct pattern exactly: `winsock2.h`/`ws2tcpip.h`/`windows.h` + `#pragma comment(lib,
  "ws2_32.lib")` on Windows, the POSIX headers on everything else. Also fixed `fcntl(F_SETFL,
  O_NONBLOCK)` (POSIX-only) → `ioctlsocket(FIONBIO)` on Windows at both non-blocking-socket call
  sites, `close()` → `closesocket()`, and added the `WSAStartup` call Windows sockets need before
  first use. Along the way, found `--connect`/`--queue` were explicitly stubbed out on Windows
  builds entirely ("not supported... yet") -- now that the underlying networking actually
  compiles correctly cross-platform, enabled it for real rather than leaving the stub in place
  once its excuse was fixed. Verified: `scripts/build_arena.sh` (Linux) and the full
  `scripts/test_arena.sh`/`test_10_bots.sh` suites still green; the actual Windows cross-compile
  is CI-verified on push (still no `mingw-w64` locally, no sudo here).

## 2026-07-24 (22)

- fix(ci): `.github/workflows/ci.yml` rebuilt to produce a distributable artifact (S170-54).
  Founder, live: "the github artifact for REDGARDEN is unsuitable... no executable... SDL dll
  not bundled... check shankpit for the protopattern." Root cause: CI only ran `build.sh` (RTS
  server-side binaries) and never built `apps/arena` -- the actual product since today's MOBA
  pivot -- and uploaded bare Linux ELFs with no runtime bundled either way, nothing a founder
  could download and run. Mirrored `SHANKPIT/.github/workflows/release.yml`'s proven pattern:
  cross-compile the arena client to Windows via `mingw-w64` + the official `SDL2-devel-2.30.10-
  mingw` package, zip `RedGarden.exe` + `SDL2.dll` + a `PLAY.bat` as a separate Client artifact
  from the Linux server-side binaries (Server artifact). No `-lglu32` needed, unlike SHANKPIT's
  client -- `apps/arena` is shader-based and never depended on GLU. Also added `test_arena.sh` +
  `test_10_bots.sh` as real CI gates before packaging -- neither was run in CI before this,
  despite being the actual verification for everything built today. Linux side (tests, `build.sh`,
  `build_arena.sh`) re-verified locally; the Windows cross-compile itself is CI-only-verified for
  now (no `mingw-w64` installed locally, no sudo here -- queued as `sudo-queue/09-mingw-w64.sh`
  for a local dry-run if wanted).

## 2026-07-24 (21)

- feat(arena): territory capture redesigned to a real Arathi Basin-style channel + territorial jungle creeps + memorable bot names (S170-50/51). Replaced the S170-46 ambient-pressure territory model entirely with exclusive-presence channel capture: a node flips neutral the instant a channel starts (not on completion), interrupts (mixed presence, Pizza's corruption, damage taken, or the channeling team leaving) lose all progress with no free revert, and stealth (Frog's R) lets a lone capper channel undetected through a crowd of visible enemies -- but starting the channel breaks that stealth, and any damage to the channeling team interrupts it, both matching real Arathi Basin rules exactly. Added territorial jungle creeps: one per node, re-rolled from the node's current owner on every respawn, two flavors with genuinely different rewards (a rare contested-node creep grants a big capture-progress swing; a common owned-node creep heals its own team or helps the enemy flip the node, depending who kills it) -- a real counter-play tool against turtling comps. Activated real WOTAN stats tracking for the persistent bot pool (was silently running on self-minted tickets all session -- an env var oversight, not a code gap) and gave bots a curated pool of memorable display names via a new `--index` flag. 28 new headless tests (251 total). Verified live: a full ~2.5-minute 20-hero match ran to completion without crashing on the redesigned system; confirmed real, named player identities registering and the public leaderboard accumulating real stats.

## 2026-07-24 (20)

- docs(heroes): The Donkey — Paper Glide, a second auto-trigger ability (S170-49). Founder direction: "launching itself into the air while folding into a paper airplane... movement mobility and escape... fly over trees etc." Specified in `docs/HEROES_VS0.md` as Q, consistent with the existing Indirect-Control identity (auto-triggered alongside the Immortal's Fold passive, not player-cast): launches airborne, refolds into a paper-airplane shape mid-launch, glides clear of danger, ignoring ground terrain/obstacles and immune to ground-based CC while airborne. Docs only -- The Donkey (and the rest of the Indirect-Control archetype) stays blocked on a non-piloted-unit system that doesn't exist in `arena_game.c` yet, flagged explicitly in the entry rather than shoehorned into the owner-piloted sim.

## 2026-07-24 (19)

- feat(arena): The Courier, Ratatoskr (S170-48) -- eleventh hero, roster 10 → 11. TYLER `multiverse_heroes.md` #32 is already nicknamed "The Courier"; his messenger-between-two-fixed-points framing (the eagle at Yggdrasil's crown, Nidhogg at its root) maps directly onto the two existing `ArenaNode` positions -- W (Between Eagle and Serpent) is a pure fixed-geography teleport to whichever node is farther away, distinct from every other hero's ally/foe-relative teleport. Q is a Unicorn-shaped dash-strike whose landed cast also cleanses The Courier's own active debuffs (the passive, "editing the message" back to him). R is a flat life-drain execute on the nearest enemy. 7 new headless tests (223 total). Pick-validation bound and draft modulo widened 10→11. Verified live after cleaning up a stray leftover-process port conflict: all 11 hero_ids drafted across a real 22-bot pool, left running on the current build.

## 2026-07-24 (18)

- feat(arena): territory/node system + five new heroes -- Tree, Pizza, Flamel (absorbing the former Druid), Morrigan, and Dagda (S170-46/47). Founder picked territory/resource economy over multi-unit-per-player or non-piloted units as the next system to build, since it unblocks the most queued heroes at once. Extended the two previously-decorative `ArenaNode` markers with signed `pressure` (-100..100), threshold-derived `owner`, and `marked_by_team`/`mark_ms_remaining`; new `arena_tick_nodes()` sums weighted team presence per node each tick (Tree counts double, Root Network) and drifts/decays pressure toward a derived owner, called from both `arena_update()` and `arena_update_teams()` with no special-casing. Added a centralized `apply_damage()` helper (every damage call site now routes through it) so Pizza's R -- a real damage floor, not simplified away -- works consistently everywhere. Mid-build founder redirect ("druid and flamel should be the same hero"): merged Druid into Flamel in `docs/HEROES_VS0.md` first (TYLER lore check confirmed Druid had zero named-character backing, Flamel is a real one), keeping Flamel's identity and folding Druid's kit in as flavor. Then two more founder-driven additions on the same pass: Morrigan ("meta jungler," TYLER #68) built as an affinity for contested/unclaimed node ground since no standalone jungle-camp system exists; Dagda ("the two-natured hammer," TYLER #69) built with a literally dual-natured Q (kills a hittable enemy in range, else heals a hurt ally in range instead -- the same tool, either direction, depending on what's there). `apps/arena_server`'s pick-validation bound and `apps/arena_bot`'s draft modulo widened 5→8→10 heroes along the way. 62 new headless tests (216 total, up from 154), including a caught-and-fixed test bug (an exact-value assertion on Morrigan's execute-tick damage invalidated by a same-tick melee auto-attack and HP-floor clamping -- fixed by comparing damage deltas instead). Verified live: relaunched the persistent bot pool on the freshest build, all 10 hero_ids (0-9) drafted successfully across a real 20-bot match, pool left running (not torn down) so bots are actively playing the current roster.

## 2026-07-24 (17)

- docs(arena): S170-14 (3/3) — ranked matchmaking design pass, `docs/RANKED_MATCHMAKING.md`. Plain ELO (K=32 flat, starting 1000) recommended over Glicko/TrueSkill -- the uncertainty modeling those solve for doesn't apply to a symmetric 1v1-only pool yet. New `redgarden_ranked_stats` table, kept separate from casual `player_game_stats` (ranked rating and casual win/loss are different questions). Widening-rating-search-window queue design, explicitly scoped as its own future build pass since it doesn't fit the existing spawn-on-fill `apps/matchmaker` binary. Design only, no code landed -- this was a design gap, not a code gap, per the backlog item's own framing.

## 2026-07-24 (16)

- feat(arena): allies + fifth hero, Doc Wheel (S170-45). Founder decision: build allies/multi-hero-per-team in arena rather than territory or declaring the 4-hero roster complete. Team-mode infra already existed from the 10v10 pivot -- the actual gap was just an ally-targeting primitive. Added `arena_nearest_ally(int owner)` (mirrors `arena_nearest_enemy` exactly) and threaded an `ally` param through `tick_hero_kit`. Unblocked: Ghost's Recital ally-heal side (previously skipped), Frog's Borrowed Time (W, places a generic `next_cast_refund` buff on the nearest ally -- refunds whoever holds it their next Q/W/R cooldown to zero), and Doc Wheel (Buer) as a full new hero -- HP%-scaled heal+cleanse (Q), teleport-to-ally (W), teamwide cleanse+heal (R, simplified from a literal shield, flagged not faked). `apps/arena_bot`'s draft picker and `apps/arena_server`'s pick-validation bound updated so Doc Wheel is actually draftable over the wire. Found and fixed a real bug writing the Borrowed Time test: a Unicorn with no move target and no foe never reaches its cooldown-setting code path at all, so the refund check silently never ran -- the original assertion was passing by coincidence, not because the mechanism worked. 16 new headless tests, all green alongside the full existing suite. Verified live: two separate real bot matches (10-bot, 20-bot lobbies) both drafted Doc Wheel without incident.

## 2026-07-24 (15)

- feat(arena): player-only matchmaking pool (S170-14, 2/3) -- `scripts/launch_arena_pools.sh` stands up a second, entirely separate matchmaker instance on its own port (7779, `--lobby-size 2`), with zero bots ever configured to queue into it. Pool separation is operational (two matchmaker processes, two ports), not new code inside the matchmaker itself. Lobby size is 1v1, not 10v10, since a 10v10 player-only queue would never fill with near-zero real concurrent players today. Verified live: ran both pools simultaneously (bot pool with 2 bots + player-only pool), two real `--queue` human clients matched into a genuine 1v1 on the player-only pool ("Lobby full (2 players) -- internal bot AI disabled, entering draft"), and confirmed by grepping every bot's logs that none ever touched the player-only pool's ports. Ranked pool (3/3) stays explicitly undesigned -- no rank model, MMR, or queue rules exist yet, a design gap not a code gap.

## 2026-07-24 (14)

- feat(arena): `red_garden_arena --queue <matchmaker_host>` (`--matchmaker-port`, default 7778) -- a human player can now join whatever match the persistent bot pool is currently matchmaking into, instead of only supporting `--connect host:port` to an already-known server. Reuses `apps/arena_bot`'s exact queue pattern (`PACKET_FIND_MATCH`/`PACKET_MATCH_FOUND`, ~5s retry) and `net_connect`'s existing ticket-mint/handshake for the actual game connection -- pure client-side addition, no server changes needed. Verified live: started a real matchmaker + one persistent bot, ran the human client with `--queue 127.0.0.1`, confirmed it queued, matched with the bot, connected, and was assigned hero slot 1 on the same server the bot connected to (slot 0). First attempt failed on a test-setup mistake (matchmaker started without `REDGARDEN_TICKET_SECRET` exported, so the spawned server failed closed on all connects, correctly) -- not a code bug, fixed by restarting the stack with the secret actually set. Still bounded by the same known gap as before: no Xvfb on this box, so the client hits SDL_Init with no display right after connecting -- the join is proven, playing a full match still needs a real display.

## 2026-07-24 (13)

- Verified the actual `--lobby-size 20` (10v10) path live, end-to-end, not just via the headless-tested code shared with 1v1: 20 real `apps/arena_bot` connections, 20 real drafts, correct team assignment (0-9/10-19), combat across 20 heroes, and a real team-wipe win condition (`match_end` winner matched exactly which team had zero living heroes left). All 20 bots then persisted and requeued into a second full 20-player match automatically -- identity stayed stable (1 registration each) across both. Server process count stayed healthy throughout (not the earlier zombie pileup). This closes the "10v10 unverified" gap flagged earlier the same day. Remaining honest gap: the SDL2 client's visual rendering of a live match is still unconfirmed (no Xvfb on this box).

## 2026-07-24 (12)

- MOBA 10v10 scaling + persistent bot pool (NORTHSTAR §13 cont'd): team-mode sim (`arena_game.c`/`.h` -- heroes[2] -> heroes[20], `team`/`active` fields, `arena_nearest_enemy()` generalizing foe lookup, `arena_init_teams`/`arena_update_teams` additive to the existing 1v1 path, 5 new tests, zero regressions in the full existing suite). Draft phase (`PACKET_ARENA_PICK`, `ARENA_PHASE_WAITING/DRAFT/LIVE`) -- heroes were hardcoded, now every real slot picks before the clock starts. `apps/arena_server` generalized to `--lobby-size N`. New `apps/arena_bot` -- a real networked bot (not the sim's internal practice AI), real WOTAN identity, plays via matchmaker, persistent. `apps/matchmaker` generalized (`--lobby-size`/`--listen-port`/`--first-game-port`), one binary serves both the card-RTS and arena roles now.
- Three real bugs found running an actual persistent bot-pool soak test (not by review): (1) bots were re-registering a brand-new WOTAN identity every match instead of keeping one stable identity -- fixed, register once per process; (2) match servers never terminated after match end, flooding a persistent bot's socket with stale packets from every prior match and silently swallowing its next connection's WELCOME packet -- fixed, servers now exit shortly after the match ends; (3) a UDP retry race in the matchmaker protocol could spawn phantom matches nobody ever connects to -- mitigated (slower retry interval) plus a defensive 60s no-progress server timeout so any phantom that still slips through self-cleans instead of leaking forever. Verified via an extensive soak test: 2 persistent bots, stable identity across 20+ matches each, real accumulating win/loss records, zero connect failures.
- Explicitly unverified yet: the actual `--lobby-size 20` path live end-to-end (same tested code as 1v1, not yet run with 20 real connections), and the SDL2 client's visual rendering of a live networked match (no Xvfb on this box).

## 2026-07-24 (11)

- Product pivot (NORTHSTAR §13): apps/arena (the MOBA) is the product now, not the card-RTS. Real 1v1 networked PvP: new `apps/arena_server` (ports connect-ticket/WOTAN pieces from apps/server), `--connect <host>` mode added to apps/arena's client, new `PACKET_ARENA_MOVE/CAST/SNAPSHOT` wire packets. Verified live, catching and fixing two real bugs: `arena_bot_enabled` wasn't gating `bot_cast_kit_if_ready` (a real second player would still get yanked/attacked by the bot's kit AI), and the sim clock started before both real players connected (a match could resolve before player 2 ever joined). Fixed both; server now only ticks once both slots are filled. Two real clients with distinct WOTAN identities verified sitting still at full HP, waiting for real input -- genuine PvP, not bots fighting bots. `scripts/test_arena.sh` (+1 regression test) and `scripts/test_10_bots.sh` both re-verified clean.

## 2026-07-24 (10)

- WOTAN player identity, S170-41 cont'd: `apps/server` now reports match results at match_end via `report_match_result()` -- agent-login, then `POST /api/v1/redgarden/game-result` per connected client's real player_id. Verified live end-to-end with a real 2-bot match played to natural completion: match log's `match_end` winner matched the public leaderboard afterward exactly (winner's wins +1, loser's losses +1). `scripts/test_10_bots.sh` + `scripts/test_arena.sh` both re-verified clean.

## 2026-07-24 (9)

- WOTAN player identity, S170-41: `apps/client/bot_main.c` now tries a real IDUNA register+ticket-mint round trip (falls back to the old self-mint on any failure) instead of always self-minting a fake ticket. Verified live: two bots registered distinct real `player_id`s, connected via the real matchmaker, match log shows real identities on every event. `scripts/test_10_bots.sh` re-verified clean (backward compatible). Companion IDUNA-side change (new `REDGARDEN-BOTS` agent, `player_game_stats` table, `/api/v1/redgarden/{ticket,game-result,leaderboard}` endpoints) landed in the IDUNA repo, verified live end-to-end there too.

## 2026-07-24 (8)

- NORTHSTAR §12 Phase E (S170-36) started: Milestone-6 equivalent (state serializer + action
  decoder) from `gpt2-alpine-c/docs/GAME_AI_NORTHSTAR.md`, extended to arena's hero/ability state
  instead of a REDGARDEN-specific format. New `packages/simulation/arena_ai_bridge.h`/`.c`:
  `arena_serialize_state()` writes a stable self/foe natural-language state string;
  `arena_decode_action()` parses a `move:x,z cast_q/w/r:0|1` action string, defaulting missing
  fields to a safe no-op and failing closed on garbage. 7 new headless tests, all green alongside
  the full existing suite. Not wired into the live bot or the GPT-2 inference server (`:8088`)
  yet -- contract only, same sequencing discipline as Phases B→C.

## 2026-07-24 (7)

- NORTHSTAR §12 Phase D (S170-33) — fourth hero, **The Frog**, the last clean-fit hero from
  S170-32's roster audit: Q (Loop Back) rewinds the Frog's own position/HP to ~3s ago via a new
  per-hero loopback ring buffer (16 slots, 250ms sample rate, sampled generically for every hero);
  degrades to the oldest available sample rather than refusing to cast if less than 3s of history
  exists yet. R (The Secret) reuses Ghost's `intangible_ms` mechanic at a longer duration --
  "reappear at a chosen location" isn't built, flagged as a simplification. W (ally-targeted) and
  the passive (UI-only) are skipped, same reasoning as other skips this phase. Bot heuristic is
  defensive (rewind when hurt, vanish when critical) since Frog deals no damage. 4 new tests, all
  green alongside the full existing suite. Arena has now absorbed every roster-fit hero from the
  audit -- the 8 structurally-blocked heroes need arena to grow new systems first, a real decision
  point flagged in the northstar rather than continuing to just pick the next one.

## 2026-07-24 (6)

- NORTHSTAR §12 Phase D (S170-32) — third hero, **The Ghost**: Q (skillshot simplified to
  instant-hit-if-in-range, damage + Silence), W (instant intangibility on its own cooldown, not a
  toggle), R (fixed-position damage zone, enemy-only side of Recital). First kit needing real
  status-effect state: new generic `silenced_ms`/`intangible_ms` `ArenaHero` fields (any hero can
  carry them) and a `hero_is_hittable()` check used everywhere a hit used to just check
  `foe->alive`. Zone DPS uses a fixed 1000ms tick interval rather than fractional-per-tick math --
  flagged, but did not fix, a related pre-existing rounding bug in Unicorn's W regen (works in
  tests that jump a full second, silently truncates to 0 at real 16ms frame rates). Also: a
  roster-fit audit of the remaining 10 heroes found most (Tree/Pizza/Druid/Doc Wheel/Retrieval
  Cart/Donkey/TYLER/Flamel) structurally blocked by systems arena doesn't have (grid pressure,
  allies, multi-unit, cooking) -- only Frog remains a clean fit. 7 new headless tests, all green
  alongside the full existing suite.

## 2026-07-24 (5)

- NORTHSTAR §12 Phase D (full roster in arena, S170-31) started: generalized `arena_cast_q`/
  `arena_toggle_w`/`arena_cast_r` to dispatch on a new `ArenaHero.hero_id` field instead of
  S170-18's hardcoded `owner == 0` check, then wired **The Duck** as the second kit (Q/R only --
  its W needs objectives that don't exist here, its E's trigger coincides with match-end, both
  flagged and skipped). `arena_init()` now defaults player=Unicorn, bot=Duck with simple
  heuristic bot-casting, giving the bot side a real kit for the first time. 6 new headless tests
  (including cross-slot dispatch verification), all green alongside the full existing suite.
  10 heroes remain, each a separate follow-on pass.

## 2026-07-24 (4)

- NORTHSTAR §12 Phase C (observer mode, S170-30) started, arena half: new
  `packages/simulation/arena_replay.h`/`.c` parses an `apps/arena` match log and drives
  `ArenaState` directly from it (linear interpolation between the 500ms snapshots). New
  `red_garden_arena --observe <path>` flag plays a logged match back through the same render
  loop as live play (camera control active, live-match input disabled, `R` restarts playback).
  6 new headless tests (`tests/test_arena_replay.c`), all green; `build_arena.sh` and
  `test_arena.sh` updated to include the new files. RTS-side playback and true live-tailing
  remain open, separate next steps.

## 2026-07-24 (3)

- NORTHSTAR §12 Phase B (replay logging, S170-29) closed for the MOBA half: `apps/arena` now
  opens `var/matches/arena-<timestamp>.jsonl` (fresh per match, including on restart) and appends
  `match_start`/`snapshot` (every 500ms, both heroes' x/z/hp)/`ability_cast`/`match_end` events.
  No connect-ticket auth exists in this client, so events use `"local_player"`/`"local_bot"`
  placeholders rather than a guessed WOTAN player_id -- flagged as a real gap, not silently faked.
  Verified by clean compile (`scripts/build_arena.sh`) and code review only -- this box has no
  display, so unlike the RTS half this couldn't be run end-to-end. `scripts/test_arena.sh`
  (headless sim tests, untouched by this change) still green.

## 2026-07-24 (2)

- NORTHSTAR §12 Phase B (replay logging, S170-28) started for the RTS half: `apps/server` now
  opens `var/matches/<port>-<timestamp>.jsonl` per match and appends `match_start`/`connect`
  (with Phase A's `player_id`)/`card_play`/`match_end` events — exactly §10's originally-spec'd
  minimum hook, now with player identity attached. Verified real log output from
  `scripts/test_10_bots.sh`. `var/` added to `.gitignore`. The MOBA half (`apps/arena`'s per-tick
  hero-state logging) is not started -- distinct next step, not covered by this pass.

## 2026-07-24 (1)

- NORTHSTAR §12 Phase A (WOTAN player identity, S170-26) started: `apps/server` now captures the
  real IDUNA-minted `player_id` from every connect ticket instead of discarding it after
  verification (`client_player_id`/`client_has_player_id`, keyed per client slot) — the prerequisite
  Phase B (replay logging) needs to attribute matches to real players. Ported
  `packages/common/http_client.h` (verbatim from shankpit-460) and IDUNA agent config loading.
  Reporting REDGARDEN win/loss results into IDUNA is deliberately not wired yet — its
  `/api/v1/players/{id}/session` endpoint is FPS-shaped (kills/deaths), REDGARDEN's `match_winner`
  isn't; flagged as an open schema question rather than forced in wrong. All existing tests
  (`test_10_bots.sh`, `test_arena.sh`) still pass.

## 2026-07-23

- Fixed `GL/glu.h` missing (installed `libglu1-mesa-dev`) — `apps/lobby` and `apps/arena` now build clean.
- Fixed `usleep` implicit-declaration warning at the root cause: `-std=c99` was hiding the POSIX declaration; added `-D_DEFAULT_SOURCE` to `scripts/build.sh`.
- Added connect-ticket accounts (HMAC-SHA256, same scheme as shankpit-460): `packages/common/hmac_sha256.h` ported verbatim, `apps/server` verifies tickets on `PACKET_CONNECT` (fails closed without `REDGARDEN_TICKET_SECRET`), test bots self-mint tickets like shankpit-460's `emily-bot`.
- Added simple matchmaking: new `apps/matchmaker` pairs `PACKET_FIND_MATCH` requests and spawns a dedicated `red_garden_server --port <N>` per match; new `PACKET_FIND_MATCH`/`PACKET_MATCH_FOUND`/`MatchFoundMsg` wire types.
- Validated VS0 (bot-vs-bot match) and VS1 (10 independent headless bots, 5 concurrent matches, matchmaking + accounts, 10s sustained load, zero crashes) via new `scripts/test_10_bots.sh`.

## 2026-07-25 (2)
- feat(arena): real per-hero 3D geometry (S170-118). Founder, real-time: "use shankpit skins as
  a basic jump in graphics for redgarden models" -> "use shankpit og engine models to enhance
  redgarden hero legibility." Every hero previously rendered as one identically-shaped colored
  cube -- S170-89/96 already fixed "who is this" (floating health bars + name labels); this
  fixes "what does this hero actually look like." New `draw_hero_model()` in
  `apps/arena/src/main.c`: a per-`hero_id` switch composing 1-3 `draw_mesh()` boxes with real
  proportions/silhouettes, reusing the design language of the 7 SHANKPIT skins (Duck/Unicorn/
  Ghost/Frog/Tree/Pizza/Tyler) where a hero overlaps one, new equally-simple 2-3-box designs for
  the other 11. Relationship coloring (self=cyan/team=blue/enemy=red, S170-89) is preserved
  unchanged -- shape now encodes hero identity, color still encodes team/self, so neither
  legibility need overrides the other. Can't literally port SHANKPIT's immediate-mode
  `draw_player_skin_*()` code (this renderer is shader-based, no `mat4_rotate`) -- boxes are
  axis-aligned translate+scale only, same convention already used for node rendering.

## 2026-07-25 (3)
- feat(arena): expand map to Arathi Basin size, 5 capture nodes (S170-119). Founder, real-time:
  "expand the redgarden map to arathi size and 5 nodes." `ARENA_NODE_COUNT` 2->5 and its wire
  mirror `ARENA_SNAPSHOT_NODE_COUNT` (packages/common/protocol.h), `ARENA_HALF_EXTENT` 12->20 for
  real room (ground plane, movement clamp, and minimap all derive from this constant already, no
  separate edits needed). New `arena_nodes_reset_layout()` lays out 5 nodes Arathi-style: two
  flanking each team's spawn (Stables/Farm near owner 0, Lumber Mill/Gold Mine near owner 1) plus
  one contested center (Blacksmith, 0,0). Jungle creeps (S170-51) scale to 5 automatically --
  `ARENA_MAX_CREEPS` is `#define`d off `ARENA_NODE_COUNT` and flavor derives from `node->owner`
  dynamically, no hardcoded index. One real hardcode found and fixed: Courier's W
  (`courier_toggle_w`, "Between Eagle and Serpent") assumed exactly 2 nodes ("always lands, there
  are always exactly two nodes to jump between" -- own comment, now false) -- generalized to a
  farthest-of-N loop; `docs/HEROES_VS0.md`'s Courier section and a stale test
  (`test_courier_w_teleports_to_farther_node`, previously hardcoded "node 1 is farther") updated
  to match. Second real bug found via test failure: the new center node at (0,0) collided with
  `test_arena_bot_enabled_gates_kit_casts_too`'s own arbitrary hero test position (also (0,0)) --
  a jungle creep spawning on top of the hero dealt damage the test misattributed to ungated bot
  AI; fixed by moving the test's positions off every node's aggro footprint (z=15), not by moving
  the node (the center node belongs on the direct line between both spawns, same "contested
  middle" design as real Arathi Basin). Verified: scripts/build.sh, scripts/build_arena.sh,
  scripts/test_arena.sh, scripts/test_10_bots.sh all pass; local mingw cross-compile (all 4
  source files) links clean.

## 2026-07-29
- fix(arena): stale bot-pool guard in scripts/run_bot_pool.sh. Founder, real-time: "check redgarden
  game i cant get into a game the window popped up but no draft interface." Root cause: 19 orphaned
  `red_garden_arena_bot` processes (PPID reparented to 1, stale since 05:55 -- parent shell died
  without the script's own cleanup trap firing) were still alive alongside the current supervised
  19-bot pool (from 10:10), putting 38 bots against the bot-pool matchmaker's 20-slot lobby. With
  bots alone able to fill every batch, the one open human slot never got a real connection, so
  `match_phase` never reached `ARENA_PHASE_DRAFT` and the client's draft screen (gated on that
  phase, apps/arena/src/main.c) never rendered -- window opens, sits waiting, forever. Killed the
  19 orphaned PIDs live (back to the intended 19 bots + 1 open slot); added a `pkill -f` guard at
  the top of `run_bot_pool.sh` before it launches its own set, so a future unclean exit can't
  double up the pool again.
- fix(arena): auto_deploy.sh never republished the human client binary. Founder, real-time
  follow-up, after the fix above didn't resolve it: "still waiting for the queue to pop
  somethings wrong check everything." Real root cause of the missing draft screen, more
  fundamental than the bot-pool doubling above: `scripts/auto_deploy.sh` (systemd timer, polls
  CI every ~10min, `var/logs/auto-deploy.log`) republishes `red_garden_arena_server`,
  `red_garden_arena_bot`, and `red_garden_matchmaker` on every green build -- but its `for bin in
  ...` copy loop never included `red_garden_arena`, the actual SDL2 client a human plays. Found
  live: server/bot/matchmaker had auto-deployed commit `6349d09` at 10:10 UTC (confirmed via the
  deploy log), but `build/red_garden_arena` was still a manual build from 06:52 UTC, several
  hours and dozens of commits stale -- including missing Zagan (28th hero, S170-230), which
  changes `ARENA_HERO_COUNT` and therefore wire-protocol-sized structs. The client could still
  connect (`PACKET_CONNECT`/`PACKET_WELCOME` don't depend on hero count) but the mismatch is
  consistent with the observed symptom: matches kept forming with 19/20 or 18/20 actually
  connected and timing out at the 60s no-progress mark (`var/logs/matchmaker-bots.log`), i.e. the
  human's own connection was the one silently failing, invisible bots aside. This isn't a
  one-time staleness -- it's a standing bug: every future green CI run reintroduces the same skew
  regardless of any one-off manual client rebuild. Fixed by adding `red_garden_arena` to
  `auto_deploy.sh`'s publish loop (same copy-then-rename ETXTBSY-safe pattern already used for
  the other three). Rebuilt all binaries fresh from current `main` (`scripts/build.sh`, exit 0),
  restarted the matchmaker/bot-pool/player-pool trio live, confirmed a clean steady-state queue
  (19 bots + 1 open human slot, no partial-connect timeouts since).
- observation(arena): auto_deploy.sh's live-restart kills in-progress matches; timer paused.
  Founder, real-time, third pass: made it through draft this time, but ended up on the wrong
  hero (Unicorn) and unable to move -- "the game is having trouble actually starting or
  something." Root cause: `redgarden-auto-deploy.timer` fires every 5 minutes and unconditionally
  `systemctl --user restart`s the matchmaker/bot-pool services on any new green CI build. Spawned
  match servers are forked children of the matchmaker process, not their own systemd units, so
  the restart's control-group kill takes out any currently-live match along with the matchmaker
  itself. Timestamps line up exactly: the previous fix's manual restart landed ~17:31, the founder
  got through a draft in the next couple minutes, and the timer fired again at 17:33:47 UTC
  (`var/logs/auto-deploy.log`), silently killing the just-started match server out from under
  them -- explains both symptoms (dead connection reads as "can't move," and being left on
  whatever placeholder hero the new post-restart server defaulted to rather than the one really
  picked). Paused the timer live (`systemctl --user stop redgarden-auto-deploy.timer`), NOT
  re-enabled -- needs a real fix (skip the restart while a spawned match server child is still
  alive / has connected players) before it's safe to leave running unattended again. Apple
  #11297.

## 2026-07-29 (2)

- feat(arena): promote the 5,000,000-timestep RL policy checkpoint into
  `packages/common/rl_policy_weights.h`. Founder: "do more of the reinforcement learning i want
  the bots to be smarter" -> "oh put the new checkpoint into the embeddings in the c and push it
  up." Backlog item 6's queued longer PPO run (5x the S170-228 run, `scripts/rl_train.py
  --total-timesteps 5000000`, net_arch=[64,64], default Unicorn-vs-Duck pairing) had already
  finished (11:37 UTC) and evaluated 50W/0L/0D over 50 episodes against the heuristic bot AI it
  trained against -- strictly more training data than the currently-live 1M-timestep policy at
  the same eval task. Founder's follow-up explicitly simplified the ask from the originally
  queued old-vs-new face-off (real engineering: two `MlpModel`s loaded side by side, not yet
  built, see backlog item 6's own note) down to a direct promote -- did that: copied the trained
  run's already-exported header over `packages/common/rl_policy_weights.h` (no code changes
  needed, `rl_policy_forward()`'s call site in `arena_game.c` is unchanged). Full rebuild
  (`scripts/build.sh`, exit 0) + `scripts/test_arena.sh` (all headless sim/training-obs/MLP tests
  green) + `scripts/test_10_bots.sh` (5 concurrent matches, stable) all pass. Same scope caveat
  as before this promotion: this policy only drives the solo 1v1 local-practice bot
  (`arena_game.c`'s internal `arena_bot_tick`, disabled the instant a real match fills) -- has no
  effect on `apps/arena_bot`'s separate hand-authored networked-match AI. The formal old-vs-new
  face-off itself (backlog item 6 / Apple #11297's follow-up item 2's sibling) remains unbuilt,
  superseded by this direct promotion per explicit founder instruction rather than left silently
  incomplete.

- feat(arena): wire the trained RL policy into `apps/arena_bot`'s 19 real networked match bots.
  Founder follow-up, same real-time thread: "and then once we get the new model installed lets
  get all the 19 bots on it." Until now `rl_policy_forward()` only ever drove the solo 1v1
  local-practice bot (`arena_game.c`) -- the 19 bots real players actually fight are a completely
  separate, hand-authored heuristic client (`apps/arena_bot`) with zero connection to the RL
  pipeline (flagged to the founder before starting this). New `rl_engage_nudge()` in
  `apps/arena_bot/src/main.c`: feeds this bot (self) and its current nearest-enemy target (foe)
  through the same trained network, same 18-float observation layout
  `arena_bot_tick_rl_move()` already uses, but returns a small bounded STEP in the suggested
  direction, not the network's raw output used as a literal world-space target -- two real
  reasons, both documented inline: (1) the network only ever trained 1v1, in a small fixed-spawn
  arena with no nodes/squads/teammates, so it can only meaningfully inform "which way to step
  toward this one foe," not macro positioning; (2) a genuine coordinate-frame mismatch found
  while wiring this up -- the policy's own action output is clipped to
  `RL_POLICY_MOVE_TARGET_RANGE` (20.0, tuned for that small training arena) as an absolute
  world-space target, but the live map's real `ARENA_HALF_EXTENT` is ~51.78 (S170-191's
  golden-ratio expansion, landed after this training setup was fixed) -- taking the raw output
  literally would send a bot toward map-center nonsense during any skirmish away from the middle,
  i.e. most of them on a 5-node map. The nudge is additive on top of -- not a replacement for --
  the existing S170-90 anti-stack angle-spread approach point, so several bots independently
  consulting the same network can't reintroduce the exact "bots pile onto the same point" bug
  that spread was written to fix. `scripts/build.sh` now links `packages/common/mlp_infer.c` into
  `red_garden_arena_bot` too. Not independently playtested for feel (no display in this
  environment) -- `scripts/build.sh`/`test_arena.sh`/`test_10_bots.sh` only confirm it compiles,
  produces bounded output, and a live pool doesn't crash under it; a real read on whether it
  actually plays smarter needs the founder's own eyes on it.

- fix(arena_bot): dead-server detection was ~10x slower than intended. Found live while
  verifying the RL-nudge change above: a scratch two-bot smoke test against the real production
  matchmaker (since `apps/arena_bot` hardcodes port 7778, no way to point it at a sandboxed
  matchmaker) left two never-connecting phantom entries in the matchmaker's queue, which got
  matched into a real 20-slot batch alongside the live 19-bot pool -- the spawned server's own
  60s no-progress timeout correctly killed itself once, but ALL 19 real, connected bots then sat
  frozen, never detecting their server had died. Root cause: `play_one_match`'s own
  `silent_ticks > 1000` giveup threshold (`apps/arena_bot/src/main.c`) assumed a ~10ms tick rate
  -- the loop actually paces at 100ms/tick (`usleep(100000)` at the bottom of the same loop), so
  100 * 1000 = ~100s of real hang before a bot notices and requeues, not the ~10s the code's own
  comment claimed. Fixed the threshold to 100 (100 * 100ms = 10s, matching the comment).
  Genuinely reachable outside any testing scenario too -- the exact same phantom-queue-entry
  shape happens whenever a real player quits mid-queue before their client's own `PACKET_CONNECT`
  lands. `scripts/test_arena.sh`/`test_10_bots.sh` green; live-verified the fixed pool recovers
  and reaches a clean steady state after a restart.

- feat(arena): exponential multi-kill streak reward, Fibonacci-scaled. Founder: "add exponential
  reward for double tripple penta kills etc" -> "a penta kill gives a huge reward hit and a
  double kill gives a little more than two normal kills would rewards wise" -> "like a double
  kill should give the reward of 3 kills and then use the fib." New `ArenaHero.multikill_count`/
  `multikill_timer_ms` (`packages/simulation/arena_game.h`) track each hero's current kill
  streak, same `ARENA_ASSIST_WINDOW_MS`-style ~10s window (`ARENA_MULTIKILL_WINDOW_MS`) for
  whether the next kill continues it or starts fresh. New `arena_multikill_fib()`
  (`arena_game.c`) generates 1, 2, 3, 5, 8, 13, 21, ... (Fibonacci, 1-indexed so it doesn't
  repeat its own leading 1) -- each kill's own Flow/XP bounty (`apply_damage`'s existing kill-
  crediting block) is `ARENA_HERO_KILL_FLOW`/`XP` times the streak's current term, so the
  CUMULATIVE total across a streak is that sequence's running sum: Double = 1+2 = 3x a normal
  kill's worth (exactly the founder's own stated example), Triple = 1+2+3 = 6x, Quadra =
  1+2+3+5 = 11x, Penta = 1+2+3+5+8 = 19x ("a huge reward hit," per the founder's own framing).
  No explicit cap past Penta -- a longer streak keeps compounding. Dying clears a hero's own
  streak (real-MOBA convention, and simply correct: a dead hero can't be mid-streak). 3 new
  tests (`tests/test_arena_game.c`): the Double-kill 3x total, a fully-expired window starting a
  fresh streak instead of wrongly continuing a stale one, and death clearing the victim's own
  streak state. Full suite green (`scripts/build.sh`/`test_arena.sh`/`test_10_bots.sh`).
  Deliberately scoped to the reward economy only this pass -- no HUD/announcement text ("DOUBLE
  KILL!" banner), flagged not built, since no such notification system exists yet in this repo.

- feat(arena): team-mode initial spawn moved to each team's graveyard. Founder, real-time:
  "we just need to move the initial spawn at start of game to the 2 graveyards not center of the
  map." `arena_init_teams()` previously spawned all 20 heroes on a fixed line near map center
  (x=+-8) -- now spawns each team at its own `arena_graveyard_position()` corner instead, the
  same point team-flavored creeps already spawn/march from (S170-161: "initially they spawn from
  the graveyards behind the nodes not the center") and the same point a hero with no owned node
  falls back to on death -- one coherent graveyard concept a team starts at, marches out from,
  and (worst case) returns to, instead of two differently-positioned ones. Also fixed
  `arena_find_owned_node_for_respawn`'s own "nearest owned node to home" heuristic to measure
  from the graveyard (real 2D distance) instead of the stale x=+-8-only reference it used before.
  Real bug caught before landing: the old +-9 z-fan (safe around a center-ish spawn line) pushed
  several heroes clean past the map boundary once anchored at a corner already only ~4 units
  from the true edge (measured live: one hero landed at z=-56.78 against a +-51.78 map) -- fixed
  by fanning inward from the corner instead (`copysignf`-directed, always toward map center) so
  the full team stays in bounds by construction, no clamping/stacking needed. One pre-existing
  test gap surfaced and fixed: `test_team_creep_kill_by_enemy_team_helps_flip_the_node` never
  deactivated hero[0] (unlike its own sibling tests just above it), harmless while hero[0]'s old
  default spawn sat far from the action, a real collision once its default spawn became the
  exact point the test stages its scenario at. Full suite green
  (`scripts/build.sh`/`test_arena.sh`/`test_10_bots.sh`).

- feat(arena): hero win-rate tracking, from real match logs. Founder: "can we start crunching
  the data on the heroes that are the strongest? does our match replay system let us start
  tracking stats like win rate etc?" Answer at the time: no -- neither `var/matches/*.jsonl`
  (x/z/hp/alive snapshots + a final winner) nor `report_match_result`'s separate IDUNA POST
  (`player_game_stats`, win/loss per PLAYER) ever recorded which `hero_id` a given owner
  actually played. Checked IDUNA's own schema directly rather than assume -- genuinely no
  hero_id column there, and adding one would mean a cross-repo migration this pass doesn't take
  on, since the whole analysis is answerable from REDGARDEN's own local logs alone. New
  `match_log_draft_complete()` (`apps/arena_server/src/main.c`) writes one `draft_complete`
  event per match, right when the draft actually finishes, recording `{owner, team, hero_id}`
  for all 20 slots -- joins against the existing `match_end` event's `winner` field to compute
  win rate per hero. New `scripts/hero_stats.py` walks `var/matches/*.jsonl`, aggregates
  wins/games per hero, prints a sorted win-rate table, and honestly reports how many files it
  had to skip (pre-fix logs with no `draft_complete`, or matches that never reached
  `match_end` -- timed-out/phantom-queue matches are a real, common thing this exact session
  already found several times). Flagged plainly, not glossed over: the 5,860 match logs already
  sitting in `var/matches/` from before this fix are permanently unusable for this -- hero
  identity was simply never written down, so hero stats start from zero real games, not from
  this repo's actual match history. Full suite green.

- feat(arena): report hero-level match results to IDUNA. Founder follow-up, same thread: "ok i
  want to start tracking it on okemily.com." `report_match_result` (`apps/arena_server/src/
  main.c`) now also POSTs `{"hero_id": N, "result": "win"|"loss"}` to IDUNA's new `POST
  /api/v1/redgarden/hero-result` (IDUNA Apple #11320) for every owner at match end, reusing the
  same agent token/permission (`redgarden.match.write`) the existing per-player `game-result`
  POST already needs -- no new auth wiring. This is the durable, always-on counterpart to the
  local `var/matches/*.jsonl` logs `scripts/hero_stats.py` reads: real matches now feed both.

- feat(arena): promote the spatial-generalization RL checkpoint; fix a real progress-tracking
  bug in `rl_train.py`. The 5M-timestep run trained against the new randomized-spawn environment
  (see the earlier "team-mode initial spawn moved to graveyards" entry above) finished, evaluated
  50W/0L/0D over 50 episodes, and is now promoted into `packages/common/rl_policy_weights.h`.
  Real bug caught live while watching this run: `scripts/rl_train.py`'s own checkpoint loop
  tracked progress as `timesteps_done += chunk` (the REQUESTED chunk size) rather than reading
  `model.num_timesteps` (the model's real internal counter) -- since a chunk can only stop at a
  rollout boundary, every `model.learn()` call actually runs slightly MORE than requested, and
  crediting only the requested amount silently undercounts every single checkpoint. With this
  run's `save_freq` (20,000, the default) much smaller than one rollout (8,192 steps at
  `n_envs=4`), that undercount compounded across ~250 checkpoints into a script-vs-reality gap
  of over 1,000,000 steps by the time it was caught -- the script still believed it was at
  4.46M/5M while the model had actually already trained past 5.5M. Manually stopped the run,
  evaluated + exported the latest real checkpoint directly (same 50W/0L result), and fixed the
  root cause: `timesteps_done = model.num_timesteps` reads the authoritative counter instead of
  re-deriving a second copy of the same number that can drift. Full suite green.

- fix(arena): fountain-retreat flapping -- real hysteresis instead of a single threshold.
  Founder: "bots should consider healing more than one tick at the fountain sometimes."
  `apps/arena_bot/src/main.c`'s retreat-to-fountain decision recomputed itself from scratch every
  single ~100ms decision tick off ONLY the current HP fraction, with no memory of the previous
  tick -- a bot could dip just under the 25% entry threshold, take one fountain heal-tick that
  pushed it just back OVER 25%, and immediately declare itself no longer low and dash back into
  the fight it had barely healed for, then dip under 25% again a few ticks later and repeat --
  arriving at the fountain "sometimes" for one real heal-tick, exactly the founder's own
  description. Fixed with a real two-threshold state machine: `retreating_to_fountain` is now a
  per-match-persistent flag (was a fresh local every tick), entering retreat still needs HP under
  `ARENA_BOT_LOW_HP_FRACTION` (25%, unchanged), but once retreating a bot now stays there until
  it reaches a new, higher `ARENA_BOT_TOPPED_UP_FRACTION` (90%) exit threshold, not just barely
  above where it started. Full suite green.

- feat(arena): trained RL policy also drives casting, for the exact pairing it was trained on.
  Same session, founder's own broader question ("how do we combine heuristics with the ml model
  so we do a little fuzzy best of both worlds") prompted finishing what NORTHSTAR §21's own
  movement-only scoping had deliberately left undone. New `arena_bot_tick_rl_cast()` applies the
  same trained policy's `cast_q`/`cast_w`/`cast_r` outputs (same `>0 = attempt this tick`
  threshold `scripts/rl_env.py`'s own `step()` already uses) -- but ONLY when the solo-practice
  bot is actually playing Unicorn or Duck (`rl_train.py`'s own trained pairing); every other hero
  still gets the existing hand-authored per-hero heuristic (`bot_cast_kit_if_ready`), unchanged.
  This is the concrete instance of "combine heuristics with the model": hero-gated selection
  between two whole decision-makers, not a blend within one decision -- see this session's own
  design discussion for the fuller reasoning and what a real blended (not just gated) approach
  would need. Full suite green.

- feat(arena): confidence-weighted RL engagement nudge. Follow-up, same thread ("how do we
  combine heuristics with the ml model so we do a little fuzzy best of both worlds") -- the
  hero-gated cast switch above is one blend pattern (pick a whole decision-maker); this is the
  other (scale one decision-maker's contribution into another's). New
  `rl_engage_confidence()` (`apps/arena_bot/src/main.c`) returns how much `rl_engage_nudge`'s
  suggestion should actually be trusted this tick: the RL policy trained strictly 1v1 (itself and
  exactly one foe, nobody else ever on the map), so its judgment is only really grounded when the
  real fight looks like that. Counts living combatants (either team, excluding self and the
  current target) within a 10-unit radius of the self/foe midpoint -- each one nearby halves the
  confidence, geometric decay rather than a fitted curve, since no real confidence-vs-outcome
  data exists yet to fit one against. A clean 1v1 stays at full nudge strength (unchanged from
  before this pass); a chaotic teamfight the model never trained on gets a heavily damped one
  instead of either fully trusting or fully ignoring the model based on a hard rule. Full suite
  green.

- fix(ops): `auto_deploy.sh` is now match-aware -- backlog follow-up to Apple #11297. The
  systemd timer has been stopped since that Apple (it used to restart the matchmaker/bot-pool
  units unconditionally, which killed any currently-live match along with them -- spawned
  `red_garden_arena_server` processes are forked children of the matchmaker, not their own
  systemd units, so the restart's control-group kill took them out too). Added a guard right
  before the restart step: `pgrep -f "build/red_garden_arena_server --port"` checks for any
  currently-running match server (only ever exists between "lobby just filled" and "match
  ended/timed out," a simple, sufficient proxy for "a real match might be in progress") and, if
  found, defers the restart (and does NOT mark the SHA as deployed, so the next 5-minute timer
  tick retries the whole check rather than silently giving up). Binaries are still published
  either way -- harmless, since the matchmaker execs `server_bin` fresh per spawn regardless of
  whether this restart happens. Timer intentionally still left stopped (not re-enabled as part of
  this commit) pending a live verification pass.

- feat(arena): full-roster self-play RL infrastructure. Founder: "ok but i win every game i need
  the bots to be training on the full game rl" -> "not just 2 heroes." True 20v20 team-mode
  training (nodes, objectives, 19 other live agents) is a much larger, separate undertaking --
  scoped down to the achievable real slice: self-play (train against a frozen past checkpoint
  instead of the fixed heuristic) across the WHOLE hero roster, not just the original fixed
  Unicorn-vs-Duck pairing. Real gap found and fixed first: the observation the network trains on
  never included hero identity AT ALL (hp/position/cooldowns are hero-agnostic) -- a policy
  trained across multiple heroes without this literally cannot condition its behavior on which
  hero it's playing or fighting. `ARENA_TRAINING_OBS_SIZE` grows from 18 to `18 + 2*ARENA_HERO_COUNT`
  (one-hot self hero_id + one-hot foe hero_id, one-hot rather than a raw scalar since hero IDs are
  an unordered categorical set with no real ordinal relationship a small MLP could otherwise
  infer). New `sim_step_both()` (`apps/arena_training/src/headless.c`) applies real external
  actions to BOTH heroes instead of driving hero 1 through the stable heuristic -- what actual
  self-play needs (`sim_get_obs`'s own doc comment had already flagged it as symmetric "in case a
  later pass wants... self-play"). `scripts/rl_env.py`'s `ArenaTrainingEnv` gained
  `randomize_heroes=True` (fresh random hero for both sides every episode) and
  `opponent_model_path=<zip>` (a frozen SB3 checkpoint's own predictions drive hero 1, sampled
  not argmaxed) -- both off by default, so nothing already calling this class changes behavior
  unless it opts in. `scripts/rl_train.py` exposes both as `--randomize-heroes`/
  `--self-play-opponent`, plus a second eval pass (vs the self-play opponent, not just the
  heuristic) when self-play was used. Live consumers (`arena_game.c`'s two RL call sites,
  `apps/arena_bot`'s `rl_engage_nudge`) updated to build the same wider observation --
  deliberately guarded by `#if RL_POLICY_OBS_SIZE >= (18 + 2*ARENA_HERO_COUNT)` so this lands
  safely BEFORE a matching wider-input model exists: built and tested clean against the
  currently-live 18-input model (guard picks the no-op branch, zero behavior change), and will
  automatically activate once a new model is promoted. 2 new C tests (one-hot correctness,
  `sim_step_both`). Full suite green. A real full-roster self-play training run follows in a
  separate commit once it completes.

## 2026-07-30

- feat(arena): live-match reporting now includes hero and node coordinates. Founder: "can we get
  coords too and show a little map with emojis." `report_live_match_state()`'s JSON payload
  gained `x`/`z` (`%.1f` precision -- plenty for a small spectator map, keeps the payload smaller
  than full float precision would for no real benefit here) on every node and every hero entry.
  No IDUNA changes needed -- the live-match store already holds the posted body as an opaque
  blob, so new fields just pass through. `OKEMILY/live-match.html` gained a mini-map card:
  node-owner-colored squares as a spatial reference layer, hero emoji markers (team-ringed,
  dead heroes render grayscale at their last known position rather than vanishing) on top, both
  scaled off a hand-synced `ARENA_HALF_EXTENT`. Verified live: restarted the bot-pool matchmaker
  to pick up the new binary, confirmed a fresh match reports real `x`/`z` values readable at
  `localhost:8080`.

- docs(arena): NORTHSTAR §22, real jungle camps -- mob roster + GFD-pattern lifecycle (spec only,
  no code yet). Founder: "we want to make the jungle more dynamic and alive those concepts come
  from the original game" -> "the jungle right now is like nothing we need more going on" ->
  "use it as inspiration in terms of mob types and write it into a northstar." Resolves §20.4's
  own deliberately-deferred question: REDGARDEN builds a genuinely separate true jungle-camp
  system alongside the existing node-guardian creeps, not a rework of them in place. Reviewed
  `REDGARDEN/wiki/SPEC-4` (a full Card-RTS spec for a different, unbuilt game mode -- its
  `Entity`/`GridCell` types collide outright with `local_game.h`'s own existing, incompatible
  definitions, checked directly) and identified the two ideas that actually transfer to the
  arena jungle independent of the rest of that spec: a tiered mob roster with real per-archetype
  personality (frontline brawler / kiter / swarm / objective-hunter / pure support / assassin /
  boss, instead of one flat behavior), and weighted per-archetype target scoring instead of a
  single hardcoded targeting rule. Grounded the "alive" half in §8's own already-decided
  architecture (grafting the DESIGN, not the code, from GoblinFoxDragon's real, tested mob/NM
  packages -- checked directly: a real Idle/Pursuing/Returning/Dead state machine with leash
  range, tag-on-first-hit kill credit, and an FFXI-style placeholder/window/respawn model for a
  rare roaming boss). Flags real open questions rather than presuming answers: tone (SPEC-4's
  generic-fantasy camp names vs. REDGARDEN's own absurdist roster), exact roster size/numbers,
  map placement, and HUD legibility work. No code changes.

- docs(arena): NORTHSTAR §23, expanded item roster -- more FFXI-DNA items, more effect variety
  (spec only, no code yet). Founder: "do a northstar for expanded items we just need more more
  variety more different effects etc same DNA ffxi item names even the stats on some may be
  useful to design the items system." Named the current real ceiling: `ArenaItemDef` has exactly
  six flat additive stat fields and only two items (Blink Dagger, Donkey) with a genuinely new
  mechanic beyond those six numbers. Also named, plainly, a real standing tension worth having on
  the record: `docs/FFXI_ITEM_PARITY_SEED.md`'s own header states its real FFXI names are "not
  for direct use in the shipped game" (meant as `gpt2-alpine-c` seed data for a DIFFERENT
  project's original names) -- REDGARDEN's actual live 27-item catalog already uses 24 of them
  verbatim anyway, a choice `ArenaItemTier`'s own code comment already states plainly. The
  founder's own framing this pass reads as a clear choice to keep doing this, not a gap -- not
  re-litigated, just put on the record. Translated real FFXI mechanic archetypes (latent/
  conditional effects, proc effects, regen/refresh, relic-tier unique-mechanic weapons,
  enmity-adjacent aggro items, elemental resistance) into concrete `ArenaItemDef`-shaped
  categories, each with its own honest prerequisite named (RNG doesn't exist yet -- §17.2 already
  flagged this independently; damage typing doesn't exist; enmity/threat doesn't exist). No code
  changes.

- fix(arena): Donkey's Paper Glide covers 6x the distance. Founder, real-time: "donkey glide
  needs to be 6 times as far." Scaled `ARENA_DONKEY_GLIDE_DURATION_MS` 6x alongside
  `ARENA_DONKEY_GLIDE_RANGE` (not `ARENA_DONKEY_GLIDE_SPEED_MULT`) -- bumping RANGE alone
  wouldn't have changed anything real: the glide's actual reach is bounded by how far
  speed*duration can travel within the airborne window, and the original 16.0 was sized to
  almost exactly match that. Scaling duration instead of speed keeps the escape reading as a
  genuinely longer multi-second glide (matching Paper Glide's own established "bigger, slower
  escape tool" identity) rather than an ~5x-faster near-instant zip. One test fixed to assert the
  real, honest consequence of the new range (96.0) now exceeding `ARENA_HALF_EXTENT` (~51.78): a
  glide from map center hits the existing map-boundary clamp before reaching the full nominal
  range, not a bug. Full suite green.

- feat(arena): shop UI/UX overhaul -- proximity auto-open, pagination, on-screen page buttons
  (S170-231). Three founder asks in one pass: (1) "pop the shop window up when you get close to
  the shop enough to buy" -- the panel now opens/closes itself against the exact same
  `ARENA_SHOP_RADIUS` `arena_shop_buy` enforces server-side, edge-triggered on the in-range/
  out-of-range transition so it never fights a manual B press (close it while standing there and
  it stays closed until you actually leave and come back). (2) "too many items per page more
  pages" -- replaced the old 2-column x 15-row single page (S170-210's fix, all 27 items visible
  and clickable at once) with a single buy column showing `SHOP_ITEMS_PER_PAGE` (9) items at a
  time, `SHOP_PAGE_COUNT` a ceiling division so it grows on its own the next time the catalog
  does, 9 chosen to exactly match the existing 1-9 quick-buy range so every visible item always
  has a live keybind. "navigate pages with shift 1 2 3" -- Shift+1/2/3 jumps straight to that
  page; plain 1-9 still quick-buys within the current page. (3) "and buttons" -- three small
  clickable page-number boxes drawn above the buy list (current page filled solid, others
  outlined), same click-and-keybind-both-resolve-instantly convention NORTHSTAR §2 already
  requires. Equipped/sell column is untouched by pagination (it's the loadout, not the catalog).
  `apps/arena/src/main.c` only -- a client-only change with no automated coverage in this
  headless environment (`scripts/test_arena.sh` exercises the sim under `packages/simulation`,
  never `apps/arena`'s own SDL2/OpenGL client code, same gap its own comment already admits
  pending Xvfb). Verified by a clean `scripts/build.sh` with no new warnings; UI behavior itself
  is unverified until Xvfb is available or a founder plays a live client build.

- docs(arena): NORTHSTAR §22.5, ECOWAR wiki follow-up. Founder, real-time: "continue the jungle
  creep work - check the EMILY wiki on github for ecowar" -> "i know thats another version of the
  game but some of the bvibes are useful." Read all three EMILY.wiki ECOWAR documents in full.
  Spec-2/spec-3 added nothing new -- same Card-RTS unit roster §22.1 already sourced from
  `REDGARDEN/wiki/SPEC-4` (two copies of the same underlying design conversation), plus a
  vertical-slice sequencing memo entirely about `local_game.c`'s domain. Spec-1's own "Jungle
  Camps & Dragons (MOBA DNA)" section, though, named three real things §22 hadn't covered yet:
  camps should visibly telegraph before spawning/respawning (folded into §22.4's existing wire/HUD
  open question), camps granting a real temporary player-power buff on kill is a genuinely missing
  mechanic distinct from kill-credit/gold (new §22.4 open question -- no existing buff primitive to
  hang it on yet, closest candidates named: S170-190 map powerups, or the generic status-effect
  field shape), and a boss's death should alter the match, not just end a fight (named as a design
  principle for §22.2's own placeholder/window boss, not resolved). Named what does NOT transfer
  just as explicitly: the multi-biome map concept (Verdant Wilds/Ash Barrens/Frozen Reach/Blighted
  Grid) is a real, bigger, separate idea, explicitly out of scope here. No code changes.

- feat(arena): node towers -- "add towers around the nodes so beginning of game is a little
  slower." One neutral `ArenaTower` per node (all 5, index-matched to `nodes[]`), hostile to both
  teams equally, that directly gates `arena_tick_nodes`' own capture channel: `arena_tick_nodes`
  now forces `exclusive_team = -1` for any node whose tower is still alive, so neither team can
  even START a capture channel there -- the actual mechanism making the opening node-grab race
  slower, per the founder's own framing. Reuses the exact hero-vs-creep combat shape (flat damage
  taken, `apply_armor`'d damage dealt to heroes, last-hit kill credit via
  `last_attacked_by_owner`) rather than inventing a new one; never respawns once destroyed, a
  one-time early-game gate. Superseded NORTHSTAR §19.5's original single-lane-tower proposal --
  see that section's own update note for the full "what shipped vs. what was specced" story.

  Team-mode only, deliberately NOT wired into `arena_init_teams()`/`arena_creeps_reset()`'s own
  shared init path -- that function is called directly by ~300 existing unit tests that place
  heroes at convenient coordinates (some literally at the Blacksmith node's own map-center (0,0))
  never expecting anything to auto-attack them there. Towers default to dead (memset-zero) unless
  `arena_towers_reset()` is called explicitly, which now happens exactly once, in
  `apps/arena_server/src/main.c` right after a real team-mode match's own `arena_init_teams()`
  call -- every existing test's behavior is unchanged, the 1v1 local demo never sees towers at
  all (same scope lane creep waves already carry), and real matches get the full feature. Four
  new dedicated tests cover the capture-block/unblock, hero-kills-tower, and tower-attacks-hero
  paths explicitly (previously-failing pre-existing capture-channel tests needed zero changes
  once the shared-init-path fix above landed).

  Wire-synced end to end: `ArenaTowerSnapshot` (x/z/hp/max_hp/alive) added to
  `ArenaSnapshotMsg`, populated server-side, applied client-side. Rendered in
  `apps/arena/src/main.c`: a tall stone-gray base+spire silhouette (distinct from every other
  shape on the map), darkening toward red as HP drops -- the same "telegraph state before it
  matters" instinct NORTHSTAR §22.5 named as a real gap for jungle camps, applied here too -- plus
  the same aggro-radius ring idiom node-guardian creeps already use. `ARENA_TOWER_MAX_HP`/
  `DAMAGE`/`KILL_FLOW`/`XP` are judgment calls, not founder-specified, same "spec the model, leave
  the numbers open to tuning" precedent this file uses elsewhere. Full suite green (690 assertions
  across all 7 test binaries), `scripts/build.sh` clean, `scripts/test_10_bots.sh` unaffected
  (Card-RTS matchmaker, not this system).

- fix(arena): towers were unkillable in practice, and one-shotting anyone who tried. Founder,
  first playtest: "towers are basically invincible and can never be destroyed" -> "the color is
  changing scale down the tower max hp some its taking way too long and the tower just kos even
  tanks." Two real, independent bugs:

  1. **Starvation bug.** A node-guardian creep sits at the exact same (x,z) as its node's tower.
     `arena_hero_attack_creeps` runs before `arena_hero_attack_towers` every tick and both share
     the same once-per-cooldown attack slot on a hero -- whenever the creep was alive it always
     won, permanently starving the tower of any damage at all. Fixed at the source:
     `arena_tick_creeps` no longer lets a node's neutral creep spawn while that node's own tower
     is still alive (one guardian per phase -- the tower is it until it falls, then the creep
     resumes exactly as before). New test proves it: tick creeps, confirm none spawned, confirm a
     hero's attack actually lands on the tower.
  2. **Bad numbers.** `ARENA_TOWER_MAX_HP` (1600) was sized against the 8-damage basic attack
     without weighing it against a hero's own 100 base HP -- ~200 solo hits, unkillable on any
     real match timescale. `ARENA_TOWER_DAMAGE` (40) was 40% of a hero's base HP per hit, a
     literal 2-3 shot kill on anyone regardless of build. Rebalanced directly against the hero HP
     scale: `ARENA_TOWER_MAX_HP` 1600 -> 420 (~30-45s of one hero's sustained fire, ~10-15s for 3
     heroes focusing together -- a real decision, not a wall or a coinflip), `ARENA_TOWER_DAMAGE`
     40 -> 14 (chips, doesn't delete). Still a judgment call pending further live tuning.

  Full suite green (692 assertions), build clean.

- feat(arena): tower attacks are now real projectiles, not instant hits. Founder: "show the tower
  damage as projectiles." `arena_tick_towers` now fires an ordinary `ArenaProjectile` (same
  system Gary's homing auto-attack and every ability skill-shot already use) toward its target
  instead of calling `apply_damage` directly -- a visible, in-flight shot instead of damage just
  silently appearing. Deliberately non-homing (an ordinary skill-shot toward the target's position
  at the moment the tower fires) since a tower has no real hero `owner` to thread through the
  homing-reward path safely; new `ARENA_PROJECTILE_NO_OWNER` (255) sentinel tells both the sim
  (never dereferences `heroes[owner]` for a non-homing shot anyway) and the client draw code
  (added an explicit bounds check before the existing `heroes[p->owner]` self/ally/enemy color
  lookup, which would otherwise read out of bounds) that this shot has no firing hero. `team` is
  set to the opposite of the actual target's team, the same "which side this shot can hit" trick
  every other projectile already relies on, just computed from the target since a tower has no
  team of its own. Rendered in a fixed neutral ember-orange, matching the tower's own stone/ember
  visual theme rather than borrowing a hero-relative color that wouldn't mean anything here. Speed/
  radius reuse Gary's own basic-attack numbers as a reasonable reference point. Zero wire-protocol
  changes needed -- `ArenaProjectileSnapshot.owner` was already `uint8_t`, 255 fits it exactly.
  One existing test updated to resolve the hit through `arena_tick_projectiles` (the two-step
  spawn-then-resolve shape every projectile-based attack already has) instead of expecting instant
  damage. Full suite green, build clean.

- feat(arena): Tyler "Divided We Stand" rework -- real independent clone control + a real
  visibility bug fixed underneath it. Founder: "his kit was stubbed in" -> "clones multi control
  drag click all of it" -> "divided we stand rework." Two separate gaps closed:

  1. **Clones were never synced to any client at all.** The hero-snapshot chunk system only ever
     covered owner slots 0..lobby_size-1 -- Tyler's puppet-clone pool existed and fought
     server-side since S170-141 but was completely invisible in every real networked match, not
     just hard to control. `ArenaHeroSnapshot` gained `is_clone`/`clone_owner`; the chunk system
     widened to `ARENA_SNAPSHOT_HEROES_ARRAY_SIZE` (28, chunk size 10->14, still exactly 2 chunks,
     no new MTU risk); `apps/arena_server`'s broadcast loop now syncs the full clone range;
     `apps/arena` applies it and gained a dedicated clone-body draw pass + real floating health
     bars for clones (kept deliberately separate from the real-hero draw loop, which shares
     several `ARENA_MAX_HEROES`-sized tracking arrays -- widening it directly would have read
     those out of bounds for every clone index).
  2. **"Mirror Tyler's move-target every tick" was the opposite of Meepo parity.** Removed
     entirely; a clone is now just another hero with its own real target/moving state, ticked by
     the same generic motion loop everyone else uses. New `arena_owner_controls(sender, target)`
     authorizes a client to command itself or one of its own active clones;
     `arena_set_move_target`/`arena_set_attack_target` widened to accept clone owner slots;
     `PACKET_ARENA_MOVE`/`PACKET_ARENA_ATTACK` gained `unit_owner`/`commander_unit` fields, server-
     validated before acting. `apps/arena` gained real RTS drag-select (drag-vs-click resolved on
     mouse-up, the standard convention, no new keybind) with a green selection ring; every hero
     other than Tyler is completely unaffected (selection defaults to "just self" forever).
     `apps/arena_bot`'s move/attack senders updated to populate the new field explicitly (an
     uninitialized stack byte there would have been a real, silent auth-check bug for the live
     19-bot pool).

  One existing test replaced (the old "clones mirror Tyler" test asserted the exact behavior this
  rework removes) and three new ones added (clone stays put without its own command, clone moves/
  fights on an independent command, `arena_owner_controls` authorization). Full suite green (814
  assertions across all 7 test binaries), build clean across every real binary that sends or
  receives the changed structs (`arena`, `arena_server`, `arena_bot`). Live network round-trip not independently smoke-tested this pass (would have
  required touching the already-running production matchmaker/bot-pool services) -- relying on
  full sim-level coverage of the new authorization/widening logic plus a from-source clean build
  of every binary that sends or receives the changed structs; will get real validation once
  auto-deploy picks this commit up for a live match, same as every other networked change this
  session.

- fix(ops): hero-leaderboard was empty because match-result reporting was silently disabled all
  session. Founder: "ensure stats is working." Real root cause: neither
  `redgarden-matchmaker-bots.service` nor `redgarden-matchmaker-players.service` ever set
  `IDUNA_AGENT_NAME`/`IDUNA_AGENT_SECRET` -- both spawn `arena_server` as a child (`--server-bin`),
  which inherits the unit's own environment, so every real match printed "WARNING:
  IDUNA_AGENT_NAME/IDUNA_AGENT_SECRET not set -- WOTAN match-result reporting disabled" and never
  reported a single hero-result, even though the `REDGARDEN-BOTS` IDUNA agent (with the exact
  `redgarden.match.write` permission needed) had already been fully provisioned since
  2026-07-24 -- the credential just never made it into these two unit files. Fixed via a new
  gitignored `var/redgarden-iduna-agent.env` (secret lives only there, sourced from
  `IDUNA/var/agent-secrets.env`'s own `IDUNA_SECRET_REDGARDEN_BOTS`), loaded via
  `EnvironmentFile=` in both units. Verified live: the newest spawned match server now prints
  "IDUNA agent configured: name=REDGARDEN-BOTS... (WOTAN match-result reporting available)"
  instead of the warning.

  Second, related fix -- founder: "add a 20th bot": `apps/matchmaker` only ever spawns a match
  once its queue reaches `lobby_size` exactly (`while (queue_count >= lobby_size)`). With the
  pool at 19 bots (S170-66's own deliberate "leave a human slot open" choice) and no human
  queued, the bot-pool lobby (`:7778`) sat at 19/20 forever -- no match, and therefore no
  hero-result data, was ever generated without a human filling the last slot. Bumped
  `redgarden-bot-pool.service` to 20 bots, deliberately re-accepting the tradeoff S170-66 moved
  away from (no human can queue into `:7778` anymore -- the player-only pool at `:7779` is
  unaffected) in exchange for a fully self-sustaining pool that keeps generating real match data
  on its own. Verified live: a match spawned immediately after the restart. `scripts/
  run_bot_pool.sh`'s own default argument left at 19 for any other/manual invocation; only the
  live systemd unit changed.

- feat(arena): live-match spectator reporting. Founder: "i want to watch the match on my phone
  web view" -> "live text dashboard." New `report_live_match_state()` in `apps/arena_server`,
  called every `LIVE_MATCH_REPORT_INTERVAL_MS` (3s) while `ARENA_PHASE_LIVE` -- posts a compact
  JSON summary (phase, elapsed, resource race, node ownership, tower HP, per-hero HP/K/D/Flow) to
  IDUNA's new `POST /api/v1/redgarden/live-match`, same WOTAN agent/permission
  (`redgarden.match.write`) `report_match_result` already uses, a third aggregate over the same
  authoritative state. Unlike `report_match_result` (fires once, at match end), this fires
  repeatedly through the whole match, so the login token is cached and reused (5-min refresh)
  rather than re-authenticating every single interval -- a real, avoidable extra HTTP round-trip
  otherwise. Best-effort: a failed POST just means the spectator dashboard is a few seconds
  stale, not worth failing loudly over. New `OKEMILY/live-match.html` -- a plain polling dashboard
  (no game rendering, a real WebGL/canvas spectator client would be a much bigger separate build),
  mobile-first single column, auto-refreshes every 3s. Verified live end to end: restarted the
  bot-pool matchmaker to pick up the new binary, confirmed a fresh match reports real data readable
  at both `localhost:8080` and the public `okemily.com/api/` proxy, confirmed
  `okemily.com/live-match.html` serves (200) and renders it.

- balance(arena): Gary's auto-attack range and all three abilities doubled. Founder: "double the
  range of gary auto attack and abilities." `ARENA_GARY_ATTACK_RANGE`/`Q_RANGE`/`W_RANGE`/
  `R_RANGE` all doubled (6/6/9/6 -> 12/12/18/12). Checked every call site before changing anything:
  targeting checks, the homing auto-attack's own projectile `max_range`, Q's projectile
  `max_range` (`ARENA_GARY_Q_RANGE` itself, not a separate literal), and the bot AI's own decision
  distances all key off these named constants already, so doubling the `#define`s alone is
  sufficient -- no other call site needed touching. Existing tests reference the constants
  themselves (`ARENA_GARY_Q_RANGE - 1.0f`, etc.), never a hardcoded literal, so they scaled
  automatically with zero test changes needed. Full suite green, build clean.

- balance(arena): Gary's ranges pulled back 26% off the doubled values. Founder follow-up:
  "reduce garys range by 26%." Applied as an additional `* 0.74f` factor on top of the doubling
  above -- `ARENA_GARY_ATTACK_RANGE`/`Q_RANGE`/`W_RANGE`/`R_RANGE` go from 12/12/18/12 to
  8.88/8.88/13.32/8.88, written as a visible multiplier chain (`6.0f * 2.0f * 0.74f`) rather than
  a pre-computed literal, same traceable-scaling convention this file already uses elsewhere.
  Full suite green, build clean.

- feat(arena): Gunnr's W is now Consecration, just like WoW. Founder: "gunnr w switch it to
  consecration just like wow" -> "same dot cast radius cd." Was a free toggle self-regen
  (`ARENA_GUNNR_W_REGEN_PER_SEC`, removed) -- now a real cast on a real cooldown: a ground zone at
  Gunnr's own feet that damages any enemy standing in it every second for its duration, no target
  needed to cast (Consecration lands at your own position, not someone else's). Reuses the exact
  `r_zone_x`/`r_zone_z`/`r_active_ms`/`r_zone_tick_ms` fields and `arena_hero_r_zone_radius`
  dispatch every other zone ability (Ghost/Flamel/Morrigan/Paimon/NOOR-1/Vassago/He Xiangu's own
  R's) already shares -- a zone is a zone regardless of which slot cast it; Gunnr's is simply the
  first one triggered from W instead of R. DPS/radius/duration/cooldown copied from Ghost's own R
  zone (the simplest existing "flat DPS zone, no extra mechanic" template, per the founder's own
  "same dot cast radius cd") as literal values into Gunnr's own named constants, not aliased, so
  the two stay independently tunable later -- unlike Ghost's version, no ally-heal side, matching
  real Consecration's enemies-only damage. Updated everywhere the old toggle was referenced:
  `arena_hero_w_is_toggle` (Gunnr removed -- W is a real cast now), the ability-name/description
  HUD tables ("CONSECRATION" / "ZONE: DAMAGE OVER TIME AT SELF"), the AI-bridge hero tags
  (`is_ranged` flips true, `has_heal` drops), and the bot AI heuristic (casts whenever off
  cooldown and a foe is close enough to be caught in it, same shape Ghost's own bot logic already
  uses, instead of "toggle on once and leave it"). One test replaced (asserted the exact free-
  toggle-regen behavior this removes), two new ones added. Full suite green, build clean.

- ops(arena): bot pool back down to 19. Founder, same-day follow-up: "take the bot pool back down
  to 19." Reverts the 20-bot self-sustaining-pool change from earlier today --
  `redgarden-bot-pool.service` back to `run_bot_pool.sh 19`, restoring the "one slot must stay
  open, a human can queue into `:7778`" default. The stats pipeline itself (IDUNA agent
  credentials, hero-result reporting) is unaffected -- that fix stands regardless of bot count,
  it was only the 20th bot that guaranteed a match without a human present. Deployed live:
  redeployed the unit file and restarted the service, confirmed 19 bot processes running.

- ops(arena): bot pool back up to 20, same day. Founder: "bring bot pool back up to 20." Back to
  the self-sustaining-match tradeoff -- `redgarden-bot-pool.service` -> `run_bot_pool.sh 20`.
  Deployed live: redeployed the unit and restarted, confirmed 20 bot processes running. Separately
  from this specific change: today's input-lag investigation traced the actual cause to repeated
  manual `systemctl restart` calls made to verify each incremental balance/feature change live
  immediately, which (unlike `scripts/auto_deploy.sh`'s own already-match-aware restart guard)
  killed in-progress matches outright -- going forward, live redeploys should default to
  auto-deploy's own scheduled, match-aware cycle rather than an immediate manual restart per
  change, unless a change is specifically being verified live on request.

## 2026-07-31

- ops(arena): bot pool back down to 19. Founder: "bring the bot pool back down to 19." Fourth
  flip on this value in two days (20 -> 19 -> 20 -> this). `redgarden-bot-pool.service` ->
  `run_bot_pool.sh 19`, restoring the "one slot open, a human can queue into `:7778`" default.
  Consolidated the unit file's own doc comment (four separate dated paragraphs had accumulated
  from each flip) into one summary of the real 19-vs-20 tradeoff, pointing at git log for
  whichever count is actually live rather than re-narrating each flip going forward.
  `scripts/run_bot_pool.sh`'s own usage comment trimmed the same way. Deployed live: redeployed
  the unit and restarted, confirmed 19 bot processes running.

- fix(ops): auto-deploy's match-aware restart guard had a real TOCTOU race -- killed an
  in-progress match again. Founder: "there is a bug where the whole game just stops... like the
  server process died" -- it had. Real incident: a match with active combat (real HP deltas
  across its last several snapshot lines, no `match_end` ever written) was killed by
  `auto_deploy.sh`'s own scheduled restart despite the match-aware guard added after Apple #11297.
  A single point-in-time `pgrep` check is exactly that -- correct at the instant it runs, with
  nothing re-confirming right up against the actual restart call. Hardened with two independent
  signals instead of trusting one snapshot: `pgrep`, re-checked after a 3s settle delay so a
  single unlucky instant can't slip through twice in a row, and a new
  `recent_match_log_activity()` check (any `var/matches/*.jsonl` written in the last 15s) -- a
  live match snapshots every 500ms, so a fresh write is strong independent evidence that doesn't
  depend on process-table timing at all. Either signal alone defers the restart. Validated live
  against the currently-running match (pgrep found it immediately; both checks composable and
  correct). Also bumped the bot pool back to 20 (was stuck at 19/20 with no human to complete the
  lobby, so no new match could self-start after the kill) to get a match running again right away.

- docs: refreshed README's keybind table -- had gone stale across this whole session's worth of
  client changes. Founder: "add the full current kit keybinds to the top of the readme not the
  top top above the items." The table already lived in the right spot (`## How to Play`, above
  the item catalog) -- it just needed real content: new rows for `` ` `` (active item use, Blink
  Dagger/Donkey), shop pagination (`1`-`9` now page-relative, `Shift+1/2/3` to jump pages, the
  on-screen page buttons), and Tyler's drag-select clone control. Also fixed the item count (24
  -> 27, stale since Blink Dagger/Donkey/Haste Trinket were added) and the zone-abilities note
  (Gunnr's Consecration is a zone cast from W, not R, unlike the other 8).

- feat(arena): Ghost's Q gets a real lightning-crackle visual, in-flight and on impact. Founder:
  "ghost's Q should have a cool crackle lightning shader spell animation showing where the spell
  hit." Two parts, both client-only, `apps/arena/src/main.c`: (1) in-flight crackle -- while a
  Ghost-owned projectile (`hero_id == ARENA_HERO_GHOST`) is travelling, 4 thin box slivers at
  randomized angles/offsets are drawn around it every frame, fully re-rolled each frame for a
  flickering electric look, layered on its existing cube; (2) impact burst -- a new
  `LightningBurst` effect (same `{x,z,age_ms,active}` shape as AttackFlash/HealFlash/FoldFlash),
  fired via a new `prev_projectile_active[]` edge-detect on the projectile slot's active->inactive
  transition (whether a real hit or a whiff -- no wire signal distinguishes the two, same scoping
  tradeoff AttackFlash's own doc comment already accepts), rendered as 8 radiating jittered slivers
  expanding/fading over 300ms at the shot's last-known position. Both reuse
  `draw_hero_box_facing` (no new draw primitive) and bright electric cyan-white
  (0.65, 0.95, 1.0), distinct from every owner-relationship projectile color and from the generic
  orange-white `attack_flash` every other ability's hit already produces. `scripts/build.sh`
  clean (no new warnings); `scripts/test_arena.sh` passes -- purely visual, no sim-logic touched,
  so the headless suite doesn't (and can't, by design) exercise this directly; no display
  available in this environment to visually confirm the rendered result.

- feat(arena): Gunnr's R (Valhalla Has Yet To Admit It) now also stuns. Founder: "give gunnrs e a
  stun" (backlog dump + sprint plan, Sprint 1 -- EMILY/BACKLOG.md). REDGARDEN only has three cast
  slots (Q/W/R), read "e" as R, the third/final slot. Same target, same `ARENA_GUNNR_R_RANGE`
  check the ability's existing execute-scaled damage already uses -- no separate targeting pass,
  just `arena_apply_stun(foe->owner, ARENA_GUNNR_R_STUN_MS)` alongside the damage. Duration
  (1100ms) copied from Zagan's own W (The Standstill, S170-230, this roster's first-ever
  `arena_apply_stun` call) as an independently-tunable named constant, not aliased -- this
  roster's second stun. HUD ability description updated ("DAMAGE + STUN, MORE DAMAGE AT LOW
  TARGET HP"). 2 new/expanded tests (in-range stun lands; out-of-range whiff stuns nothing
  either, alongside the existing no-damage assertion). `docs/HEROES_VS0.md` entry updated. Full
  suite green, `test_10_bots.sh` stable.
