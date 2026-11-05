#pragma once

#include <functional>

#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariant>

namespace chimera::qt {

/* The system's clipboard, for QML: the piano roll's Copy and Cut put their
 * lines there and Paste reads lines back from it, as Chimera's do, so frames
 * go between projects and to and from a text editor.
 *
 * Windows lets one program have the clipboard open at a time, and Qt gives up
 * on a busy one after 300 ms without a word. Worse, a second set of the text
 * that fails takes back what the first one put there, so a paste gets nothing
 * (issue 485: Copy sets the text from the menu and again from the main window,
 * and the first Copy of the movie pasted nothing). So the text is set once
 * when it is already there, and read back; one that did not take is set again
 * a moment later, and said when it never does. */
class ClipboardRelay : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool hasText READ hasText NOTIFY changed)
public:
	explicit ClipboardRelay(QObject *parent = nullptr) : QObject(parent)
	{
		connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &ClipboardRelay::changed);
	}
	bool hasText() const { return !QGuiApplication::clipboard()->text().isEmpty(); }
	Q_INVOKABLE QString text() const { return QGuiApplication::clipboard()->text(); }
	Q_INVOKABLE void setText(const QString &text)
	{
		const int ask = ++asked_;
		if (put_ == text && took(text)) return;
		put(text, ask, 0);
	}
	/* a picture (an item grabbed) on the clipboard: the Surface Viewer's Copy to Clipboard */
	Q_INVOKABLE void setImage(const QVariant &image) { QGuiApplication::clipboard()->setImage(image.value<QImage>()); }

	/* the system's clipboard written and read; a test puts a busy one in their place */
	std::function<void(QClipboard *, const QString &)> write = [](QClipboard *c, const QString &t) { c->setText(t); };
	std::function<QString(QClipboard *)> read = [](QClipboard *c) { return c->text(); };

Q_SIGNALS:
	void changed();
	/* the text never got on the clipboard: another program had it open */
	void failed();

private:
	static constexpr int kTries = 8, kWaitMs = 250;
	bool took(const QString &text) const { return read(QGuiApplication::clipboard()) == text; }
	void put(const QString &text, int ask, int tried)
	{
		/* a later copy has come: this one's turn is over */
		if (ask != asked_) return;
		write(QGuiApplication::clipboard(), text);
		if (took(text))
		{
			put_ = text;
			return;
		}
		put_.clear();
		if (tried + 1 >= kTries)
		{
			Q_EMIT failed();
			return;
		}
		QTimer::singleShot(kWaitMs, this, [this, text, ask, tried] { put(text, ask, tried + 1); });
	}
	/* the copy asked for last, and the text that is on the clipboard from here */
	int asked_ = 0;
	QString put_;
};

} // namespace chimera::qt
