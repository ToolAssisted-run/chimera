#pragma once
/* The window in a language (docs/translations.md). Every text is a key in
 * Chimera's language tables, which the engine holds (ce_text); the window's
 * QML asks for them with qsTrId, which a translator installed here answers
 * from the engine. Config > Language chooses one; at the first open it is
 * the first of the system's languages Chimera has, else English (owner,
 * 2026-10-28, issue 376). */

#include <QLocale>
#include <QString>
#include <QStringList>

class QCoreApplication;

namespace chimera::qt {

/* the languages there are tables for ("en", "de", "zh_CN"), English first */
QStringList languages();

/* the language the window starts in: the one chosen, when there is a table
 * for it, else the first of the locale's languages there is one for, the
 * most precise name first (pt_BR before pt), else "en" */
QString startLanguage(const QString &chosen, const QLocale &locale = QLocale());

/* Chimera's texts in a language from now on, for the window and the engine
 * alike; false when there is no table for it */
bool useLanguage(QCoreApplication &app, const QString &language);

/* a language's name in that language ("Deutsch"), and in English ("German") */
QString languageName(const QString &language);
QString languageEnglishName(const QString &language);
/* as the Language menu names it: in itself and in English, "Deutsch -
 * German", whatever language the window is in; English alone (owner,
 * 2026-10-31, issue 430: a dash, since some names have brackets of their own) */
QString languageLabel(const QString &language);

} // namespace chimera::qt
