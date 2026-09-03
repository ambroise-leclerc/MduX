/**
 * @file GeneratedModelConsumers.hpp
 * @brief Declares the module and header routes to one generated model package.
 */
#pragma once

namespace mdux::test::generated {

/// @brief Returns the package compiled through its generated module interface.
[[nodiscard]] mdux::ml::ModelPackage modelFromModule() noexcept;

/// @brief Returns the package compiled through its generated header fallback.
[[nodiscard]] mdux::ml::ModelPackage modelFromHeader() noexcept;

}  // namespace mdux::test::generated
