# include/verify/CMakeLists.txt
# Слой доказательств: модель обязательств (`obligation.hpp`), контракт доказательного бэкенда
# (`proof_backend.hpp`), сертификат `*.trustcert` (`certificate.hpp`) и флаг `--verify`.
# Не зависит от `pipeline`/`semantic`/`solver`: решатель подключается как `ProofBackend`.

trust_component(verify
    PUBLIC_HEADERS certificate.hpp proof_backend.hpp verify.hpp
    INTERNAL_DIRS model
    SOURCES flags.cpp report.cpp certificate.cpp
    LINK_PUBLIC diag_lib location_lib trust_include_dirs
    LINK_PRIVATE utils_lib
)
