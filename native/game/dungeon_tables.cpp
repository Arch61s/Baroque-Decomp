// Generated from SLUS_217.14 .data by tools/gen/gen_tables.py (tools/disasm.py word reads).
// Entity tag words are zero in the ELF image and are filled by the static-init stub at 0x4c2a40..0x4c3340
// (static ctor region, function 0x4c2840) from string literals at 0x44a610..; that stub was emulated to recover them.
// Scenario floor layouts: pointer tables 0x44a1b0 (ids 301-307), 0x44a1d0 (311-317), 0x44a1f0 (321-327),
// each entry {conn_t* connections; spawn_t* spawns} (see work/reports/MAP_FORMATS.md section 8.4).
#include "game/dungeon_tables.hpp"
namespace baroque::game::tables {
static const ScenarioConn kConn_A1[] = { // 0x48cab0
    {0x00,0x00,0x01,0x02}, {0xff,0xff,0xff,0xff} };
static const ScenarioEntity kEnt_A1[] = { // 0x48caf0
    {0xff, 0, 0.0f, 0.0f, 0.0f, 0} };
static const ScenarioLayout kLayout_A1 = { 0x48cb08, kConn_A1, {0, 0x00000000, 800.0f, 0.0f, 700.0f, 180}, {1, 0x00000000, 1200.0f, 0.0f, 1400.0f, 0}, kEnt_A1, 0 };
static const ScenarioConn kConn_A2[] = { // 0x48cb10
    {0x00,0x00,0x02,0x01}, {0xff,0xff,0xff,0xff} };
static const ScenarioEntity kEnt_A2[] = { // 0x48cb50
    {0x02, 0x41383145 /*E18A*/, 1900.0f, 0.0f, 1100.0f, 0x0},
    {0xff, 0, 0.0f, 0.0f, 0.0f, 0} };
static const ScenarioLayout kLayout_A2 = { 0x48cb80, kConn_A2, {0, 0x00000000, 800.0f, 0.0f, 700.0f, 180}, {2, 0x00000000, 700.0f, 0.0f, 550.0f, 0}, kEnt_A2, 1 };
static const ScenarioConn kConn_A3[] = { // 0x48cb88
    {0x03,0x00,0x04,0x02}, {0x04,0x00,0x06,0x02}, {0xff,0xff,0xff,0xff} };
static const ScenarioEntity kEnt_A3[] = { // 0x48cbd0
    {0x03, 0x31304453 /*SD01*/, 1400.0f, 0.0f, 1400.0f, 0x0},
    {0x04, 0x41383145 /*E18A*/, 800.0f, 0.0f, 1000.0f, 0x0},
    {0x04, 0x41383145 /*E18A*/, 800.0f, 0.0f, 1000.0f, 0x0},
    {0xff, 0, 0.0f, 0.0f, 0.0f, 0} };
static const ScenarioLayout kLayout_A3 = { 0x48cc30, kConn_A3, {3, 0x00000000, 2100.0f, 0.0f, 1700.0f, 270}, {6, 0x00000000, 600.0f, 0.0f, 650.0f, 0}, kEnt_A3, 3 };
static const ScenarioConn kConn_A4[] = { // 0x48cc38
    {0x0f,0x00,0x06,0x02}, {0x06,0x00,0x03,0x01}, {0xff,0xff,0xff,0xff} };
static const ScenarioEntity kEnt_A4[] = { // 0x48cc80
    {0x03, 0x31305443 /*CT01*/, 1400.0f, 0.0f, 1800.0f, 0x0},
    {0x06, 0x30305448 /*HT00*/, 1000.0f, 0.0f, 1100.0f, 0x0},
    {0xff, 0, 0.0f, 0.0f, 0.0f, 0} };
static const ScenarioLayout kLayout_A4 = { 0x48ccc8, kConn_A4, {15, 0x00000000, 800.0f, 0.0f, 2000.0f, 180}, {3, 0x00000000, 650.0f, 0.0f, 1600.0f, 0}, kEnt_A4, 2 };
static const ScenarioConn kConn_A5[] = { // 0x48ccd0
    {0x0f,0x01,0x04,0x01}, {0x04,0x02,0x03,0x00}, {0xff,0xff,0xff,0xff} };
static const ScenarioEntity kEnt_A5[] = { // 0x48cd10
    {0x04, 0x41383145 /*E18A*/, 1000.0f, 0.0f, 1100.0f, 0x0},
    {0x04, 0x41383145 /*E18A*/, 1000.0f, 0.0f, 1100.0f, 0x0},
    {0x04, 0x41383145 /*E18A*/, 1000.0f, 0.0f, 1100.0f, 0x0},
    {0x04, 0x41383145 /*E18A*/, 1000.0f, 0.0f, 1100.0f, 0x0},
    {0x03, 0x41383145 /*E18A*/, 1400.0f, 0.0f, 900.0f, 0x0},
    {0x03, 0x41383145 /*E18A*/, 1400.0f, 0.0f, 900.0f, 0x0},
    {0x03, 0x41383145 /*E18A*/, 1400.0f, 0.0f, 900.0f, 0x0},
    {0x03, 0x41383145 /*E18A*/, 1400.0f, 0.0f, 900.0f, 0x0},
    {0xff, 0, 0.0f, 0.0f, 0.0f, 0} };
static const ScenarioLayout kLayout_A5 = { 0x48cde8, kConn_A5, {15, 0x00000000, 700.0f, 0.0f, 1300.0f, 0}, {3, 0x00000000, 650.0f, 0.0f, 1600.0f, 0}, kEnt_A5, 8 };
static const ScenarioConn kConn_A6[] = { // 0x48cdf0
    {0x0e,0x02,0x0c,0x00}, {0x0c,0x03,0x01,0x00}, {0xff,0xff,0xff,0xff} };
static const ScenarioEntity kEnt_A6[] = { // 0x48ce30
    {0x01, 0x41383145 /*E18A*/, 1200.0f, 0.0f, 1100.0f, 0x0},
    {0xff, 0, 0.0f, 0.0f, 0.0f, 0} };
static const ScenarioLayout kLayout_A6 = { 0x48ce60, kConn_A6, {14, 0x00000000, 1200.0f, 0.0f, 800.0f, 0}, {1, 0x00000000, 1200.0f, 0.0f, 1400.0f, 0}, kEnt_A6, 1 };
static const ScenarioConn kConn_A7[] = { // 0x48ce68
    {0x09,0xff,0xff,0xff}, {0xff,0xff,0xff,0xff} };
static const ScenarioEntity kEnt_A7[] = { // 0x48cea0
    {0xff, 0, 0.0f, 0.0f, 0.0f, 0} };
static const ScenarioLayout kLayout_A7 = { 0x48ceb8, kConn_A7, {9, 0x00000000, 700.0f, 0.0f, 700.0f, 0}, {9, 0x00000000, 1200.0f, 0.0f, 1200.0f, 0}, kEnt_A7, 0 };
static const ScenarioConn kConn_B1[] = { // 0x48cec0
    {0x03,0x01,0x02,0x00}, {0x02,0x01,0x0f,0x00}, {0xff,0xff,0xff,0xff} };
static const ScenarioEntity kEnt_B1[] = { // 0x48cf00
    {0x03, 0x31304453 /*SD01*/, 1200.0f, 0.0f, 1200.0f, 0x0},
    {0x03, 0x32304e42 /*BN02*/, 1600.0f, 0.0f, 1000.0f, 0x0},
    {0x03, 0x31305443 /*CT01*/, 2000.0f, 0.0f, 1600.0f, 0x0},
    {0x02, 0x41383145 /*E18A*/, 1900.0f, 0.0f, 1300.0f, 0x0},
    {0x02, 0x41383145 /*E18A*/, 1900.0f, 0.0f, 1300.0f, 0x0},
    {0x02, 0x41383145 /*E18A*/, 1900.0f, 0.0f, 1300.0f, 0x0},
    {0x0f, 0x3030544d /*MT00*/, 1300.0f, 0.0f, 2100.0f, 0x0},
    {0x0f, 0x41383145 /*E18A*/, 1700.0f, 0.0f, 2100.0f, 0x0},
    {0xff, 0, 0.0f, 0.0f, 0.0f, 0} };
static const ScenarioLayout kLayout_B1 = { 0x48cfd8, kConn_B1, {3, 0x00000000, 1100.0f, 0.0f, 1700.0f, 270}, {15, 0x00000000, 900.0f, 0.0f, 2100.0f, 0}, kEnt_B1, 8 };
static const ScenarioConn kConn_B2[] = { // 0x48cfe0
    {0x03,0x00,0x04,0x02}, {0x04,0x01,0x0f,0x01}, {0xff,0xff,0xff,0xff} };
static const ScenarioEntity kEnt_B2[] = { // 0x48d020
    {0x0f, 0x3030544d /*MT00*/, 1200.0f, 0.0f, 2100.0f, 0x0},
    {0x0f, 0x41383145 /*E18A*/, 1700.0f, 0.0f, 2100.0f, 0x0},
    {0x04, 0x41333045 /*E03A*/, 900.0f, 0.0f, 1100.0f, 0x0},
    {0x04, 0x41333045 /*E03A*/, 900.0f, 0.0f, 1100.0f, 0x0},
    {0x04, 0x30305054 /*TP00*/, 900.0f, 0.0f, 1900.0f, 0x0},
    {0x04, 0x34314e42 /*BN14*/, 600.0f, 0.0f, 800.0f, 0x0},
    {0x04, 0x35314e42 /*BN15*/, 700.0f, 0.0f, 1100.0f, 0x0},
    {0xff, 0, 0.0f, 0.0f, 0.0f, 0} };
static const ScenarioLayout kLayout_B2 = { 0x48d0e0, kConn_B2, {3, 0x00000000, 1100.0f, 0.0f, 1700.0f, 180}, {15, 0x00000000, 900.0f, 0.0f, 2100.0f, 0}, kEnt_B2, 7 };
static const ScenarioConn kConn_B3[] = { // 0x48d0f0
    {0x06,0x00,0x00,0x01}, {0x00,0x02,0x04,0x00}, {0x04,0x01,0x03,0x01}, {0xff,0xff,0xff,0xff} };
static const ScenarioEntity kEnt_B3[] = { // 0x48d130
    {0x06, 0x30305054 /*TP00*/, 600.0f, 0.0f, 1000.0f, 0x0},
    {0x06, 0x30305054 /*TP00*/, 800.0f, 0.0f, 600.0f, 0x1},
    {0x00, 0x32304e42 /*BN02*/, 800.0f, 0.0f, 600.0f, 0x0},
    {0x04, 0x41383145 /*E18A*/, 800.0f, 0.0f, 1400.0f, 0x0},
    {0x04, 0x41383145 /*E18A*/, 800.0f, 0.0f, 1400.0f, 0x0},
    {0x03, 0x41383145 /*E18A*/, 1400.0f, 0.0f, 1600.0f, 0x0},
    {0x03, 0x41383145 /*E18A*/, 1400.0f, 0.0f, 1600.0f, 0x0},
    {0x03, 0x32304e42 /*BN02*/, 1400.0f, 0.0f, 1400.0f, 0x0},
    {0xff, 0, 0.0f, 0.0f, 0.0f, 0} };
static const ScenarioLayout kLayout_B3 = { 0x48d208, kConn_B3, {6, 0x00000000, 1300.0f, 0.0f, 1300.0f, 90}, {3, 0x00000000, 1300.0f, 0.0f, 900.0f, 0}, kEnt_B3, 8 };
static const ScenarioConn kConn_B4[] = { // 0x48d210
    {0x0f,0x00,0x06,0x02}, {0x06,0x00,0x0e,0x00}, {0xff,0xff,0xff,0xff} };
static const ScenarioEntity kEnt_B4[] = { // 0x48d250
    {0x06, 0x36304e42 /*BN06*/, 1400.0f, 0.0f, 1400.0f, 0x0},
    {0x06, 0x38304e42 /*BN08*/, 800.0f, 0.0f, 1600.0f, 0x0},
    {0x06, 0x35314e42 /*BN15*/, 600.0f, 0.0f, 1200.0f, 0x0},
    {0x06, 0x33304e42 /*BN03*/, 1200.0f, 0.0f, 1000.0f, 0x0},
    {0xff, 0, 0.0f, 0.0f, 0.0f, 0} };
static const ScenarioLayout kLayout_B4 = { 0x48d2c8, kConn_B4, {15, 0x00000000, 1300.0f, 0.0f, 2100.0f, 90}, {14, 0x00000000, 900.0f, 0.0f, 700.0f, 0}, kEnt_B4, 4 };
static const ScenarioConn kConn_B5[] = { // 0x48d2d0
    {0x08,0x02,0x0f,0x00}, {0x0f,0x01,0x06,0x00}, {0xff,0xff,0xff,0xff} };
static const ScenarioEntity kEnt_B5[] = { // 0x48d310
    {0x0f, 0x30304e42 /*BN00*/, 900.0f, 0.0f, 2100.0f, 0x0},
    {0x0f, 0x41383145 /*E18A*/, 1700.0f, 0.0f, 2100.0f, 0x0},
    {0x0f, 0x41383145 /*E18A*/, 1700.0f, 0.0f, 2100.0f, 0x0},
    {0x0f, 0x41383145 /*E18A*/, 1700.0f, 0.0f, 2100.0f, 0x0},
    {0x0f, 0x41383145 /*E18A*/, 1700.0f, 0.0f, 2100.0f, 0x0},
    {0x0f, 0x41383145 /*E18A*/, 1700.0f, 0.0f, 2100.0f, 0x0},
    {0x0f, 0x41383145 /*E18A*/, 1700.0f, 0.0f, 2100.0f, 0x0},
    {0x0f, 0x41383145 /*E18A*/, 1700.0f, 0.0f, 2100.0f, 0x0},
    {0x0f, 0x41383145 /*E18A*/, 1700.0f, 0.0f, 2100.0f, 0x0},
    {0x0f, 0x41383145 /*E18A*/, 1700.0f, 0.0f, 2100.0f, 0x0},
    {0x0f, 0x41383145 /*E18A*/, 1700.0f, 0.0f, 2100.0f, 0x0},
    {0xff, 0, 0.0f, 0.0f, 0.0f, 0} };
static const ScenarioLayout kLayout_B5 = { 0x48d430, kConn_B5, {8, 0x00000000, 900.0f, 0.0f, 900.0f, 0}, {6, 0x00000000, 1100.0f, 0.0f, 1300.0f, 0}, kEnt_B5, 11 };
static const ScenarioConn kConn_B6[] = { // 0x48d438
    {0x06,0x00,0x0e,0x00}, {0x0e,0x02,0x0c,0x00}, {0xff,0xff,0xff,0xff} };
static const ScenarioEntity kEnt_B6[] = { // 0x48d480
    {0xff, 0, 0.0f, 0.0f, 0.0f, 0} };
static const ScenarioLayout kLayout_B6 = { 0x48d498, kConn_B6, {6, 0x00000000, 1400.0f, 0.0f, 1750.0f, 180}, {12, 0x00000000, 600.0f, 0.0f, 1800.0f, 0}, kEnt_B6, 0 };
static const ScenarioConn kConn_B7[] = { // 0x48d4a0
    {0x09,0xff,0xff,0xff}, {0xff,0xff,0xff,0xff} };
static const ScenarioEntity kEnt_B7[] = { // 0x48d4e0
    {0xff, 0, 0.0f, 0.0f, 0.0f, 0} };
static const ScenarioLayout kLayout_B7 = { 0x48d4f8, kConn_B7, {9, 0x00000000, 700.0f, 0.0f, 700.0f, 0}, {9, 0x00000000, 1200.0f, 0.0f, 1200.0f, 0}, kEnt_B7, 0 };
static const ScenarioConn kConn_C1[] = { // 0x48d500
    {0x01,0x01,0x02,0x00}, {0x02,0x01,0x0f,0x00}, {0xff,0xff,0xff,0xff} };
static const ScenarioEntity kEnt_C1[] = { // 0x48d540
    {0x02, 0x30305842 /*BX00*/, 1900.0f, 0.0f, 900.0f, 0x0},
    {0x02, 0x30305842 /*BX00*/, 1300.0f, 0.0f, 700.0f, 0x0},
    {0x02, 0x41383143 /*C18A*/, 1000.0f, 0.0f, 600.0f, 0x0},
    {0x02, 0x3030544d /*MT00*/, 2000.0f, 0.0f, 1600.0f, 0x0},
    {0xff, 0, 0.0f, 0.0f, 0.0f, 0} };
static const ScenarioLayout kLayout_C1 = { 0x48d5b8, kConn_C1, {1, 0x00000000, 600.0f, 0.0f, 1900.0f, 180}, {15, 0x00000000, 900.0f, 0.0f, 2100.0f, 0}, kEnt_C1, 4 };
static const ScenarioConn kConn_C2[] = { // 0x48d5c0
    {0x04,0x01,0x00,0x01}, {0x00,0x00,0x02,0x01}, {0xff,0xff,0xff,0xff} };
static const ScenarioEntity kEnt_C2[] = { // 0x48d600
    {0x04, 0x3331514c /*LQ13*/, 800.0f, 0.0f, 800.0f, 0x0},
    {0x02, 0x41383145 /*E18A*/, 800.0f, 0.0f, 600.0f, 0x0},
    {0x02, 0x41383145 /*E18A*/, 800.0f, 0.0f, 600.0f, 0x0},
    {0x02, 0x41383145 /*E18A*/, 800.0f, 0.0f, 600.0f, 0x0},
    {0x00, 0x41383145 /*E18A*/, 800.0f, 0.0f, 600.0f, 0x0},
    {0x00, 0x41383145 /*E18A*/, 800.0f, 0.0f, 600.0f, 0x0},
    {0xff, 0, 0.0f, 0.0f, 0.0f, 0} };
static const ScenarioLayout kLayout_C2 = { 0x48d6a8, kConn_C2, {4, 0x00000000, 800.0f, 0.0f, 1200.0f, 180}, {2, 0x00000000, 700.0f, 0.0f, 550.0f, 0}, kEnt_C2, 6 };
static const ScenarioConn kConn_C3[] = { // 0x48d6b0
    {0x04,0x00,0x06,0x02}, {0x06,0x01,0x07,0x01}, {0xff,0xff,0xff,0xff} };
static const ScenarioEntity kEnt_C3[] = { // 0x48d6f0
    {0x04, 0x3231514c /*LQ12*/, 1000.0f, 0.0f, 1000.0f, 0x0},
    {0x06, 0x35304e42 /*BN05*/, 800.0f, 0.0f, 1400.0f, 0x0},
    {0x06, 0x41383145 /*E18A*/, 1000.0f, 0.0f, 1000.0f, 0x0},
    {0x06, 0x41383145 /*E18A*/, 1000.0f, 0.0f, 1000.0f, 0x0},
    {0x06, 0x41383145 /*E18A*/, 1000.0f, 0.0f, 1000.0f, 0x0},
    {0xff, 0, 0.0f, 0.0f, 0.0f, 0} };
static const ScenarioLayout kLayout_C3 = { 0x48d780, kConn_C3, {4, 0x00000000, 800.0f, 0.0f, 1600.0f, 180}, {7, 0x00000000, 1100.0f, 0.0f, 1200.0f, 0}, kEnt_C3, 5 };
static const ScenarioConn kConn_C4[] = { // 0x48d788
    {0x02,0x00,0x06,0x01}, {0x06,0x02,0x03,0x00}, {0xff,0xff,0xff,0xff} };
static const ScenarioEntity kEnt_C4[] = { // 0x48d7d0
    {0x02, 0x3030544d /*MT00*/, 2000.0f, 0.0f, 1000.0f, 0x0},
    {0x06, 0x3031514c /*LQ10*/, 1000.0f, 0.0f, 1200.0f, 0x0},
    {0x06, 0x41383145 /*E18A*/, 1000.0f, 0.0f, 1000.0f, 0x0},
    {0x06, 0x41383145 /*E18A*/, 1000.0f, 0.0f, 1000.0f, 0x0},
    {0x06, 0x41383145 /*E18A*/, 1000.0f, 0.0f, 1000.0f, 0x0},
    {0x06, 0x41383145 /*E18A*/, 1400.0f, 0.0f, 1200.0f, 0x0},
    {0x06, 0x41383145 /*E18A*/, 1400.0f, 0.0f, 1200.0f, 0x0},
    {0x06, 0x41383145 /*E18A*/, 1400.0f, 0.0f, 1200.0f, 0x0},
    {0xff, 0, 0.0f, 0.0f, 0.0f, 0} };
static const ScenarioLayout kLayout_C4 = { 0x48d8a8, kConn_C4, {2, 0x00000000, 1900.0f, 0.0f, 1500.0f, 180}, {3, 0x00000000, 1100.0f, 0.0f, 1700.0f, 0}, kEnt_C4, 8 };
static const ScenarioConn kConn_C5[] = { // 0x48d8b0
    {0x08,0x02,0x04,0x00}, {0x04,0x01,0x0f,0x01}, {0x0f,0x00,0x0c,0x03}, {0xff,0xff,0xff,0xff} };
static const ScenarioEntity kEnt_C5[] = { // 0x48d8f0
    {0x08, 0x38314e42 /*BN18*/, 1200.0f, 0.0f, 600.0f, 0x0},
    {0x08, 0x38314e42 /*BN18*/, 1100.0f, 0.0f, 900.0f, 0x0},
    {0x08, 0x38304d57 /*WM08*/, 600.0f, 0.0f, 800.0f, 0x0},
    {0x08, 0x38314e42 /*BN18*/, 800.0f, 0.0f, 1000.0f, 0x0},
    {0x08, 0x38314e42 /*BN18*/, 600.0f, 0.0f, 1400.0f, 0x0},
    {0x04, 0x38314e42 /*BN18*/, 1000.0f, 0.0f, 800.0f, 0x0},
    {0x04, 0x38314e42 /*BN18*/, 600.0f, 0.0f, 1000.0f, 0x0},
    {0x04, 0x41383145 /*E18A*/, 1100.0f, 0.0f, 1400.0f, 0x0},
    {0x04, 0x41383145 /*E18A*/, 1100.0f, 0.0f, 1400.0f, 0x0},
    {0x04, 0x41383145 /*E18A*/, 1100.0f, 0.0f, 1400.0f, 0x0},
    {0x04, 0x41383145 /*E18A*/, 1100.0f, 0.0f, 1400.0f, 0x0},
    {0x04, 0x41383145 /*E18A*/, 1100.0f, 0.0f, 1400.0f, 0x0},
    {0x04, 0x41383145 /*E18A*/, 1100.0f, 0.0f, 1400.0f, 0x0},
    {0x04, 0x38314e42 /*BN18*/, 900.0f, 0.0f, 1700.0f, 0x0},
    {0x0f, 0x38314e42 /*BN18*/, 2200.0f, 0.0f, 2000.0f, 0x0},
    {0x0f, 0x38314e42 /*BN18*/, 1600.0f, 0.0f, 2200.0f, 0x0},
    {0x0f, 0x38314e42 /*BN18*/, 1000.0f, 0.0f, 1200.0f, 0x0},
    {0x0f, 0x38314e42 /*BN18*/, 800.0f, 0.0f, 800.0f, 0x0},
    {0xff, 0, 0.0f, 0.0f, 0.0f, 0} };
static const ScenarioLayout kLayout_C5 = { 0x48dab8, kConn_C5, {8, 0x00000000, 1000.0f, 0.0f, 800.0f, 0}, {12, 0x00000000, 600.0f, 0.0f, 900.0f, 0}, kEnt_C5, 18 };
static const ScenarioConn kConn_C6[] = { // 0x48dac0
    {0x0e,0x02,0x0c,0x00}, {0xff,0xff,0xff,0xff} };
static const ScenarioEntity kEnt_C6[] = { // 0x48db00
    {0x0c, 0x41383145 /*E18A*/, 800.0f, 0.0f, 1600.0f, 0x0},
    {0xff, 0, 0.0f, 0.0f, 0.0f, 0} };
static const ScenarioLayout kLayout_C6 = { 0x48db30, kConn_C6, {14, 0x00000000, 1200.0f, 0.0f, 800.0f, 0}, {12, 0x00000000, 1600.0f, 300.0f, 800.0f, 0}, kEnt_C6, 1 };
static const ScenarioConn kConn_C7[] = { // 0x48db38
    {0x09,0xff,0xff,0xff}, {0xff,0xff,0xff,0xff} };
static const ScenarioEntity kEnt_C7[] = { // 0x48db70
    {0xff, 0, 0.0f, 0.0f, 0.0f, 0} };
static const ScenarioLayout kLayout_C7 = { 0x48db88, kConn_C7, {9, 0x00000000, 700.0f, 0.0f, 700.0f, 0}, {9, 0x00000000, 1200.0f, 0.0f, 1200.0f, 0}, kEnt_C7, 0 };
const ScenarioLayout* const kScenarioLayoutsA[7] = { // 0x44a1b0
    &kLayout_A1, &kLayout_A2, &kLayout_A3, &kLayout_A4, &kLayout_A5, &kLayout_A6, &kLayout_A7 };
const ScenarioLayout* const kScenarioLayoutsB[7] = { // 0x44a1d0
    &kLayout_B1, &kLayout_B2, &kLayout_B3, &kLayout_B4, &kLayout_B5, &kLayout_B6, &kLayout_B7 };
const ScenarioLayout* const kScenarioLayoutsC[7] = { // 0x44a1f0
    &kLayout_C1, &kLayout_C2, &kLayout_C3, &kLayout_C4, &kLayout_C5, &kLayout_C6, &kLayout_C7 };
}
// ---- hand-copied small tables (tools/disasm.py word reads; tag words are built at runtime by the owning function)
namespace baroque::game::tables {
namespace {
constexpr uint32_t T(const char (&s)[5]) {
    return static_cast<uint32_t>(static_cast<uint8_t>(s[0])) | (static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(s[3])) << 24);
}
}
// 0x48dbe0: flag ids {192,184,174,186,178,182,190,180,176,188,5}; tags from 0x44a488.. in FUN_00120f70
const FlagTag kC05Table[11] = {
    {192, T("C05A")}, {184, T("C05B")}, {174, T("C05F")}, {186, T("C05K")}, {178, T("C05E")}, {182, T("C05I")},
    {190, T("C05G")}, {180, T("C05C")}, {176, T("C05H")}, {188, T("C05J")}, {5, T("C05D")}};
// 0x44a500: 02 1f .. LQ22 (1950, 0, 1450) ; 06 1e .. MT00 (550, 0, 400)
const FixedItem kFixedItems[2] = {{2, 0x1f, T("LQ22"), 1950.0f, 0.0f, 1450.0f}, {6, 0x1e, T("MT00"), 550.0f, 0.0f, 400.0f}};
// 0x48dca0: PT10 (-584.74, -2100) PT11 (1600, -2600) PT12 (-780, 2700)
const TownStairs kTownStairs[3] = {{T("PT10"), -584.739990234375f, -2100.0f}, {T("PT11"), 1600.0f, -2600.0f}, {T("PT12"), -780.0f, 2700.0f}};
// 0x44a210 (24 entries); tag words written by FUN_0011e7c0 on first use
const ExtraSpawn kExtraSpawns[24] = {
    {127, T("E24A"), 3}, {137, T("E27A"), 3}, {127, T("E25A"), 3}, {140, T("E26A"), 3}, {440, T("E25A"), 3}, {455, T("E27B"), 3},
    {456, T("E27B"), 3}, {493, T("E26C"), 3}, {469, T("E26B"), 3}, {444, T("E26A"), 3}, {489, T("E25C"), 3}, {445, T("E26A"), 3},
    {483, T("E25C"), 3}, {426, T("E24A"), 3}, {430, T("E24A"), 3}, {433, T("E27A"), 3}, {498, T("E26C"), 3}, {438, T("E27A"), 3},
    {439, T("E25A"), 3}, {446, T("E24B"), 3}, {460, T("E25B"), 3}, {466, T("E26B"), 3}, {471, T("E24C"), 3}, {479, T("E27C"), 3}};
// 0x463eb0 -> strings (15 entries; the 16th pointer is not a prefix)
const char* const kItemPrefixes[15] = {"SD", "CT", "WG", "HT", "MT", "BX", "LQ", "ST", "TL", "TP", "BN", "WM", "GN", "IS", "CC"};
}
