// T150-T159: hidden camp (light discipline, camouflage nets, launchers, revetments).
#include <cmath>
#include <cstdio>

#include "TestKit.h"
#include "objects/Base.h"
#include "objects/Bunker.h"
#include "objects/Environment.h"

using ts::testkit::fmt;
using ts::testkit::record;

namespace {
using namespace ts;
void countLights(const SceneNode& n, int& count, float& power) {
    if (n.isLeaf() && n.material.lightIntensity > 0.0f) {
        ++count;
        power += n.material.lightIntensity * n.material.lightRange;
    }
    for (const auto& c : n.children) countLights(*c, count, power);
}
}  // namespace

TS_TEST_GROUP(CampProbe) {
    int c = 0; float p = 0;
    auto base = buildBase(); countLights(*base.root, c, p);
    int c2 = 0; float p2 = 0; auto env = buildEnvironment(); countLights(*env, c2, p2);
    int c3 = 0; float p3 = 0; auto bk = buildBunker(); countLights(*bk.root, c3, p3);
    record("T159", "probe", "-", fmt("base %d (%.0f) env %d (%.0f) bunker %d (%.0f)", c, p, c2, p2, c3, p3), true);
}
