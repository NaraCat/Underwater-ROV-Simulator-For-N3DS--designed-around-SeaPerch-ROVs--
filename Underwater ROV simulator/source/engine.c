#include "engine.h"
#include <3ds.h>
#include <citro3d.h>
#include <citro2d.h>
#include <tex3ds.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

static DVLB_s* vshader_dvlb;
static shaderProgram_s program;
static int uLoc_projection, uLoc_modelView;
static int uLoc_lightVec, uLoc_lightHalfVec, uLoc_lightClr, uLoc_material;

//Helper function for loading a texture from memory
static bool loadTextureFromMem(C3D_Tex* tex, const void* data, size_t size) {
  Tex3DS_Texture t3x = Tex3DS_TextureImport(data, size, tex, NULL, false);
  if (!t3x)
    return false;
 
  //Delete the t3x object since we don't need it
  Tex3DS_TextureFree(t3x);
  return true;
}
 
bool Scene3D_Init(const void* shbinData, u32 shbinSize) {
  //Load the vertex shader, create a shader program and bind it
  vshader_dvlb = DVLB_ParseFile((u32*)shbinData, shbinSize);
  if (!vshader_dvlb)
    return false;
 
  shaderProgramInit(&program);
  shaderProgramSetVsh(&program, &vshader_dvlb->DVLE[0]);
  C3D_BindProgram(&program);
 
  //get the location of the uniforms
  uLoc_projection   = shaderInstanceGetUniformLocation(program.vertexShader, "projection");
  uLoc_modelView    = shaderInstanceGetUniformLocation(program.vertexShader, "modelView");
  uLoc_lightVec     = shaderInstanceGetUniformLocation(program.vertexShader, "lightVec");
  uLoc_lightHalfVec = shaderInstanceGetUniformLocation(program.vertexShader, "lightHalfVec");
  uLoc_lightClr     = shaderInstanceGetUniformLocation(program.vertexShader, "lightClr");
  uLoc_material     = shaderInstanceGetUniformLocation(program.vertexShader, "material");
 
  //Configure attributes for use with the vertex shader
  C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
  AttrInfo_Init(attrInfo);
  AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3); // v0=position (x, y, z)
  AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 2); // v1 = texcoord (UV)
  AttrInfo_AddLoader(attrInfo, 2, GPU_FLOAT, 3); // v2=normal (nx, ny, nz)
 
  //Configure the first fragment shading substage to blend the texture color with
  //the vertex color (calculated by the vertex shader using a lighting algorithm)
  //See https://www.opengl.org/sdk/docs/man2/xhtml/glTexEnv.xml for more insight
  C3D_TexEnv* env = C3D_GetTexEnv(0);
  C3D_TexEnvInit(env);
  C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
  C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
 
  C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
  C3D_CullFace(GPU_CULL_NONE);
 
  return true;
}
 
void Scene3D_Exit(void) {
  //Free the shader program
  shaderProgramFree(&program);
  DVLB_Free(vshader_dvlb);
}
 
bool Model3D_Load(Model3D* model, const void* vertexData, u32 vertexCount,
                   const void* texData, size_t texSize) {
  if (!model)
    return false;
 
  memset(model, 0, sizeof(*model));
 
  //Allocate linear memory sized exactly for this model's vertex data
  u32 bytes = vertexCount * sizeof(Scene3D_Vertex);
  model->vbo_data = linearAlloc(bytes);
  if (!model->vbo_data)
    return false;
 
  memcpy(model->vbo_data, vertexData, bytes);
 
  //Flush CPU cache so the GPU sees the freshly-copied vertex data
  GSPGPU_FlushDataCache(model->vbo_data, bytes);
  model->vertex_count = vertexCount;
 
  //Load the texture
  if (!loadTextureFromMem(&model->tex, texData, texSize)) {
    linearFree(model->vbo_data);
    model->vbo_data = NULL;
    model->vertex_count = 0;
    return false;
  }
  C3D_TexSetFilter(&model->tex, GPU_LINEAR, GPU_NEAREST);
 
  return true;
}
 
void Model3D_Unload(Model3D* model) {
  if (!model)
    return;
 
  if (model->vbo_data) {
    C3D_TexDelete(&model->tex);
    linearFree(model->vbo_data);
    model->vbo_data = NULL;
  }
  model->vertex_count = 0;
}
 
void Model3D_Render(const Model3D* model,
                     const C3D_Mtx* projection,
                     const C3D_Mtx* modelView,
                     const C3D_Mtx* material,
                     Scene3D_Vec4 lightVec,
                     Scene3D_Vec4 lightHalfVec,
                     Scene3D_Vec4 lightClr) {
  if (!model || !model->vbo_data)
    return;
 
  C3D_BindProgram(&program);
 
  C3D_TexEnv* env = C3D_GetTexEnv(0);
  C3D_TexEnvInit(env);
  C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
  C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
 
  //Upload the transforms and lighting for this draw call
  C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_projection, projection);
  C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView,  modelView);
  C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material,   material);
  C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc_lightVec,
                lightVec.x, lightVec.y, lightVec.z, lightVec.w);
  C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc_lightHalfVec,
                lightHalfVec.x, lightHalfVec.y, lightHalfVec.z, lightHalfVec.w);
  C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc_lightClr,
                lightClr.x, lightClr.y, lightClr.z, lightClr.w);
 
  //Bind this model's texture to Unit0
  C3D_TexBind(0, (C3D_Tex*)&model->tex);
 
  C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
  AttrInfo_Init(attrInfo);
  AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3); // position
  AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 2); // texcoord
  AttrInfo_AddLoader(attrInfo, 2, GPU_FLOAT, 3); // normal
 
  C3D_BufInfo* bufInfo = C3D_GetBufInfo();
  BufInfo_Init(bufInfo);
  BufInfo_Add(bufInfo, model->vbo_data, sizeof(Scene3D_Vertex), 3, 0x210);
 
  //Draw the model
  C3D_DrawArrays(GPU_TRIANGLES, 0, model->vertex_count);
}
//================================COLLISION======================================

//Creates axis aligned bounding boxes
Collision3D_AABB Collision3D_TransformAABB(const C3D_Mtx* worldMtx, float scale,
                                           float lx0, float ly0, float lz0,
                                           float lx1, float ly1, float lz1) {
  C3D_FVec p0 = FVec4_New(lx0 * scale, ly0 * scale, lz0 * scale, 1.0f);
  C3D_FVec p1 = FVec4_New(lx1 * scale, ly1 * scale, lz1 * scale, 1.0f);
  C3D_FVec w0 = Mtx_MultiplyFVec4(worldMtx, p0);
  C3D_FVec w1 = Mtx_MultiplyFVec4(worldMtx, p1);
  
  Collision3D_AABB box;
  box.minX = fminf(w0.x, w1.x); box.maxX = fmaxf(w0.x, w1.x);
  box.minY = fminf(w0.y, w1.y); box.maxY = fmaxf(w0.y, w1.y);
  box.minZ = fminf(w0.z, w1.z); box.maxZ = fmaxf(w0.z, w1.z);
  return box;
}

//determines if the 2d OBB is colliding with a AABB
//This uses the struct for the hit box defined in engine.h.
bool Collision3D_OBB2DIntersectsAABB(const Collision3D_OBB2D* obb, const Collision3D_AABB* box) {
  //Checks if y range overlaps, if not, it skips collision detection. This is like a wide sweep
  if (obb->centerY + obb->halfHeight < box->minY || obb->centerY - obb->halfHeight > box->maxY)
    return false;
 
  //sets the right axis to the forward one rotated by 90 degrees
  float rightX = -obb->forwardZ;
  float rightZ =  obb->forwardX;
 
  //Builds the hit box's four x and z corners
  float corners[4][2];
  for (int i = 0; i < 4; i++) {
    float sf = (i & 1) ? -1.0f : 1.0f;
    float sr = (i & 2) ? -1.0f : 1.0f;
    corners[i][0] = obb->centerX + sf * obb->halfForward * obb->forwardX + sr * obb->halfRight * rightX;
    corners[i][1] = obb->centerZ + sf * obb->halfForward * obb->forwardZ + sr * obb->halfRight * rightZ;
  }
 
  //tests four separating axes: obb forward, obb right, world X, world Z.
  //lessens the math load compared to doing a full 3d object hit box using SAT,
  //SAT would take sixteen ish calculations rather than the four here.
  float axes[4][2] = { {obb->forwardX, obb->forwardZ},
                       {rightX,        rightZ},
                       {1.0f,          0.0f},
                       {0.0f,          1.0f} };
 
  //does the four separating axes tests, aka SAT
  for (int a = 0; a < 4; a++) {
    float ax = axes[a][0], az = axes[a][1];
 
    //object's hit box corners
    float boxMin = 1e30f, boxMax = -1e30f;
    for (int i = 0; i < 4; i++) {
      float proj = corners[i][0] * ax + corners[i][1] * az;
      if (proj < boxMin) boxMin = proj;
      if (proj > boxMax) boxMax = proj;
    }
 
    //other object's AABB hit box corners. usually the stationary object
    float aabbCorners[4][2] = {
      {box->minX, box->minZ},
      {box->minX, box->maxZ},
      {box->maxX, box->minZ},
      {box->maxX, box->maxZ}
    };
    float aabbMin = 1e30f, aabbMax = -1e30f;
    for (int i = 0; i < 4; i++) {
      float proj = aabbCorners[i][0] * ax + aabbCorners[i][1] * az;
      if (proj < aabbMin) aabbMin = proj;
      if (proj > aabbMax) aabbMax = proj;
    }
 
    //If they DON'T collide on this axis, return false for no collision
    if (boxMax < aabbMin || aabbMax < boxMin)
      return false;
  }
 
  return true; //Returns true if there are no separating axis found on any tested axis. basically, true if they overlap.
}



//---------------------------- 3D OBB --------------------------------------
//General oriented-box collision. Unlike Collision3D_OBB2D above, this
//supports boxes tilted on any axis, at the cost of a heavier overlap test
//(15 candidate axes instead of 4).

static float v3dot(const float a[3], const float b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void v3cross(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

static void v3normalize(float v[3]) {
  float len = sqrtf(v3dot(v, v));
  if (len > 1e-8f) {
    v[0] /= len; v[1] /= len; v[2] /= len;
  }
}

Collision3D_OBB3D Collision3D_MakeOBB3D(const C3D_Mtx* worldMtx, float scale,
                                         float centerLX, float centerLY, float centerLZ,
                                         float halfLX, float halfLY, float halfLZ) {
  Collision3D_OBB3D obb;

  //World-space center: transform the local center point through the full matrix
  C3D_FVec c  = FVec4_New(centerLX * scale, centerLY * scale, centerLZ * scale, 1.0f);
  C3D_FVec wc = Mtx_MultiplyFVec4(worldMtx, c);
  obb.centerX = wc.x; obb.centerY = wc.y; obb.centerZ = wc.z;

  //World-space axis directions: transform the local unit axes with w=0 so
  //translation is ignored, then normalize (this also removes the scale).
  C3D_FVec ax = Mtx_MultiplyFVec4(worldMtx, FVec4_New(1.0f, 0.0f, 0.0f, 0.0f));
  C3D_FVec ay = Mtx_MultiplyFVec4(worldMtx, FVec4_New(0.0f, 1.0f, 0.0f, 0.0f));
  C3D_FVec az = Mtx_MultiplyFVec4(worldMtx, FVec4_New(0.0f, 0.0f, 1.0f, 0.0f));

  obb.axisX[0] = ax.x; obb.axisX[1] = ax.y; obb.axisX[2] = ax.z;
  obb.axisY[0] = ay.x; obb.axisY[1] = ay.y; obb.axisY[2] = ay.z;
  obb.axisZ[0] = az.x; obb.axisZ[1] = az.y; obb.axisZ[2] = az.z;
  v3normalize(obb.axisX);
  v3normalize(obb.axisY);
  v3normalize(obb.axisZ);

  obb.halfX = halfLX * scale;
  obb.halfY = halfLY * scale;
  obb.halfZ = halfLZ * scale;

  return obb;
}

bool Collision3D_OBB3DIntersectsOBB3D(const Collision3D_OBB3D* a, const Collision3D_OBB3D* b) {
  //Vector between the two centers - what we project onto each candidate axis
  float d[3] = { b->centerX - a->centerX, b->centerY - a->centerY, b->centerZ - a->centerZ };

  //---- Axes 1-6: each box's own face normals -----------------------------
  const float* boxAxes[6] = { a->axisX, a->axisY, a->axisZ, b->axisX, b->axisY, b->axisZ };

  for (int i = 0; i < 6; i++) {
    const float* L = boxAxes[i];

    //Project each box's half-extents onto L to get its "radius" along L
    float ra = fabsf(v3dot(a->axisX, L)) * a->halfX
             + fabsf(v3dot(a->axisY, L)) * a->halfY
             + fabsf(v3dot(a->axisZ, L)) * a->halfZ;
    float rb = fabsf(v3dot(b->axisX, L)) * b->halfX
             + fabsf(v3dot(b->axisY, L)) * b->halfY
             + fabsf(v3dot(b->axisZ, L)) * b->halfZ;

    if (fabsf(v3dot(d, L)) > ra + rb)
      return false; //gap found along this axis - boxes can't be touching
  }

  //---- Axes 7-15: cross products of one box's axes with the other's ------
  //These catch "edge vs edge" separations that the 6 face normals miss -
  //e.g. two boxes crossed like an X, touching only along their edges.
  const float* aAxes[3] = { a->axisX, a->axisY, a->axisZ };
  const float  aHalf[3] = { a->halfX, a->halfY, a->halfZ };
  const float* bAxes[3] = { b->axisX, b->axisY, b->axisZ };
  const float  bHalf[3] = { b->halfX, b->halfY, b->halfZ };

  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      float L[3];
      v3cross(aAxes[i], bAxes[j], L);

      //Skip Near-parallel axes to save compute 
      if (v3dot(L, L) < 1e-8f)
        continue;

      float ra = 0.0f, rb = 0.0f;
      for (int k = 0; k < 3; k++) {
        ra += fabsf(v3dot(aAxes[k], L)) * aHalf[k];
        rb += fabsf(v3dot(bAxes[k], L)) * bHalf[k];
      }

      if (fabsf(v3dot(d, L)) > ra + rb)
        return false;
    }
  }

  return true; //no separating axis found on any of the 15 candidates -> overlapping
}

bool Collision3D_OBB3DIntersectsAABB(const Collision3D_OBB3D* obb, const Collision3D_AABB* box) {
  Collision3D_OBB3D aabbAsObb;
  aabbAsObb.centerX = (box->minX + box->maxX) * 0.5f;
  aabbAsObb.centerY = (box->minY + box->maxY) * 0.5f;
  aabbAsObb.centerZ = (box->minZ + box->maxZ) * 0.5f;
  aabbAsObb.axisX[0] = 1.0f; aabbAsObb.axisX[1] = 0.0f; aabbAsObb.axisX[2] = 0.0f;
  aabbAsObb.axisY[0] = 0.0f; aabbAsObb.axisY[1] = 1.0f; aabbAsObb.axisY[2] = 0.0f;
  aabbAsObb.axisZ[0] = 0.0f; aabbAsObb.axisZ[1] = 0.0f; aabbAsObb.axisZ[2] = 1.0f;
  aabbAsObb.halfX = (box->maxX - box->minX) * 0.5f;
  aabbAsObb.halfY = (box->maxY - box->minY) * 0.5f;
  aabbAsObb.halfZ = (box->maxZ - box->minZ) * 0.5f;

  return Collision3D_OBB3DIntersectsOBB3D(obb, &aabbAsObb);
}

bool Collision3D_OBB2DIntersectsOBB3D(const Collision3D_OBB2D* obb2d, const Collision3D_OBB3D* obb3d) {
  Collision3D_OBB3D asObb3d;
  asObb3d.centerX = obb2d->centerX;
  asObb3d.centerY = obb2d->centerY;
  asObb3d.centerZ = obb2d->centerZ;

  asObb3d.axisX[0] = obb2d->forwardX;  asObb3d.axisX[1] = 0.0f; asObb3d.axisX[2] = obb2d->forwardZ;
  asObb3d.axisY[0] = 0.0f;             asObb3d.axisY[1] = 1.0f; asObb3d.axisY[2] = 0.0f;
  asObb3d.axisZ[0] = -obb2d->forwardZ; asObb3d.axisZ[1] = 0.0f; asObb3d.axisZ[2] = obb2d->forwardX;

  asObb3d.halfX = obb2d->halfForward;
  asObb3d.halfY = obb2d->halfHeight;
  asObb3d.halfZ = obb2d->halfRight;

  return Collision3D_OBB3DIntersectsOBB3D(&asObb3d, obb3d);
}

bool Collision3D_OBB2DIntersectsTorus(const Collision3D_OBB2D* obb2d, const Collision3D_Torus* torus) {
  float obbX = obb2d->centerX;
  float obbY = obb2d->centerY;
  float obbZ = obb2d->centerZ;

  float dx = obbX - torus->centerX;
  float dy = obbY - torus->centerY;
  float dz = obbZ - torus->centerZ;

  float localX = dx * torus->axisX[0] + dy * torus->axisX[1] + dz * torus->axisX[2];
  float localY = dx * torus->axisY[0] + dy * torus->axisY[1] + dz * torus->axisY[2];
  float localZ = dx * torus->axisZ[0] + dy * torus->axisZ[1] + dz * torus->axisZ[2];

  float radialDist = sqrtf(localX * localX + localZ * localZ);
  float qx = radialDist - torus->majorRadius;
  float qy = localY;
  float distanceSq = (qx * qx) + (qy * qy);

  float maxExtent = fmaxf(obb2d->halfForward, fmaxf(obb2d->halfHeight, obb2d->halfRight));
  float totalRadius = torus->minorRadius + maxExtent;

  return distanceSq <= (totalRadius * totalRadius);
}

void runDebuggerText(C2D_TextBuf g_textBuf, C2D_Text* g_statsText, C3D_RenderTarget* targetBottom) {
  C2D_TextBufClear(g_textBuf);
  
  C2D_SceneBegin(targetBottom);
  
  C2D_Prepare();
  char buf[256];
  
  //Gets heap usage 
  u32 memUsed  = osGetMemRegionUsed(MEMREGION_APPLICATION);
  u32 memTotal = osGetMemRegionSize(MEMREGION_APPLICATION);

  //gets linear alloc ram
  u32 linearFree = linearSpaceFree();
  
  snprintf(buf, sizeof(buf), 
         "Debugger For Funnies\n"
         "CPU:     %6.2f%%\n"
         "GPU:     %6.2f%%\n"
         "RAM:     %6lu / %6lu KB\n"
         "Linear free: %6lu KB\n", 
         C3D_GetProcessingTime() * 6.0f, 
         C3D_GetDrawingTime() * 6.0f,
         (unsigned long)(memUsed / 1024), (unsigned long)(memTotal / 1024),
         (unsigned long)(linearFree / 1024));
           
  // Parse the raw char array string into a Citro2D hardware string texture
  C2D_TextParse(g_statsText, g_textBuf, buf);
  C2D_TextOptimize(g_statsText);
  C2D_DrawText(g_statsText, C2D_WithColor, 0.0f, 0.0f, 0.5f, 0.6f, 0.6f, C2D_Color32(0, 0, 0, 255));
}
