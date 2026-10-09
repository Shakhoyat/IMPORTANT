#include "report/Designs.h"

#include <functional>

#include "scene/PartOrder.h"

namespace ts::designs {

namespace {

enum class Match {
    Object,        // the scene root of an object whose family (objectFamily of the tag) is 'key'
    Group,         // a group node named exactly 'key'
    GroupPrefix,   // a group node whose name starts with 'key'
    Leaf           // a leaf named exactly 'key' (a one-part design)
};

struct Rule {
    const char* design;
    const char* family;
    const char* source;
    Match match;
    const char* key;
    const char* object = "";   // only nodes of this object family ("" = any)
};

// Report order: family by family; inside a family the larger designs first. Layout groups that only place designs
// (Environment, "Lone pines", "Forest patch", "Shrubs", "Rocks", "Lamp posts", "Military dressing", "Barracks",
// "Treeline", the Base / Camp / Backdrop / UAV pads roots) have no rule: their leaves all belong to the designs inside.
const Rule kRules[] = {
    // people
    {"Soldier", "people", "objects/Soldier.cpp: buildSoldier", Match::Object, "Soldier"},
    // vehicles and aircraft
    {"Main battle tank", "vehicles and aircraft", "objects/Tank.cpp: Tank::buildRig", Match::Object, "Main Battle Tank"},
    {"MLRS rocket truck", "vehicles and aircraft", "objects/Launchers.cpp: MlrsTruck::build", Match::Object, "MLRS rocket truck"},
    {"Scout helicopter", "vehicles and aircraft", "objects/Helicopter.cpp: Helicopter()", Match::Object, "Scout Helicopter"},
    {"Autonomous UAV", "vehicles and aircraft", "objects/Uav.cpp: buildUav", Match::Object, "Autonomous UAV"},
    {"Cargo truck", "vehicles and aircraft", "objects/Base.cpp: truck", Match::Group, "Cargo truck"},
    {"Target rover", "vehicles and aircraft", "objects/Rover.cpp: buildRover", Match::Object, "Target Rover"},
    // weapons and munitions
    {"SAM launcher", "weapons and munitions", "objects/Launchers.cpp: SamLauncher::build", Match::Object, "SAM launcher"},
    {"AA Gatling gun", "weapons and munitions", "objects/AaGun.cpp: AaGun::buildRig", Match::Object, "AA Gatling Gun"},
    {"SAM missile", "weapons and munitions", "objects/Launchers.cpp: roundGroup", Match::Group, "SAM round", "SAM launcher"},
    {"MLRS rocket", "weapons and munitions", "objects/Launchers.cpp: roundGroup", Match::GroupPrefix, "Rocket ", "MLRS rocket truck"},
    {"UAV round", "weapons and munitions", "objects/Uav.cpp: buildUav (launcher rail)", Match::Group, "Round", "Autonomous UAV"},
    {"AA shell casing", "weapons and munitions", "objects/AaGun.cpp: AaGun::buildRig", Match::Leaf, "Shell casing", "AA Gatling Gun"},
    // buildings
    {"Command bunker", "buildings", "objects/Bunker.cpp: buildBunker", Match::Object, "Command Bunker"},
    {"Barracks hall", "buildings", "objects/BarracksHall.cpp: buildBarracksHall", Match::Object, "Barracks hall"},
    {"Air-defence radar", "buildings", "objects/Radar.cpp: Radar()", Match::Object, "Air-defence radar"},
    {"Motor pool", "buildings", "objects/Base.cpp: addMotorPool", Match::Group, "Motor pool"},
    {"Helipad", "buildings", "objects/Base.cpp: addHelipad", Match::Group, "Helipad"},
    {"Ammunition bunker", "buildings", "objects/Launchers.cpp: makeAmmoBunker", Match::Object, "Ammunition bunker"},
    {"Guard tower", "buildings", "objects/Base.cpp: addTower", Match::GroupPrefix, "Guard tower "},
    {"Watchtower", "buildings", "objects/Structures.cpp: addWatchtower", Match::Group, "Watchtower"},
    {"Fuel depot", "buildings", "objects/Base.cpp: addDepot", Match::Group, "Fuel depot"},
    {"Quonset hut", "buildings", "objects/Base.cpp: addBarracks", Match::GroupPrefix, "Quonset hut"},
    {"Radio mast", "buildings", "objects/Structures.cpp: addRadioMast", Match::Group, "Radio mast"},
    {"Camouflaged tent", "buildings", "objects/Structures.cpp: addCamoTent", Match::Group, "Camouflaged tent"},
    {"Container", "buildings", "objects/Base.cpp: container", Match::GroupPrefix, "Container "},
    {"Guard booth", "buildings", "objects/Base.cpp: addGate", Match::Group, "Guard booth"},
    {"UAV pad", "buildings", "objects/UavPad.cpp: buildUavPads", Match::GroupPrefix, "UAV pad "},
    // fortifications
    {"HESCO perimeter", "fortifications", "objects/Base.cpp: addPerimeter", Match::Group, "HESCO perimeter"},
    {"Concertina wire", "fortifications", "objects/Base.cpp: addPerimeter", Match::Group, "Concertina wire"},
    {"Perimeter fence", "fortifications", "objects/Structures.cpp: addPerimeterFence", Match::Group, "Perimeter fence"},
    {"Outer gate", "fortifications", "objects/Base.cpp: addGate", Match::Group, "Outer gate"},
    {"Camouflage net", "fortifications", "objects/Structures.cpp: addCamoNet", Match::Group, "Camouflage net"},
    {"Earth revetment", "fortifications", "objects/Structures.cpp: addRevetment", Match::Group, "Earth revetment"},
    {"Radar surround", "fortifications", "objects/Base.cpp: addRadarSurround", Match::Group, "Radar surround"},
    {"AA gun surround", "fortifications", "objects/Base.cpp: addAaSurround", Match::Group, "AA gun surround"},
    {"Sandbag wall", "fortifications", "objects/Structures.cpp: addSandbagArc", Match::Group, "Sandbag wall"},
    {"Boom barrier", "fortifications", "objects/Base.cpp: buildBase (gate barrier)", Match::Object, "Boom barrier"},
    {"HESCO blocks", "fortifications", "objects/Structures.cpp: addMilitaryDressing", Match::Group, "HESCO blocks"},
    {"Tank trap", "fortifications", "objects/Base.cpp: addGate", Match::Group, "Tank trap"},
    {"Jersey barrier", "fortifications", "objects/Base.cpp: jersey; Structures.cpp: addMilitaryDressing", Match::Group, "Jersey barrier"},
    // vegetation
    {"Broadleaf tree", "vegetation", "objects/Vegetation.cpp: addRoundTree", Match::Group, "Tree"},
    {"Pine", "vegetation", "objects/Vegetation.cpp: addPine", Match::Group, "Pine"},
    {"Cut branches", "vegetation", "objects/Structures.cpp: addCutBranches", Match::Group, "Cut branches"},
    {"Bush", "vegetation", "objects/Vegetation.cpp: addBush", Match::Group, "Bush"},
    {"Treeline pine", "vegetation", "objects/Backdrop.cpp: addTreeline", Match::Group, "Treeline pine"},
    // props
    {"Flag", "props", "objects/Base.cpp: buildBase (flag)", Match::Object, "Flag"},
    {"Windsock", "props", "objects/Base.cpp: buildBase (windsock)", Match::Object, "Windsock"},
    {"Supply cache", "props", "objects/Structures.cpp: addSupplyCache", Match::Group, "Supply cache"},
    {"Boulder", "props", "objects/Vegetation.cpp: addRock", Match::Group, "Boulder"},
    {"Lamp post", "props", "objects/Structures.cpp: addPracticalLights", Match::Group, "Lamp post"},
    {"Track sign", "props", "objects/Structures.cpp: addMilitaryDressing", Match::Group, "Track sign"},
    {"Fire barrel", "props", "objects/Structures.cpp: addPracticalLights", Match::Group, "Fire barrel"},
    {"Sky clock (sun)", "props", "scene/Scene.cpp: sky clock", Match::Object, "Sky Clock"},
    {"Moon", "props", "objects/Environment.cpp: buildEnvironment", Match::Leaf, "Moon", "Environment"},
    // terrain and backdrop
    {"Terrain disc", "terrain and backdrop", "objects/Terrain.cpp: buildTerrain", Match::Object, "Terrain"},
    {"Far ground", "terrain and backdrop", "objects/Backdrop.cpp: buildBackdrop", Match::Leaf, "Far ground", "Backdrop"},
    {"Distant hills", "terrain and backdrop", "objects/Backdrop.cpp: addHills", Match::Group, "Distant hills"},
    {"Far radio tower", "terrain and backdrop", "objects/Backdrop.cpp: addFarTower", Match::Group, "Far radio tower"},
    {"Road", "terrain and backdrop", "objects/Base.cpp: addRoad", Match::Group, "Road"},
    {"Tyre tracks", "terrain and backdrop", "objects/Base.cpp: addTyreTracks", Match::Group, "Tyre tracks"},
    {"Tank spot marking", "terrain and backdrop", "objects/Base.cpp: addMotorPool", Match::Group, "Tank spot marking"},
};

int ruleFor(const SceneNode& n) {
    const int count = static_cast<int>(sizeof(kRules) / sizeof(kRules[0]));
    for (int i = 0; i < count; ++i) {
        const Rule& r = kRules[i];
        if (r.object[0] && objectFamily(n.object) != r.object) continue;
        switch (r.match) {
            case Match::Object:
                if (!n.parent && objectFamily(n.object) == r.key) return i;
                break;
            case Match::Group:
                if (!n.isLeaf() && n.parent && n.name == r.key) return i;
                break;
            case Match::GroupPrefix:
                if (!n.isLeaf() && n.parent && n.name.rfind(r.key, 0) == 0) return i;
                break;
            case Match::Leaf:
                if (n.isLeaf() && n.name == r.key) return i;
                break;
        }
    }
    return -1;
}

}  // namespace

const Design* Catalogue::find(const std::string& name) const {
    for (const Design& d : designs)
        if (d.name == name) return &d;
    return nullptr;
}

int Catalogue::instanceAt(const SceneNode& n) const {
    for (const SceneNode* p = &n; p; p = p->parent)
        for (size_t i = 0; i < instances.size(); ++i)
            if (instances[i].root == p) return static_cast<int>(i);
    return -1;
}

Catalogue collect(const std::vector<SceneNode*>& roots) {
    Catalogue c;
    const int ruleCount = static_cast<int>(sizeof(kRules) / sizeof(kRules[0]));
    std::vector<int> designOfRule(static_cast<size_t>(ruleCount), -1);
    std::vector<std::vector<int>> byRule(static_cast<size_t>(ruleCount));   // instances per rule, scene order
    std::function<void(const SceneNode&, int)> walk = [&](const SceneNode& n, int owner) {
        const int rule = ruleFor(n);
        if (rule >= 0) {
            Instance in;
            in.root = &n;
            in.parent = owner;
            in.design = rule;   // rule index for now, remapped below
            c.instances.push_back(in);
            const int id = static_cast<int>(c.instances.size()) - 1;
            if (owner >= 0) c.instances[static_cast<size_t>(owner)].nested.push_back(id);
            byRule[static_cast<size_t>(rule)].push_back(id);
            owner = id;
        }
        if (n.isLeaf()) {
            if (owner >= 0) {
                c.instances[static_cast<size_t>(owner)].own.push_back(&n);
                c.instanceOf[&n] = owner;
            } else {
                c.unassigned.push_back(&n);
            }
        }
        for (const auto& ch : n.children) walk(*ch, owner);
    };
    for (const SceneNode* r : roots) walk(*r, -1);
    for (int i = 0; i < ruleCount; ++i) {
        if (byRule[static_cast<size_t>(i)].empty()) continue;
        // Two rules may name the same design (none do today); merge by name.
        int d = -1;
        for (size_t k = 0; k < c.designs.size(); ++k)
            if (c.designs[k].name == kRules[i].design) d = static_cast<int>(k);
        if (d < 0) {
            Design ds;
            ds.name = kRules[i].design;
            ds.family = kRules[i].family;
            ds.source = kRules[i].source;
            c.designs.push_back(ds);
            d = static_cast<int>(c.designs.size()) - 1;
        }
        designOfRule[static_cast<size_t>(i)] = d;
        for (int id : byRule[static_cast<size_t>(i)]) c.designs[static_cast<size_t>(d)].instances.push_back(id);
    }
    for (Instance& in : c.instances) in.design = designOfRule[static_cast<size_t>(in.design)];
    return c;
}

}  // namespace ts::designs
