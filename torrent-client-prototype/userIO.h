#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include "spdlog/spdlog.h"


// Splits a string by comma or space into tokens (e.g. "1,3-5 7" -> ["1", "3-5", "7"])
std::vector<std::string> splitInput(const std::string& input);

/**
 * Parses user input for file selection.
 * - If user types "a" or "all", returns all indices from 1..fileCount.
 * - Otherwise, interprets tokens as either:
 *       "N" (single number) or "X-Y" (range).
 * - Everything is validated to be within [1, fileCount], exception is thrown otherwise.
 */
std::vector<size_t> parseFileSelection(const std::string& input, size_t fileCount);
