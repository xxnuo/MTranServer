CXX ?= c++
CPPFLAGS ?=
CXXFLAGS ?= -O2 -g
TEST_FLAGS = -std=c++20 -Wall -Wextra -Werror -Wpedantic -ffp-contract=off -pthread
SOURCES = ssru.cc ssru_test.cc
OUTPUT = .tmp

.PHONY: test sanitize

test: $(OUTPUT)/ssru-test
	./$(OUTPUT)/ssru-test

sanitize: $(OUTPUT)/ssru-test-sanitize
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OUTPUT)/ssru-test-sanitize

$(OUTPUT):
	mkdir -p $@

$(OUTPUT)/ssru-test: $(SOURCES) ssru.h Makefile | $(OUTPUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_FLAGS) $(SOURCES) $(LDFLAGS) -o $@

$(OUTPUT)/ssru-test-sanitize: $(SOURCES) ssru.h Makefile | $(OUTPUT)
	$(CXX) $(CPPFLAGS) -O1 -g $(TEST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) $(LDFLAGS) -o $@
