#include "byte_tools.h"
#include <openssl/sha.h>
#include <vector>
#include <sstream>
#include <iomanip>

size_t BytesToInt(std::string_view bytes) {

    size_t a = (long long)((unsigned char)(bytes[0]) << 24 |
            (unsigned char)(bytes[1]) << 16 |
            (unsigned char)(bytes[2]) << 8 |
            (unsigned char)(bytes[3]));
    return a;
}


std::string CalculateSHA1(const std::string& msg) {
    unsigned char SHA_info[20];
    const unsigned char* to_encode = reinterpret_cast<const unsigned char *>(msg.c_str());
    SHA1(to_encode, msg.size(), SHA_info);

    std::string s;
    s.resize(20);
    for(int i = 0; i < 20; ++i){
        s[i] = SHA_info[i];
    }
    return s;
}

std::string IntToBytes(int num){
    std::string result;
    for (int i = 3; i >= 0; --i) {
        unsigned char byte = (num >> (i * 8)) & 0xFF; 
        result.push_back(byte); 
    }
    return result;
}

std::string HexEncode(const std::string& input){
    std::string res;
    for(size_t i = 0; i < input.size(); ++i ){
        res += (unsigned int)input[i]; 
    }
    return res;

}

std::string URLEncode(const std::string& data) {
    std::ostringstream encoded;
    for (unsigned char c : data) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else {
            encoded << '%' << std::hex << std::setw(2) << std::setfill('0') << std::nouppercase << static_cast<int>(c);
        }
    }
    return encoded.str();
}