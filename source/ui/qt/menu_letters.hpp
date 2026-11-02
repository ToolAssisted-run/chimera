#pragma once

#include <QObject>
#include <QString>

namespace chimera::qt {

/* The letter that opens a menu, underlined only while the keyboard works the
 * menus, as the old window showed it (issue 405): from Alt pressed until the
 * menus are left (a click, the focus elsewhere, Alt let go with no menu
 * taken). The letters work either way; only what is drawn changes. An
 * application-wide event filter; the window's menu items read it. */
class MenuLetters : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool shown READ shown NOTIFY shownChanged)

public:
	explicit MenuLetters(QObject *parent = nullptr);
	bool shown() const { return shown_; }
	/* a menu's text as drawn without its letter: "&File" is "File", "&&" is
	 * "&", and the "(&F)" a Chinese, Japanese or Korean text ends with goes
	 * with the spaces before it, as Qt hides it */
	Q_INVOKABLE static QString plain(const QString &text);
	/* a menu's label (Qt's, holding the text with its "&") told whether to
	 * underline its letter: its own switch, which QML cannot reach */
	Q_INVOKABLE static void underline(QObject *label, bool on);

protected:
	bool eventFilter(QObject *watched, QEvent *e) override;

Q_SIGNALS:
	void shownChanged();

private:
	bool shown_ = false;
	void show(bool on);
	/* the focus is on a menu bar, a menu or one of their items */
	static bool inMenus(QObject *focus);
};

} // namespace chimera::qt
