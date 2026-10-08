# WP-L game audio (agent L) - notes

Owner: src/audio/*, tests/test_audio.cpp (T300-T309), TestKit.h v7 line for T300-T309. Status: **done**, 268/268 in
build/tests-wpl when this was written (T247/T250/T264/T268/T272/T276 failed briefly during G/H in-progress edits; they
are not audio tests). -Wall -Wextra clean. Mix cost: 10 s of night-scene audio in 0.79 s (about 8 % of one core).

## Survey (what existed)
- `GameEvents.h` events -> `AudioEngine::handleEvent` -> procedural clips (Synth.cpp); continuous loops for UAVs,
  helicopter, tank and rounds in flight; one ambient night bed; equal-power pan, 1/r, air low-pass, light Doppler,
  tanh limiter. App: `audio_.update(*scene_, cam.position, fwd, up, dt)` per frame + `pollEvents` per sim step.
- `--record` (Record.cpp, CAP): its own `AudioEngine`, `update()` per video frame, `renderOffline(1/fps)` -> WavWriter
  -> ffmpeg `-c:a aac -b:a 192k` mux. It gets every v7 feature automatically through `update()`; nothing to change.

## What v7 adds (files)
| File | Content |
|------|---------|
| `Acoustics.{h,cpp}` (new) | `bunkerAmount(p)` (1 inside the rooms, ramp trench 0.5 -> 0), `surfaceAt(p)` (dirt / gravel / concrete / metal / grass from BaseLayout + terrain), `echoTaps()` (image source off the HESCO wall r = 34 m and the treeline r = 72 m, sqrt spreading for legibility), `propagationDelay(d)` = (d - 20) / 343 s, cap 1 s |
| `Spatial.{h,cpp}` | HRTF-lite `computeHrtf()`: Woodworth ITD (max 0.66 ms), softened equal-power ILD (~11 dB at 90 deg), far-ear head-shadow low-pass (20 kHz -> 1.8 kHz), rear pinna low-pass (6 kHz behind), air absorption, physical Doppler (clamp 0.8..1.25). Legacy `computeSpatial` / 4-arg `dopplerRatio` unchanged |
| `Mixing.{h,cpp}` (new) | `RoomReverb` (Freeverb-style, 6 combs + 3 all-passes per channel, 12 ms pre-delay) gliding open-air (RT ~0.35 s, dark) <-> bunker (RT ~1.4 s, bright concrete); `PeakLimiter` (1 ms look-ahead, 0.75 threshold, 150 ms release, soft-knee clip 0.6 -> 0.88 ceiling) |
| `Ambience.{h,cpp}` (new, AmbientGen moved out of Synth) | time of day: koel / myna / sparrow / warbler calls (dawn chorus ~06:00), cicada swells at midday; crickets, owl, tokay gecko at night; wind gustier by day (now low-passed at 1.1 kHz), generator louder at night; underground: bed muffled + ventilation rush + 100 Hz hum |
| `Synth.{h,cpp}` | richer UAV (independent rotor throttle wander, motor whine, blade-chopped noise, downwash), helicopter (4-blade slap pattern, tail rotor, two turbine tones, jet noise), tank (lumpy V12 firing, turbo, track link clanks through metal resonances, rumble, squeal) |
| `SynthVehicles.cpp` (new) | `RoverWhineGen` (electric whine 180 + 420 x speed Hz, gear mesh, PWM hiss, tyre crunch, steering servo), `GeneratorGen` (bunker diesel 75 Hz firing, 50 Hz alternator, fan, rattle) |
| `SynthFoley.cpp` (new) | `synthFootstep(surface, take)` heel + toe, `synthRadioChatter(seed)` (squelch, formant "voice" in a 300-3000 Hz overdriven channel, roger beep), `synthUiCue(UiCue)` (9 cues) |
| `AudioEngine.{h,cpp}` | 32 voices with priorities (footsteps < events / loops < UI); per-voice HRTF (ITD delay line + shadow / rear / occlusion filters), sound propagation delay, echo taps on explosions / cannon / gatling, reverb send (wetter with distance, full in the bunker), bunker occlusion (listener and source on different sides of the roof: -12 dB and 350 Hz low-pass), ducking (blasts duck bed + loops up to -9 dB, 0.12 s hold, ~1 s release; UI never ducked), look-ahead limiter; rovers, bunker generator and soldiers' footsteps (gait plant points, all floors incl. underground) from `update()`; radio chatter on WaveStart / EnemySpotted / CombinedStrike / EnemyDestroyed / Breach / Dusk / NightFalls / Dawn / DayBreak / ShiftChange (one transmission at a time); EnemyHit -> hit-marker cue; Breach -> alert cue |

Defaults keep every v6 test unchanged (T160-T169 pass unmodified). `AudioEngine()` == `AudioEngine(99)`.

## API for agent G (camera rig / POV / possession)
```cpp
#include "audio/AudioEngine.h"   // already included by App.h
namespace audio = ts::audio;

struct audio::ListenerState {
    Vec3 position, forward, up;   // camera eye + basis (RigPose::eye / forward() / up())
    Vec3 velocity;                // possessed object's / camera's velocity, m/s
    bool hasVelocity = false;     // false: derived from position (jumps > 40 m/s are treated as cuts)
    float bunker = -1.0f;         // < 0 auto from position (recommended); 0 / 1 force the bunker reverb zone
};
void update(const Scene&, const ListenerState&, float dt);   // replaces update(scene, pos, fwd, up, dt)
void setListener(const ListenerState&, float dt);
void cameraCut();                                            // next listener update is a teleport (no Doppler)
void uiCue(audio::UiCue c, float gain = 1.0f);               // 2D UI bus, never ducked / reverberated
//   UiCue::PovJump, PickerOpen, PickerClose, PickerMove, Possess, Release, HitMarker, Alert, FreeCam
//   PovJump / FreeCam / Possess / Release also imply cameraCut().
void footstep(const Vec3& feet, float intensity = 1.0f);     // surface from surfaceAt(); or footstep(p, Surface, i)
void radioChatter(int variant = -1);
float listenerBunker() const;  float duckGain() const;       // e.g. for a HUD "muffled" vignette / shake sync
```
Soldier footsteps (also the possessed soldier's) are already produced inside `update()` from `Soldier::planted()` /
`plantPoint()`; G does not need to call `footstep()` for soldiers. Rovers, UAVs, helicopter, tank, rounds and the
generator are automatic too.

### Exact wiring lines (G or the supervisor; App.cpp is G's)
In `App::renderFrame`, replace `audio_.update(*scene_, cam.position, fwd, up, frameDt);` with:
```cpp
        audio::ListenerState ls;
        ls.position = cam.position;
        ls.forward = fwd;
        ls.up = up;
        // when the rig follows a possessed / POV object, give its velocity (clean Doppler during blends):
        // ls.velocity = <object velocity>; ls.hasVelocity = true;
        audio_.update(*scene_, ls, frameDt);
```
UI cues (only where audioLive_; the engine is cheap to call anyway):
```cpp
audio_.uiCue(audio::UiCue::PickerOpen);   // F4 opens the POV picker
audio_.uiCue(audio::UiCue::PickerClose);  // F4 closes it / Esc
audio_.uiCue(audio::UiCue::PickerMove);   // arrow up / down in the picker
audio_.uiCue(audio::UiCue::PovJump);      // a jump starts (Enter in picker, Insert / Delete, middle-click pick, End)
audio_.uiCue(audio::UiCue::FreeCam);      // Home: free camera
audio_.uiCue(audio::UiCue::Possess);      // control taken of the object (after the blend lands, or on keys 5-0 / F3)
audio_.uiCue(audio::UiCue::Release);      // Backspace / leaving possession
audio_.uiCue(audio::UiCue::HitMarker);    // the player's own round hits (EnemyHit already plays one at 0.6)
audio_.uiCue(audio::UiCue::Alert);        // damage / designation vignette starts (rover being designated)
```
Recorder (CAP, optional): Record.cpp may call `audioEngine.update(*scene_, ls, dt)` with the director's camera; the
old signature keeps working and is equivalent with auto bunker / derived velocity.

## Verification (offline, no device)
`./build/tests-wpl` -> T300-T309 (all PASS):
- T300 19 v7 synths deterministic / finite / audible; T301 footstep spectral centroids (dirt 409, gravel 4761,
  concrete 1366, metal 1280, grass 3195 Hz) + surface map; T302 ITD 31.5 samples (0.656 ms), rendered lag 35 samples
  (ITD + shadow group delay), ILD 11 dB; T303 Doppler on a rendered rover pass at 25 m/s: 646 / 560 Hz (theory 647 /
  560); T304 battle peak 0.82 (<= 0.88), limiter engaged, battle -12 dBFS RMS, night bed -32 dBFS; T305 night
  cricket/bird ratio 14 dB vs day 1.4 dB, birds +11.9 dB by day, underground -31 dB in 2-6 kHz; T306 duck 0.35 near,
  0.90 far, recovered after 4 s; T307 bunker tail +13 dB vs open, outside cannon muffled (HF/LF -16 dB, level -12.6 dB);
  T308 echo taps exact (0.163 s wall, 0.385 s treeline), causal, blast at 120 m arrives 0.293 s late; T309 full scene
  (dusk) bit-identical with seed 7, differs with seed 8, 20 footsteps + radio, UI cues centred.
- Listening samples: `TS_AUDIO_SAMPLES=build/audio_v7 ./build/tests-wpl` writes build/audio_v7/v7_*.wav
  (ambience dawn/noon/night/bunker, battle with ducking + echoes, bunker vs open reverb, cannon echoes, footsteps per
  surface, helicopter / UAV fly-by, tank drive-by, rover Doppler, dusk scene with UI cues, UI cues + radio).

## Follow-up (after WP-G landed): small arms + possessed vehicle
- `AudioEngine::gunshot(muzzle)`: possessed soldier's rifle (`synthRifleShot`, 4 takes: N-wave crack, muzzle blast,
  low boom, bolt clicks), spatial + echo taps, light duck (0.25). Wired in `AppGame.cpp` `soldierWeapon()` after
  `fireTracer` (`audio_.gunshot(muzzle);`).
- Impact ticks: Explosion events with value <= 20 (tracer / bullet hits, Combat power 0.15) now play
  `synthImpact(surfaceAt(pos), take)` (dirt / gravel / concrete / metal ping / grass, ricochet whine on hard
  surfaces) instead of a mini explosion; no ducking. This also applies to the AA gun's tracer hits.
  `AudioEngine::impact(pos)` is public too.
- `ListenerState::selfControllable / selfRover / selfInterior / selfThrottle` and
  `setSource(..., load, interior)`: the possessed vehicle's engine follows the throttle (`ContinuousGen::setLoad`:
  tank +600 rpm, rover drive whine spins up ahead of the wheels with strained harmonics); first person inside it the
  own engine is centred, Doppler-free and goes through a hull / cockpit low-pass (tank 650 Hz, helicopter 1.5 kHz,
  rover 1.8 kHz) plus ~+5.6 dB of rumble below 150 Hz. Wired in `App.cpp` `renderFrame()` (possessed_ with
  possessSight and no alt view; possessedRover_ with a non-chase view; throttle = held_.forward).
- Test T309b (all PASS): rifle peak 0.50, echo contribution present, deterministic; tick 0.08 s, no duck (a power-1
  blast ducks to 0.35); impact centroid metal 4362 > dirt 2702 Hz; tank HF/LF tilt inside -24.5 vs outside -13.9 dB,
  rumble +6 dB; rover at standstill 180 -> 1105 Hz whine and +8 dB at full throttle. Suite 289/289.
- Sample: build/audio_v7/v7_rifle_first_person.wav.

## Open issues / ideas
- Echo taps use two analytic reflectors (circle wall + treeline), no per-building reflections or occlusion by
  buildings; tap delays are fixed at the start of a clip.
- Bunker blast doors are not modelled acoustically (the zone is geometric; an open door would leak more).
- Possessed UAVs: the drone loop is keyed by squad index, not controllable index, so no interior filter for a
  possessed UAV (its gimbal view is outside the airframe anyway).
- App wiring (ListenerState with velocity, UI cues) is G's / supervisor's; until then the old update() path gives
  all v7 sound except the UI cues and explicit listener velocity.
