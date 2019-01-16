include ../MakeVars

CC			= g++ -Wno-write-strings #remove const char warning
AR			= ar

CFLAG       = $(CFLAGS) -g -DHAVE_INTTYPE_H -std=c++11
INC_PATH    = -I. -I../build/include
LIB_PATH    = -L../build/lib
LIBS        = -lpthread -ldl -lrt -lm -luuid

# TODO!!! make libzmq that include all needed libraries (event, zmq ...)
BUILD_INC_PATH  = -I../build/include -I../build/include/glib-2.0 -I../build/lib/glib-2.0/include
BUILD_LIBS_PATH = ../build/lib
BUILD_LIBS =./lib_wrapzmq.a \
             $(BUILD_LIBS_PATH)/libzmq.a \
			 $(BUILD_LIBS_PATH)/libevent.a \
			 $(BUILD_LIBS_PATH)/libevent_pthreads.a \
			 $(BUILD_LIBS_PATH)/libglib-2.0.a 

all: lib_wrapzmq broker hub ahif regib smsib psib feaib perfs perfr

clean:
	rm -rf *.o *.ipc *.dat *.png core.* \
		*.pid *.log \
		lib_wrapzmq.a \
		broker hub \
		ahif regib smsib psib feaib \
		perfs perfr 

test: test.c
	$(CC) $(CFLAG) -o $@ $< $(BUILD_INC_PATH) $(INC_PATH) $(BUILD_LIBS) $(LIB_PATH) $(LIBS) 

lib_wrapzmq: zmq_wrapper.c
	$(CC) $(CFLAGS) -c $< $(BUILD_INC_PATH) $(INC_PATH) 
	$(AR) crv lib_wrapzmq.a zmq_wrapper.o

broker: broker.c
	$(CC) $(CFLAG) -o $@ $< $(BUILD_INC_PATH) $(INC_PATH) $(BUILD_LIBS) $(LIB_PATH) $(LIBS) 

hub: hub.c
	$(CC) $(CFLAG) -o $@ $< $(BUILD_INC_PATH) $(INC_PATH) $(BUILD_LIBS) $(LIB_PATH) $(LIBS) 

ahif: ahif.c
	$(CC) $(CFLAG) -o $@ $< $(BUILD_INC_PATH) $(INC_PATH) $(BUILD_LIBS) $(LIB_PATH) $(LIBS) 

regib: regib.c
	$(CC) $(CFLAG) -o $@ $< $(BUILD_INC_PATH) $(INC_PATH) $(BUILD_LIBS) $(LIB_PATH) $(LIBS) 

smsib: smsib.c
	$(CC) $(CFLAG) -o $@ $< $(BUILD_INC_PATH) $(INC_PATH) $(BUILD_LIBS) $(LIB_PATH) $(LIBS) 

psib: psib.c
	$(CC) $(CFLAG) -o $@ $< $(BUILD_INC_PATH) $(INC_PATH) $(BUILD_LIBS) $(LIB_PATH) $(LIBS) 

feaib: feaib.c
	$(CC) $(CFLAG) -o $@ $< $(BUILD_INC_PATH) $(INC_PATH) $(BUILD_LIBS) $(LIB_PATH) $(LIBS) 

perfs: perfs.c
	$(CC) $(CFLAG) -o $@ $< $(BUILD_INC_PATH) $(INC_PATH) $(BUILD_LIBS) $(LIB_PATH) $(LIBS) 

perfr: perfr.c
	$(CC) $(CFLAG) -o $@ $< $(BUILD_INC_PATH) $(INC_PATH) $(BUILD_LIBS) $(LIB_PATH) $(LIBS) 
