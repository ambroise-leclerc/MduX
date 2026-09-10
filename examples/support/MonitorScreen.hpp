/**
 * @file MonitorScreen.hpp
 * @brief The `endoscope-monitor` screen and its committed packages, from the embedded blobs.
 *
 * @compliance ADR-004 Trust zones in C++ (examples-support zone)
 *
 * `BoundScreen` holds the generated `constexpr` screen and the font / text / image packages a device
 * joins it to at start-up, parsed from the `mdux_embed_blob` byte arrays so the demonstrator opens
 * no files. `makeRenderer` builds a `UiRenderer` from the same blobs plus the generated `mdux-ui`
 * shader module.
 *
 * Only `MedicalScreenMonitorExample` includes this. `mdux-verify-scenario` (#321) loads the same
 * packages from `generated/` on disk instead - it must attest what it actually rendered - so it uses
 * only `MonitorFrame.hpp`, which names no blob and no generated module.
 *
 * ## Include order
 *
 * After `import std;`, `import mdux.core.result;`, `import mdux.core.units;`,
 * `import mdux.font.schema;`, `import mdux.image.schema;`, `import mdux.text.schema;`,
 * `import mdux.medui.schema;`, `import mdux.medui.screen;`, `import mdux.render.vulkan;`,
 * `import mdux.shader.generated.mdux_ui;`, `import mdux.medui.generated.screen_endoscope_monitor;`,
 * `#include "MonitorApp.hpp"`, `#include "MonitorFrame.hpp"` and the embedded-blob headers.
 */
#pragma once

namespace mdux::examples {

/// An embedded blob's bytes as UTF-8 text, for the `*Package::parse()` calls.
[[nodiscard]] inline std::string_view asText(std::span<const std::byte> bytes) noexcept {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/**
 * @brief The compiled `endoscope-monitor` screen and the committed packages a device joins it to.
 *
 * Held on the heap for the life of the program and never moved: `TextBinding` / `ImageBinding` keep
 * pointers into `font` / `text` / `image`, so a move would dangle them.
 */
struct BoundScreen {
    mdux::medui::ScreenPackage screen{mdux::medui::generated::screen_endoscope_monitor::package()};
    mdux::font::FontPackage    font{};
    mdux::text::TextPackage    text{};
    mdux::image::ImagePackage  image{};
    mdux::medui::TextBinding   textBinding{};
    mdux::medui::ImageBinding  imageBinding{};

    BoundScreen()                              = default;
    BoundScreen(const BoundScreen&)            = delete;
    BoundScreen& operator=(const BoundScreen&) = delete;

    /// The authored surface extent the screen was compiled for - the `UiRenderer` viewport.
    [[nodiscard]] mdux::core::Extent2D surface() const noexcept {
        return {screen.surfaceWidth, screen.surfaceHeight};
    }

    /// Parses the embedded font / text / image packages for `locale` and builds the two bindings, or
    /// prints why and returns `nullptr`.
    [[nodiscard]] static std::unique_ptr<BoundScreen> load(Locale locale) {
        namespace ms = mdux::medui;
        auto        bound = std::make_unique<BoundScreen>();

        const std::span<const std::byte> textJson =
            locale == Locale::FrFr ? endoscopeTextFrFrPackageJson() : endoscopeTextEnUsPackageJson();
        const std::span<const std::byte> textRuns =
            locale == Locale::FrFr ? endoscopeTextFrFrRuns() : endoscopeTextEnUsRuns();

        auto font  = mdux::font::FontPackage::parse(asText(dejavuUiPackageJson()));
        auto text  = mdux::text::TextPackage::parse(asText(textJson));
        auto image = mdux::image::ImagePackage::parse(asText(brandMarkPackageJson()));
        if (!font || !text || !image) {
            std::cerr << "monitor: a committed package did not parse\n";
            return nullptr;
        }
        bound->font  = std::move(*font);
        bound->text  = std::move(*text);
        bound->image = std::move(*image);

        auto textBinding = ms::TextBinding::create(bound->screen, bound->font, bound->text, textJson, textRuns);
        if (!textBinding) {
            std::cerr << "monitor: the committed text artifacts were refused: " << ms::describe(textBinding.error()) << '\n';
            return nullptr;
        }
        bound->textBinding = *textBinding;

        auto imageBinding = ms::ImageBinding::create(bound->screen, bound->image, brandMarkPackageJson(), brandMarkPixels());
        if (!imageBinding) {
            std::cerr << "monitor: the committed image artifacts were refused: " << ms::describe(imageBinding.error()) << '\n';
            return nullptr;
        }
        bound->imageBinding = *imageBinding;
        return bound;
    }
};

/// A `UiRenderer` for `context` with the committed font coverage atlas and the brand-mark image
/// atlas, so text and the logo draw as themselves rather than white blocks.
[[nodiscard]] inline mdux::core::Result<mdux::render::UiRenderer, mdux::render::RenderError>
makeRenderer(const mdux::render::VulkanRenderContext& context, const BoundScreen& bound) {
    return mdux::render::UiRenderer::createWithAtlases(context,
                                                      mdux::shader::generated::mdux_ui::package(),
                                                      bound.screen.budget,
                                                      dejavuUiAtlas(),
                                                      bound.font.atlas.width,
                                                      bound.font.atlas.height,
                                                      brandMarkPixels(),
                                                      bound.image.width,
                                                      bound.image.height);
}

}  // namespace mdux::examples
