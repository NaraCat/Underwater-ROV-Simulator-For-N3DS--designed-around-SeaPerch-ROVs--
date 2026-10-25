#ifndef ENGINE_H
#define ENGINE_H

#include <citro3d.h>
#include <citro2d.h>

#ifdef __cplusplus
extern "C" {
#endif


// Vertex layout expected by the shader this module binds to:
// v0 = position (x,y,z), v1 = texcoord (u,v), v2 = normal (nx,ny,nz)
typedef struct {
    float position[3];
    float texcoord[2];
    float normal[3];
} Scene3D_Vertex;

// A single loaded 3D asset: a vertex buffer (in linear memory) plus a texture.
// Load with Model3D_Load(), draw with Model3D_Render(), free with Model3D_Unload().
typedef struct {
    void*   vbo_data;
    u32     vertex_count;
    C3D_Tex tex;
} Model3D;
 
// Plain 4-float vector, used for the light uniforms passed to Model3D_Render.
typedef struct {
    float x, y, z, w;
} Scene3D_Vec4;

// ---- Pipeline lifecycle (call once per app) ------------------------------
// Call once after C3D_Init(). Binds the given vertex shader binary, looks up
// its uniforms (projection, modelView, lightVec, lightHalfVec, lightClr,
// material), configures the position/texcoord/normal vertex attributes, and
// sets default depth test / cull face state. Returns false on failure.
bool Scene3D_Init(const void* shbinData, u32 shbinSize);
 
// Call once before C3D_Fini() to free the shader program.
void Scene3D_Exit(void);
 
// ---- Per-model asset lifecycle (call for each model you use) ------------
// Copies vertexCount Scene3D_Vertex entries from vertexData into linear
// memory and imports a t3x texture from texData/texSize into `model`.
// Returns false on failure (model is left zeroed).
bool Model3D_Load(Model3D* model, const void* vertexData, u32 vertexCount,
                   const void* texData, size_t texSize);
 
// Frees a model's VBO and texture. Safe to call on an already-unloaded model.
void Model3D_Unload(Model3D* model);
 
// ---- Per-frame rendering (call for each model, each frame) --------------
// Draws `model` with the given projection/modelView/material matrices and
// light vectors. Call C3D_FrameDrawOn(target) beforehand to pick which
// screen/target you're drawing to - this function doesn't touch the target,
// so you can draw several models to the same target before switching.
void Model3D_Render(const Model3D* model,
                     const C3D_Mtx* projection,
                     const C3D_Mtx* modelView,
                     const C3D_Mtx* material,
                     Scene3D_Vec4 lightVec,
                     Scene3D_Vec4 lightHalfVec,
                     Scene3D_Vec4 lightClr);
                     
//=========================COLLISION=============================
// AABB in world space
typedef struct {
  float minX, minY, minZ;
  float maxX, maxY, maxZ;
} Collision3D_AABB;
 
//Defines a box that can rotate around the y axis (yaw) but never tilts.
//Like mentioned in engine.c, this is a good fit for vehicles or characters
//that only turn left or right.
//forwardX and Z is a unit vector giving the box's local forward axis in world xz. 
//halfForward, halfRight, and halfHeight are half the length along the forward,
//perpendicular to forward (right), and world Y respectively.
typedef struct {
  float centerX, centerY, centerZ;
  float forwardX, forwardZ;
  float halfForward;
  float halfRight;
  float halfHeight;
} Collision3D_OBB2D;

//Builds a worlds space AABB from a local space boix, given as two opposite corners in local space. 
//This uses a combined position*orientation matrix and a uniform scale factor. 
//This is intended for static geometry called once at load time rather than every frame
Collision3D_AABB Collision3D_TransformAABB(const C3D_Mtx* worldMtx, float scale,
                                            float lx0, float ly0, float lz0,
                                            float lx1, float ly1, float lz1);
                                            
//Returns true if the obb overlaps with the AABB
bool Collision3D_OBB2DIntersectsAABB(const Collision3D_OBB2D* obb, const Collision3D_AABB* box);

//A full 3D oriented bounding box: a box that can be tilted on any axis
//(pitch/roll/yaw), not just rotated around Y like Collision3D_OBB2D.
//Good for airborne/free-rotating objects - e.g. the bot doing a barrel roll.
//axisX/axisY/axisZ are the box's local axes given as UNIT vectors in world
//space (they must stay orthonormal to each other); halfX/halfY/halfZ are
//the half-extents (box size / 2) measured along those axes.
typedef struct {
  float centerX, centerY, centerZ;
  float axisX[3];
  float axisY[3];
  float axisZ[3];
  float halfX, halfY, halfZ;
} Collision3D_OBB3D;

//Builds a world-space OBB3D from a world matrix (position + rotation) and a
//uniform scale, given the box's local-space center and local half-extents.
//Like Collision3D_TransformAABB, this is meant for a one-time setup / a
//per-frame refresh of a moving object's collider, not a hot inner loop.
Collision3D_OBB3D Collision3D_MakeOBB3D(const C3D_Mtx* worldMtx, float scale,
                                         float centerLX, float centerLY, float centerLZ,
                                         float halfLX, float halfLY, float halfLZ);

//Returns true if the two OBBs overlap. Full 3D SAT test over 15 axes: each
//box's 3 face normals, plus the 9 cross products between them. This is what
//lets it handle arbitrary rotations instead of just yaw.
bool Collision3D_OBB3DIntersectsOBB3D(const Collision3D_OBB3D* a, const Collision3D_OBB3D* b);

//Returns true if the OBB overlaps the AABB. Implemented by treating the AABB
//as an axis-aligned OBB3D and reusing Collision3D_OBB3DIntersectsOBB3D, so
//there's only one SAT implementation to maintain.
bool Collision3D_OBB3DIntersectsAABB(const Collision3D_OBB3D* obb, const Collision3D_AABB* box);

//Returns true if a yaw-only OBB2D (e.g. the bot's hit box) overlaps a fully
//rotated OBB3D (e.g. a tilted loop/ring segment). Internally treats the
//OBB2D as an OBB3D with its "up" axis pinned to world Y - since OBB2D never
//tilts - and reuses Collision3D_OBB3DIntersectsOBB3D, so this is the
//function to call to check the bot against loop obstacles.
bool Collision3D_OBB2DIntersectsOBB3D(const Collision3D_OBB2D* obb2d, const Collision3D_OBB3D* obb3d);

// A Torus (donut shape) collider in world space
typedef struct {
  float centerX, centerY, centerZ;
  float majorRadius; 
  float minorRadius; 
  float axisX[3]; // Local X axis as a unit vector
  float axisY[3]; // Local Y axis (normal to the ring plane) as a unit vector
  float axisZ[3]; // Local Z axis as a unit vector
} Collision3D_Torus;

// Updated function declaration
bool Collision3D_OBB2DIntersectsTorus(const Collision3D_OBB2D* obb2d, const Collision3D_Torus* torus);

extern void runDebuggerText(C2D_TextBuf g_textBuf, C2D_Text* g_statsText, C3D_RenderTarget* targetBottom); 

#ifdef __cplusplus
}
#endif


#endif
