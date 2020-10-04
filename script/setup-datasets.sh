#!/bin/bash

# go to PDSHarness/script
cd "$( dirname "${BASH_SOURCE[0]}" )"
# go to PDSHarness
cd ..

mkdir -p graph_data
cd graph_data
if [ $? -ne 0 ]; then
    tput setaf 1; echo "Could not find graph_data/ directory (permission failure?)!"; tput sgr0
    exit -1
fi

echo "Downloading Orkut dataset...";
wget --no-check-certificate -q https://snap.stanford.edu/data/bigdata/communities/com-orkut.ungraph.txt.gz -O orkut.gz
if [ $? -ne 0 ]; then
    tput setaf 1; echo "Could not download Orkut dataset!"; tput sgr0
    exit -1
fi

echo "Extracting dataset...";
gunzip -f orkut.gz
if [ $? -ne 0 ]; then
    tput setaf 1; echo "Could extract 'orkut.gz'!"; tput sgr0
    exit -1
fi

echo "Checking md5 checksum...";
md5sum --quiet -c <<<"85029388d2bb72409a9a74a47f76e54f orkut"
if [ $? -ne 0 ]; then
    tput setaf 1; echo "Failed to match expected md5sum! Dataset changes or corrupted download!"; tput sgr0
    exit -1
fi

tput setaf 2; echo "Dataset downloaded and verified!"; tput sgr0
echo "Performing binary conversion process...";
find . -name "orkut-edge-list_*.txt" -print0 | xargs -0 rm -f
find . -name "orkut-edge-list_*.bin" -print0 | xargs -0 rm -f
tail -n +5 orkut > orkut-tmp
mv orkut-tmp orkut
split -d -l 4096 -a 5 --additional-suffix=.txt orkut orkut-edge-list_
cd ..
python3 ./script/dataset-txt-to-binary.py

tput setaf 2; echo "Dataset has been preprocessed!"; tput sgr0