#pragma once

#include <string>
#include <vector>

struct TorrentFile {
    std::vector<std::string> announceList;
    std::string comment;
    std::vector<std::string> pieceHashes;
    size_t pieceLength;
    size_t length;
    std::string name;
    std::string infoHash;
    std::string encoding;
    std::string publisher;
    std::string publisherURL;
};

TorrentFile LoadTorrentFile(const std::string& filename);
