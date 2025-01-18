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

    struct bencodeList;
    using elementList = std::variant<std::string, size_t, std::unique_ptr<bencodeList>>;
    struct bencodeList{
        std::list<elementList> elements;
    };

    std::unique_ptr<bencodeList> makeBencodeList();
    std::pair<std::unique_ptr<bencodeList>, size_t> ParseListRec(const std::string& str);

    struct bencodeDict;
    using elementDict = std::variant<std::string, size_t, std::unique_ptr<bencodeList>, std::unique_ptr<bencodeDict>>;
    struct bencodeDict{
        std::map<std::string, elementDict> elements;
    };
    
    std::unique_ptr<bencodeDict> makeBencodeDict();
    std::pair<std::unique_ptr<bencodeDict>, size_t> ParseDictRec(const std::string& str);

}
