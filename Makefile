.PHONY: test clean

test:
	sudo apt-get update
	sudo apt-get install -y gcc-multilib
	sudo apt-get install -y g++-multilib
	g++ -m32 tehssl.cpp -DTEHSSL_DEBUG -DTEHSSL_TEST -o tehssl -Wall -Wextra -Wpedantic 2> test_reports/gpp_warnings.txt
	g++ -m32 -g tehssl.cpp -DTEHSSL_DEBUG -DTEHSSL_TEST -o tehssl -Wall -Wextra -Wpedantic -O55 2> /dev/null
	valgrind --leak-check=full --track-origins=yes --log-file=test_reports/valgrind.txt ./tehssl > test_reports/output.txt
	$(MAKE) clean
clean:
	rm ./tehssl