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
class MemberListController;
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
  // Called when the app is opened via invoke — including when
  // BlackBerry Hub builds an InvokeRequest after the user taps/
  // long-presses an item in BBCord's Hub tab (see HubIntegration.cpp,
  // uds_register_item_context_action). Parses channelId from the
  // payload and navigates straight to the matching guild/channel or DM.
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
  MemberListController *m_memberListController;
  ServerListController *m_serverListController;
  AboutController *m_aboutController;
};

#endif /* ApplicationUI_HPP_ */
