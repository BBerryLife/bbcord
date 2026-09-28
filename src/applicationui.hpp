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

#include <QByteArray>
#include <QObject>

namespace bb {
namespace cascades {
class LocaleHandler;
}
namespace system {
class InvokeManager;
class InvokeRequest;
class CardResizeMessage;
class CardDoneMessage;
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

/** Application UI object: creates and initializes the UI, context objects and meta types. */
class ApplicationUI : public QObject {
  Q_OBJECT
public:
  ApplicationUI();
  virtual ~ApplicationUI() {}
  Q_INVOKABLE void openLink(const QString &url);
  // Called from HubPreviewCard.qml once the placeholder has been shown for a frame.
  // Ends the card and hands off to the app.
  Q_INVOKABLE void finishCardAndHandoff();
private Q_SLOTS:
  void onSystemLanguageChanged();
  // Called when the app is opened via invoke, including from a Hub item. Parses
  // channelId from the payload and navigates to the matching guild/channel or DM.
  void onInvoked(const bb::system::InvokeRequest &request);
  // Hub resizes the card window after creation; nothing needs redrawing, so this can be a no-op.
  void onCardResizeRequested(const bb::system::CardResizeMessage &message);

Q_SIGNALS:
  // Asks main.qml to open the chat page (selectChannel() only updates backend
  // state). channelName is omitted; ChatCard.qml fixes its title after load.
  void hubOpenChannelRequested(const QString &channelId,
                                const QString &guildId);

private:
  // Builds the normal full UI from main.qml (every startup except a Hub card).
  void initFullUI();
  // Builds a tiny placeholder scene from HubPreviewCard.qml. Hub resolves a short
  // tap through a card.* target, so the card parses the same sourceId payload as
  // onInvoked(), selects the channel, calls cardDone() and lets Hub bring the full UI forward.
  void initCardUI();

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

  // Card handoff state. onInvoked() and finishCardAndHandoff() can run in either
  // order; the second one performs the handoff.
  //   m_cardHandoffReady: finishCardAndHandoff() was called (placeholder shown)
  //   m_cardInvokeDataReady / m_cardInvokeData: stashed by onInvoked() in card mode
  bool m_cardHandoffReady;
  bool m_cardInvokeDataReady;
  QByteArray m_cardInvokeData;
  // Parses sourceId, selects the guild/channel and calls cardDone(), once both the
  // card UI is shown and the invoke data has arrived.
  void performCardHandoffIfReady();
};

#endif /* ApplicationUI_HPP_ */
