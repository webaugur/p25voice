CXX ?= g++
# The vocoder tables use brace constants that do not fit in signed 16-bit.
# -Wno-narrowing keeps that C++98 code building as C++17.
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wno-unused-parameter -Wno-narrowing
CPPFLAGS += -Ithird_party/imbe_vocoder/src/lib -Isrc

VOCODER := $(wildcard third_party/imbe_vocoder/src/lib/*.cc)
SRCS := src/main.cc src/ldu.cc $(VOCODER)

.PHONY: all clean test

all: p25voice

p25voice: $(SRCS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $(SRCS)

test: p25voice
	./p25voice --self-test

clean:
	rm -f p25voice
