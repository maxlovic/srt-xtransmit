#include <atomic>
#include <chrono>
#include <ctime>
#include <future>
#include <iomanip>
#include <memory>
#include <thread>
#include <vector>

#include "srt_socket.hpp"
#include "udp_socket.hpp"
#include "receive.hpp"

// OpenSRT
#include "apputil.hpp"
#include "uriparser.hpp"

using namespace std;
using namespace xtransmit;
using namespace xtransmit::receive;
using namespace std::chrono;

using shared_srt  = std::shared_ptr<socket::srt>;
using shared_sock = std::shared_ptr<socket::isocket>;


void read_timestamp(const vector<char>& buffer)
{
	// Note: std::put_time is supported only in GCC 5 and higher
#if !defined(__GNUC__) || (__GNUC__ >= 5)
	const time_t    send_time    = *(reinterpret_cast<const time_t*>   (buffer.data()));
	const long long send_time_us = *(reinterpret_cast<const long long*>(buffer.data() + 8));

	const auto   systime_now = system_clock::now();
	const time_t read_time   = system_clock::to_time_t(systime_now);

	std::tm tm_send = *std::localtime(&send_time);
	::cout << " snd_time " << std::put_time(&tm_send, "%T.") << std::setfill('0') << std::setw(6) << send_time_us;
	std::tm                tm_read = *std::localtime(&read_time);
	system_clock::duration read_time_frac =
		systime_now.time_since_epoch() - duration_cast<seconds>(systime_now.time_since_epoch());
	::cout << " read_time " << std::put_time(&tm_read, "%T.") << duration_cast<microseconds>(read_time_frac).count();

	const auto delay = systime_now - (system_clock::from_time_t(send_time) + microseconds(send_time_us));
	::cout << " delta " << duration_cast<milliseconds>(delay).count() << " ms";
	::cout << endl;
#endif
}


void trace_message(const size_t bytes, const vector<char> &buffer, int conn_id)
{
	::cout << "RECEIVED MESSAGE length " << bytes << " on conn ID " << conn_id;

#if 0
	if (bytes < 50)
	{
		::cout << ":\n";
		::cout << string(buffer.data(), bytes).c_str();
	}
	else if (buffer[0] >= '0' && buffer[0] <= 'z')
	{
		::cout << " (first character):";
		::cout << buffer[0];
	}
#endif
	::cout << endl;
}

void run_pipe(shared_sock src, const config &cfg, const atomic_bool &force_break)
{
	atomic_bool local_break(false);

	auto stats_func = [&cfg, &force_break, &local_break](shared_sock sock) {
		if (cfg.stats_freq_ms == 0)
			return;
		if (cfg.stats_file.empty())
			return;

		ofstream logfile_stats(cfg.stats_file.c_str());
		if (!logfile_stats)
		{
			cerr << "ERROR: Can't open '" << cfg.stats_file << "' for writing stats. No output.\n";
			return;
		}

		bool print_header = true;

		const milliseconds interval(cfg.stats_freq_ms);
		while (!force_break && !local_break)
		{
			this_thread::sleep_for(interval);

			logfile_stats << sock->statistics_csv(print_header) << flush;
			print_header = false;
		}
	};

	auto stats_logger = async(launch::async, stats_func, src);

	socket::isocket &sock = *src.get();

	vector<char> buffer(cfg.message_size);
	try
	{
		while (!force_break)
		{
			const size_t bytes = sock.read(mutable_buffer(buffer.data(), buffer.size()), -1);

			if (bytes == 0)
			{
				::cerr << "src->read returned 0\n";
				continue;
			}

			if (cfg.print_notifications)
				trace_message(bytes, buffer, sock.id());
			if (cfg.check_timestamp)
				read_timestamp(buffer);

			if (cfg.send_reply)
			{
				const string out_message("Message received");
				sock.write(const_buffer(out_message.data(), out_message.size()));

				if (cfg.print_notifications)
					::cerr << "Reply sent on conn ID " << sock.id() << "\n";
			}
		}
	}
	catch (const socket::exception &)
	{
		local_break = true;
	}

	if (force_break)
	{
		cerr << "\n (interrupted on request)\n";
	}

	local_break = true;
	stats_logger.wait();
}

void start_receiver(future<shared_srt> &&connection, const config &cfg, const atomic_bool &force_break)
{
	try
	{
		const shared_srt sock = connection.get();
		if (!sock)
			return;

		run_pipe(sock, cfg, force_break);
	}
	catch (const socket::exception &e)
	{
		::cerr << e.what();
		return;
	}
}

void xtransmit::receive::receive_main(const string &url, const config &cfg, const atomic_bool &force_break)
{
	shared_srt socket = make_shared<socket::srt>(UriParser(url));
	const bool        accept = socket->mode() == socket::srt::LISTENER;
	start_receiver(accept ? socket->async_accept() : socket->async_connect(), cfg, force_break);
}
