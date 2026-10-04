// Multi-target tracking on the ground plane, shared by both UAVs (the squad's common track picture).
//   - one constant-velocity Kalman filter per track, state (x, z, vx, vz), white-noise acceleration model
//   - gating: squared Mahalanobis distance of the innovation < chi-square(2 dof, 99 %) = 9.21
//   - assignment: Hungarian algorithm (Kuhn-Munkres) on the gated Mahalanobis costs, one sensor at a time
//   - two-view triangulation: a track seen by both UAVs in the same frame gets one fused measurement from the
//     midpoint of the shortest segment between the two pixel rays (much smaller error than one ground ray)
//   - life cycle: tentative -> confirmed (3 hits) -> coasting (no update) -> deleted (3 s unseen, or a tentative
//     track missing 2 frames)
#pragma once

#include <vector>

#include "math/Math3D.h"

namespace ts {

struct GroundMeasurement {
    Vec3 point;          // ray hit on the terrain
    Vec3 rayOrigin, rayDir;
    float sigma = 0.3f;  // 1-sigma position error on the ground (m), grows with range
    int sensor = 0;      // UAV index
    int area = 0;        // blob size in pixels
};

enum class TrackState { Tentative, Confirmed, Coasting };

struct Track {
    int id = 0;
    TrackState state = TrackState::Tentative;
    float x[4] = {0, 0, 0, 0};     // x, z, vx, vz
    float P[16] = {};              // covariance, row-major 4x4
    int hits = 0, misses = 0;
    float age = 0.0f, sinceUpdate = 0.0f;
    float lastSeen[2] = {-1e9f, -1e9f};  // scene time each UAV last measured this track
    unsigned seenNow = 0;          // bit per sensor: measured in the latest update
    bool triangulated = false;     // the latest update used two views
    float triResidual = 0.0f;      // distance between the two rays at closest approach (m)
    float height = 0.0f;           // y of the latest measurement / triangulation
    Vec3 position() const { return {x[0], height, x[1]}; }
    Vec3 velocity() const { return {x[2], 0.0f, x[3]}; }
    float positionSigma() const;   // sqrt of the mean position variance (m)
};

class MultiTracker {
public:
    float gateChi2 = 9.21f;        // 2 dof, 99 %
    float accelNoise = 1.5f;       // m/s^2, process noise of the constant-velocity model
    int confirmHits = 3;
    float coastAfter = 0.25f, deleteAfter = 3.0f;

    void predict(float dt);
    // All measurements of one frame from all sensors.
    void update(const std::vector<GroundMeasurement>& measurements, float time);
    const std::vector<Track>& tracks() const { return tracks_; }
    std::vector<Track>& tracks() { return tracks_; }
    const Track* find(int id) const;
    void remove(int id);
    void clear() { tracks_.clear(); }
    int fusedUpdates() const { return fused_; }  // two-view updates so far (statistics)

    // Hungarian algorithm for a rectangular cost matrix (rows = tracks, cols = measurements).
    // Returns col per row (-1 = unassigned). Minimises the total cost.
    static std::vector<int> hungarian(const std::vector<std::vector<float>>& cost);
    // Closest points of two rays; out = midpoint, residual = their distance. False if nearly parallel.
    static bool triangulate(const Vec3& o1, const Vec3& d1, const Vec3& o2, const Vec3& d2, Vec3& out, float& residual);
    // Squared Mahalanobis distance of a measurement (x, z) with variance r to the predicted track.
    static float mahalanobis2(const Track& t, float mx, float mz, float r);

private:
    void kalmanUpdate(Track& t, float mx, float mz, float r);
    std::vector<Track> tracks_;
    int nextId_ = 1;
    int fused_ = 0;
};

}  // namespace ts
