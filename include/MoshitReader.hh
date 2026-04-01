#ifndef MOSHIT_READER_HH
#define MOSHIT_READER_HH

#include "MoshitWriter.hh"
#include <string>
#include <vector>

struct MoshitFile {
    MoshitFileHeader header{};
    std::vector<MoshitFileHit> hits;
};

class MoshitReader {
public:
    static MoshitFile Read(const std::string& filename);
};

#endif
