all:
	$(CC) -g scanner.c -o scanner -pthread
	$(CXX) -g win.cpp -o win -pthread -lusb-1.0
