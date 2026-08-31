/**
 * @file GeneratedModelModuleConsumer.cpp
 * @brief Compiles and consumes the generated model module form.
 */
import std;
import mdux.ml.schema;
import mdux.ml.generated.model_ecg_demo;

#include "GeneratedModelConsumers.hpp"

namespace mdux::test::generated {

/// Returns the package compiled through its generated module interface.
mdux::ml::ModelPackage modelFromModule() noexcept {
    return mdux::ml::generated::model_ecg_demo::package();
}

}  // namespace mdux::test::generated
