#pragma once

#include <string>

#include "datum/natural.hpp"
#include "datum/bytes.hpp"
#include "datum/enum.hpp"

namespace WarGrey::DTPM {
    template<typename I, int S>
    private struct ScaledInteger {
    public:
        ScaledInteger(I integer) { this->box(integer); }

    public:
        void box(I integer) { this->scalar = integer; }
        double unbox() { return double(this->scalar) * WarGrey::SCADA::flexpt(0.1, double(S)); }
        
    public:
        I value() { return this->scalar; }
        int precision() { return S; }

    private:
        I scalar;
    };

    template<int S>
    private struct U : public WarGrey::DTPM::ScaledInteger<unsigned int, S> {
    public:
        U() : U(0U) {}
        U(unsigned int uint) : ScaledInteger(uint) {}
    };

    template<int S>
    private struct I : public WarGrey::DTPM::ScaledInteger<signed int, S> {
    public:
        I() : I(0) {}
        I(signed int sint) : ScaledInteger(sint) {}
    };

    /*********************************************************************************************/
    typedef unsigned int           u;
    typedef signed int             i;
    typedef bool                   b;
    typedef WarGrey::SCADA::bytes  x;
    typedef std::string            t;
    typedef WarGrey::SCADA::bytes  d;

    // 0-origin indice
    unsigned long long ais_bit_field(WarGrey::GYDM::Natural& payload, size_t idx, size_t length);

    WarGrey::DTPM::b ais_b_ref(WarGrey::GYDM::Natural& payload, size_t idx);
    WarGrey::DTPM::u ais_u_ref(WarGrey::GYDM::Natural& payload, size_t idx, size_t length);
    WarGrey::DTPM::i ais_i_ref(WarGrey::GYDM::Natural& payload, size_t idx, size_t length);
    WarGrey::DTPM::t ais_t_ref(WarGrey::GYDM::Natural& payload, size_t idx, size_t length);

    template<typename E>
    E ais_e_ref(WarGrey::GYDM::Natural& payload, size_t idx, size_t length, E fallback, WarGrey::DTPM::i offset = 0) {
        WarGrey::DTPM::i e = ais_u_ref(payload, idx, length) + offset;

        return ((e < _N(E)) ? _E(E, e) : fallback);
    }

    /*********************************************************************************************/
    WarGrey::GYDM::Natural ais_unarmor(std::string& payload, int pad_bits);
}
