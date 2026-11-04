#include "qml_view_model.hpp"

#include <QUrl>

#include <cstdio>
#include <limits>

namespace chimera::qt {

static int g_argumentsRefused = 0;

QVariant toVariant(const vm::Value &v)
{
	if (const bool *b = std::get_if<bool>(&v)) return *b;
	if (const int64_t *i = std::get_if<int64_t>(&v)) return QVariant::fromValue<qint64>(*i);
	if (const double *d = std::get_if<double>(&v)) return *d;
	if (const std::string *s = std::get_if<std::string>(&v)) return QString::fromStdString(*s);
	return {};
}

vm::Value fromVariant(const QVariant &v)
{
	switch (v.typeId())
	{
	case QMetaType::Bool: return v.toBool();
	case QMetaType::Int:
	case QMetaType::UInt:
	case QMetaType::Long:
	case QMetaType::LongLong:
	case QMetaType::ULong:
	case QMetaType::ULongLong:
	case QMetaType::Short:
	case QMetaType::UShort:
		return static_cast<int64_t>(v.toLongLong());
	case QMetaType::Float:
	case QMetaType::Double: return v.toDouble();
	case QMetaType::QString: return v.toString().toStdString();
	/* a file a dialog chose arrives as a URL; a view model takes paths */
	case QMetaType::QUrl: return v.toUrl().toLocalFile().toStdString();
	default: return {};
	}
}

static int clampRows(int64_t n)
{
	return static_cast<int>(std::min<int64_t>(n, std::numeric_limits<int>::max()));
}

QmlRows::QmlRows(vm::RowModel &rows, QObject *parent) : QAbstractTableModel(parent), rows_(rows)
{
	shown_ = clampRows(rows_.rowCount());
	watches_.push_back(rows_.reset.connect([this] {
		beginResetModel();
		shown_ = clampRows(rows_.rowCount());
		endResetModel();
		Q_EMIT rowsReset();
	}));
	watches_.push_back(rows_.changed.connect([this](int64_t first, int64_t last) {
		/* every role: a list's delegate reads the columns by name */
		if (first < shown_)
			Q_EMIT dataChanged(index(static_cast<int>(first), 0),
				index(clampRows(std::min<int64_t>(last, shown_ - 1)), std::max(0, columnCount() - 1)), {});
		Q_EMIT rowsEdited(first, last);
	}));
	watches_.push_back(rows_.appended.connect([this](int64_t, int64_t) {
		const int now = clampRows(rows_.rowCount());
		if (now > shown_)
		{
			beginInsertRows({}, shown_, now - 1);
			shown_ = now;
			endInsertRows();
		}
		Q_EMIT rowsGrew();
	}));
}

int QmlRows::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : shown_; }

int QmlRows::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : rows_.columnCount(); }

QVariant QmlRows::data(const QModelIndex &index, int role) const
{
	if (!index.isValid()) return {};
	if (role == Qt::DisplayRole) return toVariant(rows_.cell(index.row(), index.column()));
	if (role >= Qt::UserRole && role < Qt::UserRole + rows_.columnCount())
		return toVariant(rows_.cell(index.row(), role - Qt::UserRole));
	return {};
}

QHash<int, QByteArray> QmlRows::roleNames() const
{
	QHash<int, QByteArray> roles = QAbstractTableModel::roleNames();
	/* a column by its name, unless the name is one of Qt's own (a column
	 * called "display" would take the cell text from every delegate) */
	const QList<QByteArray> qts = roles.values();
	for (int c = 0; c < rows_.columnCount(); c++)
	{
		const QByteArray name = QByteArray::fromStdString(rows_.columnName(c));
		if (!qts.contains(name)) roles.insert(Qt::UserRole + c, name);
	}
	return roles;
}

QVariant QmlRows::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (role != Qt::DisplayRole) return {};
	if (orientation == Qt::Horizontal) return QString::fromStdString(rows_.columnName(section));
	return section;
}

QmlPicture::QmlPicture(vm::Picture &picture, vm::Signal<> &changedSignal, QObject *parent)
	: QObject(parent), picture_(picture)
{
	watch_ = changedSignal.connect([this] { Q_EMIT changed(); });
}

QmlViewModel::QmlViewModel(vm::ViewModel &model, QObject *parent)
	: QQmlPropertyMap(this, parent), model_(model)
{
	for (const auto &p : model_.properties())
	{
		const QString key = QString::fromStdString(p.name);
		insert(key, toVariant(p.get()));
		auto get = p.get;
		watches_.push_back(p.changed->connect([this, key, get] { insert(key, toVariant(get())); }));
	}
	/* a command that cannot always run says when it can as "<name>Enabled" */
	for (const auto &c : model_.commands())
	{
		if (c.enabled == nullptr) continue;
		const QString key = QString::fromStdString(c.name) + "Enabled";
		vm::Property<bool> *enabled = c.enabled;
		insert(key, enabled->get());
		watches_.push_back(enabled->changed.connect([this, key, enabled] { insert(key, enabled->get()); }));
	}
	insert("error", QString());
	for (const auto &r : model_.rowLists())
		rows_.emplace_back(QString::fromStdString(r.name), new QmlRows(*r.model, this));
	for (const auto &pic : model_.pictures())
		pictures_.emplace_back(QString::fromStdString(pic.name), new QmlPicture(*pic.picture, *pic.changed, this));
}

QVariant QmlViewModel::run(const QString &command, const QVariantList &args)
{
	vm::Args a;
	a.reserve(static_cast<size_t>(args.size()));
	for (const QVariant &v : args) a.push_back(fromVariant(v));
	std::string error;
	const int before = vm::ViewModel::argumentsRefused;
	const auto result = model_.run(command.toStdString(), a, error);
	if (vm::ViewModel::argumentsRefused != before)
	{
		g_argumentsRefused++;
		std::fprintf(stderr, "the window called %s.%s with the wrong arguments: %s\n", model_.name().c_str(),
			command.toUtf8().constData(), error.c_str());
	}
	insert("error", QString::fromStdString(error));
	return result ? toVariant(*result) : QVariant();
}

int QmlViewModel::argumentsRefused() { return g_argumentsRefused; }

QObject *QmlViewModel::rows(const QString &name) const
{
	for (const auto &[n, r] : rows_)
		if (n == name) return r;
	return nullptr;
}

QObject *QmlViewModel::picture(const QString &name) const
{
	for (const auto &[n, p] : pictures_)
		if (n == name) return p;
	return nullptr;
}

QVariant QmlViewModel::updateValue(const QString &key, const QVariant &input)
{
	std::string error;
	if (!model_.set(key.toStdString(), fromVariant(input), error))
	{
		insert("error", QString::fromStdString(error));
		/* refused: the property keeps what the view model holds */
		const auto now = model_.get(key.toStdString());
		return now ? toVariant(*now) : value(key);
	}
	const auto now = model_.get(key.toStdString());
	return now ? toVariant(*now) : input;
}

} // namespace chimera::qt
