#pragma once

// Signature scanning helper: find a byte pattern (with ?? wildcards) in the game's
// executable sections. Returns the absolute VA of the first match, or 0.
// Used to re-locate hook sites in newer game builds where raw offsets drift.
namespace Scan
{
    inline std::vector<std::uint8_t> Parse(std::string_view a_pattern, std::vector<bool>& a_mask)
    {
        std::vector<std::uint8_t> bytes;
        std::istringstream        iss{ std::string(a_pattern) };
        std::string               tok;
        while (iss >> tok) {
            if (tok == "??" || tok == "?") {
                bytes.push_back(0);
                a_mask.push_back(false);
            } else {
                bytes.push_back(static_cast<std::uint8_t>(std::stoul(tok, nullptr, 16)));
                a_mask.push_back(true);
            }
        }
        return bytes;
    }

    inline std::uintptr_t FindInSegment(REL::Module::Segment::Name a_seg, std::string_view a_pattern)
    {
        std::vector<bool> mask;
        const auto        bytes = Parse(a_pattern, mask);
        if (bytes.empty()) {
            return 0;
        }

        const auto& module = REL::Module::get();
        const auto  segment = module.segment(a_seg);
        const auto* begin = static_cast<const std::uint8_t*>(segment.pointer());
        const auto  size = segment.size();
        const auto  n = bytes.size();
        if (!begin || size < n) {
            return 0;
        }

        for (std::size_t i = 0; i <= size - n; ++i) {
            bool match = true;
            for (std::size_t j = 0; j < n; ++j) {
                if (mask[j] && begin[i + j] != bytes[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return reinterpret_cast<std::uintptr_t>(begin + i);
            }
        }
        return 0;
    }

    inline std::uintptr_t Find(std::string_view a_pattern)
    {
        auto va = FindInSegment(REL::Module::Segment::textx, a_pattern);
        if (va) {
            return va;
        }
        return FindInSegment(REL::Module::Segment::textw, a_pattern);
    }

    // Count matches to verify a signature is unique.
    inline std::size_t CountMatches(REL::Module::Segment::Name a_seg, std::string_view a_pattern)
    {
        std::vector<bool> mask;
        const auto        bytes = Parse(a_pattern, mask);
        const auto&       module = REL::Module::get();
        const auto        segment = module.segment(a_seg);
        const auto*       begin = static_cast<const std::uint8_t*>(segment.pointer());
        const auto        size = segment.size();
        const auto        n = bytes.size();
        std::size_t       count = 0;
        if (!begin || size < n) {
            return 0;
        }
        for (std::size_t i = 0; i <= size - n; ++i) {
            bool match = true;
            for (std::size_t j = 0; j < n; ++j) {
                if (mask[j] && begin[i + j] != bytes[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                ++count;
            }
        }
        return count;
    }
}
