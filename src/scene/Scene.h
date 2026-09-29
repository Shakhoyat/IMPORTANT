// The ThermalSense world: scene graph, animation and the two autonomous agents.
#pragma once

#include <memory>
#include <vector>

#include "ai/DroneController.h"
#include "ai/HideController.h"
#include "objects/Bunker.h"
#include "objects/Rover.h"
#include "objects/Uav.h"
#include "rendering/Lighting.h"
#include "scene/HeatTrail.h"
#include "scene/SceneNode.h"
#include "vision/ThermalTracker.h"

namespace ts {

class Scene {
public:
    Scene();

    // Advances the simulation by dt seconds. 'det' is the tracker output for the previous camera frame.
    void update(float dt, const Detection& det);
    // Recomputes all world matrices (explode > 0 pushes composite parts apart for the diagnostic view).
    void updateWorld();

    float time() const { return time_; }
    float explode = 0.0f;

    const std::vector<SceneNode*>& leaves() const { return leaves_; }
    std::vector<SceneNode*> roots() const;

    LightRig lights;
    DroneState drone;
    RoverState rover;
    Detection lastDetection;

    const UavRig& uav() const { return uav_; }
    const std::vector<SceneNode*>& practicalLights() const { return practicals_; }
    const HeatTrail& heatTrail() const { return heatTrail_; }
    float wetness = 0.0f;  // 0 dry .. 1 soaked (rain); wet ground cools the heat trail faster
    const RoverRig& roverRig() const { return rover_; }
    const HideController& hideController() const { return hide_; }
    Mat4 cameraWorld() const { return uav_.gimbalPitch->world; }  // UAV thermal camera = searchlight axis
    float beamLength() const { return beamLength_; }

private:
    void applyAgentStates();
    void updateLightsAndBeam();

    std::unique_ptr<SceneNode> terrain_, environment_, backdrop_;
    std::vector<SceneNode*> aviationLights_;  // blinking red obstruction lights (radio masts)
    BunkerRig bunker_;
    RoverRig rover_;
    UavRig uav_;
    std::vector<SceneNode*> leaves_;
    std::vector<SceneNode*> practicals_;  // emissive parts that are also light sources
    HeatTrail heatTrail_;
    void updateHeatTrail(float dt);

    HideController hide_;
    DroneController droneCtl_;
    float time_ = 0.0f;
    float beamLength_ = 5.0f;
};

}  // namespace ts
