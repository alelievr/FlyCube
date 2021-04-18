#include "ShaderReflection/DXILReflection.h"
#include <Utilities/DXUtility.h>
#include <Utilities/FileUtility.h>
#include <HLSLCompiler/DXCLoader.h>
#include <assert.h>
#include <dxc/DXIL/DxilConstants.h>
#include <dxc/DxilContainer/DxilContainer.h>
#include <dxc/DxilContainer/DxilRuntimeReflection.inl>
#include <dia2.h>
#include <algorithm>
#include <set>

ShaderKind ConvertShaderKind(hlsl::DXIL::ShaderKind kind)
{
    switch (kind)
    {
    case hlsl::DXIL::ShaderKind::Pixel:
        return ShaderKind::kPixel;
    case hlsl::DXIL::ShaderKind::Vertex:
        return ShaderKind::kVertex;
    case hlsl::DXIL::ShaderKind::Geometry:
        return ShaderKind::kGeometry;
    case hlsl::DXIL::ShaderKind::Compute:
        return ShaderKind::kCompute;
    case hlsl::DXIL::ShaderKind::RayGeneration:
        return ShaderKind::kRayGeneration;
    case hlsl::DXIL::ShaderKind::Intersection:
        return ShaderKind::kIntersection;
    case hlsl::DXIL::ShaderKind::AnyHit:
        return ShaderKind::kAnyHit;
    case hlsl::DXIL::ShaderKind::ClosestHit:
        return ShaderKind::kClosestHit;
    case hlsl::DXIL::ShaderKind::Miss:
        return ShaderKind::kMiss;
    case hlsl::DXIL::ShaderKind::Callable:
        return ShaderKind::kCallable;
    case hlsl::DXIL::ShaderKind::Mesh:
        return ShaderKind::kMesh;
    case hlsl::DXIL::ShaderKind::Amplification:
        return ShaderKind::kAmplification;
    }
    assert(false);
    return ShaderKind::kUnknown;
}

ComPtr<IDiaTable> FindTable(ComPtr<IDiaSession> session, const std::wstring& name)
{
    ComPtr<IDiaEnumTables> enum_tables;
    session->getEnumTables(&enum_tables);
    LONG count = 0;
    enum_tables->get_Count(&count);
    for (LONG i = 0; i < count; ++i)
    {
        ULONG fetched = 0;
        ComPtr<IDiaTable> table;
        enum_tables->Next(1, &table, &fetched);
        CComBSTR table_name;
        table->get_name(&table_name);
        if (table_name.m_str == name)
            return table;
    }
    return nullptr;
}

std::string FindStrValue(ComPtr<IDiaTable> table, const std::wstring& name)
{
    LONG count = 0;
    table->get_Count(&count);
    for (LONG i = 0; i < count; ++i)
    {
        CComPtr<IUnknown> item;
        table->Item(i, &item);
        CComPtr<IDiaSymbol> symbol;
        if (FAILED(item.QueryInterface(&symbol)))
            continue;

        CComBSTR item_name;
        symbol->get_name(&item_name);
        if (!item_name || item_name.m_str != name)
            continue;

        VARIANT value = {};
        symbol->get_value(&value);
        if (value.vt == VT_BSTR)
            return wstring_to_utf8(value.bstrVal);
    }
    return "";
}

DXILReflection::DXILReflection(const void* data, size_t size)
{
    decltype(auto) dxc_support = GetDxcSupport(ShaderBlobType::kDXIL);
    ComPtr<IDxcLibrary> library;
    dxc_support.CreateInstance(CLSID_DxcLibrary, library.GetAddressOf());
    ComPtr<IDxcBlobEncoding> source;
    ASSERT_SUCCEEDED(library->CreateBlobWithEncodingOnHeapCopy(data, size, CP_ACP, &source));
    ComPtr<IDxcContainerReflection> reflection;
    dxc_support.CreateInstance(CLSID_DxcContainerReflection, reflection.GetAddressOf());
    ASSERT_SUCCEEDED(reflection->Load(source.Get()));

    ComPtr<IDxcBlob> pdb;
    uint32_t part_count = 0;
    ASSERT_SUCCEEDED(reflection->GetPartCount(&part_count));
    for (uint32_t i = 0; i < part_count; ++i)
    {
        uint32_t kind = 0;
        ASSERT_SUCCEEDED(reflection->GetPartKind(i, &kind));
        if (kind == hlsl::DxilFourCC::DFCC_RuntimeData)
        {
            ParseRuntimeData(reflection, i);
        }
        else if (kind == hlsl::DxilFourCC::DFCC_DXIL)
        {
            ComPtr<ID3D12ShaderReflection> shader_reflection;
            ComPtr<ID3D12LibraryReflection> library_reflection;
            if (SUCCEEDED(reflection->GetPartReflection(i, IID_PPV_ARGS(&shader_reflection))))
            {
                ParseShaderReflection(shader_reflection);
            }
            else if (SUCCEEDED(reflection->GetPartReflection(i, IID_PPV_ARGS(&library_reflection))))
            {
                m_is_library = true;
                ParseLibraryReflection(library_reflection);
            }
        }
        else if (kind == hlsl::DxilFourCC::DFCC_ShaderDebugInfoDXIL)
        {
            reflection->GetPartContent(i, &pdb);
        }
    }

    if (pdb && !m_is_library)
    {
        ParseDebugInfo(dxc_support, pdb);
    }
}

const std::vector<EntryPoint>& DXILReflection::GetEntryPoints() const
{
    return m_entry_points;
}

const std::vector<ResourceBindingDesc>& DXILReflection::GetBindings() const
{
    return m_bindings;
}

const std::vector<VariableLayout>& DXILReflection::GetVariableLayouts() const
{
    return m_layouts;
}

const std::vector<InputParameterDesc>& DXILReflection::GetInputParameters() const
{
    return m_input_parameters;
}

const std::vector<OutputParameterDesc>& DXILReflection::GetOutputParameters() const
{
    return m_output_parameters;
}

void DXILReflection::ParseRuntimeData(ComPtr<IDxcContainerReflection> reflection, uint32_t idx)
{
    ComPtr<IDxcBlob> part_blob;
    reflection->GetPartContent(idx, &part_blob);
    hlsl::RDAT::DxilRuntimeData context;
    context.InitFromRDAT(part_blob->GetBufferPointer(), part_blob->GetBufferSize());
    hlsl::RDAT::FunctionTableReader* func_table_reader = context.GetFunctionTableReader();
    for (uint32_t j = 0; j < func_table_reader->GetNumFunctions(); ++j)
    {
        hlsl::RDAT::FunctionReader func_reader = func_table_reader->GetItem(j);
        auto kind = func_reader.GetShaderKind();
        m_entry_points.push_back({ func_reader.GetUnmangledName(), ConvertShaderKind(kind), func_reader.GetPayloadSizeInBytes(), func_reader.GetAttributeSizeInBytes() });
    }
}

bool IsBufferDimension(D3D_SRV_DIMENSION dimension)
{
    switch (dimension)
    {
    case D3D_SRV_DIMENSION_BUFFER:
        return true;
    case D3D_SRV_DIMENSION_TEXTURE1D:
    case D3D_SRV_DIMENSION_TEXTURE1DARRAY:
    case D3D_SRV_DIMENSION_TEXTURE2D:
    case D3D_SRV_DIMENSION_TEXTURE2DARRAY:
    case D3D_SRV_DIMENSION_TEXTURE2DMS:
    case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY:
    case D3D_SRV_DIMENSION_TEXTURE3D:
    case D3D_SRV_DIMENSION_TEXTURECUBE:
    case D3D_SRV_DIMENSION_TEXTURECUBEARRAY:
        return false;
    default:
        assert(false);
        return false;
    }
}

ViewType GetViewType(const D3D12_SHADER_INPUT_BIND_DESC& bind_desc)
{
    switch (bind_desc.Type)
    {
    case D3D_SIT_CBUFFER:
        return ViewType::kConstantBuffer;
    case D3D_SIT_SAMPLER:
        return ViewType::kSampler;
    case D3D_SIT_TEXTURE:
    {
        if (IsBufferDimension(bind_desc.Dimension))
        {
            return ViewType::kBuffer;
        }
        else
        {
            return ViewType::kTexture;
        }
    }
    case D3D_SIT_STRUCTURED:
        return ViewType::kStructuredBuffer;
    case D3D_SIT_RTACCELERATIONSTRUCTURE:
        return ViewType::kAccelerationStructure;
    case D3D_SIT_UAV_RWSTRUCTURED:
        return ViewType::kRWStructuredBuffer;
    case D3D_SIT_UAV_RWTYPED:
    {
        if (IsBufferDimension(bind_desc.Dimension))
        {
            return ViewType::kRWBuffer;
        }
        else
        {
            return ViewType::kRWTexture;
        }
    }
    default:
        assert(false);
        return ViewType::kUnknown;
    }
}

ViewDimension GetViewDimension(const D3D12_SHADER_INPUT_BIND_DESC& bind_desc)
{
    switch (bind_desc.Dimension)
    {
    case D3D_SRV_DIMENSION_UNKNOWN:
        return ViewDimension::kUnknown;
    case D3D_SRV_DIMENSION_BUFFER:
        return ViewDimension::kBuffer;
    case D3D_SRV_DIMENSION_TEXTURE1D:
        return ViewDimension::kTexture1D;
    case D3D_SRV_DIMENSION_TEXTURE1DARRAY:
        return ViewDimension::kTexture1DArray;
    case D3D_SRV_DIMENSION_TEXTURE2D:
        return ViewDimension::kTexture2D;
    case D3D_SRV_DIMENSION_TEXTURE2DARRAY:
        return ViewDimension::kTexture2DArray;
    case D3D_SRV_DIMENSION_TEXTURE2DMS:
        return ViewDimension::kTexture2DMS;
    case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY:
        return ViewDimension::kTexture2DMSArray;
    case D3D_SRV_DIMENSION_TEXTURE3D:
        return ViewDimension::kTexture3D;
    case D3D_SRV_DIMENSION_TEXTURECUBE:
        return ViewDimension::kTextureCube;
    case D3D_SRV_DIMENSION_TEXTURECUBEARRAY:
        return ViewDimension::kTextureCubeArray;
    default:
        assert(false);
        return ViewDimension::kUnknown;
    }
}

ReturnType GetReturnType(ViewType view_type, const D3D12_SHADER_INPUT_BIND_DESC& bind_desc)
{
    auto check_type = [&](ReturnType return_type)
    {
        switch (view_type)
        {
        case ViewType::kBuffer:
        case ViewType::kRWBuffer:
        case ViewType::kTexture:
        case ViewType::kRWTexture:
            assert(return_type != ReturnType::kUnknown);
            break;
        case ViewType::kAccelerationStructure:
            return ReturnType::kUnknown;
        default:
            assert(return_type == ReturnType::kUnknown);
            break;
        }
        return return_type;
    };

    switch (bind_desc.ReturnType)
    {
    case D3D_RETURN_TYPE_FLOAT:
        return check_type(ReturnType::kFloat);
    case D3D_RETURN_TYPE_UINT:
        return check_type(ReturnType::kUint);
    case D3D_RETURN_TYPE_SINT:
        return check_type(ReturnType::kInt);
    case D3D_RETURN_TYPE_DOUBLE:
        return check_type(ReturnType::kDouble);
    default:
        return check_type(ReturnType::kUnknown);
    }
}

template<typename T>
uint32_t GetStructureStride(ViewType view_type, const D3D12_SHADER_INPUT_BIND_DESC& bind_desc, T* reflection)
{
    switch (view_type)
    {
    case ViewType::kStructuredBuffer:
    case ViewType::kRWStructuredBuffer:
        break;
    default:
        return 0;
    }

    auto get_buffer_stride = [&](const std::string& name)
    {
        ID3D12ShaderReflectionConstantBuffer* cbuffer = reflection->GetConstantBufferByName(name.c_str());
        if (cbuffer)
        {
            D3D12_SHADER_BUFFER_DESC cbuffer_desc = {};
            if (SUCCEEDED(cbuffer->GetDesc(&cbuffer_desc)))
            {
                return cbuffer_desc.Size;
            }
        }
        return 0u;
    };
    uint32_t stride = get_buffer_stride(bind_desc.Name);
    if (!stride)
    {
        stride = get_buffer_stride(std::string(bind_desc.Name) + "[0]");
    }
    assert(stride);
    return stride;
}

template<typename T>
ResourceBindingDesc GetBindingDesc(const D3D12_SHADER_INPUT_BIND_DESC& bind_desc, T* reflection)
{
    ResourceBindingDesc desc = {};
    desc.name = bind_desc.Name;
    desc.type = GetViewType(bind_desc);
    desc.slot = bind_desc.BindPoint;
    desc.space = bind_desc.Space;
    desc.count = bind_desc.BindCount;
    if (desc.count == 0)
    {
        desc.count = std::numeric_limits<uint32_t>::max();
    }
    desc.dimension = GetViewDimension(bind_desc);
    desc.return_type = GetReturnType(desc.type, bind_desc);
    desc.structure_stride = GetStructureStride(desc.type, bind_desc, reflection);
    return desc;
}

VariableLayout GetVariableLayout(const std::string& name, uint32_t offset, uint32_t size, ID3D12ShaderReflectionType* variable_type)
{
    D3D12_SHADER_TYPE_DESC type_desc = {};
    variable_type->GetDesc(&type_desc);

    VariableLayout layout = {};
    layout.name = name;
    layout.offset = offset + type_desc.Offset;
    layout.size = size;
    layout.rows = type_desc.Rows;
    layout.columns = type_desc.Columns;
    layout.elements = type_desc.Elements;
    switch (type_desc.Type)
    {
    case D3D_SHADER_VARIABLE_TYPE::D3D_SVT_FLOAT:
        layout.type = VariableType::kFloat;
        break;
    case D3D_SHADER_VARIABLE_TYPE::D3D_SVT_INT:
        layout.type = VariableType::kInt;
        break;
    case D3D_SHADER_VARIABLE_TYPE::D3D_SVT_UINT:
        layout.type = VariableType::kUint;
        break;
    case D3D_SHADER_VARIABLE_TYPE::D3D_SVT_BOOL:
        layout.type = VariableType::kBool;
        break;
    default:
        assert(false);
        break;
    }
    return layout;
}

template<typename ReflectionType>
VariableLayout GetBufferLayout(const D3D12_SHADER_INPUT_BIND_DESC& bind_desc, ReflectionType* reflection)
{
    if (bind_desc.Type != D3D_SIT_CBUFFER)
    {
        return {};
    }
    ID3D12ShaderReflectionConstantBuffer* cbuffer = reflection->GetConstantBufferByName(bind_desc.Name);
    if (!cbuffer)
    {
        assert(false);
        return {};
    }

    D3D12_SHADER_BUFFER_DESC cbuffer_desc = {};
    cbuffer->GetDesc(&cbuffer_desc);

    VariableLayout layout = {};
    layout.name = bind_desc.Name;
    layout.type = VariableType::kStruct;
    layout.offset = 0;
    layout.size = cbuffer_desc.Size;
    for (UINT i = 0; i < cbuffer_desc.Variables; ++i)
    {
        ID3D12ShaderReflectionVariable* variable = cbuffer->GetVariableByIndex(i);
        D3D12_SHADER_VARIABLE_DESC variable_desc = {};
        variable->GetDesc(&variable_desc);
        layout.members.emplace_back(GetVariableLayout(variable_desc.Name, variable_desc.StartOffset, variable_desc.Size, variable->GetType()));
    }
    return layout;
}

template<typename T, typename U>
std::vector<ResourceBindingDesc> ParseReflection(const T& desc, U* reflection)
{
    std::vector<ResourceBindingDesc> res;
    res.reserve(desc.BoundResources);
    for (uint32_t i = 0; i < desc.BoundResources; ++i)
    {
        D3D12_SHADER_INPUT_BIND_DESC bind_desc = {};
        ASSERT_SUCCEEDED(reflection->GetResourceBindingDesc(i, &bind_desc));
        res.emplace_back(GetBindingDesc(bind_desc, reflection));
    }
    return res;
}

template<typename T, typename U>
std::vector<VariableLayout> ParseLayout(const T& desc, U* reflection)
{
    std::vector<VariableLayout> res;
    res.reserve(desc.BoundResources);
    for (uint32_t i = 0; i < desc.BoundResources; ++i)
    {
        D3D12_SHADER_INPUT_BIND_DESC bind_desc = {};
        ASSERT_SUCCEEDED(reflection->GetResourceBindingDesc(i, &bind_desc));
        res.emplace_back(GetBufferLayout(bind_desc, reflection));
    }
    return res;
}

std::vector<InputParameterDesc> ParseInputParameters(const D3D12_SHADER_DESC& desc, ComPtr<ID3D12ShaderReflection> shader_reflection)
{
    std::vector<InputParameterDesc> input_parameters;
    D3D12_SHADER_VERSION_TYPE type = static_cast<D3D12_SHADER_VERSION_TYPE>((desc.Version & 0xFFFF0000) >> 16);
    if (type != D3D12_SHADER_VERSION_TYPE::D3D12_SHVER_VERTEX_SHADER)
    {
        return input_parameters;
    }
    for (uint32_t i = 0; i < desc.InputParameters; ++i)
    {
        D3D12_SIGNATURE_PARAMETER_DESC param_desc = {};
        ASSERT_SUCCEEDED(shader_reflection->GetInputParameterDesc(i, &param_desc));
        decltype(auto) input = input_parameters.emplace_back();
        input.semantic_name = param_desc.SemanticName;
        if (param_desc.SemanticIndex)
            input.semantic_name += std::to_string(param_desc.SemanticIndex);
        input.location = i;
        if (param_desc.Mask == 1)
        {
            if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)
                input.format = gli::format::FORMAT_R32_UINT_PACK32;
            else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)
                input.format = gli::format::FORMAT_R32_SINT_PACK32;
            else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32)
                input.format = gli::format::FORMAT_R32_SFLOAT_PACK32;
        }
        else if (param_desc.Mask <= 3)
        {
            if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)
                input.format = gli::format::FORMAT_RG32_UINT_PACK32;
            else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)
                input.format = gli::format::FORMAT_RG32_SINT_PACK32;
            else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32)
                input.format = gli::format::FORMAT_RG32_SFLOAT_PACK32;
        }
        else if (param_desc.Mask <= 7)
        {
            if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)
                input.format = gli::format::FORMAT_RGB32_UINT_PACK32;
            else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)
                input.format = gli::format::FORMAT_RGB32_SINT_PACK32;
            else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32)
                input.format = gli::format::FORMAT_RGB32_SFLOAT_PACK32;
        }
        else if (param_desc.Mask <= 15)
        {
            if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)
                input.format = gli::format::FORMAT_RGBA32_UINT_PACK32;
            else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)
                input.format = gli::format::FORMAT_RGBA32_SINT_PACK32;
            else if (param_desc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32)
                input.format = gli::format::FORMAT_RGBA32_SFLOAT_PACK32;
        }
    }
    return input_parameters;
}

std::vector<OutputParameterDesc> ParseOutputParameters(const D3D12_SHADER_DESC& desc, ComPtr<ID3D12ShaderReflection> shader_reflection)
{
    std::vector<OutputParameterDesc> output_parameters;
    D3D12_SHADER_VERSION_TYPE type = static_cast<D3D12_SHADER_VERSION_TYPE>((desc.Version & 0xFFFF0000) >> 16);
    if (type != D3D12_SHADER_VERSION_TYPE::D3D12_SHVER_PIXEL_SHADER)
    {
        return output_parameters;
    }
    for (uint32_t i = 0; i < desc.OutputParameters; ++i)
    {
        D3D12_SIGNATURE_PARAMETER_DESC param_desc = {};
        ASSERT_SUCCEEDED(shader_reflection->GetOutputParameterDesc(i, &param_desc));
        assert(param_desc.SemanticName == std::string("SV_TARGET"));
        assert(param_desc.SystemValueType == D3D_NAME_TARGET);
        assert(param_desc.SemanticIndex == param_desc.Register);
        decltype(auto) output = output_parameters.emplace_back();
        output.slot = param_desc.Register;
    }
    return output_parameters;
}

void DXILReflection::ParseShaderReflection(ComPtr<ID3D12ShaderReflection> shader_reflection)
{
    D3D12_SHADER_DESC desc = {};
    ASSERT_SUCCEEDED(shader_reflection->GetDesc(&desc));
    hlsl::DXIL::ShaderKind kind = hlsl::GetVersionShaderType(desc.Version);
    m_entry_points.push_back({ "", ConvertShaderKind(kind) });
    m_bindings = ParseReflection(desc, shader_reflection.Get());
    m_layouts = ParseLayout(desc, shader_reflection.Get());
    assert(m_bindings.size() == m_layouts.size());
    m_input_parameters = ParseInputParameters(desc, shader_reflection);
    m_output_parameters = ParseOutputParameters(desc, shader_reflection);
}

void DXILReflection::ParseLibraryReflection(ComPtr<ID3D12LibraryReflection> library_reflection)
{
    D3D12_LIBRARY_DESC library_desc = {};
    ASSERT_SUCCEEDED(library_reflection->GetDesc(&library_desc));
    std::map<std::string, size_t> exist;
    for (uint32_t i = 0; i < library_desc.FunctionCount; ++i)
    {
        ID3D12FunctionReflection* function_reflection = library_reflection->GetFunctionByIndex(i);
        D3D12_FUNCTION_DESC function_desc = {};
        ASSERT_SUCCEEDED(function_reflection->GetDesc(&function_desc));
        auto function_bindings = ParseReflection(function_desc, function_reflection);
        auto function_layouts = ParseLayout(function_desc, function_reflection);
        assert(function_bindings.size() == function_layouts.size());
        for (size_t i = 0; i < function_bindings.size(); ++i)
        {
            auto it = exist.find(function_bindings[i].name);
            if (it == exist.end())
            {
                exist[function_bindings[i].name] = m_bindings.size();
                m_bindings.emplace_back(function_bindings[i]);
                m_layouts.emplace_back(function_layouts[i]);
            }
            else
            {
                assert(function_bindings[i] == m_bindings[it->second]);
                assert(function_layouts[i] == m_layouts[it->second]);
            }
        }
    }
}

void DXILReflection::ParseDebugInfo(dxc::DxcDllSupport& dxc_support, ComPtr<IDxcBlob> pdb)
{
    ComPtr<IDxcLibrary> library;
    ASSERT_SUCCEEDED(dxc_support.CreateInstance(CLSID_DxcLibrary, library.GetAddressOf()));
    ComPtr<IStream> stream;
    ASSERT_SUCCEEDED(library->CreateStreamFromBlobReadOnly(pdb.Get(), &stream));

    ComPtr<IDiaDataSource> dia;
    ASSERT_SUCCEEDED(dxc_support.CreateInstance(CLSID_DxcDiaDataSource, dia.GetAddressOf()));
    ASSERT_SUCCEEDED(dia->loadDataFromIStream(stream.Get()));
    ComPtr<IDiaSession> session;
    ASSERT_SUCCEEDED(dia->openSession(&session));

    ComPtr<IDiaTable> symbols_table = FindTable(session, L"Symbols");
    assert(m_entry_points.size() == 1);
    m_entry_points.front().name = FindStrValue(symbols_table, L"hlslEntry");
}
