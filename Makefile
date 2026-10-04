# ThermalSense build.
#   Windows (MinGW-w64, Git Bash / MSYS):  mingw32-make -j8          -> ThermalSense.exe (GLFW from lib/)
#   Linux (RTX remote machine):            make -j$(nproc)           -> ./ThermalSense   (system GLFW, see README)
#   Linux, headless only (no GLFW needed): make -j HEADLESS=1        -> capture / record / benchmark over SSH (EGL)
CXX      ?= g++
CC       ?= gcc
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Isrc -Iinclude -MMD -MP
CFLAGS   = -O2 -Iinclude

ifeq ($(OS),Windows_NT)
  EXE          = .exe
  LDFLAGS      = -Llib -lglfw3 -lopengl32 -lgdi32 -static -static-libgcc -static-libstdc++
  TEST_LDFLAGS = -static -static-libgcc -static-libstdc++
else
  EXE          =
  TEST_LDFLAGS =
  ifeq ($(HEADLESS),1)
    CXXFLAGS += -DTS_NO_GLFW
    LDFLAGS   = -ldl -lpthread
  else
    GLFW_LIBS := $(shell pkg-config --libs glfw3 2>/dev/null)
    LDFLAGS   = $(if $(GLFW_LIBS),$(GLFW_LIBS),-lglfw) -ldl -lpthread
  endif
endif

OBJ      = build/obj$(if $(filter 1,$(HEADLESS)),-headless,)
APP      = ThermalSense$(EXE)
TESTS    = build/tests$(EXE)
EXPORTER = build/export_docs$(EXE)

APP_SRC  = $(wildcard src/*.cpp src/*/*.cpp)
CORE_SRC = $(filter-out src/main.cpp src/app/% src/rendering/%,$(APP_SRC))
APP_OBJ  = $(patsubst src/%.cpp,$(OBJ)/%.o,$(APP_SRC)) $(OBJ)/glad.o
TEST_OBJ = $(patsubst src/%.cpp,$(OBJ)/%.o,$(CORE_SRC)) $(patsubst tests/%.cpp,$(OBJ)/tests/%.o,$(wildcard tests/*.cpp))

all: $(APP) $(TESTS)

$(APP): $(APP_OBJ)
	$(CXX) $^ $(LDFLAGS) -o $@

$(TESTS): $(TEST_OBJ)
	$(CXX) $^ $(TEST_LDFLAGS) -o $@

$(OBJ)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ)/tests/%.o: tests/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ)/glad.o: src/glad.c
	@mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TESTS)
	./$(TESTS) build/test_results.json

# Headless (GPU-free) exporter, built in one call straight from the sources so it never touches build/obj.
EXPORT_SRC = $(CORE_SRC) src/app/DocExport.cpp tools/export_docs_main.cpp

$(EXPORTER): $(EXPORT_SRC) $(wildcard src/*/*.h)
	@mkdir -p build
	$(CXX) -std=c++17 -O1 -Isrc -Iinclude $(EXPORT_SRC) $(TEST_LDFLAGS) -o $@

# Regenerates the workbook. tests / benchmark.json / trace.csv are used as they are on disk
# (run `make test`, `ThermalSense --benchmark` / `--trace` to refresh them).
docs: $(EXPORTER)
	./$(EXPORTER) build/scene_export.json
	@if [ -x $(TESTS) ]; then ./$(TESTS) build/test_results.json || true; fi
	uv run --with openpyxl --with pillow --with pypdf python tools/build_workbook.py

run: $(APP)
	./$(APP)

clean:
	rm -rf build/obj build/obj-headless $(APP) $(TESTS)

-include $(APP_OBJ:.o=.d) $(TEST_OBJ:.o=.d)
.PHONY: all test docs run clean
