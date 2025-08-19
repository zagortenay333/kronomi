.SILENT:
.PHONY := release debug asan pp clean_pp bt clean run_no_aslr run loc

SRC_DIR       := src
SRC_FILES     := $(shell find $(SRC_DIR) \
				   -path $(SRC_DIR)"/gfx" -prune -false -o \
				   -path $(SRC_DIR)"/os/linux" -prune -false -o \
				   -iname *.cpp)
OBJ_FILES     := $(SRC_FILES:.cpp=.o)
DEP_FILES     := $(SRC_FILES:.cpp=.dep)
EXE           := test.bin
CXX            := g++
RELEASE_FLAGS := -fno-omit-frame-pointer -g -O2 -DBUILD_RELEASE=1 -DBUILD_DEBUG=0 -DNDEBUG -Wno-unused-parameter
DEBUG_FLAGS   := -g3 -DBUILD_RELEASE=0 -DBUILD_DEBUG=1 -fno-omit-frame-pointer
CPPFLAGS      := -std=c++11 -fno-delete-null-pointer-checks -fno-strict-aliasing -fwrapv -Werror=vla \
                 -Wall -Wextra -Wimplicit-fallthrough -Wswitch -Wno-unused-function -Wno-unused-value -Wno-unused-parameter -Wno-missing-braces \
				 -I$(SRC_DIR) -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=600
LDFLAGS       := -fuse-ld=mold -lm -lGL -lX11 -lpthread -lXrandr -lXi -ldl

ifeq ($(CXX), clang++)
	CPPFLAGS  += -ferror-limit=2 -fno-spell-checking -Wno-initializer-overrides
	CXX_DEPGEN := clang++
else ifeq ($(CXX), g++)
	CPPFLAGS  += -fmax-errors=2 -Wno-empty-body -Wno-missing-field-initializers
	CXX_DEPGEN := g++
endif

ifdef CXX_DEPGEN
	# Create dependencies between .cpp files and .h files they include.
	# This needs compiler support. Clang and gcc provide the MM flag.
    -include $(DEP_FILES)
    %.dep: %.cpp
		$(CXX_DEPGEN) $(CPPFLAGS) $< -MM -MT $(@:.dep=.o) >$@
endif

$(EXE): $(OBJ_FILES)
	@$(CXX) $(CPPFLAGS) $^ -o $@ $(LDFLAGS)

release: CPPFLAGS  += $(RELEASE_FLAGS) -Wno-unused -g # -flto
release: LDFLAGS += # -flto
release: $(EXE)

debug: CPPFLAGS  += $(DEBUG_FLAGS)
debug: LDFLAGS += -fsanitize=address # For asan stack traces.
debug: $(EXE)

asan: CPPFLAGS  += -fsanitize=address,undefined -DASAN_ENABLED=1 $(DEBUG_FLAGS)
asan: LDFLAGS += -fsanitize=address,undefined
asan: $(EXE)

pp:
	$(foreach f, $(SRC_FILES), $(CXX) -E -P $(CPPFLAGS) $(f) > $(f:.cpp=.pp);)

clean_pp:
	rm -rf $(SRC_FILES:.cpp=.pp)

bt:
	coredumpctl debug

clean:
	rm -rf $(EXE) $(SRC_FILES:.cpp=.pp) $(DEP_FILES) $(OBJ_FILES) $(COVERAGE_DIR)

run_no_aslr:
	setarch $(uname -m) -R ./$(EXE)

run:
	ASAN_OPTIONS=symbolize=1:detect_leaks=0:abort_on_error=1:disable_coredump=0:umap_shadow_on_exit=1\
		./$(EXE)

loc:
	find $(SRC_DIR) -path $(SRC_DIR)"/vendor" -prune -false -o -iname "*.cpp" -o -iname "*.h" | xargs wc -l | sort -g -r
