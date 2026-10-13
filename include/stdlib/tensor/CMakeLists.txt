# include/stdlib/tensor/CMakeLists.txt
# Provider package for the stable tensor API: `tensor.trust` in share/trust.
# The package contains the Trust API (tensor.src) and metadata only; the implementation
# is the toolchain `tensor_cpu` plugin (built by include/runtime/CMakeLists.txt when
# WITH_TORCH=ON). Using the package without that capability is a compile-time diagnostic.

set(TRUST_TENSOR_PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(TRUST_TENSOR_PACKAGE_ARCHIVE "${CMAKE_BINARY_DIR}/share/trust/tensor.trust")

add_custom_target(trust_tensor_package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/share/trust"
    COMMAND $<TARGET_FILE:trust> --package "${TRUST_TENSOR_PACKAGE_DIR}" -o "${TRUST_TENSOR_PACKAGE_ARCHIVE}"
    DEPENDS trust
    COMMENT "Building tensor provider package -> ${TRUST_TENSOR_PACKAGE_ARCHIVE}"
    VERBATIM
)

# Self-registration: the distribution aggregate target depends on every registered
# package without a hardcoded list in the root/package scripts.
set_property(GLOBAL APPEND PROPERTY TRUST_PACKAGE_TARGETS trust_tensor_package)
