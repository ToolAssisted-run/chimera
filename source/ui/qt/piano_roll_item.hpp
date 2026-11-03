#pragma once

#include <functional>

#include <QHash>
#include <QImage>
#include <QVariantList>
#include <vector>
/* The piano roll's grid: it draws only the rows in view, asking the rows
 * model for those cells alone, so a million frames scroll as fast as forty.
 * It edits nothing itself: a press or a drag is a signal, and the window
 * turns it into a command of the piano roll's view model. */

#include <QPointer>
#include <QQuickPaintedItem>

namespace chimera::qt {

class QmlRows;

class PianoRollItem : public QQuickPaintedItem
{
	Q_OBJECT
	Q_PROPERTY(QObject *rows READ rows WRITE setRows NOTIFY rowsChanged)
	Q_PROPERTY(qint64 topRow READ topRow WRITE setTopRow NOTIFY topRowChanged)
	Q_PROPERTY(qint64 currentRow READ currentRow WRITE setCurrentRow NOTIFY currentRowChanged)
	/* the frames picked, drawn on the frame numbers */
	Q_PROPERTY(qint64 selectFirst READ selectFirst WRITE setSelectFirst NOTIFY selectionChanged)
	Q_PROPERTY(qint64 selectLast READ selectLast WRITE setSelectLast NOTIFY selectionChanged)
	/* the analog cell picked for editing, outlined (-1: none) */
	Q_PROPERTY(qint64 pickedRow MEMBER pickedRow_ NOTIFY pickedChanged)
	Q_PROPERTY(int pickedColumn MEMBER pickedColumn_ NOTIFY pickedChanged)
	/* which of TAStudio's input rolls this is: its own columns */
	Q_PROPERTY(int view MEMBER view_ NOTIFY viewChanged)
	/* how wide its columns are, all told (how tall turned on its side): an
	 * input roll beside another takes this much, as Chimera does */
	Q_PROPERTY(int contentExtent READ contentExtent NOTIFY contentExtentChanged)
	/* the rows a notch of the wheel scrolls */
	Q_PROPERTY(int wheelRows MEMBER wheelRows_)
	Q_PROPERTY(qint64 rowCount READ rowCount NOTIFY rowCountChanged)
	Q_PROPERTY(int visibleRows READ visibleRows NOTIFY visibleRowsChanged)
	Q_PROPERTY(int rowHeight READ rowHeight NOTIFY visibleRowsChanged)
	/* TAStudio's appearance: states and markers by colour and by icon, the
	 * roll's own colours over the theme's (JSON: a theme role each), and
	 * its font ({"family", "pointSize", "bold", "italic"}; "" the window's) */
	Q_PROPERTY(bool denoteStatesColor MEMBER denoteStatesColor_ NOTIFY lookChanged)
	Q_PROPERTY(bool denoteStatesIcons MEMBER denoteStatesIcons_ NOTIFY lookChanged)
	Q_PROPERTY(bool denoteMarkersColor MEMBER denoteMarkersColor_ NOTIFY lookChanged)
	Q_PROPERTY(bool denoteMarkersIcons MEMBER denoteMarkersIcons_ NOTIFY lookChanged)
	Q_PROPERTY(QString colors READ colors WRITE setColors NOTIFY lookChanged)
	Q_PROPERTY(QString rollFont READ rollFont WRITE setRollFont NOTIFY lookChanged)
	/* TAStudio's Rotate: the frames run across, the buttons down */
	Q_PROPERTY(bool horizontal READ horizontal WRITE setHorizontal NOTIFY lookChanged)
	/* a script's own columns after the machine's, JSON [{"name", "text", "width"}] */
	Q_PROPERTY(QString extraColumns READ extraColumns WRITE setExtraColumns NOTIFY lookChanged)
	/* whether to ask the script for a cell's colour and text while drawing */
	Q_PROPERTY(bool queries MEMBER queries_ NOTIFY lookChanged)
	/* moved: the icons a script names are read again */
	Q_PROPERTY(qint64 iconEpoch READ iconEpoch WRITE setIconEpoch NOTIFY lookChanged)
	/* the frames branches were made on: Chimera numbers them in the column
	 * before the frame numbers */
	Q_PROPERTY(QVariantList branchFrames READ branchFrames WRITE setBranchFrames NOTIFY lookChanged)
	/* the frame the machine goes back to after edits (Chimera's green arrow),
	 * and the one a seek is playing to (its row is the current one's colour); -1 for none */
	Q_PROPERTY(qint64 restoreRow MEMBER restoreRow_ NOTIFY lookChanged)
	Q_PROPERTY(qint64 seekRow MEMBER seekRow_ NOTIFY lookChanged)
	/* how long the last paint took, in milliseconds, for measuring */
	Q_PROPERTY(double lastPaintMs READ lastPaintMs NOTIFY painted)

public:
	explicit PianoRollItem(QQuickItem *parent = nullptr);

	QObject *rows() const;
	void setRows(QObject *rows);
	qint64 topRow() const { return top_; }
	void setTopRow(qint64 row);
	qint64 currentRow() const { return current_; }
	void setCurrentRow(qint64 row);
	qint64 selectFirst() const { return selectFirst_; }
	qint64 selectLast() const { return selectLast_; }
	void setSelectFirst(qint64 row);
	void setSelectLast(qint64 row);
	qint64 rowCount() const;
	int visibleRows() const;
	int rowHeight() const { return rowHeight_; }
	double lastPaintMs() const { return lastPaintMs_; }

	void paint(QPainter *painter) override;

	int contentExtent() const;
	QString colors() const { return colors_; }
	void setColors(const QString &json);
	/* Chimera's twelve TAStudio colours as the theme has them, before the
	 * roll's own (TAStudio's Set Colors shows them, and its Defaults goes
	 * back to them): {"TasCurrentFrame": "#rrggbb", ...} */
	Q_INVOKABLE QString themeColors() const;
	QString rollFont() const { return rollFont_; }
	void setRollFont(const QString &font);
	bool horizontal() const { return horizontal_; }
	QString extraColumns() const { return extraJson_; }
	void setExtraColumns(const QString &json);
	/* what the scripts say of the cells while a roll is drawn: kind 0 a
	 * cell's colour, 1 its text, 2 its icon; "" nothing. The cells of one
	 * drawing are asked at once (issue 476). One for every roll, made now or
	 * later: TAStudio's window makes its rolls itself, after the program has
	 * started */
	struct Asked
	{
		int kind = 0;
		qint64 row = 0;
		QString column, answer;
	};
	using Query = std::function<void(std::vector<Asked> &asked)>;
	static void setQuery(Query query);
	qint64 iconEpoch() const { return iconEpoch_; }
	void setIconEpoch(qint64 epoch)
	{
		if (epoch == iconEpoch_) return;
		iconEpoch_ = epoch;
		icons_.clear();
		update();
	}
	void setHorizontal(bool on);
	QVariantList branchFrames() const { return branchFramesList_; }
	void setBranchFrames(const QVariantList &frames);

Q_SIGNALS:
	void lookChanged();
	void viewChanged();
	void contentExtentChanged();
	void rowsChanged();
	void topRowChanged();
	void currentRowChanged();
	void rowCountChanged();
	void visibleRowsChanged();
	void painted();
	/* alt: Alt was held (TAStudio paints a column's pattern then) */
	void cellPressed(qint64 row, int column, bool alt);
	/* the right button on a column's name: its menu */
	void columnMenuRequested(int column, const QString &name, bool autoHeld, qreal x, qreal y);
	/* the right button on the rows: TAStudio's menu for the frames picked */
	void rowMenuRequested(qint64 row);
	void cellDragged(qint64 row, int column);
	/* a press on a row's frame number */
	void frameClicked(qint64 row);
	/* a double click with the left button on a row's frame number (issue 436) */
	void frameDoubleClicked(qint64 row);
	/* Chimera's right-button drags on the frame numbers (Shift, Ctrl, Alt held): begun on a row,
	 * gone on to another, let go */
	void rightDragStarted(qint64 row, bool ctrl, bool shift, bool alt);
	void rightDraggedTo(qint64 row);
	void rightDragEnded();
	/* a marker dragged by its frame number with Alt held, dropped on another frame */
	void markerDropped(qint64 from, qint64 to);
	/* the pointer on a column's title (its name), or off every title (""), for the window's tooltip */
	void titleHovered(const QString &name, qreal x, qreal y);
	/* the wheel turned with the right button held on a row: Chimera's seek by a frame a notch;
	 * first: the first notch since the button went down */
	void wheelSeekRequested(int notches, bool first);
	/* the frame numbers pressed or dragged over, or Shift-pressed */
	void selectionRequested(qint64 first, qint64 last);
	void selectionChanged();
	void pickedChanged();
	/* the wheel asks for another first row; whoever owns topRow decides */
	void topRowRequested(qint64 row);

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void hoverMoveEvent(QHoverEvent *e) override;
	void hoverLeaveEvent(QHoverEvent *e) override;
	void geometryChange(const QRectF &now, const QRectF &before) override;

private:
	bool cellAt(QPointF p, qint64 &row, int &column) const;
	int frameColumnWidth() const;
	/* the narrow column before the frame numbers, as wide as Chimera's (a "10" and its padding) */
	int cursorWidth() const;
	/* the row shown on a line of the roll (0 the first under the header),
	 * hidden rows passed over; and the last one shown */
	qint64 rowOnLine(qint64 line) const;
	qint64 lastShown() const { return rowOnLine(visibleRows()); }
	void rowsTouched(qint64 first, qint64 last);

	QPointer<QmlRows> rows_;
	std::vector<QMetaObject::Connection> watches_;
	qint64 top_ = 0, current_ = 0;
	qint64 selectFirst_ = -1, selectLast_ = -1, selectAnchor_ = -1;
	bool selecting_ = false;
	/* the right button held on a row: its menu opens when it is let go, unless the wheel turned meanwhile */
	bool rightHeld_ = false;
	bool rightWheeled_ = false;
	/* a wheel seek sent since the right button went down */
	bool rightSeeked_ = false;
	qint64 rightRow_ = -1;
	/* a right-button drag on the frame numbers: where it is, and whether keys made it an edit */
	bool rightDragging_ = false, rightEditing_ = false, rightMoved_ = false;
	qint64 rightDragRow_ = -1;
	/* a left-button drag in the cursor column (the machine follows it), and a marker dragged with Alt */
	bool cursorDragging_ = false;
	qint64 cursorDragRow_ = -1, markerDrag_ = -1;
	qint64 pickedRow_ = -1;
	int pickedColumn_ = -1;
	int view_ = 0;
	int wheelRows_ = 6;
	bool denoteStatesColor_ = true, denoteStatesIcons_ = false, denoteMarkersColor_ = true, denoteMarkersIcons_ = false;
	QString colors_, rollFont_;
	QFont font_;
	/* what the roll is drawn in: the theme's, then the roll's own over them */
	struct Look
	{
		QColor plain, text, header, headerText, grid, lit, now, green, lag, marker, picked, pickedText, permanent, greenStated,
			lagStated, greenGone, lagGone, frameWash, altPlayer, greenFrame, lagFrame, analogEdit, columnBorder, hint, emphasis,
			cursorColumn, defaultRow, iconPlayback, iconRecording, iconMarker, iconAnchor, iconLagAnchor;
		bool dark = false;
	};
	Look themeLook() const;
	int rowHeight_ = 14, columnWidth_ = 17;
	/* each column's width, worked out once for a font and a set of columns */
	mutable std::vector<int> widths_;
	mutable bool widthsDirty_ = true;
	/* where the pointer is: a row and a column (-2 the cursor's, -1 the frame numbers'), and over the titles */
	qint64 hoverRow_ = -1;
	int hoverColumn_ = -3;
	bool hoverTitles_ = false;
	QVariantList branchFramesList_;
	std::vector<qint64> branchFrames_;
	qint64 restoreRow_ = -1, seekRow_ = -1;
	bool horizontal_ = false;
	struct Extra
	{
		QString name, text;
		int width = 30;
	};
	std::vector<Extra> extras_;
	QString extraJson_ = QStringLiteral("[]");
	bool queries_ = false;
	static Query &query();
	qint64 iconEpoch_ = 0;
	/* the icons a script named, read once each */
	QHash<QString, QImage> icons_;
	/* the roll is laid out as if upright; turned, x and y change places */
	QPointF logical(QPointF physical) const { return horizontal_ ? QPointF(physical.y(), physical.x()) : physical; }
	int logicalWidth() const { return static_cast<int>(horizontal_ ? height() : width()); }
	int logicalHeight() const { return static_cast<int>(horizontal_ ? width() : height()); }
	/* the header's depth: a row upright; turned, room for the longest name */
	int headerSize() const;
	/* where a column starts and how wide it is: an analog control's column
	 * shows a number, and is wider */
	int columnX(int column) const;
	int columnW(int column) const;
	double lastPaintMs_ = 0;
	int dragColumn_ = -1;
	qint64 dragRow_ = -1;
};

} // namespace chimera::qt
