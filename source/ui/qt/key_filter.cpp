#include "key_filter.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQuickItem>

#include "key_names.hpp"

namespace chimera::qt {

void KeyFilter::button(const QString &name, bool down)
{
	if (down)
	{
		if (down_.count(name) != 0) return;
		bool taken = false;
		const auto route = routeOf(name, name, taken);
		down_[name] = {name, route.hotkey, route.control};
		if (route.control) session_.key(name.toStdString(), true);
		if (route.hotkey) hotkeys_.key(name.toStdString(), true);
	}
	else if (const auto it = down_.find(name); it != down_.end())
	{
		if (it->second.control) session_.key(name.toStdString(), false);
		if (it->second.hotkey) hotkeys_.key(name.toStdString(), false);
		down_.erase(it);
	}
}

vm::HotkeysViewModel::Route KeyFilter::routeOf(const QString &name, const QString &combo, bool &taken) const
{
	const bool hotkey = hotkeys_.boundTo(combo.toStdString()), control = session_.keyIsBound(name.toStdString());
	/* Key Priority says which of the two it goes to */
	const auto route = vm::HotkeysViewModel::route(hotkeys_.keyPriority.get(), hotkey, control);
	taken = (route.hotkey && hotkey) || (route.control && control);
	return route;
}

void KeyFilter::forgetAltForMenus()
{
	QEvent forget(QEvent::ActivationChange);
	QCoreApplication::sendEvent(&forgetter_, &forget);
}

bool KeyFilter::typing(const QKeyEvent &)
{
	auto *focus = qobject_cast<QQuickItem *>(QGuiApplication::focusObject());
	return focus != nullptr && focus->flags().testFlag(QQuickItem::ItemAcceptsInputMethod);
}

bool KeyFilter::eventFilter(QObject *watched, QEvent *e)
{
	if (e->type() == QEvent::ShortcutOverride)
	{
		/* a combination a hotkey or a control takes is not a shortcut's or a
		 * menu letter's: overridden, it comes as the key it is */
		auto *k = static_cast<QKeyEvent *>(e);
		const QString name = qt::keyName(*k);
		if (typing(*k) || name.isEmpty()) return QObject::eventFilter(watched, e);
		const bool taken = vm::HotkeysViewModel::takesCombination(hotkeys_.keyPriority.get(),
			hotkeys_.boundTo(qt::keyCombination(name, k->modifiers()).toStdString()), session_.keyIsBound(name.toStdString()),
			(k->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) != 0);
		if (taken)
		{
			e->accept();
			return true;
		}
	}
	else if (e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease)
	{
		auto *k = static_cast<QKeyEvent *>(e);
		const bool down = e->type() == QEvent::KeyPress;
		if (down && typing(*k)) return QObject::eventFilter(watched, e);
		/* the arrows, Page Up and Down, Home and End move TAStudio's picked
		 * frame while its roll has the keyboard, when nothing is bound to them
		 * (owner, 2026-11-07, issue 542); bound, they are the game's and the
		 * hotkeys' as before. Held, they repeat */
		if (down && navigates(*k)) return true;
		/* Alt held comes again and again, and with each Qt's menu bar waits for
		 * it to be let go again (issue 573): bound Alt is forgotten each time,
		 * or a game button held would hand the menus the keyboard */
		if (down && k->isAutoRepeat() && k->key() == Qt::Key_Alt)
		{
			const QString name = qt::keyName(*k);
			bool taken = false;
			if (!name.isEmpty()) routeOf(name, qt::keyCombination(name, k->modifiers()), taken);
			if (taken) forgetAltForMenus();
		}
		if (!k->isAutoRepeat())
		{
			const QString name = qt::keyName(*k);
			if (!name.isEmpty())
			{
				if (down)
				{
					/* the combination it was pressed with is the one let go */
					const QString combo = qt::keyCombination(name, k->modifiers());
					bool taken = false;
					const auto route = routeOf(name, combo, taken);
					down_[name] = {combo, route.hotkey, route.control};
					/* what is held, whatever it is bound to, for Lua's input.get */
					input_.key(name.toStdString(), true);
					if (route.control) session_.key(name.toStdString(), true);
					if (route.hotkey) hotkeys_.key(combo.toStdString(), true);
					/* Windows takes the keyboard to the menus when Alt is let go,
					 * unless another key was pressed while it was held; Qt's menu
					 * bar also does after another key, and for Alt that is bound */
					if (k->key() == Qt::Key_Alt ? taken : k->modifiers().testFlag(Qt::AltModifier)) forgetAltForMenus();
				}
				else if (const auto it = down_.find(name); it != down_.end())
				{
					if (it->second.control) session_.key(name.toStdString(), false);
					input_.key(name.toStdString(), false);
					if (it->second.hotkey) hotkeys_.key(it->second.combo.toStdString(), false);
					down_.erase(it);
				}
			}
		}
	}
	else if (e->type() == QEvent::FocusOut || e->type() == QEvent::WindowDeactivate)
	{
		for (const auto &[name, held] : down_)
		{
			if (held.control) session_.key(name.toStdString(), false);
			if (held.hotkey) hotkeys_.key(held.combo.toStdString(), false);
		}
		down_.clear();
		input_.releaseKeys();
	}
	return QObject::eventFilter(watched, e);
}

bool KeyFilter::navigates(const QKeyEvent &k)
{
	switch (k.key())
	{
	case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_PageUp: case Qt::Key_PageDown: case Qt::Key_Home: case Qt::Key_End: break;
	default: return false;
	}
	QObject *focus = QGuiApplication::focusObject();
	if (focus == nullptr || focus->metaObject()->indexOfMethod("navigate(int,bool)") < 0) return false;
	const QString name = qt::keyName(k);
	if (name.isEmpty() || session_.keyIsBound(name.toStdString())
		|| hotkeys_.boundTo(qt::keyCombination(name, k.modifiers()).toStdString()))
		return false;
	bool moved = false;
	QMetaObject::invokeMethod(focus, "navigate", Q_RETURN_ARG(bool, moved), Q_ARG(int, k.key()), Q_ARG(bool, k.modifiers().testFlag(Qt::ShiftModifier)));
	return moved;
}

bool HeldKeyClicksOnce::eventFilter(QObject *watched, QEvent *e)
{
	if ((e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease) && static_cast<QKeyEvent *>(e)->isAutoRepeat()
		&& watched->inherits("QQuickAbstractButton"))
		return true;
	return QObject::eventFilter(watched, e);
}

} // namespace chimera::qt
