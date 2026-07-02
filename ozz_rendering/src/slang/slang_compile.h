//
// Shared Slang compile pipeline used by both the Vulkan (SPIR-V) and WebGPU
// (WGSL) backends. Extracts the identical session/module/entry-point/composite/
// link/entry-point-code sequence that previously lived duplicated in
// RHIShaderVulkan::compileProgramSlang and RHIShaderWebGPU::compile.
//
// Only the target format and the post-processing of the returned entry-point
// code blobs (SPIR-V words vs. WGSL string, reflection, module creation) differ
// between the two backends; those steps stay at the call sites.
//

#pragma once

#include "ozz_rendering/rhi_shader.h"  // OZZ::rendering::ShaderDefine

#include <slang.h>

#include <optional>
#include <string>
#include <vector>

namespace OZZ::rendering::slang_compile {

    // Result of a successful CompileSlangProgram() call.
    //
    // Memory ownership mirrors the original hand-written code exactly:
    //  - Session is deliberately NOT released and must be kept alive by the
    //    caller (store it in the shader's slangCompileSession member). See the
    //    Slang 2026.8.1 release()-crash note in slang_compile.cpp.
    //  - Linked is a live reference the caller owns; call Linked->release() when
    //    done reflecting / extracting code from it.
    //  - VertexBlob / FragmentBlob are the entry-point code blobs for entry
    //    point 0 (vertex) and 1 (fragment) respectively, retained for the
    //    caller. Either may be null if that entry point was absent or its code
    //    generation failed. Call release() on each non-null blob when done.
    struct SlangCompileResult {
        ::slang::ISession*        Session {nullptr};        // kept alive deliberately; TODO(slang>2026.8.1)
        ::slang::IComponentType*  Linked {nullptr};         // caller owns; release() when done
        ISlangBlob*               VertexBlob {nullptr};     // entry point 0 code for the requested target
        ISlangBlob*               FragmentBlob {nullptr};   // entry point 1 code for the requested target
    };

    // Run the shared Slang compile pipeline for a single-module, two-entry-point
    // (vertexMain / fragmentMain) shader.
    //
    // On success returns a populated SlangCompileResult. On failure returns
    // std::nullopt; in that case any session that was created is still handed
    // back to the caller via outSession so the caller can keep it alive to avoid
    // the corrupt-free crash (see slang_compile.cpp). outSession is left
    // untouched when no session could be created.
    //
    // Any Slang diagnostics text (module-load blob) is appended to
    // outDiagnostics; call sites log it however they prefer. globalSession,
    // source and defines must outlive the call. defines' name/value strings are
    // referenced by pointer during compilation and so must outlive it too.
    std::optional<SlangCompileResult> CompileSlangProgram(
        ::slang::IGlobalSession*         globalSession,
        SlangCompileTarget               target,
        const std::string&               source,
        const std::vector<ShaderDefine>& defines,
        std::string&                     outDiagnostics,
        ::slang::ISession*&              outSession,
        const std::string&               vertexEntryPoint   = "vertexMain",
        const std::string&               fragmentEntryPoint = "fragmentMain");

} // namespace OZZ::rendering::slang_compile
