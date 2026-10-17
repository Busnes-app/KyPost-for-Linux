#pragma once

#include "models/Email.h"
#include "models/KeywordSettings.h"

#include <QString>
#include <QStringList>
#include <QVector>

class SettingsStore;

struct KeywordTab
{
    QString name;
    int unread = 0; // unread emails carrying the keyword

    bool operator==(const KeywordTab&) const = default;
};

// Mirrors Domain/Repositories/KeywordRepository.swift's three-method shape:
// derive the keyword-tab set (with unread counts) from whatever emails are on hand,
// then filter/annotate by the per-keyword visibility SettingsStore persists.
class KeywordRepository
{
public:
    explicit KeywordRepository(SettingsStore& settingsStore);

    // All keywords present in `emails` (Email::keywords, as populated by
    // MailRepository), alphabetical (case-insensitive) with unread counts.
    static QVector<KeywordTab> computeTabs(const QVector<Email>& emails);

    // Tabs to show in the inbox tab bar (hidden keywords filtered out).
    QVector<KeywordTab> visibleTabs(const QVector<Email>& emails) const;

    // All keywords with their visibility, for a future KeywordSettings screen.
    QVector<KeywordSettings> allSettings(const QVector<Email>& emails) const;

    void setVisible(const QString& keyword, bool visible);
    void setOrder(const QStringList& keywords);

private:
    SettingsStore& m_settingsStore;
};
