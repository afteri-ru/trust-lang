# tensor provider package

Stable tensor API for generated programs.

- `tensor.src` — implementation of the pre-declared Core type `Tensor` (`Tensor = %trust::Tensor { };`),
  carrying `@[include("@stdlib/tensor.hpp")]` and `@[link("tensor")]`.
- `trust.toml` — `[[artifact]] provides=["tensor"] kind="toolchain" libs=["tensor_cpu"]`,
  `capabilities=["with-torch"]`; `[[link.headers]] stdlib/tensor.hpp -> tensor`.
- Public header `stdlib/tensor.hpp` (`trust::Tensor<Elem>`, `TensorHandle`) is self-contained (no
  libtorch types) and is delivered by THIS package; the C ABI lives in `stdlib/tensor_backend.hpp`.
- The `tensor_cpu` toolchain plugin (built with `-DWITH_TORCH=ON`) encapsulates libtorch; generated
  programs link only the plugin.

Using the package without a `WITH_TORCH` toolchain is a compile-time diagnostic (capability check),
not a linker error.
