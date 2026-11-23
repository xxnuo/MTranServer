CXX ?= c++
CPPFLAGS ?=
CXXFLAGS ?= -O2 -g
TEST_FLAGS = -std=c++20 -Wall -Wextra -Werror -Wpedantic -ffp-contract=off -pthread
SOURCES = ssru.cc ssru_test.cc
OUTPUT = .tmp

.PHONY: test sanitize packed-test packed-sanitize packed-vnni-test packed-vnni-sanitize ops-test ops-sanitize

$(OUTPUT):
	mkdir -p $(OUTPUT)

test: $(OUTPUT)/ssru-test
	./$(OUTPUT)/ssru-test

sanitize: $(OUTPUT)/ssru-test-sanitize
	./$(OUTPUT)/ssru-test-sanitize

packed-test: $(OUTPUT)/packed-test $(OUTPUT)/packed-test-no-vnni
	./$(OUTPUT)/packed-test
	./$(OUTPUT)/packed-test-no-vnni

packed-sanitize: $(OUTPUT)/packed-test-sanitize $(OUTPUT)/packed-test-no-vnni-sanitize
	./$(OUTPUT)/packed-test-sanitize
	./$(OUTPUT)/packed-test-no-vnni-sanitize

packed-vnni-test: $(OUTPUT)/packed-vnni-test
	./$(OUTPUT)/packed-vnni-test

packed-vnni-sanitize: $(OUTPUT)/packed-vnni-sanitize
	./$(OUTPUT)/packed-vnni-sanitize

ops-test: $(OUTPUT)/ops-test
	./$(OUTPUT)/ops-test

ops-sanitize: $(OUTPUT)/ops-sanitize
	./$(OUTPUT)/ops-sanitize

$(OUTPUT)/ssru-test: $(SOURCES) ssru.h Makefile | $(OUTPUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_FLAGS) $(SOURCES) $(LDFLAGS) -o $@

$(OUTPUT)/ssru-test-sanitize: $(SOURCES) ssru.h Makefile | $(OUTPUT)
	$(CXX) $(CPPFLAGS) -O1 -g $(TEST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) $(LDFLAGS) -o $@

$(OUTPUT)/pcore_pool.o: pcore_pool.cc pcore_pool.h Makefile | $(OUTPUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_FLAGS) -c $< -o $@

$(OUTPUT)/pcore_pool-sanitize.o: pcore_pool.cc pcore_pool.h Makefile | $(OUTPUT)
	$(CXX) $(CPPFLAGS) -O1 -g $(TEST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer -c $< -o $@

$(OUTPUT)/packed-test: packed.cc packed_test.cc packed.h kleidi.h pcore_pool.h Makefile $(OUTPUT)/pcore_pool.o | $(OUTPUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_FLAGS) packed.cc packed_test.cc $(OUTPUT)/pcore_pool.o $(LDFLAGS) -o $@

$(OUTPUT)/packed-test-no-vnni: packed.cc packed_test.cc packed.h kleidi.h pcore_pool.h Makefile $(OUTPUT)/pcore_pool.o | $(OUTPUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_FLAGS) -DMTRAN_SSRU_NO_VNNI -DMTRAN_SSRU_NO_DOTPROD packed.cc packed_test.cc $(OUTPUT)/pcore_pool.o $(LDFLAGS) -o $@

$(OUTPUT)/packed-test-sanitize: packed.cc packed_test.cc packed.h kleidi.h pcore_pool.h Makefile $(OUTPUT)/pcore_pool-sanitize.o | $(OUTPUT)
	$(CXX) $(CPPFLAGS) -O1 -g $(TEST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer packed.cc packed_test.cc $(OUTPUT)/pcore_pool-sanitize.o $(LDFLAGS) -o $@

$(OUTPUT)/packed-test-no-vnni-sanitize: packed.cc packed_test.cc packed.h kleidi.h pcore_pool.h Makefile $(OUTPUT)/pcore_pool-sanitize.o | $(OUTPUT)
	$(CXX) $(CPPFLAGS) -O1 -g $(TEST_FLAGS) -DMTRAN_SSRU_NO_VNNI -DMTRAN_SSRU_NO_DOTPROD -fsanitize=address,undefined -fno-omit-frame-pointer packed.cc packed_test.cc $(OUTPUT)/pcore_pool-sanitize.o $(LDFLAGS) -o $@

$(OUTPUT)/packed-vnni.o: packed_vnni.cc packed.h Makefile | $(OUTPUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_FLAGS) -Wno-unused-parameter -mavx2 -mavxvnni -mfma -c $< -o $@

$(OUTPUT)/packed-vnni-sanitize.o: packed_vnni.cc packed.h Makefile | $(OUTPUT)
	$(CXX) $(CPPFLAGS) -O1 -g $(TEST_FLAGS) -Wno-unused-parameter -mavx2 -mavxvnni -mfma -fsanitize=address,undefined -fno-omit-frame-pointer -c $< -o $@

$(OUTPUT)/packed-avx512vnni.o: packed_avx512vnni.cc packed.h Makefile | $(OUTPUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_FLAGS) -Wno-unused-parameter -mavx512f -mavx512bw -mavx512dq -mavx512vl -mavx512vnni -mfma -c $< -o $@

$(OUTPUT)/packed-avx512vnni-sanitize.o: packed_avx512vnni.cc packed.h Makefile | $(OUTPUT)
	$(CXX) $(CPPFLAGS) -O1 -g $(TEST_FLAGS) -Wno-unused-parameter -mavx512f -mavx512bw -mavx512dq -mavx512vl -mavx512vnni -mfma -fsanitize=address,undefined -fno-omit-frame-pointer -c $< -o $@

$(OUTPUT)/packed-vnni-test: packed.cc packed_test.cc packed.h kleidi.h pcore_pool.h Makefile $(OUTPUT)/pcore_pool.o $(OUTPUT)/packed-vnni.o $(OUTPUT)/packed-avx512vnni.o | $(OUTPUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_FLAGS) -DMTRAN_SSRU_HAVE_VNNI -DMTRAN_SSRU_HAVE_AVX512VNNI packed.cc packed_test.cc $(OUTPUT)/pcore_pool.o $(OUTPUT)/packed-vnni.o $(OUTPUT)/packed-avx512vnni.o $(LDFLAGS) -o $@

$(OUTPUT)/packed-vnni-sanitize: packed.cc packed_test.cc packed.h kleidi.h pcore_pool.h Makefile $(OUTPUT)/pcore_pool-sanitize.o $(OUTPUT)/packed-vnni-sanitize.o $(OUTPUT)/packed-avx512vnni-sanitize.o | $(OUTPUT)
	$(CXX) $(CPPFLAGS) -O1 -g $(TEST_FLAGS) -DMTRAN_SSRU_HAVE_VNNI -DMTRAN_SSRU_HAVE_AVX512VNNI -fsanitize=address,undefined -fno-omit-frame-pointer packed.cc packed_test.cc $(OUTPUT)/pcore_pool-sanitize.o $(OUTPUT)/packed-vnni-sanitize.o $(OUTPUT)/packed-avx512vnni-sanitize.o $(LDFLAGS) -o $@

$(OUTPUT)/ops-test: pcore_pool.cc ops.cc ops_test.cc pcore_pool.h ops.h Makefile | $(OUTPUT)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_FLAGS) pcore_pool.cc ops.cc ops_test.cc $(LDFLAGS) -o $@

$(OUTPUT)/ops-sanitize: pcore_pool.cc ops.cc ops_test.cc pcore_pool.h ops.h Makefile | $(OUTPUT)
	$(CXX) $(CPPFLAGS) -O1 -g $(TEST_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer pcore_pool.cc ops.cc ops_test.cc $(LDFLAGS) -o $@
