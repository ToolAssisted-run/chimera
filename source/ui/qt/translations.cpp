#include "translations.hpp"

#include "chimera/engine.h"

#include <QCoreApplication>
#include <QTranslator>

namespace chimera::qt {

namespace {

/* qsTrId's answers, from the engine's table of the language in use */
class EngineTranslator : public QTranslator
{
public:
	using QTranslator::QTranslator;
	bool isEmpty() const override { return false; }
	QString translate(const char *, const char *key, const char *, int n) const override
	{
		return QString::fromUtf8(ce_text(key, n, nullptr, 0));
	}
};

EngineTranslator *installed = nullptr;

int indexOf(const QString &language)
{
	const QByteArray code = language.toUtf8();
	for (int32_t i = 0; i < ce_language_count(); i++)
	{
		if (code == ce_language_code(i)) return i;
	}
	return -1;
}

} // namespace

QStringList languages()
{
	QStringList out;
	for (int32_t i = 0; i < ce_language_count(); i++) out << QString::fromUtf8(ce_language_code(i));
	return out;
}

QString startLanguage(const QString &chosen, const QLocale &locale)
{
	const QStringList there = languages();
	if (there.contains(chosen)) return chosen;
	/* "fr-CA" is tried as fr_CA, then as fr */
	for (QString name : locale.uiLanguages())
	{
		name.replace(QLatin1Char('-'), QLatin1Char('_'));
		if (there.contains(name)) return name;
		const QString general = name.section(QLatin1Char('_'), 0, 0);
		if (there.contains(general)) return general;
	}
	return QStringLiteral("en");
}

bool useLanguage(QCoreApplication &app, const QString &language)
{
	if (ce_language_use(language.toUtf8().constData()) == 0) return false;
	if (installed == nullptr)
	{
		installed = new EngineTranslator(&app);
		QCoreApplication::installTranslator(installed);
	}
	return true;
}

QString languageName(const QString &language)
{
	const int i = indexOf(language);
	return i < 0 ? language : QString::fromUtf8(ce_language_name(i));
}

QString languageEnglishName(const QString &language)
{
	const int i = indexOf(language);
	return i < 0 ? language : QString::fromUtf8(ce_language_english_name(i));
}

QString languageLabel(const QString &language)
{
	const QString own = languageName(language), english = languageEnglishName(language);
	return own == english ? own : own + QStringLiteral(" - ") + english;
}

} // namespace chimera::qt
