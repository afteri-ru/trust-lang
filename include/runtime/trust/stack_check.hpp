// trust/stack_check.hpp - защита стека от переполнения (runtime primitives).
//
// Публичный рантайм-заголовок: самодостаточный (стандартные заголовки + pthread/ELF на Linux),
// чтобы сгенерированные C++-программы могли подключить его без дерева include компилятора.
// На этапе сборки встраивается в trust-runtime (via #embed, ELF-секция "trust/stack_check.hpp");
// конвейер извлекает его во временный каталог trust/ и линкует рантайм, когда программа реально
// использует контроль стека.
//
// Основная идея: перед вызовом защищаемой функции проверяется свободное место на стеке, и если
// его недостаточно - выбрасывается программное исключение `stack_overflow`, которое можно
// перехватить и обработать внутри приложения, не дожидаясь segmentation fault.
//
// Семантика проверок (резерв `reserve` нужен как минимальный запас на обработку возможного
// прерывания/исключения во время вызова):
//   check_overflow(N)      - throw, если свободно < N + reserve   (free >= N + reserve)
//   check_stack_limit()    - throw, если свободно < m_stack_limit + reserve
//                            (m_stack_limit - максимальный размер стека функции из секции .stack_sizes,
//                            вычисляется ОДИН раз при инициализации рантайма)
//   check_reserve()        - throw, если свободно < reserve       (только минимальный резерв)
//   set_reserve(N)         - установить минимальный резерв `reserve` для ВСЕХ функций (thread_local)
//   set_limit(A)           - ограничить m_stack_limit перечисленными функциями (по адресам); пусто = все
//   get_limit()            - текущий m_stack_limit (вычисляется один раз и кешируется)
//
// Для работы `check_stack_limit()` программа должна быть скомпилирована с -fstack-size-section
// (секция .stack_sizes). Границы стека потока определяются через pthread_getattr_np (Linux).
//
// TLS-переменная `info` определяется в сгенерированном entry `_main.cppt`:
//   const thread_local trust::stack_check trust::stack_check::info;
// (единственная TU программы; `reserve`/`m_stack_limit` - header-only inline thread_local).

#ifndef TRUST_STACK_CHECK_HPP
#define TRUST_STACK_CHECK_HPP

#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "trust/interrupt.hpp" // IntMinus: переполнение стека - встроенная ошибка языка, ловится {- ... -}

#ifndef STACK_RESERVE
#define STACK_RESERVE 8192
#endif // STACK_RESERVE

namespace trust {

struct stack_check;

// Исключение переполнения стека - программное (перехватываемое) вместо segmentation fault.
// Встроенная в язык ошибка: подкласс IntMinus (отрицательное прерывание), поэтому перехватывается
// trust-блоком {- ... -} (catch IntMinus&) и одновременно является std::runtime_error (IntMinus наследует
// его), т.е. ловится и C++-кодом через catch(std::runtime_error&). Несёт диагностику (size/info/frame).
struct stack_overflow : public IntMinus {
    size_t size;             ///< требуемый размер свободного места, по которому сработала проверка
    const stack_check* info; ///< информация о стеке потока (границы/кадр)
    stack_overflow(size_t size, const stack_check* stack)
    : IntMinus(std::string("Stack overflow"))
    , size(size)
    , info(stack) {}
};

struct stack_check {
    // Минимальный резерв стека (reserve) по умолчанию - на обработку прерывания/исключения.
    static constexpr size_t default_reserve = STACK_RESERVE;

    // Минимальный резерв `reserve` (thread_local, header-only inline) - устанавливается set_reserve()
    // для ВСЕХ функций и учитывается всеми проверками. См. set_reserve()/get_reserve().
    static inline thread_local size_t reserve = default_reserve;

    // Информация о стеке текущего потока (thread_local; единственный entity на программу).
    // ОПРЕДЕЛЯЕТСЯ ВНЕШНЕ (в сгенерированном `_main.cppt`), здесь - только объявление:
    //   const thread_local trust::stack_check trust::stack_check::info;
    // Конструктор вычисляет границы стека текущего потока при первом обращении.
    static const thread_local stack_check info;

    // Границы стека текущего потока (top - верх, bottom - низ/начало).
    void* top = nullptr;
    void* bottom = nullptr;
    // Кадр, на котором было выброшено исключение (для диагностики).
    void* frame = nullptr;

    // Перечень функций (адреса), по которым считается m_stack_limit для limit-проверок; пусто = все.
    // Устанавливается set_limit() в точке входа ДО первой проверки. thread_local - как reserve и кеш
    // m_stack_limit: контроль стека ведётся по-потоково (у каждого потока свой стек), поэтому
    // ограничение списка функций применяется к текущему потоку (без гонки между потоками).
    static inline thread_local std::vector<void*> include_functions;

    stack_check() { get_stack_info(top, bottom); }

    // --- Проверки свободного места на стеке ---

    /// throw, если свободно < N + reserve. Используется атрибутом `@[stack_check(N)@]`
    /// и макросом `@stack_check(N)` (явный размер свободного места).
    /// ВАЖНО: free_space() меряет от текущего кадра до дна стека и НЕ включает кадр вызываемой
    /// функции. Для @[stack_check(N)@] N должно закладывать ожидаемый кадр callee (резерв `reserve`
    /// покрывает обработку исключения). В limit-режиме (@[stack_check@]) m_stack_limit (максимум по
    /// .stack_sizes) уже покрывает кадр любой функции программы.
    static inline void check_overflow(size_t N) {
        if (free_space() < N + reserve) {
            throw_stack_overflow(N + reserve, info);
        }
    }

    /// throw, если свободно < m_stack_limit + reserve. m_stack_limit (максимальный размер стека
    /// функции из секции .stack_sizes) вычисляется ОДИН раз при первом limit-проверке и кешируется.
    /// Используется атрибутом `@[stack_check@]` (без аргумента) и функцией `%trust_stack_check`.
    static inline void check_stack_limit() {
        // Считаем лимит один раз (stackLimit() кешируется; не вызывать дважды на проверку).
        const size_t need = stackLimit() + reserve;
        if (free_space() < need) {
            throw_stack_overflow(need, info);
        }
    }

    /// Проверка минимального резерва. При free < reserve на стеке НЕ хватает места для создания
    /// исключения и обработки ошибки в рантайме, поэтому вместо броса stack_overflow выполняется
    /// abort с диагностикой (нельзя сформировать исключение при исчерпанном резерве).
    static inline void check_reserve() {
        if (free_space() < reserve) {
            std::fprintf(stderr,
                         "stack overflow: insufficient stack reserve (%zu bytes) to create exception and "
                         "handle the error in the runtime\n",
                         static_cast<std::size_t>(reserve));
            std::fflush(stderr);
            std::_Exit(EXIT_FAILURE);
        }
    }

    /// Установить минимальный резерв `reserve` для ВСЕХ функций (thread_local). Влияет на все
    /// проверки (check_overflow/check_stack_limit/check_reserve).
    static inline void set_reserve(size_t N) { reserve = N; }

    /// Текущий минимальный резерв `reserve`.
    static inline size_t get_reserve() { return reserve; }

    /// Ограничить m_stack_limit для limit-проверок перечисленными функциями (адреса). Пустой список -
    /// используются все функции секции .stack_sizes. Сброс кеша m_stack_limit (пересчёт при следующей
    /// limit-проверке). Реальная функция `%trust_stack_check_set_limit` / set_limit({&f1, &f2}).
    static inline void set_limit(std::vector<void*> addrs) {
        include_functions = std::move(addrs);
        m_stack_limit_ready = false;
    }

    /// Текущий общий лимит (m_stack_limit): максимальный размер стека функции по заданному перечню
    /// (или по всем функциям .stack_sizes), вычисляется один раз и кешируется. Реальная функция
    /// `%trust_stack_check_get_limit` / get_limit().
    static inline size_t get_limit() { return stackLimit(); }

    /// Свободное место на стеке текущего потока (текущий кадр - начало стека).
    static inline size_t free_space() {
        char* cur = static_cast<char*>(__builtin_frame_address(0));
        char* bot = static_cast<char*>(info.bottom);
        return cur > bot ? static_cast<size_t>(cur - bot) : 0;
    }

    /// Границы стека текущего потока (Linux: pthread_getattr_np).
    static bool get_stack_info(void*& top, void*& bottom);

    /// Максимальный размер стека функции из секции .stack_sizes (по include_functions или по всем).
    static size_t get_stack_limit();

  private:
    // Макс. размер стека функции для limit-проверок: вычисляется ОДИН раз (лениво при первой
    // check_stack_limit) и кешируется; set_limit сбрасывает кеш.
    static inline thread_local size_t m_stack_limit = 0;
    static inline thread_local bool m_stack_limit_ready = false;

    // Глобальный кеш максимума по ВСЕМ функциям .stack_sizes (не зависит от include_functions).
    // Избегает повторного mmap+парсинга /proc/self/exe на каждый поток (m_stack_limit - thread_local).
    static inline size_t s_stack_limit_all = 0;
    static inline bool s_stack_limit_all_ready = false;

    static size_t stackLimit() {
        if (!m_stack_limit_ready) {
            m_stack_limit = get_stack_limit();
            m_stack_limit_ready = true;
        }
        return m_stack_limit;
    }

    /// Бросок исключения (optnone, чтобы не оптимизировать путь броса).
    [[clang::optnone]] static void throw_stack_overflow [[noreturn]] (size_t size, const stack_check& st) {
        *const_cast<void**>(&st.frame) = __builtin_frame_address(0);
        throw stack_overflow(size, &st);
    }
};

} // namespace trust

#if defined(__linux__)

#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace trust {

// Информация о размере стека текущего потока (Linux).
inline bool trust::stack_check::get_stack_info(void*& top, void*& bottom) {
    pthread_attr_t attr;
    pthread_getattr_np(pthread_self(), &attr);

    size_t stack_size;
    pthread_attr_getstack(&attr, (void**)&bottom, &stack_size);

    pthread_attr_destroy(&attr);

    top = static_cast<char*>(bottom) + stack_size;

    // Взятие адресов контрольных функций предотвращает их удаление при оптимизации из-за
    // отсутствия прямых вызовов в другом коде. НЕ вызываем их здесь (раньше в конструкторе
    // реально исполнялись check_reserve()/check_overflow(), что на малом стеке потока могло
    // abort/throw в момент инициализации TLS `info`): достаточно odr-use (взятия адреса).
    (void)&check_reserve;
    (void)&check_overflow;

    return top > bottom;
}

// Вспомогательная структура для чтения секций отображённого ELF-файла.
struct MappedELF {
    void* mapped;
    size_t size;

    MappedELF()
    : mapped(nullptr)
    , size(0) {
        int fd = open("/proc/self/exe", O_RDONLY);
        if (fd < 0) {
            throw std::runtime_error("Error open file '/proc/self/exe'!");
        }

        struct stat st;
        if (fstat(fd, &st) < 0) {
            close(fd);
            throw std::runtime_error("Error call 'fstat'!");
        }

        mapped = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (mapped == MAP_FAILED) {
            mapped = nullptr;
            throw std::runtime_error("Error call 'mmap'!");
        }
        size = st.st_size;
    }

    ~MappedELF() {
        if (mapped) {
            munmap(mapped, size);
        }
    }

    // База загрузки главного исполняемого файла через dl_iterate_phdr.
    uint64_t get_base_address_dl() const {
        struct BaseAddrContext {
            uint64_t base_addr;
            bool found;
        };

        BaseAddrContext ctx = {0, false};

        dl_iterate_phdr(
            [](struct dl_phdr_info* info, size_t, void* data) -> int {
                BaseAddrContext* c = (BaseAddrContext*)data;
                // Главная программа имеет пустое имя.
                if (info->dlpi_name == nullptr || info->dlpi_name[0] == '\0') {
                    c->base_addr = info->dlpi_addr;
                    c->found = true;
                    return 1; // Остановить итерацию
                }
                return 0;
            },
            &ctx);

        return ctx.base_addr;
    }

    static uint64_t decode_uleb128(const uint8_t** ptr) {
        uint64_t result = 0;
        int shift = 0;
        uint8_t byte;
        do {
            byte = **ptr;
            (*ptr)++;
            result |= (uint64_t)(byte & 0x7f) << shift;
            shift += 7;
        } while (byte & 0x80);
        return result;
    }

    bool GetSection(std::string_view view, const uint8_t*& data, size_t& size) const {
        Elf64_Ehdr* ehdr = (Elf64_Ehdr*)mapped;
        Elf64_Shdr* shdr = (Elf64_Shdr*)((char*)mapped + ehdr->e_shoff);
        Elf64_Shdr* shstrtab = &shdr[ehdr->e_shstrndx];
        const char* shstrtab_data = (const char*)mapped + shstrtab->sh_offset;

        for (int i = 0; i < ehdr->e_shnum; i++) {
            const char* name = shstrtab_data + shdr[i].sh_name;
            if (!view.empty() && view.compare(name) == 0) {
                data = (const uint8_t*)mapped + shdr[i].sh_offset;
                size = shdr[i].sh_size;
                return true;
            }
        }
        return false;
    }
};

// Максимальный размер стека функций программы из секции .stack_sizes.
// Если задан include_functions (адреса функций), максимум считается ТОЛЬКО по перечисленным
// функциям; иначе - по всем функциям секции. Вызывается один раз при limit-проверке (кеш).
inline size_t trust::stack_check::get_stack_limit() {
    // Без фильтра (по всем функциям .stack_sizes) используем глобальный кеш, чтобы не парсить
    // ELF-файл на каждый поток: максимум по всем функциям не зависит от include_functions.
    if (include_functions.empty() && s_stack_limit_all_ready) {
        return s_stack_limit_all;
    }

    MappedELF elf;
    const uint8_t* data = nullptr;
    size_t sec_size = 0;
    if (!elf.GetSection(".stack_sizes", data, sec_size)) {
        throw std::runtime_error("Section '.stack_sizes' not found! Use the -fstack-size-section option when compiling.");
    }

    const uint64_t base = elf.get_base_address_dl();
    const bool filtered = !include_functions.empty();

    const uint8_t* ptr = data;
    const uint8_t* end = data + sec_size;
    uint64_t max_size = 0;
    while (ptr < end) {
        const uint64_t addr = *(uint64_t*)ptr;
        ptr += 8; // адрес функции (8 байт)
        const uint64_t curr = MappedELF::decode_uleb128(&ptr);
        if (filtered) {
            const void* rt_addr = reinterpret_cast<void*>(addr + base);
            bool in = false;
            for (void* inc : include_functions) {
                if (inc == rt_addr) {
                    in = true;
                    break;
                }
            }
            if (!in) {
                continue;
            }
        }
        if (curr > max_size) {
            max_size = curr;
        }
    }
    // В глобальный кеш сохраняем только результат "по всем функциям" (не зависит от фильтра).
    if (!filtered) {
        s_stack_limit_all = static_cast<size_t>(max_size);
        s_stack_limit_all_ready = true;
    }
    return static_cast<size_t>(max_size);
}

} // namespace trust

#else
#error "trust/stack_check.hpp: unsupported platform (Linux required)"
#endif // __linux__

#endif // TRUST_STACK_CHECK_HPP
