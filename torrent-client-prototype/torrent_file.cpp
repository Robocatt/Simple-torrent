#include "torrent_file.h"
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
                auto& infoDict = *res.first;
                // loop over info dictionary
                for(auto& [key, val] : infoDict.elements){
                    if (key == "name") {
                        if (!std::holds_alternative<std::string>(val)) {
                            TFile.l->error("Parser error, Expected string for 'name' filed");
                            throw std::runtime_error("Parser error, Expected string for 'name' filed");
                        }
                        TFile.name = std::get<std::string>(val);
                    }else if (key == "piece length") {
                        if (!std::holds_alternative<size_t>(val)) {
                            TFile.l->error("Parser error, Expected size_t for 'piece length' filed");
                            throw std::runtime_error("Parser error, Expected size_t for 'piece length' filed");
                        }
                        TFile.pieceLength = std::get<size_t>(val);
                    }else if (key == "pieces") {
                        if (!std::holds_alternative<std::string>(val)) {
                            TFile.l->error("Parser error, Expected string for 'pieces' filed");
                            throw std::runtime_error("Parser error, Expected string for 'pieces' filed");
                        }
                        std::string pieceStr = std::get<std::string>(val);
                        // TFile.l->trace("!!!!! pieceStr\n{}", pieceStr);
                        size_t cur_pos = 0;
                        while(cur_pos < pieceStr.size()){
                            TFile.pieceHashes.push_back(
                                pieceStr.substr(cur_pos, std::min(size_t(20), pieceStr.size() - cur_pos))
                            );
                            cur_pos += 20;
                        }
                    }else if (key == "private") {
                        if (!std::holds_alternative<size_t>(val)) {
                            TFile.l->error("Parser error, Expected string for 'private' filed");
                            throw std::runtime_error("Parser error, Expected string for 'private' filed");
                        }
                        size_t p = std::get<size_t>(val);
                        TFile.isPrivate = (p == 1);
                    }
                    // single-file length
                    else if (key == "length") {
                        TFile.multipleFiles = false;
                        if (!std::holds_alternative<size_t>(val)) {
                            TFile.l->error("Parser error, Expected size_t for 'length' filed");
                            throw std::runtime_error("Parser error, Expected size_t for 'length' filed");
                        }
                        if(TFile.filesList.empty()){
                            TFile.filesList.emplace_back(std::get<size_t>(val), "", "");
                        }else{
                            TFile.filesList.back().length = std::get<size_t>(val);
                        }
                    }
                    // single file md5sum
                    else if (key == "md5sum") {
                        if (!std::holds_alternative<std::string>(val)) {
                            TFile.l->error("Parser error, Expected string for 'md5sum' filed");
                            throw std::runtime_error("Parser error, Expected string for 'md5sum' filed");
                        }
                        if(TFile.filesList.empty()){
                            TFile.filesList.emplace_back(0, "", std::get<std::string>(val));
                        }else{
                            TFile.filesList.back().md5sum = std::get<std::string>(val);
                        }
                    }
                    // multi-file mode
                    else if (key == "files") {
                        if (!std::holds_alternative<std::unique_ptr<Bencode::bencodeList>>(val)) {
                            TFile.l->error("Parser error, Expected bencoded list for 'files' filed");
                            throw std::runtime_error("Parser error, Expected bencoded list for 'files' filed");
                        }
                        auto& fileList = *std::get<std::unique_ptr<Bencode::bencodeList>>(val);
                        TFile.multipleFiles = true;
                        // loop over files
                        for (auto& fileEntry : fileList.elements) {
                            if (!std::holds_alternative<std::unique_ptr<Bencode::bencodeDict>>(fileEntry)) {
                                TFile.l->error("Parser error, Each file entry must be a dictionary");
                                throw std::runtime_error("Parser error, Each file entry must be a dictionary");
                            }
                            auto& fileDict = *std::get<std::unique_ptr<Bencode::bencodeDict>>(fileEntry);
                            File f;
                            // loop over dict for a specific file
                            for (auto& [fkey, fval] : fileDict.elements) {
                                if (fkey == "length") {
                                    if (!std::holds_alternative<size_t>(fval)) {
                                        TFile.l->error("Parser error, Expected size_t for file length filed");
                                        throw std::runtime_error("Parser error, Expected size_t for file length filed");
                                    }
                                    f.length = std::get<size_t>(fval);
                                }
                                else if (fkey == "md5sum") {
                                    if (!std::holds_alternative<std::string>(fval)) {
                                        TFile.l->error("Parser error, Expected string for file md5sum filed");
                                        throw std::runtime_error("Parser error, Expected string for file md5sum filed");
                                    }
                                    f.md5sum = std::get<std::string>(fval);
                                }
                                else if (fkey == "path") {
                                    if (!std::holds_alternative<std::unique_ptr<Bencode::bencodeList>>(fval)) {
                                        TFile.l->error("Parser error, Expected list for file path filed");
                                        throw std::runtime_error("Parser error, Expected list for file path filed");
                                    }
                                    auto& pathList = *std::get<std::unique_ptr<Bencode::bencodeList>>(fval);
                                    // loop over directories in path
                                    for (auto& pathElement : pathList.elements) {
                                        if (!std::holds_alternative<std::string>(pathElement)) {
                                            TFile.l->error("Parser error, Expected string for file path dir");
                                            throw std::runtime_error("Parser error, Expected string for file path dir");
                                        }
                                        f.path.push_back(std::get<std::string>(pathElement));
                                    }
                                }
                            } 
                            // add file to list
                            TFile.filesList.push_back(f);
                        }
                    }else {
                        TFile.l->warn("Ignoring unknown key in 'info': {}", key);
                    }
                }
                // single file, add name to the filesList
                if(!TFile.multipleFiles){
                    TFile.length = TFile.filesList.back().length;
                    TFile.filesList.back().path.push_back(TFile.name);
                }
                // for a multi file sum total length
                if(TFile.multipleFiles){
                    size_t totalTorrentLengthBytes = 0;
                    for (const File& f : TFile.filesList) {
                        totalTorrentLengthBytes += f.length;
                    }
                    TFile.length = totalTorrentLengthBytes;
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