#!/bin/bash
venv_dir = .venv
requirements = requirements.txt

.PHONY: all setup venv_install compile run clean cleanb 

all: compile run_test

setup: venv_install compile run_test

venv_install: 
	@if [ ! -d "./$(venv_dir)" ]; then \
	python3 -m venv ./$(venv_dir); \
	fi
	$(venv_dir)/bin/pip install --upgrade pip
	$(venv_dir)/bin/pip install -r ./$(requirements)

compile:
	cmake -S ./torrent-client-prototype -B ./cmake-build
	cd cmake-build && make

run_test: compile
	/bin/bash check_script.sh ./cmake-build/torrent-client-prototype > output.txt 2>&1

run: compile
	./cmake-build/torrent-client-prototype -d ~/torrent -p 100 resources/TheOHenryPrizeStories.torrent > output.txt 2>&1

py:
	/bin/bash check_script.sh

cleanb:
	rm -rf cmake-build

clean: cleanb
	rm -rf ./$(venv_dir)

