import random
import string
import os

def generate_random_string(length):
    return ''.join(random.choices(string.ascii_letters + string.digits, k=length))

def fuzz_cmdline():
    # Generate complex command lines
    keys = []
    cmd_parts = []
    
    for _ in range(20):
        key = generate_random_string(random.randint(1, 10))
        val = generate_random_string(random.randint(1, 20))
        sep = random.choice([' ', '  ', '\t'])
        
        if random.random() > 0.5:
            cmd_parts.append(f"{key}={val}")
            keys.append((key, val))
        else:
            cmd_parts.append(key)
            keys.append((key, None)) # Boolean
            
        cmd_parts.append(sep)
        
    cmdline = "".join(cmd_parts)
    print(f"Fuzzing with: {cmdline}")
    
    # Ideally we would pipe this to a C binary, but here we generate logic to verify via C
    # Generating a C test case for this specific string
    
    c_code = f"""
#include <stdio.h>
#include <string.h>
#include <assert.h>

void kprint(const char *s) {{}}
#include "../kern/cmdline.c"

int main() {{
    const char *input = "{cmdline}";
    cmdline_init(input);
    char buf[128];
    
"""
    for k, v in keys:
        if v:
            c_code += f"""
    if (cmdline_get("{k}", buf, 128) == 0) {{
        if (strcmp(buf, "{v}") != 0) {{
            printf("Mismatch for {k}: expected {v}, got %s\\n", buf);
            return 1;
        }}
    }} else {{
        printf("Failed to find {k}\\n");
        return 1;
    }}
"""
        else:
            c_code += f"""
    if (!cmdline_has("{k}")) {{
        printf("Failed to find flag {k}\\n");
        return 1;
    }}
"""

    c_code += "\n    return 0;\n}\n"
    
    with open("sys/tests/fuzz_generated.c", "w") as f:
        f.write(c_code)
        
    # Compile and run
    # Assuming gcc is available on host
    os.system("gcc -m32 -I sys/kern -o sys/tests/fuzz_gen sys/tests/fuzz_generated.c")
    ret = os.system("./sys/tests/fuzz_gen")
    
    if ret != 0:
        print("Fuzz Test FAILED")
        exit(1)
    else:
        print("Fuzz Test PASSED")

if __name__ == "__main__":
    fuzz_cmdline()
