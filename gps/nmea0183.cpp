#include <ppltasks.h>

#include "datum/bytes.hpp"
#include "datum/time.hpp"
#include "datum/string.hpp"

#include "gps/nmea0183.hpp"
#include "gps/gparser.hpp"

#include "network/netexn.hpp"
#include "network/socket.hpp"

#include "syslog.hpp"
#include "taskexn.hpp"

using namespace WarGrey::SCADA;
using namespace WarGrey::DTPM;
using namespace WarGrey::GYDM;

using namespace Concurrency;

using namespace Windows::Foundation;
using namespace Windows::Networking;
using namespace Windows::Networking::Sockets;

/*************************************************************************************************/
INMEA0183::INMEA0183(TCPType type, Syslog* sl, Platform::String^ h, uint16 p, INMEA0183Receiver* r, int id) : ITCPFeedBackConnection(type), id(id) {
	this->logger = ((sl == nullptr) ? make_silent_logger("Silent GPS Receiver") : sl);
	this->logger->reference();

	this->push_receiver(r);
	this->service = p.ToString();
	this->device = ref new HostName(h);

	if (this->id == 0) {
		this->id = p;
	}

    this->shake_hands();
};

INMEA0183::~INMEA0183() {
	if (this->socket != nullptr) {
		delete this->socket; // stop the confirmation loop before release transactions.
	}

	this->logger->destroy();
}

Platform::String^ INMEA0183::device_hostname() {
	return this->device->RawName;
}

Platform::String^ INMEA0183::device_description() {
	return this->device->RawName + ":" + this->service;
}

int INMEA0183::device_identity() {
	return this->id;
}

Syslog* INMEA0183::get_logger() {
	return this->logger;
}

void INMEA0183::push_receiver(INMEA0183Receiver* receiver) {
	if (receiver != nullptr) {
		this->receivers.push_back(receiver);
	}
}

void INMEA0183::tolerate_bad_checksum(bool yes_no) {
	this->tolerate_checksum = yes_no;
}

void INMEA0183::shake_hands() {
	this->clear();
	this->socket = make_stream_socket();

	this->logger->log_message(Log::Debug, L">> connecting to device[%s]", this->device_description()->Data());

	create_task(this->socket->ConnectAsync(this->device, this->service)).then([this](task<void> handshaking) {
		try {
			handshaking.get();

			this->nmeain = make_socket_available_reader(this->socket);
			this->refresh_data_size = 0;

			this->logger->log_message(Log::Debug, L">> connected to %s[%s]",
				this->get_type().ToString()->Data(), this->device_description()->Data());

			this->notify_connectivity_changed();
			this->wait_process_confirm_loop();
		} catch (Platform::Exception^ e) {
			this->logger->log_message(Log::Warning, socket_strerror(e));
			this->shake_hands();
		}
	});
}

void INMEA0183::wait_process_confirm_loop() {
	size_t pool_capacity = sizeof(this->message_pool) / sizeof(uint8);
	unsigned int request_max_size = (unsigned int)(pool_capacity - this->refresh_data_size);
	double receiving_ts = current_inexact_milliseconds();
	
	create_task(this->nmeain->LoadAsync(request_max_size)).then([=](unsigned int size) {
		if (size == 0) {
			task_fatal(this->logger, L"%s[%s] has lost", this->get_type().ToString()->Data(), this->device_description()->Data());
		}

		READ_BYTES(this->nmeain, this->message_pool + this->refresh_data_size, size);
		this->data_size = this->refresh_data_size + size;
	}).then([=](task<void> verify) {
		verify.get();

		this->notify_data_received(this->refresh_data_size, current_inexact_milliseconds() - receiving_ts);
		this->refresh_data_size = this->data_size;
		this->message_start = 0;
		
		do {
			size_t message_size = this->check_message();

			if (this->CR_LF_idx > 0) {
				double confirming_ms = current_inexact_milliseconds();
				size_t endp1 = ((this->checksum_idx > 0) ? this->checksum_idx : this->CR_LF_idx);

				this->message_pool[this->field0_idx] = '\0';

				if (this->refresh_data_size <= 0) {
					this->logger->log_message(Log::Debug,
						L"<received %d-byte-size %S message coming from %s[%s]>",
						message_size, this->message_pool + this->message_start,
						this->get_type().ToString()->Data(), this->device_description()->Data());
				} else {
					this->logger->log_message(Log::Debug,
						L"<received %d-byte-size %S message coming from %s[%s] along with extra %d bytes>",
						message_size, this->message_pool + (this->message_start - message_size),
						this->get_type().ToString()->Data(), this->device_description()->Data(),
						this->refresh_data_size);
				}

				this->apply_confirmation(this->message_pool, this->field0_idx - 5, this->field0_idx + 1, endp1);
				this->notify_data_confirmed(message_size, current_inexact_milliseconds() - confirming_ms);
			} else if (this->message_start == 0) {
				task_fatal(this->logger,
					L"message coming from %s[%s] is overlength",
					this->get_type().ToString()->Data(), this->device_description()->Data());
			}
		} while ((this->CR_LF_idx > 0) && (this->refresh_data_size > 0));
	}).then([=](task<void> confirm) {
		try {
			confirm.get();

			this->realign_message();
			this->wait_process_confirm_loop();
		} catch (task_discarded&) {
			this->realign_message();
			this->wait_process_confirm_loop();
		} catch (task_terminated&) {
			this->shake_hands();
		} catch (task_canceled&) {
			this->shake_hands();
		} catch (Platform::Exception^ e) {
			this->logger->log_message(Log::Warning, e->Message);
			this->shake_hands();
		}
	});
}

size_t INMEA0183::check_message() {
	unsigned short checksum = 0;
	size_t message_size = 0;
	size_t start_idx = this->message_start + 1;
	
	this->field0_idx = 0;
	this->checksum_idx = 0;
	this->CR_LF_idx = 0;
	
	for (size_t i = this->message_start; (i < this->data_size) && (this->CR_LF_idx == 0); i++) {
		unsigned char ch = this->message_pool[i];

		switch (ch) {
		case '$': case '!': start_idx = i; break;
		case ',': if (this->field0_idx == 0) this->field0_idx = i; break;
		case '*': this->checksum_idx = i; break;
		case 0x0D: this->CR_LF_idx = i; break;
		default: {
			if ((ch <= ' ') || (ch > '~')) {
				this->get_logger()->log_message(Log::Warning,
					L"message@%d coming from %s[%s] constains non-printable ASCII character: 0x%02x@%d",
					this->message_start, this->get_type().ToString()->Data(), this->device_description()->Data(), ch, i);
			}
		}
		}

		if ((i > this->message_start) && (this->checksum_idx == 0)) {
			checksum ^= ch;
		}
	}

	if (this->CR_LF_idx > 0) {
		size_t self_start = this->message_start;

		message_size = this->CR_LF_idx + 2 - self_start;

		if (this->refresh_data_size > message_size) {
			this->refresh_data_size -= message_size;
		} else {
			this->refresh_data_size = 0U;
		}

		if (this->refresh_data_size > 0U) {
			this->message_start += message_size;
		}

		if ((start_idx != self_start) || (this->message_pool[this->CR_LF_idx + 1] != 0x0A)
			|| ((this->checksum_idx > 0) && (this->checksum_idx != CR_LF_idx - 3))) {
			this->message_pool[this->CR_LF_idx] = '\0';
			task_discard(this->logger, L"message@%d coming from %s[%s] is malformed: %S",
				self_start, this->get_type().ToString()->Data(), this->device_description()->Data(),
				this->message_pool + self_start);
		}

		if (this->checksum_idx > 0) {
			unsigned short signature
				= (hexadecimal_ref(this->message_pool, this->checksum_idx + 1, 0U) << 4)
				+ hexadecimal_ref(this->message_pool, this->checksum_idx + 2, 0U);

			if (checksum != signature) {
				Platform::String^ message = make_wstring(
					L"message@%d coming from %s[%s] has been corrupted(signature: 0X%02X; checksum: 0X%02X)",
					self_start, this->get_type().ToString()->Data(), this->device_description()->Data(), signature, checksum);

				if (this->tolerate_checksum) {
					this->logger->log_message(Log::Warning, message);
				} else {
					task_discard(this->logger, message);
				}
			}
		}
	}

	return message_size;
}

void INMEA0183::realign_message() {
	if (this->refresh_data_size > 0) {
		memmove(this->message_pool, this->message_pool + this->message_start, this->refresh_data_size);

		this->logger->log_message(Log::Debug,
			L"<realigned %d-byte-size partial message coming from %s[%s]>",
			this->refresh_data_size, this->get_type().ToString()->Data(), this->device_description()->Data());
	}
}

void INMEA0183::apply_confirmation(const unsigned char* pool, size_t head_start, size_t body_start, size_t endp1) {
	long long timepoint = current_milliseconds();
	
	for (auto r : this->receivers) {
		if (r->available(this->id)) {
			r->on_message(this->id, timepoint, pool, head_start, body_start, endp1, this->logger);
		}
	}
}

bool INMEA0183::connected() {
	return (this->nmeain != nullptr);
}

void INMEA0183::suicide() {
	if (this->nmeain != nullptr) {
		delete this->nmeain;
		this->nmeain = nullptr;
	}

	if (this->socket != nullptr) {
		delete this->socket;
		this->socket = nullptr;

		this->notify_connectivity_changed();
	}
}

void INMEA0183::clear() {
	if (this->nmeain != nullptr) {
		this->suicide();
	}
}

/*************************************************************************************************/
void GPSReceiver::on_message(int id, long long timepoint, const unsigned char* pool, size_t head_start, size_t body_start, size_t endp1, Syslog* logger) {
	unsigned int type = message_type(pool, head_start + 2);
	size_t cursor = body_start;

#define ON_MESSAGE(MSG, scan, pool, cursor, endp1, id, logger, timepoint) \
{ \
	MSG msg; \
	scan(&msg, pool, &cursor, endp1); \
	if (cursor >= endp1) { \
		this->pre_scan_data(id, logger); \
        this->on_##MSG(id, timepoint, &msg, logger); \
        this->post_scan_data(id, logger); \
	} \
}

	switch (type) {
		// from GPS
	case MESSAGE_TYPE('G', 'G', 'A'): ON_MESSAGE(GGA, scan_gga, pool, cursor, endp1, id, logger, timepoint); break;
	case MESSAGE_TYPE('V', 'T', 'G'): ON_MESSAGE(VTG, scan_vtg, pool, cursor, endp1, id, logger, timepoint); break;
	case MESSAGE_TYPE('G', 'L', 'L'): ON_MESSAGE(GLL, scan_gll, pool, cursor, endp1, id, logger, timepoint); break;
	case MESSAGE_TYPE('G', 'S', 'A'): ON_MESSAGE(GSA, scan_gsa, pool, cursor, endp1, id, logger, timepoint); break;
	case MESSAGE_TYPE('G', 'S', 'V'): ON_MESSAGE(GSV, scan_gsv, pool, cursor, endp1, id, logger, timepoint); break;
	case MESSAGE_TYPE('Z', 'D', 'A'): ON_MESSAGE(ZDA, scan_zda, pool, cursor, endp1, id, logger, timepoint); break;

		// from compass
	case MESSAGE_TYPE('H', 'D', 'T'): ON_MESSAGE(HDT, scan_hdt, pool, cursor, endp1, id, logger, timepoint); break;
	case MESSAGE_TYPE('R', 'O', 'T'): ON_MESSAGE(ROT, scan_rot, pool, cursor, endp1, id, logger, timepoint); break;

	default: {
		logger->log_message(Log::Debug, L"unrecognized message[%S:%S], ignored", pool + head_start, pool + body_start);

		cursor = endp1;
	}
	}

	if (cursor < endp1) {
		logger->log_message(Log::Error, L"invalid %S message: %S, ignored", pool + head_start, pool + body_start);
	}
}
