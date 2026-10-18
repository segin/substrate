/* a decimal floating operand does not meet a binary one, _Float128 among them (C23 6.3.1.8) */
_Float128 f(_Decimal64 d, _Float128 q) { return d + q; }
