#!/usr/bin/env bash
# v10 capture harness: every figure the report uses, rendered by the program itself into report/figures/raw/<group>/
# (1920x1080, EPIC, seed 1, RTX 4070 via WSL d3d12). Groups run as separate processes (play_* / lab_* / v6 shot lists
# must not be mixed in one --only). Usage: tools/v10/capture_all.sh [group ...]   (default: all groups)
set -uo pipefail
cd "$(dirname "$0")/../.."
APP=${APP:-./build/ts-v9}
OUT=report/figures/raw
COMMON="--headless --quality epic --size 1920x1080 --seed 1"
declare -A G
G[scene]="day_overview,night_searchlights,sky_noon,sky_sunset,dusk_shift_change,dawn_withdraw,day_patrol_spline,day_mechanic,day_logistics,soldier_closeup,soldier_axes,soldier_exploded,day_thermal_fpv_persons,bunker_offduty,iso_tank,iso_heli,iso_aa,iso_sam,iso_mlrs,iso_uav,soldier_front,v10_proj_persp,v10_proj_top,v10_proj_front,v10_proj_side,v10_proj_iso,v10_light_all,v10_light_ambient,v10_light_diffuse,v10_light_specular,m6_tree_close,m6_grass_close,m6_forest_night"
G[v5]="primitive_library,scene_thermal,uav_closeup_visible,scene_primitives_exploded,iso_uav_assembled,iso_uav_exploded,iso_rover_assembled,iso_rover_exploded,iso_bunker_assembled,iso_bunker_exploded,scene_xray,scene_depth,scene_hiddenline,uav_camera_view,bunker_flat_visible,hierarchy_axes,thermal_fpv,v1,shadows,ssao,fog,bloom,plan_topdown,base_overview,base_gate,plan_routes,tank_fire,aa_gun_burst,hide_thermal,tank_closeup"
G[play]="play_select,play_first_person,play_third_person,play_prompt_uav,play_door_closed,play_door_opening,play_door_open,play_exposure_enter,play_exposure_adapted,play_hall_inside,play_sprint_fov"
G[lab]="lab_shading,lab_buffers,lab_cv,lab_materials,lab_relief,lab_reflect,explorer"
G[rt]="raytrace_modes,raytrace_tree,raytrace_perf"
G[game]="uav_missile_launch,combined_strike,thermal_fpv_strike,explosion_early,explosion_mid,explosion_smoke,wreck_burning,kill_banner"
G[m3]="prims,parts:Autonomous UAV,parts:Main Battle Tank,parts:Soldier 1,parts:Command Bunker,parts:Ammunition bunker,build:Autonomous UAV,build:Main Battle Tank,build:Soldier 1,build:Command Bunker,build:Ammunition bunker"
SEL=("$@")
[ ${#SEL[@]} -eq 0 ] && SEL=(scene v5 play lab rt game m3)
for g in "${SEL[@]}"; do
  mkdir -p "$OUT/$g"
  echo "== $g: ${G[$g]}"
  "$APP" $COMMON --capture "$OUT/$g" --only "${G[$g]}" > "$OUT/$g/capture_stdout.txt" 2>&1
  echo "   exit $? : $(ls "$OUT/$g" | grep -c png) png"
done
