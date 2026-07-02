//
// Shared Slang compile pipeline. See slang_compile.h.
//

#include "slang/slang_compile.h"

namespace OZZ::rendering::slang_compile {

    std::optional<SlangCompileResult> CompileSlangProgram(
        ::slang::IGlobalSession*         globalSession,
        SlangCompileTarget               target,
        const std::string&               source,
        const std::vector<ShaderDefine>& defines,
        std::string&                     outDiagnostics,
        ::slang::ISession*&              outSession,
        const std::string&               vertexEntryPoint,
        const std::string&               fragmentEntryPoint)
    {
        ::slang::TargetDesc targetDesc = {};
        targetDesc.format = target;

        ::slang::SessionDesc sessionDesc = {};
        sessionDesc.targets     = &targetDesc;
        sessionDesc.targetCount = 1;
        // GLM (used for all matrices uploaded via UBO/push-constant) is column-major;
        // Slang defaults to row-major, which silently transposes every matrix read in
        // shader code (e.g. camera.proj/view, pc.model) unless overridden here.
        sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

        // Slang preprocessor macros. The macro descs hold raw c_str pointers into
        // `defines`, which outlives this compile call, so no local copy is needed.
        std::vector<::slang::PreprocessorMacroDesc> macros;
        macros.reserve(defines.size());
        for (const auto& def : defines) {
            macros.push_back({def.Name.c_str(), def.Value.c_str()});
        }
        if (!macros.empty()) {
            sessionDesc.preprocessorMacros     = macros.data();
            sessionDesc.preprocessorMacroCount = static_cast<SlangInt>(macros.size());
        }

        ::slang::ISession* session = nullptr;
        if (SLANG_FAILED(globalSession->createSession(sessionDesc, &session)) || !session) {
            // No session created — leave outSession untouched.
            return std::nullopt;
        }
        // Hand the session back immediately so failure paths below can keep it
        // alive (see the release()-crash note near the successful return).
        outSession = session;

        ISlangBlob* diagBlob = nullptr;

        ::slang::IModule* module = session->loadModuleFromSourceString(
            "shader", "shader.slang", source.c_str(), &diagBlob);
        if (diagBlob) {
            outDiagnostics.assign(static_cast<const char*>(diagBlob->getBufferPointer()));
            diagBlob->release();
            diagBlob = nullptr;
        }
        if (!module) {
            return std::nullopt;
        }

        // Both entry points live in the single Slang module source.
        // Always search for both; Slang returns null non-fatally if one is absent.
        // The caller decides whether a missing entry point is fatal.
        ::slang::IEntryPoint* vertEP = nullptr;
        ::slang::IEntryPoint* fragEP = nullptr;
        module->findAndCheckEntryPoint(
            vertexEntryPoint.c_str(), SLANG_STAGE_VERTEX, &vertEP, &diagBlob);
        if (diagBlob) { diagBlob->release(); diagBlob = nullptr; }
        module->findAndCheckEntryPoint(
            fragmentEntryPoint.c_str(), SLANG_STAGE_FRAGMENT, &fragEP, &diagBlob);
        if (diagBlob) { diagBlob->release(); diagBlob = nullptr; }

        std::vector<::slang::IComponentType*> comps;
        comps.push_back(module);
        if (vertEP) comps.push_back(vertEP);
        if (fragEP) comps.push_back(fragEP);

        ::slang::IComponentType* composite = nullptr;
        session->createCompositeComponentType(
            comps.data(), static_cast<SlangInt>(comps.size()), &composite, &diagBlob);
        if (diagBlob) { diagBlob->release(); diagBlob = nullptr; }

        ::slang::IComponentType* linked = nullptr;
        if (composite) {
            composite->link(&linked, &diagBlob);
            if (diagBlob) { diagBlob->release(); diagBlob = nullptr; }
            composite->release();
        }

        if (!linked) {
            if (vertEP) vertEP->release();
            if (fragEP) fragEP->release();
            module->release();
            return std::nullopt;
        }

        // Entry-point code indices in the linked composite follow the order the
        // entry points were added to `comps` (module is comp[0]).
        int epIdx         = 0;
        int vertEPCompIdx = vertEP ? epIdx++ : -1;
        int fragEPCompIdx = fragEP ? epIdx++ : -1;

        auto extractBlob = [&](int compEPIdx) -> ISlangBlob* {
            if (compEPIdx < 0) return nullptr;
            ISlangBlob* codeBlob = nullptr;
            if (SLANG_FAILED(linked->getEntryPointCode(compEPIdx, 0, &codeBlob, &diagBlob))
                    || !codeBlob) {
                if (diagBlob) { diagBlob->release(); diagBlob = nullptr; }
                return nullptr;
            }
            if (diagBlob) { diagBlob->release(); diagBlob = nullptr; }
            return codeBlob;
        };

        SlangCompileResult result;
        result.Session      = session;
        result.Linked       = linked;
        result.VertexBlob   = extractBlob(vertEPCompIdx);
        result.FragmentBlob = extractBlob(fragEPCompIdx);

        // The entry point components are owned by the composite/linked program;
        // release our references now that linking is done (matches both originals).
        if (vertEP) vertEP->release();
        if (fragEP) fragEP->release();
        module->release();

        // Slang 2026.8.1 bug: session->release() triggers "free(): corrupted
        // unsorted chunks" (WGSL std140 matrix wrapper types) and other heap
        // corruption for some SPIR-V shaders. Keep the session alive; the OS
        // reclaims memory at program exit. Note this leaks one ISession per
        // shader for the lifetime of the process. The session was already handed
        // back via outSession above and is returned again in result.Session.
        // TODO(slang>2026.8.1): re-test session->release(); currently crashes /
        // corrupts the heap in 2026.8.1.

        return result;
    }

} // namespace OZZ::rendering::slang_compile
