// "Lab" figures for the report (docs/v5_notes/v5-f2.md): technique explanations rendered BY THE PROGRAM -
// the computer-vision pipeline on a real thermal frame, the PBR material grid, flat / Gouraud / Phong shading,
// surface relief on / off, screen-space reflections, and the internal render-pipeline buffers.
// Driven by Capture.cpp (shots named lab_*): ./ts --headless --capture DIR --only lab_materials,lab_shading,...
#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "rendering/Renderer.h"
#include "scene/Scene.h"
#include "vision/CvPipeline.h"
#include "vision/SensorModel.h"

namespace ts::lab {

struct Ctx {
    Scene* scene = nullptr;
    Renderer* renderer = nullptr;
    std::string dir;                 // output directory (report/figures_lab)
    Quality quality = Quality::Ultra;
    bool qualityForced = false;      // --quality given: used everywhere; otherwise stage figures use HIGH
    std::ostream* log = nullptr;
};

struct TrackInfo {
    int id = 0;
    int state = 0;                   // TrackState
    Vec3 pos, vel;
    float sigma = 0.0f, residual = 0.0f;
    bool triangulated = false;
    unsigned seenNow = 0;
    float nearestTruth = -1.0f;      // distance to the nearest intruder rover (ground truth), m
};

// One UAV's thermal frame with its CV debug images and the shared track picture of that instant.
struct CvView {
    int uav = 0;
    float time = 0.0f;
    std::vector<uint8_t> img;        // 320 x 240 raw heat image
    Mat4 camera;                     // gimbal camera world matrix of this frame
    Vec3 camPos;
    cv::CvDebug dbg;
    std::vector<TrackInfo> tracks;
    int bestTrack(const Measurement2D& m, Vec3* ground, float* range) const;  // nearest track to the blob's ground point
};
CvView makeCvView(const Scene& scene, int uav, const std::vector<uint8_t>& img, float time);

bool cvFrame(Ctx& c, const CvView views[2]);                     // panels a-f + montage + cv_frame.json
bool cvStrip(Ctx& c, const std::vector<CvView>& frames, int uav);  // 6-frame strip
// v6: one UAV frame through the ideal sensor and through the sensor model (noise, fixed pattern, PSF, stuck pixels),
// both classified by cv::detectAll. Writes lab_cv_noise.png (a clean, b noisy, c difference x8, d noisy + classes)
// and cv_noise.json.
struct NoiseFrame {
    int uav = 0;
    float time = 0.0f;
    std::vector<uint8_t> clean, noisy;   // 320 x 240
    std::vector<Measurement2D> cleanBlobs, noisyBlobs;
    SensorNoiseConfig cfg;
    uint32_t seed = 0;
};
bool cvNoise(Ctx& c, const NoiseFrame& f);
bool materials(Ctx& c);
bool shading(Ctx& c);
bool relief(Ctx& c);
bool reflections(Ctx& c);
bool buffers(Ctx& c);
bool writeCaptions(Ctx& c);

// v7 WP-I Frame Explorer figures (rendering/FrameExplorer): the frozen frame explained stage by stage.
// Writes explorer_stage_01_graph.png .. explorer_stage_16_final.png (full UI, 1280x720 unless sized), the clean
// montage explorer_montage.png (4 x 4), extras (explorer_raster_scan, explorer_interp_gouraud_phong,
// explorer_inspector, explorer_wipe, explorer_imagelab_sobel, explorer_imagelab_gauss_gamma, explorer_reflection_mask)
// and explorer_stages.json (titles, captions, formulas, probe numbers). cam = nullptr: a fixed view over the camp.
struct ExplorerShot {
    const CameraView* cam = nullptr;
    int width = 1280, height = 720;
    float wetness = 0.7f;            // ground wetness for the frame (reflections need wet ground); < 0 = keep the scene's
    float hour = 10.5f;              // time of day of the frozen frame (day reads best in the report); < 0 = keep
    bool nightMontage = true;        // also <prefix>_montage_night.png from the same camera at 23:00
    std::string prefix = "explorer";
};
bool explorer(Ctx& c, const ExplorerShot& shot = {});

}  // namespace ts::lab
