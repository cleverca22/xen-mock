xen-mock: main.cpp
	g++ -O0 -g main.cpp -o xen-mock -ldl -lelf -lpthread -Wall -Wl,-Ttext-segment=0x1000000
install: xen-mock
	mkdir ${out}/bin -p
	cp xen-mock ${out}/bin/
	objdump -S ${out}/bin/xen-mock | grep test_findme -A10
disassemble:
	objdump -D -b binary hypercall_page.bin -mi386:x86-64 | head -n20
