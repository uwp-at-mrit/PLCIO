#include "datum/bytes.hpp"
#include "datum/string.hpp"
#include "datum/time.hpp"

#include "gps/gparser.hpp"

using namespace WarGrey::SCADA;
using namespace WarGrey::DTPM;

/*************************************************************************************************/
static inline void move_index(const unsigned char* pool, size_t* idx, size_t endp1, size_t endoff) {
	size_t end_idx = (*idx) + endoff;

	if (pool[end_idx] == ',') {
		(*idx) += endoff + 1;
	} else if (end_idx == endp1) {
		(*idx) += endoff;
	}
}

/*************************************************************************************************/
unsigned int WarGrey::DTPM::message_type(const unsigned char* pool, size_t index) {
	return MESSAGE_TYPE(pool[index + 0], pool[index + 1], pool[index + 2]);
}

bool WarGrey::DTPM::scan_boolean(const unsigned char* pool, size_t* idx, size_t endp1, unsigned char T, unsigned char F) {
	unsigned char b = pool[(*idx)];
	
	if ((b == T) || (b == F)) {
		move_index(pool, idx, endp1, 1);
	}

	return (b == T);
}

unsigned char WarGrey::DTPM::scan_char(const unsigned char* pool, size_t* idx, size_t endp1, unsigned char NIL) {
	unsigned char c = pool[(*idx)];

	if ((c == ',') || (c == '*')) {
		c = NIL;
		move_index(pool, idx, endp1, 0);
	} else {
		move_index(pool, idx, endp1, 1);
	}

	return c;
}

std::string WarGrey::DTPM::scan_text(const unsigned char* pool, size_t* idx, size_t endp1) {
	unsigned char c = pool[(*idx)];
	std::string s;

	while ((c != ',') && (c != '*')) {
		s.push_back(c);
		(*idx)++;
		c = pool[(*idx)];
	}

	move_index(pool, idx, endp1, 0);

	return s;
}

unsigned long long WarGrey::DTPM::scan_natural(const unsigned char* pool, size_t* idx, size_t endp1) {
	unsigned long long fxnum = WarGrey::SCADA::scan_natural(pool, idx, endp1, false);

	move_index(pool, idx, endp1, 0);

	return fxnum;
}

double WarGrey::DTPM::scan_scalar(const unsigned char* pool, size_t* idx, size_t endp1) {
	double flnum = scan_flonum(pool, idx, endp1, false);

	move_index(pool, idx, endp1, 0);

	return flnum;
}

double WarGrey::DTPM::scan_vector(const unsigned char* pool, size_t* idx, size_t endp1, unsigned char unit) {
	double flnum = scan_flonum(pool, idx, endp1, false);
	
	if ((pool[(*idx)] == ',') && (pool[(*idx) + 1] == unit)) {
		move_index(pool, idx, endp1, 2);
	}

	return flnum;
}

double WarGrey::DTPM::scan_vector(const unsigned char* pool, size_t* idx, size_t endp1, unsigned char pdir, unsigned char ndir) {
	double flnum = scan_flonum(pool, idx, endp1, false);

	if (pool[(*idx)] == ',') {
		unsigned char dir = pool[(*idx) + 1];

		if (dir == pdir) {
			move_index(pool, idx, endp1, 2);
		} else if (dir == ndir) {
			move_index(pool, idx, endp1, 2);
			flnum *= -1.0;
		}
	}

	return flnum;
}

NMEA_PSMI WarGrey::DTPM::scan_positioning_system_mode_indicator(const unsigned char* pool, size_t* idx, size_t endp1) {
	NMEA_PSMI indicator = NMEA_PSMI::_;

	switch (pool[(*idx)]) {
	case 'A': indicator = NMEA_PSMI::Autonomouse; break;
	case 'D': indicator = NMEA_PSMI::Differential; break;
	case 'E': indicator = NMEA_PSMI::Estimated; break;
	case 'M': indicator = NMEA_PSMI::Manual; break;
	case 'N': indicator = NMEA_PSMI::NotValid; break;
	}

	if (indicator != NMEA_PSMI::_) {
		move_index(pool, idx, endp1, 1);
	}

	return indicator;
}

NMEA_GQI WarGrey::DTPM::scan_gps_quality_indicator(const unsigned char* pool, size_t* idx, size_t endp1) {
	NMEA_GQI indicator = NMEA_GQI::_;

	switch (pool[(*idx)]) {
	case '1': indicator = NMEA_GQI::Invalid; break;
	case '2': indicator = NMEA_GQI::SinglePoint; break;
	case '3': indicator = NMEA_GQI::PseudorangeDifferential; break;
	case '4': indicator = NMEA_GQI::FixedRTK; break;
	case '5': indicator = NMEA_GQI::FloatRTK; break;
	case '6': indicator = NMEA_GQI::DeadReckoningMode; break;
	case '7': indicator = NMEA_GQI::ManualInputMode; break;
	case '8': indicator = NMEA_GQI::SimulatorMode; break;
	case '9': indicator = NMEA_GQI::WAAS; break;
	}

	if (indicator != NMEA_GQI::_) {
		move_index(pool, idx, endp1, 1);
	}

	return indicator;
}

NMEA_FIX_TYPE WarGrey::DTPM::scan_gps_fix_type(const unsigned char* pool, size_t* idx, size_t endp1) {
	NMEA_FIX_TYPE type = NMEA_FIX_TYPE::_;

	switch (pool[(*idx)]) {
	case '1': type = NMEA_FIX_TYPE::None; break;
	case '2': type = NMEA_FIX_TYPE::TWO_Dimension; break;
	case '3': type = NMEA_FIX_TYPE::THREE_DIMENSION; break;
	}

	if (type != NMEA_FIX_TYPE::_) {
		move_index(pool, idx, endp1, 1);
	}

	return type;
}
