#include "RayTracingAOPass.h"

RayTracingAOPass::RayTracingAOPass(Context& context, const Input& input, int width, int height)
    : m_context(context)
    , m_input(input)
    , m_width(width)
    , m_height(height)
    , m_raytracing_program(context, [&](auto& program)
    {
        program.lib.desc.define["SAMPLE_COUNT"] = std::to_string(m_settings.msaa_count);
    })
    , m_program_blur(context)
{
    CreateSizeDependentResources();
    m_sampler = m_context.CreateSampler({
        SamplerFilter::kAnisotropic,
        SamplerTextureAddressMode::kWrap,
        SamplerComparisonFunc::kNever 
    });

    std::vector<glm::uvec4> data;
    for (auto& model : m_input.scene_list)
    {
        for (auto& range : model.ia.ranges)
        {
            auto& material = model.GetMaterial(range.id);
            glm::uvec4 info = {};
            ViewDesc view_desc = {};
            view_desc.bindless = true;
            view_desc.dimension = ResourceDimension::kTexture2D;
            view_desc.view_type = ViewType::kShaderResource;
            m_views.emplace_back(m_context.CreateView(material.texture.opacity, view_desc));
            info.x = m_views.back()->GetDescriptorId();

            view_desc.bindless = true;
            view_desc.dimension = ResourceDimension::kBuffer;
            view_desc.view_type = ViewType::kShaderResource;
            view_desc.offset = sizeof(uint32_t) * range.start_index_location;
            view_desc.stride = sizeof(uint32_t);
            m_views.emplace_back(m_context.CreateView(model.ia.indices.GetBuffer(), view_desc));
            info.y = m_views.back()->GetDescriptorId();

            view_desc.bindless = true;
            view_desc.dimension = ResourceDimension::kBuffer;
            view_desc.view_type = ViewType::kShaderResource;
            view_desc.offset = sizeof(glm::vec2) * range.base_vertex_location;
            view_desc.stride = sizeof(glm::vec2);
            m_views.emplace_back(m_context.CreateView(model.ia.texcoords.IsDynamic() ? model.ia.texcoords.GetDynamicBuffer() : model.ia.texcoords.GetBuffer(), view_desc));
            info.z = m_views.back()->GetDescriptorId();

            data.emplace_back(info);
        }
    }

    m_buffer = m_context.CreateBuffer(BindFlag::kShaderResource, sizeof(glm::uvec4) * data.size());
    m_context->UpdateSubresource(m_buffer, 0, data.data());
}

void RayTracingAOPass::OnUpdate()
{
}

void RayTracingAOPass::OnRender()
{
    if (!m_settings.use_rtao)
        return;

    m_raytracing_program.lib.cbuffer.Settings.ao_radius = m_settings.ao_radius;
    m_raytracing_program.lib.cbuffer.Settings.num_rays = m_settings.rtao_num_rays;
    m_raytracing_program.lib.cbuffer.Settings.use_alpha_test = m_settings.use_alpha_test;

    auto build_geometry = [&](bool force_rebuild)
    {
        size_t id = 0;
        size_t node_updated = 0;
        for (auto& model : m_input.scene_list)
        {
            for (auto& range : model.ia.ranges)
            {
                size_t cur_id = id++;
                if (!force_rebuild && !model.ia.positions.IsDynamic())
                    continue;

                BufferDesc vertex = {
                    model.ia.positions.IsDynamic() ? model.ia.positions.GetDynamicBuffer() : model.ia.positions.GetBuffer(),
                    gli::format::FORMAT_RGB32_SFLOAT_PACK32,
                    model.ia.positions.Count() - range.base_vertex_location,
                    range.base_vertex_location
                };

                BufferDesc index = {
                     model.ia.indices.GetBuffer(),
                     model.ia.indices.Format(),
                     range.index_count,
                     range.start_index_location
                };

                if (cur_id >= m_bottom.size())
                    m_bottom.resize(cur_id + 1);

                if (cur_id >= m_geometry.size())
                    m_geometry.resize(cur_id + 1);

                m_bottom[cur_id] = m_context->CreateBottomLevelAS(vertex, index);
                m_geometry[cur_id] = { m_bottom[cur_id], glm::transpose(model.matrix) };
                ++node_updated;
            }
        }
        if (node_updated)
            m_top = m_context->CreateTopLevelAS(m_geometry);
    };

    build_geometry(!m_is_initialized);

    if (!m_is_initialized)
        m_raytracing_program.lib.cbuffer.Settings.frame_index = 0;
    else
        ++m_raytracing_program.lib.cbuffer.Settings.frame_index;
    m_is_initialized = true;

    m_context->UseProgram(m_raytracing_program);
    m_context->Attach(m_raytracing_program.lib.cbv.Settings, m_raytracing_program.lib.cbuffer.Settings);
    m_context->Attach(m_raytracing_program.lib.srv.gPosition, m_input.geometry_pass.position);
    m_context->Attach(m_raytracing_program.lib.srv.gNormal, m_input.geometry_pass.normal);
    m_context->Attach(m_raytracing_program.lib.srv.geometry, m_top);
    m_context->Attach(m_raytracing_program.lib.uav.result, m_ao);
    m_context->Attach(m_raytracing_program.lib.sampler.gSampler, m_sampler);
    m_context->Attach(m_raytracing_program.lib.srv.descriptor_offset, m_buffer);
    m_context->DispatchRays(m_width, m_height, 1);

    if (m_settings.use_ao_blur)
    {
        m_context->UseProgram(m_program_blur);
        m_context->Attach(m_program_blur.ps.uav.out_uav, m_ao_blur);

        m_input.square.ia.indices.Bind();
        m_input.square.ia.positions.BindToSlot(m_program_blur.vs.ia.POSITION);
        m_input.square.ia.texcoords.BindToSlot(m_program_blur.vs.ia.TEXCOORD);
        for (auto& range : m_input.square.ia.ranges)
        {
            m_context->Attach(m_program_blur.ps.srv.ssaoInput, m_ao);
            m_context->DrawIndexed(range.index_count, range.start_index_location, range.base_vertex_location);
        }

        output.ao = m_ao_blur;
    }
    else
    {
        output.ao = m_ao;
    }
}

void RayTracingAOPass::OnResize(int width, int height)
{
    m_width = width;
    m_height = height;
    CreateSizeDependentResources();
}

void RayTracingAOPass::CreateSizeDependentResources()
{
    m_ao = m_context.CreateTexture(BindFlag::kShaderResource | BindFlag::kUnorderedAccess, gli::format::FORMAT_RGBA32_SFLOAT_PACK32, 1, m_width, m_height, 1);
    m_ao_blur = m_context.CreateTexture(BindFlag::kShaderResource | BindFlag::kUnorderedAccess, gli::format::FORMAT_RGBA32_SFLOAT_PACK32, 1, m_width, m_height, 1);
}

void RayTracingAOPass::OnModifySponzaSettings(const SponzaSettings& settings)
{
    SponzaSettings prev = m_settings;
    m_settings = settings;
    if (prev.msaa_count != m_settings.msaa_count)
    {
        m_raytracing_program.lib.desc.define["SAMPLE_COUNT"] = std::to_string(m_settings.msaa_count);
        m_raytracing_program.UpdateProgram();
    }
}
