/* TAStudio's piano roll with more columns than it has room for (issue 568):
 * a column that starts past the roll's edge is not seen, so it is not read,
 * nor asked of the scripts. The picture is the same as before: what a roll
 * wide enough for every column shows on the same width. */
#include <QGuiApplication>
#include <QImage>
#include <QPainter>

#include <cstdio>
#include <string>

#include "../piano_roll_item.hpp"
#include "../qml_view_model.hpp"
#include "chimera/vm/rows.hpp"

namespace {

int failures = 0, checks = 0;
#define CHECK(c) \
	do { \
		checks++; \
		if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } \
	} while (0)

/* frames of many buttons, every second one held, counting the cells read */
class Buttons : public chimera::vm::RowModel
{
public:
	explicit Buttons(int columns) : columns_(columns) {}
	int64_t rowCount() const override { return 1000; }
	int32_t columnCount() const override { return columns_; }
	std::string columnName(int32_t c) const override { return "B" + std::to_string(c); }
	chimera::vm::Value cell(int64_t row, int32_t c) const override
	{
		reads++;
		return (row + c) % 2 == 0;
	}
	int32_t columnPlayer(int32_t c) const override { return 1 + c / 8; }
	mutable int64_t reads = 0;

private:
	int columns_;
};

int64_t asked = 0;

/* the roll drawn at a width, its picture and the cells read for it */
QImage paint(Buttons &model, int width, int height)
{
	chimera::qt::QmlRows rows(model, nullptr);
	chimera::qt::PianoRollItem roll;
	roll.setRows(&rows);
	roll.setProperty("queries", true);
	roll.setWidth(width);
	roll.setHeight(height);
	roll.setTopRow(10);
	QImage image(width, height, QImage::Format_RGB32);
	image.fill(Qt::black);
	QPainter p(&image);
	roll.paint(&p);
	return image;
}

} // namespace

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	/* every question a script would be asked counted, none answered */
	chimera::qt::PianoRollItem::setQuery([](std::vector<chimera::qt::PianoRollItem::Asked> &a) { asked += static_cast<int64_t>(a.size()); });
	const int width = 400, height = 300;

	Buttons many(256);
	asked = 0;
	const QImage narrow = paint(many, width, height);
	const int64_t narrowReads = many.reads, narrowAsked = asked;
	many.reads = 0;
	asked = 0;
	const QImage wide = paint(many, 20000, height);
	const int64_t wideReads = many.reads, wideAsked = asked;
	std::printf("256 columns: %lld cells read and %lld questions on %d pixels, %lld and %lld with room for all\n",
		static_cast<long long>(narrowReads), static_cast<long long>(narrowAsked), width, static_cast<long long>(wideReads),
		static_cast<long long>(wideAsked));
	CHECK(narrowReads > 0 && narrowAsked > 0);
	CHECK(narrowReads * 8 < wideReads && narrowAsked * 8 < wideAsked);
	/* the same pixels as the wide roll's on the width shown */
	CHECK(narrow == wide.copy(0, 0, width, height));

	/* the columns past the edge are not read at all: 64 columns or 256, as many cells */
	Buttons fewer(64);
	asked = 0;
	const QImage fewerImage = paint(fewer, width, height);
	CHECK(fewer.reads == narrowReads && asked == narrowAsked);
	CHECK(fewerImage == narrow);

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
