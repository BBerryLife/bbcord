#ifndef STREAMSCANNER_HPP
#define STREAMSCANNER_HPP

// Pulls session_id and resume_gateway_url out of a READY payload WITHOUT holding the
// payload. A user-token READY is ~5 MB of JSON; the headless service has a 3 MB heap
// limit, so it feeds the inflated bytes through this scanner chunk by chunk and throws
// them away. Plain C++ (no Qt) so it can be unit-tested on a desktop.
//
// Only strings that are direct members of the "d" object are captured: the payload is
// {"t":"READY","s":1,"op":0,"d":{...}}, i.e. depth 2. The "sessions" array inside d also
// has "session_id" keys, but those sit deeper and are ignored.

#include <string>

class ReadyScanner {
public:
    ReadyScanner() { reset(); }

    void reset()
    {
        m_depth = 0;
        m_inString = false;
        m_escape = false;
        m_capturing = false;
        m_captureIsKey = false;
        m_expectValue = false;
        m_valueTarget = 0;
        m_buf.clear();
        m_lastKey.clear();
        m_sessionId.clear();
        m_resumeUrl.clear();
    }

    void feed(const char *data, int length)
    {
        for (int i = 0; i < length; ++i) feedByte(data[i]);
    }

    bool complete() const { return !m_sessionId.empty() && !m_resumeUrl.empty(); }
    const std::string &sessionId() const { return m_sessionId; }
    const std::string &resumeUrl() const { return m_resumeUrl; }

private:
    enum { kMaxCapture = 512 };

    void feedByte(char c)
    {
        if (m_inString) {
            if (m_escape) {
                m_escape = false;
                if (m_capturing && m_buf.size() < kMaxCapture) {
                    // Only \/ matters (resume_gateway_url is written "wss:\/\/..."); keep the
                    // escaped character as is for anything else.
                    m_buf.push_back(c);
                }
                return;
            }
            if (c == '\\') { m_escape = true; return; }
            if (c == '"') { m_inString = false; endString(); return; }
            if (m_capturing && m_buf.size() < kMaxCapture) m_buf.push_back(c);
            return;
        }

        switch (c) {
        case '"':
            m_inString = true;
            m_buf.clear();
            m_capturing = false;
            if (m_depth == 2) {
                if (!m_expectValue) {
                    m_capturing = true;
                    m_captureIsKey = true;
                } else if (m_valueTarget != 0) {
                    m_capturing = true;
                    m_captureIsKey = false;
                }
            }
            break;
        case '{':
        case '[':
            ++m_depth;
            break;
        case '}':
        case ']':
            --m_depth;
            if (m_depth == 2) m_expectValue = false;
            break;
        case ':':
            if (m_depth == 2) {
                m_expectValue = true;
                m_valueTarget = (m_lastKey == "session_id") ? 1
                              : (m_lastKey == "resume_gateway_url") ? 2 : 0;
            }
            break;
        case ',':
            if (m_depth == 2) { m_expectValue = false; m_valueTarget = 0; }
            break;
        default:
            break;
        }
    }

    void endString()
    {
        if (!m_capturing) return;
        m_capturing = false;
        if (m_captureIsKey) {
            m_lastKey = m_buf;
        } else if (m_valueTarget == 1) {
            m_sessionId = m_buf;
        } else if (m_valueTarget == 2) {
            m_resumeUrl = m_buf;
        }
    }

    int m_depth;
    bool m_inString;
    bool m_escape;
    bool m_capturing;
    bool m_captureIsKey;
    bool m_expectValue;
    int m_valueTarget; // 0 none, 1 session_id, 2 resume_gateway_url
    std::string m_buf;
    std::string m_lastKey;
    std::string m_sessionId;
    std::string m_resumeUrl;
};

#endif
