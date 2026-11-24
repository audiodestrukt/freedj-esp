// The stick, served read-only over NFSv2 on UDP the way a Pro DJ Link
// player serves its USB: portmap on UDP 111, mountd v1 and nfsd v2 (both on
// UDP 2049), the stick exported as "/C/", names on the wire in UTF-16LE.
// A peer that has our status's track ID reads our export.pdb, finds the
// track's path in it, and reads the file from here (freedj-3000's FOLLOW and
// LINK loads do).  RFC 1057 (RPC), RFC 1094 (NFS v2 and mount v1).
#pragma once

namespace nfsd {
void begin();       // a cooperative task: call after fat::mount()
} // namespace nfsd
