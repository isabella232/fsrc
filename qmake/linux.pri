
QMAKE_CXXFLAGS_RELEASE += -msse2 -Ofast
QMAKE_LFLAGS_RELEASE += -flto

LIBS += -lstdc++fs -lpthread -lrt

# QMAKE_CC = gcc-9
# QMAKE_CXX = g++-9
