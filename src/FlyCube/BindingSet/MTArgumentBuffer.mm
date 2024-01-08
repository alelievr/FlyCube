#include "BindingSet/MTArgumentBuffer.h"

#include "BindingSetLayout/MTBindingSetLayout.h"
#include "Device/MTDevice.h"
#include "View/MTView.h"

namespace {

MTLRenderStages GetStage(ShaderType type)
{
    switch (type) {
    case ShaderType::kPixel:
        return MTLRenderStageFragment;
    case ShaderType::kVertex:
        return MTLRenderStageVertex;
    default:
        assert(false);
        return 0;
    }
}

MTLResourceUsage GetUsage(ViewType type)
{
    switch (type) {
    case ViewType::kAccelerationStructure:
    case ViewType::kBuffer:
    case ViewType::kConstantBuffer:
    case ViewType::kSampler:
    case ViewType::kStructuredBuffer:
    case ViewType::kTexture:
        return MTLResourceUsageRead;
    case ViewType::kRWBuffer:
    case ViewType::kRWStructuredBuffer:
    case ViewType::kRWTexture:
        return MTLResourceUsageWrite;
    default:
        assert(false);
        return MTLResourceUsageRead | MTLResourceUsageWrite;
    }
}

} // namespace

MTArgumentBuffer::MTArgumentBuffer(MTDevice& device, const std::shared_ptr<MTBindingSetLayout>& layout)
    : m_device(device)
    , m_layout(layout)
{
    const std::vector<BindKey>& bind_keys = m_layout->GetBindKeys();
    for (BindKey bind_key : bind_keys) {
        auto shader_space = std::make_pair(bind_key.shader_type, bind_key.space);
        m_slots_count[shader_space] = std::max(m_slots_count[shader_space], bind_key.slot + 1);
    }
    for (const auto& [shader_space, slots] : m_slots_count) {
        m_argument_buffers[shader_space] = [m_device.GetDevice() newBufferWithLength:slots * sizeof(uint64_t)
                                                                             options:MTLResourceStorageModeShared];
    }
}

void MTArgumentBuffer::WriteBindings(const std::vector<BindingDesc>& bindings)
{
    m_bindings = bindings;
    m_compure_resouces.clear();
    m_graphics_resouces.clear();

    const std::vector<BindKey>& bind_keys = m_layout->GetBindKeys();
    for (const auto& binding : m_bindings) {
        const BindKey& bind_key = binding.bind_key;
        decltype(auto) view = std::static_pointer_cast<MTView>(binding.view);
        decltype(auto) mt_resource = view->GetMTResource();
        uint32_t index = bind_key.slot;
        uint32_t slots = m_slots_count[{ bind_key.shader_type, bind_key.space }];
        assert(index < slots);
        uint64_t* arguments = (uint64_t*)m_argument_buffers[{ bind_key.shader_type, bind_key.space }].contents;
        if (!mt_resource) {
            arguments[index] = 0;
            continue;
        }

        id<MTLResource> resource = {};
        switch (bind_key.view_type) {
        case ViewType::kConstantBuffer:
        case ViewType::kBuffer:
        case ViewType::kRWBuffer:
        case ViewType::kStructuredBuffer:
        case ViewType::kRWStructuredBuffer: {
            id<MTLBuffer> buffer = buffer = mt_resource->buffer.res;
            arguments[index] = [buffer gpuAddress] + view->GetViewDesc().offset;
            resource = buffer;
            break;
        }
        case ViewType::kSampler: {
            id<MTLSamplerState> sampler = sampler = mt_resource->sampler.res;
            arguments[index] = [sampler gpuResourceID]._impl;
            break;
        }
        case ViewType::kTexture:
        case ViewType::kRWTexture: {
            id<MTLTexture> texture = view->GetTextureView();
            if (!texture) {
                texture = mt_resource->texture.res;
            }
            arguments[index] = [texture gpuResourceID]._impl;
            resource = texture;
            break;
        }
        case ViewType::kAccelerationStructure: {
            id<MTLAccelerationStructure> acceleration_structure = mt_resource->acceleration_structure;
            arguments[index] = [acceleration_structure gpuResourceID]._impl;
            resource = acceleration_structure;
            break;
        }
        default:
            assert(false);
            break;
        }

        if (!resource) {
            continue;
        }

        MTLResourceUsage usage = GetUsage(bind_key.view_type);
        if (bind_key.shader_type == ShaderType::kCompute) {
            m_compure_resouces[usage].push_back(resource);
        } else {
            m_graphics_resouces[{ GetStage(bind_key.shader_type), usage }].push_back(resource);
        }
    }
}

void MTArgumentBuffer::Apply(id<MTLRenderCommandEncoder> render_encoder, const std::shared_ptr<Pipeline>& state)
{
    for (const auto& [key, slots] : m_slots_count) {
        switch (key.first) {
        case ShaderType::kVertex:
            [render_encoder setVertexBuffer:m_argument_buffers[key] offset:0 atIndex:key.second];
            break;
        case ShaderType::kPixel:
            [render_encoder setFragmentBuffer:m_argument_buffers[key] offset:0 atIndex:key.second];
            break;
        default:
            assert(false);
            break;
        }
    }
    for (const auto& [stages_usage, resources] : m_graphics_resouces) {
        [render_encoder useResources:resources.data()
                               count:resources.size()
                               usage:stages_usage.second
                              stages:stages_usage.first];
    }
}

void MTArgumentBuffer::Apply(id<MTLComputeCommandEncoder> compute_encoder, const std::shared_ptr<Pipeline>& state)
{
    for (const auto& [key, slots] : m_slots_count) {
        switch (key.first) {
        case ShaderType::kCompute:
            [compute_encoder setBuffer:m_argument_buffers[key] offset:0 atIndex:key.second];
            break;
        default:
            assert(false);
            break;
        }
    }
    for (const auto& [usage, resources] : m_compure_resouces) {
        [compute_encoder useResources:resources.data() count:resources.size() usage:usage];
    }
}
