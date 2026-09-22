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

import bb.cascades 1.4
import bb.system 1.2
import bb.multimedia 1.0

NavigationPane {
    id: nav

    property variant currentLoginPage: null
    property variant currentMainPage: null
    property variant currentUserSheet: null
    property variant aboutDialog: null
    property variant currentSettingsSheet: null
    // Fix: short-tap/long-press on a Hub item wasn't navigating into
    // the actual chat, only opening the app to the main server/DM list
    // - selectChannel() (called from ApplicationUI::onInvoked() in
    // applicationui.cpp) only updates backend state, it never pushes
    // any QML page itself. Stashed here instead of acting immediately
    // because hubOpenChannelRequested can arrive before currentMainPage
    // exists yet (a cold start still has to finish login first) - see
    // tryOpenPendingHubChannel(), called both from the signal handler
    // below AND from openMainPage() itself for whichever order actually
    // happens first.
    property string pendingHubChannelId: ""
    property string pendingHubGuildId: ""

    function tryOpenPendingHubChannel() {
        if (pendingHubChannelId === "" || !currentMainPage) {
            return;
        }
        currentMainPage.openChat(pendingHubChannelId, pendingHubGuildId, "");
        pendingHubChannelId = "";
        pendingHubGuildId = "";
    }

    function playSfx(player) {
        if (!settingsController.sfxEnabled) {
            return
        }

        player.stop()
        player.play()
    }

    function updateConnectingSfx() {
        if (!settingsController.sfxEnabled) {
            connectingPlayer.stop()
            connectedPlayer.stop()
            errorPlayer.stop()
            return
        }

        if (appStore.busy) {
            connectingPlayer.play()
        } else {
            connectingPlayer.stop()
        }
    }

    function playConnectedSfx() {
        connectingPlayer.stop()
        playSfx(connectedPlayer)
    }

    function playErrorSfx() {
        connectingPlayer.stop()
        playSfx(errorPlayer)
    }

    onCreationCompleted: {
        currentUserSheet = userSheetDefinition.createObject()
        currentSettingsSheet = settingsSheetDefinition.createObject()
        aboutDialog = aboutDialogDefinition.createObject()

        var loginPage = loginPageDefinition.createObject()
        if (loginPage) {
            currentLoginPage = loginPage
            loginPage.loginSucceeded.connect(openMainPage)
            nav.push(loginPage)
        }
        discordClient.loginSucceeded.connect(openMainPage)
        discordClient.loginSucceeded.connect(playConnectedSfx)
        discordClient.loginFailed.connect(playErrorSfx)
        settingsController.sfxEnabledChanged.connect(updateConnectingSfx)
        appStore.busyChanged.connect(updateConnectingSfx)
        // Fix: see pendingHubChannelId's doc comment above -
        // applicationUI (the C++ ApplicationUI object, already exposed
        // as a context property for openLink()) emits this after a Hub
        // short-tap/long-press invoke, whether or not the main page
        // exists yet.
        applicationUI.hubOpenChannelRequested.connect(function (channelId, guildId) {
            pendingHubChannelId = channelId
            pendingHubGuildId = guildId
            tryOpenPendingHubChannel()
        })
        discordClient.autoLogin()
    }

    function openMainPage() {
        if (currentMainPage) {
            return
        }

        var mainPage = mainPageDefinition.createObject()
        if (mainPage) {
            currentMainPage = mainPage
            mainPage.navigationPane = nav
            nav.push(mainPage)
            if (currentLoginPage) {
                nav.remove(currentLoginPage)
                currentLoginPage.destroy()
                currentLoginPage = null
            }
            // Fix: handles the cold-start ordering - if
            // hubOpenChannelRequested arrived while the app was still
            // logging in (pendingHubChannelId got set before
            // currentMainPage existed), apply it now that the main
            // page is finally here. A no-op (pendingHubChannelId is
            // "") on every normal, non-Hub launch.
            tryOpenPendingHubChannel()
        }
    }

    function showLoginPage() {
        if (currentMainPage) {
            nav.remove(currentMainPage)
            currentMainPage.destroy()
            currentMainPage = null
        }

        if (!currentLoginPage) {
            currentLoginPage = loginPageDefinition.createObject()
            if (currentLoginPage) {
                currentLoginPage.loginSucceeded.connect(openMainPage)
                nav.push(currentLoginPage)
            }
        }
    }

    Menu.definition: MenuDefinition {
        actions: [
            ActionItem {
                title: "Me"
                imageSource: appStore.currentUserAvatarSource
                enabled: appStore.loggedIn

                onTriggered: {
                    if (currentUserSheet) {
                        currentUserSheet.open()
                    }
                }
            },

            ActionItem {
                title: "Log out"
                imageSource: "asset:///images/icons/ic_sign_out.png"
                enabled: appStore.loggedIn

                onTriggered: {
                    logoutDialog.show()
                }
            },

            ActionItem {
                title: "Settings"

                onTriggered: {
                    if (currentSettingsSheet) {
                        currentSettingsSheet.open()
                    }
                }
            },

            ActionItem {
                title: "About"
                imageSource: "asset:///images/icons/ic_info.png"

                onTriggered: {
                    aboutDialog.open()
                }
            }
        ]
    }

    onPopTransitionEnded: {
        if (page != currentMainPage) {
            page.destroy()
        }
    }

    attachedObjects: [
        ComponentDefinition {
            id: loginPageDefinition
            source: "asset:///LoginPage.qml"
        },
        ComponentDefinition {
            id: mainPageDefinition
            source: "asset:///MainPage.qml"
        },
        ComponentDefinition {
            id: userSheetDefinition
            source: "asset:///UserSheet.qml"
        },
        ComponentDefinition {
            id: settingsSheetDefinition
            source: "asset:///Settings.qml"
        },

        ComponentDefinition {
            id: aboutDialogDefinition
            source: "asset:///About.qml"
        },

        MediaPlayer {
            id: connectingPlayer
            sourceUrl: "asset:///audio/connecting.ogg"
            repeatMode: RepeatMode.Track
        },
        MediaPlayer {
            id: connectedPlayer
            sourceUrl: "asset:///audio/connected.ogg"
            repeatMode: RepeatMode.None
        },
        MediaPlayer {
            id: errorPlayer
            sourceUrl: "asset:///audio/error.ogg"
            repeatMode: RepeatMode.None
        },

        SystemDialog {
            id: logoutDialog
            title: "Log out"
            body: "Log out and clear saved token?"
            confirmButton.label: "OK"
            cancelButton.label: "Cancel"

            onFinished: {
                if (result == SystemUiResult.ConfirmButtonSelection) {
                    discordClient.logout()
                    nav.showLoginPage()
                }
            }
        }
    ]
}
