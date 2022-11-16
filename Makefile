.PHONY: test clean

test:
	g++ tehssl.cpp -DTEHSSL_DEBUG -DTEHSSL_TEST -o tehssl -Wall -Wextra -Wpedantic -O55
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=valgrind_log.txt ./tehssl
	$(MAKE) clean
clean:
	rm ./tehssl