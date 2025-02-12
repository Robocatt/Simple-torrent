#include "bencode.h"

namespace Bencode {
    
    std::pair<std::string, size_t> ParseString(const std::string& str){
        if(str.size() == 0){
            return {"", 0};
        }
        if(str.find(":") == std::string::npos){
            throw std::runtime_error("Parse string : was not found\n");
        }
        std::shared_ptr<spdlog::logger> l = spdlog::get("mainLogger");
        std::string expectedStringLengthString = str.substr(0, str.find(':'));
        long long expectedStringLength = std::stoll(expectedStringLengthString);
        l->debug("ParseString, expected len {}", expectedStringLength);
        if(str.size() < expectedStringLength){
            l->error("ParseString error, length of a string is less than expected");
            throw std::runtime_error("Error in Parse String length of a string is less than a len\n");
        }
        std::string cur_str = str.substr(expectedStringLengthString.size() + 1, expectedStringLength);
        return {cur_str, expectedStringLength + 1 + expectedStringLengthString.size()}; 
    }

    std::pair<size_t, size_t> ParseInt(const std::string& str){
        std::shared_ptr<spdlog::logger> l = spdlog::get("mainLogger");
        size_t end = str.find('e');
        if(end == std::string::npos){
            l->error("ParseInt error, end of int does not found");
            throw std::runtime_error("Error in Parse Int end of int does not found\n");
        }
        std::string value = str.substr(1, end - 1);
        size_t value_size_t = std::stoull(value);
        return {value_size_t, end + 1};
    }

    std::unique_ptr<bencodeList> makeBencodeList() {
        return std::make_unique<bencodeList>();
    }

    std::pair<std::unique_ptr<bencodeList>, size_t> ParseListRec(const std::string& str){
        std::shared_ptr<spdlog::logger> l = spdlog::get("mainLogger");
        auto lst = makeBencodeList();
        size_t cur_pos = 1;
        while(str[cur_pos] != 'e'){
            if(isdigit(str[cur_pos])){
                std::pair<std::string, size_t> listItem = ParseString(str.substr(cur_pos));
                lst->elements.push_back(listItem.first);
                cur_pos += listItem.second;
            }else if(str[cur_pos] == 'l'){
                std::pair<std::unique_ptr<bencodeList>, size_t> listItem = ParseListRec(str.substr(cur_pos));
                lst->elements.push_back(std::move(listItem.first));
                cur_pos += listItem.second;
            }else if (str[cur_pos] == 'i'){
                std::pair<size_t, size_t> listItem = ParseInt(str.substr(cur_pos));
                lst->elements.push_back(listItem.first);
                cur_pos += listItem.second;
            }else{
                std::pair<std::unique_ptr<bencodeDict>, size_t> listItem = ParseDictRec(str.substr(cur_pos));
                lst->elements.push_back(std::move(listItem.first));
                cur_pos += listItem.second;
            }
        }
        return {std::move(lst), cur_pos + 1}; 
    }


    std::unique_ptr<bencodeDict> makeBencodeDict(){
        return std::make_unique<bencodeDict>();
    }

    std::pair<std::unique_ptr<bencodeDict>, size_t> ParseDictRec(const std::string& str){
        std::shared_ptr<spdlog::logger> l = spdlog::get("mainLogger");
        auto dict = makeBencodeDict();
        size_t cur_pos = 1;
        l->debug("Parse dict, averall size of dict {}", str.size());
        while(str[cur_pos] != 'e'){
            std::pair<std::string, size_t> key = ParseString(str.substr(cur_pos));
            if(isdigit(str[cur_pos + key.second])){
                l->debug("Parse dict, str on pos {}", cur_pos); 
                std::pair<std::string, size_t> dictItem = ParseString(str.substr(cur_pos + key.second));
                dict->elements[key.first] = dictItem.first;
                cur_pos += dictItem.second;
            }else if(str[cur_pos + key.second] == 'l'){
                l->debug("Parse dict, list on pos {}", cur_pos); 
                std::pair<std::unique_ptr<bencodeList>, size_t>  dictItem = ParseListRec(str.substr(cur_pos + key.second));
                dict->elements[key.first] = std::move(dictItem.first);
                cur_pos += dictItem.second;
            }else if(str[cur_pos + key.second] == 'i'){
                l->debug("Parse dict, int on pos {}", cur_pos); 
                std::pair<size_t, size_t> dictItem = ParseInt(str.substr(cur_pos + key.second));
                dict->elements[key.first] = dictItem.first;
                cur_pos += dictItem.second;
            }else{
                l->debug("Parse dict, dict on pos {}", cur_pos); 
                std::pair<std::unique_ptr<bencodeDict>, size_t> dictItem = ParseDictRec(str.substr(cur_pos + key.second));
                dict->elements[key.first] = std::move(dictItem.first);
                cur_pos += dictItem.second;
            }
            cur_pos += key.second;   
            l->trace("Parse dict, in mp key {}", key.first); 
        }
        return {std::move(dict), cur_pos + 1}; 
    }


}


