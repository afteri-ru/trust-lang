#pragma once
// Module API — ABI-стабильные структуры для __trust_get_exports
// Используется как компилятором (--module-info), так и встраивается
// в сгенерированный .cppt код через #embed при сборке модуля.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* name;
    void* addr;
} __trust_export_entry;

typedef struct {
    int count;
    const char* version;
    const __trust_export_entry* entries;
} __trust_exports;

#ifdef __cplusplus
}
#endif