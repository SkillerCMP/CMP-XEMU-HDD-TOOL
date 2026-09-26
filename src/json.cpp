// SPDX-License-Identifier: GPL-2.0-or-later
#include "xhc/common.hpp"
#include <charconv>
#include <limits>

namespace xhc {
namespace {
class Parser {
    const std::string& s_;
    size_t p_ = 0, nodes_ = 0;
    [[noreturn]] void bad() const { throw Error("Invalid or excessive qemu-img JSON at byte " + std::to_string(p_)); }
    void ws() {
        while (p_ < s_.size() && (s_[p_] == ' ' || s_[p_] == '\n' || s_[p_] == '\r' || s_[p_] == '\t'))
            ++p_;
    }
    bool take(char c) {
        ws();
        if (p_ < s_.size() && s_[p_] == c) {
            ++p_;
            return true;
        }
        return false;
    }
    uint32_t unit() {
        uint32_t x = 0;
        for (int i = 0; i < 4; ++i) {
            if (p_ == s_.size())
                bad();
            char c = s_[p_++];
            int d = c >= '0' && c <= '9'   ? c - '0'
                    : c >= 'a' && c <= 'f' ? c - 'a' + 10
                    : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                           : -1;
            if (d < 0)
                bad();
            x = x * 16 + uint32_t(d);
        }
        return x;
    }
    void append(std::string& out, uint32_t c) {
        if (c <= 127)
            out += char(c);
        else if (c <= 2047) {
            out += char(0xc0 | (c >> 6));
            out += char(0x80 | (c & 63));
        } else if (c <= 65535) {
            out += char(0xe0 | (c >> 12));
            out += char(0x80 | ((c >> 6) & 63));
            out += char(0x80 | (c & 63));
        } else {
            out += char(0xf0 | (c >> 18));
            out += char(0x80 | ((c >> 12) & 63));
            out += char(0x80 | ((c >> 6) & 63));
            out += char(0x80 | (c & 63));
        }
    }
    std::string string() {
        if (!take('"'))
            bad();
        std::string out;
        while (p_ < s_.size()) {
            unsigned char c = static_cast<unsigned char>(s_[p_++]);
            if (c == '"')
                return out;
            if (c < 32)
                bad();
            if (c != '\\') {
                out += char(c);
                continue;
            }
            if (p_ == s_.size())
                bad();
            char e = s_[p_++];
            switch (e) {
            case '"':
            case '\\':
            case '/':
                out += e;
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u': {
                uint32_t u = unit();
                if (u >= 0xd800 && u <= 0xdbff) {
                    if (p_ + 2 > s_.size() || s_[p_++] != '\\' || s_[p_++] != 'u')
                        bad();
                    uint32_t lo = unit();
                    if (lo < 0xdc00 || lo > 0xdfff)
                        bad();
                    u = 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00);
                } else if (u >= 0xdc00 && u <= 0xdfff)
                    bad();
                append(out, u);
                break;
            }
            default:
                bad();
            }
        }
        bad();
    }
    Json value(unsigned depth) {
        if (depth > 32 || ++nodes_ > 100000)
            bad();
        ws();
        if (p_ == s_.size())
            bad();
        Json j;
        if (s_[p_] == '"') {
            j.kind = Json::Kind::String;
            j.scalar = string();
            return j;
        }
        if (take('{')) {
            j.kind = Json::Kind::Object;
            if (take('}'))
                return j;
            do {
                auto key = string();
                if (!take(':'))
                    bad();
                if (!j.object.emplace(key, value(depth + 1)).second)
                    bad();
            } while (take(','));
            if (!take('}'))
                bad();
            return j;
        }
        if (take('[')) {
            j.kind = Json::Kind::Array;
            if (take(']'))
                return j;
            do {
                j.array.push_back(value(depth + 1));
            } while (take(','));
            if (!take(']'))
                bad();
            return j;
        }
        for (const char* lit : {"true", "false", "null"}) {
            std::string w(lit);
            if (s_.compare(p_, w.size(), w) == 0) {
                p_ += w.size();
                j.kind = w == "null" ? Json::Kind::Null : Json::Kind::Boolean;
                j.scalar = w;
                return j;
            }
        }
        size_t start = p_;
        if (s_[p_] == '-')
            ++p_;
        if (p_ == s_.size())
            bad();
        if (s_[p_] == '0')
            ++p_;
        else {
            if (s_[p_] < '1' || s_[p_] > '9')
                bad();
            while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9')
                ++p_;
        }
        if (p_ < s_.size() && s_[p_] == '.') {
            ++p_;
            size_t first = p_;
            while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9')
                ++p_;
            if (first == p_)
                bad();
        }
        if (p_ < s_.size() && (s_[p_] == 'e' || s_[p_] == 'E')) {
            ++p_;
            if (p_ < s_.size() && (s_[p_] == '+' || s_[p_] == '-'))
                ++p_;
            size_t first = p_;
            while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9')
                ++p_;
            if (first == p_)
                bad();
        }
        j.kind = Json::Kind::Number;
        j.scalar = s_.substr(start, p_ - start);
        return j;
    }

public:
    explicit Parser(const std::string& s) : s_(s) {
        require(s.size() <= 8 * 1024 * 1024, "qemu-img JSON exceeds limit.");
    }
    Json parse() {
        auto j = value(0);
        ws();
        if (p_ != s_.size())
            bad();
        return j;
    }
};
}
Json Json::parse(const std::string& s) { return Parser(s).parse(); }
bool Json::contains(const std::string& k) const { return kind == Kind::Object && object.count(k) != 0; }
const Json& Json::at(const std::string& k) const {
    require(contains(k), "Missing qemu-img JSON field: " + k);
    return object.at(k);
}
std::string Json::text() const {
    require(kind == Kind::String, "Expected JSON string.");
    return scalar;
}
uint64_t Json::u64() const {
    require(kind == Kind::Number && !scalar.empty(), "Expected JSON integer.");
    uint64_t x = 0;
    auto r = std::from_chars(scalar.data(), scalar.data() + scalar.size(), x);
    require(r.ec == std::errc() && r.ptr == scalar.data() + scalar.size(), "Invalid JSON unsigned integer.");
    return x;
}
bool Json::boolean() const {
    require(kind == Kind::Boolean, "Expected JSON Boolean.");
    return scalar == "true";
}
}
