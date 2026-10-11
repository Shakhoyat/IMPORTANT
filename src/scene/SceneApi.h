// Adapter between the app layer (camera, director, App, HUD) and the Scene API that other engineers are still
// adding (controllables(), combat(), walkSpace(), update(dt, det, input, possessed), kill counters, a rover list).
// Every accessor is detected at compile time (SFINAE): if Scene has the member it is used, otherwise a safe
// default is returned, so the app files compile and run both before and after the Scene wiring lands.
#pragma once

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "scene/Combat.h"
#include "scene/Controllable.h"
#include "scene/DayCycle.h"
#include "scene/Garrison.h"
#include "scene/Scene.h"
#include "scene/WalkSpace.h"
#include "vision/CvPipeline.h"


namespace ts::sa {

namespace detail {

template <int N>
struct Pri : Pri<N - 1> {};
template <>
struct Pri<0> {};

template <class T>
Controllable* asCtl(T* p) { return static_cast<Controllable*>(p); }
template <class T>
Controllable* asCtl(const std::unique_ptr<T>& p) { return static_cast<Controllable*>(p.get()); }
template <class T>
Controllable* asCtl(const std::shared_ptr<T>& p) { return static_cast<Controllable*>(p.get()); }

template <class V>
void collect(const V& v, std::vector<Controllable*>& out) {
    for (const auto& e : v) out.push_back(asCtl(e));
}

template <class S>
auto ctlImpl(S& s, std::vector<Controllable*>& out, Pri<1>) -> decltype(collect(s.controllables(), out), void()) {
    collect(s.controllables(), out);
}
template <class S>
void ctlImpl(S&, std::vector<Controllable*>&, Pri<0>) {}

template <class T>
auto asPtr(T& x) -> decltype(&x) { return &x; }
template <class T>
T* asPtr(T* x) { return x; }

template <class S>
auto combatImpl(S& s, Pri<1>) -> decltype(static_cast<Combat*>(asPtr(s.combat()))) {
    return static_cast<Combat*>(asPtr(s.combat()));
}
template <class S>
Combat* combatImpl(S&, Pri<0>) { return nullptr; }

template <class S>
auto walkImpl(S& s, Pri<1>) -> decltype(static_cast<const WalkSpace*>(asPtr(s.walkSpace()))) {
    return static_cast<const WalkSpace*>(asPtr(s.walkSpace()));
}
template <class S>
const WalkSpace* walkImpl(S&, Pri<0>) { return nullptr; }

template <class S>
auto updateImpl(S& s, float dt, const Detection& d, const ControlInput* in, int possessed, Pri<1>)
    -> decltype(s.update(dt, d, in, possessed), void()) {
    s.update(dt, d, in, possessed);
}
template <class S>
void updateImpl(S& s, float dt, const Detection& d, const ControlInput*, int, Pri<0>) { s.update(dt, d); }

template <class S>
auto killsImpl(const S& s, Pri<2>) -> decltype(static_cast<int>(s.kills())) { return static_cast<int>(s.kills()); }
template <class S>
auto killsImpl(const S& s, Pri<1>) -> decltype(static_cast<int>(s.killCount())) { return static_cast<int>(s.killCount()); }
template <class S>
int killsImpl(const S&, Pri<0>) { return -1; }

// Rover positions: a list accessor rovers() whose elements expose position (directly or through .state), else the
// single v3 member 'rover'.
template <class R>
auto posOf(const R& r, Pri<2>) -> decltype(Vec3(r.position)) { return r.position; }
template <class R>
auto posOf(const R& r, Pri<1>) -> decltype(Vec3(r.state.position)) { return r.state.position; }
template <class R>
auto posOf(const R& r, Pri<0>) -> decltype(Vec3(r->position())) { return r->position(); }

template <class S>
auto roverImpl(const S& s, std::vector<Vec3>& out, Pri<2>) -> decltype(posOf(*s.rovers().begin(), Pri<2>{}), void()) {
    for (const auto& r : s.rovers()) out.push_back(posOf(r, Pri<2>{}));
}
template <class S>
auto roverImpl(const S& s, std::vector<Vec3>& out, Pri<1>) -> decltype(Vec3(s.rover.position), void()) {
    out.push_back(s.rover.position);
}
template <class S>
void roverImpl(const S&, std::vector<Vec3>&, Pri<0>) {}

template <class S>
auto dayImpl(S& s, Pri<1>) -> decltype(static_cast<DayCycle*>(&s.dayCycle())) { return &s.dayCycle(); }
template <class S>
DayCycle* dayImpl(S&, Pri<0>) { return nullptr; }

template <class S>
auto garrisonImpl(const S& s, Pri<1>) -> decltype(static_cast<const Garrison*>(s.garrison())) { return s.garrison(); }
template <class S>
const Garrison* garrisonImpl(const S&, Pri<0>) { return nullptr; }

template <class C>
auto setCfgImpl(C& c, const DayCycleConfig& cfg, Pri<1>) -> decltype(c.setConfig(cfg), true) {
    c.setConfig(cfg);
    return true;
}
template <class C>
bool setCfgImpl(C&, const DayCycleConfig&, Pri<0>) { return false; }

template <class S>
auto hiddenImpl(const S& s, Pri<1>) -> decltype(static_cast<bool>(s.rover.hidden)) { return s.rover.hidden; }
template <class S>
bool hiddenImpl(const S&, Pri<0>) { return false; }

template <class S>
auto speedImpl(const S& s, Pri<1>) -> decltype(static_cast<float>(s.rover.leftSpeed + s.rover.rightSpeed)) {
    return 0.5f * (s.rover.leftSpeed + s.rover.rightSpeed);
}
template <class S>
float speedImpl(const S&, Pri<0>) { return 0.0f; }

}  // namespace detail

inline std::vector<Controllable*> controllables(const Scene& s) {
    std::vector<Controllable*> out;
    detail::ctlImpl(const_cast<Scene&>(s), out, detail::Pri<1>{});
    return out;
}
inline Combat* combat(const Scene& s) { return detail::combatImpl(const_cast<Scene&>(s), detail::Pri<1>{}); }
inline const WalkSpace* walkSpace(const Scene& s) { return detail::walkImpl(const_cast<Scene&>(s), detail::Pri<1>{}); }
inline void update(Scene& s, float dt, const Detection& d, const ControlInput* in, int possessed) {
    detail::updateImpl(s, dt, d, in, possessed, detail::Pri<1>{});
}
// Kills reported by the scene, or -1 if it has no counter.
inline int kills(const Scene& s) { return detail::killsImpl(s, detail::Pri<2>{}); }
// Positions of every rover (alive or wreck), in scene order. Never empty for the v3 scene.
inline std::vector<Vec3> roverPositions(const Scene& s) {
    std::vector<Vec3> out;
    detail::roverImpl(s, out, detail::Pri<2>{});
    return out;
}
// The rover the cameras and scripted strikes care about: the most recent burning wreck (< 15 s old), else the first
// rover that is alive, else the first intruder.
inline Vec3 primaryRover(const Scene& s) {
    const auto& in = s.intruders();
    const Intruder* wreck = nullptr;
    for (const auto& i : in)
        if (i->life == Intruder::Life::Wreck && i->timer < 15.0f && (!wreck || i->timer < wreck->timer)) wreck = i.get();
    if (wreck) return wreck->wreckPosition;
    for (const auto& i : in)
        if (i->life == Intruder::Life::Alive) return i->state.position;
    return in.empty() ? Vec3{6.0f, 0.0f, 7.5f} : in.front()->state.position;
}
// True while some rover is alive (a strike has a target).
inline bool anyAlive(const Scene& s) {
    for (const auto& i : s.intruders())
        if (i->life == Intruder::Life::Alive) return true;
    return false;
}
// Ground speed of the first rover, m/s (0 if unknown).
inline float roverSpeed(const Scene& s) { return detail::speedImpl(s, detail::Pri<1>{}); }
// True when the first rover is hidden from the UAV (v3 state); false if unknown.
inline bool roverHidden(const Scene& s) {
    return detail::hiddenImpl(s, detail::Pri<1>{});
}

// --- v6 ---
// The scene's day / night clock (nullptr before the Scene wiring exists).
inline DayCycle* dayCycle(const Scene& s) { return detail::dayImpl(const_cast<Scene&>(s), detail::Pri<1>{}); }
// The 8 soldiers (nullptr when the scene was built without the garrison).
inline const Garrison* garrison(const Scene& s) { return detail::garrisonImpl(s, detail::Pri<1>{}); }
// Index of soldier i in controllables(), -1 if absent.
inline int soldierIndex(const Scene& s, int i) {
    const Garrison* g = garrison(s);
    if (!g || i < 0 || i >= g->count()) return -1;
    const Controllable* c = g->soldier(i);
    const std::vector<Controllable*> list = controllables(s);
    for (size_t k = 0; k < list.size(); ++k)
        if (list[k] == c) return static_cast<int>(k);
    return -1;
}
// Position of soldier i (fallback when absent).
inline Vec3 soldierPosition(const Scene& s, int i, const Vec3& fallback) {
    const Garrison* g = garrison(s);
    return g && i >= 0 && i < g->count() && g->soldier(i) ? g->soldier(i)->position() : fallback;
}
// Changes the clock's rates (--cycle-seconds). False when DayCycle has no setConfig() yet.
inline bool setDayCycleConfig(DayCycle& c, const DayCycleConfig& cfg) { return detail::setCfgImpl(c, cfg, detail::Pri<1>{}); }

}  // namespace ts::sa
