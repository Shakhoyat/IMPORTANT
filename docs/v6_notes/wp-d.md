# WP-D notes (sensor noise + real CV) - DONE

## Files
- src/vision/SensorModel.{h,cpp}: PCG32 (struct Pcg32 in the header) + Box-Muller; y = Q(g(.)(PSF*x) + o + c + n).
  Two streams per sensor: fixed pattern (seed, stream 0x5eedf1d0, drawn once per image size) and temporal
  (noise, jitter, drops). 3x3 binomial PSF (separable). Stuck-hot pixels isolated (2e-4 -> 15 px on 320x240).
  New sensor = uncalibrated (full FPN, stuck = 255); nuc() -> FPN x nucResidual (0.25) + bad-pixel replacement
  (mean of 4 neighbours). jitterCamera(cam, yaw, pitch) = cam * Ry * Rx. makeSyntheticFrame(..., roverScale = 1).
- src/vision/CvPipeline.{h,cpp}: detectTargets refactored into measureAll + isTarget (results bit-identical, T236);
  BlobClass {Target, Person, HotClutter, Clutter}, Measurement2D::cls, detectAll(image,w,h,cfg={},stats=nullptr),
  classifyBlob, blobsOfClass, blobClassName (inline). CvConfig: new person* fields appended (old defaults unchanged).
  CvFrameStats += persons, hotClutter, clutter.
  Person rule: no hot core (peak < 230), area 4..600, elongation <= 5, major axis >= 45 deg from x (upright) or
  elongation < 1.25 (compact). HotClutter: peak >= 230 but failed the target filter. Clutter: everything else.
- MultiTracker: unchanged (confirmed tracks coast 0.25 s, delete at 3 s: drops of 1-3 frames are harmless, T231).
- tools/figdata_main.cpp: dumpCvNoise (`figdata build/figdata_out cvnoise`, 25 s) -> build/figdata_out/cv_noise.json.
- tests/test_sensor.cpp: T225-T236 (12 checks, 8.3 s, all pass).

## API for WP-E (App::step), per UAV u
    SensorModel sensor_[kUavCount];   // construct: SensorModel(deriveSeed(runSeed, 100 + u)); then sensor_[u].nuc();
    if (sensor_[u].dropFrame()) { /* no submitVision for u this frame: tracker coasts */ }
    else {
      float jy, jp; sensor_[u].gimbalJitter(jy, jp);
      const Mat4 cam = scene_->cameraWorld(u), jit = jitterCamera(cam, jy, jp);
      renderer_->captureUavCamera(*scene_, img, stats, u, &jit);   // render with the jittered pose
      sensor_[u].apply(img, kCvWidth, kCvHeight);
      auto blobs = cv::detectAll(img, kCvWidth, kCvHeight, CvConfig{}, &cvFrame_[u]);
      scene_->submitVision(u, cv::blobsOfClass(blobs, BlobClass::Target), cam);   // reported (unjittered) pose
      // persons for the HUD "PERS" brackets: cv::blobsOfClass(blobs, BlobClass::Person)
    }
  Call order matters for reproducibility (drop -> jitter -> apply each frame). Seed 0 legacy: any fixed seed.

## Sweep (500 frames per point, after NUC, full FPN / PSF / stuck model)
| NETD DN | P_d | P_d far (half size) | false tgt/frame | person->target | person recognised | centroid RMS px |
| 0 | 1.000 | 1.000 | 0     | 0     | 1.000 | 0.140 |
| 1 | 1.000 | 0.996 | 0     | 0     | 1.000 | 0.140 |
| 2 | 1.000 | 0.990 | 0     | 0     | 1.000 | 0.142 |
| 4 | 1.000 | 0.964 | 0     | 0     | 0.998 | 0.153 |
| 8 | 1.000 | 0.938 | 0.030 | 0.054 | 0.944 | 0.180 |
No NUC at NETD 2: false 0.006/frame, person->target 0.008 (stuck pixel on a soldier = "hot core").
Jitter 0.08 deg/axis = 0.48 px per axis (0.67 px axis RMS) at 40 deg / 240 px.

## Open / for others
- Workbook/report: cv_noise.json has netd/pd/pdFar/falsePerFrame/personAsTarget/personRecognised/centroidRms,
  noNuc row, sensor config (incl. jitterPx) and 5 example crops with classified blobs.
- Full tree build was blocked during my run by in-progress WP-B (Spline.cpp) / WP-E (test_app.cpp) edits; my
  binary linked manually: 162/162 (test_app objects missing), Sensor group 12/12.
