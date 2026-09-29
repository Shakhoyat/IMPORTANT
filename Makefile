# ThermalSense build (MinGW-w64 g++). Run from Git Bash / MSYS:  mingw32-make -j8
CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Isrc -Iinclude -MMD -MP
LDFLAGS  = -Llib -lglfw3 -lopengl32 -lgdi32 -static -static-libgcc -static-libstdc++

APP_SRC  = $(wildcard src/*.cpp src/*/*.cpp)
CORE_SRC = $(filter-out src/main.cpp src/app/% src/rendering/%,$(APP_SRC))
APP_OBJ  = $(patsubst src/%.cpp,build/obj/%.o,$(APP_SRC)) build/obj/glad.o
TEST_OBJ = $(patsubst src/%.cpp,build/obj/%.o,$(CORE_SRC)) build/obj/tests/test_main.o

all: ThermalSense.exe build/tests.exe

ThermalSense.exe: $(APP_OBJ)
	$(CXX) $^ $(LDFLAGS) -o $@

build/tests.exe: $(TEST_OBJ)
	$(CXX) $^ -static -static-libgcc -static-libstdc++ -o $@

build/obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/obj/tests/%.o: tests/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/obj/glad.o: src/glad.c
	@mkdir -p build/obj
	gcc -O2 -Iinclude -c $< -o $@

test: build/tests.exe
	./build/tests.exe build/test_results.json

# Headless (GPU-free) exporter, built in one g++ call straight from the sources so it never touches build/obj.
EXPORT_SRC = $(CORE_SRC) src/app/DocExport.cpp tools/export_docs_main.cpp

build/export_docs.exe: $(EXPORT_SRC) $(wildcard src/*/*.h)
	@mkdir -p build
	$(CXX) -std=c++17 -O1 -Isrc -Iinclude $(EXPORT_SRC) -static -static-libgcc -static-libstdc++ -o $@

# Regenerates the workbook. tests.exe / benchmark.json / trace.csv are used as they are on disk
# (run `mingw32-make test`, `ThermalSense.exe --benchmark` / `--trace` to refresh them).
docs: build/export_docs.exe
	./build/export_docs.exe build/scene_export.json
	@if [ -x build/tests.exe ]; then ./build/tests.exe build/test_results.json || true; fi
	python tools/build_workbook.py

run: ThermalSense.exe
	./ThermalSense.exe

clean:
	rm -rf build/obj ThermalSense.exe build/tests.exe

-include $(APP_OBJ:.o=.d) $(TEST_OBJ:.o=.d)
.PHONY: all test docs run clean
