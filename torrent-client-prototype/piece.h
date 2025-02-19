#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>

/*
 * Части файла скачиваются не за одно сообщение, а блоками размером 2^14 байт или меньше (последний блок обычно меньше)
 */
struct Block {

    enum Status {
        Missing = 0,
        Pending,
        Retrieved,
    };
    
    Block() = delete;
    Block(uint32_t piece_, uint32_t offset_, uint32_t length_, Status status_, std::string data_) : piece(piece_), offset(offset_), length(length_), status(status_), data(data_) {}

    uint32_t piece;  // id части файла, к которой относится данный блок
    uint32_t offset;  // смещение начала блока относительно начала части файла в байтах
    uint32_t length;  // длина блока в байтах
    Status status;  // статус загрузки данного блока
    std::string data;  // бинарные данные
};

/*
 * Часть скачиваемого файла
 */
class Piece {
public:
    /*
     * index -- номер части файла, нумерация начинается с 0
     * length -- длина части файла. Все части, кроме последней, имеют длину, равную `torrentFile.pieceLength`
     * hash -- хеш-сумма части файла, взятая из `torrentFile.pieceHashes`
     */
    Piece(size_t index, size_t length, std::string hash);

    /*
     * Совпадает ли хеш скачанных данных с ожидаемым
     */
    bool HashMatches() const;

    /*
     * Дать указатель на отсутствующий (еще не скачанный и не запрошенный) блок
     */
    Block* FirstMissingBlock();

    /*
     * Получить порядковый номер части файла
     */
    size_t GetIndex() const;

    /*
     * Сохранить скачанные данные для какого-то блока,
     return number of bytes saved
     */
    size_t SaveBlock(size_t blockOffset, std::string data);

    /*
     * Скачали ли уже все блоки
     */
    bool AllBlocksRetrieved() const;

    /*
     * Получить скачанные данные для части файла
     */
    std::string GetData() const;

    /*
     * Посчитать хеш по скачанным данным
     */
    std::string GetDataHash() const;

    /*
     * Получить хеш для части из .torrent файла
     */
    const std::string& GetHash() const;

    /*
     * Удалить все скачанные данные и отметить все блоки как Missing
     */
    void Reset();

    const size_t GetDownloadedBytes(){
        return localDownloadedBytes_;
    }

    Block* GetBlockByOffset(size_t offset){
        for(auto& blk : blocks_){
            if(blk.offset == offset){
                return &blk;
            }
        }
        return nullptr;
    }

private:
    const size_t index_, length_;
    const std::string hash_;
    std::vector<Block> blocks_;
    size_t localDownloadedBytes_;
};

using PiecePtr = std::shared_ptr<Piece>;
