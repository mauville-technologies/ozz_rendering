//
// Created by ozzadar on 2026-04-03.
//

#pragma once

#ifdef OZZ_PROFILING_ENABLED

#include <tracy/Tracy.hpp>

#define OZZ_PROFILE_SCOPE ZoneScoped
#define OZZ_PROFILE_SCOPE_N(name) ZoneScopedN(name)
#define OZZ_PROFILE_SCOPE_C(color) ZoneScopedC(color)
#define OZZ_PROFILE_FUNCTION ZoneScoped
#define OZZ_FRAME_MARK FrameMark
#define OZZ_FRAME_MARK_NAMED(name) FrameMarkNamed(name)
#define OZZ_PROFILE_TEXT(text, size) ZoneText(text, size)
#define OZZ_PROFILE_PLOT(name, value) TracyPlot(name, value)
#define OZZ_PROFILE_ALLOC(ptr, size) TracyAlloc(ptr, size)
#define OZZ_PROFILE_FREE(ptr) TracyFree(ptr)

#else

#define OZZ_PROFILE_SCOPE
#define OZZ_PROFILE_SCOPE_N(name)
#define OZZ_PROFILE_SCOPE_C(color)
#define OZZ_PROFILE_FUNCTION
#define OZZ_FRAME_MARK
#define OZZ_FRAME_MARK_NAMED(name)
#define OZZ_PROFILE_TEXT(text, size)
#define OZZ_PROFILE_PLOT(name, value)
#define OZZ_PROFILE_ALLOC(ptr, size)
#define OZZ_PROFILE_FREE(ptr)

#endif
