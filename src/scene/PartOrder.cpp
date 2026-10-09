#include "scene/PartOrder.h"

#include <cctype>
#include <cmath>
#include <functional>

namespace ts {

const SceneNode* rigRoot(const SceneNode& n) {
    const SceneNode* r = &n;
    while (r->parent && r->parent->object == n.object) r = r->parent;
    return r;
}

std::vector<const SceneNode*> rigLeavesDfs(const SceneNode& root) {
    std::vector<const SceneNode*> out;
    std::function<void(const SceneNode&)> walk = [&](const SceneNode& n) {
        if (n.object != root.object) return;
        if (n.isLeaf()) out.push_back(&n);
        for (const auto& c : n.children) walk(*c);
    };
    walk(root);
    return out;
}

int buildStageCount(int parts, int pct) {
    if (parts <= 0) return 0;
    const int k = static_cast<int>((static_cast<long long>(parts) * pct + 99) / 100);
    return k < 1 ? 1 : (k > parts ? parts : k);
}

Vec3 restScaleOf(const SceneNode& n) {
    const Vec3& r = n.restScale;
    return (r.x != 0.0f || r.y != 0.0f || r.z != 0.0f) ? r : n.local.scale;
}

bool hiddenAtRest(const SceneNode& n) {
    for (const SceneNode* p = &n; p; p = p->parent)
        if (!p->visible) return true;
    const Mat4& w = n.world;
    const float det = w.at(0, 0) * (w.at(1, 1) * w.at(2, 2) - w.at(1, 2) * w.at(2, 1)) -
                      w.at(0, 1) * (w.at(1, 0) * w.at(2, 2) - w.at(1, 2) * w.at(2, 0)) +
                      w.at(0, 2) * (w.at(1, 0) * w.at(2, 1) - w.at(1, 1) * w.at(2, 0));
    return std::fabs(det) < 1e-9f || w.at(1, 3) < -6.0f;
}

namespace {

bool isSuffixToken(const std::string& t) {
    static const char* sides[] = {"L", "R", "A", "B", "C", "D", "FL", "FR", "RL", "RR", "BL", "BR"};
    for (const char* s : sides)
        if (t == s) return true;
    size_t i = 0;
    if (i < t.size() && (t[i] == 'L' || t[i] == 'R' || t[i] == '#')) ++i;
    if (i == t.size()) return false;
    for (; i < t.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(t[i]))) return false;
    return true;
}

}  // namespace

std::string partKindName(const std::string& name) {
    std::string s = name;
    for (;;) {
        const size_t sp = s.find_last_of(' ');
        if (sp == std::string::npos || sp == 0) break;
        if (!isSuffixToken(s.substr(sp + 1))) break;
        s = s.substr(0, sp);
    }
    return s;
}

std::vector<PartKind> partKinds(const SceneNode& root) {
    std::vector<PartKind> kinds;
    for (const SceneNode* n : rigLeavesDfs(root)) {
        const std::string k = partKindName(n->name);
        bool found = false;
        for (PartKind& pk : kinds)
            if (pk.name == k && pk.primitive == n->primitive) {
                ++pk.count;
                found = true;
                break;
            }
        if (!found) kinds.push_back({k, n->primitive, n, 1});
    }
    return kinds;
}

std::string objectFamily(const std::string& object) {
    const size_t sp = object.find_last_of(' ');
    if (sp == std::string::npos || sp + 1 >= object.size()) return object;
    for (size_t i = sp + 1; i < object.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(object[i]))) return object;
    return object.substr(0, sp);
}

std::string slug(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) o += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if (!o.empty() && o.back() != '_') o += '_';
    }
    while (!o.empty() && o.back() == '_') o.pop_back();
    return o;
}

}  // namespace ts
