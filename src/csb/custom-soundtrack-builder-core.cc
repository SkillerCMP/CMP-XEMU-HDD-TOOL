#include "xhc/csb_format.hh"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <vector>

namespace XemuCsb {
namespace {

constexpr size_t kRecordSize = 0x200;
constexpr size_t kSoundtrackTableOffset = 0x200;
constexpr size_t kSoundtrackSlots = 100;
constexpr size_t kSongGroupsOffset = 0xCA00;
constexpr size_t kMaxGroupsPerSoundtrack = 84;
constexpr size_t kSongsPerGroup = 6;
constexpr size_t kMaxSongs = kMaxGroupsPerSoundtrack * kSongsPerGroup;
constexpr uint32_t kSoundtrackMagic = 0x00021371;
constexpr uint32_t kSongGroupMagic = 0x00031073;

uint16_t GetU16(const std::vector<uint8_t>& buf, size_t offset) {
    return static_cast<uint16_t>(buf[offset]) | static_cast<uint16_t>(buf[offset + 1] << 8);
}
uint32_t GetU32(const std::vector<uint8_t>& buf, size_t offset) {
    return static_cast<uint32_t>(buf[offset]) | (static_cast<uint32_t>(buf[offset + 1]) << 8) |
           (static_cast<uint32_t>(buf[offset + 2]) << 16) | (static_cast<uint32_t>(buf[offset + 3]) << 24);
}
void PutU16(std::vector<uint8_t>& buf, size_t offset, uint16_t value) {
    buf[offset + 0] = static_cast<uint8_t>(value & 0xff);
    buf[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xff);
}
void PutU32(std::vector<uint8_t>& buf, size_t offset, uint32_t value) {
    buf[offset + 0] = static_cast<uint8_t>(value & 0xff);
    buf[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xff);
    buf[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xff);
    buf[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

uint64_t ReadU64(const uint8_t* p) {
    uint64_t value = 0;
    for (unsigned int i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(p[i]) << (i * 8);
    return value;
}

bool Utf8ToUtf16(const std::string& text, std::vector<uint16_t>& out, std::string& error) {
    out.clear();
    for (size_t i = 0; i < text.size();) {
        const uint8_t c0 = static_cast<uint8_t>(text[i]);
        uint32_t cp = 0;
        size_t n = 0;
        if (c0 < 0x80) {
            cp = c0;
            n = 1;
        } else if ((c0 & 0xE0) == 0xC0) {
            cp = c0 & 0x1F;
            n = 2;
        } else if ((c0 & 0xF0) == 0xE0) {
            cp = c0 & 0x0F;
            n = 3;
        } else if ((c0 & 0xF8) == 0xF0) {
            cp = c0 & 0x07;
            n = 4;
        } else {
            error = "Name is not valid UTF-8.";
            return false;
        }
        if (i + n > text.size()) {
            error = "Name is not valid UTF-8.";
            return false;
        }
        for (size_t j = 1; j < n; ++j) {
            const uint8_t cx = static_cast<uint8_t>(text[i + j]);
            if ((cx & 0xC0) != 0x80) {
                error = "Name is not valid UTF-8.";
                return false;
            }
            cp = (cp << 6) | (cx & 0x3F);
        }
        const bool overlong = (n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000);
        if (overlong || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            error = "Name is not valid UTF-8.";
            return false;
        }
        if (cp <= 0xFFFF)
            out.push_back(static_cast<uint16_t>(cp));
        else {
            cp -= 0x10000;
            out.push_back(static_cast<uint16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<uint16_t>(0xDC00 + (cp & 0x3FF)));
        }
        i += n;
    }
    return true;
}

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7F)
        out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string ReadFixedUtf16(const std::vector<uint8_t>& buf, size_t offset, size_t wchar_count) {
    std::string out;
    for (size_t i = 0; i < wchar_count && offset + i * 2 + 1 < buf.size(); ++i) {
        uint16_t w = GetU16(buf, offset + i * 2);
        if (!w)
            break;
        uint32_t cp = w;
        if (w >= 0xD800 && w <= 0xDBFF && i + 1 < wchar_count) {
            const uint16_t lo = GetU16(buf, offset + (++i) * 2);
            if (lo >= 0xDC00 && lo <= 0xDFFF)
                cp = 0x10000u + ((static_cast<uint32_t>(w - 0xD800) << 10) | (lo - 0xDC00));
            else
                cp = 0xFFFD;
        } else if (w >= 0xDC00 && w <= 0xDFFF)
            cp = 0xFFFD;
        AppendUtf8(out, cp);
    }
    return out;
}

bool PutFixedUtf16(std::vector<uint8_t>& buf, size_t offset, size_t wchar_count, const std::string& text,
                   std::string& error) {
    std::vector<uint16_t> wide;
    if (!Utf8ToUtf16(text, wide, error))
        return false;
    const size_t max_payload = wchar_count > 0 ? wchar_count - 1 : 0;
    const size_t copy_units = std::min(wide.size(), max_payload);
    for (size_t i = 0; i < copy_units; ++i)
        PutU16(buf, offset + i * 2, wide[i]);
    return true;
}

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& bytes, std::string& error) {
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    if (!in) {
        error = "Unable to open file: " + path;
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size <= 0) {
        error = "Unexpected empty media file while reading metadata: " + path;
        return false;
    }
    in.seekg(0, std::ios::beg);
    const std::streamoff read_size = std::min<std::streamoff>(size, 2 * 1024 * 1024);
    bytes.resize(static_cast<size_t>(read_size));
    in.read(reinterpret_cast<char*>(bytes.data()), read_size);
    if (!in) {
        error = "Unable to read file metadata: " + path;
        return false;
    }
    return true;
}

} // namespace

std::string XboxSoundtrackFolderName(uint16_t soundtrack_id) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned int>(soundtrack_id);
    return out.str();
}

std::string XboxSoundtrackFileName(uint16_t soundtrack_id, uint16_t song_id) {
    const uint32_t combined = (static_cast<uint32_t>(soundtrack_id) << 16) | static_cast<uint32_t>(song_id);
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << combined << ".WMA";
    return out.str();
}

bool ReadStDb(const std::vector<uint8_t>& bytes, std::vector<StDbSoundtrack>& soundtracks, std::string& error,
              StDbMetadata* metadata) {
    soundtracks.clear();
    error.clear();
    if (metadata)
        *metadata = {};
    if (bytes.size() < kSongGroupsOffset) {
        error = "ST.DB is smaller than the Xbox soundtrack header/table area.";
        return false;
    }
    if (GetU32(bytes, 0x0000) != 1) {
        error = "ST.DB header signature/version is not supported.";
        return false;
    }
    const uint32_t declared = GetU32(bytes, 0x0004);
    if (declared > kSoundtrackSlots) {
        error = "ST.DB declares more than 100 soundtracks.";
        return false;
    }
    std::vector<uint16_t> header_ids;
    header_ids.reserve(declared);
    bool header_ids_representable = true;
    for (uint32_t i = 0; i < declared; ++i) {
        const uint32_t raw = GetU32(bytes, 0x000C + static_cast<size_t>(i) * 4);
        if (raw > 0xFFFFu)
            header_ids_representable = false;
        header_ids.push_back(static_cast<uint16_t>(raw & 0xFFFFu));
    }
    uint16_t max_record_id = 0, max_song_serial = 0;
    bool have_record = false, have_song = false;
    std::vector<uint16_t> record_ids;
    std::set<uint16_t> ids;
    for (size_t slot = 0; slot < kSoundtrackSlots; ++slot) {
        const size_t s = kSoundtrackTableOffset + slot * kRecordSize;
        const uint32_t magic = GetU32(bytes, s);
        if (magic == 0)
            continue;
        if (magic != kSoundtrackMagic) {
            error = "ST.DB contains an unsupported soundtrack-table record.";
            return false;
        }
        const uint32_t raw_id = GetU32(bytes, s + 0x004);
        const uint32_t song_count = GetU32(bytes, s + 0x008);
        if (raw_id > 0xFFFF || song_count > kMaxSongs) {
            error = "ST.DB soundtrack ID/song count is outside supported Xbox limits.";
            return false;
        }
        const uint16_t id = static_cast<uint16_t>(raw_id);
        if (!ids.insert(id).second) {
            error = "ST.DB contains a duplicate soundtrack ID.";
            return false;
        }
        StDbSoundtrack soundtrack;
        soundtrack.soundtrack_id = id;
        soundtrack.name = ReadFixedUtf16(bytes, s + 0x160, 64);
        const size_t groups = (song_count + kSongsPerGroup - 1) / kSongsPerGroup;
        for (size_t group = 0; group < groups; ++group) {
            const uint32_t group_index = GetU32(bytes, s + 0x00C + group * 4);
            const uint64_t go64 =
                static_cast<uint64_t>(kSongGroupsOffset) + static_cast<uint64_t>(group_index) * kRecordSize;
            if (go64 + kRecordSize > bytes.size()) {
                error = "ST.DB soundtrack references a song group outside the file.";
                return false;
            }
            const size_t g = static_cast<size_t>(go64);
            if (GetU32(bytes, g) != kSongGroupMagic || GetU32(bytes, g + 0x004) != id) {
                error = "ST.DB song-group ownership/signature is inconsistent.";
                return false;
            }
            for (size_t item = 0; item < kSongsPerGroup && soundtrack.tracks.size() < song_count; ++item) {
                StDbTrack track;
                const uint32_t packed_song_id = GetU32(bytes, g + 0x010 + item * 4);
                const uint16_t owner = static_cast<uint16_t>(packed_song_id >> 16);
                track.song_id = static_cast<uint16_t>(packed_song_id & 0xFFFFu);
                if (owner != id) {
                    error = "ST.DB track entry belongs to the wrong soundtrack.";
                    return false;
                }
                max_song_serial = std::max(max_song_serial, track.song_id);
                have_song = true;
                track.duration_ms = GetU32(bytes, g + 0x028 + item * 4);
                track.title = ReadFixedUtf16(bytes, g + 0x040 + item * 64, 32);
                soundtrack.tracks.push_back(std::move(track));
            }
        }
        record_ids.push_back(id);
        max_record_id = std::max(max_record_id, id);
        have_record = true;
        soundtracks.push_back(std::move(soundtrack));
    }
    if (declared != soundtracks.size()) {
        error = "ST.DB declared soundtrack count does not match its table.";
        return false;
    }
    if (metadata) {
        metadata->next_soundtrack_id = GetU32(bytes, 0x0008);
        metadata->next_song_id = GetU32(bytes, 0x019C);
        metadata->header_soundtrack_ids = header_ids;
        metadata->soundtrack_id_index_matches_records = header_ids_representable && header_ids == record_ids;
        metadata->next_soundtrack_id_covers_records = !have_record || metadata->next_soundtrack_id > max_record_id;
        metadata->next_song_id_covers_records = !have_song || metadata->next_song_id > max_song_serial;
    }
    return true;
}

bool ReadStDbFile(const std::string& path, std::vector<StDbSoundtrack>& soundtracks, std::string& error,
                  StDbMetadata* metadata) {
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    if (!in) {
        error = "Unable to open ST.DB: " + path;
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
    if (bytes.empty()) {
        error = "ST.DB is empty.";
        return false;
    }
    return ReadStDb(bytes, soundtracks, error, metadata);
}

bool WriteStDb(const std::string& path, const std::vector<StDbSoundtrack>& soundtracks, std::string& error,
               const StDbWriteOptions& options) {
    error.clear();
    if (soundtracks.size() > kSoundtrackSlots) {
        error = "Xbox ST.DB supports at most 100 soundtrack records.";
        return false;
    }
    size_t total_groups = 0;
    uint16_t max_soundtrack_id = 0, max_song_id = 0;
    std::set<uint16_t> ids;
    for (const auto& soundtrack : soundtracks) {
        if (!ids.insert(soundtrack.soundtrack_id).second) {
            error = "Duplicate soundtrack ID while building ST.DB.";
            return false;
        }
        if (soundtrack.tracks.empty() || soundtrack.tracks.size() > kMaxSongs) {
            error = "Each soundtrack must contain 1 to 504 tracks.";
            return false;
        }
        total_groups += (soundtrack.tracks.size() + kSongsPerGroup - 1) / kSongsPerGroup;
        max_soundtrack_id = std::max(max_soundtrack_id, soundtrack.soundtrack_id);
        std::set<uint16_t> song_ids;
        for (const auto& track : soundtrack.tracks) {
            if (!song_ids.insert(track.song_id).second) {
                error = "Duplicate song ID inside soundtrack.";
                return false;
            }
            max_song_id = std::max(max_song_id, track.song_id);
        }
    }
    if (total_groups > std::numeric_limits<uint32_t>::max() / kRecordSize) {
        error = "ST.DB song-group table is too large.";
        return false;
    }
    const size_t total_size = kSongGroupsOffset + total_groups * kRecordSize;
    std::vector<uint8_t> db(total_size, 0);
    PutU32(db, 0x0000, 1);
    PutU32(db, 0x0004, static_cast<uint32_t>(soundtracks.size()));
    const uint32_t calculated_next_soundtrack = soundtracks.empty() ? 0 : static_cast<uint32_t>(max_soundtrack_id) + 1;
    const uint32_t calculated_next_song = soundtracks.empty() ? 0 : static_cast<uint32_t>(max_song_id) + 1;
    PutU32(db, 0x0008, std::max(calculated_next_soundtrack, options.minimum_next_soundtrack_id));
    for (size_t slot = 0; slot < soundtracks.size(); ++slot)
        PutU32(db, 0x000C + slot * 4, soundtracks[slot].soundtrack_id);
    PutU32(db, 0x019C, std::max(calculated_next_song, options.minimum_next_song_id));

    uint32_t next_group = 0;
    for (size_t slot = 0; slot < soundtracks.size(); ++slot) {
        const auto& soundtrack = soundtracks[slot];
        const size_t s = kSoundtrackTableOffset + slot * kRecordSize;
        PutU32(db, s + 0x000, kSoundtrackMagic);
        PutU32(db, s + 0x004, soundtrack.soundtrack_id);
        PutU32(db, s + 0x008, static_cast<uint32_t>(soundtrack.tracks.size()));
        const size_t groups = (soundtrack.tracks.size() + kSongsPerGroup - 1) / kSongsPerGroup;
        uint64_t total_ms = 0;
        for (const auto& track : soundtrack.tracks)
            total_ms += track.duration_ms;
        if (total_ms > std::numeric_limits<uint32_t>::max()) {
            error = "Total soundtrack duration exceeds the ST.DB 32-bit duration field.";
            return false;
        }
        PutU32(db, s + 0x15C, static_cast<uint32_t>(total_ms));
        if (!PutFixedUtf16(db, s + 0x160, 64, soundtrack.name, error))
            return false;
        for (size_t group = 0; group < groups; ++group) {
            const uint32_t group_index = next_group++;
            PutU32(db, s + 0x00C + group * 4, group_index);
            const size_t g = kSongGroupsOffset + static_cast<size_t>(group_index) * kRecordSize;
            PutU32(db, g + 0x000, kSongGroupMagic);
            PutU32(db, g + 0x004, soundtrack.soundtrack_id);
            PutU32(db, g + 0x008, static_cast<uint32_t>(group));
            PutU32(db, g + 0x00C, 1);
            for (size_t item = 0; item < kSongsPerGroup; ++item) {
                const size_t index = group * kSongsPerGroup + item;
                if (index >= soundtrack.tracks.size())
                    break;
                const auto& track = soundtrack.tracks[index];
                const uint32_t packed_song_id =
                    (static_cast<uint32_t>(soundtrack.soundtrack_id) << 16) | static_cast<uint32_t>(track.song_id);
                PutU32(db, g + 0x010 + item * 4, packed_song_id);
                PutU32(db, g + 0x028 + item * 4, track.duration_ms);
                if (!PutFixedUtf16(db, g + 0x040 + item * 64, 32, track.title, error))
                    return false;
            }
        }
    }
    std::ofstream out(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Unable to create ST.DB: " + path;
        return false;
    }
    out.write(reinterpret_cast<const char*>(db.data()), static_cast<std::streamsize>(db.size()));
    if (!out) {
        error = "Unable to finish writing ST.DB: " + path;
        return false;
    }
    return true;
}

bool WriteStDbOneSoundtrack(const std::string& path, uint16_t soundtrack_id, const std::string& soundtrack_name,
                            const std::vector<StDbTrack>& tracks, std::string& error) {
    return WriteStDb(path, {{soundtrack_id, soundtrack_name, tracks}}, error);
}

bool ReadAsfDurationMs(const std::string& path, uint32_t& duration_ms, std::string& error) {
    duration_ms = 0;
    std::vector<uint8_t> bytes;
    if (!ReadWholeFile(path, bytes, error))
        return false;

    // ASF File Properties Object GUID in on-disk little-endian byte order.
    static constexpr std::array<uint8_t, 16> kFilePropertiesGuid = {0xA1, 0xDC, 0xAB, 0x8C, 0x47, 0xA9, 0xCF, 0x11,
                                                                    0x8E, 0xE4, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65};
    for (size_t i = 0; i + 104 <= bytes.size(); ++i) {
        if (!std::equal(kFilePropertiesGuid.begin(), kFilePropertiesGuid.end(),
                        bytes.begin() + static_cast<std::ptrdiff_t>(i))) {
            continue;
        }
        const uint64_t object_size = ReadU64(&bytes[i + 16]);
        if (object_size < 104 || i + object_size > bytes.size())
            continue;
        const uint64_t play_duration_100ns = ReadU64(&bytes[i + 64]);
        const uint64_t preroll_ms = ReadU64(&bytes[i + 80]);
        uint64_t ms = play_duration_100ns / 10000ULL;
        ms = ms > preroll_ms ? ms - preroll_ms : 0;
        if (ms > std::numeric_limits<uint32_t>::max()) {
            error = "WMA duration is too large for ST.DB.";
            return false;
        }
        duration_ms = static_cast<uint32_t>(ms);
        return true;
    }
    error = "ASF File Properties object was not found in converted WMA.";
    return false;
}

bool ValidateWma2XboxProfile(const std::string& path, std::string& error) {
    std::vector<uint8_t> bytes;
    if (!ReadWholeFile(path, bytes, error))
        return false;

    // WAVEFORMATEX prefix expected from FFmpeg's wmav2 output:
    // tag 0x0161, stereo, 44100 Hz. Bitrate/block alignment may vary slightly.
    static constexpr std::array<uint8_t, 8> kProfilePrefix = {0x61, 0x01, 0x02, 0x00, 0x44, 0xAC, 0x00, 0x00};
    const auto it = std::search(bytes.begin(), bytes.end(), kProfilePrefix.begin(), kProfilePrefix.end());
    if (it == bytes.end()) {
        error = "Converted output is not WMAudio2 stereo 44.1 kHz (format tag 0x0161).";
        return false;
    }
    return true;
}

} // namespace XemuCsb
