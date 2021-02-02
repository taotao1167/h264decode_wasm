ifeq ($(OS), Windows_NT)
	PLATFORM := "Windows"
else
	PLATFORM := "Linux"
endif

#source file
OBJS_C := get_frames.o get_usbimgs.o web_dispatch.o main.o

OBJS_CPP :=

#compile and lib parameter
CC      := gcc
CXX     := g++
LIBS    := -lavdevice -lavcodec -lavformat -lavutil -lswscale -lwebserver -lpthread -levent -levent_openssl -lcrypto -lssl -lzmq
DEFINES := -DWATCH_RAM -DWITH_IPV6 -DWITH_WEBSOCKET -DWITH_SSL
ifeq ($(PLATFORM), "Windows")
	LIBS    +=
	LDFLAGS := -L/mingw64/lib
	INCLUDE := -I/mingw64/include
else
	LDFLAGS := -L/usr/local/lib -L./webserver/lib
	INCLUDE := -I/usr/local/include -I/usr/local/include/opencv4 -I./webserver/include
endif

CFLAGS  := -g -Wall -pthread -O3 $(DEFINES) $(INCLUDE)
CXXFLAGS := $(CFLAGS)

all: app

app: libwebserver ${OBJS_C} ${OBJS_CPP}
	$(CC) $(CFLAGS) ${OBJS_C} ${OBJS_CPP} ${LDFLAGS} ${LIBS} -o $@

${OBJS_C}: %.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

${OBJS_CPP}: %.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

libwebserver:
	@${MAKE} -C webserver

clean:
	@rm -f *.o
	@rm -f app

