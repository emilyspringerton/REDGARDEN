# RED GARDEN

## Acceptance Criteria (Vertical Slice)

- Render a 20×20 isometric grid with visible cell boundaries.
- Run a cellular-automata tick every 2 seconds for grid state updates.
- Support grid states: Neutral, Player, Enemy, Corrupted.
- Place static Frontier Villages during map generation.
- Provide a card hand UI with 5 slots at the bottom of the screen.
- Implement mouse drag-and-drop to place cards onto valid grid cells.
- Show a ghost preview and valid/invalid placement feedback while dragging.
- Enforce influence cost and cooldown per card when placing.
- Include exactly 4 playable cards: Militia, Scout, Swarmlings, Outpost.
- Implement autonomous unit behaviors for Militia, Scout, and Swarmlings.
- Implement Outpost as a spawner that produces Militia on a timer.
- Implement a minimal Dominion tech tree with 2 tiers:
  - Tier 1: Militia +20 HP.
  - Tier 2: Outpost spawns Militia at 2× speed.
- Implement a win condition: hold 60% of cells for 60 seconds or destroy the enemy Outpost.
