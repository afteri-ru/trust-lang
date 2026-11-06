# test/guards/fixture_subdir_layout
# Негативная фикстура subdir_layout_guard. Нарушает механические правила гейта:
#   A) TU в корне src/demo/ (компонент не объявлен одно-TU в _root_tu_by_design);
#   B) подкаталог src/demo/misc/ не объявлен ни в INTERNAL_DIRS, ни в PUBLIC_DIRS;
#   D) INCLUDE_PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} публикует корень компонента.
# Гейт на этой фикстуре обязан завершаться ошибкой (selftest WILL_FAIL).
