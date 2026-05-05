#include "IssouRHI.h"

namespace IssouRHI
{
Surface::Surface(Device* device, void* handle) : m_Device(device), m_Handle(handle) {}

Surface::~Surface() = default;

void Surface::Configure(SurfaceConfiguration& config)
{
  CreateSwapChain(config);
  CreateTextures(config);
  m_EnableVsync = config.enableVsync;

  m_Configured = true;
}
}  // namespace IssouRHI
