// SPDX-License-Identifier: MIT
//
// ui.hh: a client of ui(4) in C++ (APE, g++ of 9front-gcc), as ui(2) is in
// C: ui in a new window of wsys or rio (window(1)), its files mounted;
// transactions to ctl, events from event, its other files read and written.
// Header only: #include <ui.hh>, link -l9 (APE's mount).
#ifndef UI_HH
#define UI_HH

// C++ even in a system directory (9front's gcc takes their headers as C)
extern "C++" {

#include <string>
#include <stdexcept>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

extern "C" {
int mount(int, int, char*, int, char*);	// APE's lib9
int unmount(char*, char*);
}

namespace ui {

// rc's quotes where a value needs them (blanks at its ends, a quote at its
// start, a newline); in them \n is a new line, \\ a backslash
inline std::string quote(const std::string &v)
{
	if(!v.empty() && v[0] != ' ' && v[0] != '\t' && v.back() != ' ' && v.back() != '\t'
	&& v[0] != '\'' && v.find('\n') == std::string::npos)
		return v;
	std::string q = "'";
	for(char c : v){
		if(c == '\'')
			q += "''";
		else if(c == '\\')
			q += "\\\\";
		else if(c == '\n')
			q += "\\n";
		else
			q += c;
	}
	return q + "'";
}

// a line of event: kind id arg (close alone: ui's window closed)
struct Event {
	std::string kind, id, arg;
};

class Ui {
public:
	std::string srv;	// its /srv entry's name
	std::string mtpt;	// where its files are

	// ui in a new window (window(1)), its files mounted on /mnt/srv
	explicit Ui(const std::string &title)
	{
		const char *u = getenv("user");
		srv = "ui." + std::string(u ? u : "none") + "." + std::to_string(getpid()) + ".0";
		std::string cmd = "/bin/ui -s " + srv;
		pid_t pid = fork();
		if(pid < 0)
			throw std::runtime_error("fork: " + std::string(strerror(errno)));
		if(pid == 0){
			execl("/bin/window", "window", cmd.c_str(), (char*)nullptr);
			_exit(1);
		}
		// window returns once the window is there; ui posts its files soon after
		waitpid(pid, nullptr, 0);
		std::string s = "/srv/" + srv;
		for(int i = 0; i < 100 && access(s.c_str(), F_OK) < 0; i++)
			usleep(100000);
		int fd = open(s.c_str(), O_RDWR);
		if(fd < 0)
			throw std::runtime_error(s + ": " + strerror(errno));
		mtpt = "/mnt/" + srv;
		// a mount that works closes fd (bind(2))
		if(mount(fd, -1, (char*)mtpt.c_str(), 0, (char*)"") < 0){
			close(fd);
			throw std::runtime_error("mount " + mtpt + ": " + strerror(errno));
		}
		ctlfd = open((mtpt + "/ctl").c_str(), O_WRONLY);
		evfd = open((mtpt + "/event").c_str(), O_RDONLY);
		if(ctlfd < 0 || evfd < 0)
			throw std::runtime_error(mtpt + ": no ctl or event");
		ctl("title " + quote(title) + "\n");
	}

	~Ui()
	{
		close(ctlfd);
		close(evfd);
		unmount(nullptr, (char*)mtpt.c_str());
	}

	Ui(const Ui&) = delete;
	Ui &operator=(const Ui&) = delete;

	// one transaction: its lines all or none (an error names the line);
	// framed by the lines begin and end (ui(4)), so that it stays one when
	// the mount cuts the write into several messages
	void ctl(const std::string &s)
	{
		if(s.empty())
			return;
		std::string f = "begin\n" + s + (s.back() == '\n' ? "" : "\n") + "end\n";
		if(write(ctlfd, f.data(), f.size()) != (ssize_t)f.size())
			throw std::runtime_error("ctl: " + std::string(strerror(errno)));
	}

	void set(const std::string &id, const std::string &key, const std::string &value)
	{
		ctl("set " + id + " " + key + " " + quote(value) + "\n");
	}

	// the next event; false when ui is gone
	bool event(Event &e)
	{
		size_t nl;
		while((nl = buf.find('\n')) == std::string::npos){
			char b[1024];
			ssize_t n = read(evfd, b, sizeof b);
			if(n <= 0)
				return false;
			buf.append(b, n);
		}
		std::string l = buf.substr(0, nl);
		buf.erase(0, nl + 1);
		size_t a = l.find(' ');
		e.kind = l.substr(0, a);
		e.id.clear();
		e.arg.clear();
		if(a != std::string::npos){
			size_t b = l.find(' ', a + 1);
			e.id = l.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
			if(b != std::string::npos)
				e.arg = l.substr(b + 1);
		}
		return true;
	}

	// a file of ui's (tree, ctl, nodes/id/props, ...); "" if none
	std::string read_file(const std::string &name) const
	{
		int fd = open((mtpt + "/" + name).c_str(), O_RDONLY);
		if(fd < 0)
			return "";
		std::string s;
		char b[8192];
		ssize_t n;
		while((n = read(fd, b, sizeof b)) > 0)
			s.append(b, n);
		close(fd);
		return s;
	}

	// nodes/id/image (shown when it is closed), nodes/id/props
	bool write_file(const std::string &name, const std::string &data) const
	{
		int fd = open((mtpt + "/" + name).c_str(), O_WRONLY);
		if(fd < 0)
			return false;
		bool ok = true;
		for(size_t o = 0; o < data.size() && ok; o += 8192){
			size_t n = std::min<size_t>(8192, data.size() - o);
			ok = write(fd, data.data() + o, n) == (ssize_t)n;
		}
		return close(fd) == 0 && ok;
	}

	bool exists(const std::string &id) const
	{
		return access((mtpt + "/nodes/" + id + "/kind").c_str(), F_OK) == 0;
	}

private:
	int ctlfd = -1, evfd = -1;
	std::string buf;
};

// the lines of one transaction
class Tx {
public:
	// node id of kind in parent, its properties key, value, ...
	template<typename... KV>
	Tx &node(const std::string &id, const std::string &kind, const std::string &parent, const KV&... kv)
	{
		s += "new " + id + " " + kind + " " + parent + "\n";
		return set(id, kv...);
	}

	Tx &set(const std::string &) { return *this; }

	template<typename V, typename... KV>
	Tx &set(const std::string &id, const std::string &key, const V &value, const KV&... kv)
	{
		s += "set " + id + " " + key + " " + quote(std::string(value)) + "\n";
		return set(id, kv...);
	}

	Tx &line(const std::string &l) { s += l + "\n"; return *this; }

	void send(Ui &u) { u.ctl(s); s.clear(); }

private:
	std::string s;
};

}	// namespace ui

}	// extern "C++"

#endif
