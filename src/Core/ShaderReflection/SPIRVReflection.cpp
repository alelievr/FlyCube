#include "ShaderReflection/SPIRVReflection.h"

ShaderKind ConvertShaderKind(spv::ExecutionModel execution_model)
{
    switch (execution_model)
    {
    case spv::ExecutionModel::ExecutionModelVertex:
        return ShaderKind::kVertex;
    case spv::ExecutionModel::ExecutionModelFragment:
        return ShaderKind::kPixel;
    case spv::ExecutionModel::ExecutionModelGeometry:
        return ShaderKind::kGeometry;
    case spv::ExecutionModel::ExecutionModelGLCompute:
        return ShaderKind::kCompute;
    case spv::ExecutionModel::ExecutionModelRayGenerationNV:
        return ShaderKind::kRayGeneration;
    case spv::ExecutionModel::ExecutionModelIntersectionNV:
        return ShaderKind::kIntersection;
    case spv::ExecutionModel::ExecutionModelAnyHitNV:
        return ShaderKind::kAnyHit;
    case spv::ExecutionModel::ExecutionModelClosestHitNV:
        return ShaderKind::kClosestHit;
    case spv::ExecutionModel::ExecutionModelMissNV:
        return ShaderKind::kMiss;
    case spv::ExecutionModel::ExecutionModelCallableNV:
        return ShaderKind::kCallable;
    case spv::ExecutionModel::ExecutionModelTaskNV:
        return ShaderKind::kAmplification;
    case spv::ExecutionModel::ExecutionModelMeshNV:
        return ShaderKind::kMesh;
    }
    assert(false);
    return ShaderKind::kUnknown;
}

std::vector<InputParameterDesc> ParseInputParameters(const spirv_cross::Compiler& compiler)
{
    spirv_cross::ShaderResources resources = compiler.get_shader_resources();
    std::vector<InputParameterDesc> input_parameters;
    for (const auto& resource : resources.stage_inputs)
    {
        decltype(auto) input = input_parameters.emplace_back();
        input.location = compiler.get_decoration(resource.id, spv::DecorationLocation);
        input.semantic_name = compiler.get_decoration_string(resource.id, spv::DecorationHlslSemanticGOOGLE);
        decltype(auto) type = compiler.get_type(resource.base_type_id);
        if (type.basetype == spirv_cross::SPIRType::Float)
        {
            if (type.vecsize == 1)
                input.format = gli::format::FORMAT_R32_SFLOAT_PACK32;
            else if (type.vecsize == 2)
                input.format = gli::format::FORMAT_RG32_SFLOAT_PACK32;
            else if (type.vecsize == 3)
                input.format = gli::format::FORMAT_RGB32_SFLOAT_PACK32;
            else if (type.vecsize == 4)
                input.format = gli::format::FORMAT_RGBA32_SFLOAT_PACK32;
        }
        else if (type.basetype == spirv_cross::SPIRType::UInt)
        {
            if (type.vecsize == 1)
                input.format = gli::format::FORMAT_R32_UINT_PACK32;
            else if (type.vecsize == 2)
                input.format = gli::format::FORMAT_RG32_UINT_PACK32;
            else if (type.vecsize == 3)
                input.format = gli::format::FORMAT_RGB32_UINT_PACK32;
            else if (type.vecsize == 4)
                input.format = gli::format::FORMAT_RGBA32_UINT_PACK32;
        }
        else if (type.basetype == spirv_cross::SPIRType::Int)
        {
            if (type.vecsize == 1)
                input.format = gli::format::FORMAT_R32_SINT_PACK32;
            else if (type.vecsize == 2)
                input.format = gli::format::FORMAT_RG32_SINT_PACK32;
            else if (type.vecsize == 3)
                input.format = gli::format::FORMAT_RGB32_SINT_PACK32;
            else if (type.vecsize == 4)
                input.format = gli::format::FORMAT_RGBA32_SINT_PACK32;
        }
    }
    return input_parameters;
}

ViewType GetViewType(const spirv_cross::Compiler& compiler, const spirv_cross::SPIRType& type, uint32_t resource_id)
{
    switch (type.basetype)
    {
    case spirv_cross::SPIRType::SampledImage:
    {
        return ViewType::kShaderResource;
    }
    case spirv_cross::SPIRType::Image:
    {
        if (type.image.sampled == 2 && type.image.dim != spv::DimSubpassData)
            return ViewType::kUnorderedAccess;
        else
            return ViewType::kShaderResource;
    }
    case spirv_cross::SPIRType::Sampler:
    {
        return ViewType::kSampler;
    }
    case spirv_cross::SPIRType::Struct:
    {
        if (type.storage == spv::StorageClassUniform || type.storage == spv::StorageClassStorageBuffer)
        {
            if (compiler.has_decoration(type.self, spv::DecorationBufferBlock))
            {
                spirv_cross::Bitset flags = compiler.get_buffer_block_flags(resource_id);
                bool is_readonly = flags.get(spv::DecorationNonWritable);
                if (is_readonly)
                    return ViewType::kShaderResource;
                else
                    return ViewType::kUnorderedAccess;
            }
            else if (compiler.has_decoration(type.self, spv::DecorationBlock))
            {
                return ViewType::kConstantBuffer;
            }
        }
        else if (type.storage == spv::StorageClassPushConstant)
        {
            return ViewType::kConstantBuffer;
        }
        assert(false);
        return ViewType::kUnknown;
    }
    default:
        assert(false);
        return ViewType::kUnknown;
    }
}

ResourceDimension GetImageDimension(const spv::Dim& dim, const spirv_cross::SPIRType& resource_type)
{
    switch (dim)
    {
    case spv::Dim::Dim1D:
    {
        if (resource_type.image.arrayed)
            return ResourceDimension::kTexture1DArray;
        else
            return ResourceDimension::kTexture1D;
    }
    case spv::Dim::Dim2D:
    {
        if (resource_type.image.arrayed)
            return ResourceDimension::kTexture2DArray;
        else
            return ResourceDimension::kTexture2D;
    }
    case spv::Dim::Dim3D:
    {
        return ResourceDimension::kTexture3D;
    }
    case spv::Dim::DimCube:
    {
        if (resource_type.image.arrayed)
            return ResourceDimension::kTextureCubeArray;
        else
            return ResourceDimension::kTextureCube;
    }
    default:
        assert(false);
        return ResourceDimension::kUnknown;
    }
}

ResourceDimension GetResourceDimension(const spirv_cross::SPIRType& resource_type)
{
    if (resource_type.basetype == spirv_cross::SPIRType::BaseType::Struct)
    {
        return ResourceDimension::kBuffer;
    }
    else if (resource_type.basetype == spirv_cross::SPIRType::BaseType::AccelerationStructure)
    {
        return ResourceDimension::kRaytracingAccelerationStructure;
    }
    else if (resource_type.basetype == spirv_cross::SPIRType::BaseType::Image)
    {
        return GetImageDimension(resource_type.image.dim, resource_type);
    }
    else
    {
        return ResourceDimension::kUnknown;
    }
}

ReturnType GetReturnType(const spirv_cross::CompilerHLSL& compiler, const spirv_cross::SPIRType& resource_type)
{
    if (resource_type.basetype == spirv_cross::SPIRType::BaseType::Image)
    {
        auto& image_type = compiler.get_type(resource_type.image.type);
        if (image_type.basetype == spirv_cross::SPIRType::BaseType::Float)
        {
            return ReturnType::kFloat;
        }
        else if (image_type.basetype == spirv_cross::SPIRType::BaseType::UInt)
        {
            return ReturnType::kUint;
        }
        else if (image_type.basetype == spirv_cross::SPIRType::BaseType::Int)
        {
            return ReturnType::kSint;
        }
        assert(false);
    }
    return ReturnType::kUnknown;
}

ResourceBindingDesc GetBindingDesc(const spirv_cross::CompilerHLSL& compiler, const spirv_cross::Resource& resource)
{
    ResourceBindingDesc desc = {};
    decltype(auto) resource_type = compiler.get_type(resource.type_id);
    desc.name = resource.name;
    desc.type = GetViewType(compiler, resource_type, resource.id);
    desc.slot = compiler.get_decoration(resource.id, spv::DecorationBinding);
    desc.space = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
    desc.dimension = GetResourceDimension(resource_type);
    desc.return_type = GetReturnType(compiler, resource_type);
    desc.stride = compiler.get_declared_struct_size(resource_type);
    return desc;
}

std::vector<ResourceBindingDesc> ParseBindings(const spirv_cross::CompilerHLSL& compiler)
{
    std::vector<ResourceBindingDesc> bindings;
    spirv_cross::ShaderResources resources = compiler.get_shader_resources();
    auto enumerate_resources = [&](const spirv_cross::SmallVector<spirv_cross::Resource>& resources)
    {
        for (const auto& resource : resources)
        {
            bindings.emplace_back(GetBindingDesc(compiler, resource));
        }
    };
    enumerate_resources(resources.uniform_buffers);
    enumerate_resources(resources.storage_buffers);
    enumerate_resources(resources.storage_images);
    enumerate_resources(resources.separate_images);
    enumerate_resources(resources.separate_samplers);
    enumerate_resources(resources.atomic_counters);
    enumerate_resources(resources.acceleration_structures);
    return bindings;
}

SPIRVReflection::SPIRVReflection(const void* data, size_t size)
    : m_blob((const uint32_t*)data, (const uint32_t*)data + size / sizeof(uint32_t))
{
    spirv_cross::CompilerHLSL compiler(m_blob);
    auto entry_points = compiler.get_entry_points_and_stages();
    for (const auto& entry_point : entry_points)
    {
        m_entry_points.push_back({ entry_point.name.c_str(), ConvertShaderKind(entry_point.execution_model) });
    }
    m_bindings = ParseBindings(compiler);
    m_input_parameters = ParseInputParameters(compiler);
}

const std::vector<EntryPoint> SPIRVReflection::GetEntryPoints() const
{
    return m_entry_points;
}

const std::vector<ResourceBindingDesc> SPIRVReflection::GetBindings() const
{
    return m_bindings;
}

const std::vector<InputParameterDesc> SPIRVReflection::GetInputParameters() const
{
    return m_input_parameters;
}