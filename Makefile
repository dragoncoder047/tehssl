.PHONY: test clean

test:
	g++ tehssl.cpp -DTEHSSL_DEBUG -DTEHSSL_TEST -o tehssl -Wall -Wextra -Wpedantic -O55
	./tehssl
	$(MAKE) clean
clean:
	rm ./tehssl