# test/build_status/CMakeLists.txt
# Контракт обёртки сборки: файлы статуса именованного сеанса живут только в подкаталоге
# каталога сборки (дерево проекта не загрязняется), `--wait` их удаляет.
#
# Позитивные/негативные проверки — в selftest.sh; ранняя стадия (не требует собранных
# бинарников, только сам wrapper и `--target help`).

trust_add_test_suite(build_status_tests
    TIMEOUT 180
    LABELS build
    # Тест инспектирует каталог сборки и создаёт файлы блокировки: под параллельной нагрузкой
    # форк дочерней оболочки может не удаться (пустой вывод обёртки), поэтому — последовательно.
    RUN_SERIAL
    COMMAND bash ${CMAKE_SOURCE_DIR}/test/build_status/selftest.sh
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}
)
