#pragma once

#include <string>
#include <sstream>
#include <vector>
#include "bencode.h"
#include "spdlog/spdlog.h"

struct File{
    size_t length;
    std::vector<std::string> path;
    std::string md5sum;
    size_t startOffset = 0;
    size_t endOffset = 0;
    bool isSelected = false;
    std::ofstream outStream;
    File () {}
    File(size_t length_, const std::string path_, const std::string md5sum_) :
        length(length_), md5sum(md5sum_){
            path.push_back(path_);
        }
};

struct TorrentFile {
    std::vector<std::string> announceList;
    std::string comment;
    std::vector<std::string> pieceHashes;
    size_t pieceLength;
    size_t creationDate;
    size_t length; // either length of a single file or length of ALL files in multi file
    std::string name;// either a name of the file or a directory for multi-file
    std::string createdBy;
    std::string infoHash;
    std::string encoding;
    std::string publisher;
    std::string publisherURL;
    bool multipleFiles;
    bool isPrivate;
    std::vector<File> filesList;
    std::shared_ptr<spdlog::logger> l;
};

TorrentFile LoadTorrentFile(const std::string& filename);
