# include/package/CMakeLists.txt
# Пакет trust (.trust): манифест trust.toml, примитивы gzip-tar, создание/распаковка и проверка
# целостности. Ниже pipeline и module_loader: используется и ими, и драйвером (--package/--extract).

trust_component(package
    PUBLIC_HEADERS manifest.hpp package.hpp
    INTERNAL_DIRS archive
    SOURCES manifest.cpp tar.cpp resource.cpp package.cpp
    # manifest.hpp (публичный заголовок) включает <toml++/toml.hpp> - include-путь обязан
    # распространяться на потребителей (pipeline_lib, unit_tests и др.).
    LINK_PUBLIC trust_include_dirs tomlplusplus::tomlplusplus
    LINK_PRIVATE utils_lib
    # trust/version.h генерируется отдельным target'ом.
    DEPENDS generate_makefile_header
)
