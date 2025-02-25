// A bunch of small utility functions for the project

#pragma once

#include <chrono>
#include <ctime>
#include <iostream>

using microseconds = std::chrono::microseconds;
using milliseconds = std::chrono::milliseconds;
using highResClock = std::chrono::high_resolution_clock;

namespace utils {
inline void sep() {
    std::cout << "---------------------------------\n";
}

inline void logWarning(const std::string& message) {
    std::cerr << "- WARN: " << message << "!\n";
}

inline void logWarning(const std::string& message, bool execute) {
    if (execute) {
        std::cerr << "- WARN: " << message << "!\n";
    }
}

inline auto now() {
    return highResClock::now();
}

template <typename DurType>
DurType duration(const std::chrono::time_point<highResClock>& start) {
    auto end = highResClock::now();
    return std::chrono::duration_cast<DurType>(end - start);
}

template <typename DurType>
inline std::string durationString(const DurType& duration) {
    static_assert(std::is_same_v<DurType, microseconds> || std::is_same_v<DurType, milliseconds>, "Invalid duration type");
    std::string length = std::to_string(duration.count());

    if constexpr (std::is_same_v<DurType, microseconds>) {
        return length + " microseconds";
    } else if constexpr (std::is_same_v<DurType, milliseconds>) {
        return length + " milliseconds";
    }
}

template <typename DurType>
void printDuration(const DurType& duration) {
    std::cout << "Time: " << durationString(duration) << "\n";
}

template <typename Type>
inline void combineHash(size_t& seed, const Type& v) {
    seed ^= std::hash<Type>()(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

template <typename Type>
inline size_t combineHashes(const Type& hash1, const Type& hash2) {
    return hash1 ^ (hash2 + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2));
}
};  // namespace utils
