all: test
.PHONY : all

# prints a help page
define help_page
"\
The following are the valid targets for this Makefile:               \n \
... all       : runs "test" (the default if no target is provided)   \n \
... test      : runs all unit tests                                  \n \
... clean     : removes build, doc, and gcov files                   \n \
... test_gcov : runs test with coverage reports                      \n \
... msys32    : runs the tests with 32bit mingw if installed         \n \
... clang     : runs the tests with clang if installed               \n"
endef
help:
	@echo $(help_page)
.PHONY : help

CPP_STANDARD = -std=c++17
CXX ?= g++

LOTS_OF_WARNINGS = \
	-Werror -Wsuggest-attribute=pure -Wsuggest-attribute=const -Wsuggest-attribute=noreturn \
	-Wsuggest-attribute=format -Wmissing-format-attribute -Wall -Wextra -pedantic -Wcast-align -Wcast-qual \
	-Wctor-dtor-privacy -Wdisabled-optimization -Winit-self -Wlogical-op -Wmissing-declarations \
	-Wmissing-include-dirs -Wnoexcept -Wold-style-cast -Woverloaded-virtual -Wredundant-decls -Wshadow -Wsign-promo \
	-Wstrict-null-sentinel -Wswitch-default -Wundef -Wno-unused -Wdouble-promotion -Wstrict-overflow=5 \
	-Wstrict-aliasing=2 -Wformat=2 -Weffc++
ifeq ($(MAKECMDGOALS),clang)
$(info configuring with clang++)
CXX := clang++
LOTS_OF_WARNINGS := \
	-Werror -Wmissing-format-attribute -Wall -Wextra -pedantic -Wcast-align -Wcast-qual \
	-Wctor-dtor-privacy -Wdisabled-optimization -Winit-self -Wmissing-declarations \
	-Wmissing-include-dirs -Wold-style-cast -Woverloaded-virtual -Wredundant-decls -Wshadow -Wsign-promo \
	-Wswitch-default -Wundef -Wno-unused -Wdouble-promotion -Wstrict-overflow=5 \
	-Wstrict-aliasing=2 -Wformat=2 -Weffc++
endif
src = test_using_udp.cpp

ifeq ($(OS),Windows_NT)
prog_name = $(basename $(src)).exe
else
prog_name = $(basename $(src)).elf
endif

# runs all unit tests
test:
	@echo "compiling ..." && \
	$(CXX) $(src) $(CPP_STANDARD) -O3 $(LOTS_OF_WARNINGS) -o $(prog_name) && \
	echo "running ..." && \
	./$(prog_name) || exit 1 && \
	rm -f $(prog_name)
.PHONY : test

# runs all unit tests using /c/msys64/mingw32/bin/g++.exe 32bit compiler 
msys32: test
.PHONY: msys32
ifeq ($(MAKECMDGOALS),msys32)
$(info configuring with /c/msys64/mingw32/bin/g++)
export PATH := /c/msys64/mingw32/bin/:$(PATH)
endif

# runs all unit tests using clang compiler 
clang: test
.PHONY: clang

# runs all unit tests and generates gcov coverage reports
test_gcov: clean
	@echo "compiling ..." && \
	$(CXX) $(src) $(CPP_STANDARD) -O3 --coverage -fprofile-arcs -ftest-coverage $(LOTS_OF_WARNINGS) -o $(prog_name) && \
	echo "running ..." && \
	./$(prog_name) && \
	echo "" && \
	find . -name '*.gcno' | xargs gcov -rm && \
	rm -f $(prog_name) *.gcda *.gcno
.PHONY : test_gcov

# removes all build, and gcov files
.PHONY : clean
clean:
	rm -f $(prog_name) *.gcda *.gcno *.gcov
