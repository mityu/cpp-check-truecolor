EXE := check-truecolor
CXXFLAGS := -std=c++20 -O2

ifneq ($(shell uname),Darwin)
CXXFLAGS += -static
endif

$(EXE): main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

.PHONY: run
run: $(EXE)
	./$(EXE)

.PHONY: clean
clean:
	$(RM) $(EXE)

.PHONY: test
test: $(EXE)
	deno check --no-lock test/
	deno lint test/
	deno test -A --parallel --shuffle test/*.ts
