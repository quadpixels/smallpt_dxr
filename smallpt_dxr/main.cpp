#include <stdio.h>

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

GLFWwindow* g_window;
int WIN_W = 720, WIN_H = 720;
int RT_W = 720, RT_H = 720;
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

struct RayGenCB {
  int frame_count;
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
    default: break;
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
    D3D12_EXPORT_DESC dxil_lib_exports[3];
    dxil_lib_exports[0].Flags = D3D12_EXPORT_FLAG_NONE;
    dxil_lib_exports[0].ExportToRename = nullptr;
    dxil_lib_exports[0].Name = L"RayGen";
    dxil_lib_exports[1].Flags = D3D12_EXPORT_FLAG_NONE;
    dxil_lib_exports[1].ExportToRename = nullptr;
    dxil_lib_exports[1].Name = L"ClosestHit";
    dxil_lib_exports[2].Flags = D3D12_EXPORT_FLAG_NONE;
    dxil_lib_exports[2].ExportToRename = nullptr;
    dxil_lib_exports[2].Name = L"Intersection";

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

  const int shader_record_size = 64;

  D3D12_RESOURCE_DESC sbt_desc{};
  sbt_desc.DepthOrArraySize = 1;
  sbt_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  sbt_desc.Format = DXGI_FORMAT_UNKNOWN;
  sbt_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  sbt_desc.Width = 128;
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
  g_sbt_storage->Unmap(0, nullptr);
  g_sbt_storage->SetName(L"RayGen SBT storage");
}

void CreateAS() {
  std::vector<D3D12_RAYTRACING_AABB> aabbs;

  // Each of the sphere becomes a Geometry
  SphereInfo spheres[] = {
    {1e5, glm::vec3(1e5 + 1,40.8,81.6),   glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.75, 0.25, 0.25), 0 },
    {1e5, glm::vec3(-1e5 + 99,40.8,81.6), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.25, 0.25, 0.75), 0 },
    {1e5, glm::vec3(50,40.8, 1e5),        glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.75, 0.75, 0.75), 0},
    {1e5, glm::vec3(50,40.8,-1e5 + 170),  glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0, 0, 0), 0 },
    {1e5, glm::vec3(50, 1e5, 81.6),       glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.75, 0.75, 0.75), 0 },
    {1e5, glm::vec3(50,-1e5 + 81.6,81.6), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.75, 0.75, 0.75), 0 },
    {16.5,glm::vec3(27,16.5,47),          glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.999, 0.999, 0.999), 1 },
    {16.5,glm::vec3(73,16.5,78),          glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.999, 0.999, 0.999), 2 },
    {600, glm::vec3(50,681.6 - .27,81.6), glm::vec3(12,12,12),         glm::vec3(0,0,0), 0 }
  };

  for (size_t i = 0; i < _countof(spheres); i++) {
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
  memcpy(mapped, spheres, sizeof(SphereInfo)* aabbs.size());
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
}

void Render() {
  // Update
  RayGenCB cb{};
  cb.frame_count = g_frame_count;
  g_frame_count++;

  char* mapped;
  d_raygen_cb->Map(0, nullptr, (void**)&mapped);
  memcpy(mapped, &cb, sizeof(RayGenCB));
  d_raygen_cb->Unmap(0, nullptr);

  // Render
  D3D12_CPU_DESCRIPTOR_HANDLE handle_rtv(g_rtv_heap->GetCPUDescriptorHandleForHeapStart());
  handle_rtv.ptr += g_rtv_descriptor_size * g_frame_index;

  float bg_color[] = { 1.0f, 1.0f, 0.8f, 1.0f };
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

  CreateAS();

  while (!glfwWindowShouldClose(g_window))
  {
    Render();
    glfwSetWindowTitle(g_window, (std::string("SmallPT DXR SPP=") + std::to_string(g_frame_count)).c_str());
    glfwPollEvents();
  }
}