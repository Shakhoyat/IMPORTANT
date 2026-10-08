// v6 sensor model + real computer vision under noise (T225-T236). CPU only: synthetic heat frames
// (vision/SensorModel makeSyntheticFrame) through the microbolometer model and the CV image stage.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "TestKit.h"
#include "ai/DroneController.h"
#include "scene/Scene.h"
#include "vision/CvPipeline.h"
#include "vision/MultiTracker.h"
#include "vision/SensorModel.h"

using namespace ts;
using ts::testkit::fmt;
using ts::testkit::record;

namespace {

constexpr int W = kCvWidth, H = kCvHeight;

// A clean sensor: no fixed pattern, no stuck pixels (temporal noise / PSF as configured).
SensorNoiseConfig cleanConfig() {
    SensorNoiseConfig c;
    c.fpnGainSigma = c.fpnOffsetSigma = c.columnSigma = 0.0f;
    c.stuckFrac = 0.0f;
    return c;
}

// Random position away from the border, from a PCG32 stream.
void randomSpot(Pcg32& r, float& x, float& y, float margin = 16.0f) {
    x = margin + r.uniform() * (W - 2.0f * margin);
    y = margin + r.uniform() * (H - 2.0f * margin);
}

bool sameMeasurement(const Measurement2D& a, const Measurement2D& b) {
    return a.u == b.u && a.v == b.v && a.cx == b.cx && a.cy == b.cy && a.area == b.area && a.meanHeat == b.meanHeat &&
           a.peak == b.peak && a.orientationDeg == b.orientationDeg && a.elongation == b.elongation &&
           a.minX == b.minX && a.minY == b.minY && a.maxX == b.maxX && a.maxY == b.maxY;
}

}  // namespace

TS_TEST_GROUP(Sensor) {
    // T225: temporal noise. The difference of two frames of a flat scene cancels the fixed pattern; its variance is
    // 2 (NETD^2 + 1/12) (the 1/12 is the quantisation of each frame).
    {
        SensorModel s(11);
        s.nuc();
        std::vector<uint8_t> a(W * H, 100), b(W * H, 100);
        s.apply(a, W, H);
        s.apply(b, W, H);
        double sum = 0, sum2 = 0, n = 0, within = 0;
        std::vector<char> stuck(W * H, 0);
        for (int i : s.stuckPixels()) stuck[i] = 1;
        for (int i = 0; i < W * H; ++i) {
            if (stuck[i]) continue;
            const double d = static_cast<double>(a[i]) - b[i];
            sum += d; sum2 += d * d; n += 1;
            if (std::fabs(d) <= 2.0 * std::sqrt(2.0)) within += 1;  // |d| <= 1 sigma of the difference
        }
        const double mean = sum / n, var = sum2 / n - mean * mean;
        const double netd = std::sqrt(std::max(0.0, var / 2.0 - 1.0 / 12.0));
        record("T225", "Sensor temporal noise (NETD) from the difference of two flat frames (fixed pattern cancels)",
               "NETD 2.0 +- 0.1 DN, zero mean, ~68 % of differences within 1 sigma (Gaussian, Box-Muller)",
               fmt("NETD %.3f DN, mean %.3f DN, within 1 sigma %.1f %%", netd, mean, 100.0 * within / n),
               std::fabs(netd - 2.0) < 0.1 && std::fabs(mean) < 0.05 && within / n > 0.62 && within / n < 0.76);
    }
    // T226: fixed pattern (gain, offset, column stripes) before / after the NUC; stuck pixels.
    {
        SensorModel s(12);
        const float level = 120.0f;
        auto spatial = [&](double& pixelStd, double& columnStd) {
            std::vector<double> mean(W * H, 0.0);
            const int frames = 64;  // averages the temporal noise down to 2 / 8 DN
            for (int k = 0; k < frames; ++k) {
                std::vector<uint8_t> img(W * H, static_cast<uint8_t>(level));
                s.apply(img, W, H);
                for (int i = 0; i < W * H; ++i) mean[i] += img[i] / static_cast<double>(frames);
            }
            std::vector<char> stuck(W * H, 0);
            for (int i : s.stuckPixels()) stuck[i] = 1;
            double m = 0, m2 = 0, n = 0;
            std::vector<double> col(W, 0.0), coln(W, 0.0);
            for (int i = 0; i < W * H; ++i) {
                if (stuck[i]) continue;
                m += mean[i]; m2 += mean[i] * mean[i]; n += 1;
                col[i % W] += mean[i]; coln[i % W] += 1;
            }
            pixelStd = std::sqrt(m2 / n - (m / n) * (m / n));
            double c = 0, c2 = 0;
            for (int x = 0; x < W; ++x) { const double v = col[x] / coln[x]; c += v; c2 += v * v; }
            columnStd = std::sqrt(c2 / W - (c / W) * (c / W));
        };
        double raw, rawCol, cal, calCol;
        spatial(raw, rawCol);
        s.nuc();
        spatial(cal, calCol);
        const SensorNoiseConfig& c = s.config();
        const double expect = std::sqrt(std::pow(c.fpnGainSigma * level, 2) + std::pow(c.fpnOffsetSigma, 2) +
                                        std::pow(c.columnSigma, 2));
        // Stuck pixels: 255 on an uncalibrated sensor (same seed = same detector), replaced after the NUC.
        bool isolated = true, allHot = true, replaced = true;
        std::vector<char> stuck(W * H, 0);
        for (int i : s.stuckPixels()) stuck[i] = 1;
        std::vector<uint8_t> probe(W * H, 30), raw0(W * H, 30);
        s.apply(probe, W, H);
        SensorModel fresh(12);
        fresh.apply(raw0, W, H);
        for (int i : s.stuckPixels()) {
            allHot = allHot && raw0[i] == 255;
            replaced = replaced && probe[i] < 40;
            const int x = i % W, y = i / W;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    if ((dx || dy) && x + dx >= 0 && y + dy >= 0 && x + dx < W && y + dy < H && stuck[(y + dy) * W + x + dx])
                        isolated = false;
        }
        const int wantStuck = static_cast<int>(std::lround(c.stuckFrac * W * H));
        record("T226", "Fixed-pattern noise (gain, offset, column stripes) before / after the NUC; stuck-hot pixels",
               fmt("raw spatial sigma %.2f DN +- 15 %%, after NUC <= 0.35 x raw, stripes visible raw, %d isolated stuck "
                   "pixels at 255 raw, replaced by the neighbour mean after the NUC", expect, wantStuck),
               fmt("raw %.3f DN (columns %.3f), after NUC %.3f DN (columns %.3f), %zu stuck, isolated %d, raw 255 %d, "
                   "replaced %d", raw, rawCol, cal, calCol, s.stuckPixels().size(), isolated, allHot, replaced),
               std::fabs(raw - expect) < 0.15 * expect && cal < 0.35 * raw && rawCol > 0.4 && calCol < 0.5 * rawCol &&
                   static_cast<int>(s.stuckPixels().size()) == wantStuck && isolated && allHot && replaced);
    }
    // T227: optics PSF = 3x3 binomial kernel: a point source spreads to [1 2 1; 2 4 2; 1 2 1] / 16, flat stays flat,
    // the rover's hot core survives the blur.
    {
        SensorNoiseConfig cfg = cleanConfig();
        cfg.netdDn = 0.0f;
        SensorModel s(13, cfg);
        std::vector<uint8_t> img(W * H, 0);
        img[100 * W + 160] = 255;
        s.apply(img, W, H);
        int sum = 0;
        for (uint8_t v : img) sum += v;
        const int c = img[100 * W + 160], e = img[100 * W + 161], k = img[101 * W + 161];
        std::vector<uint8_t> flat(W * H, 77);
        s.apply(flat, W, H);
        const bool flatOk = std::all_of(flat.begin(), flat.end(), [](uint8_t v) { return v == 77; });
        const std::vector<uint8_t> clean = makeSyntheticFrame(W, H, 5, true, 160, 120, false, 0, 0);
        std::vector<uint8_t> seen = clean;
        s.apply(seen, W, H);
        const int peak0 = *std::max_element(clean.begin(), clean.end()), peak1 = *std::max_element(seen.begin(), seen.end());
        record("T227", "Optics PSF (3x3 binomial): point source, flat field, rover core peak",
               "centre 64, edge 32, corner 16 (= 255 x 4/16, 2/16, 1/16), energy kept, flat field unchanged, core >= 230",
               fmt("centre %d, edge %d, corner %d, sum %d, flat %s, rover peak %d -> %d", c, e, k, sum,
                   flatOk ? "unchanged" : "changed", peak0, peak1),
               c == 64 && e == 32 && k == 16 && std::abs(sum - 255) <= 4 && flatOk && peak1 >= 230);
    }
    // T228 / T236: detection probability at NETD 2 DN (full sensor model, after NUC) and detectAll == detectTargets.
    {
        Pcg32 r(2280, 1);
        SensorModel s(228);
        s.nuc();
        const int frames = 300;
        int detected = 0, consistent = 0;
        double err2 = 0;
        for (int f = 0; f < frames; ++f) {
            float rx, ry, sx, sy;
            randomSpot(r, rx, ry);
            do randomSpot(r, sx, sy); while (std::hypot(sx - rx, sy - ry) < 25.0f);
            std::vector<uint8_t> img = makeSyntheticFrame(W, H, 1000 + f, true, rx, ry, f % 2 == 0, sx, sy);
            s.apply(img, W, H);
            CvFrameStats a, b;
            const std::vector<Measurement2D> all = cv::detectAll(img, W, H, CvConfig{}, &a);
            const std::vector<Measurement2D> tg = cv::detectTargets(img, W, H, CvConfig{}, &b);
            const std::vector<Measurement2D> fromAll = cv::blobsOfClass(all, BlobClass::Target);
            bool same = fromAll.size() == tg.size() && a.threshold == b.threshold && a.targets == b.targets &&
                        a.components == b.components && b.persons == 0;
            for (size_t i = 0; same && i < tg.size(); ++i) same = sameMeasurement(fromAll[i], tg[i]);
            consistent += same;
            float best = 1e9f;
            for (const Measurement2D& m : tg) best = std::min(best, std::hypot(m.cx - rx, m.cy - ry));
            if (best < 3.0f) {
                ++detected;
                err2 += best * best;
            }
        }
        const float pd = static_cast<float>(detected) / frames;
        const double rms = std::sqrt(err2 / std::max(1, detected));
        record("T228", "Rover detection probability under the full sensor model (NETD 2 DN, FPN after NUC, PSF, stuck px)",
               "P_d >= 0.97 over 300 random frames (target within 3 px of the true centre), centroid RMS < 1 px",
               fmt("P_d %.3f (%d / %d), centroid RMS %.3f px", pd, detected, frames, rms), pd >= 0.97f && rms < 1.0);
        record("T236", "detectAll: its Target blobs are exactly detectTargets' result (v5 filter unchanged)",
               "300 / 300 frames identical (count, order, every feature, threshold, components)",
               fmt("%d / %d frames identical", consistent, frames), consistent == frames);
    }
    // T229: false targets without a rover (sunlit patch, soldier, stuck pixels, noise): at most 1 per 1000 frames.
    {
        Pcg32 r(2290, 1);
        SensorModel s(229);
        s.nuc();
        const int frames = 1000;
        int falseTargets = 0, hotClutter = 0;
        for (int f = 0; f < frames; ++f) {
            float sx, sy;
            randomSpot(r, sx, sy);
            std::vector<uint8_t> img = makeSyntheticFrame(W, H, 5000 + f, false, 0, 0, f % 2 == 0, sx, sy);
            s.apply(img, W, H);
            CvFrameStats st;
            cv::detectAll(img, W, H, CvConfig{}, &st);
            falseTargets += st.targets;
            hotClutter += st.hotClutter;
        }
        record("T229", "False targets without a rover (sunlit patch 0.62, soldier, NETD 2 DN, FPN + stuck pixels after the NUC)",
               "<= 1 false target in 1000 frames",
               fmt("%d false targets, %d hot-clutter blobs in %d frames (NUC + bad-pixel map)", falseTargets,
                   hotClutter, frames),
               falseTargets <= 1);
    }
    // T230: a soldier (warm, no hot core, upright) is never a target, and is recognised as a person.
    {
        Pcg32 r(2300, 1);
        SensorModel s(230);
        s.nuc();
        const int frames = 400;
        int asTarget = 0, asPerson = 0, roverOk = 0;
        for (int f = 0; f < frames; ++f) {
            float rx, ry, sx, sy;
            randomSpot(r, sx, sy);
            do randomSpot(r, rx, ry); while (std::hypot(sx - rx, sy - ry) < 25.0f);
            std::vector<uint8_t> img = makeSyntheticFrame(W, H, 7000 + f, true, rx, ry, true, sx, sy);
            s.apply(img, W, H);
            bool t = false, p = false, rv = false;
            for (const Measurement2D& m : cv::detectAll(img, W, H)) {
                const float ds = std::hypot(m.cx - sx, m.cy - sy), dr = std::hypot(m.cx - rx, m.cy - ry);
                if (ds < 5.0f && m.cls == BlobClass::Target) t = true;
                if (ds < 5.0f && m.cls == BlobClass::Person) p = true;
                if (dr < 3.0f && m.cls == BlobClass::Target) rv = true;
            }
            asTarget += t; asPerson += p; roverOk += rv;
        }
        record("T230", "Persons are never targets: soldier blob (0.80-0.82, no core, upright) next to a rover",
               "soldier as target 0 / 400, as person >= 90 %, rover still a target >= 97 %",
               fmt("soldier: target %d, person %d / %d; rover target %d / %d", asTarget, asPerson, frames, roverOk, frames),
               asTarget == 0 && asPerson >= frames * 9 / 10 && roverOk >= frames * 97 / 100);
    }
    // T231: dropped frames (1 %) and tracker coasting: a confirmed track survives drops (even a burst of 3).
    {
        SensorModel s(231);
        int drops = 0;
        const int calls = 20000;
        for (int i = 0; i < calls; ++i) drops += s.dropFrame();
        SensorNoiseConfig stress;
        stress.dropProb = 0.2f;
        SensorModel d(232, stress);
        MultiTracker mt;
        int id = -1, idChanges = 0, lost = 0, dropped = 0;
        Vec3 truth;
        for (int k = 0; k < 300; ++k) {
            const float t = k / 30.0f;
            truth = Vec3{-5.0f + 1.2f * t, 0.15f, 2.0f};
            mt.predict(1.0f / 30.0f);
            const bool burst = k >= 150 && k < 153;
            std::vector<GroundMeasurement> ms;
            if (burst || d.dropFrame()) {
                ++dropped;
            } else {
                GroundMeasurement m;
                m.point = truth;
                m.rayOrigin = {0, 8, -6};
                m.rayDir = normalize(truth - m.rayOrigin);
                ms.push_back(m);
            }
            mt.update(ms, t);
            if (k < 10) continue;
            const Track* best = nullptr;
            for (const Track& tr : mt.tracks())
                if (tr.state != TrackState::Tentative) best = &tr;
            if (!best) { ++lost; continue; }
            if (id >= 0 && best->id != id) ++idChanges;
            id = best->id;
        }
        const float err = mt.tracks().empty() ? 99.0f : length(mt.tracks()[0].position() - truth);
        const float rate = static_cast<float>(drops) / calls;
        record("T231", "Dropped frames: 1 % drop rate; the tracker coasts through drops (20 % + a burst of 3)",
               "drop rate 1.0 +- 0.2 %, confirmed track never lost, no id change, error < 0.3 m",
               fmt("drop rate %.2f %%, %d / 300 frames dropped, lost %d, id changes %d, %zu tracks, error %.3f m",
                   100.0f * rate, dropped, lost, idChanges, mt.tracks().size(), err),
               std::fabs(rate - 0.01f) < 0.002f && lost == 0 && idChanges == 0 && mt.tracks().size() == 1 && err < 0.3f);
    }
    // T232: gimbal jitter statistics and its effect on the camera axis.
    {
        SensorModel s(233);
        double sy = 0, sy2 = 0, sp = 0, sp2 = 0, syp = 0, axis2 = 0;
        const int n = 20000;
        const Mat4 cam = translate({1, 6, 2}) * rotateY(radians(30.0f)) * rotateX(radians(40.0f));
        const Vec3 f0 = transformDirection(cam, {0, 0, 1});
        for (int i = 0; i < n; ++i) {
            float y, p;
            s.gimbalJitter(y, p);
            sy += y; sy2 += y * y; sp += p; sp2 += p * p; syp += y * p;
            if (i < 2000) {
                const Vec3 f1 = transformDirection(jitterCamera(cam, y, p), {0, 0, 1});
                const float a = degrees(std::acos(std::min(1.0f, dot(f0, f1))));
                axis2 += a * a;
            }
        }
        const double stdY = std::sqrt(sy2 / n - (sy / n) * (sy / n)), stdP = std::sqrt(sp2 / n - (sp / n) * (sp / n));
        const double corr = (syp / n - (sy / n) * (sp / n)) / (stdY * stdP);
        const double axisRms = std::sqrt(axis2 / 2000.0);
        const double pxRms = axisRms / (kCvFovYDeg / kCvHeight);
        record("T232", "Gimbal jitter: per-axis sigma 0.08 deg, independent axes; jittered camera axis moves accordingly",
               "sigma 0.08 +- 5 % on both axes, |corr| < 0.03, axis RMS = sqrt(2) x 0.08 deg +- 10 %",
               fmt("sigma yaw %.4f, pitch %.4f deg, corr %.4f, axis RMS %.4f deg = %.2f px at 40 deg / 240 px", stdY, stdP,
                   corr, axisRms, pxRms),
               std::fabs(stdY - 0.08) < 0.004 && std::fabs(stdP - 0.08) < 0.004 && std::fabs(corr) < 0.03 &&
                   std::fabs(axisRms - 0.08 * std::sqrt(2.0)) < 0.1 * 0.08 * std::sqrt(2.0));
    }
    // T233: seed reproducibility: same seed -> identical sensor output and detections; another seed differs.
    {
        const std::vector<uint8_t> f1 = makeSyntheticFrame(W, H, 42, true, 100, 90, true, 220, 150);
        const std::vector<uint8_t> f2 = makeSyntheticFrame(W, H, 42, true, 100, 90, true, 220, 150);
        const std::vector<uint8_t> f3 = makeSyntheticFrame(W, H, 43, true, 100, 90, true, 220, 150);
        SensorModel a(77), b(77), c(78);
        a.nuc(); b.nuc(); c.nuc();
        bool same = f1 == f2, detSame = true;
        int differ = 0;
        for (int k = 0; k < 5; ++k) {
            std::vector<uint8_t> ia = f1, ib = f1, ic = f1;
            a.apply(ia, W, H); b.apply(ib, W, H); c.apply(ic, W, H);
            float ja, pa, jb, pb, jc, pc;
            a.gimbalJitter(ja, pa); b.gimbalJitter(jb, pb); c.gimbalJitter(jc, pc);
            same = same && ia == ib && ja == jb && pa == pb && a.dropFrame() == b.dropFrame();
            c.dropFrame();
            for (int i = 0; i < W * H; ++i) differ += ia[i] != ic[i];
            const auto da = cv::detectAll(ia, W, H), db = cv::detectAll(ib, W, H);
            detSame = detSame && da.size() == db.size();
            for (size_t i = 0; detSame && i < da.size(); ++i) detSame = sameMeasurement(da[i], db[i]) && da[i].cls == db[i].cls;
        }
        const double frac = differ / (5.0 * W * H);
        record("T233", "Seed reproducibility: same seed -> identical frames, jitter, drops, detections; other seed differs",
               "seed 77 twice identical; seed 78 changes > 50 % of the pixels; synthetic frame 42 == 42 != 43",
               fmt("identical %d, detections identical %d, seed 78 differs in %.1f %% of pixels, frame 42 vs 43 %s", same,
                   detSame, 100.0 * frac, f1 == f3 ? "same" : "different"),
               same && detSame && frac > 0.5 && f1 != f3);
    }
    // T234: blind squad never fires: no vision is submitted and the v4 detection is empty -> no track, no round,
    // although rovers infiltrate (the squad has no access to the true rover positions).
    {
        Scene s;
        int launched = 0, maxAlive = 0;
        size_t maxTracks = 0;
        const Detection none;
        for (int k = 0; k < 120 * 30; ++k) {
            s.update(1.0f / 30.0f, none);
            for (const GameEvent& e : s.events()) launched += e.type == GameEventType::RoundLaunched;
            maxAlive = std::max(maxAlive, s.aliveIntruders());
            maxTracks = std::max(maxTracks, s.squad().tracker().tracks().size());
        }
        record("T234", "Blind squad never fires: 120 s without vision frames (empty detection), rovers infiltrating",
               "0 RoundLaunched events, 0 tracks, rovers alive at some point",
               fmt("%d rounds launched, max tracks %zu, max rovers alive %d, kills %d", launched, maxTracks, maxAlive,
                   s.kills()),
               launched == 0 && maxTracks == 0 && maxAlive > 0 && s.roundsLaunched() == 0);
    }
    // T235: clutter is classified, never a target: fire (too big), tracer (thin streak), warm horizontal pipe.
    {
        SensorModel s(235);
        s.nuc();
        int targets = 0, hot = 0, clutter = 0, persons = 0, pipeOk = 0;
        const int frames = 50;
        for (int f = 0; f < frames; ++f) {
            std::vector<uint8_t> img = makeSyntheticFrame(W, H, 9000 + f, false, 0, 0, false, 0, 0);
            auto put = [&](int x, int y, float v) {
                if (x >= 0 && y >= 0 && x < W && y < H)
                    img[y * W + x] = std::max(img[y * W + x], static_cast<uint8_t>(std::lround(v * 255.0f)));
            };
            for (int y = 0; y < H; ++y)  // fire: 33 px radius at 0.93, 1.0 in the middle
                for (int x = 0; x < W; ++x) {
                    const float r = std::hypot(x - 250.0f, y - 170.0f);
                    if (r < 33.0f) put(x, y, r < 10.0f ? 1.0f : 0.93f);
                }
            for (int i = 0; i < 80; ++i)  // tracer: 80 px streak, 3 px wide (the PSF thins it), 0.98
                for (int w = -1; w <= 1; ++w) put(20 + i, 30 + i / 5 + w, 0.98f);
            for (int y = 120; y < 124; ++y)  // warm pipe: 40 x 4 px, 0.83, no hot core, horizontal
                for (int x = 40; x < 80; ++x) put(x, y, 0.83f);
            s.apply(img, W, H);
            CvFrameStats st;
            for (const Measurement2D& m : cv::detectAll(img, W, H, CvConfig{}, &st))
                if (std::fabs(m.cy - 121.5f) < 3.0f && m.cx > 40 && m.cx < 80 && m.cls == BlobClass::Clutter) ++pipeOk;
            targets += st.targets; hot += st.hotClutter; clutter += st.clutter; persons += st.persons;
        }
        record("T235", "Clutter classes: fire (too big) and tracer (thin) = hot clutter, horizontal warm pipe = clutter",
               "0 targets, 0 persons, fire + tracer hot clutter and the pipe clutter in every frame (50 noisy frames)",
               fmt("targets %d, persons %d, hot clutter %d, clutter %d, pipe as clutter %d / %d", targets, persons, hot,
                   clutter, pipeOk, frames),
               targets == 0 && persons == 0 && hot >= 2 * frames && pipeOk == frames);
    }
}
