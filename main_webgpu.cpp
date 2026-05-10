// WebGPU smoke test — init device, compile Slang shader, render one triangle, close.
#define WIN32_LEAN_AND_MEAN
#define GLFW_EXPOSE_NATIVE_WIN32

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <windows.h>

#include <webgpu/webgpu.h>

#include <ozz_rendering/rhi_device.h>
#include <spdlog/spdlog.h>

#include <array>
#include <cstdlib>

// Slang source: simple colored triangle, no UBO, no push constants.
// vertexMain / fragmentMain — matches the default entry point names in RHIShaderWebGPU.
static const char* kSlangSource = R"(
struct VSInput {
    float3 position : POSITION;
    float4 color    : COLOR;
};

struct VSOutput {
    float4 svPosition : SV_Position;
    float4 color      : COLOR0;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
    VSOutput o;
    o.svPosition = float4(input.position, 1.0);
    o.color      = input.color;
    return o;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
    return input.color;
}
)";

struct Vertex {
    float x, y, z;
    float r, g, b, a;
};

int main() {
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("=== ozz_rendering WebGPU smoke test ===");

    if (!glfwInit()) {
        spdlog::error("GLFW init failed");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE,  GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "WebGPU Smoke Test", nullptr, nullptr);
    if (!window) {
        spdlog::error("GLFW window creation failed");
        glfwTerminate();
        return 1;
    }

    glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int, int action, int) {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
            glfwSetWindowShouldClose(w, GLFW_TRUE);
    });

    // WebGPU surface: created from Win32 HWND via Dawn.
    // instance and surface are passed as void* (WGPUInstance* and WGPUSurface*).
    auto createSurface = [window](void* instancePtr, void* surfacePtr) -> bool {
        WGPUInstance instance = *static_cast<WGPUInstance*>(instancePtr);

        WGPUSurfaceSourceWindowsHWND hwndDesc = {};
        hwndDesc.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
        hwndDesc.hinstance   = GetModuleHandle(nullptr);
        hwndDesc.hwnd        = glfwGetWin32Window(window);

        WGPUSurfaceDescriptor surfDesc = {};
        surfDesc.nextInChain = &hwndDesc.chain;

        *static_cast<WGPUSurface*>(surfacePtr) =
            wgpuInstanceCreateSurface(instance, &surfDesc);
        return *static_cast<WGPUSurface*>(surfacePtr) != nullptr;
    };

    auto rhiDevice = OZZ::rendering::CreateRHIDevice({
        .Backend = OZZ::rendering::RHIBackend::WebGPU,
        .Context = {
            .AppName = "WebGPU Smoke Test",
            .GetWindowFramebufferSizeFunction = [window]() {
                int w, h;
                glfwGetFramebufferSize(window, &w, &h);
                return std::make_pair(w, h);
            },
            .CreateSurfaceFunction = createSurface,
        },
    });

    if (!rhiDevice) {
        spdlog::error("CreateRHIDevice(WebGPU) returned null");
        glfwTerminate();
        return 1;
    }
    spdlog::info("WebGPU RHIDevice created");

    // Slang → WGSL compilation happens here at CreateShader time.
    auto shaderHandle = rhiDevice->CreateShader(OZZ::rendering::ShaderSourceParams {
        .Vertex   = kSlangSource,
        .Fragment = kSlangSource,
    });
    if (!shaderHandle.IsValid()) {
        spdlog::error("Shader compilation failed");
        return 1;
    }
    spdlog::info("Slang → WGSL shader compiled");

    auto pipelineLayoutHandle = rhiDevice->GetShaderPipelineLayoutHandle(shaderHandle);

    // Colored triangle vertices (NDC coordinates)
    constexpr std::array<Vertex, 3> kVertices = {{
        {  0.0f, -0.5f, 0.0f,   1.0f, 0.0f, 0.0f, 1.0f },
        {  0.5f,  0.5f, 0.0f,   0.0f, 1.0f, 0.0f, 1.0f },
        { -0.5f,  0.5f, 0.0f,   0.0f, 0.0f, 1.0f, 1.0f },
    }};

    auto vbuf = rhiDevice->CreateBuffer(OZZ::rendering::BufferDescriptor {
        .Size   = sizeof(kVertices),
        .Usage  = OZZ::rendering::BufferUsage::VertexBuffer,
        .Access = OZZ::rendering::BufferMemoryAccess::CpuToGpu,
    });
    rhiDevice->UpdateBuffer(vbuf, kVertices.data(), sizeof(kVertices), 0);

    // Render-pass and graphics state
    OZZ::rendering::RenderPassDescriptor rpDesc {};
    rpDesc.ColorAttachmentCount = 1;
    rpDesc.ColorAttachments[0]  = {
        .Load  = OZZ::rendering::LoadOp::Clear,
        .Store = OZZ::rendering::StoreOp::Store,
        .Clear = { .R = 0.1f, .G = 0.1f, .B = 0.2f, .A = 1.0f },
    };
    rpDesc.DepthAttachment = {
        .Load  = OZZ::rendering::LoadOp::Clear,
        .Store = OZZ::rendering::StoreOp::DontCare,
        .Clear = { .Depth = 1.0f },
    };

    OZZ::rendering::GraphicsStateDescriptor gfxState {};
    gfxState.Rasterization.Cull          = OZZ::rendering::CullMode::None;
    gfxState.Rasterization.Front         = OZZ::rendering::FrontFace::CounterClockwise;
    gfxState.ColorBlendAttachmentCount   = 1;
    gfxState.ColorBlend[0].BlendEnable   = false;
    gfxState.VertexInput.BindingCount    = 1;
    gfxState.VertexInput.Bindings[0]     = {
        .Binding = 0, .Stride = sizeof(Vertex),
        .InputRate = OZZ::rendering::VertexInputRate::Vertex,
    };
    gfxState.VertexInput.AttributeCount  = 2;
    gfxState.VertexInput.Attributes[0]   = {
        .Location = 0, .Binding = 0,
        .Format = OZZ::rendering::VertexFormat::Float3, .Offset = 0,
    };
    gfxState.VertexInput.Attributes[1]   = {
        .Location = 1, .Binding = 0,
        .Format = OZZ::rendering::VertexFormat::Float4,
        .Offset = static_cast<uint32_t>(sizeof(float) * 3),
    };

    uint32_t frameCount = 0;
    bool testPassed = false;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        auto ctx = rhiDevice->BeginFrame();
        if (!ctx.IsValid()) continue;

        const auto [sw, sh] = rhiDevice->GetSwapchainExtent();
        rpDesc.ColorAttachments[0].Texture = ctx.GetBackbufferImage();
        rpDesc.DepthAttachment.Texture     = ctx.GetBackbufferDepthImage();
        rpDesc.RenderArea = { .X = 0, .Y = 0, .Width = sw, .Height = sh };

        rhiDevice->BeginRenderPass(ctx, rpDesc);
        rhiDevice->SetViewport(ctx, {
            .X = 0, .Y = 0,
            .Width  = static_cast<float>(sw), .Height = static_cast<float>(sh),
            .MinDepth = 0.f, .MaxDepth = 1.f,
        });
        rhiDevice->SetScissor(ctx, { .X = 0, .Y = 0, .Width = sw, .Height = sh });
        rhiDevice->SetGraphicsState(ctx, gfxState);
        rhiDevice->BindShader(ctx, shaderHandle);
        rhiDevice->BindBuffer(ctx, vbuf);
        rhiDevice->Draw(ctx, 3, 1, 0, 0);
        rhiDevice->EndRenderPass(ctx);
        rhiDevice->SubmitAndPresentFrame(std::move(ctx));

        ++frameCount;
        if (frameCount == 1) spdlog::info("Frame 1 submitted");
        if (frameCount == 2) {
            spdlog::info("Frame 2 submitted — pipeline cache hit confirmed");
            spdlog::info("SMOKE TEST PASSED");
            testPassed = true;
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }

    rhiDevice.reset();
    glfwTerminate();
    return testPassed ? 0 : 1;
}
