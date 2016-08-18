GCC_ROOT=/usr
GCC=$(GCC_ROOT)/bin/gcc
GXX=$(GCC_ROOT)/bin/g++
ZEROMQ_ROOT=/usr/local/Cellar/zeromq/4.1.4
ZEROMQ_INC=$(ZEROMQ_ROOT)/include
ZEROMQ_LIB=$(ZEROMQ_ROOT)/lib
GXX_FLAGS_ZMQ=-I$(ZEROMQ_INC) -L$(ZEROMQ_LIB) -lzmq -lev #-Wl,-R,$(ZEROMQ_LIB)
LIBEV_ROOT=/usr/local/Cellar/libev/4.22
LIBEV_INC=$(LIBEV_ROOT)/include
LIBEV_LIB=$(LIBEV_ROOT)/lib
GXX_FLAGS_EV=-I$(LIBEV_INC) -L$(LIBEV_LIB) -lev #-Wl,-R,$(LIBEV_LIB)

#GXX_FLAGS= -O3 $(GXX_FLAGS_ZMQ) $(GXX_FLAGS_EV) -Wl,-R,$(GCC_ROOT)/lib64
#GXX_FLAGS= -std=c++11 -O3 $(GXX_FLAGS_EV)
GXX_FLAGS= -O3 $(GXX_FLAGS_ZMQ)

all: concur.x
#concurrency.x inproc_thr.x ev_zmq_test.x concur.x
#asyncsrv.x

%.x: %.cpp
	$(GXX) -o $@ -DAS_X $(GXX_FLAGS) $<

%.x1: %.cpp
	$(GXX) -o $@ -DAS_X1 $(GXX_FLAGS) $<

%.so: %.cpp
	$(GCC) -o $@ -fPIC -shared $(GXX_FLAGS) $<

clean:
	rm -f *.x *.so *.x1
