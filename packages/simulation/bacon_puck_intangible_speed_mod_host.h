/* bacon_puck_intangible_speed_mod_host.h -- real extern declaration for
 * bacon_puck_intangible_speed_mod.c's own exported entry point. Same
 * "-include this header before compiling the generated C" pattern every
 * other REDGARDEN/ECOWAR mod host header already established -- pure C
 * linking, no cgo layer needed.
 *
 * on_bacon_puck_intangible_speed_pct has no host-side call-INTO-C
 * requirement (unlike bloodflower_mod/tree_passive_mod/etc, which call
 * back into a redgarden_host_* function) -- this mod is pure PARENA
 * logic, called FROM host C (update_hero_motion, arena_game.c), not
 * calling back out. Same shape ECOWAR's own card_effect_mod_host.h
 * already established for this same "pure logic, no callback" case.
 */
#ifndef BACON_PUCK_INTANGIBLE_SPEED_MOD_HOST_H
#define BACON_PUCK_INTANGIBLE_SPEED_MOD_HOST_H

extern int on_bacon_puck_intangible_speed_pct(void);

#endif /* BACON_PUCK_INTANGIBLE_SPEED_MOD_HOST_H */
