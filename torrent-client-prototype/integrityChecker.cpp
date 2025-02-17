#include "integrityChecker.h"

bool CheckDownloadedPiecesIntegrity(const std::filesystem::path& outputPath, const TorrentFile& tf, PieceStorage& pieces, std::vector<size_t>& selectedIndices) {
    auto l = spdlog::get("mainLogger");
    l->info("Start downloaded pieces hash check for file: {}", outputPath.string());
    const auto& savedIndices = pieces.GetPiecesSavedToDiscIndices();
    
    // check for directory, file_size works?
    if(savedIndices.empty()){
        if(std::filesystem::exists(outputPath) && std::filesystem::file_size(outputPath) > 0){
            l->error("Output file is not empty, but pieces were not marked as saved");
            throw std::runtime_error("Output file is not empty, but pieces were not marked as saved");
        }
        return false;
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
    
    return true;
}