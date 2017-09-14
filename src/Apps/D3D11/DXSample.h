#pragma once

#include <d3d11.h>
#include <DXGI1_4.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

#include <map>
#include <memory>
#include <string>

#include <Util.h>
#include <Geometry/Geometry.h>
#include "DX11Geometry.h"

#include <Program/DX11Program.h>

#include <ISample/ISample.h>
#include <simple_camera/camera.h>

using namespace Microsoft::WRL;
using namespace DirectX;

class DXSample : public ISample
{
public:
    DXSample();
    ~DXSample();

    static std::unique_ptr<ISample> Create()
    {
        return std::make_unique<DXSample>();
    }

    virtual void OnInit(int width, int height) override;
    virtual void OnUpdate() override;
    void GeometryPass();
    void LightPass();
    virtual void OnRender() override;
    virtual void OnDestroy() override;
    virtual void OnSizeChanged(int width, int height) override;

    virtual void OnKey(int key, int action) override;
    virtual void OnMouse(bool first_event, double xpos, double ypos) override;

private:
    void CreateDeviceAndSwapChain();
    void CreateRT();
    void CreateViewPort();
    void CreateSampler();
    void UpdateCameraMovement();

    std::unique_ptr<Model<DX11Mesh>> CreateGeometry(const std::string & path);

    void CreateRTV(ComPtr<ID3D11RenderTargetView>& rtv, ComPtr<ID3D11ShaderResourceView>& srv);

    void InitGBuffer();

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_device_context;
    ComPtr<IDXGISwapChain> m_swap_chain;

    ComPtr<ID3D11RenderTargetView> m_render_target_view;
    ComPtr<ID3D11DepthStencilView> m_depth_stencil_view;
    ComPtr<ID3D11Texture2D> m_depth_stencil_buffer;

    D3D11_VIEWPORT m_viewport;

    ComPtr<ID3D11SamplerState> m_texture_sampler;

    std::unique_ptr<DXShader> m_shader_geometry_pass;
    std::unique_ptr<DXShader> m_shader_light_pass;

    std::unique_ptr<Model<DX11Mesh>> m_model_of_file;
    std::unique_ptr<Model<DX11Mesh>> m_model_square;

    ComPtr<ID3D11RenderTargetView> m_position_rtv;
    ComPtr<ID3D11RenderTargetView> m_normal_rtv;
    ComPtr<ID3D11RenderTargetView> m_ambient_rtv;
    ComPtr<ID3D11RenderTargetView> m_diffuse_rtv;
    ComPtr<ID3D11RenderTargetView> m_specular_rtv;
    ComPtr<ID3D11RenderTargetView> m_gloss_rtv;

    ComPtr<ID3D11ShaderResourceView> m_position_srv;
    ComPtr<ID3D11ShaderResourceView> m_normal_srv;
    ComPtr<ID3D11ShaderResourceView> m_ambient_srv;
    ComPtr<ID3D11ShaderResourceView> m_diffuse_srv;
    ComPtr<ID3D11ShaderResourceView> m_specular_srv;
    ComPtr<ID3D11ShaderResourceView> m_gloss_srv;

    Camera camera_;
    std::map<int, bool> keys_;
    float last_frame_ = 0.0;
    float delta_time_ = 0.0f;
    double last_x_ = 0.0f;
    double last_y_ = 0.0f;

    int m_width;
    int m_height;
    const bool m_use_rotare = true;
};