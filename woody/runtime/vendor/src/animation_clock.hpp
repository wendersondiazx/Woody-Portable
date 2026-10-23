#pragma once

#include "level.h"
#include <cmath>

namespace woody {
inline void wrap_animation_clock(Instance &instance) {
    const Model &model = *instance.model;
    if (instance.anim < 0 || static_cast<uint32_t>(instance.anim) >= model.nanims)
        return;
    const float duration = model.anims[instance.anim].duration_s;
    if (duration > 0 && instance.anim_time >= duration)
        instance.anim_time = std::fmod(instance.anim_time, duration);
}

inline void advance_animation_chain(Instance &instance, const int (&chain)[4], int &part) {
    const Model &model = *instance.model;
    for (int step = 0; step < 4; step++) {
        if (instance.anim < 0 || static_cast<uint32_t>(instance.anim) >= model.nanims)
            return;
        const float duration = model.anims[instance.anim].duration_s;
        if (!(duration > 0))
            return;
        if (part >= 3) {
            wrap_animation_clock(instance);
            return;
        }
        const int next = chain[part + 1];
        if (next < 0 || static_cast<uint32_t>(next) >= model.nanims) {
            if (instance.anim_time >= duration) {
                instance.anim_time = std::nextafter(duration, 0.0f);
                instance.anim_speed = 0;
            }
            return;
        }
        if (instance.anim_time < duration)
            return;
        instance.anim_time -= duration;
        instance.anim = next;
        ++part;
    }
}
} // namespace woody
