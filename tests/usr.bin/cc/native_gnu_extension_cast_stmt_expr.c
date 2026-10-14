typedef unsigned short fpucw_t;

int main(void) {
	fpucw_t oldcw;

	(void)(oldcw = __extension__ ({ fpucw_t _cw; _cw = 7; _cw; }),
	       __extension__ (void)({ fpucw_t _ncw = oldcw; (void)_ncw; }));

	return oldcw != 7;
}
