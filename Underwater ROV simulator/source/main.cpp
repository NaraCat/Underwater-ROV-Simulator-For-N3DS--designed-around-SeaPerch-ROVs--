#include <stdio.h>
#include <citro3d.h>
#include <citro2d.h>
#include <tex3ds.h>
#include <string.h>
#include <3ds.h>
#include <stdint.h>
#include "engine.h"
#include "vshader_shbin.h"
#include "FlutterCube_t3x.h"
#include "FlutterCube.h"
#include "SeaPerchBot_t3x.h"
#include "SeaPerchBot.h"
#include "SeaPerchObsticalCourse_t3x.h"
#include "SeaPerchObsticalCourse.h"
#include "SeaPerchTopMenu_t3x.h"
#include "SeaPerchBottomMenu_t3x.h"

#define CLEAR_COLOR 0x0090ff90

//This gets the display to work normally
#define DISPLAY_TRANSFER_FLAGS \
  (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
  GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
  GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

#define MAX_COURSE_COLLIDERS 8
#define MAX_TORUS_COLLIDERS 5
static Collision3D_Torus torusColliders[MAX_TORUS_COLLIDERS];
static int torusColliderCount = 0;
  
bool showDebugger = false;
  
//sets the Scene3D_Vertex layout to work with the .h models
typedef Scene3D_Vertex vertex;

bool onMainMenu = true;

//Enables rendering for top and bottom screen
static C3D_RenderTarget* targetTopLeft;
static C3D_RenderTarget* targetTopRight;
static C3D_RenderTarget* targetBottom;

//Text rendering variables
static C2D_TextBuf g_textBuf;
static C2D_Text g_statsText;

//Sets up bottom screen projection matrices for this app/game
static C3D_Mtx projectionBottom;
static C3D_Mtx material = 
{
  {
  { { 0.0f, 0.2f, 0.2f, 0.2f } }, // Ambient
  { { 0.0f, 0.4f, 0.4f, 0.4f } }, // Diffuse
  { { 0.0f, 0.8f, 0.8f, 0.8f } }, // Specular
  { { 1.0f, 0.0f, 0.0f, 0.0f } }, // Emission
  }
};

//3d Assets
static Model3D flutterCubeModel;
static Model3D seaPerchBotModel;
static Model3D obsticalCourseModel;

//Sets up global matrices for 3d assets
C3D_Mtx flutterCubeOrientation;
C3D_Mtx seaPerchBotOrientation;
C3D_Mtx seaPerchBotPosition;
C3D_Mtx obsticalCourseOrientation;
C3D_Mtx obsticalCoursePosition;

//Sets up menu variables
static C2D_SpriteSheet topMenuSpriteSheet;
static C2D_Image topMenuImage;
static C2D_SpriteSheet bottomMenuSpriteSheet;
static C2D_Image bottomMenuImage;

//Bot's default world position in world units
float seaPerchBotX = 0.0f;
float seaPerchBotY = -4.0f;
float seaPerchBotZ = 0.0f;

//Static collision geometry for obstacal course
static Collision3D_AABB courseColliders[MAX_COURSE_COLLIDERS];
static int courseColliderCount = 0;

//BGM variables
static u32* bgmBuffer = NULL;
static size_t bgmSize = 0;
static ndspWaveBuf waveBuf;
static char audioStatus[256] = "Audio status: Initializing...";

//Defines bot hit boxes
//Units are in world space
typedef struct { 
  float fwdOffset, rightOffset, upOffset; 
  float halfFwd, halfRight, halfHeight; 
} BotBoxDef;


//Defines the actual hit boxes
static const BotBoxDef botBoxes[2] = {
  { 0.0f, 0.0f, 0.6f, 0.8f, 0.5f, 0.35f }, // first hit box
  { -0.6f, 0.0f, 0.6f, 0.4f, 0.6f, 0.25f }, // second hit box
};

class Timer3DS {
  u64 startTick;

public:
  Timer3DS() {
    reset();
  }
  
  void reset() {
    startTick = svcGetSystemTick();
  }
  
   u32 getElapsedDeciseconds() const {
        u64 currentTick = svcGetSystemTick();
        u64 diff = currentTick - startTick;
        // Multiply first to avoid losing precision in integer division
        return (diff * 10) / SYSCLOCK_ARM11;
    }
};

//Adds one obatacle course collider
static void addCourseCollider(float lx0, float ly0, float lz0,
                               float lx1, float ly1, float lz1) {
  if (courseColliderCount >=  MAX_COURSE_COLLIDERS) 
    return;
    
  C3D_Mtx worldMtx;
  Mtx_Multiply(&worldMtx, &obsticalCourseOrientation, &obsticalCoursePosition);
  
  courseColliders[courseColliderCount++] = 
      Collision3D_TransformAABB(&worldMtx, 0.1f, lx0, ly0, lz0, lx1, ly1, lz1);
}

//Camera and light matrices
C3D_Mtx cameraRotationTop;
bool cameraFlipped = false;
C3D_Mtx lightOrientation;

//Default light direction
const Scene3D_Vec4 baseLightDir = { 0.0f, 0.0f, 1.0f, 0.0f };

//Helper function to move the light direction and location
static Scene3D_Vec4 Vec4_TransformByMtx(const C3D_Mtx* mtx, Scene3D_Vec4 v) {
  C3D_FVec in  = FVec4_New(v.x, v.y, v.z, v.w);
  C3D_FVec out = Mtx_MultiplyFVec4(mtx, in);
  Scene3D_Vec4 result = { out.x, out.y, out.z, out.w };
  return result;
}
//handles background music playing and looping
//handles background music playing and looping
static void playLoopingBGM(const char* filePath) {
  FILE* f = fopen(filePath, "rb");
  if (!f) {
    snprintf(audioStatus, sizeof(audioStatus), "AUDIO ERROR: Cannot open %s", filePath);
    return;
  }
  
  //Gets the file size
  fseek(f, 0, SEEK_END);
  size_t fileSize = ftell(f);
  
  //skips the 44 byte wav header
  const size_t wavHeaderSize = 44;
  if (fileSize <= wavHeaderSize) {
    snprintf(audioStatus, sizeof(audioStatus), "AUDIO ERROR: File too small / invalid WAV");
    fclose(f);
    return;
  }
  
  bgmSize = fileSize -wavHeaderSize;
  fseek(f, wavHeaderSize, SEEK_SET);
  
  //Allocate linear memory required by the 3ds dsp hadware
  bgmBuffer = (u32*)linearAlloc(bgmSize);
  if (!bgmBuffer) {
    snprintf(audioStatus, sizeof(audioStatus), "AUDIO ERROR: Out of Linear RAM");
    fclose(f);
    return;
  }
  
  //Reads the raw audio file into buffer
  fread(bgmBuffer, 1, bgmSize, f);
  fclose(f);
  
  //Flush CPU cache to main ram so dsp hardware sees the audio data
  DSP_FlushDataCache(bgmBuffer, bgmSize);
  
  //configures the ndsp channel 0
  ndspChnReset(0);
  ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
  ndspChnSetRate(0, 44100); // sets playback rate (11025Hz)
  ndspChnSetFormat(0, NDSP_FORMAT_MONO_PCM16);
  
  // set volume / speaker mix
  float mix[12] = {0};
  mix[0] = 0.6f;
  mix[1] = 0.0f;
  mix[2] = 0.0f;
  mix[3] = 0.6f; 
  ndspChnSetMix(0, mix);
  
  //sets up the audio wave buffer with looping
  memset(&waveBuf, 0, sizeof(ndspWaveBuf));
  waveBuf.data_vaddr = bgmBuffer;
  waveBuf.nsamples   = bgmSize / 2;
  waveBuf.looping    = true;
  
  ndspChnWaveBufAdd(0, &waveBuf);
  
  snprintf(audioStatus, sizeof(audioStatus), "BGM Playing Successfully!");
}

static void stopBGM(void) {
  ndspChnReset(0);
  if (bgmBuffer) {
    linearFree(bgmBuffer);
    bgmBuffer = NULL;
  }
}
static void addRotatedTorus(float centerX, float centerY, float centerZ, float majorR, float minorR, float degX, float degZ) {
  if (torusColliderCount >= MAX_TORUS_COLLIDERS) return;

  C3D_Mtx rotMtx, mtxX, mtxZ;
  Mtx_Identity(&mtxX);
  Mtx_Identity(&mtxZ);
  Mtx_RotateX(&mtxX, C3D_AngleFromDegrees(degX), true);
  Mtx_RotateZ(&mtxZ, C3D_AngleFromDegrees(degZ), true);
  Mtx_Multiply(&rotMtx, &mtxZ, &mtxX); // Combine rotations

  Collision3D_Torus torus;
  torus.centerX = centerX;
  torus.centerY = centerY;
  torus.centerZ = centerZ;
  torus.majorRadius = majorR;
  torus.minorRadius = minorR;

  // Correctly extract axes from row-major C3D_Mtx layout:
  // Row 0 = local X axis, Row 1 = local Y axis (ring normal), Row 2 = local Z axis
  torus.axisX[0] = rotMtx.r[0].x; torus.axisX[1] = rotMtx.r[0].y; torus.axisX[2] = rotMtx.r[0].z;
  torus.axisY[0] = rotMtx.r[1].x; torus.axisY[1] = rotMtx.r[1].y; torus.axisY[2] = rotMtx.r[1].z;
  torus.axisZ[0] = rotMtx.r[2].x; torus.axisZ[1] = rotMtx.r[2].y; torus.axisZ[2] = rotMtx.r[2].z;

  torusColliders[torusColliderCount++] = torus;
}

static void obsticalCourseInit(void) {
  //Sets up the generic 3D pipeline, shader, attributes, depth/cull state
  if (!Scene3D_Init(vshader_shbin, vshader_shbin_size)) 
    svcBreak(USERBREAK_PANIC);
  
  //Compute the projection matrix
  Mtx_PerspTilt(&projectionBottom, C3D_AngleFromDegrees(80.0f), (320.0f / 240.0f), 0.01f, 1000.0f, false);
  
  if (!Model3D_Load(&seaPerchBotModel, SeaPerchBot_vertex_data, SeaPerchBot_vertex_count,
                    SeaPerchBot_t3x, SeaPerchBot_t3x_size))
    svcBreak(USERBREAK_PANIC);
  if (!Model3D_Load(&flutterCubeModel, FlutterCube_vertex_data, FlutterCube_vertex_count,
                    FlutterCube_t3x, FlutterCube_t3x_size))
    svcBreak(USERBREAK_PANIC);
  if (!Model3D_Load(&obsticalCourseModel, ObsticalCourse_vertex_data, ObsticalCourse_vertex_count,
                    SeaPerchObsticalCourse_t3x, SeaPerchObsticalCourse_t3x_size))
    svcBreak(USERBREAK_PANIC);
    
  //Load the title Screen from titlescren_t3x
  topMenuSpriteSheet = C2D_SpriteSheetLoadFromMem(SeaPerchTopMenu_t3x, SeaPerchTopMenu_t3x_size);
  if (!topMenuSpriteSheet) svcBreak(USERBREAK_PANIC);
  
    
  topMenuImage = C2D_SpriteSheetGetImage(topMenuSpriteSheet, 0);
  C3D_TexSetFilter(topMenuImage.tex, GPU_NEAREST, GPU_NEAREST);
  
  bottomMenuSpriteSheet = C2D_SpriteSheetLoadFromMem(SeaPerchBottomMenu_t3x, SeaPerchBottomMenu_t3x_size);
  if (!bottomMenuSpriteSheet) svcBreak(USERBREAK_PANIC);
    
  bottomMenuImage = C2D_SpriteSheetGetImage(bottomMenuSpriteSheet, 0);
  
  Mtx_Identity(&flutterCubeOrientation);
  Mtx_Identity(&seaPerchBotOrientation);
  Mtx_RotateX(&seaPerchBotOrientation, C3D_AngleFromDegrees(90.0f), true);
  Mtx_RotateZ(&seaPerchBotOrientation, C3D_AngleFromDegrees(180.0f), true);
  Mtx_Identity(&seaPerchBotPosition);
  
  Mtx_Identity(&obsticalCourseOrientation);
  Mtx_RotateX(&obsticalCourseOrientation, C3D_AngleFromDegrees(-90.0f), true);
  Mtx_RotateZ(&obsticalCourseOrientation, C3D_AngleFromDegrees(90.0f), true);
  Mtx_Identity(&obsticalCoursePosition);
  Mtx_Translate(&obsticalCoursePosition, -0.1f, -10.0f, -40.0f, true);
  
  //creates the an obstical course collider
  //                 z                   x                            y     
  addCourseCollider((-25.0f * -10.0f), ((0.0f *-10.0f) + 100.0f), ((-6.4f*10) + 400.0f),
                    (-25.9f * -10.0f), ((0.6f *-10.0f) + 100.0f), ((-3.4f*10) + 400.0f));
  addCourseCollider((-55.0f * -10.0f), ((0.0f *-10.0f) + 100.0f), ((-6.4f*10) + 400.0f),
                    (-55.9f * -10.0f), ((0.6f *-10.0f) + 100.0f), ((-3.4f*10) + 400.0f));
                    
  // centerX, centerY, centerZ, majorRadius, minorRadius, rotX, rotZ
  addRotatedTorus(0.0f, -5.2f, -10.0f, 2.7f, 0.1f, 90.0f, 0.0f);
  addRotatedTorus(-2.9f, -2.6f, -25.0f, 2.7f, 0.1f, 0.0f, 0.0f);
  addRotatedTorus(0.0f, -3.4f, -40.0f, 2.7f, 0.1f, 90.0f, 45.0f);
  addRotatedTorus(2.6f, -2.6f, -55.0f, 2.7f, 0.1f, 0.0f, 0.0f);
  addRotatedTorus(0.0f, -5.2f, -70.0f, 2.7f, 0.1f, 90.0f, 0.0f);
  //torusColliders[torusColliderCount++] = (Collision3D_Torus){ 0.0f, -5.2f, -10.0f, 2.7f, 0.1f, 1 };
  //torusColliders[torusColliderCount++] = (Collision3D_Torus){ -2.9f, -2.7f, -25.0f, 2.7f, 0.1f, 0 };

  
  Mtx_Identity(&lightOrientation);
  Mtx_RotateY(&lightOrientation, C3D_AngleFromDegrees(0.0f), true); //yaw
  Mtx_RotateX(&lightOrientation, C3D_AngleFromDegrees(70.0f), true); //tilt
  
  Mtx_Identity(&cameraRotationTop);
  Mtx_RotateX(&cameraRotationTop, C3D_AngleFromDegrees(-10.0f), true); //rotates the camera -10 degrees pitch
  //Add RotateY for yaw, or combine both
  
  playLoopingBGM("romfs:/bgm.wav");
}

static void obsticalCourseRenderTop(C3D_RenderTarget* target, const C3D_Mtx* projection) {
  C3D_Mtx botModelView, obsticalCourseModelView;
  C3D_Mtx translationMtx, scaleMtx, tempMtx;
  
  //Sets up lighting in world space
  const Scene3D_Vec4 lightClr           = { 1.0f, 1.0f, 1.0f, 1.0f };
  //Currently uses the default light orientation matrix
  Scene3D_Vec4 lightVec_world     = Vec4_TransformByMtx(&lightOrientation, baseLightDir);
  Scene3D_Vec4 lightHalfVec_world = lightVec_world;

  C3D_FrameDrawOn(target);
  
  //Sets up Camera transforms
  Mtx_Identity(&translationMtx);
  
  if ((seaPerchBotZ <= -72.0f) && !cameraFlipped) {
    cameraFlipped = true;
    Mtx_RotateY(&cameraRotationTop, C3D_AngleFromDegrees (180.0f), true);
    Mtx_Translate(&translationMtx, 0.0, 2.0f, (-seaPerchBotZ + 6.0f), true);
  }
  else if ((seaPerchBotZ >= -6.0f) && cameraFlipped) {
    cameraFlipped = false;
    Mtx_RotateY(&cameraRotationTop, C3D_AngleFromDegrees (180.0f), true);
    Mtx_Translate(&translationMtx, 0.0, 2.0f, (-seaPerchBotZ - 6.0f), true);
  }
  else if (cameraFlipped) {
    Mtx_Translate(&translationMtx, 0.0, 2.0f, (-seaPerchBotZ + 6.0f), true);
  }
  else {
    Mtx_Translate(&translationMtx, 0.0, 2.0f, (-seaPerchBotZ - 6.0f), true);
  }
  //Applies camerea tilt after translating into camera space
  C3D_Mtx viewMtx;
  Mtx_Multiply(&viewMtx, &cameraRotationTop, &translationMtx);
  
  //Builds the bot's world position from it's xyz each frame
  Mtx_Identity(&seaPerchBotPosition);
  Mtx_Translate(&seaPerchBotPosition, seaPerchBotX, seaPerchBotY, seaPerchBotZ, true);
  
  //combines world position and bts rotation to get world transform
  C3D_Mtx botWorldMtx;
  Mtx_Multiply(&botWorldMtx, &seaPerchBotPosition, &seaPerchBotOrientation);
  
  //Applies camera's view on top of bot's world transform
  Mtx_Multiply(&tempMtx, &viewMtx, &botWorldMtx);
  
  //Scales the model if needed and renders it
  Mtx_Identity(&scaleMtx);
  Mtx_Scale(&scaleMtx, 0.1f, 0.1f, 0.1f); //Change these floats to change the scale
  Mtx_Multiply(&botModelView, &tempMtx, &scaleMtx);
  
  C3D_Mtx obsticalCourseWorldMtx;
  Mtx_Multiply(&obsticalCourseWorldMtx, &obsticalCoursePosition, &obsticalCourseOrientation);
  Mtx_Multiply(&tempMtx, &viewMtx, &obsticalCourseWorldMtx);
  Mtx_Multiply(&obsticalCourseModelView, &tempMtx, &scaleMtx);
  
  //converts world space light into the top screen's view space
  Scene3D_Vec4 lightVec_top     = Vec4_TransformByMtx(&cameraRotationTop, lightVec_world);
  Scene3D_Vec4 lightHalfVec_top = Vec4_TransformByMtx(&cameraRotationTop, lightHalfVec_world);
  
  Model3D_Render(&seaPerchBotModel, projection, &botModelView, &material, lightVec_top, lightHalfVec_top, lightClr);
  
  
  Model3D_Render(&obsticalCourseModel, projection, &obsticalCourseModelView, &material, lightVec_top, lightHalfVec_top, lightClr);
  
  if (showDebugger) {
    // 1. Render the AABB course colliders
    for (int i = 0; i < courseColliderCount; i++) {
      const Collision3D_AABB* box = &courseColliders[i];
      
      C3D_Mtx boxWorldMtx, boxModelView, tempBoxMtx, scaleBoxMtx;
      
      float centerX = (box->minX + box->maxX) * 0.5f;
      float centerY = (box->minY + box->maxY) * 0.5f;
      float centerZ = (box->minZ + box->maxZ) * 0.5f;
      
      float sizeX = box->maxX - box->minX;
      float sizeY = box->maxY - box->minY;
      float sizeZ = box->maxZ - box->minZ;
      
      Mtx_Identity(&boxWorldMtx);
      Mtx_Translate(&boxWorldMtx, centerX, centerY, centerZ, true);

      Mtx_Multiply(&tempBoxMtx, &viewMtx, &boxWorldMtx);

      Mtx_Identity(&scaleBoxMtx);
      Mtx_Scale(&scaleBoxMtx, sizeX, sizeY, sizeZ);
      Mtx_Multiply(&boxModelView, &tempBoxMtx, &scaleBoxMtx);

      Model3D_Render(&flutterCubeModel, projection, &boxModelView, &material, lightVec_top, lightHalfVec_top, lightClr);
    }

    // 2. Render the Bot's Hitboxes using locally derived movement vectors
    Scene3D_Vec4 localForward = { 0.0f, 1.0f, 0.0f, 0.0f };
    Scene3D_Vec4 worldMove = Vec4_TransformByMtx(&seaPerchBotOrientation, localForward);
    float fwdX = worldMove.x, fwdZ = worldMove.z;
    float rightX = -fwdZ, rightZ = fwdX;

    for (int b = 0; b < 2; b++) {
      C3D_Mtx boxWorldMtx, boxModelView, tempBoxMtx, scaleBoxMtx;
      
      float boxCenterX = seaPerchBotX + botBoxes[b].fwdOffset * fwdX + botBoxes[b].rightOffset * rightX;
      float boxCenterY = seaPerchBotY + botBoxes[b].upOffset;
      float boxCenterZ = seaPerchBotZ + botBoxes[b].fwdOffset * fwdZ + botBoxes[b].rightOffset * rightZ;

      Mtx_Identity(&boxWorldMtx);
      Mtx_Translate(&boxWorldMtx, boxCenterX, boxCenterY, boxCenterZ, true);

      Mtx_Multiply(&tempBoxMtx, &viewMtx, &boxWorldMtx);

      Mtx_Identity(&scaleBoxMtx);
      Mtx_Scale(&scaleBoxMtx, botBoxes[b].halfRight * 2.0f, botBoxes[b].halfHeight * 2.0f, botBoxes[b].halfFwd * 2.0f);
      Mtx_Multiply(&boxModelView, &tempBoxMtx, &scaleBoxMtx);

      Model3D_Render(&flutterCubeModel, projection, &boxModelView, &material, lightVec_top, lightHalfVec_top, lightClr);
    }
    // 3. Render the Torus Colliders for Debugging
    for (int i = 0; i < torusColliderCount; i++) {
      const Collision3D_Torus* torus = &torusColliders[i];
      
      int segments = 16;
      for (int s = 0; s < segments; s++) {
        float angle = (s / (float)segments) * (2.0f * M_PI);
        float localX = torus->majorRadius * cosf(angle);
        float localZ = torus->majorRadius * sinf(angle);
        float localY = 0.0f;

        // Transform local ring coordinates back to world space using the torus axes
        float cx = torus->centerX + (localX * torus->axisX[0] + localY * torus->axisY[0] + localZ * torus->axisZ[0]);
        float cy = torus->centerY + (localX * torus->axisX[1] + localY * torus->axisY[1] + localZ * torus->axisZ[1]);
        float cz = torus->centerZ + (localX * torus->axisX[2] + localY * torus->axisY[2] + localZ * torus->axisZ[2]);
        
        C3D_Mtx boxWorldMtx, boxModelView, tempBoxMtx, scaleBoxMtx;
        
        Mtx_Identity(&boxWorldMtx);
        Mtx_Translate(&boxWorldMtx, cx, cy, cz, true);

        Mtx_Multiply(&tempBoxMtx, &viewMtx, &boxWorldMtx);

        float tubeSize = torus->minorRadius * 2.0f;
        Mtx_Identity(&scaleBoxMtx);
        Mtx_Scale(&scaleBoxMtx, tubeSize, tubeSize, tubeSize);
        Mtx_Multiply(&boxModelView, &tempBoxMtx, &scaleBoxMtx);

        Model3D_Render(&flutterCubeModel, projection, &boxModelView, &material, lightVec_top, lightHalfVec_top, lightClr);
      }
    }
  }
  
  }
  
static void obsticalCourseRenderBottom(void){
  if (showDebugger) {
    C3D_Mtx modelView;
    C3D_Mtx translationMtx, scaleMtx, tempMtx;
    
    //Sets up lighting in world space
    const Scene3D_Vec4 lightClr           = { 1.0f, 1.0f, 1.0f, 1.0f };
    //Currently uses the default light orientation matrix
    Scene3D_Vec4 lightVec_world     = Vec4_TransformByMtx(&lightOrientation, baseLightDir);
    Scene3D_Vec4 lightHalfVec_world = lightVec_world;
    
    C3D_FrameDrawOn(targetBottom);
    
    //Sets up Camera transforms
    Mtx_Identity(&translationMtx);
    Mtx_Translate(&translationMtx, 0.0, 0.0, -6.0f, true);
    
    Mtx_Multiply(&tempMtx, &translationMtx, &flutterCubeOrientation);
    
    //Scales the model if needed and renders it
    Mtx_Identity(&scaleMtx);
    Mtx_Scale(&scaleMtx, 2.0f, 2.0f, 2.0f); //Change these floats to change the scale
    Mtx_Multiply(&modelView, &tempMtx, &scaleMtx);
    
    Model3D_Render(&flutterCubeModel, &projectionBottom, &modelView, &material, lightVec_world, lightHalfVec_world, lightClr);
  }
}

static void obsticalCourseExit(void) {
  stopBGM();
  //Unload the 3D assets for the obstical course and tear down the shared pipeline
  Model3D_Unload(&flutterCubeModel);
  Model3D_Unload(&seaPerchBotModel);
  Model3D_Unload(&obsticalCourseModel);
  C2D_SpriteSheetFree(topMenuSpriteSheet);
  C2D_SpriteSheetFree(bottomMenuSpriteSheet);
  Scene3D_Exit();
}

int main(int argc, char **argv) {
  //Initializes the graphics system with default settings
  gfxInitDefault();
  
  
  romfsInit();
  
  ndspInit();
  
  //Initializes other libraries
  C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
  C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
  
  g_textBuf = C2D_TextBufNew(256);
  
  //Initialize the top and bottom screen
  targetTopLeft    = C3D_RenderTargetCreate(240, 400, (GPU_COLORBUF)GPU_RGBA8, GPU_RB_DEPTH16);
  targetTopRight    = C3D_RenderTargetCreate(240, 400, (GPU_COLORBUF)GPU_RGBA8, GPU_RB_DEPTH16);
  targetBottom = C3D_RenderTargetCreate(240, 320, (GPU_COLORBUF)GPU_RGBA8, GPU_RB_DEPTH16);
  
  
  C3D_RenderTargetSetOutput(targetTopLeft,    GFX_TOP,    GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
  C3D_RenderTargetSetOutput(targetTopRight,    GFX_TOP,    GFX_RIGHT, DISPLAY_TRANSFER_FLAGS);
  C3D_RenderTargetSetOutput(targetBottom, GFX_BOTTOM, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
  
  //Enables 3d for top screen
  gfxSet3D(true);
  
  //Initializes the obstical Course
  obsticalCourseInit();
  
  //Variable that stores the change in bots movement 
  float moveSpeed = 0.0f;
  //Sets objects' local forward movement direction
  Scene3D_Vec4 localForward = { 0.0f, 1.0f, 0.0f, 0.0f };
  
  //What do you think dingus? it sets the bot turn speed.
  float botTurnSpeed = 0.0f;
  
  //Rise/Sink speed control variable (moveSpeed for up/down)
  float riseSinkSpeed = 0.0f;
  
  //Timer Time variable in miliseconds
  Timer3DS timerTime;
  bool startTimer = false;
  u32 timePassed = 0;
  u32 deciSeconds = 0;
  u32 seconds = 0;
  u32 minutes = 0;
  
  //Main loop
  while (aptMainLoop()) {
    //Sets the world movement change for object movement along local forward using moveSpeed
    Scene3D_Vec4 worldMove = Vec4_TransformByMtx(&seaPerchBotOrientation, localForward);
  
    //Scan all the inputs. This should be done once for each frame
    hidScanInput();
    
    //hidKeysDown returns which buttons have been just pressed that frame
    u32 kDown = hidKeysDown();
    //hidKeysHeld returns which buttons have/are held down in this fram
    u32 kHeld = hidKeysHeld();
    
    if(kDown & KEY_START) break; //Break in order to return to homebrew menu.
    
    if((kHeld & KEY_A) && (kHeld & KEY_B) && (kDown & KEY_SELECT)) {
      showDebugger = !showDebugger;
    }
    
    if ((kDown & KEY_TOUCH) && onMainMenu) {
      onMainMenu = false;
    }
    else if ((kDown & KEY_TOUCH) && !onMainMenu) {
      startTimer = !startTimer;
      timerTime.reset();
      
    }
    if (startTimer) {
      timePassed = timerTime.getElapsedDeciseconds();
      minutes = timePassed / 600;
      seconds = (timePassed % 600) / 10;
      deciSeconds = timePassed % 10;
    }
    
    //===================================================
    //START OF MOVEMENT
    //==================================================
    //Adjust movement speed depending on if up arrow is pressed.
    //WARNING: KEEP THE VALUE YOU LOWER/RAISE moveSpeed BY BE EVENLY DIVISIBLE FROM moveSpeed!!!
    //Forward
    if(((kDown & KEY_UP) || (kHeld & KEY_UP)) && ((kDown & KEY_X) || (kHeld & KEY_X))) { //Checks if both up and x are pressed or held
      if(moveSpeed < 0.05f) moveSpeed = moveSpeed + 0.001f;
    }
    //Backward
    else if(((kDown & KEY_DOWN) || (kHeld & KEY_DOWN)) && ((kDown & KEY_B) || (kHeld & KEY_B))) { //Checks if both down and B are pressed or held
      if(moveSpeed > -0.05f) moveSpeed = moveSpeed - 0.001f;
    }
    //Left
    else if(((kDown & KEY_DOWN) || (kHeld & KEY_DOWN)) && ((kDown & KEY_X) || (kHeld & KEY_X))) { //Checks if x and down are pressed or held
      
      
      if (botTurnSpeed > (M_PI / 180.0f) * -2.0f) botTurnSpeed = botTurnSpeed + (M_PI / 180.0f * -2.0f / 10.0f);
      
    }
    //Right
    else if(((kDown & KEY_UP) || (kHeld & KEY_UP)) && ((kDown & KEY_B) || (kHeld & KEY_B))) { //Checks if up and b are pressed or held
      
      
      if (botTurnSpeed < (M_PI / 180.0f) * 2.0f) botTurnSpeed = botTurnSpeed + (M_PI / 180.0f * 2.0f / 10.0f);
    }
    //Forward and left
    else if((kDown & KEY_X) || (kHeld & KEY_X)) { //Checks if x is pressed or held
      if(moveSpeed < 0.04f) moveSpeed = moveSpeed + 0.001f;
      
      if (botTurnSpeed > (M_PI / 180.0f) * -2.0f) botTurnSpeed = botTurnSpeed + (M_PI / 180.0f * -2.0f / 10.0f);
      
    }
    //Forward and right
    else if((kDown & KEY_UP) || (kHeld & KEY_UP)) { //Checks if up is pressed or held
      if(moveSpeed < 0.04f) moveSpeed = moveSpeed + 0.001f;
      
      if (botTurnSpeed < (M_PI / 180.0f) * 2.0f) botTurnSpeed = botTurnSpeed + (M_PI / 180.0f * 2.0f / 10.0f);
      
    }
    //Backward and left
    else if((kDown & KEY_B) || (kHeld & KEY_B)) { //Checks if b is pressed or held
      if(moveSpeed > -0.05f) moveSpeed = moveSpeed - 0.001f;
      
      if (botTurnSpeed > (M_PI / 180.0f) * -2.0f) botTurnSpeed = botTurnSpeed + (M_PI / 180.0f * -2.0f / 10.0f);
      
    }
    //Backward and right
    else if((kDown & KEY_DOWN) || (kHeld & KEY_DOWN)) { //Checks if Down is pressed or held
      if(moveSpeed > -0.05f) moveSpeed = moveSpeed - 0.001f;
      
      if (botTurnSpeed < (M_PI / 180.0f) * 2.0f) botTurnSpeed = botTurnSpeed + (M_PI / 180.0f * 2.0f / 10.0f); 
    }
    
    
    //Handles inertia after keys are released
    if(moveSpeed > 0.0f) {
      moveSpeed = moveSpeed - 0.0006f;
      if(moveSpeed < 0.0f) moveSpeed = 0.0f;//prevents drifting in oposite dirrection
    }
    else if(moveSpeed < 0.0f) {
      moveSpeed = moveSpeed + 0.0006f;
      if(moveSpeed > 0.0f) moveSpeed = 0.0f;//prevents drifting in oposite dirrection
    }
    
    
    //handles bot inertia after turning
    if (botTurnSpeed > 0.0f) {
      botTurnSpeed = botTurnSpeed - (M_PI / 180.0f * 2.0f / 40.0f);
      if (botTurnSpeed < 0.0f) botTurnSpeed = 0.0f;
    }
    else if (botTurnSpeed < 0.0f) {
      botTurnSpeed = botTurnSpeed + (M_PI / 180.0f * 2.0f / 40.0f);
      if (botTurnSpeed > 0.0f) botTurnSpeed = 0.0f;
    }
    
    //Handles bot up and down
    if((kDown & KEY_L) || (kHeld & KEY_L)) {
      if(riseSinkSpeed > -0.025f) riseSinkSpeed = riseSinkSpeed - 0.001f;
    }
    else if((kDown & KEY_R) || (kHeld & KEY_R)) {
      if(riseSinkSpeed < 0.025f) riseSinkSpeed = riseSinkSpeed + 0.001f;
    }
    //Handles inertia after keys are released
    else if(riseSinkSpeed > 0.002f) {
      riseSinkSpeed = riseSinkSpeed - 0.0008f;
      if(riseSinkSpeed < 0.002f) riseSinkSpeed = 0.002f;//prevents drifting in oposite dirrection
    }
    else if(riseSinkSpeed < 0.002f) {
      riseSinkSpeed = riseSinkSpeed + 0.0008f;
      if(riseSinkSpeed > 0.002f) riseSinkSpeed = 0.002f;//prevents drifting in oposite dirrection
    }
    //=======================================================
    //END OF MOVEMENT
    //=======================================================
    
    
    //sets up variables to check if the bot is going to collide with something
    float nextX = seaPerchBotX + worldMove.x * moveSpeed;
    float nextY = seaPerchBotY + riseSinkSpeed;
    float nextZ = seaPerchBotZ + worldMove.z * moveSpeed;
    float fwdX = worldMove.x, fwdZ = worldMove.z;
    float rightX = -fwdZ, rightZ = fwdX;
    
    bool collided = false;
    for (int b = 0; b < 2 && !collided; b++) {
      Collision3D_OBB2D obb;
      obb.centerX = nextX + botBoxes[b].fwdOffset * fwdX + botBoxes[b].rightOffset * rightX;
      obb.centerZ = nextZ + botBoxes[b].fwdOffset * fwdZ + botBoxes[b].rightOffset * rightZ;
      obb.centerY = nextY + botBoxes[b].upOffset;
      obb.forwardX = fwdX;
      obb.forwardZ = fwdZ;
      obb.halfForward = botBoxes[b].halfFwd;
      obb.halfRight   = botBoxes[b].halfRight;
      obb.halfHeight  = botBoxes[b].halfHeight;
      
      for (int i = 0; i < courseColliderCount; i++) {
        if (Collision3D_OBB2DIntersectsAABB(&obb, &courseColliders[i])) {
          collided = true;
          break;
        }
      }
      
      for (int i = 0; i < torusColliderCount; i++) {
        if (Collision3D_OBB2DIntersectsTorus(&obb, &torusColliders[i])) {
          collided = true;
          break;
        }
      }
    }
    if (collided) {
      moveSpeed = -moveSpeed;
      botTurnSpeed = 0.0f;
    } else {
      seaPerchBotX = nextX;
      seaPerchBotZ = nextZ;
    }
    bool vertCollided = false;
    for (int b = 0; b < 2 && !vertCollided; b++) {
      Collision3D_OBB2D obb;
      obb.centerX = seaPerchBotX + botBoxes[b].fwdOffset * fwdX + botBoxes[b].rightOffset * rightX;
      obb.centerZ = seaPerchBotZ + botBoxes[b].fwdOffset * fwdZ + botBoxes[b].rightOffset * rightZ;
      obb.centerY = nextY + botBoxes[b].upOffset; // Test new Y position
      obb.forwardX = fwdX;
      obb.forwardZ = fwdZ;
      obb.halfForward = botBoxes[b].halfFwd;
      obb.halfRight   = botBoxes[b].halfRight;
      obb.halfHeight  = botBoxes[b].halfHeight;
      
      // Check AABB
      for (int i = 0; i < courseColliderCount; i++) {
        if (Collision3D_OBB2DIntersectsAABB(&obb, &courseColliders[i])) {
          vertCollided = true;
          break;
        }
      }
      // Check Torus
      for (int i = 0; i < torusColliderCount; i++) {
        if (Collision3D_OBB2DIntersectsTorus(&obb, &torusColliders[i])) {
          vertCollided = true;
          break;
        }
      }
    }

    if (vertCollided) {
      riseSinkSpeed = 0.0f; // Kill vertical momentum/speed
      // seaPerchBotY remains unchanged, preventing it from going up/down into the obstacle
    } else {
      seaPerchBotY = nextY;
    }
    
    //Applies rotational speed that was calculated to the bot.
    //Also applies the speed and movement to the bot
    C3D_Mtx tempRotY, newOrientation;
    
    Mtx_Identity(&tempRotY);
    
    Mtx_RotateY(&tempRotY, botTurnSpeed, true);
    
    Mtx_Multiply(&newOrientation, &tempRotY, &seaPerchBotOrientation);
    seaPerchBotOrientation = newOrientation;
    
    
    //==========================================
    //Read slider state and render the scene
    //==========================================
    //gets slider state and calculates interocular distance, iod.
    float slider3D = osGet3DSliderState();
    float iod = slider3D / 4.0f;
    
    //Sets the focal distance where rendered geometry sits exactly at the screen
    float screenDepth = 6.0f;
    
    C3D_Mtx projectionTopLeft, projectionTopRight;
    Mtx_PerspStereoTilt(&projectionTopLeft, C3D_AngleFromDegrees(80.0f), C3D_AspectRatioTop,
                        0.01f, 1000.0f,  iod, screenDepth, false);
    Mtx_PerspStereoTilt(&projectionTopRight, C3D_AngleFromDegrees(80.0f), C3D_AspectRatioTop,
                        0.01f, 1000.0f, -iod, screenDepth, false);
    
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
      
    //Clears top and bottom screen
    C3D_RenderTargetClear(targetTopLeft,    C3D_CLEAR_ALL, CLEAR_COLOR, 0);
    C3D_RenderTargetClear(targetTopRight,    C3D_CLEAR_ALL, CLEAR_COLOR, 0);
    C3D_RenderTargetClear(targetBottom, C3D_CLEAR_ALL, CLEAR_COLOR, 0);
        
    if (!onMainMenu){
      obsticalCourseRenderTop(targetTopLeft, &projectionTopLeft);
      if (slider3D > 0.0f) {
        obsticalCourseRenderTop(targetTopRight, &projectionTopRight);
      }
      obsticalCourseRenderBottom();
    }
    else {
      C2D_TextBufClear(g_textBuf);
  
      C2D_SceneBegin(targetTopLeft);
      C2D_TargetClear(targetTopLeft, 0x00fff060);
      
      
      C2D_Prepare();
      
      //draw the title screen startin at the top left of top screen
      C2D_DrawImageAt(topMenuImage, 0.0f, 0.0f, 0.5f, NULL, 1.0f, 1.0f);
      
      
      C2D_SceneBegin(targetBottom);
      C2D_TargetClear(targetBottom, 0x00fff060);
      
      C2D_DrawImageAt(bottomMenuImage, 0.0f, 0.0f, 0.5f, NULL, 1.0f, 1.0f);
    }
    if (showDebugger) {
      //renders the debugger text on the bottom screen
      runDebuggerText(g_textBuf, &g_statsText, targetBottom);C2D_TextBufClear(g_textBuf);
  
      C2D_SceneBegin(targetBottom);
      
      C2D_Prepare();
      char buf[256];
      
      snprintf(buf, sizeof(buf), 
             audioStatus);
               
      // Parse the raw char array string into a Citro2D hardware string texture
      C2D_TextParse(&g_statsText, g_textBuf, buf);
      C2D_TextOptimize(&g_statsText);
      C2D_DrawText(&g_statsText, C2D_WithColor, 0.0f,100.0f, 0.5f, 0.6f, 0.6f, C2D_Color32(0, 0, 0, 255));
      
    }
    else if (!onMainMenu) {
      C2D_SceneBegin(targetBottom);
      C2D_TargetClear(targetBottom, 0x00fff060);
      
      C2D_TextBufClear(g_textBuf);
      C2D_Prepare();
      char buf[256];
      
      snprintf(buf, sizeof(buf), 
             "      Press start to quit\n"
             "Touch screen to start/stop\n"
             "timer.\n");
               
      // Parse the raw char array string into a Citro2D hardware string texture
      C2D_TextParse(&g_statsText, g_textBuf, buf);
      C2D_TextOptimize(&g_statsText);
      C2D_DrawText(&g_statsText, C2D_WithColor, 25.0f,20.0f, 0.5f, 0.8f, 0.8f, C2D_Color32(0, 0, 0, 255));
      
      C2D_TextBufClear(g_textBuf);
      C2D_Prepare();
      
      snprintf(buf, sizeof(buf), 
             "Time: %lu:%lu:%lu\n", minutes, seconds, deciSeconds);
               
      // Parse the raw char array string into a Citro2D hardware string texture
      C2D_TextParse(&g_statsText, g_textBuf, buf);
      C2D_TextOptimize(&g_statsText);
      C2D_DrawText(&g_statsText, C2D_WithColor, 25.0f,110.0f, 0.5f, 1.0f, 1.0f, C2D_Color32(0, 0, 0, 255));
    }
    
    
    C3D_FrameEnd(0);
    }
  obsticalCourseExit();
    
  //Free memory targets
  C3D_RenderTargetDelete(targetTopLeft);
  C3D_RenderTargetDelete(targetTopRight);
  C3D_RenderTargetDelete(targetBottom);
  C2D_TextBufDelete(g_textBuf);
  C2D_Fini();

  //Deinitialize the graphics
  ndspExit();
  C3D_Fini();
  gfxExit();
  romfsExit();
  return 0;
}
