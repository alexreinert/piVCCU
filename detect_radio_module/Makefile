CXX = g++
CXXFLAGS = -pthread -static-libstdc++ 
OBJS = main.o hmframe.o streamparser.o radiomoduleconnector.o radiomoduledetector.o

all: detect_radio_module

detect_radio_module: $(OBJS)
	$(LINK.cc) $(OBJS) -o $@

clean:
	rm -f $(OBJS) detect_radio_module

