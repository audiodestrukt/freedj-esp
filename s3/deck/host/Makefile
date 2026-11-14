# SPDX-FileCopyrightText: 2026 Dan Newcome
# SPDX-License-Identifier: GPL-3.0-or-later
# The round UI on the PC: make -C host  ->  host/out/*.png
UI = ../main/ui
SRC = preview.cpp $(UI)/rgfx.cpp $(UI)/round_ui.cpp $(UI)/fonts.cpp

all: out/.done

preview: $(SRC) $(UI)/*.h
	g++ -std=c++17 -O2 -Wall -Wextra -I$(UI) -o $@ $(SRC)

fake_track.bin: ../tools/fake_track.py
	python3 ../tools/fake_track.py $@

out/.done: preview fake_track.bin
	mkdir -p out
	./preview fake_track.bin
	for f in out/*.ppm; do convert $$f $${f%.ppm}.png && rm $$f; done
	touch $@

clean:
	rm -rf preview out
.PHONY: all clean
