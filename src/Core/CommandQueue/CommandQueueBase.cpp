#include "CommandQueue/CommandQueueBase.h"
#include <CommandList/CommandListBase.h>
#include <Device/Device.h>

CommandQueueBase::CommandQueueBase(Device& device, CommandListType type)
    : m_device(device)
    , m_type(type)
{
}

void CommandQueueBase::OnDestroy()
{
    if (m_fence)
    {
        m_fence->Wait(m_fence_value);
        m_fence.reset();
    }
    m_command_list_pool.clear();
}

void CommandQueueBase::ExecuteCommandLists(const std::vector<std::shared_ptr<CommandList>>& command_lists)
{
    if (!m_fence)
        m_fence = m_device.CreateFence(m_fence_value);

    std::vector<std::shared_ptr<CommandList>> raw_command_lists;
    size_t patch_cmds = 0;
    for (size_t c = 0; c < command_lists.size(); ++c)
    {
        std::vector<ResourceBarrierManualDesc> new_barriers;
        auto& command_list_base = command_lists[c]->As<CommandListBase>();
        auto barriers = command_list_base.GetLazyBarriers();
        for (auto& barrier : barriers)
        {
            if (c == 0 && AllowCommonStatePromotion(barrier.resource, barrier.state_after))
                continue;
            auto& global_state_tracker = barrier.resource->GetGlobalResourceStateTracker();
            for (uint32_t i = 0; i < barrier.level_count; ++i)
            {
                for (uint32_t j = 0; j < barrier.layer_count; ++j)
                {
                    barrier.state_before = global_state_tracker.GetSubresourceState(barrier.base_mip_level + i, barrier.base_array_layer + j);
                    if (barrier.state_before != barrier.state_after)
                        new_barriers.emplace_back(barrier);
                }
            }
        }

        if (!new_barriers.empty())
        {
            std::shared_ptr<CommandList> tmp_cmd;
            if (c != 0 && kUseFakeClose)
            {
                tmp_cmd = command_lists[c - 1];
            }
            else
            {
                if (!m_fence_value_by_cmd.empty())
                {
                    auto& desc = m_fence_value_by_cmd.front();
                    if (m_fence->GetCompletedValue() >= desc.first)
                    {
                        m_fence->Wait(desc.first);
                        tmp_cmd = m_command_list_pool[desc.second];
                        tmp_cmd->Reset();
                        m_fence_value_by_cmd.pop_front();
                        m_fence_value_by_cmd.emplace_back(m_fence_value + 1, desc.second);
                    }
                }
                if (!tmp_cmd)
                {
                    tmp_cmd = m_command_list_pool.emplace_back(m_device.CreateCommandList(m_type));
                    m_fence_value_by_cmd.emplace_back(m_fence_value + 1, m_command_list_pool.size() - 1);
                }
                raw_command_lists.emplace_back(tmp_cmd);
            }

            auto& tmp_cmd_base = tmp_cmd->As<CommandListBase>();
            tmp_cmd_base.ResourceBarrierManual(new_barriers);
            tmp_cmd_base.Close();
            ++patch_cmds;
        }

        auto& state_trackers = command_list_base.GetResourceStateTrackers();
        for (const auto& state_tracker_pair : state_trackers)
        {
            auto& resource = state_tracker_pair.first;
            auto& state_tracker = state_tracker_pair.second;
            auto& global_state_tracker = resource->GetGlobalResourceStateTracker();
            for (uint32_t i = 0; i < resource->GetLevelCount(); ++i)
            {
                for (uint32_t j = 0; j < resource->GetLayerCount(); ++j)
                {
                    auto state = state_tracker.GetSubresourceState(i, j);
                    if (state != ResourceState::kUnknown)
                        global_state_tracker.SetSubresourceState(i, j, state);
                }
            }
        }

        raw_command_lists.emplace_back(command_lists[c]);
    }
    ExecuteCommandListsImpl(raw_command_lists);
    if (patch_cmds)
        Signal(m_fence, ++m_fence_value);
}
