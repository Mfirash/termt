CXX = g++
CXXFLAGS = -std=c++17 `pkg-config gtkmm-3.0 --cflags`
# Added -lwinpty for Windows builds
LIBS = `pkg-config gtkmm-3.0 --libs` -luv -lpthread -lwinpty

BUILD_DIR = build
TARGET = term

SRCS = main.cpp termwidget.cpp terminalgridmanager.cpp
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.cpp=.o))

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LIBS) -mwindows

$(BUILD_DIR)/%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean