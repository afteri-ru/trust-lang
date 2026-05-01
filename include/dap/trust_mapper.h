#ifndef TRUST_MAPPER_H
#define TRUST_MAPPER_H

#include <string>
#include <vector>
#include <map>
#include <utility>

struct TrustMappingEntry {
    int trust_line;
    int cpp_line;
    std::vector<std::string> trust_vars;
    std::vector<std::string> cpp_vars;
};

struct TrustMapEntry {
    std::string trust_file;   // relative path
    std::string cpp_file;     // relative path
    std::vector<TrustMappingEntry> mappings;
};

class TrustMapper {
public:
    TrustMapper();
    
    // Загрузка из ELF секции .trust_map (парсинг бинарника)
    bool loadFromElfSection(const std::string& binaryPath);
    
    // Загрузка из внешнего JSON файла
    bool loadFromJsonFile(const std::string& mapPath);
    
    // Автозагрузка: сначала ELF, потом JSON
    void load(const std::string& binaryPath, const std::string& mapPath);
    
    // Маппинг Trust -> CPP (возвращает {cpp_file, cpp_line})
    std::pair<std::string, int> trustToCpp(const std::string& trustFile, int trustLine);
    
    // Маппинг CPP -> Trust (возвращает {trust_file, trust_line})
    std::pair<std::string, int> cppToTrust(const std::string& cppFile, int cppLine);
    
    // Маппинг переменной: cpp_var -> trust_var
    std::vector<std::string> getTrustVars(const std::string& cppFile, int cppLine, 
                                          const std::vector<std::string>& cppVars);
    
    // Получить все записи
    const std::vector<TrustMapEntry>& getEntries() const { return entries_; }

private:
    // Парсинг JSON строки (простой ручной парсер без внешних библиотек)
    bool parseJson(const std::string& json);
    
    std::vector<TrustMapEntry> entries_;
};

#endif // TRUST_MAPPER_H