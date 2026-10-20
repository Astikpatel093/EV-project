// demos.hpp - educational demonstrations: every algorithm on a small textbook example whose
// answer can be checked by hand. Kept separate from the planning workflow on purpose: several
// syllabus algorithms (N-Queens, Strassen, matrix-chain order, optimal BST, CRT, ...) are not
// needed to plan EV trips, so they are shown here on their classic inputs instead of being
// forced into the EV model.
#pragma once

#include <ostream>

namespace ev {

// Prints each demonstration with its expected answer and whether the implementation matched.
// Returns the number of demonstrations whose result differed from the expected answer (0 = all ok).
int run_algorithm_demos(std::ostream &out);

} // namespace ev
