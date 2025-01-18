# Simple torrent client written in c++

## CLI interface 
`./torrent-client-prototype -d <path to directory for a file> -p <percent to download> <path to torrent-file>`

## Requirements to build 
- `CMake`
- `OpenSSL`
- `libcurl`
- `CPR` 

## A brief list of functionality:

- Bencode parser for .torrent files of different content
 
- HTTP requests via Cpr requests 

- socket connectivity with tracker and peers via tcp 

- multithread download of file's pieces


