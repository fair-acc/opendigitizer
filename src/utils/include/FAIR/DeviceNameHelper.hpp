#ifndef OPENDIGITIZER_FAIR_DEVICE_NAME_HELPER_HPP
#define OPENDIGITIZER_FAIR_DEVICE_NAME_HELPER_HPP

#include <algorithm>
#include <array>
#include <fmt/core.h>
#include <stdexcept>
#include <string_view>

namespace fair {

constexpr inline std::array<std::string_view, 630> testDeviceNames = {"1S00KS2CV", "1S00MH", "1S00QD1F", "1S11KH1", "1S11KV1", "GE00BE_F", "GE01BU1", "GE01BU1F", "GE01BU2", "GE01BU2F", "GE01KP02", "GE01KP03", "GE01KP04", "GE01KP05", "GE01KP06", "GE01KP07", "GE01KP08", "GE01KP09", "GE01KP10", "GE01KP17", "GE01KP18", "GE01KP19", "GE01KP20", "GE01KP21", "GE01KP22", "GE01KP23", "GE01KP24", "GE01KS1", "GE01KS2", "GE01KS3", "GE01KS4", "GE01KX1", "GE01KX2", "GE01KX3", "GE01KX4", "GE01KX5", "GE01KX6", "GE01KY1", "GE01KY2", "GE01KY3", "GE01KY4", "GE01MU0R", "GE01MU1", "GE01MU4I", "GE01MU5L", "GE01QS0D", "GE01QS1F", "GE01QS2F", "GE01QS3D", "GE01QS4F", "GE01QS5F", "GE01QS6D", "GE01QS7F", "GE01QS8F", "GE01QS9D", "GE02BE1", "GE02BE1F", "GE02KS1", "GE02KS2", "GE02KS3", "GE02KS4", "GE02KX1", "GE02KX2", "GE02KX3", "GE02KX4", "GE02KX5", "GE02KX6", "GE02KY1", "GE02KY2", "GE02KY3", "GE02KY4", "GE02MU0L", "GECD001", "GECD002", "GECD003", "GECD004", "GECD008", "GECD009", "GECD010", "GECD011", "GECD012", "GECD013", "GECD014", "GECD015", "GECD016", "GECD017", "GECD018", "GECD019", "GECD020", "GECD021", "GECD022", "GECD023", "GECD024", "GECD025", "GECD026", "GECD027", "GECD028", "GECEBG1D", "GECEBG1E", "GECEBG2D", "GECEBG2T", "GECEBG3D", "GECEBG4D", "GECEBG5T", "GECEBG6T", "GECEBG7T", "GECEBG8T", "GECEKD1D", "GECEKD2D", "GECEKT1G", "GECEKT3C", "GECEKT4C", "GECEKT5K", "GECEKX1", "GECEKX3G", "GECEKX4K", "GECEKX5C", "GECEKY1", "GECEKY3G", "GECEKY4K", "GECEKY5C", "GECEKY6G", "GECEKY7K", "GECEKY8G", "GECEKY9K", "GECEMO1P", "GECEMO3C", "GECEMO4K", "GECEMT3G", "GEITKX1", "GEITKX1_H", "GEITKX2", "GEITKX2_H", "GEITQT11", "GEITQT12", "GEITQT13", "GHADKX1", "GHADKY1", "GHADKY2", "GHADKY3", "GHADKY4", "GHADMU1", "GHADMU2", "GHADQD11", "GHADQD12", "GHADQD21", "GHADQD22", "GHADQD31", "GHADQD32", "GHADQD41", "GHADQD42", "GHADQT51", "GHADQT52", "GHADQT53", "GHFSKS1", "GHFSKY1", "GHFSMU1", "GHFSMU1_0", "GHFSQT11", "GHFSQT12", "GHFSQT13", "GHHDKX1", "GHHDKX2", "GHHDKY1", "GHHDKY2", "GHHTKV1", "GHHTKV2",
    "GHHTMU1", "GHHTMU1_0", "GHHTMU2", "GHHTMU3", "GHHTMU4", "GHHTQD11", "GHHTQD12", "GHHTQD21", "GHHTQD22", "GHHTQT31", "GHHTQT32", "GHHTQT33", "GHTAKX1", "GHTAKY1", "GHTAMU1", "GHTAMU2", "GHTAQD11", "GHTAQD12", "GHTAQD21", "GHTAQD22", "GHTBKH1", "GHTBKH2", "GHTBKV2", "GHTBMU1", "GHTBQD11", "GHTBQD12", "GHTCKY1", "GHTCKY2", "GHTCKY3", "GHTCMU1", "GHTCQD11", "GHTCQD12", "GHTCQT21", "GHTCQT22", "GHTCQT23", "GHTDMU1", "GHTMKX1", "GHTMKX2", "GHTMKY1", "GHTMKY2", "GHTMMU1", "GHTMMU2", "GHTMQD11", "GHTMQD12", "GHTMQD21", "GHTMQD22", "GHTMQD31", "GHTMQD32", "GHTTKY1", "GHTTQD11", "GHTTQD12", "GHTYKH1", "GHTYKH2", "GHTYKH4", "GHTYKV1", "GHTYKV2", "GHTYKV3", "GHTYKV4", "GHTYMH1", "GHTYMH2", "GHTYQD11", "GHTYQD12", "GHTYQD21", "GHTYQD22", "GHTYQD31", "GHTYQD32", "GHTYQD41", "GHTYQD42", "GS00BE_F", "GS00BS1W", "GS01BO1EH", "GS01KM2DV", "GS01KM3QS", "GS01KS1C", "GS01KS3C", "GS01MU1A", "GS01QS1F", "GS01QS2D", "GS02BB1F", "GS02BE1", "GS02BE1F", "GS02KM2DV", "GS02KM3QS", "GS02KM5SS", "GS02KQ1E", "GS02KQ4", "GS02MU1A", "GS03KH1I", "GS03KH2I", "GS03KH3I", "GS03KM2DV", "GS03KS1C", "GS03KS3C", "GS03KV1I", "GS03KV2I", "GS03KV3I", "GS03MU1A", "GS04KM2DV", "GS04KM3QS", "GS04KQ4", "GS04MU1A", "GS04MU2A", "GS05KM2DV", "GS05KS1C", "GS05KS3C", "GS05MU1A", "GS05MU2A", "GS06KM2DV", "GS06KM3QS", "GS06MU1A", "GS06MU2A", "GS06MU4", "GS07BE3", "GS07BE3F", "GS07BE4", "GS07BE4F", "GS07BE5", "GS07BE5F", "GS07KM2DV", "GS07KM3QS", "GS07KS1C", "GS07KS3C", "GS07MU1A", "GS07MU2A", "GS08BE2", "GS08BE2F", "GS08KM2DV", "GS08KM3QS", "GS08KM5SS", "GS08KQ4", "GS08MU1A", "GS09KM2DV", "GS09KS1C", "GS09KS3C", "GS09MU1A", "GS10KM2DV", "GS10KM3QS", "GS10KQ4", "GS10KX1", "GS10KX2", "GS10MU1A", "GS10MU2A", "GS11KM2DV", "GS11KS1C", "GS11KS3C", "GS11MU1A", "GS11MU2", "GS11MU2A", "GS12KM2DV", "GS12KM3QS", "GS12MU1A", "GS12MU2A", "GS12QS1F", "GS12QS2D", "GS12QS3T", "GSCD001", "GSCD002", "GSCD005", "GSCD012", "GSCD013", "GSCD014", "GSCD015", "GSCD016", "GSCD017", "GSCD018", "GSCD019", "GSCD021", "GSCD022", "GSCD023",
    "GSCD024", "GSCD025", "GSCD027", "GSCD028", "GSCD029", "GSCD031", "GSCD032", "GSCD034", "GSCD035", "GSCD038", "GSCD040", "GSCD042", "GSCD043", "GSCD044", "GSCD045", "GSCD047", "GSCD049", "GSCD050", "GSCD051", "GSCD053", "GSCD054", "GSCD057", "GSCD058", "GSCD059", "GSCD060", "GSCEBG1D", "GSCEBG1E", "GSCEBG2D", "GSCEBG2T", "GSCEBG3D", "GSCEBG3T", "GSCEBG4D", "GSCEBG4T", "GSCEBG5T", "GSCEKD1D", "GSCEKD2D", "GSCEKX1G", "GSCEKX3C", "GSCEKX5K", "GSCEKY1G", "GSCEKY2G", "GSCEKY3C", "GSCEKY4K", "GSCEKY5K", "GSCEMO3C", "GSCEMO5K", "GSCEMT4K", "GTE1KY1", "GTE1QD11", "GTE1QD12", "GTE2KX1", "GTE2QT11", "GTE2QT12", "GTE2QT13", "GTE3KY1", "GTE3MU1", "GTE3MU1_0", "GTE3QD11", "GTE3QD12", "GTE4KX3", "GTE4KY1", "GTE4KY2", "GTE4KY3", "GTE4MU1", "GTE4MU2", "GTE4QD11", "GTE4QD12", "GTE4QT21", "GTE4QT22", "GTE4QT23", "GTE4QT31", "GTE4QT32", "GTE4QT33", "GTE5KS1", "GTE5KY1", "GTE5MU0", "GTE5QD11", "GTE5QD12", "GTE5QD21", "GTE5QD22", "GTH1KX1", "GTH1KY1", "GTH1QD11", "GTH1QD12", "GTH2KX1", "GTH2KY1", "GTH2QD11", "GTH2QD12", "GTH2QD21", "GTH2QD22", "GTH3KY1", "GTH3MK1", "GTH3MU1", "GTH3QD11", "GTH3QD12", "GTH4KS1", "GTH4KY1", "GTH4MU1", "GTH4MU1_0", "GTH4MU2", "GTH4QD11", "GTH4QD12", "GTH4QD21", "GTH4QD22", "GTH4QD31", "GTH4QD32", "GTK7MU5", "GTP1KY1", "GTP1KY2", "GTP1MU1", "GTP1QD11", "GTP1QD12", "GTR1KYA", "GTR1KYB", "GTR1MU0", "GTR1MU1", "GTR1QD11", "GTR1QD12", "GTR2KX1", "GTR2KX2", "GTR2KX3", "GTR2KY1", "GTR2KY2", "GTR2KY3", "GTR2QT21", "GTR2QT22", "GTR2QT23", "GTR3KX4", "GTR3KX5", "GTR3KY4", "GTR3KY5", "GTR3KY6", "GTR3KY7", "GTR3QD41", "GTR3QD42", "GTR3QD51", "GTR3QD52", "GTR3QT31", "GTR3QT32", "GTR3QT33", "GTS1KY1", "GTS1MU1", "GTS1MU1_0", "GTS1MU2", "GTS1QD11", "GTS1QD12", "GTS2KS1", "GTS2KY1", "GTS2QT11", "GTS2QT12", "GTS2QT13", "GTS3KS1", "GTS3KS2", "GTS3KS3", "GTS3KY1", "GTS3KY2", "GTS3MU1", "GTS3MU1_0", "GTS3QD11", "GTS3QD12", "GTS3QD21", "GTS3QD22", "GTS3QT31", "GTS3QT32", "GTS3QT33", "GTS4KS1", "GTS4KS2", "GTS4KS3", "GTS4KY1", "GTS4KY2", "GTS4MU1", "GTS4QD21", "GTS4QD22",
    "GTS4QD31", "GTS4QD32", "GTS4QT11", "GTS4QT12", "GTS4QT13", "GTS5KS1", "GTS5KY1", "GTS5MU1", "GTS5QT11", "GTS5QT12", "GTS5QT13", "GTS6MU1", "GTS6MU1_0", "GTS7KS1", "GTS7KY1", "GTS7KY2", "GTS7MU1", "GTS7MU1_0", "GTS7QD11", "GTS7QD12", "GTT1KX2", "GTT1KY1", "GTT1KY2", "GTT1MU0", "GTT1MU1", "GTT1QD11", "GTT1QD12", "GTT1QD21", "GTT1QD22", "GTT1QD31", "GTT1QD32", "GTV1MU1", "GTV1MU2", "GTV2MU1", "GTV2MU2", "GTV2MU3", "GTV2QD11", "GTV2QD12", "GUCD001", "YR00BE_F", "YR00MH", "YR00QS1", "YR00QS2", "YR01MP1I", "YR02KD", "YR02KH", "YR02KS1", "YR02KS2", "YR02KV", "YR03BG0E", "YR03BG1E", "YR03BG2E", "YR03BG3T", "YR03BG3TS", "YR03BG4T", "YR03BG5T", "YR03BG6T", "YR03BG7T", "YR03BG7TL", "YR03DX1K", "YR03KD1D", "YR03KD2D", "YR03KH1", "YR03KH3G", "YR03KH4K", "YR03KH5C", "YR03KV3G", "YR03KV4K", "YR03KV5C", "YR03KV6G", "YR03KV7K", "YR03MO3C", "YR04KH", "YR04KS1", "YR04KS2", "YR04KV", "YR05BE1", "YR06KH", "YR06KS1", "YR06KS2", "YR07KV", "YR07MP1E", "YR08KH", "YR08KS1", "YR08KS2", "YR08KV", "YR10KH", "YR10KS1", "YR10KS2", "YR10KV", "YR12KH", "YR12KS1", "YR12KS2", "YR12KV", "YRCD001", "YRCD002", "YRCD003", "YRCD004", "YRCD005", "YRT1IN1E", "YRT1IN1K", "YRT1IN1M", "YRT1IQ1H", "YRT1IQ1O", "YRT1IZ1EP", "YRT1KH1", "YRT1KH2", "YRT1KV1", "YRT1KV2", "YRT1LD51H", "YRT1LD51V", "YRT1LD52H", "YRT1LD52V", "YRT1LE1", "YRT1MH1", "YRT1MH2", "YRT1QD61", "YRT1QD62", "YRT1QD71", "YRT1QD72", "ZZCD002"};

struct DeviceInfo {
    std::string_view name{};
    std::string_view location{};
    std::string_view section{};
    std::string_view deviceFunction{};
    std::string_view sequence{};
    std::string_view devicePosition{};
};

/**
 * @brief GSI/FAIR device name to human-readable info conversion
 * based on: https://www-acc.gsi.de/wiki/pub/Accnomen/WebHome/acc-nomen.pdf
 *
 * @param deviceName device name e.g. 'GS11MU2'
 * @return DeviceInfo meta information
 */
constexpr DeviceInfo getDeviceInfo(std::string_view deviceName) {
    if (deviceName.size() < 2) {
        throw std::invalid_argument("Device name must be at least 2 characters long");
    }

    DeviceInfo info = {.name = deviceName};

    static constexpr std::array<std::pair<std::string_view, std::array<std::pair<std::string_view, std::string_view>, 22>>, 35> locations = {{
        //
        {"1S", {{
                   {"", "SIS100"},    //
                   {"00", "(global"}, //
                   {"CD", "(global"}, //
               }}},                   //
        {"3S", {{
                   {"", "SIS300"},     //
                   {"00", "(global)"}, //
                   {"CD", "(global)"}, //
               }}},                    //
        {"AG", {{
                   {"", "alarm (generic)"},         //
                   {"E-", "alarm ESR Complex"},     //
                   {"H-", "alarm GSI target hall"}, //
                   {"S-", "alarm SIS18 complex"},   //
                   {"T-", "alarm transfer lines"},  //
                   {"U-", "alarm UNILAC"},          //
               }}},                                 //
        {"CR", {{
                   {"", "Collector Ring"}, //
                   {"00", "(global)"},     //
                   {"CD", "(global)"},     //
               }}},                        //
        {"D.", {{
                   {"", "Cable Duct"}, //
               }}},                    //
        {"ER", {{
                   {"", "Electron Ring"}, //
                   {"00", "(global)"},    //
                   {"CD", "(global)"},    //
               }}},                       //
        {"GE", {{
                   {"", "ESR"},                       //
                   {"00", "(global)"},                //
                   {"01", "1st arc"},                 //
                   {"EC", "electron cooler ()"},      //
                   {"02", "2nd arc"},                 //
                   {"XE", "experimental section ()"}, //
                   {"CD", "(global)"},                //
               }}},                                   //
        {"GH", {{
                   {"", "exp. target hall"},               //
                   {"HD", "high-dosage beam line"},        //
                   {"HT", "high-temperature beam line"},   //
                   {"FS", "fragment separator beam line"}, //
                   {"TA", "exp. beam line: cave A"},       //
                   {"TB", "exp. beam line: cave B"},       //
                   {"TP", "dump and test line"},           //
                   {"TM", "medical beam line"},            //
                   {"AD", "HADES"},                        //
                   {"TD", "beam line GHTDMU1->GHTD"},      //
               }}},                                        //
        {"GS", {{
                   {"", "SIS18"},                          //
                   {"00", "(global)"},                     //
                   {"01", "1st period"},                   //
                   {"02", "2nd period"},                   //
                   {"03", "3rd period"},                   //
                   {"04", "4th period"},                   //
                   {"05", "5th period"},                   //
                   {"06", "6th period"},                   //
                   {"07", "7th period"},                   //
                   {"08", "8th period (electron cooler)"}, //
                   {"09", "9th period"},                   //
                   {"10", "10th period"},                  //
                   {"11", "11th period"},                  //
                   {"12", "12th period"},                  //
                   {"EC", "electron cooler (GS08)"},       //
                   {"CE", "cooling section (GS08)"},       //
                   {"CD", "(global)"},                     //
               }}},                                        //
        {"GT", {{
                   {"", "beam line"},                      //
                   {"K1", "GU->SIS18: seg 1"},             //
                   {"K2", "GU->SIS18: seg 2"},             //
                   {"K3", "GU->SIS18: seg 3"},             //
                   {"K4", "GU->SIS18: seg 4"},             //
                   {"K5", "GU->SIS18: seg 5"},             //
                   {"K6", "GU->SIS18: seg 6"},             //
                   {"K7", "GU->SIS18: seg 7"},             //
                   {"K8", "GU->SIS18: seg 8"},             //
                   {"K9", "GU->SIS18: seg 8"},             //
                   {"KD", "GU->SIS18: diagnostics"},       //
                   {"KG", "GU->SIS18: straight"},          //
                   {"KU", "GU->SIS18: charge separation"}, //
                   {"R1", "re-injection ESR->SIS18"},      //
                   {"R2", "HITRAP: seg 2"},                //
                   {"R3", "HITRAP: seg 3"},                //
                   {"R4", "HITRAP: seg 4"},                //
                   {"R5", "HITRAP: seg 5"},                //
                   {"R6", "HITRAP: seg 6"},                //
                   {"R7", "HITRAP: seg 7"},                //
                   {"RS", "EBIT ion source"},              //
                   {"CD", "(global)"},                     //
               }}},                                        //
        {"GU", {{
                   {"", "UNILAC"},     //
                   {"00", "(global)"}, //
                   {"CD", "(global)"}, //
               }}},                    //
        {"HR", {{
                   {"", "HESR"},       //
                   {"00", "(global)"}, //
                   {"CD", "(global)"}, //
               }}},                    //
        {"YR", {{
                   {"", "CRYRING"},                        //
                   {"00", "(global)"},                     //
                   {"01", "1st period"},                   //
                   {"02", "2nd period"},                   //
                   {"03", "3rd period"},                   //
                   {"04", "4th period"},                   //
                   {"05", "5th period"},                   //
                   {"06", "6th period"},                   //
                   {"07", "7th period"},                   //
                   {"08", "8th period (electron cooler)"}, //
                   {"09", "9th period"},                   //
                   {"10", "10th period"},                  //
                   {"11", "11th period"},                  //
                   {"12", "12th period"},                  //
                   {"T1", "YRT1IQ->YRT1MH2"},              //
                   {"CD", "(global)"},                     //
               }}} //
    }};

    // Function specifier mappings based on technical groups
    static constexpr std::array<std::pair<std::string_view, std::array<std::pair<std::string_view, std::string_view>, 10>>, 205> functionSpecifiers = {{
        {"AK", {{
                   {"", "vacuum chamber"}, //
               }}},                        //
        {"BA", {{
                   {"", "RF Alvarez structure"}, //
                   {"T", "RF Alvarez tank"},     //
               }}},                              //
        {"BB", {{
                   {"A", "Alvarez accelerator structure"}, //
                   {"A-T", "tank of Alvarez cavity"},      //
                   {"C", "RF control"},                    //
                   {"F", "RF cavity frequency"},           //
                   {"R", "RF cavity resonance frequency"}, //
                   {"T", "tank of cavity"},                //
               }}},                                        //
        {"BC", {{
                   {"", "RF chopper"},                       //
                   {"-LC", "e-static chopper (in general)"}, //
                   {"-L", "chopper slow (100 Hz...5 MHz)"},  //
               }}},                                          //
        {"BE", {{
                   {"", "RF cavity"},                 //
                   {"A", "RF cavity amplitude ramp"}, //
                   {"C", "RF control"},               //
                   {"D", "delay"},                    //
                   {"F", "RF cavity frequency ramp"}, //
               }}},                                   //
        {"BF", {{
                   {"", "RF-feedback system"},              //
                   {"A", "feedback system amplitude ramp"}, //
                   {"F", "feedback system frequency ramp"}, //
                   {"H", "horizontal component"},           //
                   {"L", "longitudinal component"},         //
               }}},                                         //
        {"BG", {{
                   {"", "RF-gap"},                  //
                   {"D", "drift tube"},             //
                   {"E", "high voltage generator"}, //
                   {"T", "power supply"},           //
                   {"TL", "current limit"},         //
               }}},                                 //
        {"BH", {{
                   {"", "RF coupled-H structure"},       //
                   {"T", "tank of coupled-H-structure"}, //
               }}},                                      //
        {"BI", {{
                   {"", "RF interdigital-H structure"}, //
               }}},                                     //
        {"BK", {{
                   {"", "RF kicker (stoch. cooling)"},                             //
                   {"H", "kicker for stochastic cooling, horizontal structure"},   //
                   {"L", "kicker for stochastic cooling, longitudinal structure"}, //
                   {"T", "tank of kicker cavity"},                                 //
                   {"V", "kicker for stochastic cooling, vertical structure"},     //
               }}},                                                                //
        {"BO", {{
                   {"", "RF K.O. exciter"},                            //
                   {"E", "RF K.O. extraction"},                        //
                   {"EH", "RF K.O. extraction horizontal electrodes"}, //
                   {"EV", "RF K.O. extraction vertical electrodes"},   //
               }}},                                                    //
        {"BP", {{
                   {"", "RF pick-up (stoch. cooling)"},                            //
                   {"H", "pickup for stochastic cooling, horizontal structure"},   //
                   {"K", "pickup chamber"},                                        //
                   {"L", "pickup for stochastic cooling, longitudinal structure"}, //
                   {"V", "pickup for stochastic cooling, vertical structure"},     //
               }}},                                                                //
        {"BR", {{
                   {"", "RF quadrupole (RFQ)"}, //
                   {"T", "tank of RFQ"},        //
               }}},                             //
        {"BS", {{
                   {"", "RF spiller smoothing cavity"},     //
                   {"W", "spill smoothing by tune wobble"}, //
               }}},                                         //
        {"BT", {{
                   {"", "RF cooling trap"}, //
               }}},                         //
        {"BU", {{
                   {"", "RF barrier bucket cavity"}, //
                   {"U", "revolution frequency"},    //
                   {"D", "delay"},                   //
               }}},                                  //
        {"BW", {{
                   {"", "RF Widerøe structure"}, //
               }}},                              //
        {"CC", {{
                   {"", "19\" rack"}, //
               }}},                   //
        {"CD", {{
                   {"", "gen. digitizer DAQ"}, //
               }}},                            //
        {"CG", {{
                   {"", "group µ-controller"}, //
               }}},                            //
        {"CI", {{
                   {"", "micro-IOC"}, //
               }}},                   //
        {"CP", {{
                   {"", "PLC controller"}, //
               }}},                        //
        {"CS", {{
                   {"", "peripheral control processor"}, //
               }}},                                      //
        {"CT", {{
                   {"", "terminal server"}, //
               }}},                         //
        {"CU", {{
                   {"", "Scalable Control Unit (SCU)"}, //
               }}},                                     //
        {"CX", {{
                   {"", "X86 Group µ-processor"}, //
               }}},                               //
        {"CZ", {{
                   {"", "ZKS access control system"}, //
               }}},                                   //
        {"DA", {{
                   {"", "diagnostic device (general)"}, //
               }}},                                     //
        {"DB", {{
                   {"", "aperture, bezel (general)"},            //
                   {"A", "aperture (F)"},                        //
                   {"D", "rotatable attenuator (P)"},            //
                   {"E", "lens aperture at end of chamber (F)"}, //
                   {"F", "fixed aperture (P)"},                  //
               }}},                                              //
        {"DC", {{
                   {"", "beam Faraday cup"},                                  //
                   {"V", "Faraday Cup for intensity trend measurements (P)"}, //
                   {"Z", "coaxial Faraday cup (impedance of Z = 50 Ω) (P)"},  //
               }}},                                                           //
        {"DD", {{
                   {"", "beam detector"},                 //
                   {"A", "outer detector (S)"},           //
                   {"H", "horizontal detector pair (S)"}, //
                   {"I", "inner detector (S)"},           //
                   {"O", "vertical upper detector (S)"},  //
               }}},                                       //
        {"DE", {{
                   {"", "beam emittance meas. system"},                                   //
                   {"H", "horizontal plane of emittance measurement system (S)"},         //
                   {"HG", "profile grid of horizontal emittance measurement system (S)"}, //
                   {"HS", "slit of horizontal emittance measurement system (S)"},         //
                   {"P", "pepper pot emittance measurement system (P)"},                  //
               }}},                                                                       //
        {"DP", {{
                   {"", "fluorescent beam screen"},                                             //
                   {"B", "bunch generator (generation of bunch signals for probe tests DP-X)"}, //
                   {"H", "phase probe for time measurement and rotation (Horizontal) (F)"},     //
                   {"I", "intensity measurement (bunch probe) (F)"},                            //
                   {"P", "phase control (RF phase probe) (F)"},                                 //
               }}},                                                                             //
        {"DG", {{
                   {"", "beam profile grid"},                        //
                   {"E", "profile grid in extraction chamber (P)"},  //
                   {"G", "profile grid with gas amplification (P)"}, //
                   {"H", "horizontal profile grid (P)"},             //
                   {"I", "profile grid in injection chamber (P)"},   //
               }}},                                                  //
        {"DH", {{
                   {"", "RF beam exciter"}, //
               }}},                         //
        {"DI", {{
                   {"", "ionisation beam monitor"},                                //
                   {"D", "thick plastic detector (P)"},                            //
                   {"E", "experimental detector (P)"},                             //
                   {"H", "ionisation monitor (horizontal measurement) (IPM) (F)"}, //
                   {"I", "ionisation chamber (P)"},                                //
               }}},                                                                //
        {"DK", {{
                   {"", "diagnostic chamber (general)"},                              //
                   {"D", "diagnostic elements in air (Dummy)"},                       //
                   {"P", "diagnostic chamber for profile measurement systems (IPM)"}, //
                   {"Q", "second IPM diagnostic chamber in one beamline segment"},    //
               }}},                                                                   //
        {"DL", {{
                   {"", "beam loss monitor"},                                  //
                   {"A", "beam loss monitor (outer position in rings) (F)"},   //
                   {"I", "beam loss monitor (inner position in rings) (F)"},   //
                   {"L", "beam loss monitor (left position of beamline) (F)"}, //
                   {"O", "beam loss monitor (upper position) (F)"},            //
               }}},                                                            //
        {"DO", {{
                   {"", "beam position monitor (BPM)"}, //
               }}},                                     //
        {"DQ", {{
                   {"", "beam current transformer (DCCT)"}, //
               }}},                                         //
        {"DR", {{
                   {"", "beam residual gas monitor"},                //
                   {"H", "horizontal residual gas profile monitor"}, //
                   {"V", "vertical residual gas profile monitor"},   //
               }}},                                                  //
        {"DS", {{
                   {"", "slit"},                                                                 //
                   {"A", "scraper with arbitrary angle (S)"},                                    //
                   {"D", "diagonal slit, delimits in horizontal and vertical direction (S)"},    //
                   {"F", "fixed collimator, delimits in horizontal and vertical direction (F)"}, //
                   {"H", "slit, scraper horizontal (S)"},                                        //
               }}},                                                                              //
        {"DT", {{
                   {"", "beam current transformer"},                                                   //
                   {"C", "transformer for charge (F) (resonant transformer)"},                         //
                   {"E", "transformer signal for beam loss measurement during emittance measurement"}, //
                   {"F", "transformer for fast current measurement (0.5 µs) (F)"},                     //
                   {"FL", "transformer for fast to slow current measurement (1 µs – DC) (F)"},         //
               }}},                                                                                    //
        {"DV", {{
                   {"", "veto counter"},                             //
                   {"H", "veto counter slit horizontal (S)"},        //
                   {"HL", "veto counter horizontal left slit (S)"},  //
                   {"HR", "veto counter horizontal right slit (S)"}, //
                   {"L", "veto counter (plastic) (S)"},              //
               }}},                                                  //
        {"DX", {{
                   {"", "beam position monitor"},                        //
                   {"C", "cold-environment beam position monitor (F)"},  //
                   {"H", "probe for horizontal measurement (F)"},        //
                   {"K", "correction voltage for position measurement"}, //
                   {"L", "probe for longitudinal measurement (F)"},      //
               }}},                                                      //
        {"DZ", {{
                   {"", "semiconductors-based Z-measurement"}, //
                   {"A", "absorber foil (S)"},                 //
                   {"T", "target (S)"},                        //
               }}},                                            //
        {"EA", {{
                   {"", "sequence control"}, //
               }}},                          //
        {"EC", {{
                   {"", "beam Faraday Cup (no cooling)"}, //
               }}},                                       //
        {"ED", {{
                   {"", "degrader device"},                 //
                   {"D", "degrader rotary wedge"},          //
                   {"L", "degrader ladder system (S)"},     //
                   {"V", "slidable wedge degrader (S)"},    //
                   {"VO", "vertical upper slidable wedge"}, //
               }}},                                         //
        {"EF", {{
                   {"", "fluorescent beam screen"}, //
               }}},                                 //
        {"EG", {{
                   {"", "beam profile grid"},                               //
                   {"T", "profile grid for single particle diagnosis (P)"}, //
               }}},                                                         //
        {"EK", {{
                   {"", "experimental chamber"}, //
               }}},                              //
        {"EM", {{
                   {"", "measurement device MuSIC"},      //
                   {"AV", "device anode high voltage"},   //
                   {"CV", "device cathode high voltage"}, //
                   {"D", "diamond detector"},             //
                   {"E", "energy measurement"},           //
               }}},                                       //
        {"EP", {{
                   {"", "vacuum pump (general)"},         //
                   {"M", "metal bellow compressor pump"}, //
               }}},                                       //
        {"ES", {{
                   {"", "beam scintillation detector"},                         //
                   {"H", "scraper/slit horizontal (S)"},                        //
                   {"HA", "scraper/slit horizontal outside stepper motor (S)"}, //
                   {"HI", "scraper/slit, horizontal inner stepper motor (S)"},  //
                   {"V", "slit, scraper vertical (S)"},                         //
               }}},                                                             //
        {"ET", {{
                   {"", "beam target"},                                        //
                   {"C", "collision target (S)"},                              //
                   {"CH", "collision target, horizontal stepper motor (S)"},   //
                   {"CZ", "collision target, z (beam) direction stepper (S)"}, //
                   {"D", "detector (same as target)"},                         //
               }}},                                                            //
        {"EV", {{
                   {"", "vacuum valve"},               //
                   {"P", "pneumatic gate valve"},      //
                   {"R", "regulatable control valve"}, //
               }}},                                    //
        {"EW", {{
                   {"", "beam wobbler"},               //
                   {"H", "(beam) wobbler horizontal"}, //
                   {"V", "(beam) wobbler vertical"},   //
               }}},                                    //
        {"EX", {{
                   {"", "position monitor at experiment"},                                      //
                   {"S", "probe for Schottky experiments (F)"},                                 //
                   {"SH", "probe for Schottky diagnose (Horizontal)"},                          //
                   {"SA", "horizontal Schottky diagnose (horizontal outer stepper motor) (S)"}, //
                   {"SI", "horizontal Schottky diagnose (horizontal inner stepper motor) (S)"}, //
               }}},                                                                             //
        {"GB", {{
                   {"", "control panel"}, //
               }}},                       //
        {"GE", {{
                   {"", "power amplifier"}, //
               }}},                         //
        {"GH", {{
                   {"", "high-power amplifier"}, //
               }}},                              //
        {"GK", {{
                   {"", "cooling"}, //
               }}},                 //
        {"GL", {{
                   {"", "ventilation"}, //
               }}},                     //
        {"GN", {{
                   {"", "power supply"}, //
               }}},                      //
        {"GR", {{
                   {"", "adaptive controller"}, //
               }}},                             //
        {"GT", {{
                   {"", "driver unit"}, //
               }}},                     //
        {"GV", {{
                   {"", "pre-amplifier"}, //
               }}},                       //
        {"IC", {{
                   {"", "Chordis ion source"}, //
               }}},                            //
        {"IE", {{
                   {"", "electron generation (indirect)"}, //
               }}},                                        //
        {"IG", {{
                   {"", "generator"}, //
               }}},                   //
        {"IH", {{
                   {"", "High current Ion Source"}, //
               }}},                                 //
        {"IK", {{
                   {"", "Compact PIG"}, //
               }}},                     //
        {"IL", {{
                   {"", "Laser Ions"}, //
               }}},                    //
        {"IM", {{
                   {"", "Ion Source Magnet"}, //
               }}},                           //
        {"IN", {{
                   {"", "Nielsen-type ion source"}, //
               }}},                                 //
        {"IP", {{
                   {"", "Penning-Ion Source (PIG)"}, //
               }}},                                  //
        {"IQ", {{
                   {"", "Ion source (general)"}, //
               }}},                              //
        {"IT", {{
                   {"", "EBIT (Electron Beam Ion Trap)"}, //
               }}},                                       //
        {"IX", {{
                   {"", "X-ray monitor"}, //
               }}},                       //
        {"IZ", {{
                   {"", "Electron Cyclotron Resonance ion source (ECR)"}, //
               }}},                                                       //
        {"KD", {{
                   {"", "copprector dipole (general)"}, //
               }}},                                     //
        {"KE", {{
                   {"", "correction coil in extraction system"}, //
               }}},                                              //
        {"KF", {{
                   {"", "ferrite RF mode damper"}, //
               }}},                                //
        {"KH", {{
                   {"", "horizontal correction dipole (steerer)"}, //
               }}},                                                //
        {"KK", {{
                   {"", "kicker for stochastic cooling and Schottky-diagnostics"}, //
               }}},                                                                //
        {"KM", {{
                   {"", "integrated correction multipoles"}, //
               }}},                                          //
        {"KO", {{
                   {"", "octupole"}, //
               }}},                  //
        {"KP", {{
                   {"", "pole face coil winding"}, //
               }}},                                //
        {"KQ", {{
                   {"", "correction coil in Quadrupole [GS, GE]"}, //
               }}},                                                //
        {"KS", {{
                   {"", "sextupole"}, //
               }}},                   //
        {"KT", {{
                   {"", "toroid"}, //
               }}},                //
        {"KU", {{
                   {"", "horizontal correction dipole (steerer)"}, //
               }}},                                                //
        {"KV", {{
                   {"", "vertical correction dipole (steerer)"}, //
               }}},                                              //
        {"KX", {{
                   {"", "decapole corrector"}, //
               }}},                            //
        {"KY", {{
                   {"", "vertical correction dipole (steerer)"}, //
               }}},                                              //
        {"LB", {{
                   {"", "e-static bumper"}, //
               }}},                         //
        {"LC", {{
                   {"", "e-static chopper"}, //
               }}},                          //
        {"LD", {{
                   {"", "e-static quadrupole doublet"},             //
                   {"H", "e-static quadrupole doublet horizontal"}, //
                   {"V", "e-static quadrupole doublet vertical"},   //
               }}},                                                 //
        {"LE", {{
                   {"", "e-static single lens"}, //
               }}},                              //
        {"LH", {{
                   {"", "e-static horizontal dipole"},        //
                   {"C", "e-static horizontal ion clearing"}, //
                   {"S", "e-static horizontal steerer"},      //
               }}},                                           //
        {"LK", {{
                   {"", "e-static kicker"}, //
               }}},                         //
        {"LP", {{
                   {"", "e-static septum"},                               //
                   {"A", "anode stepper motor for of e-static septum"},   //
                   {"E", "e-static septum for extraction"},               //
                   {"I", "e-static septum for injection"},                //
                   {"K", "cathode stepper motor for of e-static septum"}, //
               }}},                                                       //
        {"LQ", {{
                   {"", "e-static quadrupole quadruplet"}, //
               }}},                                        //
        {"LS", {{
                   {"", "e-static quadrupole singulet"}, //
               }}},                                      //
        {"LT", {{
                   {"", "e-static quadrupole triplet"}, //
               }}},                                     //
        {"LV", {{
                   {"", "e-static vertical dipole"},        //
                   {"C", "e-static vertical ion clearing"}, //
                   {"S", "e-static vertical steerer"},      //
               }}},                                         //
        {"MA", {{
                   {"", "angular dipole magnet"}, //
               }}},                               //
        {"MB", {{
                   {"", "bumper magnet"},             //
                   {"H", "bumper magnet horizontal"}, //
                   {"V", "bumper magnet vertical"},   //
               }}},                                   //
        {"MC", {{
                   {"", "magnetic horn (collector)"}, //
               }}},                                   //
        {"MD", {{
                   {"", "permanent magnet"},             //
                   {"Q", "permanent magnet quadruplet"}, //
               }}},                                      //
        {"ME", {{
                   {"", "e-static septum/lens"},                       //
                   {"A", "anode stepper motor for e-static septum"},   //
                   {"E", "e-static septum for extraction"},            //
                   {"I", "e-static septum for injection"},             //
                   {"K", "cathode stepper motor for e-static septum"}, //
               }}},                                                    //
        {"MH", {{
                   {"", "horizontal bending magnet"},                 //
                   {"K", "correction coil for dipoles"},              //
                   {"A", "auxiliary dipole coil"},                    //
                   {"B", "B-train of reference magnet"},              //
                   {"E", "horizontal bending magnet for extraction"}, //
               }}},                                                   //
        {"MK", {{
                   {"", "Kicker"},                    //
                   {"E", "kicker for extraction"},    //
                   {"I", "kicker for injection"},     //
                   {"Q", "kicker for Q measurement"}, //
                   {"T", "kicker for transfer"},      //
               }}},                                   //
        {"MM", {{
                   {"", "magnet chamber for magnetic septum"}, //
               }}},                                            //
        {"MO", {{
                   {"", "solenoid"},            //
                   {"C", "cooling solenoid"},   //
                   {"G", "gun solenoid"},       //
                   {"H", "Helmholtz coil"},     //
                   {"K", "collector solenoid"}, //
               }}},                             //
        {"MP", {{
                   {"", "magnetic septum"},                            //
                   {"E", "septum magnet for extraction (horizontal)"}, //
                   {"I", "septum magnet for injection (horizontal)"},  //
                   {"L", "Lambertson septum"},                         //
                   {"S", "septum for slow extraction"},                //
               }}},                                                    //
        {"MQ", {{
                   {"", "Q measurement kicker"}, //
               }}},                              //
        {"MS", {{
                   {"", "steering magnet"},                                       //
                   {"H", "horizontal steering magnet (see KH for [GS, GE, GT])"}, //
                   {"V", "vertical steering magnet (see KV for [GS, GE, GT])"},   //
               }}},                                                               //
        {"MT", {{
                   {"", "toroid"},            //
                   {"C", "cooling toroid"},   //
                   {"G", "gun toroid"},       //
                   {"K", "collector toroid"}, //
               }}},                           //
        {"MU", {{
                   {"", "horizontal bending magnet"},                 //
                   {"K", "correction coil for dipoles"},              //
                   {"A", "auxiliary coil of dipole"},                 //
                   {"B", "B-train of reference magnet"},              //
                   {"E", "horizontal bending magnet for extraction"}, //
               }}},                                                   //
        {"MV", {{
                   {"", "vertical bending magnet"},                            //
                   {"0", "automatic field suppression (0 field) for dipoles"}, //
                   {"K", "correction coil for dipoles"},                       //
                   {"D", "vertical down bending magnet"},                      //
                   {"T", "vertical bending magnet for transfer"},              //
                   {"U", "vertical up bending magnet"},                        //
               }}},                                                            //
        {"MW", {{
                   {"", "wobbler"}, //
               }}},                 //
        {"PA", {{
                   {"", "Alvarez phase monitor"}, //
               }}},                               //
        {"PB", {{
                   {"", "(Re-)Buncher, helix phase monitor"}, //
               }}},                                           //
        {"PC", {{
                   {"", "chopper phase monitor (in general)"}, //
                   {"-L", "slow chopper"},                     //
               }}},                                            //
        {"PE", {{
                   {"", "single cavity phase monitor"}, //
               }}},                                     //
        {"PI", {{
                   {"", "IH phase monitor"}, //
               }}},                          //
        {"PP", {{
                   {"", "phase axis"},           //
                   {"36", "36 MHz phase axis"},  //
                   {"08", "108 MHz phase axis"}, //
               }}},                              //
        {"PR", {{
                   {"", "RFQ phase monitor"}, //
               }}},                           //
        {"PW", {{
                   {"", "RF Widerøe phase monitor"}, //
               }}},                                  //
        {"QD", {{
                   {"", "quadrupole doublet"}, //
               }}},                            //
        {"QG", {{
                   {"", "quadrupole group"}, //
               }}},                          //
        {"QQ", {{
                   {"", "quadruplet"}, //
               }}},                    //
        {"QS", {{
                   {"", "quadrupole singulet"}, //
               }}},                             //
        {"QT", {{
                   {"", "quadrupole triplet"}, //
               }}},                            //
        {"QX", {{
                   {"", "sextupole"}, //
               }}},                   //
        {"SB", {{
                   {"", "safety beam stopper"}, //
               }}},                             //
        {"SD", {{
                   {"", "beam dump"}, //
               }}},                   //
        {"SI", {{
                   {"", "interlock/safety installation"}, //
               }}},                                       //
        {"SM", {{
                   {"", "radiation safety monitor"}, //
               }}},                                  //
        {"ST", {{
                   {"", "radioactive transport container (hot cell)"}, //
               }}},                                                    //
        {"SV", {{
                   {"", "radiation protection shutter (block d’arrêt)"}, //
               }}},                                                      //
        {"TA", {{
                   {"", "RF amplitude measurement"}, //
               }}},                                  //
        {"TE", {{
                   {"", "RF coupling loop"}, //
               }}},                          //
        {"TK", {{
                   {"", "RF cooling"}, //
               }}},                    //
        {"TM", {{
                   {"", "RF amplitude measurement"}, //
               }}},                                  //
        {"TP", {{
                   {"", "RF tank phase measurement"}, //
               }}},                                   //
        {"TS", {{
                   {"", "RF pick-up loop (in general)"},           //
                   {"A", "RF pick-up loop for amplitude control"}, //
                   {"D", "RF pick-up loop for diagnostics"},       //
                   {"E", "RF pick-up loop for envelope"},          //
                   {"P", "RF pick-up loop for phase control"},     //
               }}},                                                //
        {"TT", {{
                   {"", "RF tank plunger"}, //
               }}},                         //
        {"TW", {{
                   {"", "RF tank water cooling"}, //
               }}},                               //
        {"UC", {{
                   {"", "beam catcher behind stripper"},         //
                   {"H", "horizontal beam catcher"},             //
                   {"HL", "horizontal left beam catcher (F-)"},  //
                   {"HR", "horizontal right beam catcher (F-)"}, //
                   {"S", "special beam catcher"},                //
               }}},                                              //
        {"UF", {{
                   {"", "foil stripper"}, //
               }}},                       //
        {"UG", {{
                   {"", "gas stripper"},             //
                   {"V", "video camera on gas jet"}, //
               }}},                                  //
        {"UI", {{
                   {"", "halo foil for ions"},                   //
                   {"H", "ion halo horizontal foil"},            //
                   {"HA", "ion halo horizontal outer foil (S)"}, //
                   {"HI", "ion halo foil horizontal inner (S)"}, //
                   {"I", "current (intensity) on halo foil"},    //
               }}},                                              //
        {"UP", {{
                   {"", "halo foil for protons"},                    //
                   {"H", "proton halo horizontal foil"},             //
                   {"HA", "proton halo horizontal outer foil (S)"},  //
                   {"HI", "proton halo horizontal inner foil (S)"},  //
                   {"I", "current (intensity) on proton halo foil"}, //
               }}},                                                  //
        {"UT", {{
                   {"", "conversion target"},                               //
                   {"C", "collision target (S)"},                           //
                   {"CH", "collision target horizontal stepper motor (S)"}, //
                   {"CZ", "collision target z direction stepper (S)"},      //
                   {"D", "detector (same as target)"},                      //
               }}},                                                         //
        {"UW", {{
                   {"", "Wien filter"}, //
               }}},                     //
        {"VA", {{
                   {"", "vacuum flow control"}, //
               }}},                             //
        {"VB", {{
                   {"", "vacuum aperture, diaphragm"},     //
                   {"H", "horizontal aperture diaphragm"}, //
                   {"V", "vertical aperture diaphragm"},   //
               }}},                                        //
        {"VC", {{
                   {"", "vacuum flange Connector, bellow"}, //
                   {"H", "horizontal flange connector"},    //
                   {"V", "vertical flange connector"},      //
               }}},                                         //
        {"VD", {{
                   {"", "vacuum Drift"},      //
                   {"H", "horizontal drift"}, //
                   {"V", "vertical drift"},   //
               }}},                           //
        {"VF", {{
                   {"", "vacuum remote control"}, //
               }}},                               //
        {"VG", {{
                   {"", "vacuum Gas inlet control"},      //
                   {"H", "horizontal gas inlet control"}, //
                   {"V", "vertical gas inlet control"},   //
               }}},                                       //
        {"VH", {{
                   {"", "vacuum Backing system, Heating jackets"}, //
               }}},                                                //
        {"VI", {{
                   {"", "vacuum Insulation vacuum"}, //
               }}},                                  //
        {"VK", {{
                   {"", "vacuum Chamber"}, //
               }}},                        //
        {"VM", {{
                   {"", "vacuum measurement devices"}, //
                   {"P", "pressure gauge"},            //
               }}},                                    //
        {"VO", {{
                   {"", "vacuum chamber for Octupole"}, //
               }}},                                     //
        {"VP", {{
                   {"", "vacuum pump, pumping station"}, //
               }}},                                      //
        {"VQ", {{
                   {"", "vacuum chamber for Quadrupole"}, //
               }}},                                       //
        {"VR", {{
                   {"", "pipe with or without pump flange"}, //
               }}},                                          //
        {"VS", {{
                   {"", "vacuum chamber with Special parts"}, //
               }}},                                           //
        {"VT", {{
                   {"", "vacuum T-piece or universal flange (for pumps)"}, //
               }}},                                                        //
        {"VU", {{
                   {"", "vacuum chamber for horizontal magnets/kicker"}, //
               }}},                                                      //
        {"VV", {{
                   {"", "vacuum valve"},               //
                   {"P", "pneumatic gate valve"},      //
                   {"R", "regulatable control valve"}, //
               }}},                                    //
        {"VW", {{
                   {"", "vacuum watchdog"}, //
               }}},                         //
        {"VX", {{
                   {"", "vacuum chamber for decapole corrector"}, //
               }}},                                               //
        {"VZ", {{
                   {"", "vacuum chamber (Z-plane)"}, //
               }}},                                  //
        {"YB", {{
                   {"", "cryo branch box"}, //
               }}},                         //
        {"YC", {{
                   {"", "cryo connection between sections"}, //
               }}},                                          //
        {"YD", {{
                   {"", "cryo distribution box"}, //
               }}},                               //
        {"YE", {{
                   {"", "cryo end box"}, //
               }}},                      //
        {"YE-x", {{
                     {"", "cryo upstream end box"}, //
                 }}},                               //
        {"YF", {{
                   {"", "cryo feed box"}, //
               }}},                       //
        {"YFK", {{
                    {"", "cryo feed box with correction element"}, //
                }}},                                               //
        {"YFM", {{
                    {"", "cryo feed box with magnet dipole"}, //
                }}},                                          //
        {"YFQ", {{
                    {"", "cryo feed box with quadrupole"}, //
                }}},                                       //
        {"YG", {{
                   {"", "cryo multi-purpose line"}, //
               }}},                                 //
        {"YJ", {{
                   {"", "cryo jumper connection"}, //
               }}},                                //
        {"YK", {{
                   {"", "cryo cold head"}, //
               }}},                        //
        {"YL", {{
                   {"", "cryo current lead box"}, //
               }}},                               //
        {"YM", {{
                   {"", "cryo module"}, //
               }}},                     //
        {"YME", {{
                    {"", "cryo module with dipole or quadrupole"}, //
                }}},                                               //
        {"YMK", {{
                    {"", "cryo module with correction element"}, //
                }}},                                             //
        {"YMM", {{
                    {"", "cryo module with magnet dipole"}, //
                }}},                                        //
        {"YMQ", {{
                    {"", "cryo module with quadrupole"}, //
                }}},                                     //
        {"YN", {{
                   {"", "cryo feed-in line"}, //
               }}},                           //
        {"YP", {{
                   {"", "cryo by-pass line"}, //
               }}},                           //
        {"YQ", {{
                   {"", "cryo quench detection"}, //
               }}},                               //
        {"YT", {{
                   {"", "cryo transfer beam line"}, //
               }}},                                 //
        {"YV", {{
                   {"", "cryo vacuum barrier"}, //
               }}},                             //
        {"YW", {{
                   {"", "cryo warm helium piping"}, //
               }}},                                 //
        {"YW5", {{
                    {"", "cryo warm gas supply"}, //
                }}},                              //
        {"YW6", {{
                    {"", "cryo warm gas return"}, //
                }}} //
    }};

    static constexpr std::array<std::pair<std::string_view, std::string_view>, 20> positionSpecifiers = {{
        {"0", "automatic dipole field suppression"},    //
        {"A", "outside"},                               //
        {"B", "bypass/shunt PSU"},                      //
        {"G", "pneumatic actuator"},                    //
        {"H", "horizontal"},                            //
        {"I", "inside"},                                //
        {"L", "left left (or long.)"},                  //
        {"M", "position controlled by magnetic field"}, //
        {"O", "above/top"},                             //
        {"P", "pneumatic actuator"},                    //
        {"R", "right"},                                 //
        {"S", "stepper motor"},                         //
        {"T", "test signal"},                           //
        {"U", "under/bottom"},                          //
        {"V", "HV generator"},                          //
        {"W", "water interlock"},                       //
        {"Z", "Z direction (beam direction) element"},  //
    }};

    // parse GSI/FAIR accelerator naming convention i.e. 'AABBDDSF9'
    // * 'AA' characters 0 & 1: accelerator/machine domain (e.g. GU->"UNILAC", GS->"SIS18", GE->"ESR")
    // * 'BB' characters 2 & 3: alphanumeric subsection within accelerator domain (N.B. machine specific, e.g. '01' -> first cell, 'K1' -> first segment, etc. )
    // * 'BB' characters 4 & 5: physical/technical device (e.g. MU->"horizontal bending magnet", DU->"beam loss monitor", etc.
    // * 'S'  character  6    : numeric|'_' as sequence indicator (if there are more than one of the same device in a section)
    // * 'F'  character  7    : device function (N.B. strictly speaking BB+F provides the exact device description)
    // * '0'  character  8    : optional positional function specifier (most do not have these, e.g. A->outer position, I->inner position, etc.)

    // There is a noteworthy exception for generic digitizers where the convention is 'AACDSSS'
    // N.B. 'AA' as above, followed by fixed 'CD' characters then three numeric digits counting the digitizer class instance

    if (deviceName.size() >= 4) {
        std::string_view locationPrefix = deviceName.substr(0, 2); // Extract the 'AA' part
        std::string_view section        = deviceName.substr(2, 2); // Extract the 'SS' part
        auto             locIt          = std::find_if(locations.begin(), locations.end(), [locationPrefix](const auto& loc) { return loc.first == locationPrefix; });

        if (locIt != locations.end()) {
            // Find the section name
            auto sectionIt = std::find_if(locIt->second.begin(), locIt->second.end(), [section](const auto& sec) { return sec.first == section; });

            if (sectionIt != locIt->second.end()) {
                info.location = locIt->second[0].second; // Get the main location name
                info.section  = sectionIt->second;       // Get the section name
            } else {
                info.location = locIt->second[0].second; // Fallback to just the main location name
                info.section  = section;                 // Use the raw section code if no match is found
            }
        } else {
            info.location = "Unknown"; // Fallback if no location match is found
            info.section  = section;   // Use the raw section code
        }
    }

    auto isSequenceDigit = [](int digit) {
        constexpr int kPlaceHolder = '_';
        return std::isdigit(digit) || digit == kPlaceHolder;
    };

    if (deviceName.size() >= 6) {
        std::string_view section       = deviceName.substr(2, 2); // Extract the 'SS' part
        std::string_view technicalName = deviceName.substr(4, 2); // extract 'AA'
        std::string_view sequence;                                // this will hold the sequence number
        std::string_view deviceFunction;                          // this will hold 'B' or 'BB'

        // check for the generic digitizer convention 'AACDSSS'
        if (section == "CD" && deviceName.size() >= 7) {
            sequence            = deviceName.substr(4, 3);
            auto techFuncIt     = std::ranges::find_if(functionSpecifiers, [section](const auto& funcSpec) { return funcSpec.first == section; });
            info.deviceFunction = techFuncIt != functionSpecifiers.end() ? techFuncIt->second.begin()->second : "gen. digitizer DAQ";
            info.sequence       = sequence;
            return info; // early return since this special case is handled
        }

        if (deviceName.size() >= 8 && isSequenceDigit(deviceName[6]) && isSequenceDigit(deviceName[7])) {
            sequence = deviceName.substr(6, 2); // two digit sequence 'yy'
        } else if (deviceName.size() >= 7 && isSequenceDigit(deviceName[6])) {
            sequence = deviceName.substr(6, 1); // single digit sequence 'y'
        }
        if (deviceName.size() >= 9) {
            deviceFunction = deviceName.substr(6UZ + sequence.size(), 3);
        } else if (deviceName.size() >= 8) {
            deviceFunction = deviceName.substr(6UZ + sequence.size(), 2);
        } else if (deviceName.size() >= 7) {
            deviceFunction = deviceName.substr(6UZ + sequence.size(), 1);
        }

        if (!sequence.empty()) {
            info.sequence = sequence;
        }

        // set the device function
        if (!deviceFunction.empty()) {
            info.deviceFunction = deviceFunction;
        }

        // extract and set the function based on functionSpecifiers map
        if (auto techFuncIt = std::ranges::find_if(functionSpecifiers, [technicalName](const auto& funcSpec) { return funcSpec.first == technicalName; }); techFuncIt != functionSpecifiers.end()) {
            // try to find 'B' or 'BB' in the function specifiers for 'AA'
            if (!deviceFunction.empty()) {
                if (auto funcIt = std::ranges::find_if(techFuncIt->second, [deviceFunction](const auto& func) { return func.first == deviceFunction; }); funcIt != techFuncIt->second.end()) {
                    info.deviceFunction = funcIt->second; // Found 'B' or 'BB' in the specifiers for 'AA'
                } else {
                    info.deviceFunction = techFuncIt->second.begin()->second; // Default to the first entry for 'AA'
                }
            } else {
                info.deviceFunction = techFuncIt->second.begin()->second; // Only 'AA' was found, use its description
            }
        } else {
            // if 'AA' not found, default to 'AA' or 'B'/'BB' as description
            info.deviceFunction = technicalName;
            if (!deviceFunction.empty()) {
                // info.function += deviceFunction;
            }
        }

        // append any remaining part of the device name to the function description
        if (deviceName.size() > 9 + sequence.size()) {
            [[maybe_unused]] std::string_view remainingFunction = deviceName.substr(9 + sequence.size());
            // info.function += remainingFunction;
        }
    }

    // extract and set the position based on positionSpecifiers map
    if (deviceName.size() >= 9) {
        std::string_view positionSpecifier = deviceName.substr(8, 1);
        auto             posIt             = std::find_if(positionSpecifiers.begin(), positionSpecifiers.end(), [positionSpecifier](const auto& posSpec) { return posSpec.first == positionSpecifier; });

        if (posIt != positionSpecifiers.end()) {
            info.devicePosition = posIt->second; // Found the position specifier
        }
    }

    return info;
}

} // namespace fair

#endif // OPENDIGITIZER_FAIR_DEVICE_NAME_HELPER_HPP
