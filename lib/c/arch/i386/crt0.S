.global _start
.extern main
.extern exit
.extern environ

_start:
    # Setup stack frame?
    # Stack contains: [argc, argv0, argv1..., 0, envp...]
    # For now, we assume standard System V ABI entry state
    
    # Push argv (ecx) and argc (eax) if they are in registers?
    # Linux puts them on stack: ESP -> argc, ESP+4 -> argv
    
    mov (%esp), %eax   # argc
    lea 4(%esp), %ebx  # argv

    # Calculate envp = argv + (argc + 1) * 4
    mov %eax, %ecx
    inc %ecx
    shl $2, %ecx
    lea (%ebx, %ecx), %edx # edx = envp

    # Store envp in environ
    mov %edx, environ
    
    push %edx # envp
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
