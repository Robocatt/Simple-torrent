#include "torrent_tracker.h"
#include "piece_storage.h"
#include "peer_connect.h"
#include "byte_tools.h"
#include <cassert>
#include <iostream>
#include <filesystem>
#include <random>
#include <thread>
#include <string>
#include <algorithm>

namespace fs = std::filesystem;

const int peerRequestsForTrackerLimit = 10;

std::string RandomString(size_t length) {
    std::random_device random;
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result.push_back(random() % ('Z' - 'A') + 'A');
    }
    return result;
}

const std::string PeerId = "TESTAPPDONTWORRY" + RandomString(4);
// constexpr size_t PiecesToDownload = 20;

void CheckDownloadedPiecesIntegrity(const std::filesystem::path& outputFilename, const TorrentFile& tf, PieceStorage& pieces) {
    pieces.CloseOutputFile();

    if (std::filesystem::file_size(outputFilename) != tf.length) {
        throw std::runtime_error("Output file has wrong size");
    }

    if (pieces.GetPiecesSavedToDiscIndices().size() != pieces.PiecesSavedToDiscCount()) {
        throw std::runtime_error("Cannot determine real amount of saved pieces");
    }

    // if (pieces.PiecesSavedToDiscCount() < PiecesToDownload) {
    //     throw std::runtime_error("Downloaded pieces amount is not enough");
    // }

    // if (pieces.TotalPiecesCount() != tf.pieceHashes.size() || pieces.TotalPiecesCount() < 200) {
    //     throw std::runtime_error("Wrong amount of pieces");
    // }

    std::vector<size_t> pieceIndices = pieces.GetPiecesSavedToDiscIndices();
    std::sort(pieceIndices.begin(), pieceIndices.end());

    std::ifstream file(outputFilename, std::ios_base::binary);
    for (size_t pieceIndex : pieceIndices) {
        const std::streamoff positionInFile = pieceIndex * tf.pieceLength;
        file.seekg(positionInFile);
        if (!file.good()) {
            throw std::runtime_error("Cannot read from file");
        }
        std::string pieceDataFromFile(tf.pieceLength, '\0');
        file.read(pieceDataFromFile.data(), tf.pieceLength);
        const size_t readBytesCount = file.gcount();
        pieceDataFromFile.resize(readBytesCount);
        const std::string realHash = CalculateSHA1(pieceDataFromFile);

        if (realHash != tf.pieceHashes[pieceIndex]) {
            std::cerr << "File piece with index " << pieceIndex << " started at position " << positionInFile <<
                      " with length " << pieceDataFromFile.length() << " has wrong hash " << HexEncode(realHash) <<
                      ". Expected hash is " << HexEncode(tf.pieceHashes[pieceIndex]) << std::endl;
            throw std::runtime_error("Wrong piece hash");
        }
    }
}

// void DeleteDownloadedFile(const std::filesystem::path& outputFilename) {
//     std::filesystem::remove(outputFilename);
// }

// std::filesystem::path PrepareDownloadDirectory(const std::string& randomString) {
//     std::filesystem::path outputDirectory = "/tmp/downloads";
//     outputDirectory /=  randomString;
//     std::filesystem::create_directories(outputDirectory);
//     return outputDirectory;
// }

bool RunDownloadMultithread(PieceStorage& pieces, const TorrentFile& torrentFile, const std::string& ourId, const TorrentTracker& tracker, size_t percent) {
    using namespace std::chrono_literals;

    std::vector<std::thread> peerThreads;
    std::vector<PeerConnect> peerConnections;

    for (const Peer& peer : tracker.GetPeers()) {
        peerConnections.emplace_back(peer, torrentFile, ourId, pieces);
    }

    for (PeerConnect& peerConnect : peerConnections) {
        peerThreads.emplace_back(
                [&peerConnect] () {
                    bool tryAgain = true;
                    int attempts = 0;
                    do {
                        try {
                            ++attempts;
                            peerConnect.Run();
                        } catch (const std::runtime_error& e) {
                            std::cerr << "Peer thread pool Runtime error: " << e.what() << std::endl;
                        } catch (const std::exception& e) {
                            std::cerr << "Peer thread pool Exception: " << e.what() << std::endl;
                        } catch (...) {
                            std::cerr << "Peer thread pool Unknown error" << std::endl;
                        }
                        tryAgain = peerConnect.Failed() && attempts < 1;//change back to 3 ! debug only
                    } while (tryAgain);
                }
        );
    }

    std::this_thread::sleep_for(10s);
    std::cout << "Main thread awake after sleep, all jobs are set\n";
    std::cout << "expected number of pieces : " << torrentFile.pieceHashes.size() * percent / 100 + 1 << "\n";
    while (pieces.PiecesSavedToDiscCount() < torrentFile.pieceHashes.size() * percent / 100 + 1) {
        std::cout << "in loop, PiecesSavedToDiscCount = " << pieces.PiecesSavedToDiscCount() << " ";
        std::cout << "PiecesInProgressCount = " << pieces.PiecesInProgressCount() << " ";
        std::cout << "peerThreads size = " << peerThreads.size() << "\n";
        if (pieces.PiecesInProgressCount() == 0) {
            std::cout << "Want to download more pieces but all peer connections are not working. Let's request new peers" << std::endl;

            for (PeerConnect& peerConnect : peerConnections) {
                peerConnect.Terminate();
            }
            for (std::thread& thread : peerThreads) {
                thread.join();
            }
            return true;
        }
        std::this_thread::sleep_for(1s);
    }
    std::cout << "All pieces are saved to disk\n";

    for (PeerConnect& peerConnect : peerConnections) {
        peerConnect.Terminate();
    }

    for (std::thread& thread : peerThreads) {
        thread.join();
    }
    std::cout << " END RunDownloadMultithread\n";
    return false;
}

void DownloadTorrentFile(const TorrentFile& torrentFile, PieceStorage& pieces, const std::string& ourId, size_t percent) {
    int trackerIndex = 0;
    bool fileSaved = false;
    while(trackerIndex < torrentFile.announceList.size() && !fileSaved){
        std::cout << "Connecting to tracker " << torrentFile.announceList[trackerIndex] << std::endl;
        TorrentTracker tracker(torrentFile.announceList[trackerIndex]);
        std::cout << "after tracker constructor\n";
        int peersReqestLimit = peerRequestsForTrackerLimit; // req limit if 0 peers received.
        bool requestMorePeers = true;
        do {
            try{
                tracker.UpdatePeers(torrentFile, ourId, 12345);
            }catch(const std::exception& e){
                std::cerr << "Error in update peers: " << e.what() << " ";
                std::cerr << "Try next tracker " << std::endl;
                requestMorePeers = false;
                break;
            }
            if (tracker.GetPeers().empty()) {
                std::cerr << "No peers found. Retry" << std::endl;
                requestMorePeers = true;
                peersReqestLimit--;
            }else{
                std::cout << "Found " << tracker.GetPeers().size() << " peers" << std::endl;
                for (const Peer& peer : tracker.GetPeers()) {
                    std::cout << "Found peer " << peer.ip << ":" << peer.port << std::endl;
                }
                fileSaved = RunDownloadMultithread(pieces, torrentFile, ourId, tracker, percent);
            }
            
        } while (peersReqestLimit && !fileSaved);
        trackerIndex++;
    }
    if(!fileSaved){
        std::cerr << "need more peers but all trackers can not provide more\n";
        return;
    }
    std::cout << " END DownloadTorrentFile\n";
}

void TestTorrentFile(const fs::path& file, const fs::path& folder_path, size_t percent) {
    TorrentFile torrentFile;
    
    try {
        torrentFile = LoadTorrentFile(file);
        std::cout << "Loaded torrent file " << file << ". Comment: " << torrentFile.comment << std::endl;
    } catch (const std::invalid_argument& e) {
        std::cerr << e.what() << std::endl;
        return;
        
    }
    std::cout << "test torrent file path " << folder_path << std::endl;
    PieceStorage pieces(torrentFile, folder_path, percent);

    // const std::filesystem::path outputDirectory = PrepareDownloadDirectory(PeerId);
    
    
    DownloadTorrentFile(torrentFile, pieces, PeerId, percent);
    pieces.CloseOutputFile();

    // CheckDownloadedPiecesIntegrity(folder_path / torrentFile.name, torrentFile, pieces);
    // std::cout << "after check download" << std::endl;
    // DeleteDownloadedFile(folder_path / torrentFile.name);
}

int main(int argc, char* argv[]) {
    try{
    std::cout << "argc is " << argc << std::endl;
    for (int i = 1; i < argc; ++i){
        std::cout << argv[i] << std::endl;
    }

    std::filesystem::path folder_path;
    size_t percent;
    std::string path_to_torrent_file;
    
    for(int i = 1; i < argc; ++i){
        std::string arg = argv[i];
        if(arg == "-d"){
            if (i + 1 < argc) {
                folder_path = std::filesystem::path(std::string(argv[i + 1]));
                i++;
                std::cout << "-d correctly passed " << folder_path << std::endl;
            }else{
                std::cout << "Missing folder path after -d option." << std::endl;
                return 1;
            }
        }else if(arg == "-p"){
            if (i + 1 < argc) {
                percent = (size_t)stoll(std::string(argv[i + 1]));
                i++;
                std::cout << "-p correctly passed " << percent << std::endl;
            }else{
                std::cout << "Missing percent after -p option." << std::endl;
                return 1;
            }
        }else{
            try{
                std::cout << "torrent path" << std::endl;
                path_to_torrent_file = arg;
                std::cout << "after torrent path" << std::endl;
            }catch (const char* exception){
                std::cout << "Exception occurred: in torrent path" << exception << std::endl;
                return 1;
            }
        }
    }
    



    // for (const auto& entry : fs::directory_iterator("resources")) {
    // std::cout << path_to_torrent_file << std::endl;
    std::filesystem::path x = path_to_torrent_file;

    TestTorrentFile(std::filesystem::path(path_to_torrent_file), folder_path, percent);
    std::cout << "end of main.cpp file was downloaded "<<std::endl;
    
    }catch (const char* exception){
        std::cout << "Exception occurred: in main" << exception << std::endl;
        return 1;
    }
    return 0;
}
