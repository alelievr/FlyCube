#include "Resource/VKResource.h"
#include <View/VKView.h>
#include <Device/VKDevice.h>
#include <Memory/VKMemory.h>
#include <Utilities/VKUtility.h>

VKResource::VKResource(VKDevice& device)
    : m_device(device)
{
}

void VKResource::CommitMemory(MemoryType memory_type)
{
    MemoryRequirements mem_requirements = GetMemoryRequirements();
    vk::MemoryDedicatedAllocateInfoKHR dedicated_allocate_info = {};
    vk::MemoryDedicatedAllocateInfoKHR* p_dedicated_allocate_info = nullptr;
    if (resource_type == ResourceType::kBuffer)
    {
        dedicated_allocate_info.buffer = buffer.res.get();
        p_dedicated_allocate_info = &dedicated_allocate_info;
    }
    else if (resource_type == ResourceType::kTexture)
    {
        dedicated_allocate_info.image = image.res;
        p_dedicated_allocate_info = &dedicated_allocate_info;
    }
    auto memory = std::make_shared<VKMemory>(m_device, mem_requirements.size, memory_type, mem_requirements.memory_type_bits, p_dedicated_allocate_info);
    BindMemory(memory, 0);
}

void VKResource::BindMemory(const std::shared_ptr<Memory>& memory, uint64_t offset)
{
    m_memory = memory;
    m_memory_type = m_memory->GetMemoryType();
    m_vk_memory = m_memory->As<VKMemory>().GetMemory();

    if (resource_type == ResourceType::kBuffer)
    {
        m_device.GetDevice().bindBufferMemory(buffer.res.get(), m_vk_memory, offset);
    }
    else if (resource_type == ResourceType::kTexture)
    {
        m_device.GetDevice().bindImageMemory(image.res, m_vk_memory, offset);
    }
    else if (resource_type == ResourceType::kTopLevelAS || resource_type == ResourceType::kBottomLevelAS)
    {
        vk::BindAccelerationStructureMemoryInfoNV acceleration_structure_memory_info = {};
        acceleration_structure_memory_info.accelerationStructure = as.acceleration_structure.get();
        acceleration_structure_memory_info.memory = m_vk_memory;
        ASSERT_SUCCEEDED(m_device.GetDevice().bindAccelerationStructureMemoryNV(1, &acceleration_structure_memory_info));
    }
}

uint64_t VKResource::GetWidth() const
{
    if (resource_type == ResourceType::kTexture)
        return image.size.width;
    return buffer.size;
}

uint32_t VKResource::GetHeight() const
{
    return image.size.height;
}

uint16_t VKResource::GetLayerCount() const
{
    return image.array_layers;
}

uint16_t VKResource::GetLevelCount() const
{
    return image.level_count;
}

uint32_t VKResource::GetSampleCount() const
{
    return image.sample_count;
}

uint64_t VKResource::GetAccelerationStructureHandle() const
{
    uint64_t handle = {};
    ASSERT_SUCCEEDED(m_device.GetDevice().getAccelerationStructureHandleNV(as.acceleration_structure.get(), sizeof(uint64_t), &handle));
    return handle;
}

void VKResource::SetName(const std::string& name)
{
    vk::DebugUtilsObjectNameInfoEXT info = {};
    info.pObjectName = name.c_str();
    if (resource_type == ResourceType::kBuffer)
    {
        info.objectType = buffer.res.get().objectType;
        info.objectHandle = reinterpret_cast<uint64_t>(static_cast<VkBuffer>(buffer.res.get()));
    }
    else if (resource_type == ResourceType::kTexture)
    {
        info.objectType = image.res.objectType;
        info.objectHandle = reinterpret_cast<uint64_t>(static_cast<VkImage>(image.res));
    }
    else if (resource_type == ResourceType::kTopLevelAS || resource_type == ResourceType::kBottomLevelAS)
    {
        info.objectType = as.acceleration_structure.get().objectType;
        info.objectHandle = reinterpret_cast<uint64_t>(static_cast<VkAccelerationStructureNV>(as.acceleration_structure.get()));
    }
    m_device.GetDevice().setDebugUtilsObjectNameEXT(info);
}

uint8_t* VKResource::Map()
{
    uint8_t* dst_data = nullptr;
    m_device.GetDevice().mapMemory(m_vk_memory, 0, VK_WHOLE_SIZE, {}, reinterpret_cast<void**>(&dst_data));
    return dst_data;
}

void VKResource::Unmap()
{
    m_device.GetDevice().unmapMemory(m_vk_memory);
}

bool VKResource::AllowCommonStatePromotion(ResourceState state_after)
{
    return false;
}

MemoryRequirements VKResource::GetMemoryRequirements() const
{
    vk::MemoryRequirements2 mem_requirements = {};
    if (resource_type == ResourceType::kBuffer)
    {
        vk::BufferMemoryRequirementsInfo2KHR buffer_mem_req = {};
        buffer_mem_req.buffer = buffer.res.get();
        m_device.GetDevice().getBufferMemoryRequirements2(&buffer_mem_req, &mem_requirements);
    }
    else if (resource_type == ResourceType::kTexture)
    {
        vk::ImageMemoryRequirementsInfo2KHR image_mem_req = {};
        image_mem_req.image = image.res;
        m_device.GetDevice().getImageMemoryRequirements2(&image_mem_req, &mem_requirements);
    }
    else if (resource_type == ResourceType::kTopLevelAS || resource_type == ResourceType::kBottomLevelAS)
    {
        vk::AccelerationStructureMemoryRequirementsInfoNV memory_requirements_info = {};
        memory_requirements_info.type = vk::AccelerationStructureMemoryRequirementsTypeNV::eObject;
        memory_requirements_info.accelerationStructure = as.acceleration_structure.get();
        m_device.GetDevice().getAccelerationStructureMemoryRequirementsNV(&memory_requirements_info, &mem_requirements);
    }
    return { mem_requirements.memoryRequirements.size, mem_requirements.memoryRequirements.alignment, mem_requirements.memoryRequirements.memoryTypeBits };
}
