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

#ifndef ApplicationUI_HPP_
#define ApplicationUI_HPP_

#include <QObject>

namespace bb {
namespace cascades {
class LocaleHandler;
}
namespace system {
class InvokeManager;
class InvokeRequest;
}
} // namespace bb

class QTranslator;
class AppStore;
class ChatController;
class DiscordClient;
class DmListController;
class ImagePreview;
class MainPageController;
class ServerListController;
class SettingsController;
class AboutController;

/*!
 * @brief Application UI object
 *
 * Use this object to create and init app UI, to create context objects, to
 * register the new meta types etc.
 */
class ApplicationUI : public QObject {
  Q_OBJECT
public:
  ApplicationUI();
  virtual ~ApplicationUI() {}
  Q_INVOKABLE void openLink(const QString &url);
private slots:
  void onSystemLanguageChanged();
  // Được gọi khi app được mở qua invoke — bao gồm cả trường hợp
  // BlackBerry Hub tự soạn InvokeRequest lúc user tap/long-press 1 item
  // trong tab BBCord của Hub (xem HubIntegration.cpp,
  // uds_register_item_context_action). Parse channelId từ payload và
  // điều hướng thẳng tới guild/channel hoặc DM tương ứng.
  void onInvoked(const bb::system::InvokeRequest &request);

private:
  QTranslator *m_pTranslator;
  bb::cascades::LocaleHandler *m_pLocaleHandler;
  bb::system::InvokeManager *m_pInvokeManager;
  AppStore *m_appStore;
  DiscordClient *m_discordClient;
  ChatController *m_chatController;
  ImagePreview *m_imagePreview;
  DmListController *m_dmListController;
  SettingsController *m_settingsController;
  MainPageController *m_mainPageController;
  ServerListController *m_serverListController;
  AboutController *m_aboutController;
};

#endif /* ApplicationUI_HPP_ */
