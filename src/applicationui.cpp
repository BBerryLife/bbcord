/*
 * Copyright (c) 2011-2015 BlackBerry Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "applicationui.hpp"

#include "core/AppStore.hpp"
#include "core/Client.hpp"
#include "core/discord/JsonParser.hpp"
#include "ui/AboutController.hpp"
#include "ui/ChatController.hpp"
#include "ui/DmListController.hpp"
#include "ui/ImagePreview.hpp"
#include "ui/MainPageController.hpp"
#include "ui/MemberListController.hpp"
#include "ui/ServerListController.hpp"
#include "ui/SettingsController.hpp"

#include <bb/ApplicationInfo>
#include <bb/cascades/AbstractPane>
#include <bb/cascades/Application>
#include <bb/cascades/LocaleHandler>
#include <bb/cascades/QmlDocument>
#include <bb/system/ApplicationStartupMode>
#include <bb/system/CardDoneMessage>
#include <bb/system/CardResizeMessage>
#include <bb/system/InvokeManager>
#include <bb/system/InvokeRequest>

#include <QDebug>

using namespace bb::cascades;

ApplicationUI::ApplicationUI()
    : QObject(), m_pInvokeManager(0), m_appStore(new AppStore(this)),
      m_discordClient(new DiscordClient(m_appStore, this)),
      m_chatController(new ChatController(m_discordClient, m_appStore, this)),
      m_imagePreview(new ImagePreview(this)),
      m_dmListController(
          new DmListController(m_discordClient, m_appStore, this)),
      m_settingsController(new SettingsController(this)),
      m_mainPageController(new MainPageController(m_discordClient, m_appStore,
                                                  m_settingsController, this)),
      m_memberListController(
          new MemberListController(m_discordClient, m_appStore, this)),
      m_serverListController(
          new ServerListController(m_discordClient, m_appStore, this)),
      m_aboutController(new AboutController(this)),
      m_cardHandoffReady(false), m_cardInvokeDataReady(false) {
  // prepare the localization
  m_pTranslator = new QTranslator(this);
  m_pLocaleHandler = new LocaleHandler(this);

  bool res = QObject::connect(m_pLocaleHandler, SIGNAL(systemLanguageChanged()),
                              this, SLOT(onSystemLanguageChanged()));
  // This is only available in Debug builds
  Q_ASSERT(res);
  // Since the variable is not used in the app, this is added to avoid a
  // compiler warning
  Q_UNUSED(res);

  // initial load
  onSystemLanguageChanged();

  // Receives the InvokeRequest from BlackBerry Hub (tap/long-press on a BBCord tab
  // item; invoke-target "ch.michioxd.bbcord.invoke"). initCardUI() below is unused now
  // (no card.previewer target) but kept for a possible card path.
  m_pInvokeManager = new bb::system::InvokeManager(this);
  bool invokeConnected = QObject::connect(
      m_pInvokeManager,
      SIGNAL(invoked(const bb::system::InvokeRequest &)), this,
      SLOT(onInvoked(const bb::system::InvokeRequest &)));
  bool resizeConnected = QObject::connect(
      m_pInvokeManager,
      SIGNAL(cardResizeRequested(const bb::system::CardResizeMessage &)),
      this,
      SLOT(onCardResizeRequested(const bb::system::CardResizeMessage &)));
  bb::system::ApplicationStartupMode::Type startupMode =
      m_pInvokeManager->startupMode();
  qDebug() << "[Hub][invoke] InvokeManager connected (invoked=" << invokeConnected
           << ", cardResizeRequested=" << resizeConnected << ") startupMode="
           << startupMode;

  QObject::connect(m_settingsController, SIGNAL(cacheCleared()),
                   m_discordClient, SLOT(clearAvatarCacheState()));
  QObject::connect(m_settingsController, SIGNAL(cacheCleared()), m_appStore,
                   SLOT(clearMediaCacheState()));
  QObject::connect(m_settingsController, SIGNAL(cacheCleared()),
                   m_chatController, SLOT(clearMediaCacheState()));

  // InvokeCard should not occur (no card.previewer target); kept as a safety net.
  if (startupMode == bb::system::ApplicationStartupMode::InvokeCard) {
    initCardUI();
  } else {
    initFullUI();
  }
}

void ApplicationUI::initFullUI() {
  // Create scene document from main.qml asset, the parent is set
  // to ensure the document gets destroyed properly at shut down.
  QmlDocument *qml = QmlDocument::create("asset:///main.qml").parent(this);
  qml->setContextProperty("appStore", m_appStore);
  qml->setContextProperty("discordClient", m_discordClient);
  qml->setContextProperty("chatController", m_chatController);
  qml->setContextProperty("imagePreview", m_imagePreview);
  qml->setContextProperty("dmListController", m_dmListController);
  qml->setContextProperty("mainPageController", m_mainPageController);
  qml->setContextProperty("memberListController", m_memberListController);
  qml->setContextProperty("serverListController", m_serverListController);
  qml->setContextProperty("settingsController", m_settingsController);
  qml->setContextProperty("aboutController", m_aboutController);
  qml->setContextProperty("applicationInfo", new bb::ApplicationInfo(this));
  qml->setContextProperty("applicationUI", this);

  // Create root object for the UI
  AbstractPane *root = qml->createRootObject<AbstractPane>();

  // Set created root object as the application scene
  Application::instance()->setScene(root);
}

void ApplicationUI::initCardUI() {
  // Minimal card UI: shows a placeholder, then QML calls finishCardAndHandoff().
  // The InvokeRequest data arrives separately via onInvoked(), which stashes it in
  // m_cardInvokeData/m_cardInvokeDataReady; performCardHandoffIfReady() runs the
  // handoff once both flags are set, in either order.
  QmlDocument *qml =
      QmlDocument::create("asset:///HubPreviewCard.qml").parent(this);
  qml->setContextProperty("applicationUI", this);
  AbstractPane *root = qml->createRootObject<AbstractPane>();
  // If HubPreviewCard.qml fails to load, createRootObject() returns 0 and the card
  // would hang on a black screen; call the handoff directly in that case.
  if (root == 0) {
    qDebug() << "[Hub][invoke] initCardUI() - HubPreviewCard.qml failed to"
                " load (createRootObject returned 0) - calling"
                " finishCardAndHandoff() directly so the card doesn't hang.";
    finishCardAndHandoff();
    return;
  }
  Application::instance()->setScene(root);
}

void ApplicationUI::openLink(const QString &url) {
  bb::system::InvokeRequest request;
  request.setTarget("sys.browser");
  request.setAction("bb.action.OPEN");
  request.setMimeType("text/html");
  request.setUri(url);

  bb::system::InvokeManager invokeManager;
  invokeManager.invoke(request);
}

void ApplicationUI::onSystemLanguageChanged() {
  QCoreApplication::instance()->removeTranslator(m_pTranslator);
  // Initiate, load and install the application translation files.
  QString locale_string = QLocale().name();
  QString file_name = QString("BBCord_%1").arg(locale_string);
  if (m_pTranslator->load(file_name, "app/native/qm")) {
    QCoreApplication::instance()->installTranslator(m_pTranslator);
  }
}

void ApplicationUI::onInvoked(const bb::system::InvokeRequest &request) {
  // The InvokeRequest data() is JSON {"attributes": {"sourceId": "<channelId>", ...}};
  // uri() is always empty for Hub-built invokes.
  qDebug() << "[Hub][invoke] onInvoked() fired - target=" << request.target()
           << "action=" << request.action()
           << "mimeType=" << request.mimeType()
           << "uri=" << request.uri().toString()
           << "dataBytes=" << request.data().size()
           << "metadata=" << request.metadata();

  if (m_pInvokeManager != 0 &&
      m_pInvokeManager->startupMode() ==
          bb::system::ApplicationStartupMode::InvokeCard) {
    // Card mode: only stash the data; performCardHandoffIfReady() acts once the card UI is shown.
    m_cardInvokeData = request.data();
    m_cardInvokeDataReady = true;
    qDebug() << "[Hub][invoke] card mode - stashed data, deferring to"
                " performCardHandoffIfReady().";
    performCardHandoffIfReady();
    return;
  }

  QByteArray rawData = request.data();
  if (rawData.isEmpty()) {
    qDebug() << "[Hub][invoke] data() is empty - nothing to parse, returning.";
    return;
  }

  QVariantMap root = DiscordJsonParser::parseObject(rawData);
  qDebug() << "[Hub][invoke] parsed data() =" << root;
  QString channelId =
      root.value("attributes").toMap().value("sourceId").toString().trimmed();
  if (channelId.isEmpty()) {
    qDebug() << "[Hub][invoke] sourceId missing/empty after parsing attributes"
                 " map - returning. Raw data was:" << rawData;
    return;
  }

  if (m_discordClient == 0 || m_appStore == 0) {
    qDebug() << "[Hub][invoke] m_discordClient or m_appStore is null - returning."
             << "discordClient=" << m_discordClient << "appStore=" << m_appStore;
    return;
  }

  // On a cold start m_chatGuildByChannelId is empty, so guildId is usually empty
  // here. Fall back to fetchChannelInfo() (GET /channels/{id}, resolves guildId and
  // name); onChannelInfoLoaded() then selects the guild.
  QString guildId = m_discordClient->guildIdForChannel(channelId);
  qDebug() << "[Hub][invoke] channelId=" << channelId << "resolved guildId="
           << guildId << "(empty is OK for a DM or a channel never opened"
                          " this session)";
  if (!guildId.isEmpty()) {
    m_discordClient->selectGuild(guildId);
  } else {
    qDebug() << "[Hub][invoke] guildId unresolved from cache - falling back"
                " to fetchChannelInfo() REST lookup for" << channelId;
    m_discordClient->fetchChannelInfo(channelId);
  }
  m_discordClient->selectChannel(channelId);
  qDebug() << "[Hub][invoke] selectChannel(" << channelId << ") called - done.";
  // selectChannel() only updates backend state; main.qml listens for this signal
  // and calls openChat() (see hubOpenChannelRequested in applicationui.hpp).
  emit hubOpenChannelRequested(channelId, guildId);
}

void ApplicationUI::finishCardAndHandoff() {
  // Called from HubPreviewCard.qml's Component.onCompleted after one event-loop turn (see that file).
  qDebug() << "[Hub][invoke] finishCardAndHandoff() called from QML.";
  m_cardHandoffReady = true;
  performCardHandoffIfReady();
}

void ApplicationUI::performCardHandoffIfReady() {
  // Only proceed once BOTH sides are ready - see the field comments in
  // applicationui.hpp for why order isn't guaranteed either way.
  if (!m_cardHandoffReady || !m_cardInvokeDataReady) {
    qDebug() << "[Hub][invoke] performCardHandoffIfReady() - not ready yet"
                " (handoffReady=" << m_cardHandoffReady << "dataReady="
             << m_cardInvokeDataReady << "), waiting for the other half.";
    return;
  }

  QByteArray rawData = m_cardInvokeData;
  if (!rawData.isEmpty() && m_discordClient != 0 && m_appStore != 0) {
    QVariantMap root = DiscordJsonParser::parseObject(rawData);
    QString channelId = root.value("attributes")
                             .toMap()
                             .value("sourceId")
                             .toString()
                             .trimmed();
    qDebug() << "[Hub][invoke] performCardHandoffIfReady() - parsed"
                " channelId=" << channelId;
    if (!channelId.isEmpty()) {
      QString guildId = m_discordClient->guildIdForChannel(channelId);
      if (!guildId.isEmpty()) {
        m_discordClient->selectGuild(guildId);
      }
      m_discordClient->selectChannel(channelId);
      // Kept in sync with onInvoked(); currently unreachable (no card.previewer target).
      emit hubOpenChannelRequested(channelId, guildId);
    }
  } else {
    qDebug() << "[Hub][invoke] performCardHandoffIfReady() - nothing to"
                " navigate to (empty data or null client/store), just"
                " closing the card.";
  }

  // Tells Hub/Navigator we are done: closes the card and brings the real app window forward.
  if (m_pInvokeManager != 0) {
    bb::system::CardDoneMessage doneMessage;
    doneMessage.setReason("opened");
    bool sent = m_pInvokeManager->sendCardDone(doneMessage);
    qDebug() << "[Hub][invoke] sendCardDone() sent =" << sent;
  }
}

void ApplicationUI::onCardResizeRequested(
    const bb::system::CardResizeMessage &message) {
  // Intentionally a no-op; connected so Hub's card sizing handshake completes (see applicationui.hpp).
  Q_UNUSED(message);
  qDebug() << "[Hub][invoke] onCardResizeRequested() fired.";
}
