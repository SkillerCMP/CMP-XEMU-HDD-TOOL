#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XemuCsb {

struct StDbTrack {
    uint16_t song_id = 0;
    uint32_t duration_ms = 0;
    std::string title;
};

struct StDbSoundtrack {
    uint16_t soundtrack_id = 0;
    std::string name;
    std::vector<StDbTrack> tracks;
};

// Header state maintained independently of the soundtrack records.
// Preserve these values so the dashboard's monotonic allocators never move backwards.
struct StDbMetadata {
    uint32_t next_soundtrack_id = 0;
    uint32_t next_song_id = 0;
    std::vector<uint16_t> header_soundtrack_ids;
    bool soundtrack_id_index_matches_records = true;
    bool next_soundtrack_id_covers_records = true;
    bool next_song_id_covers_records = true;
};

struct StDbWriteOptions {
    uint32_t minimum_next_soundtrack_id = 0;
    uint32_t minimum_next_song_id = 0;
};

std::string XboxSoundtrackFolderName(uint16_t soundtrack_id);
std::string XboxSoundtrackFileName(uint16_t soundtrack_id, uint16_t song_id);

bool ReadStDb(const std::vector<uint8_t>& bytes, std::vector<StDbSoundtrack>& soundtracks, std::string& error,
              StDbMetadata* metadata = nullptr);
bool ReadStDbFile(const std::string& path, std::vector<StDbSoundtrack>& soundtracks, std::string& error,
                  StDbMetadata* metadata = nullptr);
bool WriteStDb(const std::string& path, const std::vector<StDbSoundtrack>& soundtracks, std::string& error,
               const StDbWriteOptions& options = StDbWriteOptions{});

bool WriteStDbOneSoundtrack(const std::string& path, uint16_t soundtrack_id, const std::string& soundtrack_name,
                            const std::vector<StDbTrack>& tracks, std::string& error);

bool ReadAsfDurationMs(const std::string& path, uint32_t& duration_ms, std::string& error);

bool ValidateWma2XboxProfile(const std::string& path, std::string& error);

} // namespace XemuCsb
