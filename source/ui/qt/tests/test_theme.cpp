/* A Chimera theme reaches Qt Quick's controls: a window's buttons and
 * fields, its menus, a window opened later, and a theme chosen while they
 * are open; a window that says it is "unthemed" (a Lua form) keeps the
 * colours it set itself.
 *
 * Qt Quick's controls take their colours from the platform's palette, not
 * the application's, so a theme set only as the application's palette
 * leaves them in the desktop's colours: this is what failed before each
 * window was given the theme as its own palette. */
#include "../file_places.hpp"
#include "../portal_file_chooser.hpp"
#include "../theme_palette.hpp"

#include <QColor>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlFileSelector>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>

namespace {
int checks = 0, failed = 0;

void check(bool ok, const char *what, int line)
{
	checks++;
	if (ok) return;
	failed++;
	std::printf("FAIL %s:%d: %s\n", __FILE__, line, what);
}
#define CHECK(x) check((x), #x, __LINE__)

const char *const kWindow = R"(
import QtQuick
import QtQuick.Controls
ApplicationWindow {
    width: 200; height: 120; visible: true
    property bool unthemed: false
    readonly property color windowColour: palette.window
    readonly property color buttonColour: button.palette.button
    readonly property color fieldColour: field.palette.base
    readonly property color menuColour: menu.palette.window
    readonly property color dividerColour: divider.contentItem.color
    readonly property color lightColour: palette.light
    Column {
        Button { id: button; text: "B" }
        TextField { id: field; text: "F" }
    }
    Menu { id: menu; MenuItem { text: "M" } MenuDivider { id: divider } }
}
)";

const char *const kDialogs = R"(
import QtQuick
import ChimeraDialogs
Item {
    readonly property string inside: [file, folder, message, colour, font].map(d => d.popupType).join(" ")
    readonly property string enums: [FileDialog.SaveFile, MessageDialog.Ok].join(" ")
    FileDialog { id: file }
    FolderDialog { id: folder }
    MessageDialog { id: message }
    ColorDialog { id: colour }
    FontDialog { id: font }
}
)";

const char *const kFileDialog = R"(
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
ApplicationWindow {
    width: 640; height: 480; visible: true
    property alias dialog: file
    FileDialog { id: file }
}
)";

const char *const kUnthemed = R"(
import QtQuick
import QtQuick.Controls
ApplicationWindow {
    width: 200; height: 120; visible: true
    readonly property bool unthemed: true
    palette.button: "#123456"
    readonly property color buttonColour: button.palette.button
    readonly property color windowColour: palette.window
    Button { id: button; text: "B" }
}
)";

std::unique_ptr<QObject> make(QQmlEngine &engine, const char *qml)
{
	QQmlComponent component(&engine);
	/* beside the window's own files, for their types (MenuDivider) */
	component.setData(qml, QUrl::fromLocalFile(QStringLiteral(CHIMERA_QT_QML_DIR "/test.qml")));
	std::unique_ptr<QObject> o(component.create());
	if (!o) std::printf("%s\n", qPrintable(component.errorString()));
	for (int i = 0; i < 5; i++) QCoreApplication::processEvents();
	return o;
}

QColor colour(const std::unique_ptr<QObject> &o, const char *name)
{
	return o ? o->property(name).value<QColor>() : QColor();
}
} // namespace

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	/* after the application, as the window sets its own (main.cpp): KDE's
	 * platform theme sets Breeze's style as the application is made */
	QQuickStyle::setStyle(QStringLiteral("Fusion"));
	const QColor platformButton = QGuiApplication::palette().color(QPalette::Button);
	const QColor window(0x2B, 0x2B, 0x2B), button(0x3C, 0x3F, 0x41), field(0x1E, 0x1E, 0x1E);
	CHECK(platformButton != button);
	chimera::qt::applyTheme(QStringLiteral(
		R"({"WindowBackground": "#2B2B2B", "WindowText": "#E0E0E0", "ButtonBackground": "#3C3F41", "InputBackground": "#1E1E1E",
		   "MenuSeparator": "#5A5A5A"})"));

	QQmlEngine engine;
	/* Qt's dialogs in Fusion's look, as the window has them (main.cpp) */
	QQmlFileSelector selector(&engine);
	selector.setExtraSelectors({QStringLiteral("Fusion")});
	/* a window opened with the theme chosen: its controls and its menu */
	auto first = make(engine, kWindow);
	CHECK(first != nullptr);
	CHECK(colour(first, "windowColour") == window);
	CHECK(colour(first, "buttonColour") == button);
	CHECK(colour(first, "fieldColour") == field);
	CHECK(colour(first, "menuColour") == window);
	/* the menu's divider in the theme's colour, not Fusion's near black */
	CHECK(colour(first, "dividerColour") == QColor(0x5A, 0x5A, 0x5A));
	/* Qt's own dialogs draw their folder bar and side places in "light":
	 * a field's colour, not the platform's white (issue 389) */
	CHECK(colour(first, "lightColour") == field);
	/* the file dialog, open (on Linux Chimera's own chooser, as with no
	 * desktop chooser to ask): the places beside its list in the theme's
	 * text, not Fusion's black (issue 389) */
	chimera::qt::PortalFileChooser portalChooser;
	chimera::qt::FilePlaces filePlaces;
	engine.rootContext()->setContextProperty(QStringLiteral("portalChooser"), &portalChooser);
	engine.rootContext()->setContextProperty(QStringLiteral("filePlaces"), &filePlaces);
	{
		auto window = make(engine, kFileDialog);
		QObject *dialog = window ? window->property("dialog").value<QObject *>() : nullptr;
		CHECK(dialog != nullptr);
		if (auto *w = qobject_cast<QQuickWindow *>(window.get())) w->requestActivate();
		if (dialog) QMetaObject::invokeMethod(dialog, "open");
		for (int i = 0; i < 20; i++) QCoreApplication::processEvents();
		QList<QColor> places;
		std::function<void(QQuickItem *, bool)> walk = [&](QQuickItem *item, bool side) {
			for (QQuickItem *c : item->childItems())
			{
				const bool in = side || c->inherits("QQuickSideBar");
				/* the place shown is drawn as picked, in the highlight's text */
				if (c->objectName() == QStringLiteral("chooserPlace"))
				{
					if (!c->property("highlighted").toBool())
						if (auto *label = c->property("contentItem").value<QQuickItem *>()) places.append(label->property("color").value<QColor>());
				}
				else if (in && c->property("folderName").isValid())
					if (auto *label = c->property("contentItem").value<QQuickItem *>()) places.append(label->property("color").value<QColor>());
				walk(c, in);
			}
		};
		if (auto *w = qobject_cast<QQuickWindow *>(window.get())) walk(w->contentItem(), false);
		CHECK(!places.isEmpty());
		CHECK(std::all_of(places.begin(), places.end(), [](const QColor &c) { return c == QColor(0xE0, 0xE0, 0xE0); }));
		if (dialog) QMetaObject::invokeMethod(dialog, "close");
	}
	/* a window opened later, the same */
	auto second = make(engine, kWindow);
	CHECK(colour(second, "buttonColour") == button);
	/* a Lua form keeps what it set, and the rest of its colours are not the theme's */
	auto form = make(engine, kUnthemed);
	CHECK(colour(form, "buttonColour") == QColor(0x12, 0x34, 0x56));
	CHECK(colour(form, "windowColour") != window);
	/* another theme chosen while they are open */
	chimera::qt::applyTheme(QStringLiteral(R"({"WindowBackground": "#F0F0F0", "ButtonBackground": "#DDEEFF", "InputBackground": "#FFFFFF"})"));
	for (int i = 0; i < 5; i++) QCoreApplication::processEvents();
	CHECK(colour(first, "buttonColour") == QColor(0xDD, 0xEE, 0xFF));
	CHECK(colour(second, "fieldColour") == QColor(Qt::white));
	CHECK(colour(first, "menuColour") == QColor(0xF0, 0xF0, 0xF0));
	/* a theme without a divider colour still draws one that can be seen */
	CHECK(colour(first, "dividerColour") != QColor(0xF0, 0xF0, 0xF0));
	CHECK(colour(first, "dividerColour").alpha() == 255);
	CHECK(colour(form, "buttonColour") == QColor(0x12, 0x34, 0x56));
	CHECK(colour(first, "lightColour") == QColor(Qt::white));
	/* Qt's dialogs as Chimera's QML names them (ChimeraDialogs, built in):
	 * drawn inside their window, Qt's enums still read through them (#389) */
	{
		auto dialogs = make(engine, kDialogs);
		CHECK(dialogs != nullptr);
		CHECK(dialogs && dialogs->property("inside").toString() == QStringLiteral("0 0 0 0 0"));
		CHECK(dialogs && dialogs->property("enums").toString() == QStringLiteral("2 1024"));
	}

	std::printf("%d checks, %d failed\n", checks, failed);
	return failed == 0 ? 0 : 1;
}
