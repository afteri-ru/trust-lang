// naming_test.cpp — тестирование Ident (обработка идентификаторов по NAMING.md)
#include "ast/naming.hpp"
#include <gtest/gtest.h>
#include <filesystem>

namespace trust {

// ══════════════════════════════════════════════
// Простые имена
// ══════════════════════════════════════════════

TEST(IdentSimpleName, ValidSimpleNames) {
    EXPECT_TRUE(Ident::is_valid_simple_name("abc"));
    EXPECT_TRUE(Ident::is_valid_simple_name("_foo"));
    EXPECT_TRUE(Ident::is_valid_simple_name("a123"));
    EXPECT_TRUE(Ident::is_valid_simple_name("привет"));
    EXPECT_TRUE(Ident::is_valid_simple_name("x"));
    EXPECT_TRUE(Ident::is_valid_simple_name("_"));
    EXPECT_TRUE(Ident::is_valid_simple_name("A_B_C"));
    EXPECT_TRUE(Ident::is_valid_simple_name("a"));                  // 1 символ
    EXPECT_TRUE(Ident::is_valid_simple_name(std::string(64, 'a'))); // ровно 64
}

TEST(IdentSimpleName, InvalidSimpleNames) {
    EXPECT_FALSE(Ident::is_valid_simple_name(""));
    EXPECT_FALSE(Ident::is_valid_simple_name("123abc"));
    EXPECT_FALSE(Ident::is_valid_simple_name("a-b"));
    EXPECT_FALSE(Ident::is_valid_simple_name("a.b"));
    EXPECT_FALSE(Ident::is_valid_simple_name("a:b"));
    EXPECT_FALSE(Ident::is_valid_simple_name(std::string(65, 'a'))); // > 64
}

// ══════════════════════════════════════════════
// Имена модулей
// ══════════════════════════════════════════════

TEST(IdentModuleName, ValidModuleNames) {
    EXPECT_TRUE(Ident::is_valid_module_name("dir"));
    EXPECT_TRUE(Ident::is_valid_module_name("dir_file"));
    EXPECT_TRUE(Ident::is_valid_module_name("a"));
    EXPECT_TRUE(Ident::is_valid_module_name("a123"));
    EXPECT_TRUE(Ident::is_valid_module_name(std::string(64, 'a'))); // ровно 64
}

TEST(IdentModuleName, InvalidModuleNames) {
    EXPECT_FALSE(Ident::is_valid_module_name(""));
    EXPECT_FALSE(Ident::is_valid_module_name("_dir")); // начинается с _
    EXPECT_FALSE(Ident::is_valid_module_name("dir_")); // заканчивается на _
    EXPECT_FALSE(Ident::is_valid_module_name("Dir"));  // заглавная
    EXPECT_FALSE(Ident::is_valid_module_name("a-b"));
    EXPECT_FALSE(Ident::is_valid_module_name(std::string(65, 'a'))); // > 64
}

// ══════════════════════════════════════════════
// Тип имени (kind)
// ══════════════════════════════════════════════

TEST(IdentKind, SimpleNameHasSimpleKind) {
    EXPECT_TRUE(Ident("abc").is_simple());
    EXPECT_TRUE(Ident("_foo").is_simple());
    EXPECT_TRUE(Ident("a123").is_simple());
}

TEST(IdentKind, QualifiedNames) {
    EXPECT_TRUE(Ident("@macro").is_qualified());
    EXPECT_TRUE(Ident("$temp").is_qualified());
    EXPECT_TRUE(Ident("::ns").is_qualified());
    EXPECT_TRUE(Ident(".field").is_qualified());
    EXPECT_TRUE(Ident("\\module").is_qualified());
    EXPECT_TRUE(Ident(":Type").is_qualified());
    EXPECT_TRUE(Ident("%native").is_qualified());
}

TEST(IdentKind, SpecialNames) {
    EXPECT_TRUE(Ident("$0").is_special());
    EXPECT_TRUE(Ident("$$").is_special());
    EXPECT_TRUE(Ident("$*").is_special());
    EXPECT_TRUE(Ident("$^").is_special());
    EXPECT_TRUE(Ident("$1").is_special());
    EXPECT_TRUE(Ident("$42").is_special());
    EXPECT_TRUE(Ident("$999").is_special());
}

TEST(IdentKind, InternalNames) {
    EXPECT_TRUE(Ident("var$").is_internal());
    EXPECT_TRUE(Ident("var::").is_internal());
    EXPECT_TRUE(Ident("var%").is_internal());
    EXPECT_TRUE(Ident("::var::").is_internal());
    EXPECT_TRUE(Ident("type:::").is_internal());
    EXPECT_TRUE(Ident("1::var::").is_internal());
    EXPECT_TRUE(Ident("ns::func%").is_internal());
}

// ══════════════════════════════════════════════
// Квалификаторные признаки
// ══════════════════════════════════════════════

TEST(IdentQualifier, Macro) {
    EXPECT_TRUE(Ident("@macro").is_macro());
    EXPECT_TRUE(Ident("@::").is_macro());
    EXPECT_TRUE(Ident("@").is_macro());
    EXPECT_FALSE(Ident("macro").is_macro());
    EXPECT_FALSE(Ident("").is_macro());
}

TEST(IdentQualifier, Static) {
    EXPECT_TRUE(Ident("::obj").is_static());
    EXPECT_TRUE(Ident("ns::obj").is_static());
    EXPECT_TRUE(Ident("::").is_static());
    EXPECT_FALSE(Ident("obj").is_static());
    EXPECT_FALSE(Ident("").is_static());
}

TEST(IdentQualifier, Field) {
    EXPECT_TRUE(Ident(".field").is_field());
    EXPECT_TRUE(Ident(".").is_field());
    EXPECT_FALSE(Ident("field").is_field());
    EXPECT_FALSE(Ident("").is_field());
}

TEST(IdentQualifier, Module) {
    EXPECT_TRUE(Ident("\\module").is_module());
    EXPECT_TRUE(Ident("\\dir\\file").is_module());
    EXPECT_TRUE(Ident("\\\\absolute").is_absolute_module());
    EXPECT_TRUE(Ident("\\relative").is_relative_module());
    EXPECT_FALSE(Ident("\\").is_absolute_module()); // только один backslash
    EXPECT_FALSE(Ident("module").is_module());
}

TEST(IdentQualifier, Type) {
    EXPECT_TRUE(Ident(":Type").is_type());
    EXPECT_TRUE(Ident(":Тип").is_type());
    EXPECT_FALSE(Ident("::ns").is_type()); // :: — это namespace
    EXPECT_FALSE(Ident(":::").is_type());  // ::: — это internal type suffix
    EXPECT_FALSE(Ident("Type").is_type());
}

TEST(IdentQualifier, Native) {
    EXPECT_TRUE(Ident("%func").is_native());
    EXPECT_TRUE(Ident("%").is_native());
    EXPECT_FALSE(Ident("func").is_native());
    EXPECT_FALSE(Ident("").is_native());
}

TEST(IdentQualifier, Temp) {
    EXPECT_TRUE(Ident("$temp").is_temp());
    EXPECT_TRUE(Ident("$myvar").is_temp());
    EXPECT_FALSE(Ident("$0").is_temp()); // self
    EXPECT_FALSE(Ident("$$").is_temp()); // parent
    EXPECT_FALSE(Ident("$*").is_temp()); // args_dict
    EXPECT_FALSE(Ident("$^").is_temp()); // last_result
    EXPECT_FALSE(Ident("$1").is_temp()); // arg_ref
    EXPECT_FALSE(Ident("").is_temp());
}

// ══════════════════════════════════════════════
// Специальные имена
// ══════════════════════════════════════════════

TEST(IdentSpecial, Self) {
    EXPECT_TRUE(Ident("$0").is_self());
    EXPECT_FALSE(Ident("$1").is_self());
    EXPECT_FALSE(Ident("$00").is_self()); // не специальное имя
}

TEST(IdentSpecial, Parent) {
    EXPECT_TRUE(Ident("$$").is_parent());
    EXPECT_FALSE(Ident("$").is_parent());
    EXPECT_FALSE(Ident("$$$").is_parent()); // не специальное
}

TEST(IdentSpecial, ArgsDict) {
    EXPECT_TRUE(Ident("$*").is_args_dict());
    EXPECT_FALSE(Ident("$").is_args_dict());
}

TEST(IdentSpecial, LastResult) {
    EXPECT_TRUE(Ident("$^").is_last_result());
    EXPECT_FALSE(Ident("$").is_last_result());
}

TEST(IdentSpecial, ArgRef) {
    EXPECT_TRUE(Ident("$1").is_arg_ref());
    EXPECT_TRUE(Ident("$42").is_arg_ref());
    EXPECT_TRUE(Ident("$999").is_arg_ref());
    EXPECT_FALSE(Ident("$0").is_arg_ref()); // self
    EXPECT_FALSE(Ident("$").is_arg_ref());
    EXPECT_FALSE(Ident("$01").is_arg_ref()); // ведущий ноль — не число
    EXPECT_FALSE(Ident("$1a").is_arg_ref()); // не цифра после числа
}

// ══════════════════════════════════════════════
// bare_name
// ══════════════════════════════════════════════

TEST(IdentBareName, SimpleName) {
    EXPECT_EQ(Ident("abc").bare_name(), "abc");
    EXPECT_EQ(Ident("_foo").bare_name(), "_foo");
}

TEST(IdentBareName, QualifiedName) {
    EXPECT_EQ(Ident("@macro").bare_name(), "macro");
    EXPECT_EQ(Ident("$temp").bare_name(), "temp");
    EXPECT_EQ(Ident(".field").bare_name(), "field");
    EXPECT_EQ(Ident("\\module").bare_name(), "module");
    EXPECT_EQ(Ident(":Type").bare_name(), "Type");
    EXPECT_EQ(Ident("%native").bare_name(), "native");
    EXPECT_EQ(Ident("::ns").bare_name(), "ns");
}

TEST(IdentBareName, WithImmutable) {
    EXPECT_EQ(Ident("var^").bare_name(), "var");
    EXPECT_EQ(Ident("::name^").bare_name(), "name");
    EXPECT_EQ(Ident("$temp^").bare_name(), "temp");
}

TEST(IdentBareName, CombinedQualifiers) {
    EXPECT_EQ(Ident("@::obj").bare_name(), "obj");
    EXPECT_EQ(Ident("::%func").bare_name(), "func");
}

// ══════════════════════════════════════════════
// Иммутабельность
// ══════════════════════════════════════════════

TEST(IdentImmutable, HasImmutable) {
    EXPECT_TRUE(Ident("var^").has_immutable());
    EXPECT_TRUE(Ident("::name^").has_immutable());
    EXPECT_TRUE(Ident("^^").has_immutable());
    EXPECT_FALSE(Ident("var").has_immutable());
    EXPECT_FALSE(Ident("").has_immutable());
}

TEST(IdentImmutable, WithoutImmutable) {
    EXPECT_EQ(Ident("var^").without_immutable(), "var");
    EXPECT_EQ(Ident("::name^").without_immutable(), "::name");
    EXPECT_EQ(Ident("var^^").without_immutable(), "var"); // два ^
    EXPECT_EQ(Ident("var").without_immutable(), "var");
    EXPECT_EQ(Ident("").without_immutable(), "");
}

// ══════════════════════════════════════════════
// Нормализация
// ══════════════════════════════════════════════

TEST(IdentNormalized, IsNormalized) {
    // Нормализованные
    EXPECT_TRUE(Ident("abc").is_normalized());
    EXPECT_TRUE(Ident("foo123").is_normalized());
    EXPECT_TRUE(Ident("::var").is_normalized());   // static — нормализован
    EXPECT_TRUE(Ident("%native").is_normalized()); // native — нормализован
    EXPECT_TRUE(Ident("var::").is_normalized());   // внутреннее — нормализовано
    EXPECT_TRUE(Ident("var$").is_normalized());    // внутреннее — нормализовано
    EXPECT_TRUE(Ident("type:::").is_normalized()); // внутреннее тип — нормализовано

    // Ненормализованные
    EXPECT_FALSE(Ident("abc^").is_normalized());   // содержит ^
    EXPECT_FALSE(Ident("@macro").is_normalized()); // макрос
    EXPECT_FALSE(Ident(".field").is_normalized()); // field
    EXPECT_FALSE(Ident("$temp").is_normalized());  // временное имя — не нормализовано
    EXPECT_FALSE(Ident("$0").is_normalized());     // self — специальное имя
    EXPECT_FALSE(Ident("$$").is_normalized());     // parent — специальное имя
    EXPECT_FALSE(Ident("$*").is_normalized());     // args_dict — специальное
    EXPECT_FALSE(Ident("$^").is_normalized());     // last_result — специальное
    EXPECT_FALSE(Ident("$1").is_normalized());     // arg_ref — специальное
}

TEST(IdentNormalized, Normalize) {
    EXPECT_EQ(Ident("var^").normalized(), "var");
    EXPECT_EQ(Ident(".field").normalized(), "field");
    EXPECT_EQ(Ident(".field^").normalized(), "field");
    EXPECT_EQ(Ident("var").normalized(), "var");
    EXPECT_EQ(Ident("@macro").normalized(), "@macro");  // макрос не раскрывается
    EXPECT_EQ(Ident("::name^").normalized(), "::name"); // с ^ и ::
    EXPECT_EQ(Ident("::name").normalized(), "::name");
}

// ══════════════════════════════════════════════
// Внутреннее имя (to_internal)
// ══════════════════════════════════════════════

TEST(IdentInternal, SimpleName) {
    // Обычное имя → name::
    EXPECT_EQ(Ident("var").to_internal(), "var::");
}

TEST(IdentInternal, TempVariable) {
    // $var → var$
    EXPECT_EQ(Ident("$var").to_internal(), "var$");
}

TEST(IdentInternal, StaticGlobal) {
    // ::var → ::var::
    EXPECT_EQ(Ident("::var").to_internal(), "::var::");
}

TEST(IdentInternal, StaticModule) {
    // @::var → var:: (bare name без ::_)
    // Примечание: @:: — это статический объект в текущей области
    EXPECT_EQ(Ident("@::var").to_internal(), "var::");
}

TEST(IdentInternal, NativeVariable) {
    // %var → var%
    EXPECT_EQ(Ident("%var").to_internal(), "var%");
}

TEST(IdentInternal, Type) {
    // :type → type:::
    EXPECT_EQ(Ident(":type").to_internal(), "type:::");
}

TEST(IdentInternal, GlobalType) {
    // ::type → ::type::: (глобальный тип)
    // Примечание: to_internal проверяет has_global первым, но :type не имеет ::
    // поэтому :type перехватывается has_type раньше has_static
    Ident id("::mytype");
    // Имеет :: в начале → has_global = true
    EXPECT_TRUE(id.is_static());
    // bare_name = "mytype"
    EXPECT_EQ(id.to_internal(), "::mytype::");
}

TEST(IdentInternal, NamespacedStatic) {
    // ns::var → ns::var::
    EXPECT_EQ(Ident("ns::var").to_internal(), "ns::var::");
}

TEST(IdentInternal, NamespacedNative) {
    // ns::%func → ns::func%
    // Примечание: bare_name() вернёт "%func" т.к. ведущий :: пропущен, но % остался
    // Поэтому to_internal видит is_native() и "func" в bare_name
    Ident id("ns::%func");
    EXPECT_EQ(id.to_internal(), "ns::func%");
}

TEST(IdentInternal, GlobalNative) {
    // ::%func → ::func%
    EXPECT_EQ(Ident("::%func").to_internal(), "::func%");
}

TEST(IdentInternal, NumericPrefixInBlock) {
    // В блоке кода: 1::var:: (числовой префикс)
    // Для to_internal числовой префикс не генерируется (это задача AST)
    // Но внутреннее имя может начинаться с цифры
    Ident id("1::var::");
    EXPECT_TRUE(id.is_internal());
}

TEST(IdentInternal, StaticWithTrailingColon) {
    // var:: (статический объект модуля без глобальной видимости) → var::
    EXPECT_EQ(Ident("var::").to_internal(), "var::");
}

// ══════════════════════════════════════════════
// to_internal с иммутабельностью (NAMING.md п.77)
// ══════════════════════════════════════════════

TEST(IdentInternal, WithImmutable) {
    // var^ → var:: (^ не входит во внутреннее имя)
    EXPECT_EQ(Ident("var^").to_internal(), "var::");
    // ::name^ → ::name::
    EXPECT_EQ(Ident("::name^").to_internal(), "::name::");
    // $temp^ → temp$
    EXPECT_EQ(Ident("$temp^").to_internal(), "temp$");
    // %func^ → func%
    EXPECT_EQ(Ident("%func^").to_internal(), "func%");
    // :type^ → type:::
    EXPECT_EQ(Ident(":type^").to_internal(), "type:::");
}

// ══════════════════════════════════════════════
// to_internal — статический модуль с макросом и нативной функцией
// ══════════════════════════════════════════════

TEST(IdentInternal, StaticModuleNative) {
    // @::%func → func:: (текущее поведение: bare_name + ::)
    EXPECT_EQ(Ident("@::%func").to_internal(), "func::");
    // @:::Type → Type:: (текущее поведение: bare_name + ::)
    EXPECT_EQ(Ident("@:::Type").to_internal(), "Type::");
}

// ══════════════════════════════════════════════
// to_internal — невалидные комбинации квалификаторов
// ══════════════════════════════════════════════

TEST(IdentInternal, InvalidQualifierCombos) {
    // @::$var — static + temp — взаимоисключающие квалификаторы
    // to_internal вернёт что-то, но это невалидная комбинация
    // Проверяем, что код не падает
    EXPECT_FALSE(Ident("@::$var").to_internal().empty());

    // @::.field — static + field — взаимоисключающие квалификаторы
    EXPECT_FALSE(Ident("@::.field").to_internal().empty());

    // :::$type — :: + $ → статический и локальный — взаимоисключающие
    Ident invalid1(":::$type");
    EXPECT_FALSE(invalid1.empty());
}

// ══════════════════════════════════════════════
// parts() — разбивка по ::
// ══════════════════════════════════════════════

TEST(IdentParts, SimpleNameNoSeparator) {
    auto p = Ident("abc").parts();
    ASSERT_EQ(p.size(), 1);
    EXPECT_EQ(p[0], "abc");
}

TEST(IdentParts, Namespaced) {
    auto p = Ident("ns::var").parts();
    ASSERT_EQ(p.size(), 2);
    EXPECT_EQ(p[0], "ns");
    EXPECT_EQ(p[1], "var");
}

TEST(IdentParts, GlobalNamespace) {
    auto p = Ident("::ns::var").parts();
    ASSERT_EQ(p.size(), 3);
    EXPECT_EQ(p[0], "::");
    EXPECT_EQ(p[1], "ns");
    EXPECT_EQ(p[2], "var");
}

TEST(IdentParts, TypeSuffix) {
    // ::: — это не разделитель ::, а часть type:::
    auto p = Ident("type:::").parts();
    ASSERT_EQ(p.size(), 1);
    EXPECT_EQ(p[0], "type:::");
}

TEST(IdentParts, SimpleDoubleColon) {
    auto p = Ident("::").parts();
    ASSERT_EQ(p.size(), 1);
    EXPECT_EQ(p[0], "::");
}

TEST(IdentParts, TrailingDoubleColon) {
    // var:: — trailing :: после имени, пустой фрагмент не добавляется
    auto p = Ident("var::").parts();
    ASSERT_EQ(p.size(), 1);
    EXPECT_EQ(p[0], "var");
}

TEST(IdentParts, NamespaceWithTrailingDoubleColon) {
    // ns::var:: — trailing :: после var
    auto p = Ident("ns::var::").parts();
    ASSERT_EQ(p.size(), 2);
    EXPECT_EQ(p[0], "ns");
    EXPECT_EQ(p[1], "var");
}

TEST(IdentParts, GlobalWithTrailingDoubleColon) {
    // ::ns::var:: — глобальный префикс с trailing ::
    auto p = Ident("::ns::var::").parts();
    ASSERT_EQ(p.size(), 3);
    EXPECT_EQ(p[0], "::");
    EXPECT_EQ(p[1], "ns");
    EXPECT_EQ(p[2], "var");
}

TEST(IdentParts, Empty) {
    auto p = Ident("").parts();
    EXPECT_TRUE(p.empty());
}

// ══════════════════════════════════════════════
// Манглинг / деманглинг
// ══════════════════════════════════════════════

TEST(IdentMangle, MainModule) {
    // var:: → _$$_var$$
    EXPECT_EQ(Ident("var::").mangle(""), "_$$_var$$");
}

TEST(IdentMangle, NamedModule) {
    // var:: → _$dir$file$_var$$  (\ → $)
    EXPECT_EQ(Ident("var::").mangle("\\dir\\file"), "_$dir$file$_var$$");
}

TEST(IdentMangle, TempInMainModule) {
    // var$ → _$$_var$
    EXPECT_EQ(Ident("var$").mangle(""), "_$$_var$");
}

TEST(IdentMangle, ModuleWithBackslash) {
    // var:: → _$dir$file$_var$$  (проверка что \ → $)
    EXPECT_EQ(Ident("var::").mangle("\\dir"), "_$dir$_var$$");
}

TEST(IdentMangle, ModuleWithBackslashNested) {
    // var:: → _$dir$sub$file$_var$$
    EXPECT_EQ(Ident("var::").mangle("\\dir\\sub\\file"), "_$dir$sub$file$_var$$");
}

TEST(IdentMangle, TypeInNamedModule) {
    // type::: → _$dir$_type$$$
    EXPECT_EQ(Ident("type:::").mangle("\\dir"), "_$dir$_type$$$");
}

TEST(IdentMangle, GlobalStatic) {
    // ::var:: → _$$_$$var$$
    EXPECT_EQ(Ident("::var::").mangle(""), "_$$_$$var$$");
}

TEST(IdentMangle, NativeFunction) {
    // func% → _$$_func%
    // Примечание: % не заменяется при манглинге, только : и backslash
    EXPECT_EQ(Ident("func%").mangle(""), "_$$_func%");
}

TEST(IdentDemangle, MainModule) {
    // _$$_var$$ → var::
    EXPECT_EQ(Ident::demangle("_$$_var$$"), "var::");
}

TEST(IdentDemangle, NamedModule) {
    // _$dir_file$_var$$ → var:: (обратная совместимость с _)
    EXPECT_EQ(Ident::demangle("_$dir_file$_var$$"), "var::");
    // _$dir$file$_var$$ → var:: (новый формат с $)
    EXPECT_EQ(Ident::demangle("_$dir$file$_var$$"), "var::");
}

TEST(IdentDemangle, Type) {
    // _$dir$_type$$$ → type:::
    EXPECT_EQ(Ident::demangle("_$dir$_type$$$"), "type:::");
}

TEST(IdentDemangle, GlobalStatic) {
    EXPECT_EQ(Ident::demangle("_$$_$$var$$"), "::var::");
}

TEST(IdentDemangle, NoPrefix) {
    // Без префикса — возвращаем как есть
    EXPECT_EQ(Ident::demangle("var"), "var");
}

// ══════════════════════════════════════════════
// Манглинг / Деманглинг — полный roundtrip
// ══════════════════════════════════════════════

TEST(IdentDemangleMangleRoundtrip, Simple) {
    Ident original("var::");
    Ident mangled = original.mangle("\\dir");
    Ident demangled = Ident::demangle(mangled);
    EXPECT_EQ(demangled, original);
}

TEST(IdentDemangleMangleRoundtrip, Type) {
    Ident original("type:::");
    Ident mangled = original.mangle("");
    Ident demangled = Ident::demangle(mangled);
    EXPECT_EQ(demangled, original);
}

TEST(IdentDemangleMangleRoundtrip, Global) {
    Ident original("::var::");
    Ident mangled = original.mangle("");
    Ident demangled = Ident::demangle(mangled);
    EXPECT_EQ(demangled, original);
}

TEST(IdentDemangleMangleRoundtrip, Native) {
    Ident original("func%");
    Ident mangled = original.mangle("\\mod");
    Ident demangled = Ident::demangle(mangled);
    EXPECT_EQ(demangled, original);
}

TEST(IdentDemangleMangleRoundtrip, NamespaceStatic) {
    // ns::var:: — статический объект в namespace
    Ident original("ns::var::");
    Ident mangled = original.mangle("\\mod");
    Ident demangled = Ident::demangle(mangled);
    EXPECT_EQ(demangled, original);
}

// ══════════════════════════════════════════════
// Edge cases
// ══════════════════════════════════════════════

TEST(IdentEdgeCase, EmptyString) {
    Ident e("");
    EXPECT_TRUE(e.empty());
    EXPECT_FALSE(e.is_simple()); // пустая строка — не простое имя
    EXPECT_FALSE(e.is_qualified());
    EXPECT_FALSE(e.is_special());
    EXPECT_FALSE(e.is_internal());
    EXPECT_FALSE(e.is_macro());
    EXPECT_FALSE(e.is_static());
    EXPECT_FALSE(e.is_field());
    EXPECT_FALSE(e.is_module());
    EXPECT_FALSE(e.is_type());
    EXPECT_FALSE(e.is_native());
    EXPECT_FALSE(e.has_immutable());
    EXPECT_EQ(e.bare_name(), "");
    EXPECT_TRUE(e.is_normalized());
    EXPECT_EQ(e.to_internal(), "::"); // пустое имя → ::
}

TEST(IdentEdgeCase, SingleQualifierOnly) {
    EXPECT_TRUE(Ident("$").is_temp());    // только $ — временное
    EXPECT_TRUE(Ident("@").is_macro());   // только @ — макрос
    EXPECT_TRUE(Ident("%").is_native());  // только % — нативное
    EXPECT_TRUE(Ident("\\").is_module()); // только \ — модуль (относительный)
    EXPECT_TRUE(Ident(".").is_field());   // только . — поле
    EXPECT_TRUE(Ident(":").is_type());    // только : — тип
    EXPECT_TRUE(Ident("::").is_static()); // :: — статическое (но само по себе)
}

TEST(IdentEdgeCase, UnicodeName) {
    EXPECT_TRUE(Ident::is_valid_simple_name("привет"));
    EXPECT_TRUE(Ident::is_valid_simple_name("名前"));
    EXPECT_TRUE(Ident::is_valid_simple_name("κόσμος"));
    Ident id("привет");
    EXPECT_TRUE(id.is_simple());
    EXPECT_EQ(id.bare_name(), "привет");
}

// ══════════════════════════════════════════════
// Негативные тесты для is_simple
// ══════════════════════════════════════════════

TEST(IdentKind, SimpleNegativeForSpecial) {
    EXPECT_FALSE(Ident("$0").is_simple()); // self
    EXPECT_FALSE(Ident("$$").is_simple()); // parent
    EXPECT_FALSE(Ident("$*").is_simple()); // args_dict
    EXPECT_FALSE(Ident("$^").is_simple()); // last_result
    EXPECT_FALSE(Ident("$1").is_simple()); // arg_ref
}

TEST(IdentKind, SimpleNegativeForQualified) {
    EXPECT_FALSE(Ident("@macro").is_simple()); // макрос
    EXPECT_FALSE(Ident("$temp").is_simple());  // временное
    EXPECT_FALSE(Ident(":Type").is_simple());  // тип
    EXPECT_FALSE(Ident(".field").is_simple()); // field
    EXPECT_FALSE(Ident("\\mod").is_simple());  // модуль
    EXPECT_FALSE(Ident("%nat").is_simple());   // нативная
}

TEST(IdentKind, SimpleNegativeForInternal) {
    EXPECT_FALSE(Ident("var$").is_simple());    // внутреннее временное
    EXPECT_FALSE(Ident("var::").is_simple());   // внутреннее статическое
    EXPECT_FALSE(Ident("var%").is_simple());    // внутреннее нативное
    EXPECT_FALSE(Ident("type:::").is_simple()); // внутреннее тип
}

// ══════════════════════════════════════════════
// Негативные тесты для is_qualified
// ══════════════════════════════════════════════

TEST(IdentKind, QualifiedNegativeForSpecial) {
    EXPECT_FALSE(Ident("$0").is_qualified()); // self
    EXPECT_FALSE(Ident("$$").is_qualified()); // parent
    EXPECT_FALSE(Ident("$*").is_qualified()); // args_dict
    EXPECT_FALSE(Ident("$^").is_qualified()); // last_result
}

TEST(IdentKind, QualifiedNegativeForSimple) {
    EXPECT_FALSE(Ident("abc").is_qualified());
    EXPECT_FALSE(Ident("_foo").is_qualified());
    EXPECT_FALSE(Ident("a123").is_qualified());
}

// Внутренние имена, содержащие ::, являются квалифицированными (is_static → true)
TEST(IdentKind, QualifiedForInternal) {
    EXPECT_TRUE(Ident("var::").is_qualified());     // внутреннее с :: — квалифицированное
    EXPECT_TRUE(Ident("ns::var::").is_qualified()); // namespace + static — квалифицированное
}

TEST(IdentKind, QualifiedNegativeForInternalNoColon) {
    EXPECT_FALSE(Ident("var$").is_qualified()); // внутреннее без :: — не квалифицированное
    EXPECT_FALSE(Ident("var%").is_qualified()); // внутреннее без :: — не квалифицированное
}

// ══════════════════════════════════════════════
// Дополнительные тесты для is_internal
// ══════════════════════════════════════════════

TEST(IdentKind, InternalExtra) {
    EXPECT_FALSE(Ident("").is_internal());
    EXPECT_FALSE(Ident("var").is_internal());
    EXPECT_FALSE(Ident("var^").is_internal());       // ^ не делает имя внутренним
    EXPECT_FALSE(Ident("$var").is_internal());       // $var — квалифицированное, не внутреннее
    EXPECT_TRUE(Ident("ns::var::").is_internal());   // namespace + static
    EXPECT_TRUE(Ident("::ns::func%").is_internal()); // global + native
    EXPECT_FALSE(Ident("var^^").is_internal());      // ^^ — не внутреннее
}

// ══════════════════════════════════════════════
// bare_name тесты — текущее поведение реализации
// ══════════════════════════════════════════════

TEST(IdentBareName, InternalNames) {
    // bare_name() не удаляет trailing ::, $, % и ::: — это ожидаемое текущее поведение
    EXPECT_EQ(Ident("var::").bare_name(), "var::");         // internal static
    EXPECT_EQ(Ident("var$").bare_name(), "var$");           // internal temp
    EXPECT_EQ(Ident("var%").bare_name(), "var%");           // internal native
    EXPECT_EQ(Ident("type:::").bare_name(), "type:::");     // internal type
    EXPECT_EQ(Ident("::var::").bare_name(), "var::");       // global static
    EXPECT_EQ(Ident("ns::func%").bare_name(), "ns::func%"); // namespace + native — ::func% после пропуска ведущих квалификаторов
}

TEST(IdentBareName, SpecialNames) {
    // bare_name() сначала убирает ^ в конце, затем пропускает квалификаторы.
    // $0 → убирает $, остаётся "0"
    EXPECT_EQ(Ident("$0").bare_name(), "0");
    // $$ → оба $ являются квалификаторами, bare_name = "" (пусто)
    EXPECT_EQ(Ident("$$").bare_name(), "");
    // $* → убирает $, остаётся "*"
    EXPECT_EQ(Ident("$*").bare_name(), "*");
    // $^ → сначала убирает ^ в конце (end-1 == ^), остаётся $, затем $ убирается как квалификатор → ""
    EXPECT_EQ(Ident("$^").bare_name(), "");
    // $1 → убирает $, остаётся "1"
    EXPECT_EQ(Ident("$1").bare_name(), "1");
}

// ══════════════════════════════════════════════
// module_name_to_path — относительные модули
// ══════════════════════════════════════════════

TEST(IdentModuleNameToPath, RelativeModule) {
    auto p = Ident::module_name_to_path("\\dir\\mod", "/base", "/sys");
    EXPECT_EQ(p, std::filesystem::path("/base/dir/mod"));
}

TEST(IdentModuleNameToPath, RelativeSimple) {
    auto p = Ident::module_name_to_path("\\mod", "/base", "/sys");
    EXPECT_EQ(p, std::filesystem::path("/base/mod"));
}

TEST(IdentModuleNameToPath, RelativeDeep) {
    auto p = Ident::module_name_to_path("\\a\\b\\c\\d", "/root", "/sys");
    EXPECT_EQ(p, std::filesystem::path("/root/a/b/c/d"));
}

TEST(IdentModuleNameToPath, RelativeUnderscore) {
    auto p = Ident::module_name_to_path("\\my_module\\sub_mod", "/base", "/sys");
    EXPECT_EQ(p, std::filesystem::path("/base/my_module/sub_mod"));
}

TEST(IdentModuleNameToPath, RelativeOnlySlash) {
    auto p = Ident::module_name_to_path("\\", "/base", "/sys");
    EXPECT_EQ(p, std::filesystem::absolute("/base"));
}

TEST(IdentModuleNameToPath, RelativeEmpty) {
    auto p = Ident::module_name_to_path("", "/base", "/sys");
    EXPECT_TRUE(p.empty());
}

// ══════════════════════════════════════════════
// module_name_to_path — абсолютные модули
// ══════════════════════════════════════════════

TEST(IdentModuleNameToPath, AbsoluteModule) {
    auto p = Ident::module_name_to_path("\\\\abs\\mod", "/base", "/sys");
    EXPECT_EQ(p, std::filesystem::path("/abs/mod"));
}

TEST(IdentModuleNameToPath, AbsoluteDeep) {
    auto p = Ident::module_name_to_path("\\\\root\\a\\b\\c", "/base", "/sys");
    EXPECT_EQ(p, std::filesystem::path("/root/a/b/c"));
}

TEST(IdentModuleNameToPath, AbsoluteOnlyTwoSlashes) {
    auto p = Ident::module_name_to_path("\\\\", "/base", "/sys");
    EXPECT_EQ(p, std::filesystem::path("/"));
}

// ══════════════════════════════════════════════
// module_name_to_path — системные модули (\\\)
// ══════════════════════════════════════════════

TEST(IdentModuleNameToPath, SysModule) {
    auto p = Ident::module_name_to_path("\\\\\\sys\\mod", "/base", "/opt/trust");
    EXPECT_EQ(p, std::filesystem::path("/opt/trust/sys/mod"));
}

TEST(IdentModuleNameToPath, SysModuleDefault) {
    // sys_dir по умолчанию "/"
    auto p = Ident::module_name_to_path("\\\\\\sys\\mod", "/base");
    EXPECT_EQ(p, std::filesystem::path("/sys/mod"));
}

TEST(IdentModuleNameToPath, SysModuleOnlyThreeSlashes) {
    auto p = Ident::module_name_to_path("\\\\\\", "/base", "/sys");
    EXPECT_EQ(p, std::filesystem::absolute("/sys"));
}

// ══════════════════════════════════════════════
// path_to_module_name — относительные пути
// ══════════════════════════════════════════════

TEST(IdentPathToModuleName, RelativeInside) {
    Ident m = Ident::path_to_module_name("/base/dir/mod", "/base");
    EXPECT_EQ(m, "\\dir\\mod");
}

TEST(IdentPathToModuleName, RelativeSimple) {
    Ident m = Ident::path_to_module_name("/base/mod", "/base");
    EXPECT_EQ(m, "\\mod");
}

TEST(IdentPathToModuleName, RelativeDeep) {
    Ident m = Ident::path_to_module_name("/root/a/b/c", "/root");
    EXPECT_EQ(m, "\\a\\b\\c");
}

TEST(IdentPathToModuleName, RelativeUnderscore) {
    Ident m = Ident::path_to_module_name("/base/my_module/sub_mod", "/base");
    EXPECT_EQ(m, "\\my_module\\sub_mod");
}

// ══════════════════════════════════════════════
// path_to_module_name — абсолютные пути (вне base_dir)
// ══════════════════════════════════════════════

TEST(IdentPathToModuleName, AbsoluteOutside) {
    Ident m = Ident::path_to_module_name("/other/mod", "/base");
    EXPECT_EQ(m, "\\\\other\\mod");
}

TEST(IdentPathToModuleName, AbsoluteOutsideDeep) {
    Ident m = Ident::path_to_module_name("/outside/a/b/c", "/base");
    EXPECT_EQ(m, "\\\\outside\\a\\b\\c");
}

TEST(IdentPathToModuleName, AbsoluteOutsideNoBase) {
    // Если base_dir = "/", то /other/mod это внутри? нет, /other/mod находится внутри /
    // Тогда lexically_relative от "/" вернёт "other/mod"
    Ident m = Ident::path_to_module_name("/other/mod", "/");
    EXPECT_EQ(m, "\\other\\mod");
}

TEST(IdentPathToModuleName, EmptyPath) {
    Ident m = Ident::path_to_module_name("", "/base");
    EXPECT_TRUE(m.empty());
}

// ══════════════════════════════════════════════
// Roundtrip — module_name_to_path затем path_to_module_name
// ══════════════════════════════════════════════

TEST(IdentModuleRoundtrip, Relative) {
    auto p = Ident::module_name_to_path("\\a\\b\\c", "/root", "/sys");
    Ident m = Ident::path_to_module_name(p, "/root");
    EXPECT_EQ(m, "\\a\\b\\c");
}

TEST(IdentModuleRoundtrip, Absolute) {
    auto p = Ident::module_name_to_path("\\\\x\\y\\z", "/root", "/sys");
    Ident m = Ident::path_to_module_name(p, "/root");
    EXPECT_EQ(m, "\\\\x\\y\\z");
}

TEST(IdentModuleRoundtrip, RelativeSimple) {
    auto p = Ident::module_name_to_path("\\module", "/base", "/sys");
    Ident m = Ident::path_to_module_name(p, "/base");
    EXPECT_EQ(m, "\\module");
}

// ══════════════════════════════════════════════
// Противоречия и замечания к NAMING.md
// ══════════════════════════════════════════════
//
// 1. NAMING.md говорит: "последний символ должен быть "$" или ":"
//    — НО не упоминает "%". Исправлено: добавлен "%".
//
// 2. NAMING.md не описывает числовой префикс для блоков (например, 1::var::)
//    — этот префикс генерируется при анализе AST, не в Ident.
//
// 3. В NAMING.md пример: `@::var ::= 0;   # var::`
//    — to_internal() для "@::var" возвращает "var::", что соответствует.
//
// 4. В NAMING.md пример: `::%func() := {};   # ::func%`
//    — to_internal() для "::%func" возвращает "::func%", что соответствует.
//
// 5. Символ иммутабельности ^ не входит во внутреннее имя (NAMING.md п.77).
//    — to_internal() теперь корректно удаляет ^ через without_immutable().
//
// 6. bare_name() не удаляет trailing ::, $, %, ::: — это особенность текущей
//    реализации. Для внутренних имён bare_name() следует использовать с учётом
//    этого ограничения.

} // namespace trust