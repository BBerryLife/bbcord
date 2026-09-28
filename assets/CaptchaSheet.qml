import bb.cascades 1.4
import QtQuick 1.0

// Shown when Discord answers a password/MFA request with a CAPTCHA (see
// tryHandleCaptcha()/captchaRequired()). Loads the real hCaptcha widget in a WebView
// and reports the token via discordClient.submitCaptchaKey(), which retries the request.
// There is no reliable JS bridge on this WebKit, so the page redirects to a fake
// "bbcord://captcha-result?token=..." URL, which onNavigationRequested() intercepts
// (cancelling the navigation) to extract the token.
Sheet {
    id: captchaSheet

    property string sitekey: ""
    property string rqdata: ""
    property string rqtoken: ""
    // "password" or "mfa": informational for the status label only (C++ tracks the pending request).
    property string requestKind: ""
    property bool submitting: false

    function reset() {
        submitting = false
        statusLabel.text = qsTr("Complete the CAPTCHA below to continue.")
    }

    function buildHtml() {
        // Site URL hardcoded to discord.com/channels/@me, the documented approach for
        // third-party clients: hCaptcha's checksiteconfig only needs a recognized host.
        return "<!DOCTYPE html><html><head><meta charset=\"utf-8\">" +
               "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">" +
               "<script src=\"https://js.hcaptcha.com/1/api.js\" async defer></script>" +
               "<style>html,body{margin:0;padding:0;background:transparent;" +
               "display:flex;align-items:center;justify-content:center;min-height:100%;}" +
               "</style></head><body>" +
               "<div class=\"h-captcha\"" +
               " data-sitekey=\"" + sitekey + "\"" +
               (rqdata.length > 0 ? " data-rqdata=\"" + rqdata + "\" data-sentry=\"true\"" : "") +
               " data-callback=\"onCaptchaSolved\"" +
               "></div>" +
               "<script>" +
               "function onCaptchaSolved(token){" +
               "  window.location = 'bbcord://captcha-result?token=' + encodeURIComponent(token);" +
               "}" +
               "</script></body></html>"
    }

    function extractToken(url) {
        var marker = "token="
        var index = url.indexOf(marker)
        if (index === -1) {
            return ""
        }
        return decodeURIComponent(url.substring(index + marker.length))
    }

    function submitToken(token) {
        if (submitting || token.length === 0) {
            return
        }
        submitting = true
        statusLabel.text = qsTr("Verifying...")
        discordClient.submitCaptchaKey(token)
        captchaSheet.close()
    }

    onOpened: {
        reset()
        captchaWebView.html = buildHtml()
    }

    Page {
        titleBar: TitleBar {
            title: qsTr("CAPTCHA verification")
            dismissAction: ActionItem {
                imageSource: "asset:///images/icons/accent/caret-left.png"
                onTriggered: captchaSheet.close()
            }
        }

        Container {
            topPadding: ui.du(2.0)
            bottomPadding: ui.du(2.0)
            leftPadding: ui.du(2.0)
            rightPadding: ui.du(2.0)
            layout: StackLayout {}

            Label {
                id: statusLabel
                text: qsTr("Complete the CAPTCHA below to continue.")
                multiline: true
                horizontalAlignment: HorizontalAlignment.Center
                bottomMargin: ui.du(1.0)
            }

            WebView {
                id: captchaWebView
                preferredWidth: captchaSheet.width - ui.du(4.0)
                preferredHeight: ui.du(60.0)
                settings.javaScriptEnabled: true

                onNavigationRequested: {
                    if (request.url.toString().indexOf("bbcord://captcha-result") === 0) {
                        request.action = WebNavigationRequestAction.Ignore
                        var token = captchaSheet.extractToken(request.url.toString())
                        captchaSheet.submitToken(token)
                    }
                }
            }

            Button {
                text: qsTr("Cancel")
                horizontalAlignment: HorizontalAlignment.Fill
                topMargin: ui.du(1.0)
                enabled: !captchaSheet.submitting

                onClicked: {
                    captchaSheet.close()
                }
            }
        }
    }
}
