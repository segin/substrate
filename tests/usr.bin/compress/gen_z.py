import sys

def main():
    if len(sys.argv) < 3:
        print("Usage: gen_z.py <input> <output.Z>")
        sys.exit(1)

    in_file = sys.argv[1]
    out_file = sys.argv[2]

    with open(in_file, "rb") as f:
        data = f.read()

    out = bytearray()
    out.append(0x1f)
    out.append(0x9d)
    out.append(0x80 | 16) # Block mode, 16 bits

    dictionary = {bytes([i]): i for i in range(256)}
    next_code = 257 # 256 is CLEAR
    current_bits = 9
    max_code = (1 << current_bits) - 1

    bit_buffer = 0
    bits_in_buffer = 0

    def output_code(code, bits):
        nonlocal bit_buffer, bits_in_buffer
        bit_buffer |= (code << bits_in_buffer)
        bits_in_buffer += bits
        while bits_in_buffer >= 8:
            out.append(bit_buffer & 0xff)
            bit_buffer >>= 8
            bits_in_buffer -= 8

    # Initial CLEAR code
    output_code(256, current_bits)

    w = b""
    for byte in data:
        c = bytes([byte])
        wc = w + c
        if wc in dictionary:
            w = wc
        else:
            output_code(dictionary[w], current_bits)

            dictionary[wc] = next_code
            next_code += 1

            if next_code > max_code and current_bits < 16:
                 current_bits += 1
                 max_code = (1 << current_bits) - 1

            if next_code >= 65536:
                 output_code(256, current_bits)
                 dictionary = {bytes([i]): i for i in range(256)}
                 next_code = 257
                 current_bits = 9
                 max_code = (1 << current_bits) - 1

            w = c

    if w:
        output_code(dictionary[w], current_bits)

    if bits_in_buffer > 0:
        out.append(bit_buffer & 0xff)

    with open(out_file, "wb") as f:
        f.write(out)

if __name__ == "__main__":
    main()
