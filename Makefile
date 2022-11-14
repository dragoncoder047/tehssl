.PHONY: test
test:
	g++ tehssl.cpp -DTEHSSL_DEBUG -DTEHSSL_TEST -o tehssl -Wall -Wpedantic
	./tehssl
	rm tehssl