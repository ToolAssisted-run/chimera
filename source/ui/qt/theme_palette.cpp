#include "theme_palette.hpp"

#include <QColor>
#include <QGuiApplication>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStyleHints>
#include <QWindow>

#include <functional>
#include <iterator>

namespace chimera::qt {

namespace {
QHash<QString, QColor> &current()
{
	static QHash<QString, QColor> colours;
	return colours;
}

/* the platform's palette, as it was before any theme */
QPalette &platformStore()
{
	static QPalette p = QGuiApplication::palette();
	return p;
}
const QPalette &platform()
{
	return platformStore();
}

/* the desktop's own palette, no theme's and nothing asked of it: as it was
 * at the start */
QPalette &desktopStore()
{
	static QPalette p = QGuiApplication::palette();
	return p;
}

/* the theme on is a dark one */
bool &themeDark()
{
	static bool d = false;
	return d;
}

/* The desktop's light colours for a light theme and its dark ones for a
 * dark theme, whatever the desktop is set to (issue 471): the Light theme
 * names the desktop's colours ("system:Window") beside light ones of its
 * own (the rows of a list), and with Windows in dark mode the two met as
 * white text on white rows. The platform's palette is read again after. */
void askScheme(bool dark)
{
	platform();
	desktopStore();
	const Qt::ColorScheme want = dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light;
	QStyleHints *hints = QGuiApplication::styleHints();
	if (hints->colorScheme() == want)
	{
		platformStore() = paletteFor(platformStore(), dark);
		return;
	}
	hints->setColorScheme(want);
	/* the theme's palette taken off, the platform's for the scheme read */
	QGuiApplication::setPalette(QPalette());
	platformStore() = paletteFor(QGuiApplication::palette(), dark);
}

/* Qt Quick's controls take their colours from the platform's palette, not
 * from the application's: a window hands its controls only the colours set
 * on its own palette. So each window is given the theme's colours as its
 * own - all of them, in each of the three states - and every control in it,
 * its menus and its dialogs inherit them. A window that sets its own
 * colours ("unthemed": the Lua forms, in the platform's) is left alone. */
void paintWindow(QQuickWindow *window)
{
	if (window->property("unthemed").toBool()) return;
	QObject *palette = window->property("palette").value<QObject *>();
	if (!palette) return;
	static const char *const names[] = {"window", "windowText", "base", "alternateBase", "text", "button", "buttonText",
		"brightText", "light", "midlight", "mid", "dark", "shadow", "highlight", "highlightedText", "link", "linkVisited",
		"toolTipBase", "toolTipText", "placeholderText", "accent"};
	static const QPalette::ColorRole roles[] = {QPalette::Window, QPalette::WindowText, QPalette::Base, QPalette::AlternateBase,
		QPalette::Text, QPalette::Button, QPalette::ButtonText, QPalette::BrightText, QPalette::Light, QPalette::Midlight,
		QPalette::Mid, QPalette::Dark, QPalette::Shadow, QPalette::Highlight, QPalette::HighlightedText, QPalette::Link,
		QPalette::LinkVisited, QPalette::ToolTipBase, QPalette::ToolTipText, QPalette::PlaceholderText, QPalette::Accent};
	static_assert(std::size(names) == std::size(roles));
	const QPalette app = QGuiApplication::palette();
	const struct
	{
		const char *name;
		QPalette::ColorGroup group;
	} groups[] = {{"active", QPalette::Active}, {"inactive", QPalette::Inactive}, {"disabled", QPalette::Disabled}};
	for (const auto &g : groups)
		if (QObject *colours = palette->property(g.name).value<QObject *>())
			for (size_t i = 0; i < std::size(names); i++)
			{
				const bool known = colours->setProperty(names[i], app.color(g.group, roles[i]));
				Q_ASSERT(known);
			}
}

/* Qt's Fusion file and folder dialogs leave the names of the places beside
 * the list (Home, Desktop...) and their icons without a colour, so black,
 * on the theme's field colour: unreadable in a dark theme (#389). Each takes
 * the button text, as Fusion's buttons do. Chimera draws Qt's dialogs in their window's
 * overlay (dialogs/qmldir), so only that is looked through, whenever
 * something in a window takes the focus - as a dialog's list does when the
 * dialog opens. */
void paintDialogPlaces(QQuickWindow *window)
{
	QQuickItem *content = window->contentItem();
	if (!content) return;
	std::function<void(QQuickItem *, bool)> walk = [&walk](QQuickItem *item, bool inSideBar) {
		for (QQuickItem *c : item->childItems())
		{
			const bool side = inSideBar || c->inherits("QQuickSideBar");
			if (side && c->property("folderName").isValid())
			{
				QObject *palette = c->property("palette").value<QObject *>();
				if (auto *label = c->property("contentItem").value<QQuickItem *>(); label && palette)
				{
					label->setProperty("color", palette->property("buttonText"));
					QQmlProperty(c, QStringLiteral("icon.color")).write(palette->property("buttonText"));
				}
			}
			walk(c, side);
		}
	};
	for (QQuickItem *top : content->childItems())
		if (top->inherits("QQuickOverlay")) walk(top, false);
}

/* each Qt Quick window, as it is first shown; Qt's dialogs, as they open */
class WindowPainter : public QObject
{
public:
	using QObject::QObject;
	bool eventFilter(QObject *watched, QEvent *event) override
	{
		if (event->type() == QEvent::Show)
			if (auto *window = qobject_cast<QQuickWindow *>(watched)) paintWindow(window);
		if (event->type() == QEvent::FocusIn)
			if (auto *item = qobject_cast<QQuickItem *>(watched); item && item->window()) paintDialogPlaces(item->window());
		return false;
	}
};
} // namespace

const QPalette &platformPalette()
{
	return platform();
}

QPalette paletteFor(const QPalette &desktop, bool dark)
{
	if (dark || desktop.color(QPalette::Window).lightness() >= 128) return desktop;
	/* Qt's own light palette, as Qt 6.12 has it with no desktop's: the active
	 * colours, then the disabled ones that differ, the inactive being the active */
	static const QPalette qt = [] {
		QPalette p;
		const struct { QPalette::ColorRole role; QRgb active, disabled; } colours[] = {
			{QPalette::WindowText, 0xff000000, 0xffbebebe}, {QPalette::Button, 0xffefefef, 0xffefefef},
			{QPalette::Light, 0xffffffff, 0xffffffff}, {QPalette::Midlight, 0xffcacaca, 0xffcacaca},
			{QPalette::Dark, 0xff9f9f9f, 0xffbebebe}, {QPalette::Mid, 0xffb8b8b8, 0xffb8b8b8},
			{QPalette::Text, 0xff000000, 0xffbebebe}, {QPalette::BrightText, 0xffffffff, 0xffffffff},
			{QPalette::ButtonText, 0xff000000, 0xffbebebe}, {QPalette::Base, 0xffffffff, 0xffefefef},
			{QPalette::Window, 0xffefefef, 0xffefefef}, {QPalette::Shadow, 0xff767676, 0xffb1b1b1},
			{QPalette::Highlight, 0xff308cc6, 0xff919191}, {QPalette::HighlightedText, 0xffffffff, 0xffffffff},
			{QPalette::Link, 0xff0000ff, 0xff0000ff}, {QPalette::LinkVisited, 0xffff00ff, 0xffff00ff},
			{QPalette::AlternateBase, 0xfff7f7f7, 0xfff7f7f7}, {QPalette::ToolTipBase, 0xffffffdc, 0xffffffdc},
			{QPalette::ToolTipText, 0xff000000, 0xff000000}, {QPalette::PlaceholderText, 0x80000000, 0x80000000},
			{QPalette::Accent, 0xff308cc6, 0xff919191}};
		for (const auto &c : colours)
		{
			p.setColor(QPalette::Active, c.role, QColor::fromRgba(c.active));
			p.setColor(QPalette::Inactive, c.role, QColor::fromRgba(c.active));
			p.setColor(QPalette::Disabled, c.role, QColor::fromRgba(c.disabled));
		}
		return p;
	}();
	return qt;
}

namespace {
QColor resolveIn(const QString &value, const QPalette &palette);
}

QColor resolveColor(const QString &value)
{
	return resolveIn(value, platform());
}

QColor resolveColorFor(const QString &value, bool dark)
{
	/* the theme on is of that kind: its palette; else the palette a theme of
	 * that kind would be given (a dark one on a light desktop: the desktop's,
	 * which a desktop that honours the ask would give dark; the drawing can
	 * only guess) */
	if (dark == themeDark()) return resolveIn(value, platform());
	return resolveIn(value, paletteFor(desktopStore(), dark));
}

namespace {
QColor resolveIn(const QString &value, const QPalette &p)
{
	if (!value.startsWith(QStringLiteral("system:"))) return QColor(value);
	const QString name = value.mid(7).trimmed().toLower();
	/* the desktop colours Chimera's themes name, on Qt's; Control is white
	 * smoke off Windows */
	if (name == QStringLiteral("control") || name == QStringLiteral("menu") || name == QStringLiteral("activeborder"))
	{
#ifdef _WIN32
		return p.color(QPalette::Window);
#else
		return name == QStringLiteral("control") ? QColor(0xF5, 0xF5, 0xF5) : p.color(QPalette::Window);
#endif
	}
	if (name == QStringLiteral("controltext") || name == QStringLiteral("menutext")) return p.color(QPalette::WindowText);
	if (name == QStringLiteral("controldark") || name == QStringLiteral("buttonshadow")) return p.color(QPalette::Mid);
	if (name == QStringLiteral("controldarkdark")) return p.color(QPalette::Dark);
	if (name == QStringLiteral("controllight")) return p.color(QPalette::Midlight);
	if (name == QStringLiteral("controllightlight") || name == QStringLiteral("buttonhighlight")) return p.color(QPalette::Light);
	if (name == QStringLiteral("graytext")) return p.color(QPalette::Disabled, QPalette::Text);
	if (name == QStringLiteral("hottrack")) return p.color(QPalette::Link);
	if (name == QStringLiteral("window")) return p.color(QPalette::Base);
	if (name == QStringLiteral("windowtext")) return p.color(QPalette::Text);
	if (name == QStringLiteral("highlight")) return p.color(QPalette::Highlight);
	if (name == QStringLiteral("highlighttext")) return p.color(QPalette::HighlightedText);
	if (name == QStringLiteral("info")) return p.color(QPalette::ToolTipBase);
	if (name == QStringLiteral("infotext")) return p.color(QPalette::ToolTipText);
	if (name == QStringLiteral("buttonface")) return p.color(QPalette::Button);
	return QColor();
}
} // namespace

QColor ThemeColors::platform(const QString &role) const
{
	static const QHash<QString, QPalette::ColorRole> roles{{QStringLiteral("window"), QPalette::Window},
		{QStringLiteral("windowText"), QPalette::WindowText}, {QStringLiteral("base"), QPalette::Base},
		{QStringLiteral("text"), QPalette::Text}, {QStringLiteral("button"), QPalette::Button},
		{QStringLiteral("buttonText"), QPalette::ButtonText}, {QStringLiteral("highlight"), QPalette::Highlight},
		{QStringLiteral("highlightedText"), QPalette::HighlightedText}, {QStringLiteral("mid"), QPalette::Mid},
		{QStringLiteral("dark"), QPalette::Dark}, {QStringLiteral("placeholderText"), QPalette::PlaceholderText}};
	const auto it = roles.find(role);
	return it == roles.end() ? QColor() : platformPalette().color(it.value());
}

QColor themeColor(const char *role)
{
	return current().value(QString::fromLatin1(role));
}

void applyTheme(const QString &colorsJson, bool dark)
{
	QHash<QString, QColor> &colours = current();
	colours.clear();
	const QJsonObject o = QJsonDocument::fromJson(colorsJson.toUtf8()).object();
	if (!o.isEmpty())
	{
		askScheme(dark);
		themeDark() = dark;
	}
	platform();
	for (auto it = o.begin(); it != o.end(); ++it) colours.insert(it.key(), resolveColor(it.value().toString()));
	if (colours.isEmpty()) return;
	auto c = [&](const char *role, QColor fallback) {
		const QColor v = colours.value(QString::fromLatin1(role));
		return v.isValid() ? v : fallback;
	};
	/* Chimera's roles on Qt's: windows, fields, buttons, the selection,
	 * tooltips (the menus' colours), and what is disabled */
	QPalette p = platform();
	const QColor window = c("WindowBackground", p.color(QPalette::Window)), text = c("WindowText", p.color(QPalette::WindowText));
	p.setColor(QPalette::Window, window);
	p.setColor(QPalette::WindowText, text);
	p.setColor(QPalette::Base, c("InputBackground", window));
	p.setColor(QPalette::AlternateBase, c("AlternateRowBackground", window));
	p.setColor(QPalette::Text, c("InputText", text));
	p.setColor(QPalette::Button, c("ButtonBackground", window));
	p.setColor(QPalette::ButtonText, c("ButtonText", text));
	p.setColor(QPalette::Highlight, c("Selection", QColor(0x3d, 0x8b, 0xff)));
	p.setColor(QPalette::HighlightedText, c("SelectionText", Qt::white));
	p.setColor(QPalette::ToolTipBase, c("MenuBackground", window));
	p.setColor(QPalette::ToolTipText, c("MenuText", text));
	p.setColor(QPalette::PlaceholderText, c("MutedText", text));
	p.setColor(QPalette::Link, c("LinkText", text));
	p.setColor(QPalette::Mid, c("Border", window));
	p.setColor(QPalette::Dark, c("ButtonBorder", window));
	/* the menus' dividers (MenuDivider.qml); Fusion draws only a split handle under the pointer in midlight */
	p.setColor(QPalette::Midlight, c("MenuSeparator", platform().color(QPalette::Mid)));
	p.setColor(QPalette::Shadow, c("GlyphShadow", Qt::black));
	/* only Qt's own dialogs use it: the folder bar, the places beside the
	 * list and the tint under a picked line, all fields of a kind (#389) */
	p.setColor(QPalette::Light, c("InputBackground", window));
	for (QPalette::ColorRole r : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
		p.setColor(QPalette::Disabled, r, c("DisabledText", text));
	for (QPalette::ColorRole r : {QPalette::Base, QPalette::Button})
		p.setColor(QPalette::Disabled, r, c("DisabledBackground", window));
	QGuiApplication::setPalette(p);
	static WindowPainter *painter = nullptr;
	if (!painter)
	{
		painter = new WindowPainter(qApp);
		qApp->installEventFilter(painter);
	}
	/* the windows open now take it, and repaint */
	for (QWindow *w : QGuiApplication::allWindows())
	{
		if (auto *quick = qobject_cast<QQuickWindow *>(w)) paintWindow(quick);
		w->requestUpdate();
	}
}

} // namespace chimera::qt
