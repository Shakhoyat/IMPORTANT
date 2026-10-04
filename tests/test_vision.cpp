// Computer vision v5: image stage (threshold, despeckle, components, moments) and the multi-target tracker
// (Kalman filter, gating, Hungarian assignment, two-view triangulation). T130-T139. CPU only, synthetic data.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

#include "TestKit.h"
#include "vision/CvPipeline.h"
#include "vision/MultiTracker.h"

using namespace ts;
using ts::testkit::fmt;
using ts::testkit::record;

namespace {

constexpr int W = 320, H = 240;

void disc(std::vector<uint8_t>& img, float cx, float cy, float r, uint8_t v) {
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) img[y * W + x] = std::max(img[y * W + x], v);
}

}  // namespace

TS_TEST_GROUP(Vision) {
    // T130: components, centroids and despeckle on a synthetic heat image.
    {
        std::vector<uint8_t> img(W * H, 60);
        std::mt19937 rng(7);
        for (int i = 0; i < 400; ++i) img[rng() % (W * H)] = 240;  // isolated hot speckles (sparks, glints)
        disc(img, 50.0f, 40.0f, 4.0f, 245);
        disc(img, 200.0f, 120.0f, 6.0f, 250);
        disc(img, 300.0f, 220.0f, 3.0f, 235);
        CvFrameStats st;
        const auto m = cv::detectTargets(img, W, H, CvConfig{}, &st);
        float worst = 0.0f;
        const float truth[3][2] = {{50, 40}, {200, 120}, {300, 220}};
        int matched = 0;
        for (const auto& t : truth) {
            for (const Measurement2D& b : m) {
                const float d = std::hypot(b.cx - t[0], b.cy - t[1]);
                if (d < 1.0f) {
                    ++matched;
                    worst = std::max(worst, d);
                }
            }
        }
        record("T130", "CV image stage: 3 hot discs found among 400 speckles, centroids exact",
               "3 targets, centroid error < 0.05 px, speckles removed",
               fmt("%d targets (%d matched), worst centroid error %.3f px, %d speckle px removed, %d components", st.targets,
                   matched, worst, st.removedSpeckles, st.components),
               st.targets == 3 && matched == 3 && worst < 0.05f && st.removedSpeckles >= 350);
    }
    // T131: union-find labelling on a U shape (two branches meet late): exactly one component.
    {
        std::vector<uint8_t> mask(W * H, 0);
        for (int y = 10; y < 60; ++y) mask[y * W + 20] = mask[y * W + 21] = mask[y * W + 60] = mask[y * W + 61] = 1;
        for (int x = 20; x <= 61; ++x) mask[60 * W + x] = mask[61 * W + x] = 1;
        for (int y = 100; y < 104; ++y)
            for (int x = 100; x < 104; ++x) mask[y * W + x] = 1;
        std::vector<int> labels;
        const int n = cv::labelComponents(mask, W, H, labels);
        record("T131", "Two-pass union-find labelling merges a U shape into one component",
               "2 components (U + square)", fmt("%d components, U label %d at both tops", n, labels[10 * W + 20] == labels[10 * W + 60]),
               n == 2 && labels[10 * W + 20] == labels[10 * W + 60]);
    }
    // T132: Otsu on a bimodal histogram picks a threshold between the modes; moments give orientation.
    {
        std::vector<uint8_t> img(W * H, 40);
        for (int y = 100; y < 104; ++y)
            for (int x = 50; x < 90; ++x) img[y * W + x] = 200;  // horizontal hot bar
        const uint8_t t = cv::otsuThreshold(img);
        CvConfig cfg;
        cfg.floor = 150;
        cfg.core = 150;
        cfg.maxElongation = 100.0f;
        const auto m = cv::detectTargets(img, W, H, cfg);
        record("T132", "Otsu threshold separates a bimodal image; second moments give the bar's axis and elongation",
               "40 <= t < 200 (dark class <= t), orientation ~0 deg, elongation > 5",
               fmt("t = %d, orientation %.1f deg, elongation %.1f", t, m.empty() ? 0.0f : m[0].orientationDeg,
                   m.empty() ? 0.0f : m[0].elongation),
               t >= 40 && t < 200 && !m.empty() && std::fabs(m[0].orientationDeg) < 1.0f && m[0].elongation > 5.0f);
    }
    // T133: Hungarian assignment equals brute force on random 5x5 problems.
    {
        std::mt19937 rng(3);
        std::uniform_real_distribution<float> U(0.0f, 10.0f);
        int agree = 0;
        for (int trial = 0; trial < 50; ++trial) {
            std::vector<std::vector<float>> c(5, std::vector<float>(5));
            for (auto& r : c)
                for (float& v : r) v = U(rng);
            const auto a = MultiTracker::hungarian(c);
            float sa = 0.0f;
            for (int i = 0; i < 5; ++i) sa += c[i][a[i]];
            int perm[5] = {0, 1, 2, 3, 4};
            float best = 1e9f;
            do {
                float s = 0.0f;
                for (int i = 0; i < 5; ++i) s += c[i][perm[i]];
                best = std::min(best, s);
            } while (std::next_permutation(perm, perm + 5));
            agree += std::fabs(sa - best) < 1e-4f ? 1 : 0;
        }
        record("T133", "Hungarian assignment finds the optimal total cost (vs brute force over 120 permutations)",
               "50 / 50 random problems optimal", fmt("%d / 50", agree), agree == 50);
    }
    // T134: two-view triangulation beats a single noisy ground ray.
    {
        const Vec3 target{4.0f, 0.2f, -3.0f};
        const Vec3 o1{-2.0f, 6.0f, 5.0f}, o2{9.0f, 5.0f, 2.0f};
        Vec3 d1 = normalize(target - o1), d2 = normalize(target - o2);
        d1 = normalize(d1 + Vec3{0.004f, -0.003f, 0.002f});  // ~0.3 deg pointing noise per camera
        d2 = normalize(d2 + Vec3{-0.003f, 0.004f, -0.002f});
        Vec3 p;
        float res = 0.0f;
        const bool ok = MultiTracker::triangulate(o1, d1, o2, d2, p, res);
        // single ray hitting the plane y = 0.2
        const float s = (0.2f - o1.y) / d1.y;
        const Vec3 single = o1 + d1 * s;
        const float eTri = length(p - target), eOne = length(single - target);
        record("T134", "Two-view triangulation (closest points of two rays) vs one ray on the ground",
               "triangulated error < single-ray error, residual small",
               fmt("triangulated %.3f m (residual %.3f m), single ray %.3f m", eTri, res, eOne), ok && eTri < eOne && res < 0.2f);
    }
    // T135: Kalman tracker follows a target moving at 1.2 m/s with noisy measurements; velocity converges.
    {
        MultiTracker mt;
        std::mt19937 rng(11);
        std::normal_distribution<float> N(0.0f, 0.25f);
        float maxErrLate = 0.0f;
        const float dt = 1.0f / 30.0f;
        for (int k = 0; k < 150; ++k) {
            const float t = k * dt;
            const Vec3 truth{-5.0f + 1.2f * t, 0.0f, 2.0f};
            mt.predict(dt);
            GroundMeasurement m;
            m.point = truth + Vec3{N(rng), 0.0f, N(rng)};
            m.sigma = 0.25f;
            mt.update({m}, t);
            if (k > 90 && !mt.tracks().empty()) maxErrLate = std::max(maxErrLate, length(mt.tracks()[0].position() - truth));
        }
        const bool one = mt.tracks().size() == 1;
        const float vx = one ? mt.tracks()[0].x[2] : 0.0f;
        record("T135", "Kalman tracker: one confirmed track, position error < 0.25 m, velocity -> 1.2 m/s",
               "1 confirmed track, |vx - 1.2| < 0.25 m/s, late error < 0.25 m",
               fmt("%zu tracks, state %d, vx %.2f m/s, late max error %.3f m", mt.tracks().size(),
                   one ? static_cast<int>(mt.tracks()[0].state) : -1, vx, maxErrLate),
               one && mt.tracks()[0].state == TrackState::Confirmed && std::fabs(vx - 1.2f) < 0.25f && maxErrLate < 0.25f);
    }
    // T136: two crossing targets keep their identities (gating + assignment), a clutter point is not confirmed.
    {
        MultiTracker mt;
        const float dt = 1.0f / 30.0f;
        int idA = -1, idB = -1;
        bool swapped = false;
        int confirmedClutter = 0;
        for (int k = 0; k < 120; ++k) {
            const float t = k * dt;
            const Vec3 a{-4.0f + 2.0f * t, 0.0f, -1.0f + 0.5f * t}, b{4.0f - 2.0f * t, 0.0f, 1.0f - 0.5f * t};
            mt.predict(dt);
            std::vector<GroundMeasurement> ms(2);
            ms[0].point = a;
            ms[1].point = b;
            ms[0].sigma = ms[1].sigma = 0.15f;
            if (k == 40) {  // one-frame clutter blob
                GroundMeasurement c;
                c.point = {10.0f, 0.0f, 10.0f};
                ms.push_back(c);
            }
            mt.update(ms, t);
            for (const Track& tr : mt.tracks()) {
                if (tr.state == TrackState::Tentative) continue;
                if (length(tr.position() - Vec3{10, 0, 10}) < 1.0f) ++confirmedClutter;
                const bool nearA = length(tr.position() - a) < 0.5f;
                if (k == 10) (nearA ? idA : idB) = tr.id;
                // Identity check only where the targets are apart (at the crossing both are near both).
                if (k > 10 && length(a - b) > 1.5f && nearA && tr.id != idA) swapped = true;
            }
        }
        record("T136", "Two crossing targets keep their track IDs; one-frame clutter never becomes a track",
               "no ID swap, clutter never confirmed, 2 tracks at the end",
               fmt("ids %d / %d, swapped %d, clutter confirmed %d, tracks %zu", idA, idB, swapped, confirmedClutter, mt.tracks().size()),
               idA > 0 && idB > 0 && !swapped && confirmedClutter == 0 && mt.tracks().size() == 2);
    }
    // T137: both UAVs see the target: tracks are fused (one track, triangulated updates), not duplicated.
    {
        MultiTracker mt;
        const Vec3 target{2.0f, 0.15f, 1.0f};
        const Vec3 o1{-4.0f, 6.0f, 5.0f}, o2{7.0f, 5.0f, -3.0f};
        for (int k = 0; k < 20; ++k) {
            mt.predict(1.0f / 30.0f);
            std::vector<GroundMeasurement> ms(2);
            ms[0].rayOrigin = o1; ms[0].rayDir = normalize(target - o1); ms[0].point = target + Vec3{0.2f, 0, -0.1f};
            ms[1].rayOrigin = o2; ms[1].rayDir = normalize(target - o2); ms[1].point = target + Vec3{-0.15f, 0, 0.2f};
            ms[0].sensor = 0; ms[1].sensor = 1;
            ms[0].sigma = ms[1].sigma = 0.3f;
            mt.update(ms, k / 30.0f);
        }
        const bool one = mt.tracks().size() == 1;
        const float err = one ? length(mt.tracks()[0].position() - target) : 99.0f;
        record("T137", "Squad fusion: one target seen by both UAVs -> one track updated by triangulation",
               "1 track, all updates fused, error < 0.05 m (ground points are 0.2 m off)",
               fmt("%zu tracks, %d fused updates, error %.3f m", mt.tracks().size(), mt.fusedUpdates(), err),
               one && mt.fusedUpdates() >= 19 && err < 0.05f);
    }
}
