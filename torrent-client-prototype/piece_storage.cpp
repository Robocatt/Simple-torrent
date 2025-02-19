#include "piece_storage.h"


PieceStorage::PieceStorage(TorrentFile& tf, const std::filesystem::path& outputDirectory, size_t percent, const std::vector<size_t>& selectedIndices, bool doCheck)
    : tf_(tf), doCheck(doCheck) {
    l = spdlog::get("mainLogger");
    l->trace("constructor Piece storage init");

    if(!tf_.multipleFiles){
        initSingleFile(outputDirectory, percent);
    }else{
        initMultiFiles(outputDirectory, selectedIndices);
    }
}

void PieceStorage::initSingleFile(const std::filesystem::path& outputDirectory, size_t percent){
    if(tf_.filesList.empty()) {
        l->error("initSingleFile called, but no files in torrent");
        throw std::runtime_error("No files in single-file torrent");
    }
    File& f = tf_.filesList.back();
    l->info("Initializing single-file storage for '{}', partial = {}%", f.path.back(), percent);
    
    
    totalBytesToDownload = static_cast<size_t>(std::ceil(
        static_cast<double>(f.length) * (static_cast<double>(percent) / 100.0))
    );
    if (totalBytesToDownload == 0 && percent > 0) {
        totalBytesToDownload = 1; 
    }
    // round up to full piece
    piecesToDownload = std::min(tf_.pieceHashes.size(),
        (totalBytesToDownload + tf_.pieceLength - 1) / tf_.pieceLength);

    l->info("constructor Piece storage, expected number of pieces is {}", piecesToDownload);

    for(size_t i = 0; i < piecesToDownload; ++i){
        size_t pieceSize = tf_.pieceLength;
        size_t pieceEnd = (i + 1) * tf_.pieceLength;
        if(pieceEnd > f.length){
            pieceSize = f.length - i * tf_.pieceLength;
        }
        remainPieces_.push(std::make_shared<Piece>(Piece(i, pieceSize, tf_.pieceHashes[i])));
    }

    std::filesystem::path filePath = outputDirectory / tf_.name;
    std::filesystem::create_directories(filePath.parent_path());
    f.fullPath = filePath;
    f.outStream.open(filePath, std::ios::binary | std::ios::out);
    if (!f.outStream.is_open()) {
        l->error("Failed to open a stream for a file {}", filePath.string());
        throw std::runtime_error("Failed to open file: " + filePath.string());
    }
    f.isSelected = true;
    l->info("Single-file: queued {} pieces (of {} total)", piecesToDownload, tf_.pieceHashes.size());
}

void PieceStorage::initMultiFiles(const std::filesystem::path& outputDirectory, const std::vector<size_t>& selectedIndices){
    l->info("Initializing multi-file storage for '{}'", tf_.name);

    if (tf_.filesList.empty()) {
        l->warn("No files in the torrent's fileList!");
        return;
    }

    if(selectedIndices.empty()){
        return;
    }

    for (size_t i = 0; i < tf_.filesList.size(); ++i) {
        File& f = tf_.filesList[i];
        std::filesystem::path filePath = outputDirectory / tf_.name;
        for (auto& p : f.path) {
            filePath /= p;
        }
        f.fullPath = filePath;

        f.isSelected = (std::find(selectedIndices.begin(), selectedIndices.end(), i) != selectedIndices.end());
        if (!f.isSelected)
            continue;
        std::filesystem::create_directories(f.fullPath.parent_path());
        f.outStream.open(f.fullPath , std::ios::binary | std::ios::out);
        
        if (!f.outStream.is_open()) {
            l->error("Failed to open output file for {}", f.fullPath.string());
            throw std::runtime_error("Failed to open multi-file output: " + f.fullPath.string());
        }
    }


    // 3) Queue only the pieces that intersect at least one selected file
    size_t downloadedCount = 0;
    for (size_t i = 0; i < tf_.pieceHashes.size(); ++i) {
        size_t pieceBegin = i * tf_.pieceLength;
        // The last piece may be smaller
        size_t pieceSize = tf_.pieceLength;
        if (pieceBegin + pieceSize > tf_.length) {
            pieceSize = (pieceBegin < tf_.length)
                        ? (tf_.length - pieceBegin) : 0;
        }
        if (pieceSize == 0) {
            break; 
        }
        size_t pieceEnd = pieceBegin + pieceSize - 1;

        // Check overlap with any selected file
        bool needed = false;
        for (auto &f : tf_.filesList) {
            if (!f.isSelected){ 
                continue;
            }
            if (pieceEnd >= f.startOffset && pieceBegin <= f.endOffset) {
                needed = true;
                break;
            }
        }

        if (needed) {
            totalBytesToDownload += pieceSize;
            auto piecePtr = std::make_shared<Piece>(i, pieceSize, tf_.pieceHashes[i]);
            remainPieces_.push(piecePtr);
            downloadedCount++;
        }
    }

    piecesToDownload = downloadedCount;

    l->info("Multi-file: queued {} pieces (of {} total) for user-selected files", 
             downloadedCount, tf_.pieceHashes.size());

}


PiecePtr PieceStorage::GetNextPieceToDownload() {
    std::lock_guard<std::mutex> lock(mtx);
    if(QueueIsEmpty()){
        l->info("QueueIsEmpty");
        return nullptr;
    } else {
        piecesInProgress++;
        PiecePtr front = remainPieces_.front();
        remainPieces_.pop();
        return front;
    }
}

void PieceStorage::PieceProcessed(const PiecePtr& piece) {
    if(piece->AllBlocksRetrieved() && piece->HashMatches()){
        SavePieceToDisk(piece);
    } else {
        l->warn("Hashes do not match, resetting piece {}");
        size_t piecesDownloadedBytes = piece->GetDownloadedBytes();
        piece->Reset();
        bytesDownloaded.fetch_sub(piecesDownloadedBytes, std::memory_order_relaxed);
        
        std::lock_guard<std::mutex> lock(mtx);
        remainPieces_.push(piece);
        piecesInProgress--;
    }
}

bool PieceStorage::QueueIsEmpty() const {
    // std::lock_guard<std::mutex> lock(mtx);
    return remainPieces_.empty();
}

size_t PieceStorage::PiecesSavedToDiscCount() const {
    std::lock_guard<std::mutex> lock(mtx);
    return savedPieces.size();
}

void PieceStorage::CloseOutputFile(){
    std::lock_guard<std::mutex> lock(mtx);
    for (auto &f : tf_.filesList) {
        if (f.outStream.is_open()) {
            f.outStream.close();
        }
    }
}

const std::vector<size_t>& PieceStorage::GetPiecesSavedToDiscIndices() const {
    return savedPieces;
}

size_t PieceStorage::TotalPiecesCount() const {
    return piecesToDownload;
}

void PieceStorage::SavePieceToDisk(const PiecePtr& piece) {
    std::lock_guard<std::mutex> lock(mtx);

    size_t index = piece->GetIndex();
    size_t pieceSize = piece->GetData().size();
    size_t pieceGlobalBegin = index * tf_.pieceLength;
    size_t pieceGlobalEnd = pieceGlobalBegin + pieceSize - 1;

    // For each mapped file, see if overlap
    for (auto &f : tf_.filesList) {
        // If no overlap, skip
        if (pieceGlobalEnd < f.startOffset || pieceGlobalBegin > f.endOffset) {
            continue;
        }
        if (!f.isSelected && doCheck) {
            l->trace("Save piece, file is NOT selected, open stream");
            f.outStream.open(f.fullPath, std::ios::binary | std::ios::out);
        }

        // Overlap range
        size_t overlapBegin = std::max(pieceGlobalBegin, f.startOffset);
        size_t overlapEnd = std::min(pieceGlobalEnd, f.endOffset);
        size_t overlapSize = overlapEnd - overlapBegin + 1;
        l->trace("Save piece, index {}, overlapBegin {}, overlapEnd {}", index, overlapBegin, overlapEnd);
        // Where to read from inside the piece's data
        size_t readOffsetInPiece = overlapBegin - pieceGlobalBegin;

        // Where to write within the file
        std::streamoff writeOffsetInFile = overlapBegin - f.startOffset;

        f.outStream.seekp(writeOffsetInFile, std::ios::beg);
        f.outStream.write(piece->GetData().data() + readOffsetInPiece, overlapSize);
        f.outStream.flush(); 
    }
    savedPieces.push_back(index);
    piecesInProgress--;

    l->info("successfully saved piece {} to disk", piece->GetIndex());
}

size_t PieceStorage::PiecesInProgressCount() const{
    return piecesInProgress;
}
