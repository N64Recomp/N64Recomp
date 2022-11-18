#include "../portultra/multilibultra.hpp"
#include "recomp.h"

extern "C" void osContInit_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void osContStartReadData_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    Multilibultra::send_si_message();
}

extern "C" void osContGetReadData_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void osMotorInit_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void osMotorStart_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}

extern "C" void osMotorStop_recomp(uint8_t* restrict rdram, recomp_context* restrict ctx) {
    ;
}
