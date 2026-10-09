// v9 M6: octahedral impostors (pure CPU math, mirrored in the impostor shaders).
//
// A distant tree is drawn as one camera-facing quad textured with a picture of the tree taken from (nearly) the
// direction the camera sees it from. The pictures are baked at start-up into an atlas of N x N views whose directions
// cover the upper hemisphere evenly: a hemi-octahedral map folds the hemisphere onto the square
//      d = (x, y, z), |x| + |y| + |z| = 1 (y up)  ->  u = x + z, v = x - z      (rotated 45 degrees, fills [-1, 1]^2)
// so view cell (i, j) looks along the direction decoded from the cell centre. At run time the view direction is encoded
// the same way; the four surrounding cells are blended bilinearly (no popping as the camera orbits).
#pragma once

namespace ts::asset {

struct OctDir {
    float x, y, z;
};

// Upper-hemisphere direction (y >= 0, normalised or not) -> octahedral coordinates in [0, 1]^2.
void hemiOctEncode(float x, float y, float z, float& u, float& v);
// [0, 1]^2 -> unit direction with y >= 0.
OctDir hemiOctDecode(float u, float v);

struct ImpostorTaps {
    int cell[4][2];      // the four atlas cells (i, j) around the view direction
    float weight[4];     // bilinear weights, sum 1
};
// View cells and weights for a view direction (from the object towards the camera, object space) on an n x n grid
// whose cell (i, j) centre decodes to the view direction of that picture.
ImpostorTaps impostorTaps(float x, float y, float z, int n);
// Direction of the picture in cell (i, j).
OctDir impostorCellDirection(int i, int j, int n);

}  // namespace ts::asset
