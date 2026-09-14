/** @file StudioAssets.hpp
 * @brief The MedUI Studio frontend files compiled into mdux-preview by cmake/MduXEmbedStudio.cmake.
 * Requires std::size_t from <cstddef>.
 */
#pragma once
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
