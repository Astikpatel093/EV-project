// content.h - the words and tables both front ends show: report rows, data-model tables, the
// live-day summary, strategy findings, trip itineraries and benchmark notes. Text may contain
// **bold** spans; each front end renders them its own way.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "engine.h"

namespace ev33 {

extern const int SEL_ORDER[SEL_COUNT];   // display order: DP, B&B LC, FIFO, LIFO, greedy

std::string lowerLabel(const City &c);

/* Algorithms tab: family headers and decision rows */
enum Check { CHECK_NA, CHECK_OK, CHECK_BAD, CHECK_SINGLE, CHECK_STRATEGIES };
const char *checkLabel(int c);
struct AlgoRow {
    bool family = false;            // a family header (only `decision` is set)
    std::string decision, algorithm, result;
    int check = CHECK_NA;
};
std::vector<AlgoRow> algorithmRows(const City &city, const Run *R, const Analysis &A);

/* Data model tab */
struct RecordRow { std::string what, type, holds, count, example, file; };
struct StructureRow { std::string name, from, size, what, code; };
std::vector<RecordRow> modelRecords(const Engine &E, const Run &R, const TripDetail *example_trip);
std::vector<StructureRow> modelStructures(const Engine &E, const Run &R, const Analysis &A);

/* Planner: trip summary and itinerary (time, text) */
std::string tripFacts(const Scene &sc, const TripDetail &D);
std::vector<std::pair<std::string, std::string>> tripItinerary(const Scene &sc, const TripDetail &D);

/* Live day: summary of the simulated day, comparison with the other policy and the baseline */
std::string liveSummary(Engine &E, const SimView &cur, const SimView *other, int policy);

/* Strategies: findings under the matrix */
const CompareRow *findCompare(const Compare &C, int sel, int route);
std::string strategiesFinding(const Compare &C);

/* Capacity */
std::string surgeNote(const SessV &se);
std::string flowNote(const Scene &sc, const CapV &c);

/* Benchmarks */
struct Family { const char *label; std::vector<std::string> fam; };
extern const std::vector<Family> FAMILIES;
std::string algName(const std::string &a);
bool benchSkip(const BenchRow &row);
std::vector<std::pair<std::string, std::string>> adversarialNotes(const std::vector<BenchRow> &rows);

} // namespace ev33
