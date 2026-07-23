# RED GARDEN — Consumable/Recipe Name Pool + Cooking/Crafting Direction

*Written: 2026-07-23 | Status: name pool + northstar direction, not implemented*

## Mined name pool

Source: `gitlab.com/mailtruck/creepy-carrots` (a 2020-era GPT-2 text-sample corpus, same lineage
as the SALLYPEMDAS/`gpt2-alpine-c` archaeology already referenced elsewhere in this org — raw
generated filenames, not structured data; only the titles are usable, the file bodies are
incoherent GPT-2 output). Curated for tone fit with RED GARDEN's existing absurdist flavor (the
duck/unicorn/pizza "chosen one" register already established in `docs/HEROES_VS0.md`).
Trademarked/real-world names from the source (a specific video game title, a Tolkien place name, a
real footballer) are deliberately excluded below.

**Consumable / cooking-ingredient names:**
Avocado Paste, Bamboo, Black Raspberry Spice, Blue Apple, Blueberry Juice from the Blue Moon,
Carrot Coffee, Carrot Cupcakes, Carrot Scented Candles, Carrot Stew, Cedar Candy, Cheese and
Sweetener, Chickpea Salad, Chuck's Chunk of Cheese in a Peanut Butter Sandwich, Coriander Seed,
Cream of Ginger, Crisp Green, Crisp Orange, Fancy Water, Fruit Fruit Cider, German Chamomile,
Green Fruit Smoothies from the Green Apple, Maraschino, Ode to Sweetness, Ode to the Goodness,
Olive and Lettuce, Olive Oil, Pineapple Juice from Oat Bream, Pink Cinnamon Roll of New York City
from the Blue Moon, Pumpkin Ice Cream, Pumpkin Puree, Sage Pudding, Spinach, **Tear of the Earth
Dragon** (a cooking ingredient with a fantasy-item register — good crossover pick).

**Item / lore-flavored names** (fantasy weapons, artifacts, factions, titles):
Four Heroes, Gladiator V, Global Infrastructure Assembly (faction-name candidate), Memories of the
King of the World, Spear of the Dragon, Starbound Starchaser: Priest (class/hero-title candidate),
The Beast of Ice and Fire, The Beast of Light, The Birth of the Dragon Child, The Evil Dragon, The
Kind of Chaos, The Rhapsody of Chaos, Vortex Nova, Who Built The Temple? (lore-hook candidate), the
Nulnians (faction-name candidate, from "In order to protect Sion, the Nulnians").

## Cooking (northstar direction, not this pass)

Founder direction: add cooking to the game. Mid-match, players use acquired resources (the same
resource/influence economy §3 of `NORTHSTAR.md` already tracks) to cook consumables from the pool
above at a cook-station structure, trading resources for a temporary buff (the recipe names
above are flavor-first — mechanical effects TBD). Not scoped to specific stat effects or a UI yet;
captured here so the item-roster pass (`docs/HEROES_VS0.md`) and any future crafting pass don't
design against it by accident.

**Concrete worked example (founder-given, the shape to design the rest toward):** controlling
certain jungle nodes (§8's Arathi-Basin-with-jungle map concept — node control already ties into
territory/resource generation) yields a raw resource — e.g. holding an "orange grove" node grants
Oranges over time. Oranges can be cooked into **Blueberry Juice from the Blue Moon**-style
consumables — concretely, an Orange-based juice grants a mana-regen buff. This is the pattern for
the rest of the pool: raw resource (tied to *which* node/territory you hold) → cooked consumable
(named from the pool above) → a specific, small buff. Gives node control a second payoff besides
straight resource-tick income, and gives the cooking system a reason to care about map control
rather than being a flat currency sink.

**Interface constraint (founder direction): high-APM, designed for pro-level play speed — applies
to every shop/menu, not just cooking.** Cooking/crafting must be fast enough to use mid-fight, not
a slow inventory-management screen — consistent with §2's existing "LoL-style card affordances"
requirement (clean cooldown sweep, no friction). Founder has generalized this explicitly to **all**
shop and menu surfaces (item shop, cooking, crafting, any future menu) — but refined: this is
**not** keybind-only. Click is still core (card drag-and-drop, targeting/placement); the actual bar
is that both keybind and click need to resolve instantly at high speed, matching how LoL's real
competitive scene mixes hotkeys and precise clicking rather than leaning on either exclusively.
Concretely this points toward a quick-cast/quick-click shape everywhere: a bound key or a single
precise click instantly buys/cooks/crafts the default option for context (whatever resource you're
holding/standing near, or a numbered shop-slot), with a modifier-key or hold-to-open path for
picking a non-default option. Exact keybind/click layout not designed yet per surface — flagged
here as a hard cross-cutting UI constraint so no future menu gets designed as a slow, casual
point-and-click screen and has to be reworked later for pro-level play.

## Crafting (northstar direction, not this pass)

Founder direction: mid-game crafting from acquired resources, sitting alongside (not replacing)
the direct-purchase item roster in `docs/HEROES_VS0.md`. Likely shape: resources gathered from the
living grid/ecology (§1/§8 of `NORTHSTAR.md`) feed into crafting recipes for the item archetypes
already defined, rather than items being pure gold-purchases only. Exact recipe/resource mapping
not designed yet — flagged here as real direction, same discipline as the hero queue's "no
mechanics designed yet" notes.
