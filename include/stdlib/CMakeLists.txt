# include/stdlib/CMakeLists.txt
# Standard library container: `stdlib_lib` (header-only interface) plus the core and
# provider packages in subdirectories. `include/stdlib` itself is NOT a package: each
# package is a directory with its own trust.toml (`core/`, `tensor/`).
# The packages are added by the root CMakeLists.txt after the `trust` target exists.

trust_component(stdlib
    INTERFACE
    PUBLIC_DIRS core tensor
    LINK_PUBLIC trust_include_dirs
)
