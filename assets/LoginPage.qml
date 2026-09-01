import bb.cascades 1.4
import bb.system 1.2
import QtQuick 1.0

Page {
    signal loginSucceeded()
    property double loginProgress: 0.0
    property int loginProgressTicks: 0
    property bool passwordMode: false
    property variant mfaSheet: null
    property variant captchaSheet: null

    function showLoginFailed(message) {
        loginFailToast.body = message.length > 0 ? message : qsTr("Login failed")
        loginFailToast.show()
    }

    function showMfaSheet(ticket, loginInstanceId) {
        if (!mfaSheet) {
            return
        }
        mfaSheet.ticket = ticket
        mfaSheet.loginInstanceId = loginInstanceId
        mfaSheet.open()
    }

    function showCaptchaSheet(requestKind, sitekey, rqdata, rqtoken) {
        if (!captchaSheet) {
            return
        }
        captchaSheet.requestKind = requestKind
        captchaSheet.sitekey = sitekey
        captchaSheet.rqdata = rqdata
        captchaSheet.rqtoken = rqtoken
        captchaSheet.open()
    }

    function updateLoginProgress() {
        if (appStore.busy) {
            if (loginProgress < 1.0) {
                loginProgressTicks = loginProgressTicks + 1
                if (loginProgressTicks < 34) {
                    loginProgress = Math.min(0.9, loginProgress + 0.026)
                } else {
                    loginProgress = Math.min(1.0, loginProgress + Math.max(0.001, (1.0 - loginProgress) * 0.035))
                }
            }
        } else {
            loginProgress = 0.0
            loginProgressTicks = 0
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

            ImageView {
                imageSource: "asset:///icon.png"
                horizontalAlignment: HorizontalAlignment.Center
                scalingMethod: ScalingMethod.AspectFit
                preferredWidth: ui.du(16.0)
                preferredHeight: ui.du(16.0)
            }

            Label {
                text: qsTr("Welcome to BBCord")
                horizontalAlignment: HorizontalAlignment.Center
            }

            TextField {
                id: tokenField
                hintText: qsTr("Enter your Discord token...")
                horizontalAlignment: HorizontalAlignment.Fill
                textFormat: TextFormat.Plain
                inputMode: TextFieldInputMode.Text
                text: ""
                visible: !appStore.busy && !passwordMode
            }

            Button {
                id: btnLogin

                text: qsTr("Login")
                horizontalAlignment: HorizontalAlignment.Fill
                enabled: !appStore.busy
                visible: !appStore.busy && !passwordMode

                onClicked: {
                    discordClient.login(tokenField.text)
                }
            }

            TextField {
                id: emailField
                hintText: qsTr("Email or phone number")
                horizontalAlignment: HorizontalAlignment.Fill
                textFormat: TextFormat.Plain
                inputMode: TextFieldInputMode.EmailAddress
                text: ""
                topMargin: ui.du(1.0)
                visible: !appStore.busy && passwordMode
            }

            TextField {
                id: passwordField
                hintText: qsTr("Password")
                horizontalAlignment: HorizontalAlignment.Fill
                textFormat: TextFormat.Plain
                inputMode: TextFieldInputMode.Password
                text: ""
                topMargin: ui.du(1.0)
                visible: !appStore.busy && passwordMode
            }

            Button {
                id: btnPasswordLogin

                text: qsTr("Login")
                horizontalAlignment: HorizontalAlignment.Fill
                enabled: !appStore.busy && emailField.text.length > 0 && passwordField.text.length > 0
                visible: !appStore.busy && passwordMode
                topMargin: ui.du(1.0)

                onClicked: {
                    discordClient.loginWithPassword(emailField.text, passwordField.text)
                }
            }

            Button {
                id: btnToggleMode

                text: passwordMode ? qsTr("Login with token instead") : qsTr("Login with email/phone & password")
                horizontalAlignment: HorizontalAlignment.Fill
                visible: !appStore.busy
                topMargin: ui.du(1.0)

                onClicked: {
                    passwordMode = !passwordMode
                }
            }

            Button {
                id: btnContinue

                text: qsTr("Continue")
                horizontalAlignment: HorizontalAlignment.Fill
                visible: false

                onClicked: {
                    console.log("[login] continue clicked")
                    loginSucceeded()
                }
            }

            ProgressIndicator {
                id: loginProgressIndicator
                visible: appStore.busy
                horizontalAlignment: HorizontalAlignment.Center
                topMargin: ui.du(4.0)
                preferredWidth: ui.du(32.0)
                fromValue: 0.0
                toValue: 1.0
                value: appStore.busy ? loginProgress : 0.0

                onVisibleChanged: {
                    loginProgress = visible ? 0.05 : 0.0
                    loginProgressTicks = 0
                }
            }

            Label {
                text: appStore.statusText
                horizontalAlignment: HorizontalAlignment.Center
                multiline: true
                visible: text.length > 0
            }

            Button {
                id: btnInst
                text: qsTr("How to get token")
                horizontalAlignment: HorizontalAlignment.Fill
                visible: !appStore.busy
            }

            Label {
                text: "powered by michioxd"
                horizontalAlignment: HorizontalAlignment.Center
                textStyle.fontSizeValue: 0.0
                textFit.minFontSizeValue: 3.0
                textFit.maxFontSizeValue: 4.0
                textFit.mode: LabelTextFitMode.Default
                opacity: 0.5
            }
        }
    }

    attachedObjects: [
        SystemToast {
            id: loginFailToast
        },
        Timer {
            id: loginProgressTimer
            interval: 120
            repeat: true
            running: appStore.busy
            onTriggered: {
                updateLoginProgress()
            }
        },
        ComponentDefinition {
            id: mfaSheetDefinition
            source: "asset:///MfaSheet.qml"
        },
        ComponentDefinition {
            id: captchaSheetDefinition
            source: "asset:///CaptchaSheet.qml"
        }
    ]

    onCreationCompleted: {
        discordClient.loginFailed.connect(showLoginFailed)
        discordClient.mfaRequired.connect(showMfaSheet)
        discordClient.captchaRequired.connect(showCaptchaSheet)
        mfaSheet = mfaSheetDefinition.createObject()
        captchaSheet = captchaSheetDefinition.createObject()
    }
}