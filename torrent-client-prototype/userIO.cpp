#include "userIO.h"

std::vector<std::string> splitInput(const std::string& input) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream iss(input);

    std::string inputWithSpaces;
    for (char c : input) {
        if (c == ',') {
            c = ' ';
        }
        inputWithSpaces.push_back(c);
    }
    std::istringstream iss2(inputWithSpaces);

    while (iss2 >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

std::vector<size_t> parseFileSelection(const std::string& input, size_t fileCount) {
    auto l = spdlog::get("mainLogger");
    std::vector<size_t> selected;

    std::string trimmed = input;
    trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(),
                                               [](unsigned char ch){ return !std::isspace(ch); }));
    trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(),
                               [](unsigned char ch){ return !std::isspace(ch); }).base(),
                  trimmed.end());

    // Check for 'a' or 'all'
    if (trimmed == "a" || trimmed == "all") {
        // Return all files
        selected.resize(fileCount);
        for (size_t i = 0; i < fileCount; ++i) {
            selected[i] = i; 
        }
        return selected;
    }

    // Split on spaces/commas
    std::vector<std::string> tokens = splitInput(trimmed);

    for (const auto& tk : tokens) {
        // Check if it's a range "X-Y"
        size_t dashPos = tk.find('-');
        if (dashPos != std::string::npos) {
            // Range
            std::string startStr = tk.substr(0, dashPos);
            std::string endStr   = tk.substr(dashPos + 1);
            try {
                int startVal = std::stoi(startStr);
                int endVal   = std::stoi(endStr);
                startVal--;
                endVal--;
                if (startVal < 0 || endVal < 0 || startVal > static_cast<int>(fileCount) || endVal > static_cast<int>(fileCount)) {
                    throw std::out_of_range("Index out of valid range");
                }
                if (startVal > endVal) {
                    // You could allow reverse ranges, but let's keep it simple
                    throw std::runtime_error("Start of range is greater than end.");
                }
                for (int i = startVal; i <= endVal; i++) {
                    selected.push_back(static_cast<size_t>(i));
                }
            } 
            catch (const std::exception& e) {
                // We handle parse errors or out_of_range errors
                l->warn("Skipping invalid range {}", tk);
            }
        } 
        else {
            // Single index
            try {
                int val = std::stoi(tk);
                val--;
                if (val < 0 || val > static_cast<int>(fileCount)) {
                    throw std::out_of_range("Index out of valid range");
                }
                selected.push_back(static_cast<size_t>(val));
            }
            catch (const std::exception& e) {
                l->warn("Skipping invalid token {}", tk);
            }
        }
    }

    std::sort(selected.begin(), selected.end());
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());

    return selected;
}