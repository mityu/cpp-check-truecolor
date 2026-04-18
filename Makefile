EXE := check-truecolor
CXXFLAGS := -std=c++20 -O2

ifneq ($(shell uname),Darwin)
CXXFLAGS += -static
endif

$(EXE): main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

run: $(EXE)
	./$(EXE)

clean:
	$(RM) $(EXE)
