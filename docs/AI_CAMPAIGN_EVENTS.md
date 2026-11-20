# Career AI inputs by event

Generated from authenticated PC 1.4 USA data by [campaign_ai_audit.py](../tools/campaign_ai_audit.py).
See [the function map](AI_ORIGINAL_MAP.md) for consumers and parity boundaries.

Production round manifest agrees with the retail data for 113 events and 139 rounds.
Event 85 has no playable round. Tournament rounds appear in authored order.

- Course suffix F/R is forward/reverse. S/L is the short/long lap curve input. P uses the point-to-point rule.
- The x count comes from the event slot. Runtime point-to-point races use a single competitive start-to-finish stage.
- T/G is the authored traffic selector and preferred player grid slot. Traffic density 2 or 3 selects the alternate ordinary catch-up curves.
- Rank weight scales the rank term in ordinary catch-up. The distance term has its own curve. Drag bypasses this ordinary controller.
- Pedal cap is the authored moving throttle ceiling from 422523/45FC90. The held grid and GO transition have separate pedal commands.
- Score scale is the drift multiplier on Easy/Medium/Hard. Drift rows are score producers, not physical competitors.
- Opponents list authored label, car, Easy/Medium/Hard contexts, tuning selection and behavior mode.
- Adaptive tuning compares the current human ride with the authored rival car. Fixed tuning uses the given selector.
- A context of -1 selects row zero for speed configuration and suppresses speed-offset jitter. The raw selector is not a difficulty percentage.
- Fixed ride selectors occur in events 107, 108, 110, 111, 112 and 113. All other physical opponents use the adaptive branch.
- Only 1201 forward supplies a type-9 road link in the campaign course data. That course is used in event 20 round 3 and events 21 and 46.

| Event | Mode | Course/round(s) | T/G | Rank weight | Pedal cap | Score scale | Opponents |
| ---: | --- | --- | --- | ---: | ---: | --- | --- |
| 0 | Circuit | 1003F x2 S | 1/2 | 1 | 1.00 | - | BRIAN (ECLIPSE), 0/0/0, adaptive, m1<br>BRETT (SUPRA), 0/0/0, adaptive, m1<br>LOLA (CELICA), 1/1/1, adaptive, m1 |
| 1 | Circuit | 1003F x2 S | 0/1 | 1 | 1.00 | - | WART (PEUGOT), 0/1/2, adaptive, m1<br>NESTO (NEON), 0/1/3, adaptive, m1<br>JOSE (GOLF), 1/2/3, adaptive, m1 |
| 2 | Sprint | 1102F x2 P | 0/1 | 1.1 | 1.00 | - | ROSIE (GOLF), 0/1/2, adaptive, m1<br>JACKIE (NEON), 0/2/3, adaptive, m1<br>BRYCE (PEUGOT), 1/2/3, adaptive, m1 |
| 3 | Drift | 1301F x4 L | 0/1 | - | - | 0.25/1/1 | CARL (MIATA), 0/1/4, score row; authored adaptive, m1<br>CARLA (LANCER), 0/1/5, score row; authored adaptive, m1<br>DIRT (240SX), 1/2/5, score row; authored adaptive, m1 |
| 4 | Drag | 1210R x2 P | 0/1 | - | 0.93 | - | DARYL (GOLF), 0/0/2, adaptive, m1<br>CARL (MIATA), 0/2/3, adaptive, m1<br>KLUTCH (NEON), 1/2/3, adaptive, m1 |
| 5 | Knockout | 1003R x3 S | 1/1 | 1 | 1.00 | - | ROSIE (GOLF), 1/1/2, adaptive, m1<br>WART (PEUGOT), 0/1/3, adaptive, m1<br>PRISS (LANCER), 0/2/3, adaptive, m1 |
| 6 | Circuit | 1003F x2 S, 1003R x2 S, 1006F x2 S | 1/1 | 1 | 1.00 | - | CARL (MIATA), 0/2/3, adaptive, m1<br>STEFAN (MIATA), 1/2/4, adaptive, m1<br>PROBES (MIATA), 1/3/4, adaptive, m1 |
| 7 | Time trial | 1102R x2 P | 0/1 | - | 1.00 | - | - |
| 8 | Circuit | 1005F x3 S | 0/1 | 1.1 | 1.00 | - | DARYL (GOLF), 0/2/4, adaptive, m1<br>STEWART (FOCUS), 1/2/4, adaptive, m1<br>ROSIE (GOLF), 2/3/3, adaptive, m1 |
| 9 | Drag | 1210R x2 P, 1210F x2 P, 1201R x2 P | 1/1 | - | 0.93 | - | AL (CIVIC), 3/4/6, adaptive, m1<br>NESTO (NEON), 2/3/5, adaptive, m1<br>STEFAN (MIATA), 1/2/4, adaptive, m1 |
| 10 | Knockout | 1003R x3 S | 1/1 | 0.9 | 1.00 | - | AL (CIVIC), 1/2/3, adaptive, m1<br>SCOTT (CIVIC), 1/2/4, adaptive, m1<br>HUNTER (CIVIC), 2/3/4, adaptive, m1 |
| 11 | Drag | 1202F x2 P | 1/1 | - | 0.93 | - | KLUTCH (NEON), 2/3/4, adaptive, m1<br>BRYCE (PEUGOT), 2/2/4, adaptive, m1<br>STEWART (FOCUS), 1/2/5, adaptive, m1 |
| 12 | Sprint | 1103F x2 P | 1/1 | 1.1 | 1.00 | - | STEFAN (MIATA), 1/3/3, adaptive, m1<br>CARLA (LANCER), 1/3/4, adaptive, m1<br>ROSIE (GOLF), 2/3/4, adaptive, m1 |
| 13 | Drift | 1302F x4 L | 0/1 | - | - | 0.4/1/1 | WART (PEUGOT), 1/2/5, score row; authored adaptive, m1<br>DIRT (240SX), 2/3/6, score row; authored adaptive, m1<br>CARL (MIATA), 1/2/6, score row; authored adaptive, m1 |
| 14 | Circuit | 1006F x4 L, 1005R x4 L, 1002F x4 L | 2/1 | 0.9 | 1.00 | - | SCOTT (CIVIC), 1/2/4, adaptive, m1<br>PRISS (LANCER), 2/3/5, adaptive, m1<br>TERRY (FOCUS), 2/4/5, adaptive, m1 |
| 15 | Time trial | 1104F x2 P | 1/1 | - | 1.00 | - | - |
| 16 | Circuit | 1006R x2 S | 1/1 | 1 | 1.00 | - | JOSE (GOLF), 2/4/6, adaptive, m1<br>JACKIE (NEON), 2/3/5, adaptive, m1<br>STEWART (FOCUS), 1/2/4, adaptive, m1 |
| 17 | Drag | 1201R x2 P | 2/1 | - | 0.93 | - | FISH (TIBURON), 1/2/6, adaptive, m1<br>LEBRON (TIBURON), 1/2/5, adaptive, m1<br>MACCA (TIBURON), 2/3/6, adaptive, m1 |
| 18 | Drift | 1302F x4 L | 0/1 | - | - | 0.65/1/1 | STEFAN (MIATA), 2/3/6, score row; authored adaptive, m1<br>PRISS (LANCER), 1/3/6, score row; authored adaptive, m1<br>HECTOR (SENTRA), 1/2/5, score row; authored adaptive, m1 |
| 19 | Circuit | 1001F x3 S | 2/1 | 1 | 1.00 | - | JOSE (GOLF), 3/4/5, adaptive, m1<br>BRYCE (PEUGOT), 2/3/5, adaptive, m1<br>BRIAN (240SX), 1/3/4, adaptive, m1 |
| 20 | Drag | 1210R x2 P, 1202F x2 P, 1201F x2 P, 1206F x2 P | 2/1 | - | 1.00 | - | JET (IMPREZA), 1/4/6, adaptive, m1<br>NESTO (NEON), 2/4/5, adaptive, m1<br>SPIN (INTEGRA), 3/3/4, adaptive, m1 |
| 21 | Drag | 1201F x2 P | 2/1 | - | 1.00 | - | KLUTCH (NEON), 3/4/5, adaptive, m1<br>SCOTT (CIVIC), 2/3/4, adaptive, m1<br>CARLA (LANCER), 1/3/4, adaptive, m1 |
| 22 | Sprint | 1103R x2 P | 2/1 | 1.1 | 1.00 | - | DESHAWN (CELICA), 3/4/5, adaptive, m1<br>WILL (IMPREZA), 2/4/5, adaptive, m1<br>FISH (TIBURON), 2/3/4, adaptive, m1 |
| 23 | Sprint | 1104F x2 P | 0/1 | 0.9 | 1.00 | - | PRISS (LANCER), 2/4/5, adaptive, m1<br>CARLA (LANCER), 3/4/5, adaptive, m1<br>MUTT (ECLIPSE), 2/3/4, adaptive, m1 |
| 24 | Knockout | 1006R x3 S | 2/1 | 1 | 1.00 | - | RICCO (SENTRA), 3/4/6, adaptive, m1<br>HECTOR (SENTRA), 3/4/5, adaptive, m1<br>HOFFMAN (SENTRA), 3/4/5, adaptive, m1 |
| 25 | Drift | 1303F x4 L | 0/1 | - | - | 0.8/1/1 | DIRT (240SX), 2/3/6, score row; authored adaptive, m1<br>LEBRON (TIBURON), 2/3/6, score row; authored adaptive, m1<br>CARL (MIATA), 1/2/5, score row; authored adaptive, m1 |
| 26 | Time trial | 1105F x2 P | 2/1 | - | 1.00 | - | - |
| 27 | Sprint | 1106F x2 P | 3/1 | 1.1 | 1.00 | - | CHAD (CELICA), 4/5/6, adaptive, m2<br>WART (PEUGOT), 3/4/5, adaptive, m1<br>TERRY (FOCUS), 2/3/4, adaptive, m1 |
| 28 | Drift | 1304F x4 L | 0/1 | - | - | 0.8/1/1 | AL (CIVIC), 2/3/6, score row; authored adaptive, m1<br>LEBRON (TIBURON), 2/4/6, score row; authored adaptive, m1<br>JARED (RSX), 2/3/7, score row; authored adaptive, m1 |
| 29 | Drag | 1201R x2 P | 3/1 | - | 1.00 | - | STEWART (FOCUS), 1/3/4, adaptive, m1<br>TODD (LANCER), 2/4/6, adaptive, m1<br>FISH (TIBURON), 1/2/4, adaptive, m1 |
| 30 | Circuit | 1003R x2 S, 1008F x2 S, 1002F x2 S | 2/1 | 1 | 1.00 | - | BRAD (SUPRA), 3/5/6, adaptive, m1<br>WILL (IMPREZA), 2/4/6, adaptive, m1<br>JACKIE (NEON), 3/4/5, adaptive, m1 |
| 31 | Knockout | 1005F x3 S | 3/1 | 1.2 | 1.00 | - | PAUL (INTEGRA), 3/3/4, adaptive, m1<br>DESHAWN (CELICA), 2/4/5, adaptive, m1<br>BOPPER (RSX), 2/4/6, adaptive, m1 |
| 32 | Circuit | 1008F x3 S | 2/1 | 0.9 | 1.00 | - | LAZIO (240SX), 2/3/4, adaptive, m1<br>MACK-D (SUPRA), 2/3/5, adaptive, m1<br>KURT (RSX), 3/5/6, adaptive, m2 |
| 33 | Drift | 1303F x4 L | 0/1 | - | - | 0.9/1/1 | LOLA (RSX), 2/3/7, score row; authored adaptive, m1<br>B-DOG (ECLIPSE), 2/3/6, score row; authored adaptive, m1<br>BRITTON (INTEGRA), 2/3/6, score row; authored adaptive, m1 |
| 34 | Sprint | 1105R x2 P | 3/1 | 0.9 | 1.00 | - | JET (IMPREZA), 2/3/4, adaptive, m1<br>LEBRON (TIBURON), 2/4/5, adaptive, m1<br>WILL (IMPREZA), 3/4/6, adaptive, m1 |
| 35 | Circuit | 1006R x6 L | 0/1 | 1.1 | 1.00 | - | TANAKA (IMPREZA), 2/3/4, adaptive, m1<br>VINCE (CELICA), 3/4/5, adaptive, m1<br>TAKASHI (SUPRA), 3/4/6, adaptive, m1 |
| 36 | Drag | 1206F x2 P | 2/1 | - | 1.00 | - | TODD (LANCER), 3/5/6, adaptive, m1<br>NUTZ (CELICA), 2/4/5, adaptive, m1<br>DARYL (GOLF), 2/3/4, adaptive, m1 |
| 37 | Circuit | 1005R x6 L | 3/1 | 0.9 | 1.00 | - | KURT (RSX), 3/5/6, adaptive, m2<br>STAN (SUPRA), 2/4/5, adaptive, m1<br>PAUL (INTEGRA), 2/4/5, adaptive, m1 |
| 38 | Drift | 1303F x4 L, 1304F x4 L, 1305F x4 L | 0/1 | - | - | 0.9/1/1 | FISH (TIBURON), 3/4/7, score row; authored adaptive, m1<br>JET (IMPREZA), 2/3/7, score row; authored adaptive, m1<br>AL (CIVIC), 2/3/6, score row; authored adaptive, m1 |
| 39 | Knockout | 1006R x3 S | 3/1 | 1.1 | 1.00 | - | LAZIO (240SX), 2/3/5, adaptive, m1<br>MUTT (ECLIPSE), 2/4/5, adaptive, m1<br>NUTZ (CELICA), 3/5/6, adaptive, m1 |
| 40 | Drift | 1304F x4 L | 0/1 | - | - | 1/1/1 | DIRT (240SX), 3/4/8, score row; authored adaptive, m1<br>SPIN (INTEGRA), 2/3/7, score row; authored adaptive, m1<br>DESHAWN (CELICA), 2/3/6, score row; authored adaptive, m1 |
| 41 | Sprint | 1103F x2 P | 3/1 | 1.1 | 1.00 | - | BRAD (SUPRA), 3/5/6, adaptive, m1<br>B-DOG (ECLIPSE), 2/4/5, adaptive, m1<br>JARED (RSX), 2/3/5, adaptive, m1 |
| 42 | Drag | 1202F x2 P | 2/1 | - | 1.00 | - | STEFAN (MIATA), 0/2/4, adaptive, m1<br>RICCO (SENTRA), 2/4/6, adaptive, m1<br>BOPPER (RSX), 2/3/6, adaptive, m1 |
| 43 | Sprint | 1104R x2 P | 2/1 | 0.9 | 1.00 | - | CHAD (CELICA), 3/5/6, adaptive, m2<br>ROSIE (GOLF), 2/4/5, adaptive, m1<br>LOLA (RSX), 2/3/5, adaptive, m1 |
| 44 | Drift | 1305F x4 L | 0/1 | - | - | 1/1/1 | WART (PEUGOT), 2/4/8, score row; authored adaptive, m1<br>BRYCE (PEUGOT), 2/4/7, score row; authored adaptive, m1<br>TICKETS (PEUGOT), 2/3/6, score row; authored adaptive, m1 |
| 45 | Circuit | 1008R x3 S | 1/1 | 0.8 | 1.00 | - | JOSE (GOLF), 3/5/6, adaptive, m1 |
| 46 | Drag | 1201F x2 P | 2/1 | - | 1.00 | - | KLUTCH (NEON), 3/5/6, adaptive, m1<br>TERRY (FOCUS), 3/4/6, adaptive, m1<br>BRIAN (240SX), 3/3/5, adaptive, m1 |
| 47 | Time trial | 1103R x2 P | 3/1 | - | 1.00 | - | - |
| 48 | Circuit | 1003F x2 S, 1006F x2 S, 1005F x2 S | 2/1 | 1 | 1.00 | - | TAKASHI (SUPRA), 3/5/6, adaptive, m1<br>NESTO (NEON), 3/4/6, adaptive, m1<br>SCOTT (CIVIC), 2/4/6, adaptive, m1 |
| 49 | Knockout | 1008F x3 S | 0/1 | 0.9 | 1.00 | - | WILL (IMPREZA), 3/5/6, adaptive, m1<br>LEBRON (TIBURON), 3/4/5, adaptive, m1<br>MACK-D (SUPRA), 2/3/5, adaptive, m1 |
| 50 | Drift | 1303F x4 L | 0/1 | - | - | 1/1/1 | GEARS (FOCUS), 3/4/8, score row; authored adaptive, m1<br>BRITTON (INTEGRA), 2/4/7, score row; authored adaptive, m1<br>VIONCE (CELICA), 2/3/6, score row; authored adaptive, m1 |
| 51 | Drag | 1210F x2 P | 2/1 | - | 1.00 | - | WILL (IMPREZA), 3/4/6, adaptive, m1<br>MUTT (ECLIPSE), 2/3/5, adaptive, m1<br>B-DOG (ECLIPSE), 2/3/5, adaptive, m1 |
| 52 | Circuit | 1005R x2 S | 2/1 | 1 | 1.00 | - | NESTO (NEON), 3/5/6, adaptive, m1<br>PRISS (LANCER), 3/5/6, adaptive, m1<br>LEBRON (TIBURON), 2/4/5, adaptive, m1 |
| 53 | Circuit | 1006R x2 S | 0/1 | 0.9 | 1.00 | - | JOSE (GOLF), 3/5/6, adaptive, m1<br>AL (CIVIC), 3/4/5, adaptive, m1<br>FISH (TIBURON), 2/4/5, adaptive, m1 |
| 54 | Sprint | 1105F x2 P | 1/1 | 1.1 | 1.00 | - | NUTZ (CELICA), 3/5/6, adaptive, m1<br>VINCE (CELICA), 3/4/6, adaptive, m1<br>DESHAWN (CELICA), 2/4/5, adaptive, m1 |
| 55 | Drag | 1206R x2 P | 2/1 | - | 1.00 | - | MUTT (ECLIPSE), 3/5/6, adaptive, m1<br>RICCO (SENTRA), 3/5/6, adaptive, m1<br>BOPPER (RSX), 2/5/5, adaptive, m1 |
| 56 | Drift | 1305F x4 L | 0/1 | - | - | 1/1/1 | DARYL (GOLF), 3/4/8, score row; authored adaptive, m1<br>HECTOR (SENTRA), 2/4/7, score row; authored adaptive, m1<br>STAN (SUPRA), 2/3/6, score row; authored adaptive, m1 |
| 57 | Sprint | 1106R x2 P | 2/1 | 0.9 | 1.00 | - | SKID (SENTRA), 3/5/5, adaptive, m1<br>CHAD (CELICA), 3/6/6, adaptive, m2<br>BRITTON (INTEGRA), 2/4/5, adaptive, m1 |
| 58 | Knockout | 1008R x3 S | 2/1 | 1 | 1.00 | - | JET (IMPREZA), 3/5/6, adaptive, m1<br>PAUL (INTEGRA), 3/4/5, adaptive, m1<br>TAKASHI (SUPRA), 2/4/5, adaptive, m1 |
| 59 | Circuit | 1002R x4 L | 2/1 | 1 | 1.00 | - | JOSE (GOLF), 3/5/6, adaptive, m1<br>LOLA (RSX), 3/4/5, adaptive, m1<br>MACK-D (SUPRA), 2/4/5, adaptive, m1 |
| 60 | Knockout | 1006F x3 S | 2/1 | 1 | 1.00 | - | BRIAN (240SX), 3/5/6, adaptive, m1<br>SPIN (INTEGRA), 3/4/6, adaptive, m1<br>LAZIO (240SX), 2/4/5, adaptive, m1 |
| 61 | Sprint | 1105R x2 P | 1/1 | 1.1 | 1.00 | - | STEWART (FOCUS), 3/5/6, adaptive, m1<br>BRYCE (PEUGOT), 3/4/6, adaptive, m1<br>ROSIE (GOLF), 2/4/5, adaptive, m1 |
| 62 | Drag | 1206F x2 P | 2/1 | - | 1.00 | - | KLUTCH (NEON), 3/6/7, adaptive, m1<br>JACKIE (NEON), 3/5/6, adaptive, m1<br>NESTO (NEON), 2/4/5, adaptive, m1 |
| 63 | Drag | 1201R x2 P | 0/1 | - | 1.00 | - | ISHII (350Z), 3/5/6, adaptive, m1<br>JARED (RSX), 2/4/7, adaptive, m1<br>CARLA (LANCER), 2/4/5, adaptive, m1 |
| 64 | Drift | 1304F x4 L | 0/1 | - | - | 1/1/1 | WU (350Z), 3/5/8, score row; authored adaptive, m1<br>STAN (SUPRA), 3/4/7, score row; authored adaptive, m1<br>BRIAN (240SX), 2/3/7, score row; authored adaptive, m1 |
| 65 | Drift | 1306F x4 L | 0/1 | - | - | 1/1/1 | BOPPER (RSX), 3/5/8, score row; authored adaptive, m1<br>SPIN (INTEGRA), 3/4/7, score row; authored adaptive, m1<br>LOLA (RSX), 2/4/7, score row; authored adaptive, m1 |
| 66 | Sprint | 1103R x2 P, 1105F x2 P, 1104R x2 P | 2/1 | 1 | 1.00 | - | LAZIO (240SX), 3/5/6, adaptive, m1<br>BRIAN (240SX), 3/4/6, adaptive, m1<br>FORGE (240SX), 3/4/6, adaptive, m1 |
| 67 | Sprint | 1104F x2 P | 0/1 | 0.9 | 1.00 | - | CHAD (CELICA), 3/5/7, adaptive, m2<br>RICCO (SENTRA), 3/4/6, adaptive, m1<br>DARYL (GOLF), 2/4/5, adaptive, m1 |
| 68 | Time trial | 1106F x2 P | 0/1 | - | 1.00 | - | - |
| 69 | Circuit | 1008F x3 S, 1005R x3 S, 1002F x4 L | 2/1 | 1.2 | 1.00 | - | MATT (S2000), 3/5/7, adaptive, m1<br>SLIK (S2000), 3/4/6, adaptive, m1<br>JARRUS (S2000), 2/4/6, adaptive, m1 |
| 70 | Drag | 1214F x2 P | 2/1 | - | 1.00 | - | STAN (SUPRA), 2/4/7, adaptive, m1 |
| 71 | Circuit | 1004F x6 L | 1/1 | 0.9 | 1.00 | - | JARRUS (S2000), 4/5/7, adaptive, m1<br>JET (IMPREZA), 3/4/5, adaptive, m1<br>PRISS (LANCER), 3/5/6, adaptive, m1 |
| 72 | Knockout | 1001F x3 S | 3/1 | 1 | 1.00 | - | B-DOG (ECLIPSE), 4/5/7, adaptive, m1<br>WILL (IMPREZA), 3/5/6, adaptive, m1<br>MUTT (ECLIPSE), 3/4/5, adaptive, m1 |
| 73 | Drift | 1304F x4 L | 0/1 | - | - | 1/1/1 | KIRBY (RX7), 3/5/8, score row; authored adaptive, m1<br>LEBRON (TIBURON), 3/4/7, score row; authored adaptive, m1<br>HECTOR (SENTRA), 2/3/7, score row; authored adaptive, m1 |
| 74 | Sprint | 1107F x2 P | 2/1 | 1 | 1.00 | - | MATT (S2000), 4/5/7, adaptive, m1<br>AL (CIVIC), 3/5/6, adaptive, m1<br>STEWART (FOCUS), 3/4/5, adaptive, m1 |
| 75 | Circuit | 1008R x5 L | 3/1 | 1 | 1.00 | - | KURT (RSX), 4/6/7, adaptive, m2<br>STEFAN (MIATA), 3/5/6, adaptive, m1<br>BRAD (SUPRA), 2/4/5, adaptive, m1 |
| 76 | Drag | 1206R x2 P | 2/1 | - | 1.00 | - | KLUTCH (NEON), 3/6/7, adaptive, m1<br>PAUL (INTEGRA), 3/5/6, adaptive, m1<br>SCOTT (CIVIC), 2/4/5, adaptive, m1 |
| 77 | Time trial | 1107R x2 P | 0/1 | - | 1.00 | - | - |
| 78 | Sprint | 1108F x2 P | 2/1 | 0.9 | 1.00 | - | SAMANTHA (CIVIC), 5/7/8, adaptive, m1 |
| 79 | Drag | 1206F x2 P | 2/1 | - | 1.00 | - | TODD (ECLIPSE), 3/6/7, adaptive, m1<br>KAZ (SKYLINE), 3/4/6, adaptive, m1<br>SPARKS (350Z), 2/4/5, adaptive, m1 |
| 80 | Drag | 1214F x2 P, 1214R x2 P, 1214F x2 P, 1214R x2 P | 2/1 | - | 1.00 | - | WART (PEUGOT), 4/5/7, adaptive, m1<br>BRITTON (INTEGRA), 3/5/6, adaptive, m1<br>FISH (TIBURON), 3/4/5, adaptive, m1 |
| 81 | Circuit | 1004F x4 L | 0/1 | 1 | 1.00 | - | TAKASHI (SUPRA), 4/6/7, adaptive, m1<br>JARED (RSX), 4/5/6, adaptive, m1<br>SPIN (INTEGRA), 3/4/6, adaptive, m1 |
| 82 | Sprint | 1108F x2 P | 1/1 | 1.1 | 1.00 | - | MATT (S2000), 4/6/7, adaptive, m1<br>STAN (SUPRA), 3/5/6, adaptive, m1<br>HECTOR (SENTRA), 3/4/6, adaptive, m1 |
| 83 | Drag | 1207F x2 P | 2/1 | - | 1.00 | - | ISHII (350Z), 3/6/7, adaptive, m1<br>MACK-D (SUPRA), 3/5/6, adaptive, m1<br>LAZIO (240SX), 2/5/6, adaptive, m1 |
| 84 | Knockout | 1001R x3 S | 0/1 | 1 | 1.00 | - | DESHAWN (CELICA), 4/6/7, adaptive, m1<br>RICCO (SENTRA), 3/5/6, adaptive, m1<br>BRYCE (PEUGOT), 3/5/6, adaptive, m1 |
| 86 | Drift | 1306F x4 L | 0/1 | - | - | 1/1/1 | TAKASHI (SUPRA), 4/5/9, score row; authored adaptive, m1<br>CARLA (LANCER), 3/5/8, score row; authored adaptive, m1<br>NESTO (NEON), 3/4/7, score row; authored adaptive, m1 |
| 87 | Circuit | 1004R x4 L | 1/1 | 1 | 1.00 | - | TANAKA (IMPREZA), 4/6/7, adaptive, m1<br>BRETT (RX7), 4/5/7, adaptive, m1<br>NUTZ (CELICA), 3/5/6, adaptive, m1 |
| 88 | Drag | 1210F x2 P | 1/1 | - | 1.00 | - | WART (PEUGOT), 4/5/7, adaptive, m1<br>B-DOG (ECLIPSE), 3/5/6, adaptive, m1<br>AMY (RX7), 3/4/6, adaptive, m1 |
| 89 | Circuit | 1004F x3 S | 1/1 | 1 | 1.00 | - | LAZIO (240SX), 4/6/7, adaptive, m1<br>WILL (IMPREZA), 3/5/7, adaptive, m1<br>PRISS (LANCER), 3/5/6, adaptive, m1 |
| 90 | Drift | 1307F x4 L | 0/1 | - | - | 1/1/1 | LOLA (RSX), 4/6/9, score row; authored adaptive, m1<br>TERRY (FOCUS), 4/5/9, score row; authored adaptive, m1<br>DARYL (GOLF), 3/5/8, score row; authored adaptive, m1 |
| 91 | Knockout | 1007F x3 S | 1/1 | 1 | 1.00 | - | BRETT (RX7), 4/6/7, adaptive, m1<br>SPARKS (350Z), 4/5/7, adaptive, m1<br>SLIK (S2000), 3/5/6, adaptive, m1 |
| 92 | Circuit | 1002R x3 S, 1004R x3 S, 1007R x3 S | 2/1 | 1 | 1.00 | - | KURT (RSX), 5/6/7, adaptive, m2<br>CHAD (CELICA), 4/5/7, adaptive, m2<br>TODD (ECLIPSE), 4/5/6, adaptive, m2 |
| 93 | Drift | 1307F x4 L | 0/1 | - | - | 1/1/1 | TANAKA (IMPREZA), 4/5/9, score row; authored adaptive, m1<br>TAKASHI (SUPRA), 3/4/8, score row; authored adaptive, m1<br>KIRBY (RX7), 3/4/7, score row; authored adaptive, m1 |
| 94 | Sprint | 1109F x2 P | 1/1 | 1.1 | 1.00 | - | CHAD (S2000), 5/6/7, adaptive, m2<br>WU (350Z), 4/5/7, adaptive, m1<br>KAZ (SKYLINE), 4/5/6, adaptive, m1 |
| 95 | Sprint | 1108R x2 P | 0/1 | 1 | 1.00 | - | SCOTT (CIVIC), 5/6/7, adaptive, m1<br>VINCE (CELICA), 4/5/7, adaptive, m1<br>SPIN (INTEGRA), 4/5/6, adaptive, m1 |
| 96 | Circuit | 1001R x6 L | 2/1 | 0.9 | 1.00 | - | KURT (RX7), 5/6/7, adaptive, m2<br>AMY (RX7), 4/5/7, adaptive, m1<br>JARRUS (S2000), 3/4/6, adaptive, m1 |
| 97 | Time trial | 1109F x2 P | 1/1 | - | 1.00 | - | - |
| 98 | Drag | 1207R x2 P | 1/1 | - | 1.00 | - | HECTOR (SENTRA), 4/6/7, adaptive, m1<br>WU (350Z), 4/6/7, adaptive, m1<br>BRETT (RX7), 3/6/7, adaptive, m1 |
| 99 | Circuit | 1007F x6 L | 0/1 | 1 | 1.00 | - | SPARKS (350Z), 5/6/7, adaptive, m1<br>SLIK (S2000), 5/6/7, adaptive, m1<br>AMY (RX7), 4/5/6, adaptive, m1 |
| 100 | Sprint | 1106R x2 P | 2/1 | 1 | 1.00 | - | VINCE (CELICA), 5/6/7, adaptive, m1<br>MACK-D (SUPRA), 5/6/7, adaptive, m1<br>MUTT (ECLIPSE), 4/5/7, adaptive, m1 |
| 101 | Sprint | 1105R x2 P | 1/1 | 1.1 | 1.00 | - | BRIAN (240SX), 5/6/7, adaptive, m1<br>LOLA (RSX), 5/6/7, adaptive, m1<br>BRAD (SUPRA), 4/5/7, adaptive, m1 |
| 102 | Sprint | 1106R x2 P, 1107F x2 P, 1103F x2 P | 1/1 | 1 | 1.00 | - | CHAD (S2000), 5/6/8, adaptive, m2<br>TODD (ECLIPSE), 5/6/7, adaptive, m2<br>KURT (RX7), 4/5/7, adaptive, m2 |
| 103 | Knockout | 1001F x3 S | 1/1 | 1 | 1.00 | - | KAZ (SKYLINE), 5/5/7, adaptive, m1<br>ISHII (350Z), 5/6/7, adaptive, m1<br>KIRBY (RX7), 4/6/7, adaptive, m1 |
| 104 | Circuit | 1004R x7 L | 0/1 | 1 | 1.00 | - | BRETT (RX7), 5/6/7, adaptive, m1<br>MATT (S2000), 5/6/7, adaptive, m1<br>WU (350Z), 4/5/7, adaptive, m1 |
| 105 | Drift | 1308F x4 L | 0/1 | - | - | 1/1/1 | FISH (TIBURON), 5/6/9, score row; authored adaptive, m1<br>KAZ (SKYLINE), 4/5/8, score row; authored adaptive, m1<br>JARRUS (S2000), 4/5/8, score row; authored adaptive, m1 |
| 106 | Drag | 1206R x2 P | 1/1 | - | 1.00 | - | KIRBY (RX7), 5/6/8, adaptive, m1<br>AMY (RX7), 5/6/7, adaptive, m1<br>BRETT (RX7), 4/5/7, adaptive, m1 |
| 107 | Circuit | 1007R x3 S | 1/1 | 1 | 1.00 | - | KURT (RX7), 6/7/8, fixed 7, m2 |
| 108 | Drag | 1202R x2 P | 1/1 | - | 1.00 | - | TODD (ECLIPSE), 5/6/8, fixed 7, m2<br>SLIK (S2000), 4/5/7, fixed 7, m1<br>NUTZ (CELICA), 4/5/6, fixed 7, m1 |
| 109 | Drift | 1308F x4 L | 0/1 | - | - | 1/1/1 | DIRT (240SX), 5/7/9, score row; authored adaptive, m1<br>RICCO (SENTRA), 4/6/9, score row; authored adaptive, m1<br>BRIAN (240SX), 4/6/8, score row; authored adaptive, m1 |
| 110 | Sprint | 1109R x2 P | 1/1 | 1 | 1.00 | - | CHAD (S2000), 6/7/8, fixed 7, m2<br>JET (IMPREZA), 5/6/7, fixed 7, m1<br>BOPPER (RSX), 5/6/7, fixed 7, m1 |
| 111 | Circuit | 1007R x5 L | 1/1 | 1.1 | 1.00 | - | TJ (CIVIC), 6/7/8, fixed 8, m1 |
| 112 | Sprint | 1108R x2 P | 0/1 | 1 | 1.00 | - | EDDIE (SKYLINE), 7/8/9, fixed 8, m2 |
| 113 | Circuit | 1001F x4 L | 1/1 | 1 | 1.00 | - | MELISSA (350Z), 7/8/9, fixed 9, m1 |

## Course AI data

Joins follow the F/R rule and are listed as type:count before flag and actor filtering.
Type 9 is the obstacle input. Intersections and placement rows are raw authored tables.

Road gate -1 uses 435CA0 (ordinary racers typically wait 12 seconds). A literal 0 uses the race-clock threshold.

| Track | Roads | Points | Forward joins | Reverse joins | Intersections | Placement rows | Road gate |
| ---: | ---: | ---: | --- | --- | ---: | ---: | ---: |
| 1001 | 25 | 1361 | 0:3, 2:17, 3:6, 5:3, 7:4 | 0:1, 2:17, 3:6, 5:5, 7:4 | 0 | 4 | -1 |
| 1002 | 13 | 999 | 0:1, 2:7, 3:4, 4:1, 7:4 | 0:1, 2:7, 3:4, 5:1, 7:4 | 2 | 3 | 0 |
| 1003 | 17 | 1099 | 0:2, 2:11, 3:4, 4:2, 7:8 | 0:2, 2:11, 3:4, 4:2, 7:8 | 3 | 3 | 0 |
| 1004 | 19 | 1141 | 0:2, 2:13, 3:4, 5:3, 7:10 | 0:3, 2:13, 3:4, 5:2, 7:10 | 1 | 3 | -1 |
| 1005 | 19 | 1222 | 0:2, 2:12, 3:5, 4:1, 5:2, 7:4 | 0:1, 1:1, 2:12, 3:5, 4:1, 5:2, 7:4 | 2 | 3 | 0 |
| 1006 | 17 | 931 | 0:2, 2:10, 3:6, 4:1, 7:2 | 0:2, 2:10, 3:6, 4:1, 7:2 | 5 | 6 | -1 |
| 1007 | 13 | 1379 | 0:2, 2:8, 3:2, 4:1, 5:2, 7:6 | 0:1, 2:8, 3:2, 4:2, 5:2, 7:6 | 0 | 1 | -1 |
| 1008 | 12 | 862 | 0:2, 1:1, 2:10, 3:1, 7:6 | 0:2, 1:1, 2:10, 3:1, 7:6 | 0 | 0 | 0 |
| 1102 | 22 | 1450 | 0:2, 1:1, 2:16, 3:5, 4:3, 5:2, 7:13 | 0:1, 1:1, 2:16, 3:5, 4:4, 5:2, 7:13 | 3 | 3 | 0 |
| 1103 | 28 | 1723 | 0:2, 2:18, 3:7, 4:3, 5:3, 7:6 | 0:2, 2:18, 3:7, 4:3, 5:3, 7:6 | 2 | 3 | 0 |
| 1104 | 35 | 2126 | 0:5, 1:1, 2:23, 3:10, 5:3, 7:4 | 0:3, 1:1, 2:23, 3:10, 5:5, 7:4 | 1 | 7 | 0 |
| 1105 | 31 | 2317 | 0:3, 2:21, 3:9, 4:2, 5:2, 7:5 | 0:2, 1:1, 2:21, 3:8, 4:1, 5:3, 7:5 | 6 | 6 | 0 |
| 1106 | 37 | 2746 | 0:4, 1:1, 2:27, 3:8, 4:3, 5:5, 7:15 | 0:3, 1:3, 2:27, 3:8, 4:2, 5:5, 7:15 | 6 | 5 | -1 |
| 1107 | 43 | 3216 | 0:6, 1:1, 2:30, 3:9, 4:2, 5:4, 7:20 | 0:5, 1:1, 2:30, 3:9, 4:3, 5:4, 7:20 | 7 | 6 | 0 |
| 1108 | 33 | 2532 | 0:3, 1:2, 2:22, 3:7, 4:1, 5:2, 7:11 | 0:4, 1:1, 2:22, 3:7, 4:1, 5:2, 7:11 | 4 | 4 | -1 |
| 1109 | 28 | 1946 | 0:4, 1:1, 2:21, 3:6, 4:2, 5:4, 7:10 | 0:4, 1:1, 2:21, 3:6, 4:2, 5:4, 7:10 | 3 | 4 | -1 |
| 1201 | 24 | 763 | 2:8, 3:10, 7:5, 8:2, 9:1 | 2:8, 3:10, 5:1, 7:5, 8:2 | 4 | 8 | -1 |
| 1202 | 13 | 785 | 2:7, 3:2, 7:7, 8:2 | 2:7, 3:2, 7:7, 8:2 | 0 | 0 | -1 |
| 1206 | 17 | 678 | 2:2, 3:9, 7:6 | 2:2, 3:9, 7:6 | 0 | 6 | -1 |
| 1207 | 12 | 477 | 2:3, 3:7, 7:2 | 2:3, 3:7, 7:2 | 0 | 5 | -1 |
| 1210 | 18 | 772 | 2:1, 3:11, 7:6 | 2:1, 3:11, 7:6 | 0 | 8 | -1 |
| 1214 | 12 | 577 | 2:4, 3:7, 8:2 | 2:4, 3:7, 8:2 | 0 | 6 | -1 |
| 1301 | 2 | 63 | 2:2 | 2:2 | 0 | 0 | -1 |
| 1302 | 2 | 103 | 2:2 | 2:2 | 0 | 0 | -1 |
| 1303 | 2 | 136 | 2:2 | 2:2 | 0 | 0 | -1 |
| 1304 | 2 | 194 | 2:2 | 2:2 | 0 | 0 | -1 |
| 1305 | 2 | 193 | 2:2 | 2:2 | 0 | 0 | -1 |
| 1306 | 2 | 291 | 2:2 | 2:2 | 0 | 0 | -1 |
| 1307 | 2 | 187 | 2:2 | 2:2 | 0 | 0 | -1 |
| 1308 | 2 | 191 | 2:2 | 2:2 | 0 | 0 | -1 |

## Context lookup

The table below shows which values change with an opponent's selected context. Units for launch are seconds relative to GO. Shift bounds are RPM offsets.

| Context | Road weights | Road speed scale | Drag launch | Drag upshift | Drag downshift | Drift score factor | Catch-up grace (s) | Jitter radius |
| ---: | --- | ---: | --- | --- | --- | --- | ---: | ---: |
| 0 | 0/0/1 | 0.95 | -5/-4.3 | -900/300 | 0/0.01 | 0.025/0.1 | 3 | 8 |
| 1 | 0/0/1 | 0.96 | -4.6/-4 | -800/260 | 0/0.01 | 0.05/0.2 | 4 | 7 |
| 2 | 0/0.05/1 | 0.97 | -4.2/-3.5 | -650/200 | 0/0.01 | 0.1/0.3 | 5 | 6 |
| 3 | 0/0.2/1 | 0.98 | -3.5/-2.5 | -500/140 | 0/0.01 | 0.2/0.4 | 6 | 6 |
| 4 | 0/0.35/1 | 0.99 | -2.9/-2.2 | -390/100 | 0/0.01 | 0.35/0.5 | 8 | 5 |
| 5 | 0/0.5/1 | 1 | -2.9/-1.8 | -290/85 | 0/0.01 | 0.45/0.6 | 10 | 4 |
| 6 | 0/0.7/1 | 1 | -2.6/-1.3 | -205/45 | 0/0.01 | 0.65/0.8 | 13 | 4 |
| 7 | 0/0.7/1 | 1 | -2.2/-1.1 | -200/30 | 0/0.01 | 0.75/0.9 | 15 | 4 |
| 8 | 0/0.85/1 | 1 | -1.6/-0.8 | -150/20 | 0/0.01 | 0.85/1 | 17 | 4 |
| 9 | 0/0.9/1 | 1 | -1.5/-0.8 | -110/15 | 0/0.01 | 0.95/1.1 | 19 | 4 |
