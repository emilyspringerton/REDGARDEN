#include "arena_ai_bridge.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

const char *arena_hero_name(ArenaHeroID hero_id) {
    switch (hero_id) {
    case ARENA_HERO_UNICORN: return "unicorn";
    case ARENA_HERO_DUCK:    return "duck";
    case ARENA_HERO_GHOST:   return "ghost";
    case ARENA_HERO_FROG:    return "frog";
    case ARENA_HERO_DOC_WHEEL: return "doc_wheel";
    case ARENA_HERO_TREE:    return "tree";
    case ARENA_HERO_PIZZA:   return "pizza";
    case ARENA_HERO_FLAMEL:  return "flamel";
    case ARENA_HERO_MORRIGAN: return "morrigan";
    case ARENA_HERO_DAGDA:   return "dagda";
    case ARENA_HERO_COURIER: return "courier";
    case ARENA_HERO_LOKI:    return "loki";
    case ARENA_HERO_GARY:    return "gary";
    case ARENA_HERO_FLUTE_DEBT: return "flute_debt";
    case ARENA_HERO_BACON_PUCK: return "bacon_puck";
    default:                 return "unknown";
    }
}

void arena_serialize_state(int owner, unsigned int tick_ms, char *out, size_t out_len) {
    if (out_len == 0) return;
    out[0] = '\0';
    if (owner < 0 || owner > 1) return;

    const ArenaHero *self = &arena_state.heroes[owner];
    const ArenaHero *foe = &arena_state.heroes[owner == 0 ? 1 : 0];
    float dx = foe->x - self->x, dz = foe->z - self->z;
    float dist = sqrtf(dx * dx + dz * dz);

    snprintf(out, out_len,
        "redgarden arena tick:%u\n"
        "self hero:%s pos:%.2f,%.2f hp:%d max_hp:%d alive:%d "
        "q_cd:%d w_active:%d w_cd:%d r_cd:%d r_active:%d silenced:%d intangible:%d\n"
        "foe hero:%s pos:%.2f,%.2f hp:%d max_hp:%d alive:%d dist:%.2f "
        "q_cd:%d w_cd:%d r_cd:%d r_active:%d silenced:%d intangible:%d",
        tick_ms,
        arena_hero_name(self->hero_id), self->x, self->z, self->hp, self->max_hp, self->alive,
        self->q_cooldown_ms, self->w_active, self->w_cooldown_ms, self->r_cooldown_ms,
        self->r_active_ms, self->silenced_ms, self->intangible_ms,
        arena_hero_name(foe->hero_id), foe->x, foe->z, foe->hp, foe->max_hp, foe->alive, dist,
        foe->q_cooldown_ms, foe->w_cooldown_ms, foe->r_cooldown_ms, foe->r_active_ms,
        foe->silenced_ms, foe->intangible_ms);
}

int arena_decode_action(const char *action_str, ArenaAction *out) {
    memset(out, 0, sizeof(*out));
    if (!action_str) return 0;
    int found = 0;

    const char *mp = strstr(action_str, "move:");
    if (mp) {
        float mx, mz;
        if (sscanf(mp, "move:%f,%f", &mx, &mz) == 2) {
            out->move_x = mx;
            out->move_z = mz;
            out->has_move = 1;
            found = 1;
        }
    }

    const char *p;
    if ((p = strstr(action_str, "cast_q:")) != NULL) {
        out->cast_q = (p[7] == '1');
        found = 1;
    }
    if ((p = strstr(action_str, "cast_w:")) != NULL) {
        out->cast_w = (p[7] == '1');
        found = 1;
    }
    if ((p = strstr(action_str, "cast_r:")) != NULL) {
        out->cast_r = (p[7] == '1');
        found = 1;
    }

    return found;
}
