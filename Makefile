xen-mock: main.cpp
	g++ -g main.cpp -o xen-mock -ldl -lelf -Wall -Wl,-Ttext-segment=0x1000000
