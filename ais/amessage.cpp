#include "ais/amessage.hpp"
#include "gps/gparser.hpp"

using namespace WarGrey::DTPM;

void WarGrey::DTPM::scan_nmea(AINMEA* nmea, const unsigned char* pool, size_t* cursor, size_t endp1) {
	nmea->s_size = int(scan_natural(pool, cursor, endp1));
	nmea->s_idx = int(scan_natural(pool, cursor, endp1));
	nmea->msg_id = int(scan_natural(pool, cursor, endp1));
	nmea->channel = scan_char(pool, cursor, endp1);
	nmea->fragment = scan_text(pool, cursor, endp1);
	nmea->fill_bits_number = int(scan_natural(pool, cursor, endp1));
}
