import bb.cascades 1.4
import bb.system 1.2

Sheet {
    id: mfaSheet

    property string ticket: ""
    property string loginInstanceId: ""

    function reset() {
        codeField.text = ""
    }

    function showMfaFailed(message) {
        // Only react while this sheet is the thing on screen asking for a
        // code; otherwise a token/password error elsewhere could pop a
        // confusing toast on top of the sheet.
        if (!mfaSheet.opened) {
            return
        }
        mfaFailToast.body = message.length > 0 ? message : qsTr("Verification failed")
        mfaFailToast.show()
    }

    onOpened: {
        reset()
        codeField.requestFocus()
    }

    onCreationCompleted: {
        discordClient.loginFailed.connect(showMfaFailed)
        discordClient.loginSucceeded.connect(mfaSheet.close)
    }

    Page {
        titleBar: TitleBar {
            title: qsTr("Two-factor authentication")
            dismissAction: ActionItem {
                imageSource: "asset:///images/icons/accent/caret-left.png"
                onTriggered: mfaSheet.close()
            }
        }

        Container {
            layout: DockLayout {}

            Container {
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center

                layout: StackLayout {
                }

                leftPadding: ui.du(8.0)
                rightPadding: ui.du(8.0)

                Label {
                    text: qsTr("Enter the 6-digit code from your authenticator app")
                    horizontalAlignment: HorizontalAlignment.Center
                    multiline: true
                    bottomMargin: ui.du(2.0)
                }

                TextField {
                    id: codeField
                    hintText: qsTr("6-digit code")
                    horizontalAlignment: HorizontalAlignment.Fill
                    inputMode: TextFieldInputMode.NumbersAndPunctuation
                    textFormat: TextFormat.Plain
                    text: ""
                    visible: !appStore.busy

                    onTextChanging: {
                        // Keep it numeric-only and capped at 6 digits.
                        var digitsOnly = text.replace(/[^0-9]/g, "")
                        if (digitsOnly.length > 6) {
                            digitsOnly = digitsOnly.substring(0, 6)
                        }
                        if (digitsOnly !== text) {
                            text = digitsOnly
                        }
                    }

                    input {
                        onSubmitted: {
                            if (codeField.text.length === 6) {
                                discordClient.submitMfaCode(mfaSheet.ticket, mfaSheet.loginInstanceId, codeField.text)
                            }
                        }
                    }
                }

                Button {
                    id: btnVerify
                    text: qsTr("Verify")
                    horizontalAlignment: HorizontalAlignment.Fill
                    enabled: !appStore.busy && codeField.text.length === 6
                    visible: !appStore.busy

                    onClicked: {
                        discordClient.submitMfaCode(mfaSheet.ticket, mfaSheet.loginInstanceId, codeField.text)
                    }
                }

                ActivityIndicator {
                    running: appStore.busy
                    visible: appStore.busy
                    horizontalAlignment: HorizontalAlignment.Center
                    preferredWidth: ui.du(10.0)
                    preferredHeight: ui.du(10.0)
                    topMargin: ui.du(4.0)
                }

                Label {
                    text: appStore.statusText
                    horizontalAlignment: HorizontalAlignment.Center
                    multiline: true
                    visible: text.length > 0
                    topMargin: ui.du(2.0)
                }
            }
        }

        attachedObjects: [
            SystemToast {
                id: mfaFailToast
            }
        ]
    }
}
