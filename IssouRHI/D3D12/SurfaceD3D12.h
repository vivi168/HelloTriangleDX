#pragma once

#include "CommonD3D12.h"

namespace IssouRHI
{
namespace D3D12
{
// TODO
class SurfaceImpl : public Surface
{
public:
  SurfaceImpl(Device* device, void* handle);
  ~SurfaceImpl() override;

  void Create() override;

  std::shared_ptr<Texture> GetCurrentTexture() override;
  void Present() override;

protected:
  void CreateSwapChain(SurfaceConfiguration& config) override;
  void CreateTextures(SurfaceConfiguration& config) override;

private:
  Microsoft::WRL::ComPtr<IDXGISwapChain3> m_SwapChain;

  Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
  HANDLE m_FenceEvent = nullptr;
  UINT64 m_NextFenceValue = 0;
  std::vector<UINT64> m_FenceValues;
};
}  // namespace D3D12
}  // namespace IssouRHI
