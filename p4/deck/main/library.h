// SPDX-FileCopyrightText: 2026 Dan Newcome
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Track list: rekordbox-analysed tracks on the USB stick first (they have
// beat grids), then the playable files on the SD card.
//
// The list changes while the deck runs (a stick goes in or comes out), so
// it's published as immutable snapshots. Readers hold a Ptr, and a new list
// replaces the old one whole. Strings live in PSRAM: a few hundred tracks of
// paths took a real share of internal RAM.
#include <stdint.h>
#include <memory>

namespace library {

class Snapshot {
public:
    struct Entry { uint32_t path, anlz, name; float bpm; };   // offsets into the PSRAM pool
    ~Snapshot();
    int size() const { return (int)n_; }
    bool empty() const { return n_ == 0; }
    const char* path(int i) const { return pool_ + e_[i].path; }   // absolute
    const char* anlz(int i) const { return pool_ + e_[i].anlz; }   // "" if no rekordbox analysis
    const char* name(int i) const { return pool_ + e_[i].name; }   // for the list
    float bpm(int i) const { return e_[i].bpm; }                    // grid BPM, 0 = none
    // Index of a track by path, or -1.
    int find(const char* path) const;

private:
    friend class Builder;
    Entry* e_ = nullptr;   // PSRAM
    size_t n_ = 0;
    char* pool_ = nullptr; // PSRAM
};
using Ptr = std::shared_ptr<const Snapshot>;

// Scan the SD card now (fast) and publish.
void begin(const char* sdRoot);
// A stick was mounted: read its rekordbox analysis in the background, then
// publish SD + USB.
void scanUsb(const char* usbRoot);
// The stick is going away: stop any scan in progress (waits for it) and
// publish SD only.
void dropUsb();

Ptr get();                  // the current list
uint32_t generation();      // bumps on every publish; cheap to poll

// Background scan progress, for the screen.
struct Progress { bool scanning; int found; };
Progress progress();

} // namespace library
