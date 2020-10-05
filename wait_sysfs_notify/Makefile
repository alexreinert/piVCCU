CXX = g++
CXXFLAGS = -static-libstdc++
OBJS = main.o

all: wait_sysfs_notify

wait_sysfs_notify: $(OBJS)
	$(LINK.cc) $(OBJS) -o $@

clean:
	rm -f $(OBJS) wait_sysfs_notify

