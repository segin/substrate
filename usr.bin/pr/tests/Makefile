PR ?= ../pr

.PHONY: test clean

test:
	chmod +x test_pr.sh
	./test_pr.sh "$(PR)"

clean:
	rm -f *.tmp *.out
