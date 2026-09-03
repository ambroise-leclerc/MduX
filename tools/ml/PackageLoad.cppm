/**
 * @file PackageLoad.cppm
 * @brief Host-tools-zone reader turning `package.json` back into a `ModelPackage`.
 *
 * @compliance ADR-004 Trust zones in C++ (host tools zone)
 * @compliance ADR-008 Zero-SOUP ML inference
 *
 * `mdux.ml.runtime` deliberately does not parse anything: it takes a `ModelPackage` whose spans
 * point at data the caller already has. On a real device that data is generated source. This
 * module is the other way of getting one - parse the committed `package.json` - and it lives in
 * the host-tools zone precisely so that the governed runtime keeps having no parser in it.
 *
 * The weight-swap test deliberately keeps using this path because it loads two committed packages
 * dynamically. Device targets instead use `mdux-mlemit`'s generated `constexpr` package and link
 * no host-tools module (issue #153).
 *
 * ## Why the result is a `unique_ptr`
 *
 * `ModelPackage` is a view: its `layers` and `goldens` spans point into the loaded object's own
 * vectors. Moving that object would leave the spans pointing at the moved-from storage. Returning
 * a `unique_ptr` gives the storage a stable address for its whole lifetime, which is the property
 * the view type needs and the one a value return cannot provide.
 */
module;

export module mdux.tools.ml.packageload;

import std;
import mdux.core.result;
import mdux.evidence.digest;
import mdux.ml.schema;
import mdux.tools.cli;

export namespace mdux::tools::ml {

/**
 * @brief A parsed package and everything its spans point at.
 *
 * Non-copyable and non-movable on purpose - see the module comment.
 */
class LoadedPackage {
public:
    LoadedPackage() = default;
    LoadedPackage(const LoadedPackage&) = delete;
    LoadedPackage& operator=(const LoadedPackage&) = delete;
    LoadedPackage(LoadedPackage&&) = delete;
    LoadedPackage& operator=(LoadedPackage&&) = delete;

    /// The package as the runtime consumes it. Valid for this object's lifetime.
    [[nodiscard]] mdux::ml::ModelPackage view() const noexcept;

    [[nodiscard]] std::string_view id() const noexcept { return id_; }
    [[nodiscard]] std::uint64_t weightsByteLength() const noexcept { return weightsByteLength_; }
    [[nodiscard]] const mdux::evidence::Digest& weightsDigest() const noexcept {
        return weightsDigest_;
    }

private:
    friend mdux::core::Result<std::unique_ptr<LoadedPackage>, cli::Diagnostic> loadPackage(
        std::string_view, std::string_view);

    std::string id_;
    std::uint64_t schemaVersion_{0};
    mdux::evidence::Digest weightsDigest_{};
    std::uint64_t weightsByteLength_{0};
    std::uint32_t inputLength_{0};
    std::uint32_t outputLength_{0};
    std::uint32_t maxScratchFloats_{0};
    std::vector<mdux::ml::LayerDesc> layers_;
    std::vector<std::vector<std::uint32_t>> goldenInputs_;
    std::vector<std::vector<std::uint32_t>> goldenOutputs_;
    std::vector<mdux::ml::GoldenVector> goldenViews_;
};

/**
 * @brief Parses canonical `package.json` text.
 *
 * Validates the assembled package before returning it, so a caller never receives one that
 * `Classifier1D::create()` would reject for a reason this function could have named.
 */
[[nodiscard]] mdux::core::Result<std::unique_ptr<LoadedPackage>, cli::Diagnostic> loadPackage(
    std::string_view text, std::string_view fileName);

}  // namespace mdux::tools::ml
