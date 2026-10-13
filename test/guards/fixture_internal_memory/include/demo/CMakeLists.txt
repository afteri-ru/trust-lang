# Фикстура internal_memory_guard:
#   small/ — малая реализация без MEMORY.md (допустимо);
#   big/   — большая реализация (4 файла) без MEMORY.md (нарушение);
#   terse/ — MEMORY.md меньше нижнего порога 1000 байт (нарушение).
trust_component(demo
    NO_LIB
    INTERNAL_DIRS small big terse
)
