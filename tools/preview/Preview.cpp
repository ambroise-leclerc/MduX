/** @file Preview.cpp
 * @brief Request-owned compilation, explicit fixtures and production offscreen previews.
 */
#include <vulkan/vulkan.h>
import std;
import mdux.core.units;
import mdux.draw;
import mdux.evidence.json;
import mdux.medui.schema;
import mdux.medui.screen;
import mdux.medui.reading;
import mdux.medui.trace;
import mdux.medui.viewport;
import mdux.tools.input;
import mdux.tools.cli;
import mdux.tools.medui.compile;
import mdux.tools.medui.document;
import mdux.tools.medui.package;
import mdux.tools.medui.parser;
import mdux.tools.medui.grammar;
import mdux.tools.medui.ir;
import mdux.tools.medui.lexer;
import mdux.tools.verify.artifacts;
import mdux.tools.verify.diff;
import mdux.render.offscreen;
import mdux.render.vulkan;
#include "../verify/HeadlessDevice.hpp"
#include "Preview.hpp"
#include "Proposal.hpp"

namespace mdux::tools::preview {
namespace j   = mdux::evidence::json;
namespace md  = mdux::tools::medui;
namespace ms  = mdux::medui;
namespace va  = mdux::tools::verify;
namespace rnd = mdux::render;
namespace {
using V = j::Value;
struct Failure : std::runtime_error {
    int         status;
    std::string code;
    Failure(int s, std::string c, const std::string& message) : std::runtime_error(message), status(s), code(std::move(c)) {}
};
[[noreturn]] void fail(std::string message) {
    throw Failure(422, "PRV002", std::move(message));
}
void put(V& v, std::string key, V value) {
    if (!v.set(std::move(key), std::move(value)))
        throw std::logic_error("duplicate response key");
}
V parse(std::string_view text) {
    auto v = j::parse(text);
    if (!v)
        throw Failure(400, "PRV001", "invalid JSON");
    return std::move(*v);
}
std::string encode(const V& v) {
    auto s = j::write(v);
    if (!s)
        throw std::logic_error("invalid response");
    return *s;
}
const V& member(const V& v, std::string_view key) {
    auto p = v.find(key);
    if (!p)
        fail("missing member: " + std::string(key));
    return *p;
}
std::string_view string(const V& v) {
    auto s = v.asString();
    if (!s)
        fail("expected string");
    return *s;
}
std::string_view str(const V& v, std::string_view key) {
    return string(member(v, key));
}
std::int64_t integer(const V& v) {
    auto n = v.asInt();
    if (!n)
        fail("expected signed integer");
    return *n;
}
std::uint32_t number(const V& v, std::string_view key, std::uint32_t max = std::numeric_limits<std::uint32_t>::max()) {
    auto n = member(v, key).asUInt();
    if (!n || *n > max)
        fail("invalid integer: " + std::string(key));
    return static_cast<std::uint32_t>(*n);
}
void keys(const V& v, std::initializer_list<std::string_view> allowed) {
    if (v.kind() != V::Kind::Object)
        fail("expected object");
    for (const auto& m : v.members())
        if (std::ranges::find(allowed, m.key) == allowed.end())
            fail("unknown member: " + m.key);
}
std::span<const V> array(const V& v) {
    if (v.kind() != V::Kind::Array)
        fail("expected array");
    return v.elements();
}
float scalar(const V& v) {
    auto f = v.asFloat32();
    if (!f || !std::isfinite(*f))
        fail("expected finite float32 bits");
    return *f;
}
mdux::core::ColorRgba8 color(const V& v) {
    keys(v, {"r", "g", "b", "a"});
    return {static_cast<std::uint8_t>(number(v, "r", 255)),
            static_cast<std::uint8_t>(number(v, "g", 255)),
            static_cast<std::uint8_t>(number(v, "b", 255)),
            static_cast<std::uint8_t>(number(v, "a", 255))};
}
std::string bytesText(const std::vector<std::byte>& b) {
    if (b.empty())
        return {};
    return std::string(reinterpret_cast<const char*>(b.data()), b.size());
}
struct Inputs {
    std::filesystem::path                                   root;
    std::map<std::filesystem::path, std::vector<std::byte>> cache;
    std::size_t                                             total{0};
    const std::vector<std::byte>&                           read(const std::filesystem::path& path) {
        auto relative = path.is_absolute() ? path.lexically_relative(root) : path;
        auto checked  = confined(root, relative);
        if (auto it = cache.find(checked); it != cache.end())
            return it->second;
        std::ifstream file(checked, std::ios::binary | std::ios::ate);
        const auto    end = file.tellg();
        if (!file || end < std::streampos{0})
            throw std::filesystem::filesystem_error("input is unreadable", checked, std::make_error_code(std::errc::io_error));
        const auto            size  = static_cast<std::uintmax_t>(end);
        constexpr std::size_t limit = std::size_t{128} * 1024U * 1024U;
        if (size > limit - total)
            throw Failure(413, "PRV003", "input snapshot exceeds 128 MiB");
        file.seekg(0);
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        if (!file || (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
            || file.peek() != std::char_traits<char>::eof())
            throw std::filesystem::filesystem_error("input changed or became unreadable", checked, std::make_error_code(std::errc::io_error));
        total += bytes.size();
        return cache.emplace(checked, std::move(bytes)).first->second;
    }
};
void budget(const md::Recipe& recipe) {
    if (recipe.surfaceWidth <= 0 || recipe.surfaceHeight <= 0 || recipe.surfaceWidth > 4096 || recipe.surfaceHeight > 4096)
        throw Failure(413, "PRV003", "surface exceeds preview limits");
    constexpr std::uint64_t cap = std::uint64_t{64} * 1024U * 1024U;
    const auto&             b   = recipe.budget;
    if (b.maxVertices > cap / sizeof(mdux::draw::UiVertex) || b.maxIndices > cap / sizeof(mdux::draw::Index)
        || b.maxCommands > cap / sizeof(mdux::draw::DrawCommand))
        throw Failure(413, "PRV003", "draw budget exceeds preview limits");
    if (b.maxVertices * sizeof(mdux::draw::UiVertex) + b.maxIndices * sizeof(mdux::draw::Index) + b.maxCommands * sizeof(mdux::draw::DrawCommand) > cap)
        throw Failure(413, "PRV003", "draw budget exceeds preview limits");
}
V diagnostics(const std::vector<cli::Diagnostic>& ds) {
    return parse(cli::render(ds, cli::Format::Json, "mdux-preview"));
}
std::string base64(std::span<const std::byte> data) {
    constexpr std::string_view abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string                result;
    result.reserve((data.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < data.size(); i += 3) {
        auto a  = std::to_integer<unsigned>(data[i]);
        auto b  = i + 1 < data.size() ? std::to_integer<unsigned>(data[i + 1]) : 0U;
        auto c  = i + 2 < data.size() ? std::to_integer<unsigned>(data[i + 2]) : 0U;
        result += abc[a >> 2];
        result += abc[((a & 3U) << 4) | (b >> 4)];
        result += i + 1 < data.size() ? abc[((b & 15U) << 2) | (c >> 6)] : '=';
        result += i + 2 < data.size() ? abc[c & 63U] : '=';
    }
    return result;
}
std::u32string utf32(std::string_view s) {
    // JSON parsing already proved UTF-8 validity; decode its string without locale-dependent APIs.
    std::u32string result;
    for (std::size_t i = 0; i < s.size();) {
        auto     c     = static_cast<unsigned char>(s[i++]);
        unsigned count = c < 128 ? 0U : c < 224 ? 1U : c < 240 ? 2U : 3U;
        char32_t cp    = count == 0 ? c : c & ((1U << (6U - count)) - 1U);
        for (unsigned k = 0; k < count; ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 63U);
        result += cp;
    }
    return result;
}
V requirements(const ms::ScreenPackage& screen) {
    std::vector<V> out;
    for (const auto& n : screen.nodes) {
        auto             kind = ms::kindName(n);
        std::string_view key  = n.id;
        if (auto s = std::get_if<ms::SignalTraceSpec>(&n.payload))
            key = s->streamSource;
        else if (auto viewport = std::get_if<ms::VulkanViewportSpec>(&n.payload))
            key = viewport->streamSource;
        else if (kind != "NumericDisplay" && kind != "Clock" && kind != "StatusIndicator" && kind != "TextInput")
            continue;
        V item = V::emptyObject();
        put(item, "kind", V::string(std::string(kind)));
        put(item, "key", V::string(std::string(key)));
        put(item, "nodeId", V::string(std::string(n.id)));
        out.push_back(std::move(item));
    }
    return V::array(std::move(out));
}
struct Fixtures {
    std::vector<ms::ReadingSlot>   readings;
    std::vector<ms::StatusSlot>    statuses;
    std::vector<ms::TextInputSlot> fields;
    std::vector<ms::SignalSlot>    signals;
    std::vector<ms::ViewportSlot>  viewports;
    std::deque<std::vector<float>> samples;
    std::deque<ms::SampleRing>     rings;
    std::deque<ms::WaterfallGrid>  grids;
    std::deque<std::u32string>     texts;
    std::optional<ms::CivilTime>   now;
    std::string                    clockColor;
    Fixtures(const ms::ScreenPackage& screen, const md::Recipe& recipe, const V& fixture) {
        keys(fixture, {"readings", "statuses", "fields", "signals", "viewports", "clock"});
        std::set<std::pair<std::string, std::string>> used;
        auto                                          lookup = [&](std::string_view category, std::string_view key) -> const V& {
            const auto& table = member(fixture, category);
            if (table.kind() != V::Kind::Object)
                fail("fixture table must be an object");
            used.emplace(category, key);
            return member(table, key);
        };
        for (const auto& n : screen.nodes) {
            if (auto numeric = std::get_if<ms::NumericDisplaySpec>(&n.payload)) {
                const auto& v = lookup("readings", n.id);
                auto        t = std::ranges::find(recipe.numericTemplates, numeric->templateId, &md::NumericTemplate::name);
                if (t == recipe.numericTemplates.end())
                    fail("missing numeric template");
                readings.push_back({n.id, t->rendering, integer(v)});
            } else if (std::holds_alternative<ms::StatusIndicatorSpec>(n.payload)) {
                const auto& v     = lookup("statuses", n.id);
                auto        state = v.asUInt();
                if (!state || *state > std::numeric_limits<std::uint32_t>::max())
                    fail("invalid status index");
                statuses.push_back({n.id, static_cast<std::uint32_t>(*state)});
            } else if (std::holds_alternative<ms::TextInputSpec>(n.payload)) {
                const auto& v = lookup("fields", n.id);
                keys(v, {"text", "caret"});
                texts.push_back(utf32(str(v, "text")));
                const auto&                c = member(v, "caret");
                std::optional<std::size_t> caret;
                if (c.kind() != V::Kind::Null) {
                    auto x = c.asUInt();
                    if (!x || *x > texts.back().size())
                        fail("invalid caret");
                    caret = static_cast<std::size_t>(*x);
                }
                fields.push_back({n.id, texts.back(), caret});
            } else if (auto spec = std::get_if<ms::SignalTraceSpec>(&n.payload)) {
                if (used.contains({"signals", std::string(spec->streamSource)}))
                    continue;
                const auto& v = lookup("signals", spec->streamSource);
                keys(v, {"samples", "minimum", "maximum", "strokeWidth"});
                auto values = array(member(v, "samples"));
                if (values.size() > ms::maxSamplesPerTrace)
                    fail("too many trace samples");
                samples.emplace_back();
                for (const auto& x : values)
                    samples.back().push_back(scalar(x));
                rings.push_back({samples.back(), 0, samples.back().size()});
                signals.push_back({
                    spec->streamSource,
                    &rings.back(),
                    {scalar(member(v, "minimum")), scalar(member(v, "maximum")), static_cast<std::int32_t>(number(v, "strokeWidth", ms::maxStrokeWidth))}
                });
            } else if (auto viewport = std::get_if<ms::VulkanViewportSpec>(&n.payload)) {
                if (used.contains({"viewports", std::string(viewport->streamSource)}))
                    continue;
                const auto& v = lookup("viewports", viewport->streamSource);
                keys(v, {"rows", "bins", "minimum", "maximum", "lowColor", "highColor"});
                auto rows = array(member(v, "rows"));
                auto bins = number(v, "bins", ms::maxWaterfallBins);
                if (!bins || rows.size() > ms::maxWaterfallRows)
                    fail("invalid waterfall shape");
                samples.emplace_back();
                for (const auto& row : rows) {
                    auto values = array(row);
                    if (values.size() != bins)
                        fail("waterfall row width mismatch");
                    for (const auto& x : values)
                        samples.back().push_back(scalar(x));
                }
                if (rows.empty())
                    samples.back().resize(bins);
                grids.push_back({samples.back(), bins, 0, rows.size()});
                viewports.push_back({
                    viewport->streamSource,
                    &grids.back(),
                    {scalar(member(v, "minimum")), scalar(member(v, "maximum")), color(member(v, "lowColor")), color(member(v, "highColor"))}
                });
            } else if (std::holds_alternative<ms::ClockSpec>(n.payload)) {
                const auto& c = member(fixture, "clock");
                keys(c, {"year", "month", "day", "hour", "minute", "second", "colorToken"});
                now = ms::CivilTime{static_cast<std::int32_t>(number(c, "year", 9999)),
                                    static_cast<std::uint8_t>(number(c, "month", 12)),
                                    static_cast<std::uint8_t>(number(c, "day", 31)),
                                    static_cast<std::uint8_t>(number(c, "hour", 23)),
                                    static_cast<std::uint8_t>(number(c, "minute", 59)),
                                    static_cast<std::uint8_t>(number(c, "second", 59))};
                if (now->year < 1
                    || !std::chrono::year_month_day(std::chrono::year(now->year), std::chrono::month(now->month), std::chrono::day(now->day)).ok())
                    fail("invalid civil date");
                clockColor = str(c, "colorToken");
            }
        }
        for (const auto& table : fixture.members()) {
            if (table.key == "clock") {
                if (!now)
                    fail("clock supplied for a screen with no clock");
                continue;
            }
            if (table.value.kind() != V::Kind::Object)
                fail("fixture table must be an object");
            for (const auto& entry : table.value.members())
                if (!used.contains({table.key, entry.key}))
                    fail("unknown fixture binding: " + entry.key);
        }
    }
};
struct Recording {
    rnd::UiRenderer*            renderer{nullptr};
    const mdux::draw::DrawList* list{nullptr};
    bool                        failed{false};
};
void record(VkCommandBuffer cmd, void* opaque) {
    auto& r  = *static_cast<Recording*>(opaque);
    r.failed = !r.renderer->record(cmd, *r.list).has_value();
}
template <class T>
auto checked(T result) {
    if (!result)
        fail("runtime refused binding/frame: " + std::string(ms::describe(result.error())));
    return std::move(*result);
}
void render(V&                            out,
            const ms::ScreenPackage&      screen,
            const md::Recipe&             recipe,
            const V&                      request,
            Inputs&                       inputs,
            const InputReader&            reader,
            std::vector<cli::Diagnostic>& ds) {
    const auto&                     localeValue = member(request, "locale");
    std::optional<va::LocaleAssets> locale;
    if (screen.approvedTextPackages.empty()) {
        if (localeValue.kind() != V::Kind::Null)
            fail("locale must be null for a locale-free screen");
    } else {
        auto tag      = string(localeValue);
        auto approval = std::ranges::find(screen.approvedTextPackages, tag, &ms::TextPackageApproval::locale);
        if (approval == screen.approvedTextPackages.end())
            fail("locale is not approved");
        locale = va::loadLocale(*approval, inputs.root / "generated", ds, reader);
        if (!locale)
            fail("locale assets refused");
    }
    std::optional<va::ImageAssets> image;
    if (screen.approvedImagePackages.size() > 1)
        fail("renderer supports one image atlas per preview");
    if (!screen.approvedImagePackages.empty()) {
        image = va::loadImage(screen.approvedImagePackages.front(), inputs.root / "generated", ds, reader);
        if (!image)
            fail("image assets refused");
    }
    const auto&      fixture = member(request, "fixture");
    Fixtures         f(screen, recipe, fixture);
    ms::TextBinding  text;
    ms::ImageBinding images;
    if (locale)
        text = checked(ms::TextBinding::create(screen, locale->font, locale->text, std::as_bytes(std::span{locale->textJson}), locale->runs));
    if (image)
        images = checked(ms::ImageBinding::create(screen, image->image, std::as_bytes(std::span{image->imageJson}), image->pixels));
    auto                                 signals   = checked(ms::SignalBinding::create(screen, f.signals));
    auto                                 viewports = checked(ms::ViewportBinding::create(screen, f.viewports));
    auto                                 readings  = checked(ms::ReadingBinding::create(screen, f.readings, f.now ? &*f.now : nullptr, f.clockColor));
    auto                                 statuses  = checked(ms::StatusBinding::create(screen, f.statuses));
    auto                                 fields    = checked(ms::TextInputBinding::create(screen, f.fields));
    auto                                 clear     = color(member(request, "clearColor"));
    std::vector<mdux::draw::UiVertex>    vertices(screen.budget.maxVertices);
    std::vector<mdux::draw::Index>       indices(screen.budget.maxIndices);
    std::vector<mdux::draw::DrawCommand> commands(screen.budget.maxCommands);
    auto                                 list = mdux::draw::DrawList::create(vertices, indices, commands, screen.budget);
    if (!list)
        fail("invalid draw storage");
    auto frame = ms::render(screen, *list, text, images, signals, readings, statuses, fields, viewports);
    if (!frame)
        fail("runtime refused frame: " + std::string(ms::describe(frame.error())));
    auto shader = va::loadShader(inputs.root / "generated", ds, reader);
    if (!shader)
        fail("shader assets refused");
    va::HeadlessDevice device;
    if (!device.available())
        throw Failure(503, "PRV004", "no render device: " + std::string(device.reason()));
    mdux::core::Extent2D extent{screen.surfaceWidth, screen.surfaceHeight};
    auto                 target = rnd::OffscreenTarget::create(device.device(), device.physicalDevice(), extent, device.family());
    if (!target)
        throw Failure(503, "PRV005", "offscreen target creation failed");
    rnd::VulkanRenderContext context{.device           = device.device(),
                                     .physicalDevice   = device.physicalDevice(),
                                     .renderPass       = target->renderPass(),
                                     .subpass          = 0,
                                     .queue            = device.queue(),
                                     .queueFamilyIndex = device.family(),
                                     .viewport         = extent};
    auto                     renderer = locale && image ? rnd::UiRenderer::createWithAtlases(context,
                                                                         shader->view(),
                                                                         screen.budget,
                                                                         locale->atlas,
                                                                         locale->font.atlas.width,
                                                                         locale->font.atlas.height,
                                                                         image->pixels,
                                                                         image->image.width,
                                                                         image->image.height)
                                        : locale        ? rnd::UiRenderer::createWithCoverageAtlas(context,
                                                                        shader->view(),
                                                                        screen.budget,
                                                                        locale->atlas,
                                                                        locale->font.atlas.width,
                                                                        locale->font.atlas.height)
                                        : image
                                            ? rnd::UiRenderer::createWithImageAtlas(context, shader->view(), screen.budget, image->pixels, image->image.width, image->image.height)
                                            : rnd::UiRenderer::create(context, shader->view(), screen.budget);
    if (!renderer)
        throw Failure(503, "PRV005", "renderer creation failed");
    Recording recording{&*renderer, &*list};
    auto      pixels = target->renderAndRead(device.queue(), clear, record, &recording);
    if (recording.failed || !pixels)
        throw Failure(503, "PRV005", "render/readback failed");
    auto png = va::encodePng(*pixels, static_cast<std::uint32_t>(screen.surfaceWidth), static_cast<std::uint32_t>(screen.surfaceHeight));
    if (png.empty())
        throw Failure(500, "PRV005", "PNG encoding failed");
    put(out, "pngBase64", V::string(base64(png)));
    put(out, "width", V::integer(screen.surfaceWidth));
    put(out, "height", V::integer(screen.surfaceHeight));
    put(out, "backend", V::string(device.backendName()));
    put(out, "clearColor", member(request, "clearColor"));
    put(out, "fixtureDigest", V::string(va::hexDigest(encode(fixture))));
    put(out, "locale", localeValue);
    put(out, "synthetic", V::boolean(true));
}
/// Lowercase ASCII words joined by single dashes: the part of an issue branch after its number.
bool isSlug(std::string_view s, std::size_t max) {
    return !s.empty() && s.size() <= max && s.front() != '-' && s.back() != '-' && s.find("--") == std::string_view::npos
           && std::ranges::all_of(s, [](unsigned char c) {
                  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
              });
}
/// `develop`, or an issue branch `<number>-<slug>` that a stacked proposal may target (AGENTS.md § 6).
bool isBaseBranch(std::string_view s) {
    const auto dash = s.find('-');
    return s == "develop"
           || (dash != std::string_view::npos && dash > 0 && dash <= 8
               && std::ranges::all_of(s.substr(0, dash),
                                      [](unsigned char c) {
                                          return c >= '0' && c <= '9';
                                      })
               && isSlug(s.substr(dash + 1), 100));
}
bool isOneLine(std::string_view s) {
    return std::ranges::none_of(s, [](unsigned char c) {
        return c < 32 || c == 127;
    });
}
Response propose(const Service&     service,
                 V&                 out,
                 const V&           request,
                 const std::string& recipePath,
                 const std::string& sourcePath,
                 const std::string& disk,
                 const std::string& proposed) {
    const auto issue = number(request, "issue", 99999999);
    if (issue == 0)
        fail("issue must be a positive issue number");
    const std::string slug{str(request, "slug")};
    if (!isSlug(slug, 60))
        fail("slug must be lowercase words joined by single dashes, at most 60 characters");
    const std::string base{str(request, "base")};
    if (!isBaseBranch(base))
        fail("base must be develop or an issue branch named <number>-<slug>");
    std::string title{str(request, "title")};
    if (title.empty() || title.size() > 200 || !isOneLine(title))
        fail("title must be one line of 1 to 200 bytes");
    const std::string description{str(request, "description")};
    if (description.size() > 16000)
        fail("description exceeds 16000 bytes");
    const auto flag = [&](std::string_view key) {
        auto b = member(request, key).asBool();
        if (!b)
            fail("expected boolean: " + std::string(key));
        return *b;
    };
    const bool        dryRun      = flag("dryRun");
    const bool        commentsAck = flag("acknowledgeCommentLoss");
    const bool        safetyAck   = flag("acknowledgeSafetyChanges");
    const std::string loaded{str(request, "baseSourceDigest")};
    if (va::hexDigest(disk) != loaded)
        throw Failure(409, "PRV009", "the source changed on disk after it was loaded; reload the screen and reapply the edit");
    auto before = md::parse(disk, sourcePath);
    auto after  = md::parse(proposed, sourcePath);
    if (!before.ok() || !after.ok())
        fail("the loaded and proposed sources must both parse cleanly");
    if (md::sameScreen(*before.screen, *after.screen))
        fail("the document makes no change to the screen");

    const auto     changes     = md::safetyChanges(*before.screen, *after.screen);
    const bool     commentLoss = md::containsComments(disk);
    std::vector<V> rows;
    for (const auto& change : changes) {
        V row = V::emptyObject();
        put(row, "nodeId", V::string(change.nodeId));
        put(row, "change", V::string(change.change));
        put(row, "before", change.before.empty() ? V::null() : parse(change.before));
        put(row, "after", change.after.empty() ? V::null() : parse(change.after));
        rows.push_back(std::move(row));
    }
    const auto stem = std::format("{}-{}", issue, slug);
    put(out, "commentLoss", V::boolean(commentLoss));
    put(out, "safetyChanges", V::array(std::move(rows)));
    put(out, "branchPrefix", V::string(stem));
    put(out, "base", V::string(base));
    put(out, "writesEnabled", V::boolean(service.proposals.has_value()));
    put(out, "diagnostics", diagnostics({}));
    if (dryRun)
        return {200, encode(out)};
    if (!service.proposals)
        throw Failure(403, "PRV008", "this service was started without proposal writes");
    if (commentLoss && !commentsAck)
        throw Failure(409, "PRV010", "the committed source has // comments that the canonical source drops; acknowledge the loss to propose");
    if (!changes.empty() && !safetyAck)
        throw Failure(409, "PRV011", "the edit adds, removes or changes safety annotations or requirement fields; acknowledge them to propose");

    auto fetched = fetchProposalBase(*service.proposals, base, sourcePath);
    if (!fetched.ok())
        throw Failure(fetched.status, fetched.code, fetched.message);
    if (va::hexDigest(fetched.baseSource) != loaded)
        throw Failure(409,
                      "PRV009",
                      std::format("{} on {} differs from the loaded source; update the served checkout, reload and reapply the edit", sourcePath, base));

    const auto reference = std::format("#{}", issue);
    if (title.find(reference) == std::string::npos)
        title += std::format(" ({})", reference);
    std::string safety;
    for (const auto& change : changes)
        safety += std::format("- `{}`: {}\n", change.nodeId, change.change);
    ProposalPlan plan;
    plan.path       = sourcePath;
    plan.base       = base;
    plan.branchStem = stem;
    plan.source     = proposed;
    plan.title      = title;
    plan.message    = std::format(
        "{}\n\n{}{}Proposed from MedUI Studio (mdux-preview) for {}.\nRecipe: {}\nSafety metadata changes acknowledged: {}\nComment loss acknowledged: {}\n",
        title,
        description,
        description.empty() || description.ends_with('\n') ? "" : "\n\n",
        reference,
        recipePath,
        changes.size(),
        commentLoss ? "yes" : "not applicable");
    plan.body = std::format(
        "## Summary\n\n{}\n\nProposed from MedUI Studio (`mdux-preview`) for {}. The service opened this as a draft and never merges it.\n\n"
        "## Dependency\n\n- **Base branch:** `{}`\n- **Predecessor PR:** {}\n- **Merge order:** requires maintainer review\n\n"
        "## Source change\n\n- File: `{}` (canonical serialization, ADR-023)\n- Base commit: `{}`\n- `//` comments dropped: {}\n\n"
        "## Verification\n\n- [x] `mdux-preview` compiled the proposed source against `{}` before committing.\n"
        "- [ ] Committed artifacts under `generated/` re-baked with `mdux-bake-update` (the Studio does not bake).\n- [ ] CI green.\n\n"
        "## Regulatory impact\n\nSafety metadata changes acknowledged by the author:\n\n{}\nRequires maintainer review; this proposal establishes no "
        "verification or certification claim.\n",
        description.empty() ? title : description,
        reference,
        base,
        base == "develop" ? "not stacked" : "stacked on `" + base + "`",
        sourcePath,
        fetched.baseCommit,
        commentLoss ? "yes, acknowledged" : "none present",
        recipePath,
        safety.empty() ? "none\n" : safety);

    auto outcome = submitProposal(*service.proposals, plan, fetched.baseCommit);
    if (!outcome.ok())
        throw Failure(outcome.status, outcome.code, outcome.message);
    put(out, "branch", V::string(outcome.branch));
    put(out, "commit", V::string(outcome.commit));
    put(out, "baseCommit", V::string(outcome.baseCommit));
    put(out, "pullRequestUrl", outcome.pullRequestUrl.empty() ? V::null() : V::string(outcome.pullRequestUrl));
    put(out, "warning", outcome.warning.empty() ? V::null() : V::string(outcome.warning));
    return {201, encode(out)};
}
/// Lexes a source against the service's structural limits, returning an encoding refusal if any.
std::optional<std::vector<cli::Diagnostic>> bounded(std::string_view text, const std::string& file) {
    if (text.size() > 4194304)
        throw Failure(413, "PRV003", "source exceeds 4 MiB");
    const auto lexed = md::lex(text, file);
    if (std::ranges::any_of(lexed.diagnostics, [](const auto& diagnostic) {
            return diagnostic.code == "MEDUI-E004";
        }))
        return lexed.diagnostics;
    // Bound parser recursion using the compiler's own tokenization, so comments and quoted
    // brackets cannot be mistaken for structure. This is a service limit, not a DSL change.
    if (lexed.tokens.size() > 65536)
        throw Failure(413, "PRV003", "source exceeds 65536 tokens");
    std::size_t depth = 0;
    for (const auto& token : lexed.tokens) {
        if (token.kind == md::TokenKind::LBracket || token.kind == md::TokenKind::LBrace || token.kind == md::TokenKind::LParen) {
            if (++depth > 64)
                throw Failure(413, "PRV003", "source nesting exceeds 64 levels");
        } else if ((token.kind == md::TokenKind::RBracket || token.kind == md::TokenKind::RBrace || token.kind == md::TokenKind::RParen) && depth != 0) {
            --depth;
        }
    }
    return std::nullopt;
}
}  // namespace

std::filesystem::path confined(const std::filesystem::path& root, const std::filesystem::path& relative) {
    if (relative.empty() || relative.is_absolute() || relative.has_root_name())
        throw Failure(403, "PRV006", "path must be repository-relative");
    auto path = root;
    for (const auto& part : relative) {
        auto word = part.generic_string();
        if (std::ranges::any_of(word,
                                [](unsigned char c) {
                                    return c < 32;
                                })
            || word == ".." || word.find('\\') != std::string::npos || word.find(':') != std::string::npos)
            throw Failure(403, "PRV006", "path traversal refused");
        if (word == "." || word.empty())
            continue;
        path       /= part;
        auto status = std::filesystem::symlink_status(path);
        if (std::filesystem::is_symlink(status))
            throw Failure(403, "PRV006", "symlink inputs refused");
    }
    auto resolved = std::filesystem::canonical(path);
    auto rel      = resolved.lexically_relative(root);
    if (rel.empty() || *rel.begin() == ".." || rel.is_absolute())
        throw Failure(403, "PRV006", "path escapes repository");
    return resolved;
}

Response handle(const Service& service, std::string_view route, std::string_view body) {
    std::vector<cli::Diagnostic> ds;
    try {
        Inputs inputs{std::filesystem::canonical(service.root), {}, 0};
        V      out = V::emptyObject();
        put(out, "schemaVersion", V::integer(1));
        if (route == "catalog") {
            put(out, "grammar", md::grammar());
            put(out, "fixtureKinds", parse("[\"readings\",\"statuses\",\"fields\",\"signals\",\"viewports\",\"clock\"]"));
            put(out, "irSchemaVersion", V::unsignedInteger(md::irSchemaVersion));
            put(out,
                "previewDiagnostics",
                parse(
                    R"([{"code":"PRV001","meaning":"malformed request or unsupported schema"},{"code":"PRV002","meaning":"invalid or unsupported input or binding"},{"code":"PRV003","meaning":"resource limit exceeded"},{"code":"PRV004","meaning":"no render device"},{"code":"PRV005","meaning":"render or internal operation failed"},{"code":"PRV006","meaning":"path refused or unavailable"},{"code":"PRV007","meaning":"backend busy"},{"code":"PRV008","meaning":"proposal writes disabled"},{"code":"PRV009","meaning":"proposal base is stale"},{"code":"PRV010","meaning":"comment loss not acknowledged"},{"code":"PRV011","meaning":"safety metadata change not acknowledged"},{"code":"PRV012","meaning":"git operation failed"}])"));
            put(out, "maxInputBytes", V::integer(134217728));
            put(out, "maxDrawBytes", V::integer(67108864));
            put(out, "maxDimension", V::integer(4096));
            put(out, "maxRequestBytes", V::integer(4194304));
            put(out, "documentSchemaVersion", V::unsignedInteger(md::documentSchemaVersion));
            V proposals = V::emptyObject();
            put(proposals, "enabled", V::boolean(service.proposals.has_value()));
            put(proposals, "pullRequests", V::boolean(service.proposals && service.proposals->pullRequests));
            put(out, "proposals", std::move(proposals));
            return {200, encode(out)};
        }
        if (route == "screens") {
            std::vector<std::string> paths;
            auto                     base = confined(inputs.root, "recipes/screen");
            for (const auto& entry : std::filesystem::recursive_directory_iterator(base)) {
                if (entry.is_symlink())
                    continue;
                if (entry.is_regular_file() && entry.path().extension() == ".toml")
                    paths.push_back(entry.path().lexically_relative(inputs.root).generic_string());
            }
            std::ranges::sort(paths);
            std::vector<V> screens;
            for (auto& path : paths)
                screens.push_back(V::string(std::move(path)));
            put(out, "screens", V::array(std::move(screens)));
            return {200, encode(out)};
        }
        if (body.size() > 4194304)
            throw Failure(413, "PRV003", "request exceeds 4 MiB");
        auto request = parse(body);
        if (route == "detail" || route == "document")
            keys(request, {"schemaVersion", "recipe", "source"});
        else if (route == "compile")
            keys(request, {"schemaVersion", "recipe", "source", "document"});
        else if (route == "frame")
            keys(request, {"schemaVersion", "recipe", "source", "document", "locale", "fixture", "clearColor"});
        else if (route == "proposals")
            keys(request,
                 {"schemaVersion",
                  "recipe",
                  "document",
                  "baseSourceDigest",
                  "issue",
                  "slug",
                  "base",
                  "title",
                  "description",
                  "acknowledgeCommentLoss",
                  "acknowledgeSafetyChanges",
                  "dryRun"});
        else
            throw Failure(404, "PRV001", "unknown route");
        if (number(request, "schemaVersion") != 1)
            throw Failure(400, "PRV001", "unsupported schemaVersion");
        std::filesystem::path relative{str(request, "recipe")};
        if (!relative.generic_string().starts_with("recipes/screen/") || relative.extension() != ".toml")
            throw Failure(403, "PRV006", "recipe is outside screen discovery");
        const auto& recipeBytes = inputs.read(inputs.root / relative);
        if (recipeBytes.size() > 4194304)
            throw Failure(413, "PRV003", "recipe exceeds 4 MiB");
        auto recipe = md::parseRecipe(bytesText(recipeBytes), relative.generic_string(), ds);
        if (!recipe) {
            put(out, "diagnostics", diagnostics(ds));
            return {422, encode(out)};
        }
        budget(*recipe);
        auto sourcePath = confined(inputs.root, recipe->source);
        if (std::filesystem::equivalent(sourcePath, confined(inputs.root, relative)))
            fail("recipe and source must be distinct files");
        if (request.find("source") && request.find("document"))
            throw Failure(400, "PRV001", "source and document are mutually exclusive");
        const bool  proposing = route == "proposals";
        std::string canonical;
        if (proposing)
            static_cast<void>(member(request, "document"));
        if (auto document = request.find("document")) {
            auto text = md::sourceFromDocument(*document);
            if (!text)
                fail(text.error());
            canonical = std::move(*text);
        }
        // A proposal changes the file on disk that the author loaded, never an earlier overlay.
        std::string disk;
        if (proposing) {
            disk = bytesText(inputs.read(sourcePath));
            if (auto refused = bounded(disk, recipe->source)) {
                put(out, "diagnostics", diagnostics(*refused));
                return {422, encode(out)};
            }
        }
        if (auto source = request.find("source"); source || !canonical.empty()) {
            auto                   s = source ? string(*source) : std::string_view{canonical};
            std::vector<std::byte> bytes(s.size());
            if (!s.empty())
                std::memcpy(bytes.data(), s.data(), s.size());
            const auto old      = inputs.cache.find(sourcePath);
            const auto oldSize  = old == inputs.cache.end() ? 0 : old->second.size();
            const auto retained = inputs.total - oldSize;
            if (bytes.size() > std::size_t{128} * 1024 * 1024 - retained)
                throw Failure(413, "PRV003", "input snapshot exceeds 128 MiB");
            inputs.total             = retained + bytes.size();
            inputs.cache[sourcePath] = std::move(bytes);
        }
        InputReader reader = [&](const std::filesystem::path& p) -> std::optional<std::vector<std::byte>> {
            try {
                return inputs.read(p);
            } catch (const std::filesystem::filesystem_error&) {
                return std::nullopt;
            }
        };
        const auto text = bytesText(inputs.read(sourcePath));
        if (text.size() > 4194304)
            throw Failure(413, "PRV003", "source exceeds 4 MiB");
        if (route == "detail") {
            const auto lexed = md::lex(text, recipe->source);
            if (std::ranges::any_of(lexed.diagnostics, [](const auto& diagnostic) {
                    return diagnostic.code == "MEDUI-E004";
                })) {
                put(out, "diagnostics", diagnostics(lexed.diagnostics));
                return {422, encode(out)};
            }
            put(out, "sourceDigest", V::string(va::hexDigest(text)));
            put(out, "source", V::string(text));
            put(out, "recipe", recipe->toOptions());
            put(out, "diagnostics", diagnostics(ds));
            return {200, encode(out)};
        }
        if (auto refused = bounded(text, recipe->source)) {
            put(out, "diagnostics", diagnostics(*refused));
            return {422, encode(out)};
        }
        put(out, "sourceDigest", V::string(va::hexDigest(text)));
        if (route == "document") {
            // ADR-023 decision 2: a recovered partial screen is never offered for editing.
            auto parsed = md::parse(text, recipe->source);
            if (!parsed.ok()) {
                put(out, "diagnostics", diagnostics(parsed.diagnostics));
                return {422, encode(out)};
            }
            put(out, "documentSchemaVersion", V::unsignedInteger(md::documentSchemaVersion));
            put(out, "document", md::screenDocument(*parsed.screen));
            put(out, "commentLines", V::boolean(md::containsComments(text)));
            put(out, "diagnostics", diagnostics(ds));
            return {200, encode(out)};
        }
        if (!canonical.empty())
            put(out, "source", V::string(canonical));
        std::string ir;
        auto        compiled = md::run(*recipe, relative.generic_string(), recipeBytes, inputs.root, ds, &ir, reader);
        if (!ir.empty())
            put(out, "ir", parse(ir));
        if (!compiled) {
            put(out, "diagnostics", diagnostics(ds));
            return {422, encode(out)};
        }
        if (proposing)
            return propose(service, out, request, relative.generic_string(), sourcePath.lexically_relative(inputs.root).generic_string(), disk, canonical);
        auto document = md::readPackage(compiled->packageJson, "preview/package.json");
        if (!document.ok())
            fail("compiled package refused");
        auto screen = document.document.package();
        put(out, "package", parse(compiled->packageJson));
        put(out, "goldens", parse(compiled->goldensJson));
        put(out, "packageDigest", V::string(va::hexDigest(compiled->packageJson)));
        put(out, "requiredBindings", requirements(screen));
        if (route == "frame")
            render(out, screen, *recipe, request, inputs, reader, ds);
        else if (route != "compile")
            throw Failure(404, "PRV001", "unknown route");
        put(out, "diagnostics", diagnostics(ds));
        return {200, encode(out)};
    } catch (const Failure& e) {
        ds.push_back(cli::Diagnostic{.file = {}, .code = e.code, .message = e.what(), .fixHint = {}});
        return {e.status, cli::render(ds, cli::Format::Json, "mdux-preview")};
    } catch (const std::filesystem::filesystem_error& e) {
        ds.push_back(cli::Diagnostic{.file    = e.path1().generic_string(),
                                     .code    = "PRV002",
                                     .message = "input file is unavailable or unreadable",
                                     .fixHint = "check that the input exists and has been generated"});
        return {422, cli::render(ds, cli::Format::Json, "mdux-preview")};
    } catch (const std::exception&) {
        ds.push_back(cli::Diagnostic{.file = {}, .code = "PRV005", .message = "preview could not complete", .fixHint = {}});
        return {500, cli::render(ds, cli::Format::Json, "mdux-preview")};
    }
}
}  // namespace mdux::tools::preview
