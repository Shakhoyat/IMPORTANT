// Helpers shared by the report capture modes (app/CaptureParts.cpp: parts / build / prims; app/CaptureShapes.cpp:
// shapeview / lineup / tile): one render path through the real Renderer, PNG output, camera helpers.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app/CaptureParts.h"
#include "primitives/MeshData.h"
#include "report/ShapeView.h"
#include "scene/Camera.h"

namespace ts::partscap {

using Image = shape::Image;   // RGB, rows top to bottom

namespace detail {

constexpr int kFramesShared = 8;   // TAA accumulation of a frozen frame

std::string num(float v);
std::string esc(const std::string& s);
void logLine(Ctx& c, const std::string& s);
RenderSettings baseSettings(const Ctx& c, const std::string& isolate);
Image renderImage(Ctx& c, const CameraView& cam, const RenderSettings& s, int w, int h);
bool save(Ctx& c, const std::string& path, const Image& img);
CameraView viewFrom(const Vec3& target, const Vec3& dir, float dist, float fovDeg, float aspect);
Vec3 dirFrom(float yawDeg, float pitchDeg);
float sunnyYaw(const Ctx& c, float front, float yaw);
void warmUp(Ctx& c);
void overlayWire(Image& img, const MeshData& mesh, const Mat4& mvp);
const SceneNode* findRig(const Scene& scene, const std::string& object);
void meshSphere(const MeshData& mesh, const Mat4& m, Vec3& centre, float& radius);

}  // namespace detail

}  // namespace ts::partscap
