.global _start
.extern main
.extern exit

_start:
    # Setup stack frame?
    # Stack contains: [argc, argv0, argv1..., 0, envp...]
    # For now, we assume standard System V ABI entry state
    
    # Push argv (ecx) and argc (eax) if they are in registers?
    # Linux puts them on stack: ESP -> argc, ESP+4 -> argv
    
    mov (%esp), %eax   # argc
    lea 4(%esp), %ebx  # argv
    
    push %ebx # argv
    push %eax # argc
    
    # Align stack?
    
    call __stdio_init
    call main
    
    # Pass return value to exit
    push %eax
    call exit
    
    # Should not reach here
    hlt
