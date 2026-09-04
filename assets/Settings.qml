import bb.cascades 1.4
import bb.system 1.2
import "components"

Sheet {
    id: settingsSheet
    property string pendingApiUrl: ""

    onOpened: {
        settingsController.refreshCacheUsed()
    }

    Page {
        titleBar: TitleBar {
            title: qsTr("Settings")
            dismissAction: ActionItem {
                imageSource: "asset:///images/icons/accent/caret-left.png"
                onTriggered: settingsSheet.close()
            }
        }

        ScrollView {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            scrollRole: ScrollRole.Main

            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                
                Header {
                    title: qsTr("User interface")
                }
                
                SettingItem {
                    iconSource: "asset:///images/icons/settings/ca_sms.png"
                    title: qsTr("Compact message")
                    description: qsTr("Fit more messages on screen at one time. #IRC")

                    ToggleButton {
                        id: uiCompactSwitch
                        checked: settingsController.compactMessageEnabled

                        onCheckedChanged: {
                            settingsController.setCompactMessageEnabled(checked)
                        }
                    }
                }

                Header {
                    title: qsTr("Storage")
                }
                
                SettingItem {
                    iconSource: "asset:///images/icons/settings/ca_storage_access.png"
                    title: qsTr("Cached media location")
                    description: settingsController.cachePath
                }

                SettingItem {
                    iconSource: "asset:///images/icons/settings/ca_data_management.png"
                    title: qsTr("Used")
                    description: settingsController.cacheUsed
                }

                SettingItem {
                    iconSource: "asset:///images/icons/settings/ca_security_wipe.png"
                    title: qsTr("Clear cache")
                    description: qsTr("Clear cached media such as avatars and attachments.")
                    onTriggered: clearCacheDialog.show()
                }

                Header {
                    title: qsTr("Discord API")
                }
                
                SettingItem {
                    iconSource: ""
                    title: qsTr("API URL")
                    description: settingsController.apiUrl
                    onTriggered: {
                        apiUrlPrompt.inputField.defaultText = settingsController.apiUrl
                        apiUrlPrompt.show()
                    }
                }
                
                SettingItem {
                    iconSource: ""
                    title: qsTr("CDN URL")
                    description: settingsController.cdnUrl
                    onTriggered: {
                        cdnUrlPrompt.inputField.defaultText = settingsController.cdnUrl
                        cdnUrlPrompt.show()
                    }
                }
                
                Container {
                    leftPadding: ui.du(2.0)
                    rightPadding: ui.du(2.0)
                    horizontalAlignment: HorizontalAlignment.Left
                    verticalAlignment: VerticalAlignment.Center

                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }
                    
                    ImageView {
                        imageSource: "asset:///images/icons/settings/ca_message_error.png"
                        preferredWidth: ui.du(8.0)
                        scalingMethod: ScalingMethod.AspectFit
                    }

                    Label {
                        text: qsTr("For security reasons, after you update the Discord API URL, your saved token will be cleared.")
                        multiline: true
                        opacity: 0.5
                        textStyle.fontSize: FontSize.XXSmall
                        verticalAlignment: VerticalAlignment.Center
                    }
                }

                Container {
                    leftPadding: ui.du(2.0)
                    rightPadding: ui.du(2.0)
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment: VerticalAlignment.Fill

                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }

                    Button {
                        text: "Reset to official Discord backend"
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        onClicked: resetBackendDialog.show()
                    }
                }

                Header {
                    title: qsTr("Miscellaneous")
                }

                SettingItem {
                    iconSource: "asset:///images/icons/settings/ca_audio_active.png"
                    title: qsTr("Sound effects")
                    description: qsTr("Play login, connecting, and error sounds. #MSNTV2")

                    ToggleButton {
                        id: sfxSwitch
                        checked: settingsController.sfxEnabled

                        onCheckedChanged: {
                            settingsController.setSfxEnabled(checked)
                        }
                    }
                }

                Header {
                    title: qsTr("Debug")
                }

                SettingItem {
                    iconSource: "asset:///images/icons/settings/ca_open.png"
                    title: qsTr("Export log")
                    description: qsTr("Save the app log file, e.g. for bug reports.")
                    onTriggered: {
                        var savedPath = settingsController.exportLog()
                        if (savedPath === "") {
                            exportLogFailedToast.show()
                        } else {
                            exportLogSuccessToast.body = qsTr("Log saved to ") + savedPath
                            exportLogSuccessToast.show()
                        }
                    }
                }
            }
        }

        attachedObjects: [
            SystemDialog {
                id: clearCacheDialog
                title: qsTr("Clear cache")
                body: qsTr("Clear cached media? This cannot be undone.")
                confirmButton.label: qsTr("Clear")
                cancelButton.label: qsTr("Cancel")

                onFinished: {
                    if (result == SystemUiResult.ConfirmButtonSelection) {
                        settingsController.clearCache()
                    }
                }
            },
            SystemPrompt {
                id: apiUrlPrompt
                title: qsTr("API URL")
                body: qsTr("Enter Discord API URL")
                inputField.defaultText: settingsController.apiUrl
                confirmButton.label: qsTr("Save")
                cancelButton.label: qsTr("Cancel")

                onFinished: {
                    if (result == SystemUiResult.ConfirmButtonSelection && inputFieldTextEntry() != settingsController.apiUrl) {
                        settingsSheet.pendingApiUrl = inputFieldTextEntry()
                        updateApiUrlDialog.show()
                    }
                }
            },
            SystemPrompt {
                id: cdnUrlPrompt
                title: qsTr("CDN URL")
                body: qsTr("Enter Discord CDN URL")
                inputField.defaultText: settingsController.cdnUrl
                confirmButton.label: qsTr("Save")
                cancelButton.label: qsTr("Cancel")

                onFinished: {
                    if (result == SystemUiResult.ConfirmButtonSelection) {
                        settingsController.setCdnUrl(inputFieldTextEntry())
                    }
                }
            },
            SystemDialog {
                id: updateApiUrlDialog
                title: qsTr("Update API URL")
                body: qsTr("Updating the Discord API URL will log you out and clear your saved token for security. Continue?")
                confirmButton.label: qsTr("Update")
                cancelButton.label: qsTr("Cancel")

                onFinished: {
                    if (result == SystemUiResult.ConfirmButtonSelection && settingsController.setApiUrl(settingsSheet.pendingApiUrl)) {
                        discordClient.logout()
                    }
                    settingsSheet.pendingApiUrl = ""
                }
            },
            SystemToast {
                id: exportLogFailedToast
                body: qsTr("No log file found to export yet.")
            },
            SystemToast {
                id: exportLogSuccessToast
                body: ""
            },
            SystemDialog {
                id: resetBackendDialog
                title: qsTr("Reset Discord backend")
                body: settingsController.apiUrl == settingsController.officialApiUrl ? qsTr("Reset API and CDN URLs to the official Discord backend?") : qsTr("Reset API and CDN URLs to the official Discord backend? Because the API URL is different from the official backend, you will be logged out.")
                confirmButton.label: qsTr("Reset")
                cancelButton.label: qsTr("Cancel")

                onFinished: {
                    if (result == SystemUiResult.ConfirmButtonSelection) {
                        var shouldLogout = settingsController.apiUrl != settingsController.officialApiUrl
                        if (settingsController.resetDiscordBackend() && shouldLogout) {
                            discordClient.logout()
                        }
                    }
                }
            }
        ]

        onCreationCompleted: {
            settingsController.refreshCacheUsed()
        }
    }
}
