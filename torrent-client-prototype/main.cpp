#include "torrent_tracker.h"
#include "piece_storage.h"
#include "peer_connect.h"
#include "byte_tools.h"
#include "userIO.h"
#include <cassert>
#include <iostream>
#include <filesystem>
#include <random>
#include <thread>
#include <string>
#include <system_error>
#include <algorithm>
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"


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


void logInit(){
    // main log for the user with WARNS and above 
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_level(spdlog::level::warn);
    
    // debug log for all messages 
    auto debugLogFileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("Logs/debug.log");
    debugLogFileSink->set_level(spdlog::level::trace);

    std::shared_ptr<spdlog::logger> logger = std::make_shared<spdlog::logger>("mainLogger");
    logger->sinks().push_back(consoleSink);
    logger->sinks().push_back(debugLogFileSink);
    logger->set_level(spdlog::level::trace);
    logger->set_pattern("%d.%m.%Y %T [%^%l%$] [%n] %v");
    spdlog::flush_every(std::chrono::seconds(5));

    spdlog::register_logger(logger);
    
    // check the logger usability  
    auto l = spdlog::get("mainLogger");
    if(l){
        l->info("mainLogger created and registered");
    }
}

/**
 * If -no-check is NOT specified, this function is called to verify
 * that all downloaded pieces match their expected SHA1 hash.
 * outputPath    - In single-file mode, the exact file path;
 *                 in multi-file mode, the base directory.
 */
void CheckDownloadedPiecesIntegrity(const std::filesystem::path& outputPath, const TorrentFile& tf, PieceStorage& pieces, std::vector<size_t>& selectedIndices) {
    auto l = spdlog::get("mainLogger");
    l->info("Start downloaded pieces hash check for file: {}", outputPath.string());
    const auto& savedIndices = pieces.GetPiecesSavedToDiscIndices();
    
    // check for directory, file_size works?
    if(savedIndices.empty()){
        if(std::filesystem::exists(outputPath) && std::filesystem::file_size(outputPath) > 0){
            l->error("Output file is not empty, but pieces were not marked as saved");
            throw std::runtime_error("Output file is not empty, but pieces were not marked as saved");
        }
        return;
    }

    // single file
    if(!tf.multipleFiles){
        
        // check name for a single file
        if(!std::filesystem::exists(outputPath)){
            l->error("Output file does not exist!");
            throw std::runtime_error("Output file does not exist!");
        }

        std::vector<size_t> pieceIndices(savedIndices.begin(), savedIndices.end());
        std::sort(pieceIndices.begin(), pieceIndices.end());
        
        // check size, compare 0 .. Nth piece, determined by a percent of a file to download
        size_t maxPieceIndex = pieceIndices.back();
        size_t lastPieceStartByte = maxPieceIndex * tf.pieceLength;
        size_t lastPieceEndByte   = (maxPieceIndex + 1) * tf.pieceLength;
        size_t lastPieceSize = (lastPieceEndByte > tf.length 
            ? tf.length - lastPieceStartByte
            : tf.pieceLength);
        // l->critical("maxPieceIndex = {}, lastPieceStartByte = {}, lastPieceEndByte = {}, lastPieceSize = {}", maxPieceIndex, lastPieceStartByte, lastPieceEndByte, lastPieceSize);
        size_t expectedSize = lastPieceStartByte + lastPieceSize;

        size_t actualSize = std::filesystem::file_size(outputPath);
        if(expectedSize != actualSize){
            std::string errMsg = 
                "Output file has incorrect size: expected = " + std::to_string(expectedSize) +
                ", actual = " + std::to_string(actualSize);
            l->error("{}", errMsg);
            throw std::runtime_error(errMsg);
        }


        std::ifstream file(outputPath, std::ios_base::binary);
        
        if (!file.is_open()) {
            l->error("Cannot open file for integrity check: {} (no_such_file_or_directory)", 
                    outputPath.string());
            throw std::filesystem::filesystem_error(
                "Cannot open file for integrity check:", outputPath,
                std::make_error_code(std::errc::no_such_file_or_directory)
            );
        }
        // check each piece in the file and compare hashs
        for (size_t pieceIndex = 0; pieceIndex <= maxPieceIndex; pieceIndex++) {
            std::size_t pieceOffset = pieceIndex * tf.pieceLength;
            std::size_t nextPieceOffset = (pieceIndex + 1) * tf.pieceLength;
            std::size_t thisPieceSize = (nextPieceOffset > tf.length)
                                        ? (tf.length - pieceOffset)
                                        : tf.pieceLength;
            
            file.seekg(static_cast<std::streamoff>(pieceOffset), std::ios::beg);
            if (!file.good()) {
                l->error("Failed to seek to piece offset {} in file {}", 
                        pieceOffset, outputPath.string());
                throw std::filesystem::filesystem_error(
                    "Failed to seek to piece offset", outputPath,
                    std::make_error_code(std::errc::no_such_file_or_directory)
                );
            }
            std::string pieceDataFromFile(thisPieceSize, '\0');
            file.read(pieceDataFromFile.data(), static_cast<std::streamsize>(thisPieceSize));
            size_t bytesRead = static_cast<std::size_t>(file.gcount());
            
            if (bytesRead != thisPieceSize) {
                std::string errMsg = 
                    "Could not read full piece from file. Expected " +
                    std::to_string(thisPieceSize) + " bytes, got " + std::to_string(bytesRead);
                l->error("{}", errMsg);
                throw std::runtime_error(errMsg);
            }

            const std::string realHash = CalculateSHA1(pieceDataFromFile);
            if (realHash != tf.pieceHashes[pieceIndex]) {
                l->error("File piece with index {} has incorrect hash\n"
                        "Expected: {}\n"
                        "Got: {}",
                        pieceIndex,
                        HexEncode(tf.pieceHashes[pieceIndex]),
                        HexEncode(realHash));
                throw std::runtime_error(
                    "Wrong piece hash for index " + std::to_string(pieceIndex)
                );
            }
        }
    }else{ // multi file 
      
        // We'll check each file in the torrent that was selected for download.

        if(!std::filesystem::exists(outputPath)) {
            l->error("Multi-file root directory does not exist: {}", outputPath.string());
            throw std::runtime_error("Multi-file root dir missing");
        }
        if(!std::filesystem::is_directory(outputPath)) {
            l->error("Multi-file: outputPath is not a directory: {}", outputPath.string());
            throw std::runtime_error("Multi-file output path not a directory");
        }

        // Ssorted list of pieces that were saved
        std::vector<size_t> pieceIndices(savedIndices.begin(), savedIndices.end());
        std::sort(pieceIndices.begin(), pieceIndices.end());

        // loop over selected files
        for (size_t fileIndex : selectedIndices) {
            if (fileIndex >= tf.filesList.size() || fileIndex < 0) {
                l->warn("Skipping invalid fileIndex: {}", fileIndex);
                continue;
            }

            const auto &f = tf.filesList[fileIndex];

            // Check that the file exists
            if (f.length > 0) {
                if (!std::filesystem::exists(f.fullPath)) {
                    l->error("Selected file does not exist: {}", f.fullPath.string());
                    throw std::runtime_error("Missing file: " + f.fullPath.string());
                }

                // Check the file size
                size_t onDiskSize = std::filesystem::file_size(f.fullPath);
                if (onDiskSize != f.length) {
                    l->error("File {} has size {} but expected {} (possibly partial?)",
                            f.fullPath.string(), onDiskSize, f.length);
                    throw std::runtime_error("File size mismatch");
                }
            }
            else {
                if (std::filesystem::exists(f.fullPath)) {
                    if (std::filesystem::file_size(f.fullPath) != 0) {
                        l->error("File expeceted length = 0 in torrent, but it is non-empty: {}", f.fullPath.string());
                        throw std::runtime_error("File expeceted length = 0 in torrent, but it is non-empty");
                    }
                }
            }
        }

        // check all piece hashs
        size_t maxPieceIndex = pieceIndices.back();

        for (size_t pieceIndex : pieceIndices) {
            // The global offset range for this piece
            size_t pieceGlobalBegin = pieceIndex * tf.pieceLength;
            size_t pieceGlobalEnd   = pieceGlobalBegin + tf.pieceLength;
            if (pieceGlobalEnd > tf.length) {
                pieceGlobalEnd = tf.length; 
            }
            size_t pieceSize = pieceGlobalEnd - pieceGlobalBegin;
            if (pieceSize == 0) {
                continue;
            }

            // We need to read [pieceGlobalBegin, pieceGlobalEnd) from the disk
            std::string pieceData;
            pieceData.reserve(pieceSize);

            // Iterate over each file that might overlap this piece
            size_t bytesRemaining = pieceSize;
            size_t readCursor = pieceGlobalBegin; 

            for (size_t fileIndex = 0; fileIndex < tf.filesList.size(); fileIndex++) {
                const auto &f = tf.filesList[fileIndex];
                if (readCursor >= pieceGlobalEnd) {
                    break; // done
                }
                if (f.endOffset < readCursor || f.startOffset > pieceGlobalEnd) {
                    // no overlap
                    continue;
                }

                // Overlap region: [overlapBegin, overlapEnd)
                size_t overlapBegin = std::max(readCursor, f.startOffset);
                size_t overlapEnd   = std::min(pieceGlobalEnd, f.endOffset + 1);
                if (overlapEnd <= overlapBegin) {
                    continue;
                }
                size_t chunkSize = overlapEnd - overlapBegin;

                std::ifstream ifs(f.fullPath, std::ios::binary);
                if(!ifs.is_open()) {
                    l->error("Failed to open file {} to read piece data", f.fullPath.string());
                    throw std::runtime_error("Cannot open file for piece read");
                }
                // Seek to the appropriate offset
                std::streamoff localOffset = static_cast<std::streamoff>(overlapBegin - f.startOffset);
                ifs.seekg(localOffset, std::ios::beg);
                if(!ifs.good()) {
                    l->error("Cannot seek to offset {} in file {}", localOffset, f.fullPath.string());
                    throw std::runtime_error("Seek error in multi-file read");
                }

                // Read chunkSize bytes
                std::string chunk(chunkSize, '\0');
                ifs.read(chunk.data(), static_cast<std::streamsize>(chunkSize));
                size_t bytesRead = static_cast<size_t>(ifs.gcount());
                if (bytesRead != chunkSize) {
                    l->error("Wanted to read {} bytes from {}, got {} bytes", chunkSize, f.fullPath.string(), bytesRead);
                    throw std::runtime_error("Partial read in multi-file check");
                }

                // Append to pieceData
                pieceData.append(chunk);

                // Advance readCursor
                readCursor += chunkSize;
                bytesRemaining -= chunkSize;
                if (bytesRemaining == 0) {
                    break; // we have the whole piece
                }
            }

            //  pieceData should have the entire piece (pieceSize bytes) from 1 or multiple files.
            if (pieceData.size() != pieceSize) {
                l->error("Piece index {}: expected {} bytes, got {} in multi-file read", pieceIndex, pieceSize, pieceData.size());
                throw std::runtime_error("Piece read mismatch in multi-file");
            }

            // Compare the SHA1 hash of pieceData and from .torrent file
            const std::string realHash = CalculateSHA1(pieceData);
            if (realHash != tf.pieceHashes[pieceIndex]) {
                l->error("Multi-file piece index {} has incorrect hash.\n"
                         "Expected: {}\nGot: {}",
                         pieceIndex,
                         HexEncode(tf.pieceHashes[pieceIndex]),
                         HexEncode(realHash));
                throw std::runtime_error("Wrong piece hash for index " + std::to_string(pieceIndex));
            }
        } // end for pieceIndex

        l->info("Multi-file: all downloaded pieces have correct hash.");
        for (size_t fileIndex = 0; fileIndex < tf.filesList.size(); fileIndex++) {
            if(!tf.filesList[fileIndex].isSelected){
                std::filesystem::remove(tf.filesList[fileIndex].fullPath);
            }
        }
    } // end multipleFiles else
    l->info("All downloaded pieces have correct hash.");
    std::cout << "All downloaded pieces have correct hash.";
    return;
}

// void DeleteDownloadedFile(const std::filesystem::path& outputFilename) {
//     std::filesystem::remove(outputFilename);
// }

std::filesystem::path PrepareDownloadDirectory(const std::filesystem::path& userPath) {
    auto l = spdlog::get("mainLogger");
    std::error_code ec;
    if (!std::filesystem::exists(userPath, ec)) {
        if (!std::filesystem::create_directories(userPath, ec) && ec) {
            std::string errMsg = "Failed to create directory " + userPath.string() + 
                                 ": " + ec.message();
            l->error("{}", errMsg);
            throw std::runtime_error(errMsg);
        }
        l->info("Created directory: {}", userPath.string());
    }
    
    std::filesystem::perms perms = std::filesystem::status(userPath, ec).permissions();
    if (ec) {
        std::string errMsg = "Could not retrieve permissions for " + 
                             userPath.string() + ": " + ec.message();
        l->error("{}", errMsg);
        throw std::runtime_error(errMsg);
    }
    bool canWrite =
        ((perms & std::filesystem::perms::owner_write)  != std::filesystem::perms::none) ||
        ((perms & std::filesystem::perms::group_write)  != std::filesystem::perms::none) ||
        ((perms & std::filesystem::perms::others_write) != std::filesystem::perms::none);

    if (!canWrite) {
        std::string errMsg = "Do not have permission to write to directory: " + 
                             userPath.string();
        l->error("{}", errMsg);
        throw std::invalid_argument(errMsg);
    }
    return userPath;
}

bool RunDownloadMultithread(PieceStorage& pieces, const TorrentFile& torrentFile, const std::string& ourId, const TorrentTracker& tracker, size_t percent) {
    using namespace std::chrono_literals;
    auto l = spdlog::get("mainLogger");
    std::vector<std::thread> peerThreads;
    std::vector<PeerConnect> peerConnections;

    for (const Peer& peer : tracker.GetPeers()) {
        peerConnections.emplace_back(peer, torrentFile, ourId, pieces);
    }

    for (PeerConnect& peerConnect : peerConnections) {
        peerThreads.emplace_back(
                [&peerConnect] () {
                    auto lthread = spdlog::get("mainLogger");
                    bool tryAgain = true;
                    int attempts = 0;
                    do {
                        try {
                            ++attempts;
                            peerConnect.Run();
                        } catch (const std::runtime_error& e) {
                        lthread->error("Peer thread pool Runtime error: {}", e.what());
                        } catch (const std::exception& e) {
                            lthread->error("Peer thread pool Exception: {}", e.what());
                        } catch (...) {
                            lthread->error("Peer thread pool Unknown error");
                        }
                        tryAgain = peerConnect.Failed() && attempts < 1;//change back to 3 ! debug only
                    } while (tryAgain);
                }
        );
    }

    std::this_thread::sleep_for(10s);
    l->info("Main thread awake after sleep, all jobs are set");
    l->info("Expected number of pieces: {}", pieces.TotalPiecesCount());
    while (pieces.PiecesSavedToDiscCount() < pieces.TotalPiecesCount()) {
        l->info("In loop, PiecesSavedToDiscCount = {}, PiecesInProgressCount = {}, peerThreads.size() = {}",
                pieces.PiecesSavedToDiscCount(),
                pieces.PiecesInProgressCount(),
                peerThreads.size());
        if (pieces.PiecesInProgressCount() == 0) {
            l->warn("Want to download more pieces but all peer connections are not working. Requesting new peers...");

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
    l->info("All pieces are saved to disk");

    for (PeerConnect& peerConnect : peerConnections) {
        peerConnect.Terminate();
    }

    for (std::thread& thread : peerThreads) {
        thread.join();
    }
    l->info("END RunDownloadMultithread");
    return true;
}

void DownloadTorrentFile(const TorrentFile& torrentFile, PieceStorage& pieces, const std::string& ourId, size_t percent) {
    auto l = spdlog::get("mainLogger");
    int trackerIndex = 0;
    bool fileSaved = false;
    while(trackerIndex < torrentFile.announceList.size() && !fileSaved){
        l->info("Connecting to tracker {}", torrentFile.announceList[trackerIndex]);
        TorrentTracker tracker(torrentFile.announceList[trackerIndex]);
        l->info("After tracker constructor");
        int peersReqestLimit = peerRequestsForTrackerLimit; // req limit if 0 peers received.
        bool requestMorePeers = true;
        do {
            try{
                tracker.UpdatePeers(torrentFile, ourId, 12345);
            }catch(const std::exception& e){
                l->warn("Error in update peers: {}. Try next tracker.", e.what());
                requestMorePeers = false;
                break;
            }
            if (tracker.GetPeers().empty()) {
                l->warn("No peers found. Retry...");
                requestMorePeers = true;
                peersReqestLimit--;
            }else{
                l->info("Found {} peers", tracker.GetPeers().size());
                for (const Peer& peer : tracker.GetPeers()) {
                    l->info("Found peer {}:{}", peer.ip, peer.port);
                }
                fileSaved = RunDownloadMultithread(pieces, torrentFile, ourId, tracker, percent);
            }
            
        } while (peersReqestLimit && !fileSaved);
        trackerIndex++;
    }
    if(!fileSaved){
        l->error("Need more peers but all trackers can not provide more");
        return;
    }
    l->info("END DownloadTorrentFile");
}

void TestTorrentFile(const std::filesystem::path& file, const std::filesystem::path& pathToSaveDirectory, size_t percent, bool doCheck) {
    TorrentFile torrentFile;
    auto l = spdlog::get("mainLogger");
    try {
        torrentFile = LoadTorrentFile(file);
        l->info("Loaded torrent file {}. Comment: {}", file.string(), torrentFile.comment);
    } catch (const std::exception& e) {
        l->error("{}", e.what());
        return;
    }
    l->info("Test torrent file path: {}", pathToSaveDirectory.string());
    
    
    std::cout << "Select files to download:\n";
    for(size_t i = 0; i <  torrentFile.filesList.size(); ++i){
        std::cout << '[' << i + 1 <<']' << torrentFile.filesList[i].path.back() << "\n";
    }
    std::cout << "[a] All\n\n";
    std::cout.flush();

    std::cout << "Enter selection (example: '1', '1,3-5', or 'a' for all): ";
    std::string input;
    if (!std::getline(std::cin, input)) {
        // I/O error! 
        throw std::runtime_error("IO error");
    }
    std::vector<size_t> selectedIndices = parseFileSelection(input, torrentFile.filesList.size());
    
    if (selectedIndices.empty()) {
        std::cout << "No valid selection given. Nothing will be downloaded.\n";
        return;
    } else {
        std::cout << "You selected these files:\n";
        for (size_t& idx : selectedIndices) {
            std::cout << "  " << torrentFile.filesList[idx].path.back() << "\n";
        }
        std::cout.flush();
    }
    PieceStorage pieces(torrentFile, pathToSaveDirectory, percent, selectedIndices, doCheck);
    DownloadTorrentFile(torrentFile, pieces, PeerId, percent);
    pieces.CloseOutputFile();
    if(doCheck){
        CheckDownloadedPiecesIntegrity(pathToSaveDirectory / torrentFile.name, torrentFile, pieces, selectedIndices);
    }
}

// add log level argument 
int main(int argc, char* argv[]) {
    try{
        logInit();
    }catch (const spdlog::spdlog_ex& ex){
        std::cerr << "Log initialization failed: " << ex.what() << std::endl;
        return 1;
    }catch (...){
        std::cerr << "Got an unexpected error." << std::endl;
        return 1;
    }
    std::shared_ptr<spdlog::logger> l = spdlog::get("mainLogger");


    try{
        l->info("argc = {}", argc);
        for (int i = 1; i < argc; ++i){
            l->info("Arg {}: {}", i, argv[i]);
        }

        std::filesystem::path pathToSaveDirectory;
        std::filesystem::path pathToTorrentFile;
        size_t percent = -1;
        bool doCheck = true; 
        
        for(int i = 1; i < argc; ++i){
            std::string arg = argv[i];
            if(arg == "-d"){
                if (i + 1 < argc) {
                    pathToSaveDirectory = std::filesystem::path(argv[++i]);
                    pathToSaveDirectory = PrepareDownloadDirectory(pathToSaveDirectory);
                    l->info("-d correctly set to {}", pathToSaveDirectory.string());
                } else {
                    std::string err = "Missing folder path after -d option.";
                    l->error("{}", err);
                    throw std::invalid_argument(err);
                }
            }else if(arg == "-p"){
                if (i + 1 < argc) {
                    long long percentLL = stoll(std::string(argv[++i]));
                    if(percentLL < 0){
                        std::string err = "Percent to download can not be negative.";
                        l->error("{}", err);
                        throw std::invalid_argument(err);
                    } else if(percentLL == 0){
                        std::string err = "Percent to download can not be 0.";
                        l->error("{}", err);
                        throw std::invalid_argument(err);
                    } else if(percentLL > 100){
                        std::string err = "Percent to download can not be more than 100.";
                        l->error("{}", err);
                        throw std::invalid_argument(err);
                    } else {
                        percent = static_cast<size_t>(percentLL);
                        l->info("-p correctly set to {}", percent);
                    }
                }else{
                    std::string err = "Missing percent to download after -p option.";
                    l->error("{}", err);
                    throw std::invalid_argument(err);
                }
            }else if (arg == "-no-check") {
                doCheck = false;
                l->info("Integrity check will be skipped.");
            }else {
                pathToTorrentFile = std::filesystem::path(arg);
                if (!std::filesystem::exists(pathToTorrentFile)) {
                    l->error("Torrent file '{}' does not exist.", arg);
                    return 1;
                }
            }
        }
        if(percent == -1){
            l->warn("Missing -p parameter, using default value 100");
            percent = 100;
        }
        if(pathToSaveDirectory.empty()){
            l->warn("Missing -d parameter, using default value ~/Downloads");
            pathToSaveDirectory = PrepareDownloadDirectory(
                std::filesystem::path(std::string(std::getenv("HOME") 
                                                   ? std::getenv("HOME") 
                                                   : ".")) / "Downloads"
            );
        }
        TestTorrentFile(pathToTorrentFile, pathToSaveDirectory, percent, doCheck);
        l->critical("End of main.cpp, file has been saved successfully");

    }catch (const std::exception& e){
        l->error("Exception occurred in main: {}", e.what());
        return 1;
    }
    return 0;
}
