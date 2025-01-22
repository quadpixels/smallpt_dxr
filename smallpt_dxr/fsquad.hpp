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

struct FullScreenQuad {
  FullScreenQuad();
  void Init(ID3D12Device* device);
  void Render(ID3D12GraphicsCommandList4* command_list);
  void CreateSRVForBuffer(ID3D12Device* device, ID3D12Resource* b);

  ID3D12RootSignature* root_sig;
  ID3D12PipelineState* pipeline;
  ID3D12Resource* vertex_buffer;
  ID3D12DescriptorHeap* srv_uav_cbv_heap;
  D3D12_VERTEX_BUFFER_VIEW vbv;
};