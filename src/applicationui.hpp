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
  // Called from HubPreviewCard.qml's Component.onCompleted once the
  // card's tiny placeholder UI has actually been shown to the user for
  // at least one frame (see the comment on the QML side for why the
  // card can't just call this instantly at construction). Ends the
  // card and hands off to the app.
  Q_INVOKABLE void finishCardAndHandoff();
private Q_SLOTS:
  void onSystemLanguageChanged();
  // Called when the app is opened via invoke — including when
  // BlackBerry Hub builds an InvokeRequest after the user taps/
  // long-presses an item in BBCord's Hub tab (see HubIntegration.cpp,
  // uds_register_item_context_action). Parses channelId from the
  // payload and navigates straight to the matching guild/channel or DM.
  void onInvoked(const bb::system::InvokeRequest &request);
  // Fix (short-tap on a Hub item doesn't open the app): BlackBerry Hub
  // resizes a card's window after creating it (see comment on
  // initCardUI() below) - required or the card renders at the wrong
  // size/doesn't show at all. We don't actually need to draw anything
  // different at the new size (the card is just a momentary
  // placeholder), so this only needs to exist and be connected - it
  // can be a no-op.
  void onCardResizeRequested(const bb::system::CardResizeMessage &message);

Q_SIGNALS:
  // Fix: short-tap/long-press on a Hub item was only calling
  // DiscordClient::selectChannel(), which updates backend/store state
  // but never pushes any QML page - main.qml listens for this and
  // calls openChat() itself. See HubIntegration.cpp/onInvoked()'s own
  // comments for the full reasoning. channelName is intentionally
  // omitted - ChatCard.qml already fixes its own title up after load.
  void hubOpenChannelRequested(const QString &channelId,
                                const QString &guildId);

private:
  // Builds the app's normal full UI from main.qml - the startup path
  // used every time EXCEPT when launched as a Hub short-tap card (see
  // initCardUI()). Was previously done inline in the constructor;
  // split out so the constructor can pick one or the other based on
  // m_pInvokeManager->startupMode().
  void initFullUI();
  // Fix (short-tap on a Hub item doesn't open the app): builds a tiny
  // placeholder scene from HubPreviewCard.qml instead of the real app
  // UI. Per BlackBerry's own Cascades invoketarget sample
  // (github.com/blackberry/Cascades-Samples), Hub's default/short-tap
  // resolution for an inbox item invokes a card.* target - there is no
  // UDS API for registering a plain "default tap" action, only
  // uds_register_item_context_action() for the long-press context
  // menu (confirmed via [Hub][invoke] onInvoked() logging - it never
  // fires at all on short-tap with an "application"-type target, only
  // on long-press). This card's only job is to immediately parse the
  // SAME sourceId payload as onInvoked() already does, select the
  // channel, call cardDone() to close the card, and let Hub bring
  // BBCord's own already-running (or newly started) full UI to the
  // foreground - i.e. replicate long-press's outcome from a short-tap.
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

  // Fix (short-tap on a Hub item doesn't open the app) - card handoff
  // bookkeeping. onInvoked() and finishCardAndHandoff() (called from
  // HubPreviewCard.qml) can run in either order - Hub sends the
  // InvokeRequest right after creating the card, but there's no
  // documented guarantee it beats the QML Component.onCompleted timer.
  // Whichever runs second does the actual handoff work using whatever
  // the first one already stored, so it works either way:
  //   - m_cardHandoffReady: true once finishCardAndHandoff() has been
  //     called (the QML placeholder has been shown and is asking to
  //     hand off).
  //   - m_cardInvokeDataReady / m_cardInvokeData: set by onInvoked()
  //     when running in card mode, instead of navigating immediately -
  //     the actual navigate+cardDone() work is only safe to do once
  //     the card UI is confirmed shown (m_cardHandoffReady).
  bool m_cardHandoffReady;
  bool m_cardInvokeDataReady;
  QByteArray m_cardInvokeData;
  // Fix (short-tap on a Hub item doesn't open the app): does the actual
  // parse-sourceId + selectGuild/selectChannel + cardDone() work, once
  // BOTH the card UI is confirmed shown AND the invoke data has
  // arrived (see the 3 fields above) - called from whichever of
  // onInvoked()/finishCardAndHandoff() runs second.
  void performCardHandoffIfReady();
};

#endif /* ApplicationUI_HPP_ */
