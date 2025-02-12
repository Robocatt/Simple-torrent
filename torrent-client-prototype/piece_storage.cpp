#include "piece_storage.h"
#include <cmath>   

PieceStorage::PieceStorage(const TorrentFile& tf, const std::filesystem::path& outputDirectory, size_t percent, const std::vector<size_t>& selectedIndices)
    : tf_(tf) {
    l = spdlog::get("mainLogger");
    l->trace("constructor Piece storage init");

    if(!tf_.multipleFiles){
        initSingleFile(outputDirectory, percent);
    }else{
        initMultiFiles(outputDirectory, selectedIndices);
    }

    // file.open(path_file);
    // file.seekp(tf.length - 1);
    // file.write("", 1);
}

void PieceStorage::initSingleFile(const std::filesystem::path& outputDirectory, size_t percent){
    const File& f = tf_.filesList.back();
    l->info("Initializing single-file storage for '{}', partial = {}%", f.path.back(), percent);
    
    size_t totalBytesWanted = static_cast<size_t>(std::ceil(
        static_cast<double>(f.length) * (static_cast<double>(percent) / 100.0))
    );
    if (totalBytesWanted == 0 && percent > 0) {
        totalBytesWanted = 1; 
    }
    // round up to full piece
    piecesToDownload = std::min(tf_.pieceHashes.size(),
        (totalBytesWanted + tf_.pieceLength - 1) / tf_.pieceLength);

    l->info("constructor Piece storage, expected number of pieces is {}", piecesToDownload);

    for(size_t i = 0; i < piecesToDownload; ++i){
        size_t pieceSize = tf_.pieceLength;
        size_t pieceEnd = (i + 1) * tf_.pieceLength;
        if(pieceEnd > f.length){
            pieceSize = f.length - i * tf_.pieceLength;
        }
        // l->trace("i = {}, ", i);
        remainPieces_.push(std::make_shared<Piece>(Piece(i, pieceSize, tf_.pieceHashes[i])));
    }

    std::filesystem::path filePath = outputDirectory / tf_.name;
    std::filesystem::create_directories(filePath.parent_path());
    FileMapping fm;
    fm.filePath = filePath;
    fm.fileOffsetBegin = 0;
    fm.fileOffsetEnd = f.length > 0 ? (f.length - 1) : 0;
    fm.stream.open(filePath, std::ios::binary | std::ios::out);
    if (!fm.stream.is_open()) {
        l->error("Failed to open a stream for a file {}", filePath.string());
        throw std::runtime_error("Failed to open file: " + filePath.string());
    }
    fileMappings_.push_back(std::move(fm));

    l->info("Single-file: queued {} pieces (of {} total)", piecesToDownload, tf_.pieceHashes.size());
    
}
void PieceStorage::initMultiFiles(const std::filesystem::path& outputDirectory, const std::vector<size_t>& selectedIndices){
    l->info("Initializing multi-file storage for '{}'", tf_.name);
    if(selectedIndices.empty()){
        return;
    }

    // 1) Build offsets for each file in tf_.filesList
    size_t globalOffset = 0;

    for (size_t i = 0; i < tf_.filesList.size(); ++i) {
        const File& f = tf_.filesList[i];
        size_t fileBegin = globalOffset;
        size_t fileEnd = fileBegin + f.length - 1;
        globalOffset += f.length;

        bool isSelected = (std::find(selectedIndices.begin(), selectedIndices.end(), i) != selectedIndices.end());
        if (!isSelected)
            continue;

        std::filesystem::path filePath = outputDirectory / tf_.name;
        for (auto& p : f.path) {
            filePath /= p;
        }
        std::filesystem::create_directories(filePath.parent_path());

        FileMapping fm;
        fm.filePath = filePath;
        fm.fileOffsetBegin = fileBegin;
        fm.fileOffsetEnd = fileEnd;
        fm.stream.open(filePath, std::ios::binary | std::ios::out);

        if (!fm.stream.is_open()) {
            l->error("Failed to open output file for {}", filePath.string());
            throw std::runtime_error("Failed to open multi-file output: " + filePath.string());
        }
        fileMappings_.push_back(std::move(fm));
    }


    // 3) Queue only the pieces that intersect at least one selected file
    size_t downloadedCount = 0;

    for (size_t i = 0; i < tf_.pieceHashes.size(); ++i) {
        size_t pieceBegin = i * tf_.pieceLength;
        // Last piece might be smaller
        size_t pieceSize = tf_.pieceLength;
        if (pieceBegin + pieceSize > tf_.length) {
            pieceSize = (pieceBegin < tf_.length) ? (tf_.length - pieceBegin): 0;
        }
        if (pieceSize == 0) {
            break;
        }
        size_t pieceEnd = pieceBegin + pieceSize - 1;

        // Check overlap
        bool needed = false;
        for (auto& fm : fileMappings_) {
            if ((pieceEnd >= fm.fileOffsetBegin) && (pieceBegin <= fm.fileOffsetEnd)) {
                needed = true;
                break;
            }
        }

        if (needed) {
            PiecePtr piece = std::make_shared<Piece>(i, pieceSize, tf_.pieceHashes[i]);
            remainPieces_.push(piece);
            downloadedCount++;
        }
    }
    piecesToDownload = downloadedCount;

    l->info("Multi-file: queued {} pieces (of {} total) for user-selected files", 
             downloadedCount, tf_.pieceHashes.size());

}


PiecePtr PieceStorage::GetNextPieceToDownload() {
    // std::lock_guard<std::mutex> lock(mtx);
    if(QueueIsEmpty()){
        l->info("QueueIsEmpty");
        return nullptr;
    } else {
        pieces_in_progress++;
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
        piece->Reset();
        std::lock_guard<std::mutex> lock(mtx);
        remainPieces_.push(piece);
    }
}

bool PieceStorage::QueueIsEmpty() const {
    std::lock_guard<std::mutex> lock(mtx);
    return remainPieces_.empty();
}

size_t PieceStorage::PiecesSavedToDiscCount() const {
    std::lock_guard<std::mutex> lock(mtx);
    return saved_pieces.size();
}

void PieceStorage::CloseOutputFile(){
    std::lock_guard<std::mutex> lock(mtx);
    for (auto &fm : fileMappings_) {
        if (fm.stream.is_open()) {
            fm.stream.close();
        }
    }
}

const std::vector<size_t>& PieceStorage::GetPiecesSavedToDiscIndices() const {
    return saved_pieces;
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

    // For each mapped file, see if we overlap
    for (auto &fm : fileMappings_) {
        if (pieceGlobalEnd < fm.fileOffsetBegin || pieceGlobalBegin > fm.fileOffsetEnd) {
            // No overlap
            continue;
        }
        // Overlap range
        size_t overlapBegin = std::max(pieceGlobalBegin, fm.fileOffsetBegin);
        size_t overlapEnd = std::min(pieceGlobalEnd,   fm.fileOffsetEnd);
        size_t overlapSize = overlapEnd - overlapBegin + 1;

        // Where to read from inside the piece's data
        size_t readOffsetInPiece = overlapBegin - pieceGlobalBegin;

        // Where to write within the file
        std::streamoff writeOffsetInFile = overlapBegin - fm.fileOffsetBegin;

        fm.stream.seekp(writeOffsetInFile, std::ios::beg);
        fm.stream.write(piece->GetData().data() + readOffsetInPiece, overlapSize);
        fm.stream.flush(); 
    }
    saved_pieces.push_back(index);
    pieces_in_progress--;

    l->info("successfully saved piece {} to disk", piece->GetIndex());
}

size_t PieceStorage::PiecesInProgressCount() const{
    return pieces_in_progress;
}
