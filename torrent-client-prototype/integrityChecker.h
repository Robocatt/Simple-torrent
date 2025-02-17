#include "spdlog/spdlog.h"
#include "byte_tools.h"
#include "torrent_tracker.h"
#include "piece_storage.h"
#include <filesystem>
#include <vector>
#include <algorithm>
#include <fstream>
/**
 * If -no-check is NOT specified, this function is called to verify
 * that all downloaded pieces match their expected SHA1 hash.
 * outputPath    - In single-file mode, the exact file path;
 *                 in multi-file mode, the base directory.
 */
bool CheckDownloadedPiecesIntegrity(const std::filesystem::path& outputPath, const TorrentFile& tf, PieceStorage& pieces, std::vector<size_t>& selectedIndices);