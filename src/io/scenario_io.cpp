// scenario_io.cpp - scenario directories: strict loading and deterministic writing.
#include "ev/io/scenario_io.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#include "ev/core/errors.hpp"
#include "ev/io/csv.hpp"

namespace ev {

// ======================================================================== files
std::optional<std::string> read_text_file(const std::string &path) {
    std::FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return std::nullopt;
    std::string s;
    char buf[65536];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

void write_text_file(const std::string &path, const std::string &text) {
    std::FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("cannot write " + path);
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = std::fclose(f) == 0 && ok;
    if (!ok) throw std::runtime_error("error while writing " + path);
}

void make_directories(const std::string &path) {
    std::string cur;
    for (std::size_t i = 0; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/' || path[i] == '\\') {
            if (!cur.empty() && cur != "." && !(cur.size() == 2 && cur[1] == ':')) {
#ifdef _WIN32
                _mkdir(cur.c_str());
#else
                mkdir(cur.c_str(), 0755);
#endif
            }
        }
        if (i < path.size()) cur.push_back(path[i]);
    }
    struct stat st;
    if (stat(path.c_str(), &st) != 0) throw std::runtime_error("cannot create directory " + path);
}

// ======================================================================== config
static std::string trim(const std::string &s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) b++;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) e--;
    return s.substr(b, e - b);
}

ScenarioConfig parse_config(const std::string &text, const std::string &file) {
    ScenarioConfig c;
    std::set<std::string> seen;
    std::size_t pos = 0;
    int lineno = 0;
    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        std::string line = trim(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
        lineno++;
        if (line.empty() || line[0] == '#') continue;
        std::size_t eq = line.find('=');
        if (eq == std::string::npos) throw DataError(file, lineno, "expected 'key = value'");
        std::string key = trim(line.substr(0, eq)), val = trim(line.substr(eq + 1));
        if (!seen.insert(key).second) throw DataError(file, lineno, "key '" + key + "' appears twice");
        auto num = [&](double &out) {
            if (!parse_double(val, out)) throw DataError(file, lineno, "'" + key + "' must be a number, got '" + val + "'");
        };
        auto integer = [&](int &out) {
            std::int64_t v;
            if (!parse_int64(val, v) || v < -1000000000 || v > 1000000000)
                throw DataError(file, lineno, "'" + key + "' must be a whole number, got '" + val + "'");
            out = static_cast<int>(v);
        };
        if (key == "name") c.name = val;
        else if (key == "data_source") {
            try { c.source = parse_data_source(val); } catch (const std::invalid_argument &e) { throw DataError(file, lineno, e.what()); }
        } else if (key == "source_note") c.source_note = val;
        else if (key == "budget_inr") {
            try { c.budget = parse_rupees(val); } catch (const std::invalid_argument &e) { throw DataError(file, lineno, std::string("budget_inr: ") + e.what()); }
        } else if (key == "target_soc") num(c.target_soc);
        else if (key == "regen_efficiency") num(c.regen_efficiency);
        else if (key == "climb_kwh_per_m") num(c.climb_kwh_per_m);
        else if (key == "reference_consumption") num(c.reference_consumption);
        else if (key == "speed_kmh") num(c.speed_kmh);
        else if (key == "battery_step_kwh") num(c.battery_step_kwh);
        else if (key == "max_detour_km") num(c.max_detour_km);
        else if (key == "grid_kw") num(c.grid_kw);
        else if (key == "window_min") integer(c.window_min);
        else if (key == "session_min") integer(c.session_min);
        else if (key == "horizon_h") integer(c.horizon_h);
        else if (key == "min_session_min") num(c.min_session_min);
        else throw DataError(file, lineno, "unknown key '" + key + "'");
    }
    try {
        validate(c);
    } catch (const std::invalid_argument &e) {
        throw DataError(file, 0, e.what());
    }
    return c;
}

static std::string fmt_num(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.10g", v);
    return buf;
}

static std::string fmt_rupees(Paise p) {
    std::string s = std::to_string(p / 100);
    if (p % 100) {
        char buf[8];
        std::snprintf(buf, sizeof buf, ".%02lld", static_cast<long long>(p % 100));
        s += buf;
    }
    return s;
}

std::string format_config(const ScenarioConfig &c) {
    std::string s;
    s += "name = " + c.name + "\n";
    s += "data_source = " + std::string(to_string(c.source)) + "\n";
    if (!c.source_note.empty()) s += "source_note = " + c.source_note + "\n";
    s += "budget_inr = " + fmt_rupees(c.budget) + "\n";
    s += "target_soc = " + fmt_num(c.target_soc) + "\n";
    s += "regen_efficiency = " + fmt_num(c.regen_efficiency) + "\n";
    s += "climb_kwh_per_m = " + fmt_num(c.climb_kwh_per_m) + "\n";
    s += "reference_consumption = " + fmt_num(c.reference_consumption) + "\n";
    s += "speed_kmh = " + fmt_num(c.speed_kmh) + "\n";
    s += "battery_step_kwh = " + fmt_num(c.battery_step_kwh) + "\n";
    s += "max_detour_km = " + fmt_num(c.max_detour_km) + "\n";
    s += "grid_kw = " + fmt_num(c.grid_kw) + "\n";
    s += "window_min = " + std::to_string(c.window_min) + "\n";
    s += "session_min = " + std::to_string(c.session_min) + "\n";
    s += "horizon_h = " + std::to_string(c.horizon_h) + "\n";
    s += "min_session_min = " + fmt_num(c.min_session_min) + "\n";
    return s;
}

// ======================================================================== loading
namespace {

// Runs `f`, turning std::invalid_argument from the model into a DataError at this row.
template <class F>
void at_row(const CsvTable &t, std::size_t r, F f) {
    try {
        f();
    } catch (const std::invalid_argument &e) {
        t.fail(r, e.what());
    }
}

int to_int(const CsvTable &t, std::size_t r, const std::string &col, std::int64_t v) {
    if (v < -2147483647LL || v > 2147483647LL) t.fail(r, "'" + col + "' is out of range");
    return static_cast<int>(v);
}

void load_nodes(const CsvTable &t, RoadNetwork &net) {
    for (std::size_t r = 0; r < t.rows(); r++) {
        RoadNode n;
        n.id = to_int(t, r, "id", t.integer(r, "id"));
        auto lat = t.opt_number(r, "lat"), lon = t.opt_number(r, "lon");
        if (lat.has_value() != lon.has_value()) t.fail(r, "give both lat and lon, or neither");
        if (lat) n.location = GeoPoint{*lat, *lon};
        n.elevation_m = t.opt_number(r, "elev_m").value_or(0.0);
        n.type = t.text(r, "type");
        n.name = t.text(r, "name");
        at_row(t, r, [&] { net.add_node(n); });
    }
}

void load_roads(const CsvTable &t, RoadNetwork &net) {
    for (std::size_t r = 0; r < t.rows(); r++) {
        RoadEdge e;
        e.id = to_int(t, r, "id", t.integer(r, "id"));
        e.from = to_int(t, r, "from", t.integer(r, "from"));
        e.to = to_int(t, r, "to", t.integer(r, "to"));
        e.distance_km = t.number(r, "distance_km");
        e.travel_time_min = t.opt_number(r, "travel_time_min");
        e.energy_kwh = t.opt_number(r, "energy_kwh");
        at_row(t, r, [&] { net.add_edge(e); });
    }
}

void load_stations(const CsvTable &t, Scenario &s, std::unordered_set<int> &site_ids) {
    for (std::size_t r = 0; r < t.rows(); r++) {
        ChargingStation st;
        st.id = to_int(t, r, "id", t.integer(r, "id"));
        st.node = to_int(t, r, "node", t.integer(r, "node"));
        st.power_kw = t.number(r, "power_kw");
        st.ports = to_int(t, r, "ports", t.integer(r, "ports"));
        st.name = t.text(r, "name");
        st.connector = t.text(r, "connector");
        if (!t.text(r, "status").empty()) at_row(t, r, [&] { st.status = parse_station_status(t.text(r, "status")); });
        if (!t.text(r, "install_cost_inr").empty()) st.install_cost = t.paise(r, "install_cost_inr");
        st.maint_period_h = to_int(t, r, "maint_period_h", t.opt_integer(r, "maint_period_h").value_or(0));
        st.maint_offset_h = to_int(t, r, "maint_offset_h", t.opt_integer(r, "maint_offset_h").value_or(0));
        at_row(t, r, [&] { validate(st); });
        if (!s.network.has_node(st.node)) t.fail(r, "station " + std::to_string(st.id) + " is at unknown node " + std::to_string(st.node));
        if (!site_ids.insert(st.id).second) t.fail(r, "duplicate station id " + std::to_string(st.id));
        s.stations.push_back(st);
    }
}

void load_candidates(const CsvTable &t, Scenario &s, std::unordered_set<int> &site_ids) {
    for (std::size_t r = 0; r < t.rows(); r++) {
        CandidateStation c;
        c.id = to_int(t, r, "id", t.integer(r, "id"));
        c.node = to_int(t, r, "node", t.integer(r, "node"));
        c.install_cost = t.paise(r, "install_cost_inr");
        c.power_kw = t.number(r, "power_kw");
        c.ports = to_int(t, r, "ports", t.integer(r, "ports"));
        c.name = t.text(r, "name");
        c.benefit = t.opt_integer(r, "benefit");
        c.suitability = t.opt_number(r, "suitability");
        c.connector = t.text(r, "connector");
        c.maint_period_h = to_int(t, r, "maint_period_h", t.opt_integer(r, "maint_period_h").value_or(0));
        c.maint_offset_h = to_int(t, r, "maint_offset_h", t.opt_integer(r, "maint_offset_h").value_or(0));
        at_row(t, r, [&] { validate(c); });
        if (!s.network.has_node(c.node)) t.fail(r, "candidate " + std::to_string(c.id) + " is at unknown node " + std::to_string(c.node));
        if (!site_ids.insert(c.id).second)
            t.fail(r, "candidate id " + std::to_string(c.id) + " is already used by another station or candidate");
        s.candidates.push_back(c);
    }
}

void load_vehicles(const CsvTable &t, Scenario &s) {
    std::unordered_set<std::string> ids;
    for (std::size_t r = 0; r < t.rows(); r++) {
        ElectricVehicle v;
        v.id = t.text(r, "id");
        v.battery_kwh = t.number(r, "battery_kwh");
        v.charge_kwh = t.number(r, "charge_kwh");
        v.consumption_kwh_per_km = t.number(r, "consumption_kwh_per_km");
        v.reserve_kwh = t.number(r, "reserve_kwh");
        v.model = t.text(r, "model");
        v.connector = t.text(r, "connector");
        v.max_charge_kw = t.opt_number(r, "max_charge_kw").value_or(0.0);
        at_row(t, r, [&] { validate(v); });
        if (!ids.insert(v.id).second) t.fail(r, "duplicate vehicle id " + v.id);
        s.vehicles.push_back(v);
    }
}

void load_trips(const CsvTable &t, Scenario &s) {
    std::unordered_set<int> ids;
    for (std::size_t r = 0; r < t.rows(); r++) {
        TripRequest q;
        q.id = to_int(t, r, "id", t.integer(r, "id"));
        q.vehicle_id = t.text(r, "vehicle");
        q.from = to_int(t, r, "from", t.integer(r, "from"));
        q.to = to_int(t, r, "to", t.integer(r, "to"));
        q.depart_min = t.opt_number(r, "depart_min").value_or(0.0);
        q.start_charge_kwh = t.opt_number(r, "start_charge_kwh");
        if (!ids.insert(q.id).second) t.fail(r, "duplicate trip id " + std::to_string(q.id));
        int vi = s.vehicle_index(q.vehicle_id);
        if (vi < 0) t.fail(r, "trip " + std::to_string(q.id) + " uses unknown vehicle '" + q.vehicle_id + "'");
        if (!s.network.has_node(q.from)) t.fail(r, "trip " + std::to_string(q.id) + " starts at unknown node " + std::to_string(q.from));
        if (!s.network.has_node(q.to)) t.fail(r, "trip " + std::to_string(q.id) + " ends at unknown node " + std::to_string(q.to));
        if (q.depart_min < 0) t.fail(r, "departure time must be >= 0");
        if (q.start_charge_kwh) {
            if (*q.start_charge_kwh < 0) t.fail(r, "start charge must not be negative");
            if (*q.start_charge_kwh > s.vehicles[static_cast<std::size_t>(vi)].battery_kwh)
                t.fail(r, "start charge exceeds the battery capacity of vehicle " + q.vehicle_id);
        }
        s.trips.push_back(q);
    }
}

void load_sessions(const CsvTable &t, Scenario &s) {
    std::unordered_set<int> ids;
    for (std::size_t r = 0; r < t.rows(); r++) {
        ChargingSession x;
        x.id = to_int(t, r, "id", t.integer(r, "id"));
        x.station_id = to_int(t, r, "station", t.integer(r, "station"));
        x.start_min = t.number(r, "start_min");
        x.end_min = t.number(r, "end_min");
        if (!ids.insert(x.id).second) t.fail(r, "duplicate session id " + std::to_string(x.id));
        if (s.station_index(x.station_id) < 0 && s.candidate_index(x.station_id) < 0)
            t.fail(r, "session " + std::to_string(x.id) + " refers to unknown station " + std::to_string(x.station_id));
        if (x.start_min < 0) t.fail(r, "start time must be >= 0");
        if (!(x.end_min > x.start_min)) t.fail(r, "session must end after it starts");
        s.sessions.push_back(x);
    }
}

} // namespace

Scenario parse_scenario(const ScenarioTexts &in) {
    const std::string L = in.label + "/";
    if (!in.config) throw DataError(L + "scenario.cfg", 0, "file not found");
    if (!in.nodes) throw DataError(L + "nodes.csv", 0, "file not found");
    if (!in.roads) throw DataError(L + "roads.csv", 0, "file not found");
    Scenario s;
    s.config = parse_config(*in.config, L + "scenario.cfg");
    load_nodes(CsvTable::parse(*in.nodes, L + "nodes.csv", {"id"}, {"lat", "lon", "elev_m", "type", "name"}), s.network);
    if (s.network.node_count() == 0) throw DataError(L + "nodes.csv", 0, "the network has no nodes");
    load_roads(CsvTable::parse(*in.roads, L + "roads.csv", {"id", "from", "to", "distance_km"}, {"travel_time_min", "energy_kwh"}),
               s.network);
    std::unordered_set<int> site_ids;
    if (in.stations)
        load_stations(CsvTable::parse(*in.stations, L + "stations.csv", {"id", "node", "power_kw", "ports"},
                                      {"name", "connector", "status", "install_cost_inr", "maint_period_h", "maint_offset_h"}),
                      s, site_ids);
    if (in.candidates)
        load_candidates(CsvTable::parse(*in.candidates, L + "candidates.csv", {"id", "node", "install_cost_inr", "power_kw", "ports"},
                                        {"name", "benefit", "suitability", "connector", "maint_period_h", "maint_offset_h"}),
                        s, site_ids);
    if (in.vehicles)
        load_vehicles(CsvTable::parse(*in.vehicles, L + "vehicles.csv", {"id", "battery_kwh", "charge_kwh", "consumption_kwh_per_km", "reserve_kwh"},
                                      {"model", "connector", "max_charge_kw"}),
                      s);
    if (in.trips)
        load_trips(CsvTable::parse(*in.trips, L + "trips.csv", {"id", "vehicle", "from", "to"}, {"depart_min", "start_charge_kwh"}), s);
    if (in.sessions)
        load_sessions(CsvTable::parse(*in.sessions, L + "sessions.csv", {"id", "station", "start_min", "end_min"}, {}), s);
    if (in.events) {
        for (char c : *in.events)
            if (c != '\n' && c != '\r') s.events.push_back(c);
    }
    s.validate();   // belt and braces: the per-row checks above already cover every rule
    return s;
}

ScenarioTexts read_scenario_texts(const std::string &dir) {
    ScenarioTexts t;
    t.label = dir;
    auto rd = [&](const char *name) { return read_text_file(dir + "/" + name); };
    t.config = rd("scenario.cfg");
    t.nodes = rd("nodes.csv");
    t.roads = rd("roads.csv");
    t.stations = rd("stations.csv");
    t.candidates = rd("candidates.csv");
    t.vehicles = rd("vehicles.csv");
    t.trips = rd("trips.csv");
    t.sessions = rd("sessions.csv");
    t.events = rd("events.txt");
    return t;
}

Scenario load_scenario(const std::string &dir) { return parse_scenario(read_scenario_texts(dir)); }

// ======================================================================== writing
static std::string header(const std::vector<std::string> &comment) {
    std::string h;
    for (const std::string &c : comment) h += "# " + c + "\n";
    return h;
}

void write_scenario(const Scenario &s, const std::string &dir, const std::vector<std::string> &comment) {
    make_directories(dir);
    std::string cfg = header(comment) + format_config(s.config);
    write_text_file(dir + "/scenario.cfg", cfg);

    std::string t = header(comment) + "id,lat,lon,elev_m,type,name\n";
    char buf[256];
    for (const RoadNode &n : s.network.nodes()) {
        if (n.location) std::snprintf(buf, sizeof buf, "%d,%.6f,%.6f,%.1f,", n.id, n.location->lat, n.location->lon, n.elevation_m);
        else std::snprintf(buf, sizeof buf, "%d,,,%.1f,", n.id, n.elevation_m);
        t += buf + csv_escape(n.type) + "," + csv_escape(n.name) + "\n";
    }
    write_text_file(dir + "/nodes.csv", t);

    t = header(comment) + "id,from,to,distance_km,travel_time_min,energy_kwh\n";
    for (const RoadEdge &e : s.network.edges()) {
        std::snprintf(buf, sizeof buf, "%d,%d,%d,%.3f,", e.id, e.from, e.to, e.distance_km);
        t += buf;
        if (e.travel_time_min) { std::snprintf(buf, sizeof buf, "%.2f", *e.travel_time_min); t += buf; }
        t += ",";
        if (e.energy_kwh) { std::snprintf(buf, sizeof buf, "%.4f", *e.energy_kwh); t += buf; }
        t += "\n";
    }
    write_text_file(dir + "/roads.csv", t);

    t = header(comment) + "id,name,node,power_kw,ports,connector,status,install_cost_inr,maint_period_h,maint_offset_h\n";
    for (const ChargingStation &x : s.stations) {
        std::snprintf(buf, sizeof buf, "%d,", x.id);
        t += buf + csv_escape(x.name);
        std::snprintf(buf, sizeof buf, ",%d,%.1f,%d,", x.node, x.power_kw, x.ports);
        t += buf + csv_escape(x.connector) + "," + to_string(x.status) + "," + fmt_rupees(x.install_cost);
        std::snprintf(buf, sizeof buf, ",%d,%d\n", x.maint_period_h, x.maint_offset_h);
        t += buf;
    }
    write_text_file(dir + "/stations.csv", t);

    t = header(comment) + "id,name,node,install_cost_inr,benefit,suitability,power_kw,ports,connector,maint_period_h,maint_offset_h\n";
    for (const CandidateStation &x : s.candidates) {
        std::snprintf(buf, sizeof buf, "%d,", x.id);
        t += buf + csv_escape(x.name);
        std::snprintf(buf, sizeof buf, ",%d,", x.node);
        t += buf + fmt_rupees(x.install_cost) + ",";
        if (x.benefit) t += std::to_string(*x.benefit);
        t += ",";
        if (x.suitability) { std::snprintf(buf, sizeof buf, "%.3f", *x.suitability); t += buf; }
        std::snprintf(buf, sizeof buf, ",%.1f,%d,", x.power_kw, x.ports);
        t += buf + csv_escape(x.connector);
        std::snprintf(buf, sizeof buf, ",%d,%d\n", x.maint_period_h, x.maint_offset_h);
        t += buf;
    }
    write_text_file(dir + "/candidates.csv", t);

    t = header(comment) + "id,model,battery_kwh,charge_kwh,consumption_kwh_per_km,reserve_kwh,connector,max_charge_kw\n";
    for (const ElectricVehicle &v : s.vehicles) {
        t += csv_escape(v.id) + "," + csv_escape(v.model);
        std::snprintf(buf, sizeof buf, ",%.2f,%.2f,%.4f,%.2f,", v.battery_kwh, v.charge_kwh, v.consumption_kwh_per_km, v.reserve_kwh);
        t += buf + csv_escape(v.connector);
        std::snprintf(buf, sizeof buf, ",%.1f\n", v.max_charge_kw);
        t += buf;
    }
    write_text_file(dir + "/vehicles.csv", t);

    t = header(comment) + "id,vehicle,from,to,depart_min,start_charge_kwh\n";
    for (const TripRequest &q : s.trips) {
        std::snprintf(buf, sizeof buf, "%d,", q.id);
        t += buf + csv_escape(q.vehicle_id);
        std::snprintf(buf, sizeof buf, ",%d,%d,%.0f,", q.from, q.to, q.depart_min);
        t += buf;
        if (q.start_charge_kwh) { std::snprintf(buf, sizeof buf, "%.2f", *q.start_charge_kwh); t += buf; }
        t += "\n";
    }
    write_text_file(dir + "/trips.csv", t);

    t = header(comment) + "id,station,start_min,end_min\n";
    for (const ChargingSession &x : s.sessions) {
        std::snprintf(buf, sizeof buf, "%d,%d,%.1f,%.1f\n", x.id, x.station_id, x.start_min, x.end_min);
        t += buf;
    }
    write_text_file(dir + "/sessions.csv", t);

    if (!s.events.empty()) {
        std::string ev;
        for (std::size_t i = 0; i < s.events.size(); i += 100) ev += s.events.substr(i, 100) + "\n";
        write_text_file(dir + "/events.txt", ev);
    }
}

} // namespace ev
