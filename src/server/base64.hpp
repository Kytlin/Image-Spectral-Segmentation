#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Base64 turns arbitrary bytes into plain text (A-Z, a-z, 0-9, +, /), so raw pixels and labels
// can travel inside JSON strings. Every 3 bytes become 4 characters.
std::string base64_encode(const std::vector<std::uint8_t>& bytes);

// Inverse of base64_encode. Throws std::invalid_argument on characters outside the alphabet.
std::vector<std::uint8_t> base64_decode(const std::string& text);
