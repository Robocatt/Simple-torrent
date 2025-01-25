#include "piece_storage.h"
#include <iostream>
#include <cmath>   

PieceStorage::PieceStorage(const TorrentFile& tf, const std::filesystem::path& outputDirectory, size_t percent): tf_(tf), path(outputDirectory), pieces_in_progress(0){ 
    std::cout << "constructor Piece storage, tf.name is  " << tf.name << std::endl;
    std::filesystem::path path_file = path / tf.name;
    std::cout << "constructor Piece storage, path is " << path_file << std::endl;

    size_t totalBytesWanted = static_cast<size_t>(std::ceil(
        static_cast<double>(tf.length) * (static_cast<double>(percent) / 100.0))
    );
    if (totalBytesWanted == 0 && percent > 0) {
        totalBytesWanted = 1; 
    }
    // round up to full piece 
    piecesToDownload = std::min(tf.pieceHashes.size(), (totalBytesWanted + tf.pieceLength - 1) / tf.pieceLength);
    std::cout << "constructor Piece storage, expected number of pieces is " << piecesToDownload << std::endl;

    
    for(size_t i = 0; i < piecesToDownload; ++i){
        size_t pieceSize = tf.pieceLength;
        size_t pieceEnd = (i + 1) * tf.pieceLength;
        if(pieceEnd > tf.length){
            pieceSize = tf.length - i * tf.pieceLength;
        }
        remainPieces_.push(
            std::make_shared<Piece>(Piece(i, pieceSize, tf.pieceHashes[i]))
            );
    }
    
    file.open(path_file);
    // file.seekp(tf.length - 1);
    // file.write("", 1);
}


PiecePtr PieceStorage::GetNextPieceToDownload() {
    std::lock_guard<std::mutex> lock(mtx);
    if(QueueIsEmpty()){
        std::cout << "QueueIsEmpty" << std::endl;
        return nullptr;
    }else{
        pieces_in_progress++;
        PiecePtr front = remainPieces_.front();
        remainPieces_.pop();
        return front;
    }
}

void PieceStorage::PieceProcessed(const PiecePtr& piece) {
    if(piece->HashMatches()){
        SavePieceToDisk(piece);
    }else{
        std::cout <<"Hashes does not match, reset" << std::endl;
        piece->Reset();
        std::lock_guard<std::mutex> lock(mtx);
        remainPieces_.push(piece);
    }
    
}

bool PieceStorage::QueueIsEmpty() const {
    return remainPieces_.empty();
}

size_t PieceStorage::PiecesSavedToDiscCount() const{
    return saved_pieces.size();
}

void PieceStorage::CloseOutputFile(){
    file.close();
}

const std::vector<size_t>& PieceStorage::GetPiecesSavedToDiscIndices() const{
    return saved_pieces;

}


size_t PieceStorage::TotalPiecesCount() const {
    return piecesToDownload;
}

void PieceStorage::SavePieceToDisk(const PiecePtr& piece) {
    size_t position = piece->GetIndex() * tf_.pieceLength;
    std::lock_guard lock(mtx);
    file.seekp(position);
    file << piece->GetData();
    saved_pieces.push_back(piece->GetIndex());
    pieces_in_progress--;
    std::cout << "successfully saved to disc, piece "<< piece->GetIndex() << "\n" << std::endl;
}


size_t PieceStorage::PiecesInProgressCount() const{
    return pieces_in_progress;
}
