#pragma once

// include/symbols/scope_path.hpp
// Единственная точка разбора текста namespace-области на сегменты (`ns::name::` / `::ns::name`).
// Классификация создателя скоупа (область/кодовый блок/метка/интерфейсный блок) остаётся у
// потребителя: она РАЗНАЯ для «текущей позиции» (`NamespaceScope::namespacePath`) и
// «модульного уровня» (`DeclarationRegistry::surfaceKeyForPosition`).

#include <string_view>
#include <vector>

namespace trust {

/// Непустые сегменты текста области имён (разделитель `::`). Пустой текст — пустой результат.
[[nodiscard]] std::vector<std::string_view> namespaceSegmentsOf(std::string_view text);

} // namespace trust
