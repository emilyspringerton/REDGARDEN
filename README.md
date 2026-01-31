RED GARDEN
Acceptance Criteria (Vertical Slice)
Render a 20×20 isometric grid with visible cell boundaries.
Run a cellular-automata tick every 2 seconds for grid state updates.
Support grid states: Neutral, Player, Enemy, Corrupted.
ITERATE
Place static Frontier Villages during map generation.
Provide a card hand UI with 5 slots at the bottom of the screen.
Implement mouse drag-and-drop to place cards onto valid grid cells.
Show a ghost preview and valid/invalid placement feedback while dragging.
Enforce influence cost and cooldown per card when placing.
Include exactly 4 playable cards: Militia, Scout, Swarmlings, Outpost.
Implement autonomous unit behaviors for Militia, Scout, and Swarmlings.
Implement Outpost as a spawner that produces Militia on a timer.
Implement a minimal Dominion tech tree with 2 tiers:
Tier 1: Militia +20 HP.
Tier 2: Outpost spawns Militia at 2× speed.
Implement a win condition: hold 60% of cells for 60 seconds or destroy the enemy Outpost.
Full Technical Design Document
1. Core Concept Refinement
What this game is: A deck-building ecosystem RTS where you seed autonomous agents into a living cellular automaton battlefield. You do not control units—you introduce pressure and watch systems collide.

Mental model

Clash Royale card deployment + hand management.
Conway’s Game of Life emergent map behavior.
Command & Conquer strategic pacing.
League of Legends objective control.
Diablo II creature ecology.
Core loop

Draw cards from an evolving deck.
Drag-drop spawners/units onto valid grid cells.
Watch autonomous behaviors create frontlines.
Tech tree upgrades mutate cards (not raw unit control).
Capture objectives to evolve deck mid-match.
2. Art Direction: Low-Poly Imperative Brutalism
Visual pillars

C99 + SDL2 + OpenGL immediate mode.
No shaders, no textures, flat colors, vertex lighting only.
Everything under 100 triangles per entity.
Neon Brutalism style

Solid matte black cores (RGB 0.02, 0.02, 0.02).
Wireframe neon cages with procedural color.
High-contrast silhouettes for instant readability.
Geometric purity: cubes, wedges, capsules.
Color language

Player Units: Hot Pink (1.0, 0.0, 0.8)
Enemy Units: Acid Green (0.0, 1.0, 0.4)
Neutral Towns: Cyan (0.0, 1.0, 1.0)
Pillager Compounds: Blood Red (1.0, 0.0, 0.0)
Terrain Grid: Deep Blue (0.0, 0.2, 0.4)
Tech Nodes: Electric Yellow (1.0, 1.0, 0.0)
Camera

Fixed high-tilt orthographic (Age of Empires II style).
No rotation, slight zoom only.
Grid always visible.
3. The Living Grid System
Cell struct

typedef struct {
    int state; // NEUTRAL=0, PLAYER=1, ENEMY=2, CORRUPTED=3
    int population; // 0-255
    int alignment_pressure; // -127 to +127
    int growth_rate; // -10 to +10
    float stability; // 0.0 to 1.0
} GridCell;
Update rules (every 2 seconds)

If 3+ neighbors share alignment → convert (if pressure > 50).
If population > 200 → split to adjacent cells.
If population < 20 for 5 ticks → revert to neutral.
If 4+ corrupted neighbors → become corrupted.
Visual feedback

Cell color intensity = population density.
Pulsing borders = conversion in progress.
Cracks for overpopulation.
Tendrils for corruption.
4. Card UI System (Clash Royale DNA)
Layout

Bottom-center: 5-card active hand.
Top-left: tech tree indicator.
Bottom strip: resource + deck preview.
Card anatomy

typedef struct {
    int card_id;
    int cost; // Influence points
    float cooldown; // 0.0 = ready, 1.0 = just used
    int tech_level; // 0-3, affects stats
    char name[32];
    float color_r, color_g, color_b; // Neon accent
} Card;
Mouse interaction

typedef struct {
    float world_x, world_z;
    int grid_x, grid_z;
    int dragging_card_idx; // -1 = none
    int hover_cell;
} MouseState;
Valid placement

int is_valid_spawn(GridCell *cell, Card *card) {
    if (cell->state == CORRUPTED) return 0;
    if (card->cost > player_influence) return 0;
    if (cell->population > 200) return 0;
    return 1;
}
5. Entity Roster (16 Units + 8 Structures)
Units
Tier 1

Militia: frontline bruiser, forms shield walls.
Scout: ranged kiter, long aggro.
Swarmlings: fast horde, weakest-target focus.
Ravager: objective breaker, ignores units initially.
Tier 2

Hexbound: debuffer, spreads corruption.
Verdant Behemoth: slow anchor, stabilizes cells.
Shade Stalker: stealth assassin, backstab.
Pyromancer: AoE caster, splash + burn.
Tier 3

Warden: defensive specialist, reflect damage.
Tide Caller: healer/support, resurrection path.
Siege Golem: tank with siege mode.
Void Reaver: self-draining, explosive death.
Tier 4

Archon: hero unit, buffs + ultimate.
Chaos Spawn: random outcomes, volatility.
Wraith King: revive loops, summons militia.
Dragon: map boss, unlockable via quest.
Structures
Outpost: Militia spawner, alignment pressure.
Mana Well: influence generator, town-adjacent.
Corruption Spire: Hexbound spawner + corruption.
Grove Heart: Behemoth spawner + healing.
Siege Workshop: Golem spawner + repairs.
Shadow Sanctum: Stalker spawner + stealth.
Inferno Tower: AoE defense.
Nexus Core: win-condition structure.
6. NPC Entities (Third Faction)
Towns

Frontier Village: peasants + easy flip.
Walled Hamlet: guards + hard flip.
Jungle Enclave: hunters + expansion.
Blighted Settlement: cultists + instability.
Creep camps

Goblin Warren: 3 goblins, loot influence.
Orc Stronghold: orcs, unlocks Ravager.
Dragon Roost: neutral dragon, major objective.
Pillager compounds

Corruption Node: spreads corruption, spawns pillagers.
Pillagers: Marauder, Destroyer, Corruptor.
7. Tech Tree System
Design rules

3 vertical paths, pick 2 per match.
Tech upgrades cards (not global stats).
Unlocks via objectives, not just time.
Doctrines

Dominion

T1: Militia +20 HP, Outpost 2× spawn, +10% structure HP.
T2: Shield Bash, Barracks upgrade, Siege Workshop unlocked.
T3: Captain upgrade, Nexus Core unlocked, +50% structure HP.
T4: Archon unlocked, Unbreakable buff.
Symbiosis

T1: Scout +2 vision, faster cell growth, towns never defect.
T2: Behemoth regen, Grove Heart unlocked, camps neutral.
T3: Behemoth spawns Militia, towns auto-upgrade, permanent conversions.
T4: World Tree unlocked, territory heal, hostile environment for enemies.
Corruption

T1: Hexbound radius +1, faster corruption, pillager hijack.
T2: Corruption Spire + Void Reaver unlocked, structure corruption damage.
T3: Chaos Spawn unlocked, corrupted cell explosions, volatile deaths.
T4: Cataclysm Beacon unlocked, permanent corruption, zombie effect.
Quest unlocks

First Blood → Pyromancer
Fortify → Warden
Treasure Hunter → +5 hand size
Dragon Slayer → Dragon card + influence cap
Ecosystem Collapse → Chaos Spawn
Necromancer → Wraith King
8. Game Modes
Skirmish (1v1 Ranked): 30 min, destroy Nexus or hold 50% map.
Survival (Co-op PvE): survive 20 waves.
Ecosystem War (2v2): shared structures + dragon objective.
Chaos Mode (FFA): corruption expands, last player standing.
9. UI Specification
Main layout

Top bar: tech tier + influence.
Center: 3D isometric battlefield.
Bottom: 5-card hand + deck preview.
Cooldowns shown as radial fills.
10. Conway Integration
void update_town_ecology() {
    for (int i = 0; i < town_count; i++) {
        Town *t = &towns[i];
        int friend_count = 0;
        int enemy_count = 0;
        int neutral_count = 0;
        for (int j = 0; j < town_count; j++) {
            if (i == j) continue;
            float dist = distance(t->x, t->z, towns[j].x, towns[j].z);
            if (dist < 3 * CELL_SIZE) {
                if (towns[j].alignment == t->alignment) friend_count++;
                else if (towns[j].alignment != NEUTRAL) enemy_count++;
                else neutral_count++;
            }
        }
        if (friend_count == 0 && enemy_count >= 2) t->morale -= 20;
        if (friend_count >= 3 && neutral_count >= 1) {
            spawn_child_town(t, find_adjacent_empty_cell(t->x, t->z));
        }
        if (t->morale <= 0) convert_to_ruins(t);
    }
}
11. Networking Adaptation
New packets

PACKET_CARD_PLAY: client → server card placement.
PACKET_TECH_UNLOCK: server → clients tech progression.
PACKET_QUEST_COMPLETE: server → client quest rewards.
typedef struct {
    unsigned char card_id;
    int grid_x, grid_z;
    unsigned int timestamp;
} CardPlayCmd;

typedef struct {
    unsigned char quest_id;
    unsigned char reward_card_id;
    int bonus_influence;
} QuestComplete;
Server simulation

Authoritative 60 TPS.
Validates card placement.
Broadcasts entity snapshots + cell states.
Handles NPC AI (towns, creeps, pillagers).
12. Fork Adaptation Guide
Keep

SDL2 window management.
OpenGL immediate mode rendering.
Network stack (UDP sockets).
Remove

FPS camera.
Weapon system.
Jump/crouch physics.
Add

Orthographic/isometric camera.
Grid-based spatial partition.
Card deck system.
Mouse → world raycast.
Cellular automata updater (2s tick).
Quest tracker + tech tree.
Key files

card_system.h
grid.h
entity_behaviors.h
quest_system.h
tech_tree.h
mouse_input.h
13. Emergence Examples
The Cascade: corruption + pillagers + pyromancer → chain wipe.
The Living Wall: Grove Hearts create a pushing defensive front.
The Dragon Gambit: last-hit dragon swings a late-game siege.
14. Visual Identity Summary
Palette

Deep Space Blue: #050514
Hot Pink: #FF00CC
Acid Green: #00FF66
Cyan: #00FFFF
Blood Red: #FF0000
Electric Yellow: #FFFF00
Matte Black: #050505
Silhouettes

Bruisers: cubes + wide stance.
Skirmishers: tall capsules.
Swarms: triangular shards.
Casters: spheres + floaters.
Tanks: stacked cubes.
Support: ribbon geometry.
Typography

Wireframe vector font (2px line thickness).
Cyan for info, yellow for warnings, red for errors.
