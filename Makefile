CXX = g++
CXXFLAGS = $(shell pkg-config --cflags gtk+-3.0 appindicator3-0.1) -Wall -O2
LDFLAGS = $(shell pkg-config --libs gtk+-3.0 appindicator3-0.1) -lpthread
TARGET = bin/xlcppl
BIN_DIR = bin
SRC = xlcppl.cpp edlock.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p $(BIN_DIR)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)
	@echo "#!/bin/bash" > start.sh
	@echo "export GDK_BACKEND=x11" >> start.sh
	@echo "./$(TARGET)" >> start.sh
	@chmod +x start.sh
	@echo "Created start.sh"

clean:
	rm -rf $(BIN_DIR) start.sh

setcap: $(TARGET)
	sudo setcap cap_net_raw,cap_net_admin+eip $(TARGET)

.PHONY: all clean setcap
