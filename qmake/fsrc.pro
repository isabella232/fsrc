MAIN_DIR =..
DESTDIR=$${MAIN_DIR} # to test on itself
SRC_DIR  =../src
INCLUDEPATH += $${SRC_DIR}

SOURCES += $${SRC_DIR}/fsrc.cpp

HEADERS += $${SRC_DIR}/utils.hpp
SOURCES += $${SRC_DIR}/utils.cpp

HEADERS += $${SRC_DIR}/searcher.hpp
SOURCES += $${SRC_DIR}/searcher.cpp

HEADERS += $${SRC_DIR}/searchoptions.hpp
SOURCES += $${SRC_DIR}/searchoptions.cpp

HEADERS += $${SRC_DIR}/stopwatch.hpp

HEADERS += $${SRC_DIR}/threadpool.hpp
SOURCES += $${SRC_DIR}/threadpool.cpp

# via https://mischasan.wordpress.com/2011/07/16/convergence-sse2-and-strstr/
HEADERS += $${SRC_DIR}/ssestr.hpp

# version
DEFINES += 'GIT_TAG=\\\"$$system(git describe --abbrev=0)\\\"'

include( options.pri )
include( setup.pri )
linux: include( linux.pri )
win32: include( win.pri )
macx:  include( mac.pri )

include( boost.pri )
