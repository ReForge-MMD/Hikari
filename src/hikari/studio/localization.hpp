#pragma once

#include <QString>
#include <QWidget>

namespace hikari::studio::localization {

enum class Language { English, Russian, Japanese };

Language current_language();
void load_language();
void set_language(Language language);
QString language_code(Language language);
Language language_from_code(const QString& code);
QString language_name(Language language);

// Translates one English source string. Unknown strings are returned unchanged.
QString text(const QString& english);
inline QString text(const char* english) {
    return text(QString::fromUtf8(english));
}

// Translates a formatted status message by matching the known English format
// patterns and substituting the already formatted arguments.
QString message(const QString& formatted);

// Translates a widget tree and its actions in place. Original English strings
// are remembered on each object so switching languages is repeatable.
void apply(QWidget* root);

// Marks a widget as data-bearing (model names, bone names, preset names) so its
// text or items are never translated.
void skip(QWidget* widget);

} // namespace hikari::studio::localization
