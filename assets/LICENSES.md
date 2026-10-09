# Third-party assets (v9 M6 / M7)

Every file under `assets/` is derived from a **CC0 1.0 Universal (public domain dedication)** source. CC0 allows copying,
modifying and redistributing without attribution; the authors are listed anyway. The originals were downloaded with
`python3 tools/fetch_assets.py` (cache in `build/asset_cache`, not committed) and reduced with `make bake-assets`
(`tools/bake_assets.cpp`): foliage cards simplified per card (meshoptimizer) and thinned to a triangle budget (cards grown
to keep the crown's coverage), solid parts simplified, wind weights written to `COLOR_0`, normals / UVs quantised
(`KHR_mesh_quantization`), the separate JPG alpha masks merged into RGBA PNG albedos, other maps re-encoded as JPG (q 90).
`assets/bake_summary.json` lists the triangle counts before / after.

| Asset | Used for | Author(s) | Source | License |
|---|---|---|---|---|
| fir_tree_01 | treeline / hill firs (3 variants) | Rob Tuytel (photography), Rico Cilliers (modeling) | https://polyhaven.com/a/fir_tree_01 | CC0 1.0 |
| pine_tree_01 | treeline / hill pines (3 variants) | Rob Tuytel (photography), Rico Cilliers (modeling) | https://polyhaven.com/a/pine_tree_01 | CC0 1.0 |
| fir_sapling_medium | camp firs (3 variants) | Rob Tuytel (photography), Rico Cilliers (modeling) | https://polyhaven.com/a/fir_sapling_medium | CC0 1.0 |
| pine_sapling_small | young pines at the forest edges (3 variants) | Rob Tuytel (photography), Rico Cilliers (modeling) | https://polyhaven.com/a/pine_sapling_small | CC0 1.0 |
| tree_small_02 | camp broadleaf trees | Rico Cilliers | https://polyhaven.com/a/tree_small_02 | CC0 1.0 |
| shrub_01 | camp bushes (5 variants) | Rico Cilliers | https://polyhaven.com/a/shrub_01 | CC0 1.0 |
| shrub_02 | camp bushes (4 variants) | Rico Cilliers | https://polyhaven.com/a/shrub_02 | CC0 1.0 |
| shrub_03 | ground cover (4 variants) | Rico Cilliers | https://polyhaven.com/a/shrub_03 | CC0 1.0 |
| shrub_04 | ground cover (4 variants) | Rico Cilliers | https://polyhaven.com/a/shrub_04 | CC0 1.0 |
| fern_02 | ground cover under trees (4 variants) | Rob Tuytel (scanning), Rico Cilliers (modeling) | https://polyhaven.com/a/fern_02 | CC0 1.0 |
| grass_medium_01 | ground-cover grass clumps (8 of 17 variants) | Rob Tuytel (photography), Rico Cilliers (modeling) | https://polyhaven.com/a/grass_medium_01 | CC0 1.0 |
| grass_medium_02 | blade atlas of the instanced grass | Rico Cilliers | https://polyhaven.com/a/grass_medium_02 | CC0 1.0 |
| boulder_01 | boulders | Rico Cilliers | https://polyhaven.com/a/boulder_01 | CC0 1.0 |
| rock_moss_set_01 | mossy rocks (6 variants) | Kless Gyzen | https://polyhaven.com/a/rock_moss_set_01 | CC0 1.0 |
| rock_moss_set_02 | mossy rocks (6 of 7 variants) | Kless Gyzen | https://polyhaven.com/a/rock_moss_set_02 | CC0 1.0 |
| Grass004 | terrain layer: grass | ambientCG (Lennart Demes) | https://ambientcg.com/view?id=Grass004 | CC0 1.0 |
| Ground048 | terrain layer: dirt | ambientCG (Lennart Demes) | https://ambientcg.com/view?id=Ground048 | CC0 1.0 |
| Rock051 | terrain layer: rock (slopes, triplanar) | ambientCG (Lennart Demes) | https://ambientcg.com/view?id=Rock051 | CC0 1.0 |
| Gravel040 | terrain layer: gravel (tracks, apron) | ambientCG (Lennart Demes) | https://ambientcg.com/view?id=Gravel040 | CC0 1.0 |
| Ground037 | terrain layer: forest floor | ambientCG (Lennart Demes) | https://ambientcg.com/view?id=Ground037 | CC0 1.0 |

License texts: Poly Haven https://polyhaven.com/license (CC0), ambientCG https://docs.ambientcg.com/license/ (CC0),
CC0 1.0 legal code https://creativecommons.org/publicdomain/zero/1.0/legalcode.

## Third-party code

| Library | Version | Where | License |
|---|---|---|---|
| cgltf (Johannes Kuhlmann) | 1.14 | `include/cgltf.h` | MIT |
| stb_image / stb_image_write (Sean Barrett) | 2.30 / 1.16 | `include/stb_image.h`, `include/stb_image_write.h` | public domain (or MIT) |
| meshoptimizer (Arseny Kapoulkine) | 0.22 | `third_party/meshoptimizer/` (LICENSE.md there) | MIT |
