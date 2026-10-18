CXX ?= g++
# The vocoder tables use brace constants that do not fit in signed 16-bit.
# -Wno-narrowing keeps that C++98 code building as C++17.
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wno-unused-parameter -Wno-narrowing
CPPFLAGS += -Ithird_party/imbe_vocoder/src/lib -Isrc

VOCODER := $(wildcard third_party/imbe_vocoder/src/lib/*.cc)
SRCS := src/main.cc src/ldu.cc $(VOCODER)

PREFIX ?= $(HOME)/.local
MODEMS ?= $(HOME)/.local/share/hamspot-commander/modems

.PHONY: all clean test install

all: p25voice

p25voice: $(SRCS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $(SRCS)

test: p25voice
	./p25voice --self-test

install: p25voice
	install -d -m 755 "$(PREFIX)/bin" "$(MODEMS)"
	install -m 755 p25voice "$(PREFIX)/bin/p25voice"
	install -m 755 examples/commander-mod-p25.txpipe "$(MODEMS)/commander-mod-p25.txpipe"
	ln -sfn "$(MODEMS)/commander-mod-p25.txpipe" "$(PREFIX)/bin/commander-mod-p25.txpipe"

clean:
	rm -f p25voice
