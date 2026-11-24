// The track table of a rekordbox export (PIONEER/rekordbox/export.pdb):
// each track's rekordbox ID beside its analysis file's path, so the deck
// can name the loaded track in its Link status by the ID a peer will look
// up in the same export.  Layout as Deep Symmetry documents it ("Database
// Exports") and rekordcrate reads it: 4 KB pages in linked lists, one per
// table; rows in a heap after a 0x28-byte page header, found through row
// groups at the page's end.  Read with stdio (main/files.cpp).
#pragma once
#include <stdint.h>
#include <functional>

namespace pdb {
// Every track row: its ID and the analysis path as stored ("/PIONEER/USBANLZ/...DAT").
// False when there is no export or it can't be read.
using TrackFn = std::function<void(uint32_t id, const char *anlz)>;
bool tracks(const char *path, const TrackFn &fn);
} // namespace pdb
