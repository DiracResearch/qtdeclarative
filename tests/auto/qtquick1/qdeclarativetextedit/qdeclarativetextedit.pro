load(qttest_p4)
contains(QT_CONFIG,declarative): QT += declarative gui network qtquick1
macx:CONFIG -= app_bundle

SOURCES += tst_qdeclarativetextedit.cpp ../../declarative/shared/testhttpserver.cpp
HEADERS += ../../declarative/shared/testhttpserver.h

symbian: {
    importFiles.files = data
    importFiles.path = .
    DEPLOYMENT += importFiles
} else {
    DEFINES += SRCDIR=\\\"$$PWD\\\"
}
QT += core-private gui-private v8-private declarative-private qtquick1-private
