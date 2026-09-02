/**
 * @file Driver.cpp
 * @brief Artifact loading, obligation planning and offscreen execution for mdux-verify-ui.
 */
module;

#include <vulkan/vulkan.h>

module mdux.tools.verify.driver;

import std;
import mdux.core.units;
import mdux.draw;
import mdux.evidence.digest;
import mdux.evidence.json;
import mdux.font.schema;
import mdux.medui.schema;
import mdux.medui.screen;
import mdux.render.offscreen;
import mdux.render.vulkan;
import mdux.shader.schema;
import mdux.text.schema;
import mdux.tools.cli;
import mdux.tools.medui.package;
import mdux.tools.verify.diff;
import mdux.verify;

namespace mdux::tools::verify {
namespace {

namespace cli  = mdux::tools::cli;
namespace json = mdux::evidence::json;
namespace mv   = mdux::verify;

constexpr mdux::core::ColorRgba8 clearColor{.r = 0, .g = 0, .b = 0, .a = 255};

[[nodiscard]] std::filesystem::path normalizeScreenDirectory(std::filesystem::path path) {
    path = path.lexically_normal();
    while (!path.has_filename()) {
        const std::filesystem::path parent = path.parent_path();
        if (parent == path)
            break;
        path = parent;
    }
    return path;
}

void report(std::vector<cli::Diagnostic>& diagnostics, const std::filesystem::path& file, std::string code, std::string message, std::string fix = {}) {
    diagnostics.push_back(cli::Diagnostic{.file     = file.generic_string(),
                                          .code     = std::move(code),
                                          .severity = cli::Severity::Error,
                                          .message  = std::move(message),
                                          .fixHint  = std::move(fix)});
}

/// A finding that does not change the verdict, so it must not be spelled like one.
///
/// The only user today is the diff image (VUI009). Whether that file was written says nothing about
/// whether the screen verified, and an `error:` line for it would put a second, unrelated failure in
/// front of a reader who is already looking at a real one.
void warn(std::vector<cli::Diagnostic>& diagnostics, const std::filesystem::path& file, std::string code, std::string message) {
    diagnostics.push_back(cli::Diagnostic{.file     = file.generic_string(),
                                          .code     = std::move(code),
                                          .severity = cli::Severity::Warning,
                                          .message  = std::move(message),
                                          .fixHint  = {}});
}

[[nodiscard]] std::optional<std::vector<std::byte>> readBytes(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file) {
        return std::nullopt;
    }
    const std::streamoff size = file.tellg();
    if (size < 0 || !file.seekg(0)) {
        return std::nullopt;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (size > 0) {
        file.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!file) {
            return std::nullopt;
        }
    }
    return bytes;
}

[[nodiscard]] std::optional<std::string> readText(const std::filesystem::path& path) {
    auto bytes = readBytes(path);
    if (!bytes.has_value()) {
        return std::nullopt;
    }
    if (bytes->empty()) {
        return std::string{};
    }
    return std::string{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
}

/// The lowercase-hex digest of already-read artifact text, spelled the way every other evidence
/// record spells one. Taken from the bytes the run actually parsed rather than by re-reading the
/// file, so what the artifact names is what the verification used.
[[nodiscard]] std::string hexDigest(std::string_view text) {
    const auto bytes  = std::as_bytes(std::span{text.data(), text.size()});
    const auto digest = mdux::evidence::toHex(mdux::evidence::sha256(bytes));
    return std::string{digest.data(), digest.size()};
}

[[nodiscard]] bool exactMembers(const json::Value& value, std::initializer_list<std::string_view> expected) {
    if (value.kind() != json::Value::Kind::Object || value.members().size() != expected.size()) {
        return false;
    }
    for (std::string_view name : expected) {
        if (value.find(name) == nullptr)
            return false;
    }
    return true;
}

[[nodiscard]] std::optional<std::string_view> stringMember(const json::Value& value, std::string_view name) {
    const json::Value* member = value.find(name);
    if (member == nullptr) {
        return std::nullopt;
    }
    auto text = member->asString();
    return text.has_value() ? std::optional<std::string_view>{*text} : std::nullopt;
}

[[nodiscard]] std::optional<std::int32_t> integerMember(const json::Value& value, std::string_view name) {
    const json::Value* member = value.find(name);
    if (member == nullptr) {
        return std::nullopt;
    }
    std::int64_t signedValue = 0;
    if (auto value64 = member->asInt(); value64.has_value()) {
        signedValue = *value64;
    } else if (auto unsignedValue = member->asUInt();
               unsignedValue.has_value() && *unsignedValue <= static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
        signedValue = static_cast<std::int64_t>(*unsignedValue);
    } else {
        return std::nullopt;
    }
    if (signedValue < std::numeric_limits<std::int32_t>::min() || signedValue > std::numeric_limits<std::int32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(signedValue);
}

struct OwnedGolden {
    std::string              nodeId;
    mdux::medui::NodeRect    bounds{};
    std::string              textKey;
    std::string              colorToken;
    std::vector<mv::CvCheck> checks;

    [[nodiscard]] mv::GoldenEntry view() const noexcept {
        return mv::GoldenEntry{.nodeId = nodeId, .bounds = bounds, .textKey = textKey, .colorToken = colorToken, .cvChecks = checks};
    }
};

[[nodiscard]] std::optional<std::vector<OwnedGolden>>
readGoldens(const std::filesystem::path& path, std::string& digestOut, std::vector<cli::Diagnostic>& diagnostics) {
    const auto text = readText(path);
    if (!text.has_value()) {
        report(diagnostics, path, "VUI002", "cannot read goldens.json");
        return std::nullopt;
    }
    digestOut = hexDigest(*text);
    auto root = json::parse(*text);
    if (!root.has_value() || root->kind() != json::Value::Kind::Array) {
        report(diagnostics, path, "VUI003", "goldens.json is not a canonical JSON array");
        return std::nullopt;
    }
    auto canonical = json::write(*root);
    if (!canonical.has_value() || *canonical != *text) {
        report(diagnostics,
               path,
               "VUI003",
               "goldens.json is not in canonical committed form",
               "Re-bake with mdux-bake-update; do not hand-edit generated artifacts.");
        return std::nullopt;
    }

    std::vector<OwnedGolden> result;
    result.reserve(root->elements().size());
    std::unordered_set<std::string> ids;
    for (const json::Value& item : root->elements()) {
        const bool        hasText      = item.find("textKey") != nullptr;
        const bool        hasColor     = item.find("colorToken") != nullptr;
        const std::size_t expectedSize = 3U + (hasText ? 1U : 0U) + (hasColor ? 1U : 0U);
        const bool        knownMembers = item.kind() == json::Value::Kind::Object && item.members().size() == expectedSize && item.find("bounds") != nullptr
                                  && item.find("cvChecks") != nullptr && item.find("nodeId") != nullptr
                                  && std::ranges::all_of(item.members(), [](const json::Member& member) {
                                         return member.key == "bounds" || member.key == "colorToken" || member.key == "cvChecks" || member.key == "nodeId"
                                                || member.key == "textKey";
                                     });
        if (!knownMembers) {
            report(diagnostics, path, "VUI003", "a golden entry has unknown, missing, or wrongly ordered members");
            return std::nullopt;
        }
        const auto         nodeId = stringMember(item, "nodeId");
        const json::Value* bounds = item.find("bounds");
        const json::Value* checks = item.find("cvChecks");
        if (!nodeId.has_value() || nodeId->empty() || bounds == nullptr || checks == nullptr || !exactMembers(*bounds, {"height", "width", "x", "y"})
            || checks->kind() != json::Value::Kind::Array) {
            report(diagnostics, path, "VUI003", "a golden entry has an invalid nodeId, bounds, or cvChecks");
            return std::nullopt;
        }
        const auto x      = integerMember(*bounds, "x");
        const auto y      = integerMember(*bounds, "y");
        const auto width  = integerMember(*bounds, "width");
        const auto height = integerMember(*bounds, "height");
        if (!x || !y || !width || !height) {
            report(diagnostics, path, "VUI003", "a golden bound is not a 32-bit integer");
            return std::nullopt;
        }

        OwnedGolden golden{
            .nodeId     = std::string{*nodeId},
            .bounds     = {.x = *x, .y = *y, .width = *width, .height = *height},
            .textKey    = {},
            .colorToken = {},
            .checks     = {}
        };
        if (!ids.insert(golden.nodeId).second) {
            report(diagnostics, path, "VUI003", "goldens.json contains duplicate nodeId '" + golden.nodeId + "'");
            return std::nullopt;
        }
        if (hasText) {
            const auto value = stringMember(item, "textKey");
            if (!value.has_value()) {
                report(diagnostics, path, "VUI003", "a golden textKey is not a string");
                return std::nullopt;
            }
            golden.textKey = *value;
        }
        if (hasColor) {
            const auto value = stringMember(item, "colorToken");
            if (!value.has_value()) {
                report(diagnostics, path, "VUI003", "a golden colorToken is not a string");
                return std::nullopt;
            }
            golden.colorToken = *value;
        }
        for (const json::Value& checkValue : checks->elements()) {
            auto name = checkValue.asString();
            if (!name.has_value()) {
                report(diagnostics, path, "VUI003", "a cvChecks entry is not a string");
                return std::nullopt;
            }
            auto parsed = mv::parseCvCheck(*name);
            if (!parsed.has_value()) {
                report(diagnostics, path, "VUI003", "goldens.json names unknown check '" + std::string{*name} + "'");
                return std::nullopt;
            }
            golden.checks.push_back(*parsed);
        }
        result.push_back(std::move(golden));
    }
    return result;
}

/**
 * @brief Writes one render scope's diff image, and says where it went or why it did not.
 *
 * A failure to write is a diagnostic and never a verdict. The image is an attachment for a person,
 * so a full disk must not turn a verification failure into a different failure - nor, worse, let a
 * real one be missed because the process died reporting that it could not draw a picture of it.
 */
void writeDiffImage(RunResult&                              result,
                    const std::filesystem::path&            directory,
                    std::string_view                        screenId,
                    std::string_view                        scope,
                    std::span<const mdux::core::ColorRgba8> frame,
                    std::uint32_t                           width,
                    std::uint32_t                           height,
                    std::span<const Outcome>                scopeOutcomes) {
    std::vector<DiffMark> marks;
    for (const Outcome& outcome : scopeOutcomes) {
        if (outcome.held()) {
            continue;
        }
        marks.push_back(DiffMark{.nodeId     = outcome.nodeId,
                                 .check      = outcome.check,
                                 .expected   = outcome.expected,
                                 .found      = outcome.found,
                                 .foundValid = outcome.foundValid});
    }
    if (marks.empty()) {
        return;
    }

    // The locale-free scope spells itself `(locale-free)`, and a CI artifact whose name carries
    // parentheses is one people quote wrongly in the shell that fetches it. Everything outside the
    // portable filename set is dropped rather than substituted, so `(locale-free)` becomes
    // `locale-free` and an approved locale tag - already `[A-Za-z0-9-]` by the manifest - is
    // unchanged. Dropping cannot collide two scopes of one screen: `RenderScope` is either the one
    // locale-free name or a locale from a manifest `validate()` has already refused duplicates in.
    std::string slug;
    slug.reserve(scope.size());
    for (const char character : scope) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '-' || character == '_' || character == '.') {
            slug.push_back(character);
        }
    }
    const std::filesystem::path path = directory / std::format("{}.{}.png", screenId, slug);

    std::error_code created;
    std::filesystem::create_directories(directory, created);
    if (created) {
        warn(result.diagnostics, path, "VUI009", "cannot create the diff image directory: " + created.message());
        return;
    }

    const std::vector<mdux::core::ColorRgba8> composed = composeDiff(frame, width, height, marks);
    const std::vector<std::byte>              encoded  = encodePng(composed, width, height);
    if (encoded.empty()) {
        warn(result.diagnostics, path, "VUI009", "the readback could not be encoded as a diff image");
        return;
    }

    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    file.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    if (!file) {
        warn(result.diagnostics, path, "VUI009", "cannot write the diff image");
        return;
    }
    result.diffImages.push_back(path);
}

[[nodiscard]] mdux::core::ColorRgba8 groundFor(const mdux::medui::ScreenPackage& screen, const mdux::medui::CompiledNode& node) {
    mdux::core::ColorRgba8 ground = clearColor;
    for (const mdux::medui::CompiledNode& candidate : screen.nodes) {
        if (&candidate == &node) {
            break;
        }
        const auto* panel = std::get_if<mdux::medui::PanelSpec>(&candidate.payload);
        if (panel == nullptr) {
            continue;
        }
        const auto right      = static_cast<std::int64_t>(candidate.bounds.x) + candidate.bounds.width;
        const auto bottom     = static_cast<std::int64_t>(candidate.bounds.y) + candidate.bounds.height;
        const auto nodeRight  = static_cast<std::int64_t>(node.bounds.x) + node.bounds.width;
        const auto nodeBottom = static_cast<std::int64_t>(node.bounds.y) + node.bounds.height;
        if (candidate.bounds.x <= node.bounds.x && candidate.bounds.y <= node.bounds.y && right >= nodeRight && bottom >= nodeBottom) {
            const auto color = mdux::medui::resolveColorToken(panel->colorToken);
            if (color.has_value()) {
                ground = mdux::medui::quantise(*color);
            }
        }
    }
    return ground;
}

struct ShaderAssets {
    mdux::shader::ShaderPackage           package;
    std::vector<std::byte>                sidecar;
    std::vector<mdux::shader::ModuleView> modules;
    std::string                           sha256;  ///< of package.json, for #254's artifact

    [[nodiscard]] mdux::shader::PackageView view() const noexcept {
        return {.id = package.header.id, .spirv = sidecar, .modules = modules, .descriptors = package.descriptors, .pushConstants = package.pushConstants};
    }
};

[[nodiscard]] std::optional<ShaderAssets> loadShader(const std::filesystem::path& artifactRoot, std::vector<cli::Diagnostic>& diagnostics) {
    const auto packagePath = artifactRoot / "shader" / "mdux-ui" / "package.json";
    auto       text        = readText(packagePath);
    if (!text) {
        report(diagnostics, packagePath, "VUI005", "cannot read the committed mdux-ui shader package");
        return std::nullopt;
    }
    auto package = mdux::shader::ShaderPackage::parse(*text);
    if (!package.has_value()) {
        report(diagnostics, packagePath, "VUI005", "invalid mdux-ui shader package: " + std::string{mdux::shader::describe(package.error())});
        return std::nullopt;
    }
    auto canonical = package->write();
    if (!canonical.has_value() || *canonical != *text) {
        report(diagnostics, packagePath, "VUI005", "mdux-ui package.json is not canonical");
        return std::nullopt;
    }
    const auto sidecarPath = packagePath.parent_path() / package->sidecarPath;
    auto       sidecar     = readBytes(sidecarPath);
    if (!sidecar || sidecar->size() != package->sidecarByteLength || mdux::evidence::sha256(*sidecar) != package->sidecarSha256) {
        report(diagnostics, sidecarPath, "VUI005", "the mdux-ui shader sidecar does not match package.json");
        return std::nullopt;
    }
    ShaderAssets assets{.package = std::move(*package), .sidecar = std::move(*sidecar), .modules = {}, .sha256 = hexDigest(*text)};
    assets.modules.reserve(assets.package.modules.size());
    for (const auto& module : assets.package.modules) {
        if (module.byteOffset > std::numeric_limits<std::size_t>::max() || module.byteLength > std::numeric_limits<std::size_t>::max()) {
            report(diagnostics, packagePath, "VUI005", "a shader module range does not fit this host");
            return std::nullopt;
        }
        assets.modules.push_back({.id         = module.id,
                                  .stage      = module.stage,
                                  .entryPoint = module.entryPoint,
                                  .byteOffset = static_cast<std::size_t>(module.byteOffset),
                                  .byteLength = static_cast<std::size_t>(module.byteLength)});
    }
    return assets;
}

struct LocaleAssets {
    std::string             locale;
    std::string             textJson;
    mdux::text::TextPackage text;
    std::vector<std::byte>  runs;
    std::string             fontJson;
    mdux::font::FontPackage font;
    std::vector<std::byte>  atlas;
};

[[nodiscard]] std::optional<LocaleAssets>
loadLocale(const mdux::medui::TextPackageApproval& approval, const std::filesystem::path& artifactRoot, std::vector<cli::Diagnostic>& diagnostics) {
    const auto textPath = artifactRoot / "text" / approval.packageId / "package.json";
    auto       textJson = readText(textPath);
    if (!textJson) {
        report(diagnostics, textPath, "VUI006", "cannot read approved text package for locale '" + std::string{approval.locale} + "'");
        return std::nullopt;
    }
    auto text = mdux::text::TextPackage::parse(*textJson);
    if (!text.has_value()) {
        report(diagnostics, textPath, "VUI006", "invalid approved text package: " + std::string{mdux::text::describe(text.error())});
        return std::nullopt;
    }
    auto canonicalText = text->write();
    if (!canonicalText.has_value() || *canonicalText != *textJson || text->header.id != approval.packageId || text->locale != approval.locale) {
        report(diagnostics, textPath, "VUI006", "approved text package identity or canonical bytes disagree with the screen manifest");
        return std::nullopt;
    }
    const auto runsPath = textPath.parent_path() / text->sidecarPath;
    auto       runs     = readBytes(runsPath);
    if (!runs || runs->size() != text->sidecarByteLength || mdux::evidence::sha256(*runs) != text->sidecarSha256) {
        report(diagnostics, runsPath, "VUI006", "text sidecar does not match its package");
        return std::nullopt;
    }

    const auto fontPath = artifactRoot / "font" / text->atlasId / "package.json";
    auto       fontJson = readText(fontPath);
    if (!fontJson) {
        report(diagnostics, fontPath, "VUI006", "cannot read the font package named by the text package");
        return std::nullopt;
    }
    auto font = mdux::font::FontPackage::parse(*fontJson);
    if (!font.has_value()) {
        report(diagnostics, fontPath, "VUI006", "invalid font package: " + std::string{mdux::font::describe(font.error())});
        return std::nullopt;
    }
    auto canonicalFont = font->write();
    if (!canonicalFont.has_value() || *canonicalFont != *fontJson || font->id != text->atlasId
        || std::ranges::find(font->locales, approval.locale) == font->locales.end()) {
        report(diagnostics, fontPath, "VUI006", "font identity, locale approval, or canonical bytes disagree with the text package");
        return std::nullopt;
    }
    const auto atlasPath = fontPath.parent_path() / font->atlas.path;
    auto       atlas     = readBytes(atlasPath);
    if (!atlas || atlas->size() != font->atlas.byteLength) {
        report(diagnostics, atlasPath, "VUI006", "font atlas length does not match its package");
        return std::nullopt;
    }
    const auto digest = mdux::evidence::toHex(mdux::evidence::sha256(*atlas));
    if (std::string_view{digest.data(), digest.size()} != font->atlas.sha256) {
        report(diagnostics, atlasPath, "VUI006", "font atlas digest does not match its package");
        return std::nullopt;
    }
    return LocaleAssets{.locale   = std::string{approval.locale},
                        .textJson = std::move(*textJson),
                        .text     = std::move(*text),
                        .runs     = std::move(*runs),
                        .fontJson = std::move(*fontJson),
                        .font     = std::move(*font),
                        .atlas    = std::move(*atlas)};
}

class HeadlessDevice {
public:
    HeadlessDevice() noexcept {
        initialise();
    }
    ~HeadlessDevice() {
        if (device_ != VK_NULL_HANDLE)
            vkDestroyDevice(device_, nullptr);
        if (instance_ != VK_NULL_HANDLE)
            vkDestroyInstance(instance_, nullptr);
    }
    HeadlessDevice(const HeadlessDevice&)            = delete;
    HeadlessDevice& operator=(const HeadlessDevice&) = delete;

    [[nodiscard]] bool available() const noexcept {
        return device_ != VK_NULL_HANDLE;
    }
    [[nodiscard]] std::string_view reason() const noexcept {
        return reason_;
    }
    [[nodiscard]] VkDevice device() const noexcept {
        return device_;
    }
    [[nodiscard]] VkPhysicalDevice physicalDevice() const noexcept {
        return physicalDevice_;
    }
    [[nodiscard]] VkQueue queue() const noexcept {
        return queue_;
    }
    [[nodiscard]] std::uint32_t family() const noexcept {
        return family_;
    }

private:
    [[nodiscard]] static bool hasInstanceExtension(std::string_view wanted) noexcept {
        std::uint32_t count = 0;
        if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS)
            return false;
        std::vector<VkExtensionProperties> values(count);
        if (vkEnumerateInstanceExtensionProperties(nullptr, &count, values.data()) != VK_SUCCESS)
            return false;
        return std::ranges::any_of(values, [wanted](const auto& value) {
            return wanted == value.extensionName;
        });
    }

    void initialise() noexcept {
        const VkApplicationInfo  app{.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                     .pNext              = nullptr,
                                     .pApplicationName   = "mdux-verify-ui",
                                     .applicationVersion = 1,
                                     .pEngineName        = "MduX",
                                     .engineVersion      = 1,
                                     .apiVersion         = VK_API_VERSION_1_3};
        std::vector<const char*> instanceExtensions;
        VkInstanceCreateFlags    flags = 0;
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        if (hasInstanceExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
            flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
            instanceExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        }
#endif
        const VkInstanceCreateInfo info{.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                        .pNext                   = nullptr,
                                        .flags                   = flags,
                                        .pApplicationInfo        = &app,
                                        .enabledLayerCount       = 0,
                                        .ppEnabledLayerNames     = nullptr,
                                        .enabledExtensionCount   = static_cast<std::uint32_t>(instanceExtensions.size()),
                                        .ppEnabledExtensionNames = instanceExtensions.empty() ? nullptr : instanceExtensions.data()};
        if (vkCreateInstance(&info, nullptr, &instance_) != VK_SUCCESS) {
            reason_ = "vkCreateInstance failed: no Vulkan loader or usable ICD";
            return;
        }
        std::uint32_t deviceCount = 0;
        if (vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr) != VK_SUCCESS || deviceCount == 0) {
            reason_ = "no Vulkan physical device";
            return;
        }
        std::vector<VkPhysicalDevice> devices(deviceCount);
        if (vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()) != VK_SUCCESS) {
            reason_ = "physical-device enumeration failed";
            return;
        }
        physicalDevice_           = devices.front();
        std::uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &familyCount, families.data());
        auto family = std::ranges::find_if(families, [](const auto& value) {
            return (value.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        });
        if (family == families.end()) {
            reason_ = "no graphics-capable Vulkan queue family";
            return;
        }
        family_                                     = static_cast<std::uint32_t>(std::distance(families.begin(), family));
        const float                        priority = 1.0F;
        const VkDeviceQueueCreateInfo      queueInfo{.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                                     .pNext            = nullptr,
                                                     .flags            = 0,
                                                     .queueFamilyIndex = family_,
                                                     .queueCount       = 1,
                                                     .pQueuePriorities = &priority};
        std::vector<VkExtensionProperties> extensions;
        while (true) {
            std::uint32_t extensionCount = 0;
            if (vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extensionCount, nullptr) != VK_SUCCESS) {
                reason_ = "device-extension count enumeration failed";
                return;
            }
            if (extensionCount == 0) {
                extensions.clear();
                break;
            }
            extensions.resize(extensionCount);
            const VkResult enumerated = vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extensionCount, extensions.data());
            if (enumerated == VK_SUCCESS) {
                extensions.resize(extensionCount);
                break;
            }
            if (enumerated != VK_INCOMPLETE) {
                reason_ = "device-extension enumeration failed";
                return;
            }
        }
        std::vector<const char*> enabled;
        if (std::ranges::any_of(extensions, [](const auto& value) {
                return std::string_view{value.extensionName} == "VK_KHR_portability_subset";
            })) {
            enabled.push_back("VK_KHR_portability_subset");
        }
        const VkDeviceCreateInfo deviceInfo{.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                            .pNext                   = nullptr,
                                            .flags                   = 0,
                                            .queueCreateInfoCount    = 1,
                                            .pQueueCreateInfos       = &queueInfo,
                                            .enabledLayerCount       = 0,
                                            .ppEnabledLayerNames     = nullptr,
                                            .enabledExtensionCount   = static_cast<std::uint32_t>(enabled.size()),
                                            .ppEnabledExtensionNames = enabled.empty() ? nullptr : enabled.data(),
                                            .pEnabledFeatures        = nullptr};
        if (vkCreateDevice(physicalDevice_, &deviceInfo, nullptr, &device_) != VK_SUCCESS) {
            reason_ = "vkCreateDevice failed";
            return;
        }
        vkGetDeviceQueue(device_, family_, 0, &queue_);
    }

    VkInstance       instance_{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
    VkDevice         device_{VK_NULL_HANDLE};
    VkQueue          queue_{VK_NULL_HANDLE};
    std::uint32_t    family_{0};
    std::string_view reason_{};
};

[[nodiscard]] Outcome own(const mv::CheckOutcome& outcome) {
    return {.finding         = outcome.finding,
            .nodeId          = std::string{outcome.nodeId},
            .scope           = std::string{outcome.scope},
            .check           = std::string{outcome.check},
            .expected        = outcome.expected,
            .found           = outcome.found,
            .foundValid      = outcome.foundValid,
            .expectedColor   = outcome.expectedColor,
            .foundColor      = outcome.foundColor,
            .foundColorValid = outcome.foundColorValid,
            .glyphIndex      = outcome.glyphIndex};
}

[[nodiscard]] std::string rectangle(mdux::medui::NodeRect value) {
    return std::format("({}, {}, {}x{})", value.x, value.y, value.width, value.height);
}

void reportFailure(RunResult& result, const std::filesystem::path& path, const Outcome& outcome) {
    std::string found   = outcome.foundValid ? rectangle(outcome.found) : "unavailable";
    std::string message = std::format("node '{}', scope '{}', check '{}': {}; expected {}, found {}",
                                      outcome.nodeId,
                                      outcome.scope,
                                      outcome.check,
                                      mv::describe(outcome.finding),
                                      rectangle(outcome.expected),
                                      found);
    if (outcome.check == mv::spell(mv::CvCheck::ColorHash)) {
        message += std::format("; expected rgba({},{},{},{}), found ",
                               outcome.expectedColor.r,
                               outcome.expectedColor.g,
                               outcome.expectedColor.b,
                               outcome.expectedColor.a);
        message += outcome.foundColorValid
                       ? std::format("rgba({},{},{},{})", outcome.foundColor.r, outcome.foundColor.g, outcome.foundColor.b, outcome.foundColor.a)
                       : "unavailable";
    }
    report(result.diagnostics, path, "VUI101", std::move(message));
}

struct Recording {
    mdux::render::UiRenderer*                renderer{nullptr};
    const mdux::draw::DrawList*              list{nullptr};
    std::optional<mdux::render::RenderError> error;
};

void recordFrame(VkCommandBuffer commandBuffer, void* context) {
    auto& recording = *static_cast<Recording*>(context);
    auto  result    = recording.renderer->record(commandBuffer, *recording.list);
    if (!result.has_value())
        recording.error = result.error();
}

}  // namespace

PlanResult enumerate(const mdux::medui::ScreenPackage& screen, std::span<const mv::GoldenEntry> goldens) {
    PlanResult                    result;
    std::vector<std::string_view> scopes;
    const bool                    hasText = std::ranges::any_of(screen.nodes, [](const auto& node) {
        return !mv::textKeyOf(node).empty();
    });
    if (hasText) {
        for (const auto& approval : screen.approvedTextPackages)
            scopes.push_back(approval.locale);
        if (scopes.empty()) {
            report(result.diagnostics, {}, "VUI004", "the screen has text obligations but approves no locale");
            return result;
        }
    } else {
        scopes.push_back(mv::localeFreeScopeName);
    }
    for (std::string_view scope : scopes) {
        for (const mv::GoldenEntry& golden : goldens) {
            for (mv::CvCheck check : golden.cvChecks) {
                result.obligations.push_back({.kind   = ObligationKind::Golden,
                                              .nodeId = std::string{golden.nodeId},
                                              .scope  = std::string{scope},
                                              .check  = std::string{mv::spell(check)}});
            }
        }
        if (hasText) {
            for (const auto& node : screen.nodes) {
                if (mv::textKeyOf(node).empty())
                    continue;
                for (mv::TextCheck check : {mv::TextCheck::InkContainment, mv::TextCheck::LocalizedTextPresence}) {
                    result.obligations.push_back(
                        {.kind = ObligationKind::Text, .nodeId = std::string{node.id}, .scope = std::string{scope}, .check = std::string{mv::spell(check)}});
                }
            }
        }
    }
    if (result.obligations.empty()) {
        report(result.diagnostics, {}, "VUI004", "the screen produces zero verification obligations");
    }
    return result;
}

RunResult run(const std::filesystem::path& requestedScreenDirectory, const RunOptions& options) {
    const std::filesystem::path& artifactRoot = options.artifactRoot;
    RunResult                   result;
    const std::filesystem::path screenDirectory = normalizeScreenDirectory(requestedScreenDirectory);
    const auto                  packagePath     = screenDirectory / "package.json";
    const auto                  packageText     = readText(packagePath);
    if (!packageText) {
        report(result.diagnostics, packagePath, "VUI001", "cannot read screen package.json");
        return result;
    }
    auto packageRead = mdux::tools::medui::readPackage(*packageText, packagePath.generic_string());
    if (!packageRead.ok()) {
        result.diagnostics.insert(result.diagnostics.end(),
                                  std::make_move_iterator(packageRead.diagnostics.begin()),
                                  std::make_move_iterator(packageRead.diagnostics.end()));
        return result;
    }
    const auto screen = packageRead.document.package();
    if (mdux::tools::medui::writePackage(screen) != *packageText || screenDirectory.filename() != screen.id) {
        report(result.diagnostics, packagePath, "VUI001", "screen package is non-canonical or its id does not match its directory");
        return result;
    }
    std::string goldensDigest;
    auto        ownedGoldens = readGoldens(screenDirectory / "goldens.json", goldensDigest, result.diagnostics);
    if (!ownedGoldens)
        return result;

    // Recorded as the run binds them, in one fixed order, because #254 commits this list and a
    // byte-compared file cannot depend on the order a container happened to yield.
    result.inputs.push_back({.role = "screenPackage", .id = std::string{screen.id}, .locale = {}, .sha256 = hexDigest(*packageText)});
    result.inputs.push_back({.role = "goldens", .id = std::string{screen.id}, .locale = {}, .sha256 = std::move(goldensDigest)});
    std::vector<mv::GoldenEntry> goldens;
    goldens.reserve(ownedGoldens->size());
    for (const auto& golden : *ownedGoldens)
        goldens.push_back(golden.view());

    PlanResult plan    = enumerate(screen, goldens);
    result.obligations = std::move(plan.obligations);
    if (!plan.ok()) {
        if (result.obligations.empty())
            result.state = RunState::ChecksFailed;
        for (auto& diagnostic : plan.diagnostics) {
            diagnostic.file = packagePath.generic_string();
            result.diagnostics.push_back(std::move(diagnostic));
        }
        return result;
    }

    auto shader = loadShader(artifactRoot, result.diagnostics);
    if (!shader)
        return result;
    result.inputs.push_back({.role = "shaderPackage", .id = std::string{shader->package.header.id}, .locale = {}, .sha256 = shader->sha256});

    const bool                hasText = std::ranges::any_of(screen.nodes, [](const auto& node) {
        return !mv::textKeyOf(node).empty();
    });
    std::vector<LocaleAssets> locales;
    if (hasText) {
        locales.reserve(screen.approvedTextPackages.size());
        for (const auto& approval : screen.approvedTextPackages) {
            auto assets = loadLocale(approval, artifactRoot, result.diagnostics);
            if (!assets)
                return result;
            // In manifest order, which `ScreenPackage::validate()` already fixes, so the same screen
            // yields the same list on every leg.
            result.inputs.push_back(
                {.role = "textPackage", .id = std::string{assets->text.header.id}, .locale = assets->locale, .sha256 = hexDigest(assets->textJson)});
            result.inputs.push_back(
                {.role = "fontPackage", .id = std::string{assets->font.id}, .locale = assets->locale, .sha256 = hexDigest(assets->fontJson)});
            locales.push_back(std::move(*assets));
        }
    }

    // Validate every artifact-derived expectation before creating a device or rendering a frame.
    for (std::size_t scopeIndex = 0; scopeIndex < (hasText ? locales.size() : 1U); ++scopeIndex) {
        const mv::RenderScope scope = hasText ? mv::RenderScope::forLocale(locales[scopeIndex].locale) : mv::RenderScope::localeFree();
        for (const mv::GoldenEntry& golden : goldens) {
            const auto* node        = screen.find(golden.nodeId);
            const auto  expectation = mv::GoldenExpectation::create(golden, screen, scope, node == nullptr ? clearColor : groundFor(screen, *node));
            if (!expectation.has_value()) {
                report(result.diagnostics,
                       screenDirectory / "goldens.json",
                       "VUI007",
                       "cannot construct golden expectation for node '" + std::string{golden.nodeId} + "' in scope '" + std::string{scope.name()}
                           + "': " + std::string{mv::describe(expectation.error())});
                return result;
            }
        }
        if (hasText) {
            auto&      assets       = locales[scopeIndex];
            const auto packageBytes = std::as_bytes(std::span{assets.textJson});
            const auto binding      = mdux::medui::TextBinding::create(screen, assets.font, assets.text, packageBytes, assets.runs);
            if (!binding.has_value()) {
                report(result.diagnostics,
                       artifactRoot / "text" / assets.text.header.id / "package.json",
                       "VUI007",
                       "cannot authenticate text binding for locale '" + assets.locale + "': " + std::string{mdux::medui::describe(binding.error())});
                return result;
            }
            for (const auto& node : screen.nodes) {
                if (mv::textKeyOf(node).empty())
                    continue;
                const auto expectation = mv::TextExpectation::create(screen, node, *binding, assets.atlas, scope, groundFor(screen, node));
                if (!expectation.has_value()) {
                    report(result.diagnostics,
                           packagePath,
                           "VUI007",
                           "cannot construct text expectation for node '" + std::string{node.id} + "' in scope '" + assets.locale
                               + "': " + std::string{mv::describe(expectation.error())});
                    return result;
                }
            }
        }
    }

    HeadlessDevice device;
    if (!device.available()) {
        report(result.diagnostics,
               packagePath,
               "VUI008",
               "verification run could not be made: " + std::string{device.reason()},
               "Install a Vulkan 1.3 implementation; automatic Linux legs use lavapipe and macOS uses MoltenVK.");
        // The sole impossibility a caller may treat as "this host cannot host the run" rather than
        // as a defect; every other early return below leaves the default `CouldNotRun`.
        result.state = RunState::NoRenderDevice;
        return result;
    }
    const mdux::core::Extent2D extent{.width = screen.surfaceWidth, .height = screen.surfaceHeight};
    auto                       target = mdux::render::OffscreenTarget::create(device.device(), device.physicalDevice(), extent, device.family());
    if (!target.has_value()) {
        report(result.diagnostics,
               packagePath,
               "VUI008",
               "verification run could not create its offscreen target: " + std::string{mdux::render::describe(target.error())});
        return result;
    }

    std::vector<mdux::draw::UiVertex>    vertices(screen.budget.maxVertices);
    std::vector<mdux::draw::Index>       indices(screen.budget.maxIndices);
    std::vector<mdux::draw::DrawCommand> commands(screen.budget.maxCommands);
    auto                                 drawList = mdux::draw::DrawList::create(vertices, indices, commands, screen.budget);
    if (!drawList.has_value()) {
        report(result.diagnostics,
               packagePath,
               "VUI008",
               "verification run cannot allocate the declared draw budget: " + std::string{mdux::draw::describe(drawList.error())});
        return result;
    }

    for (std::size_t scopeIndex = 0; scopeIndex < (hasText ? locales.size() : 1U); ++scopeIndex) {
        const mv::RenderScope                   scope = hasText ? mv::RenderScope::forLocale(locales[scopeIndex].locale) : mv::RenderScope::localeFree();
        std::optional<mdux::medui::TextBinding> binding;
        LocaleAssets*                           locale = hasText ? &locales[scopeIndex] : nullptr;
        if (hasText) {
            auto made = mdux::medui::TextBinding::create(screen, locale->font, locale->text, std::as_bytes(std::span{locale->textJson}), locale->runs);
            if (!made.has_value()) {
                report(result.diagnostics, packagePath, "VUI008", "authenticated text binding became unavailable before render");
                return result;
            }
            binding.emplace(std::move(*made));
        }
        drawList->reset();
        const auto frame = hasText ? mdux::medui::render(screen, *drawList, *binding) : mdux::medui::render(screen, *drawList);
        if (!frame.has_value()) {
            report(result.diagnostics,
                   packagePath,
                   "VUI008",
                   "screen runtime refused scope '" + std::string{scope.name()} + "': " + std::string{mdux::medui::describe(frame.error())});
            return result;
        }
        const mdux::render::VulkanRenderContext context{.device           = device.device(),
                                                        .physicalDevice   = device.physicalDevice(),
                                                        .renderPass       = target->renderPass(),
                                                        .subpass          = 0,
                                                        .queue            = device.queue(),
                                                        .queueFamilyIndex = device.family(),
                                                        .viewport         = extent};
        auto                                    renderer = hasText ? mdux::render::UiRenderer::createWithCoverageAtlas(context,
                                                                                    shader->view(),
                                                                                    screen.budget,
                                                                                    locale->atlas,
                                                                                    locale->font.atlas.width,
                                                                                    locale->font.atlas.height)
                                                                   : mdux::render::UiRenderer::create(context, shader->view(), screen.budget);
        if (!renderer.has_value()) {
            report(result.diagnostics,
                   packagePath,
                   "VUI008",
                   "verification renderer creation failed: " + std::string{mdux::render::describe(renderer.error())});
            return result;
        }
        Recording recording{.renderer = &*renderer, .list = &*drawList, .error = std::nullopt};
        auto      pixels = target->renderAndRead(device.queue(), clearColor, recordFrame, &recording);
        if (recording.error.has_value() || !pixels.has_value()) {
            const std::string reason = recording.error.has_value() ? std::string{mdux::render::describe(*recording.error)}
                                                                   : std::string{mdux::render::describe(pixels.error())};
            report(result.diagnostics, packagePath, "VUI008", "verification render/readback failed: " + reason);
            return result;
        }
        ++result.renderCount;
        const auto framebuffer = mv::FramebufferView::createPacked(*pixels, screen.surfaceWidth, screen.surfaceHeight);
        if (!framebuffer.has_value()) {
            report(result.diagnostics, packagePath, "VUI008", "readback cannot form a framebuffer: " + std::string{mv::describe(framebuffer.error())});
            return result;
        }
        // Where this scope's outcomes begin, so the diff image below marks the failures of the frame
        // it is drawn on rather than every failure the run has accumulated.
        const std::size_t scopeOutcomeBase = result.outcomes.size();

        for (const mv::GoldenEntry& golden : goldens) {
            // The pre-render pass above rejects both of these, so reaching either here means that
            // validation and rendering have drifted apart. Say so rather than dereferencing null.
            const auto* node = screen.find(golden.nodeId);
            if (node == nullptr) {
                report(result.diagnostics, packagePath, "VUI008", "validated golden node '" + std::string{golden.nodeId} + "' vanished before render");
                return result;
            }
            const auto expectation = mv::GoldenExpectation::create(golden, screen, scope, groundFor(screen, *node));
            if (!expectation.has_value()) {
                report(result.diagnostics,
                       packagePath,
                       "VUI008",
                       "validated golden expectation for node '" + std::string{golden.nodeId}
                           + "' became unconstructible before render: " + std::string{mv::describe(expectation.error())});
                return result;
            }
            for (mv::CvCheck check : golden.cvChecks) {
                const auto checked = check == mv::CvCheck::Bounds ? mv::goldenBounds(*framebuffer, *expectation) : mv::colorHash(*framebuffer, *expectation);
                result.outcomes.push_back(own(checked));
            }
        }
        if (hasText) {
            for (const auto& node : screen.nodes) {
                if (mv::textKeyOf(node).empty())
                    continue;
                const auto expectation = mv::TextExpectation::create(screen, node, *binding, locale->atlas, scope, groundFor(screen, node));
                result.outcomes.push_back(own(mv::inkContainment(*framebuffer, *expectation)));
                result.outcomes.push_back(own(mv::localizedTextPresence(*framebuffer, *expectation)));
            }
        }

        // Written here rather than after the loop, because `pixels` is the target's own storage and
        // the next scope's render overwrites it. A run that failed in three locales needs the frame
        // that failed in each, not three copies of the last one.
        if (!options.diffImageDirectory.empty()) {
            writeDiffImage(result,
                           options.diffImageDirectory,
                           screen.id,
                           scope.name(),
                           *pixels,
                           // Non-negative by `ScreenPackage::validate()`, which refuses a surface
                           // that is not, and by `FramebufferView::createPacked()` above.
                           static_cast<std::uint32_t>(screen.surfaceWidth),
                           static_cast<std::uint32_t>(screen.surfaceHeight),
                           std::span{result.outcomes}.subspan(scopeOutcomeBase));
        }
    }

    if (result.outcomes.size() != result.obligations.size()) {
        report(result.diagnostics,
               packagePath,
               "VUI008",
               std::format("verification produced {} outcomes for {} enumerated obligations", result.outcomes.size(), result.obligations.size()));
        return result;
    }
    const bool failed = std::ranges::any_of(result.outcomes, [](const Outcome& outcome) {
        return !outcome.held();
    });
    result.state      = failed ? RunState::ChecksFailed : RunState::Passed;
    if (failed) {
        for (const Outcome& outcome : result.outcomes) {
            if (!outcome.held())
                reportFailure(result, packagePath, outcome);
        }
    }
    return result;
}

RunResult run(const std::filesystem::path& screenDirectory, const std::filesystem::path& artifactRoot) {
    return run(screenDirectory, RunOptions{.artifactRoot = artifactRoot, .diffImageDirectory = {}});
}

RunResult run(const std::filesystem::path& screenDirectory) {
    const std::filesystem::path normalized = normalizeScreenDirectory(screenDirectory);
    return run(normalized, normalized.parent_path().parent_path());
}

std::string usage() {
    return std::format("usage:\n  {} --screen=<generated/screen/id> --locales=all [--format=json|text]\n"
                       "  {:{}}  [--diff-image-dir=<dir>]\n\n"
                       "Verifies every golden check in every render scope and both mandatory text checks\n"
                       "for every approved locale. The locale manifest cannot be narrowed.\n\n"
                       "--diff-image-dir names where to write <screen>.<scope>.png for each render scope\n"
                       "that fails. It chooses a location, never an expectation: the same checks run and\n"
                       "the same status is returned whether or not it is given.\n",
                       toolName,
                       "",
                       toolName.size());
}

Invocation parseArguments(std::span<const std::string_view> arguments) {
    Invocation result;
    bool       screenSeen  = false;
    bool       localesSeen = false;
    bool       diffSeen    = false;
    for (std::string_view argument : arguments) {
        if (argument == "--help" || argument == "-h")
            throw cli::UsageError{usage()};
        if (argument == "--format=json") {
            result.format = cli::Format::Json;
            continue;
        }
        if (argument == "--format=text") {
            result.format = cli::Format::Text;
            continue;
        }
        if (argument.starts_with("--screen=")) {
            if (screenSeen || argument.size() == std::string_view{"--screen="}.size())
                throw cli::UsageError{"--screen must occur once with a non-empty bundle path\n\n" + usage()};
            result.screenDirectory = normalizeScreenDirectory(std::filesystem::path{argument.substr(std::string_view{"--screen="}.size())});
            screenSeen             = true;
            continue;
        }
        if (argument == "--locales=all") {
            if (localesSeen)
                throw cli::UsageError{"--locales=all must occur exactly once\n\n" + usage()};
            localesSeen = true;
            continue;
        }
        if (argument.starts_with("--locales="))
            throw cli::UsageError{"locale selection cannot narrow the approved manifest; use --locales=all\n\n" + usage()};
        if (argument.starts_with("--diff-image-dir=")) {
            constexpr std::string_view flag = "--diff-image-dir=";
            if (diffSeen || argument.size() == flag.size())
                throw cli::UsageError{"--diff-image-dir must occur at most once with a non-empty directory\n\n" + usage()};
            result.diffImageDirectory = std::filesystem::path{argument.substr(flag.size())};
            diffSeen                  = true;
            continue;
        }
        throw cli::UsageError{"unrecognized argument '" + std::string{argument} + "'\n\n" + usage()};
    }
    if (!screenSeen || !localesSeen)
        throw cli::UsageError{"both --screen=<bundle> and --locales=all are required\n\n" + usage()};
    return result;
}

Invocation parseArguments(int argc, const char* const* argv) {
    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0U);
    for (int index = 1; index < argc; ++index)
        arguments.emplace_back(argv[index]);
    return parseArguments(arguments);
}

}  // namespace mdux::tools::verify
