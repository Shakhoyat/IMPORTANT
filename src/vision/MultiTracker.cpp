#include "vision/MultiTracker.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ts {

namespace {

constexpr float kBig = 1e6f;
float& at(float* P, int r, int c) { return P[r * 4 + c]; }
float at(const float* P, int r, int c) { return P[r * 4 + c]; }

}  // namespace

float Track::positionSigma() const { return std::sqrt(std::max(0.0f, 0.5f * (P[0] + P[5]))); }

void MultiTracker::predict(float dt) {
    // x' = F x, P' = F P F^T + Q with F = [I dt I; 0 I], Q from a white-noise acceleration of variance q.
    const float q = accelNoise * accelNoise;
    const float dt2 = dt * dt, dt3 = dt2 * dt, dt4 = dt3 * dt;
    for (Track& t : tracks_) {
        t.x[0] += t.x[2] * dt;
        t.x[1] += t.x[3] * dt;
        float F[16] = {1, 0, dt, 0, 0, 1, 0, dt, 0, 0, 1, 0, 0, 0, 0, 1};
        float FP[16] = {}, N[16] = {};
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                for (int k = 0; k < 4; ++k) FP[r * 4 + c] += F[r * 4 + k] * t.P[k * 4 + c];
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                for (int k = 0; k < 4; ++k) N[r * 4 + c] += FP[r * 4 + k] * F[c * 4 + k];
        const float qa = q * dt4 / 4.0f, qb = q * dt3 / 2.0f, qc = q * dt2;
        at(N, 0, 0) += qa; at(N, 1, 1) += qa;
        at(N, 0, 2) += qb; at(N, 2, 0) += qb; at(N, 1, 3) += qb; at(N, 3, 1) += qb;
        at(N, 2, 2) += qc; at(N, 3, 3) += qc;
        std::copy(N, N + 16, t.P);
        t.age += dt;
        t.sinceUpdate += dt;
        t.seenNow = 0;
        t.triangulated = false;
    }
}

float MultiTracker::mahalanobis2(const Track& t, float mx, float mz, float r) {
    // S = H P H^T + R (2x2 position block + r I); d2 = y^T S^-1 y.
    const float s00 = at(t.P, 0, 0) + r, s01 = at(t.P, 0, 1), s11 = at(t.P, 1, 1) + r;
    const float det = s00 * s11 - s01 * s01;
    if (det <= 1e-12f) return kBig;
    const float y0 = mx - t.x[0], y1 = mz - t.x[1];
    return (y0 * y0 * s11 - 2.0f * y0 * y1 * s01 + y1 * y1 * s00) / det;
}

void MultiTracker::kalmanUpdate(Track& t, float mx, float mz, float r) {
    const float s00 = at(t.P, 0, 0) + r, s01 = at(t.P, 0, 1), s11 = at(t.P, 1, 1) + r;
    const float det = s00 * s11 - s01 * s01;
    if (det <= 1e-12f) return;
    const float i00 = s11 / det, i01 = -s01 / det, i11 = s00 / det;
    // K = P H^T S^-1 (4x2); H picks columns 0 and 1 of P.
    float K[4][2];
    for (int r4 = 0; r4 < 4; ++r4) {
        const float p0 = at(t.P, r4, 0), p1 = at(t.P, r4, 1);
        K[r4][0] = p0 * i00 + p1 * i01;
        K[r4][1] = p0 * i01 + p1 * i11;
    }
    const float y0 = mx - t.x[0], y1 = mz - t.x[1];
    for (int i = 0; i < 4; ++i) t.x[i] += K[i][0] * y0 + K[i][1] * y1;
    // P = (I - K H) P
    float N[16];
    for (int rr = 0; rr < 4; ++rr)
        for (int c = 0; c < 4; ++c) N[rr * 4 + c] = at(t.P, rr, c) - (K[rr][0] * at(t.P, 0, c) + K[rr][1] * at(t.P, 1, c));
    std::copy(N, N + 16, t.P);
}

std::vector<int> MultiTracker::hungarian(const std::vector<std::vector<float>>& costIn) {
    const int rows = static_cast<int>(costIn.size());
    if (rows == 0) return {};
    const int cols = static_cast<int>(costIn[0].size());
    const int n = std::max(rows, cols);
    // Square matrix padded with large costs; classic O(n^3) potentials formulation (1-based).
    std::vector<std::vector<double>> a(n + 1, std::vector<double>(n + 1, kBig));
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j) a[i + 1][j + 1] = costIn[i][j];
    std::vector<double> u(n + 1, 0.0), v(n + 1, 0.0);
    std::vector<int> p(n + 1, 0), way(n + 1, 0);
    for (int i = 1; i <= n; ++i) {
        p[0] = i;
        int j0 = 0;
        std::vector<double> minv(n + 1, std::numeric_limits<double>::infinity());
        std::vector<bool> used(n + 1, false);
        do {
            used[j0] = true;
            const int i0 = p[j0];
            double delta = std::numeric_limits<double>::infinity();
            int j1 = 0;
            for (int j = 1; j <= n; ++j) {
                if (used[j]) continue;
                const double cur = a[i0][j] - u[i0] - v[j];
                if (cur < minv[j]) {
                    minv[j] = cur;
                    way[j] = j0;
                }
                if (minv[j] < delta) {
                    delta = minv[j];
                    j1 = j;
                }
            }
            for (int j = 0; j <= n; ++j) {
                if (used[j]) {
                    u[p[j]] += delta;
                    v[j] -= delta;
                } else {
                    minv[j] -= delta;
                }
            }
            j0 = j1;
        } while (p[j0] != 0);
        do {
            const int j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0);
    }
    std::vector<int> result(rows, -1);
    for (int j = 1; j <= n; ++j) {
        const int i = p[j];
        if (i >= 1 && i <= rows && j <= cols && costIn[i - 1][j - 1] < kBig * 0.5f) result[i - 1] = j - 1;
    }
    return result;
}

bool MultiTracker::triangulate(const Vec3& o1, const Vec3& d1, const Vec3& o2, const Vec3& d2, Vec3& out,
                               float& residual) {
    // Minimise |o1 + s d1 - (o2 + t d2)|^2 -> 2x2 linear system.
    const Vec3 w = o1 - o2;
    const float a = dot(d1, d1), b = dot(d1, d2), c = dot(d2, d2), d = dot(d1, w), e = dot(d2, w);
    const float den = a * c - b * b;
    if (std::fabs(den) < 1e-6f * a * c) return false;  // rays (nearly) parallel: no depth from two views
    const float s = (b * e - c * d) / den, t = (a * e - b * d) / den;
    if (s <= 0.0f || t <= 0.0f) return false;          // behind a camera
    const Vec3 p1 = o1 + d1 * s, p2 = o2 + d2 * t;
    out = (p1 + p2) * 0.5f;
    residual = length(p1 - p2);
    return true;
}

void MultiTracker::update(const std::vector<GroundMeasurement>& meas, float time) {
    const int nT0 = static_cast<int>(tracks_.size());
    // Per track and sensor, the index of the associated measurement this frame.
    std::vector<int> assoc[2];
    assoc[0].assign(nT0, -1);
    assoc[1].assign(nT0, -1);
    std::vector<int> createdBy(nT0, -1);  // measurement that started a track this frame

    for (int sensor = 0; sensor < 2; ++sensor) {
        std::vector<int> idx;
        for (size_t i = 0; i < meas.size(); ++i)
            if (meas[i].sensor == sensor) idx.push_back(static_cast<int>(i));
        const int nT = static_cast<int>(tracks_.size());
        assoc[0].resize(nT, -1);
        assoc[1].resize(nT, -1);
        createdBy.resize(nT, -1);
        std::vector<bool> used(idx.size(), false);
        if (nT > 0 && !idx.empty()) {
            std::vector<std::vector<float>> cost(nT, std::vector<float>(idx.size(), kBig));
            for (int t = 0; t < nT; ++t) {
                for (size_t k = 0; k < idx.size(); ++k) {
                    const GroundMeasurement& m = meas[idx[k]];
                    const float d2 = mahalanobis2(tracks_[t], m.point.x, m.point.z, m.sigma * m.sigma);
                    if (d2 < gateChi2) cost[t][k] = d2;
                }
            }
            const std::vector<int> a = hungarian(cost);
            for (int t = 0; t < nT; ++t) {
                if (a[t] < 0) continue;
                assoc[sensor][t] = idx[a[t]];
                used[a[t]] = true;
            }
        }
        // Unassigned measurements start tentative tracks (sensor 1 can still join them this frame).
        for (size_t k = 0; k < idx.size(); ++k) {
            if (used[k]) continue;
            const GroundMeasurement& m = meas[idx[k]];
            Track t;
            t.id = nextId_++;
            t.x[0] = m.point.x;
            t.x[1] = m.point.z;
            t.height = m.point.y;
            const float pv = m.sigma * m.sigma;
            t.P[0] = pv; t.P[5] = pv; t.P[10] = 4.0f; t.P[15] = 4.0f;  // unknown speed: 2 m/s sigma
            t.hits = 1;
            t.lastSeen[sensor] = time;
            t.seenNow = 1u << sensor;
            tracks_.push_back(t);
            assoc[0].push_back(-1);
            assoc[1].push_back(-1);
            createdBy.push_back(idx[k]);
        }
    }

    for (size_t ti = 0; ti < tracks_.size(); ++ti) {
        Track& t = tracks_[ti];
        int m0 = assoc[0][ti];
        const int m1 = assoc[1][ti];
        const bool fresh = createdBy[ti] >= 0;
        // A track born from UAV 0 this frame and matched by UAV 1: triangulate with the creating measurement.
        if (fresh && m0 < 0 && m1 >= 0 && meas[createdBy[ti]].sensor == 0) {
            m0 = createdBy[ti];
            t.hits = 0;
        } else if (fresh) {
            continue;  // just created from this measurement
        }
        if (m0 < 0 && m1 < 0) {
            if (!fresh) ++t.misses;
            continue;
        }
        if (m0 >= 0 && m1 >= 0) {
            // Two views of the same target: one fused measurement from the rays' closest approach.
            const GroundMeasurement& a = meas[m0];
            const GroundMeasurement& b = meas[m1];
            Vec3 p;
            float res;
            if (triangulate(a.rayOrigin, a.rayDir, b.rayOrigin, b.rayDir, p, res) && res < 1.2f) {
                const float s = std::max(0.08f, 0.5f * std::min(a.sigma, b.sigma) + 0.25f * res);
                kalmanUpdate(t, p.x, p.z, s * s);
                t.height = p.y;
                t.triangulated = true;
                t.triResidual = res;
                ++fused_;
            } else {
                kalmanUpdate(t, a.point.x, a.point.z, a.sigma * a.sigma);
                kalmanUpdate(t, b.point.x, b.point.z, b.sigma * b.sigma);
                t.height = 0.5f * (a.point.y + b.point.y);
            }
            t.lastSeen[0] = t.lastSeen[1] = time;
            t.seenNow = 3u;
        } else {
            const GroundMeasurement& m = meas[m0 >= 0 ? m0 : m1];
            kalmanUpdate(t, m.point.x, m.point.z, m.sigma * m.sigma);
            t.height = m.point.y;
            t.lastSeen[m.sensor] = time;
            t.seenNow |= 1u << m.sensor;
        }
        ++t.hits;
        t.misses = 0;
        t.sinceUpdate = 0.0f;
    }

    // Life cycle.
    for (Track& t : tracks_) {
        if (t.state == TrackState::Tentative && t.hits >= confirmHits) t.state = TrackState::Confirmed;
        if (t.state != TrackState::Tentative) t.state = t.sinceUpdate > coastAfter ? TrackState::Coasting : TrackState::Confirmed;
    }
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
                                 [&](const Track& t) {
                                     if (t.state == TrackState::Tentative) return t.misses >= 2;
                                     return t.sinceUpdate > deleteAfter;
                                 }),
                  tracks_.end());
}

const Track* MultiTracker::find(int id) const {
    for (const Track& t : tracks_)
        if (t.id == id) return &t;
    return nullptr;
}

void MultiTracker::remove(int id) {
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(), [id](const Track& t) { return t.id == id; }),
                  tracks_.end());
}

}  // namespace ts
