# ADR-003: Compiler Modernization for C++23 Modules Support

## Status
Accepted (2025-07-26)

## Context
MduX is evolving towards a pure C++23 modules-based architecture to provide better build performance, improved dependency management, and enhanced developer experience. This evolution requires updating the minimum supported compiler versions to ensure robust C++23 modules support.

The current compiler requirements were:
- MSVC 19.35+ (Visual Studio 2022)
- GCC 14+
- Clang 17+

However, C++23 modules support varies significantly across compiler versions, and earlier versions have known issues with modules implementation that could impact the reliability and performance of a medical device library.

## Medical Device Considerations

### IEC 62304 Implications (Software Lifecycle)
- **Compiler Stability**: Medical device software requires stable, well-tested compiler toolchains
- **Modules Benefits**: C++23 modules provide better dependency tracking and build reproducibility
- **Validation**: Updated compilers must be validated for medical device software development
- **Long-term Support**: Selected compiler versions must have long-term support for medical device lifecycles

### Performance and Reliability
- **Build Performance**: C++23 modules can significantly reduce compilation times in large medical device codebases
- **Dependency Management**: Modules provide explicit dependency management crucial for medical device software
- **Symbol Isolation**: Better symbol isolation reduces the risk of ODR violations in complex medical device software

## Decision
**Update minimum compiler versions to:**
- **MSVC 17.14+** (Visual Studio 2022 version 17.10+)
- **GCC 15+**
- **Clang 20+**

**Rationale:**
1. **MSVC 17.14**: First version with stable C++23 modules support and significant modules performance improvements
2. **GCC 15**: Includes major improvements to modules implementation and better standards compliance
3. **Clang 20**: Provides comprehensive C++23 modules support with robust implementation

## Alternatives Considered

### 1. Maintain Current Requirements (Rejected)
**Pros:**
- No breaking changes for existing development environments
- Broader compiler support

**Cons:**
- Incomplete or buggy C++23 modules support
- Potential build issues and performance problems
- Limited ability to leverage modern C++ features

### 2. Even Higher Requirements (Considered)
**Pros:**
- Access to the latest C++23 features and optimizations
- Better modules performance

**Cons:**
- Too restrictive for current development environments
- May exclude valid medical device development platforms

### 3. Gradual Migration (Rejected)
**Pros:**
- Smoother transition period
- Maintain backward compatibility

**Cons:**
- Complicates build system and testing matrix
- Delays the benefits of full modules adoption
- Increases maintenance burden

## Consequences

### Positive
- **Enhanced Build Performance**: Faster compilation times with mature modules support
- **Better Dependency Management**: Explicit module dependencies improve build reliability
- **Future-Proof Architecture**: Positions MduX for continued C++23 evolution
- **Improved Developer Experience**: Better tooling support and error messages
- **Medical Device Benefits**: More reliable builds and clearer dependency tracking

### Negative
- **Breaking Change**: Requires development environment updates
- **CI/CD Updates**: Continuous integration systems must be updated
- **Documentation Updates**: All build documentation requires revision
- **Transition Period**: Development teams need time to update toolchains

### Risks and Mitigations
- **Adoption Resistance**: Some teams may resist compiler updates
  - *Mitigation*: Provide clear migration guide and rationale
- **Toolchain Availability**: Newer compilers may not be available on all platforms
  - *Mitigation*: Focus on Windows/Linux as primary platforms, provide installation guidance
- **Regression Risk**: Newer compilers might introduce new issues
  - *Mitigation*: Comprehensive CI testing across all supported compiler versions

## Implementation Plan

### Phase 1: Build System Updates (Completed)
- ✅ Update CMakeLists.txt with version checks
- ✅ Add explicit error messages for unsupported compiler versions
- ✅ Update CI configuration with new compiler versions

### Phase 2: Documentation Updates (Completed)
- ✅ Update CLAUDE.md with new requirements
- ✅ Update README.md with compiler version specifications
- ✅ Update project descriptions to reflect modules-based architecture

### Phase 3: CI/CD Integration (Completed)
- ✅ Add Clang 20 build job to GitHub Actions
- ✅ Update existing jobs to use GCC 15
- ✅ Update static analysis tools to latest versions

### Phase 4: Validation and Testing (Completed)
- ✅ Verify CMake configuration works with updated requirements
- ✅ Test build process across all supported compilers
- ✅ Fix Clang 20 compatibility issues (removed unsupported `-fconcepts-diagnostics-depth` flag)
- ✅ Clean up unused code (removed unused `shouldCloseFlag` member variable)
- ✅ Validate examples and tests work with new toolchain
- ✅ Confirm all unit tests pass with both GCC 15 and Clang 20

## Migration Guide

### For Development Teams
1. **Windows**: Update to Visual Studio 2022 v17.10 or later
2. **Linux**: Install GCC 15+ or Clang 20+
3. **CMake**: Ensure CMake 4.0+ is available
4. **Verification**: Run `cmake --version` and compiler version checks

### For CI/CD Systems
1. Update runner images to include required compiler versions
2. Update build scripts to use new compiler flags
3. Verify all automated tests pass with new toolchain
4. Update deployment documentation

## References
- [C++23 Modules Support in MSVC](https://docs.microsoft.com/en-us/cpp/cpp/modules-cpp)
- [GCC C++23 Modules Documentation](https://gcc.gnu.org/wiki/cxx-modules)
- [Clang C++23 Modules Status](https://clang.llvm.org/cxx_status.html)
- [IEC 62304: Medical device software lifecycle processes](https://www.iso.org/standard/64686.html)

## Approval
- **Decision Date**: 2025-07-26
- **Approved By**: Architecture Team, CTO
- **Review Date**: 2026-01-26 (6-month review for implementation effectiveness)
- **Implementation Status**: Completed (All phases)