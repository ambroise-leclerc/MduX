/**
 * @file GeneratedModelHeaderConsumer.cpp
 * @brief Compiles and consumes the generated model header fallback.
 */
import std;
import mdux.ml.schema;

#include "GeneratedModelConsumers.hpp"
#include "model_ecg_demo.hpp"

namespace mdux::test::generated {

/// Returns the package compiled through its generated header fallback.
mdux::ml::ModelPackage modelFromHeader() noexcept {
    return mdux::ml::generated::model_ecg_demo::package();
}

}  // namespace mdux::test::generated
