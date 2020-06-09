#include "socket_stats.hpp"

// submodules
#include "spdlog/spdlog.h"


using namespace std;
using namespace xtransmit;
using namespace std::chrono;


xtransmit::socket::stats_writer::stats_writer(const std::string& filename, const std::chrono::milliseconds& interval)
	: m_logfile(filename.c_str())
	, m_interval(interval)
{
	if (!m_logfile)
	{
		spdlog::critical("Failed to open file for stats output. Path: {0}", filename);
		throw socket::exception("Failed to open file for stats output. Path " + filename);
	}
}

xtransmit::socket::stats_writer::~stats_writer()
{
	stop();
}

void xtransmit::socket::stats_writer::add_socket(shared_sock sock)
{
	if (!sock || !sock->supports_statistics())
		return;

	m_lock.lock();
	m_sock.push_back(sock);
	m_lock.unlock();

	if (m_stat_future.valid())
		return;

	m_stop = false;
	m_stat_future = launch();
}

void xtransmit::socket::stats_writer::clear()
{
	m_lock.lock();
	m_sock.clear();
	m_lock.unlock();
}

void xtransmit::socket::stats_writer::stop()
{
	m_stop = true;
	if (m_stat_future.valid())
		m_stat_future.wait();
}


future<void> xtransmit::socket::stats_writer::launch()
{
	auto stats_func = [](vector<shared_sock>& sock, ofstream& out, const milliseconds interval,
		mutex& stats_lock, const atomic_bool& stop_stats)
	{
		bool print_header = true;

		while (!stop_stats)
		{
#ifdef ENABLE_CXX17
			scoped_lock<mutex> lock(stats_lock);
#else
			lock_guard<mutex> lock(stats_lock);
#endif
			for_each(sock.begin(), sock.end(), [&out, &print_header](shared_sock& s) {
				if (print_header)
					out << s->statistics_csv(true);	
				out << s->statistics_csv(false) << flush;
				print_header = false;
				});

			this_thread::sleep_for(interval);
		}
	};

	return async(::launch::async, stats_func, ref(m_sock), ref(m_logfile), m_interval, ref(m_lock), ref(m_stop));
}
