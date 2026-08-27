#include "formatter/format_test_util.hpp"

namespace trust::formatter {
TEST(FormatterTest, AttributeBeforeIdentifierWraps) {
    const std::string in = "@[link(\"stdc++\")]@ fabs(x:Int32):Int32 := %fabs...;\n";
    const std::string exp = "@[ link(\"stdc++\") ]@\nfabs(x:Int32):Int32 := %fabs ...;\n";
    EXPECT_EQ(fmt(in), exp);
    EXPECT_EQ(fmt(fmt(in)), exp);
}

TEST(FormatterTest, AttributeBeforeKeywordWraps) {
    const std::string in = "@[pure]@ func add(a,b):Int { @return a + b; }\n";
    const std::string exp = "@[ pure ]@\nfunc add(a, b):Int {\n    @return a + b;\n}\n";
    EXPECT_EQ(fmt(in), exp);
    EXPECT_EQ(fmt(fmt(in)), exp);
}

TEST(FormatterTest, InlineTypeAttributeStaysInline) {
    // Атрибут типа внутри сигнатуры (в скобках) не переносится на новую строку.
    const std::string in = "@[format(\"printf\",1,2)]@ %printf(fmt:@[reftype(ptr)]@ StrChar^,...):Int32 := ...;\n";
    const std::string exp = "@[ format(\"printf\", 1, 2) ]@\n"
                            "%printf(fmt:@[ reftype(ptr) ]@ StrChar^, ...):Int32 := ...;\n";
    EXPECT_EQ(fmt(in), exp);
    EXPECT_EQ(fmt(fmt(in)), exp);
}

TEST(FormatterTest, ContractAfterSignatureIndented) {
    const std::string in = "sum(n:Int32):Int32 trust_pre( n >= 0 ) trust_post( sum >= 0 ) := { s := 0; @return s; };\n";
    const std::string exp = "sum(n:Int32):Int32\n"
                            "    trust_pre(n >= 0)\n"
                            "    trust_post(sum >= 0) := {\n"
                            "    s := 0;\n"
                            "    @return s;\n"
                            "};\n";
    EXPECT_EQ(fmt(in), exp);
    EXPECT_EQ(fmt(fmt(in)), exp);
}

TEST(FormatterTest, ContractAfterVarIndented) {
    const std::string in = "y trust_assert( y > 0 ) := 42;\n";
    const std::string exp = "y\n    trust_assert(y > 0) := 42;\n";
    EXPECT_EQ(fmt(in), exp);
    EXPECT_EQ(fmt(fmt(in)), exp);
}

TEST(FormatterTest, RawInvariantColonSpacing) {
    const std::string in = "@main() := { s := 0; @{ invariant: s >= 0 @}; @while(i<n){ s:=s+1; i:=i+1; }; }\n";
    const std::string exp = "@main() := {\n"
                            "    s := 0;\n"
                            "    @{ invariant: s >= 0 @};\n"
                            "    @while(i < n) {\n"
                            "        s := s + 1;\n"
                            "        i := i + 1;\n"
                            "    };\n"
                            "}\n";
    EXPECT_EQ(fmt(in), exp);
    EXPECT_EQ(fmt(fmt(in)), exp);
}

TEST(FormatterTest, AtPrefixedContractFormats) {
    // Контракты trust_pre/trust_post с '@'-сигилом (зарегистрированы в DSL) форматируются.
    const std::string in = "sum(n:Int32):Int32 @trust_pre( n >= 0 ) @trust_post( sum >= 0 ) := { s := 0; @return s; };\n";
    const std::string exp = "sum(n:Int32):Int32\n"
                            "    @trust_pre(n >= 0)\n"
                            "    @trust_post(sum >= 0) := {\n"
                            "    s := 0;\n"
                            "    @return s;\n"
                            "};\n";
    EXPECT_EQ(fmt(in), exp);
    EXPECT_EQ(fmt(fmt(in)), exp);
}

TEST(FormatterTest, FuncMacroContractNoAssignIndent) {
    // @func съедает '{' и генерирует ':=', поэтому в исходнике нет токена ASSIGN; contractIndent_
    // должен сбрасываться на '{', иначе тело функции получает лишний уровень отступа (8 вместо 4),
    // что ломает format-check examples/contracts.src. Это ловушка "макрос func съедает '{'".
    const std::string in = "@func sum(n:Int32):Int32 @trust_pre( n >= 0 ) @trust_post( sum >= 0 ) { s := 0; @return s; };";
    const std::string exp = "@func sum(n:Int32):Int32\n"
                            "    @trust_pre(n >= 0)\n"
                            "    @trust_post(sum >= 0) {\n"
                            "    s := 0;\n"
                            "    @return s;\n"
                            "};\n";
    EXPECT_EQ(fmt(in), exp);
    EXPECT_EQ(fmt(fmt(in)), exp);
}

TEST(FormatterTest, SourceDefinedContractMacroFormats) {
    // Макрос-контракт, определённый в самом файле (тело = вызов trust_contract), распознаётся.
    const std::string in = "@@ my_inv( $x ) @@ trust_contract(invariant, @$x) @@@@;\n"
                           "sum(n:Int32):Int32 my_inv( n >= 0 ) := { s := 0; @return s; };\n";
    const std::string exp = "@@ my_inv($x) @@ trust_contract(invariant, @$x) @@@@;\n"
                            "sum(n:Int32):Int32\n"
                            "    my_inv(n >= 0) := {\n"
                            "    s := 0;\n"
                            "    @return s;\n"
                            "};\n";
    EXPECT_EQ(fmt(in), exp);
    EXPECT_EQ(fmt(fmt(in)), exp);
}

TEST(FormatterTest, SourceDefinedNoParenMacroSpaced) {
    // No-paren макрос, определённый в самом файле, форматируется с пробелом после имени.
    const std::string in = "@@ dup $x @@ ( @$x * 2 ) @@@@;\n@main() := { @dup 5; @return @dup 5; }\n";
    const std::string exp = "@@ dup $x @@(@$x * 2) @@@@;\n"
                            "@main() := {\n"
                            "    @dup 5;\n"
                            "    @return @dup 5;\n"
                            "}\n";
    EXPECT_EQ(fmt(in), exp);
    EXPECT_EQ(fmt(fmt(in)), exp);
}

} // namespace trust::formatter
