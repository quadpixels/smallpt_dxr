#include "fsquad.hpp"

#include <stdexcept>

#pragma comment(lib, "d3dcompiler.lib")

extern int WIN_W, WIN_H;

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

FullScreenQuad::FullScreenQuad() {

}

void FullScreenQuad::Init(ID3D12Device* device) {
  // Rootsig
  // Root params for drawing the FSQUAD
  {
    D3D12_ROOT_PARAMETER root_params[1]{};
    root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_DESCRIPTOR_RANGE desc_ranges[1]{};
    desc_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;  // RT output viewed as SRV
    desc_ranges[0].NumDescriptors = 1;
    desc_ranges[0].BaseShaderRegister = 0;
    desc_ranges[0].RegisterSpace = 0;
    desc_ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    root_params[0].DescriptorTable.NumDescriptorRanges = _countof(desc_ranges);
    root_params[0].DescriptorTable.pDescriptorRanges = desc_ranges;

    D3D12_STATIC_SAMPLER_DESC sampler_desc{};
    sampler_desc.ShaderRegister = 0;
    sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    sampler_desc.Filter = D3D12_FILTER_ANISOTROPIC;
    sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler_desc.MipLODBias = 0;
    sampler_desc.MaxAnisotropy = 8;
    sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    sampler_desc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    sampler_desc.MinLOD = 0.0f;
    sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
    sampler_desc.RegisterSpace = 0;

    D3D12_ROOT_SIGNATURE_DESC rootsig_desc{};
    rootsig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootsig_desc.NumParameters = 1;
    rootsig_desc.pParameters = root_params;
    rootsig_desc.NumStaticSamplers = 1;
    rootsig_desc.pStaticSamplers = &sampler_desc;

    ID3DBlob* signature{}, * error{};
    D3D12SerializeRootSignature(&rootsig_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if (error)
    {
      printf("Error: %s\n", (char*)(error->GetBufferPointer()));
    }
    CE(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root_sig)));
    root_sig->SetName(L"FSQuad root signature");
    signature->Release();
    if (error)
      error->Release();
  }

  ID3DBlob* vs_blob, * ps_blob, * error;
  unsigned  compile_flags = 0;
  D3DCompileFromFile(L"shaders/fsquad.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compile_flags, 0, &vs_blob, &error);
  if (error)
    printf("Error building VS: %s\n", (char*)(error->GetBufferPointer()));

  D3DCompileFromFile(L"shaders/fsquad.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compile_flags, 0, &ps_blob, &error);
  if (error)
    printf("Error building PS: %s\n", (char*)(error->GetBufferPointer()));

  // Pipeline
  D3D12_INPUT_ELEMENT_DESC input_element_descs[] = { {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA},
                                                     {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA} };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
  pso_desc.pRootSignature = root_sig;
  pso_desc.VS.pShaderBytecode = vs_blob->GetBufferPointer();
  pso_desc.VS.BytecodeLength = vs_blob->GetBufferSize();
  pso_desc.PS.pShaderBytecode = ps_blob->GetBufferPointer();
  pso_desc.PS.BytecodeLength = ps_blob->GetBufferSize();
  const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc = {
      FALSE,
      FALSE,
      D3D12_BLEND_ONE,
      D3D12_BLEND_ZERO,
      D3D12_BLEND_OP_ADD,
      D3D12_BLEND_ONE,
      D3D12_BLEND_ZERO,
      D3D12_BLEND_OP_ADD,
      D3D12_LOGIC_OP_NOOP,
      D3D12_COLOR_WRITE_ENABLE_ALL,
  };
  pso_desc.BlendState.AlphaToCoverageEnable = false;
  pso_desc.BlendState.IndependentBlendEnable = false;
  for (unsigned i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
  {
    pso_desc.BlendState.RenderTarget[i] = defaultRenderTargetBlendDesc;
  }
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pso_desc.RasterizerState.FrontCounterClockwise = FALSE;
  pso_desc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
  pso_desc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
  pso_desc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
  pso_desc.RasterizerState.DepthClipEnable = TRUE;
  pso_desc.RasterizerState.MultisampleEnable = FALSE;
  pso_desc.RasterizerState.AntialiasedLineEnable = FALSE;
  pso_desc.RasterizerState.ForcedSampleCount = 0;
  pso_desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
  pso_desc.DepthStencilState.DepthEnable = FALSE;
  pso_desc.DepthStencilState.StencilEnable = FALSE;
  pso_desc.InputLayout.pInputElementDescs = input_element_descs;
  pso_desc.InputLayout.NumElements = _countof(input_element_descs);
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 1;
  pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pso_desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
  pso_desc.SampleDesc.Count = 1;
  CE(device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline)));
  pipeline->SetName(L"FSQuad pipeline");

  // Vertex buffer of FSQUAD
  float verts[][4] = {
      {-1,  1, 0, 0},  // top-left
      { 3,  1, 2, 0},   // top-right
      {-1, -3, 0, 2}  // bottom-left
  };

  D3D12_RESOURCE_DESC res_desc{};
  res_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  res_desc.Alignment = 0;
  res_desc.Height = 1;
  res_desc.DepthOrArraySize = 1;
  res_desc.MipLevels = 1;
  res_desc.Format = DXGI_FORMAT_UNKNOWN;
  res_desc.SampleDesc.Count = 1;
  res_desc.SampleDesc.Quality = 0;
  res_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  res_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  res_desc.Width = sizeof(verts);

  D3D12_HEAP_PROPERTIES heap_props{};
  heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;
  heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  heap_props.CreationNodeMask = 1;
  heap_props.VisibleNodeMask = 1;

  ID3D12Resource* d_all_verts;
  CE(device->CreateCommittedResource(
    &heap_props, D3D12_HEAP_FLAG_NONE, &res_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertex_buffer)));
  char* mapped;
  vertex_buffer->Map(0, nullptr, (void**)(&mapped));
  memcpy(mapped, verts, sizeof(verts));
  vertex_buffer->Unmap(0, nullptr);

  vbv.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
  vbv.SizeInBytes = sizeof(verts);
  vbv.StrideInBytes = sizeof(float) * 4;

  // SRV Heap
  D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
  heap_desc.NumDescriptors = 1;  // [0]=rt_output's SRV
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  CE(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&srv_uav_cbv_heap)));
}

void FullScreenQuad::CreateSRVForBuffer(ID3D12Device* device, ID3D12Resource* b) {
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  srv_desc.Texture2D.MipLevels = 1;
  srv_desc.Texture2D.MostDetailedMip = 0;
  D3D12_CPU_DESCRIPTOR_HANDLE srv_handle(srv_uav_cbv_heap->GetCPUDescriptorHandleForHeapStart());
  device->CreateShaderResourceView(b, &srv_desc, srv_handle);
}

void FullScreenQuad::Render(ID3D12GraphicsCommandList4* command_list) {
  command_list->SetGraphicsRootSignature(root_sig);
  command_list->SetPipelineState(pipeline);
  command_list->SetDescriptorHeaps(1, &srv_uav_cbv_heap);
  D3D12_GPU_DESCRIPTOR_HANDLE srv_uav_cbv_handle(srv_uav_cbv_heap->GetGPUDescriptorHandleForHeapStart());
  command_list->SetGraphicsRootDescriptorTable(0, srv_uav_cbv_handle);

  D3D12_VIEWPORT viewport{};
  viewport.TopLeftX = 0;
  viewport.TopLeftY = 0;
  viewport.Width = WIN_W;
  viewport.Height = WIN_H;
  viewport.MinDepth = 0;
  viewport.MaxDepth = 1;

  D3D12_RECT scissor{};
  scissor.left = 0;
  scissor.top = 0;
  scissor.right = WIN_W;
  scissor.bottom = WIN_H;
  command_list->RSSetViewports(1, &viewport);
  command_list->RSSetScissorRects(1, &scissor);
  command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  command_list->IASetVertexBuffers(0, 1, &vbv);
  command_list->DrawInstanced(3, 1, 0, 0);
}