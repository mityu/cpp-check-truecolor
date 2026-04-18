check-truecolor: main.cpp
	$(CXX) -std=c++20 -O2 -o $@ $^

run: check-truecolor
	./check-truecolor
