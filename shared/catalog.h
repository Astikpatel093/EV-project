// catalog.h - the algorithm catalogue shown by the desktop app's Algorithms view and the website:
// what each algorithm does, its input and output, its cost and its role in EV33.
#pragma once

#include <vector>

namespace ev33 {

struct AlgoInfo {
    const char *name, *family, *purpose, *input, *output, *time, *space, *role, *where, *check;
    bool core;                               // used by the core EV workflow (else: analysis / teaching)
};
const std::vector<AlgoInfo> &algorithmCatalog();

} // namespace ev33
