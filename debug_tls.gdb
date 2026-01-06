target remote :1234
set pagination off
# Break at kernel transition to user
break jump_to_userspace
# Break at user entry
break *0x08048ae4
# Break at TLS init start
break *0x08065c80
continue
