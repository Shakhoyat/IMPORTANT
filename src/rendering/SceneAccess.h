// Optional scene features the renderer uses when the Scene provides them (combat effects, walkable indoor
// volumes). Detected at compile time so the renderer builds against older and newer Scene versions.
#pragma once

#include <type_traits>
#include <vector>

#include "scene/EffectsData.h"
#include "scene/Scene.h"
#include "scene/WalkSpace.h"

namespace ts::sceneaccess {

template <typename S, typename = void>
struct HasCombat : std::false_type {};
template <typename S>
struct HasCombat<S, std::void_t<decltype(std::declval<const S&>().combat().lights())>> : std::true_type {};

template <typename S, typename = void>
struct HasWalkSpace : std::false_type {};
template <typename S>
struct HasWalkSpace<S, std::void_t<decltype(std::declval<const S&>().walkSpace().indoorVolumes())>> : std::true_type {};

template <typename S>
const std::vector<DynamicLight>* dynamicLights(const S& scene) {
    if constexpr (HasCombat<S>::value) return &scene.combat().lights();
    else { (void)scene; return nullptr; }
}

template <typename S>
const WalkSpace* walkSpace(const S& scene) {
    if constexpr (HasWalkSpace<S>::value) return &scene.walkSpace();
    else { (void)scene; return nullptr; }
}

// Same test as WalkSpace::indoor(), on the indoor boxes only (keeps the renderer independent of WalkSpace.cpp).
inline bool insideIndoor(const WalkSpace* ws, const Vec3& p) {
    if (!ws) return false;
    for (const Box& b : ws->indoorVolumes()) {
        if (p.x >= b.min.x && p.x <= b.max.x && p.y >= b.min.y && p.y <= b.max.y && p.z >= b.min.z && p.z <= b.max.z)
            return true;
    }
    return false;
}

}  // namespace ts::sceneaccess
