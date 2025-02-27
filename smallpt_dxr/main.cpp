#include <stdio.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <dxcapi.h>
#include <DirectXMath.h>

#include "fsquad.hpp"

FullScreenQuad g_fsquad;

IDxcBlob* CompileShaderLibrary(LPCWSTR fileName);  // compile_shader_library.cpp
void SetSceneAndCreateAS(int);

GLFWwindow* g_window;
int WIN_W = 800, WIN_H = 600;
int RT_W = 800, RT_H = 600;
#ifdef _DEBUG
bool g_use_debug_layer{ true };
#else
bool g_use_debug_layer{ false };
#endif
ID3D12Device5*   g_device12;
IDXGIFactory4*   g_factory;
IDXGISwapChain3* g_swapchain;

ID3D12Fence* g_fence;
int          g_fence_value;
HANDLE       g_fence_event;
int          g_frame_index;

ID3D12CommandQueue*     g_command_queue;
ID3D12CommandAllocator* g_command_allocator;

ID3D12GraphicsCommandList4* g_command_list;

const int FRAME_COUNT = 2;
ID3D12DescriptorHeap* g_rtv_heap;
ID3D12DescriptorHeap* g_srv_uav_cbv_heap;
int                   g_srv_uav_cbv_descriptor_size;
int                   g_rtv_descriptor_size;
ID3D12Resource*       g_rendertargets[FRAME_COUNT];

ID3D12RootSignature* g_global_rootsig{};
ID3D12StateObject*   g_rt_state_object{};
ID3D12StateObjectProperties* g_rt_state_object_props;
ID3D12Resource*      g_rt_output_resource;
ID3D12Resource*      g_rt_scratch_resource;

ID3D12Resource*      g_sbt_storage{};

ID3D12Resource*      d_raygen_cb;
ID3D12Resource*      d_fsquad_cb;

int                  g_frame_count{ 0 };

std::vector<char> g_axes(3);  // XYZ
glm::vec3 g_cam_pos = { 50, 52, 295.6 };
//glm::vec3 g_cam_pos = { 50.0f, 40.8f, -860.0f };
glm::vec3 g_cam_dir = glm::normalize(glm::vec3(0, -0.042612, -1));
glm::vec3 g_cx = { WIN_W * 0.5135f / WIN_H, 0, 0 };
glm::vec3 g_cy = glm::normalize(glm::cross(g_cx, g_cam_dir)) * 0.5135f;
int g_recursion_depth = 5;
int g_nsamp = 1;

struct RayGenCB {
  int frame_count;
  glm::vec3 cam_pos;
  glm::vec3 cam_dir; float pad1;
  glm::vec3 cx; float pad2;
  glm::vec3 cy; float pad3;
  int recursion_depth;
  int nsamp;
};

struct FSQuadCB {
  int frame_count;
};

struct SphereInfo {
  float radius;
  glm::vec3 pos;
  glm::vec3 emission;
  glm::vec3 color;
  int material;
};

struct SceneInfo {
  int recursion_depth;
  std::vector<SphereInfo> spheres;
};

glm::vec3 Cen(50.0f, 40.8f, -860.0f);
std::vector<SceneInfo> g_scenes = {
  { // Cornell Box
    5,
    {
      {1e5, glm::vec3(1e5 + 1,40.8,81.6),   glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.75, 0.25, 0.25), 0 },
      {1e5, glm::vec3(-1e5 + 99,40.8,81.6), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.25, 0.25, 0.75), 0 },
      {1e5, glm::vec3(50,40.8, 1e5),        glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.75, 0.75, 0.75), 0},
      {1e5, glm::vec3(50,40.8,-1e5 + 370),  glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0, 0, 0), 0 },
      {1e5, glm::vec3(50, 1e5, 81.6),       glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.75, 0.75, 0.75), 0 },
      {1e5, glm::vec3(50,-1e5 + 81.6,81.6), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.75, 0.75, 0.75), 0 },
      {16.5,glm::vec3(27,16.5,47),          glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.999, 0.999, 0.999), 1 },
      {16.5,glm::vec3(73,16.5,78),          glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.999, 0.999, 0.999), 2 },
      {600, glm::vec3(50,681.6 - .27,81.6), glm::vec3(12,12,12),         glm::vec3(0,0,0), 0 }
    }
  },
  { // Sky
    5,
    {
      {1600, glm::vec3(1,0,2) * 3000.0f, glm::vec3(1.0f,.9,.8) * 1.2e1f * 1.56f * 2.0f, glm::vec3(0), 0}, // sun
      {1560, glm::vec3(1,0,2) * 3500.0f, glm::vec3(1.0f,.5,.05) * 4.8e1f * 1.56f * 2.0f, glm::vec3(0),  0}, // horizon sun2
      
      {10000, Cen + glm::vec3(0,0,-200), glm::vec3(0.00063842, 0.02001478, 0.28923243), glm::vec3(.7,.7,1) * .25f,  0}, // sky

      {100000, glm::vec3(50, -100000, 0), glm::vec3(), glm::vec3(.3,.3,.3), 0}, // grnd
      {110000, glm::vec3(50, -110048.5, 0),  glm::vec3(.9,.5,.05) * 4.0f, glm::vec3(), 0},// horizon brightener
      {4e4, glm::vec3(50, -4e4 - 30, -3000),  glm::vec3(),glm::vec3(.2,.2,.2),0},// mountains

      {26.5, glm::vec3(22,26.5,42),   glm::vec3(),glm::vec3(1,1,1) * .596f, 1}, // white Mirr
      {13, glm::vec3(75,13,82),   glm::vec3(),glm::vec3(.96,.96,.96) * .96f, 2},// Glas
      {22, glm::vec3(87,22,24),   glm::vec3(),glm::vec3(.6,.6,.6) * .696f, 2}    // Glas2
    }
  },
  {  // Night Sky
    4,
    {
      {2.5e3,   glm::vec3(.82,.92,-2) * 1e4f,    glm::vec3(1,1,1) * .8e2f,     glm::vec3(), 0}, // moon
      {2.5e4, glm::vec3(50, 0, 0),  glm::vec3(0.114, 0.133, 0.212) * 1e-2f,  glm::vec3(.216,.384,1) * 0.003f, 0}, // sky
      {5e0,   glm::vec3(-.2,0.16,-1) * 1e4f, glm::vec3(1.00, 0.843, 0.698) * 1e2f,   glm::vec3(), 0},  // star
      {5e0,   glm::vec3(0,  0.18,-1) * 1e4f, glm::vec3(1.00, 0.851, 0.710) * 1e2f,  glm::vec3(), 0},  // star
      {5e0,   glm::vec3(.3, 0.15,-1) * 1e4f, glm::vec3(0.671, 0.780, 1.00) * 1e2f,   glm::vec3(), 0},  // star
      {3.5e4,   glm::vec3(600,-3.5e4 + 1, 300), glm::vec3(),   glm::vec3(.6,.8,1) * .01f,  1},   //pool
      {5e4,   glm::vec3(-500,-5e4 + 0, 0),   glm::vec3(),      glm::vec3(1,1,1) * .35f,  0},    //hill
      {16.5,  glm::vec3(27,0,47),         glm::vec3(),              glm::vec3(1,1,1) * .33f, 0}, //hut
      {7,     glm::vec3(27 + 8 * sqrt(2),0,47 + 8 * sqrt(2)),glm::vec3(),  glm::vec3(1,1,1) * .33f,  0}, //door
      {500,   glm::vec3(-1e3,-300,-3e3), glm::vec3(),  glm::vec3(1,1,1) * .351f,    0},  //mnt
      {830,   glm::vec3(0,   -500,-3e3), glm::vec3(),  glm::vec3(1,1,1) * .354f,    0},  //mnt
      {490,  glm::vec3(1e3,  -300,-3e3), glm::vec3(),  glm::vec3(1,1,1) * .352f,    0},  //mnt
    }
  },
  {  // Island
    4,
    {
      {160,  Cen + glm::vec3(0, 600, -500),glm::vec3(1,1,1) * 2e2f, glm::vec3(),  0}, // sun
      {800, Cen + glm::vec3(0,-880,-9120),glm::vec3(1,1,1) * 2e1f, glm::vec3(),  0}, // horizon
      {10000,Cen + glm::vec3(0,0,-200), glm::vec3(0.0627, 0.188, 0.569) * 1e0f, glm::vec3(1,1,1) * .4f,  0}, // sky
      {800, Cen + glm::vec3(0,-720,-200),glm::vec3(),  glm::vec3(0.110, 0.898, 1.00) * .996f,  1}, // water
      {790, Cen + glm::vec3(0,-720,-200),glm::vec3(),  glm::vec3(.4,.3,.04) * .6f,    0}, // earth
      {325, Cen + glm::vec3(0,-255,-50), glm::vec3(),  glm::vec3(.4,.3,.04) * .8f,       0}, // island
      {275, Cen + glm::vec3(0,-205,-33), glm::vec3(),  glm::vec3(.02,.3,.02) * .75f,      0}, // grass
    }
  },
  {  // Vista
    5,
    {
      {8000, Cen + glm::vec3(0,-8000,-900),glm::vec3(1,.4,.1) * 5e-1f, glm::vec3(),  0}, // sun
      {1e4,  Cen + glm::vec3(), glm::vec3(0.631, 0.753, 1.00) * 3e-1f, glm::vec3(1,1,1) * .5f,  0}, // sky

      {150,  Cen + glm::vec3(-350,0, -100),glm::vec3(),  glm::vec3(1,1,1) * .3f,  0}, // mnt
      {200,  Cen + glm::vec3(-210,0,-100), glm::vec3(),  glm::vec3(1,1,1) * .3f,  0}, // mnt
      {145,  Cen + glm::vec3(-210,85,-100),glm::vec3(),  glm::vec3(1,1,1) * .8f,  0}, // snow
      {150,  Cen + glm::vec3(-50,0,-100),  glm::vec3(),  glm::vec3(1,1,1) * .3f,  0}, // mnt
      {150,  Cen + glm::vec3(100,0,-100),  glm::vec3(),  glm::vec3(1,1,1) * .3f,  0}, // mnt
      {125,  Cen + glm::vec3(250,0,-100),  glm::vec3(),  glm::vec3(1,1,1) * .3f,  0}, // mnt
      {150,  Cen + glm::vec3(375,0,-100),  glm::vec3(),  glm::vec3(1,1,1) * .3f,  0}, // mnt

      {2500, Cen + glm::vec3(0,-2400,-500),glm::vec3(),  glm::vec3(1,1,1) * .1f,  0}, // mnt base

      {8000, Cen + glm::vec3(0,-8000,200), glm::vec3(),  glm::vec3(.2,.2,1),    2}, // water
      {8000, Cen + glm::vec3(0,-8000,1100),glm::vec3(),  glm::vec3(0,.3,0),     0}, // grass
      {8   , Cen + glm::vec3(-75, -5, 850),glm::vec3(),  glm::vec3(0,.3,0),     0}, // bush
      {7,    Cen + glm::vec3(-14,   23, 825),glm::vec3(),  glm::vec3(1,1,1) * .996f, 2}, // ball

      {30,  Cen + glm::vec3(200,280,-400),  glm::vec3(),  glm::vec3(1,1,1) * .8f,  0},   // clouds
      {37,  Cen + glm::vec3(237,280,-400),  glm::vec3(),  glm::vec3(1,1,1) * .8f,  0},   // clouds
      {28,  Cen + glm::vec3(267,280,-400),  glm::vec3(),  glm::vec3(1,1,1) * .8f,  0},   // clouds

      {40,  Cen + glm::vec3(150,280,-1000),  glm::vec3(),  glm::vec3(1,1,1) * .8f,  0},  // clouds
      {37,  Cen + glm::vec3(187,280,-1000),  glm::vec3(),  glm::vec3(1,1,1) * .8f,  0},  // clouds

      {40,  Cen + glm::vec3(600,280,-1100),  glm::vec3(),  glm::vec3(1,1,1) * .8f,  0},  // clouds
      {37,  Cen + glm::vec3(637,280,-1100),  glm::vec3(),  glm::vec3(1,1,1) * .8f,  0},  // clouds

      {37,  Cen + glm::vec3(-800,280,-1400),  glm::vec3(),  glm::vec3(1,1,1) * .8f,  0}, // clouds
      {37,  Cen + glm::vec3(0,280,-1600),  glm::vec3(),  glm::vec3(1,1,1) * .8f,  0},    // clouds
      {37,  Cen + glm::vec3(537,280,-1800),  glm::vec3(),  glm::vec3(1,1,1) * .8f,  0},  // clouds
    }
  },
  {  // overlap --- needs to increase recursion depth
    50,
    {
      {150, glm::vec3(50 + 75,28,62), glm::vec3(1,1,1) * 0e-3f, glm::vec3(1,.9,.8) * .93f, 2},
      {28,  glm::vec3(50 + 5,-28,62), glm::vec3(1,1,1) * 1e1f, glm::vec3(1,1,1) * 0.0f, 0},
      {300, glm::vec3(50,28,62), glm::vec3(1,1,1) * 0e-3f, glm::vec3(1,1,1) * .93f, 1}
    }
  },
  {  // Wada
    50,
    {
      #define R 60.0f
      #define T (30 * 3.1415926 / 180.0f)
      #define D (float(R / cos(T)))
      #define Z 60.0f
      {1e5, glm::vec3(50, 100, 0),      glm::vec3(1,1,1) * 3e0f, glm::vec3(), 0}, // sky
      {1e5, glm::vec3(50, -1e5 - D - R, 0), glm::vec3(),     glm::vec3(.1,.1,.1), 0},           //grnd

      {R, glm::vec3(50,40.8,62) + glm::vec3(cos(T),sin(T),0) * D, glm::vec3(), glm::vec3(1,.3,.3) * .999f, 1}, //red
      {R, glm::vec3(50,40.8,62) + glm::vec3(-cos(T),sin(T),0) * D, glm::vec3(), glm::vec3(.3,1,.3) * .999f, 1}, //grn
      {R, glm::vec3(50,40.8,62) + glm::vec3(0,-1,0) * D,         glm::vec3(), glm::vec3(.3,.3,1) * .999f, 1}, //blue
      {R, glm::vec3(50,40.8,62) + glm::vec3(0,0,-1) * D,       glm::vec3(), glm::vec3(.53,.53,.53) * .999f, 1}, //back
      {R, glm::vec3(50,40.8,62) + glm::vec3(0,0,1) * D,      glm::vec3(), glm::vec3(1,1,1) * .999f, 2}, //front
      #undef R
      #undef T
      #undef D
      #undef Z
    }
  },
  {  // Wada2
    30,
    {
      #define R 120.0f
      #define T 30 * 3.1415926 / 180.0f
      #define D (float(R / cos(T)))
      #define Z 62
      #define C glm::vec3(0.275, 0.612, 0.949)
      {R, glm::vec3(50,28,Z) + glm::vec3(cos(T),sin(T),0) * D,    C * 6e-2f,glm::vec3(1,1,1) * .996f, 1}, //red
      {R, glm::vec3(50,28,Z) + glm::vec3(-cos(T),sin(T),0) * D,    C * 6e-2f,glm::vec3(1,1,1) * .996f, 1}, //grn
      {R, glm::vec3(50,28,Z) + glm::vec3(0,-1,0) * D,              C * 6e-2f,glm::vec3(1,1,1) * .996f, 1}, //blue
      {R, glm::vec3(50,28,Z) + glm::vec3(0,0,-1) * R * 2.0f * sqrtf(2. / 3.),C * 0e-2f,glm::vec3(1,1,1) * .996f, 1}, //back
      {2*2*R*2*sqrtf(2. / 3.) - R*2*sqrtf(2./3.)/3.f, glm::vec3(50,28,Z) + glm::vec3(0,0,-R * 2 * sqrt(2. / 3.) / 3.),   glm::vec3(1,1,1) * .0f,glm::vec3(1,1,1) * .5f, 1} //front
      #undef R
      #undef T
      #undef D
      #undef Z
      #undef C
    }
  },
  {  // Forest
    30,
    {
      #define tc glm::vec3(0.0588, 0.361, 0.0941)
      #define sc (glm::vec3(1,1,1)*.7f)
      {1e5, glm::vec3(50, 1e5 + 130, 0),  glm::vec3(1,1,1) * 1.3f,glm::vec3(),0}, //lite
      {1e2, glm::vec3(50, -1e2 + 2, 47),  glm::vec3(),glm::vec3(1,1,1) * .7f,0}, //grnd

      {1e4, glm::vec3(50, -30, 300) + glm::vec3(-sin(50 * 3.14159f / 180), 0, cos(50 * 3.14159f / 180)) * 1e4f,  glm::vec3(), glm::vec3(1,1,1) * .99f, 1},// mirr L
      {1e4, glm::vec3(50, -30, 300) + glm::vec3( sin(50 * 3.14159f / 180), 0, cos(50 * 3.14159f / 180)) * 1e4f,  glm::vec3(), glm::vec3(1,1,1) * .99f, 1},// mirr R
      {1e4, glm::vec3(50, -30, -50) + glm::vec3(-sin(30 * 3.14159f / 180), 0,-cos(30 * 3.14159f / 180)) * 1e4f,  glm::vec3(), glm::vec3(1,1,1) * .99f, 1},// mirr FL
      {1e4, glm::vec3(50, -30, -50) + glm::vec3( sin(30 * 3.14159f / 180), 0,-cos(30 * 3.14159f / 180)) * 1e4f,  glm::vec3(), glm::vec3(1,1,1) * .99f, 1},// mirr

      {4, glm::vec3(50,6 * .6,47),   glm::vec3(),glm::vec3(.13,.066,.033), 0},//"tree"
      {16,glm::vec3(50,6 * 2 + 16 * .6,47),   glm::vec3(), tc,  0},//"tree"
      {11,glm::vec3(50,6 * 2 + 16 * .6 * 2 + 11 * .6,47),   glm::vec3(), tc,  0},//"tree"
      {7, glm::vec3(50,6 * 2 + 16 * .6 * 2 + 11 * .6 * 2 + 7 * .6,47),   glm::vec3(), tc,  0},//"tree"

      {15.5,glm::vec3(50,1.8 + 6 * 2 + 16 * .6,47),   glm::vec3(), sc,  0},//"tree"
      {10.5,glm::vec3(50,1.8 + 6 * 2 + 16 * .6 * 2 + 11 * .6,47),   glm::vec3(), sc,  0},//"tree"
      {6.5, glm::vec3(50,1.8 + 6 * 2 + 16 * .6 * 2 + 11 * .6 * 2 + 7 * .6,47),   glm::vec3(), sc,  0},//"tree"
      #undef tc
      #undef sc
    }
  }
};

void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
  if (action == GLFW_PRESS)
  {
    switch (key) {
    case GLFW_KEY_ESCAPE: {
      glfwTerminate();
      exit(0);
      break;
    }
    case GLFW_KEY_W: g_axes[1] = 1; break;
    case GLFW_KEY_S: g_axes[1] = -1; break;
    case GLFW_KEY_D: g_axes[0] = 1; break;
    case GLFW_KEY_A: g_axes[0] = -1; break;
    case GLFW_KEY_Q: g_axes[2] = -1; break;
    case GLFW_KEY_E: g_axes[2] = 1; break;
    case GLFW_KEY_1: 
    case GLFW_KEY_2: 
    case GLFW_KEY_3: 
    case GLFW_KEY_4:
    case GLFW_KEY_5:
    case GLFW_KEY_6:
    case GLFW_KEY_7:
    case GLFW_KEY_8:
    case GLFW_KEY_9: SetSceneAndCreateAS(key - GLFW_KEY_1); break;
    case GLFW_KEY_SPACE: g_nsamp = 10; break;
    default: break;
    }
  }
  else if (action == GLFW_RELEASE) {
    switch (key) {
    case GLFW_KEY_W: g_axes[1] = 0; break;
    case GLFW_KEY_S: g_axes[1] = 0; break;
    case GLFW_KEY_D: g_axes[0] = 0; break;
    case GLFW_KEY_A: g_axes[0] = 0; break;
    case GLFW_KEY_Q: g_axes[2] = 0; break;
    case GLFW_KEY_E: g_axes[2] = 0; break;
    case GLFW_KEY_SPACE: g_nsamp = 1; break;
    }
  }
}

void CreateMyGLFWWindow() {
  AllocConsole();
  freopen_s((FILE**)stdin, "CONIN$", "r", stderr);
  freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
  freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);

  if (!glfwInit())
  {
    printf("GLFW initialization failed\n");
  }
  printf("GLFW inited.\n");
  GLFWmonitor* primary_monitor = glfwGetPrimaryMonitor();
  const GLFWvidmode* video_mode = glfwGetVideoMode(primary_monitor);
  printf("Video mode of primary monitor is %dx%d\n", video_mode->width, video_mode->height);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  g_window = glfwCreateWindow(WIN_W, WIN_H, "SmallPT DXR", nullptr, nullptr);

  glfwSetKeyCallback(g_window, KeyCallback);
  glfwSetWindowSizeLimits(g_window, 64, 64, GLFW_DONT_CARE, GLFW_DONT_CARE);
}

// CE = Check Error
#define CE(call) \
{ \
  HRESULT x = (call); \
  if (FAILED(x)) \
  { \
    printf("ERROR: %X line %d\n", x, __LINE__); \
    throw std::exception(); \
  } \
}

void WaitForPreviousFrame()
{
  int val = g_fence_value++;
  CE(g_command_queue->Signal(g_fence, val));
  if (g_fence->GetCompletedValue() < val)
  {
    CE(g_fence->SetEventOnCompletion(val, g_fence_event));
    CE(WaitForSingleObject(g_fence_event, INFINITE));
  }
  g_frame_index = g_swapchain->GetCurrentBackBufferIndex();
}

void InitDeviceAndCommandQ() {
  unsigned dxgi_factory_flags{ 0 };
  if (g_use_debug_layer) {
    ID3D12Debug* debug_controller{};
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller))))
    {
      debug_controller->EnableDebugLayer();
      dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
      printf("Enabling DX12 debugging layer.\n");

      ID3D12Debug1* debug_controller1{};
      debug_controller->QueryInterface(IID_PPV_ARGS(&debug_controller1));
      if (debug_controller1)
      {
        printf("Enabling GPU-based validation.\n");
        debug_controller1->SetEnableGPUBasedValidation(true);
      }
    }
  }

  CE(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&g_factory)));
  IDXGIAdapter1* adapter;
  for (int i = 0; g_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++)
  {
    DXGI_ADAPTER_DESC1 desc;
    adapter->GetDesc1(&desc);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
      continue;
    else
    {
      CE(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&g_device12)));
      printf("Created device = %ls\n", desc.Description);
      break;
    }
  }

  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5;
  CE(g_device12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)));
  if (options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0)
  {
    printf("This device supports DXR 1.0.\n");
  }
  if (options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1)
  {
    printf("This device supports DXR 1.1.\n");
  }

  D3D12_COMMAND_QUEUE_DESC qdesc{};
  qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  qdesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  CE(g_device12->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&g_command_queue)));
  CE(g_device12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)));
  g_fence_value = 1;
  g_fence_event = CreateEvent(nullptr, false, false, L"Fence");
}

void InitSwapChain() {
  DXGI_SWAP_CHAIN_DESC1 scd{};
  scd.BufferCount = FRAME_COUNT;
  scd.Width = WIN_W;
  scd.Height = WIN_H;
  scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  scd.SampleDesc.Count = 1;
  CE(g_factory->CreateSwapChainForHwnd(g_command_queue, glfwGetWin32Window(g_window), &scd, nullptr, nullptr, (IDXGISwapChain1**)&g_swapchain));
  printf("Created swapchain.\n");

  // RTV heap
  D3D12_DESCRIPTOR_HEAP_DESC dhd{};
  dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  dhd.NumDescriptors = FRAME_COUNT;
  dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  CE(g_device12->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&g_rtv_heap)));

  g_rtv_descriptor_size = g_device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = g_rtv_heap->GetCPUDescriptorHandleForHeapStart();
  for (int i = 0; i < FRAME_COUNT; i++)
  {
    CE(g_swapchain->GetBuffer(i, IID_PPV_ARGS(&g_rendertargets[i])));
    g_device12->CreateRenderTargetView(g_rendertargets[i], nullptr, rtv_handle);
    rtv_handle.ptr += g_rtv_descriptor_size;
  }
  printf("Created backbuffers' RTVs\n");

  CE(g_factory->MakeWindowAssociation(glfwGetWin32Window(g_window), DXGI_MWA_NO_ALT_ENTER));
}

void InitDX12Stuff() {
  CE(g_device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_command_allocator)));
  CE(g_device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_command_allocator, nullptr, IID_PPV_ARGS(&g_command_list)));
  g_command_list->Close();

  // CBV SRV UAV Heap
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
  heap_desc.NumDescriptors = 8;  // [0]=output, [1]=BVH, [2]=CBV, [3]=Verts, [4]=Offsets, [5]=HitNormal, [6]=Mapping, [7]=Dirs
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  CE(g_device12->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&g_srv_uav_cbv_heap)));
  g_srv_uav_cbv_heap->SetName(L"SRV UAV CBV heap");
  g_srv_uav_cbv_descriptor_size = g_device12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  // RT Output Rsrc
  D3D12_RESOURCE_DESC desc{};
  desc.DepthOrArraySize = 1;
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  desc.Width = RT_W;
  desc.Height = RT_H;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  D3D12_HEAP_PROPERTIES props{};
  props.Type = D3D12_HEAP_TYPE_DEFAULT;
  props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  props.CreationNodeMask = 1;
  props.VisibleNodeMask = 1;
  CE(g_device12->CreateCommittedResource(
    &props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&g_rt_output_resource)));
  g_rt_output_resource->SetName(L"RT output resource");

  // UAV
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  D3D12_CPU_DESCRIPTOR_HANDLE      handle(g_srv_uav_cbv_heap->GetCPUDescriptorHandleForHeapStart());
  g_device12->CreateUnorderedAccessView(g_rt_output_resource, nullptr, &uav_desc, handle);

  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Format = DXGI_FORMAT_UNKNOWN;
  desc.Width = RT_W * RT_H * sizeof(int);
  desc.Height = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  CE(g_device12->CreateCommittedResource(
    &props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&g_rt_scratch_resource)));
  g_rt_scratch_resource->SetName(L"RT scratch resource");
  handle.ptr += 4 * g_srv_uav_cbv_descriptor_size;

  uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.NumElements = RT_W * RT_H;
  uav_desc.Buffer.StructureByteStride = sizeof(int);
  g_device12->CreateUnorderedAccessView(g_rt_scratch_resource, nullptr, &uav_desc, handle);

  // CB
  D3D12_RESOURCE_DESC cb_desc{};
  cb_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  cb_desc.Alignment = 0;
  cb_desc.Width = 256;
  cb_desc.Height = 1;
  cb_desc.DepthOrArraySize = 1;
  cb_desc.MipLevels = 1;
  cb_desc.Format = DXGI_FORMAT_UNKNOWN;
  cb_desc.SampleDesc.Count = 1;
  cb_desc.SampleDesc.Quality = 0;
  cb_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  cb_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

  D3D12_HEAP_PROPERTIES heap_props{};
  heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;
  heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  heap_props.CreationNodeMask = 1;
  heap_props.VisibleNodeMask = 1;
  CE(g_device12->CreateCommittedResource(
    &heap_props, D3D12_HEAP_FLAG_NONE, &cb_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&d_raygen_cb)));
  d_raygen_cb->SetName(L"RayGen CB");

  D3D12_CPU_DESCRIPTOR_HANDLE cbv_handle(g_srv_uav_cbv_heap->GetCPUDescriptorHandleForHeapStart());
  cbv_handle.ptr += 3 * g_srv_uav_cbv_descriptor_size;
  D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc{};
  cbv_desc.BufferLocation = d_raygen_cb->GetGPUVirtualAddress();
  cbv_desc.SizeInBytes = 256;
  g_device12->CreateConstantBufferView(&cbv_desc, cbv_handle);
}

void CreateRTPipeline() {
  // 1. Root Parameters (global)
  {
    D3D12_ROOT_PARAMETER root_params[1];
    root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;

    D3D12_DESCRIPTOR_RANGE desc_ranges[4]{};
    desc_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;  // Output
    desc_ranges[0].NumDescriptors = 1;
    desc_ranges[0].BaseShaderRegister = 0;
    desc_ranges[0].RegisterSpace = 0;
    desc_ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    desc_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;  // TLAS, SpheresInfo
    desc_ranges[1].NumDescriptors = 2;
    desc_ranges[1].BaseShaderRegister = 0;
    desc_ranges[1].RegisterSpace = 0;
    desc_ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    desc_ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    desc_ranges[2].NumDescriptors = 1;
    desc_ranges[2].BaseShaderRegister = 0;
    desc_ranges[2].RegisterSpace = 0;
    desc_ranges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    desc_ranges[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;  // Scratch
    desc_ranges[3].NumDescriptors = 1;
    desc_ranges[3].BaseShaderRegister = 1;
    desc_ranges[3].RegisterSpace = 0;
    desc_ranges[3].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    root_params[0].DescriptorTable.pDescriptorRanges = desc_ranges;
    root_params[0].DescriptorTable.NumDescriptorRanges = _countof(desc_ranges);
    root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootsig_desc{};
    rootsig_desc.NumStaticSamplers = 0;
    rootsig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    rootsig_desc.NumParameters = 1;
    rootsig_desc.pParameters = root_params;

    ID3DBlob* signature, * error;
    D3D12SerializeRootSignature(&rootsig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if (error)
    {
      printf("Error: %s\n", (char*)(error->GetBufferPointer()));
    }
    CE(g_device12->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&g_global_rootsig)));
    signature->Release();
    if (error)
      error->Release();
  }

  // 2. RTPSO
  {
    std::vector<D3D12_STATE_SUBOBJECT> subobjects;

    // 1. DXIL Library
    IDxcBlob* dxil_library = CompileShaderLibrary(L"shaders/shaders.hlsl");
    D3D12_EXPORT_DESC dxil_lib_exports[4];
    dxil_lib_exports[0].Flags = D3D12_EXPORT_FLAG_NONE;
    dxil_lib_exports[0].ExportToRename = nullptr;
    dxil_lib_exports[0].Name = L"RayGen";
    dxil_lib_exports[1].Flags = D3D12_EXPORT_FLAG_NONE;
    dxil_lib_exports[1].ExportToRename = nullptr;
    dxil_lib_exports[1].Name = L"ClosestHit";
    dxil_lib_exports[2].Flags = D3D12_EXPORT_FLAG_NONE;
    dxil_lib_exports[2].ExportToRename = nullptr;
    dxil_lib_exports[2].Name = L"Intersection";
    dxil_lib_exports[3].Flags = D3D12_EXPORT_FLAG_NONE;
    dxil_lib_exports[3].ExportToRename = nullptr;
    dxil_lib_exports[3].Name = L"Miss";

    D3D12_DXIL_LIBRARY_DESC dxil_lib_desc{};
    dxil_lib_desc.DXILLibrary.pShaderBytecode = dxil_library->GetBufferPointer();
    dxil_lib_desc.DXILLibrary.BytecodeLength = dxil_library->GetBufferSize();
    dxil_lib_desc.NumExports = _countof(dxil_lib_exports);
    dxil_lib_desc.pExports = dxil_lib_exports;

    D3D12_STATE_SUBOBJECT subobj_dxil_lib{};
    subobj_dxil_lib.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    subobj_dxil_lib.pDesc = &dxil_lib_desc;
    subobjects.push_back(subobj_dxil_lib);

    // 2. Shader Config
    D3D12_RAYTRACING_SHADER_CONFIG shader_config{};
    shader_config.MaxAttributeSizeInBytes = 12;   // normal
    shader_config.MaxPayloadSizeInBytes = 16;  // float4 color
    D3D12_STATE_SUBOBJECT subobj_shaderconfig{};
    subobj_shaderconfig.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    subobj_shaderconfig.pDesc = &shader_config;
    subobjects.push_back(subobj_shaderconfig);

    // 3. Global Root Signature
    D3D12_STATE_SUBOBJECT subobj_global_rootsig{};
    subobj_global_rootsig.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    subobj_global_rootsig.pDesc = &g_global_rootsig;
    subobjects.push_back(subobj_global_rootsig);

    // 4. Pipeline config
    D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config{};
    pipeline_config.MaxTraceRecursionDepth = 3;
    D3D12_STATE_SUBOBJECT subobj_pipeline_config{};
    subobj_pipeline_config.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    subobj_pipeline_config.pDesc = &pipeline_config;
    subobjects.push_back(subobj_pipeline_config);

    // 5. Hit Group
    D3D12_HIT_GROUP_DESC hitgroup_desc{};
    hitgroup_desc.Type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE;
    hitgroup_desc.HitGroupExport = L"HitGroup";
    hitgroup_desc.ClosestHitShaderImport = L"ClosestHit";
    hitgroup_desc.IntersectionShaderImport = L"Intersection";
    D3D12_STATE_SUBOBJECT subobj_hitgroup{};
    subobj_hitgroup.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    subobj_hitgroup.pDesc = &hitgroup_desc;
    subobjects.push_back(subobj_hitgroup);

    D3D12_STATE_OBJECT_DESC rtpso_desc{};
    rtpso_desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    rtpso_desc.NumSubobjects = subobjects.size();
    rtpso_desc.pSubobjects = subobjects.data();
    CE(g_device12->CreateStateObject(&rtpso_desc, IID_PPV_ARGS(&g_rt_state_object)));

    g_rt_state_object->QueryInterface(IID_PPV_ARGS(&g_rt_state_object_props));
  }
}

void CreateShaderBindingTable() {
  void* raygen_shader_id = g_rt_state_object_props->GetShaderIdentifier(L"RayGen");
  void* hitgroup_id      = g_rt_state_object_props->GetShaderIdentifier(L"HitGroup");
  void* miss_id          = g_rt_state_object_props->GetShaderIdentifier(L"Miss");

  const int shader_record_size = 64;

  D3D12_RESOURCE_DESC sbt_desc{};
  sbt_desc.DepthOrArraySize = 1;
  sbt_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  sbt_desc.Format = DXGI_FORMAT_UNKNOWN;
  sbt_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  sbt_desc.Width = 192;  // Raygen + Hitgroup + Miss, 64B stride
  sbt_desc.Height = 1;
  sbt_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  sbt_desc.SampleDesc.Count = 1;
  sbt_desc.SampleDesc.Quality = 0;
  sbt_desc.MipLevels = 1;

  D3D12_HEAP_PROPERTIES heap_props{};
  heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;
  heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  heap_props.CreationNodeMask = 1;
  heap_props.VisibleNodeMask = 1;

  CE(g_device12->CreateCommittedResource(
    &heap_props, D3D12_HEAP_FLAG_NONE, &sbt_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_sbt_storage)));
  char* mapped;
  g_sbt_storage->Map(0, nullptr, (void**)&mapped);
  memcpy(mapped, raygen_shader_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
  memcpy(mapped + 64, hitgroup_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
  memcpy(mapped + 128, miss_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
  g_sbt_storage->Unmap(0, nullptr);
  g_sbt_storage->SetName(L"RayGen SBT storage");
}

void SetSceneAndCreateAS(int idx) {
  g_recursion_depth = g_scenes[idx].recursion_depth;
  std::vector<D3D12_RAYTRACING_AABB> aabbs;

  // Each of the sphere becomes a Geometry
  std::vector<SphereInfo>& spheres = g_scenes[idx].spheres;

  for (size_t i = 0; i < spheres.size(); i++) {
    SphereInfo& s = spheres[i];
    D3D12_RAYTRACING_AABB aabb{};
    aabb.MinX = s.pos.x - s.radius;
    aabb.MaxX = s.pos.x + s.radius;
    aabb.MinY = s.pos.y - s.radius;
    aabb.MaxY = s.pos.y + s.radius;
    aabb.MinZ = s.pos.z - s.radius;
    aabb.MaxZ = s.pos.z + s.radius;
    aabbs.push_back(aabb);
  }

  D3D12_HEAP_PROPERTIES heap_props{};
  heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;
  heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  heap_props.CreationNodeMask = 1;
  heap_props.VisibleNodeMask = 1;

  D3D12_RESOURCE_DESC res_desc{};
  res_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  res_desc.Alignment = 0;
  res_desc.Width = sizeof(SphereInfo) * aabbs.size();
  res_desc.Height = 1;
  res_desc.DepthOrArraySize = 1;
  res_desc.MipLevels = 1;
  res_desc.Format = DXGI_FORMAT_UNKNOWN;
  res_desc.SampleDesc.Count = 1;
  res_desc.SampleDesc.Quality = 0;
  res_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  res_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

  ID3D12Resource* spheres_buf{};
  void* mapped;
  CE(g_device12->CreateCommittedResource(
    &heap_props, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&spheres_buf)));
  spheres_buf->Map(0, nullptr, &mapped);
  memcpy(mapped, spheres.data(), sizeof(SphereInfo)* aabbs.size());
  spheres_buf->Unmap(0, nullptr);

  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
  srv_desc.Buffer.FirstElement = 0;
  srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
  srv_desc.Buffer.NumElements = aabbs.size();
  srv_desc.Buffer.StructureByteStride = sizeof(SphereInfo);
  srv_desc.Format = DXGI_FORMAT_UNKNOWN;
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;

  D3D12_CPU_DESCRIPTOR_HANDLE srv_handle(g_srv_uav_cbv_heap->GetCPUDescriptorHandleForHeapStart());
  srv_handle.ptr += g_srv_uav_cbv_descriptor_size * 2;
  g_device12->CreateShaderResourceView(spheres_buf, &srv_desc, srv_handle);

  ID3D12Resource* aabbs_buf{};
  res_desc.Width = sizeof(D3D12_RAYTRACING_AABB) * aabbs.size();

  CE(g_device12->CreateCommittedResource(
    &heap_props, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&aabbs_buf)));
  aabbs_buf->Map(0, nullptr, &mapped);
  memcpy(mapped, aabbs.data(), sizeof(D3D12_RAYTRACING_AABB)* aabbs.size());
  aabbs_buf->Unmap(0, nullptr);

  D3D12_RAYTRACING_GEOMETRY_DESC geom_desc{};
  geom_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
  geom_desc.AABBs.AABBCount = aabbs.size();
  geom_desc.AABBs.AABBs.StartAddress = aabbs_buf->GetGPUVirtualAddress();
  geom_desc.AABBs.AABBs.StrideInBytes = sizeof(D3D12_RAYTRACING_AABB);
  geom_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION; // <---- Key point here

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_inputs{};
  blas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  blas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  blas_inputs.NumDescs = 1;
  blas_inputs.pGeometryDescs = &geom_desc;
  blas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO pb_info{};
  g_device12->GetRaytracingAccelerationStructurePrebuildInfo(&blas_inputs, &pb_info);
  printf("BLAS prebuild info:");
  printf(" Scratch: %d", int(pb_info.ScratchDataSizeInBytes));
  printf(", Result : %d\n", int(pb_info.ResultDataMaxSizeInBytes));

  D3D12_RESOURCE_DESC scratch_desc{};
  scratch_desc.Alignment = 0;
  scratch_desc.DepthOrArraySize = 1;
  scratch_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  scratch_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  scratch_desc.Format = DXGI_FORMAT_UNKNOWN;
  scratch_desc.Height = 1;
  scratch_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  scratch_desc.MipLevels = 1;
  scratch_desc.SampleDesc.Count = 1;
  scratch_desc.SampleDesc.Quality = 0;
  scratch_desc.Width = pb_info.ScratchDataSizeInBytes;

  heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  heap_props.CreationNodeMask = 1;
  heap_props.VisibleNodeMask = 1;

  ID3D12Resource* blas_scratch;
  ID3D12Resource* blas_result;

  CE(g_device12->CreateCommittedResource(
    &heap_props, D3D12_HEAP_FLAG_NONE, &scratch_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&blas_scratch)));

  D3D12_RESOURCE_DESC result_desc = scratch_desc;
  result_desc.Width = pb_info.ResultDataMaxSizeInBytes;

  CE(g_device12->CreateCommittedResource(
    &heap_props, D3D12_HEAP_FLAG_NONE, &result_desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&blas_result)));

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build_desc{};
  build_desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  build_desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  build_desc.Inputs.NumDescs = 1;
  build_desc.Inputs.pGeometryDescs = &geom_desc;
  build_desc.DestAccelerationStructureData = blas_result->GetGPUVirtualAddress();
  build_desc.ScratchAccelerationStructureData = blas_scratch->GetGPUVirtualAddress();
  build_desc.SourceAccelerationStructureData = 0;
  build_desc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

  // Build BLAS
  g_command_list->Reset(g_command_allocator, nullptr);
  g_command_list->BuildRaytracingAccelerationStructure(&build_desc, 0, nullptr);

  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  barrier.UAV.pResource = blas_result;
  g_command_list->ResourceBarrier(1, &barrier);

  g_command_list->Close();
  g_command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)(&g_command_list));
  WaitForPreviousFrame();

  aabbs_buf->Release();
  blas_scratch->Release();

  // TLAS's Instance
  D3D12_RAYTRACING_INSTANCE_DESC inst_desc{};
  inst_desc.InstanceID = 10000;
  inst_desc.InstanceContributionToHitGroupIndex = 0;
  inst_desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
  inst_desc.Transform[0][0] = 1;
  inst_desc.Transform[1][1] = 1;
  inst_desc.Transform[2][2] = 1;
  inst_desc.AccelerationStructure = blas_result->GetGPUVirtualAddress();
  inst_desc.InstanceMask = 0xFF;

  res_desc.Width = sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
  heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;
  ID3D12Resource* insts_buf;
  CE(g_device12->CreateCommittedResource(
    &heap_props, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&insts_buf)));
  insts_buf->Map(0, nullptr, &mapped);
  memcpy(mapped, &inst_desc, sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
  insts_buf->Unmap(0, nullptr);

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs{};
  tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  tlas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  tlas_inputs.NumDescs = 1;
  tlas_inputs.pGeometryDescs = nullptr;
  tlas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  
  g_device12->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_inputs, &pb_info);
  printf("TLAS prebuild info:");
  printf(" Scratch: %d", int(pb_info.ScratchDataSizeInBytes));
  printf(", Result : %d\n", int(pb_info.ResultDataMaxSizeInBytes));

  ID3D12Resource* tlas_scratch;
  ID3D12Resource* tlas_result;

  heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
  scratch_desc.Width = pb_info.ScratchDataSizeInBytes;
  CE(g_device12->CreateCommittedResource(
    &heap_props, D3D12_HEAP_FLAG_NONE, &scratch_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tlas_scratch)));

  result_desc.Width = pb_info.ResultDataMaxSizeInBytes;
  CE(g_device12->CreateCommittedResource(
    &heap_props, D3D12_HEAP_FLAG_NONE, &scratch_desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&tlas_result)));
  tlas_result->SetName(L"TLAS");

  build_desc = {};
  build_desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  build_desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  build_desc.Inputs.InstanceDescs = insts_buf->GetGPUVirtualAddress();
  build_desc.Inputs.NumDescs = 1;
  build_desc.DestAccelerationStructureData = tlas_result->GetGPUVirtualAddress();
  build_desc.ScratchAccelerationStructureData = tlas_scratch->GetGPUVirtualAddress();
  build_desc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

  g_command_list->Reset(g_command_allocator, nullptr);
  g_command_list->BuildRaytracingAccelerationStructure(&build_desc, 0, nullptr);

  barrier.UAV.pResource = tlas_result;
  g_command_list->ResourceBarrier(1, &barrier);

  g_command_list->Close();
  g_command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)(&g_command_list));
  WaitForPreviousFrame();

  tlas_scratch->Release();

  // SRV of TLAS
  srv_handle = D3D12_CPU_DESCRIPTOR_HANDLE(g_srv_uav_cbv_heap->GetCPUDescriptorHandleForHeapStart());
  srv_handle.ptr += g_srv_uav_cbv_descriptor_size;
  srv_desc = {};
  srv_desc.Format = DXGI_FORMAT_UNKNOWN;
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv_desc.RaytracingAccelerationStructure.Location = tlas_result->GetGPUVirtualAddress();
  g_device12->CreateShaderResourceView(nullptr, &srv_desc, srv_handle);

  g_frame_count = 0;
}

void Render() {
  static float last_secs;
  float secs = glfwGetTime();
  float delta_secs = secs - last_secs;
  last_secs = secs;

  if (g_axes[0] != 0) {
    g_cam_pos += g_cx * delta_secs * 10.0f * float(g_axes[0]);
  }
  if (g_axes[2] != 0) {
    g_cam_pos += g_cy * delta_secs * 10.0f * float(g_axes[2]);
  }
  if (g_axes[1] != 0) {
    g_cam_pos += g_cam_dir * delta_secs * 10.0f * float(g_axes[1]);
  }

  bool zeros = std::all_of(g_axes.begin(), g_axes.end(), [](char i) { return i == 0; });
  if (!zeros) g_frame_count = 0;

  // Update
  RayGenCB cb{};
  cb.frame_count = g_frame_count;
  cb.cam_pos = g_cam_pos;
  cb.cam_dir = g_cam_dir;
  cb.cx = g_cx;
  cb.cy = g_cy;
  cb.recursion_depth = g_recursion_depth;
  cb.nsamp = g_nsamp;

  char* mapped;
  d_raygen_cb->Map(0, nullptr, (void**)&mapped);
  memcpy(mapped, &cb, sizeof(RayGenCB));
  D3D12_RANGE written_range{};
  written_range.Begin = 0;
  written_range.End = sizeof(RayGenCB);
  d_raygen_cb->Unmap(0, &written_range);

  // Render
  D3D12_CPU_DESCRIPTOR_HANDLE handle_rtv(g_rtv_heap->GetCPUDescriptorHandleForHeapStart());
  handle_rtv.ptr += g_rtv_descriptor_size * g_frame_index;

  float bg_color[] = { 1.0f, 1.0f, 0.8f, 1.0f };
  CE(g_command_allocator->Reset());  // Prevent memory leak
  CE(g_command_list->Reset(g_command_allocator, nullptr));

  D3D12_RESOURCE_BARRIER barrier_rtv{};
  barrier_rtv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier_rtv.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier_rtv.Transition.pResource = g_rendertargets[g_frame_index];
  barrier_rtv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier_rtv.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
  barrier_rtv.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
  g_command_list->ResourceBarrier(1, &barrier_rtv);

  g_command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);

  // Dispatch Rays
  D3D12_RESOURCE_BARRIER barrier_rt_out = barrier_rtv;
  barrier_rt_out.Transition.pResource = g_rt_output_resource;
  barrier_rt_out.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  barrier_rt_out.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  g_command_list->ResourceBarrier(1, &barrier_rt_out);

  D3D12_GPU_DESCRIPTOR_HANDLE srv_uav_cbv_handle(g_srv_uav_cbv_heap->GetGPUDescriptorHandleForHeapStart());
  g_command_list->SetComputeRootSignature(g_global_rootsig);
  g_command_list->SetDescriptorHeaps(1, &g_srv_uav_cbv_heap);
  g_command_list->SetComputeRootDescriptorTable(0, srv_uav_cbv_handle);
  g_command_list->SetPipelineState1(g_rt_state_object);

  D3D12_DISPATCH_RAYS_DESC desc{};
  desc.RayGenerationShaderRecord.StartAddress = g_sbt_storage->GetGPUVirtualAddress();
  desc.RayGenerationShaderRecord.SizeInBytes = 64;
  desc.HitGroupTable.StartAddress = g_sbt_storage->GetGPUVirtualAddress() + 64;
  desc.HitGroupTable.SizeInBytes = 32;
  desc.HitGroupTable.StrideInBytes = 32;
  desc.MissShaderTable.StartAddress = g_sbt_storage->GetGPUVirtualAddress() + 128;
  desc.MissShaderTable.SizeInBytes = 32;
  desc.Width = RT_W;
  desc.Height = RT_H;
  desc.Depth = 1;
  g_command_list->DispatchRays(&desc);

  barrier_rt_out.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  barrier_rt_out.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  g_command_list->ResourceBarrier(1, &barrier_rt_out);

  g_command_list->OMSetRenderTargets(1, &handle_rtv, false, nullptr);
  g_fsquad.Render(g_command_list);

  barrier_rtv.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  barrier_rtv.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
  g_command_list->ResourceBarrier(1, &barrier_rtv);

  barrier_rt_out.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  barrier_rt_out.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
  g_command_list->ResourceBarrier(1, &barrier_rt_out);

  CE(g_command_list->Close());

  g_command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&g_command_list);
  CE(g_swapchain->Present(1, 0));
  WaitForPreviousFrame();

  g_frame_count += g_nsamp;
}

int main() {
  CreateMyGLFWWindow();
  InitDeviceAndCommandQ();
  g_fsquad.Init(g_device12);
  InitSwapChain();
  InitDX12Stuff();
  g_fsquad.CreateSRVForBuffer(g_device12, g_rt_output_resource);

  CreateRTPipeline();
  CreateShaderBindingTable();

  SetSceneAndCreateAS(1);

  while (!glfwWindowShouldClose(g_window))
  {
    Render();
    char title[100];
    snprintf(title, 100, "SmallPT DXR SPP=%d", g_frame_count);
    glfwSetWindowTitle(g_window, title);
    glfwPollEvents();
  }
}