/** @file StudioAssets.hpp
 * @brief The MedUI Studio frontend files compiled into mdux-preview by cmake/MduXEmbedStudio.cmake.
 * Included only by C++17 textual translation units (the transport and the generated table), so it
 * includes what it uses rather than following the include-nothing rule of the module-facing headers.
 */
#pragma once
#include <cstddef>
namespace mdux::tools::preview {
/// One embedded file and the route that serves it.
struct StudioAsset {
    const char*          route;
    const char*          contentType;
    const unsigned char* data;
    std::size_t          size;
};
extern const StudioAsset studioAssets[];
extern const std::size_t studioAssetCount;
}  // namespace mdux::tools::preview
