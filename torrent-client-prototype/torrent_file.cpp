#include "torrent_file.h"
#include "bencode.h"
#include "byte_tools.h"
#include <vector>
#include <openssl/sha.h>
#include <fstream>
#include <variant>
#include <sstream>
#include <iostream>
#include <stdexcept>




#include <typeinfo>
#include <cxxabi.h>


void populateAnnounceList(const Bencode::bencodeList& list, TorrentFile& TFile){
    for (const auto& el : list.elements) {
        std::visit([&TFile](const auto& value) {
            using T = std::decay_t<decltype(value)>; 
            if constexpr (std::is_same_v<T, std::string>) {
                TFile.announceList.push_back(value);
            } else if constexpr (std::is_same_v<T, size_t>) {
                throw std::runtime_error("Torrent parser visit integer in announce list");
            } else if constexpr (std::is_same_v<T, std::unique_ptr<Bencode::bencodeList>>) {
                // std::cout << "Nested List:\n";
                populateAnnounceList(*value, TFile);
            }
        }, el);
    }
}

TorrentFile LoadTorrentFile(const std::string& filename){

    std::ifstream file(filename, std::ios::binary);
    
    if (!file) {
        throw std::runtime_error("Error opening file");
    }

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::string data(size, '\0');
    file.read(&data[0], size); 

    
    int cur_pos = 1;
    // std::cout << data << "\n";
    // std::cout << data.size() << "\n\n\n\n"<< std::endl;
    
    TorrentFile TFile;
    
    while(cur_pos != data.size() - 1){
        // std::cout << "first thing was called" << std::endl;
        auto global_key = Bencode::ParseString(data.substr(cur_pos));
        cur_pos += global_key.second;
        if(global_key.first == "announce"){
            std::cout << "announce was called"<< std::endl;
            auto res = Bencode::ParseString(data.substr(cur_pos));
            if(TFile.announceList.empty()){
                TFile.announceList.emplace_back(res.first);
            }
            cur_pos += res.second;
        }
        //3 variants are possible see http://bittorrent.org/beps/bep_0012.html

        else if(global_key.first == "info"){
            std::cout << "info was called"<< std::endl;
            
            // auto res = Bencode::ParseDict(data.substr(cur_pos));
            auto res = Bencode::ParseDictRec(data.substr(cur_pos));
            std::cout << "info was called, dict parsed"<< std::endl;
            try{
                unsigned char SHA_info[20];
                std::string full_info_string = data.substr(cur_pos, res.second);
                const unsigned char* to_encode = reinterpret_cast<const unsigned char *>(full_info_string.c_str());
                SHA1(to_encode, full_info_string.size(), SHA_info);

                std::string s;
                s.resize(20);
                for(int i = 0; i < 20; ++i){
                    s[i] = SHA_info[i];
                }
                TFile.infoHash = s;

                //6d4795dee7aeb88e03e5336ca7c9fcfa1e206d
                // std::string string_formated;
                // for(auto i : s){
                //     string_formated.push_back(char(i));
                // }
                // TFile.infoHash = string_formated;
                // std::cout << string_formated;

                
                cur_pos += res.second;
                for(const auto& [key, val] : res.first->elements ){
                    auto result = std::visit([](const auto& value) -> std::variant<std::string, size_t> {
                        using T = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<T, std::string>){
                            return value;
                        }else if constexpr (std::is_same_v<T, size_t>){
                            return value;
                        }else{
                            throw std::runtime_error("Unexpected value in visitor");
                        }
                    }, val);
                    if(key == "name"){
                        TFile.name = std::get<std::string>(result);
                    }else if(key == "length"){
                        TFile.length = std::get<size_t>(result);
                    }else if(key == "piece length"){
                        TFile.pieceLength = std::get<size_t>(result);
                    }else if(key == "pieces"){
                        std::string str_pieces = std::get<std::string>(result);
                        size_t cur_pos = 0;
                        while(cur_pos < str_pieces.size()){
                            TFile.pieceHashes.push_back(str_pieces.substr(cur_pos, std::min(size_t(20), str_pieces.size() - cur_pos)));
                            cur_pos += 20;
                        }
                    }
                }
            }
            catch(std::exception& e){
                std::cerr << "Load torrent file exception in info " << e.what() << std::endl; 
            }
            std::cout << "after info\n";
        }else if(global_key.first == "announce-list"){
            // need to change for backups rather then a single list 
            // auto res = Bencode::ParseList(data.substr(cur_pos));
            auto res = Bencode::ParseListRec(data.substr(cur_pos));
            cur_pos += res.second;
            if(!TFile.announceList.empty()){
                TFile.announceList.clear();
            }

            // int status;
            // char* realname;
            // const std::type_info &ti = typeid(res.first);
            // realname = abi::__cxa_demangle(ti.name(), NULL, NULL, &status);
            // rewrite as not void function 
            populateAnnounceList(*res.first, TFile);

            std::cout << "announce list called, total links: " << TFile.announceList.size() << "\n";
            for(auto elem : TFile.announceList){
                std::cout << elem << "\n";
            }
        }else if(global_key.first == "creation date"){
            std::cout << "creation date was called\n";
            auto res = Bencode::ParseInt(data.substr(cur_pos));
            cur_pos += res.second;
        }else if(global_key.first == "comment"){
            std::cout << "comment was called\n";
            auto res = Bencode::ParseString(data.substr(cur_pos));
            TFile.comment = res.first;
            cur_pos += res.second;
        }else if(global_key.first == "created by"){
            std::cout << "created by was called"<< std::endl;
            auto res = Bencode::ParseString(data.substr(cur_pos));
            cur_pos += res.second;
        }else if(global_key.first == "private"){
            auto res = Bencode::ParseInt(data.substr(cur_pos));
            cur_pos += res.second;
            int private_value = res.first;//maybe i'll need it 
        }else if(global_key.first == "url-list"){
            auto res = Bencode::ParseListRec(data.substr(cur_pos));
            cur_pos += res.second;
            // std::cout << "url-list does not supported in this task"<< std::endl;
        }else if (global_key.first == "httpseeds"){
            auto res = Bencode::ParseListRec(data.substr(cur_pos));
            cur_pos += res.second;
        }else if (global_key.first == "encoding"){
            std::cout << "encoding was called\n";
            auto res = Bencode::ParseString(data.substr(cur_pos));
            cur_pos += res.second;
            TFile.encoding = res.first;
            if(res.first != "UTF-8"){
                throw std::runtime_error("Torrent parser encoding is not UTF-8");
            }
        }else if (global_key.first == "publisher"){
            auto res = Bencode::ParseString(data.substr(cur_pos));
            cur_pos += res.second;
            TFile.publisher = res.first;
        }else if (global_key.first == "publisher-url"){
            auto res = Bencode::ParseString(data.substr(cur_pos));
            cur_pos += res.second;
            TFile.publisherURL = res.first;
        }else{
            std::cout<< "TORRENT FILE STARNGE KEY IS " << global_key.first << std::endl;
        }
    }
    std::cout << "parsed announce 1" << TFile.announceList[0] << std::endl;
    // std::cout << TFile.comment << std::endl;
    // std::cout << TFile.pieceLength << std::endl;
    // std::cout << TFile.length << std::endl;
    // std::cout << TFile.name << std::endl;


    return TFile;
}