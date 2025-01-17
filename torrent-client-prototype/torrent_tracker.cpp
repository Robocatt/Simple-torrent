#include "torrent_tracker.h"
#include "bencode.h"
#include "byte_tools.h"
#include <iostream>
#include <string>
#include <cpr/cpr.h>

TorrentTracker::TorrentTracker(const std::string& url) : url_(url) {}

const std::vector<Peer> &TorrentTracker::GetPeers() const {
    return peers_;
}



void TorrentTracker::UpdatePeers(const TorrentFile& tf, std::string peerId, int port){
    std::cout << "Before update peers call"  << std::endl;
        cpr::Response res = cpr::Get(
            cpr::Url{url_},
            cpr::Parameters {
                    {"info_hash", tf.infoHash},
                    {"peer_id", peerId},
                    {"port", std::to_string(port)},
                    {"uploaded", std::to_string(0)},
                    {"downloaded", std::to_string(0)},
                    {"left", std::to_string(tf.length)},
                    {"compact", std::to_string(1)}
            },
            cpr::Timeout{2000}
        );
        if(res.status_code != 200){
            throw std::runtime_error("status code " + std::to_string(res.status_code));
        }
        std::cout << "Update peers, get respsonse, status code " << res.status_code << std::endl;

        // if (res.status_code == 200) {
        //     if (res.text.find("failure reason") == std::string::npos) {
        //         std::cout << "Successfully requested peers from " << tf.announce << std::endl;
        //     } else {
        //         std::cerr << "Server responded '" << res.text << "'" << std::endl;
        //     }
        // }
        // std::ofstream test("out.txt");
        // test << res.text << std::endl;
        // std::cout << res.text << std::endl;

        // auto dict_response = Bencode::ParseDict(res.text); // Parse response
        auto dict_response = Bencode::ParseDictRec(res.text); // Parse response
        auto map_from_response = *(dict_response.first); // get data out of pair 
        std::string peers = std::get<std::string>(map_from_response.elements["peers"]); // get raw peers string
        int peers_position = res.text.find("peers");    // shortcuts for a correct determination of number of peers
        int peers_colon_position = res.text.find(":", peers_position);
        int len_of_peers = std::stoi(res.text.substr(peers_position + 5, peers_colon_position - peers_position - 5 ));

        // loop over all peers
        for(int k = 0; k < len_of_peers; k += 6){
            long long id = 0;
            std::string id_str;

            // Convert chars to a 4 byte hex
            for(int i = 0; i < 4; ++i){
                unsigned long long x = (unsigned char)(peers[i + k]);
                id += (x << (24 - 8 * i));
            }

            //convert each byte to it's corresponding digit
            id_str += std::to_string((id & 0xFF000000) >> 24);
            id_str += ".";
            id_str += std::to_string((id & 0x00FF0000) >> 16);
            id_str += ".";
            id_str += std::to_string((id & 0x0000FF00) >> 8);
            id_str += ".";
            id_str += std::to_string((id & 0x000000FF));
               
            // std::cout << id_str << std::endl << std::endl;

            // convert port just to hex
            int port_ = 0;
            for(int i = 4; i < 6; ++i){
                unsigned long long x = (unsigned char)(peers[i + k]);
                port_ += (x << (8 - 8 * (i - 4)));
            }

            // construct Peer from data provided
            Peer current_peer;
            current_peer.ip = id_str;
            current_peer.port = port_;
            peers_.push_back(current_peer);
        }
        
}
