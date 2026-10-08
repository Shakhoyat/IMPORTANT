#include "vision/CvPipeline.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace ts::cv {

uint8_t otsuThreshold(const std::vector<uint8_t>& image) {
    double hist[256] = {};
    for (uint8_t p : image) hist[p] += 1.0;
    const double total = static_cast<double>(image.size());
    if (total <= 0.0) return 0;
    double sumAll = 0.0;
    for (int i = 0; i < 256; ++i) sumAll += i * hist[i];
    double wB = 0.0, sumB = 0.0, best = -1.0;
    int bestT = 0;
    for (int t = 0; t < 256; ++t) {
        wB += hist[t];
        if (wB == 0.0) continue;
        const double wF = total - wB;
        if (wF == 0.0) break;
        sumB += t * hist[t];
        const double mB = sumB / wB, mF = (sumAll - sumB) / wF;
        const double between = wB * wF * (mB - mF) * (mB - mF);
        if (between > best) {
            best = between;
            bestT = t;
        }
    }
    return static_cast<uint8_t>(bestT);
}

int despeckle(std::vector<uint8_t>& mask, int w, int h, int minNeighbours) {
    std::vector<int> remove;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!mask[y * w + x]) continue;
            int n = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    const int xx = x + dx, yy = y + dy;
                    if (xx >= 0 && yy >= 0 && xx < w && yy < h && mask[yy * w + xx]) ++n;
                }
            }
            if (n < minNeighbours) remove.push_back(y * w + x);
        }
    }
    for (int i : remove) mask[i] = 0;
    return static_cast<int>(remove.size());
}

namespace {

int findRoot(std::vector<int>& parent, int a) {
    while (parent[a] != a) {
        parent[a] = parent[parent[a]];  // path halving
        a = parent[a];
    }
    return a;
}

}  // namespace

int labelComponents(const std::vector<uint8_t>& mask, int w, int h, std::vector<int>& labels) {
    labels.assign(static_cast<size_t>(w) * h, 0);
    std::vector<int> parent{0};
    // Pass 1: provisional labels from the already visited neighbours (W, NW, N, NE); record equivalences.
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!mask[y * w + x]) continue;
            int best = 0;
            const int nb[4][2] = {{-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
            int found[4], nf = 0;
            for (const auto& d : nb) {
                const int xx = x + d[0], yy = y + d[1];
                if (xx < 0 || yy < 0 || xx >= w) continue;
                const int l = labels[yy * w + xx];
                if (l) found[nf++] = l;
            }
            if (nf == 0) {
                best = static_cast<int>(parent.size());
                parent.push_back(best);
            } else {
                best = findRoot(parent, found[0]);
                for (int k = 1; k < nf; ++k) {  // union: the larger root points to the smaller one
                    const int r = findRoot(parent, found[k]);
                    if (r == best) continue;
                    parent[std::max(r, best)] = std::min(r, best);
                    best = std::min(r, best);
                }
            }
            labels[y * w + x] = best;
        }
    }
    // Pass 2: replace every label by its root and renumber 1..count.
    std::vector<int> compact(parent.size(), 0);
    int count = 0;
    for (size_t i = 1; i < parent.size(); ++i) {
        const int r = findRoot(parent, static_cast<int>(i));
        if (!compact[r]) compact[r] = ++count;
        compact[i] = compact[r];
    }
    for (int& l : labels)
        if (l) l = compact[l];
    return count;
}

namespace {

// Stages 1-4 for every component (label order), the threshold statistics in 'st'.
std::vector<Measurement2D> measureAll(const std::vector<uint8_t>& image, int w, int h, const CvConfig& cfg,
                                      CvFrameStats& st) {
    st.otsu = otsuThreshold(image);
    // Otsu's t is the last level of the dark class: hot pixels are those > t.
    st.threshold = std::max<uint8_t>(cfg.floor, static_cast<uint8_t>(std::min(255, st.otsu + 1)));
    std::vector<uint8_t> mask(image.size());
    for (size_t i = 0; i < image.size(); ++i) {
        mask[i] = image[i] >= st.threshold ? 1 : 0;
        st.hotPixels += mask[i];
    }
    if (cfg.despeckle) st.removedSpeckles = despeckle(mask, w, h);

    std::vector<int> labels;
    st.components = labelComponents(mask, w, h, labels);
    struct Acc {
        double n = 0, sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, heat = 0;
        int minX = 1 << 30, minY = 1 << 30, maxX = -1, maxY = -1;
        uint8_t peak = 0;
    };
    std::vector<Acc> acc(static_cast<size_t>(st.components) + 1);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int l = labels[y * w + x];
            if (!l) continue;
            Acc& a = acc[l];
            const uint8_t p = image[y * w + x];
            a.n += 1;
            a.sx += x; a.sy += y;
            a.sxx += static_cast<double>(x) * x; a.syy += static_cast<double>(y) * y; a.sxy += static_cast<double>(x) * y;
            a.heat += p / 255.0;
            a.peak = std::max(a.peak, p);
            a.minX = std::min(a.minX, x); a.maxX = std::max(a.maxX, x);
            a.minY = std::min(a.minY, y); a.maxY = std::max(a.maxY, y);
        }
    }
    std::vector<Measurement2D> out;
    for (int l = 1; l <= st.components; ++l) {
        const Acc& a = acc[l];
        Measurement2D m;
        m.area = static_cast<int>(a.n);
        m.cx = static_cast<float>(a.sx / a.n);
        m.cy = static_cast<float>(a.sy / a.n);
        // Second central moments -> principal axes (eigenvalues of the 2x2 covariance).
        const double mu20 = a.sxx / a.n - m.cx * m.cx, mu02 = a.syy / a.n - m.cy * m.cy, mu11 = a.sxy / a.n - m.cx * m.cy;
        const double tr = mu20 + mu02, det = mu20 * mu02 - mu11 * mu11;
        const double disc = std::sqrt(std::max(0.0, tr * tr / 4.0 - det));
        const double l1 = tr / 2.0 + disc, l2 = std::max(tr / 2.0 - disc, 1.0 / 12.0);  // 1/12: a pixel's own spread
        m.orientationDeg = static_cast<float>(0.5 * std::atan2(2.0 * mu11, mu20 - mu02) * 57.29578);
        m.elongation = static_cast<float>(std::sqrt(std::max(l1, 1.0 / 12.0) / l2));
        m.meanHeat = static_cast<float>(a.heat / a.n);
        m.peak = a.peak;
        m.minX = a.minX; m.minY = a.minY; m.maxX = a.maxX; m.maxY = a.maxY;
        m.u = (m.cx + 0.5f) / static_cast<float>(w) * 2.0f - 1.0f;
        m.v = 1.0f - (m.cy + 0.5f) / static_cast<float>(h) * 2.0f;
        out.push_back(m);
    }
    return out;
}

// Stage 5, the v5 target filter.
bool isTarget(const Measurement2D& m, const CvConfig& cfg) {
    return m.area >= cfg.minArea && m.area <= cfg.maxArea && m.peak >= cfg.core &&
           (m.area < 6 || m.elongation <= cfg.maxElongation);
}

}  // namespace

std::vector<Measurement2D> detectTargets(const std::vector<uint8_t>& image, int w, int h, const CvConfig& cfg,
                                         CvFrameStats* statsOut) {
    CvFrameStats st;
    std::vector<Measurement2D> out;
    for (const Measurement2D& m : measureAll(image, w, h, cfg, st))
        if (isTarget(m, cfg)) out.push_back(m);
    st.targets = static_cast<int>(out.size());
    if (statsOut) *statsOut = st;
    return out;
}

BlobClass classifyBlob(const Measurement2D& m, const CvConfig& cfg) {
    if (isTarget(m, cfg)) return BlobClass::Target;
    if (m.peak >= cfg.core) return BlobClass::HotClutter;  // engine-hot but a fire, a tracer streak, a glint
    // Warm without a hot core: a person if it has a person's size and stands upright (major axis steep in the
    // image), or is compact (seen from above). Lying / horizontal warm shapes (walls, pipes) are clutter.
    const bool sized = m.area >= cfg.personMinArea && m.area <= cfg.personMaxArea && m.elongation <= cfg.personMaxElongation;
    const bool upright = std::fabs(m.orientationDeg) >= cfg.personMinTiltDeg || m.elongation < cfg.personCompact;
    return sized && upright ? BlobClass::Person : BlobClass::Clutter;
}

std::vector<Measurement2D> detectAll(const std::vector<uint8_t>& image, int w, int h, const CvConfig& cfg,
                                     CvFrameStats* statsOut) {
    CvFrameStats st;
    std::vector<Measurement2D> out = measureAll(image, w, h, cfg, st);
    for (Measurement2D& m : out) {
        m.cls = classifyBlob(m, cfg);
        switch (m.cls) {
            case BlobClass::Target: ++st.targets; break;
            case BlobClass::Person: ++st.persons; break;
            case BlobClass::HotClutter: ++st.hotClutter; break;
            case BlobClass::Clutter: ++st.clutter; break;
        }
    }
    if (statsOut) *statsOut = st;
    return out;
}

std::vector<Measurement2D> blobsOfClass(const std::vector<Measurement2D>& blobs, BlobClass c) {
    std::vector<Measurement2D> out;
    for (const Measurement2D& m : blobs)
        if (m.cls == c) out.push_back(m);
    return out;
}

CvDebug detectTargetsDebug(const std::vector<uint8_t>& image, int w, int h, const CvConfig& cfg) {
    CvDebug d;
    d.targets = detectTargets(image, w, h, cfg, &d.stats);  // the real result and statistics
    // The same first stages again, keeping the intermediate images (identical functions, identical thresholds).
    d.mask.assign(image.size(), 0);
    for (size_t i = 0; i < image.size(); ++i) d.mask[i] = image[i] >= d.stats.threshold ? 1 : 0;
    d.clean = d.mask;
    if (cfg.despeckle) despeckle(d.clean, w, h);
    d.removed.assign(image.size(), 0);
    for (size_t i = 0; i < image.size(); ++i) d.removed[i] = d.mask[i] && !d.clean[i] ? 1 : 0;
    const int n = labelComponents(d.clean, w, h, d.labels);
    d.components.assign(static_cast<size_t>(n), CvComponent{});
    for (int l = 1; l <= n; ++l) {
        CvComponent& c = d.components[l - 1];
        c.label = l;
        c.minX = c.minY = 1 << 30;
        c.maxX = c.maxY = -1;
    }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int l = d.labels[static_cast<size_t>(y) * w + x];
            if (!l) continue;
            CvComponent& c = d.components[l - 1];
            ++c.area;
            c.minX = std::min(c.minX, x); c.maxX = std::max(c.maxX, x);
            c.minY = std::min(c.minY, y); c.maxY = std::max(c.maxY, y);
        }
    for (CvComponent& c : d.components)
        for (const Measurement2D& m : d.targets)
            if (m.minX == c.minX && m.maxX == c.maxX && m.minY == c.minY && m.maxY == c.maxY && m.area == c.area) c.accepted = true;
    return d;
}

}  // namespace ts::cv
