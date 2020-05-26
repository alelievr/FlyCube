#pragma once
#include "CommandList.h"
#include <Utilities/Vulkan.h>

class VKDevice;

class VKCommandList : public CommandList
{
public:
    VKCommandList(VKDevice& device);
    void Open() override;
    void Close() override;
    void BindPipeline(const std::shared_ptr<Pipeline>& state) override;
    void BindBindingSet(const std::shared_ptr<BindingSet>& binding_set) override;
    void BeginRenderPass(const std::shared_ptr<Framebuffer>& framebuffer) override;
    void EndRenderPass() override;
    void BeginEvent(const std::string& name) override;
    void EndEvent() override;
    void ClearColor(const std::shared_ptr<View>& view, const std::array<float, 4>& color) override;
    void ClearDepth(const std::shared_ptr<View>& view, float depth) override;
    void DrawIndexed(uint32_t index_count, uint32_t start_index_location, int32_t base_vertex_location) override;
    void Dispatch(uint32_t thread_group_count_x, uint32_t thread_group_count_y, uint32_t thread_group_count_z) override;
    void DispatchRays(uint32_t width, uint32_t height, uint32_t depth) override;
    void ResourceBarrier(const std::shared_ptr<Resource>& resource, ResourceState state) override;
    void SetViewport(float width, float height) override;
    void IASetIndexBuffer(const std::shared_ptr<Resource>& resource, gli::format format) override;
    void IASetVertexBuffer(uint32_t slot, const std::shared_ptr<Resource>& resource) override;
    void CopyBuffer(const std::shared_ptr<Resource>& src_buffer, const std::shared_ptr<Resource>& dst_buffer,
                    const std::vector<BufferCopyRegion>& regions) override;
    void CopyBufferToTexture(const std::shared_ptr<Resource>& src_buffer, const std::shared_ptr<Resource>& dst_texture,
                             const std::vector<BufferToTextureCopyRegion>& regions) override;

    vk::CommandBuffer GetCommandList();

private:
    void ResourceBarrier(const std::shared_ptr<Resource>& resource, const ViewDesc& view_desc, ResourceState state);

    VKDevice& m_device;
    vk::UniqueCommandBuffer m_command_list;
    std::shared_ptr<Pipeline> m_state;
    std::shared_ptr<BindingSet> m_binding_set;
    static constexpr bool m_debug_regions = false;
};