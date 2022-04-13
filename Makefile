SRC_DIR := src
BUILD_DIR := build
INCLUDE_DIR := include

SRC_FILES := $(wildcard $(SRC_DIR)/*.cpp)
OBJ_FILES := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRC_FILES))

LDLIBS := -lpthread -lasound
CPPFLAGS := -Wall -Wextra -Werror -pedantic

synth_controller: $(OBJ_FILES)
	g++ $(LDLIBS) -o $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	g++ $(CPPFLAGS) -c -o $@ $<

clean:
	rm -f synth_controller $(BUILD_DIR)/*.o

#-include $(SRC_FILES:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.d)


