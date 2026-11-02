#pragma once

#include <QColor>
#include <QObject>
#include <QPalette>
#include <QString>

namespace chimera::qt {

/* A Chimera theme's colours ({"Role": "#RRGGBB" or "system:Name"}) made the
 * application's palette, and kept for the items that draw their own
 * colours (the piano roll's TAStudio colours). "{}" leaves the platform's
 * palette. A light theme takes the desktop's light colours and a dark one
 * its dark colours, whatever the desktop is set to (issue 471). */
void applyTheme(const QString &colorsJson, bool dark = false);
/* a colour of the theme in use, or an invalid colour when it has none */
QColor themeColor(const char *role);
/* the platform's palette as it was before any theme, in the colour scheme
 * the theme asked for */
const QPalette &platformPalette();
/* a theme's colour as written: "#RRGGBB", or a desktop colour by the name
 * Chimera's themes use ("system:Control"), which is the platform's own */
QColor resolveColor(const QString &value);

/* the same, for QML */
class ThemeColors : public QObject
{
	Q_OBJECT
public:
	using QObject::QObject;
	Q_INVOKABLE QColor resolve(const QString &value) const { return resolveColor(value); }
	/* the platform's own colour for a palette role ("window", "windowText",
	 * "base", "text", "button", "buttonText", "highlight", "highlightedText",
	 * "mid", "dark", "placeholderText"), as it was before any theme: what a
	 * window Chimera leaves unthemed (a Lua form) is drawn in */
	Q_INVOKABLE QColor platform(const QString &role) const;
};

} // namespace chimera::qt
