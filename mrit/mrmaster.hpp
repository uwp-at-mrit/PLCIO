#pragma once

#include <list>

#include "mrit/mrmessage.hpp"

#include "network/tcp.hpp"
#include "network/stream.hpp"

#include "syslog.hpp"

namespace WarGrey::SCADA {
	private class IMRConfirmation abstract {
	public:
		virtual bool available() { return true; }

	public:
		virtual void pre_read_data(WarGrey::GYDM::Syslog* logger) = 0;
		virtual void on_all_signals(long long timepoint_ms, size_t addr0, size_t addrn, uint8* data, size_t size, WarGrey::GYDM::Syslog* logger) = 0;
		virtual void post_read_data(WarGrey::GYDM::Syslog* logger) = 0;
	};

	private class IMRMaster abstract : public WarGrey::SCADA::ITCPStatedConnection, public WarGrey::SCADA::IStreamAcceptPort {
    public:
        virtual ~IMRMaster() noexcept;

		IMRMaster(WarGrey::GYDM::Syslog* logger, Platform::String^ server, uint16 service, WarGrey::SCADA::IMRConfirmation* confirmation);

	public:
		Platform::String^ device_hostname() override;
		Platform::String^ device_description() override;

	public:
		WarGrey::GYDM::Syslog* get_logger() override;
		void shake_hands() override;
		bool connected() override;
		void suicide() override;

	public:
		void push_confirmation_receiver(WarGrey::SCADA::IMRConfirmation* confirmation);

    public:
		virtual void read_all_signal(uint16 data_block, uint16 addr0, uint16 addrn, float tidemark = 0.0F) = 0;
		virtual void write_analog_quantity(uint16 data_block, uint16 address, float datum) = 0;
		virtual void write_digital_quantity(uint16 data_block, uint8 index, uint8 bit_index, bool value = true) = 0;

	public:
		void on_socket(Windows::Networking::Sockets::StreamSocket^ plc) override;
		
	protected:
		void request(size_t fcode, size_t data_block, size_t addr0, size_t addrn, uint8* data, size_t size);
		void set_message_preference(WarGrey::SCADA::MrMessagePreference& config);

	private:
		void connect();
		void listen();
		void clear();
		void clear_if_confirmation_broken();
		void wait_process_confirm_loop();
		void apply_confirmation(size_t code, size_t db, size_t addr0, size_t addrn, uint8* data, size_t size);

	protected:
		std::list<WarGrey::SCADA::IMRConfirmation*> confirmations;
		WarGrey::SCADA::IMRConfirmation* current_confirmation;
		WarGrey::SCADA::MrMessagePreference preference;
		WarGrey::GYDM::Syslog* logger;

    private:
		// NOTE: Either `listener` or `socket` will work depends on the `device`.
		WarGrey::SCADA::StreamListener* listener;
        Windows::Networking::Sockets::StreamSocket^ socket;
        Windows::Networking::HostName^ device;
        Platform::String^ service;

	private:
		Windows::Storage::Streams::DataReader^ mrin;
		Windows::Storage::Streams::DataWriter^ mrout;

	private:
		uint8* data_pool;
		unsigned int pool_size;
    };

    private class MRMaster : public WarGrey::SCADA::IMRMaster {
    public:
        MRMaster(WarGrey::GYDM::Syslog* logger, Platform::String^ server, uint16 port, IMRConfirmation* confirmation = nullptr)
			: IMRMaster(logger, server, port, confirmation) {}

		MRMaster(WarGrey::GYDM::Syslog* logger, uint16 port, IMRConfirmation* confirmation = nullptr)
			: MRMaster(logger, nullptr, port, confirmation) {}

	public:
		void send_scheduled_request(long long count, long long interval, long long uptime) override {}

	public:
		void read_all_signal(uint16 data_block, uint16 addr0, uint16 addrn, float tidemark = 0.0F) override;

	public:
		void write_analog_quantity(uint16 data_block, uint16 address, float datum) override;
		void write_digital_quantity(uint16 data_block, uint8 index, uint8 bit_index, bool value = true) override;
	};

	private class MRConfirmation : public WarGrey::SCADA::IMRConfirmation {
	public:
		void pre_read_data(WarGrey::GYDM::Syslog* logger) override {}
		void on_all_signals(long long timepoint_ms, size_t addr0, size_t addrn, uint8* data, size_t size, WarGrey::GYDM::Syslog* logger) override {}
		void post_read_data(WarGrey::GYDM::Syslog* logger) override {}
	};
}
