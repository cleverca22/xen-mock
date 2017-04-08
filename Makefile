xen-mock: main.cpp
	g++ -g main.cpp -o xen-mock -ldl -lelf -Wall -Wl,-Ttext-segment=0x1000000
install: xen-mock
	mkdir ${out}/bin -p
	cp xen-mock ${out}/bin/
disassemble:
	objdump -D -b binary hypercall_page.bin -mi386:x86-64 | head -n20
