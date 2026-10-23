#include "mail/MailController.h"

#include "db/Database.h"
#include "db/EmailDao.h"
#include "domain/DevicePairing.h"
#include "domain/KeywordRepository.h"
#include "db/FolderDao.h"
#include "domain/FolderRepository.h"
#include "domain/MailRepository.h"
#include "net/FolderClient.h"
#include "domain/PairingStore.h"
#include "mail/EmailListModel.h"
#include "net/HttpClient.h"
#include "net/NetworkExecutor.h"
#include "net/PgpBootstrapClient.h"
#include "net/PgpRecipientChecker.h"
#include "net/RelayMailSource.h"
#include "stores/CursorStore.h"
#include "stores/SecureStoreFile.h"
#include "stores/SettingsStore.h"

#include "../../core/net/FakeRelayServer.h"
#include "../ExecutorShutdownGuard.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

class MailControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void selectKeywordFiltersCachedEmailsWithoutAnyNetworkCall();
    void archiveEmailsNotPairedShortCircuitsWithNoNetworkCall();
    void sendMailOverAttachmentCapRejectsBeforeAnyNetworkCall();
    void sendMailUsesHtmlSendMode();
    void downloadAttachmentSanitizesPathTraversalInSuggestedName();
    void downloadAttachmentSanitizesPathTraversalInServerFilename();
    void downloadAttachmentStripsEmbeddedNulFromTheFilename();
    void hostileLocationRefusesToOpenNonAllowlistedAttachmentTypes();
    void hostileLocationForcesTheExtensionToMatchTheDeclaredType();
    void hostileLocationRefusesASymlinkPlantedAtThePredictableTemporaryPath();
    void hostileLocationRefusesATemporaryDirectoryItCannotCreate();
    void anAttachmentNoApplicationWillOpenIsDeletedRatherThanLeftBehind();
    void findByMessageIdReturnsMapForCachedEmailAndEmptyMapWhenMissing();
    void aNotificationForAMessageInTwoMailboxesAsksWhichOne();
    void allKeywordSettingsReflectsInboxCacheAndSetKeywordVisibleRoundTrips();
    void sendMailEmitsPickupFallbackRequiredWithTheServersAddressList();
    void confirmPickupFallbackSendResendsTheIdenticalBodyWithTheOptIn();
    void confirmPickupFallbackSendWithoutAPendingSendDoesNothing();
    void aReplyKeepsItsReferenceThroughThePickupResendUntilRefused();
    void replyAllLeavesOutOnlyThisAccountsOwnAddresses();
    void repliesLeaveOutAccountAddressesFromANewerRelay();
    void sendMailSurfacesAWarningOnAnOtherwiseSuccessfulSend();
    void sendMailClientSideNeededHandsOffAndOffersNoToggles();
    void openWebmailDraftsRefusesAnInsecurePairingBeforeSavingAnything();
    void refreshPgpComposeStateFailureHidesEveryControl();
    void preflightRecipientsFailureClearsRatherThanReassures();
    void refreshReturnsWithoutBlockingTheCallingThread();
    void overlappingRefreshesCollapseToOneFollowUpRequest();
    void aFolderSwitchDuringARefreshIsStillFetched();
    void archiveEmailsReportsCompletionAndDropsTheRowOptimistically();
    void moveEmailsSendsTheTargetMailboxAndDropsTheRow();
    void aRefusedMoveKeepsTheRowAndReportsWhy();
    void aMoveThatLandsAfterAFolderSwitchLeavesTheNewFolderAlone();
    void snoozeEmailsSendsWakeAtAndDropsTheRow();
    void snoozeEmailsRefusesATimeOutsideTheWindowWithoutARequest();
    void unsnoozeIsAMoveFromSnoozedToInbox();
    void unsnoozeOutsideSnoozedSendsNothing();
    void mailFoldersListsSnoozedLast();
    void refreshFoldersWalksTheArchiveTree();
    void refreshFoldersStopsAtTheDepthCap();
    void refreshFoldersPlacesALocalizedSentAndDraftsByRole();
    void mailFoldersShowsNoDeeperThanTheCap();
    void aLockedAppSendsNoFolderMutation();
    void aLockedAppSendsNoMailboxAction();
    void aRefreshThatOutlivesItsAccountWritesNothingAndReportsNoError();
    void anAttachmentThatOutlivesItsAccountIsNeverWrittenOrOpened();
    void aPgpComposeAnswerThatOutlivesItsAccountIsNotCached();
    void aRecipientPreflightThatOutlivesItsAccountShowsNothing();

    // Notification tap-through (phase 4).
    void aNotificationForUncachedMailRefreshesBeforeAnsweringIt();
    void aNotificationForCachedMailAnswersWithoutARequest();
    void aNotificationForMailThatCannotBeFoundSaysSoRatherThanOpeningNothing();
    void markReadIsOptimisticAndCoalescesRepeats();
    void aRefusedMarkReadRollsBackSilently();
    void aRefusedMarkUnreadRollsBackAndSaysSo();
    void aReadStateReplyForAReplacedAccountRollsNothingBack();
    void aLockedAppChangesNoReadStateUntilUnlocked();
    void unreadCountsComeFromTheCacheWithArchiveTotallingItsTree();
    void readStateChangesUpdateTheUnreadCounts();
    void aMoveDropsTheSourceCopyFromTheUnreadCounts();
    void aMoveReplyForAReplacedAccountLeavesTheCacheAlone();
    void aPurgedCacheShowsNoUnreadCounts();

    // Server search (L6).
    void searchIsDebouncedAndSendsOnlyTheLastQuery();
    void aStaleSearchReplyIsNeverShown();
    void aSearchHitOpensFromMemoryAndNeverReachesTheCache();
    void aFolderSwitchReRunsTheSearchThere();
    void aSearchOnlyHitIsMarkedReadThroughTheRelayInMemoryOnly();
    void aRefusedSearchOnlyMarkReadRollsBackTheRow();
    void aLockedAppNeitherEditsNorSendsASearchUntilUnlocked();

    // Drafts (L7).
    void aReopenedDraftPrefillsComposeAndItsFirstSaveReplacesTheSource();
    void aFailedSaveOrADraftWithAttachmentsKeepsTheSource();
    void sendingAReopenedPlaintextDraftUsesTheNormalSendAndDeletesTheSource();
    void aScheduledSendAsksTheRelayFirstThenCarriesSendAt();
    void aSendTimeTheRelayWouldRefuseSendsNothing();
    void aRelayThatCannotScheduleSendsNothing();
    void aScheduledSendHasNoImmediateLinkResend();
    void aSearchOnlyDraftWithNoReadableBodyIsNotReopened();
    void aDraftInALocalizedDraftsFolderIsReopenedAndReplacedThere();

    // Older mail (L12).
    void anOlderPageLoadsBelowTheListUntilTheRelayHasNoMore();
    void aLockedOrSearchingAppLoadsNoOlderMail();
    void anOlderPageForAFolderNoLongerOpenIsKeptButNotShown();
    void anOlderRowFetchesItsBodyWhenOpenedAndKeepsIt();
    void anOlderRowBodyForAReplacedAccountIsNotWritten();
    void anOlderDraftWithNoReadableBodyIsNotReopened();

    // Forward with the original's attachments.
    void aForwardCarriesTheOriginalsAttachmentsInMemory();
    void aForwardSaysWhichAttachmentsItCouldNotCarry();
    void aForwardThatOutlivesItsAccountOrTheLockKeepsNothing();
    void aProtectedMessageForwardsNoAttachments();
    void aLiveDraftSessionSurvivesACompletedForward();

private:
    static void savePairing(PairingStore& pairingStore, quint16 port);
    // downloadAttachment() dispatches and returns; the answer arrives as
    // attachmentDownloaded(). Wraps "start it, wait for that signal, report
    // its ok flag" so the security assertions below read the way they did
    // when the call was synchronous.
    //
    // executor.shutdown() before returning, not after the test's scope ends:
    // the executor is declared before the controller so it can be handed to
    // it, which means it is destroyed AFTER -- so anything still queued when
    // the test returns would be delivered to a dead receiver.
    static bool downloadAndWait(MailController& controller, NetworkExecutor& executor, const QString& mailbox,
                                 const QString& messageId, int index, const QString& suggestedName);
};

void MailControllerTest::savePairing(PairingStore& pairingStore, quint16 port)
{
    DevicePairing pairing;
    pairing.subscriberId = QStringLiteral("sub-1");
    pairing.deviceSecret = QStringLiteral("secret-1");
    pairing.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);
    pairing.deviceId = QStringLiteral("dev-1");
    QVERIFY(pairingStore.save(pairing));
}

bool MailControllerTest::downloadAndWait(MailController& controller, NetworkExecutor& executor,
                                          const QString& mailbox, const QString& messageId, int index,
                                          const QString& suggestedName)
{
    QSignalSpy spy(&controller, &MailController::attachmentDownloaded);
    controller.downloadAttachment(mailbox, messageId, index, suggestedName);
    const bool arrived = spy.wait(10000) || spy.count() > 0;
    executor.shutdown();
    if (!arrived || spy.isEmpty())
        return false;
    return spy.at(0).at(2).toBool();
}

void MailControllerTest::selectKeywordFiltersCachedEmailsWithoutAnyNetworkCall()
{
    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    // "Work" tab has m1, the display-only "Uncategorized" fallback tab has
    // m2 -- so after refresh() m1 carries keywords=["Work"], m2 carries no
    // keywords at all (matches MailRepository's existing keyword-population
    // rule, see MailRepositoryTest.cpp).
    const QByteArray body = R"(
    {
      "tabs": ["Work", "Uncategorized"],
      "byTab": {
        "Work": [
          {
            "messageId": "m1",
            "sender": "a@example.com",
            "sentTo": "b@example.com",
            "cc": "",
            "bcc": "",
            "subject": "Work item",
            "status": "unread",
            "atUtc": "2026-07-01T00:00:00Z",
            "hasAttachments": false,
            "label": ""
          }
        ],
        "Uncategorized": [
          {
            "messageId": "m2",
            "sender": "c@example.com",
            "sentTo": "d@example.com",
            "cc": "",
            "bcc": "",
            "subject": "Solo item",
            "status": "unread",
            "atUtc": "2026-07-02T00:00:00Z",
            "hasAttachments": false,
            "label": ""
          }
        ]
      }
    }
    )";
    FakeRelayServer fake(httpResponse(200, "OK", body));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    // refresh() is the only call in this test allowed to reach the network
    // -- it populates the cache selectKeyword() below must filter locally.
    // It dispatches to the executor and returns, so isBusy is the barrier:
    // set before the call returns, cleared by the completion handler.
    controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 5000);
    auto* model = qobject_cast<EmailListModel*>(controller.emailModel());
    QVERIFY(model != nullptr);
    QCOMPARE(model->rowCount(), 2);

    const QByteArray requestAfterRefresh = fake.receivedRequest();
    QVERIFY(!requestAfterRefresh.isEmpty());

    controller.selectKeyword(QStringLiteral("Work"));

    QCOMPARE(controller.selectedKeyword(), QStringLiteral("Work"));
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(model->emailAt(0).messageId, QStringLiteral("m1"));

    // No second connection was made -- FakeRelayServer's captured buffer is
    // unchanged (it can only append via a new connection's readyRead).
    QCOMPARE(fake.receivedRequest(), requestAfterRefresh);

    // Clearing the keyword restores both cached rows, still with no network.
    controller.selectKeyword(QString());
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(fake.receivedRequest(), requestAfterRefresh);

    // Settings -> Connection: the refresh above is the last sync, and
    // notification history is labelled from this same cache.
    QVERIFY(controller.lastSyncAt().isValid());
    QVERIFY(controller.notificationHistory().isEmpty()); // no source set
    controller.setNotificationHistorySource([]() {
        return QVector<PushRecord>{ { QStringLiteral("m1"), 2, QStringLiteral("2026-10-11T07:05:00Z"), false },
                                    { QStringLiteral("gone"), 1, QStringLiteral("2026-10-11T06:00:00Z"), true } };
    });
    const QVariantList history = controller.notificationHistory();
    QCOMPARE(history.size(), 2);
    const QVariantMap cached = history.at(0).toMap();
    QCOMPARE(cached.value(QStringLiteral("messageId")).toString(), QStringLiteral("m1"));
    QCOMPARE(cached.value(QStringLiteral("subject")).toString(), QStringLiteral("Work item"));
    QCOMPARE(cached.value(QStringLiteral("sender")).toString(), QStringLiteral("a@example.com"));
    QCOMPARE(cached.value(QStringLiteral("receivedAt")).toDateTime(),
             QDateTime(QDate(2026, 10, 11), QTime(7, 5), QTimeZone::UTC));
    QCOMPARE(history.at(1).toMap().value(QStringLiteral("subject")).toString(), QString());
}

void MailControllerTest::archiveEmailsNotPairedShortCircuitsWithNoNetworkCall()
{
    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"action":"archive","processed":0,"failed":[],"targetMailbox":""})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore); // never saved -- not paired

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    QSignalSpy errorSpy(&controller, &MailController::lastErrorChanged);
    QSignalSpy doneSpy(&controller, &MailController::actionCompleted);
    controller.archiveEmails({ QStringLiteral("m1") });

    // The refusal is synchronous -- requirePairing() runs before anything is
    // dispatched, which is the point: no request is built at all.
    QCOMPARE(doneSpy.count(), 1);
    QCOMPARE(doneSpy.at(0).at(0).toString(), QStringLiteral("archive"));
    QCOMPARE(doneSpy.at(0).at(1).toStringList(), QStringList{ QStringLiteral("m1") });
    QCOMPARE(doneSpy.at(0).at(2).toBool(), false);
    QCOMPARE(controller.lastError(), QStringLiteral("Not paired"));
    QVERIFY(errorSpy.count() >= 1);
    QVERIFY(fake.receivedRequest().isEmpty());
}

void MailControllerTest::sendMailOverAttachmentCapRejectsBeforeAnyNetworkCall()
{
    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"sentSaved":true,"warning":""})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    QTemporaryDir attachmentDir;
    QVERIFY(attachmentDir.isValid());
    const QString bigFilePath = attachmentDir.filePath(QStringLiteral("big.bin"));
    QFile bigFile(bigFilePath);
    QVERIFY(bigFile.open(QIODevice::WriteOnly));
    // One byte over the 25 MB cap -- sparse-zero-filled, so this stays fast
    // and small on disk regardless of the logical size reported back.
    QVERIFY(bigFile.resize(25LL * 1024 * 1024 + 1));
    bigFile.close();

    // 0 = refused before anything was dispatched, which is the property this
    // test is about: the cap is enforced without contacting the server.
    const quint64 token = controller.sendMail(QStringLiteral("to@example.com"), QString(), QString(),
                                               QStringLiteral("Subject"), QStringLiteral("Body"), { bigFilePath },
                                               /*sign=*/false, /*encrypt=*/false);

    QCOMPARE(token, 0u);
    QVERIFY(controller.lastError().contains(QStringLiteral("25 MB")));
    QVERIFY(fake.receivedRequest().isEmpty());
}

void MailControllerTest::sendMailUsesHtmlSendMode()
{
    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"sentSaved":true,"warning":""})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    QSignalSpy sendSpy(&controller, &MailController::sendCompleted);
    const quint64 token = controller.sendMail(QStringLiteral("to@example.com"), QString(), QString(),
                                               QStringLiteral("Subject"), QStringLiteral("<b>Body</b>"), {},
                                               /*sign=*/false, /*encrypt=*/false);
    QVERIFY(token != 0u);
    QVERIFY(sendSpy.wait(10000));
    QCOMPARE(sendSpy.at(0).at(0).toULongLong(), token);
    QCOMPARE(sendSpy.at(0).at(1).toBool(), true);

    const QJsonObject sent = fake.receivedJsonBody();
    QCOMPARE(sent.value(QStringLiteral("mode")).toString(), QStringLiteral("html"));
    QCOMPARE(sent.value(QStringLiteral("body")).toString(), QStringLiteral("<b>Body</b>"));
}

void MailControllerTest::downloadAttachmentSanitizesPathTraversalInSuggestedName()
{
    // Regression test for a path-traversal fix: both the caller-supplied
    // suggestedName (this test) and the server's Content-Disposition
    // filename (next test) are attacker-influenced -- they originate from
    // the mail message's own attachment metadata -- so a name containing
    // "../" segments must not be able to write outside the Downloads
    // directory. QStandardPaths::setTestModeOn() redirects
    // QStandardPaths::DownloadLocation to a sandboxed test location so this
    // test can safely assert on real filesystem writes.
    QStandardPaths::setTestModeEnabled(true);

    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    const QByteArray attachmentBytes = "attachment-bytes";
    FakeRelayServer fake(httpResponse(200, "OK", attachmentBytes, "application/octet-stream"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    const QString downloadDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QDir().mkpath(downloadDir);
    const QString escapeTarget = QDir(downloadDir).filePath(QStringLiteral("../evil.txt"));
    QFile::remove(QDir::cleanPath(escapeTarget));

    const bool ok = downloadAndWait(controller, executor, QStringLiteral("Inbox"), QStringLiteral("42"), 0,
                                     QStringLiteral("../evil.txt"));

    QCOMPARE(ok, true);
    QVERIFY(!QFile::exists(QDir::cleanPath(escapeTarget)));

    const QString sanitizedPath = QDir(downloadDir).filePath(QStringLiteral("evil.txt"));
    QVERIFY(QFile::exists(sanitizedPath));
    QFile written(sanitizedPath);
    QVERIFY(written.open(QIODevice::ReadOnly));
    QCOMPARE(written.readAll(), attachmentBytes);
}

void MailControllerTest::downloadAttachmentSanitizesPathTraversalInServerFilename()
{
    QStandardPaths::setTestModeEnabled(true);

    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    const QByteArray attachmentBytes = "server-named-bytes";
    // No suggestedName supplied by the caller -- downloadAttachment() falls
    // back to the response's Content-Disposition filename, which is just as
    // attacker-influenced as the caller-supplied name in the sibling test.
    FakeRelayServer fake(httpResponse(200, "OK", attachmentBytes, "application/octet-stream",
                                       { { "Content-Disposition", R"(attachment; filename="../../evil2.txt")" } }));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    const QString downloadDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QDir().mkpath(downloadDir);
    const QString escapeTarget = QDir::cleanPath(QDir(downloadDir).filePath(QStringLiteral("../../evil2.txt")));
    QFile::remove(escapeTarget);

    const bool ok = downloadAndWait(controller, executor, QStringLiteral("Inbox"), QStringLiteral("42"), 0,
                                     QString());

    QCOMPARE(ok, true);
    QVERIFY(!QFile::exists(escapeTarget));

    const QString sanitizedPath = QDir(downloadDir).filePath(QStringLiteral("evil2.txt"));
    QVERIFY(QFile::exists(sanitizedPath));
}

void MailControllerTest::findByMessageIdReturnsMapForCachedEmailAndEmptyMapWhenMissing()
{
    // Task 35: findByMessageId() is the QML-facing wrapper EmailDetail.qml
    // calls to look up a full Email by messageId -- a pure local-cache read
    // (MailController::findByMessageId -> MailRepository::findCachedEmail
    // -> EmailDao::findById), no network/pairing involved.
    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    Email seed;
    seed.messageId = QStringLiteral("m-1");
    seed.folder = QStringLiteral("INBOX");
    seed.sender = QStringLiteral("Alice <alice@example.com>");
    seed.subject = QStringLiteral("Hello");
    seed.preview = QStringLiteral("Preview text");
    seed.keywords = QStringList{ QStringLiteral("Work") };
    seed.hasAttachments = true;
    QVERIFY(emailDao.insertOrReplace(seed));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    const QVariantMap found = controller.findByMessageId(QStringLiteral("m-1"));
    QCOMPARE(found.value(QStringLiteral("messageId")).toString(), QStringLiteral("m-1"));
    QCOMPARE(found.value(QStringLiteral("sender")).toString(), QStringLiteral("Alice <alice@example.com>"));
    QCOMPARE(found.value(QStringLiteral("subject")).toString(), QStringLiteral("Hello"));
    QCOMPARE(found.value(QStringLiteral("hasAttachments")).toBool(), true);
    QCOMPARE(found.value(QStringLiteral("keywords")).toStringList(), QStringList{ QStringLiteral("Work") });

    QVERIFY(controller.findByMessageId(QStringLiteral("no-such-id")).isEmpty());
}

// A push envelope carries no mailbox, and a message id is not unique across
// them. Until 2026-08-23 this fell through to a refresh that cannot
// disambiguate anything and then to a blank detail page: the user tapped View
// and got nothing to act on, for a question only they can answer.
void MailControllerTest::aNotificationForAMessageInTwoMailboxesAsksWhichOne()
{
    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    // The same id in two mailboxes, which is what migration 006 made possible.
    Email inboxCopy;
    inboxCopy.messageId = QStringLiteral("m-shared");
    inboxCopy.folder = QStringLiteral("INBOX");
    inboxCopy.subject = QStringLiteral("Inbox copy");
    QVERIFY(emailDao.insertOrReplace(inboxCopy));
    Email archiveCopy = inboxCopy;
    archiveCopy.folder = QStringLiteral("Archive");
    QVERIFY(emailDao.insertOrReplace(archiveCopy));

    FakeRelayServer fake(httpResponse(200, "OK", R"({"tabs":[],"byTab":{}})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    QSignalSpy ambiguous(&controller, &MailController::notificationEmailAmbiguous);
    QSignalSpy resolved(&controller, &MailController::notificationEmailResolved);

    controller.openFromNotification(QStringLiteral("m-shared"));

    QCOMPARE(ambiguous.count(), 1);
    QCOMPARE(ambiguous.at(0).at(0).toString(), QStringLiteral("m-shared"));
    // Both mailboxes, so the user can pick the one they meant.
    QCOMPARE(ambiguous.at(0).at(1).toStringList(),
             QStringList({ QStringLiteral("Archive"), QStringLiteral("INBOX") }));

    // Not answered as "resolved with no folder" -- that is the not-found
    // outcome, which the roots render as a terminal error.
    QCOMPARE(resolved.count(), 0);

    // And no request was made. A delta cannot remove an ambiguity that is
    // already in the cache, so spending a round trip to arrive at the same
    // question is just a slower way to ask it.
    QVERIFY2(fake.receivedRequest().isEmpty(), "a refresh was made for a question it cannot answer");
}

void MailControllerTest::allKeywordSettingsReflectsInboxCacheAndSetKeywordVisibleRoundTrips()
{
    // Task 39: allKeywordSettings()/setKeywordVisible() (Settings > Keywords
    // pane) are pure local reads/writes over EmailDao + SettingsStore --
    // no network/pairing involved, same shape as
    // findByMessageId's test above.
    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    Email seed;
    seed.messageId = QStringLiteral("m-1");
    seed.folder = QStringLiteral("INBOX");
    seed.sender = QStringLiteral("Alice <alice@example.com>");
    seed.subject = QStringLiteral("Hello");
    seed.preview = QStringLiteral("Preview text");
    seed.keywords = QStringList{ QStringLiteral("Work") };
    QVERIFY(emailDao.insertOrReplace(seed));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    // "Work" was never toggled -- SettingsStore::keywordVisible() defaults
    // to true (see its own doc comment), so it should show up as visible.
    const QVariantList before = controller.allKeywordSettings();
    QCOMPARE(before.size(), 1);
    const QVariantMap workEntry = before.at(0).toMap();
    QCOMPARE(workEntry.value(QStringLiteral("keyword")).toString(), QStringLiteral("Work"));
    QCOMPARE(workEntry.value(QStringLiteral("visible")).toBool(), true);

    QSignalSpy keywordTabsChangedSpy(&controller, &MailController::keywordTabsChanged);
    controller.setKeywordVisible(QStringLiteral("Work"), false);
    QVERIFY(keywordTabsChangedSpy.count() >= 1);

    const QVariantList after = controller.allKeywordSettings();
    QCOMPARE(after.size(), 1);
    QCOMPARE(after.at(0).toMap().value(QStringLiteral("visible")).toBool(), false);

    // Also persisted through to SettingsStore directly, independent of this
    // controller.
    QCOMPARE(settingsStore.keywordVisible(QStringLiteral("Work")), false);
}

void MailControllerTest::sendMailEmitsPickupFallbackRequiredWithTheServersAddressList()
{
    // The dialog names the SERVER's list, not the preflight's: the server ran
    // the discovery ladder as well as contacts, and may name an address typed
    // after the last preflight.
    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    FakeRelayServer fake(httpResponse(
        409, "Conflict",
        R"({"error":"some recipients have no usable PGP key",)"
        R"("keylessRecipients":["bob@example.com"],"pickupFallbackAvailable":true})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    QSignalSpy spy(&controller, &MailController::pickupFallbackRequired);
    const quint64 sendToken = controller.sendMail(QStringLiteral("bob@example.com"), QString(), QString(),
                                                   QStringLiteral("Hi"), QStringLiteral("Body"), {},
                                                   /*sign=*/false, /*encrypt=*/true);

    QVERIFY(sendToken != 0u);
    QVERIFY(spy.wait(10000));
    QCOMPARE(spy.count(), 1);
    // The refusal names the very send the caller was handed a token for --
    // that identity is what lets one composer of several know it is theirs.
    QCOMPARE(spy.at(0).at(0).toULongLong(), sendToken);
    QCOMPARE(spy.at(0).at(1).toStringList(), QStringList{ QStringLiteral("bob@example.com") });
    // The token names the one pending send this refusal belongs to. Never 0:
    // a default-constructed PendingSend must not be able to match it.
    QVERIFY(spy.at(0).at(0).value<quint64>() != 0);
}

void MailControllerTest::confirmPickupFallbackSendResendsTheIdenticalBodyWithTheOptIn()
{
    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    FakeRelayServer refusal(httpResponse(
        409, "Conflict",
        R"({"error":"no usable key","keylessRecipients":["bob@example.com"],"pickupFallbackAvailable":true})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, refusal.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    // A REAL attachment, deleted between the refusal and the confirm. This is
    // the assertion the whole PendingSend cache exists to satisfy: with an
    // empty attachment list the test could not tell "the cache held the bytes"
    // apart from "there were never any bytes", and rebuilding the payload from
    // the QML fields on confirm would re-read this path and fail here.
    QTemporaryDir attachmentDir;
    QVERIFY(attachmentDir.isValid());
    const QString attachmentPath = attachmentDir.filePath(QStringLiteral("note.txt"));
    const QByteArray originalBytes = "the-bytes-the-user-reviewed";
    {
        QFile attachment(attachmentPath);
        QVERIFY(attachment.open(QIODevice::WriteOnly));
        QCOMPARE(attachment.write(originalBytes), static_cast<qint64>(originalBytes.size()));
    }

    QSignalSpy fallbackSpy(&controller, &MailController::pickupFallbackRequired);
    QVERIFY(controller.sendMail(QStringLiteral("bob@example.com"), QString(), QString(), QStringLiteral("Hi"),
                                 QStringLiteral("Body"), { attachmentPath },
                                 /*sign=*/false, /*encrypt=*/true)
            != 0u);
    QVERIFY(fallbackSpy.wait(10000));
    QCOMPARE(fallbackSpy.count(), 1);
    const quint64 token = fallbackSpy.at(0).at(0).value<quint64>();

    // The file is gone AND its contents replaced by the time the user
    // confirms -- an editor saved over it, or a temp export was cleaned up.
    QVERIFY(QFile::remove(attachmentPath));
    {
        QFile replacement(attachmentPath);
        QVERIFY(replacement.open(QIODevice::WriteOnly));
        replacement.write("SOMETHING-ELSE-ENTIRELY");
    }

    // Second server: FakeRelayServer serves exactly one connection.
    FakeRelayServer accepted(httpResponse(200, "OK", R"({"ok":true,"sentSaved":true,"warning":""})"));
    savePairing(pairingStore, accepted.port());

    // A confirmation naming a DIFFERENT pending send must not send: this is
    // what stops a dialog opened in another composer from resolving this one.
    // Checked before the real confirm on purpose -- `accepted` serves a single
    // connection, so if this reached the network the confirm below would fail.
    QCOMPARE(controller.confirmPickupFallbackSend(token + 1), 0u);

    QSignalSpy confirmSpy(&controller, &MailController::sendCompleted);
    QCOMPARE(controller.confirmPickupFallbackSend(token), token);
    QVERIFY(confirmSpy.wait(10000));
    QCOMPARE(confirmSpy.at(0).at(1).toBool(), true);

    const QJsonObject first = refusal.receivedJsonBody();
    const QJsonObject second = accepted.receivedJsonBody();
    QCOMPARE(second.value(QStringLiteral("allowPickupFallback")).toBool(), true);
    QCOMPARE(first.value(QStringLiteral("allowPickupFallback")).toBool(), false);
    // Everything else is byte-identical: same subject, body, recipients, flags.
    for (const QString& key : { QStringLiteral("to"), QStringLiteral("cc"), QStringLiteral("bcc"),
                                 QStringLiteral("subject"), QStringLiteral("body"),
                                 QStringLiteral("mode"), QStringLiteral("sign"),
                                 QStringLiteral("encrypt") }) {
        QCOMPARE(second.value(key), first.value(key));
    }
    QCOMPARE(second.value(QStringLiteral("attachments")), first.value(QStringLiteral("attachments")));

    // ...and those identical attachment bytes are the ORIGINAL file's, not
    // whatever is on disk now.
    const QJsonArray resentAttachments = second.value(QStringLiteral("attachments")).toArray();
    QCOMPARE(resentAttachments.size(), 1);
    const QJsonObject resent = resentAttachments.at(0).toObject();
    QCOMPARE(resent.value(QStringLiteral("name")).toString(), QStringLiteral("note.txt"));
    QCOMPARE(QByteArray::fromBase64(resent.value(QStringLiteral("dataBase64")).toString().toLatin1()),
             originalBytes);

    // The opt-in is per-message: a second confirm must not re-send anything.
    QCOMPARE(controller.confirmPickupFallbackSend(token), 0u);
}

void MailControllerTest::confirmPickupFallbackSendWithoutAPendingSendDoesNothing()
{
    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    // Any token at all, with nothing cached: a stray confirm must not mail.
    QCOMPARE(controller.confirmPickupFallbackSend(1), 0u);
    QCOMPARE(controller.confirmPickupFallbackSend(0), 0u);
}

void MailControllerTest::sendMailSurfacesAWarningOnAnOtherwiseSuccessfulSend()
{
    // A non-empty warning means partial trouble (the Sent copy failed, or some
    // pickup links did not deliver) -- the message WAS sent, so this must not
    // look like a failure and must not offer a retry that would duplicate it.
    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    // Custom raw-string delimiter (not the default R"(...)"): the warning
    // text's "recipient(s)" contains the literal sequence )" , which would
    // otherwise terminate a default-delimited raw string early.
    FakeRelayServer fake(httpResponse(
        200, "OK",
        R"json({"ok":true,"sentSaved":false,"warning":"failed to deliver a pickup link to 1 of 3 recipient(s)"})json"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    QSignalSpy spy(&controller, &MailController::sendWarning);
    QSignalSpy doneSpy(&controller, &MailController::sendCompleted);
    QVERIFY(controller.sendMail(QStringLiteral("bob@example.com"), QString(), QString(), QStringLiteral("Hi"),
                                 QStringLiteral("Body"), {}, /*sign=*/false, /*encrypt=*/true)
            != 0u);
    QVERIFY(doneSpy.wait(10000));

    QCOMPARE(doneSpy.at(0).at(1).toBool(), true);
    QCOMPARE(spy.count(), 1);
    QVERIFY(!spy.at(0).at(0).toString().isEmpty());
}

void MailControllerTest::sendMailClientSideNeededHandsOffAndOffersNoToggles()
{
    // The other 409. Categorical, not confirmable: the account's private key
    // exists only in the user's browser, so no request from this app can
    // encrypt on its behalf. Nothing may stay cached for a confirm, and the
    // composer must show the handoff INSTEAD OF the toggles, never both.
    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    FakeRelayServer fake(httpResponse(
        409, "Conflict",
        R"({"error":"this account's PGP key is end-to-end protected","clientSideNeeded":true})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    QSignalSpy fallbackSpy(&controller, &MailController::pickupFallbackRequired);
    QSignalSpy composeStateSpy(&controller, &MailController::pgpComposeStateChanged);

    QSignalSpy doneSpy(&controller, &MailController::sendCompleted);
    QVERIFY(controller.sendMail(QStringLiteral("bob@example.com"), QString(), QString(), QStringLiteral("Hi"),
                                 QStringLiteral("Body"), {}, /*sign=*/false, /*encrypt=*/true)
            != 0u);
    QVERIFY(doneSpy.wait(10000));

    QCOMPARE(doneSpy.at(0).at(1).toBool(), false);
    // Never the plaintext-link dialog: this refusal is not confirmable.
    QCOMPARE(fallbackSpy.count(), 0);
    QVERIFY(composeStateSpy.count() >= 1);
    QCOMPARE(controller.pgpHandoffToWebmail(), true);
    // One source of truth: the handoff block replaces the toggles.
    QCOMPARE(controller.pgpCanEncrypt(), false);
    QCOMPARE(controller.pgpCanSign(), false);
    QVERIFY(!controller.lastError().isEmpty());
    // The pending send was dropped, so a stray confirm cannot resurrect it.
    QCOMPARE(controller.confirmPickupFallbackSend(1), 0u);
}

void MailControllerTest::openWebmailDraftsRefusesAnInsecurePairingBeforeSavingAnything()
{
    // Order matters more than the return value: the https check runs BEFORE
    // the draft POST, so a handoff that cannot open the browser does not leave
    // a silently duplicated draft behind. savePairing() writes an
    // http://127.0.0.1 base and webmailMailboxUrl() is https-only, so this
    // pairing can never produce a link.
    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    // 0 = refused before anything was dispatched, which is the whole point:
    // the https check runs ahead of the draft POST.
    const quint64 token = controller.openWebmailDrafts(QStringLiteral("bob@example.com"), QString(), QString(),
                                                        QStringLiteral("Hi"), QStringLiteral("Body"), {});

    QCOMPARE(token, 0u);
    QVERIFY(controller.lastError().contains(QStringLiteral("insecure connection")));
    // The actual invariant: no draft POST was made at all.
    QVERIFY(fake.receivedRequest().isEmpty());
}

void MailControllerTest::refreshPgpComposeStateFailureHidesEveryControl()
{
    // "Couldn't check" is never "no PGP" AND never "client custody" -- a
    // failed bootstrap must leave every control hidden rather than guess a
    // custody mode and offer the wrong send path.
    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    FakeRelayServer fake(httpResponse(503, "Service Unavailable", "unavailable", "text/plain"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    // The fetch is off-thread now, so this has to wait for the answer.
    // Asserting straight after the call would assert the INITIAL state --
    // which is also all-false, so the test would pass no matter what the
    // completion handler did.
    QSignalSpy stateSpy(&controller, &MailController::pgpComposeStateChanged);
    controller.refreshPgpComposeState();
    QVERIFY(stateSpy.wait(10000));
    executor.shutdown();

    QCOMPARE(controller.pgpCanEncrypt(), false);
    QCOMPARE(controller.pgpCanSign(), false);
    QCOMPARE(controller.pgpHandoffToWebmail(), false);
}

void MailControllerTest::preflightRecipientsFailureClearsRatherThanReassures()
{
    // The preflight is an advisory lower bound. A failed check must leave the
    // list EMPTY -- showing nothing -- rather than a stale answer standing in
    // for one this call never got.
    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    FakeRelayServer fake(httpResponse(500, "Internal Server Error", "boom", "text/plain"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    // Populate the list for real first. Asserting "empty after a failure"
    // starting from empty proves nothing -- it is the initial state, and was
    // the weakness in this test even while the call was synchronous.
    fake.setResponse(httpResponse(200, "OK", R"({"results":[{"address":"bob@example.com","hasKey":false}]})"));
    QSignalSpy keylessSpy(&controller, &MailController::pgpKeylessRecipientsChanged);
    controller.preflightRecipients(QStringLiteral("bob@example.com"), QString(), QString());
    QVERIFY(keylessSpy.wait(10000));
    QCOMPARE(controller.pgpKeylessRecipients(), QStringList{ QStringLiteral("bob@example.com") });

    // Now the failure. It must CLEAR that list rather than leave the previous
    // answer standing in for one this call never got.
    fake.setResponse(httpResponse(500, "Internal Server Error", "boom", "text/plain"));
    controller.preflightRecipients(QStringLiteral("carol@example.com"), QString(), QString());
    QVERIFY(keylessSpy.wait(10000));
    executor.shutdown();

    QVERIFY(controller.pgpKeylessRecipients().isEmpty());
}


// Hostile Location Protection routes attachments through
// openAttachmentEphemerally(), which ends in QDesktopServices::openUrl --
// i.e. it hands attacker-supplied bytes to whatever handler the desktop has
// registered for the file's extension. There was no type check at all, so
// "Invoice.pdf.desktop" or "notes.pdf.html" got an arbitrary handler
// launched with the user's full session privileges. The mode built for
// people who expect to be attacked was the only mode that auto-opened
// hostile input.
void MailControllerTest::hostileLocationRefusesToOpenNonAllowlistedAttachmentTypes()
{
    QStandardPaths::setTestModeEnabled(true);

    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    // Declared type is not on the allowlist, whatever the name claims.
    FakeRelayServer fake(httpResponse(200, "OK", "#!/bin/sh\necho pwned", "application/x-desktop",
                                       { { "Content-Disposition",
                                           "attachment; filename=\"Invoice.pdf.desktop\"" } }));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    QVERIFY(settingsStore.setHostileLocationProtectionEnabled(true));
    KeywordRepository keywordRepository(settingsStore);

    QTemporaryDir runtimeDir;
    QVERIFY(runtimeDir.isValid());
    qputenv("XDG_RUNTIME_DIR", runtimeDir.path().toUtf8());

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    const bool ok = downloadAndWait(controller, executor, QStringLiteral("Inbox"), QStringLiteral("42"), 0,
                                     QStringLiteral("Invoice.pdf.desktop"));

    QCOMPARE(ok, false);
    QVERIFY2(!controller.lastError().isEmpty(), "the refusal must be explained, not silent");

    // Nothing was written anywhere for a handler to pick up.
    const QDir attachmentDir(runtimeDir.filePath(QStringLiteral("kypost-attachments")));
    QVERIFY(attachmentDir.entryList(QDir::Files).isEmpty());
}

// Even for an allowlisted type, the filename must not decide the handler:
// a .desktop name declared as application/pdf would otherwise pass the gate
// and still be launched as a .desktop file.
void MailControllerTest::hostileLocationForcesTheExtensionToMatchTheDeclaredType()
{
    QStandardPaths::setTestModeEnabled(true);

    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    FakeRelayServer fake(httpResponse(200, "OK", "%PDF-1.4 fake", "application/pdf",
                                       { { "Content-Disposition",
                                           "attachment; filename=\"Invoice.pdf.desktop\"" } }));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    QVERIFY(settingsStore.setHostileLocationProtectionEnabled(true));
    KeywordRepository keywordRepository(settingsStore);

    QTemporaryDir runtimeDir;
    QVERIFY(runtimeDir.isValid());
    qputenv("XDG_RUNTIME_DIR", runtimeDir.path().toUtf8());

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    // Stands in for the desktop's handler. QDesktopServices::openUrl() needs
    // a QGuiApplication and this binary is guiless, so the real one always
    // fails here -- and, now that a failed launch is a failed open, this test
    // measures the extension rule rather than the QPA platform.
    QUrl launched;
    controller.setAttachmentLauncher([&launched](const QUrl& url) {
        launched = url;
        return true;
    });

    QVERIFY2(downloadAndWait(controller, executor, QStringLiteral("Inbox"), QStringLiteral("42"), 0,
                             QStringLiteral("Invoice.pdf.desktop")),
             qUtf8Printable(controller.lastError()));

    QVERIFY2(launched.isLocalFile(), "the attachment was reported as opened without being handed over");
    QVERIFY(launched.toLocalFile().endsWith(QStringLiteral(".pdf")));

    const QDir attachmentDir(runtimeDir.filePath(QStringLiteral("kypost-attachments")));
    const QStringList written = attachmentDir.entryList(QDir::Files);
    QCOMPARE(written.size(), 1);
    QVERIFY2(written.first().endsWith(QStringLiteral(".pdf")),
             "the extension must come from the declared MIME type, not the message");
    QVERIFY(!written.first().contains(QStringLiteral(".desktop")));

    controller.clearEphemeralAttachments();
}

// The one that used to fail open.
//
// /tmp/kypost-attachments is a PREDICTABLE path, and on a sticky /tmp any
// local user can create it before KyPost does. A symlink planted there sends
// the mail wherever it points; mkpath() succeeded because the path already
// resolved to a directory, the chmod result was dropped, and the attachment
// was written through the link.
//
// The link here points at a directory this user owns and that IS private, so
// the permission check cannot be what refuses -- only the symlink check can.
void MailControllerTest::hostileLocationRefusesASymlinkPlantedAtThePredictableTemporaryPath()
{
    QStandardPaths::setTestModeEnabled(true);

    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    FakeRelayServer fake(httpResponse(200, "OK", "%PDF-1.4 confidential", "application/pdf",
                                       { { "Content-Disposition", "attachment; filename=\"payslip.pdf\"" } }));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    QVERIFY(settingsStore.setHostileLocationProtectionEnabled(true));
    KeywordRepository keywordRepository(settingsStore);

    QTemporaryDir runtimeDir;
    QVERIFY(runtimeDir.isValid());
    QTemporaryDir elsewhere; // where the planted link points
    QVERIFY(elsewhere.isValid());
    const QString planted = runtimeDir.filePath(QStringLiteral("kypost-attachments"));
    QVERIFY(QFile::link(elsewhere.path(), planted));
    QVERIFY2(QFileInfo(planted).isSymLink(), "the premise is a symlink at the predictable path");
    qputenv("XDG_RUNTIME_DIR", runtimeDir.path().toUtf8());

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    bool launched = false;
    controller.setAttachmentLauncher([&launched](const QUrl&) {
        launched = true;
        return true;
    });

    QCOMPARE(downloadAndWait(controller, executor, QStringLiteral("Inbox"), QStringLiteral("42"), 0,
                              QStringLiteral("payslip.pdf")),
             false);
    QVERIFY2(!controller.lastError().isEmpty(), "the refusal must be explained, not silent");
    QVERIFY2(!launched, "the attachment was handed to a viewer out of a planted directory");
    QVERIFY2(QDir(elsewhere.path()).entryList(QDir::Files).isEmpty(),
             "the attachment was written through a symlink planted at the predictable path");
}

// And the other half of the same rule: when the location cannot be created at
// all, nothing is written and nothing is reported as opened. mkpath()'s result
// used to be checked here but its companion chmod's was not, so this is the
// branch that stayed honest -- it is locked down so it cannot quietly stop
// being.
//
// NOT COVERED, and not pretendable: a directory owned by ANOTHER user, which
// is the case where the chmod is the only thing that fails. It needs a second
// uid, which an in-process test does not have. PrivatePath::ensureDirectory()
// answers from the mode on the disk either way, and PrivatePathTest covers it
// from the other side.
void MailControllerTest::hostileLocationRefusesATemporaryDirectoryItCannotCreate()
{
    QStandardPaths::setTestModeEnabled(true);

    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    FakeRelayServer fake(httpResponse(200, "OK", "%PDF-1.4 confidential", "application/pdf",
                                       { { "Content-Disposition", "attachment; filename=\"payslip.pdf\"" } }));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    QVERIFY(settingsStore.setHostileLocationProtectionEnabled(true));
    KeywordRepository keywordRepository(settingsStore);

    // A plain FILE sitting where the directory has to go.
    QTemporaryDir runtimeDir;
    QVERIFY(runtimeDir.isValid());
    const QString blocked = runtimeDir.filePath(QStringLiteral("kypost-attachments"));
    QFile blocker(blocked);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    qputenv("XDG_RUNTIME_DIR", runtimeDir.path().toUtf8());

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    bool launched = false;
    controller.setAttachmentLauncher([&launched](const QUrl&) {
        launched = true;
        return true;
    });

    QCOMPARE(downloadAndWait(controller, executor, QStringLiteral("Inbox"), QStringLiteral("42"), 0,
                              QStringLiteral("payslip.pdf")),
             false);
    QVERIFY2(!controller.lastError().isEmpty(), "the refusal must be explained, not silent");
    QVERIFY2(!launched, "an attachment that was never written was handed to a viewer");
    QCOMPARE(QFileInfo(blocked).size(), 0); // nothing appended to the blocker either
}

// A launch that never happened used to be reported as success, and the file
// stayed on disk for the five-minute timer -- an "ephemeral" attachment
// nobody ever opened, sitting there for no reason at all.
void MailControllerTest::anAttachmentNoApplicationWillOpenIsDeletedRatherThanLeftBehind()
{
    QStandardPaths::setTestModeEnabled(true);

    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    FakeRelayServer fake(httpResponse(200, "OK", "%PDF-1.4 fake", "application/pdf",
                                       { { "Content-Disposition", "attachment; filename=\"payslip.pdf\"" } }));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    QVERIFY(settingsStore.setHostileLocationProtectionEnabled(true));
    KeywordRepository keywordRepository(settingsStore);

    QTemporaryDir runtimeDir;
    QVERIFY(runtimeDir.isValid());
    qputenv("XDG_RUNTIME_DIR", runtimeDir.path().toUtf8());

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    // No handler for this type on this machine.
    controller.setAttachmentLauncher([](const QUrl&) { return false; });

    QCOMPARE(downloadAndWait(controller, executor, QStringLiteral("Inbox"), QStringLiteral("42"), 0,
                              QStringLiteral("payslip.pdf")),
             false);
    QVERIFY2(!controller.lastError().isEmpty(), "the failure must be explained, not silent");

    const QDir attachmentDir(runtimeDir.filePath(QStringLiteral("kypost-attachments")));
    QVERIFY2(attachmentDir.entryList(QDir::Files).isEmpty(),
             "the attachment nobody opened was left on disk");
}


// A NUL in the mail-supplied filename split Qt's own view of the path in two:
// QFile::open() hands the encoded path to open(2), which truncates at the
// NUL, while QFile::exists()/QFileInfo::exists()/QFile::remove() reject the
// same string outright. Under Hostile Location Protection that defeated the
// MIME-driven extension forcing -- "Invoice.desktop\0.pdf" looked like a
// .pdf to the gate and landed as "Invoice.desktop" on disk, handing the
// desktop's handler choice back to the sender -- and it silently no-op'd the
// delete timer, the exit cleanup and the write rollback, so the file outlived
// the session in the one mode that promises it cannot.
void MailControllerTest::downloadAttachmentStripsEmbeddedNulFromTheFilename()
{
    QStandardPaths::setTestModeEnabled(true);

    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    const QByteArray attachmentBytes = "attachment-bytes";
    FakeRelayServer fake(httpResponse(200, "OK", attachmentBytes, "application/octet-stream"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    const QString downloadDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QDir().mkpath(downloadDir);
    const QString truncatedTarget = QDir(downloadDir).filePath(QStringLiteral("Invoice.desktop"));
    QFile::remove(truncatedTarget);

    QString hostile = QStringLiteral("Invoice.desktop");
    hostile.append(QChar(u'\0'));
    hostile.append(QStringLiteral(".pdf"));
    QCOMPARE(hostile.size(), 20);

    QVERIFY(downloadAndWait(controller, executor, QStringLiteral("Inbox"), QStringLiteral("42"), 0, hostile));

    // The name open(2) would have truncated to must NOT be what landed.
    QVERIFY(!QFile::exists(truncatedTarget));

    // What did land keeps its real extension and is visible to the same Qt
    // calls that clean it up -- exists() agreeing with open() is the point.
    const QString expected = QDir(downloadDir).filePath(QStringLiteral("Invoice.desktop.pdf"));
    QVERIFY(QFile::exists(expected));
    QVERIFY(QFile::remove(expected));
}

namespace {

// The eight objects every refresh test below needs, in the one order that
// works. Bundled because the assertions are the interesting part of these
// three tests and the wiring is not -- and because getting the declaration
// order wrong here is a use-after-free, not a compile error.
//
// The executor is declared BEFORE the controller so it can be handed to it,
// which means it is destroyed AFTER -- so callers must call shutdown()
// themselves while the controller is still alive if any work may still be
// outstanding. NetworkExecutor::run() states that precondition.
struct RefreshFixture
{
    explicit RefreshFixture(quint16 port)
    {
        [&] {
            QVERIFY(db.open(QStringLiteral(":memory:")));
            QVERIFY(secureDir.isValid());
            QVERIFY(cursorDir.isValid());
            QVERIFY(settingsDir.isValid());
        }();

        DevicePairing pairing;
        pairing.subscriberId = QStringLiteral("sub-1");
        pairing.deviceSecret = QStringLiteral("secret-1");
        pairing.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);
        pairing.deviceId = QStringLiteral("dev-1");
        [&] { QVERIFY(pairingStore.save(pairing)); }();
    }

    Database db;
    QTemporaryDir secureDir;
    QTemporaryDir cursorDir;
    QTemporaryDir settingsDir;

    EmailDao emailDao{ db.handle() };
    FolderDao folderDao{ db.handle() };
    SecureStoreFile secureStore{ secureDir.path() };
    PairingStore pairingStore{ secureStore };
    CursorStore cursorStore{ cursorDir.filePath(QStringLiteral("cursor.ini")) };
    SettingsStore settingsStore{ settingsDir.filePath(QStringLiteral("settings.ini")) };
    KeywordRepository keywordRepository{ settingsStore };

    QNetworkAccessManager manager;
    HttpClient http{ manager };
    RelayMailSource source{ http };
    PgpBootstrapClient bootstrapClient{ http };
    PgpRecipientChecker recipientChecker{ http };
    FolderClient folderClient{ http };
    MailRepository mailRepository{ source, emailDao, pairingStore, cursorStore };
    FolderRepository folderRepository{ folderClient, folderDao, pairingStore };

    NetworkExecutor executor{ 1500 };
    MailController controller{ mailRepository,  source,         keywordRepository, pairingStore, folderRepository,
                               settingsStore,   bootstrapClient, recipientChecker,  executor };
    // Last member, so it is destroyed FIRST -- see the type's own comment.
    ExecutorShutdownGuard shutdownGuard{ executor };
};

// Two messages, one per tab, in whatever mailbox was asked for.
const char* const kTwoMessageInbox = R"(
{
  "tabs": ["Work", "Uncategorized"],
  "byTab": {
    "Work": [{"messageId":"m1","sender":"a@example.com","sentTo":"b@example.com","cc":"","bcc":"",
              "subject":"Work item","status":"unread","atUtc":"2026-07-01T00:00:00Z",
              "hasAttachments":false,"label":""}],
    "Uncategorized": [{"messageId":"m2","sender":"c@example.com","sentTo":"d@example.com","cc":"","bcc":"",
              "subject":"Solo item","status":"unread","atUtc":"2026-07-02T00:00:00Z",
              "hasAttachments":false,"label":""}]
  }
}
)";

} // namespace

// The whole point of the conversion: the GUI thread is not suspended in a
// nested event loop for the duration of the request, so QML keeps rendering
// and nothing can re-enter this object half-way through its own state
// changes.
//
// The server accepts and never answers, so a synchronous implementation
// would sit here for the full transfer timeout.
void MailControllerTest::refreshReturnsWithoutBlockingTheCallingThread()
{
    FakeRelayServer fake(QByteArray{});
    RefreshFixture f(fake.port());

    QElapsedTimer timer;
    timer.start();
    f.controller.refresh();
    const qint64 elapsed = timer.elapsed();

    QVERIFY2(elapsed < 200, qPrintable(QStringLiteral("refresh() blocked for %1 ms").arg(elapsed)));
    QVERIFY(f.controller.isBusy());

    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    QVERIFY(!f.controller.lastError().isEmpty());
}

// Pull-to-refresh, the toolbar button and a folder switch can all fire
// inside one round trip. They must collapse to ONE follow-up request rather
// than queueing N behind each other on the single executor thread.
//
// Asserted as a comparison against the one-call case rather than as an
// absolute count, per docs/THREADING.md: the number of TCP connections one
// logical request costs is Qt's business, not this test's.
void MailControllerTest::overlappingRefreshesCollapseToOneFollowUpRequest()
{
    const auto connectionsForRefreshCount = [](int refreshes) {
        FakeRelayServer fake(httpResponse(200, "OK", kTwoMessageInbox));
        RefreshFixture f(fake.port());

        for (int i = 0; i < refreshes; ++i)
            f.controller.refresh();

        [&] { QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000); }();
        f.executor.shutdown();
        return fake.connectionCount();
    };

    const int one = connectionsForRefreshCount(1);
    const int ten = connectionsForRefreshCount(10);
    QVERIFY(one > 0);
    // One in flight plus exactly one coalesced follow-up -- the other eight
    // were dropped, not queued.
    QCOMPARE(ten, 2 * one);
}

// The follow-up re-reads m_currentFolder rather than replaying the folder the
// coalesced call named, so switching mailboxes while a refresh is out still
// fetches the mailbox the user actually ended up on. Without that, selecting
// a folder during a refresh would leave it never fetched at all.
void MailControllerTest::aFolderSwitchDuringARefreshIsStillFetched()
{
    FakeRelayServer fake(httpResponse(200, "OK", kTwoMessageInbox));
    RefreshFixture f(fake.port());

    f.controller.refresh();                              // INBOX, dispatched
    f.controller.selectFolder(QStringLiteral("Sent"));   // coalesced into the follow-up

    QCOMPARE(f.controller.currentFolder(), QStringLiteral("Sent"));
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    f.executor.shutdown();

    // FakeRelayServer appends every connection's bytes to one buffer, so both
    // requests are visible in it.
    const QByteArray requests = fake.receivedRequest();
    QVERIFY2(requests.contains("mailbox=INBOX"), requests.constData());
    QVERIFY2(requests.contains("mailbox=Sent"), requests.constData());
}

// The four actions dispatch and return, so every QML site that used to read
// their bool now reads actionCompleted instead: `messageIds` is what lets a
// detail view or a swipe row tell "that was mine" from "that was the other
// window's", since nothing has a call on the stack any more.
void MailControllerTest::archiveEmailsReportsCompletionAndDropsTheRowOptimistically()
{
    FakeRelayServer fake(httpResponse(200, "OK", kTwoMessageInbox));
    RefreshFixture f(fake.port());

    f.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);

    auto* model = qobject_cast<EmailListModel*>(f.controller.emailModel());
    QVERIFY(model != nullptr);
    QCOMPARE(model->rowCount(), 2);

    fake.setResponse(httpResponse(
        200, "OK", R"({"ok":true,"action":"archive","processed":1,"failed":[],"targetMailbox":""})"));

    QSignalSpy doneSpy(&f.controller, &MailController::actionCompleted);
    f.controller.archiveEmails({ QStringLiteral("m1") });
    QVERIFY(f.controller.isBusy()); // set before the call returned

    QTRY_VERIFY_WITH_TIMEOUT(doneSpy.count() == 1, 10000);
    f.executor.shutdown();

    QCOMPARE(doneSpy.at(0).at(0).toString(), QStringLiteral("archive"));
    QCOMPARE(doneSpy.at(0).at(1).toStringList(), QStringList{ QStringLiteral("m1") });
    QCOMPARE(doneSpy.at(0).at(2).toBool(), true);
    QVERIFY(!f.controller.isBusy());

    // Optimistic removal: the archived row is gone from the list without a
    // second round trip, and the untouched one stays.
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(model->emailAt(0).messageId, QStringLiteral("m2"));

    // The action really was sent for the message it named -- proving the
    // second response was matched to the second request, not served early
    // off the first one's headers.
    QVERIFY2(fake.receivedRequest().contains("\"archive\""), fake.receivedRequest().constData());
    QVERIFY2(fake.receivedRequest().contains("\"m1\""), fake.receivedRequest().constData());
}

// End to end on the real path: the account is replaced while a refresh is
// out on the executor thread.
//
// No timing trick is needed to arrange it -- refresh() dispatches and
// returns, so the save() below genuinely races the request, which is exactly
// how it happens when someone opens a pairing link with the mail screen open.
// The reply must not repopulate the inbox with the previous account's mail,
// and the user -- who has just paired successfully -- must not be shown a
// refresh error for it.
void MailControllerTest::aRefreshThatOutlivesItsAccountWritesNothingAndReportsNoError()
{
    const QByteArray body = R"(
    {
      "tabs": ["Uncategorized"],
      "byTab": {
        "Uncategorized": [
          {
            "messageId": "m-previous-account",
            "sender": "hr@example.com",
            "sentTo": "bob@example.com",
            "cc": "",
            "bcc": "",
            "subject": "Salary review",
            "status": "unread",
            "atUtc": "2026-07-02T00:00:00Z",
            "hasAttachments": false,
            "label": ""
          }
        ]
      }
    }
    )";
    FakeRelayServer fake(httpResponse(200, "OK", body));

    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    controller.refresh();

    // The request is on the wire, authorised by sub-1/dev-1. Pair a different
    // account, as PairingController's completion handler does -- including
    // the cache purge that this reply must not undo.
    DevicePairing replacement;
    replacement.subscriberId = QStringLiteral("sub-2");
    replacement.deviceId = QStringLiteral("dev-2");
    replacement.deviceSecret = QStringLiteral("secret-2");
    replacement.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    QVERIFY(pairingStore.save(replacement));
    QVERIFY(emailDao.deleteAll());

    QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 5000);

    // The request really was made -- otherwise this test would pass on a
    // controller that simply never refreshed.
    QVERIFY(!fake.receivedRequest().isEmpty());

    QVERIFY2(!emailDao.findById(QStringLiteral("INBOX"), QStringLiteral("m-previous-account")).has_value(),
             "the previous account's mail was written into the new account's cache");
    auto* model = qobject_cast<EmailListModel*>(controller.emailModel());
    QVERIFY(model != nullptr);
    QCOMPARE(model->rowCount(), 0);

    // Nothing failed. Reporting "Refresh failed" here would be a lie told to
    // someone who just paired successfully.
    QVERIFY(controller.lastError().isEmpty());
}

// The download completes after the device has been re-paired.
//
// Not merely a file left on disk: the ephemeral branch of
// storeDownloadedAttachment() hands the file straight to the desktop to open,
// so proceeding would display the previous account's attachment to whoever is
// using the machine now.
void MailControllerTest::anAttachmentThatOutlivesItsAccountIsNeverWrittenOrOpened()
{
    QStandardPaths::setTestModeEnabled(true);

    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    const QByteArray attachmentBytes = "confidential-attachment-bytes";
    FakeRelayServer fake(httpResponse(200, "OK", attachmentBytes, "application/octet-stream"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    const QString downloadDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QDir().mkpath(downloadDir);
    const QString target = QDir(downloadDir).filePath(QStringLiteral("payslip.pdf"));
    QFile::remove(target);

    QSignalSpy spy(&controller, &MailController::attachmentDownloaded);
    controller.downloadAttachment(QStringLiteral("Inbox"), QStringLiteral("42"), 0,
                                   QStringLiteral("payslip.pdf"));

    DevicePairing replacement;
    replacement.subscriberId = QStringLiteral("sub-2");
    replacement.deviceId = QStringLiteral("dev-2");
    replacement.deviceSecret = QStringLiteral("secret-2");
    replacement.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    QVERIFY(pairingStore.save(replacement));

    QVERIFY(spy.wait(10000) || spy.count() > 0);
    executor.shutdown();

    QVERIFY(!spy.isEmpty());
    QCOMPARE(spy.at(0).at(2).toBool(), false);
    QVERIFY2(!QFile::exists(target), "the previous account's attachment was written to disk");
    // The request did go out -- otherwise this passes for the wrong reason.
    QVERIFY(!fake.receivedRequest().isEmpty());
}

// The bootstrap answer describes the PREVIOUS account's key custody, and it
// decides whether compose offers to sign and encrypt at all. Caching it
// against the new account would either hide those controls from someone
// entitled to them or offer them to someone whose server has no identity.
void MailControllerTest::aPgpComposeAnswerThatOutlivesItsAccountIsNotCached()
{
    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    FakeRelayServer fake(httpResponse(200, "OK",
                                       R"({"ok":true,"hasIdentity":true,"protection":"server"})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    QSignalSpy spy(&controller, &MailController::pgpComposeStateChanged);
    controller.refreshPgpComposeState();

    DevicePairing replacement;
    replacement.subscriberId = QStringLiteral("sub-2");
    replacement.deviceId = QStringLiteral("dev-2");
    replacement.deviceSecret = QStringLiteral("secret-2");
    replacement.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    QVERIFY(pairingStore.save(replacement));

    // The request really was made and really did come back; what must not
    // happen is the answer being applied.
    QTRY_VERIFY_WITH_TIMEOUT(!fake.receivedRequest().isEmpty(), 5000);
    QVERIFY(!spy.wait(1000));

    QCOMPARE(controller.pgpCanEncrypt(), false);
    QCOMPARE(controller.pgpCanSign(), false);
    executor.shutdown();
}

// Which recipients the PREVIOUS account had keys for says nothing about this
// one. A stale all-clear is the single outcome this warning must never show,
// so the answer is dropped and the list cleared rather than kept.
void MailControllerTest::aRecipientPreflightThatOutlivesItsAccountShowsNothing()
{
    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    FakeRelayServer fake(httpResponse(200, "OK",
                                       R"({"results":[{"address":"bob@example.com","hasKey":false}]})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    controller.preflightRecipients(QStringLiteral("bob@example.com"), QString(), QString());

    DevicePairing replacement;
    replacement.subscriberId = QStringLiteral("sub-2");
    replacement.deviceId = QStringLiteral("dev-2");
    replacement.deviceSecret = QStringLiteral("secret-2");
    replacement.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    QVERIFY(pairingStore.save(replacement));

    QTRY_VERIFY_WITH_TIMEOUT(!fake.receivedRequest().isEmpty(), 5000);
    QTest::qWait(500); // let any (wrong) apply land before asserting it did not
    QVERIFY(controller.pgpKeylessRecipients().isEmpty());
    executor.shutdown();
}

// THE CASE THIS EXISTS FOR, and it is the common one rather than the edge.
//
// A push notification is generated server-side the instant mail arrives; this
// client only learns of the message on its next sync, up to 90 seconds later
// on the polling tier. So "the user tapped View" almost always means a
// message this device has never seen. Both roots used to hydrate it from the
// cache, miss, and show a blank detail page -- with the mail one refresh away
// and nothing on screen saying so.
void MailControllerTest::aNotificationForUncachedMailRefreshesBeforeAnsweringIt()
{
    const QByteArray body = R"(
    {
      "tabs": ["Uncategorized"],
      "byTab": {
        "Uncategorized": [
          {
            "messageId": "m-just-arrived",
            "sender": "alice@example.com",
            "sentTo": "bob@example.com",
            "cc": "",
            "bcc": "",
            "subject": "Just arrived",
            "status": "unread",
            "atUtc": "2026-07-02T00:00:00Z",
            "hasAttachments": false,
            "label": ""
          }
        ]
      }
    }
    )";
    FakeRelayServer fake(httpResponse(200, "OK", body));

    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    // Nothing cached: exactly the state a fresh push arrives in.
    QVERIFY(!emailDao.findById(QStringLiteral("INBOX"), QStringLiteral("m-just-arrived")).has_value());

    QSignalSpy resolvedSpy(&controller, &MailController::notificationEmailResolved);
    controller.openFromNotification(QStringLiteral("m-just-arrived"));

    QVERIFY(resolvedSpy.wait(10000) || resolvedSpy.count() > 0);
    executor.shutdown();

    QCOMPARE(resolvedSpy.count(), 1);
    QCOMPARE(resolvedSpy.at(0).at(0).toString(), QStringLiteral("m-just-arrived"));
    // A real folder, so the roots open the message rather than a blank page.
    QCOMPARE(resolvedSpy.at(0).at(1).toString(), QStringLiteral("INBOX"));
    QVERIFY2(!fake.receivedRequest().isEmpty(), "no refresh was made for a message that was not cached");
    QVERIFY(controller.lastError().isEmpty());
}

// Already cached: answered straight away. The point is the ABSENCE of a
// request -- refreshing to learn what is already known would put a network
// round trip in front of every notification tap.
void MailControllerTest::aNotificationForCachedMailAnswersWithoutARequest()
{
    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    Email cached;
    cached.messageId = QStringLiteral("m-known");
    cached.folder = QStringLiteral("Archive");
    cached.subject = QStringLiteral("Already here");
    cached.atUtc = QStringLiteral("2026-07-01T00:00:00Z");
    QVERIFY(emailDao.insertOrReplace(cached));

    FakeRelayServer fake(httpResponse(200, "OK", R"({"tabs":[],"byTab":{}})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    QSignalSpy resolvedSpy(&controller, &MailController::notificationEmailResolved);
    controller.openFromNotification(QStringLiteral("m-known"));

    // Synchronous: no event loop was spun between the call and this line.
    QCOMPARE(resolvedSpy.count(), 1);
    QCOMPARE(resolvedSpy.at(0).at(1).toString(), QStringLiteral("Archive"));
    QVERIFY2(fake.receivedRequest().isEmpty(), "a cached message still cost a network round trip");
    executor.shutdown();
}

// The refresh happens and the message still is not there -- filed elsewhere
// by a server-side rule, or deleted from another client between the
// notification and the tap. Saying so beats the blank page they got before.
void MailControllerTest::aNotificationForMailThatCannotBeFoundSaysSoRatherThanOpeningNothing()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"tabs":[],"byTab":{}})"));

    Database db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    EmailDao emailDao(db.handle());

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QTemporaryDir cursorDir;
    QVERIFY(cursorDir.isValid());
    CursorStore cursorStore(cursorDir.filePath(QStringLiteral("cursor.ini")));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    KeywordRepository keywordRepository(settingsStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    PgpBootstrapClient bootstrapClient(http);
    PgpRecipientChecker recipientChecker(http);
    MailRepository mailRepository(source, emailDao, pairingStore, cursorStore);
    FolderDao folderDao(db.handle());
    FolderClient folderClient(http);
    FolderRepository folderRepository(folderClient, folderDao, pairingStore);

    NetworkExecutor executor(3000);
    MailController controller(mailRepository, source, keywordRepository, pairingStore, folderRepository,
                               settingsStore, bootstrapClient, recipientChecker, executor);
    ExecutorShutdownGuard shutdownGuard{ executor };

    QSignalSpy resolvedSpy(&controller, &MailController::notificationEmailResolved);
    controller.openFromNotification(QStringLiteral("m-vanished"));

    QVERIFY(resolvedSpy.wait(10000) || resolvedSpy.count() > 0);
    executor.shutdown();

    QCOMPARE(resolvedSpy.count(), 1);
    // Empty folder is the "do not open anything" answer both roots act on.
    QVERIFY(resolvedSpy.at(0).at(1).toString().isEmpty());
    QVERIFY2(!controller.lastError().isEmpty(),
             "a message that could not be found was reported as nothing at all");
}



namespace {

QByteArray readStateReply(const char* action, bool ok)
{
    return httpResponse(200, "OK",
                        ok ? QByteArray(R"({"ok":true,"action":")") + action
                                + R"(","processed":1,"failed":[],"targetMailbox":""})"
                           : QByteArray(R"({"ok":false,"action":")") + action
                                + R"(","processed":0,"failed":[{"messageId":"m1","error":"imap said no"}],"targetMailbox":""})");
}

QString cachedStatus(EmailDao& dao, const char* id)
{
    const std::optional<Email> email = dao.findById(QStringLiteral("INBOX"), QString::fromLatin1(id));
    return email.has_value() ? email->status : QStringLiteral("<missing>");
}

} // namespace

void MailControllerTest::markReadIsOptimisticAndCoalescesRepeats()
{
    FakeRelayServer fake(httpResponse(200, "OK", kTwoMessageInbox));
    RefreshFixture f(fake.port());
    f.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    auto* model = qobject_cast<EmailListModel*>(f.controller.emailModel());
    QCOMPARE(model->emailAt(1).messageId, QStringLiteral("m1")); // newest first: m2, m1
    QCOMPARE(model->emailAt(1).status, QStringLiteral("unread"));

    fake.setResponse(readStateReply("read", true));
    const int requestsBefore = fake.receivedRequests().size();
    QSignalSpy stateSpy(&f.controller, &MailController::readStateChanged);
    QSignalSpy resetSpy(model, &QAbstractItemModel::modelReset);

    f.controller.markRead(QStringLiteral("INBOX"), { QStringLiteral("m1") });

    // Before any reply: cache, list and signal already say read.
    QCOMPARE(cachedStatus(f.emailDao, "m1"), QStringLiteral("read"));
    QCOMPARE(model->emailAt(1).status, QStringLiteral("read"));
    QCOMPARE(stateSpy.count(), 1);
    QCOMPARE(stateSpy.at(0).at(2).toBool(), false);
    QCOMPARE(resetSpy.count(), 0);
    QVERIFY(!f.controller.isBusy()); // opening a message must not grey out the toolbar

    // Already read (and still in flight): nothing more is sent.
    f.controller.markRead(QStringLiteral("INBOX"), { QStringLiteral("m1") });
    QTRY_VERIFY_WITH_TIMEOUT(fake.receivedRequests().size() > requestsBefore, 10000);
    QVERIFY2(fake.receivedRequest().contains("\"action\":\"read\""), fake.receivedRequest().constData());
    QVERIFY2(fake.receivedRequest().contains("\"mailbox\":\"INBOX\""), fake.receivedRequest().constData());

    // Once the reply lands the message is no longer in flight, so the
    // opposite change goes through; until then it is refused.
    fake.setResponse(readStateReply("unread", true));
    QTRY_VERIFY_WITH_TIMEOUT((f.controller.markUnread(QStringLiteral("INBOX"), { QStringLiteral("m1") }),
                              stateSpy.count() == 2),
                             10000);
    QCOMPARE(cachedStatus(f.emailDao, "m1"), QStringLiteral("unread"));
    QCOMPARE(cachedStatus(f.emailDao, "m2"), QStringLiteral("unread"));
    f.executor.shutdown();
    QVERIFY(f.controller.lastError().isEmpty());
}

void MailControllerTest::aRefusedMarkReadRollsBackSilently()
{
    FakeRelayServer fake(httpResponse(200, "OK", kTwoMessageInbox));
    RefreshFixture f(fake.port());
    f.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    auto* model = qobject_cast<EmailListModel*>(f.controller.emailModel());

    fake.setResponse(httpResponse(502, "Bad Gateway", "upstream down"));
    QSignalSpy stateSpy(&f.controller, &MailController::readStateChanged);
    f.controller.markRead(QStringLiteral("INBOX"), { QStringLiteral("m1") });
    QCOMPARE(cachedStatus(f.emailDao, "m1"), QStringLiteral("read"));

    QTRY_VERIFY_WITH_TIMEOUT(stateSpy.count() == 2, 10000);
    f.executor.shutdown();
    QCOMPARE(stateSpy.at(1).at(2).toBool(), true);
    QCOMPARE(cachedStatus(f.emailDao, "m1"), QStringLiteral("unread"));
    QCOMPARE(model->emailAt(1).status, QStringLiteral("unread"));
    QCOMPARE(f.controller.unreadCounts().value(QStringLiteral("INBOX")).toInt(), 2);
    // Incidental to opening the message: no error banner for it.
    QVERIFY(f.controller.lastError().isEmpty());
}

void MailControllerTest::aRefusedMarkUnreadRollsBackAndSaysSo()
{
    FakeRelayServer fake(httpResponse(200, "OK", kTwoMessageInbox));
    RefreshFixture f(fake.port());
    f.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    QVERIFY(f.emailDao.updateStatus(QStringLiteral("INBOX"), QStringLiteral("m1"), QStringLiteral("read")));

    // ok:false with a per-message failure, the relay's partial-failure shape.
    fake.setResponse(readStateReply("unread", false));
    QSignalSpy stateSpy(&f.controller, &MailController::readStateChanged);
    f.controller.markUnread(QStringLiteral("INBOX"), { QStringLiteral("m1") });
    QCOMPARE(cachedStatus(f.emailDao, "m1"), QStringLiteral("unread"));

    QTRY_VERIFY_WITH_TIMEOUT(stateSpy.count() == 2, 10000);
    f.executor.shutdown();
    QCOMPARE(cachedStatus(f.emailDao, "m1"), QStringLiteral("read"));
    QVERIFY(!f.controller.lastError().isEmpty());
}

// The relay's refusal arrives after another account was paired. Its cache is
// not ours to roll back: message ids are per-mailbox UIDs and collide across
// accounts.
void MailControllerTest::aReadStateReplyForAReplacedAccountRollsNothingBack()
{
    FakeRelayServer fake(httpResponse(200, "OK", kTwoMessageInbox));
    RefreshFixture f(fake.port());
    f.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    QVERIFY(f.emailDao.updateStatus(QStringLiteral("INBOX"), QStringLiteral("m1"), QStringLiteral("read")));

    fake.setResponseForPath("/api/inbox/actions", readStateReply("unread", false));
    fake.setResponse(httpResponse(500, "Internal Server Error", "{}"));
    f.controller.markUnread(QStringLiteral("INBOX"), { QStringLiteral("m1") });

    DevicePairing replacement;
    replacement.subscriberId = QStringLiteral("sub-2");
    replacement.deviceId = QStringLiteral("dev-2");
    replacement.deviceSecret = QStringLiteral("secret-2");
    replacement.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    QVERIFY(f.pairingStore.save(replacement));

    // One executor thread: this refresh (which fails, writing nothing)
    // completes only after the action's reply has been handled.
    f.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    f.executor.shutdown();
    QVERIFY2(fake.receivedRequest().contains("/api/inbox/actions"), fake.receivedRequest().constData());
    QCOMPARE(cachedStatus(f.emailDao, "m1"), QStringLiteral("unread"));
}

// A reader opened behind the PIN overlay (a notification tap on a cached
// message) calls markRead before anyone has unlocked. Nothing may reach the
// relay, or the cache, until the app is unlocked.
void MailControllerTest::aLockedAppChangesNoReadStateUntilUnlocked()
{
    FakeRelayServer fake(httpResponse(200, "OK", kTwoMessageInbox));
    RefreshFixture f(fake.port());
    f.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    QVERIFY(f.emailDao.updateStatus(QStringLiteral("INBOX"), QStringLiteral("m2"), QStringLiteral("read")));
    auto* model = qobject_cast<EmailListModel*>(f.controller.emailModel());

    fake.setResponse(readStateReply("read", true));
    const int requestsBefore = fake.receivedRequests().size();
    QSignalSpy stateSpy(&f.controller, &MailController::readStateChanged);

    f.controller.setAppLocked(true);
    f.controller.markRead(QStringLiteral("INBOX"), { QStringLiteral("m1") });
    f.controller.markUnread(QStringLiteral("INBOX"), { QStringLiteral("m2") });
    QCOMPARE(stateSpy.count(), 0);
    QCOMPARE(cachedStatus(f.emailDao, "m1"), QStringLiteral("unread"));
    QCOMPARE(cachedStatus(f.emailDao, "m2"), QStringLiteral("read"));
    QCOMPARE(model->emailAt(1).status, QStringLiteral("unread"));
    // Give a dispatched request every chance to arrive before saying none did.
    QTest::qWait(300);
    QCOMPARE(fake.receivedRequests().size(), requestsBefore);

    f.controller.setAppLocked(false);
    f.controller.markRead(QStringLiteral("INBOX"), { QStringLiteral("m1") });
    QCOMPARE(cachedStatus(f.emailDao, "m1"), QStringLiteral("read"));
    QTRY_VERIFY_WITH_TIMEOUT(fake.receivedRequests().size() > requestsBefore, 10000);
    f.executor.shutdown();
    QVERIFY2(fake.receivedRequest().contains("\"action\":\"read\""), fake.receivedRequest().constData());
}

namespace {
QJsonObject jsonBodyOf(const QByteArray& request)
{
    const int headerEnd = request.indexOf("\r\n\r\n");
    return headerEnd < 0 ? QJsonObject() : QJsonDocument::fromJson(request.mid(headerEnd + 4)).object();
}

QByteArray folderListing(const char* parent, const char* foldersJson)
{
    return httpResponse(200, "OK",
                        QByteArray(R"({"parent":")") + parent + R"(","folders":)" + foldersJson + "}");
}
} // namespace

// The relay's field is `targetMailbox` (server_inbox.go handleInboxActions);
// a wrong name is a 400 "targetMailbox is required for move action".
namespace {

void cacheEmail(EmailDao& dao, const char* folder, const char* id, const char* status,
                const QStringList& keywords = {})
{
    Email email;
    email.folder = QString::fromLatin1(folder);
    email.messageId = QString::fromLatin1(id);
    email.status = QString::fromLatin1(status);
    email.keywords = keywords;
    QVERIFY(dao.insertOrReplace(email));
}

int keywordUnread(const MailController& controller, const QString& keyword)
{
    for (const QVariant& tab : controller.keywordTabs()) {
        if (tab.toMap().value(QStringLiteral("name")).toString() == keyword)
            return tab.toMap().value(QStringLiteral("unread")).toInt();
    }
    return -1;
}

} // namespace

// Android #189: COUNT of status='unread' rows per cached folder. Archive adds
// its subfolders; Drafts, Sent and Snoozed show no count at all.
void MailControllerTest::unreadCountsComeFromTheCacheWithArchiveTotallingItsTree()
{
    FakeRelayServer fake(QByteArray{});
    RefreshFixture f(fake.port());
    const QStringList work{ QStringLiteral("Work") };
    cacheEmail(f.emailDao, "INBOX", "1", "unread", work);
    cacheEmail(f.emailDao, "INBOX", "2", "unread");
    cacheEmail(f.emailDao, "INBOX", "3", "read", work);
    cacheEmail(f.emailDao, "Junk", "4", "unread");
    cacheEmail(f.emailDao, "Archive", "5", "unread");
    cacheEmail(f.emailDao, "Archive/a", "6", "unread");
    cacheEmail(f.emailDao, "Archive/a", "7", "unread");
    cacheEmail(f.emailDao, "Archive/a/b", "8", "unread");
    cacheEmail(f.emailDao, "Archive/gone", "9", "unread"); // not in the folder tree
    cacheEmail(f.emailDao, "Drafts", "10", "unread");
    cacheEmail(f.emailDao, "Sent", "11", "unread");
    cacheEmail(f.emailDao, "Snoozed", "12", "unread");
    QVERIFY(f.folderDao.insertOrReplace(QStringLiteral("Archive/a"), QStringLiteral("Archive"), true, QString()));
    QVERIFY(f.folderDao.insertOrReplace(QStringLiteral("Archive/a/b"), QStringLiteral("Archive/a"), true, QString()));

    QSignalSpy changedSpy(&f.controller, &MailController::unreadCountsChanged);
    f.controller.reloadCurrentFolder();
    QCOMPARE(changedSpy.count(), 1);
    f.controller.reloadCurrentFolder();
    QCOMPARE(changedSpy.count(), 1); // unchanged counts do not repaint

    const QVariantMap counts = f.controller.unreadCounts();
    QCOMPARE(counts.value(QStringLiteral("INBOX")).toInt(), 2);
    QCOMPARE(counts.value(QStringLiteral("Junk")).toInt(), 1);
    QCOMPARE(counts.value(QStringLiteral("Archive")).toInt(), 4);
    QCOMPARE(counts.value(QStringLiteral("Archive/a")).toInt(), 2);
    QCOMPARE(counts.value(QStringLiteral("Archive/a/b")).toInt(), 1);
    QVERIFY(!counts.contains(QStringLiteral("Drafts")));
    QVERIFY(!counts.contains(QStringLiteral("Sent")));
    QVERIFY(!counts.contains(QStringLiteral("Snoozed")));

    // The "All" tab and the keyword tabs count the folder on screen.
    QCOMPARE(f.controller.currentFolderUnread(), 2);
    QCOMPARE(keywordUnread(f.controller, QStringLiteral("Work")), 1);
}

void MailControllerTest::readStateChangesUpdateTheUnreadCounts()
{
    FakeRelayServer fake(httpResponse(200, "OK", kTwoMessageInbox));
    RefreshFixture f(fake.port());
    f.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    QCOMPARE(f.controller.unreadCounts().value(QStringLiteral("INBOX")).toInt(), 2);
    QCOMPARE(keywordUnread(f.controller, QStringLiteral("Work")), 1);

    fake.setResponse(readStateReply("read", true));
    QSignalSpy countsSpy(&f.controller, &MailController::unreadCountsChanged);
    QSignalSpy tabsSpy(&f.controller, &MailController::keywordTabsChanged);
    f.controller.markRead(QStringLiteral("INBOX"), { QStringLiteral("m1") });
    QCOMPARE(countsSpy.count(), 1);
    QVERIFY(tabsSpy.count() >= 1);
    QCOMPARE(f.controller.unreadCounts().value(QStringLiteral("INBOX")).toInt(), 1);
    QCOMPARE(f.controller.currentFolderUnread(), 1);
    QCOMPARE(keywordUnread(f.controller, QStringLiteral("Work")), 0);

    fake.setResponse(readStateReply("unread", true));
    QTRY_VERIFY_WITH_TIMEOUT((f.controller.markUnread(QStringLiteral("INBOX"), { QStringLiteral("m1") }),
                              countsSpy.count() == 2),
                             10000);
    f.executor.shutdown();
    QCOMPARE(f.controller.unreadCounts().value(QStringLiteral("INBOX")).toInt(), 2);
    QCOMPARE(keywordUnread(f.controller, QStringLiteral("Work")), 1);
}

void MailControllerTest::aMoveDropsTheSourceCopyFromTheUnreadCounts()
{
    FakeRelayServer fake(httpResponse(200, "OK", kTwoMessageInbox));
    fake.setResponseForPath("/api/inbox/actions", httpResponse(
        200, "OK", R"({"ok":true,"action":"move","processed":1,"failed":[],"targetMailbox":"Archive"})"));
    RefreshFixture f(fake.port());
    f.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    QCOMPARE(f.controller.unreadCounts().value(QStringLiteral("INBOX")).toInt(), 2);

    QSignalSpy doneSpy(&f.controller, &MailController::actionCompleted);
    f.controller.moveEmails({ QStringLiteral("m1") }, QStringLiteral("Archive"));
    QTRY_VERIFY_WITH_TIMEOUT(doneSpy.count() == 1, 10000);
    f.executor.shutdown();
    QCOMPARE(doneSpy.at(0).at(2).toBool(), true);
    QVERIFY(!f.emailDao.findById(QStringLiteral("INBOX"), QStringLiteral("m1")).has_value());
    QCOMPARE(f.controller.unreadCounts().value(QStringLiteral("INBOX")).toInt(), 1);
    QCOMPARE(f.controller.currentFolderUnread(), 1);
}

// The move landed after another account was paired. Its INBOX may hold the
// same UID, which is not the message the relay just moved.
void MailControllerTest::aMoveReplyForAReplacedAccountLeavesTheCacheAlone()
{
    FakeRelayServer fake(httpResponse(200, "OK", kTwoMessageInbox));
    RefreshFixture f(fake.port());
    f.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);

    fake.setResponseForPath("/api/inbox/actions", httpResponse(
        200, "OK", R"({"ok":true,"action":"move","processed":1,"failed":[],"targetMailbox":"Archive"})"));
    QSignalSpy doneSpy(&f.controller, &MailController::actionCompleted);
    f.controller.moveEmails({ QStringLiteral("m1") }, QStringLiteral("Archive"));

    DevicePairing replacement;
    replacement.subscriberId = QStringLiteral("sub-2");
    replacement.deviceId = QStringLiteral("dev-2");
    replacement.deviceSecret = QStringLiteral("secret-2");
    replacement.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    QVERIFY(f.pairingStore.save(replacement));

    QTRY_VERIFY_WITH_TIMEOUT(doneSpy.count() == 1, 10000);
    f.executor.shutdown();
    QCOMPARE(doneSpy.at(0).at(2).toBool(), true);
    QVERIFY(f.emailDao.findById(QStringLiteral("INBOX"), QStringLiteral("m1")).has_value());
}

// A pairing replacement purges the cache; the hook in main.cpp then reloads,
// and nothing of the previous account's counts may survive it.
void MailControllerTest::aPurgedCacheShowsNoUnreadCounts()
{
    FakeRelayServer fake(httpResponse(200, "OK", kTwoMessageInbox));
    RefreshFixture f(fake.port());
    f.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    f.executor.shutdown();
    QCOMPARE(f.controller.unreadCounts().value(QStringLiteral("INBOX")).toInt(), 2);

    QVERIFY(f.emailDao.deleteAll());
    f.controller.reloadCurrentFolder();
    QCOMPARE(f.controller.unreadCounts().value(QStringLiteral("INBOX")).toInt(), 0);
    QCOMPARE(f.controller.unreadCounts().value(QStringLiteral("Archive")).toInt(), 0);
    QCOMPARE(f.controller.currentFolderUnread(), 0);
    QVERIFY(f.controller.keywordTabs().isEmpty());
}

void MailControllerTest::moveEmailsSendsTheTargetMailboxAndDropsTheRow()
{
    FakeRelayServer fake(httpResponse(200, "OK", kTwoMessageInbox));
    RefreshFixture f(fake.port());

    f.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    auto* model = qobject_cast<EmailListModel*>(f.controller.emailModel());
    QCOMPARE(model->rowCount(), 2);

    fake.setResponse(httpResponse(
        200, "OK", R"({"ok":true,"action":"move","processed":1,"failed":[],"targetMailbox":"Archive/Projects"})"));

    QSignalSpy doneSpy(&f.controller, &MailController::actionCompleted);
    f.controller.moveEmails({ QStringLiteral("m1") }, QStringLiteral("Archive/Projects"));
    QVERIFY(f.controller.isBusy());
    QTRY_VERIFY_WITH_TIMEOUT(doneSpy.count() == 1, 10000);
    f.executor.shutdown();

    QCOMPARE(doneSpy.at(0).at(0).toString(), QStringLiteral("move"));
    QCOMPARE(doneSpy.at(0).at(1).toStringList(), QStringList{ QStringLiteral("m1") });
    QCOMPARE(doneSpy.at(0).at(2).toBool(), true);

    QVERIFY(fake.receivedRequests().last().startsWith("POST /api/inbox/actions "));
    const QJsonObject body = jsonBodyOf(fake.receivedRequests().last());
    QCOMPARE(body.value(QStringLiteral("action")).toString(), QStringLiteral("move"));
    QCOMPARE(body.value(QStringLiteral("mailbox")).toString(), QStringLiteral("INBOX"));
    QCOMPARE(body.value(QStringLiteral("targetMailbox")).toString(), QStringLiteral("Archive/Projects"));
    QCOMPARE(body.value(QStringLiteral("messageIds")).toArray(), QJsonArray{ QStringLiteral("m1") });

    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(model->emailAt(0).messageId, QStringLiteral("m2"));
}

void MailControllerTest::aRefusedMoveKeepsTheRowAndReportsWhy()
{
    FakeRelayServer fake(httpResponse(200, "OK", kTwoMessageInbox));
    RefreshFixture f(fake.port());

    f.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    auto* model = qobject_cast<EmailListModel*>(f.controller.emailModel());
    QCOMPARE(model->rowCount(), 2);

    fake.setResponse(httpResponse(400, "Bad Request", "invalid mailbox name"));

    QSignalSpy doneSpy(&f.controller, &MailController::actionCompleted);
    f.controller.moveEmails({ QStringLiteral("m1") }, QStringLiteral("Archive/Projects"));
    QTRY_VERIFY_WITH_TIMEOUT(doneSpy.count() == 1, 10000);
    f.executor.shutdown();

    QCOMPARE(doneSpy.at(0).at(2).toBool(), false);
    QVERIFY(!f.controller.isBusy());
    QVERIFY(!f.controller.lastError().isEmpty());
    QCOMPARE(model->rowCount(), 2);
}

// Ids repeat across mailboxes, so after switching to the move's target the
// moved copy shares its id with the row being removed. The reply must not
// hide it from the folder now on screen.
void MailControllerTest::aMoveThatLandsAfterAFolderSwitchLeavesTheNewFolderAlone()
{
    FakeRelayServer fake(httpResponse(200, "OK", kTwoMessageInbox));
    fake.setResponseForPath("/api/inbox/actions", httpResponse(
        200, "OK", R"({"ok":true,"action":"move","processed":1,"failed":[],"targetMailbox":"Archive"})"));
    RefreshFixture f(fake.port());
    auto* model = qobject_cast<EmailListModel*>(f.controller.emailModel());

    // Seed both caches so the switch shows rows synchronously.
    f.controller.selectFolder(QStringLiteral("Archive"));
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    f.controller.selectFolder(QStringLiteral("INBOX"));
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    QCOMPARE(model->rowCount(), 2);

    int rowsWhenDone = -1;
    connect(&f.controller, &MailController::actionCompleted, &f.controller,
            [&] { rowsWhenDone = model->rowCount(); });
    f.controller.moveEmails({ QStringLiteral("m1") }, QStringLiteral("Archive"));
    f.controller.selectFolder(QStringLiteral("Archive"));
    QCOMPARE(model->rowCount(), 2);

    QTRY_VERIFY_WITH_TIMEOUT(rowsWhenDone != -1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    f.executor.shutdown();
    QCOMPARE(rowsWhenDone, 2);
}

// snooze.go: {action:"snooze", messageIds, mailbox, wakeAt}. The relay moves
// the message to Snoozed, so the row leaves the folder like a move.
void MailControllerTest::snoozeEmailsSendsWakeAtAndDropsTheRow()
{
    FakeRelayServer fake(httpResponse(200, "OK", kTwoMessageInbox));
    RefreshFixture f(fake.port());

    f.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    auto* model = qobject_cast<EmailListModel*>(f.controller.emailModel());
    QCOMPARE(model->rowCount(), 2);

    fake.setResponse(httpResponse(
        200, "OK", R"({"ok":true,"action":"snooze","processed":1,"failed":[],"targetMailbox":""})"));

    const QDateTime wakeAt = QDateTime::currentDateTimeUtc().addDays(1);
    QSignalSpy doneSpy(&f.controller, &MailController::actionCompleted);
    f.controller.snoozeEmails({ QStringLiteral("m1") }, wakeAt);
    QTRY_VERIFY_WITH_TIMEOUT(doneSpy.count() == 1, 10000);
    f.executor.shutdown();

    QCOMPARE(doneSpy.at(0).at(0).toString(), QStringLiteral("snooze"));
    QCOMPARE(doneSpy.at(0).at(2).toBool(), true);

    const QJsonObject body = jsonBodyOf(fake.receivedRequests().last());
    QCOMPARE(body.value(QStringLiteral("action")).toString(), QStringLiteral("snooze"));
    QCOMPARE(body.value(QStringLiteral("mailbox")).toString(), QStringLiteral("INBOX"));
    QCOMPARE(body.value(QStringLiteral("messageIds")).toArray(), QJsonArray{ QStringLiteral("m1") });
    QCOMPARE(body.value(QStringLiteral("wakeAt")).toString(), wakeAt.toString(Qt::ISODate));
    QVERIFY(!body.contains(QStringLiteral("targetMailbox")));

    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(model->emailAt(0).messageId, QStringLiteral("m2"));
}

// The relay would answer 400; refusing here keeps the row and says why
// without a round trip. A picker left open past its time lands here.
void MailControllerTest::snoozeEmailsRefusesATimeOutsideTheWindowWithoutARequest()
{
    FakeRelayServer fake(httpResponse(
        200, "OK", R"({"ok":true,"action":"snooze","processed":1,"failed":[],"targetMailbox":""})"));
    RefreshFixture f(fake.port());
    QSignalSpy doneSpy(&f.controller, &MailController::actionCompleted);

    f.controller.snoozeEmails({ QStringLiteral("m1") }, QDateTime::currentDateTime().addSecs(-60));
    f.controller.snoozeEmails({ QStringLiteral("m1") }, QDateTime::currentDateTime().addDays(400));
    f.controller.snoozeEmails({ QStringLiteral("m1") }, QDateTime());
    QTest::qWait(300);

    QCOMPARE(fake.connectionCount(), 0);
    QCOMPARE(doneSpy.count(), 3);
    for (const QList<QVariant>& args : doneSpy) {
        QCOMPARE(args.at(0).toString(), QStringLiteral("snooze"));
        QCOMPARE(args.at(2).toBool(), false);
    }
    QVERIFY(!f.controller.lastError().isEmpty());
}

// No unsnooze verb on the relay: a move out of Snoozed is the unsnooze, and
// its wake record is dropped at the old wake time (processor/snooze.go).
void MailControllerTest::unsnoozeIsAMoveFromSnoozedToInbox()
{
    FakeRelayServer fake(httpResponse(200, "OK", kTwoMessageInbox));
    RefreshFixture f(fake.port());

    f.controller.selectFolder(QStringLiteral("Snoozed"));
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    auto* model = qobject_cast<EmailListModel*>(f.controller.emailModel());
    QCOMPARE(model->rowCount(), 2);

    fake.setResponse(httpResponse(
        200, "OK", R"({"ok":true,"action":"move","processed":1,"failed":[],"targetMailbox":"INBOX"})"));
    QSignalSpy doneSpy(&f.controller, &MailController::actionCompleted);
    f.controller.unsnoozeEmails({ QStringLiteral("m1") });
    QTRY_VERIFY_WITH_TIMEOUT(doneSpy.count() == 1, 10000);
    f.executor.shutdown();

    QCOMPARE(doneSpy.at(0).at(0).toString(), QStringLiteral("move"));
    QCOMPARE(doneSpy.at(0).at(2).toBool(), true);
    const QJsonObject body = jsonBodyOf(fake.receivedRequests().last());
    QCOMPARE(body.value(QStringLiteral("action")).toString(), QStringLiteral("move"));
    QCOMPARE(body.value(QStringLiteral("mailbox")).toString(), QStringLiteral("Snoozed"));
    QCOMPARE(body.value(QStringLiteral("targetMailbox")).toString(), QStringLiteral("INBOX"));
    QVERIFY(!body.contains(QStringLiteral("wakeAt")));
    QCOMPARE(model->rowCount(), 1);
}

void MailControllerTest::unsnoozeOutsideSnoozedSendsNothing()
{
    FakeRelayServer fake(httpResponse(
        200, "OK", R"({"ok":true,"action":"move","processed":1,"failed":[],"targetMailbox":"INBOX"})"));
    RefreshFixture f(fake.port());
    QSignalSpy doneSpy(&f.controller, &MailController::actionCompleted);

    QCOMPARE(f.controller.currentFolder(), QStringLiteral("INBOX"));
    f.controller.unsnoozeEmails({ QStringLiteral("m1") });
    QTest::qWait(300);

    QCOMPARE(fake.connectionCount(), 0);
    QCOMPARE(doneSpy.count(), 1);
    QCOMPARE(doneSpy.at(0).at(2).toBool(), false);
}

void MailControllerTest::mailFoldersListsSnoozedLast()
{
    FakeRelayServer fake(httpResponse(200, "OK", "{}"));
    RefreshFixture f(fake.port());

    const QVariantMap last = f.controller.mailFolders().last().toMap();
    QCOMPARE(last.value(QStringLiteral("wireName")).toString(), QStringLiteral("Snoozed"));
    QCOMPARE(last.value(QStringLiteral("depth")).toInt(), 0);
    QCOMPARE(last.value(QStringLiteral("deletable")).toBool(), false);
    QCOMPARE(f.controller.snoozedFolder(), QStringLiteral("Snoozed"));
}

void MailControllerTest::refreshFoldersWalksTheArchiveTree()
{
    FakeRelayServer fake(folderListing("", "[]"));
    fake.setResponseForPath("parent=Archive%2Fa%2Fb ",
                            folderListing("Archive/a/b", "[]"));
    fake.setResponseForPath("parent=Archive%2Fa ",
                            folderListing("Archive/a", R"([{"path":"Archive/a/b","deletable":true}])"));
    fake.setResponseForPath("parent=Archive ",
                            folderListing("Archive", R"([{"path":"Archive/a","deletable":true}])"));
    RefreshFixture f(fake.port());

    QSignalSpy changedSpy(&f.controller, &MailController::foldersChanged);
    f.controller.refreshFolders();
    f.controller.refreshFolders(); // coalesced: a walk is already running
    QTRY_VERIFY_WITH_TIMEOUT(changedSpy.count() == 1, 10000);
    f.executor.shutdown();
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(fake.connectionCount(), 4); // the top level, then Archive's three

    QStringList paths;
    QList<int> depths;
    for (const QVariant& v : f.controller.mailFolders()) {
        const QVariantMap m = v.toMap();
        paths << m.value(QStringLiteral("wireName")).toString();
        depths << m.value(QStringLiteral("depth")).toInt();
    }
    const qsizetype archive = paths.indexOf(QStringLiteral("Archive"));
    QVERIFY(archive >= 0);
    QCOMPARE(paths.mid(archive, 3), (QStringList{ QStringLiteral("Archive"), QStringLiteral("Archive/a"), QStringLiteral("Archive/a/b") }));
    QCOMPARE(depths.mid(archive, 3), (QList<int>{ 0, 1, 2 }));
}

// A relay that answers every listing with a child must not walk forever.
void MailControllerTest::refreshFoldersStopsAtTheDepthCap()
{
    FakeRelayServer fake(folderListing("Archive", R"([{"path":"Archive/loop","deletable":true}])"));
    RefreshFixture f(fake.port());

    QSignalSpy changedSpy(&f.controller, &MailController::foldersChanged);
    f.controller.refreshFolders();
    // Emitted only once nothing is in flight, so the walk has ended.
    QTRY_VERIFY_WITH_TIMEOUT(changedSpy.count() == 1, 10000);
    f.executor.shutdown();
    // The top-level listing is one level only, whatever it answers.
    QCOMPARE(fake.connectionCount(), MailController::kMaxFolderDepth + 1);
}

// A German account: the relay flags its Sent and Drafts by RFC 6154 role.
void MailControllerTest::refreshFoldersPlacesALocalizedSentAndDraftsByRole()
{
    FakeRelayServer fake(folderListing("", R"([
        {"path":"INBOX.Gesendet","deletable":false,"special":"sent"},
        {"path":"INBOX.Entw&APw-rfe","deletable":false,"special":"drafts"},
        {"path":"Sent","deletable":false}])"));
    fake.setResponseForPath("parent=Archive ", folderListing("Archive", "[]"));
    RefreshFixture f(fake.port());
    QCOMPARE(f.controller.draftsFolder(), QStringLiteral("Drafts"));

    QSignalSpy changedSpy(&f.controller, &MailController::foldersChanged);
    f.controller.refreshFolders();
    QTRY_VERIFY_WITH_TIMEOUT(changedSpy.count() == 1, 10000);
    f.executor.shutdown();

    QStringList paths;
    for (const QVariant& v : f.controller.mailFolders())
        paths << v.toMap().value(QStringLiteral("wireName")).toString();
    QCOMPARE(paths, (QStringList{ QStringLiteral("INBOX"), QStringLiteral("INBOX.Entw&APw-rfe"),
                                  QStringLiteral("Junk"), QStringLiteral("INBOX.Gesendet"),
                                  QStringLiteral("Trash"), QStringLiteral("Archive"),
                                  QStringLiteral("Snoozed") }));
    QCOMPARE(f.controller.draftsFolder(), QStringLiteral("INBOX.Entw&APw-rfe"));
    QCOMPARE(f.controller.archiveFolder(), QStringLiteral("Archive"));
}

void MailControllerTest::mailFoldersShowsNoDeeperThanTheCap()
{
    FakeRelayServer fake(httpResponse(200, "OK", "{}"));
    RefreshFixture f(fake.port());

    QString parent = QStringLiteral("Archive");
    for (int level = 1; level <= MailController::kMaxFolderDepth + 2; ++level) {
        const QString path = parent + QStringLiteral("/l%1").arg(level);
        QVERIFY(f.folderDao.insertOrReplace(path, parent, true, QStringLiteral("relay")));
        parent = path;
    }

    int deepest = 0;
    for (const QVariant& v : f.controller.mailFolders())
        deepest = qMax(deepest, v.toMap().value(QStringLiteral("depth")).toInt());
    QCOMPARE(deepest, MailController::kMaxFolderDepth);
}

// A create/rename prompt can be left open when the app locks. Whatever still
// reaches the controller from it must send nothing until unlock, and must not
// report back either: an error under the PIN overlay would leak the attempt.
void MailControllerTest::aLockedAppSendsNoFolderMutation()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"parent":"Archive","folder":"Archive/New"})"));
    RefreshFixture f(fake.port());
    QSignalSpy completedSpy(&f.controller, &MailController::folderMutationCompleted);

    f.controller.setAppLocked(true);
    f.controller.createFolder(QStringLiteral("Archive"), QStringLiteral("New"));
    f.controller.renameFolder(QStringLiteral("Archive/Old"), QStringLiteral("New"));
    f.controller.deleteFolder(QStringLiteral("Archive/Old"));
    QVERIFY(!f.controller.isBusy());
    // Give a dispatched request every chance to arrive before saying none did.
    QTest::qWait(300);
    QCOMPARE(fake.connectionCount(), 0);
    QCOMPARE(completedSpy.count(), 0);

    f.controller.setAppLocked(false);
    f.controller.createFolder(QStringLiteral("Archive"), QStringLiteral("New"));
    QTRY_VERIFY_WITH_TIMEOUT(completedSpy.count() == 1, 10000);
    f.executor.shutdown();
    QVERIFY(fake.connectionCount() > 0);
    QVERIFY2(fake.receivedRequests().first().startsWith("POST "), fake.receivedRequests().first().constData());
}

// Archive, delete, spam and move reach the relay from a reader or a move
// picker that can sit behind the PIN overlay. None of them may go out while
// locked; each reports failure so a latched swipe row lets go.
void MailControllerTest::aLockedAppSendsNoMailboxAction()
{
    FakeRelayServer fake(httpResponse(
        200, "OK", R"({"ok":true,"action":"archive","processed":1,"failed":[],"targetMailbox":""})"));
    RefreshFixture f(fake.port());
    QSignalSpy doneSpy(&f.controller, &MailController::actionCompleted);

    f.controller.setAppLocked(true);
    f.controller.archiveEmails({ QStringLiteral("m1") });
    f.controller.deleteEmails({ QStringLiteral("m1") });
    f.controller.markSpam({ QStringLiteral("m1") });
    f.controller.moveEmails({ QStringLiteral("m1") }, QStringLiteral("Archive"));
    f.controller.snoozeEmails({ QStringLiteral("m1") }, QDateTime::currentDateTime().addDays(1));
    QVERIFY(!f.controller.isBusy());
    // Give a dispatched request every chance to arrive before saying none did.
    QTest::qWait(300);
    QCOMPARE(fake.connectionCount(), 0);
    QCOMPARE(doneSpy.count(), 5);
    for (const QList<QVariant>& args : doneSpy)
        QCOMPARE(args.at(2).toBool(), false);
    QVERIFY(f.controller.lastError().isEmpty());

    f.controller.setAppLocked(false);
    doneSpy.clear();
    f.controller.archiveEmails({ QStringLiteral("m1") });
    QTRY_VERIFY_WITH_TIMEOUT(doneSpy.count() == 1, 10000);
    f.executor.shutdown();
    QCOMPARE(doneSpy.at(0).at(2).toBool(), true);
    QVERIFY(fake.connectionCount() > 0);
    QVERIFY2(fake.receivedRequests().last().contains("\"archive\""), fake.receivedRequests().last().constData());
}

namespace {

QByteArray searchReply(const char* id, const char* subject, const char* status = "read")
{
    return httpResponse(200, "OK", QByteArray(R"({"results":[{"messageId":")") + id
                                       + R"(","sender":"x@example.com","subject":")" + subject
                                       + R"(","status":")" + status + R"(","atUtc":"2026-07-03T00:00:00Z"}]})");
}

int requestsMatching(const FakeRelayServer& fake, const QByteArray& needle)
{
    int count = 0;
    for (const QByteArray& request : fake.receivedRequests())
        count += request.contains(needle) ? 1 : 0;
    return count;
}

QByteArray firstRequestMatching(const FakeRelayServer& fake, const QByteArray& needle)
{
    for (const QByteArray& request : fake.receivedRequests()) {
        if (request.contains(needle))
            return request;
    }
    return {};
}

const char* const kDraftsInbox = R"({"tabs":["Uncategorized"],"byTab":{"Uncategorized":[
  {"messageId":"d1","sender":"me@example.com","sentTo":"\"Doe, Jane\" <jane@example.com>","cc":"cc@example.com",
   "bcc":"bcc@example.com","subject":"Half done","body":"line one\nline <two>","bodyMode":"plain",
   "status":"read","atUtc":"2026-07-01T00:00:00Z","label":""}]}})";

// Answers every endpoint a reopened draft touches; tests override what they test.
void serveDraftsAccount(FakeRelayServer& fake)
{
    fake.setResponseForPath("/api/inbox/actions",
                            httpResponse(200, "OK", R"({"ok":true,"action":"delete","processed":1,"failed":[],"targetMailbox":""})"));
    fake.setResponseForPath("GET /api/inbox?", httpResponse(200, "OK", kDraftsInbox));
    fake.setResponseForPath("/api/pgp/bootstrap",
                            httpResponse(200, "OK", R"({"ok":true,"hasIdentity":true,"protection":"server"})"));
    fake.setResponseForPath("/api/mail/send", httpResponse(200, "OK", R"({"ok":true,"sentSaved":true,"warning":""})"));
}

} // namespace

void MailControllerTest::searchIsDebouncedAndSendsOnlyTheLastQuery()
{
    FakeRelayServer fake(searchReply("s1", "Invoice"));
    RefreshFixture f(fake.port());
    auto* results = qobject_cast<EmailListModel*>(f.controller.searchModel());

    f.controller.setSearchQuery(QStringLiteral("i"));
    f.controller.setSearchQuery(QStringLiteral("in"));
    f.controller.setSearchQuery(QStringLiteral("inv"));
    QVERIFY(f.controller.searchActive());
    QVERIFY(f.controller.searchBusy()); // set synchronously, before any request
    QCOMPARE(fake.connectionCount(), 0);

    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.searchBusy(), 10000);
    QCOMPARE(fake.receivedRequests().size(), 1);
    QVERIFY2(fake.receivedRequest().contains("GET /api/mail/search?q=inv&"), fake.receivedRequest().constData());
    QVERIFY(fake.receivedRequest().contains("mailbox=INBOX"));
    QCOMPARE(results->rowCount(), 1);
    QCOMPARE(results->emailAt(0).folder, QStringLiteral("INBOX"));

    // Text the relay would refuse clears the results and sends nothing.
    f.controller.setSearchQuery(QStringLiteral("   "));
    QVERIFY(!f.controller.searchActive());
    QVERIFY(!f.controller.searchBusy());
    QCOMPARE(results->rowCount(), 0);
    QTest::qWait(MailController::kSearchDebounceMs + 100);
    QCOMPARE(fake.receivedRequests().size(), 1);
}

// The query changes while the first request is already out. Its reply must
// not be painted, even briefly, under the newer query.
void MailControllerTest::aStaleSearchReplyIsNeverShown()
{
    FakeRelayServer fake(httpResponse(500, "Internal Server Error", "unexpected"));
    fake.setResponseForPath("q=first", searchReply("old", "Stale"));
    fake.setResponseForPath("q=second", searchReply("new", "Fresh"));
    RefreshFixture f(fake.port());
    auto* results = qobject_cast<EmailListModel*>(f.controller.searchModel());

    QStringList shown;
    connect(results, &QAbstractItemModel::modelReset, &f.controller, [&] {
        for (int row = 0; row < results->rowCount(); ++row)
            shown << results->emailAt(row).subject;
    });

    f.controller.setSearchQuery(QStringLiteral("first"));
    // The next searchChanged is startSearch() marking the request in flight.
    bool switched = false;
    connect(&f.controller, &MailController::searchChanged, &f.controller, [&] {
        if (!switched) {
            switched = true;
            f.controller.setSearchQuery(QStringLiteral("second"));
        }
    });

    QTRY_VERIFY_WITH_TIMEOUT(requestsMatching(fake, "q=second") == 1 && !f.controller.searchBusy(), 10000);
    f.executor.shutdown();
    QCOMPARE(requestsMatching(fake, "q=first"), 1); // it really went out and came back
    QCOMPARE(shown, QStringList{ QStringLiteral("Fresh") });
    QVERIFY(f.controller.lastError().isEmpty());
}

void MailControllerTest::aSearchHitOpensFromMemoryAndNeverReachesTheCache()
{
    FakeRelayServer fake(searchReply("s1", "Found"));
    fake.setResponseForPath("/api/mail/body", httpResponse(200, "OK", R"({"body":"<p>found</p>","bodyMode":"html"})"));
    RefreshFixture f(fake.port());

    f.controller.setSearchQuery(QStringLiteral("found"));
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.searchBusy(), 10000);

    QVariantMap email = f.controller.findByMessageId(QStringLiteral("s1"), QStringLiteral("INBOX"));
    QCOMPARE(email.value(QStringLiteral("subject")).toString(), QStringLiteral("Found"));
    QVERIFY(email.value(QStringLiteral("searchOnly")).toBool());
    QVERIFY(email.value(QStringLiteral("bodyPending")).toBool());

    QSignalSpy loaded(&f.controller, &MailController::searchBodyLoaded);
    f.controller.loadSearchBody(QStringLiteral("INBOX"), QStringLiteral("s1"));
    f.controller.loadSearchBody(QStringLiteral("INBOX"), QStringLiteral("s1")); // coalesced
    QTRY_VERIFY_WITH_TIMEOUT(loaded.count() == 1, 10000);
    QCOMPARE(requestsMatching(fake, "GET /api/mail/body?"), 1);

    email = f.controller.findByMessageId(QStringLiteral("s1"), QStringLiteral("INBOX"));
    QCOMPARE(email.value(QStringLiteral("body")).toString(), QStringLiteral("<p>found</p>"));
    QCOMPARE(email.value(QStringLiteral("bodyMode")).toString(), QStringLiteral("html"));
    QVERIFY(!email.value(QStringLiteral("bodyPending")).toBool());
    QVERIFY(!f.emailDao.findById(QStringLiteral("INBOX"), QStringLiteral("s1")).has_value());

    // Another account's search hits are not this account's mail.
    QVERIFY(f.pairingStore.clear());
    QVERIFY(f.controller.findByMessageId(QStringLiteral("s1"), QStringLiteral("INBOX")).isEmpty());
    f.executor.shutdown();
}

void MailControllerTest::aFolderSwitchReRunsTheSearchThere()
{
    FakeRelayServer fake(httpResponse(200, "OK", "{}"));
    fake.setResponseForPath("/api/mail/search", searchReply("a1", "Archived"));
    RefreshFixture f(fake.port());
    auto* results = qobject_cast<EmailListModel*>(f.controller.searchModel());

    f.controller.setSearchQuery(QStringLiteral("x"));
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.searchBusy(), 10000);
    f.controller.selectFolder(QStringLiteral("Archive"));
    QTRY_VERIFY_WITH_TIMEOUT(requestsMatching(fake, "GET /api/mail/search?") == 2 && !f.controller.searchBusy(), 10000);
    f.executor.shutdown();
    QCOMPARE(requestsMatching(fake, "mailbox=Archive&limit=200"), 1);
    QCOMPARE(results->rowCount(), 1);
    QCOMPARE(results->emailAt(0).folder, QStringLiteral("Archive"));
}

void MailControllerTest::aSearchOnlyHitIsMarkedReadThroughTheRelayInMemoryOnly()
{
    FakeRelayServer fake(searchReply("s1", "Found", "unread"));
    RefreshFixture f(fake.port());
    auto* results = qobject_cast<EmailListModel*>(f.controller.searchModel());
    f.controller.setSearchQuery(QStringLiteral("found"));
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.searchBusy(), 10000);
    QVERIFY(f.controller.findByMessageId(QStringLiteral("s1"), QStringLiteral("INBOX"))
                .value(QStringLiteral("searchOnly")).toBool());

    fake.setResponseForPath("/api/inbox/actions", readStateReply("read", true));
    QSignalSpy stateSpy(&f.controller, &MailController::readStateChanged);
    f.controller.markRead(QStringLiteral("INBOX"), { QStringLiteral("s1") });

    // Before any reply: the row and the reader already say read.
    QCOMPARE(results->emailAt(0).status, QStringLiteral("read"));
    QCOMPARE(stateSpy.count(), 1);
    QCOMPARE(stateSpy.at(0).at(2).toBool(), false);

    QTRY_VERIFY_WITH_TIMEOUT(requestsMatching(fake, "/api/inbox/actions") == 1, 10000);
    const QJsonObject body = jsonBodyOf(firstRequestMatching(fake, "/api/inbox/actions"));
    QCOMPARE(body.value(QStringLiteral("action")).toString(), QStringLiteral("read"));
    QCOMPARE(body.value(QStringLiteral("mailbox")).toString(), QStringLiteral("INBOX"));

    // The opposite change goes through once the first has landed.
    fake.setResponseForPath("/api/inbox/actions", readStateReply("unread", true));
    QTRY_VERIFY_WITH_TIMEOUT((f.controller.markUnread(QStringLiteral("INBOX"), { QStringLiteral("s1") }),
                              stateSpy.count() == 2),
                             10000);
    QTRY_VERIFY_WITH_TIMEOUT(requestsMatching(fake, "/api/inbox/actions") == 2, 10000);
    f.executor.shutdown();
    QCOMPARE(results->emailAt(0).status, QStringLiteral("unread"));
    QVERIFY(f.controller.lastError().isEmpty());
    QVERIFY(!f.emailDao.findById(QStringLiteral("INBOX"), QStringLiteral("s1")).has_value());
}

void MailControllerTest::aRefusedSearchOnlyMarkReadRollsBackTheRow()
{
    FakeRelayServer fake(searchReply("s1", "Found", "unread"));
    RefreshFixture f(fake.port());
    auto* results = qobject_cast<EmailListModel*>(f.controller.searchModel());
    f.controller.setSearchQuery(QStringLiteral("found"));
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.searchBusy(), 10000);

    fake.setResponseForPath("/api/inbox/actions", httpResponse(502, "Bad Gateway", "upstream down"));
    QSignalSpy stateSpy(&f.controller, &MailController::readStateChanged);
    f.controller.markRead(QStringLiteral("INBOX"), { QStringLiteral("s1") });
    QCOMPARE(results->emailAt(0).status, QStringLiteral("read"));

    QTRY_VERIFY_WITH_TIMEOUT(stateSpy.count() == 2, 10000);
    f.executor.shutdown();
    QCOMPARE(stateSpy.at(1).at(2).toBool(), true);
    QCOMPARE(results->emailAt(0).status, QStringLiteral("unread"));
    QCOMPARE(f.controller.findByMessageId(QStringLiteral("s1"), QStringLiteral("INBOX"))
                 .value(QStringLiteral("status")).toString(),
             QStringLiteral("unread"));
    QVERIFY(!f.emailDao.findById(QStringLiteral("INBOX"), QStringLiteral("s1")).has_value());
}

// The search field is disabled under the PIN overlay; this is the controller's
// own refusal for anything else that reaches it. Nothing about the search
// changes, and nothing goes to the relay, until unlock.
void MailControllerTest::aLockedAppNeitherEditsNorSendsASearchUntilUnlocked()
{
    FakeRelayServer fake(httpResponse(200, "OK", "{}"));
    fake.setResponseForPath("/api/mail/search", searchReply("s1", "Found"));
    fake.setResponseForPath("/api/mail/body", httpResponse(200, "OK", R"({"body":"<p>found</p>","bodyMode":"html"})"));
    RefreshFixture f(fake.port());

    f.controller.setSearchQuery(QStringLiteral("found"));
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.searchBusy(), 10000);
    QCOMPARE(requestsMatching(fake, "GET /api/mail/search?"), 1);

    // Typed just before the lock: its debounce does not fire under it.
    f.controller.setSearchQuery(QStringLiteral("founder"));
    f.controller.setAppLocked(true);
    QVERIFY(!f.controller.searchBusy());

    QSignalSpy changed(&f.controller, &MailController::searchChanged);
    f.controller.setSearchQuery(QStringLiteral("other"));
    QCOMPARE(f.controller.searchQuery(), QStringLiteral("founder"));
    QCOMPARE(changed.count(), 0);

    // A reader left open on a hit fetches no body.
    f.controller.loadSearchBody(QStringLiteral("INBOX"), QStringLiteral("s1"));
    // A folder switch re-runs an active search; not while locked.
    f.controller.selectFolder(QStringLiteral("Archive"));
    QTest::qWait(MailController::kSearchDebounceMs + 300);
    QCOMPARE(requestsMatching(fake, "GET /api/mail/search?"), 1);
    QCOMPARE(requestsMatching(fake, "GET /api/mail/body?"), 0);
    QVERIFY(!f.controller.searchBusy());

    // Unlock sends the held-back search, for the query and folder now current.
    f.controller.setAppLocked(false);
    QTRY_VERIFY_WITH_TIMEOUT(requestsMatching(fake, "GET /api/mail/search?") == 2 && !f.controller.searchBusy(), 10000);
    f.executor.shutdown();
    const QByteArray resumed = firstRequestMatching(fake, "q=founder&");
    QVERIFY2(resumed.contains("mailbox=Archive"), resumed.constData());
    QCOMPARE(requestsMatching(fake, "q=other"), 0);
}

void MailControllerTest::aReopenedDraftPrefillsComposeAndItsFirstSaveReplacesTheSource()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true})"));
    serveDraftsAccount(fake);
    fake.setResponseForPath("/api/mail/attachments", httpResponse(200, "OK", R"({"ok":true,"attachments":[]})"));
    RefreshFixture f(fake.port());
    f.controller.selectFolder(QStringLiteral("Drafts"));
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    QVERIFY(f.emailDao.findById(QStringLiteral("Drafts"), QStringLiteral("d1")).has_value());

    QVERIFY(f.controller.reopenDraft(QStringLiteral("INBOX"), QStringLiteral("d1")).isEmpty());
    const QVariantMap draft = f.controller.reopenDraft(QStringLiteral("Drafts"), QStringLiteral("d1"));
    const QString draftToken = draft.value(QStringLiteral("token")).toString();
    QVERIFY(!draftToken.isEmpty());
    QCOMPARE(draft.value(QStringLiteral("to")).toString(), QStringLiteral("\"Doe, Jane\" <jane@example.com>"));
    QCOMPARE(draft.value(QStringLiteral("cc")).toString(), QStringLiteral("cc@example.com"));
    QCOMPARE(draft.value(QStringLiteral("bcc")).toString(), QStringLiteral("bcc@example.com"));
    QCOMPARE(draft.value(QStringLiteral("subject")).toString(), QStringLiteral("Half done"));
    QCOMPARE(draft.value(QStringLiteral("body")).toString(), QStringLiteral("line one<br>line &lt;two&gt;"));
    QVERIFY(!draft.value(QStringLiteral("encrypted")).toBool());

    QSignalSpy saved(&f.controller, &MailController::draftSaveCompleted);
    const auto save = [&] {
        return f.controller.saveDraft(QStringLiteral("jane@example.com"), {}, {}, QStringLiteral("Half done"),
                                      QStringLiteral("<p>edited</p>"), {}, draftToken);
    };
    QVERIFY(save() != 0);
    QTRY_VERIFY_WITH_TIMEOUT(requestsMatching(fake, "POST /api/inbox/actions") == 1, 10000);
    QCOMPARE(saved.count(), 1);
    QVERIFY(saved.at(0).at(1).toBool());

    const QByteArray upload = firstRequestMatching(fake, "POST /api/mail/draft");
    QVERIFY2(upload.contains("\"subject\":\"Half done\"") && !upload.contains("pgpDraft"), upload.constData());
    const QByteArray removal = firstRequestMatching(fake, "POST /api/inbox/actions");
    QVERIFY(removal.contains("\"action\":\"delete\""));
    QVERIFY(removal.contains("\"mailbox\":\"Drafts\""));
    QVERIFY(removal.contains("\"messageIds\":[\"d1\"]"));
    QVERIFY(fake.receivedRequests().indexOf(upload) < fake.receivedRequests().indexOf(removal));

    // The source is gone; a second save appends and deletes nothing more.
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    QVERIFY(save() != 0);
    QTRY_VERIFY_WITH_TIMEOUT(saved.count() == 2, 10000);
    QTest::qWait(200);
    f.executor.shutdown();
    QCOMPARE(requestsMatching(fake, "POST /api/inbox/actions"), 1);
    QCOMPARE(requestsMatching(fake, "POST /api/mail/draft"), 2);
}

void MailControllerTest::aFailedSaveOrADraftWithAttachmentsKeepsTheSource()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true})"));
    serveDraftsAccount(fake);
    fake.setResponseForPath("/api/mail/attachments",
                            httpResponse(200, "OK", R"({"ok":true,"attachments":[{"index":0,"name":"a.pdf","mimeType":"application/pdf","size":3}]})"));
    RefreshFixture f(fake.port());
    f.controller.selectFolder(QStringLiteral("Drafts"));
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    const QString draftToken =
        f.controller.reopenDraft(QStringLiteral("Drafts"), QStringLiteral("d1")).value(QStringLiteral("token")).toString();
    QVERIFY(!draftToken.isEmpty());
    QSignalSpy saved(&f.controller, &MailController::draftSaveCompleted);
    const auto save = [&] {
        return f.controller.saveDraft({}, {}, {}, QStringLiteral("s"), QStringLiteral("<p>b</p>"), {}, draftToken);
    };

    fake.setResponse(httpResponse(502, "Bad Gateway", "failed to save draft", "text/plain"));
    QVERIFY(save() != 0);
    QTRY_VERIFY_WITH_TIMEOUT(saved.count() == 1, 10000);
    QVERIFY(!saved.at(0).at(1).toBool());
    QCOMPARE(requestsMatching(fake, "/api/mail/attachments"), 0);

    fake.setResponse(httpResponse(200, "OK", R"({"ok":true})"));
    QVERIFY(save() != 0);
    QTRY_VERIFY_WITH_TIMEOUT(requestsMatching(fake, "/api/mail/attachments") == 1 && !f.controller.lastError().isEmpty(), 10000);
    f.executor.shutdown();
    QVERIFY(saved.at(1).at(1).toBool());
    QCOMPARE(requestsMatching(fake, "POST /api/inbox/actions"), 0);
}

void MailControllerTest::sendingAReopenedPlaintextDraftUsesTheNormalSendAndDeletesTheSource()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true})"));
    serveDraftsAccount(fake);
    fake.setResponseForPath("/api/mail/attachments", httpResponse(200, "OK", R"({"ok":true,"attachments":[]})"));
    RefreshFixture f(fake.port());
    f.controller.selectFolder(QStringLiteral("Drafts"));
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    const QString draftToken =
        f.controller.reopenDraft(QStringLiteral("Drafts"), QStringLiteral("d1")).value(QStringLiteral("token")).toString();

    QSignalSpy sent(&f.controller, &MailController::sendCompleted);
    QVERIFY(f.controller.sendMail(QStringLiteral("jane@example.com"), {}, {}, QStringLiteral("Half done"),
                                  QStringLiteral("<p>done</p>"), {}, false, false, draftToken) != 0);
    QTRY_VERIFY_WITH_TIMEOUT(requestsMatching(fake, "POST /api/inbox/actions") == 1, 10000);
    f.executor.shutdown();
    QCOMPARE(sent.count(), 1);
    QVERIFY(sent.at(0).at(1).toBool());
    QCOMPARE(requestsMatching(fake, "POST /api/mail/send "), 1);
    QCOMPARE(requestsMatching(fake, "/api/mail/send-pgp"), 0);
    QVERIFY(firstRequestMatching(fake, "POST /api/inbox/actions").contains("\"messageIds\":[\"d1\"]"));
}

void MailControllerTest::aSearchOnlyDraftWithNoReadableBodyIsNotReopened()
{
    FakeRelayServer fake(httpResponse(200, "OK", "{}"));
    fake.setResponseForPath("/api/mail/search", searchReply("e1", "Encrypted Message"));
    fake.setResponseForPath("/api/mail/body", httpResponse(200, "OK", R"({"body":"","bodyMode":""})"));
    RefreshFixture f(fake.port());
    f.controller.selectFolder(QStringLiteral("Drafts"));
    f.controller.setSearchQuery(QStringLiteral("x"));
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.searchBusy() && !f.controller.isBusy(), 10000);

    QSignalSpy loaded(&f.controller, &MailController::searchBodyLoaded);
    f.controller.loadSearchBody(QStringLiteral("Drafts"), QStringLiteral("e1"));
    QTRY_VERIFY_WITH_TIMEOUT(loaded.count() == 1, 10000);
    f.executor.shutdown();
    const QVariantMap email = f.controller.findByMessageId(QStringLiteral("e1"), QStringLiteral("Drafts"));
    QVERIFY(!email.value(QStringLiteral("bodyNotice")).toString().isEmpty());
    QVERIFY(f.controller.reopenDraft(QStringLiteral("Drafts"), QStringLiteral("e1")).isEmpty());
}

void MailControllerTest::aScheduledSendAsksTheRelayFirstThenCarriesSendAt()
{
    FakeRelayServer fake(httpResponse(200, "OK", "{}"));
    fake.setResponseForPath("/api/scheduled", httpResponse(200, "OK", R"({"scheduled":[]})"));
    fake.setResponseForPath("/api/mail/send",
                            httpResponse(202, "Accepted", R"({"scheduled":true,"id":"a","sendAt":"2030-01-01T08:00:00Z"})"));
    RefreshFixture f(fake.port());
    QSignalSpy sent(&f.controller, &MailController::sendCompleted);
    QSignalSpy notice(&f.controller, &MailController::sendWarning);

    const QDateTime at = QDateTime::currentDateTime().addDays(2);
    QVERIFY(f.controller.sendMail(QStringLiteral("jane@example.com"), {}, {}, QStringLiteral("Later"),
                                  QStringLiteral("<p>b</p>"), {}, false, false, {}, {}, {}, at.toMSecsSinceEpoch()) != 0);
    QTRY_VERIFY_WITH_TIMEOUT(sent.count() == 1, 10000);
    f.executor.shutdown();

    QVERIFY2(sent.at(0).at(1).toBool(), qPrintable(f.controller.lastError()));
    QCOMPARE(fake.receivedRequests().size(), 2);
    QVERIFY(fake.receivedRequests().at(0).startsWith("GET /api/scheduled "));
    const QByteArray send = fake.receivedRequests().at(1);
    QVERIFY(send.startsWith("POST /api/mail/send "));
    const QJsonObject body = QJsonDocument::fromJson(send.mid(send.indexOf("\r\n\r\n") + 4)).object();
    QCOMPARE(body.value(QStringLiteral("sendAt")).toString(), sendAtWireValue(at));
    QCOMPARE(notice.count(), 1);
    QVERIFY(notice.at(0).at(0).toString().startsWith(QStringLiteral("Scheduled for")));
}

void MailControllerTest::aSendTimeTheRelayWouldRefuseSendsNothing()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true})"));
    RefreshFixture f(fake.port());
    const qint64 past = QDateTime::currentMSecsSinceEpoch() - 1000;
    const qint64 tooFar = QDateTime::currentDateTime().addDays(367).toMSecsSinceEpoch();

    for (const qint64 at : { past, tooFar }) {
        QCOMPARE(f.controller.sendMail(QStringLiteral("jane@example.com"), {}, {}, QStringLiteral("s"),
                                       QStringLiteral("b"), {}, false, false, {}, {}, {}, at), 0u);
        QVERIFY(!f.controller.lastError().isEmpty());
        QCOMPARE(f.controller.sendClientEncrypted(QStringLiteral("jane@example.com"), {}, {}, QStringLiteral("s"),
                                                  QStringLiteral("b"), {}, {}, at), 0u);
    }
    QTest::qWait(200);
    QCOMPARE(fake.connectionCount(), 0);
}

// An older relay ignores sendAt and would send at once.
void MailControllerTest::aRelayThatCannotScheduleSendsNothing()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"sentSaved":true,"warning":""})"));
    fake.setResponseForPath("/api/scheduled", httpResponse(404, "Not Found", "404 page not found", "text/plain"));
    RefreshFixture f(fake.port());
    QSignalSpy sent(&f.controller, &MailController::sendCompleted);

    QVERIFY(f.controller.sendMail(QStringLiteral("jane@example.com"), {}, {}, QStringLiteral("Later"),
                                  QStringLiteral("<p>b</p>"), {}, false, false, {}, {}, {},
                                  QDateTime::currentDateTime().addDays(1).toMSecsSinceEpoch()) != 0);
    QTRY_VERIFY_WITH_TIMEOUT(sent.count() == 1, 10000);
    f.executor.shutdown();

    QVERIFY(!sent.at(0).at(1).toBool());
    QVERIFY(!f.controller.lastError().isEmpty());
    QCOMPARE(requestsMatching(fake, "/api/mail/send"), 0);
}

// The one-time-link re-send carries no sendAt: confirming it would send now.
void MailControllerTest::aScheduledSendHasNoImmediateLinkResend()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"sentSaved":true,"warning":""})"));
    fake.setResponseForPath("/api/scheduled", httpResponse(200, "OK", R"({"scheduled":[]})"));
    fake.setResponseForPath("/api/mail/send",
                            httpResponse(409, "Conflict", R"({"error":"no key","keylessRecipients":["jane@example.com"]})"));
    RefreshFixture f(fake.port());
    QSignalSpy sent(&f.controller, &MailController::sendCompleted);

    const quint64 token = f.controller.sendMail(QStringLiteral("jane@example.com"), {}, {}, QStringLiteral("Later"),
                                                QStringLiteral("<p>b</p>"), {}, false, true, {}, {}, {},
                                                QDateTime::currentDateTime().addDays(1).toMSecsSinceEpoch());
    QVERIFY(token != 0);
    QTRY_VERIFY_WITH_TIMEOUT(sent.count() == 1, 10000);

    QCOMPARE(f.controller.confirmPickupFallbackSend(token), 0u);
    QTest::qWait(200);
    f.executor.shutdown();
    QCOMPARE(requestsMatching(fake, "POST /api/mail/send "), 1);
}

// The replaced source is deleted from the mailbox it was reopened from, which
// here is the flagged one, never the conventional name.
void MailControllerTest::aDraftInALocalizedDraftsFolderIsReopenedAndReplacedThere()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true})"));
    serveDraftsAccount(fake);
    fake.setResponseForPath("/api/mail/attachments", httpResponse(200, "OK", R"({"ok":true,"attachments":[]})"));
    RefreshFixture f(fake.port());
    const QString brouillons = QStringLiteral("INBOX.Brouillons");
    QVERIFY(f.folderDao.insertOrReplace(brouillons, QString(), false, QStringLiteral("relay"), QStringLiteral("drafts")));
    QCOMPARE(f.controller.draftsFolder(), brouillons);

    f.controller.selectFolder(brouillons);
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    QVERIFY(f.controller.reopenDraft(QStringLiteral("Drafts"), QStringLiteral("d1")).isEmpty());
    const QString draftToken =
        f.controller.reopenDraft(brouillons, QStringLiteral("d1")).value(QStringLiteral("token")).toString();
    QVERIFY(!draftToken.isEmpty());

    QVERIFY(f.controller.saveDraft({}, {}, {}, QStringLiteral("s"), QStringLiteral("<p>b</p>"), {}, draftToken) != 0);
    QTRY_VERIFY_WITH_TIMEOUT(requestsMatching(fake, "POST /api/inbox/actions") == 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    f.executor.shutdown();
    const QByteArray removal = firstRequestMatching(fake, "POST /api/inbox/actions");
    QVERIFY2(removal.contains("\"mailbox\":\"INBOX.Brouillons\""), removal.constData());
    QVERIFY(removal.contains("\"messageIds\":[\"d1\"]"));
}

// Android #188's order: a keyless refusal, then the confirmed pickup re-send
// is refused threading. Both requests name the answered message; the refusal
// is its own signal so Compose can drop the reference for the next Send.
void MailControllerTest::aReplyKeepsItsReferenceThroughThePickupResendUntilRefused()
{
    FakeRelayServer fake(httpResponse(
        409, "Conflict", R"({"error":"no usable key","keylessRecipients":["bob@example.com"],"pickupFallbackAvailable":true})"));
    RefreshFixture f(fake.port());

    QSignalSpy pickupSpy(&f.controller, &MailController::pickupFallbackRequired);
    const quint64 token = f.controller.sendMail(QStringLiteral("bob@example.com"), {}, {}, QStringLiteral("Re: Hi"),
                                                QStringLiteral("Body"), {}, false, /*encrypt=*/true, {},
                                                QStringLiteral("42"), QStringLiteral("Archive"));
    QVERIFY(token != 0u);
    QVERIFY(pickupSpy.wait(10000));

    fake.setResponse(httpResponse(404, "Not Found", "the message being replied to was not found; nothing was sent\n"));
    QSignalSpy refusedSpy(&f.controller, &MailController::replyThreadingRefused);
    QSignalSpy completedSpy(&f.controller, &MailController::sendCompleted);
    QCOMPARE(f.controller.confirmPickupFallbackSend(token), token);
    QVERIFY(completedSpy.wait(10000));

    QCOMPARE(refusedSpy.count(), 1);
    QCOMPARE(refusedSpy.at(0).at(0).value<quint64>(), token);
    QCOMPARE(completedSpy.at(0).at(1).toBool(), false);
    QVERIFY(f.controller.lastError().contains(QStringLiteral("not found")));

    QCOMPARE(fake.receivedRequests().size(), 2);
    for (const QByteArray& request : fake.receivedRequests()) {
        const QJsonObject body = QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n") + 4)).object();
        QCOMPARE(body.value(QStringLiteral("replyToMessageId")).toString(), QStringLiteral("42"));
        QCOMPARE(body.value(QStringLiteral("replyToMailbox")).toString(), QStringLiteral("Archive"));
    }
}

void MailControllerTest::replyAllLeavesOutOnlyThisAccountsOwnAddresses()
{
    FakeRelayServer fake(httpResponse(
        200, "OK",
        R"({"hasIdentity":true,"protection":"server","suggestedUserIDs":["me@example.com","alias@example.com"]})"));
    RefreshFixture f(fake.port());
    const QString sender = QStringLiteral("Bob <bob@example.com>");
    const QString to = QStringLiteral("Me <ME@example.com>, carol@example.com");
    const QString cc = QStringLiteral("alias@example.com");
    const QStringList everyone{ QStringLiteral("bob@example.com"), QStringLiteral("ME@example.com"),
                                QStringLiteral("carol@example.com"), QStringLiteral("alias@example.com") };

    QCOMPARE(f.controller.replyAllRecipients(sender, to, cc), everyone); // not fetched yet

    QSignalSpy stateSpy(&f.controller, &MailController::pgpComposeStateChanged);
    f.controller.loadAccountAddresses();
    QVERIFY(stateSpy.wait(10000));
    QCOMPARE(f.controller.replyAllRecipients(sender, to, cc),
             QStringList({ QStringLiteral("bob@example.com"), QStringLiteral("carol@example.com") }));
    QCOMPARE(f.controller.replyRecipients(QStringLiteral("ME@example.com"), to),
             QStringList({ QStringLiteral("carol@example.com") }));

    // Held for this pairing: no second fetch.
    f.controller.loadAccountAddresses();
    QCOMPARE(fake.connectionCount(), 1);

    // Another account's addresses are never applied to this one.
    DevicePairing other;
    other.subscriberId = QStringLiteral("sub-2");
    other.deviceSecret = QStringLiteral("secret-2");
    other.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    other.deviceId = QStringLiteral("dev-2");
    QVERIFY(f.pairingStore.save(other));
    QCOMPARE(f.controller.replyAllRecipients(sender, to, cc), everyone);
}

// Issue #90, on demo.kypost.org's shape: the login "demo" is not an address,
// so suggestedUserIDs never named demo@kypost-demo.local and Reply All put it
// back in Cc. accountAddresses does; every other recipient stays.
void MailControllerTest::repliesLeaveOutAccountAddressesFromANewerRelay()
{
    FakeRelayServer fake(httpResponse(
        200, "OK",
        R"({"hasIdentity":true,"protection":"client","suggestedUserIDs":["demo"],)"
        R"("accountAddresses":["demo@kypost-demo.local","alias@example.com"]})"));
    RefreshFixture f(fake.port());
    QSignalSpy stateSpy(&f.controller, &MailController::pgpComposeStateChanged);
    f.controller.loadAccountAddresses();
    QVERIFY(stateSpy.wait(10000));

    QCOMPARE(f.controller.replyAllRecipients(QStringLiteral("Bob <bob@example.com>"),
                                             QStringLiteral("Demo <DEMO@kypost-demo.local>, carol@example.com"),
                                             QStringLiteral("alias@example.com, dave@example.com")),
             QStringList({ QStringLiteral("bob@example.com"), QStringLiteral("carol@example.com"),
                           QStringLiteral("dave@example.com") }));
    QCOMPARE(f.controller.replyRecipients(QStringLiteral("Bob <bob@example.com>"), QStringLiteral("demo@kypost-demo.local")),
             QStringList({ QStringLiteral("bob@example.com") }));
    QCOMPARE(f.controller.replyRecipients(QStringLiteral("demo@kypost-demo.local"),
                                          QStringLiteral("Alias@Example.com, carol@example.com")),
             QStringList({ QStringLiteral("carol@example.com") }));
}

namespace {

// An /api/inbox reply holding these UIDs, newest first.
QByteArray inboxRows(const QList<int>& ids, const char* hasMore = "false")
{
    QByteArray rows;
    for (int id : ids) {
        if (!rows.isEmpty())
            rows += ',';
        rows += R"({"messageId":")" + QByteArray::number(id) + R"(","subject":"Message )" + QByteArray::number(id)
            + R"(","status":"read","atUtc":"2026-07-)" + QByteArray::number(10 + id % 20) + R"(T00:00:00Z"})";
    }
    return httpResponse(200, "OK",
                        R"({"tabs":["Uncategorized"],"byTab":{"Uncategorized":[)" + rows + R"(]},"hasMore":)" + hasMore
                            + "}");
}

void holdRow(EmailDao& dao, const QString& folder, const QString& id, bool metadataOnly,
             std::optional<QString> body = std::nullopt)
{
    Email email;
    email.messageId = id;
    email.folder = folder;
    email.subject = QStringLiteral("Held ") + id;
    email.status = QStringLiteral("read");
    email.atUtc = QStringLiteral("2026-08-01T00:00:00Z");
    email.body = body;
    email.metadataOnly = metadataOnly;
    QVERIFY(dao.insertOrReplace(email));
}

} // namespace

void MailControllerTest::anOlderPageLoadsBelowTheListUntilTheRelayHasNoMore()
{
    FakeRelayServer fake(inboxRows({ 9, 8 }));
    fake.setResponseForPath("before=", inboxRows({ 5, 4 }));
    RefreshFixture f(fake.port());
    holdRow(f.emailDao, QStringLiteral("INBOX"), QStringLiteral("9"), false, QStringLiteral("b"));
    holdRow(f.emailDao, QStringLiteral("INBOX"), QStringLiteral("8"), false, QStringLiteral("b"));
    auto* model = qobject_cast<EmailListModel*>(f.controller.emailModel());

    f.controller.loadOlder();
    f.controller.loadOlder(); // one at a time
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount() == 4, 10000);
    QCOMPARE(requestsMatching(fake, "before=8&"), 1);
    QVERIFY(f.emailDao.findById(QStringLiteral("INBOX"), QStringLiteral("4"))->metadataOnly);

    // The relay said nothing lies below 4. One executor thread: an older
    // request sent now would reach the relay before this refresh.
    f.controller.loadOlder();
    f.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(requestsMatching(fake, "since=") == 1 && !f.controller.isBusy(), 10000);
    f.executor.shutdown();
    QCOMPARE(requestsMatching(fake, "before="), 1);
    QVERIFY(f.controller.lastError().isEmpty());
}

void MailControllerTest::aLockedOrSearchingAppLoadsNoOlderMail()
{
    FakeRelayServer fake(inboxRows({ 9 }));
    fake.setResponseForPath("before=", inboxRows({ 5 }));
    RefreshFixture f(fake.port());
    holdRow(f.emailDao, QStringLiteral("INBOX"), QStringLiteral("9"), false, QStringLiteral("b"));

    f.controller.setAppLocked(true);
    f.controller.loadOlder();
    f.controller.setAppLocked(false);
    f.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(requestsMatching(fake, "since=") == 1 && !f.controller.isBusy(), 10000);
    QCOMPARE(requestsMatching(fake, "before="), 0);

    // The list on screen is the search results, not the folder.
    f.controller.setSearchQuery(QStringLiteral("x"));
    f.controller.loadOlder();
    f.controller.clearSearch();
    f.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(requestsMatching(fake, "since=") == 2 && !f.controller.isBusy(), 10000);
    f.executor.shutdown();
    QCOMPARE(requestsMatching(fake, "before="), 0);
}

void MailControllerTest::anOlderPageForAFolderNoLongerOpenIsKeptButNotShown()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"tabs":[],"byTab":{}})"));
    fake.setResponseForPath("before=", httpResponse(200, "OK",
        R"({"tabs":["Uncategorized"],"byTab":{"Uncategorized":[{"messageId":"5","subject":"Old","status":"unread",)"
        R"("atUtc":"2026-07-15T00:00:00Z"}]},"hasMore":false})"));
    RefreshFixture f(fake.port());
    holdRow(f.emailDao, QStringLiteral("INBOX"), QStringLiteral("9"), false, QStringLiteral("b"));
    auto* model = qobject_cast<EmailListModel*>(f.controller.emailModel());

    f.controller.loadOlder();
    f.controller.selectFolder(QStringLiteral("Archive"));
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    f.executor.shutdown();
    QCOMPARE(requestsMatching(fake, "before=9&"), 1);
    QVERIFY(f.emailDao.findById(QStringLiteral("INBOX"), QStringLiteral("5")).has_value());
    QCOMPARE(model->rowCount(), 0);
    // Not shown, but counted.
    QCOMPARE(f.controller.unreadCounts().value(QStringLiteral("INBOX")).toInt(), 1);
}

void MailControllerTest::anOlderRowFetchesItsBodyWhenOpenedAndKeepsIt()
{
    FakeRelayServer fake(httpResponse(500, "Internal Server Error", "{}"));
    fake.setResponseForPath("/api/mail/body", httpResponse(200, "OK", R"({"body":"<p>old</p>","bodyMode":"html"})"));
    RefreshFixture f(fake.port());
    holdRow(f.emailDao, QStringLiteral("INBOX"), QStringLiteral("7"), true);

    QVariantMap email = f.controller.findByMessageId(QStringLiteral("7"), QStringLiteral("INBOX"));
    QVERIFY(email.value(QStringLiteral("searchOnly")).toBool());
    QVERIFY(email.value(QStringLiteral("bodyPending")).toBool());

    QSignalSpy loaded(&f.controller, &MailController::searchBodyLoaded);
    f.controller.loadSearchBody(QStringLiteral("INBOX"), QStringLiteral("7"));
    QTRY_VERIFY_WITH_TIMEOUT(loaded.count() == 1, 10000);
    f.executor.shutdown();
    QCOMPARE(requestsMatching(fake, "GET /api/mail/body?"), 1);
    QCOMPARE(f.emailDao.findById(QStringLiteral("INBOX"), QStringLiteral("7"))->body,
             std::optional<QString>(QStringLiteral("<p>old</p>")));
    email = f.controller.findByMessageId(QStringLiteral("7"), QStringLiteral("INBOX"));
    QCOMPARE(email.value(QStringLiteral("body")).toString(), QStringLiteral("<p>old</p>"));
    QVERIFY(!email.value(QStringLiteral("bodyPending")).toBool());
}

void MailControllerTest::anOlderRowBodyForAReplacedAccountIsNotWritten()
{
    FakeRelayServer fake(httpResponse(500, "Internal Server Error", "{}"));
    fake.setResponseForPath("/api/mail/body", httpResponse(200, "OK", R"({"body":"<p>theirs</p>","bodyMode":"html"})"));
    RefreshFixture f(fake.port());
    // The same UID, cached for whoever is paired when the reply lands.
    holdRow(f.emailDao, QStringLiteral("INBOX"), QStringLiteral("7"), true);

    f.controller.loadSearchBody(QStringLiteral("INBOX"), QStringLiteral("7"));
    DevicePairing replacement;
    replacement.subscriberId = QStringLiteral("sub-2");
    replacement.deviceId = QStringLiteral("dev-2");
    replacement.deviceSecret = QStringLiteral("secret-2");
    replacement.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    QVERIFY(f.pairingStore.save(replacement));

    // One executor thread: this refresh completes after the body reply.
    f.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
    f.executor.shutdown();
    QCOMPARE(requestsMatching(fake, "GET /api/mail/body?"), 1);
    QVERIFY(!f.emailDao.findById(QStringLiteral("INBOX"), QStringLiteral("7"))->body.has_value());
}

void MailControllerTest::anOlderDraftWithNoReadableBodyIsNotReopened()
{
    FakeRelayServer fake(httpResponse(500, "Internal Server Error", "{}"));
    RefreshFixture f(fake.port());
    // An older page has no PGP flags: an empty body may be an encrypted draft.
    holdRow(f.emailDao, QStringLiteral("Drafts"), QStringLiteral("e1"), true, QString(QLatin1String("")));
    QVERIFY(f.controller.reopenDraft(QStringLiteral("Drafts"), QStringLiteral("e1")).isEmpty());
    const QVariantMap email = f.controller.findByMessageId(QStringLiteral("e1"), QStringLiteral("Drafts"));
    QVERIFY(!email.value(QStringLiteral("bodyNotice")).toString().isEmpty());
    f.executor.shutdown();
}

namespace {

Email forwardable()
{
    Email email;
    email.messageId = QStringLiteral("f1");
    email.folder = QStringLiteral("INBOX");
    email.sender = QStringLiteral("a@example.com");
    email.subject = QStringLiteral("With files");
    email.body = QStringLiteral("hello");
    email.bodyMode = QStringLiteral("plain");
    email.hasAttachments = true;
    email.atUtc = QStringLiteral("2026-07-01T00:00:00Z");
    return email;
}

// attachmentsListed()'s shape, as EmailDetail passes it back.
QVariantList twoListed()
{
    return { QVariantMap{ { QStringLiteral("index"), 0 }, { QStringLiteral("name"), QStringLiteral("first.txt") },
                          { QStringLiteral("mimeType"), QStringLiteral("text/plain") } },
             QVariantMap{ { QStringLiteral("index"), 1 }, { QStringLiteral("name"), QStringLiteral("second.pdf") },
                          { QStringLiteral("mimeType"), QStringLiteral("application/pdf") } } };
}

void serveTwoAttachments(FakeRelayServer& fake, const QByteArray& first, const QByteArray& second)
{
    fake.setResponseForPath("/api/mail/attachment?mailbox=INBOX&messageId=f1&index=0",
                            first.isNull() ? httpResponse(500, "Internal Server Error", "boom", "text/plain")
                                           : httpResponse(200, "OK", first, "text/plain"));
    fake.setResponseForPath("/api/mail/attachment?mailbox=INBOX&messageId=f1&index=1",
                            second.isNull() ? httpResponse(500, "Internal Server Error", "boom", "text/plain")
                                            : httpResponse(200, "OK", second, "application/pdf"));
}

} // namespace

// Android's ForwardAttachmentHandoff: the forward carries the original's
// files, in the sender's order, held in memory and never replacing anything.
void MailControllerTest::aForwardCarriesTheOriginalsAttachmentsInMemory()
{
    FakeRelayServer fake(httpResponse(200, "OK", "{}"));
    serveDraftsAccount(fake);
    serveTwoAttachments(fake, "first-bytes", "second-bytes");
    RefreshFixture f(fake.port());
    QVERIFY(f.emailDao.insertOrReplace(forwardable()));
    QSignalSpy opened(&f.controller, &MailController::forwardComposeRequested);

    QVERIFY(f.controller.forwardWithAttachments(QStringLiteral("INBOX"), QStringLiteral("f1"), twoListed(),
                                                QStringLiteral("Fwd: With files"), QStringLiteral("<p>q</p>")));
    QVERIFY(f.controller.isBusy()); // set before any request
    QTRY_VERIFY_WITH_TIMEOUT(opened.count() == 1, 10000);
    QVERIFY(!f.controller.isBusy());

    const QVariantMap draft = opened.at(0).at(0).toMap();
    const QStringList paths = draft.value(QStringLiteral("paths")).toStringList();
    QCOMPARE(paths, (QStringList{ QStringLiteral("kypost-draft:0"), QStringLiteral("kypost-draft:1") }));
    const QVariantMap names = draft.value(QStringLiteral("names")).toMap();
    QCOMPARE(names.value(QStringLiteral("kypost-draft:0")).toString(), QStringLiteral("first.txt"));
    QCOMPARE(names.value(QStringLiteral("kypost-draft:1")).toString(), QStringLiteral("second.pdf"));
    QCOMPARE(draft.value(QStringLiteral("subject")).toString(), QStringLiteral("Fwd: With files"));
    QCOMPARE(draft.value(QStringLiteral("body")).toString(), QStringLiteral("<p>q</p>"));
    QVERIFY(draft.value(QStringLiteral("forwarded")).toBool());
    QVERIFY(!draft.value(QStringLiteral("encrypted")).toBool());
    const QString token = draft.value(QStringLiteral("token")).toString();
    QCOMPARE(token, f.controller.m_draftToken);
    QVERIFY(f.controller.m_draftSourceId.isEmpty());
    QVERIFY(f.controller.lastError().isEmpty());

    QVector<MailAttachmentUpload> uploads;
    QVERIFY(f.controller.readAttachments(paths, uploads, token));
    QCOMPARE(uploads.size(), 2);
    QCOMPARE(uploads.at(0).name, QStringLiteral("first.txt"));
    QCOMPARE(uploads.at(0).data, QByteArray("first-bytes"));
    QCOMPARE(uploads.at(1).name, QStringLiteral("second.pdf"));
    QCOMPARE(uploads.at(1).mimeType, QStringLiteral("application/pdf"));
    QCOMPARE(uploads.at(1).data, QByteArray("second-bytes"));

    QSignalSpy sent(&f.controller, &MailController::sendCompleted);
    QVERIFY(f.controller.sendMail(QStringLiteral("jane@example.com"), {}, {}, QStringLiteral("Fwd: With files"),
                                  QStringLiteral("<p>q</p>"), paths, false, false, token) != 0);
    QTRY_VERIFY_WITH_TIMEOUT(sent.count() == 1, 10000);
    f.executor.shutdown();
    QVERIFY2(sent.at(0).at(1).toBool(), qPrintable(f.controller.lastError()));
    const QByteArray send = firstRequestMatching(fake, "POST /api/mail/send ");
    QVERIFY(send.contains(QByteArray("first-bytes").toBase64()));
    QVERIFY(send.contains(QByteArray("second-bytes").toBase64()));
    // No source: the session replaces nothing.
    QCOMPARE(requestsMatching(fake, "/api/inbox/actions"), 0);
}

// Android's two toasts: a failed download and an over-budget refusal are
// different things to tell the user, and Compose opens with the rest.
void MailControllerTest::aForwardSaysWhichAttachmentsItCouldNotCarry()
{
    {
        FakeRelayServer fake(httpResponse(200, "OK", "{}"));
        serveTwoAttachments(fake, QByteArray(), "second-bytes");
        RefreshFixture f(fake.port());
        QVERIFY(f.emailDao.insertOrReplace(forwardable()));
        QSignalSpy opened(&f.controller, &MailController::forwardComposeRequested);
        QVERIFY(f.controller.forwardWithAttachments(QStringLiteral("INBOX"), QStringLiteral("f1"), twoListed(),
                                                    QStringLiteral("Fwd"), QString()));
        QTRY_VERIFY_WITH_TIMEOUT(opened.count() == 1, 10000);
        f.executor.shutdown();
        const QVariantMap draft = opened.at(0).at(0).toMap();
        QCOMPARE(draft.value(QStringLiteral("paths")).toStringList(), QStringList{ QStringLiteral("kypost-draft:0") });
        QCOMPARE(draft.value(QStringLiteral("names")).toMap().value(QStringLiteral("kypost-draft:0")).toString(),
                 QStringLiteral("second.pdf"));
        QCOMPARE(f.controller.m_draftAttachments.size(), 1);
        QCOMPARE(f.controller.m_draftAttachments.at(0).data, QByteArray("second-bytes"));
        // In the session, which Compose shows until dismissed; a refresh
        // clears lastError while the user is still writing.
        const QString notice = draft.value(QStringLiteral("notice")).toString();
        QVERIFY2(notice.contains(QStringLiteral("1 attachment could not be downloaded")), qPrintable(notice));
        QVERIFY(!notice.contains(QStringLiteral("too large")));
        QVERIFY(f.controller.lastError().isEmpty());
    }
    {
        // 13 MB + 13 MB passes the 25 MB send cap: the second is refused,
        // the first is never evicted for it.
        const QByteArray first(13 * 1024 * 1024, 'a');
        FakeRelayServer fake(httpResponse(200, "OK", "{}"));
        serveTwoAttachments(fake, first, QByteArray(13 * 1024 * 1024, 'b'));
        RefreshFixture f(fake.port());
        QVERIFY(f.emailDao.insertOrReplace(forwardable()));
        QSignalSpy opened(&f.controller, &MailController::forwardComposeRequested);
        QVERIFY(f.controller.forwardWithAttachments(QStringLiteral("INBOX"), QStringLiteral("f1"), twoListed(),
                                                    QStringLiteral("Fwd"), QString()));
        QTRY_VERIFY_WITH_TIMEOUT(opened.count() == 1, 20000);
        f.executor.shutdown();
        const QVariantMap draft = opened.at(0).at(0).toMap();
        QCOMPARE(draft.value(QStringLiteral("paths")).toStringList(), QStringList{ QStringLiteral("kypost-draft:0") });
        QCOMPARE(f.controller.m_draftAttachments.size(), 1);
        QCOMPARE(f.controller.m_draftAttachments.at(0).name, QStringLiteral("first.txt"));
        QVERIFY(f.controller.m_draftAttachments.at(0).data == first);
        const QString notice = draft.value(QStringLiteral("notice")).toString();
        QVERIFY2(notice.contains(QStringLiteral("1 attachment is too large")), qPrintable(notice));
        QVERIFY(!notice.contains(QStringLiteral("could not be downloaded")));
    }
    {
        // A listed size past what is left is refused without a download, and
        // the caller's quote notice leads the session's own.
        FakeRelayServer fake(httpResponse(200, "OK", "{}"));
        serveTwoAttachments(fake, "first-bytes", "second-bytes");
        RefreshFixture f(fake.port());
        QVERIFY(f.emailDao.insertOrReplace(forwardable()));
        QVariantList listed = twoListed();
        QVariantMap second = listed.at(1).toMap();
        second.insert(QStringLiteral("size"), 26LL * 1024 * 1024);
        listed[1] = second;
        QSignalSpy opened(&f.controller, &MailController::forwardComposeRequested);
        QVERIFY(f.controller.forwardWithAttachments(QStringLiteral("INBOX"), QStringLiteral("f1"), listed,
                                                    QStringLiteral("Fwd"), QString(), QStringLiteral("Quote note.")));
        QTRY_VERIFY_WITH_TIMEOUT(opened.count() == 1, 10000);
        f.executor.shutdown();
        QCOMPARE(requestsMatching(fake, "index=0"), 1);
        QCOMPARE(requestsMatching(fake, "index=1"), 0);
        QCOMPARE(f.controller.m_draftAttachments.size(), 1);
        const QString notice = opened.at(0).at(0).toMap().value(QStringLiteral("notice")).toString();
        QVERIFY2(notice.startsWith(QStringLiteral("Quote note. 1 attachment is too large")), qPrintable(notice));
    }
}

// AGENTS.md 6g: the bytes belong to the account and the unlocked session that
// asked. A lock (even one already lifted) or a re-pair while the downloads run
// leaves nothing behind and opens nothing.
void MailControllerTest::aForwardThatOutlivesItsAccountOrTheLockKeepsNothing()
{
    for (const bool repair : { false, true }) {
        FakeRelayServer fake(httpResponse(200, "OK", "{}"));
        serveTwoAttachments(fake, "first-bytes", "second-bytes");
        RefreshFixture f(fake.port());
        QVERIFY(f.emailDao.insertOrReplace(forwardable()));
        QSignalSpy opened(&f.controller, &MailController::forwardComposeRequested);
        QSignalSpy finished(&f.controller, &MailController::forwardFinished);
        QVERIFY(f.controller.forwardWithAttachments(QStringLiteral("INBOX"), QStringLiteral("f1"), twoListed(),
                                                    QStringLiteral("Fwd"), QString()));
        if (repair) {
            DevicePairing replacement;
            replacement.subscriberId = QStringLiteral("sub-2");
            replacement.deviceId = QStringLiteral("dev-2");
            replacement.deviceSecret = QStringLiteral("secret-2");
            replacement.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
            QVERIFY(f.pairingStore.save(replacement));
        } else {
            f.controller.setAppLocked(true);
            f.controller.setAppLocked(false);
        }
        QTRY_VERIFY_WITH_TIMEOUT(!f.controller.isBusy(), 10000);
        f.executor.shutdown();
        QCOMPARE(requestsMatching(fake, "GET /api/mail/attachment?"), 2); // they did come back
        QCOMPARE(opened.count(), 0);
        QCOMPARE(finished.count(), 1); // the reader's status line still clears
        QVERIFY(f.controller.m_draftToken.isEmpty());
        QVERIFY(f.controller.m_draftAttachments.isEmpty());
    }
}

// A forward that lands while the user is writing a message with a draft
// session (a reopened draft and its attachments) never touches that session:
// releasing it would make the message unsendable.
void MailControllerTest::aLiveDraftSessionSurvivesACompletedForward()
{
    FakeRelayServer fake(httpResponse(200, "OK", "{}"));
    serveTwoAttachments(fake, "first-bytes", "second-bytes");
    RefreshFixture f(fake.port());
    QVERIFY(f.emailDao.insertOrReplace(forwardable()));
    f.controller.m_draftToken = QStringLiteral("live");
    f.controller.m_draftIdentity = f.pairingStore.currentIdentity();
    f.controller.m_draftAttachments = { { QStringLiteral("draft.pdf"), QStringLiteral("application/pdf"), QString(),
                                          QByteArray("draft-bytes") } };
    f.controller.m_draftSourceId = QStringLiteral("d1");

    QSignalSpy opened(&f.controller, &MailController::forwardComposeRequested);
    QSignalSpy finished(&f.controller, &MailController::forwardFinished);
    QVERIFY(f.controller.forwardWithAttachments(QStringLiteral("INBOX"), QStringLiteral("f1"), twoListed(),
                                                QStringLiteral("Fwd"), QString()));
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 10000);
    f.executor.shutdown();

    QVERIFY(finished.at(0).at(0).toBool()); // refused because a message is being written
    QCOMPARE(opened.count(), 0);
    QCOMPARE(f.controller.m_draftToken, QStringLiteral("live"));
    QCOMPARE(f.controller.m_draftSourceId, QStringLiteral("d1"));
    QCOMPARE(f.controller.m_draftAttachments.size(), 1);
    QCOMPARE(f.controller.m_draftAttachments.at(0).data, QByteArray("draft-bytes"));
    QVector<MailAttachmentUpload> uploads;
    QVERIFY(f.controller.readAttachments({ QStringLiteral("kypost-draft:0") }, uploads, QStringLiteral("live")));
    QCOMPARE(uploads.at(0).data, QByteArray("draft-bytes"));
}

// Decrypted content never rides along on a send that may go out in the clear.
void MailControllerTest::aProtectedMessageForwardsNoAttachments()
{
    FakeRelayServer fake(httpResponse(200, "OK", "{}"));
    serveTwoAttachments(fake, "first-bytes", "second-bytes");
    RefreshFixture f(fake.port());

    Email clientProtected = forwardable();
    clientProtected.body = QString();
    clientProtected.pgpEncrypted = true;
    QVERIFY(f.emailDao.insertOrReplace(clientProtected));
    QVERIFY(!f.controller.forwardWithAttachments(QStringLiteral("INBOX"), QStringLiteral("f1"), twoListed(),
                                                 QStringLiteral("Fwd"), QString()));

    QVERIFY(f.emailDao.insertOrReplace(forwardable()));
    f.controller.m_decryptedMessageId = QStringLiteral("f1"); // shown here only as local decryption
    f.controller.m_decryptedFolder = QStringLiteral("INBOX");
    QVERIFY(!f.controller.forwardWithAttachments(QStringLiteral("INBOX"), QStringLiteral("f1"), twoListed(),
                                                 QStringLiteral("Fwd"), QString()));
    QVERIFY(!f.controller.isBusy());
    QTest::qWait(100);
    QCOMPARE(fake.connectionCount(), 0);
    QVERIFY(f.controller.m_draftAttachments.isEmpty());
}

QTEST_GUILESS_MAIN(MailControllerTest)
#include "MailControllerTest.moc"
