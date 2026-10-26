# The VR hands' mesh (SOMVR_XR_HANDS, docs/re-xr-pipeline.md 33)

`right.glb` is the WebXR Input Profiles' `generic-hand` right hand, unchanged:
- source: npm package `@webxr-input-profiles/assets` 1.0.20, `dist/profiles/generic-hand/right.glb`
  (https://cdn.jsdelivr.net/npm/@webxr-input-profiles/assets@1.0.20/dist/profiles/generic-hand/right.glb, fetched
  2026-10-24), sha256 `291790c14f7f88a7f9bd35330c47392ed8e8d395ae6728f4bb7089f1bc1f2b96`;
- license: MIT, Copyright (c) 2019 Amazon (`LICENSE.md` next to this file, the package's own; its README: "Assets are
  available under MIT license"). The MIT notice ships with anything built from it (`somvr_handmesh.h` repeats it).

The mesh: 1360 vertices (1159 positions: the rest are UV seams), 2314 triangles, a closed surface (the wrist end is
capped), one skin of 25 joints in the WebXR / XR_EXT_hand_tracking order (wrist, then thumb metacarpal..tip, index,
middle, ring, pinky metacarpal..tip), at most 4 joints a vertex, every joint a direct child of the armature (the rest
transforms are model space). Model space: metres, the fingers along -Y, the back of the hand +X, the thumb toward -Z.

`scripts/handmesh_gen.py` turns it into `mod/somvr/somvr_handmesh.h` (positions, normals, joints, weights, triangles,
the joints' rest poses; no UVs, no material). The left hand is the right one mirrored (somvr_vrhands.c).
