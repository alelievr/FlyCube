#pragma once
#include "Resource/ResourceBase.h"
#include <d3d12.h>
#include <wrl.h>
using namespace Microsoft::WRL;

class DXDevice;

class DXResource : public ResourceBase
{
public:
    DXResource(DXDevice& device);
    uint64_t GetWidth() const override;
    uint32_t GetHeight() const override;
    uint16_t GetDepthOrArraySize() const override;
    uint16_t GetMipLevels() const override;
    void SetName(const std::string& name) override;
    uint8_t* Map() override;
    void Unmap() override;

    ComPtr<ID3D12Resource> resource;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_DESC desc = {};
    D3D12_SAMPLER_DESC sampler_desc = {};

private:
    DXDevice& m_device;
};
