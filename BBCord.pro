APP_NAME = BBCord

CONFIG += qt warn_on cascades10

QT += network sql

LIBS += -lbb
LIBS += -lbbdata
LIBS += -lbbmultimedia
LIBS += -lbbsystem
LIBS += -lsocket
LIBS += -lm
LIBS += -lz
# -lunifieddatasourcec: thư viện thật của UDS API
# (bb/pim/unified/unified_data_source.h), dùng bởi HubIntegration.cpp để
# đăng ký BBCord thành 1 tab riêng trong BlackBerry Hub. Tên thư viện thật
# theo header/doc chính thức của BlackBerry là "unifieddatasourcec", KHÔNG
# phải "uds" — đã xác nhận qua thực tế build bên Zalo10 (cùng tác giả,
# cùng kiến trúc UDS, xem Zalo10.pro).
LIBS += -lunifieddatasourcec
# -lbbpim: namespace bb::pim, cần cho unified_data_source.h ở trên.
LIBS += -lbbpim

SOURCES += third_party/mongoose/mongoose.c
HEADERS += third_party/mongoose/mongoose.h

INCLUDEPATH += $$PWD/third_party/mongoose
DEFINES += MG_TLS=MG_TLS_OPENSSL
DEFINES += MG_ENABLE_POLL=1
DEFINES += MG_ENABLE_LOG=0
DEFINES += \
    MG_ENABLE_OPENSSL=1 \
    MG_MAX_RECV_SIZE=67108864 \
    MG_ENABLE_LINES=1

QMAKE_CFLAGS += -Wc,-std=gnu99
QMAKE_CFLAGS += -Wc,"-DMG_TLS=MG_TLS_OPENSSL"
QMAKE_CFLAGS += -Wc,"-DMG_ENABLE_OPENSSL=1"
QMAKE_CFLAGS += -Wc,"-DMG_ENABLE_POLL=1"
QMAKE_CFLAGS += -Wc,"-DMG_ENABLE_LOG=0"
QMAKE_CFLAGS += -Wc,"-DMG_MAX_RECV_SIZE=67108864"

simulator {
    INCLUDEPATH += $$PWD/third_party/prebuilt/simulator/openssl/include
    LIBS += $$PWD/third_party/prebuilt/simulator/openssl/lib/libssl.a
    LIBS += $$PWD/third_party/prebuilt/simulator/openssl/lib/libcrypto.a
}

device {
    INCLUDEPATH += $$PWD/third_party/prebuilt/device/openssl/include
    LIBS += $$PWD/third_party/prebuilt/device/openssl/lib/libssl.a
    LIBS += $$PWD/third_party/prebuilt/device/openssl/lib/libcrypto.a
}

include(config.pri)

CONFIG -= precompile_header
CONFIG -= precompiled_header
PRECOMPILED_HEADER =
QMAKE_CFLAGS_USE_PRECOMPILE =
QMAKE_CXXFLAGS_USE_PRECOMPILE =
QMAKE_CFLAGS_PRECOMPILE =
QMAKE_CXXFLAGS_PRECOMPILE =

