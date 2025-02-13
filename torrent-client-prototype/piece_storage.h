#pragma once

#include "torrent_file.h"
#include "piece.h"
#include <queue>
#include <string>
#include <mutex>
#include <cmath>   
#include <filesystem>
#include "spdlog/spdlog.h"

/*
 * Хранилище информации о частях скачиваемого файла.
 * В этом классе отслеживается информация о том, какие части файла осталось скачать
 */
class PieceStorage {
public:
    PieceStorage(TorrentFile& tf, const std::filesystem::path& outputDirectory, size_t percent, const std::vector<size_t>& selectedIndices, bool doCheck);

    /*
     * Отдает указатель на следующую часть файла, которую надо скачать
     */
    PiecePtr GetNextPieceToDownload();

    /*
     * Эта функция вызывается из PeerConnect, когда скачивание одной части файла завершено.
     * В рамках данного задания требуется очистить очередь частей для скачивания как только хотя бы одна часть будет успешно скачана.
     */
    void PieceProcessed(const PiecePtr& piece);

    /*
     * Остались ли нескачанные части файла?
     */
    bool QueueIsEmpty() const;

    /*
     * Сколько частей файла было сохранено на диск
     */
    size_t PiecesSavedToDiscCount() const;

    /*
     * Сколько частей файла всего
     */
    size_t TotalPiecesCount() const;

    /*
     * Закрыть поток вывода в файл
     */
    void CloseOutputFile();

    /*
     * Отдает список номеров частей файла, которые были сохранены на диск
     */
    const std::vector<size_t>& GetPiecesSavedToDiscIndices() const;

    /*
     * Сколько частей файла в данный момент скачивается
     */
    size_t PiecesInProgressCount() const;
    

private:
    mutable std::mutex mtx; 
    TorrentFile& tf_;
    std::shared_ptr<spdlog::logger> l;
    std::queue<PiecePtr> remainPieces_;
    size_t pieces_in_progress;
    size_t piecesToDownload;
    std::vector<size_t> savedPieces;
    bool doCheck;
    // if doCheck download previous whole piece even if the file is not selected
    /*
     * Сохраняет данную скачанную часть файла на диск.
     * Сохранение всех частей происходит в один выходной файл. Позиция записываемых данных зависит от индекса части
     * и размера частей. Данные, содержащиеся в части файла, должны быть записаны сразу в правильную позицию.
     */
    void SavePieceToDisk(const PiecePtr& piece);

    void initSingleFile(const std::filesystem::path& outputDirectory, size_t percent);

    void initMultiFiles(const std::filesystem::path& outputDirectory, const std::vector<size_t>& selectedIndices);
};
