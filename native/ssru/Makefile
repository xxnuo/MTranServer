CXX ?= c++
CPPFLAGS ?=
CXXFLAGS ?= -O2 -g
TEST_FLAGS = -std=c++20 -Wall -Wextra -Werror -Wpedantic -ffp-contract=off -pthread
SOURCES = ssru.cc ssru_test.cc
OUTPUT = .tmp

.PHONY: test sanitize packed-test packed-sanitize packed-vnni-test packed-vnni-sanitize

test: $(OUTPUT)/ssru-test
	./$(OUTPUT)/ssru-test

sanitize: $(OUTPUT)/ssru-test-sanitize
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OUTPUT)/ssru-test-sanitize

# Keep global ISA flags off: packed.cc isolates AVX2/VNNI functions itself.
packed-test: $(OUTPUT)/packed-test $(OUTPUT)/packed-test-no-vnni
	./$(OUTPUT)/packed-test
	./$(OUTPUT)/packed-test-no-vnni

packed-sanitize: $(OUTPUT)/packed-test-sanitize $(OUTPUT)/packed-test-no-vnni-sanitize
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OUTPUT)/packed-test-sanitize
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OUTPUT)/packed-test-no-vnni-sanitize

# Optional x64-only targets. Only this object may contain VNNI instructions.
packed-vnni-test: $(OUTPUT)/packed-vnni-test
	./$(OUTPUT)/packed-vnni-test

packed-vnni-sanitize: $(OUTPUT)/packed-vnni-sanitize
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ./$(OUTPUT)/packed-vnni-sanitize

$(OUTPUT):
	mkdir -p $@

$(OUTPUT)/ssru-test: $(SOURCES) ssru.h Makefile | $(OUTPUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_FLAGS) $(SOURCES) $(LDFLAGS) -o $@

$(OUTPUT)/ssru-test-sanitize: $(SOURCES) ssru.h Makefile | $(OUTPUT)
	$(CXX) $(CPPFLAGS) -O1 -g $(TEST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) $(LDFLAGS) -o $@

$(OUTPUT)/packed-test: packed.cc packed_test.cc packed.h Makefile | $(OUTPUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_FLAGS) packed.cc packed_test.cc $(LDFLAGS) -o $@

$(OUTPUT)/packed-test-no-vnni: packed.cc packed_test.cc packed.h Makefile | $(OUTPUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_FLAGS) -DMTRAN_SSRU_NO_VNNI -DMTRAN_SSRU_NO_DOTPROD packed.cc packed_test.cc $(LDFLAGS) -o $@

$(OUTPUT)/packed-test-sanitize: packed.cc packed_test.cc packed.h Makefile | $(OUTPUT)
	$(CXX) $(CPPFLAGS) -O1 -g $(TEST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer packed.cc packed_test.cc $(LDFLAGS) -o $@

$(OUTPUT)/packed-test-no-vnni-sanitize: packed.cc packed_test.cc packed.h Makefile | $(OUTPUT)
	$(CXX) $(CPPFLAGS) -O1 -g $(TEST_FLAGS) -DMTRAN_SSRU_NO_VNNI -DMTRAN_SSRU_NO_DOTPROD -fsanitize=address,undefined -fno-omit-frame-pointer packed.cc packed_test.cc $(LDFLAGS) -o $@

$(OUTPUT)/packed-vnni.o: packed_vnni.cc Makefile | $(OUTPUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_FLAGS) -Wno-unused-parameter -mavx2 -mavxvnni -isystem third_party/xsimd/include -c $< -o $@

$(OUTPUT)/packed-vnni-sanitize.o: packed_vnni.cc Makefile | $(OUTPUT)
	$(CXX) $(CPPFLAGS) -O1 -g $(TEST_FLAGS) -Wno-unused-parameter -mavx2 -mavxvnni -isystem third_party/xsimd/include -fsanitize=address,undefined -fno-omit-frame-pointer -c $< -o $@

$(OUTPUT)/packed-vnni-test: packed.cc packed_test.cc packed.h Makefile $(OUTPUT)/packed-vnni.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_FLAGS) -DMTRAN_SSRU_HAVE_VNNI packed.cc packed_test.cc $(OUTPUT)/packed-vnni.o $(LDFLAGS) -o $@

$(OUTPUT)/packed-vnni-sanitize: packed.cc packed_test.cc packed.h Makefile $(OUTPUT)/packed-vnni-sanitize.o
	$(CXX) $(CPPFLAGS) -O1 -g $(TEST_FLAGS) -DMTRAN_SSRU_HAVE_VNNI -fsanitize=address,undefined -fno-omit-frame-pointer packed.cc packed_test.cc $(OUTPUT)/packed-vnni-sanitize.o $(LDFLAGS) -o $@
