// A small .zip extractor (stored and deflated entries, zlib inflate), enough for the pinned embeddable
// Python the Windows setup downloads (tools/installer/toolchains.json). The archive is checked against its
// pinned SHA-256 before it gets here; this still refuses entry names that would leave the destination
// folder (absolute paths, drive letters, ".."), Zip64, encryption and CRC mismatches.
#pragma once

#include <string>

// Extracts the archive held in memory into dest (created if missing; existing files are replaced).
// An entry named last_name (case-insensitive, e.g. "python.exe") is written last, so its presence means
// the extraction finished. Returns false with err set on any problem.
bool unzip_to_folder(const std::string& zip, const std::string& dest, const char* last_name, std::string& err);
