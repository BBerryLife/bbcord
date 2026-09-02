import bb.cascades 1.4
import QtQuick 1.0

// Shown when Discord rejects a password-login or MFA request with a
// CAPTCHA challenge (see DiscordRestClient::tryHandleCaptcha() /
// captchaRequired() in the C++ layer). Loads Discord's real hCaptcha
// widget in a WebView so the user solves it exactly as they would on
// discord.com, then reports the resulting token back to C++ via
// discordClient.submitCaptchaKey(), which retries the original
// password-login/MFA request with the token attached.
//
// There is no reliable two-way JS bridge on this WebKit version, so the
// hand-off works by navigation interception instead: the page's JS
// callback redirects to a fake "bbcord://captcha-result?token=..." URL
// once hCaptcha succeeds, which onNavigationRequested() below intercepts
// (cancelling the real navigation) and extracts the token from.
Sheet {
    id: captchaSheet

    property string sitekey: ""
    property string rqdata: ""
    property string rqtoken: ""
    // "password" or "mfa" - purely informational for the status label
    // below; C++ already knows which request is pending via
    // m_pendingCaptchaRequest and does not need this passed back.
    property string requestKind: ""
    property bool submitting: false

    function reset() {
        submitting = false
        statusLabel.text = qsTr("Complete the CAPTCHA below to continue.")
    }

    function buildHtml() {
        // Hardcoding the site URL to discord.com/channels/@me is the
        // documented approach for third-party clients solving Discord's
        // hCaptcha challenge outside the real discord.com page (confirmed
        // by the discord.py-self maintainer for the equivalent bot-side
        // flow) - hCaptcha's checksiteconfig only needs a "host" value it
        // recognizes, not for the page to actually be served from there.
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
