#include "torrent_file.h"
#include "bencode.h"
#include "byte_tools.h"
#include <vector>
#include <openssl/sha.h>
#include <fstream>
#include <stdexcept>



void populateAnnounceList(const Bencode::bencodeList& list, TorrentFile& TFile){
    for (const auto& el : list.elements) {
        std::visit([&TFile](const auto& value) {
            using T = std::decay_t<decltype(value)>; 
            if constexpr (std::is_same_v<T, std::string>) {
                TFile.announceList.push_back(value);
            } else if constexpr (std::is_same_v<T, size_t>) {
                throw std::runtime_error("Torrent parser visit integer in announce list");
            } else if constexpr (std::is_same_v<T, std::unique_ptr<Bencode::bencodeList>>) {
                populateAnnounceList(*value, TFile);
            }
        }, el);
    }
}

TorrentFile LoadTorrentFile(const std::string& filename){

    std::ifstream file(filename, std::ios::binary);
    
    if (!file) {
        spdlog::get("mainLogger")->error("Error opening file: {}", filename);
        throw std::runtime_error("Error opening file");
    }

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::string data(size, '\0');
    file.read(&data[0], size); 

    
    int cur_pos = 1;
    TorrentFile TFile;
    TFile.l = spdlog::get("mainLogger");
    
    while(cur_pos != data.size() - 1){
        auto global_key = Bencode::ParseString(data.substr(cur_pos));
        cur_pos += global_key.second;
        if(global_key.first == "announce"){
            TFile.l->info("announce was called");
            auto res = Bencode::ParseString(data.substr(cur_pos));
            if(TFile.announceList.empty()){
                TFile.announceList.emplace_back(res.first);
            }
            cur_pos += res.second;
        }
        //3 variants are possible see http://bittorrent.org/beps/bep_0012.html

        else if(global_key.first == "info"){
            TFile.l->info("info was called");
            auto res = Bencode::ParseDictRec(data.substr(cur_pos));
            TFile.l->info("info was called, dict parsed");
            try{
                TFile.infoHash = CalculateSHA1(data.substr(cur_pos, res.second));
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
                TFile.l->error("Load torrent file exception in info: {}", e.what());
            }
            TFile.l->trace("after info");
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

            TFile.l->info("announce list called, total links: {}", TFile.announceList.size());
            for (const auto& elem : TFile.announceList) {
                TFile.l->trace("{}", elem);
            }
        }else if(global_key.first == "creation date"){
            TFile.l->info("creation date was called");
            auto res = Bencode::ParseInt(data.substr(cur_pos));
            TFile.creationDate = res.first;
            cur_pos += res.second;
        }else if(global_key.first == "comment"){
            TFile.l->info("comment was called");
            auto res = Bencode::ParseString(data.substr(cur_pos));
            TFile.comment = res.first;
            cur_pos += res.second;
        }else if(global_key.first == "created by"){
            TFile.l->info("created by was called");
            auto res = Bencode::ParseString(data.substr(cur_pos));
            TFile.createdBy = res.first;
            cur_pos += res.second;
        }else if(global_key.first == "private"){
            TFile.l->info("private by was called");
            auto res = Bencode::ParseInt(data.substr(cur_pos));
            cur_pos += res.second;
            int private_value = res.first;//maybe i'll need it 
        }else if(global_key.first == "url-list"){
            TFile.l->info("url-list was called");
            auto res = Bencode::ParseListRec(data.substr(cur_pos));
            cur_pos += res.second;
            // std::cout << "url-list does not supported in this task"<< std::endl;
        }else if (global_key.first == "httpseeds"){
            TFile.l->info("httpseeds was called");
            auto res = Bencode::ParseListRec(data.substr(cur_pos));
            cur_pos += res.second;
        }else if (global_key.first == "encoding"){
            TFile.l->info("encoding was called");
            auto res = Bencode::ParseString(data.substr(cur_pos));
            cur_pos += res.second;
            TFile.encoding = res.first;
            if(res.first != "UTF-8"){
                TFile.l->error("Torrent parser encoding is not UTF-8");
                throw std::runtime_error("Torrent parser encoding is not UTF-8");
            }
        }else if (global_key.first == "publisher"){
            TFile.l->info("publisher was called");
            auto res = Bencode::ParseString(data.substr(cur_pos));
            cur_pos += res.second;
            TFile.publisher = res.first;
        }else if (global_key.first == "publisher-url"){
            TFile.l->info("publisher-url was called");
            auto res = Bencode::ParseString(data.substr(cur_pos));
            cur_pos += res.second;
            TFile.publisherURL = res.first;
        }else{
            TFile.l->warn("TORRENT FILE STRANGE KEY IS {}", global_key.first);
        }
    }
    if (!TFile.announceList.empty()) {
        TFile.l->info("parsed announce 1 {}", TFile.announceList[0]);
    }


    return TFile;
}