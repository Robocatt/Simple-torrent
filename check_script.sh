#!/bin/bash -x

torrent_client_binary=$1
echo "Testing program $torrent_client_binary"

percent=1
echo "First ${percent}% of pieces of the downloaded file must be downloaded correctly to pass test"

torrent_file=resources/debian-12.8.0-amd64-netinst.iso.torrent

random_dir=`mktemp -d`
trap 'rm -rf -- "$random_dir"' EXIT

downloaded_file=$random_dir/debian-12.8.0-amd64-netinst.iso

$torrent_client_binary -d $random_dir -p $percent $torrent_file
source .venv/bin/activate
python3 checksum.py -p $percent $torrent_file $downloaded_file
checksum_result=$?

exit $checksum_result

