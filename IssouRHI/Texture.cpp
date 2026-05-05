#include "IssouRHI.h"

namespace IssouRHI
{
Texture::Texture(Device* device, const TextureDesc& desc) : m_Device(device), m_Desc(desc) {}

Texture::~Texture() = default;

static TextureViewDimension ViewDimension(TextureDimension dim)
{
  switch (dim) {
    case TextureDimension::Texture1D:
      return TextureViewDimension::Texture1D;
    case TextureDimension::Texture2D:
      return TextureViewDimension::Texture2D;
    case TextureDimension::Texture3D:
      return TextureViewDimension::Texture3D;
    default:
      std::unreachable();
  }
}

std::shared_ptr<TextureView> Texture::CreateView()
{
  TextureViewDesc desc{};
  desc.format = Format();
  desc.dimension = ViewDimension(m_Desc.dimension);
  desc.range = {
      .baseMipLevel = 0,
      .mipLevelCount = m_Desc.mipLevelCount,
      .baseArrayLayer = 0,
      .arrayLayerCount = m_Desc.size.depth,
  };

  return CreateView(desc);
}

Extent3D Texture::SizeAtMipLevel(uint32_t level) const
{
  Extent3D base = Size();
  Extent3D size{};

  size.width = std::max(base.width >> level, 1u);
  if (m_Desc.dimension == TextureDimension::Texture1D) {
    return size;
  }

  size.height = std::max(base.height >> level, 1u);
  if (m_Desc.dimension == TextureDimension::Texture2D) {
    return size;
  }

  size.depth = std::max(base.depth >> level, 1u);
  return size;
}

TextureView::TextureView(Texture* tex, const TextureViewDesc& desc) : m_Texture(tex), m_Desc(desc) {}

TextureView::~TextureView() = default;

}  // namespace IssouRHI
