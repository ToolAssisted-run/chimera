#pragma once
/* The one adapter between any view model and QML. It knows no view model by
 * name: it walks the registry and offers each property as a QML property,
 * each command through run(), each list of rows as a table model and each
 * picture as an object a PictureItem draws. A new view model needs no code
 * here. */

#include <QAbstractTableModel>
#include <QQmlPropertyMap>
#include <QVariant>

#include <vector>

#include "chimera/vm/view_model.hpp"

namespace chimera::qt {

QVariant toVariant(const vm::Value &v);
vm::Value fromVariant(const QVariant &v);

/* A list of rows as a Qt table: data(row, column) asks the view model for
 * that one cell, so only what is on screen is ever read. */
class QmlRows : public QAbstractTableModel
{
	Q_OBJECT
public:
	QmlRows(vm::RowModel &rows, QObject *parent);
	int rowCount(const QModelIndex &parent = {}) const override;
	int columnCount(const QModelIndex &parent = {}) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
	/* every column is also a role by its name, for a list (rather than a
	 * table) whose delegate reads a row's fields: model.label, model.value */
	QHash<int, QByteArray> roleNames() const override;

	vm::RowModel &rows() { return rows_; }

Q_SIGNALS:
	/* rows first..last changed in place, for a view that draws its own cells */
	void rowsEdited(qint64 first, qint64 last);
	void rowsGrew();
	void rowsReset();

private:
	vm::RowModel &rows_;
	/* a Qt model counts in int: past that the view sees the first 2^31 - 1 */
	int shown_ = 0;
	std::vector<vm::Connection> watches_;
};

/* A picture, for PictureItem */
class QmlPicture : public QObject
{
	Q_OBJECT
public:
	QmlPicture(vm::Picture &picture, vm::Signal<> &changed, QObject *parent);
	vm::Picture &picture() { return picture_; }

Q_SIGNALS:
	void changed();

private:
	vm::Picture &picture_;
	vm::Connection watch_;
};

class QmlViewModel : public QQmlPropertyMap
{
	Q_OBJECT
public:
	QmlViewModel(vm::ViewModel &model, QObject *parent = nullptr);

	vm::ViewModel &model() { return model_; }

	/* runs a command by name; a failure goes to the `error` property */
	Q_INVOKABLE QVariant run(const QString &command, const QVariantList &args = {});
	/* the window's calls refused for what they were given (an argument
	 * missing or of the wrong kind): each is a mistake in the window, said on
	 * standard error, and CHIMERA_QT_STRICT_QML fails the run on any (issue 483) */
	static int argumentsRefused();
	Q_INVOKABLE QObject *rows(const QString &name) const;
	Q_INVOKABLE QObject *picture(const QString &name) const;

protected:
	QVariant updateValue(const QString &key, const QVariant &input) override;

private:
	vm::ViewModel &model_;
	std::vector<vm::Connection> watches_;
	std::vector<std::pair<QString, QmlRows *>> rows_;
	std::vector<std::pair<QString, QmlPicture *>> pictures_;
};

} // namespace chimera::qt
