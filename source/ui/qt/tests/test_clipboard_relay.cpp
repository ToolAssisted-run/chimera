/* The system's clipboard for QML (clipboard_relay.hpp), on a clipboard that is
 * busy: Windows lets one program have it open at a time and Qt gives up on a
 * busy one without a word. A copy is set once when its text is already
 * there, set again until it takes, and said when it never does (issue 485:
 * the first Copy of a whole movie pasted nothing). */
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTest>

#include <cstdio>

#include "../clipboard_relay.hpp"

namespace {

int failures = 0, checks = 0;
#define CHECK(c) \
	do { \
		checks++; \
		if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } \
	} while (0)

/* a clipboard that refuses the first `busy` writes, as one another program has open does */
struct Clipboard
{
	int busy = 0, writes = 0;
	QString held;
};

} // namespace

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	chimera::qt::ClipboardRelay relay;
	Clipboard board;
	relay.write = [&](QClipboard *, const QString &text) {
		board.writes++;
		if (board.busy > 0) board.busy--;
		else board.held = text;
	};
	relay.read = [&](QClipboard *) { return board.held; };
	QSignalSpy failed(&relay, &chimera::qt::ClipboardRelay::failed);
	const QString lines(500, QLatin1Char('x'));

	/* a free clipboard takes the text at once, and the same text again is left alone */
	relay.setText(lines);
	CHECK(board.held == lines && board.writes == 1);
	relay.setText(lines);
	CHECK(board.writes == 1);

	/* another program's copy in between: the same text is set again */
	board.held = QStringLiteral("something else");
	relay.setText(lines);
	CHECK(board.held == lines && board.writes == 2);

	/* a busy clipboard: set again until it takes, and nothing is said */
	board.held.clear();
	board.busy = 2;
	relay.setText(QStringLiteral("frames"));
	CHECK(board.held.isEmpty() && board.writes == 3);
	CHECK(QTest::qWaitFor([&] { return board.held == QStringLiteral("frames"); }, 3000));
	CHECK(board.writes == 5 && failed.count() == 0);

	/* a newer copy takes over from one still waiting */
	board.held.clear();
	board.busy = 1;
	board.writes = 0;
	relay.setText(QStringLiteral("old"));
	relay.setText(QStringLiteral("new"));
	CHECK(QTest::qWaitFor([&] { return board.held == QStringLiteral("new"); }, 3000));
	QTest::qWait(600);
	CHECK(board.held == QStringLiteral("new") && failed.count() == 0);

	/* one that never takes is said, once, after the last try */
	board.held.clear();
	board.busy = 1000;
	board.writes = 0;
	relay.setText(QStringLiteral("never"));
	CHECK(QTest::qWaitFor([&] { return failed.count() == 1; }, 5000));
	CHECK(board.writes == 8 && board.held.isEmpty());
	QTest::qWait(600);
	CHECK(failed.count() == 1 && board.writes == 8);

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
