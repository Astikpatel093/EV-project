// Unit tests: money parsing, CSV reading, model validation.
#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "ev/core/errors.hpp"
#include "ev/core/money.hpp"
#include "ev/io/csv.hpp"
#include "ev/models/road_network.hpp"
#include "ev/models/station.hpp"
#include "ev/models/vehicle.hpp"

using namespace ev;

TEST(Money, ParsesRupeesExactlyIntoPaise) {
    EXPECT_EQ(parse_rupees("8700000"), 870000000);
    EXPECT_EQ(parse_rupees("12.5"), 1250);
    EXPECT_EQ(parse_rupees("12.05"), 1205);
    EXPECT_EQ(parse_rupees(" 0.10 "), 10);
    EXPECT_EQ(parse_rupees("0"), 0);
    // 0.1 + 0.2 style rounding problems cannot happen: the total is an exact integer
    EXPECT_EQ(parse_rupees("0.10") + parse_rupees("0.20"), parse_rupees("0.30"));
}

TEST(Money, RejectsMalformedAmounts) {
    for (const char *bad : {"", "-5", "+5", "1.234", "12a", "abc", "1,000", ".", "1e5"})
        EXPECT_THROW(parse_rupees(bad), std::invalid_argument) << bad;
    EXPECT_THROW(parse_rupees("100000000000000000"), std::invalid_argument);   // absurdly large
}

TEST(Money, IndianFormatting) {
    EXPECT_EQ(format_inr(parse_rupees("12345678")), "INR 1,23,45,678");
    EXPECT_EQ(format_inr(parse_rupees("999")), "INR 999");
    EXPECT_EQ(format_inr(parse_rupees("1000.5")), "INR 1,000.50");
    EXPECT_EQ(format_inr_short(87 * kPaisePerLakh), "87 L");
    EXPECT_EQ(format_inr_short(125 * kPaisePerLakh), "1.25 Cr");
}

TEST(Csv, ColumnsByNameCommentsAndQuotes) {
    CsvTable t = CsvTable::parse("# provenance line\nid,name,cost\n1,\"Main St, North\",5\n\n2,\"Say \"\"hi\"\"\",7\n", "t.csv",
                                 {"id", "cost"}, {"name"});
    ASSERT_EQ(t.rows(), 2u);
    EXPECT_EQ(t.comments(), std::vector<std::string>{"provenance line"});
    EXPECT_EQ(t.text(0, "name"), "Main St, North");
    EXPECT_EQ(t.text(1, "name"), "Say \"hi\"");
    EXPECT_EQ(t.integer(1, "cost"), 7);
    EXPECT_EQ(t.line(1), 5);
}

TEST(Csv, StructuralErrorsNameFileAndLine) {
    auto expect_error = [](const std::string &text, const std::string &needle) {
        try {
            CsvTable::parse(text, "f.csv", {"id", "km"}, {"note"});
            FAIL() << "no error for: " << text;
        } catch (const DataError &e) {
            EXPECT_NE(std::string(e.what()).find(needle), std::string::npos) << e.what();
            EXPECT_EQ(e.file(), "f.csv");
        }
    };
    expect_error("", "file is empty");
    expect_error("id\n1\n", "missing required column 'km'");
    expect_error("id,km,colour\n1,2,red\n", "unknown column 'colour'");
    expect_error("id,km,km\n1,2,3\n", "appears twice");
    expect_error("id,km\n1,2,3\n", "f.csv:2: expected 2 fields, found 3");
    expect_error("id,km\n1,\"2\n", "unterminated");
}

TEST(Csv, MalformedNumbers) {
    CsvTable t = CsvTable::parse("id,km\n1,abc\n2,\n3,nan\n4,1e3\n", "f.csv", {"id", "km"}, {});
    EXPECT_THROW(t.number(0, "km"), DataError);
    EXPECT_THROW(t.number(1, "km"), DataError);   // missing value
    EXPECT_THROW(t.number(2, "km"), DataError);   // NaN is not accepted
    EXPECT_DOUBLE_EQ(t.number(3, "km"), 1000);
    EXPECT_FALSE(t.opt_number(1, "km").has_value());
}

TEST(RoadNetwork, ValidatesNodesAndRoads) {
    RoadNetwork n;
    n.add_node({1, GeoPoint{12.97, 77.59}, 900, "junction", "A"});
    n.add_node({2, std::nullopt, 0, "", ""});
    EXPECT_THROW(n.add_node({1, std::nullopt, 0, "", ""}), std::invalid_argument);                  // duplicate id
    EXPECT_THROW(n.add_node({3, GeoPoint{91, 0}, 0, "", ""}), std::invalid_argument);               // latitude > 90
    EXPECT_THROW(n.add_node({4, GeoPoint{0, -181}, 0, "", ""}), std::invalid_argument);             // longitude < -180
    EXPECT_NO_THROW(n.add_edge({10, 1, 2, 5.0, std::nullopt, std::nullopt}));
    EXPECT_THROW(n.add_edge({10, 1, 2, 5.0, std::nullopt, std::nullopt}), std::invalid_argument);   // duplicate road id
    EXPECT_THROW(n.add_edge({11, 1, 99, 5.0, std::nullopt, std::nullopt}), std::invalid_argument);  // unknown node
    EXPECT_THROW(n.add_edge({12, 1, 1, 5.0, std::nullopt, std::nullopt}), std::invalid_argument);   // self-loop
    EXPECT_THROW(n.add_edge({13, 1, 2, -5.0, std::nullopt, std::nullopt}), std::invalid_argument);  // negative distance
    EXPECT_THROW(n.add_edge({14, 1, 2, 0.0, std::nullopt, std::nullopt}), std::invalid_argument);   // zero length
    EXPECT_THROW(n.add_edge({15, 1, 2, 5.0, -1.0, std::nullopt}), std::invalid_argument);           // negative time
    EXPECT_EQ(n.edge_count(), 1);   // failed additions changed nothing
    EXPECT_EQ(n.index_of(2), 1);
    EXPECT_EQ(n.index_of(77), -1);
}

TEST(RoadNetwork, ComponentsAndDistances) {
    RoadNetwork n;
    for (int id : {1, 2, 3, 4}) n.add_node({id, std::nullopt, 0, "", ""});
    n.add_edge({1, 1, 2, 3, std::nullopt, std::nullopt});
    n.add_edge({2, 3, 4, 3, std::nullopt, std::nullopt});
    std::vector<int> label;
    EXPECT_EQ(n.components(&label), 2);
    EXPECT_EQ(label[0], label[1]);
    EXPECT_NE(label[1], label[2]);
    EXPECT_NEAR(haversine_km({0, 0}, {0, 1}), 111.19, 0.01);
}

TEST(Vehicle, Validation) {
    ElectricVehicle ok{"V", "test", 40, 20, 0.18, 4, "CCS2", 0};
    EXPECT_NO_THROW(validate(ok));
    ElectricVehicle v = ok;
    v.battery_kwh = 0;
    EXPECT_THROW(validate(v), std::invalid_argument);   // zero capacity
    v = ok;
    v.consumption_kwh_per_km = 0;
    EXPECT_THROW(validate(v), std::invalid_argument);   // zero consumption
    v = ok;
    v.charge_kwh = 41;
    EXPECT_THROW(validate(v), std::invalid_argument);   // charge above capacity
    v = ok;
    v.reserve_kwh = 40;
    EXPECT_THROW(validate(v), std::invalid_argument);   // reserve must be below capacity
    v = ok;
    v.charge_kwh = -1;
    EXPECT_THROW(validate(v), std::invalid_argument);
    EXPECT_DOUBLE_EQ(ok.usable_now(), 16);
}

TEST(Station, StatusConnectorsAndCosts) {
    EXPECT_EQ(parse_station_status("Available"), StationStatus::Available);
    EXPECT_EQ(parse_station_status(" offline "), StationStatus::Offline);
    EXPECT_THROW(parse_station_status("broken"), std::invalid_argument);
    EXPECT_TRUE(connectors_compatible("CCS2|Type2", "ccs2"));
    EXPECT_FALSE(connectors_compatible("CHAdeMO", "CCS2"));
    EXPECT_TRUE(connectors_compatible("", "CCS2"));   // not recorded: assumed compatible (documented)
    CandidateStation c;
    c.id = 1;
    c.power_kw = 50;
    c.ports = 2;
    c.install_cost = -100;
    EXPECT_THROW(validate(c), std::invalid_argument);   // negative cost
    c.install_cost = 100;
    c.benefit = -3;
    EXPECT_THROW(validate(c), std::invalid_argument);   // negative benefit
    c.benefit = 3;
    c.suitability = 1.5;
    EXPECT_THROW(validate(c), std::invalid_argument);
    c.suitability = 0.5;
    EXPECT_NO_THROW(validate(c));
}
