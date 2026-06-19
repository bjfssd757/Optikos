#include "WebGPURenderer.hpp"

#include <dawn/native/DawnNative.h>
#include <webgpu/webgpu_cpp.h>

#include <iostream>
#include <ostream>
#include <shader/WGSL/WGSLShader.hpp>
#include <utility>

#include "render/IRenderQueue.hpp"

#if defined(_WIN32)
#include <windows.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif

namespace Optikos
{

struct OptikosVertex
{
    float position[2];    // x, y
    float color[4];       // r, g, b, a (normalized 0.0-1.0)
    float texCoord[2];    // u, v
    float fontParams[4];  // fw, tw, fh, th
};

struct RenderUniform
{
    float   uScreenSize[2];
    int32_t uHasTexture;
    int32_t padding;
};

WebGPURenderer::~WebGPURenderer() = default;

WebGPURenderer::WebGPURenderer(IWindow* window, std::unique_ptr<IShader> shader)
{
    m_window = window;
    m_shader = std::move(shader);

    createInstance();
    createSurface();
    createAdapter();
    createDevice();
    createQueue();
    createBuffers();

    std::vector<unsigned char> defaultTextureData = {255, 255, 255, 255};
    loadTexture(defaultTextureData, 1, 1);
}

unsigned int WebGPURenderer::loadTexture(const std::vector<unsigned char>& data, int width,
                                         int height)
{
    if (data.empty() || width <= 0 || height <= 0) return 0;

    wgpu::TextureDescriptor textureDesc{};
    textureDesc.label = wgpu::StringView(std::format("{} Loaded Texture", APP_NAME));
    textureDesc.dimension = wgpu::TextureDimension::e2D;
    textureDesc.size = wgpu::Extent3D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
    textureDesc.format = wgpu::TextureFormat::RGBA8Unorm;
    textureDesc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    textureDesc.mipLevelCount = 1;
    textureDesc.sampleCount = 1;

    wgpu::Texture texture = m_device.CreateTexture(&textureDesc);

    bool isFont = (data.size() == static_cast<size_t>(width * height));
    std::vector<unsigned char> rgbaData;

    const unsigned char* uploadPtr = data.data();
    size_t uploadSize = data.size();

    if (isFont) {
        rgbaData.resize(width * height * 4);
        for (int i = 0; i < width * height; ++i) {
            unsigned char alpha = data[i];
            rgbaData[i * 4 + 0] = 255;   // R
            rgbaData[i * 4 + 1] = 255;   // G
            rgbaData[i * 4 + 2] = 255;   // B
            rgbaData[i * 4 + 3] = alpha; // Alpha
        }
        uploadPtr = rgbaData.data();
        uploadSize = rgbaData.size();
    }

    wgpu::TexelCopyTextureInfo texInfo{};
    texInfo.texture = texture;
    texInfo.mipLevel = 0;
    texInfo.origin = wgpu::Origin3D{ 0, 0, 0 };
    texInfo.aspect = wgpu::TextureAspect::All;

    wgpu::TexelCopyBufferLayout texLayout{};
    texLayout.offset = 0;
    texLayout.bytesPerRow = static_cast<uint32_t>(width) * 4;
    texLayout.rowsPerImage = static_cast<uint32_t>(height);

    wgpu::Extent3D writeSize = wgpu::Extent3D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };

    m_queue.WriteTexture(&texInfo, uploadPtr, uploadSize, &texLayout, &writeSize);

    wgpu::TextureViewDescriptor viewDesc{};
    viewDesc.label = wgpu::StringView(std::format("{} Texture View", APP_NAME));
    viewDesc.format = wgpu::TextureFormat::RGBA8Unorm;
    viewDesc.dimension = wgpu::TextureViewDimension::e2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = wgpu::TextureAspect::All;

    wgpu::TextureView textureView = texture.CreateView(&viewDesc);

    unsigned int id = m_nextTextureId++;
    m_textures[id] = std::move(textureView);

    return id;
}

void WebGPURenderer::flush()
{
    if (m_renderQueue.getCommands().empty() || !m_currentEncoder) return;

    m_passEncoder.SetPipeline(m_pipeline);
    m_passEncoder.SetVertexBuffer(0, m_vertexBuffer);
    m_passEncoder.SetIndexBuffer(m_indexBuffer, wgpu::IndexFormat::Uint32);

    auto& queue = m_renderQueue.getCommands();

    std::vector<OptikosVertex> vertices;
    std::vector<uint32_t>      indices;

    uint32_t indexOffset = 0;

    for (const auto& cmd : queue)
    {
        for (const auto& vertex : cmd.vertices)
        {
            OptikosVertex v{};
            v.position[0] = vertex.x;
            v.position[1] = vertex.y;

            v.color[0] = vertex.r / 255.0f;
            v.color[1] = vertex.g / 255.0f;
            v.color[2] = vertex.b / 255.0f;
            v.color[3] = vertex.a / 255.0f;

            v.texCoord[0] = vertex.u;
            v.texCoord[1] = vertex.v;

            v.fontParams[0] = vertex.fw;
            v.fontParams[1] = vertex.tw;
            v.fontParams[2] = vertex.fh;
            v.fontParams[3] = vertex.th;

            vertices.push_back(v);
        }

        for (auto idx : cmd.indices)
        {
            indices.push_back(idx + indexOffset);
        }

        indexOffset += cmd.vertices.size();
    }

    if (!vertices.empty())
    {
        m_queue.WriteBuffer(m_vertexBuffer, 0, vertices.data(),
                            vertices.size() * sizeof(OptikosVertex));
        m_queue.WriteBuffer(m_indexBuffer, 0, indices.data(), indices.size() * sizeof(uint32_t));
    }

    float screenWidth  = static_cast<float>(m_window->getWidth());
    float screenHeight = static_cast<float>(m_window->getHeight());

    RenderUniform uniformData{};
    uniformData.uHasTexture =
        (!queue.empty() && queue[0].textureId != 0) ? queue[0].textureMode : 0;
    uniformData.uScreenSize[0] = screenWidth;
    uniformData.uScreenSize[1] = screenHeight;

    m_queue.WriteBuffer(m_uniformBuffer, 0, &uniformData, sizeof(RenderUniform));

    wgpu::BindGroupLayout bindGroupLayout = m_pipeline.GetBindGroupLayout(0);

    unsigned int      activeTextureId = !queue.empty() ? queue[0].textureId : 0;
    wgpu::TextureView currentView     = nullptr;

    auto it = m_textures.find(activeTextureId);
    if (it != m_textures.end())
    {
        currentView = it->second;
    }
    else if (!m_textures.empty())
    {
        currentView = m_textures.begin()->second;
    }

    if (currentView)
    {
        std::vector<wgpu::BindGroupEntry> entries(3);

        entries[0].binding = 0;
        entries[0].buffer  = m_uniformBuffer;
        entries[0].offset  = 0;
        entries[0].size    = sizeof(RenderUniform);

        entries[1].binding     = 1;
        entries[1].textureView = currentView;

        if (m_defaultSampler == nullptr)
        {
            wgpu::SamplerDescriptor samplerDesc{};
            m_defaultSampler = m_device.CreateSampler(&samplerDesc);
        }
        entries[2].binding = 2;
        entries[2].sampler = m_defaultSampler;

        wgpu::BindGroupDescriptor bindGroupDesc{};
        bindGroupDesc.label      = wgpu::StringView(std::format("{} Main Bind Group", APP_NAME));
        bindGroupDesc.layout     = bindGroupLayout;
        bindGroupDesc.entryCount = entries.size();
        bindGroupDesc.entries    = entries.data();

        wgpu::BindGroup bindGroup = m_device.CreateBindGroup(&bindGroupDesc);

        m_passEncoder.SetBindGroup(0, bindGroup);
    }

    if (!indices.empty() && currentView)
    {
        m_passEncoder.DrawIndexed(static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
    }

    m_renderQueue.clear();
}

void WebGPURenderer::beginFrame()
{
    if (m_pipeline == nullptr)
    {
        createRenderPipeline();
    }
    m_renderQueue.clear();

    wgpu::CommandEncoderDescriptor encoderDesc{};
    m_currentEncoder = m_device.CreateCommandEncoder(&encoderDesc);

    wgpu::SurfaceTexture surfaceTexture;
    m_surface.GetCurrentTexture(&surfaceTexture);
    wgpu::TextureView screenView = surfaceTexture.texture.CreateView();

    wgpu::RenderPassColorAttachment colorAttachment{};
    colorAttachment.view       = screenView;
    colorAttachment.loadOp     = wgpu::LoadOp::Clear;
    colorAttachment.storeOp    = wgpu::StoreOp::Store;
    colorAttachment.clearValue = wgpu::Color{0.1f, 0.1f, 0.11f, 1.0f};

    wgpu::RenderPassDescriptor renderPassDesc{};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments     = &colorAttachment;

    m_passEncoder = m_currentEncoder.BeginRenderPass(&renderPassDesc);
}

void WebGPURenderer::endFrame()
{
    flush();

    if (!m_currentEncoder) return;

    if (m_passEncoder)
    {
        m_passEncoder.End();
        m_passEncoder = nullptr;
    }

    wgpu::CommandBufferDescriptor cmdBufferDesc{};
    wgpu::CommandBuffer           commandBuffer = m_currentEncoder.Finish(&cmdBufferDesc);

    m_queue.Submit(1, &commandBuffer);
    m_currentEncoder = nullptr;
}

void WebGPURenderer::swap_buffer()
{
    m_surface.Present();
}

void WebGPURenderer::submit(const DrawCommand&& command)
{
    m_renderQueue.submit(std::move(command));
}

void WebGPURenderer::resetToDefault()
{
}
void WebGPURenderer::restoreStates()
{
}

IRenderQueue& WebGPURenderer::getRenderQueue()
{
    return m_renderQueue;
}

void WebGPURenderer::createRenderPipeline()
{
    std::vector<wgpu::VertexAttribute> vertexAttributes(4);

    vertexAttributes[0].format         = wgpu::VertexFormat::Float32x2;
    vertexAttributes[0].offset         = 0;
    vertexAttributes[0].shaderLocation = 0;

    vertexAttributes[1].format         = wgpu::VertexFormat::Float32x4;
    vertexAttributes[1].offset         = 2 * sizeof(float);
    vertexAttributes[1].shaderLocation = 1;

    vertexAttributes[2].format         = wgpu::VertexFormat::Float32x2;
    vertexAttributes[2].offset         = (2 + 4) * sizeof(float);
    vertexAttributes[2].shaderLocation = 2;

    vertexAttributes[3].format         = wgpu::VertexFormat::Float32x4;
    vertexAttributes[3].offset         = (2 + 4 + 2) * sizeof(float);
    vertexAttributes[3].shaderLocation = 3;

    wgpu::VertexBufferLayout vertexBufferLayout{};
    vertexBufferLayout.arrayStride    = (2 + 4 + 2 + 4) * sizeof(float);
    vertexBufferLayout.stepMode       = wgpu::VertexStepMode::Vertex;
    vertexBufferLayout.attributeCount = vertexAttributes.size();
    vertexBufferLayout.attributes     = vertexAttributes.data();

    wgpu::RenderPipelineDescriptor pipelineDesc{};
    pipelineDesc.label = wgpu::StringView(std::format("{} Main Pipeline", APP_NAME));

    auto wgslShader = static_cast<WGSLShader*>(m_shader.get());

    pipelineDesc.vertex.module      = wgslShader->getModule(m_defaultShader);
    pipelineDesc.vertex.entryPoint  = m_wgsl_vertex_entrypoint;
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers     = &vertexBufferLayout;

    wgpu::BlendState blend{};
    blend.color.operation = wgpu::BlendOperation::Add;
    blend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
    blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    blend.alpha.operation = wgpu::BlendOperation::Add;
    blend.alpha.srcFactor = wgpu::BlendFactor::One;
    blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;

    wgpu::ColorTargetState colorTarget{};
    colorTarget.format    = wgpu::TextureFormat::BGRA8Unorm;
    colorTarget.blend     = &blend;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;

    wgpu::FragmentState fragmentState{};
    fragmentState.module      = wgslShader->getModule(m_defaultShader);
    fragmentState.entryPoint  = m_wgsl_fragment_entrypoint;
    fragmentState.targetCount = 1;
    fragmentState.targets     = &colorTarget;

    pipelineDesc.fragment = &fragmentState;

    pipelineDesc.primitive.topology         = wgpu::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.stripIndexFormat = wgpu::IndexFormat::Undefined;
    pipelineDesc.primitive.frontFace        = wgpu::FrontFace::CCW;
    pipelineDesc.primitive.cullMode         = wgpu::CullMode::None;

    pipelineDesc.multisample.count                  = 1;
    pipelineDesc.multisample.mask                   = 0xFFFFFFFF;
    pipelineDesc.multisample.alphaToCoverageEnabled = false;

    pipelineDesc.layout = nullptr;

    m_pipeline = m_device.CreateRenderPipeline(&pipelineDesc);
}

void WebGPURenderer::onWindowResize(int width, int height)
{
    if (width == 0 || height == 0) return;

    wgpu::SurfaceConfiguration cfg{};
    cfg.device      = m_device;
    cfg.format      = wgpu::TextureFormat::BGRA8Unorm;
    cfg.width       = static_cast<uint32_t>(width);
    cfg.height      = static_cast<uint32_t>(height);
    cfg.usage       = wgpu::TextureUsage::RenderAttachment;
    cfg.presentMode = wgpu::PresentMode::Fifo;

    m_surface.Configure(&cfg);
}

void WebGPURenderer::createBuffers()
{
    wgpu::BufferDescriptor vertexBufDesc{};
    vertexBufDesc.label = wgpu::StringView(std::format("{} Vertex Buffer", APP_NAME));
    vertexBufDesc.size  = m_maxVertices * sizeof(OptikosVertex);
    vertexBufDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;

    m_vertexBuffer = m_device.CreateBuffer(&vertexBufDesc);

    wgpu::BufferDescriptor indexBufferDesc{};
    indexBufferDesc.label = wgpu::StringView(std::format("{} Index Buffer", APP_NAME));
    indexBufferDesc.size  = m_maxIndices * sizeof(uint32_t);
    indexBufferDesc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;

    m_indexBuffer = m_device.CreateBuffer(&indexBufferDesc);

    wgpu::BufferDescriptor uniformBufferDesc{};
    uniformBufferDesc.label = wgpu::StringView(std::format("{} Uniform Buffer", APP_NAME));
    uniformBufferDesc.size  = sizeof(RenderUniform);
    uniformBufferDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;

    m_uniformBuffer = m_device.CreateBuffer(&uniformBufferDesc);
}

void WebGPURenderer::createInstance()
{
    dawn::native::DawnInstanceDescriptor dawnDesc{};
    dawnDesc.backendValidationLevel = dawn::native::BackendValidationLevel::Full;

    wgpu::InstanceDescriptor instanceDesc{};
    instanceDesc.nextInChain = &dawnDesc;

    m_instance = wgpu::CreateInstance(&instanceDesc);
}

void WebGPURenderer::createSurface()
{
    wgpu::SurfaceDescriptor surfaceDesc{};
    surfaceDesc.label = APP_NAME;

#if defined(_WIN32)
    wgpu::SurfaceSourceWindowsHWND winDesc{};
    winDesc.sType           = wgpu::SType::SurfaceSourceWindowsHWND;
    winDesc.hwnd            = glfwGetWin32Window(m_window->getGLFWWindow());
    winDesc.hinstance       = GetModuleHandle(nullptr);
    surfaceDesc.nextInChain = &winDesc;
#elif defined(__APPLE__)
    wgpu::SurfaceSourceMetalLayer macDesc{};
    macDesc.sType           = wgpu::SType::SurfaceSourceMetalLayer;
    macDesc.layer           = getMetalLayerFromGLFW(glfwGetCocoaWindow(m_window->getGLFWWindow()));
    surfaceDesc.nextInChain = &macDesc;
#else
    if (m_window->isX11Active())
    {
        wgpu::SurfaceSourceXlibWindow x11Desc{};
        x11Desc.sType           = wgpu::SType::SurfaceSourceXlibWindow;
        x11Desc.display         = glfwGetX11Display();
        x11Desc.window          = glfwGetX11Window(m_window->getGLFWWindow());
        surfaceDesc.nextInChain = &x11Desc;
    }
    else
    {
        wgpu::SurfaceSourceWaylandWindow waylandDesc{};
        waylandDesc.sType       = wgpu::SType::SurfaceSourceWaylandWindow;
        waylandDesc.display     = glfwGetWaylandDisplay();
        waylandDesc.window      = glfwGetWaylandWindow(m_window->getGLFWWindow());
        surfaceDesc.nextInChain = &waylandDesc;
    }
#endif

    m_surface = m_instance.CreateSurface(&surfaceDesc);
}

void WebGPURenderer::createAdapter()
{
    wgpu::RequestAdapterOptions options{};
    options.powerPreference   = wgpu::PowerPreference::HighPerformance;
    options.compatibleSurface = m_surface;

    m_instance.RequestAdapter(
        &options, wgpu::CallbackMode::AllowProcessEvents,
        [](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message,
           wgpu::Adapter* userdata)
        {
            if (status != wgpu::RequestAdapterStatus::Success)
            {
                std::string msg =
                    "Can't find GPU. Status code: " + std::to_string(static_cast<int>(status));
                if (message.data && message.length > 0)
                {
                    msg.append("| Message: ");
                    msg.append(std::string_view(message.data, message.length));
                }
                LOG_ERROR(msg, "log");
                return;
            }

            *userdata = std::move(adapter);
        },
        &m_adapter);

    while (m_adapter == nullptr)
    {
        m_instance.ProcessEvents();
    }
}

void WebGPURenderer::createDevice()
{
    if (!m_adapter)
    {
        LOG_ERROR("[FATAL] createDevice() (WebGPU backend) called but m_adapter is null", "log");
        return;
    }

    std::vector<wgpu::FeatureName> requiredFeatures = {wgpu::FeatureName::Float32Filterable};

    wgpu::DeviceDescriptor deviceDesc{};
    deviceDesc.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message)
        {
            if (message.data)
            {
                std::string msg = "[Dawn Error] Device uncaptured error: ";
                msg.append(std::string_view(message.data, message.length));
                LOG_ERROR(msg, "log");
            }
        });
    deviceDesc.requiredFeatureCount = requiredFeatures.size();
    deviceDesc.requiredFeatures     = requiredFeatures.data();
    deviceDesc.label                = wgpu::StringView(std::format("{} Main Device", APP_NAME));

    m_adapter.RequestDevice(
        &deviceDesc, wgpu::CallbackMode::AllowProcessEvents,
        [](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message,
           wgpu::Device* userdata)
        {
            if (status != wgpu::RequestDeviceStatus::Success)
            {
                std::string msg = "Can't create WebGPU Device. Status: " +
                                  std::to_string(static_cast<int>(status));
                if (message.data && message.length > 0)
                {
                    msg.append(" | Error: ");
                    msg.append(std::string_view(message.data, message.length));
                }
                LOG_ERROR(msg, "log");
                return;
            }

            *userdata = std::move(device);
        },
        &m_device);

    while (m_device == nullptr)
    {
        m_instance.ProcessEvents();
    }

    if (auto* wgslShader = static_cast<WGSLShader*>(m_shader.get()))
    {
        wgslShader->setDevice(m_device);
    }

    m_device.SetLoggingCallback(
        [](wgpu::LoggingType type, const char* message)
        {
            LOG_ERROR("[Dawn GPU] Type: " + std::to_string(static_cast<int>(type)) +
                          "| Message: " + message,
                      "log");
        });

    if (auto* wgslShader = static_cast<WGSLShader*>(m_shader.get()))
    {
        wgslShader->setDevice(m_device);

        std::string shaderPath = std::string(OPTIKOS_SHADER_PATH) + "shader.wgsl";
        auto        sources    = wgslShader->parseShader(shaderPath);

        m_defaultShader = wgslShader->createShader(sources.vertexSource, sources.fragmentSource);

        LOG_INFO("[WebGPURenderer] Default WGSL shader compiled successfully with ID: " +
                     std::to_string(m_defaultShader),
                 "log");
    }
}

void WebGPURenderer::createQueue()
{
    m_queue = m_device.GetQueue();
}

}  // namespace Optikos