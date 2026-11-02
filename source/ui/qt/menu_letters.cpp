#include "menu_letters.hpp"

#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQuickItem>

namespace chimera::qt {

MenuLetters::MenuLetters(QObject *parent) : QObject(parent)
{
	connect(qGuiApp, &QGuiApplication::focusObjectChanged, this, [this](QObject *focus) {
		if (shown_ && !inMenus(focus)) show(false);
	});
}

QString MenuLetters::plain(const QString &text)
{
	QString out;
	out.reserve(text.size());
	for (qsizetype i = 0; i < text.size(); i++)
	{
		const QChar c = text.at(i);
		if (c == QLatin1Char('(') && i + 3 < text.size() && text.at(i + 1) == QLatin1Char('&') && text.at(i + 2) != QLatin1Char('&') &&
			text.at(i + 3) == QLatin1Char(')'))
		{
			while (!out.isEmpty() && out.back().isSpace()) out.chop(1);
			i += 3;
			continue;
		}
		if (c == QLatin1Char('&'))
		{
			if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('&'))
			{
				out += QLatin1Char('&');
				i++;
			}
			continue;
		}
		out += c;
	}
	return out;
}

void MenuLetters::underline(QObject *label, bool on)
{
	if (label != nullptr && label->property("mnemonicVisible").toBool() != on) label->setProperty("mnemonicVisible", on);
}

bool MenuLetters::inMenus(QObject *focus)
{
	for (QObject *o = focus; o != nullptr; o = o->parent())
		if (o->inherits("QQuickMenuBar") || o->inherits("QQuickMenu") || o->inherits("QQuickMenuItem") || o->inherits("QQuickMenuBarItem"))
			return true;
	if (auto *item = qobject_cast<QQuickItem *>(focus))
		for (QQuickItem *i = item; i != nullptr; i = i->parentItem())
			if (i->inherits("QQuickMenuBar") || i->inherits("QQuickPopupItem")) return true;
	return false;
}

void MenuLetters::show(bool on)
{
	if (shown_ == on) return;
	shown_ = on;
	Q_EMIT shownChanged();
}

bool MenuLetters::eventFilter(QObject *watched, QEvent *e)
{
	switch (e->type())
	{
	case QEvent::KeyPress:
		if (static_cast<QKeyEvent *>(e)->key() == Qt::Key_Alt) show(true);
		break;
	case QEvent::KeyRelease:
		/* Alt let go: the letters stay only while it took the keyboard to the menus */
		if (static_cast<QKeyEvent *>(e)->key() == Qt::Key_Alt && !inMenus(QGuiApplication::focusObject())) show(false);
		break;
	case QEvent::MouseButtonPress:
	case QEvent::ApplicationDeactivate:
		show(false);
		break;
	default:
		break;
	}
	return QObject::eventFilter(watched, e);
}

} // namespace chimera::qt
