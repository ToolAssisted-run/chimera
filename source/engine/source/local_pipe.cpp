/* local_pipe.cpp - see local_pipe.hpp. */

#include "local_pipe.hpp"
#include "language.hpp"

#include <cstdlib>
#include <cstring>

namespace chimera
{

namespace
{

std::string g_suffix;
std::string g_name;

/* What is added to the pipe's name: the tests' suffix, and the name in the
 * environment variable CHIMERA_MCP_NAME when it is set. Both ends must be
 * given the same name. It is how a second Chimera of one user serves beside
 * the first, and how a test keeps clear of a Chimera that is running. */
std::string nameSuffix()
{
	std::string out = g_suffix;
	const char *named = g_name.empty() ? std::getenv("CHIMERA_MCP_NAME") : g_name.c_str();
	if (named != nullptr && named[0] != '\0')
	{
		out += '-';
		for (const char *c = named; *c != '\0' && out.size() < 64; c++)
		{
			const bool plain = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9');
			out += plain ? *c : '_';
		}
	}
	return out;
}

/* Takes the first whole line out of `buffer`. */
bool takeLine(std::string &buffer, std::string &line)
{
	const size_t end = buffer.find('\n');
	if (end == std::string::npos) return false;
	line.assign(buffer, 0, end);
	if (!line.empty() && line.back() == '\r') line.pop_back();
	buffer.erase(0, end + 1);
	return true;
}

const char *const kNoServer = "pipe.no_server";
const char *const kOtherServer = "pipe.other_server";
const char *const kOtherClient = "pipe.other_client";

} // namespace

void pipeNameSuffixForTests(const std::string &suffix) { g_suffix = suffix; }
void pipeName(const std::string &name) { g_name = name; }

} // namespace chimera

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>

namespace chimera
{

namespace
{

/* \\.\pipe\chimera-mcp-<user>: one per user, so two users of one machine do
 * not meet. */
std::wstring pipeName()
{
	wchar_t user[257];
	DWORD len = 257;
	std::wstring name = L"\\\\.\\pipe\\chimera-mcp-";
	if (GetUserNameW(user, &len) != 0)
	{
		for (const wchar_t *c = user; *c != 0; c++)
		{
			const bool plain = (*c >= L'a' && *c <= L'z') || (*c >= L'A' && *c <= L'Z') || (*c >= L'0' && *c <= L'9');
			name += plain ? *c : L'_';
		}
	}
	for (char c : nameSuffix()) name += static_cast<wchar_t>(c);
	return name;
}

std::string narrow(const std::wstring &wide)
{
	const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
	std::string out(static_cast<size_t>(n > 0 ? n : 0), '\0');
	if (n > 0) WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), n, nullptr, nullptr);
	return out;
}

std::string lastErrorText(const std::string &what, DWORD code)
{
	return chimera::lang::tr("pipe.windows_error", {what, std::to_string(static_cast<unsigned long>(code))});
}

/* A security descriptor that lets this user in and nobody else. The default
 * one would let every user of the machine read the pipe. NULL when it cannot
 * be made, and the caller then refuses to serve. */
PSECURITY_DESCRIPTOR onlyThisUser()
{
	HANDLE token = nullptr;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == 0) return nullptr;
	BYTE info[512];
	DWORD used = 0;
	PSECURITY_DESCRIPTOR sd = nullptr;
	if (GetTokenInformation(token, TokenUser, info, sizeof(info), &used) != 0)
	{
		LPWSTR sid = nullptr;
		if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(info)->User.Sid, &sid) != 0)
		{
			const std::wstring sddl = std::wstring(L"D:P(A;;GA;;;") + sid + L")";
			LocalFree(sid);
			if (ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr) == 0) sd = nullptr;
		}
	}
	CloseHandle(token);
	return sd;
}

/* Whether the process at the other end of a pipe runs as this user. A pipe's
 * name is only a name: any user of the machine may make one of it first, and
 * a client that took whatever answered would hand its calls - and, with the
 * default security, the right to act as this user - to that one. */
bool serverIsThisUser(HANDLE pipe)
{
	ULONG pid = 0;
	if (GetNamedPipeServerProcessId(pipe, &pid) == 0) return false;
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (process == nullptr) return false;   /* another user's process will not even be opened */
	bool same = false;
	HANDLE theirs = nullptr, mine = nullptr;
	if (OpenProcessToken(process, TOKEN_QUERY, &theirs) != 0 && OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &mine) != 0)
	{
		BYTE a[512], b[512];
		DWORD used = 0;
		if (GetTokenInformation(theirs, TokenUser, a, sizeof(a), &used) != 0
			&& GetTokenInformation(mine, TokenUser, b, sizeof(b), &used) != 0)
		{
			same = EqualSid(reinterpret_cast<TOKEN_USER *>(a)->User.Sid, reinterpret_cast<TOKEN_USER *>(b)->User.Sid) != 0;
		}
	}
	if (theirs != nullptr) CloseHandle(theirs);
	if (mine != nullptr) CloseHandle(mine);
	CloseHandle(process);
	return same;
}

/* One end's reading: an overlapped read that a wait with a time limit can be
 * put on. Shared by the server and the client. */
struct Reader
{
	OVERLAPPED ov{};
	bool pending = false;
	char chunk[65536];
	std::string buffer;

	Reader() { ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
	~Reader() { CloseHandle(ov.hEvent); }

	/* Line, Nothing or Disconnected. */
	PipeEvent wait(HANDLE pipe, int timeout_ms, std::string &line)
	{
		if (takeLine(buffer, line)) return PipeEvent::Line;
		DWORD got = 0;
		if (!pending)
		{
			ResetEvent(ov.hEvent);
			if (ReadFile(pipe, chunk, sizeof(chunk), &got, &ov) != 0)
			{
				buffer.append(chunk, got);
				return takeLine(buffer, line) ? PipeEvent::Line : PipeEvent::Nothing;
			}
			if (GetLastError() != ERROR_IO_PENDING) return PipeEvent::Disconnected;
			pending = true;
		}
		if (WaitForSingleObject(ov.hEvent, static_cast<DWORD>(timeout_ms < 0 ? 0 : timeout_ms)) != WAIT_OBJECT_0)
			return PipeEvent::Nothing;
		pending = false;
		if (GetOverlappedResult(pipe, &ov, &got, FALSE) == 0) return PipeEvent::Disconnected;
		buffer.append(chunk, got);
		return takeLine(buffer, line) ? PipeEvent::Line : PipeEvent::Nothing;
	}

	void reset(HANDLE pipe)
	{
		if (pending)
		{
			CancelIo(pipe);
			DWORD got = 0;
			GetOverlappedResult(pipe, &ov, &got, TRUE);
			pending = false;
		}
		buffer.clear();
	}
};

bool writeLine(HANDLE pipe, const std::string &line)
{
	std::string data = line;
	data += '\n';
	OVERLAPPED ov{};
	ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	bool ok = true;
	size_t at = 0;
	while (ok && at < data.size())
	{
		DWORD wrote = 0;
		ResetEvent(ov.hEvent);
		if (WriteFile(pipe, data.data() + at, static_cast<DWORD>(data.size() - at), &wrote, &ov) == 0)
		{
			if (GetLastError() != ERROR_IO_PENDING) ok = false;
			/* the reader is slow: give it five seconds, as on Linux, then give
			 * up on it - a reader that never reads would otherwise hold this
			 * thread, and the server's stop that waits for it, for ever */
			else if (WaitForSingleObject(ov.hEvent, 5000) != WAIT_OBJECT_0)
			{
				CancelIoEx(pipe, &ov);
				GetOverlappedResult(pipe, &ov, &wrote, TRUE);
				ok = false;
			}
			else if (GetOverlappedResult(pipe, &ov, &wrote, FALSE) == 0) ok = false;
		}
		at += wrote;
		if (ok && wrote == 0) ok = false;
	}
	CloseHandle(ov.hEvent);
	return ok;
}

} // namespace

struct PipeServer::Impl
{
	HANDLE pipe = INVALID_HANDLE_VALUE;
	OVERLAPPED connectOv{};
	bool connecting = false; /* a ConnectNamedPipe is waiting for a client */
	bool arrived = false;    /* a client came the moment we started listening */
	bool connected = false;
	Reader reader;

	Impl() { connectOv.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
	~Impl() { CloseHandle(connectOv.hEvent); }

	/* Starts waiting for a client. A pipe that is not listening refuses
	 * every client as "busy", so this is done as soon as the pipe is made and
	 * again the moment a client leaves, not at the next wait. */
	void listen()
	{
		if (connecting || arrived || connected) return;
		ResetEvent(connectOv.hEvent);
		if (ConnectNamedPipe(pipe, &connectOv) != 0 || GetLastError() == ERROR_PIPE_CONNECTED) arrived = true;
		else if (GetLastError() == ERROR_IO_PENDING) connecting = true;
		else DisconnectNamedPipe(pipe); /* a client that came and went; the next wait listens again */
	}

	void drop()
	{
		reader.reset(pipe);
		DisconnectNamedPipe(pipe);
		connected = false;
		listen();
	}
};

PipeServer::PipeServer() : impl(new Impl) {}
PipeServer::~PipeServer() { close(); delete impl; }

bool PipeServer::open(std::string &error)
{
	close();
	PSECURITY_DESCRIPTOR sd = onlyThisUser();
	if (sd == nullptr)
	{
		error = chimera::lang::tr("pipe.not_private");
		return false;
	}
	SECURITY_ATTRIBUTES sa{};
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = sd;
	impl->pipe = CreateNamedPipeW(pipeName().c_str(),
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
		1, 1 << 16, 1 << 16, 0, &sa);
	const DWORD code = GetLastError();
	LocalFree(sd);
	if (impl->pipe == INVALID_HANDLE_VALUE)
	{
		error = code == ERROR_ACCESS_DENIED || code == ERROR_PIPE_BUSY
			? chimera::lang::tr(kOtherServer) : lastErrorText(chimera::lang::tr("pipe.not_made"), code);
		return false;
	}
	impl->listen();
	return true;
}

void PipeServer::close()
{
	if (impl->pipe == INVALID_HANDLE_VALUE) return;
	impl->reader.reset(impl->pipe);
	CancelIo(impl->pipe);
	DisconnectNamedPipe(impl->pipe);
	CloseHandle(impl->pipe);
	impl->pipe = INVALID_HANDLE_VALUE;
	impl->connecting = impl->arrived = impl->connected = false;
}

PipeEvent PipeServer::wait(int timeout_ms, std::string &line)
{
	if (impl->pipe == INVALID_HANDLE_VALUE) return PipeEvent::Nothing;
	if (!impl->connected)
	{
		impl->listen();
		if (impl->connecting)
		{
			if (WaitForSingleObject(impl->connectOv.hEvent, static_cast<DWORD>(timeout_ms < 0 ? 0 : timeout_ms)) != WAIT_OBJECT_0)
				return PipeEvent::Nothing;
			impl->connecting = false;
			DWORD ignored = 0;
			if (GetOverlappedResult(impl->pipe, &impl->connectOv, &ignored, FALSE) == 0)
			{
				DisconnectNamedPipe(impl->pipe);
				return PipeEvent::Nothing;
			}
			impl->arrived = true;
		}
		if (!impl->arrived) return PipeEvent::Nothing;
		impl->arrived = false;
		impl->connected = true;
		return PipeEvent::Connected;
	}
	const PipeEvent event = impl->reader.wait(impl->pipe, timeout_ms, line);
	if (event == PipeEvent::Disconnected) impl->drop();
	return event;
}

bool PipeServer::send(const std::string &line)
{
	return impl->pipe != INVALID_HANDLE_VALUE && impl->connected && writeLine(impl->pipe, line);
}

std::string PipeServer::name() const { return narrow(pipeName()); }

struct PipeClient::Impl
{
	HANDLE pipe = INVALID_HANDLE_VALUE;
	Reader reader;
};

PipeClient::PipeClient() : impl(new Impl) {}
PipeClient::~PipeClient() { close(); delete impl; }

bool PipeClient::connect(std::string &error)
{
	close();
	const std::wstring name = pipeName();
	DWORD code = 0;
	for (int attempt = 0; attempt < 2; attempt++)
	{
		/* identification only: the server may learn who called, never act as them */
		impl->pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
			OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
		if (impl->pipe != INVALID_HANDLE_VALUE)
		{
			if (serverIsThisUser(impl->pipe)) return true;
			CloseHandle(impl->pipe);
			impl->pipe = INVALID_HANDLE_VALUE;
			error = chimera::lang::tr("pipe.other_user");
			return false;
		}
		code = GetLastError();
		/* "busy" is also what a pipe says for the instant between one client
		 * leaving and the server listening again: wait a moment and ask once
		 * more before calling it another agent */
		if (code != ERROR_PIPE_BUSY || WaitNamedPipeW(name.c_str(), 300) == 0) break;
	}
	error = code == ERROR_FILE_NOT_FOUND ? chimera::lang::tr(kNoServer)
		: code == ERROR_PIPE_BUSY ? chimera::lang::tr(kOtherClient)
		: lastErrorText(chimera::lang::tr("pipe.not_opened"), code);
	return false;
}

void PipeClient::close()
{
	if (impl->pipe == INVALID_HANDLE_VALUE) return;
	impl->reader.reset(impl->pipe);
	CloseHandle(impl->pipe);
	impl->pipe = INVALID_HANDLE_VALUE;
}

bool PipeClient::connected() const { return impl->pipe != INVALID_HANDLE_VALUE; }

bool PipeClient::send(const std::string &line)
{
	return impl->pipe != INVALID_HANDLE_VALUE && writeLine(impl->pipe, line);
}

PipeEvent PipeClient::wait(int timeout_ms, std::string &line)
{
	if (impl->pipe == INVALID_HANDLE_VALUE) return PipeEvent::Disconnected;
	return impl->reader.wait(impl->pipe, timeout_ms, line);
}

} // namespace chimera

#else // POSIX

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace chimera
{

namespace
{

/* The folder the two FIFOs live in. */
std::string pipeDir()
{
	const char *runtime = std::getenv("XDG_RUNTIME_DIR");
	std::string dir = runtime != nullptr && runtime[0] == '/'
		? std::string(runtime) + "/chimera-mcp"
		: "/tmp/chimera-mcp-" + std::to_string(static_cast<unsigned long>(getuid()));
	return dir + nameSuffix();
}

/* Where a client looks: the server's folder is decided from ITS environment,
 * and a chimera-mcp started by a program that cleared XDG_RUNTIME_DIR (or a
 * Chimera started without it) would otherwise look in the other place and
 * say no Chimera is running. So: this process's own choice first, then the
 * usual runtime folder, then the /tmp one. */
std::vector<std::string> pipeDirsToTry()
{
	std::vector<std::string> dirs{ pipeDir() };
	const std::string uid = std::to_string(static_cast<unsigned long>(getuid()));
	for (const std::string &d : { "/run/user/" + uid + "/chimera-mcp" + nameSuffix(), "/tmp/chimera-mcp-" + uid + nameSuffix() })
	{
		if (std::find(dirs.begin(), dirs.end(), d) == dirs.end()) dirs.push_back(d);
	}
	return dirs;
}

/* The folder must be ours and closed to everybody else: in /tmp anybody could
 * have made one by that name first. */
bool dirIsOurs(const std::string &dir)
{
	struct stat st;
	return lstat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == getuid() && (st.st_mode & 077) == 0;
}

/* A write to a FIFO whose reader left raises SIGPIPE, which would end the
 * program. It is held back for the length of the write, and taken off the
 * thread again if the write raised it. */
bool writeAll(int fd, const std::string &line)
{
	std::string data = line;
	data += '\n';
	sigset_t pipeSignal, before;
	sigemptyset(&pipeSignal);
	sigaddset(&pipeSignal, SIGPIPE);
	pthread_sigmask(SIG_BLOCK, &pipeSignal, &before);
#ifdef F_SETNOSIGPIPE
	/* macOS: a write to a pipe nobody reads raises no SIGPIPE at all, only
	 * EPIPE - so there is nothing to collect below */
	fcntl(fd, F_SETNOSIGPIPE, 1);
#endif
	bool ok = true;
	bool raised = false;
	size_t at = 0;
	while (ok && at < data.size())
	{
		const ssize_t n = write(fd, data.data() + at, data.size() - at);
		if (n > 0) { at += static_cast<size_t>(n); continue; }
		if (n < 0 && errno == EINTR) continue;
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
		{
			/* the reader is slow: give it five seconds, then give up on it */
			struct pollfd p = { fd, POLLOUT, 0 };
			if (poll(&p, 1, 5000) <= 0 || (p.revents & POLLOUT) == 0) ok = false;
			continue;
		}
		raised = n < 0 && errno == EPIPE;
		ok = false;
	}
	if (raised)
	{
#ifdef __APPLE__
		/* (no sigtimedwait: take it only if it is there, which sigwait then
		 * does at once) */
		sigset_t pending;
		int sig = 0;
		if (sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE)) sigwait(&pipeSignal, &sig);
#else
		const struct timespec now = { 0, 0 };
		sigtimedwait(&pipeSignal, nullptr, &now);
#endif
	}
	pthread_sigmask(SIG_SETMASK, &before, nullptr);
	return ok;
}

int openFlags(int flags) { return flags | O_NONBLOCK | O_CLOEXEC; }

/* Whether a poll that saw nothing may still have missed the other end going
 * away, so that a read has to be asked. Linux reports a FIFO whose last writer
 * closed as POLLHUP; macOS reports nothing at all (measured on macOS 27), and
 * only a read says so, by returning 0. Only once there was a writer: before
 * that, 0 from a read means no more than that nobody has come yet. */
bool writerMayHaveLeft(bool hadWriter)
{
#ifdef __APPLE__
	return hadWriter;
#else
	(void)hadWriter;
	return false;
#endif
}

} // namespace

struct PipeServer::Impl
{
	std::string dir;
	int lockFd = -1;
	int inFd = -1;   /* what the client writes, we read */
	int outFd = -1;  /* what we write, the client reads; open only while a client is there */
	bool connected = false;
	std::string buffer;

	std::string in() const { return dir + "/to-chimera"; }
	std::string out() const { return dir + "/from-chimera"; }

	void reopenIn()
	{
		if (inFd >= 0) ::close(inFd);
		inFd = ::open(in().c_str(), openFlags(O_RDONLY));
		/* deleted under us (the runtime directory goes when the user's last
		 * session ends): made again */
		if (inFd < 0 && errno == ENOENT && ::mkfifo(in().c_str(), 0600) == 0)
			inFd = ::open(in().c_str(), openFlags(O_RDONLY));
	}

	void drop()
	{
		if (outFd >= 0) ::close(outFd);
		outFd = -1;
		connected = false;
		buffer.clear();
		reopenIn();
	}
};

PipeServer::PipeServer() : impl(new Impl) {}
PipeServer::~PipeServer() { close(); delete impl; }

bool PipeServer::open(std::string &error)
{
	close();
	impl->dir = pipeDir();
	if (mkdir(impl->dir.c_str(), 0700) != 0 && errno != EEXIST)
	{
		error = chimera::lang::tr("pipe.folder_not_made", {impl->dir, std::strerror(errno)});
		return false;
	}
	if (!dirIsOurs(impl->dir))
	{
		error = chimera::lang::tr("pipe.folder_not_private", {impl->dir});
		return false;
	}
	impl->lockFd = ::open((impl->dir + "/server.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
	if (impl->lockFd < 0 || flock(impl->lockFd, LOCK_EX | LOCK_NB) != 0)
	{
		error = impl->lockFd < 0 ? chimera::lang::tr("pipe.folder_not_writable", {impl->dir}) : chimera::lang::tr(kOtherServer);
		if (impl->lockFd >= 0) ::close(impl->lockFd);
		impl->lockFd = -1;
		return false;
	}
	/* whatever an earlier server that did not close left behind */
	unlink(impl->in().c_str());
	unlink(impl->out().c_str());
	if (mkfifo(impl->in().c_str(), 0600) != 0 || mkfifo(impl->out().c_str(), 0600) != 0)
	{
		error = chimera::lang::tr("pipe.not_made_why", {std::strerror(errno)});
		close();
		return false;
	}
	impl->reopenIn();
	if (impl->inFd < 0)
	{
		error = chimera::lang::tr("pipe.not_opened_why", {std::strerror(errno)});
		close();
		return false;
	}
	return true;
}

void PipeServer::close()
{
	if (impl->lockFd < 0) return;
	if (impl->outFd >= 0) ::close(impl->outFd);
	if (impl->inFd >= 0) ::close(impl->inFd);
	impl->outFd = impl->inFd = -1;
	impl->connected = false;
	impl->buffer.clear();
	unlink(impl->in().c_str());
	unlink(impl->out().c_str());
	::close(impl->lockFd);
	impl->lockFd = -1;
}

PipeEvent PipeServer::wait(int timeout_ms, std::string &line)
{
	/* The pipe could not be opened again after a client left (it was deleted,
	 * or no descriptor was free): tried again, and if it still cannot be, this
	 * waits out its time rather than return at once. It returned at once, and
	 * the server's thread spun at 100% (engine analysis, 2026-10-18). */
	if (impl->inFd < 0)
	{
		impl->reopenIn();
		if (impl->inFd < 0)
		{
			usleep(static_cast<useconds_t>((timeout_ms > 0 ? timeout_ms : 200) * 1000));
			return PipeEvent::Nothing;
		}
	}
	if (impl->connected && takeLine(impl->buffer, line)) return PipeEvent::Line;
	struct pollfd p = { impl->inFd, POLLIN, 0 };
	if (poll(&p, 1, timeout_ms < 0 ? 0 : timeout_ms) <= 0)
	{
		if (!writerMayHaveLeft(impl->connected)) return PipeEvent::Nothing;
		p.revents = POLLIN;   /* the read below tells: 0 is gone, EAGAIN is not */
	}
	if ((p.revents & POLLIN) != 0)
	{
		char chunk[65536];
		const ssize_t n = read(impl->inFd, chunk, sizeof(chunk));
		if (n > 0)
		{
			impl->buffer.append(chunk, static_cast<size_t>(n));
			if (!impl->connected)
			{
				/* a client's first bytes: it opened its reading end before it
				 * wrote, so ours can be opened for writing now */
				impl->outFd = ::open(impl->out().c_str(), openFlags(O_WRONLY));
				if (impl->outFd < 0)
				{
					impl->drop();
					return PipeEvent::Nothing;
				}
				impl->connected = true;
				return PipeEvent::Connected;
			}
			return takeLine(impl->buffer, line) ? PipeEvent::Line : PipeEvent::Nothing;
		}
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return PipeEvent::Nothing;
	}
	/* the writer closed its end (or there was an error) */
	const bool was = impl->connected;
	if (!was) usleep(20000); /* not a busy loop while nobody is there */
	impl->drop();
	return was ? PipeEvent::Disconnected : PipeEvent::Nothing;
}

bool PipeServer::send(const std::string &line)
{
	return impl->outFd >= 0 && writeAll(impl->outFd, line);
}

std::string PipeServer::name() const { return pipeDir(); }

struct PipeClient::Impl
{
	int lockFd = -1;
	int readFd = -1;
	int writeFd = -1;
	bool heard = false; /* the server has written at least once */
	std::string buffer;
};

PipeClient::PipeClient() : impl(new Impl) {}
PipeClient::~PipeClient() { close(); delete impl; }

bool PipeClient::connect(std::string &error)
{
	close();
	error = chimera::lang::tr(kNoServer);
	for (const std::string &dir : pipeDirsToTry())
	{
		if (!dirIsOurs(dir)) continue;
		impl->lockFd = ::open((dir + "/client.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
		if (impl->lockFd < 0 || flock(impl->lockFd, LOCK_EX | LOCK_NB) != 0)
		{
			if (impl->lockFd >= 0) error = chimera::lang::tr(kOtherClient);
			close();
			continue;
		}
		/* the reading end first: the server opens its writing end when it sees our
		 * first bytes, and that needs a reader to be there */
		impl->readFd = ::open((dir + "/from-chimera").c_str(), openFlags(O_RDONLY));
		/* opening a FIFO for writing fails when nobody has it open for reading,
		 * which is how a server that is not there is told from one that is */
		impl->writeFd = impl->readFd < 0 ? -1 : ::open((dir + "/to-chimera").c_str(), openFlags(O_WRONLY));
		if (impl->writeFd >= 0)
		{
			error.clear();
			return true;
		}
		close();
	}
	return false;
}

void PipeClient::close()
{
	if (impl->writeFd >= 0) ::close(impl->writeFd);
	if (impl->readFd >= 0) ::close(impl->readFd);
	if (impl->lockFd >= 0) ::close(impl->lockFd);
	impl->writeFd = impl->readFd = impl->lockFd = -1;
	impl->heard = false;
	impl->buffer.clear();
}

bool PipeClient::connected() const { return impl->writeFd >= 0; }

bool PipeClient::send(const std::string &line)
{
	return impl->writeFd >= 0 && writeAll(impl->writeFd, line);
}

PipeEvent PipeClient::wait(int timeout_ms, std::string &line)
{
	if (impl->readFd < 0) return PipeEvent::Disconnected;
	if (takeLine(impl->buffer, line)) return PipeEvent::Line;
	struct pollfd p = { impl->readFd, POLLIN, 0 };
	if (poll(&p, 1, timeout_ms < 0 ? 0 : timeout_ms) <= 0)
	{
		if (!writerMayHaveLeft(impl->heard)) return PipeEvent::Nothing;
		p.revents = POLLIN;   /* the read below tells: 0 is gone, EAGAIN is not */
	}
	if ((p.revents & POLLIN) != 0)
	{
		char chunk[65536];
		const ssize_t n = read(impl->readFd, chunk, sizeof(chunk));
		if (n > 0)
		{
			impl->heard = true;
			impl->buffer.append(chunk, static_cast<size_t>(n));
			return takeLine(impl->buffer, line) ? PipeEvent::Line : PipeEvent::Nothing;
		}
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return PipeEvent::Nothing;
	}
	/* No writer. Before the server has written anything that is only the
	 * moment before it opens its end; after, it means the server is gone. */
	if (!impl->heard)
	{
		usleep(10000);
		return PipeEvent::Nothing;
	}
	return PipeEvent::Disconnected;
}

} // namespace chimera

#endif
