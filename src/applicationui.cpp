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
#include <bb/system/InvokeManager>
#include <bb/system/InvokeRequest>

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
      m_aboutController(new AboutController(this)) {
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

  // Nhận InvokeRequest từ BlackBerry Hub khi user tap/long-press item
  // trong tab BBCord (xem HubIntegration.cpp + bar-descriptor.xml
  // <invoke-target id="ch.michioxd.bbcord.invoke">).
  m_pInvokeManager = new bb::system::InvokeManager(this);
  QObject::connect(
      m_pInvokeManager,
      SIGNAL(invoked(const bb::system::InvokeRequest &)), this,
      SLOT(onInvoked(const bb::system::InvokeRequest &)));

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

  QObject::connect(m_settingsController, SIGNAL(cacheCleared()),
                   m_discordClient, SLOT(clearAvatarCacheState()));
  QObject::connect(m_settingsController, SIGNAL(cacheCleared()), m_appStore,
                   SLOT(clearMediaCacheState()));
  QObject::connect(m_settingsController, SIGNAL(cacheCleared()),
                   m_chatController, SLOT(clearMediaCacheState()));

  // Create root object for the UI
  AbstractPane *root = qml->createRootObject<AbstractPane>();

  // Set created root object as the application scene
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
  // BlackBerry Hub tự soạn InvokeRequest khi user tap/long-press 1 item
  // trong tab BBCord. sourceId (chính là channelId, xem
  // HubIntegration::upsertThreadItem) nằm trong data() dưới dạng JSON:
  //   { "attributes": { "sourceId": "<channelId>", ... } }
  // KHÔNG nằm trong uri() (Hub luôn để uri() rỗng khi tự soạn invoke).
  QByteArray rawData = request.data();
  if (rawData.isEmpty()) {
    return;
  }

  QVariantMap root = DiscordJsonParser::parseObject(rawData);
  QString channelId =
      root.value("attributes").toMap().value("sourceId").toString().trimmed();
  if (channelId.isEmpty()) {
    return;
  }

  if (m_discordClient == 0 || m_appStore == 0) {
    return;
  }

  // channelId rỗng ở m_chatGuildByChannelId nghĩa là DM (guild channel
  // luôn được insert vào map này ngay khi select — xem
  // GuildChannels.cpp::selectChannel()), hoặc app đang cold-start và
  // channel đó chưa từng được mở trong phiên hiện tại. Với guild channel
  // chưa từng mở, selectGuild() rỗng sẽ bị AppStore::selectChannel() bỏ
  // qua phần điều hướng guild — chấp nhận được vì đây là trường hợp hiếm
  // (channel có ping nhưng app chưa từng load nó trong phiên này).
  QString guildId = m_discordClient->guildIdForChannel(channelId);
  if (!guildId.isEmpty()) {
    m_discordClient->selectGuild(guildId);
  }
  m_discordClient->selectChannel(channelId);
}
