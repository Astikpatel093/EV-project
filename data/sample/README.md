# Sample dataset (illustrative, synthetic)

A ten-node network written by hand so that every answer can be checked with pencil and paper.
**It is not a real place.** Coordinates sit near latitude 0, longitude 0 on purpose, and the vehicles are
generic descriptions, not real products. The integration tests (`tests/integration`) use this data and
assert exactly the numbers worked out below.

## Network

```
                 6 Hill Pass (700 m)
               /15 km          \15 km
 1 Depot West --20-- 2 Junction A --20-- 3 Town Centre --20-- 4 Junction B --20-- 5 Depot East
                       \12              /12                      \10            /10
                        7 Ring North ---                          8 Ring South -

 9 Island Port --5-- 10 Island Village        (a separate island: no road to the mainland)
```

Every node is at 100 m except the Hill Pass (700 m) and the island (0 m).

## Energy model settings (`scenario.cfg`)

| key | value | meaning |
|---|---|---|
| reference_consumption | 0.2 kWh/km | the reference vehicle |
| climb_kwh_per_m | 0.012 | energy per metre of climb (reference vehicle) |
| regen_efficiency | 0.5 | half of the climb energy comes back downhill |
| target_soc | 0.8 | a charging stop fills the battery to 80 % |
| budget_inr | 1,00,00,000 | 1 crore = 100 lakh for new stations |

Road energy for a vehicle with consumption 0.2 kWh/km (scale 1):

| arc | km | kWh |
|---|---|---|
| any flat 20 km road | 20 | 4.0 |
| 2–7, 7–3 | 12 | 2.4 |
| 4–8, 8–5 | 10 | 2.0 |
| 1 → 6 (uphill 600 m) | 15 | 3.0 + 600 × 0.012 = **10.2** |
| 6 → 3 (downhill 600 m) | 15 | 3.0 − 600 × 0.012 × 0.5 = **−0.6** |

## Stations and candidates

| id | where | notes |
|---|---|---|
| 101 Town Centre Charger | node 3 | 50 kW, 2 ports, CCS2/Type2, available |
| 102 Junction B Charger | node 4 | **offline** |
| 103 Ring North CHAdeMO | node 7 | CHAdeMO only - the CCS2 vehicles cannot use it |

| candidate | node | cost | benefit | benefit / cost |
|---|---|---|---|---|
| 501 | 2 | 40 lakh | 60 | 1.50 |
| 502 | 4 | 30 lakh | 50 | 1.67 |
| 503 | 7 | 50 lakh | 70 | 1.40 |
| 504 | 8 | 20 lakh | 30 | 1.50 |
| 505 | 6 | 60 lakh | 65 | 1.08 |
| 506 | 5 | 30 lakh | 45 | 1.50 |

**Placement, budget 100 lakh.** The optimum is {501, 502, 506}: cost exactly 100 lakh, benefit 155.
Greedy by ratio takes 502, then 501 and 504 (ties broken by file order), then cannot afford 506:
{501, 502, 504}, cost 90 lakh, benefit 140. So greedy is not optimal here.

## Vehicles

| id | battery | start charge | consumption | reserve | usable at start |
|---|---|---|---|---|---|
| SV1 | 20 kWh | 12 kWh | 0.20 kWh/km | 2 kWh | 10 kWh (50 km on the flat) |
| SV2 | 8 kWh | 8 kWh | 0.20 kWh/km | 1 kWh | 7 kWh; after a stop 6.4 − 1 = 5.4 kWh |
| SV3 | 60 kWh | 50 kWh | 0.15 kWh/km | 6 kWh | 44 kWh (energy × 0.75) |

## Trips and expected plans (existing stations only)

| trip | what happens | expected result |
|---|---|---|
| 1 SV1 1→3 | The shortest road route is the 30 km hill pass, but climbing to node 6 needs 10.2 kWh and would leave 1.8 kWh, below the 2 kWh reserve. The flat road 1-2-3 is 40 km and uses 8 kWh. | **Direct, 40 km**, arrives with 4.0 kWh. The shortest route is *not* energy-feasible. |
| 2 SV1 1→5 | 80 km of flat road needs 16 kWh; only 10 kWh is usable. Drive 1-2-3 (arrive 4.0 kWh), charge to 16 kWh at the Town Centre Charger (12 kWh at 50 kW = 14.4 min), then 40 km to node 5 (arrive 8.0 kWh). | **With charging, 80 km, 1 stop** at station 101. |
| 3 SV1 1→9 | The island has no road to the mainland. | **No road connection.** |
| 4 SV2 3→5, 6.4 kWh | 40 km needs 8 kWh; usable is 5.4 kWh. The only usable charger in reach (101, at the start) can only fill to 6.4 kWh; station 102 on the way is offline. | **Infeasible even though a charging station exists** (charging insufficient). |
| 5 SV3 1→5 | Big battery: the 70 km route over the pass uses 13.2 kWh of the 44 kWh usable. | **Direct, 70 km.** |
| 6 SV1 4→5 | Two different 20 km routes (4-5 and 4-8-5). | **Direct, 20 km** (either route is a correct answer). |
| 7 SV1 1→3, 9.5 kWh | Every route needs at least 8 kWh but only 7.5 kWh is usable. Station 103 is reachable (6.4 kWh away) but has no CCS2 connector; station 101 is out of reach. | **Out of range.** With candidate 501 built at node 2 it becomes **with charging, 40 km, 1 stop**. |
| 8 SV1 3→3 | Origin equals destination. | **Zero-length**, 0 km. |

## Booked sessions at station 101 (2 bays)

Sessions (minutes): 1 [480,540), 2 [500,560), 3 [520,600), 4 [540,570), 5 [560,620), 6 [600,660),
7 [610,640), 8 [650,700).

* One bay, activity selection (earliest finish first): sessions **1, 4, 7, 8** (4 sessions).
* Serving every session, interval partitioning: **3 bays** (three sessions overlap during [520,540)).
* With the 2 installed bays: **6 of 8** sessions (bay 1: 1, 4, 7, 8; bay 2: 2, 5).
