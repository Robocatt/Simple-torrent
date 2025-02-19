# Simple torrent client written in c++
A minimal multi-threaded torrent client written in modern C++. This project showcases how to parse and download files using the BitTorrent protocol. It includes a custom bencode parser, tracker communication, peer connections, and file-piece management with optional integrity checking.

## Features

    Bencode Parser
        Fully parses .torrent files of various contents (single-file and multi-file).

    Custom TCP Peer Connections
        Establishes direct socket connections to peers.

    Tracker Communication
        Uses HTTP requests (via cpr) to contact the tracker.

    Piece-Based Downloading
        Supports multi-threaded piece requests to maximize download throughput.

    Partial Download & File Selection
        Lets you specify a percentage (-p) of the total file(s) to download.
        Allows selecting specific files from a multi-file torrent.

    Integrity Checking (Optional)
        Verifies SHA1 hashes of downloaded pieces.

    Rich Logging
        Console and file-based logging using spdlog.
        Adjustable log levels (trace, debug, info, warn, error, critical).

## Preview & Highlights
<details> <summary>Console Preview (click to expand)</summary>

> ./torrent-client-prototype -log-level debug -d ~/torrents -p 50 /path/to/file.torrent

[ Download Progress ]
Downloaded: 524288 / 1048576 bytes (50.00%)

All downloaded pieces have correct hash.

    The client starts, connects to trackers, finds peers, and begins downloading in pieces.
    Real-time progress is displayed in the console.
    Logging at debug level also writes verbose output into Logs/debug.log.

</details>

## Usage

After building, you can run the client with:

./torrent-client-prototype \
   -log-level <LOG_LEVEL>  \
   -d <DOWNLOAD_DIRECTORY> \
   -p <PERCENT_TO_DOWNLOAD> \
   [-no-check]             \
   <PATH_TO_TORRENT_FILE>

Command-Line Options

    -log-level <LEVEL>
    Set the verbosity of logging. Options: trace, debug, info, warn, error, critical.
    Default: error.

    -d <DOWNLOAD_DIRECTORY>
    Directory where downloaded files will be saved.
    Default: ~/Downloads.

    -p <PERCENT_TO_DOWNLOAD>
    Indicates how much of the torrent should be downloaded, as an integer percent [1..100].
    Default: 100 (the entire content).

    -no-check
    Skip SHA1 hash verification of downloaded pieces.

    <PATH_TO_TORRENT_FILE>
    Path to a .torrent file.

## Dependencies
-   CMake (version ≥ 3.16)
-   OpenSSL
-   libcurl (used by cpr under the hood)
-   [cpr](https://github.com/libcpr/cpr) (HTTP client library)
-   [spdlog](https://github.com/gabime/spdlog) (logging library)

    >The CMakeLists.txt fetches and builds cpr and spdlog automatically via FetchContent, so you generally only need to ensure you have OpenSSL and libcurl development packages installed.




