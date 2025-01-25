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
#include <system_error>
#include <algorithm>


const int peerRequestsForTrackerLimit = 10;

std::string RandomString(size_t length) {
    std::random_device random;
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result.push_back(random() % ('Z' - 'A' + 1) + 'A');
    }
    return result;
}

const std::string PeerId = "TESTAPPDONTWORRY" + RandomString(4);

/**
 * If -no-check is NOT specified, this function is called to verify
 * that all downloaded pieces match their expected SHA1 hash.
 */
void CheckDownloadedPiecesIntegrity(const std::filesystem::path& outputFilename, const TorrentFile& tf, PieceStorage& pieces) {
    std::cout << "Start downloaded pieces hash check for file: " << outputFilename << std::endl;
    const auto& savedIndices = pieces.GetPiecesSavedToDiscIndices();
    
    if(savedIndices.empty()){
        if(std::filesystem::exists(outputFilename) &&
        std::filesystem::file_size(outputFilename) > 0){
            throw std::runtime_error("Output file is not empty, but pieces were not marked as saved");
        }
        return;
    }
    
    if(!std::filesystem::exists(outputFilename)){
        throw std::runtime_error("Output file does not exist!");
    }

    std::vector<size_t> pieceIndices(savedIndices.begin(), savedIndices.end());
    std::sort(pieceIndices.begin(), pieceIndices.end());
    
    size_t maxPieceIndex = pieceIndices.back();
    size_t pieceStartByte = maxPieceIndex * tf.pieceLength;
    size_t pieceEndByte   = (maxPieceIndex + 1) * tf.pieceLength;
    size_t lastPieceSize = (pieceEndByte > tf.length 
        ? tf.length - pieceStartByte
        : tf.pieceLength);

    size_t expectedSize = pieceStartByte + lastPieceSize;

    size_t actualSize = std::filesystem::file_size(outputFilename);
    if(expectedSize != actualSize){
        throw std::runtime_error(
            "Output file has incorrect size: expected = " + std::to_string(expectedSize) 
            + ", actual = " + std::to_string(actualSize));
    }


    std::ifstream file(outputFilename, std::ios_base::binary);
    
    if (!file.is_open()) {\
        const std::string tmp = "sdg";
        throw std::filesystem::filesystem_error(
            "Cannot open file for integrity check:", outputFilename,
            std::make_error_code(std::errc::no_such_file_or_directory)
        );
    }
    
    for (size_t pieceIndex = 0; pieceIndex <= maxPieceIndex; pieceIndex++) {
        std::size_t pieceOffset = pieceIndex * tf.pieceLength;
        std::size_t nextPieceOffset = (pieceIndex + 1) * tf.pieceLength;
        std::size_t thisPieceSize = (nextPieceOffset > tf.length)
                                       ? (tf.length - pieceOffset)
                                       : tf.pieceLength;
        
        file.seekg(static_cast<std::streamoff>(pieceOffset), std::ios::beg);
        if (!file.good()) {
            throw std::filesystem::filesystem_error(
                "Failed to seek to piece offset", outputFilename,
                std::make_error_code(std::errc::no_such_file_or_directory)
            );
        }
        std::string pieceDataFromFile(thisPieceSize, '\0');
        file.read(pieceDataFromFile.data(), static_cast<std::streamsize>(thisPieceSize));
        size_t bytesRead = static_cast<std::size_t>(file.gcount());
        
        if (bytesRead != thisPieceSize) {
            throw std::runtime_error(
                "Could not read full piece from file. Expected " + 
                std::to_string(thisPieceSize) + " bytes, got " + std::to_string(bytesRead)
            );
        }

        const std::string realHash = CalculateSHA1(pieceDataFromFile);
        if (realHash != tf.pieceHashes[pieceIndex]) {
            std::cerr << "File piece with index " << pieceIndex << " has incorrect hash\n Expected: "
                      << HexEncode(tf.pieceHashes[pieceIndex]) << "\n Got : "
                      << HexEncode(realHash) << std::endl;
            throw std::runtime_error("Wrong piece hash for index " + std::to_string(pieceIndex));
        }
    }
    std::cout << "All downloaded pieces has correct hash.\n";
    return;
}

// void DeleteDownloadedFile(const std::filesystem::path& outputFilename) {
//     std::filesystem::remove(outputFilename);
// }

std::filesystem::path PrepareDownloadDirectory(const std::filesystem::path& userPath) {
    std::error_code ec;
    if (!std::filesystem::exists(userPath, ec)) {
        if (!std::filesystem::create_directories(userPath, ec) && ec) {
            throw std::runtime_error(
                "Failed to create directory " + userPath.string() + ": " + ec.message()
            );
        }
        std::cout << "Created directory: " << userPath.string() << std::endl;
    }
    
    std::filesystem::perms perms = std::filesystem::status(userPath, ec).permissions();
    if (ec) {
        throw std::runtime_error(
            "Could not retrieve permissions for " + userPath.string() + ": " + ec.message()
        );
    }
    bool canWrite =
        ((perms & std::filesystem::perms::owner_write)  != std::filesystem::perms::none) ||
        ((perms & std::filesystem::perms::group_write)  != std::filesystem::perms::none) ||
        ((perms & std::filesystem::perms::others_write) != std::filesystem::perms::none);

    if (!canWrite) {
        throw std::invalid_argument(
            "Do not have permission to write to directory: " + userPath.string()
        );
    }
    return userPath;
}

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
    std::cout << "expected number of pieces : " << pieces.TotalPiecesCount() << "\n";
    while (pieces.PiecesSavedToDiscCount() < pieces.TotalPiecesCount()) {
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
            return false;
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
    return true;
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

void TestTorrentFile(const std::filesystem::path& file, const std::filesystem::path& pathToSaveDirectory, size_t percent) {
    TorrentFile torrentFile;
    
    try {
        torrentFile = LoadTorrentFile(file);
        std::cout << "Loaded torrent file " << file << ". Comment: " << torrentFile.comment << std::endl;
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return;
    }
    std::cout << "test torrent file path " << pathToSaveDirectory << std::endl;
    PieceStorage pieces(torrentFile, pathToSaveDirectory, percent);
    
    DownloadTorrentFile(torrentFile, pieces, PeerId, percent);
    pieces.CloseOutputFile();

    CheckDownloadedPiecesIntegrity(pathToSaveDirectory / torrentFile.name, torrentFile, pieces);

}

int main(int argc, char* argv[]) {
    try{
        std::cout << "argc is " << argc << std::endl;
        for (int i = 1; i < argc; ++i){
            std::cout << argv[i] << std::endl;
        }

        std::filesystem::path pathToSaveDirectory{};
        std::filesystem::path pathToTorrentFile;
        size_t percent = -1;
        bool doCheck = true; 
        
        for(int i = 1; i < argc; ++i){
            std::string arg = argv[i];
            if(arg == "-d"){
                if (i + 1 < argc) {
                    pathToSaveDirectory = std::filesystem::path(argv[++i]);
                    pathToSaveDirectory = PrepareDownloadDirectory(pathToSaveDirectory);
                    std::cout << "-d correctly set to " << pathToSaveDirectory << std::endl;
                }else{
                    throw std::invalid_argument("Missing folder path after -d option.");
                }
            }else if(arg == "-p"){
                if (i + 1 < argc) {
                    long long percentLL = stoll(std::string(argv[++i]));
                    if(percentLL < 0){
                        throw std::invalid_argument("Percent to download can not be negative.");
                    }else if(percentLL == 0){
                        throw std::invalid_argument("Percent to download can not be 0.");
                    }else if(percentLL > 100){
                        std::invalid_argument("Percent to download can not be more than 100.");
                    }else {
                        percent = (size_t)percentLL;
                        std::cout << "-p correctly set to  " << percent << std::endl;
                    }
                }else{
                    throw std::invalid_argument("Missing percent to downaload after -p option.");                    
                }
            }else if (arg == "-no-check") {
                doCheck = false;
                std::cout << "Integrity check will be skipped."<< std::endl;
            }else {
                pathToTorrentFile = std::filesystem::path(arg);
                if (!std::filesystem::exists(pathToTorrentFile)) {
                    std::cerr << "Torrent file in " << arg << " does not exist." << std::endl;
                    return 1;
                }
            }
        }
        if(percent == -1){
            std::cout << "Missing -p parameter, set default value 100" << std::endl;
            percent = 100;
        }
        if(pathToSaveDirectory.empty()){
            std::cout << "Missing -d parameter, set default value to ~/Downloads";
            pathToSaveDirectory = PrepareDownloadDirectory(
                std::filesystem::path(std::string(std::getenv("HOME") ? std::getenv("HOME") : ".")) / "Downloads");
        }
        TestTorrentFile(pathToTorrentFile, pathToSaveDirectory, percent);
        std::cout << "end of main.cpp, file has been saved successfully" << std::endl;

    }catch (const std::exception& e){
        std::cout << "Exception occurred in main: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
