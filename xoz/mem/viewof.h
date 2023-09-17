#pragma once

#include <span>
#include <vector>

inline std::span<const char> viewof(const std::vector<char>& datavec) { return {datavec.data(), datavec.size()}; }

inline std::span<char> viewof(std::vector<char>& datavec) { return {datavec.data(), datavec.size()}; }
