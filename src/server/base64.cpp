#include "base64.hpp"

#include <array>
#include <stdexcept>

namespace {

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Maps a character back to its 6-bit value, or -1 if it isn't in the alphabet.
std::array<int, 256> make_decode_table() {
    std::array<int, 256> table{};
    table.fill(-1);
    for (int value = 0; value < 64; ++value) {
        table[static_cast<unsigned char>(kAlphabet[value])] = value;
    }
    return table;
}

}  // namespace

std::string base64_encode(const std::vector<std::uint8_t>& bytes) {
    std::string text;
    text.reserve((bytes.size() + 2) / 3 * 4);

    // Take 3 bytes (24 bits) at a time and write them as four 6-bit characters.
    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const std::uint32_t group = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
        text += kAlphabet[(group >> 18) & 63];
        text += kAlphabet[(group >> 12) & 63];
        text += kAlphabet[(group >> 6) & 63];
        text += kAlphabet[group & 63];
    }

    // 1 or 2 bytes left over: pad the output with '=' to a multiple of 4 characters.
    const std::size_t remaining = bytes.size() - i;
    if (remaining == 1) {
        const std::uint32_t group = bytes[i] << 16;
        text += kAlphabet[(group >> 18) & 63];
        text += kAlphabet[(group >> 12) & 63];
        text += "==";
    } else if (remaining == 2) {
        const std::uint32_t group = (bytes[i] << 16) | (bytes[i + 1] << 8);
        text += kAlphabet[(group >> 18) & 63];
        text += kAlphabet[(group >> 12) & 63];
        text += kAlphabet[(group >> 6) & 63];
        text += '=';
    }
    return text;
}

std::vector<std::uint8_t> base64_decode(const std::string& text) {
    static const std::array<int, 256> decode_table = make_decode_table();

    std::vector<std::uint8_t> bytes;
    bytes.reserve(text.size() / 4 * 3);

    // Collect 6 bits per character; every time 8 or more are buffered, emit one byte.
    std::uint32_t buffer = 0;
    int buffered_bits = 0;
    for (const char character : text) {
        if (character == '=') {
            break;  // padding: no more data
        }
        const int value = decode_table[static_cast<unsigned char>(character)];
        if (value < 0) {
            throw std::invalid_argument("base64_decode: invalid character");
        }
        buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
        buffered_bits += 6;
        if (buffered_bits >= 8) {
            buffered_bits -= 8;
            bytes.push_back(static_cast<std::uint8_t>((buffer >> buffered_bits) & 0xFF));
        }
    }
    return bytes;
}
