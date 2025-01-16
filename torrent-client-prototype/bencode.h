#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <variant>
#include <list>
#include <map>
#include <sstream>
#include <memory>

typedef std::variant<std::string, size_t> string_or_int;
namespace Bencode {
    /*
    * В это пространство имен рекомендуется вынести функции для работы с данными в формате bencode.
    * Этот формат используется в .torrent файлах и в протоколе общения с трекером
    */
    std::pair<std::string, size_t> ParseString(const std::string& str);

    std::pair<size_t, size_t> ParseInt(const std::string& str);

    std::pair<std::list<string_or_int>, size_t> ParseListInner(const std::string& str);

    std::pair<std::list<std::variant<std::string, size_t, std::list<string_or_int>>>, size_t> ParseList(const std::string& str);

    std::pair<std::map<std::string, std::variant<std::string, size_t, std::list<std::variant<std::string, size_t, std::list<string_or_int>>>>>, size_t> ParseDict(const std::string& str);

    struct bencodeList;
    using element = std::variant<std::string, size_t, std::unique_ptr<bencodeList>>;
    struct bencodeList{
        public:
        std::list<element> elements;
    };
    std::unique_ptr<bencodeList> makeBencodeList();
    std::pair<std::unique_ptr<bencodeList>, size_t> ParseListRec(const std::string& str);
}
