import os

def update_man_page(filepath, section):
    with open(filepath, 'r') as f:
        lines = f.readlines()

    content = "".join(lines)
    
    # 1. Add LIBRARY section if missing
    if ".SH LIBRARY" not in content:
        # Find placement: after NAME section (NAME + 1 line + description?)
        # Standard: NAME, then SYNOPSIS. Insert LIBRARY before SYNOPSIS if possible.
        try:
            syn_idx = -1
            for i, line in enumerate(lines):
                if ".SH SYNOPSIS" in line:
                    syn_idx = i
                    break
            
            if syn_idx != -1:
                # Insert before SYNOPSIS
                lib_lines = [".SH LIBRARY\n", "Standard C library (libc, -lc)\n"]
                lines = lines[:syn_idx] + lib_lines + lines[syn_idx:]
            else:
                 print(f"Skipping LIBRARY for {filepath}: No SYNOPSIS found")
        except ValueError:
            pass

    # 2. Add SEE ALSO section if missing
    content = "".join(lines) # re-join
    if ".SH SEE ALSO" not in content:
        # Append to end
        see_also = [".SH SEE ALSO\n", ".BR intro(2)\n"] 
        if section == "3":
             see_also = [".SH SEE ALSO\n", ".BR intro(2),\n", ".BR errno(3)\n"]
        lines.append("\n" + "".join(see_also))

    with open(filepath, 'w') as f:
        f.writelines(lines)

def process_dir(directory, section):
    for filename in os.listdir(directory):
        if filename.endswith("." + section):
             update_man_page(os.path.join(directory, filename), section)

if __name__ == "__main__":
    process_dir("man/man2", "2")
    process_dir("man/man3", "3")
