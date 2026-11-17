#!/bin/sh
# Chimera reaches for nothing over the network (user-decided, 2026-10-07;
# docs/core-manager.md, "Nothing reaches the network"). This holds the line:
# it fails if the frontend's or the engine's sources name a network API.
#
# It reads SOURCE, not binaries, and it is a list of names, so it is a tripwire
# and not a proof: what it catches is the ordinary way such code comes back - a
# class that seemed harmless, pasted in or restored from history. A link that
# is handed to the system's browser is not a request Chimera makes, and is not
# looked for; nor is local IPC (named pipes, memory-mapped files).
#
# Usage: tools/check-no-network.sh [<root>]      exit 0 clean, 1 with the lines
set -eu
root="${1:-$(cd "$(dirname "$0")/.." && pwd)}"

# native: the headers and calls
native='sys/socket\.h|netinet/|arpa/inet\.h|netdb\.h|winsock|ws2tcpip|\bWSAStartup\b|\bgetaddrinfo\b|\bgethostbyname\b|curl/curl\.h|\bcurl_easy_'

found=0
# and the sandbox's host side, which ships in every bundle too: libminiboxhost
# is built from source/host, and libchimera compiles the GL, Vulkan and cache
# bridges. Its guest side is not Chimera's: it is the C library a core is
# built with, inside the sandbox, and a guest's socket call is refused there.
mb="$root/extern/chimera-common-minibox/source"
if grep -rnE --include='*.c' --include='*.cc' --include='*.cpp' --include='*.h' --include='*.hpp' \
	"$native" "$root/source/engine" "$mb/host" "$mb/gl" "$mb/vk" "$mb/cache"; then
	found=1
fi
# the window (source/ui): its C++ with the native names, and Qt's network
# classes and modules in its C++, QML and build file. Local pipes and Qt's
# QLocalSocket are not the network and are not looked for.
qt='\bQNetwork[A-Za-z]*\b|\bQTcpSocket\b|\bQTcpServer\b|\bQUdpSocket\b|\bQSslSocket\b|\bQWebSocket[A-Za-z]*\b|\bQHostAddress\b|\bXMLHttpRequest\b|import QtNetwork|QtWebEngine'
if [ -d "$root/source/ui" ]; then
	if grep -rnE --include='*.c' --include='*.cpp' --include='*.h' --include='*.hpp' "$native" "$root/source/ui"; then
		found=1
	fi
	# One file is let through, and checked instead: qt/core_windows.cpp, whose
	# manager turns every request a core's own window would make into one to
	# a scheme nothing serves (issue 610). It must still refuse, and its build
	# line says so.
	refuser="$root/source/ui/qt/core_windows.cpp"
	if grep -rnE --include='*.cpp' --include='*.hpp' --include='*.qml' --include='*.js' "$qt" "$root/source/ui" | grep -v "^$refuser:"; then
		found=1
	fi
	if [ -f "$refuser" ] && ! { grep -q 'createRequest' "$refuser" && grep -q 'kRefused' "$refuser"; }; then
		echo "$refuser: its network manager no longer refuses every request" >&2
		found=1
	fi
	if grep -nE "'Network'|'WebSockets'|'WebEngine" "$root/source/ui/meson.build" | grep -v 'refuses, never sends'; then
		found=1
	fi
fi
# what is BUILT from extern/ for the bundle is named in meson.build; a network
# library there is the same thing by another road
if grep -nE 'luasocket|libcurl|ws2_32|wsock32' "$root/meson.build"; then
	found=1
fi

if [ "$found" -ne 0 ]; then
	echo "check-no-network: the lines above name a network API. Chimera has none; see docs/core-manager.md." >&2
	exit 1
fi
echo "check-no-network: clean (source/ui, source/engine, miniBox's host side, meson.build)"
