.PHONY: test clean

test:
	g++ -g tehssl.cpp -DTEHSSL_DEBUG -DTEHSSL_TEST -o tehssl -Wall -Wextra -Wpedantic -O55 2> test_reports/warnings.txt
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --log-file=test_reports/valgrind.txt ./tehssl > test_reports/output.txt
	$(MAKE) clean
clean:
	rm ./tehssl