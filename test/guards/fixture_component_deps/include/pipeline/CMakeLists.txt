# Нарушение запрещённого ребра: ребро pipeline -> toolchain (граница слоёв E20/R34).
# В отличие от aa/cc нарушение НЕ ловится правилами A/B: include подкреплён (см. src/pipeline),
# ребро используется — срабатывает только список _forbidden_edges.
trust_component(pipeline
    LINK_PUBLIC toolchain_lib
)
