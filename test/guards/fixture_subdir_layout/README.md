# test/guards/fixture_subdir_layout
# Негативная фикстура subdir_layout_guard. Нарушает все четыре правила гейта:
#   A) TU в корне src/demo/ (компонент не малый: 5 публичных заголовков > порога 4);
#   B) подкаталог src/demo/misc/ не объявлен ни в INTERNAL_DIRS, ни в PUBLIC_DIRS;
#   C) документ разработки include/demo/NOTES.md в корне компонента;
#   D) INCLUDE_PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} публикует корень компонента.
# Гейт на этой фикстуре обязан завершаться ошибкой (selftest WILL_FAIL).
