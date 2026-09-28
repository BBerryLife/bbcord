import bb.cascades 1.4
import bb.system 1.2

Sheet {
    id: mfaSheet

    property string ticket: ""
    property string loginInstanceId: ""
    // Guards against submitting the same single-use /auth/mfa/totp ticket twice: on BB10's
    // virtual keyboard both onSubmitted and the Verify button's onClicked can fire before
    // appStore.busy propagates, and Discord rejects the reused ticket ("Invalid two-factor
    // code"). Tracked locally and synchronously.
    property bool submitting: false

    function reset() {
        codeField.text = ""
        codeField.liveText = ""
        submitting = false
    }

    function submitCode() {
        if (submitting || codeField.liveText.length !== 6) {
            return
        }
        submitting = true
        discordClient.submitMfaCode(mfaSheet.ticket, mfaSheet.loginInstanceId, codeField.liveText)
    }

    function showMfaFailed(message) {
        // Only react while this sheet is on screen asking for a code, so other errors do not toast over it.
        if (!mfaSheet.opened) {
            return
        }
        submitting = false
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
        // On a CAPTCHA at the MFA step C++ emits captchaRequired() instead of loginFailed();
        // close this sheet so LoginPage's CaptchaSheet is not stacked under it.
        discordClient.captchaRequired.connect(mfaSheet.close)
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

                    // Same BB10 keyboard buffering as LoginPage.qml: text lags behind onTextChanging and
                    // kept btnVerify disabled after 6 digits. Mirror the live value into a plain property
                    // and bind/submit against it.
                    property string liveText: ""

                    onTextChanging: {
                        // Keep it numeric-only and capped at 6 digits.
                        var digitsOnly = text.replace(/[^0-9]/g, "")
                        if (digitsOnly.length > 6) {
                            digitsOnly = digitsOnly.substring(0, 6)
                        }
                        if (digitsOnly !== text) {
                            text = digitsOnly
                        }
                        codeField.liveText = digitsOnly
                    }

                    input {
                        onSubmitted: {
                            mfaSheet.submitCode()
                        }
                    }
                }

                Button {
                    id: btnVerify
                    text: qsTr("Verify")
                    horizontalAlignment: HorizontalAlignment.Fill
                    enabled: !appStore.busy && !mfaSheet.submitting && codeField.liveText.length === 6
                    visible: !appStore.busy

                    onClicked: {
                        mfaSheet.submitCode()
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
