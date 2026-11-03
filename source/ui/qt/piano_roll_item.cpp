#include "piano_roll_item.hpp"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPalette>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <tuple>

#include "chimera/vm/piano_roll_vm.hpp"
#include "qml_view_model.hpp"
#include "theme_palette.hpp"

namespace chimera::qt {

PianoRollItem::Query &PianoRollItem::query()
{
	static Query asked;
	return asked;
}

void PianoRollItem::setQuery(Query query)
{
	PianoRollItem::query() = std::move(query);
}

PianoRollItem::PianoRollItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
	setOpaquePainting(true);
	setAntialiasing(false);
	setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
	/* Chimera's roll: the title under the pointer lit, an empty cell under it naming its column */
	setAcceptHoverEvents(true);
	connect(this, &PianoRollItem::lookChanged, this, [this] { widthsDirty_ = true; });
	connect(this, &PianoRollItem::viewChanged, this, [this] { widthsDirty_ = true; });
	connect(this, &PianoRollItem::pickedChanged, this, [this] { update(); });
	connect(this, &PianoRollItem::lookChanged, this, [this] { update(); });
	connect(this, &PianoRollItem::viewChanged, this, [this] {
		update();
		Q_EMIT contentExtentChanged();
	});
	connect(this, &PianoRollItem::lookChanged, this, &PianoRollItem::contentExtentChanged);
}

void PianoRollItem::setColors(const QString &json)
{
	if (json == colors_) return;
	colors_ = json;
	Q_EMIT lookChanged();
}

void PianoRollItem::setRollFont(const QString &font)
{
	if (font == rollFont_) return;
	rollFont_ = font;
	/* {"family": "Arial", "pointSize": 8.25, "bold": true, "italic": false}; none, Chimera's own: Arial 8.25 bold */
	const QJsonObject f = QJsonDocument::fromJson(font.toUtf8()).object();
	font_ = QFont(QStringLiteral("Arial"));
	font_.setPointSizeF(8.25);
	font_.setBold(true);
	if (!f.isEmpty())
	{
		if (f.contains(QLatin1String("family"))) font_.setFamily(f.value(QLatin1String("family")).toString());
		if (f.value(QLatin1String("pointSize")).toDouble() > 0) font_.setPointSizeF(f.value(QLatin1String("pointSize")).toDouble());
		font_.setBold(f.value(QLatin1String("bold")).toBool());
		font_.setItalic(f.value(QLatin1String("italic")).toBool());
	}
	/* the rows as tall as the font, as Chimera's roll (no padding above or below) */
	rowHeight_ = std::max(10, QFontMetrics(font_).height());
	widthsDirty_ = true;
	Q_EMIT lookChanged();
	Q_EMIT visibleRowsChanged();
}

int PianoRollItem::columnW(int column) const
{
	if (rows_ == nullptr) return columnWidth_;
	const int columns = rows_->rows().columnCount();
	if (widthsDirty_ || static_cast<int>(widths_.size()) != columns)
	{
		/* as Chimera does: a one-letter title as wide as a W, a
		 * longer one as itself, an analog value as five digits, with three
		 * pixels each side; a hidden column takes no room */
		const QFontMetrics fm(font_);
		widths_.assign(static_cast<size_t>(columns), 0);
		for (int c = 0; c < columns; c++)
		{
			if (rows_->rows().columnHiddenIn(view_, c)) continue;
			const QString title = QString::fromStdString(rows_->rows().columnShortName(c));
			int w = title.size() > 1 ? fm.horizontalAdvance(title) : fm.horizontalAdvance(QLatin1Char('W'));
			if (rows_->rows().columnIsNumber(c)) w = std::max(w, fm.horizontalAdvance(QStringLiteral("88888")));
			widths_[static_cast<size_t>(c)] = w + 6;
		}
		widthsDirty_ = false;
	}
	return column >= 0 && column < columns ? widths_[static_cast<size_t>(column)] : columnWidth_;
}

qint64 PianoRollItem::rowOnLine(qint64 line) const
{
	if (rows_ == nullptr) return top_ + line;
	vm::RowModel &rows = rows_->rows();
	qint64 row = rows.nextShown(top_ - 1);
	for (qint64 i = 0; i < line; i++) row = rows.nextShown(row);
	return row;
}

int PianoRollItem::columnX(int column) const
{
	/* the machine's columns, then the script's */
	const int own = rows_ != nullptr ? rows_->rows().columnCount() : 0;
	int x = frameColumnWidth();
	for (int c = 0; c < column && c < own; c++) x += columnW(c);
	for (int e = 0; e < column - own && e < static_cast<int>(extras_.size()); e++) x += extras_[static_cast<size_t>(e)].width;
	return x;
}

int PianoRollItem::contentExtent() const
{
	const int own = rows_ != nullptr ? rows_->rows().columnCount() : 0;
	return columnX(own + static_cast<int>(extras_.size())) + 1;
}

void PianoRollItem::setExtraColumns(const QString &json)
{
	if (json == extraJson_) return;
	extraJson_ = json;
	extras_.clear();
	for (const QJsonValue v : QJsonDocument::fromJson(json.toUtf8()).array())
	{
		const QJsonObject o = v.toObject();
		extras_.push_back({o.value(QLatin1String("name")).toString(), o.value(QLatin1String("text")).toString(),
			std::max(8, o.value(QLatin1String("width")).toInt(30))});
	}
	Q_EMIT lookChanged();
	update();
}

QObject *PianoRollItem::rows() const { return rows_.data(); }

void PianoRollItem::setRows(QObject *rows)
{
	QmlRows *r = qobject_cast<QmlRows *>(rows);
	if (r == rows_) return;
	for (auto &c : watches_) disconnect(c);
	watches_.clear();
	rows_ = r;
	widthsDirty_ = true;
	if (r != nullptr)
	{
		watches_.push_back(connect(r, &QmlRows::rowsEdited, this, &PianoRollItem::rowsTouched));
		watches_.push_back(connect(r, &QmlRows::rowsGrew, this, [this] {
			Q_EMIT rowCountChanged();
			update();
		}));
		watches_.push_back(connect(r, &QmlRows::rowsReset, this, [this] {
			widthsDirty_ = true;
			Q_EMIT rowCountChanged();
			/* a column shown or hidden comes as a reset */
			Q_EMIT contentExtentChanged();
			update();
		}));
	}
	Q_EMIT rowsChanged();
	Q_EMIT contentExtentChanged();
	Q_EMIT rowCountChanged();
	update();
}

qint64 PianoRollItem::rowCount() const { return rows_ != nullptr ? rows_->rows().rowCount() : 0; }

int PianoRollItem::visibleRows() const
{
	return std::max(0, (logicalHeight() - headerSize()) / rowHeight_); /* less the header */
}

void PianoRollItem::setHorizontal(bool on)
{
	if (on == horizontal_) return;
	horizontal_ = on;
	Q_EMIT lookChanged();
	Q_EMIT visibleRowsChanged();
	update();
}

int PianoRollItem::headerSize() const
{
	if (!horizontal_) return rowHeight_;
	const QFontMetrics fm(font_);
	int widest = fm.horizontalAdvance(QStringLiteral("Frame#"));
	if (rows_ != nullptr)
		for (int c = 0; c < rows_->rows().columnCount(); c++)
			widest = std::max(widest, fm.horizontalAdvance(QString::fromStdString(rows_->rows().columnShortName(c))));
	return widest + 12;
}

void PianoRollItem::setTopRow(qint64 row)
{
	if (row == top_) return;
	top_ = row;
	Q_EMIT topRowChanged();
	update();
}

void PianoRollItem::setCurrentRow(qint64 row)
{
	if (row == current_) return;
	const qint64 was = current_;
	current_ = row;
	Q_EMIT currentRowChanged();
	const qint64 last = lastShown();
	if ((was >= top_ && was <= last) || (row >= top_ && row <= last)) update();
}

void PianoRollItem::setSelectFirst(qint64 row)
{
	if (row == selectFirst_) return;
	selectFirst_ = row;
	Q_EMIT selectionChanged();
	update();
}

void PianoRollItem::setSelectLast(qint64 row)
{
	if (row == selectLast_) return;
	selectLast_ = row;
	Q_EMIT selectionChanged();
	update();
}

void PianoRollItem::rowsTouched(qint64 first, qint64 last)
{
	if (last >= top_ && first <= lastShown()) update();
}

/* TAStudio's two columns before the buttons: a narrow one a press in moves
 * the machine to its frame, then the frame numbers, which pick frames; as
 * wide as Chimera's ("10" less four, and "0000000" and seven, each with three
 * pixels a side) */
int PianoRollItem::cursorWidth() const
{
	return QFontMetrics(font_).horizontalAdvance(QStringLiteral("10")) - 4 + 6;
}

int PianoRollItem::frameColumnWidth() const
{
	return cursorWidth() + QFontMetrics(font_).horizontalAdvance(QStringLiteral("0000000")) + 7 + 6;
}

void PianoRollItem::setBranchFrames(const QVariantList &frames)
{
	if (frames == branchFramesList_) return;
	branchFramesList_ = frames;
	branchFrames_.clear();
	for (const QVariant &v : frames) branchFrames_.push_back(v.toLongLong());
	Q_EMIT lookChanged();
}

void PianoRollItem::geometryChange(const QRectF &now, const QRectF &before)
{
	QQuickPaintedItem::geometryChange(now, before);
	if (now.height() != before.height() || now.width() != before.width()) Q_EMIT visibleRowsChanged();
}

bool PianoRollItem::cellAt(QPointF p, qint64 &row, int &column) const
{
	if (rows_ == nullptr) return false;
	const int fx = frameColumnWidth();
	const int hs = headerSize();
	if (p.x() < fx || p.y() < hs) return false;
	const int columns = rows_->rows().columnCount();
	column = -1;
	for (int c = 0, x = fx; c < columns; x += columnW(c), c++)
		if (p.x() >= x && p.x() < x + columnW(c)) column = c;
	row = rowOnLine(static_cast<qint64>((p.y() - hs) / rowHeight_));
	return column >= 0 && row < rows_->rows().rowCount();
}

PianoRollItem::Look PianoRollItem::themeLook() const
{
	/* TAStudio's meanings: the current frame blue, the greenzone green, a
	 * lag frame in it red, frames not run with their inputs plain. The
	 * colours follow the system's light or dark theme: each meaning is a
	 * tint mixed into the theme's own background. */
	const QPalette pal = QGuiApplication::palette();
	QColor base = pal.color(QPalette::Base), text = pal.color(QPalette::Text);
	const bool dark = base.lightnessF() < 0.5;
	auto tint = [&](QColor c, double amount) {
		return QColor::fromRgbF(base.redF() + (c.redF() - base.redF()) * amount,
			base.greenF() + (c.greenF() - base.greenF()) * amount,
			base.blueF() + (c.blueF() - base.blueF()) * amount);
	};
	const double a = dark ? 0.32 : 0.22;
	QColor plain = base, header = pal.color(QPalette::Button), headerText = text, grid = tint(text, 0.18),
		lit = pal.color(QPalette::Highlight), now = tint(QColor(0x3d, 0x8b, 0xff), a + 0.08),
		green = tint(QColor(0x3c, 0xc0, 0x3c), a), lag = tint(QColor(0xe0, 0x40, 0x30), a),
		marker = tint(QColor(0xe0, 0xa0, 0x20), dark ? 0.5 : 0.45), picked = lit, pickedText = pal.color(QPalette::HighlightedText);
	/* a Chimera theme says TAStudio's colours itself */
	auto themed = [](QColor &c, const char *role) {
		const QColor t = themeColor(role);
		if (t.isValid()) c = t;
	};
	themed(plain, "RollBackground");
	themed(text, "RollText");
	/* the column titles as Chimera draws them: on the roll's column
	 * colour, in the roll's text colour, the border around them its own */
	themed(header, "RollColumnBackground");
	themed(headerText, "RollText");
	QColor columnBorder = dark ? QColor(0x60, 0x60, 0x60) : QColor(Qt::black), hint = tint(text, 0.45), emphasis = tint(pal.color(QPalette::Highlight), 0.5);
	themed(columnBorder, "RollColumnBorder");
	themed(hint, "RollHintText");
	themed(emphasis, "RollEmphasisColumn");
	themed(grid, "RollGridLines");
	themed(now, "TasCurrentFrame");
	themed(green, "TasGreenZoneInput");
	themed(lag, "TasLagZoneInput");
	themed(marker, "TasMarker");
	/* the run's own markers (start, last input, end) are not the person's and
	 * do not look like them: Chimera's light and dark themes' colour */
	QColor permanent = dark ? QColor(0x1F, 0x33, 0x50) : QColor(0xC9, 0xE4, 0xFF);
	themed(permanent, "TasPermanentMarker");
	themed(picked, "RollSelection");
	themed(pickedText, "RollSelectionText");
	/* the greenzone's frames with a state held, and those an edit sent the
	 * greenzone back past (Chimera's "invalidated"); the frame column's
	 * wash; every second player's columns */
	QColor greenStated = tint(QColor(0x3c, 0xc0, 0x3c), a + 0.08), lagStated = tint(QColor(0xe0, 0x40, 0x30), a + 0.08),
		greenGone = tint(QColor(0x3c, 0xc0, 0x3c), a * 0.4), lagGone = tint(QColor(0xe0, 0x40, 0x30), a * 0.4),
		frameWash, altPlayer = QColor(text.red(), text.green(), text.blue(), 14);
	themed(greenStated, "TasGreenZoneInputStated");
	themed(lagStated, "TasLagZoneInputStated");
	themed(greenGone, "TasGreenZoneInputInvalidated");
	themed(lagGone, "TasLagZoneInputInvalidated");
	themed(frameWash, "TasFrameColumnWash");
	themed(altPlayer, "TasAlternatePlayer");
	/* the frame column's greenzone and lag colours are Chimera's to offer
	 * (its Set Colors has them) though its roll does not draw them either;
	 * the analog cell being edited is filled with its own */
	/* the cursor column, a row nothing coloured, and the roll's icons */
	QColor cursorColumn, defaultRow = plain, iconPlayback(0x53, 0xD9, 0xFF), iconRecording(0x00, 0xC2, 0x40), iconMarker(0xFC, 0xD1, 0x37),
		iconAnchor(0x00, 0xDE, 0x62), iconLagAnchor(0xFF, 0x60, 0x64);
	themed(cursorColumn, "TasCursorColumn");
	themed(defaultRow, "TasDefaultRow");
	themed(iconPlayback, "TasIconPlayback");
	themed(iconRecording, "TasIconRecording");
	themed(iconMarker, "TasIconMarker");
	themed(iconAnchor, "TasIconAnchor");
	themed(iconLagAnchor, "TasIconLagAnchor");
	QColor greenFrame = green, lagFrame = lag, analogEdit = picked;
	analogEdit.setAlpha(90);
	themed(greenFrame, "TasGreenZone");
	themed(lagFrame, "TasLagZone");
	themed(analogEdit, "TasAnalogEdit");
	return Look{plain, text, header, headerText, grid, lit, now, green, lag, marker, picked, pickedText, permanent, greenStated, lagStated,
		greenGone, lagGone, frameWash, altPlayer, greenFrame, lagFrame, analogEdit, columnBorder, hint, emphasis, cursorColumn, defaultRow,
		iconPlayback, iconRecording, iconMarker, iconAnchor, iconLagAnchor, dark};
}

QString PianoRollItem::themeColors() const
{
	const Look k = themeLook();
	const std::pair<const char *, QColor> roles[] = {{"TasCurrentFrame", k.now}, {"TasGreenZone", k.greenFrame},
		{"TasGreenZoneInput", k.green}, {"TasGreenZoneInputStated", k.greenStated}, {"TasGreenZoneInputInvalidated", k.greenGone},
		{"TasLagZone", k.lagFrame}, {"TasLagZoneInput", k.lag}, {"TasLagZoneInputStated", k.lagStated},
		{"TasLagZoneInputInvalidated", k.lagGone}, {"TasMarker", k.marker}, {"TasPermanentMarker", k.permanent},
		{"TasAnalogEdit", k.analogEdit},
		/* the roll's own, which every InputRoll of Chimera's shares (TAStudio's branch and marker lists) */
		{"RollBackground", k.plain}, {"RollText", k.text}, {"RollColumnBackground", k.header}, {"RollColumnBorder", k.columnBorder},
		{"RollGridLines", k.grid}, {"RollSelection", k.picked}, {"RollSelectionText", k.pickedText}, {"RollHintText", k.hint},
		{"RollEmphasisColumn", k.emphasis}};
	QJsonObject out;
	for (const auto &[role, color] : roles) out.insert(QLatin1String(role), color.name(color.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb));
	return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

void PianoRollItem::paint(QPainter *p)
{
	QElapsedTimer timer;
	timer.start();

	/* the theme's colours, then the roll's own over them (TAStudio's Set Colors) */
	const Look k = themeLook();
	QColor plain = k.plain, text = k.text, header = k.header, headerText = k.headerText, grid = k.grid, now = k.now,
		green = k.green, lag = k.lag, marker = k.marker, picked = k.picked, pickedText = k.pickedText, permanent = k.permanent,
		greenStated = k.greenStated, lagStated = k.lagStated, greenGone = k.greenGone, lagGone = k.lagGone, frameWash = k.frameWash,
		altPlayer = k.altPlayer, greenFrame = k.greenFrame, lagFrame = k.lagFrame, analogEdit = k.analogEdit, defaultRow = k.defaultRow;
	{
		const QJsonObject own = QJsonDocument::fromJson(colors_.toUtf8()).object();
		const std::pair<const char *, QColor *> roles[] = {{"TasCurrentFrame", &now}, {"TasGreenZoneInput", &green},
			{"TasGreenZoneInputStated", &greenStated}, {"TasGreenZoneInputInvalidated", &greenGone}, {"TasLagZoneInput", &lag},
			{"TasLagZoneInputStated", &lagStated}, {"TasLagZoneInputInvalidated", &lagGone}, {"TasMarker", &marker},
			{"TasPermanentMarker", &permanent}, {"TasGreenZone", &greenFrame}, {"TasLagZone", &lagFrame}, {"TasAnalogEdit", &analogEdit},
			{"TasDefaultRow", &defaultRow}, {"TasFrameColumnWash", &frameWash}, {"TasAlternatePlayer", &altPlayer}};
		for (const auto &[role, color] : roles)
			if (const QColor c(own.value(QLatin1String(role)).toString()); c.isValid()) *color = c;
	}
	p->setFont(font_);
	p->fillRect(0, 0, static_cast<int>(width()), static_cast<int>(height()), plain);
	if (rows_ == nullptr) return;
	/* drawn upright; turned on its side, x and y change places, and text is
	 * drawn the right way round: names level, numbers turned, as Chimera's */
	const int w = logicalWidth(), h = logicalHeight(), hs = headerSize();
	if (horizontal_) p->setTransform(QTransform(0, 1, 1, 0, 0, 0));
	auto label = [&](const QRect &r, int flags, const QString &s, bool turn) {
		if (!horizontal_)
		{
			p->drawText(r, flags, s);
			return;
		}
		p->save();
		p->resetTransform();
		const QRect at(r.y(), r.x(), r.height(), r.width());
		if (turn)
		{
			p->translate(at.left(), at.bottom() + 1);
			p->rotate(-90);
			p->drawText(QRect(0, 0, at.height(), at.width()), flags, s);
		}
		else
			p->drawText(at, Qt::AlignVCenter | Qt::AlignRight, s);
		p->restore();
	};

	vm::RowModel &rows = rows_->rows();
	const int fx = frameColumnWidth(), cx0 = cursorWidth();
	const int columns = rows.columnCount();
	const int extraCount = static_cast<int>(extras_.size());
	/* the columns' places, widths, players and kinds, worked out once a
	 * paint: asked for each cell, adding up the widths before a column cost
	 * more than all the drawing (issue 404) */
	std::vector<int> xs(static_cast<size_t>(columns + extraCount) + 1), ws(static_cast<size_t>(columns));
	std::vector<int32_t> players(static_cast<size_t>(columns));
	std::vector<bool> numbers(static_cast<size_t>(columns)), shownColumns(static_cast<size_t>(columns));
	xs[0] = fx;
	for (int c = 0; c < columns + extraCount; c++)
	{
		const size_t i = static_cast<size_t>(c);
		if (c < columns)
		{
			ws[i] = columnW(c);
			players[i] = rows.columnPlayer(c);
			numbers[i] = rows.columnIsNumber(c);
			shownColumns[i] = ws[i] > 0;
		}
		xs[i + 1] = xs[i] + (c < columns ? ws[i] : extras_[static_cast<size_t>(c - columns)].width);
	}
	auto colX = [&](int c) { return xs[static_cast<size_t>(c)]; };
	auto colW = [&](int c) { return ws[static_cast<size_t>(c)]; };
	const int right = colX(columns + extraCount);
	const qint64 count = rows.rowCount();
	const int shown = visibleRows() + 1;
	const QFontMetrics fm(font_);
	/* Chimera's icons are drawn to its text's height (13 pixels at its font) */
	const double scale = fm.height() / 13.0;
	const int left = static_cast<int>(Qt::AlignVCenter | Qt::AlignLeft);
	auto title = [&](int c) { return QString::fromStdString(rows.columnShortName(c)); };

	/* the column titles, as Chimera draws them: on the column
	 * colour, an Auto-hold column's in the emphasis colour, the one under the
	 * pointer lit; the frame numbers' headed Frame# */
	p->fillRect(0, 0, w, hs, header);
	for (int c = 0; c < columns; c++)
		if (colW(c) > 0 && rows.columnEmphasized(c)) p->fillRect(colX(c) + 1, 1, colW(c) - 1, hs - 1, k.emphasis);
	const bool titleLit = hoverTitles_ && hoverColumn_ >= -1 && hoverColumn_ < columns && (hoverColumn_ < 0 || colW(hoverColumn_) > 0);
	if (titleLit)
		p->fillRect(hoverColumn_ < 0 ? QRect(cx0 + 1, 1, fx - cx0 - 1, hs - 1) : QRect(colX(hoverColumn_) + 1, 1, colW(hoverColumn_) - 1, hs - 1), picked);
	p->setPen(titleLit && hoverColumn_ == -1 ? pickedText : headerText);
	label(QRect(cx0 + 4, 0, fx - cx0 - 4, hs), left, QStringLiteral("Frame#"), false);
	for (int c = 0; c < columns; c++)
	{
		if (colW(c) == 0) continue; /* hidden */
		p->setPen(titleLit && hoverColumn_ == c ? pickedText : headerText);
		label(QRect(colX(c) + 4, 0, colW(c) - 4, hs), left, horizontal_ ? title(c) : fm.elidedText(title(c), Qt::ElideRight, colW(c) - 4), false);
	}
	p->setPen(headerText);
	for (int e = 0; e < extraCount; e++)
		label(QRect(colX(columns + e) + 4, 0, extras_[static_cast<size_t>(e)].width - 4, hs), left, extras_[static_cast<size_t>(e)].text, false);
	/* the lines, one screen pixel wide at any display scale (a pen one unit
	 * wide drew some lines one pixel and some two on a scaled screen), end
	 * to end, filled: drawn as lines they took 3 ms a paint on Windows */
	const double px = 1.0 / p->device()->devicePixelRatioF();
	auto across = [&](int y, int x0, int x1, const QColor &color) { p->fillRect(QRectF(x0, y, x1 - x0 + px, px), color); };
	auto down = [&](int x, int y0, int y1, const QColor &color) { p->fillRect(QRectF(x, y0, px, y1 - y0 + px), color); };
	across(0, 0, w, k.columnBorder);
	down(cx0, 0, hs, k.columnBorder);
	for (int c = 0; c <= columns + extraCount; c++)
		if (c == columns + extraCount || c >= columns || colW(c) > 0) down(colX(c), 0, hs, k.columnBorder);
	/* a thick line where a player's columns start and end, the players as
	 * the core declares them (the console's a group too) */
	const std::vector<bool> playerEdge = chimera::vm::PianoRollViewModel::playerEdges(players, shownColumns);
	auto thickLines = [&](int top, int bottom, const QColor &color) {
		for (int c = 0; c <= columns; c++)
			if (playerEdge[static_cast<size_t>(c)]) p->fillRect(QRectF(colX(c) - 1.0, top, 2.0, bottom - top), color);
	};
	thickLines(0, hs, k.columnBorder);

	/* what the scripts say of the cells of the rows shown, when they say
	 * anything: asked at once before the rows are drawn (issue 476) */
	std::map<std::tuple<int, qint64, QString>, QString> said;
	if (queries_ && query())
	{
		std::vector<Asked> asked;
		qint64 at = rows.nextShown(top_ - 1);
		for (int i = 0; i < shown && at < count; i++, at = rows.nextShown(at))
		{
			auto cell = [&](const QString &column) { for (int kind : {0, 1, 2}) asked.push_back({kind, at, column, {}}); };
			asked.push_back({0, at, QStringLiteral("FrameColumn"), {}});
			for (int c = 0; c < columns; c++)
				if (colW(c) > 0) cell(QString::fromStdString(rows.columnName(c)));
			for (const Extra &x : extras_) cell(x.name);
		}
		query()(asked);
		for (Asked &a : asked)
			if (!a.answer.isEmpty()) said.emplace(std::make_tuple(a.kind, a.row, std::move(a.column)), std::move(a.answer));
	}
	auto ask = [&](int kind, qint64 row, const QString &column) {
		const auto it = said.find(std::make_tuple(kind, row, column));
		return it == said.end() ? QString() : it->second;
	};
	/* an icon the script names for a cell, drawn in it */
	auto icon = [&](qint64 row, const QString &column, const QRect &cell) {
		const QString path = ask(2, row, column);
		if (path.isEmpty()) return;
		auto it = icons_.find(path);
		if (it == icons_.end()) it = icons_.insert(path, QImage(path));
		if (it->isNull()) return;
		const int side = std::min(cell.width(), cell.height()) - 2;
		p->drawImage(QRect(cell.x() + (cell.width() - side) / 2, cell.y() + (cell.height() - side) / 2, side, side), *it);
	};
	/* Chimera's arrow in the cursor column and its corner icons in the frame column */
	auto arrow = [&](int y, const QColor &ink) {
		const double a = std::max(3.0, std::floor(5 * scale));
		const double x0 = cx0 - 3 - a - std::floor(2 * scale), y0 = y + std::floor(1.5 * scale);
		const QPointF points[3] = {QPointF(x0, y0), QPointF(x0 + a, y0 + a), QPointF(x0, y0 + 2 * a)};
		p->setPen(Qt::NoPen);
		p->setBrush(ink);
		p->drawPolygon(points, 3);
		p->setBrush(Qt::NoBrush);
	};
	auto corner = [&](int y, const QColor &ink) {
		const double a = std::max(4.0, std::floor(6 * scale));
		const QPointF points[3] = {QPointF(cx0 + 1, y + 1), QPointF(cx0 + 1, y + 1 + a), QPointF(cx0 + 1 + a, y + 1)};
		p->setPen(Qt::NoPen);
		p->setBrush(ink);
		p->drawPolygon(points, 3);
		p->setBrush(Qt::NoBrush);
	};
	/* the frame numbers all as long, as Chimera pads them: five digits at least */
	int digits = 1;
	for (qint64 n = std::max<qint64>(count, 1); n >= 10; n /= 10) digits++;
	digits = std::max(4, digits - 1) + 1;
	QFont hintFont = font_;
	hintFont.setBold(false);

	/* the rows shown, hidden ones (lag frames TAStudio hides) passed over */
	qint64 next = rows.nextShown(top_ - 1);
	for (int i = 0; i < shown; i++)
	{
		const qint64 row = next;
		if (row >= count) break;
		next = rows.nextShown(row);
		const int y = hs + rowHeight_ * i;
		const uint32_t flags = rows.rowFlags(row);
		const bool isLag = flags & 1, inGreen = flags & 2, isMarker = flags & 4, stated = flags & 8, ran = flags & 16, isPermanent = flags & 32;
		/* as Chimera's TAStudio colours a row: the frame a seek is going to, else
		 * the machine's; the greenzone's, apart where a state is held; those an
		 * edit took out of it; the rest */
		const bool current = seekRow_ >= 0 ? row == seekRow_ : row == current_;
		QColor back = defaultRow;
		if (current) back = now;
		else if (inGreen) back = !stated && denoteStatesColor_ ? (isLag ? lag : green) : (isLag ? lagStated : greenStated);
		else if (ran) back = isLag ? lagGone : greenGone;
		p->fillRect(0, y, right, rowHeight_, back);
		if (k.cursorColumn.isValid()) p->fillRect(0, y, cx0, rowHeight_, k.cursorColumn);
		/* every second player's columns */
		for (int c = 0; c < columns; c++)
		{
			const int cw = colW(c);
			if (cw == 0) continue;
			if (const int32_t player = players[static_cast<size_t>(c)]; player > 0 && player % 2 == 0)
				p->fillRect(colX(c), y, cw, rowHeight_, altPlayer);
		}
		/* the frame number's own colour: a marker's (when markers are told by colour), else Set Colors' Frame#
		 * Column on a frame the greenzone holds, else the wash over the row */
		if (isMarker && denoteMarkersColor_ && row != current_)
			p->fillRect(cx0, y, fx - cx0, rowHeight_, isPermanent ? permanent : marker);
		else
		{
			const std::string frameRole = vm::PianoRollViewModel::frameColumnRole(current, inGreen, isLag);
			if (frameRole == "TasGreenZone" || frameRole == "TasLagZone")
				p->fillRect(cx0, y, fx - cx0, rowHeight_, frameRole == "TasGreenZone" ? greenFrame : lagFrame);
			else if (frameWash.isValid())
				p->fillRect(cx0, y, fx - cx0, rowHeight_, frameWash);
		}
		/* the frame number's cell: the script's colour over it, when it gives one */
		if (const QColor c(ask(0, row, QStringLiteral("FrameColumn"))); c.isValid()) p->fillRect(cx0, y, fx - cx0, rowHeight_, c);
		for (int c = 0; c < columns; c++)
		{
			const int cw = colW(c);
			if (cw == 0) continue;
			if (row == pickedRow_ && c == pickedColumn_) p->fillRect(colX(c), y, cw, rowHeight_, analogEdit);
			if (queries_)
				if (const QColor b(ask(0, row, QString::fromStdString(rows.columnName(c)))); b.isValid()) p->fillRect(colX(c), y, cw, rowHeight_, b);
		}
		for (int e = 0; e < extraCount; e++)
			if (const QColor b(ask(0, row, extras_[static_cast<size_t>(e)].name)); b.isValid())
				p->fillRect(colX(columns + e), y, extras_[static_cast<size_t>(e)].width, rowHeight_, b);
		/* the frames picked, the whole row a third of the way to the selection colour, as Chimera's */
		const bool isPicked = selectFirst_ >= 0 && row >= selectFirst_ && row <= selectLast_;
		if (isPicked) p->fillRect(0, y, right, rowHeight_, QColor(picked.red(), picked.green(), picked.blue(), 84));

		/* the icons: the machine's frame and the one edits go back to, as arrows; a marker's corner, else a held state's */
		if (row == current_) arrow(y, k.iconPlayback);
		else if (row == restoreRow_) arrow(y, k.iconRecording);
		if (isMarker && denoteMarkersIcons_) corner(y, k.iconMarker);
		else if (stated && denoteStatesIcons_) corner(y, isLag ? k.iconLagAnchor : k.iconAnchor);

		const QColor ink = isPicked ? pickedText : text;
		p->setPen(ink);
		/* a branch's number on the frame it was made on */
		if (const auto at = std::find(branchFrames_.begin(), branchFrames_.end(), row); at != branchFrames_.end())
			label(QRect(1, y, cx0 + 6, rowHeight_), left, QString::number(at - branchFrames_.begin() + 1), true);
		label(QRect(cx0 + 4 + 7, y, fx - cx0 - 11, rowHeight_), left, QString::number(row).rightJustified(digits, QLatin1Char('0')), true);
		for (int c = 0; c < columns; c++)
		{
			const int cx = colX(c), cw = colW(c);
			if (cw == 0) continue; /* hidden */
			const QString columnName = queries_ ? QString::fromStdString(rows.columnName(c)) : QString();
			icon(row, columnName, QRect(cx, y, cw, rowHeight_));
			QString said = ask(1, row, columnName);
			bool numberRight = false;
			if (said.isEmpty())
			{
				if (numbers[static_cast<size_t>(c)])
				{
					int64_t value = 0;
					if (vm::fromValue(rows.cell(row, c), value)) said = QString::number(value), numberRight = true;
				}
				else
				{
					/* a pressed button's cell reads as its column's title (chimera#225) */
					bool held = false;
					vm::fromValue(rows.cell(row, c), held);
					if (held) said = title(c);
				}
			}
			if (!said.isEmpty())
			{
				p->setPen(ink);
				label(QRect(cx + 4, y, cw - 6, rowHeight_), numberRight ? int(Qt::AlignVCenter | Qt::AlignRight) : left, said, true);
			}
			else if (row == hoverRow_ && c == hoverColumn_ && dragColumn_ < 0 && !selecting_)
			{
				/* an empty cell under the pointer names its column, as Chimera's */
				p->setFont(hintFont);
				p->setPen(k.hint);
				label(QRect(cx + 4, y, cw - 4, rowHeight_), left, title(c), true);
				p->setFont(font_);
			}
			if (row == pickedRow_ && c == pickedColumn_)
			{
				p->setPen(QPen(k.lit, 2));
				p->drawRect(cx + 1, y + 1, cw - 2, rowHeight_ - 2);
			}
		}
		/* the script's own columns: what it says, nothing else */
		for (int e = 0; e < extraCount; e++)
		{
			const Extra &x = extras_[static_cast<size_t>(e)];
			const int cx = colX(columns + e);
			icon(row, x.name, QRect(cx, y, x.width, rowHeight_));
			if (const QString said = ask(1, row, x.name); !said.isEmpty())
			{
				p->setPen(ink);
				label(QRect(cx + 4, y, x.width - 6, rowHeight_), left, said, true);
			}
		}
	}

	/* the grid: a line at every column's left and the last's right, and between the rows, each one
	 * screen pixel wide; a thick one where a player's columns start and end */
	const int bottom = std::min(h, hs + rowHeight_ * shown);
	down(cx0, hs + 1, bottom, grid);
	for (int c = 0; c <= columns + extraCount; c++)
		if (c >= columns || colW(c) > 0) down(colX(c), hs + 1, bottom, grid);
	for (int i = 1; i < shown; i++) across(hs + rowHeight_ * i, 0, w, grid);
	/* the titles' bottom line over the first row, which would cover it */
	across(hs, 0, w, k.columnBorder);
	thickLines(hs + 1, bottom, k.columnBorder);

	lastPaintMs_ = timer.nsecsElapsed() / 1.0e6;
	Q_EMIT painted();
}

void PianoRollItem::hoverMoveEvent(QHoverEvent *e)
{
	const QPointF at = logical(e->position());
	const int hs = headerSize();
	int column = -3;
	if (at.x() < cursorWidth()) column = -2;
	else if (at.x() < frameColumnWidth()) column = -1;
	else if (rows_ != nullptr)
		for (int c = 0; c < rows_->rows().columnCount(); c++)
			if (columnW(c) > 0 && at.x() >= columnX(c) && at.x() < columnX(c) + columnW(c)) column = c;
	const bool titles = at.y() < hs;
	const qint64 row = titles || rows_ == nullptr ? -1 : rowOnLine(static_cast<qint64>((at.y() - hs) / rowHeight_));
	if (row == hoverRow_ && column == hoverColumn_ && titles == hoverTitles_) return;
	/* a control's title come onto, or left */
	const bool wasTitle = hoverTitles_ && hoverColumn_ >= 0, isTitle = titles && column >= 0 && rows_ != nullptr;
	if (isTitle && (!wasTitle || column != hoverColumn_))
		Q_EMIT titleHovered(QString::fromStdString(rows_->rows().columnName(column)), at.x(), at.y());
	else if (wasTitle && !isTitle)
		Q_EMIT titleHovered(QString(), 0, 0);
	hoverRow_ = row;
	hoverColumn_ = column;
	hoverTitles_ = titles;
	update();
}

void PianoRollItem::hoverLeaveEvent(QHoverEvent *)
{
	if (hoverTitles_ && hoverColumn_ >= 0) Q_EMIT titleHovered(QString(), 0, 0);
	hoverRow_ = -1;
	hoverColumn_ = -3;
	hoverTitles_ = false;
	update();
}

void PianoRollItem::mouseDoubleClickEvent(QMouseEvent *e)
{
	const QPointF at = logical(e->position());
	const int hs = headerSize();
	if (e->button() == Qt::LeftButton && rows_ != nullptr && at.x() < frameColumnWidth() && at.y() >= hs)
	{
		const qint64 row = rowOnLine(static_cast<qint64>((at.y() - hs) / rowHeight_));
		if (row < rowCount()) Q_EMIT frameDoubleClicked(row);
	}
	/* the second press of the double click is a press as well */
	mousePressEvent(e);
}

void PianoRollItem::mousePressEvent(QMouseEvent *e)
{
	/* the roll takes the keyboard, from a field such as the greenzone's
	 * period, so the hotkeys are heard again (issue 462) */
	forceActiveFocus(Qt::MouseFocusReason);
	qint64 row = 0;
	int column = 0;
	/* where it is in the roll as laid out upright */
	const QPointF at = logical(e->position());
	const int hs = headerSize();
	/* the right button: a column's menu on its name, else for whoever is under the roll */
	if (e->button() == Qt::RightButton)
	{
		if (rows_ != nullptr && at.y() < hs && cellAt(QPointF(at.x(), hs), row, column))
		{
			Q_EMIT columnMenuRequested(column, QString::fromStdString(rows_->rows().columnName(column)), rows_->rows().columnEmphasized(column),
				e->position().x(), e->position().y());
			e->accept();
			return;
		}
		if (rows_ != nullptr && at.y() >= hs)
		{
			rightHeld_ = true;
			rightWheeled_ = rightSeeked_ = false;
			rightRow_ = rowOnLine(static_cast<qint64>((at.y() - hs) / rowHeight_));
			/* on a frame number: Chimera's drags with Shift, Ctrl or Alt */
			rightDragging_ = at.x() >= cursorWidth() && at.x() < frameColumnWidth() && rightRow_ < rowCount();
			rightMoved_ = false;
			rightDragRow_ = rightRow_;
			const Qt::KeyboardModifiers keys = e->modifiers();
			rightEditing_ = rightDragging_ && (keys & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier));
			if (rightDragging_)
				Q_EMIT rightDragStarted(rightRow_, (keys & Qt::ControlModifier) != 0, (keys & Qt::ShiftModifier) != 0, (keys & Qt::AltModifier) != 0);
			e->accept();
			return;
		}
		e->ignore();
		return;
	}
	if (at.x() < frameColumnWidth() && at.y() >= hs && rows_ != nullptr)
	{
		row = rowOnLine(static_cast<qint64>((at.y() - hs) / rowHeight_));
		if (row >= rowCount())
		{
			e->accept();
			return;
		}
		if (at.x() < cursorWidth())
		{
			/* the machine goes to the frame, and follows the pointer while it is held */
			cursorDragging_ = true;
			cursorDragRow_ = row;
			Q_EMIT frameClicked(row);
		}
		else if ((e->modifiers() & Qt::AltModifier) && (rows_->rows().rowFlags(row) & 4))
		{
			/* a marker taken up by its frame number, to drop on another (Chimera's Alt + Drag) */
			markerDrag_ = row;
		}
		else
		{
			/* Shift extends from where picking began; a drag picks what it passes */
			if (!(e->modifiers() & Qt::ShiftModifier) || selectAnchor_ < 0) selectAnchor_ = row;
			selecting_ = true;
			Q_EMIT selectionRequested(std::min(selectAnchor_, row), std::max(selectAnchor_, row));
		}
		e->accept();
		return;
	}
	if (!cellAt(at, row, column))
	{
		e->ignore();
		return;
	}
	dragColumn_ = column;
	dragRow_ = row;
	Q_EMIT cellPressed(row, column, (e->modifiers() & Qt::AltModifier) != 0);
	e->accept();
}

void PianoRollItem::mouseMoveEvent(QMouseEvent *e)
{
	const QPointF where = logical(e->position());
	const int hs = headerSize();
	auto rowUnder = [&] {
		return std::min(rowCount() - 1, rowOnLine(static_cast<qint64>(std::max(0.0, where.y() - hs) / rowHeight_)));
	};
	if (rightDragging_ && rows_ != nullptr)
	{
		const qint64 row = rowUnder();
		if (row >= 0 && row != rightDragRow_)
		{
			rightDragRow_ = row;
			rightMoved_ = true;
			Q_EMIT rightDraggedTo(row);
		}
		return;
	}
	if (cursorDragging_ && rows_ != nullptr)
	{
		const qint64 row = rowUnder();
		if (row >= 0 && row != cursorDragRow_)
		{
			cursorDragRow_ = row;
			Q_EMIT frameClicked(row);
		}
		return;
	}
	if (markerDrag_ >= 0) return;
	if (selecting_ && rows_ != nullptr && where.y() >= hs)
	{
		const qint64 row = std::min(rowCount() - 1, rowOnLine(static_cast<qint64>((where.y() - hs) / rowHeight_)));
		if (row >= 0) Q_EMIT selectionRequested(std::min(selectAnchor_, row), std::max(selectAnchor_, row));
		return;
	}
	if (dragColumn_ < 0) return;
	/* a drag stays in the column it began in, whatever the pointer's x */
	const QPointF at(columnX(dragColumn_) + 1, where.y());
	qint64 row = 0;
	int column = 0;
	if (cellAt(at, row, column) && row != dragRow_)
	{
		/* every row passed over, even when the pointer moved fast */
		const qint64 step = row > dragRow_ ? 1 : -1;
		for (qint64 r = dragRow_ + step; r != row + step; r += step) Q_EMIT cellDragged(r, dragColumn_);
		dragRow_ = row;
	}
}

void PianoRollItem::mouseReleaseEvent(QMouseEvent *e)
{
	if (e->button() == Qt::RightButton)
	{
		/* Chimera's row menu opens on the release, and not after a wheel seek or a drag that edited */
		if (rightDragging_) Q_EMIT rightDragEnded();
		if (rightHeld_ && !rightWheeled_ && !(rightEditing_ && rightMoved_)) Q_EMIT rowMenuRequested(rightRow_);
		rightHeld_ = false;
		rightDragging_ = rightEditing_ = rightMoved_ = false;
		return;
	}
	cursorDragging_ = false;
	if (markerDrag_ >= 0)
	{
		const QPointF where = logical(e->position());
		const qint64 to = rows_ != nullptr ? std::min(rowCount() - 1, rowOnLine(static_cast<qint64>(std::max(0.0, where.y() - headerSize()) / rowHeight_))) : -1;
		if (to >= 0 && to != markerDrag_) Q_EMIT markerDropped(markerDrag_, to);
		markerDrag_ = -1;
		return;
	}
	selecting_ = false;
	dragColumn_ = -1;
	dragRow_ = -1;
}

void PianoRollItem::wheelEvent(QWheelEvent *e)
{
	const int notches = e->angleDelta().y() / 120;
	if (rightHeld_)
	{
		rightWheeled_ = true;
		if (notches != 0)
		{
			Q_EMIT wheelSeekRequested(notches, !rightSeeked_);
			rightSeeked_ = true;
		}
		e->accept();
		return;
	}
	if (notches != 0) Q_EMIT topRowRequested(std::max<qint64>(0, top_ - notches * std::max(1, wheelRows_)));
	e->accept();
}

} // namespace chimera::qt
