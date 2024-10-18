# Define compiler and flags
CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra

# Define the target executable
TARGET = flow

# Source files
SRC = hello.cpp

# Build rules
all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)
