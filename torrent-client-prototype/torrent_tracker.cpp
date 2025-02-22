#include "torrent_tracker.h"
#include "bencode.h"
#include "byte_tools.h"
#include <cpr/cpr.h>


TorrentTracker::TorrentTracker(const std::string& url){
    l = spdlog::get("mainLogger");
    long long pos = url.find("?");
    // fix for bt.t-ru.org
    // parse pk value and set as param
    l->info("pos of ? symbol in torrentTracker constructor = {}", pos);
    if(pos != std::string::npos){
        url_ = url.substr(0, pos);
        pk = url.substr(pos + 4);
    }else {
        // general case 
        url_ = url;
        pk = "";
    }
}

const std::vector<Peer> &TorrentTracker::GetPeers() const {
    return peers_;
}


cpr::Proxies proxies{
        {"http", "socks5://127.0.0.1:20170"},  // Proxy for HTTP traffic
        {"https", "socks5://127.0.0.1:20170"}  // Proxy for HTTPS traffic
    };

void TorrentTracker::UpdatePeers(const TorrentFile& tf, std::string peerId, int port){
    l->trace("Before update peers call");
    cpr::Session session;
    cpr::Url url{url_};
    cpr::Header header{
            {"Connection", "Close"},
            {"User-Agent", "Custom-user-agent/1.0"}
        };
    cpr::Timeout timeout{2000};
    session.SetUrl(url);
    session.SetHeader(header);
    session.SetTimeout(timeout);
    session.SetProxies(proxies);
    cpr::Parameters params;
    if (!pk.empty()) {
        params.Add({
            {"pk", pk}
        });
    }
    params.Add({
                {"info_hash", tf.infoHash},
                {"peer_id", peerId},
                {"port", std::to_string(port)},
                {"uploaded", std::to_string(0)},
                {"downloaded", std::to_string(0)},
                {"left", std::to_string(tf.length)},
                {"compact", std::to_string(1)}
                });
    session.SetParameters(params);
    cpr::Response res = session.Get();
    if(res.status_code != 200){
        l->error("Update peers, hash {} \n\n {}", tf.infoHash, res.text);
        throw std::runtime_error("status code " + std::to_string(res.status_code));
    }
    l->trace("Update peers, get respsonse, status code  {}\n {}",res.status_code, res.text);

    auto dict_response = Bencode::ParseDictRec(res.text); // Parse response
    auto& map_from_response = dict_response.first->elements; // get data out of pair 
    std::string peers;
    if(map_from_response.find("failure reason") != map_from_response.end()){
        l->error("failed to update peers, error: {}", std::get<std::string>(map_from_response["failure reason"]));
        throw std::runtime_error("failed to update peers, error:" + std::get<std::string>(map_from_response["failure reason"]));
    }
    try{
        peers = std::get<std::string>(map_from_response["peers"]); // get raw peers string
    }catch(const std::exception& e){
        l->error("Torrent tracker peers not string");
        throw std::runtime_error("123123 torrent tracker failed");
    }
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