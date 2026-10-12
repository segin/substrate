# What GNU as makes of each line of x86_64_lines.txt: its result, a tab, the line.
# Written by gnu-diff.py --write-gnu-results with GNU assembler (GNU Binutils) 2.47
8900	movl %eax,(%rax)
8b08	movl (%rax),%ecx
8d10	leal (%rax),%edx
894008	movl %eax,8(%rax)
8b4808	movl 8(%rax),%ecx
8d5008	leal 8(%rax),%edx
8940f8	movl %eax,-8(%rax)
8b48f8	movl -8(%rax),%ecx
8d50f8	leal -8(%rax),%edx
89407f	movl %eax,127(%rax)
8b487f	movl 127(%rax),%ecx
8d507f	leal 127(%rax),%edx
898080000000	movl %eax,128(%rax)
8b8880000000	movl 128(%rax),%ecx
8d9080000000	leal 128(%rax),%edx
894080	movl %eax,-128(%rax)
8b4880	movl -128(%rax),%ecx
8d5080	leal -128(%rax),%edx
89807fffffff	movl %eax,-129(%rax)
8b887fffffff	movl -129(%rax),%ecx
8d907fffffff	leal -129(%rax),%edx
898045230100	movl %eax,0x12345(%rax)
8b8845230100	movl 0x12345(%rax),%ecx
8d9045230100	leal 0x12345(%rax),%edx
8900	movl %eax,0(%rax)
8b08	movl 0(%rax),%ecx
8d10	leal 0(%rax),%edx
898000000000 2:11:sym+0	movl %eax,sym(%rax)
8b8800000000 2:11:sym+0	movl sym(%rax),%ecx
8d9000000000 2:10:sym+0	leal sym(%rax),%edx
898000000000 2:11:sym+4	movl %eax,sym+4(%rax)
8b8800000000 2:11:sym+4	movl sym+4(%rax),%ecx
8d9000000000 2:10:sym+4	leal sym+4(%rax),%edx
8903	movl %eax,(%rbx)
8b0b	movl (%rbx),%ecx
8d13	leal (%rbx),%edx
894308	movl %eax,8(%rbx)
8b4b08	movl 8(%rbx),%ecx
8d5308	leal 8(%rbx),%edx
8943f8	movl %eax,-8(%rbx)
8b4bf8	movl -8(%rbx),%ecx
8d53f8	leal -8(%rbx),%edx
89437f	movl %eax,127(%rbx)
8b4b7f	movl 127(%rbx),%ecx
8d537f	leal 127(%rbx),%edx
898380000000	movl %eax,128(%rbx)
8b8b80000000	movl 128(%rbx),%ecx
8d9380000000	leal 128(%rbx),%edx
894380	movl %eax,-128(%rbx)
8b4b80	movl -128(%rbx),%ecx
8d5380	leal -128(%rbx),%edx
89837fffffff	movl %eax,-129(%rbx)
8b8b7fffffff	movl -129(%rbx),%ecx
8d937fffffff	leal -129(%rbx),%edx
898345230100	movl %eax,0x12345(%rbx)
8b8b45230100	movl 0x12345(%rbx),%ecx
8d9345230100	leal 0x12345(%rbx),%edx
8903	movl %eax,0(%rbx)
8b0b	movl 0(%rbx),%ecx
8d13	leal 0(%rbx),%edx
898300000000 2:11:sym+0	movl %eax,sym(%rbx)
8b8b00000000 2:11:sym+0	movl sym(%rbx),%ecx
8d9300000000 2:10:sym+0	leal sym(%rbx),%edx
898300000000 2:11:sym+4	movl %eax,sym+4(%rbx)
8b8b00000000 2:11:sym+4	movl sym+4(%rbx),%ecx
8d9300000000 2:10:sym+4	leal sym+4(%rbx),%edx
890424	movl %eax,(%rsp)
8b0c24	movl (%rsp),%ecx
8d1424	leal (%rsp),%edx
89442408	movl %eax,8(%rsp)
8b4c2408	movl 8(%rsp),%ecx
8d542408	leal 8(%rsp),%edx
894424f8	movl %eax,-8(%rsp)
8b4c24f8	movl -8(%rsp),%ecx
8d5424f8	leal -8(%rsp),%edx
8944247f	movl %eax,127(%rsp)
8b4c247f	movl 127(%rsp),%ecx
8d54247f	leal 127(%rsp),%edx
89842480000000	movl %eax,128(%rsp)
8b8c2480000000	movl 128(%rsp),%ecx
8d942480000000	leal 128(%rsp),%edx
89442480	movl %eax,-128(%rsp)
8b4c2480	movl -128(%rsp),%ecx
8d542480	leal -128(%rsp),%edx
8984247fffffff	movl %eax,-129(%rsp)
8b8c247fffffff	movl -129(%rsp),%ecx
8d94247fffffff	leal -129(%rsp),%edx
89842445230100	movl %eax,0x12345(%rsp)
8b8c2445230100	movl 0x12345(%rsp),%ecx
8d942445230100	leal 0x12345(%rsp),%edx
890424	movl %eax,0(%rsp)
8b0c24	movl 0(%rsp),%ecx
8d1424	leal 0(%rsp),%edx
89842400000000 3:11:sym+0	movl %eax,sym(%rsp)
8b8c2400000000 3:11:sym+0	movl sym(%rsp),%ecx
8d942400000000 3:10:sym+0	leal sym(%rsp),%edx
89842400000000 3:11:sym+4	movl %eax,sym+4(%rsp)
8b8c2400000000 3:11:sym+4	movl sym+4(%rsp),%ecx
8d942400000000 3:10:sym+4	leal sym+4(%rsp),%edx
894500	movl %eax,(%rbp)
8b4d00	movl (%rbp),%ecx
8d5500	leal (%rbp),%edx
894508	movl %eax,8(%rbp)
8b4d08	movl 8(%rbp),%ecx
8d5508	leal 8(%rbp),%edx
8945f8	movl %eax,-8(%rbp)
8b4df8	movl -8(%rbp),%ecx
8d55f8	leal -8(%rbp),%edx
89457f	movl %eax,127(%rbp)
8b4d7f	movl 127(%rbp),%ecx
8d557f	leal 127(%rbp),%edx
898580000000	movl %eax,128(%rbp)
8b8d80000000	movl 128(%rbp),%ecx
8d9580000000	leal 128(%rbp),%edx
894580	movl %eax,-128(%rbp)
8b4d80	movl -128(%rbp),%ecx
8d5580	leal -128(%rbp),%edx
89857fffffff	movl %eax,-129(%rbp)
8b8d7fffffff	movl -129(%rbp),%ecx
8d957fffffff	leal -129(%rbp),%edx
898545230100	movl %eax,0x12345(%rbp)
8b8d45230100	movl 0x12345(%rbp),%ecx
8d9545230100	leal 0x12345(%rbp),%edx
894500	movl %eax,0(%rbp)
8b4d00	movl 0(%rbp),%ecx
8d5500	leal 0(%rbp),%edx
898500000000 2:11:sym+0	movl %eax,sym(%rbp)
8b8d00000000 2:11:sym+0	movl sym(%rbp),%ecx
8d9500000000 2:10:sym+0	leal sym(%rbp),%edx
898500000000 2:11:sym+4	movl %eax,sym+4(%rbp)
8b8d00000000 2:11:sym+4	movl sym+4(%rbp),%ecx
8d9500000000 2:10:sym+4	leal sym+4(%rbp),%edx
8906	movl %eax,(%rsi)
8b0e	movl (%rsi),%ecx
8d16	leal (%rsi),%edx
894608	movl %eax,8(%rsi)
8b4e08	movl 8(%rsi),%ecx
8d5608	leal 8(%rsi),%edx
8946f8	movl %eax,-8(%rsi)
8b4ef8	movl -8(%rsi),%ecx
8d56f8	leal -8(%rsi),%edx
89467f	movl %eax,127(%rsi)
8b4e7f	movl 127(%rsi),%ecx
8d567f	leal 127(%rsi),%edx
898680000000	movl %eax,128(%rsi)
8b8e80000000	movl 128(%rsi),%ecx
8d9680000000	leal 128(%rsi),%edx
894680	movl %eax,-128(%rsi)
8b4e80	movl -128(%rsi),%ecx
8d5680	leal -128(%rsi),%edx
89867fffffff	movl %eax,-129(%rsi)
8b8e7fffffff	movl -129(%rsi),%ecx
8d967fffffff	leal -129(%rsi),%edx
898645230100	movl %eax,0x12345(%rsi)
8b8e45230100	movl 0x12345(%rsi),%ecx
8d9645230100	leal 0x12345(%rsi),%edx
8906	movl %eax,0(%rsi)
8b0e	movl 0(%rsi),%ecx
8d16	leal 0(%rsi),%edx
898600000000 2:11:sym+0	movl %eax,sym(%rsi)
8b8e00000000 2:11:sym+0	movl sym(%rsi),%ecx
8d9600000000 2:10:sym+0	leal sym(%rsi),%edx
898600000000 2:11:sym+4	movl %eax,sym+4(%rsi)
8b8e00000000 2:11:sym+4	movl sym+4(%rsi),%ecx
8d9600000000 2:10:sym+4	leal sym+4(%rsi),%edx
418900	movl %eax,(%r8)
418b08	movl (%r8),%ecx
418d10	leal (%r8),%edx
41894008	movl %eax,8(%r8)
418b4808	movl 8(%r8),%ecx
418d5008	leal 8(%r8),%edx
418940f8	movl %eax,-8(%r8)
418b48f8	movl -8(%r8),%ecx
418d50f8	leal -8(%r8),%edx
4189407f	movl %eax,127(%r8)
418b487f	movl 127(%r8),%ecx
418d507f	leal 127(%r8),%edx
41898080000000	movl %eax,128(%r8)
418b8880000000	movl 128(%r8),%ecx
418d9080000000	leal 128(%r8),%edx
41894080	movl %eax,-128(%r8)
418b4880	movl -128(%r8),%ecx
418d5080	leal -128(%r8),%edx
4189807fffffff	movl %eax,-129(%r8)
418b887fffffff	movl -129(%r8),%ecx
418d907fffffff	leal -129(%r8),%edx
41898045230100	movl %eax,0x12345(%r8)
418b8845230100	movl 0x12345(%r8),%ecx
418d9045230100	leal 0x12345(%r8),%edx
418900	movl %eax,0(%r8)
418b08	movl 0(%r8),%ecx
418d10	leal 0(%r8),%edx
41898000000000 3:11:sym+0	movl %eax,sym(%r8)
418b8800000000 3:11:sym+0	movl sym(%r8),%ecx
418d9000000000 3:10:sym+0	leal sym(%r8),%edx
41898000000000 3:11:sym+4	movl %eax,sym+4(%r8)
418b8800000000 3:11:sym+4	movl sym+4(%r8),%ecx
418d9000000000 3:10:sym+4	leal sym+4(%r8),%edx
41890424	movl %eax,(%r12)
418b0c24	movl (%r12),%ecx
418d1424	leal (%r12),%edx
4189442408	movl %eax,8(%r12)
418b4c2408	movl 8(%r12),%ecx
418d542408	leal 8(%r12),%edx
41894424f8	movl %eax,-8(%r12)
418b4c24f8	movl -8(%r12),%ecx
418d5424f8	leal -8(%r12),%edx
418944247f	movl %eax,127(%r12)
418b4c247f	movl 127(%r12),%ecx
418d54247f	leal 127(%r12),%edx
4189842480000000	movl %eax,128(%r12)
418b8c2480000000	movl 128(%r12),%ecx
418d942480000000	leal 128(%r12),%edx
4189442480	movl %eax,-128(%r12)
418b4c2480	movl -128(%r12),%ecx
418d542480	leal -128(%r12),%edx
418984247fffffff	movl %eax,-129(%r12)
418b8c247fffffff	movl -129(%r12),%ecx
418d94247fffffff	leal -129(%r12),%edx
4189842445230100	movl %eax,0x12345(%r12)
418b8c2445230100	movl 0x12345(%r12),%ecx
418d942445230100	leal 0x12345(%r12),%edx
41890424	movl %eax,0(%r12)
418b0c24	movl 0(%r12),%ecx
418d1424	leal 0(%r12),%edx
4189842400000000 4:11:sym+0	movl %eax,sym(%r12)
418b8c2400000000 4:11:sym+0	movl sym(%r12),%ecx
418d942400000000 4:10:sym+0	leal sym(%r12),%edx
4189842400000000 4:11:sym+4	movl %eax,sym+4(%r12)
418b8c2400000000 4:11:sym+4	movl sym+4(%r12),%ecx
418d942400000000 4:10:sym+4	leal sym+4(%r12),%edx
41894500	movl %eax,(%r13)
418b4d00	movl (%r13),%ecx
418d5500	leal (%r13),%edx
41894508	movl %eax,8(%r13)
418b4d08	movl 8(%r13),%ecx
418d5508	leal 8(%r13),%edx
418945f8	movl %eax,-8(%r13)
418b4df8	movl -8(%r13),%ecx
418d55f8	leal -8(%r13),%edx
4189457f	movl %eax,127(%r13)
418b4d7f	movl 127(%r13),%ecx
418d557f	leal 127(%r13),%edx
41898580000000	movl %eax,128(%r13)
418b8d80000000	movl 128(%r13),%ecx
418d9580000000	leal 128(%r13),%edx
41894580	movl %eax,-128(%r13)
418b4d80	movl -128(%r13),%ecx
418d5580	leal -128(%r13),%edx
4189857fffffff	movl %eax,-129(%r13)
418b8d7fffffff	movl -129(%r13),%ecx
418d957fffffff	leal -129(%r13),%edx
41898545230100	movl %eax,0x12345(%r13)
418b8d45230100	movl 0x12345(%r13),%ecx
418d9545230100	leal 0x12345(%r13),%edx
41894500	movl %eax,0(%r13)
418b4d00	movl 0(%r13),%ecx
418d5500	leal 0(%r13),%edx
41898500000000 3:11:sym+0	movl %eax,sym(%r13)
418b8d00000000 3:11:sym+0	movl sym(%r13),%ecx
418d9500000000 3:10:sym+0	leal sym(%r13),%edx
41898500000000 3:11:sym+4	movl %eax,sym+4(%r13)
418b8d00000000 3:11:sym+4	movl sym+4(%r13),%ecx
418d9500000000 3:10:sym+4	leal sym+4(%r13),%edx
418907	movl %eax,(%r15)
418b0f	movl (%r15),%ecx
418d17	leal (%r15),%edx
41894708	movl %eax,8(%r15)
418b4f08	movl 8(%r15),%ecx
418d5708	leal 8(%r15),%edx
418947f8	movl %eax,-8(%r15)
418b4ff8	movl -8(%r15),%ecx
418d57f8	leal -8(%r15),%edx
4189477f	movl %eax,127(%r15)
418b4f7f	movl 127(%r15),%ecx
418d577f	leal 127(%r15),%edx
41898780000000	movl %eax,128(%r15)
418b8f80000000	movl 128(%r15),%ecx
418d9780000000	leal 128(%r15),%edx
41894780	movl %eax,-128(%r15)
418b4f80	movl -128(%r15),%ecx
418d5780	leal -128(%r15),%edx
4189877fffffff	movl %eax,-129(%r15)
418b8f7fffffff	movl -129(%r15),%ecx
418d977fffffff	leal -129(%r15),%edx
41898745230100	movl %eax,0x12345(%r15)
418b8f45230100	movl 0x12345(%r15),%ecx
418d9745230100	leal 0x12345(%r15),%edx
418907	movl %eax,0(%r15)
418b0f	movl 0(%r15),%ecx
418d17	leal 0(%r15),%edx
41898700000000 3:11:sym+0	movl %eax,sym(%r15)
418b8f00000000 3:11:sym+0	movl sym(%r15),%ecx
418d9700000000 3:10:sym+0	leal sym(%r15),%edx
41898700000000 3:11:sym+4	movl %eax,sym+4(%r15)
418b8f00000000 3:11:sym+4	movl sym+4(%r15),%ecx
418d9700000000 3:10:sym+4	leal sym+4(%r15),%edx
890400	movl %eax,(%rax,%rax)
8b0c00	movl (%rax,%rax),%ecx
8d1400	leal (%rax,%rax),%edx
890400	movl %eax,(%rax,%rax,1)
8b0c00	movl (%rax,%rax,1),%ecx
8d1400	leal (%rax,%rax,1),%edx
890440	movl %eax,(%rax,%rax,2)
8b0c40	movl (%rax,%rax,2),%ecx
8d1440	leal (%rax,%rax,2),%edx
89448004	movl %eax,4(%rax,%rax,4)
8b4c8004	movl 4(%rax,%rax,4),%ecx
8d548004	leal 4(%rax,%rax,4),%edx
8984c000100000	movl %eax,0x1000(%rax,%rax,8)
8b8cc000100000	movl 0x1000(%rax,%rax,8),%ecx
8d94c000100000	leal 0x1000(%rax,%rax,8),%edx
8984c000000000 3:11:sym+0	movl %eax,sym(%rax,%rax,8)
8b8cc000000000 3:11:sym+0	movl sym(%rax,%rax,8),%ecx
8d94c000000000 3:10:sym+0	leal sym(%rax,%rax,8),%edx
890418	movl %eax,(%rax,%rbx)
8b0c18	movl (%rax,%rbx),%ecx
8d1418	leal (%rax,%rbx),%edx
890418	movl %eax,(%rax,%rbx,1)
8b0c18	movl (%rax,%rbx,1),%ecx
8d1418	leal (%rax,%rbx,1),%edx
890458	movl %eax,(%rax,%rbx,2)
8b0c58	movl (%rax,%rbx,2),%ecx
8d1458	leal (%rax,%rbx,2),%edx
89449804	movl %eax,4(%rax,%rbx,4)
8b4c9804	movl 4(%rax,%rbx,4),%ecx
8d549804	leal 4(%rax,%rbx,4),%edx
8984d800100000	movl %eax,0x1000(%rax,%rbx,8)
8b8cd800100000	movl 0x1000(%rax,%rbx,8),%ecx
8d94d800100000	leal 0x1000(%rax,%rbx,8),%edx
8984d800000000 3:11:sym+0	movl %eax,sym(%rax,%rbx,8)
8b8cd800000000 3:11:sym+0	movl sym(%rax,%rbx,8),%ecx
8d94d800000000 3:10:sym+0	leal sym(%rax,%rbx,8),%edx
890428	movl %eax,(%rax,%rbp)
8b0c28	movl (%rax,%rbp),%ecx
8d1428	leal (%rax,%rbp),%edx
890428	movl %eax,(%rax,%rbp,1)
8b0c28	movl (%rax,%rbp,1),%ecx
8d1428	leal (%rax,%rbp,1),%edx
890468	movl %eax,(%rax,%rbp,2)
8b0c68	movl (%rax,%rbp,2),%ecx
8d1468	leal (%rax,%rbp,2),%edx
8944a804	movl %eax,4(%rax,%rbp,4)
8b4ca804	movl 4(%rax,%rbp,4),%ecx
8d54a804	leal 4(%rax,%rbp,4),%edx
8984e800100000	movl %eax,0x1000(%rax,%rbp,8)
8b8ce800100000	movl 0x1000(%rax,%rbp,8),%ecx
8d94e800100000	leal 0x1000(%rax,%rbp,8),%edx
8984e800000000 3:11:sym+0	movl %eax,sym(%rax,%rbp,8)
8b8ce800000000 3:11:sym+0	movl sym(%rax,%rbp,8),%ecx
8d94e800000000 3:10:sym+0	leal sym(%rax,%rbp,8),%edx
890430	movl %eax,(%rax,%rsi)
8b0c30	movl (%rax,%rsi),%ecx
8d1430	leal (%rax,%rsi),%edx
890430	movl %eax,(%rax,%rsi,1)
8b0c30	movl (%rax,%rsi,1),%ecx
8d1430	leal (%rax,%rsi,1),%edx
890470	movl %eax,(%rax,%rsi,2)
8b0c70	movl (%rax,%rsi,2),%ecx
8d1470	leal (%rax,%rsi,2),%edx
8944b004	movl %eax,4(%rax,%rsi,4)
8b4cb004	movl 4(%rax,%rsi,4),%ecx
8d54b004	leal 4(%rax,%rsi,4),%edx
8984f000100000	movl %eax,0x1000(%rax,%rsi,8)
8b8cf000100000	movl 0x1000(%rax,%rsi,8),%ecx
8d94f000100000	leal 0x1000(%rax,%rsi,8),%edx
8984f000000000 3:11:sym+0	movl %eax,sym(%rax,%rsi,8)
8b8cf000000000 3:11:sym+0	movl sym(%rax,%rsi,8),%ecx
8d94f000000000 3:10:sym+0	leal sym(%rax,%rsi,8),%edx
42890420	movl %eax,(%rax,%r12)
428b0c20	movl (%rax,%r12),%ecx
428d1420	leal (%rax,%r12),%edx
42890420	movl %eax,(%rax,%r12,1)
428b0c20	movl (%rax,%r12,1),%ecx
428d1420	leal (%rax,%r12,1),%edx
42890460	movl %eax,(%rax,%r12,2)
428b0c60	movl (%rax,%r12,2),%ecx
428d1460	leal (%rax,%r12,2),%edx
428944a004	movl %eax,4(%rax,%r12,4)
428b4ca004	movl 4(%rax,%r12,4),%ecx
428d54a004	leal 4(%rax,%r12,4),%edx
428984e000100000	movl %eax,0x1000(%rax,%r12,8)
428b8ce000100000	movl 0x1000(%rax,%r12,8),%ecx
428d94e000100000	leal 0x1000(%rax,%r12,8),%edx
428984e000000000 4:11:sym+0	movl %eax,sym(%rax,%r12,8)
428b8ce000000000 4:11:sym+0	movl sym(%rax,%r12,8),%ecx
428d94e000000000 4:10:sym+0	leal sym(%rax,%r12,8),%edx
42890428	movl %eax,(%rax,%r13)
428b0c28	movl (%rax,%r13),%ecx
428d1428	leal (%rax,%r13),%edx
42890428	movl %eax,(%rax,%r13,1)
428b0c28	movl (%rax,%r13,1),%ecx
428d1428	leal (%rax,%r13,1),%edx
42890468	movl %eax,(%rax,%r13,2)
428b0c68	movl (%rax,%r13,2),%ecx
428d1468	leal (%rax,%r13,2),%edx
428944a804	movl %eax,4(%rax,%r13,4)
428b4ca804	movl 4(%rax,%r13,4),%ecx
428d54a804	leal 4(%rax,%r13,4),%edx
428984e800100000	movl %eax,0x1000(%rax,%r13,8)
428b8ce800100000	movl 0x1000(%rax,%r13,8),%ecx
428d94e800100000	leal 0x1000(%rax,%r13,8),%edx
428984e800000000 4:11:sym+0	movl %eax,sym(%rax,%r13,8)
428b8ce800000000 4:11:sym+0	movl sym(%rax,%r13,8),%ecx
428d94e800000000 4:10:sym+0	leal sym(%rax,%r13,8),%edx
42890438	movl %eax,(%rax,%r15)
428b0c38	movl (%rax,%r15),%ecx
428d1438	leal (%rax,%r15),%edx
42890438	movl %eax,(%rax,%r15,1)
428b0c38	movl (%rax,%r15,1),%ecx
428d1438	leal (%rax,%r15,1),%edx
42890478	movl %eax,(%rax,%r15,2)
428b0c78	movl (%rax,%r15,2),%ecx
428d1478	leal (%rax,%r15,2),%edx
428944b804	movl %eax,4(%rax,%r15,4)
428b4cb804	movl 4(%rax,%r15,4),%ecx
428d54b804	leal 4(%rax,%r15,4),%edx
428984f800100000	movl %eax,0x1000(%rax,%r15,8)
428b8cf800100000	movl 0x1000(%rax,%r15,8),%ecx
428d94f800100000	leal 0x1000(%rax,%r15,8),%edx
428984f800000000 4:11:sym+0	movl %eax,sym(%rax,%r15,8)
428b8cf800000000 4:11:sym+0	movl sym(%rax,%r15,8),%ecx
428d94f800000000 4:10:sym+0	leal sym(%rax,%r15,8),%edx
890403	movl %eax,(%rbx,%rax)
8b0c03	movl (%rbx,%rax),%ecx
8d1403	leal (%rbx,%rax),%edx
890403	movl %eax,(%rbx,%rax,1)
8b0c03	movl (%rbx,%rax,1),%ecx
8d1403	leal (%rbx,%rax,1),%edx
890443	movl %eax,(%rbx,%rax,2)
8b0c43	movl (%rbx,%rax,2),%ecx
8d1443	leal (%rbx,%rax,2),%edx
89448304	movl %eax,4(%rbx,%rax,4)
8b4c8304	movl 4(%rbx,%rax,4),%ecx
8d548304	leal 4(%rbx,%rax,4),%edx
8984c300100000	movl %eax,0x1000(%rbx,%rax,8)
8b8cc300100000	movl 0x1000(%rbx,%rax,8),%ecx
8d94c300100000	leal 0x1000(%rbx,%rax,8),%edx
8984c300000000 3:11:sym+0	movl %eax,sym(%rbx,%rax,8)
8b8cc300000000 3:11:sym+0	movl sym(%rbx,%rax,8),%ecx
8d94c300000000 3:10:sym+0	leal sym(%rbx,%rax,8),%edx
89041b	movl %eax,(%rbx,%rbx)
8b0c1b	movl (%rbx,%rbx),%ecx
8d141b	leal (%rbx,%rbx),%edx
89041b	movl %eax,(%rbx,%rbx,1)
8b0c1b	movl (%rbx,%rbx,1),%ecx
8d141b	leal (%rbx,%rbx,1),%edx
89045b	movl %eax,(%rbx,%rbx,2)
8b0c5b	movl (%rbx,%rbx,2),%ecx
8d145b	leal (%rbx,%rbx,2),%edx
89449b04	movl %eax,4(%rbx,%rbx,4)
8b4c9b04	movl 4(%rbx,%rbx,4),%ecx
8d549b04	leal 4(%rbx,%rbx,4),%edx
8984db00100000	movl %eax,0x1000(%rbx,%rbx,8)
8b8cdb00100000	movl 0x1000(%rbx,%rbx,8),%ecx
8d94db00100000	leal 0x1000(%rbx,%rbx,8),%edx
8984db00000000 3:11:sym+0	movl %eax,sym(%rbx,%rbx,8)
8b8cdb00000000 3:11:sym+0	movl sym(%rbx,%rbx,8),%ecx
8d94db00000000 3:10:sym+0	leal sym(%rbx,%rbx,8),%edx
89042b	movl %eax,(%rbx,%rbp)
8b0c2b	movl (%rbx,%rbp),%ecx
8d142b	leal (%rbx,%rbp),%edx
89042b	movl %eax,(%rbx,%rbp,1)
8b0c2b	movl (%rbx,%rbp,1),%ecx
8d142b	leal (%rbx,%rbp,1),%edx
89046b	movl %eax,(%rbx,%rbp,2)
8b0c6b	movl (%rbx,%rbp,2),%ecx
8d146b	leal (%rbx,%rbp,2),%edx
8944ab04	movl %eax,4(%rbx,%rbp,4)
8b4cab04	movl 4(%rbx,%rbp,4),%ecx
8d54ab04	leal 4(%rbx,%rbp,4),%edx
8984eb00100000	movl %eax,0x1000(%rbx,%rbp,8)
8b8ceb00100000	movl 0x1000(%rbx,%rbp,8),%ecx
8d94eb00100000	leal 0x1000(%rbx,%rbp,8),%edx
8984eb00000000 3:11:sym+0	movl %eax,sym(%rbx,%rbp,8)
8b8ceb00000000 3:11:sym+0	movl sym(%rbx,%rbp,8),%ecx
8d94eb00000000 3:10:sym+0	leal sym(%rbx,%rbp,8),%edx
890433	movl %eax,(%rbx,%rsi)
8b0c33	movl (%rbx,%rsi),%ecx
8d1433	leal (%rbx,%rsi),%edx
890433	movl %eax,(%rbx,%rsi,1)
8b0c33	movl (%rbx,%rsi,1),%ecx
8d1433	leal (%rbx,%rsi,1),%edx
890473	movl %eax,(%rbx,%rsi,2)
8b0c73	movl (%rbx,%rsi,2),%ecx
8d1473	leal (%rbx,%rsi,2),%edx
8944b304	movl %eax,4(%rbx,%rsi,4)
8b4cb304	movl 4(%rbx,%rsi,4),%ecx
8d54b304	leal 4(%rbx,%rsi,4),%edx
8984f300100000	movl %eax,0x1000(%rbx,%rsi,8)
8b8cf300100000	movl 0x1000(%rbx,%rsi,8),%ecx
8d94f300100000	leal 0x1000(%rbx,%rsi,8),%edx
8984f300000000 3:11:sym+0	movl %eax,sym(%rbx,%rsi,8)
8b8cf300000000 3:11:sym+0	movl sym(%rbx,%rsi,8),%ecx
8d94f300000000 3:10:sym+0	leal sym(%rbx,%rsi,8),%edx
42890423	movl %eax,(%rbx,%r12)
428b0c23	movl (%rbx,%r12),%ecx
428d1423	leal (%rbx,%r12),%edx
42890423	movl %eax,(%rbx,%r12,1)
428b0c23	movl (%rbx,%r12,1),%ecx
428d1423	leal (%rbx,%r12,1),%edx
42890463	movl %eax,(%rbx,%r12,2)
428b0c63	movl (%rbx,%r12,2),%ecx
428d1463	leal (%rbx,%r12,2),%edx
428944a304	movl %eax,4(%rbx,%r12,4)
428b4ca304	movl 4(%rbx,%r12,4),%ecx
428d54a304	leal 4(%rbx,%r12,4),%edx
428984e300100000	movl %eax,0x1000(%rbx,%r12,8)
428b8ce300100000	movl 0x1000(%rbx,%r12,8),%ecx
428d94e300100000	leal 0x1000(%rbx,%r12,8),%edx
428984e300000000 4:11:sym+0	movl %eax,sym(%rbx,%r12,8)
428b8ce300000000 4:11:sym+0	movl sym(%rbx,%r12,8),%ecx
428d94e300000000 4:10:sym+0	leal sym(%rbx,%r12,8),%edx
4289042b	movl %eax,(%rbx,%r13)
428b0c2b	movl (%rbx,%r13),%ecx
428d142b	leal (%rbx,%r13),%edx
4289042b	movl %eax,(%rbx,%r13,1)
428b0c2b	movl (%rbx,%r13,1),%ecx
428d142b	leal (%rbx,%r13,1),%edx
4289046b	movl %eax,(%rbx,%r13,2)
428b0c6b	movl (%rbx,%r13,2),%ecx
428d146b	leal (%rbx,%r13,2),%edx
428944ab04	movl %eax,4(%rbx,%r13,4)
428b4cab04	movl 4(%rbx,%r13,4),%ecx
428d54ab04	leal 4(%rbx,%r13,4),%edx
428984eb00100000	movl %eax,0x1000(%rbx,%r13,8)
428b8ceb00100000	movl 0x1000(%rbx,%r13,8),%ecx
428d94eb00100000	leal 0x1000(%rbx,%r13,8),%edx
428984eb00000000 4:11:sym+0	movl %eax,sym(%rbx,%r13,8)
428b8ceb00000000 4:11:sym+0	movl sym(%rbx,%r13,8),%ecx
428d94eb00000000 4:10:sym+0	leal sym(%rbx,%r13,8),%edx
4289043b	movl %eax,(%rbx,%r15)
428b0c3b	movl (%rbx,%r15),%ecx
428d143b	leal (%rbx,%r15),%edx
4289043b	movl %eax,(%rbx,%r15,1)
428b0c3b	movl (%rbx,%r15,1),%ecx
428d143b	leal (%rbx,%r15,1),%edx
4289047b	movl %eax,(%rbx,%r15,2)
428b0c7b	movl (%rbx,%r15,2),%ecx
428d147b	leal (%rbx,%r15,2),%edx
428944bb04	movl %eax,4(%rbx,%r15,4)
428b4cbb04	movl 4(%rbx,%r15,4),%ecx
428d54bb04	leal 4(%rbx,%r15,4),%edx
428984fb00100000	movl %eax,0x1000(%rbx,%r15,8)
428b8cfb00100000	movl 0x1000(%rbx,%r15,8),%ecx
428d94fb00100000	leal 0x1000(%rbx,%r15,8),%edx
428984fb00000000 4:11:sym+0	movl %eax,sym(%rbx,%r15,8)
428b8cfb00000000 4:11:sym+0	movl sym(%rbx,%r15,8),%ecx
428d94fb00000000 4:10:sym+0	leal sym(%rbx,%r15,8),%edx
890404	movl %eax,(%rsp,%rax)
8b0c04	movl (%rsp,%rax),%ecx
8d1404	leal (%rsp,%rax),%edx
890404	movl %eax,(%rsp,%rax,1)
8b0c04	movl (%rsp,%rax,1),%ecx
8d1404	leal (%rsp,%rax,1),%edx
890444	movl %eax,(%rsp,%rax,2)
8b0c44	movl (%rsp,%rax,2),%ecx
8d1444	leal (%rsp,%rax,2),%edx
89448404	movl %eax,4(%rsp,%rax,4)
8b4c8404	movl 4(%rsp,%rax,4),%ecx
8d548404	leal 4(%rsp,%rax,4),%edx
8984c400100000	movl %eax,0x1000(%rsp,%rax,8)
8b8cc400100000	movl 0x1000(%rsp,%rax,8),%ecx
8d94c400100000	leal 0x1000(%rsp,%rax,8),%edx
8984c400000000 3:11:sym+0	movl %eax,sym(%rsp,%rax,8)
8b8cc400000000 3:11:sym+0	movl sym(%rsp,%rax,8),%ecx
8d94c400000000 3:10:sym+0	leal sym(%rsp,%rax,8),%edx
89041c	movl %eax,(%rsp,%rbx)
8b0c1c	movl (%rsp,%rbx),%ecx
8d141c	leal (%rsp,%rbx),%edx
89041c	movl %eax,(%rsp,%rbx,1)
8b0c1c	movl (%rsp,%rbx,1),%ecx
8d141c	leal (%rsp,%rbx,1),%edx
89045c	movl %eax,(%rsp,%rbx,2)
8b0c5c	movl (%rsp,%rbx,2),%ecx
8d145c	leal (%rsp,%rbx,2),%edx
89449c04	movl %eax,4(%rsp,%rbx,4)
8b4c9c04	movl 4(%rsp,%rbx,4),%ecx
8d549c04	leal 4(%rsp,%rbx,4),%edx
8984dc00100000	movl %eax,0x1000(%rsp,%rbx,8)
8b8cdc00100000	movl 0x1000(%rsp,%rbx,8),%ecx
8d94dc00100000	leal 0x1000(%rsp,%rbx,8),%edx
8984dc00000000 3:11:sym+0	movl %eax,sym(%rsp,%rbx,8)
8b8cdc00000000 3:11:sym+0	movl sym(%rsp,%rbx,8),%ecx
8d94dc00000000 3:10:sym+0	leal sym(%rsp,%rbx,8),%edx
89042c	movl %eax,(%rsp,%rbp)
8b0c2c	movl (%rsp,%rbp),%ecx
8d142c	leal (%rsp,%rbp),%edx
89042c	movl %eax,(%rsp,%rbp,1)
8b0c2c	movl (%rsp,%rbp,1),%ecx
8d142c	leal (%rsp,%rbp,1),%edx
89046c	movl %eax,(%rsp,%rbp,2)
8b0c6c	movl (%rsp,%rbp,2),%ecx
8d146c	leal (%rsp,%rbp,2),%edx
8944ac04	movl %eax,4(%rsp,%rbp,4)
8b4cac04	movl 4(%rsp,%rbp,4),%ecx
8d54ac04	leal 4(%rsp,%rbp,4),%edx
8984ec00100000	movl %eax,0x1000(%rsp,%rbp,8)
8b8cec00100000	movl 0x1000(%rsp,%rbp,8),%ecx
8d94ec00100000	leal 0x1000(%rsp,%rbp,8),%edx
8984ec00000000 3:11:sym+0	movl %eax,sym(%rsp,%rbp,8)
8b8cec00000000 3:11:sym+0	movl sym(%rsp,%rbp,8),%ecx
8d94ec00000000 3:10:sym+0	leal sym(%rsp,%rbp,8),%edx
890434	movl %eax,(%rsp,%rsi)
8b0c34	movl (%rsp,%rsi),%ecx
8d1434	leal (%rsp,%rsi),%edx
890434	movl %eax,(%rsp,%rsi,1)
8b0c34	movl (%rsp,%rsi,1),%ecx
8d1434	leal (%rsp,%rsi,1),%edx
890474	movl %eax,(%rsp,%rsi,2)
8b0c74	movl (%rsp,%rsi,2),%ecx
8d1474	leal (%rsp,%rsi,2),%edx
8944b404	movl %eax,4(%rsp,%rsi,4)
8b4cb404	movl 4(%rsp,%rsi,4),%ecx
8d54b404	leal 4(%rsp,%rsi,4),%edx
8984f400100000	movl %eax,0x1000(%rsp,%rsi,8)
8b8cf400100000	movl 0x1000(%rsp,%rsi,8),%ecx
8d94f400100000	leal 0x1000(%rsp,%rsi,8),%edx
8984f400000000 3:11:sym+0	movl %eax,sym(%rsp,%rsi,8)
8b8cf400000000 3:11:sym+0	movl sym(%rsp,%rsi,8),%ecx
8d94f400000000 3:10:sym+0	leal sym(%rsp,%rsi,8),%edx
42890424	movl %eax,(%rsp,%r12)
428b0c24	movl (%rsp,%r12),%ecx
428d1424	leal (%rsp,%r12),%edx
42890424	movl %eax,(%rsp,%r12,1)
428b0c24	movl (%rsp,%r12,1),%ecx
428d1424	leal (%rsp,%r12,1),%edx
42890464	movl %eax,(%rsp,%r12,2)
428b0c64	movl (%rsp,%r12,2),%ecx
428d1464	leal (%rsp,%r12,2),%edx
428944a404	movl %eax,4(%rsp,%r12,4)
428b4ca404	movl 4(%rsp,%r12,4),%ecx
428d54a404	leal 4(%rsp,%r12,4),%edx
428984e400100000	movl %eax,0x1000(%rsp,%r12,8)
428b8ce400100000	movl 0x1000(%rsp,%r12,8),%ecx
428d94e400100000	leal 0x1000(%rsp,%r12,8),%edx
428984e400000000 4:11:sym+0	movl %eax,sym(%rsp,%r12,8)
428b8ce400000000 4:11:sym+0	movl sym(%rsp,%r12,8),%ecx
428d94e400000000 4:10:sym+0	leal sym(%rsp,%r12,8),%edx
4289042c	movl %eax,(%rsp,%r13)
428b0c2c	movl (%rsp,%r13),%ecx
428d142c	leal (%rsp,%r13),%edx
4289042c	movl %eax,(%rsp,%r13,1)
428b0c2c	movl (%rsp,%r13,1),%ecx
428d142c	leal (%rsp,%r13,1),%edx
4289046c	movl %eax,(%rsp,%r13,2)
428b0c6c	movl (%rsp,%r13,2),%ecx
428d146c	leal (%rsp,%r13,2),%edx
428944ac04	movl %eax,4(%rsp,%r13,4)
428b4cac04	movl 4(%rsp,%r13,4),%ecx
428d54ac04	leal 4(%rsp,%r13,4),%edx
428984ec00100000	movl %eax,0x1000(%rsp,%r13,8)
428b8cec00100000	movl 0x1000(%rsp,%r13,8),%ecx
428d94ec00100000	leal 0x1000(%rsp,%r13,8),%edx
428984ec00000000 4:11:sym+0	movl %eax,sym(%rsp,%r13,8)
428b8cec00000000 4:11:sym+0	movl sym(%rsp,%r13,8),%ecx
428d94ec00000000 4:10:sym+0	leal sym(%rsp,%r13,8),%edx
4289043c	movl %eax,(%rsp,%r15)
428b0c3c	movl (%rsp,%r15),%ecx
428d143c	leal (%rsp,%r15),%edx
4289043c	movl %eax,(%rsp,%r15,1)
428b0c3c	movl (%rsp,%r15,1),%ecx
428d143c	leal (%rsp,%r15,1),%edx
4289047c	movl %eax,(%rsp,%r15,2)
428b0c7c	movl (%rsp,%r15,2),%ecx
428d147c	leal (%rsp,%r15,2),%edx
428944bc04	movl %eax,4(%rsp,%r15,4)
428b4cbc04	movl 4(%rsp,%r15,4),%ecx
428d54bc04	leal 4(%rsp,%r15,4),%edx
428984fc00100000	movl %eax,0x1000(%rsp,%r15,8)
428b8cfc00100000	movl 0x1000(%rsp,%r15,8),%ecx
428d94fc00100000	leal 0x1000(%rsp,%r15,8),%edx
428984fc00000000 4:11:sym+0	movl %eax,sym(%rsp,%r15,8)
428b8cfc00000000 4:11:sym+0	movl sym(%rsp,%r15,8),%ecx
428d94fc00000000 4:10:sym+0	leal sym(%rsp,%r15,8),%edx
89440500	movl %eax,(%rbp,%rax)
8b4c0500	movl (%rbp,%rax),%ecx
8d540500	leal (%rbp,%rax),%edx
89440500	movl %eax,(%rbp,%rax,1)
8b4c0500	movl (%rbp,%rax,1),%ecx
8d540500	leal (%rbp,%rax,1),%edx
89444500	movl %eax,(%rbp,%rax,2)
8b4c4500	movl (%rbp,%rax,2),%ecx
8d544500	leal (%rbp,%rax,2),%edx
89448504	movl %eax,4(%rbp,%rax,4)
8b4c8504	movl 4(%rbp,%rax,4),%ecx
8d548504	leal 4(%rbp,%rax,4),%edx
8984c500100000	movl %eax,0x1000(%rbp,%rax,8)
8b8cc500100000	movl 0x1000(%rbp,%rax,8),%ecx
8d94c500100000	leal 0x1000(%rbp,%rax,8),%edx
8984c500000000 3:11:sym+0	movl %eax,sym(%rbp,%rax,8)
8b8cc500000000 3:11:sym+0	movl sym(%rbp,%rax,8),%ecx
8d94c500000000 3:10:sym+0	leal sym(%rbp,%rax,8),%edx
89441d00	movl %eax,(%rbp,%rbx)
8b4c1d00	movl (%rbp,%rbx),%ecx
8d541d00	leal (%rbp,%rbx),%edx
89441d00	movl %eax,(%rbp,%rbx,1)
8b4c1d00	movl (%rbp,%rbx,1),%ecx
8d541d00	leal (%rbp,%rbx,1),%edx
89445d00	movl %eax,(%rbp,%rbx,2)
8b4c5d00	movl (%rbp,%rbx,2),%ecx
8d545d00	leal (%rbp,%rbx,2),%edx
89449d04	movl %eax,4(%rbp,%rbx,4)
8b4c9d04	movl 4(%rbp,%rbx,4),%ecx
8d549d04	leal 4(%rbp,%rbx,4),%edx
8984dd00100000	movl %eax,0x1000(%rbp,%rbx,8)
8b8cdd00100000	movl 0x1000(%rbp,%rbx,8),%ecx
8d94dd00100000	leal 0x1000(%rbp,%rbx,8),%edx
8984dd00000000 3:11:sym+0	movl %eax,sym(%rbp,%rbx,8)
8b8cdd00000000 3:11:sym+0	movl sym(%rbp,%rbx,8),%ecx
8d94dd00000000 3:10:sym+0	leal sym(%rbp,%rbx,8),%edx
89442d00	movl %eax,(%rbp,%rbp)
8b4c2d00	movl (%rbp,%rbp),%ecx
8d542d00	leal (%rbp,%rbp),%edx
89442d00	movl %eax,(%rbp,%rbp,1)
8b4c2d00	movl (%rbp,%rbp,1),%ecx
8d542d00	leal (%rbp,%rbp,1),%edx
89446d00	movl %eax,(%rbp,%rbp,2)
8b4c6d00	movl (%rbp,%rbp,2),%ecx
8d546d00	leal (%rbp,%rbp,2),%edx
8944ad04	movl %eax,4(%rbp,%rbp,4)
8b4cad04	movl 4(%rbp,%rbp,4),%ecx
8d54ad04	leal 4(%rbp,%rbp,4),%edx
8984ed00100000	movl %eax,0x1000(%rbp,%rbp,8)
8b8ced00100000	movl 0x1000(%rbp,%rbp,8),%ecx
8d94ed00100000	leal 0x1000(%rbp,%rbp,8),%edx
8984ed00000000 3:11:sym+0	movl %eax,sym(%rbp,%rbp,8)
8b8ced00000000 3:11:sym+0	movl sym(%rbp,%rbp,8),%ecx
8d94ed00000000 3:10:sym+0	leal sym(%rbp,%rbp,8),%edx
89443500	movl %eax,(%rbp,%rsi)
8b4c3500	movl (%rbp,%rsi),%ecx
8d543500	leal (%rbp,%rsi),%edx
89443500	movl %eax,(%rbp,%rsi,1)
8b4c3500	movl (%rbp,%rsi,1),%ecx
8d543500	leal (%rbp,%rsi,1),%edx
89447500	movl %eax,(%rbp,%rsi,2)
8b4c7500	movl (%rbp,%rsi,2),%ecx
8d547500	leal (%rbp,%rsi,2),%edx
8944b504	movl %eax,4(%rbp,%rsi,4)
8b4cb504	movl 4(%rbp,%rsi,4),%ecx
8d54b504	leal 4(%rbp,%rsi,4),%edx
8984f500100000	movl %eax,0x1000(%rbp,%rsi,8)
8b8cf500100000	movl 0x1000(%rbp,%rsi,8),%ecx
8d94f500100000	leal 0x1000(%rbp,%rsi,8),%edx
8984f500000000 3:11:sym+0	movl %eax,sym(%rbp,%rsi,8)
8b8cf500000000 3:11:sym+0	movl sym(%rbp,%rsi,8),%ecx
8d94f500000000 3:10:sym+0	leal sym(%rbp,%rsi,8),%edx
4289442500	movl %eax,(%rbp,%r12)
428b4c2500	movl (%rbp,%r12),%ecx
428d542500	leal (%rbp,%r12),%edx
4289442500	movl %eax,(%rbp,%r12,1)
428b4c2500	movl (%rbp,%r12,1),%ecx
428d542500	leal (%rbp,%r12,1),%edx
4289446500	movl %eax,(%rbp,%r12,2)
428b4c6500	movl (%rbp,%r12,2),%ecx
428d546500	leal (%rbp,%r12,2),%edx
428944a504	movl %eax,4(%rbp,%r12,4)
428b4ca504	movl 4(%rbp,%r12,4),%ecx
428d54a504	leal 4(%rbp,%r12,4),%edx
428984e500100000	movl %eax,0x1000(%rbp,%r12,8)
428b8ce500100000	movl 0x1000(%rbp,%r12,8),%ecx
428d94e500100000	leal 0x1000(%rbp,%r12,8),%edx
428984e500000000 4:11:sym+0	movl %eax,sym(%rbp,%r12,8)
428b8ce500000000 4:11:sym+0	movl sym(%rbp,%r12,8),%ecx
428d94e500000000 4:10:sym+0	leal sym(%rbp,%r12,8),%edx
4289442d00	movl %eax,(%rbp,%r13)
428b4c2d00	movl (%rbp,%r13),%ecx
428d542d00	leal (%rbp,%r13),%edx
4289442d00	movl %eax,(%rbp,%r13,1)
428b4c2d00	movl (%rbp,%r13,1),%ecx
428d542d00	leal (%rbp,%r13,1),%edx
4289446d00	movl %eax,(%rbp,%r13,2)
428b4c6d00	movl (%rbp,%r13,2),%ecx
428d546d00	leal (%rbp,%r13,2),%edx
428944ad04	movl %eax,4(%rbp,%r13,4)
428b4cad04	movl 4(%rbp,%r13,4),%ecx
428d54ad04	leal 4(%rbp,%r13,4),%edx
428984ed00100000	movl %eax,0x1000(%rbp,%r13,8)
428b8ced00100000	movl 0x1000(%rbp,%r13,8),%ecx
428d94ed00100000	leal 0x1000(%rbp,%r13,8),%edx
428984ed00000000 4:11:sym+0	movl %eax,sym(%rbp,%r13,8)
428b8ced00000000 4:11:sym+0	movl sym(%rbp,%r13,8),%ecx
428d94ed00000000 4:10:sym+0	leal sym(%rbp,%r13,8),%edx
4289443d00	movl %eax,(%rbp,%r15)
428b4c3d00	movl (%rbp,%r15),%ecx
428d543d00	leal (%rbp,%r15),%edx
4289443d00	movl %eax,(%rbp,%r15,1)
428b4c3d00	movl (%rbp,%r15,1),%ecx
428d543d00	leal (%rbp,%r15,1),%edx
4289447d00	movl %eax,(%rbp,%r15,2)
428b4c7d00	movl (%rbp,%r15,2),%ecx
428d547d00	leal (%rbp,%r15,2),%edx
428944bd04	movl %eax,4(%rbp,%r15,4)
428b4cbd04	movl 4(%rbp,%r15,4),%ecx
428d54bd04	leal 4(%rbp,%r15,4),%edx
428984fd00100000	movl %eax,0x1000(%rbp,%r15,8)
428b8cfd00100000	movl 0x1000(%rbp,%r15,8),%ecx
428d94fd00100000	leal 0x1000(%rbp,%r15,8),%edx
428984fd00000000 4:11:sym+0	movl %eax,sym(%rbp,%r15,8)
428b8cfd00000000 4:11:sym+0	movl sym(%rbp,%r15,8),%ecx
428d94fd00000000 4:10:sym+0	leal sym(%rbp,%r15,8),%edx
890406	movl %eax,(%rsi,%rax)
8b0c06	movl (%rsi,%rax),%ecx
8d1406	leal (%rsi,%rax),%edx
890406	movl %eax,(%rsi,%rax,1)
8b0c06	movl (%rsi,%rax,1),%ecx
8d1406	leal (%rsi,%rax,1),%edx
890446	movl %eax,(%rsi,%rax,2)
8b0c46	movl (%rsi,%rax,2),%ecx
8d1446	leal (%rsi,%rax,2),%edx
89448604	movl %eax,4(%rsi,%rax,4)
8b4c8604	movl 4(%rsi,%rax,4),%ecx
8d548604	leal 4(%rsi,%rax,4),%edx
8984c600100000	movl %eax,0x1000(%rsi,%rax,8)
8b8cc600100000	movl 0x1000(%rsi,%rax,8),%ecx
8d94c600100000	leal 0x1000(%rsi,%rax,8),%edx
8984c600000000 3:11:sym+0	movl %eax,sym(%rsi,%rax,8)
8b8cc600000000 3:11:sym+0	movl sym(%rsi,%rax,8),%ecx
8d94c600000000 3:10:sym+0	leal sym(%rsi,%rax,8),%edx
89041e	movl %eax,(%rsi,%rbx)
8b0c1e	movl (%rsi,%rbx),%ecx
8d141e	leal (%rsi,%rbx),%edx
89041e	movl %eax,(%rsi,%rbx,1)
8b0c1e	movl (%rsi,%rbx,1),%ecx
8d141e	leal (%rsi,%rbx,1),%edx
89045e	movl %eax,(%rsi,%rbx,2)
8b0c5e	movl (%rsi,%rbx,2),%ecx
8d145e	leal (%rsi,%rbx,2),%edx
89449e04	movl %eax,4(%rsi,%rbx,4)
8b4c9e04	movl 4(%rsi,%rbx,4),%ecx
8d549e04	leal 4(%rsi,%rbx,4),%edx
8984de00100000	movl %eax,0x1000(%rsi,%rbx,8)
8b8cde00100000	movl 0x1000(%rsi,%rbx,8),%ecx
8d94de00100000	leal 0x1000(%rsi,%rbx,8),%edx
8984de00000000 3:11:sym+0	movl %eax,sym(%rsi,%rbx,8)
8b8cde00000000 3:11:sym+0	movl sym(%rsi,%rbx,8),%ecx
8d94de00000000 3:10:sym+0	leal sym(%rsi,%rbx,8),%edx
89042e	movl %eax,(%rsi,%rbp)
8b0c2e	movl (%rsi,%rbp),%ecx
8d142e	leal (%rsi,%rbp),%edx
89042e	movl %eax,(%rsi,%rbp,1)
8b0c2e	movl (%rsi,%rbp,1),%ecx
8d142e	leal (%rsi,%rbp,1),%edx
89046e	movl %eax,(%rsi,%rbp,2)
8b0c6e	movl (%rsi,%rbp,2),%ecx
8d146e	leal (%rsi,%rbp,2),%edx
8944ae04	movl %eax,4(%rsi,%rbp,4)
8b4cae04	movl 4(%rsi,%rbp,4),%ecx
8d54ae04	leal 4(%rsi,%rbp,4),%edx
8984ee00100000	movl %eax,0x1000(%rsi,%rbp,8)
8b8cee00100000	movl 0x1000(%rsi,%rbp,8),%ecx
8d94ee00100000	leal 0x1000(%rsi,%rbp,8),%edx
8984ee00000000 3:11:sym+0	movl %eax,sym(%rsi,%rbp,8)
8b8cee00000000 3:11:sym+0	movl sym(%rsi,%rbp,8),%ecx
8d94ee00000000 3:10:sym+0	leal sym(%rsi,%rbp,8),%edx
890436	movl %eax,(%rsi,%rsi)
8b0c36	movl (%rsi,%rsi),%ecx
8d1436	leal (%rsi,%rsi),%edx
890436	movl %eax,(%rsi,%rsi,1)
8b0c36	movl (%rsi,%rsi,1),%ecx
8d1436	leal (%rsi,%rsi,1),%edx
890476	movl %eax,(%rsi,%rsi,2)
8b0c76	movl (%rsi,%rsi,2),%ecx
8d1476	leal (%rsi,%rsi,2),%edx
8944b604	movl %eax,4(%rsi,%rsi,4)
8b4cb604	movl 4(%rsi,%rsi,4),%ecx
8d54b604	leal 4(%rsi,%rsi,4),%edx
8984f600100000	movl %eax,0x1000(%rsi,%rsi,8)
8b8cf600100000	movl 0x1000(%rsi,%rsi,8),%ecx
8d94f600100000	leal 0x1000(%rsi,%rsi,8),%edx
8984f600000000 3:11:sym+0	movl %eax,sym(%rsi,%rsi,8)
8b8cf600000000 3:11:sym+0	movl sym(%rsi,%rsi,8),%ecx
8d94f600000000 3:10:sym+0	leal sym(%rsi,%rsi,8),%edx
42890426	movl %eax,(%rsi,%r12)
428b0c26	movl (%rsi,%r12),%ecx
428d1426	leal (%rsi,%r12),%edx
42890426	movl %eax,(%rsi,%r12,1)
428b0c26	movl (%rsi,%r12,1),%ecx
428d1426	leal (%rsi,%r12,1),%edx
42890466	movl %eax,(%rsi,%r12,2)
428b0c66	movl (%rsi,%r12,2),%ecx
428d1466	leal (%rsi,%r12,2),%edx
428944a604	movl %eax,4(%rsi,%r12,4)
428b4ca604	movl 4(%rsi,%r12,4),%ecx
428d54a604	leal 4(%rsi,%r12,4),%edx
428984e600100000	movl %eax,0x1000(%rsi,%r12,8)
428b8ce600100000	movl 0x1000(%rsi,%r12,8),%ecx
428d94e600100000	leal 0x1000(%rsi,%r12,8),%edx
428984e600000000 4:11:sym+0	movl %eax,sym(%rsi,%r12,8)
428b8ce600000000 4:11:sym+0	movl sym(%rsi,%r12,8),%ecx
428d94e600000000 4:10:sym+0	leal sym(%rsi,%r12,8),%edx
4289042e	movl %eax,(%rsi,%r13)
428b0c2e	movl (%rsi,%r13),%ecx
428d142e	leal (%rsi,%r13),%edx
4289042e	movl %eax,(%rsi,%r13,1)
428b0c2e	movl (%rsi,%r13,1),%ecx
428d142e	leal (%rsi,%r13,1),%edx
4289046e	movl %eax,(%rsi,%r13,2)
428b0c6e	movl (%rsi,%r13,2),%ecx
428d146e	leal (%rsi,%r13,2),%edx
428944ae04	movl %eax,4(%rsi,%r13,4)
428b4cae04	movl 4(%rsi,%r13,4),%ecx
428d54ae04	leal 4(%rsi,%r13,4),%edx
428984ee00100000	movl %eax,0x1000(%rsi,%r13,8)
428b8cee00100000	movl 0x1000(%rsi,%r13,8),%ecx
428d94ee00100000	leal 0x1000(%rsi,%r13,8),%edx
428984ee00000000 4:11:sym+0	movl %eax,sym(%rsi,%r13,8)
428b8cee00000000 4:11:sym+0	movl sym(%rsi,%r13,8),%ecx
428d94ee00000000 4:10:sym+0	leal sym(%rsi,%r13,8),%edx
4289043e	movl %eax,(%rsi,%r15)
428b0c3e	movl (%rsi,%r15),%ecx
428d143e	leal (%rsi,%r15),%edx
4289043e	movl %eax,(%rsi,%r15,1)
428b0c3e	movl (%rsi,%r15,1),%ecx
428d143e	leal (%rsi,%r15,1),%edx
4289047e	movl %eax,(%rsi,%r15,2)
428b0c7e	movl (%rsi,%r15,2),%ecx
428d147e	leal (%rsi,%r15,2),%edx
428944be04	movl %eax,4(%rsi,%r15,4)
428b4cbe04	movl 4(%rsi,%r15,4),%ecx
428d54be04	leal 4(%rsi,%r15,4),%edx
428984fe00100000	movl %eax,0x1000(%rsi,%r15,8)
428b8cfe00100000	movl 0x1000(%rsi,%r15,8),%ecx
428d94fe00100000	leal 0x1000(%rsi,%r15,8),%edx
428984fe00000000 4:11:sym+0	movl %eax,sym(%rsi,%r15,8)
428b8cfe00000000 4:11:sym+0	movl sym(%rsi,%r15,8),%ecx
428d94fe00000000 4:10:sym+0	leal sym(%rsi,%r15,8),%edx
41890404	movl %eax,(%r12,%rax)
418b0c04	movl (%r12,%rax),%ecx
418d1404	leal (%r12,%rax),%edx
41890404	movl %eax,(%r12,%rax,1)
418b0c04	movl (%r12,%rax,1),%ecx
418d1404	leal (%r12,%rax,1),%edx
41890444	movl %eax,(%r12,%rax,2)
418b0c44	movl (%r12,%rax,2),%ecx
418d1444	leal (%r12,%rax,2),%edx
4189448404	movl %eax,4(%r12,%rax,4)
418b4c8404	movl 4(%r12,%rax,4),%ecx
418d548404	leal 4(%r12,%rax,4),%edx
418984c400100000	movl %eax,0x1000(%r12,%rax,8)
418b8cc400100000	movl 0x1000(%r12,%rax,8),%ecx
418d94c400100000	leal 0x1000(%r12,%rax,8),%edx
418984c400000000 4:11:sym+0	movl %eax,sym(%r12,%rax,8)
418b8cc400000000 4:11:sym+0	movl sym(%r12,%rax,8),%ecx
418d94c400000000 4:10:sym+0	leal sym(%r12,%rax,8),%edx
4189041c	movl %eax,(%r12,%rbx)
418b0c1c	movl (%r12,%rbx),%ecx
418d141c	leal (%r12,%rbx),%edx
4189041c	movl %eax,(%r12,%rbx,1)
418b0c1c	movl (%r12,%rbx,1),%ecx
418d141c	leal (%r12,%rbx,1),%edx
4189045c	movl %eax,(%r12,%rbx,2)
418b0c5c	movl (%r12,%rbx,2),%ecx
418d145c	leal (%r12,%rbx,2),%edx
4189449c04	movl %eax,4(%r12,%rbx,4)
418b4c9c04	movl 4(%r12,%rbx,4),%ecx
418d549c04	leal 4(%r12,%rbx,4),%edx
418984dc00100000	movl %eax,0x1000(%r12,%rbx,8)
418b8cdc00100000	movl 0x1000(%r12,%rbx,8),%ecx
418d94dc00100000	leal 0x1000(%r12,%rbx,8),%edx
418984dc00000000 4:11:sym+0	movl %eax,sym(%r12,%rbx,8)
418b8cdc00000000 4:11:sym+0	movl sym(%r12,%rbx,8),%ecx
418d94dc00000000 4:10:sym+0	leal sym(%r12,%rbx,8),%edx
4189042c	movl %eax,(%r12,%rbp)
418b0c2c	movl (%r12,%rbp),%ecx
418d142c	leal (%r12,%rbp),%edx
4189042c	movl %eax,(%r12,%rbp,1)
418b0c2c	movl (%r12,%rbp,1),%ecx
418d142c	leal (%r12,%rbp,1),%edx
4189046c	movl %eax,(%r12,%rbp,2)
418b0c6c	movl (%r12,%rbp,2),%ecx
418d146c	leal (%r12,%rbp,2),%edx
418944ac04	movl %eax,4(%r12,%rbp,4)
418b4cac04	movl 4(%r12,%rbp,4),%ecx
418d54ac04	leal 4(%r12,%rbp,4),%edx
418984ec00100000	movl %eax,0x1000(%r12,%rbp,8)
418b8cec00100000	movl 0x1000(%r12,%rbp,8),%ecx
418d94ec00100000	leal 0x1000(%r12,%rbp,8),%edx
418984ec00000000 4:11:sym+0	movl %eax,sym(%r12,%rbp,8)
418b8cec00000000 4:11:sym+0	movl sym(%r12,%rbp,8),%ecx
418d94ec00000000 4:10:sym+0	leal sym(%r12,%rbp,8),%edx
41890434	movl %eax,(%r12,%rsi)
418b0c34	movl (%r12,%rsi),%ecx
418d1434	leal (%r12,%rsi),%edx
41890434	movl %eax,(%r12,%rsi,1)
418b0c34	movl (%r12,%rsi,1),%ecx
418d1434	leal (%r12,%rsi,1),%edx
41890474	movl %eax,(%r12,%rsi,2)
418b0c74	movl (%r12,%rsi,2),%ecx
418d1474	leal (%r12,%rsi,2),%edx
418944b404	movl %eax,4(%r12,%rsi,4)
418b4cb404	movl 4(%r12,%rsi,4),%ecx
418d54b404	leal 4(%r12,%rsi,4),%edx
418984f400100000	movl %eax,0x1000(%r12,%rsi,8)
418b8cf400100000	movl 0x1000(%r12,%rsi,8),%ecx
418d94f400100000	leal 0x1000(%r12,%rsi,8),%edx
418984f400000000 4:11:sym+0	movl %eax,sym(%r12,%rsi,8)
418b8cf400000000 4:11:sym+0	movl sym(%r12,%rsi,8),%ecx
418d94f400000000 4:10:sym+0	leal sym(%r12,%rsi,8),%edx
43890424	movl %eax,(%r12,%r12)
438b0c24	movl (%r12,%r12),%ecx
438d1424	leal (%r12,%r12),%edx
43890424	movl %eax,(%r12,%r12,1)
438b0c24	movl (%r12,%r12,1),%ecx
438d1424	leal (%r12,%r12,1),%edx
43890464	movl %eax,(%r12,%r12,2)
438b0c64	movl (%r12,%r12,2),%ecx
438d1464	leal (%r12,%r12,2),%edx
438944a404	movl %eax,4(%r12,%r12,4)
438b4ca404	movl 4(%r12,%r12,4),%ecx
438d54a404	leal 4(%r12,%r12,4),%edx
438984e400100000	movl %eax,0x1000(%r12,%r12,8)
438b8ce400100000	movl 0x1000(%r12,%r12,8),%ecx
438d94e400100000	leal 0x1000(%r12,%r12,8),%edx
438984e400000000 4:11:sym+0	movl %eax,sym(%r12,%r12,8)
438b8ce400000000 4:11:sym+0	movl sym(%r12,%r12,8),%ecx
438d94e400000000 4:10:sym+0	leal sym(%r12,%r12,8),%edx
4389042c	movl %eax,(%r12,%r13)
438b0c2c	movl (%r12,%r13),%ecx
438d142c	leal (%r12,%r13),%edx
4389042c	movl %eax,(%r12,%r13,1)
438b0c2c	movl (%r12,%r13,1),%ecx
438d142c	leal (%r12,%r13,1),%edx
4389046c	movl %eax,(%r12,%r13,2)
438b0c6c	movl (%r12,%r13,2),%ecx
438d146c	leal (%r12,%r13,2),%edx
438944ac04	movl %eax,4(%r12,%r13,4)
438b4cac04	movl 4(%r12,%r13,4),%ecx
438d54ac04	leal 4(%r12,%r13,4),%edx
438984ec00100000	movl %eax,0x1000(%r12,%r13,8)
438b8cec00100000	movl 0x1000(%r12,%r13,8),%ecx
438d94ec00100000	leal 0x1000(%r12,%r13,8),%edx
438984ec00000000 4:11:sym+0	movl %eax,sym(%r12,%r13,8)
438b8cec00000000 4:11:sym+0	movl sym(%r12,%r13,8),%ecx
438d94ec00000000 4:10:sym+0	leal sym(%r12,%r13,8),%edx
4389043c	movl %eax,(%r12,%r15)
438b0c3c	movl (%r12,%r15),%ecx
438d143c	leal (%r12,%r15),%edx
4389043c	movl %eax,(%r12,%r15,1)
438b0c3c	movl (%r12,%r15,1),%ecx
438d143c	leal (%r12,%r15,1),%edx
4389047c	movl %eax,(%r12,%r15,2)
438b0c7c	movl (%r12,%r15,2),%ecx
438d147c	leal (%r12,%r15,2),%edx
438944bc04	movl %eax,4(%r12,%r15,4)
438b4cbc04	movl 4(%r12,%r15,4),%ecx
438d54bc04	leal 4(%r12,%r15,4),%edx
438984fc00100000	movl %eax,0x1000(%r12,%r15,8)
438b8cfc00100000	movl 0x1000(%r12,%r15,8),%ecx
438d94fc00100000	leal 0x1000(%r12,%r15,8),%edx
438984fc00000000 4:11:sym+0	movl %eax,sym(%r12,%r15,8)
438b8cfc00000000 4:11:sym+0	movl sym(%r12,%r15,8),%ecx
438d94fc00000000 4:10:sym+0	leal sym(%r12,%r15,8),%edx
4189440500	movl %eax,(%r13,%rax)
418b4c0500	movl (%r13,%rax),%ecx
418d540500	leal (%r13,%rax),%edx
4189440500	movl %eax,(%r13,%rax,1)
418b4c0500	movl (%r13,%rax,1),%ecx
418d540500	leal (%r13,%rax,1),%edx
4189444500	movl %eax,(%r13,%rax,2)
418b4c4500	movl (%r13,%rax,2),%ecx
418d544500	leal (%r13,%rax,2),%edx
4189448504	movl %eax,4(%r13,%rax,4)
418b4c8504	movl 4(%r13,%rax,4),%ecx
418d548504	leal 4(%r13,%rax,4),%edx
418984c500100000	movl %eax,0x1000(%r13,%rax,8)
418b8cc500100000	movl 0x1000(%r13,%rax,8),%ecx
418d94c500100000	leal 0x1000(%r13,%rax,8),%edx
418984c500000000 4:11:sym+0	movl %eax,sym(%r13,%rax,8)
418b8cc500000000 4:11:sym+0	movl sym(%r13,%rax,8),%ecx
418d94c500000000 4:10:sym+0	leal sym(%r13,%rax,8),%edx
4189441d00	movl %eax,(%r13,%rbx)
418b4c1d00	movl (%r13,%rbx),%ecx
418d541d00	leal (%r13,%rbx),%edx
4189441d00	movl %eax,(%r13,%rbx,1)
418b4c1d00	movl (%r13,%rbx,1),%ecx
418d541d00	leal (%r13,%rbx,1),%edx
4189445d00	movl %eax,(%r13,%rbx,2)
418b4c5d00	movl (%r13,%rbx,2),%ecx
418d545d00	leal (%r13,%rbx,2),%edx
4189449d04	movl %eax,4(%r13,%rbx,4)
418b4c9d04	movl 4(%r13,%rbx,4),%ecx
418d549d04	leal 4(%r13,%rbx,4),%edx
418984dd00100000	movl %eax,0x1000(%r13,%rbx,8)
418b8cdd00100000	movl 0x1000(%r13,%rbx,8),%ecx
418d94dd00100000	leal 0x1000(%r13,%rbx,8),%edx
418984dd00000000 4:11:sym+0	movl %eax,sym(%r13,%rbx,8)
418b8cdd00000000 4:11:sym+0	movl sym(%r13,%rbx,8),%ecx
418d94dd00000000 4:10:sym+0	leal sym(%r13,%rbx,8),%edx
4189442d00	movl %eax,(%r13,%rbp)
418b4c2d00	movl (%r13,%rbp),%ecx
418d542d00	leal (%r13,%rbp),%edx
4189442d00	movl %eax,(%r13,%rbp,1)
418b4c2d00	movl (%r13,%rbp,1),%ecx
418d542d00	leal (%r13,%rbp,1),%edx
4189446d00	movl %eax,(%r13,%rbp,2)
418b4c6d00	movl (%r13,%rbp,2),%ecx
418d546d00	leal (%r13,%rbp,2),%edx
418944ad04	movl %eax,4(%r13,%rbp,4)
418b4cad04	movl 4(%r13,%rbp,4),%ecx
418d54ad04	leal 4(%r13,%rbp,4),%edx
418984ed00100000	movl %eax,0x1000(%r13,%rbp,8)
418b8ced00100000	movl 0x1000(%r13,%rbp,8),%ecx
418d94ed00100000	leal 0x1000(%r13,%rbp,8),%edx
418984ed00000000 4:11:sym+0	movl %eax,sym(%r13,%rbp,8)
418b8ced00000000 4:11:sym+0	movl sym(%r13,%rbp,8),%ecx
418d94ed00000000 4:10:sym+0	leal sym(%r13,%rbp,8),%edx
4189443500	movl %eax,(%r13,%rsi)
418b4c3500	movl (%r13,%rsi),%ecx
418d543500	leal (%r13,%rsi),%edx
4189443500	movl %eax,(%r13,%rsi,1)
418b4c3500	movl (%r13,%rsi,1),%ecx
418d543500	leal (%r13,%rsi,1),%edx
4189447500	movl %eax,(%r13,%rsi,2)
418b4c7500	movl (%r13,%rsi,2),%ecx
418d547500	leal (%r13,%rsi,2),%edx
418944b504	movl %eax,4(%r13,%rsi,4)
418b4cb504	movl 4(%r13,%rsi,4),%ecx
418d54b504	leal 4(%r13,%rsi,4),%edx
418984f500100000	movl %eax,0x1000(%r13,%rsi,8)
418b8cf500100000	movl 0x1000(%r13,%rsi,8),%ecx
418d94f500100000	leal 0x1000(%r13,%rsi,8),%edx
418984f500000000 4:11:sym+0	movl %eax,sym(%r13,%rsi,8)
418b8cf500000000 4:11:sym+0	movl sym(%r13,%rsi,8),%ecx
418d94f500000000 4:10:sym+0	leal sym(%r13,%rsi,8),%edx
4389442500	movl %eax,(%r13,%r12)
438b4c2500	movl (%r13,%r12),%ecx
438d542500	leal (%r13,%r12),%edx
4389442500	movl %eax,(%r13,%r12,1)
438b4c2500	movl (%r13,%r12,1),%ecx
438d542500	leal (%r13,%r12,1),%edx
4389446500	movl %eax,(%r13,%r12,2)
438b4c6500	movl (%r13,%r12,2),%ecx
438d546500	leal (%r13,%r12,2),%edx
438944a504	movl %eax,4(%r13,%r12,4)
438b4ca504	movl 4(%r13,%r12,4),%ecx
438d54a504	leal 4(%r13,%r12,4),%edx
438984e500100000	movl %eax,0x1000(%r13,%r12,8)
438b8ce500100000	movl 0x1000(%r13,%r12,8),%ecx
438d94e500100000	leal 0x1000(%r13,%r12,8),%edx
438984e500000000 4:11:sym+0	movl %eax,sym(%r13,%r12,8)
438b8ce500000000 4:11:sym+0	movl sym(%r13,%r12,8),%ecx
438d94e500000000 4:10:sym+0	leal sym(%r13,%r12,8),%edx
4389442d00	movl %eax,(%r13,%r13)
438b4c2d00	movl (%r13,%r13),%ecx
438d542d00	leal (%r13,%r13),%edx
4389442d00	movl %eax,(%r13,%r13,1)
438b4c2d00	movl (%r13,%r13,1),%ecx
438d542d00	leal (%r13,%r13,1),%edx
4389446d00	movl %eax,(%r13,%r13,2)
438b4c6d00	movl (%r13,%r13,2),%ecx
438d546d00	leal (%r13,%r13,2),%edx
438944ad04	movl %eax,4(%r13,%r13,4)
438b4cad04	movl 4(%r13,%r13,4),%ecx
438d54ad04	leal 4(%r13,%r13,4),%edx
438984ed00100000	movl %eax,0x1000(%r13,%r13,8)
438b8ced00100000	movl 0x1000(%r13,%r13,8),%ecx
438d94ed00100000	leal 0x1000(%r13,%r13,8),%edx
438984ed00000000 4:11:sym+0	movl %eax,sym(%r13,%r13,8)
438b8ced00000000 4:11:sym+0	movl sym(%r13,%r13,8),%ecx
438d94ed00000000 4:10:sym+0	leal sym(%r13,%r13,8),%edx
4389443d00	movl %eax,(%r13,%r15)
438b4c3d00	movl (%r13,%r15),%ecx
438d543d00	leal (%r13,%r15),%edx
4389443d00	movl %eax,(%r13,%r15,1)
438b4c3d00	movl (%r13,%r15,1),%ecx
438d543d00	leal (%r13,%r15,1),%edx
4389447d00	movl %eax,(%r13,%r15,2)
438b4c7d00	movl (%r13,%r15,2),%ecx
438d547d00	leal (%r13,%r15,2),%edx
438944bd04	movl %eax,4(%r13,%r15,4)
438b4cbd04	movl 4(%r13,%r15,4),%ecx
438d54bd04	leal 4(%r13,%r15,4),%edx
438984fd00100000	movl %eax,0x1000(%r13,%r15,8)
438b8cfd00100000	movl 0x1000(%r13,%r15,8),%ecx
438d94fd00100000	leal 0x1000(%r13,%r15,8),%edx
438984fd00000000 4:11:sym+0	movl %eax,sym(%r13,%r15,8)
438b8cfd00000000 4:11:sym+0	movl sym(%r13,%r15,8),%ecx
438d94fd00000000 4:10:sym+0	leal sym(%r13,%r15,8),%edx
41890407	movl %eax,(%r15,%rax)
418b0c07	movl (%r15,%rax),%ecx
418d1407	leal (%r15,%rax),%edx
41890407	movl %eax,(%r15,%rax,1)
418b0c07	movl (%r15,%rax,1),%ecx
418d1407	leal (%r15,%rax,1),%edx
41890447	movl %eax,(%r15,%rax,2)
418b0c47	movl (%r15,%rax,2),%ecx
418d1447	leal (%r15,%rax,2),%edx
4189448704	movl %eax,4(%r15,%rax,4)
418b4c8704	movl 4(%r15,%rax,4),%ecx
418d548704	leal 4(%r15,%rax,4),%edx
418984c700100000	movl %eax,0x1000(%r15,%rax,8)
418b8cc700100000	movl 0x1000(%r15,%rax,8),%ecx
418d94c700100000	leal 0x1000(%r15,%rax,8),%edx
418984c700000000 4:11:sym+0	movl %eax,sym(%r15,%rax,8)
418b8cc700000000 4:11:sym+0	movl sym(%r15,%rax,8),%ecx
418d94c700000000 4:10:sym+0	leal sym(%r15,%rax,8),%edx
4189041f	movl %eax,(%r15,%rbx)
418b0c1f	movl (%r15,%rbx),%ecx
418d141f	leal (%r15,%rbx),%edx
4189041f	movl %eax,(%r15,%rbx,1)
418b0c1f	movl (%r15,%rbx,1),%ecx
418d141f	leal (%r15,%rbx,1),%edx
4189045f	movl %eax,(%r15,%rbx,2)
418b0c5f	movl (%r15,%rbx,2),%ecx
418d145f	leal (%r15,%rbx,2),%edx
4189449f04	movl %eax,4(%r15,%rbx,4)
418b4c9f04	movl 4(%r15,%rbx,4),%ecx
418d549f04	leal 4(%r15,%rbx,4),%edx
418984df00100000	movl %eax,0x1000(%r15,%rbx,8)
418b8cdf00100000	movl 0x1000(%r15,%rbx,8),%ecx
418d94df00100000	leal 0x1000(%r15,%rbx,8),%edx
418984df00000000 4:11:sym+0	movl %eax,sym(%r15,%rbx,8)
418b8cdf00000000 4:11:sym+0	movl sym(%r15,%rbx,8),%ecx
418d94df00000000 4:10:sym+0	leal sym(%r15,%rbx,8),%edx
4189042f	movl %eax,(%r15,%rbp)
418b0c2f	movl (%r15,%rbp),%ecx
418d142f	leal (%r15,%rbp),%edx
4189042f	movl %eax,(%r15,%rbp,1)
418b0c2f	movl (%r15,%rbp,1),%ecx
418d142f	leal (%r15,%rbp,1),%edx
4189046f	movl %eax,(%r15,%rbp,2)
418b0c6f	movl (%r15,%rbp,2),%ecx
418d146f	leal (%r15,%rbp,2),%edx
418944af04	movl %eax,4(%r15,%rbp,4)
418b4caf04	movl 4(%r15,%rbp,4),%ecx
418d54af04	leal 4(%r15,%rbp,4),%edx
418984ef00100000	movl %eax,0x1000(%r15,%rbp,8)
418b8cef00100000	movl 0x1000(%r15,%rbp,8),%ecx
418d94ef00100000	leal 0x1000(%r15,%rbp,8),%edx
418984ef00000000 4:11:sym+0	movl %eax,sym(%r15,%rbp,8)
418b8cef00000000 4:11:sym+0	movl sym(%r15,%rbp,8),%ecx
418d94ef00000000 4:10:sym+0	leal sym(%r15,%rbp,8),%edx
41890437	movl %eax,(%r15,%rsi)
418b0c37	movl (%r15,%rsi),%ecx
418d1437	leal (%r15,%rsi),%edx
41890437	movl %eax,(%r15,%rsi,1)
418b0c37	movl (%r15,%rsi,1),%ecx
418d1437	leal (%r15,%rsi,1),%edx
41890477	movl %eax,(%r15,%rsi,2)
418b0c77	movl (%r15,%rsi,2),%ecx
418d1477	leal (%r15,%rsi,2),%edx
418944b704	movl %eax,4(%r15,%rsi,4)
418b4cb704	movl 4(%r15,%rsi,4),%ecx
418d54b704	leal 4(%r15,%rsi,4),%edx
418984f700100000	movl %eax,0x1000(%r15,%rsi,8)
418b8cf700100000	movl 0x1000(%r15,%rsi,8),%ecx
418d94f700100000	leal 0x1000(%r15,%rsi,8),%edx
418984f700000000 4:11:sym+0	movl %eax,sym(%r15,%rsi,8)
418b8cf700000000 4:11:sym+0	movl sym(%r15,%rsi,8),%ecx
418d94f700000000 4:10:sym+0	leal sym(%r15,%rsi,8),%edx
43890427	movl %eax,(%r15,%r12)
438b0c27	movl (%r15,%r12),%ecx
438d1427	leal (%r15,%r12),%edx
43890427	movl %eax,(%r15,%r12,1)
438b0c27	movl (%r15,%r12,1),%ecx
438d1427	leal (%r15,%r12,1),%edx
43890467	movl %eax,(%r15,%r12,2)
438b0c67	movl (%r15,%r12,2),%ecx
438d1467	leal (%r15,%r12,2),%edx
438944a704	movl %eax,4(%r15,%r12,4)
438b4ca704	movl 4(%r15,%r12,4),%ecx
438d54a704	leal 4(%r15,%r12,4),%edx
438984e700100000	movl %eax,0x1000(%r15,%r12,8)
438b8ce700100000	movl 0x1000(%r15,%r12,8),%ecx
438d94e700100000	leal 0x1000(%r15,%r12,8),%edx
438984e700000000 4:11:sym+0	movl %eax,sym(%r15,%r12,8)
438b8ce700000000 4:11:sym+0	movl sym(%r15,%r12,8),%ecx
438d94e700000000 4:10:sym+0	leal sym(%r15,%r12,8),%edx
4389042f	movl %eax,(%r15,%r13)
438b0c2f	movl (%r15,%r13),%ecx
438d142f	leal (%r15,%r13),%edx
4389042f	movl %eax,(%r15,%r13,1)
438b0c2f	movl (%r15,%r13,1),%ecx
438d142f	leal (%r15,%r13,1),%edx
4389046f	movl %eax,(%r15,%r13,2)
438b0c6f	movl (%r15,%r13,2),%ecx
438d146f	leal (%r15,%r13,2),%edx
438944af04	movl %eax,4(%r15,%r13,4)
438b4caf04	movl 4(%r15,%r13,4),%ecx
438d54af04	leal 4(%r15,%r13,4),%edx
438984ef00100000	movl %eax,0x1000(%r15,%r13,8)
438b8cef00100000	movl 0x1000(%r15,%r13,8),%ecx
438d94ef00100000	leal 0x1000(%r15,%r13,8),%edx
438984ef00000000 4:11:sym+0	movl %eax,sym(%r15,%r13,8)
438b8cef00000000 4:11:sym+0	movl sym(%r15,%r13,8),%ecx
438d94ef00000000 4:10:sym+0	leal sym(%r15,%r13,8),%edx
4389043f	movl %eax,(%r15,%r15)
438b0c3f	movl (%r15,%r15),%ecx
438d143f	leal (%r15,%r15),%edx
4389043f	movl %eax,(%r15,%r15,1)
438b0c3f	movl (%r15,%r15,1),%ecx
438d143f	leal (%r15,%r15,1),%edx
4389047f	movl %eax,(%r15,%r15,2)
438b0c7f	movl (%r15,%r15,2),%ecx
438d147f	leal (%r15,%r15,2),%edx
438944bf04	movl %eax,4(%r15,%r15,4)
438b4cbf04	movl 4(%r15,%r15,4),%ecx
438d54bf04	leal 4(%r15,%r15,4),%edx
438984ff00100000	movl %eax,0x1000(%r15,%r15,8)
438b8cff00100000	movl 0x1000(%r15,%r15,8),%ecx
438d94ff00100000	leal 0x1000(%r15,%r15,8),%edx
438984ff00000000 4:11:sym+0	movl %eax,sym(%r15,%r15,8)
438b8cff00000000 4:11:sym+0	movl sym(%r15,%r15,8),%ecx
438d94ff00000000 4:10:sym+0	leal sym(%r15,%r15,8),%edx
89048500000000	movl %eax,(,%rax,4)
8b0c8500000000	movl (,%rax,4),%ecx
8d148500000000	leal (,%rax,4),%edx
89044508000000	movl %eax,8(,%rax,2)
8b0c4508000000	movl 8(,%rax,2),%ecx
8d144508000000	leal 8(,%rax,2),%edx
8904c500000000 3:11:sym+0	movl %eax,sym(,%rax,8)
8b0cc500000000 3:11:sym+0	movl sym(,%rax,8),%ecx
8d14c500000000 3:10:sym+0	leal sym(,%rax,8),%edx
89040500000000	movl %eax,(,%rax,1)
8b0c0500000000	movl (,%rax,1),%ecx
8d140500000000	leal (,%rax,1),%edx
89040500000000	movl %eax,(,%rax)
8b0c0500000000	movl (,%rax),%ecx
8d140500000000	leal (,%rax),%edx
89049d00000000	movl %eax,(,%rbx,4)
8b0c9d00000000	movl (,%rbx,4),%ecx
8d149d00000000	leal (,%rbx,4),%edx
89045d08000000	movl %eax,8(,%rbx,2)
8b0c5d08000000	movl 8(,%rbx,2),%ecx
8d145d08000000	leal 8(,%rbx,2),%edx
8904dd00000000 3:11:sym+0	movl %eax,sym(,%rbx,8)
8b0cdd00000000 3:11:sym+0	movl sym(,%rbx,8),%ecx
8d14dd00000000 3:10:sym+0	leal sym(,%rbx,8),%edx
89041d00000000	movl %eax,(,%rbx,1)
8b0c1d00000000	movl (,%rbx,1),%ecx
8d141d00000000	leal (,%rbx,1),%edx
89041d00000000	movl %eax,(,%rbx)
8b0c1d00000000	movl (,%rbx),%ecx
8d141d00000000	leal (,%rbx),%edx
8904ad00000000	movl %eax,(,%rbp,4)
8b0cad00000000	movl (,%rbp,4),%ecx
8d14ad00000000	leal (,%rbp,4),%edx
89046d08000000	movl %eax,8(,%rbp,2)
8b0c6d08000000	movl 8(,%rbp,2),%ecx
8d146d08000000	leal 8(,%rbp,2),%edx
8904ed00000000 3:11:sym+0	movl %eax,sym(,%rbp,8)
8b0ced00000000 3:11:sym+0	movl sym(,%rbp,8),%ecx
8d14ed00000000 3:10:sym+0	leal sym(,%rbp,8),%edx
89042d00000000	movl %eax,(,%rbp,1)
8b0c2d00000000	movl (,%rbp,1),%ecx
8d142d00000000	leal (,%rbp,1),%edx
89042d00000000	movl %eax,(,%rbp)
8b0c2d00000000	movl (,%rbp),%ecx
8d142d00000000	leal (,%rbp),%edx
8904b500000000	movl %eax,(,%rsi,4)
8b0cb500000000	movl (,%rsi,4),%ecx
8d14b500000000	leal (,%rsi,4),%edx
89047508000000	movl %eax,8(,%rsi,2)
8b0c7508000000	movl 8(,%rsi,2),%ecx
8d147508000000	leal 8(,%rsi,2),%edx
8904f500000000 3:11:sym+0	movl %eax,sym(,%rsi,8)
8b0cf500000000 3:11:sym+0	movl sym(,%rsi,8),%ecx
8d14f500000000 3:10:sym+0	leal sym(,%rsi,8),%edx
89043500000000	movl %eax,(,%rsi,1)
8b0c3500000000	movl (,%rsi,1),%ecx
8d143500000000	leal (,%rsi,1),%edx
89043500000000	movl %eax,(,%rsi)
8b0c3500000000	movl (,%rsi),%ecx
8d143500000000	leal (,%rsi),%edx
4289048500000000	movl %eax,(,%r8,4)
428b0c8500000000	movl (,%r8,4),%ecx
428d148500000000	leal (,%r8,4),%edx
4289044508000000	movl %eax,8(,%r8,2)
428b0c4508000000	movl 8(,%r8,2),%ecx
428d144508000000	leal 8(,%r8,2),%edx
428904c500000000 4:11:sym+0	movl %eax,sym(,%r8,8)
428b0cc500000000 4:11:sym+0	movl sym(,%r8,8),%ecx
428d14c500000000 4:10:sym+0	leal sym(,%r8,8),%edx
4289040500000000	movl %eax,(,%r8,1)
428b0c0500000000	movl (,%r8,1),%ecx
428d140500000000	leal (,%r8,1),%edx
4289040500000000	movl %eax,(,%r8)
428b0c0500000000	movl (,%r8),%ecx
428d140500000000	leal (,%r8),%edx
428904a500000000	movl %eax,(,%r12,4)
428b0ca500000000	movl (,%r12,4),%ecx
428d14a500000000	leal (,%r12,4),%edx
4289046508000000	movl %eax,8(,%r12,2)
428b0c6508000000	movl 8(,%r12,2),%ecx
428d146508000000	leal 8(,%r12,2),%edx
428904e500000000 4:11:sym+0	movl %eax,sym(,%r12,8)
428b0ce500000000 4:11:sym+0	movl sym(,%r12,8),%ecx
428d14e500000000 4:10:sym+0	leal sym(,%r12,8),%edx
4289042500000000	movl %eax,(,%r12,1)
428b0c2500000000	movl (,%r12,1),%ecx
428d142500000000	leal (,%r12,1),%edx
4289042500000000	movl %eax,(,%r12)
428b0c2500000000	movl (,%r12),%ecx
428d142500000000	leal (,%r12),%edx
428904ad00000000	movl %eax,(,%r13,4)
428b0cad00000000	movl (,%r13,4),%ecx
428d14ad00000000	leal (,%r13,4),%edx
4289046d08000000	movl %eax,8(,%r13,2)
428b0c6d08000000	movl 8(,%r13,2),%ecx
428d146d08000000	leal 8(,%r13,2),%edx
428904ed00000000 4:11:sym+0	movl %eax,sym(,%r13,8)
428b0ced00000000 4:11:sym+0	movl sym(,%r13,8),%ecx
428d14ed00000000 4:10:sym+0	leal sym(,%r13,8),%edx
4289042d00000000	movl %eax,(,%r13,1)
428b0c2d00000000	movl (,%r13,1),%ecx
428d142d00000000	leal (,%r13,1),%edx
4289042d00000000	movl %eax,(,%r13)
428b0c2d00000000	movl (,%r13),%ecx
428d142d00000000	leal (,%r13),%edx
428904bd00000000	movl %eax,(,%r15,4)
428b0cbd00000000	movl (,%r15,4),%ecx
428d14bd00000000	leal (,%r15,4),%edx
4289047d08000000	movl %eax,8(,%r15,2)
428b0c7d08000000	movl 8(,%r15,2),%ecx
428d147d08000000	leal 8(,%r15,2),%edx
428904fd00000000 4:11:sym+0	movl %eax,sym(,%r15,8)
428b0cfd00000000 4:11:sym+0	movl sym(,%r15,8),%ecx
428d14fd00000000 4:10:sym+0	leal sym(,%r15,8),%edx
4289043d00000000	movl %eax,(,%r15,1)
428b0c3d00000000	movl (,%r15,1),%ecx
428d143d00000000	leal (,%r15,1),%edx
4289043d00000000	movl %eax,(,%r15)
428b0c3d00000000	movl (,%r15),%ecx
428d143d00000000	leal (,%r15),%edx
89042500000000 3:11:sym+0	movl %eax,sym
8b0c2500000000 3:11:sym+0	movl sym,%ecx
8d142500000000 3:10:sym+0	leal sym,%edx
89042500000000 3:11:sym+4	movl %eax,sym+4
8b0c2500000000 3:11:sym+4	movl sym+4,%ecx
8d142500000000 3:10:sym+4	leal sym+4,%edx
89042500000000 3:11:sym-4	movl %eax,sym-4
8b0c2500000000 3:11:sym-4	movl sym-4,%ecx
8d142500000000 3:10:sym-4	leal sym-4,%edx
89042534120000	movl %eax,0x1234
8b0c2534120000	movl 0x1234,%ecx
8d142534120000	leal 0x1234,%edx
89042500000000	movl %eax,0
8b0c2500000000	movl 0,%ecx
8d142500000000	leal 0,%edx
890425fcffffff	movl %eax,-4
8b0c25fcffffff	movl -4,%ecx
8d1425fcffffff	leal -4,%edx
6489042500000000	movl %eax,%fs:0
648b0c2500000000	movl %fs:0,%ecx
648d142500000000	leal %fs:0,%edx
6589042500000000	movl %eax,%gs:0
658b0c2500000000	movl %gs:0,%ecx
658d142500000000	leal %gs:0,%edx
6589042514000000	movl %eax,%gs:20
658b0c2514000000	movl %gs:20,%ecx
658d142514000000	leal %gs:20,%edx
648900	movl %eax,%fs:(%rax)
648b08	movl %fs:(%rax),%ecx
648d10	leal %fs:(%rax),%edx
65894308	movl %eax,%gs:8(%rbx)
658b4b08	movl %gs:8(%rbx),%ecx
658d5308	leal %gs:8(%rbx),%edx
2e89042500000000 4:11:sym+0	movl %eax,%cs:sym
2e8b0c2500000000 4:11:sym+0	movl %cs:sym,%ecx
2e8d142500000000 4:10:sym+0	leal %cs:sym,%edx
89042500000000 3:11:sym+0	movl %eax,%ds:sym
8b0c2500000000 3:11:sym+0	movl %ds:sym,%ecx
8d142500000000 3:10:sym+0	leal %ds:sym,%edx
894500	movl %eax,%ss:(%rbp)
8b4d00	movl %ss:(%rbp),%ecx
8d5500	leal %ss:(%rbp),%edx
6489042500000000 4:23:sym+0	movl %eax,%fs:sym@TPOFF
648b0c2500000000 4:23:sym+0	movl %fs:sym@TPOFF,%ecx
refused	leal %fs:sym@TPOFF,%edx
890500000000 2:2:sym-4	movl %eax,sym(%rip)
8b0d00000000 2:2:sym-4	movl sym(%rip),%ecx
8d1500000000 2:2:sym-4	leal sym(%rip),%edx
890500000000 2:2:sym+0	movl %eax,sym+4(%rip)
8b0d00000000 2:2:sym+0	movl sym+4(%rip),%ecx
8d1500000000 2:2:sym+0	leal sym+4(%rip),%edx
890500000000 2:2:sym-12	movl %eax,sym-8(%rip)
8b0d00000000 2:2:sym-12	movl sym-8(%rip),%ecx
8d1500000000 2:2:sym-12	leal sym-8(%rip),%edx
890500000000 2:9:sym-4	movl %eax,sym@GOTPCREL(%rip)
8b0d00000000 2:41:sym-4	movl sym@GOTPCREL(%rip),%ecx
8d1500000000 2:9:sym-4	leal sym@GOTPCREL(%rip),%edx
890500000000 2:4:sym-4	movl %eax,sym@PLT(%rip)
8b0d00000000 2:4:sym-4	movl sym@PLT(%rip),%ecx
8d1500000000 2:4:sym-4	leal sym@PLT(%rip),%edx
refused	movl %eax,sym@GOTTPOFF(%rip)
refused	movl sym@GOTTPOFF(%rip),%ecx
refused	leal sym@GOTTPOFF(%rip),%edx
refused	movl %eax,sym@TLSGD(%rip)
refused	movl sym@TLSGD(%rip),%ecx
refused	leal sym@TLSGD(%rip),%edx
refused	movl %eax,sym@TLSLD(%rip)
refused	movl sym@TLSLD(%rip),%ecx
refused	leal sym@TLSLD(%rip),%edx
890500000000	movl %eax,0(%rip)
8b0d00000000	movl 0(%rip),%ecx
8d1500000000	leal 0(%rip),%edx
890508000000	movl %eax,8(%rip)
8b0d08000000	movl 8(%rip),%ecx
8d1508000000	leal 8(%rip),%edx
890500000000	movl %eax,(%rip)
8b0d00000000	movl (%rip),%ecx
8d1500000000	leal (%rip),%edx
678900	movl %eax,(%eax)
678b08	movl (%eax),%ecx
678d10	leal (%eax),%edx
6789448b08	movl %eax,8(%ebx,%ecx,4)
678b4c8b08	movl 8(%ebx,%ecx,4),%ecx
678d548b08	leal 8(%ebx,%ecx,4),%edx
67418900	movl %eax,(%r8d)
67418b08	movl (%r8d),%ecx
67418d10	leal (%r8d),%edx
67890500000000 3:2:sym-4	movl %eax,sym(%eip)
678b0d00000000 3:2:sym-4	movl sym(%eip),%ecx
678d1500000000 3:2:sym-4	leal sym(%eip),%edx
67890424	movl %eax,(%esp)
678b0c24	movl (%esp),%ecx
678d1424	leal (%esp),%edx
674389046c	movl %eax,(%r12d,%r13d,2)
67438b0c6c	movl (%r12d,%r13d,2),%ecx
67438d146c	leal (%r12d,%r13d,2),%edx
898000000000 2:3:sym+0	movl %eax,sym@GOT(%rax)
8b8800000000 2:3:sym+0	movl sym@GOT(%rax),%ecx
refused	leal sym@GOT(%rax),%edx
898000000000 2:23:sym+0	movl %eax,sym@TPOFF(%rax)
8b8800000000 2:23:sym+0	movl sym@TPOFF(%rax),%ecx
refused	leal sym@TPOFF(%rax),%edx
898000000000 2:21:sym+0	movl %eax,sym@DTPOFF(%rax)
8b8800000000 2:21:sym+0	movl sym@DTPOFF(%rax),%ecx
refused	leal sym@DTPOFF(%rax),%edx
890500000000 2:9:sym+0	movl %eax,sym@GOTPCREL+4(%rip)
8b0d00000000 2:41:sym+0	movl sym@GOTPCREL+4(%rip),%ecx
8d1500000000 2:9:sym+0	leal sym@GOTPCREL+4(%rip),%edx
refused	movl %eax,(%esp,%esp)
refused	movl (%esp,%esp),%ecx
refused	leal (%esp,%esp),%edx
refused	movl %eax,(%eax,%esp)
refused	movl (%eax,%esp),%ecx
refused	leal (%eax,%esp),%edx
refused	movl %eax,(%eax,%ebx,3)
refused	movl (%eax,%ebx,3),%ecx
refused	leal (%eax,%ebx,3),%edx
refused	movl %eax,(%eax,%ebx,16)
refused	movl (%eax,%ebx,16),%ecx
refused	leal (%eax,%ebx,16),%edx
refused	movl %eax,(%ax,%bx)
refused	movl (%ax,%bx),%ecx
refused	leal (%ax,%bx),%edx
refused	movl %eax,(%eax,%bx)
refused	movl (%eax,%bx),%ecx
refused	leal (%eax,%bx),%edx
refused	movl %eax,(%al)
refused	movl (%al),%ecx
refused	leal (%al),%edx
refused	movl %eax,(%eax,%eax,0)
refused	movl (%eax,%eax,0),%ecx
refused	leal (%eax,%eax,0),%edx
refused	movl %eax,(%bx,%bp)
refused	movl (%bx,%bp),%ecx
refused	leal (%bx,%bp),%edx
refused	movl %eax,(%si,%di)
refused	movl (%si,%di),%ecx
refused	leal (%si,%di),%edx
refused	movl %eax,(%cx)
refused	movl (%cx),%ecx
refused	leal (%cx),%edx
refused	movl %eax,(%eax,%ebx,4,5)
refused	movl (%eax,%ebx,4,5),%ecx
refused	leal (%eax,%ebx,4,5),%edx
refused	movl %eax,(%cr0)
refused	movl (%cr0),%ecx
refused	leal (%cr0),%edx
refused	movl %eax,(%eax,%xmm0)
refused	movl (%eax,%xmm0),%ecx
refused	leal (%eax,%xmm0),%edx
refused	movl %eax,(%rsp,%rsp)
refused	movl (%rsp,%rsp),%ecx
refused	leal (%rsp,%rsp),%edx
refused	movl %eax,(%rax,%rsp,2)
refused	movl (%rax,%rsp,2),%ecx
refused	leal (%rax,%rsp,2),%edx
refused	movl %eax,(%rax,%ebx)
refused	movl (%rax,%ebx),%ecx
refused	leal (%rax,%ebx),%edx
refused	movl %eax,(%eax,%rbx)
refused	movl (%eax,%rbx),%ecx
refused	leal (%eax,%rbx),%edx
refused	movl %eax,(%rip,%rax)
refused	movl (%rip,%rax),%ecx
refused	leal (%rip,%rax),%edx
refused	movl %eax,(%rax,%rip)
refused	movl (%rax,%rip),%ecx
refused	leal (%rax,%rip),%edx
refused	movl %eax,sym(%rip,%rax,2)
refused	movl sym(%rip,%rax,2),%ecx
refused	leal sym(%rip,%rax,2),%edx
refused	movl %eax,(%bx)
refused	movl (%bx),%ecx
refused	leal (%bx),%edx
refused	movl %eax,(%bx,%si)
refused	movl (%bx,%si),%ecx
refused	leal (%bx,%si),%edx
refused	movl %eax,0x123456789(%rax)
refused	movl 0x123456789(%rax),%ecx
8d9089674523	leal 0x123456789(%rax),%edx
refused	movl %eax,0x80000000(%rax)
refused	movl 0x80000000(%rax),%ecx
8d9000000080	leal 0x80000000(%rax),%edx
refused	movl %eax,-0x80000001(%rax)
refused	movl -0x80000001(%rax),%ecx
8d90ffffff7f	leal -0x80000001(%rax),%edx
refused	movl %eax,0xffffffff(%rax)
refused	movl 0xffffffff(%rax),%ecx
8d50ff	leal 0xffffffff(%rax),%edx
d5108900	movl %eax,(%r16)
d5108b08	movl (%r16),%ecx
d5108d10	leal (%r16),%edx
refused	movl %eax,(%rax,%rbx,3)
refused	movl (%rax,%rbx,3),%ecx
refused	leal (%rax,%rbx,3),%edx
c60001	movb $1,(%rax)
66c7003412	movw $0x1234,(%rax)
c70078563412	movl $0x12345678,(%rax)
830004	addl $4,(%rax)
803800	cmpb $0,(%rax)
ff00	incl (%rax)
ff30	pushq (%rax)
ff10	call *(%rax)
c70000000000 2:10:sym2+0	movl $sym2,(%rax)
f70001000000	testl $1,(%rax)
c12003	shll $3,(%rax)
6b0064	imull $100,(%rax),%eax
dd00	fldl (%rax)
8800	movb %al,(%rax)
668b18	movw (%rax),%bx
488900	movq %rax,(%rax)
4c8b08	movq (%rax),%r9
48c70001000000	movq $1,(%rax)
4c8d10	leaq (%rax),%r10
48813800010000	cmpq $0x100,(%rax)
48c70000000000 3:11:sym2+0	movq $sym2,(%rax)
488300ff	addq $-1,(%rax)
c6804523010001	movb $1,0x12345(%rax)
66c780452301003412	movw $0x1234,0x12345(%rax)
c7804523010078563412	movl $0x12345678,0x12345(%rax)
83804523010004	addl $4,0x12345(%rax)
80b84523010000	cmpb $0,0x12345(%rax)
ff8045230100	incl 0x12345(%rax)
ffb045230100	pushq 0x12345(%rax)
ff9045230100	call *0x12345(%rax)
c7804523010000000000 6:10:sym2+0	movl $sym2,0x12345(%rax)
f7804523010001000000	testl $1,0x12345(%rax)
c1a04523010003	shll $3,0x12345(%rax)
6b804523010064	imull $100,0x12345(%rax),%eax
dd8045230100	fldl 0x12345(%rax)
888045230100	movb %al,0x12345(%rax)
668b9845230100	movw 0x12345(%rax),%bx
48898045230100	movq %rax,0x12345(%rax)
4c8b8845230100	movq 0x12345(%rax),%r9
48c7804523010001000000	movq $1,0x12345(%rax)
4c8d9045230100	leaq 0x12345(%rax),%r10
4881b84523010000010000	cmpq $0x100,0x12345(%rax)
48c7804523010000000000 7:11:sym2+0	movq $sym2,0x12345(%rax)
48838045230100ff	addq $-1,0x12345(%rax)
c6437f01	movb $1,127(%rbx)
66c7437f3412	movw $0x1234,127(%rbx)
c7437f78563412	movl $0x12345678,127(%rbx)
83437f04	addl $4,127(%rbx)
807b7f00	cmpb $0,127(%rbx)
ff437f	incl 127(%rbx)
ff737f	pushq 127(%rbx)
ff537f	call *127(%rbx)
c7437f00000000 3:10:sym2+0	movl $sym2,127(%rbx)
f7437f01000000	testl $1,127(%rbx)
c1637f03	shll $3,127(%rbx)
6b437f64	imull $100,127(%rbx),%eax
dd437f	fldl 127(%rbx)
88437f	movb %al,127(%rbx)
668b5b7f	movw 127(%rbx),%bx
4889437f	movq %rax,127(%rbx)
4c8b4b7f	movq 127(%rbx),%r9
48c7437f01000000	movq $1,127(%rbx)
4c8d537f	leaq 127(%rbx),%r10
48817b7f00010000	cmpq $0x100,127(%rbx)
48c7437f00000000 4:11:sym2+0	movq $sym2,127(%rbx)
4883437fff	addq $-1,127(%rbx)
c6830000000001 2:11:sym+4	movb $1,sym+4(%rbx)
66c783000000003412 3:11:sym+4	movw $0x1234,sym+4(%rbx)
c7830000000078563412 2:11:sym+4	movl $0x12345678,sym+4(%rbx)
83830000000004 2:11:sym+4	addl $4,sym+4(%rbx)
80bb0000000000 2:11:sym+4	cmpb $0,sym+4(%rbx)
ff8300000000 2:11:sym+4	incl sym+4(%rbx)
ffb300000000 2:11:sym+4	pushq sym+4(%rbx)
ff9300000000 2:11:sym+4	call *sym+4(%rbx)
c7830000000000000000 2:11:sym+4 6:10:sym2+0	movl $sym2,sym+4(%rbx)
f7830000000001000000 2:11:sym+4	testl $1,sym+4(%rbx)
c1a30000000003 2:11:sym+4	shll $3,sym+4(%rbx)
6b830000000064 2:11:sym+4	imull $100,sym+4(%rbx),%eax
dd8300000000 2:11:sym+4	fldl sym+4(%rbx)
888300000000 2:11:sym+4	movb %al,sym+4(%rbx)
668b9b00000000 3:11:sym+4	movw sym+4(%rbx),%bx
48898300000000 3:11:sym+4	movq %rax,sym+4(%rbx)
4c8b8b00000000 3:11:sym+4	movq sym+4(%rbx),%r9
48c7830000000001000000 3:11:sym+4	movq $1,sym+4(%rbx)
4c8d9300000000 3:11:sym+4	leaq sym+4(%rbx),%r10
4881bb0000000000010000 3:11:sym+4	cmpq $0x100,sym+4(%rbx)
48c7830000000000000000 3:11:sym+4 7:11:sym2+0	movq $sym2,sym+4(%rbx)
48838300000000ff 3:11:sym+4	addq $-1,sym+4(%rbx)
c684247fffffff01	movb $1,-129(%rsp)
66c784247fffffff3412	movw $0x1234,-129(%rsp)
c784247fffffff78563412	movl $0x12345678,-129(%rsp)
8384247fffffff04	addl $4,-129(%rsp)
80bc247fffffff00	cmpb $0,-129(%rsp)
ff84247fffffff	incl -129(%rsp)
ffb4247fffffff	pushq -129(%rsp)
ff94247fffffff	call *-129(%rsp)
c784247fffffff00000000 7:10:sym2+0	movl $sym2,-129(%rsp)
f784247fffffff01000000	testl $1,-129(%rsp)
c1a4247fffffff03	shll $3,-129(%rsp)
6b84247fffffff64	imull $100,-129(%rsp),%eax
dd84247fffffff	fldl -129(%rsp)
8884247fffffff	movb %al,-129(%rsp)
668b9c247fffffff	movw -129(%rsp),%bx
488984247fffffff	movq %rax,-129(%rsp)
4c8b8c247fffffff	movq -129(%rsp),%r9
48c784247fffffff01000000	movq $1,-129(%rsp)
4c8d94247fffffff	leaq -129(%rsp),%r10
4881bc247fffffff00010000	cmpq $0x100,-129(%rsp)
48c784247fffffff00000000 8:11:sym2+0	movq $sym2,-129(%rsp)
488384247fffffffff	addq $-1,-129(%rsp)
c645f801	movb $1,-8(%rbp)
66c745f83412	movw $0x1234,-8(%rbp)
c745f878563412	movl $0x12345678,-8(%rbp)
8345f804	addl $4,-8(%rbp)
807df800	cmpb $0,-8(%rbp)
ff45f8	incl -8(%rbp)
ff75f8	pushq -8(%rbp)
ff55f8	call *-8(%rbp)
c745f800000000 3:10:sym2+0	movl $sym2,-8(%rbp)
f745f801000000	testl $1,-8(%rbp)
c165f803	shll $3,-8(%rbp)
6b45f864	imull $100,-8(%rbp),%eax
dd45f8	fldl -8(%rbp)
8845f8	movb %al,-8(%rbp)
668b5df8	movw -8(%rbp),%bx
488945f8	movq %rax,-8(%rbp)
4c8b4df8	movq -8(%rbp),%r9
48c745f801000000	movq $1,-8(%rbp)
4c8d55f8	leaq -8(%rbp),%r10
48817df800010000	cmpq $0x100,-8(%rbp)
48c745f800000000 4:11:sym2+0	movq $sym2,-8(%rbp)
488345f8ff	addq $-1,-8(%rbp)
c6850000000001 2:11:sym+0	movb $1,sym(%rbp)
66c785000000003412 3:11:sym+0	movw $0x1234,sym(%rbp)
c7850000000078563412 2:11:sym+0	movl $0x12345678,sym(%rbp)
83850000000004 2:11:sym+0	addl $4,sym(%rbp)
80bd0000000000 2:11:sym+0	cmpb $0,sym(%rbp)
ff8500000000 2:11:sym+0	incl sym(%rbp)
ffb500000000 2:11:sym+0	pushq sym(%rbp)
ff9500000000 2:11:sym+0	call *sym(%rbp)
c7850000000000000000 2:11:sym+0 6:10:sym2+0	movl $sym2,sym(%rbp)
f7850000000001000000 2:11:sym+0	testl $1,sym(%rbp)
c1a50000000003 2:11:sym+0	shll $3,sym(%rbp)
6b850000000064 2:11:sym+0	imull $100,sym(%rbp),%eax
dd8500000000 2:11:sym+0	fldl sym(%rbp)
888500000000 2:11:sym+0	movb %al,sym(%rbp)
668b9d00000000 3:11:sym+0	movw sym(%rbp),%bx
48898500000000 3:11:sym+0	movq %rax,sym(%rbp)
4c8b8d00000000 3:11:sym+0	movq sym(%rbp),%r9
48c7850000000001000000 3:11:sym+0	movq $1,sym(%rbp)
4c8d9500000000 3:11:sym+0	leaq sym(%rbp),%r10
4881bd0000000000010000 3:11:sym+0	cmpq $0x100,sym(%rbp)
48c7850000000000000000 3:11:sym+0 7:11:sym2+0	movq $sym2,sym(%rbp)
48838500000000ff 3:11:sym+0	addq $-1,sym(%rbp)
c6468001	movb $1,-128(%rsi)
66c746803412	movw $0x1234,-128(%rsi)
c7468078563412	movl $0x12345678,-128(%rsi)
83468004	addl $4,-128(%rsi)
807e8000	cmpb $0,-128(%rsi)
ff4680	incl -128(%rsi)
ff7680	pushq -128(%rsi)
ff5680	call *-128(%rsi)
c7468000000000 3:10:sym2+0	movl $sym2,-128(%rsi)
f7468001000000	testl $1,-128(%rsi)
c1668003	shll $3,-128(%rsi)
6b468064	imull $100,-128(%rsi),%eax
dd4680	fldl -128(%rsi)
884680	movb %al,-128(%rsi)
668b5e80	movw -128(%rsi),%bx
48894680	movq %rax,-128(%rsi)
4c8b4e80	movq -128(%rsi),%r9
48c7468001000000	movq $1,-128(%rsi)
4c8d5680	leaq -128(%rsi),%r10
48817e8000010000	cmpq $0x100,-128(%rsi)
48c7468000000000 4:11:sym2+0	movq $sym2,-128(%rsi)
48834680ff	addq $-1,-128(%rsi)
41c6400801	movb $1,8(%r8)
6641c740083412	movw $0x1234,8(%r8)
41c7400878563412	movl $0x12345678,8(%r8)
4183400804	addl $4,8(%r8)
4180780800	cmpb $0,8(%r8)
41ff4008	incl 8(%r8)
41ff7008	pushq 8(%r8)
41ff5008	call *8(%r8)
41c7400800000000 4:10:sym2+0	movl $sym2,8(%r8)
41f7400801000000	testl $1,8(%r8)
41c1600803	shll $3,8(%r8)
416b400864	imull $100,8(%r8),%eax
41dd4008	fldl 8(%r8)
41884008	movb %al,8(%r8)
66418b5808	movw 8(%r8),%bx
49894008	movq %rax,8(%r8)
4d8b4808	movq 8(%r8),%r9
49c7400801000000	movq $1,8(%r8)
4d8d5008	leaq 8(%r8),%r10
4981780800010000	cmpq $0x100,8(%r8)
49c7400800000000 4:11:sym2+0	movq $sym2,8(%r8)
49834008ff	addq $-1,8(%r8)
41c60001	movb $1,0(%r8)
6641c7003412	movw $0x1234,0(%r8)
41c70078563412	movl $0x12345678,0(%r8)
41830004	addl $4,0(%r8)
41803800	cmpb $0,0(%r8)
41ff00	incl 0(%r8)
41ff30	pushq 0(%r8)
41ff10	call *0(%r8)
41c70000000000 3:10:sym2+0	movl $sym2,0(%r8)
41f70001000000	testl $1,0(%r8)
41c12003	shll $3,0(%r8)
416b0064	imull $100,0(%r8),%eax
41dd00	fldl 0(%r8)
418800	movb %al,0(%r8)
66418b18	movw 0(%r8),%bx
498900	movq %rax,0(%r8)
4d8b08	movq 0(%r8),%r9
49c70001000000	movq $1,0(%r8)
4d8d10	leaq 0(%r8),%r10
49813800010000	cmpq $0x100,0(%r8)
49c70000000000 3:11:sym2+0	movq $sym2,0(%r8)
498300ff	addq $-1,0(%r8)
41c684248000000001	movb $1,128(%r12)
6641c78424800000003412	movw $0x1234,128(%r12)
41c784248000000078563412	movl $0x12345678,128(%r12)
418384248000000004	addl $4,128(%r12)
4180bc248000000000	cmpb $0,128(%r12)
41ff842480000000	incl 128(%r12)
41ffb42480000000	pushq 128(%r12)
41ff942480000000	call *128(%r12)
41c784248000000000000000 8:10:sym2+0	movl $sym2,128(%r12)
41f784248000000001000000	testl $1,128(%r12)
41c1a4248000000003	shll $3,128(%r12)
416b84248000000064	imull $100,128(%r12),%eax
41dd842480000000	fldl 128(%r12)
4188842480000000	movb %al,128(%r12)
66418b9c2480000000	movw 128(%r12),%bx
4989842480000000	movq %rax,128(%r12)
4d8b8c2480000000	movq 128(%r12),%r9
49c784248000000001000000	movq $1,128(%r12)
4d8d942480000000	leaq 128(%r12),%r10
4981bc248000000000010000	cmpq $0x100,128(%r12)
49c784248000000000000000 8:11:sym2+0	movq $sym2,128(%r12)
4983842480000000ff	addq $-1,128(%r12)
41c6450001	movb $1,(%r13)
6641c745003412	movw $0x1234,(%r13)
41c7450078563412	movl $0x12345678,(%r13)
4183450004	addl $4,(%r13)
41807d0000	cmpb $0,(%r13)
41ff4500	incl (%r13)
41ff7500	pushq (%r13)
41ff5500	call *(%r13)
41c7450000000000 4:10:sym2+0	movl $sym2,(%r13)
41f7450001000000	testl $1,(%r13)
41c1650003	shll $3,(%r13)
416b450064	imull $100,(%r13),%eax
41dd4500	fldl (%r13)
41884500	movb %al,(%r13)
66418b5d00	movw (%r13),%bx
49894500	movq %rax,(%r13)
4d8b4d00	movq (%r13),%r9
49c7450001000000	movq $1,(%r13)
4d8d5500	leaq (%r13),%r10
49817d0000010000	cmpq $0x100,(%r13)
49c7450000000000 4:11:sym2+0	movq $sym2,(%r13)
49834500ff	addq $-1,(%r13)
41c6854523010001	movb $1,0x12345(%r13)
6641c785452301003412	movw $0x1234,0x12345(%r13)
41c7854523010078563412	movl $0x12345678,0x12345(%r13)
4183854523010004	addl $4,0x12345(%r13)
4180bd4523010000	cmpb $0,0x12345(%r13)
41ff8545230100	incl 0x12345(%r13)
41ffb545230100	pushq 0x12345(%r13)
41ff9545230100	call *0x12345(%r13)
41c7854523010000000000 7:10:sym2+0	movl $sym2,0x12345(%r13)
41f7854523010001000000	testl $1,0x12345(%r13)
41c1a54523010003	shll $3,0x12345(%r13)
416b854523010064	imull $100,0x12345(%r13),%eax
41dd8545230100	fldl 0x12345(%r13)
41888545230100	movb %al,0x12345(%r13)
66418b9d45230100	movw 0x12345(%r13),%bx
49898545230100	movq %rax,0x12345(%r13)
4d8b8d45230100	movq 0x12345(%r13),%r9
49c7854523010001000000	movq $1,0x12345(%r13)
4d8d9545230100	leaq 0x12345(%r13),%r10
4981bd4523010000010000	cmpq $0x100,0x12345(%r13)
49c7854523010000000000 7:11:sym2+0	movq $sym2,0x12345(%r13)
49838545230100ff	addq $-1,0x12345(%r13)
41c6477f01	movb $1,127(%r15)
6641c7477f3412	movw $0x1234,127(%r15)
41c7477f78563412	movl $0x12345678,127(%r15)
4183477f04	addl $4,127(%r15)
41807f7f00	cmpb $0,127(%r15)
41ff477f	incl 127(%r15)
41ff777f	pushq 127(%r15)
41ff577f	call *127(%r15)
41c7477f00000000 4:10:sym2+0	movl $sym2,127(%r15)
41f7477f01000000	testl $1,127(%r15)
41c1677f03	shll $3,127(%r15)
416b477f64	imull $100,127(%r15),%eax
41dd477f	fldl 127(%r15)
4188477f	movb %al,127(%r15)
66418b5f7f	movw 127(%r15),%bx
4989477f	movq %rax,127(%r15)
4d8b4f7f	movq 127(%r15),%r9
49c7477f01000000	movq $1,127(%r15)
4d8d577f	leaq 127(%r15),%r10
49817f7f00010000	cmpq $0x100,127(%r15)
49c7477f00000000 4:11:sym2+0	movq $sym2,127(%r15)
4983477fff	addq $-1,127(%r15)
41c6870000000001 3:11:sym+4	movb $1,sym+4(%r15)
6641c787000000003412 4:11:sym+4	movw $0x1234,sym+4(%r15)
41c7870000000078563412 3:11:sym+4	movl $0x12345678,sym+4(%r15)
4183870000000004 3:11:sym+4	addl $4,sym+4(%r15)
4180bf0000000000 3:11:sym+4	cmpb $0,sym+4(%r15)
41ff8700000000 3:11:sym+4	incl sym+4(%r15)
41ffb700000000 3:11:sym+4	pushq sym+4(%r15)
41ff9700000000 3:11:sym+4	call *sym+4(%r15)
41c7870000000000000000 3:11:sym+4 7:10:sym2+0	movl $sym2,sym+4(%r15)
41f7870000000001000000 3:11:sym+4	testl $1,sym+4(%r15)
41c1a70000000003 3:11:sym+4	shll $3,sym+4(%r15)
416b870000000064 3:11:sym+4	imull $100,sym+4(%r15),%eax
41dd8700000000 3:11:sym+4	fldl sym+4(%r15)
41888700000000 3:11:sym+4	movb %al,sym+4(%r15)
66418b9f00000000 4:11:sym+4	movw sym+4(%r15),%bx
49898700000000 3:11:sym+4	movq %rax,sym+4(%r15)
4d8b8f00000000 3:11:sym+4	movq sym+4(%r15),%r9
49c7870000000001000000 3:11:sym+4	movq $1,sym+4(%r15)
4d8d9700000000 3:11:sym+4	leaq sym+4(%r15),%r10
4981bf0000000000010000 3:11:sym+4	cmpq $0x100,sym+4(%r15)
49c7870000000000000000 3:11:sym+4 7:11:sym2+0	movq $sym2,sym+4(%r15)
49838700000000ff 3:11:sym+4	addq $-1,sym+4(%r15)
c6041801	movb $1,(%rax,%rbx)
66c704183412	movw $0x1234,(%rax,%rbx)
c7041878563412	movl $0x12345678,(%rax,%rbx)
83041804	addl $4,(%rax,%rbx)
803c1800	cmpb $0,(%rax,%rbx)
ff0418	incl (%rax,%rbx)
ff3418	pushq (%rax,%rbx)
ff1418	call *(%rax,%rbx)
c7041800000000 3:10:sym2+0	movl $sym2,(%rax,%rbx)
f7041801000000	testl $1,(%rax,%rbx)
c1241803	shll $3,(%rax,%rbx)
6b041864	imull $100,(%rax,%rbx),%eax
dd0418	fldl (%rax,%rbx)
880418	movb %al,(%rax,%rbx)
668b1c18	movw (%rax,%rbx),%bx
48890418	movq %rax,(%rax,%rbx)
4c8b0c18	movq (%rax,%rbx),%r9
48c7041801000000	movq $1,(%rax,%rbx)
4c8d1418	leaq (%rax,%rbx),%r10
48813c1800010000	cmpq $0x100,(%rax,%rbx)
48c7041800000000 4:11:sym2+0	movq $sym2,(%rax,%rbx)
48830418ff	addq $-1,(%rax,%rbx)
c6042801	movb $1,(%rax,%rbp,1)
66c704283412	movw $0x1234,(%rax,%rbp,1)
c7042878563412	movl $0x12345678,(%rax,%rbp,1)
83042804	addl $4,(%rax,%rbp,1)
803c2800	cmpb $0,(%rax,%rbp,1)
ff0428	incl (%rax,%rbp,1)
ff3428	pushq (%rax,%rbp,1)
ff1428	call *(%rax,%rbp,1)
c7042800000000 3:10:sym2+0	movl $sym2,(%rax,%rbp,1)
f7042801000000	testl $1,(%rax,%rbp,1)
c1242803	shll $3,(%rax,%rbp,1)
6b042864	imull $100,(%rax,%rbp,1),%eax
dd0428	fldl (%rax,%rbp,1)
880428	movb %al,(%rax,%rbp,1)
668b1c28	movw (%rax,%rbp,1),%bx
48890428	movq %rax,(%rax,%rbp,1)
4c8b0c28	movq (%rax,%rbp,1),%r9
48c7042801000000	movq $1,(%rax,%rbp,1)
4c8d1428	leaq (%rax,%rbp,1),%r10
48813c2800010000	cmpq $0x100,(%rax,%rbp,1)
48c7042800000000 4:11:sym2+0	movq $sym2,(%rax,%rbp,1)
48830428ff	addq $-1,(%rax,%rbp,1)
c6047001	movb $1,(%rax,%rsi,2)
66c704703412	movw $0x1234,(%rax,%rsi,2)
c7047078563412	movl $0x12345678,(%rax,%rsi,2)
83047004	addl $4,(%rax,%rsi,2)
803c7000	cmpb $0,(%rax,%rsi,2)
ff0470	incl (%rax,%rsi,2)
ff3470	pushq (%rax,%rsi,2)
ff1470	call *(%rax,%rsi,2)
c7047000000000 3:10:sym2+0	movl $sym2,(%rax,%rsi,2)
f7047001000000	testl $1,(%rax,%rsi,2)
c1247003	shll $3,(%rax,%rsi,2)
6b047064	imull $100,(%rax,%rsi,2),%eax
dd0470	fldl (%rax,%rsi,2)
880470	movb %al,(%rax,%rsi,2)
668b1c70	movw (%rax,%rsi,2),%bx
48890470	movq %rax,(%rax,%rsi,2)
4c8b0c70	movq (%rax,%rsi,2),%r9
48c7047001000000	movq $1,(%rax,%rsi,2)
4c8d1470	leaq (%rax,%rsi,2),%r10
48813c7000010000	cmpq $0x100,(%rax,%rsi,2)
48c7047000000000 4:11:sym2+0	movq $sym2,(%rax,%rsi,2)
48830470ff	addq $-1,(%rax,%rsi,2)
42c644a00401	movb $1,4(%rax,%r12,4)
6642c744a0043412	movw $0x1234,4(%rax,%r12,4)
42c744a00478563412	movl $0x12345678,4(%rax,%r12,4)
428344a00404	addl $4,4(%rax,%r12,4)
42807ca00400	cmpb $0,4(%rax,%r12,4)
42ff44a004	incl 4(%rax,%r12,4)
42ff74a004	pushq 4(%rax,%r12,4)
42ff54a004	call *4(%rax,%r12,4)
42c744a00400000000 5:10:sym2+0	movl $sym2,4(%rax,%r12,4)
42f744a00401000000	testl $1,4(%rax,%r12,4)
42c164a00403	shll $3,4(%rax,%r12,4)
426b44a00464	imull $100,4(%rax,%r12,4),%eax
42dd44a004	fldl 4(%rax,%r12,4)
428844a004	movb %al,4(%rax,%r12,4)
66428b5ca004	movw 4(%rax,%r12,4),%bx
4a8944a004	movq %rax,4(%rax,%r12,4)
4e8b4ca004	movq 4(%rax,%r12,4),%r9
4ac744a00401000000	movq $1,4(%rax,%r12,4)
4e8d54a004	leaq 4(%rax,%r12,4),%r10
4a817ca00400010000	cmpq $0x100,4(%rax,%r12,4)
4ac744a00400000000 5:11:sym2+0	movq $sym2,4(%rax,%r12,4)
4a8344a004ff	addq $-1,4(%rax,%r12,4)
42c684e80010000001	movb $1,0x1000(%rax,%r13,8)
6642c784e8001000003412	movw $0x1234,0x1000(%rax,%r13,8)
42c784e80010000078563412	movl $0x12345678,0x1000(%rax,%r13,8)
428384e80010000004	addl $4,0x1000(%rax,%r13,8)
4280bce80010000000	cmpb $0,0x1000(%rax,%r13,8)
42ff84e800100000	incl 0x1000(%rax,%r13,8)
42ffb4e800100000	pushq 0x1000(%rax,%r13,8)
42ff94e800100000	call *0x1000(%rax,%r13,8)
42c784e80010000000000000 8:10:sym2+0	movl $sym2,0x1000(%rax,%r13,8)
42f784e80010000001000000	testl $1,0x1000(%rax,%r13,8)
42c1a4e80010000003	shll $3,0x1000(%rax,%r13,8)
426b84e80010000064	imull $100,0x1000(%rax,%r13,8),%eax
42dd84e800100000	fldl 0x1000(%rax,%r13,8)
428884e800100000	movb %al,0x1000(%rax,%r13,8)
66428b9ce800100000	movw 0x1000(%rax,%r13,8),%bx
4a8984e800100000	movq %rax,0x1000(%rax,%r13,8)
4e8b8ce800100000	movq 0x1000(%rax,%r13,8),%r9
4ac784e80010000001000000	movq $1,0x1000(%rax,%r13,8)
4e8d94e800100000	leaq 0x1000(%rax,%r13,8),%r10
4a81bce80010000000010000	cmpq $0x100,0x1000(%rax,%r13,8)
4ac784e80010000000000000 8:11:sym2+0	movq $sym2,0x1000(%rax,%r13,8)
4a8384e800100000ff	addq $-1,0x1000(%rax,%r13,8)
42c684f80000000001 4:11:sym+0	movb $1,sym(%rax,%r15,8)
6642c784f8000000003412 5:11:sym+0	movw $0x1234,sym(%rax,%r15,8)
42c784f80000000078563412 4:11:sym+0	movl $0x12345678,sym(%rax,%r15,8)
428384f80000000004 4:11:sym+0	addl $4,sym(%rax,%r15,8)
4280bcf80000000000 4:11:sym+0	cmpb $0,sym(%rax,%r15,8)
42ff84f800000000 4:11:sym+0	incl sym(%rax,%r15,8)
42ffb4f800000000 4:11:sym+0	pushq sym(%rax,%r15,8)
42ff94f800000000 4:11:sym+0	call *sym(%rax,%r15,8)
42c784f80000000000000000 4:11:sym+0 8:10:sym2+0	movl $sym2,sym(%rax,%r15,8)
42f784f80000000001000000 4:11:sym+0	testl $1,sym(%rax,%r15,8)
42c1a4f80000000003 4:11:sym+0	shll $3,sym(%rax,%r15,8)
426b84f80000000064 4:11:sym+0	imull $100,sym(%rax,%r15,8),%eax
42dd84f800000000 4:11:sym+0	fldl sym(%rax,%r15,8)
428884f800000000 4:11:sym+0	movb %al,sym(%rax,%r15,8)
66428b9cf800000000 5:11:sym+0	movw sym(%rax,%r15,8),%bx
4a8984f800000000 4:11:sym+0	movq %rax,sym(%rax,%r15,8)
4e8b8cf800000000 4:11:sym+0	movq sym(%rax,%r15,8),%r9
4ac784f80000000001000000 4:11:sym+0	movq $1,sym(%rax,%r15,8)
4e8d94f800000000 4:11:sym+0	leaq sym(%rax,%r15,8),%r10
4a81bcf80000000000010000 4:11:sym+0	cmpq $0x100,sym(%rax,%r15,8)
4ac784f80000000000000000 4:11:sym+0 8:11:sym2+0	movq $sym2,sym(%rax,%r15,8)
4a8384f800000000ff 4:11:sym+0	addq $-1,sym(%rax,%r15,8)
c6041b01	movb $1,(%rbx,%rbx)
66c7041b3412	movw $0x1234,(%rbx,%rbx)
c7041b78563412	movl $0x12345678,(%rbx,%rbx)
83041b04	addl $4,(%rbx,%rbx)
803c1b00	cmpb $0,(%rbx,%rbx)
ff041b	incl (%rbx,%rbx)
ff341b	pushq (%rbx,%rbx)
ff141b	call *(%rbx,%rbx)
c7041b00000000 3:10:sym2+0	movl $sym2,(%rbx,%rbx)
f7041b01000000	testl $1,(%rbx,%rbx)
c1241b03	shll $3,(%rbx,%rbx)
6b041b64	imull $100,(%rbx,%rbx),%eax
dd041b	fldl (%rbx,%rbx)
88041b	movb %al,(%rbx,%rbx)
668b1c1b	movw (%rbx,%rbx),%bx
4889041b	movq %rax,(%rbx,%rbx)
4c8b0c1b	movq (%rbx,%rbx),%r9
48c7041b01000000	movq $1,(%rbx,%rbx)
4c8d141b	leaq (%rbx,%rbx),%r10
48813c1b00010000	cmpq $0x100,(%rbx,%rbx)
48c7041b00000000 4:11:sym2+0	movq $sym2,(%rbx,%rbx)
4883041bff	addq $-1,(%rbx,%rbx)
c6042b01	movb $1,(%rbx,%rbp,1)
66c7042b3412	movw $0x1234,(%rbx,%rbp,1)
c7042b78563412	movl $0x12345678,(%rbx,%rbp,1)
83042b04	addl $4,(%rbx,%rbp,1)
803c2b00	cmpb $0,(%rbx,%rbp,1)
ff042b	incl (%rbx,%rbp,1)
ff342b	pushq (%rbx,%rbp,1)
ff142b	call *(%rbx,%rbp,1)
c7042b00000000 3:10:sym2+0	movl $sym2,(%rbx,%rbp,1)
f7042b01000000	testl $1,(%rbx,%rbp,1)
c1242b03	shll $3,(%rbx,%rbp,1)
6b042b64	imull $100,(%rbx,%rbp,1),%eax
dd042b	fldl (%rbx,%rbp,1)
88042b	movb %al,(%rbx,%rbp,1)
668b1c2b	movw (%rbx,%rbp,1),%bx
4889042b	movq %rax,(%rbx,%rbp,1)
4c8b0c2b	movq (%rbx,%rbp,1),%r9
48c7042b01000000	movq $1,(%rbx,%rbp,1)
4c8d142b	leaq (%rbx,%rbp,1),%r10
48813c2b00010000	cmpq $0x100,(%rbx,%rbp,1)
48c7042b00000000 4:11:sym2+0	movq $sym2,(%rbx,%rbp,1)
4883042bff	addq $-1,(%rbx,%rbp,1)
c6047301	movb $1,(%rbx,%rsi,2)
66c704733412	movw $0x1234,(%rbx,%rsi,2)
c7047378563412	movl $0x12345678,(%rbx,%rsi,2)
83047304	addl $4,(%rbx,%rsi,2)
803c7300	cmpb $0,(%rbx,%rsi,2)
ff0473	incl (%rbx,%rsi,2)
ff3473	pushq (%rbx,%rsi,2)
ff1473	call *(%rbx,%rsi,2)
c7047300000000 3:10:sym2+0	movl $sym2,(%rbx,%rsi,2)
f7047301000000	testl $1,(%rbx,%rsi,2)
c1247303	shll $3,(%rbx,%rsi,2)
6b047364	imull $100,(%rbx,%rsi,2),%eax
dd0473	fldl (%rbx,%rsi,2)
880473	movb %al,(%rbx,%rsi,2)
668b1c73	movw (%rbx,%rsi,2),%bx
48890473	movq %rax,(%rbx,%rsi,2)
4c8b0c73	movq (%rbx,%rsi,2),%r9
48c7047301000000	movq $1,(%rbx,%rsi,2)
4c8d1473	leaq (%rbx,%rsi,2),%r10
48813c7300010000	cmpq $0x100,(%rbx,%rsi,2)
48c7047300000000 4:11:sym2+0	movq $sym2,(%rbx,%rsi,2)
48830473ff	addq $-1,(%rbx,%rsi,2)
42c644a30401	movb $1,4(%rbx,%r12,4)
6642c744a3043412	movw $0x1234,4(%rbx,%r12,4)
42c744a30478563412	movl $0x12345678,4(%rbx,%r12,4)
428344a30404	addl $4,4(%rbx,%r12,4)
42807ca30400	cmpb $0,4(%rbx,%r12,4)
42ff44a304	incl 4(%rbx,%r12,4)
42ff74a304	pushq 4(%rbx,%r12,4)
42ff54a304	call *4(%rbx,%r12,4)
42c744a30400000000 5:10:sym2+0	movl $sym2,4(%rbx,%r12,4)
42f744a30401000000	testl $1,4(%rbx,%r12,4)
42c164a30403	shll $3,4(%rbx,%r12,4)
426b44a30464	imull $100,4(%rbx,%r12,4),%eax
42dd44a304	fldl 4(%rbx,%r12,4)
428844a304	movb %al,4(%rbx,%r12,4)
66428b5ca304	movw 4(%rbx,%r12,4),%bx
4a8944a304	movq %rax,4(%rbx,%r12,4)
4e8b4ca304	movq 4(%rbx,%r12,4),%r9
4ac744a30401000000	movq $1,4(%rbx,%r12,4)
4e8d54a304	leaq 4(%rbx,%r12,4),%r10
4a817ca30400010000	cmpq $0x100,4(%rbx,%r12,4)
4ac744a30400000000 5:11:sym2+0	movq $sym2,4(%rbx,%r12,4)
4a8344a304ff	addq $-1,4(%rbx,%r12,4)
42c684eb0010000001	movb $1,0x1000(%rbx,%r13,8)
6642c784eb001000003412	movw $0x1234,0x1000(%rbx,%r13,8)
42c784eb0010000078563412	movl $0x12345678,0x1000(%rbx,%r13,8)
428384eb0010000004	addl $4,0x1000(%rbx,%r13,8)
4280bceb0010000000	cmpb $0,0x1000(%rbx,%r13,8)
42ff84eb00100000	incl 0x1000(%rbx,%r13,8)
42ffb4eb00100000	pushq 0x1000(%rbx,%r13,8)
42ff94eb00100000	call *0x1000(%rbx,%r13,8)
42c784eb0010000000000000 8:10:sym2+0	movl $sym2,0x1000(%rbx,%r13,8)
42f784eb0010000001000000	testl $1,0x1000(%rbx,%r13,8)
42c1a4eb0010000003	shll $3,0x1000(%rbx,%r13,8)
426b84eb0010000064	imull $100,0x1000(%rbx,%r13,8),%eax
42dd84eb00100000	fldl 0x1000(%rbx,%r13,8)
428884eb00100000	movb %al,0x1000(%rbx,%r13,8)
66428b9ceb00100000	movw 0x1000(%rbx,%r13,8),%bx
4a8984eb00100000	movq %rax,0x1000(%rbx,%r13,8)
4e8b8ceb00100000	movq 0x1000(%rbx,%r13,8),%r9
4ac784eb0010000001000000	movq $1,0x1000(%rbx,%r13,8)
4e8d94eb00100000	leaq 0x1000(%rbx,%r13,8),%r10
4a81bceb0010000000010000	cmpq $0x100,0x1000(%rbx,%r13,8)
4ac784eb0010000000000000 8:11:sym2+0	movq $sym2,0x1000(%rbx,%r13,8)
4a8384eb00100000ff	addq $-1,0x1000(%rbx,%r13,8)
42c684fb0000000001 4:11:sym+0	movb $1,sym(%rbx,%r15,8)
6642c784fb000000003412 5:11:sym+0	movw $0x1234,sym(%rbx,%r15,8)
42c784fb0000000078563412 4:11:sym+0	movl $0x12345678,sym(%rbx,%r15,8)
428384fb0000000004 4:11:sym+0	addl $4,sym(%rbx,%r15,8)
4280bcfb0000000000 4:11:sym+0	cmpb $0,sym(%rbx,%r15,8)
42ff84fb00000000 4:11:sym+0	incl sym(%rbx,%r15,8)
42ffb4fb00000000 4:11:sym+0	pushq sym(%rbx,%r15,8)
42ff94fb00000000 4:11:sym+0	call *sym(%rbx,%r15,8)
42c784fb0000000000000000 4:11:sym+0 8:10:sym2+0	movl $sym2,sym(%rbx,%r15,8)
42f784fb0000000001000000 4:11:sym+0	testl $1,sym(%rbx,%r15,8)
42c1a4fb0000000003 4:11:sym+0	shll $3,sym(%rbx,%r15,8)
426b84fb0000000064 4:11:sym+0	imull $100,sym(%rbx,%r15,8),%eax
42dd84fb00000000 4:11:sym+0	fldl sym(%rbx,%r15,8)
428884fb00000000 4:11:sym+0	movb %al,sym(%rbx,%r15,8)
66428b9cfb00000000 5:11:sym+0	movw sym(%rbx,%r15,8),%bx
4a8984fb00000000 4:11:sym+0	movq %rax,sym(%rbx,%r15,8)
4e8b8cfb00000000 4:11:sym+0	movq sym(%rbx,%r15,8),%r9
4ac784fb0000000001000000 4:11:sym+0	movq $1,sym(%rbx,%r15,8)
4e8d94fb00000000 4:11:sym+0	leaq sym(%rbx,%r15,8),%r10
4a81bcfb0000000000010000 4:11:sym+0	cmpq $0x100,sym(%rbx,%r15,8)
4ac784fb0000000000000000 4:11:sym+0 8:11:sym2+0	movq $sym2,sym(%rbx,%r15,8)
4a8384fb00000000ff 4:11:sym+0	addq $-1,sym(%rbx,%r15,8)
c6041c01	movb $1,(%rsp,%rbx)
66c7041c3412	movw $0x1234,(%rsp,%rbx)
c7041c78563412	movl $0x12345678,(%rsp,%rbx)
83041c04	addl $4,(%rsp,%rbx)
803c1c00	cmpb $0,(%rsp,%rbx)
ff041c	incl (%rsp,%rbx)
ff341c	pushq (%rsp,%rbx)
ff141c	call *(%rsp,%rbx)
c7041c00000000 3:10:sym2+0	movl $sym2,(%rsp,%rbx)
f7041c01000000	testl $1,(%rsp,%rbx)
c1241c03	shll $3,(%rsp,%rbx)
6b041c64	imull $100,(%rsp,%rbx),%eax
dd041c	fldl (%rsp,%rbx)
88041c	movb %al,(%rsp,%rbx)
668b1c1c	movw (%rsp,%rbx),%bx
4889041c	movq %rax,(%rsp,%rbx)
4c8b0c1c	movq (%rsp,%rbx),%r9
48c7041c01000000	movq $1,(%rsp,%rbx)
4c8d141c	leaq (%rsp,%rbx),%r10
48813c1c00010000	cmpq $0x100,(%rsp,%rbx)
48c7041c00000000 4:11:sym2+0	movq $sym2,(%rsp,%rbx)
4883041cff	addq $-1,(%rsp,%rbx)
c6042c01	movb $1,(%rsp,%rbp,1)
66c7042c3412	movw $0x1234,(%rsp,%rbp,1)
c7042c78563412	movl $0x12345678,(%rsp,%rbp,1)
83042c04	addl $4,(%rsp,%rbp,1)
803c2c00	cmpb $0,(%rsp,%rbp,1)
ff042c	incl (%rsp,%rbp,1)
ff342c	pushq (%rsp,%rbp,1)
ff142c	call *(%rsp,%rbp,1)
c7042c00000000 3:10:sym2+0	movl $sym2,(%rsp,%rbp,1)
f7042c01000000	testl $1,(%rsp,%rbp,1)
c1242c03	shll $3,(%rsp,%rbp,1)
6b042c64	imull $100,(%rsp,%rbp,1),%eax
dd042c	fldl (%rsp,%rbp,1)
88042c	movb %al,(%rsp,%rbp,1)
668b1c2c	movw (%rsp,%rbp,1),%bx
4889042c	movq %rax,(%rsp,%rbp,1)
4c8b0c2c	movq (%rsp,%rbp,1),%r9
48c7042c01000000	movq $1,(%rsp,%rbp,1)
4c8d142c	leaq (%rsp,%rbp,1),%r10
48813c2c00010000	cmpq $0x100,(%rsp,%rbp,1)
48c7042c00000000 4:11:sym2+0	movq $sym2,(%rsp,%rbp,1)
4883042cff	addq $-1,(%rsp,%rbp,1)
c6047401	movb $1,(%rsp,%rsi,2)
66c704743412	movw $0x1234,(%rsp,%rsi,2)
c7047478563412	movl $0x12345678,(%rsp,%rsi,2)
83047404	addl $4,(%rsp,%rsi,2)
803c7400	cmpb $0,(%rsp,%rsi,2)
ff0474	incl (%rsp,%rsi,2)
ff3474	pushq (%rsp,%rsi,2)
ff1474	call *(%rsp,%rsi,2)
c7047400000000 3:10:sym2+0	movl $sym2,(%rsp,%rsi,2)
f7047401000000	testl $1,(%rsp,%rsi,2)
c1247403	shll $3,(%rsp,%rsi,2)
6b047464	imull $100,(%rsp,%rsi,2),%eax
dd0474	fldl (%rsp,%rsi,2)
880474	movb %al,(%rsp,%rsi,2)
668b1c74	movw (%rsp,%rsi,2),%bx
48890474	movq %rax,(%rsp,%rsi,2)
4c8b0c74	movq (%rsp,%rsi,2),%r9
48c7047401000000	movq $1,(%rsp,%rsi,2)
4c8d1474	leaq (%rsp,%rsi,2),%r10
48813c7400010000	cmpq $0x100,(%rsp,%rsi,2)
48c7047400000000 4:11:sym2+0	movq $sym2,(%rsp,%rsi,2)
48830474ff	addq $-1,(%rsp,%rsi,2)
42c644a40401	movb $1,4(%rsp,%r12,4)
6642c744a4043412	movw $0x1234,4(%rsp,%r12,4)
42c744a40478563412	movl $0x12345678,4(%rsp,%r12,4)
428344a40404	addl $4,4(%rsp,%r12,4)
42807ca40400	cmpb $0,4(%rsp,%r12,4)
42ff44a404	incl 4(%rsp,%r12,4)
42ff74a404	pushq 4(%rsp,%r12,4)
42ff54a404	call *4(%rsp,%r12,4)
42c744a40400000000 5:10:sym2+0	movl $sym2,4(%rsp,%r12,4)
42f744a40401000000	testl $1,4(%rsp,%r12,4)
42c164a40403	shll $3,4(%rsp,%r12,4)
426b44a40464	imull $100,4(%rsp,%r12,4),%eax
42dd44a404	fldl 4(%rsp,%r12,4)
428844a404	movb %al,4(%rsp,%r12,4)
66428b5ca404	movw 4(%rsp,%r12,4),%bx
4a8944a404	movq %rax,4(%rsp,%r12,4)
4e8b4ca404	movq 4(%rsp,%r12,4),%r9
4ac744a40401000000	movq $1,4(%rsp,%r12,4)
4e8d54a404	leaq 4(%rsp,%r12,4),%r10
4a817ca40400010000	cmpq $0x100,4(%rsp,%r12,4)
4ac744a40400000000 5:11:sym2+0	movq $sym2,4(%rsp,%r12,4)
4a8344a404ff	addq $-1,4(%rsp,%r12,4)
42c684ec0010000001	movb $1,0x1000(%rsp,%r13,8)
6642c784ec001000003412	movw $0x1234,0x1000(%rsp,%r13,8)
42c784ec0010000078563412	movl $0x12345678,0x1000(%rsp,%r13,8)
428384ec0010000004	addl $4,0x1000(%rsp,%r13,8)
4280bcec0010000000	cmpb $0,0x1000(%rsp,%r13,8)
42ff84ec00100000	incl 0x1000(%rsp,%r13,8)
42ffb4ec00100000	pushq 0x1000(%rsp,%r13,8)
42ff94ec00100000	call *0x1000(%rsp,%r13,8)
42c784ec0010000000000000 8:10:sym2+0	movl $sym2,0x1000(%rsp,%r13,8)
42f784ec0010000001000000	testl $1,0x1000(%rsp,%r13,8)
42c1a4ec0010000003	shll $3,0x1000(%rsp,%r13,8)
426b84ec0010000064	imull $100,0x1000(%rsp,%r13,8),%eax
42dd84ec00100000	fldl 0x1000(%rsp,%r13,8)
428884ec00100000	movb %al,0x1000(%rsp,%r13,8)
66428b9cec00100000	movw 0x1000(%rsp,%r13,8),%bx
4a8984ec00100000	movq %rax,0x1000(%rsp,%r13,8)
4e8b8cec00100000	movq 0x1000(%rsp,%r13,8),%r9
4ac784ec0010000001000000	movq $1,0x1000(%rsp,%r13,8)
4e8d94ec00100000	leaq 0x1000(%rsp,%r13,8),%r10
4a81bcec0010000000010000	cmpq $0x100,0x1000(%rsp,%r13,8)
4ac784ec0010000000000000 8:11:sym2+0	movq $sym2,0x1000(%rsp,%r13,8)
4a8384ec00100000ff	addq $-1,0x1000(%rsp,%r13,8)
42c684fc0000000001 4:11:sym+0	movb $1,sym(%rsp,%r15,8)
6642c784fc000000003412 5:11:sym+0	movw $0x1234,sym(%rsp,%r15,8)
42c784fc0000000078563412 4:11:sym+0	movl $0x12345678,sym(%rsp,%r15,8)
428384fc0000000004 4:11:sym+0	addl $4,sym(%rsp,%r15,8)
4280bcfc0000000000 4:11:sym+0	cmpb $0,sym(%rsp,%r15,8)
42ff84fc00000000 4:11:sym+0	incl sym(%rsp,%r15,8)
42ffb4fc00000000 4:11:sym+0	pushq sym(%rsp,%r15,8)
42ff94fc00000000 4:11:sym+0	call *sym(%rsp,%r15,8)
42c784fc0000000000000000 4:11:sym+0 8:10:sym2+0	movl $sym2,sym(%rsp,%r15,8)
42f784fc0000000001000000 4:11:sym+0	testl $1,sym(%rsp,%r15,8)
42c1a4fc0000000003 4:11:sym+0	shll $3,sym(%rsp,%r15,8)
426b84fc0000000064 4:11:sym+0	imull $100,sym(%rsp,%r15,8),%eax
42dd84fc00000000 4:11:sym+0	fldl sym(%rsp,%r15,8)
428884fc00000000 4:11:sym+0	movb %al,sym(%rsp,%r15,8)
66428b9cfc00000000 5:11:sym+0	movw sym(%rsp,%r15,8),%bx
4a8984fc00000000 4:11:sym+0	movq %rax,sym(%rsp,%r15,8)
4e8b8cfc00000000 4:11:sym+0	movq sym(%rsp,%r15,8),%r9
4ac784fc0000000001000000 4:11:sym+0	movq $1,sym(%rsp,%r15,8)
4e8d94fc00000000 4:11:sym+0	leaq sym(%rsp,%r15,8),%r10
4a81bcfc0000000000010000 4:11:sym+0	cmpq $0x100,sym(%rsp,%r15,8)
4ac784fc0000000000000000 4:11:sym+0 8:11:sym2+0	movq $sym2,sym(%rsp,%r15,8)
4a8384fc00000000ff 4:11:sym+0	addq $-1,sym(%rsp,%r15,8)
c6441d0001	movb $1,(%rbp,%rbx)
66c7441d003412	movw $0x1234,(%rbp,%rbx)
c7441d0078563412	movl $0x12345678,(%rbp,%rbx)
83441d0004	addl $4,(%rbp,%rbx)
807c1d0000	cmpb $0,(%rbp,%rbx)
ff441d00	incl (%rbp,%rbx)
ff741d00	pushq (%rbp,%rbx)
ff541d00	call *(%rbp,%rbx)
c7441d0000000000 4:10:sym2+0	movl $sym2,(%rbp,%rbx)
f7441d0001000000	testl $1,(%rbp,%rbx)
c1641d0003	shll $3,(%rbp,%rbx)
6b441d0064	imull $100,(%rbp,%rbx),%eax
dd441d00	fldl (%rbp,%rbx)
88441d00	movb %al,(%rbp,%rbx)
668b5c1d00	movw (%rbp,%rbx),%bx
4889441d00	movq %rax,(%rbp,%rbx)
4c8b4c1d00	movq (%rbp,%rbx),%r9
48c7441d0001000000	movq $1,(%rbp,%rbx)
4c8d541d00	leaq (%rbp,%rbx),%r10
48817c1d0000010000	cmpq $0x100,(%rbp,%rbx)
48c7441d0000000000 5:11:sym2+0	movq $sym2,(%rbp,%rbx)
4883441d00ff	addq $-1,(%rbp,%rbx)
c6442d0001	movb $1,(%rbp,%rbp,1)
66c7442d003412	movw $0x1234,(%rbp,%rbp,1)
c7442d0078563412	movl $0x12345678,(%rbp,%rbp,1)
83442d0004	addl $4,(%rbp,%rbp,1)
807c2d0000	cmpb $0,(%rbp,%rbp,1)
ff442d00	incl (%rbp,%rbp,1)
ff742d00	pushq (%rbp,%rbp,1)
ff542d00	call *(%rbp,%rbp,1)
c7442d0000000000 4:10:sym2+0	movl $sym2,(%rbp,%rbp,1)
f7442d0001000000	testl $1,(%rbp,%rbp,1)
c1642d0003	shll $3,(%rbp,%rbp,1)
6b442d0064	imull $100,(%rbp,%rbp,1),%eax
dd442d00	fldl (%rbp,%rbp,1)
88442d00	movb %al,(%rbp,%rbp,1)
668b5c2d00	movw (%rbp,%rbp,1),%bx
4889442d00	movq %rax,(%rbp,%rbp,1)
4c8b4c2d00	movq (%rbp,%rbp,1),%r9
48c7442d0001000000	movq $1,(%rbp,%rbp,1)
4c8d542d00	leaq (%rbp,%rbp,1),%r10
48817c2d0000010000	cmpq $0x100,(%rbp,%rbp,1)
48c7442d0000000000 5:11:sym2+0	movq $sym2,(%rbp,%rbp,1)
4883442d00ff	addq $-1,(%rbp,%rbp,1)
c644750001	movb $1,(%rbp,%rsi,2)
66c74475003412	movw $0x1234,(%rbp,%rsi,2)
c744750078563412	movl $0x12345678,(%rbp,%rsi,2)
8344750004	addl $4,(%rbp,%rsi,2)
807c750000	cmpb $0,(%rbp,%rsi,2)
ff447500	incl (%rbp,%rsi,2)
ff747500	pushq (%rbp,%rsi,2)
ff547500	call *(%rbp,%rsi,2)
c744750000000000 4:10:sym2+0	movl $sym2,(%rbp,%rsi,2)
f744750001000000	testl $1,(%rbp,%rsi,2)
c164750003	shll $3,(%rbp,%rsi,2)
6b44750064	imull $100,(%rbp,%rsi,2),%eax
dd447500	fldl (%rbp,%rsi,2)
88447500	movb %al,(%rbp,%rsi,2)
668b5c7500	movw (%rbp,%rsi,2),%bx
4889447500	movq %rax,(%rbp,%rsi,2)
4c8b4c7500	movq (%rbp,%rsi,2),%r9
48c744750001000000	movq $1,(%rbp,%rsi,2)
4c8d547500	leaq (%rbp,%rsi,2),%r10
48817c750000010000	cmpq $0x100,(%rbp,%rsi,2)
48c744750000000000 5:11:sym2+0	movq $sym2,(%rbp,%rsi,2)
4883447500ff	addq $-1,(%rbp,%rsi,2)
42c644a50401	movb $1,4(%rbp,%r12,4)
6642c744a5043412	movw $0x1234,4(%rbp,%r12,4)
42c744a50478563412	movl $0x12345678,4(%rbp,%r12,4)
428344a50404	addl $4,4(%rbp,%r12,4)
42807ca50400	cmpb $0,4(%rbp,%r12,4)
42ff44a504	incl 4(%rbp,%r12,4)
42ff74a504	pushq 4(%rbp,%r12,4)
42ff54a504	call *4(%rbp,%r12,4)
42c744a50400000000 5:10:sym2+0	movl $sym2,4(%rbp,%r12,4)
42f744a50401000000	testl $1,4(%rbp,%r12,4)
42c164a50403	shll $3,4(%rbp,%r12,4)
426b44a50464	imull $100,4(%rbp,%r12,4),%eax
42dd44a504	fldl 4(%rbp,%r12,4)
428844a504	movb %al,4(%rbp,%r12,4)
66428b5ca504	movw 4(%rbp,%r12,4),%bx
4a8944a504	movq %rax,4(%rbp,%r12,4)
4e8b4ca504	movq 4(%rbp,%r12,4),%r9
4ac744a50401000000	movq $1,4(%rbp,%r12,4)
4e8d54a504	leaq 4(%rbp,%r12,4),%r10
4a817ca50400010000	cmpq $0x100,4(%rbp,%r12,4)
4ac744a50400000000 5:11:sym2+0	movq $sym2,4(%rbp,%r12,4)
4a8344a504ff	addq $-1,4(%rbp,%r12,4)
42c684ed0010000001	movb $1,0x1000(%rbp,%r13,8)
6642c784ed001000003412	movw $0x1234,0x1000(%rbp,%r13,8)
42c784ed0010000078563412	movl $0x12345678,0x1000(%rbp,%r13,8)
428384ed0010000004	addl $4,0x1000(%rbp,%r13,8)
4280bced0010000000	cmpb $0,0x1000(%rbp,%r13,8)
42ff84ed00100000	incl 0x1000(%rbp,%r13,8)
42ffb4ed00100000	pushq 0x1000(%rbp,%r13,8)
42ff94ed00100000	call *0x1000(%rbp,%r13,8)
42c784ed0010000000000000 8:10:sym2+0	movl $sym2,0x1000(%rbp,%r13,8)
42f784ed0010000001000000	testl $1,0x1000(%rbp,%r13,8)
42c1a4ed0010000003	shll $3,0x1000(%rbp,%r13,8)
426b84ed0010000064	imull $100,0x1000(%rbp,%r13,8),%eax
42dd84ed00100000	fldl 0x1000(%rbp,%r13,8)
428884ed00100000	movb %al,0x1000(%rbp,%r13,8)
66428b9ced00100000	movw 0x1000(%rbp,%r13,8),%bx
4a8984ed00100000	movq %rax,0x1000(%rbp,%r13,8)
4e8b8ced00100000	movq 0x1000(%rbp,%r13,8),%r9
4ac784ed0010000001000000	movq $1,0x1000(%rbp,%r13,8)
4e8d94ed00100000	leaq 0x1000(%rbp,%r13,8),%r10
4a81bced0010000000010000	cmpq $0x100,0x1000(%rbp,%r13,8)
4ac784ed0010000000000000 8:11:sym2+0	movq $sym2,0x1000(%rbp,%r13,8)
4a8384ed00100000ff	addq $-1,0x1000(%rbp,%r13,8)
42c684fd0000000001 4:11:sym+0	movb $1,sym(%rbp,%r15,8)
6642c784fd000000003412 5:11:sym+0	movw $0x1234,sym(%rbp,%r15,8)
42c784fd0000000078563412 4:11:sym+0	movl $0x12345678,sym(%rbp,%r15,8)
428384fd0000000004 4:11:sym+0	addl $4,sym(%rbp,%r15,8)
4280bcfd0000000000 4:11:sym+0	cmpb $0,sym(%rbp,%r15,8)
42ff84fd00000000 4:11:sym+0	incl sym(%rbp,%r15,8)
42ffb4fd00000000 4:11:sym+0	pushq sym(%rbp,%r15,8)
42ff94fd00000000 4:11:sym+0	call *sym(%rbp,%r15,8)
42c784fd0000000000000000 4:11:sym+0 8:10:sym2+0	movl $sym2,sym(%rbp,%r15,8)
42f784fd0000000001000000 4:11:sym+0	testl $1,sym(%rbp,%r15,8)
42c1a4fd0000000003 4:11:sym+0	shll $3,sym(%rbp,%r15,8)
426b84fd0000000064 4:11:sym+0	imull $100,sym(%rbp,%r15,8),%eax
42dd84fd00000000 4:11:sym+0	fldl sym(%rbp,%r15,8)
428884fd00000000 4:11:sym+0	movb %al,sym(%rbp,%r15,8)
66428b9cfd00000000 5:11:sym+0	movw sym(%rbp,%r15,8),%bx
4a8984fd00000000 4:11:sym+0	movq %rax,sym(%rbp,%r15,8)
4e8b8cfd00000000 4:11:sym+0	movq sym(%rbp,%r15,8),%r9
4ac784fd0000000001000000 4:11:sym+0	movq $1,sym(%rbp,%r15,8)
4e8d94fd00000000 4:11:sym+0	leaq sym(%rbp,%r15,8),%r10
4a81bcfd0000000000010000 4:11:sym+0	cmpq $0x100,sym(%rbp,%r15,8)
4ac784fd0000000000000000 4:11:sym+0 8:11:sym2+0	movq $sym2,sym(%rbp,%r15,8)
4a8384fd00000000ff 4:11:sym+0	addq $-1,sym(%rbp,%r15,8)
c6041e01	movb $1,(%rsi,%rbx)
66c7041e3412	movw $0x1234,(%rsi,%rbx)
c7041e78563412	movl $0x12345678,(%rsi,%rbx)
83041e04	addl $4,(%rsi,%rbx)
803c1e00	cmpb $0,(%rsi,%rbx)
ff041e	incl (%rsi,%rbx)
ff341e	pushq (%rsi,%rbx)
ff141e	call *(%rsi,%rbx)
c7041e00000000 3:10:sym2+0	movl $sym2,(%rsi,%rbx)
f7041e01000000	testl $1,(%rsi,%rbx)
c1241e03	shll $3,(%rsi,%rbx)
6b041e64	imull $100,(%rsi,%rbx),%eax
dd041e	fldl (%rsi,%rbx)
88041e	movb %al,(%rsi,%rbx)
668b1c1e	movw (%rsi,%rbx),%bx
4889041e	movq %rax,(%rsi,%rbx)
4c8b0c1e	movq (%rsi,%rbx),%r9
48c7041e01000000	movq $1,(%rsi,%rbx)
4c8d141e	leaq (%rsi,%rbx),%r10
48813c1e00010000	cmpq $0x100,(%rsi,%rbx)
48c7041e00000000 4:11:sym2+0	movq $sym2,(%rsi,%rbx)
4883041eff	addq $-1,(%rsi,%rbx)
c6042e01	movb $1,(%rsi,%rbp,1)
66c7042e3412	movw $0x1234,(%rsi,%rbp,1)
c7042e78563412	movl $0x12345678,(%rsi,%rbp,1)
83042e04	addl $4,(%rsi,%rbp,1)
803c2e00	cmpb $0,(%rsi,%rbp,1)
ff042e	incl (%rsi,%rbp,1)
ff342e	pushq (%rsi,%rbp,1)
ff142e	call *(%rsi,%rbp,1)
c7042e00000000 3:10:sym2+0	movl $sym2,(%rsi,%rbp,1)
f7042e01000000	testl $1,(%rsi,%rbp,1)
c1242e03	shll $3,(%rsi,%rbp,1)
6b042e64	imull $100,(%rsi,%rbp,1),%eax
dd042e	fldl (%rsi,%rbp,1)
88042e	movb %al,(%rsi,%rbp,1)
668b1c2e	movw (%rsi,%rbp,1),%bx
4889042e	movq %rax,(%rsi,%rbp,1)
4c8b0c2e	movq (%rsi,%rbp,1),%r9
48c7042e01000000	movq $1,(%rsi,%rbp,1)
4c8d142e	leaq (%rsi,%rbp,1),%r10
48813c2e00010000	cmpq $0x100,(%rsi,%rbp,1)
48c7042e00000000 4:11:sym2+0	movq $sym2,(%rsi,%rbp,1)
4883042eff	addq $-1,(%rsi,%rbp,1)
c6047601	movb $1,(%rsi,%rsi,2)
66c704763412	movw $0x1234,(%rsi,%rsi,2)
c7047678563412	movl $0x12345678,(%rsi,%rsi,2)
83047604	addl $4,(%rsi,%rsi,2)
803c7600	cmpb $0,(%rsi,%rsi,2)
ff0476	incl (%rsi,%rsi,2)
ff3476	pushq (%rsi,%rsi,2)
ff1476	call *(%rsi,%rsi,2)
c7047600000000 3:10:sym2+0	movl $sym2,(%rsi,%rsi,2)
f7047601000000	testl $1,(%rsi,%rsi,2)
c1247603	shll $3,(%rsi,%rsi,2)
6b047664	imull $100,(%rsi,%rsi,2),%eax
dd0476	fldl (%rsi,%rsi,2)
880476	movb %al,(%rsi,%rsi,2)
668b1c76	movw (%rsi,%rsi,2),%bx
48890476	movq %rax,(%rsi,%rsi,2)
4c8b0c76	movq (%rsi,%rsi,2),%r9
48c7047601000000	movq $1,(%rsi,%rsi,2)
4c8d1476	leaq (%rsi,%rsi,2),%r10
48813c7600010000	cmpq $0x100,(%rsi,%rsi,2)
48c7047600000000 4:11:sym2+0	movq $sym2,(%rsi,%rsi,2)
48830476ff	addq $-1,(%rsi,%rsi,2)
42c644a60401	movb $1,4(%rsi,%r12,4)
6642c744a6043412	movw $0x1234,4(%rsi,%r12,4)
42c744a60478563412	movl $0x12345678,4(%rsi,%r12,4)
428344a60404	addl $4,4(%rsi,%r12,4)
42807ca60400	cmpb $0,4(%rsi,%r12,4)
42ff44a604	incl 4(%rsi,%r12,4)
42ff74a604	pushq 4(%rsi,%r12,4)
42ff54a604	call *4(%rsi,%r12,4)
42c744a60400000000 5:10:sym2+0	movl $sym2,4(%rsi,%r12,4)
42f744a60401000000	testl $1,4(%rsi,%r12,4)
42c164a60403	shll $3,4(%rsi,%r12,4)
426b44a60464	imull $100,4(%rsi,%r12,4),%eax
42dd44a604	fldl 4(%rsi,%r12,4)
428844a604	movb %al,4(%rsi,%r12,4)
66428b5ca604	movw 4(%rsi,%r12,4),%bx
4a8944a604	movq %rax,4(%rsi,%r12,4)
4e8b4ca604	movq 4(%rsi,%r12,4),%r9
4ac744a60401000000	movq $1,4(%rsi,%r12,4)
4e8d54a604	leaq 4(%rsi,%r12,4),%r10
4a817ca60400010000	cmpq $0x100,4(%rsi,%r12,4)
4ac744a60400000000 5:11:sym2+0	movq $sym2,4(%rsi,%r12,4)
4a8344a604ff	addq $-1,4(%rsi,%r12,4)
42c684ee0010000001	movb $1,0x1000(%rsi,%r13,8)
6642c784ee001000003412	movw $0x1234,0x1000(%rsi,%r13,8)
42c784ee0010000078563412	movl $0x12345678,0x1000(%rsi,%r13,8)
428384ee0010000004	addl $4,0x1000(%rsi,%r13,8)
4280bcee0010000000	cmpb $0,0x1000(%rsi,%r13,8)
42ff84ee00100000	incl 0x1000(%rsi,%r13,8)
42ffb4ee00100000	pushq 0x1000(%rsi,%r13,8)
42ff94ee00100000	call *0x1000(%rsi,%r13,8)
42c784ee0010000000000000 8:10:sym2+0	movl $sym2,0x1000(%rsi,%r13,8)
42f784ee0010000001000000	testl $1,0x1000(%rsi,%r13,8)
42c1a4ee0010000003	shll $3,0x1000(%rsi,%r13,8)
426b84ee0010000064	imull $100,0x1000(%rsi,%r13,8),%eax
42dd84ee00100000	fldl 0x1000(%rsi,%r13,8)
428884ee00100000	movb %al,0x1000(%rsi,%r13,8)
66428b9cee00100000	movw 0x1000(%rsi,%r13,8),%bx
4a8984ee00100000	movq %rax,0x1000(%rsi,%r13,8)
4e8b8cee00100000	movq 0x1000(%rsi,%r13,8),%r9
4ac784ee0010000001000000	movq $1,0x1000(%rsi,%r13,8)
4e8d94ee00100000	leaq 0x1000(%rsi,%r13,8),%r10
4a81bcee0010000000010000	cmpq $0x100,0x1000(%rsi,%r13,8)
4ac784ee0010000000000000 8:11:sym2+0	movq $sym2,0x1000(%rsi,%r13,8)
4a8384ee00100000ff	addq $-1,0x1000(%rsi,%r13,8)
42c684fe0000000001 4:11:sym+0	movb $1,sym(%rsi,%r15,8)
6642c784fe000000003412 5:11:sym+0	movw $0x1234,sym(%rsi,%r15,8)
42c784fe0000000078563412 4:11:sym+0	movl $0x12345678,sym(%rsi,%r15,8)
428384fe0000000004 4:11:sym+0	addl $4,sym(%rsi,%r15,8)
4280bcfe0000000000 4:11:sym+0	cmpb $0,sym(%rsi,%r15,8)
42ff84fe00000000 4:11:sym+0	incl sym(%rsi,%r15,8)
42ffb4fe00000000 4:11:sym+0	pushq sym(%rsi,%r15,8)
42ff94fe00000000 4:11:sym+0	call *sym(%rsi,%r15,8)
42c784fe0000000000000000 4:11:sym+0 8:10:sym2+0	movl $sym2,sym(%rsi,%r15,8)
42f784fe0000000001000000 4:11:sym+0	testl $1,sym(%rsi,%r15,8)
42c1a4fe0000000003 4:11:sym+0	shll $3,sym(%rsi,%r15,8)
426b84fe0000000064 4:11:sym+0	imull $100,sym(%rsi,%r15,8),%eax
42dd84fe00000000 4:11:sym+0	fldl sym(%rsi,%r15,8)
428884fe00000000 4:11:sym+0	movb %al,sym(%rsi,%r15,8)
66428b9cfe00000000 5:11:sym+0	movw sym(%rsi,%r15,8),%bx
4a8984fe00000000 4:11:sym+0	movq %rax,sym(%rsi,%r15,8)
4e8b8cfe00000000 4:11:sym+0	movq sym(%rsi,%r15,8),%r9
4ac784fe0000000001000000 4:11:sym+0	movq $1,sym(%rsi,%r15,8)
4e8d94fe00000000 4:11:sym+0	leaq sym(%rsi,%r15,8),%r10
4a81bcfe0000000000010000 4:11:sym+0	cmpq $0x100,sym(%rsi,%r15,8)
4ac784fe0000000000000000 4:11:sym+0 8:11:sym2+0	movq $sym2,sym(%rsi,%r15,8)
4a8384fe00000000ff 4:11:sym+0	addq $-1,sym(%rsi,%r15,8)
41c6041c01	movb $1,(%r12,%rbx)
6641c7041c3412	movw $0x1234,(%r12,%rbx)
41c7041c78563412	movl $0x12345678,(%r12,%rbx)
4183041c04	addl $4,(%r12,%rbx)
41803c1c00	cmpb $0,(%r12,%rbx)
41ff041c	incl (%r12,%rbx)
41ff341c	pushq (%r12,%rbx)
41ff141c	call *(%r12,%rbx)
41c7041c00000000 4:10:sym2+0	movl $sym2,(%r12,%rbx)
41f7041c01000000	testl $1,(%r12,%rbx)
41c1241c03	shll $3,(%r12,%rbx)
416b041c64	imull $100,(%r12,%rbx),%eax
41dd041c	fldl (%r12,%rbx)
4188041c	movb %al,(%r12,%rbx)
66418b1c1c	movw (%r12,%rbx),%bx
4989041c	movq %rax,(%r12,%rbx)
4d8b0c1c	movq (%r12,%rbx),%r9
49c7041c01000000	movq $1,(%r12,%rbx)
4d8d141c	leaq (%r12,%rbx),%r10
49813c1c00010000	cmpq $0x100,(%r12,%rbx)
49c7041c00000000 4:11:sym2+0	movq $sym2,(%r12,%rbx)
4983041cff	addq $-1,(%r12,%rbx)
41c6042c01	movb $1,(%r12,%rbp,1)
6641c7042c3412	movw $0x1234,(%r12,%rbp,1)
41c7042c78563412	movl $0x12345678,(%r12,%rbp,1)
4183042c04	addl $4,(%r12,%rbp,1)
41803c2c00	cmpb $0,(%r12,%rbp,1)
41ff042c	incl (%r12,%rbp,1)
41ff342c	pushq (%r12,%rbp,1)
41ff142c	call *(%r12,%rbp,1)
41c7042c00000000 4:10:sym2+0	movl $sym2,(%r12,%rbp,1)
41f7042c01000000	testl $1,(%r12,%rbp,1)
41c1242c03	shll $3,(%r12,%rbp,1)
416b042c64	imull $100,(%r12,%rbp,1),%eax
41dd042c	fldl (%r12,%rbp,1)
4188042c	movb %al,(%r12,%rbp,1)
66418b1c2c	movw (%r12,%rbp,1),%bx
4989042c	movq %rax,(%r12,%rbp,1)
4d8b0c2c	movq (%r12,%rbp,1),%r9
49c7042c01000000	movq $1,(%r12,%rbp,1)
4d8d142c	leaq (%r12,%rbp,1),%r10
49813c2c00010000	cmpq $0x100,(%r12,%rbp,1)
49c7042c00000000 4:11:sym2+0	movq $sym2,(%r12,%rbp,1)
4983042cff	addq $-1,(%r12,%rbp,1)
41c6047401	movb $1,(%r12,%rsi,2)
6641c704743412	movw $0x1234,(%r12,%rsi,2)
41c7047478563412	movl $0x12345678,(%r12,%rsi,2)
4183047404	addl $4,(%r12,%rsi,2)
41803c7400	cmpb $0,(%r12,%rsi,2)
41ff0474	incl (%r12,%rsi,2)
41ff3474	pushq (%r12,%rsi,2)
41ff1474	call *(%r12,%rsi,2)
41c7047400000000 4:10:sym2+0	movl $sym2,(%r12,%rsi,2)
41f7047401000000	testl $1,(%r12,%rsi,2)
41c1247403	shll $3,(%r12,%rsi,2)
416b047464	imull $100,(%r12,%rsi,2),%eax
41dd0474	fldl (%r12,%rsi,2)
41880474	movb %al,(%r12,%rsi,2)
66418b1c74	movw (%r12,%rsi,2),%bx
49890474	movq %rax,(%r12,%rsi,2)
4d8b0c74	movq (%r12,%rsi,2),%r9
49c7047401000000	movq $1,(%r12,%rsi,2)
4d8d1474	leaq (%r12,%rsi,2),%r10
49813c7400010000	cmpq $0x100,(%r12,%rsi,2)
49c7047400000000 4:11:sym2+0	movq $sym2,(%r12,%rsi,2)
49830474ff	addq $-1,(%r12,%rsi,2)
43c644a40401	movb $1,4(%r12,%r12,4)
6643c744a4043412	movw $0x1234,4(%r12,%r12,4)
43c744a40478563412	movl $0x12345678,4(%r12,%r12,4)
438344a40404	addl $4,4(%r12,%r12,4)
43807ca40400	cmpb $0,4(%r12,%r12,4)
43ff44a404	incl 4(%r12,%r12,4)
43ff74a404	pushq 4(%r12,%r12,4)
43ff54a404	call *4(%r12,%r12,4)
43c744a40400000000 5:10:sym2+0	movl $sym2,4(%r12,%r12,4)
43f744a40401000000	testl $1,4(%r12,%r12,4)
43c164a40403	shll $3,4(%r12,%r12,4)
436b44a40464	imull $100,4(%r12,%r12,4),%eax
43dd44a404	fldl 4(%r12,%r12,4)
438844a404	movb %al,4(%r12,%r12,4)
66438b5ca404	movw 4(%r12,%r12,4),%bx
4b8944a404	movq %rax,4(%r12,%r12,4)
4f8b4ca404	movq 4(%r12,%r12,4),%r9
4bc744a40401000000	movq $1,4(%r12,%r12,4)
4f8d54a404	leaq 4(%r12,%r12,4),%r10
4b817ca40400010000	cmpq $0x100,4(%r12,%r12,4)
4bc744a40400000000 5:11:sym2+0	movq $sym2,4(%r12,%r12,4)
4b8344a404ff	addq $-1,4(%r12,%r12,4)
43c684ec0010000001	movb $1,0x1000(%r12,%r13,8)
6643c784ec001000003412	movw $0x1234,0x1000(%r12,%r13,8)
43c784ec0010000078563412	movl $0x12345678,0x1000(%r12,%r13,8)
438384ec0010000004	addl $4,0x1000(%r12,%r13,8)
4380bcec0010000000	cmpb $0,0x1000(%r12,%r13,8)
43ff84ec00100000	incl 0x1000(%r12,%r13,8)
43ffb4ec00100000	pushq 0x1000(%r12,%r13,8)
43ff94ec00100000	call *0x1000(%r12,%r13,8)
43c784ec0010000000000000 8:10:sym2+0	movl $sym2,0x1000(%r12,%r13,8)
43f784ec0010000001000000	testl $1,0x1000(%r12,%r13,8)
43c1a4ec0010000003	shll $3,0x1000(%r12,%r13,8)
436b84ec0010000064	imull $100,0x1000(%r12,%r13,8),%eax
43dd84ec00100000	fldl 0x1000(%r12,%r13,8)
438884ec00100000	movb %al,0x1000(%r12,%r13,8)
66438b9cec00100000	movw 0x1000(%r12,%r13,8),%bx
4b8984ec00100000	movq %rax,0x1000(%r12,%r13,8)
4f8b8cec00100000	movq 0x1000(%r12,%r13,8),%r9
4bc784ec0010000001000000	movq $1,0x1000(%r12,%r13,8)
4f8d94ec00100000	leaq 0x1000(%r12,%r13,8),%r10
4b81bcec0010000000010000	cmpq $0x100,0x1000(%r12,%r13,8)
4bc784ec0010000000000000 8:11:sym2+0	movq $sym2,0x1000(%r12,%r13,8)
4b8384ec00100000ff	addq $-1,0x1000(%r12,%r13,8)
43c684fc0000000001 4:11:sym+0	movb $1,sym(%r12,%r15,8)
6643c784fc000000003412 5:11:sym+0	movw $0x1234,sym(%r12,%r15,8)
43c784fc0000000078563412 4:11:sym+0	movl $0x12345678,sym(%r12,%r15,8)
438384fc0000000004 4:11:sym+0	addl $4,sym(%r12,%r15,8)
4380bcfc0000000000 4:11:sym+0	cmpb $0,sym(%r12,%r15,8)
43ff84fc00000000 4:11:sym+0	incl sym(%r12,%r15,8)
43ffb4fc00000000 4:11:sym+0	pushq sym(%r12,%r15,8)
43ff94fc00000000 4:11:sym+0	call *sym(%r12,%r15,8)
43c784fc0000000000000000 4:11:sym+0 8:10:sym2+0	movl $sym2,sym(%r12,%r15,8)
43f784fc0000000001000000 4:11:sym+0	testl $1,sym(%r12,%r15,8)
43c1a4fc0000000003 4:11:sym+0	shll $3,sym(%r12,%r15,8)
436b84fc0000000064 4:11:sym+0	imull $100,sym(%r12,%r15,8),%eax
43dd84fc00000000 4:11:sym+0	fldl sym(%r12,%r15,8)
438884fc00000000 4:11:sym+0	movb %al,sym(%r12,%r15,8)
66438b9cfc00000000 5:11:sym+0	movw sym(%r12,%r15,8),%bx
4b8984fc00000000 4:11:sym+0	movq %rax,sym(%r12,%r15,8)
4f8b8cfc00000000 4:11:sym+0	movq sym(%r12,%r15,8),%r9
4bc784fc0000000001000000 4:11:sym+0	movq $1,sym(%r12,%r15,8)
4f8d94fc00000000 4:11:sym+0	leaq sym(%r12,%r15,8),%r10
4b81bcfc0000000000010000 4:11:sym+0	cmpq $0x100,sym(%r12,%r15,8)
4bc784fc0000000000000000 4:11:sym+0 8:11:sym2+0	movq $sym2,sym(%r12,%r15,8)
4b8384fc00000000ff 4:11:sym+0	addq $-1,sym(%r12,%r15,8)
41c6441d0001	movb $1,(%r13,%rbx)
6641c7441d003412	movw $0x1234,(%r13,%rbx)
41c7441d0078563412	movl $0x12345678,(%r13,%rbx)
4183441d0004	addl $4,(%r13,%rbx)
41807c1d0000	cmpb $0,(%r13,%rbx)
41ff441d00	incl (%r13,%rbx)
41ff741d00	pushq (%r13,%rbx)
41ff541d00	call *(%r13,%rbx)
41c7441d0000000000 5:10:sym2+0	movl $sym2,(%r13,%rbx)
41f7441d0001000000	testl $1,(%r13,%rbx)
41c1641d0003	shll $3,(%r13,%rbx)
416b441d0064	imull $100,(%r13,%rbx),%eax
41dd441d00	fldl (%r13,%rbx)
4188441d00	movb %al,(%r13,%rbx)
66418b5c1d00	movw (%r13,%rbx),%bx
4989441d00	movq %rax,(%r13,%rbx)
4d8b4c1d00	movq (%r13,%rbx),%r9
49c7441d0001000000	movq $1,(%r13,%rbx)
4d8d541d00	leaq (%r13,%rbx),%r10
49817c1d0000010000	cmpq $0x100,(%r13,%rbx)
49c7441d0000000000 5:11:sym2+0	movq $sym2,(%r13,%rbx)
4983441d00ff	addq $-1,(%r13,%rbx)
41c6442d0001	movb $1,(%r13,%rbp,1)
6641c7442d003412	movw $0x1234,(%r13,%rbp,1)
41c7442d0078563412	movl $0x12345678,(%r13,%rbp,1)
4183442d0004	addl $4,(%r13,%rbp,1)
41807c2d0000	cmpb $0,(%r13,%rbp,1)
41ff442d00	incl (%r13,%rbp,1)
41ff742d00	pushq (%r13,%rbp,1)
41ff542d00	call *(%r13,%rbp,1)
41c7442d0000000000 5:10:sym2+0	movl $sym2,(%r13,%rbp,1)
41f7442d0001000000	testl $1,(%r13,%rbp,1)
41c1642d0003	shll $3,(%r13,%rbp,1)
416b442d0064	imull $100,(%r13,%rbp,1),%eax
41dd442d00	fldl (%r13,%rbp,1)
4188442d00	movb %al,(%r13,%rbp,1)
66418b5c2d00	movw (%r13,%rbp,1),%bx
4989442d00	movq %rax,(%r13,%rbp,1)
4d8b4c2d00	movq (%r13,%rbp,1),%r9
49c7442d0001000000	movq $1,(%r13,%rbp,1)
4d8d542d00	leaq (%r13,%rbp,1),%r10
49817c2d0000010000	cmpq $0x100,(%r13,%rbp,1)
49c7442d0000000000 5:11:sym2+0	movq $sym2,(%r13,%rbp,1)
4983442d00ff	addq $-1,(%r13,%rbp,1)
41c644750001	movb $1,(%r13,%rsi,2)
6641c74475003412	movw $0x1234,(%r13,%rsi,2)
41c744750078563412	movl $0x12345678,(%r13,%rsi,2)
418344750004	addl $4,(%r13,%rsi,2)
41807c750000	cmpb $0,(%r13,%rsi,2)
41ff447500	incl (%r13,%rsi,2)
41ff747500	pushq (%r13,%rsi,2)
41ff547500	call *(%r13,%rsi,2)
41c744750000000000 5:10:sym2+0	movl $sym2,(%r13,%rsi,2)
41f744750001000000	testl $1,(%r13,%rsi,2)
41c164750003	shll $3,(%r13,%rsi,2)
416b44750064	imull $100,(%r13,%rsi,2),%eax
41dd447500	fldl (%r13,%rsi,2)
4188447500	movb %al,(%r13,%rsi,2)
66418b5c7500	movw (%r13,%rsi,2),%bx
4989447500	movq %rax,(%r13,%rsi,2)
4d8b4c7500	movq (%r13,%rsi,2),%r9
49c744750001000000	movq $1,(%r13,%rsi,2)
4d8d547500	leaq (%r13,%rsi,2),%r10
49817c750000010000	cmpq $0x100,(%r13,%rsi,2)
49c744750000000000 5:11:sym2+0	movq $sym2,(%r13,%rsi,2)
4983447500ff	addq $-1,(%r13,%rsi,2)
43c644a50401	movb $1,4(%r13,%r12,4)
6643c744a5043412	movw $0x1234,4(%r13,%r12,4)
43c744a50478563412	movl $0x12345678,4(%r13,%r12,4)
438344a50404	addl $4,4(%r13,%r12,4)
43807ca50400	cmpb $0,4(%r13,%r12,4)
43ff44a504	incl 4(%r13,%r12,4)
43ff74a504	pushq 4(%r13,%r12,4)
43ff54a504	call *4(%r13,%r12,4)
43c744a50400000000 5:10:sym2+0	movl $sym2,4(%r13,%r12,4)
43f744a50401000000	testl $1,4(%r13,%r12,4)
43c164a50403	shll $3,4(%r13,%r12,4)
436b44a50464	imull $100,4(%r13,%r12,4),%eax
43dd44a504	fldl 4(%r13,%r12,4)
438844a504	movb %al,4(%r13,%r12,4)
66438b5ca504	movw 4(%r13,%r12,4),%bx
4b8944a504	movq %rax,4(%r13,%r12,4)
4f8b4ca504	movq 4(%r13,%r12,4),%r9
4bc744a50401000000	movq $1,4(%r13,%r12,4)
4f8d54a504	leaq 4(%r13,%r12,4),%r10
4b817ca50400010000	cmpq $0x100,4(%r13,%r12,4)
4bc744a50400000000 5:11:sym2+0	movq $sym2,4(%r13,%r12,4)
4b8344a504ff	addq $-1,4(%r13,%r12,4)
43c684ed0010000001	movb $1,0x1000(%r13,%r13,8)
6643c784ed001000003412	movw $0x1234,0x1000(%r13,%r13,8)
43c784ed0010000078563412	movl $0x12345678,0x1000(%r13,%r13,8)
438384ed0010000004	addl $4,0x1000(%r13,%r13,8)
4380bced0010000000	cmpb $0,0x1000(%r13,%r13,8)
43ff84ed00100000	incl 0x1000(%r13,%r13,8)
43ffb4ed00100000	pushq 0x1000(%r13,%r13,8)
43ff94ed00100000	call *0x1000(%r13,%r13,8)
43c784ed0010000000000000 8:10:sym2+0	movl $sym2,0x1000(%r13,%r13,8)
43f784ed0010000001000000	testl $1,0x1000(%r13,%r13,8)
43c1a4ed0010000003	shll $3,0x1000(%r13,%r13,8)
436b84ed0010000064	imull $100,0x1000(%r13,%r13,8),%eax
43dd84ed00100000	fldl 0x1000(%r13,%r13,8)
438884ed00100000	movb %al,0x1000(%r13,%r13,8)
66438b9ced00100000	movw 0x1000(%r13,%r13,8),%bx
4b8984ed00100000	movq %rax,0x1000(%r13,%r13,8)
4f8b8ced00100000	movq 0x1000(%r13,%r13,8),%r9
4bc784ed0010000001000000	movq $1,0x1000(%r13,%r13,8)
4f8d94ed00100000	leaq 0x1000(%r13,%r13,8),%r10
4b81bced0010000000010000	cmpq $0x100,0x1000(%r13,%r13,8)
4bc784ed0010000000000000 8:11:sym2+0	movq $sym2,0x1000(%r13,%r13,8)
4b8384ed00100000ff	addq $-1,0x1000(%r13,%r13,8)
43c684fd0000000001 4:11:sym+0	movb $1,sym(%r13,%r15,8)
6643c784fd000000003412 5:11:sym+0	movw $0x1234,sym(%r13,%r15,8)
43c784fd0000000078563412 4:11:sym+0	movl $0x12345678,sym(%r13,%r15,8)
438384fd0000000004 4:11:sym+0	addl $4,sym(%r13,%r15,8)
4380bcfd0000000000 4:11:sym+0	cmpb $0,sym(%r13,%r15,8)
43ff84fd00000000 4:11:sym+0	incl sym(%r13,%r15,8)
43ffb4fd00000000 4:11:sym+0	pushq sym(%r13,%r15,8)
43ff94fd00000000 4:11:sym+0	call *sym(%r13,%r15,8)
43c784fd0000000000000000 4:11:sym+0 8:10:sym2+0	movl $sym2,sym(%r13,%r15,8)
43f784fd0000000001000000 4:11:sym+0	testl $1,sym(%r13,%r15,8)
43c1a4fd0000000003 4:11:sym+0	shll $3,sym(%r13,%r15,8)
436b84fd0000000064 4:11:sym+0	imull $100,sym(%r13,%r15,8),%eax
43dd84fd00000000 4:11:sym+0	fldl sym(%r13,%r15,8)
438884fd00000000 4:11:sym+0	movb %al,sym(%r13,%r15,8)
66438b9cfd00000000 5:11:sym+0	movw sym(%r13,%r15,8),%bx
4b8984fd00000000 4:11:sym+0	movq %rax,sym(%r13,%r15,8)
4f8b8cfd00000000 4:11:sym+0	movq sym(%r13,%r15,8),%r9
4bc784fd0000000001000000 4:11:sym+0	movq $1,sym(%r13,%r15,8)
4f8d94fd00000000 4:11:sym+0	leaq sym(%r13,%r15,8),%r10
4b81bcfd0000000000010000 4:11:sym+0	cmpq $0x100,sym(%r13,%r15,8)
4bc784fd0000000000000000 4:11:sym+0 8:11:sym2+0	movq $sym2,sym(%r13,%r15,8)
4b8384fd00000000ff 4:11:sym+0	addq $-1,sym(%r13,%r15,8)
41c6041f01	movb $1,(%r15,%rbx)
6641c7041f3412	movw $0x1234,(%r15,%rbx)
41c7041f78563412	movl $0x12345678,(%r15,%rbx)
4183041f04	addl $4,(%r15,%rbx)
41803c1f00	cmpb $0,(%r15,%rbx)
41ff041f	incl (%r15,%rbx)
41ff341f	pushq (%r15,%rbx)
41ff141f	call *(%r15,%rbx)
41c7041f00000000 4:10:sym2+0	movl $sym2,(%r15,%rbx)
41f7041f01000000	testl $1,(%r15,%rbx)
41c1241f03	shll $3,(%r15,%rbx)
416b041f64	imull $100,(%r15,%rbx),%eax
41dd041f	fldl (%r15,%rbx)
4188041f	movb %al,(%r15,%rbx)
66418b1c1f	movw (%r15,%rbx),%bx
4989041f	movq %rax,(%r15,%rbx)
4d8b0c1f	movq (%r15,%rbx),%r9
49c7041f01000000	movq $1,(%r15,%rbx)
4d8d141f	leaq (%r15,%rbx),%r10
49813c1f00010000	cmpq $0x100,(%r15,%rbx)
49c7041f00000000 4:11:sym2+0	movq $sym2,(%r15,%rbx)
4983041fff	addq $-1,(%r15,%rbx)
41c6042f01	movb $1,(%r15,%rbp,1)
6641c7042f3412	movw $0x1234,(%r15,%rbp,1)
41c7042f78563412	movl $0x12345678,(%r15,%rbp,1)
4183042f04	addl $4,(%r15,%rbp,1)
41803c2f00	cmpb $0,(%r15,%rbp,1)
41ff042f	incl (%r15,%rbp,1)
41ff342f	pushq (%r15,%rbp,1)
41ff142f	call *(%r15,%rbp,1)
41c7042f00000000 4:10:sym2+0	movl $sym2,(%r15,%rbp,1)
41f7042f01000000	testl $1,(%r15,%rbp,1)
41c1242f03	shll $3,(%r15,%rbp,1)
416b042f64	imull $100,(%r15,%rbp,1),%eax
41dd042f	fldl (%r15,%rbp,1)
4188042f	movb %al,(%r15,%rbp,1)
66418b1c2f	movw (%r15,%rbp,1),%bx
4989042f	movq %rax,(%r15,%rbp,1)
4d8b0c2f	movq (%r15,%rbp,1),%r9
49c7042f01000000	movq $1,(%r15,%rbp,1)
4d8d142f	leaq (%r15,%rbp,1),%r10
49813c2f00010000	cmpq $0x100,(%r15,%rbp,1)
49c7042f00000000 4:11:sym2+0	movq $sym2,(%r15,%rbp,1)
4983042fff	addq $-1,(%r15,%rbp,1)
41c6047701	movb $1,(%r15,%rsi,2)
6641c704773412	movw $0x1234,(%r15,%rsi,2)
41c7047778563412	movl $0x12345678,(%r15,%rsi,2)
4183047704	addl $4,(%r15,%rsi,2)
41803c7700	cmpb $0,(%r15,%rsi,2)
41ff0477	incl (%r15,%rsi,2)
41ff3477	pushq (%r15,%rsi,2)
41ff1477	call *(%r15,%rsi,2)
41c7047700000000 4:10:sym2+0	movl $sym2,(%r15,%rsi,2)
41f7047701000000	testl $1,(%r15,%rsi,2)
41c1247703	shll $3,(%r15,%rsi,2)
416b047764	imull $100,(%r15,%rsi,2),%eax
41dd0477	fldl (%r15,%rsi,2)
41880477	movb %al,(%r15,%rsi,2)
66418b1c77	movw (%r15,%rsi,2),%bx
49890477	movq %rax,(%r15,%rsi,2)
4d8b0c77	movq (%r15,%rsi,2),%r9
49c7047701000000	movq $1,(%r15,%rsi,2)
4d8d1477	leaq (%r15,%rsi,2),%r10
49813c7700010000	cmpq $0x100,(%r15,%rsi,2)
49c7047700000000 4:11:sym2+0	movq $sym2,(%r15,%rsi,2)
49830477ff	addq $-1,(%r15,%rsi,2)
43c644a70401	movb $1,4(%r15,%r12,4)
6643c744a7043412	movw $0x1234,4(%r15,%r12,4)
43c744a70478563412	movl $0x12345678,4(%r15,%r12,4)
438344a70404	addl $4,4(%r15,%r12,4)
43807ca70400	cmpb $0,4(%r15,%r12,4)
43ff44a704	incl 4(%r15,%r12,4)
43ff74a704	pushq 4(%r15,%r12,4)
43ff54a704	call *4(%r15,%r12,4)
43c744a70400000000 5:10:sym2+0	movl $sym2,4(%r15,%r12,4)
43f744a70401000000	testl $1,4(%r15,%r12,4)
43c164a70403	shll $3,4(%r15,%r12,4)
436b44a70464	imull $100,4(%r15,%r12,4),%eax
43dd44a704	fldl 4(%r15,%r12,4)
438844a704	movb %al,4(%r15,%r12,4)
66438b5ca704	movw 4(%r15,%r12,4),%bx
4b8944a704	movq %rax,4(%r15,%r12,4)
4f8b4ca704	movq 4(%r15,%r12,4),%r9
4bc744a70401000000	movq $1,4(%r15,%r12,4)
4f8d54a704	leaq 4(%r15,%r12,4),%r10
4b817ca70400010000	cmpq $0x100,4(%r15,%r12,4)
4bc744a70400000000 5:11:sym2+0	movq $sym2,4(%r15,%r12,4)
4b8344a704ff	addq $-1,4(%r15,%r12,4)
43c684ef0010000001	movb $1,0x1000(%r15,%r13,8)
6643c784ef001000003412	movw $0x1234,0x1000(%r15,%r13,8)
43c784ef0010000078563412	movl $0x12345678,0x1000(%r15,%r13,8)
438384ef0010000004	addl $4,0x1000(%r15,%r13,8)
4380bcef0010000000	cmpb $0,0x1000(%r15,%r13,8)
43ff84ef00100000	incl 0x1000(%r15,%r13,8)
43ffb4ef00100000	pushq 0x1000(%r15,%r13,8)
43ff94ef00100000	call *0x1000(%r15,%r13,8)
43c784ef0010000000000000 8:10:sym2+0	movl $sym2,0x1000(%r15,%r13,8)
43f784ef0010000001000000	testl $1,0x1000(%r15,%r13,8)
43c1a4ef0010000003	shll $3,0x1000(%r15,%r13,8)
436b84ef0010000064	imull $100,0x1000(%r15,%r13,8),%eax
43dd84ef00100000	fldl 0x1000(%r15,%r13,8)
438884ef00100000	movb %al,0x1000(%r15,%r13,8)
66438b9cef00100000	movw 0x1000(%r15,%r13,8),%bx
4b8984ef00100000	movq %rax,0x1000(%r15,%r13,8)
4f8b8cef00100000	movq 0x1000(%r15,%r13,8),%r9
4bc784ef0010000001000000	movq $1,0x1000(%r15,%r13,8)
4f8d94ef00100000	leaq 0x1000(%r15,%r13,8),%r10
4b81bcef0010000000010000	cmpq $0x100,0x1000(%r15,%r13,8)
4bc784ef0010000000000000 8:11:sym2+0	movq $sym2,0x1000(%r15,%r13,8)
4b8384ef00100000ff	addq $-1,0x1000(%r15,%r13,8)
43c684ff0000000001 4:11:sym+0	movb $1,sym(%r15,%r15,8)
6643c784ff000000003412 5:11:sym+0	movw $0x1234,sym(%r15,%r15,8)
43c784ff0000000078563412 4:11:sym+0	movl $0x12345678,sym(%r15,%r15,8)
438384ff0000000004 4:11:sym+0	addl $4,sym(%r15,%r15,8)
4380bcff0000000000 4:11:sym+0	cmpb $0,sym(%r15,%r15,8)
43ff84ff00000000 4:11:sym+0	incl sym(%r15,%r15,8)
43ffb4ff00000000 4:11:sym+0	pushq sym(%r15,%r15,8)
43ff94ff00000000 4:11:sym+0	call *sym(%r15,%r15,8)
43c784ff0000000000000000 4:11:sym+0 8:10:sym2+0	movl $sym2,sym(%r15,%r15,8)
43f784ff0000000001000000 4:11:sym+0	testl $1,sym(%r15,%r15,8)
43c1a4ff0000000003 4:11:sym+0	shll $3,sym(%r15,%r15,8)
436b84ff0000000064 4:11:sym+0	imull $100,sym(%r15,%r15,8),%eax
43dd84ff00000000 4:11:sym+0	fldl sym(%r15,%r15,8)
438884ff00000000 4:11:sym+0	movb %al,sym(%r15,%r15,8)
66438b9cff00000000 5:11:sym+0	movw sym(%r15,%r15,8),%bx
4b8984ff00000000 4:11:sym+0	movq %rax,sym(%r15,%r15,8)
4f8b8cff00000000 4:11:sym+0	movq sym(%r15,%r15,8),%r9
4bc784ff0000000001000000 4:11:sym+0	movq $1,sym(%r15,%r15,8)
4f8d94ff00000000 4:11:sym+0	leaq sym(%r15,%r15,8),%r10
4b81bcff0000000000010000 4:11:sym+0	cmpq $0x100,sym(%r15,%r15,8)
4bc784ff0000000000000000 4:11:sym+0 8:11:sym2+0	movq $sym2,sym(%r15,%r15,8)
4b8384ff00000000ff 4:11:sym+0	addq $-1,sym(%r15,%r15,8)
c6045d0800000001	movb $1,8(,%rbx,2)
66c7045d080000003412	movw $0x1234,8(,%rbx,2)
c7045d0800000078563412	movl $0x12345678,8(,%rbx,2)
83045d0800000004	addl $4,8(,%rbx,2)
803c5d0800000000	cmpb $0,8(,%rbx,2)
ff045d08000000	incl 8(,%rbx,2)
ff345d08000000	pushq 8(,%rbx,2)
ff145d08000000	call *8(,%rbx,2)
c7045d0800000000000000 7:10:sym2+0	movl $sym2,8(,%rbx,2)
f7045d0800000001000000	testl $1,8(,%rbx,2)
c1245d0800000003	shll $3,8(,%rbx,2)
6b045d0800000064	imull $100,8(,%rbx,2),%eax
dd045d08000000	fldl 8(,%rbx,2)
88045d08000000	movb %al,8(,%rbx,2)
668b1c5d08000000	movw 8(,%rbx,2),%bx
4889045d08000000	movq %rax,8(,%rbx,2)
4c8b0c5d08000000	movq 8(,%rbx,2),%r9
48c7045d0800000001000000	movq $1,8(,%rbx,2)
4c8d145d08000000	leaq 8(,%rbx,2),%r10
48813c5d0800000000010000	cmpq $0x100,8(,%rbx,2)
48c7045d0800000000000000 8:11:sym2+0	movq $sym2,8(,%rbx,2)
4883045d08000000ff	addq $-1,8(,%rbx,2)
c6042d0000000001	movb $1,(,%rbp,1)
66c7042d000000003412	movw $0x1234,(,%rbp,1)
c7042d0000000078563412	movl $0x12345678,(,%rbp,1)
83042d0000000004	addl $4,(,%rbp,1)
803c2d0000000000	cmpb $0,(,%rbp,1)
ff042d00000000	incl (,%rbp,1)
ff342d00000000	pushq (,%rbp,1)
ff142d00000000	call *(,%rbp,1)
c7042d0000000000000000 7:10:sym2+0	movl $sym2,(,%rbp,1)
f7042d0000000001000000	testl $1,(,%rbp,1)
c1242d0000000003	shll $3,(,%rbp,1)
6b042d0000000064	imull $100,(,%rbp,1),%eax
dd042d00000000	fldl (,%rbp,1)
88042d00000000	movb %al,(,%rbp,1)
668b1c2d00000000	movw (,%rbp,1),%bx
4889042d00000000	movq %rax,(,%rbp,1)
4c8b0c2d00000000	movq (,%rbp,1),%r9
48c7042d0000000001000000	movq $1,(,%rbp,1)
4c8d142d00000000	leaq (,%rbp,1),%r10
48813c2d0000000000010000	cmpq $0x100,(,%rbp,1)
48c7042d0000000000000000 8:11:sym2+0	movq $sym2,(,%rbp,1)
4883042d00000000ff	addq $-1,(,%rbp,1)
42c604850000000001	movb $1,(,%r8,4)
6642c70485000000003412	movw $0x1234,(,%r8,4)
42c704850000000078563412	movl $0x12345678,(,%r8,4)
428304850000000004	addl $4,(,%r8,4)
42803c850000000000	cmpb $0,(,%r8,4)
42ff048500000000	incl (,%r8,4)
42ff348500000000	pushq (,%r8,4)
42ff148500000000	call *(,%r8,4)
42c704850000000000000000 8:10:sym2+0	movl $sym2,(,%r8,4)
42f704850000000001000000	testl $1,(,%r8,4)
42c124850000000003	shll $3,(,%r8,4)
426b04850000000064	imull $100,(,%r8,4),%eax
42dd048500000000	fldl (,%r8,4)
4288048500000000	movb %al,(,%r8,4)
66428b1c8500000000	movw (,%r8,4),%bx
4a89048500000000	movq %rax,(,%r8,4)
4e8b0c8500000000	movq (,%r8,4),%r9
4ac704850000000001000000	movq $1,(,%r8,4)
4e8d148500000000	leaq (,%r8,4),%r10
4a813c850000000000010000	cmpq $0x100,(,%r8,4)
4ac704850000000000000000 8:11:sym2+0	movq $sym2,(,%r8,4)
4a83048500000000ff	addq $-1,(,%r8,4)
42c604e50000000001 4:11:sym+0	movb $1,sym(,%r12,8)
6642c704e5000000003412 5:11:sym+0	movw $0x1234,sym(,%r12,8)
42c704e50000000078563412 4:11:sym+0	movl $0x12345678,sym(,%r12,8)
428304e50000000004 4:11:sym+0	addl $4,sym(,%r12,8)
42803ce50000000000 4:11:sym+0	cmpb $0,sym(,%r12,8)
42ff04e500000000 4:11:sym+0	incl sym(,%r12,8)
42ff34e500000000 4:11:sym+0	pushq sym(,%r12,8)
42ff14e500000000 4:11:sym+0	call *sym(,%r12,8)
42c704e50000000000000000 4:11:sym+0 8:10:sym2+0	movl $sym2,sym(,%r12,8)
42f704e50000000001000000 4:11:sym+0	testl $1,sym(,%r12,8)
42c124e50000000003 4:11:sym+0	shll $3,sym(,%r12,8)
426b04e50000000064 4:11:sym+0	imull $100,sym(,%r12,8),%eax
42dd04e500000000 4:11:sym+0	fldl sym(,%r12,8)
428804e500000000 4:11:sym+0	movb %al,sym(,%r12,8)
66428b1ce500000000 5:11:sym+0	movw sym(,%r12,8),%bx
4a8904e500000000 4:11:sym+0	movq %rax,sym(,%r12,8)
4e8b0ce500000000 4:11:sym+0	movq sym(,%r12,8),%r9
4ac704e50000000001000000 4:11:sym+0	movq $1,sym(,%r12,8)
4e8d14e500000000 4:11:sym+0	leaq sym(,%r12,8),%r10
4a813ce50000000000010000 4:11:sym+0	cmpq $0x100,sym(,%r12,8)
4ac704e50000000000000000 4:11:sym+0 8:11:sym2+0	movq $sym2,sym(,%r12,8)
4a8304e500000000ff 4:11:sym+0	addq $-1,sym(,%r12,8)
42c6042d0000000001	movb $1,(,%r13)
6642c7042d000000003412	movw $0x1234,(,%r13)
42c7042d0000000078563412	movl $0x12345678,(,%r13)
4283042d0000000004	addl $4,(,%r13)
42803c2d0000000000	cmpb $0,(,%r13)
42ff042d00000000	incl (,%r13)
42ff342d00000000	pushq (,%r13)
42ff142d00000000	call *(,%r13)
42c7042d0000000000000000 8:10:sym2+0	movl $sym2,(,%r13)
42f7042d0000000001000000	testl $1,(,%r13)
42c1242d0000000003	shll $3,(,%r13)
426b042d0000000064	imull $100,(,%r13),%eax
42dd042d00000000	fldl (,%r13)
4288042d00000000	movb %al,(,%r13)
66428b1c2d00000000	movw (,%r13),%bx
4a89042d00000000	movq %rax,(,%r13)
4e8b0c2d00000000	movq (,%r13),%r9
4ac7042d0000000001000000	movq $1,(,%r13)
4e8d142d00000000	leaq (,%r13),%r10
4a813c2d0000000000010000	cmpq $0x100,(,%r13)
4ac7042d0000000000000000 8:11:sym2+0	movq $sym2,(,%r13)
4a83042d00000000ff	addq $-1,(,%r13)
c604250000000001 3:11:sym+4	movb $1,sym+4
66c70425000000003412 4:11:sym+4	movw $0x1234,sym+4
c704250000000078563412 3:11:sym+4	movl $0x12345678,sym+4
8304250000000004 3:11:sym+4	addl $4,sym+4
803c250000000000 3:11:sym+4	cmpb $0,sym+4
ff042500000000 3:11:sym+4	incl sym+4
ff342500000000 3:11:sym+4	pushq sym+4
ff142500000000 3:11:sym+4	call *sym+4
c704250000000000000000 3:11:sym+4 7:10:sym2+0	movl $sym2,sym+4
f704250000000001000000 3:11:sym+4	testl $1,sym+4
c124250000000003 3:11:sym+4	shll $3,sym+4
6b04250000000064 3:11:sym+4	imull $100,sym+4,%eax
dd042500000000 3:11:sym+4	fldl sym+4
88042500000000 3:11:sym+4	movb %al,sym+4
668b1c2500000000 4:11:sym+4	movw sym+4,%bx
4889042500000000 4:11:sym+4	movq %rax,sym+4
4c8b0c2500000000 4:11:sym+4	movq sym+4,%r9
48c704250000000001000000 4:11:sym+4	movq $1,sym+4
4c8d142500000000 4:11:sym+4	leaq sym+4,%r10
48813c250000000000010000 4:11:sym+4	cmpq $0x100,sym+4
48c704250000000000000000 4:11:sym+4 8:11:sym2+0	movq $sym2,sym+4
4883042500000000ff 4:11:sym+4	addq $-1,sym+4
65c604251400000001	movb $1,%gs:20
6566c70425140000003412	movw $0x1234,%gs:20
65c704251400000078563412	movl $0x12345678,%gs:20
658304251400000004	addl $4,%gs:20
65803c251400000000	cmpb $0,%gs:20
65ff042514000000	incl %gs:20
65ff342514000000	pushq %gs:20
65ff142514000000	call *%gs:20
65c704251400000000000000 8:10:sym2+0	movl $sym2,%gs:20
65f704251400000001000000	testl $1,%gs:20
65c124251400000003	shll $3,%gs:20
656b04251400000064	imull $100,%gs:20,%eax
65dd042514000000	fldl %gs:20
6588042514000000	movb %al,%gs:20
65668b1c2514000000	movw %gs:20,%bx
654889042514000000	movq %rax,%gs:20
654c8b0c2514000000	movq %gs:20,%r9
6548c704251400000001000000	movq $1,%gs:20
654c8d142514000000	leaq %gs:20,%r10
6548813c251400000000010000	cmpq $0x100,%gs:20
6548c704251400000000000000 9:11:sym2+0	movq $sym2,%gs:20
654883042514000000ff	addq $-1,%gs:20
c6050000000001 2:2:sym-5	movb $1,sym(%rip)
66c705000000003412 3:2:sym-6	movw $0x1234,sym(%rip)
c7050000000078563412 2:2:sym-8	movl $0x12345678,sym(%rip)
83050000000004 2:2:sym-5	addl $4,sym(%rip)
803d0000000000 2:2:sym-5	cmpb $0,sym(%rip)
ff0500000000 2:2:sym-4	incl sym(%rip)
ff3500000000 2:2:sym-4	pushq sym(%rip)
ff1500000000 2:2:sym-4	call *sym(%rip)
c7050000000000000000 2:2:sym-8 6:10:sym2+0	movl $sym2,sym(%rip)
f7050000000001000000 2:2:sym-8	testl $1,sym(%rip)
c1250000000003 2:2:sym-5	shll $3,sym(%rip)
6b050000000064 2:2:sym-5	imull $100,sym(%rip),%eax
dd0500000000 2:2:sym-4	fldl sym(%rip)
880500000000 2:2:sym-4	movb %al,sym(%rip)
668b1d00000000 3:2:sym-4	movw sym(%rip),%bx
48890500000000 3:2:sym-4	movq %rax,sym(%rip)
4c8b0d00000000 3:2:sym-4	movq sym(%rip),%r9
48c7050000000001000000 3:2:sym-8	movq $1,sym(%rip)
4c8d1500000000 3:2:sym-4	leaq sym(%rip),%r10
48813d0000000000010000 3:2:sym-8	cmpq $0x100,sym(%rip)
48c7050000000000000000 3:2:sym-8 7:11:sym2+0	movq $sym2,sym(%rip)
48830500000000ff 3:2:sym-5	addq $-1,sym(%rip)
refused	movb $1,sym@TLSLD(%rip)
refused	movw $0x1234,sym@TLSLD(%rip)
refused	movl $0x12345678,sym@TLSLD(%rip)
refused	addl $4,sym@TLSLD(%rip)
refused	cmpb $0,sym@TLSLD(%rip)
refused	incl sym@TLSLD(%rip)
refused	pushq sym@TLSLD(%rip)
refused	call *sym@TLSLD(%rip)
refused	movl $sym2,sym@TLSLD(%rip)
refused	testl $1,sym@TLSLD(%rip)
refused	shll $3,sym@TLSLD(%rip)
refused	imull $100,sym@TLSLD(%rip),%eax
refused	fldl sym@TLSLD(%rip)
refused	movb %al,sym@TLSLD(%rip)
refused	movw sym@TLSLD(%rip),%bx
refused	movq %rax,sym@TLSLD(%rip)
refused	movq sym@TLSLD(%rip),%r9
refused	movq $1,sym@TLSLD(%rip)
refused	leaq sym@TLSLD(%rip),%r10
refused	cmpq $0x100,sym@TLSLD(%rip)
refused	movq $sym2,sym@TLSLD(%rip)
refused	addq $-1,sym@TLSLD(%rip)
67c6050000000001 3:2:sym-5	movb $1,sym(%eip)
6766c705000000003412 4:2:sym-6	movw $0x1234,sym(%eip)
67c7050000000078563412 3:2:sym-8	movl $0x12345678,sym(%eip)
6783050000000004 3:2:sym-5	addl $4,sym(%eip)
67803d0000000000 3:2:sym-5	cmpb $0,sym(%eip)
67ff0500000000 3:2:sym-4	incl sym(%eip)
67ff3500000000 3:2:sym-4	pushq sym(%eip)
67ff1500000000 3:2:sym-4	call *sym(%eip)
67c7050000000000000000 3:2:sym-8 7:10:sym2+0	movl $sym2,sym(%eip)
67f7050000000001000000 3:2:sym-8	testl $1,sym(%eip)
67c1250000000003 3:2:sym-5	shll $3,sym(%eip)
676b050000000064 3:2:sym-5	imull $100,sym(%eip),%eax
67dd0500000000 3:2:sym-4	fldl sym(%eip)
67880500000000 3:2:sym-4	movb %al,sym(%eip)
67668b1d00000000 4:2:sym-4	movw sym(%eip),%bx
6748890500000000 4:2:sym-4	movq %rax,sym(%eip)
674c8b0d00000000 4:2:sym-4	movq sym(%eip),%r9
6748c7050000000001000000 4:2:sym-8	movq $1,sym(%eip)
674c8d1500000000 4:2:sym-4	leaq sym(%eip),%r10
6748813d0000000000010000 4:2:sym-8	cmpq $0x100,sym(%eip)
6748c7050000000000000000 4:2:sym-8 8:11:sym2+0	movq $sym2,sym(%eip)
6748830500000000ff 4:2:sym-5	addq $-1,sym(%eip)
refused	movb $1,(%esp,%esp)
refused	movw $0x1234,(%esp,%esp)
refused	movl $0x12345678,(%esp,%esp)
refused	addl $4,(%esp,%esp)
refused	cmpb $0,(%esp,%esp)
refused	incl (%esp,%esp)
refused	pushq (%esp,%esp)
refused	call *(%esp,%esp)
refused	movl $sym2,(%esp,%esp)
refused	testl $1,(%esp,%esp)
refused	shll $3,(%esp,%esp)
refused	imull $100,(%esp,%esp),%eax
refused	fldl (%esp,%esp)
refused	movb %al,(%esp,%esp)
refused	movw (%esp,%esp),%bx
refused	movq %rax,(%esp,%esp)
refused	movq (%esp,%esp),%r9
refused	movq $1,(%esp,%esp)
refused	leaq (%esp,%esp),%r10
refused	cmpq $0x100,(%esp,%esp)
refused	movq $sym2,(%esp,%esp)
refused	addq $-1,(%esp,%esp)
refused	movb $1,(%eax,%esp)
refused	movw $0x1234,(%eax,%esp)
refused	movl $0x12345678,(%eax,%esp)
refused	addl $4,(%eax,%esp)
refused	cmpb $0,(%eax,%esp)
refused	incl (%eax,%esp)
refused	pushq (%eax,%esp)
refused	call *(%eax,%esp)
refused	movl $sym2,(%eax,%esp)
refused	testl $1,(%eax,%esp)
refused	shll $3,(%eax,%esp)
refused	imull $100,(%eax,%esp),%eax
refused	fldl (%eax,%esp)
refused	movb %al,(%eax,%esp)
refused	movw (%eax,%esp),%bx
refused	movq %rax,(%eax,%esp)
refused	movq (%eax,%esp),%r9
refused	movq $1,(%eax,%esp)
refused	leaq (%eax,%esp),%r10
refused	cmpq $0x100,(%eax,%esp)
refused	movq $sym2,(%eax,%esp)
refused	addq $-1,(%eax,%esp)
refused	movb $1,(%eax,%ebx,3)
refused	movw $0x1234,(%eax,%ebx,3)
refused	movl $0x12345678,(%eax,%ebx,3)
refused	addl $4,(%eax,%ebx,3)
refused	cmpb $0,(%eax,%ebx,3)
refused	incl (%eax,%ebx,3)
refused	pushq (%eax,%ebx,3)
refused	call *(%eax,%ebx,3)
refused	movl $sym2,(%eax,%ebx,3)
refused	testl $1,(%eax,%ebx,3)
refused	shll $3,(%eax,%ebx,3)
refused	imull $100,(%eax,%ebx,3),%eax
refused	fldl (%eax,%ebx,3)
refused	movb %al,(%eax,%ebx,3)
refused	movw (%eax,%ebx,3),%bx
refused	movq %rax,(%eax,%ebx,3)
refused	movq (%eax,%ebx,3),%r9
refused	movq $1,(%eax,%ebx,3)
refused	leaq (%eax,%ebx,3),%r10
refused	cmpq $0x100,(%eax,%ebx,3)
refused	movq $sym2,(%eax,%ebx,3)
refused	addq $-1,(%eax,%ebx,3)
refused	movb $1,(%eax,%ebx,16)
refused	movw $0x1234,(%eax,%ebx,16)
refused	movl $0x12345678,(%eax,%ebx,16)
refused	addl $4,(%eax,%ebx,16)
refused	cmpb $0,(%eax,%ebx,16)
refused	incl (%eax,%ebx,16)
refused	pushq (%eax,%ebx,16)
refused	call *(%eax,%ebx,16)
refused	movl $sym2,(%eax,%ebx,16)
refused	testl $1,(%eax,%ebx,16)
refused	shll $3,(%eax,%ebx,16)
refused	imull $100,(%eax,%ebx,16),%eax
refused	fldl (%eax,%ebx,16)
refused	movb %al,(%eax,%ebx,16)
refused	movw (%eax,%ebx,16),%bx
refused	movq %rax,(%eax,%ebx,16)
refused	movq (%eax,%ebx,16),%r9
refused	movq $1,(%eax,%ebx,16)
refused	leaq (%eax,%ebx,16),%r10
refused	cmpq $0x100,(%eax,%ebx,16)
refused	movq $sym2,(%eax,%ebx,16)
refused	addq $-1,(%eax,%ebx,16)
refused	movb $1,(%ax,%bx)
refused	movw $0x1234,(%ax,%bx)
refused	movl $0x12345678,(%ax,%bx)
refused	addl $4,(%ax,%bx)
refused	cmpb $0,(%ax,%bx)
refused	incl (%ax,%bx)
refused	pushq (%ax,%bx)
refused	call *(%ax,%bx)
refused	movl $sym2,(%ax,%bx)
refused	testl $1,(%ax,%bx)
refused	shll $3,(%ax,%bx)
refused	imull $100,(%ax,%bx),%eax
refused	fldl (%ax,%bx)
refused	movb %al,(%ax,%bx)
refused	movw (%ax,%bx),%bx
refused	movq %rax,(%ax,%bx)
refused	movq (%ax,%bx),%r9
refused	movq $1,(%ax,%bx)
refused	leaq (%ax,%bx),%r10
refused	cmpq $0x100,(%ax,%bx)
refused	movq $sym2,(%ax,%bx)
refused	addq $-1,(%ax,%bx)
refused	movb $1,(%eax,%bx)
refused	movw $0x1234,(%eax,%bx)
refused	movl $0x12345678,(%eax,%bx)
refused	addl $4,(%eax,%bx)
refused	cmpb $0,(%eax,%bx)
refused	incl (%eax,%bx)
refused	pushq (%eax,%bx)
refused	call *(%eax,%bx)
refused	movl $sym2,(%eax,%bx)
refused	testl $1,(%eax,%bx)
refused	shll $3,(%eax,%bx)
refused	imull $100,(%eax,%bx),%eax
refused	fldl (%eax,%bx)
refused	movb %al,(%eax,%bx)
refused	movw (%eax,%bx),%bx
refused	movq %rax,(%eax,%bx)
refused	movq (%eax,%bx),%r9
refused	movq $1,(%eax,%bx)
refused	leaq (%eax,%bx),%r10
refused	cmpq $0x100,(%eax,%bx)
refused	movq $sym2,(%eax,%bx)
refused	addq $-1,(%eax,%bx)
refused	movb $1,(%al)
refused	movw $0x1234,(%al)
refused	movl $0x12345678,(%al)
refused	addl $4,(%al)
refused	cmpb $0,(%al)
refused	incl (%al)
refused	pushq (%al)
refused	call *(%al)
refused	movl $sym2,(%al)
refused	testl $1,(%al)
refused	shll $3,(%al)
refused	imull $100,(%al),%eax
refused	fldl (%al)
refused	movb %al,(%al)
refused	movw (%al),%bx
refused	movq %rax,(%al)
refused	movq (%al),%r9
refused	movq $1,(%al)
refused	leaq (%al),%r10
refused	cmpq $0x100,(%al)
refused	movq $sym2,(%al)
refused	addq $-1,(%al)
refused	movb $1,(%eax,%eax,0)
refused	movw $0x1234,(%eax,%eax,0)
refused	movl $0x12345678,(%eax,%eax,0)
refused	addl $4,(%eax,%eax,0)
refused	cmpb $0,(%eax,%eax,0)
refused	incl (%eax,%eax,0)
refused	pushq (%eax,%eax,0)
refused	call *(%eax,%eax,0)
refused	movl $sym2,(%eax,%eax,0)
refused	testl $1,(%eax,%eax,0)
refused	shll $3,(%eax,%eax,0)
refused	imull $100,(%eax,%eax,0),%eax
refused	fldl (%eax,%eax,0)
refused	movb %al,(%eax,%eax,0)
refused	movw (%eax,%eax,0),%bx
refused	movq %rax,(%eax,%eax,0)
refused	movq (%eax,%eax,0),%r9
refused	movq $1,(%eax,%eax,0)
refused	leaq (%eax,%eax,0),%r10
refused	cmpq $0x100,(%eax,%eax,0)
refused	movq $sym2,(%eax,%eax,0)
refused	addq $-1,(%eax,%eax,0)
refused	movb $1,(%bx,%bp)
refused	movw $0x1234,(%bx,%bp)
refused	movl $0x12345678,(%bx,%bp)
refused	addl $4,(%bx,%bp)
refused	cmpb $0,(%bx,%bp)
refused	incl (%bx,%bp)
refused	pushq (%bx,%bp)
refused	call *(%bx,%bp)
refused	movl $sym2,(%bx,%bp)
refused	testl $1,(%bx,%bp)
refused	shll $3,(%bx,%bp)
refused	imull $100,(%bx,%bp),%eax
refused	fldl (%bx,%bp)
refused	movb %al,(%bx,%bp)
refused	movw (%bx,%bp),%bx
refused	movq %rax,(%bx,%bp)
refused	movq (%bx,%bp),%r9
refused	movq $1,(%bx,%bp)
refused	leaq (%bx,%bp),%r10
refused	cmpq $0x100,(%bx,%bp)
refused	movq $sym2,(%bx,%bp)
refused	addq $-1,(%bx,%bp)
refused	movb $1,(%si,%di)
refused	movw $0x1234,(%si,%di)
refused	movl $0x12345678,(%si,%di)
refused	addl $4,(%si,%di)
refused	cmpb $0,(%si,%di)
refused	incl (%si,%di)
refused	pushq (%si,%di)
refused	call *(%si,%di)
refused	movl $sym2,(%si,%di)
refused	testl $1,(%si,%di)
refused	shll $3,(%si,%di)
refused	imull $100,(%si,%di),%eax
refused	fldl (%si,%di)
refused	movb %al,(%si,%di)
refused	movw (%si,%di),%bx
refused	movq %rax,(%si,%di)
refused	movq (%si,%di),%r9
refused	movq $1,(%si,%di)
refused	leaq (%si,%di),%r10
refused	cmpq $0x100,(%si,%di)
refused	movq $sym2,(%si,%di)
refused	addq $-1,(%si,%di)
refused	movb $1,(%cx)
refused	movw $0x1234,(%cx)
refused	movl $0x12345678,(%cx)
refused	addl $4,(%cx)
refused	cmpb $0,(%cx)
refused	incl (%cx)
refused	pushq (%cx)
refused	call *(%cx)
refused	movl $sym2,(%cx)
refused	testl $1,(%cx)
refused	shll $3,(%cx)
refused	imull $100,(%cx),%eax
refused	fldl (%cx)
refused	movb %al,(%cx)
refused	movw (%cx),%bx
refused	movq %rax,(%cx)
refused	movq (%cx),%r9
refused	movq $1,(%cx)
refused	leaq (%cx),%r10
refused	cmpq $0x100,(%cx)
refused	movq $sym2,(%cx)
refused	addq $-1,(%cx)
refused	movb $1,(%eax,%ebx,4,5)
refused	movw $0x1234,(%eax,%ebx,4,5)
refused	movl $0x12345678,(%eax,%ebx,4,5)
refused	addl $4,(%eax,%ebx,4,5)
refused	cmpb $0,(%eax,%ebx,4,5)
refused	incl (%eax,%ebx,4,5)
refused	pushq (%eax,%ebx,4,5)
refused	call *(%eax,%ebx,4,5)
refused	movl $sym2,(%eax,%ebx,4,5)
refused	testl $1,(%eax,%ebx,4,5)
refused	shll $3,(%eax,%ebx,4,5)
refused	imull $100,(%eax,%ebx,4,5),%eax
refused	fldl (%eax,%ebx,4,5)
refused	movb %al,(%eax,%ebx,4,5)
refused	movw (%eax,%ebx,4,5),%bx
refused	movq %rax,(%eax,%ebx,4,5)
refused	movq (%eax,%ebx,4,5),%r9
refused	movq $1,(%eax,%ebx,4,5)
refused	leaq (%eax,%ebx,4,5),%r10
refused	cmpq $0x100,(%eax,%ebx,4,5)
refused	movq $sym2,(%eax,%ebx,4,5)
refused	addq $-1,(%eax,%ebx,4,5)
refused	movb $1,(%cr0)
refused	movw $0x1234,(%cr0)
refused	movl $0x12345678,(%cr0)
refused	addl $4,(%cr0)
refused	cmpb $0,(%cr0)
refused	incl (%cr0)
refused	pushq (%cr0)
refused	call *(%cr0)
refused	movl $sym2,(%cr0)
refused	testl $1,(%cr0)
refused	shll $3,(%cr0)
refused	imull $100,(%cr0),%eax
refused	fldl (%cr0)
refused	movb %al,(%cr0)
refused	movw (%cr0),%bx
refused	movq %rax,(%cr0)
refused	movq (%cr0),%r9
refused	movq $1,(%cr0)
refused	leaq (%cr0),%r10
refused	cmpq $0x100,(%cr0)
refused	movq $sym2,(%cr0)
refused	addq $-1,(%cr0)
refused	movb $1,(%eax,%xmm0)
refused	movw $0x1234,(%eax,%xmm0)
refused	movl $0x12345678,(%eax,%xmm0)
refused	addl $4,(%eax,%xmm0)
refused	cmpb $0,(%eax,%xmm0)
refused	incl (%eax,%xmm0)
refused	pushq (%eax,%xmm0)
refused	call *(%eax,%xmm0)
refused	movl $sym2,(%eax,%xmm0)
refused	testl $1,(%eax,%xmm0)
refused	shll $3,(%eax,%xmm0)
refused	imull $100,(%eax,%xmm0),%eax
refused	fldl (%eax,%xmm0)
refused	movb %al,(%eax,%xmm0)
refused	movw (%eax,%xmm0),%bx
refused	movq %rax,(%eax,%xmm0)
refused	movq (%eax,%xmm0),%r9
refused	movq $1,(%eax,%xmm0)
refused	leaq (%eax,%xmm0),%r10
refused	cmpq $0x100,(%eax,%xmm0)
refused	movq $sym2,(%eax,%xmm0)
refused	addq $-1,(%eax,%xmm0)
refused	movb $1,(%rsp,%rsp)
refused	movw $0x1234,(%rsp,%rsp)
refused	movl $0x12345678,(%rsp,%rsp)
refused	addl $4,(%rsp,%rsp)
refused	cmpb $0,(%rsp,%rsp)
refused	incl (%rsp,%rsp)
refused	pushq (%rsp,%rsp)
refused	call *(%rsp,%rsp)
refused	movl $sym2,(%rsp,%rsp)
refused	testl $1,(%rsp,%rsp)
refused	shll $3,(%rsp,%rsp)
refused	imull $100,(%rsp,%rsp),%eax
refused	fldl (%rsp,%rsp)
refused	movb %al,(%rsp,%rsp)
refused	movw (%rsp,%rsp),%bx
refused	movq %rax,(%rsp,%rsp)
refused	movq (%rsp,%rsp),%r9
refused	movq $1,(%rsp,%rsp)
refused	leaq (%rsp,%rsp),%r10
refused	cmpq $0x100,(%rsp,%rsp)
refused	movq $sym2,(%rsp,%rsp)
refused	addq $-1,(%rsp,%rsp)
refused	movb $1,(%rax,%rsp,2)
refused	movw $0x1234,(%rax,%rsp,2)
refused	movl $0x12345678,(%rax,%rsp,2)
refused	addl $4,(%rax,%rsp,2)
refused	cmpb $0,(%rax,%rsp,2)
refused	incl (%rax,%rsp,2)
refused	pushq (%rax,%rsp,2)
refused	call *(%rax,%rsp,2)
refused	movl $sym2,(%rax,%rsp,2)
refused	testl $1,(%rax,%rsp,2)
refused	shll $3,(%rax,%rsp,2)
refused	imull $100,(%rax,%rsp,2),%eax
refused	fldl (%rax,%rsp,2)
refused	movb %al,(%rax,%rsp,2)
refused	movw (%rax,%rsp,2),%bx
refused	movq %rax,(%rax,%rsp,2)
refused	movq (%rax,%rsp,2),%r9
refused	movq $1,(%rax,%rsp,2)
refused	leaq (%rax,%rsp,2),%r10
refused	cmpq $0x100,(%rax,%rsp,2)
refused	movq $sym2,(%rax,%rsp,2)
refused	addq $-1,(%rax,%rsp,2)
refused	movb $1,(%rax,%ebx)
refused	movw $0x1234,(%rax,%ebx)
refused	movl $0x12345678,(%rax,%ebx)
refused	addl $4,(%rax,%ebx)
refused	cmpb $0,(%rax,%ebx)
refused	incl (%rax,%ebx)
refused	pushq (%rax,%ebx)
refused	call *(%rax,%ebx)
refused	movl $sym2,(%rax,%ebx)
refused	testl $1,(%rax,%ebx)
refused	shll $3,(%rax,%ebx)
refused	imull $100,(%rax,%ebx),%eax
refused	fldl (%rax,%ebx)
refused	movb %al,(%rax,%ebx)
refused	movw (%rax,%ebx),%bx
refused	movq %rax,(%rax,%ebx)
refused	movq (%rax,%ebx),%r9
refused	movq $1,(%rax,%ebx)
refused	leaq (%rax,%ebx),%r10
refused	cmpq $0x100,(%rax,%ebx)
refused	movq $sym2,(%rax,%ebx)
refused	addq $-1,(%rax,%ebx)
refused	movb $1,(%eax,%rbx)
refused	movw $0x1234,(%eax,%rbx)
refused	movl $0x12345678,(%eax,%rbx)
refused	addl $4,(%eax,%rbx)
refused	cmpb $0,(%eax,%rbx)
refused	incl (%eax,%rbx)
refused	pushq (%eax,%rbx)
refused	call *(%eax,%rbx)
refused	movl $sym2,(%eax,%rbx)
refused	testl $1,(%eax,%rbx)
refused	shll $3,(%eax,%rbx)
refused	imull $100,(%eax,%rbx),%eax
refused	fldl (%eax,%rbx)
refused	movb %al,(%eax,%rbx)
refused	movw (%eax,%rbx),%bx
refused	movq %rax,(%eax,%rbx)
refused	movq (%eax,%rbx),%r9
refused	movq $1,(%eax,%rbx)
refused	leaq (%eax,%rbx),%r10
refused	cmpq $0x100,(%eax,%rbx)
refused	movq $sym2,(%eax,%rbx)
refused	addq $-1,(%eax,%rbx)
refused	movb $1,(%rip,%rax)
refused	movw $0x1234,(%rip,%rax)
refused	movl $0x12345678,(%rip,%rax)
refused	addl $4,(%rip,%rax)
refused	cmpb $0,(%rip,%rax)
refused	incl (%rip,%rax)
refused	pushq (%rip,%rax)
refused	call *(%rip,%rax)
refused	movl $sym2,(%rip,%rax)
refused	testl $1,(%rip,%rax)
refused	shll $3,(%rip,%rax)
refused	imull $100,(%rip,%rax),%eax
refused	fldl (%rip,%rax)
refused	movb %al,(%rip,%rax)
refused	movw (%rip,%rax),%bx
refused	movq %rax,(%rip,%rax)
refused	movq (%rip,%rax),%r9
refused	movq $1,(%rip,%rax)
refused	leaq (%rip,%rax),%r10
refused	cmpq $0x100,(%rip,%rax)
refused	movq $sym2,(%rip,%rax)
refused	addq $-1,(%rip,%rax)
refused	movb $1,(%rax,%rip)
refused	movw $0x1234,(%rax,%rip)
refused	movl $0x12345678,(%rax,%rip)
refused	addl $4,(%rax,%rip)
refused	cmpb $0,(%rax,%rip)
refused	incl (%rax,%rip)
refused	pushq (%rax,%rip)
refused	call *(%rax,%rip)
refused	movl $sym2,(%rax,%rip)
refused	testl $1,(%rax,%rip)
refused	shll $3,(%rax,%rip)
refused	imull $100,(%rax,%rip),%eax
refused	fldl (%rax,%rip)
refused	movb %al,(%rax,%rip)
refused	movw (%rax,%rip),%bx
refused	movq %rax,(%rax,%rip)
refused	movq (%rax,%rip),%r9
refused	movq $1,(%rax,%rip)
refused	leaq (%rax,%rip),%r10
refused	cmpq $0x100,(%rax,%rip)
refused	movq $sym2,(%rax,%rip)
refused	addq $-1,(%rax,%rip)
refused	movb $1,sym(%rip,%rax,2)
refused	movw $0x1234,sym(%rip,%rax,2)
refused	movl $0x12345678,sym(%rip,%rax,2)
refused	addl $4,sym(%rip,%rax,2)
refused	cmpb $0,sym(%rip,%rax,2)
refused	incl sym(%rip,%rax,2)
refused	pushq sym(%rip,%rax,2)
refused	call *sym(%rip,%rax,2)
refused	movl $sym2,sym(%rip,%rax,2)
refused	testl $1,sym(%rip,%rax,2)
refused	shll $3,sym(%rip,%rax,2)
refused	imull $100,sym(%rip,%rax,2),%eax
refused	fldl sym(%rip,%rax,2)
refused	movb %al,sym(%rip,%rax,2)
refused	movw sym(%rip,%rax,2),%bx
refused	movq %rax,sym(%rip,%rax,2)
refused	movq sym(%rip,%rax,2),%r9
refused	movq $1,sym(%rip,%rax,2)
refused	leaq sym(%rip,%rax,2),%r10
refused	cmpq $0x100,sym(%rip,%rax,2)
refused	movq $sym2,sym(%rip,%rax,2)
refused	addq $-1,sym(%rip,%rax,2)
refused	movb $1,(%bx)
refused	movw $0x1234,(%bx)
refused	movl $0x12345678,(%bx)
refused	addl $4,(%bx)
refused	cmpb $0,(%bx)
refused	incl (%bx)
refused	pushq (%bx)
refused	call *(%bx)
refused	movl $sym2,(%bx)
refused	testl $1,(%bx)
refused	shll $3,(%bx)
refused	imull $100,(%bx),%eax
refused	fldl (%bx)
refused	movb %al,(%bx)
refused	movw (%bx),%bx
refused	movq %rax,(%bx)
refused	movq (%bx),%r9
refused	movq $1,(%bx)
refused	leaq (%bx),%r10
refused	cmpq $0x100,(%bx)
refused	movq $sym2,(%bx)
refused	addq $-1,(%bx)
refused	movb $1,(%bx,%si)
refused	movw $0x1234,(%bx,%si)
refused	movl $0x12345678,(%bx,%si)
refused	addl $4,(%bx,%si)
refused	cmpb $0,(%bx,%si)
refused	incl (%bx,%si)
refused	pushq (%bx,%si)
refused	call *(%bx,%si)
refused	movl $sym2,(%bx,%si)
refused	testl $1,(%bx,%si)
refused	shll $3,(%bx,%si)
refused	imull $100,(%bx,%si),%eax
refused	fldl (%bx,%si)
refused	movb %al,(%bx,%si)
refused	movw (%bx,%si),%bx
refused	movq %rax,(%bx,%si)
refused	movq (%bx,%si),%r9
refused	movq $1,(%bx,%si)
refused	leaq (%bx,%si),%r10
refused	cmpq $0x100,(%bx,%si)
refused	movq $sym2,(%bx,%si)
refused	addq $-1,(%bx,%si)
refused	movb $1,0x123456789(%rax)
refused	movw $0x1234,0x123456789(%rax)
refused	movl $0x12345678,0x123456789(%rax)
refused	addl $4,0x123456789(%rax)
refused	cmpb $0,0x123456789(%rax)
refused	incl 0x123456789(%rax)
refused	pushq 0x123456789(%rax)
refused	call *0x123456789(%rax)
refused	movl $sym2,0x123456789(%rax)
refused	testl $1,0x123456789(%rax)
refused	shll $3,0x123456789(%rax)
refused	imull $100,0x123456789(%rax),%eax
refused	fldl 0x123456789(%rax)
refused	movb %al,0x123456789(%rax)
refused	movw 0x123456789(%rax),%bx
refused	movq %rax,0x123456789(%rax)
refused	movq 0x123456789(%rax),%r9
refused	movq $1,0x123456789(%rax)
refused	leaq 0x123456789(%rax),%r10
refused	cmpq $0x100,0x123456789(%rax)
refused	movq $sym2,0x123456789(%rax)
refused	addq $-1,0x123456789(%rax)
refused	movb $1,0x80000000(%rax)
refused	movw $0x1234,0x80000000(%rax)
refused	movl $0x12345678,0x80000000(%rax)
refused	addl $4,0x80000000(%rax)
refused	cmpb $0,0x80000000(%rax)
refused	incl 0x80000000(%rax)
refused	pushq 0x80000000(%rax)
refused	call *0x80000000(%rax)
refused	movl $sym2,0x80000000(%rax)
refused	testl $1,0x80000000(%rax)
refused	shll $3,0x80000000(%rax)
refused	imull $100,0x80000000(%rax),%eax
refused	fldl 0x80000000(%rax)
refused	movb %al,0x80000000(%rax)
refused	movw 0x80000000(%rax),%bx
refused	movq %rax,0x80000000(%rax)
refused	movq 0x80000000(%rax),%r9
refused	movq $1,0x80000000(%rax)
refused	leaq 0x80000000(%rax),%r10
refused	cmpq $0x100,0x80000000(%rax)
refused	movq $sym2,0x80000000(%rax)
refused	addq $-1,0x80000000(%rax)
refused	movb $1,-0x80000001(%rax)
refused	movw $0x1234,-0x80000001(%rax)
refused	movl $0x12345678,-0x80000001(%rax)
refused	addl $4,-0x80000001(%rax)
refused	cmpb $0,-0x80000001(%rax)
refused	incl -0x80000001(%rax)
refused	pushq -0x80000001(%rax)
refused	call *-0x80000001(%rax)
refused	movl $sym2,-0x80000001(%rax)
refused	testl $1,-0x80000001(%rax)
refused	shll $3,-0x80000001(%rax)
refused	imull $100,-0x80000001(%rax),%eax
refused	fldl -0x80000001(%rax)
refused	movb %al,-0x80000001(%rax)
refused	movw -0x80000001(%rax),%bx
refused	movq %rax,-0x80000001(%rax)
refused	movq -0x80000001(%rax),%r9
refused	movq $1,-0x80000001(%rax)
refused	leaq -0x80000001(%rax),%r10
refused	cmpq $0x100,-0x80000001(%rax)
refused	movq $sym2,-0x80000001(%rax)
refused	addq $-1,-0x80000001(%rax)
refused	movb $1,0xffffffff(%rax)
refused	movw $0x1234,0xffffffff(%rax)
refused	movl $0x12345678,0xffffffff(%rax)
refused	addl $4,0xffffffff(%rax)
refused	cmpb $0,0xffffffff(%rax)
refused	incl 0xffffffff(%rax)
refused	pushq 0xffffffff(%rax)
refused	call *0xffffffff(%rax)
refused	movl $sym2,0xffffffff(%rax)
refused	testl $1,0xffffffff(%rax)
refused	shll $3,0xffffffff(%rax)
refused	imull $100,0xffffffff(%rax),%eax
refused	fldl 0xffffffff(%rax)
refused	movb %al,0xffffffff(%rax)
refused	movw 0xffffffff(%rax),%bx
refused	movq %rax,0xffffffff(%rax)
refused	movq 0xffffffff(%rax),%r9
refused	movq $1,0xffffffff(%rax)
refused	leaq 0xffffffff(%rax),%r10
refused	cmpq $0x100,0xffffffff(%rax)
refused	movq $sym2,0xffffffff(%rax)
refused	addq $-1,0xffffffff(%rax)
d510c60001	movb $1,(%r16)
66d510c7003412	movw $0x1234,(%r16)
d510c70078563412	movl $0x12345678,(%r16)
d510830004	addl $4,(%r16)
d510803800	cmpb $0,(%r16)
d510ff00	incl (%r16)
d510ff30	pushq (%r16)
d510ff10	call *(%r16)
d510c70000000000 4:10:sym2+0	movl $sym2,(%r16)
d510f70001000000	testl $1,(%r16)
d510c12003	shll $3,(%r16)
d5106b0064	imull $100,(%r16),%eax
d510dd00	fldl (%r16)
d5108800	movb %al,(%r16)
66d5108b18	movw (%r16),%bx
d5188900	movq %rax,(%r16)
d51c8b08	movq (%r16),%r9
d518c70001000000	movq $1,(%r16)
d51c8d10	leaq (%r16),%r10
d518813800010000	cmpq $0x100,(%r16)
d518c70000000000 4:11:sym2+0	movq $sym2,(%r16)
d5188300ff	addq $-1,(%r16)
refused	movb $1,(%rax,%rbx,3)
refused	movw $0x1234,(%rax,%rbx,3)
refused	movl $0x12345678,(%rax,%rbx,3)
refused	addl $4,(%rax,%rbx,3)
refused	cmpb $0,(%rax,%rbx,3)
refused	incl (%rax,%rbx,3)
refused	pushq (%rax,%rbx,3)
refused	call *(%rax,%rbx,3)
refused	movl $sym2,(%rax,%rbx,3)
refused	testl $1,(%rax,%rbx,3)
refused	shll $3,(%rax,%rbx,3)
refused	imull $100,(%rax,%rbx,3),%eax
refused	fldl (%rax,%rbx,3)
refused	movb %al,(%rax,%rbx,3)
refused	movw (%rax,%rbx,3),%bx
refused	movq %rax,(%rax,%rbx,3)
refused	movq (%rax,%rbx,3),%r9
refused	movq $1,(%rax,%rbx,3)
refused	leaq (%rax,%rbx,3),%r10
refused	cmpq $0x100,(%rax,%rbx,3)
refused	movq $sym2,(%rax,%rbx,3)
refused	addq $-1,(%rax,%rbx,3)
88c0	movb %al,%al
88c1	movb %al,%cl
88c2	movb %al,%dl
88c3	movb %al,%bl
88c4	movb %al,%ah
88c5	movb %al,%ch
88c6	movb %al,%dh
88c7	movb %al,%bh
4088c6	movb %al,%sil
4088c7	movb %al,%dil
4088c4	movb %al,%spl
4088c5	movb %al,%bpl
4188c0	movb %al,%r8b
4188c4	movb %al,%r12b
4188c7	movb %al,%r15b
88c8	movb %cl,%al
88c9	movb %cl,%cl
88ca	movb %cl,%dl
88cb	movb %cl,%bl
88cc	movb %cl,%ah
88cd	movb %cl,%ch
88ce	movb %cl,%dh
88cf	movb %cl,%bh
4088ce	movb %cl,%sil
4088cf	movb %cl,%dil
4088cc	movb %cl,%spl
4088cd	movb %cl,%bpl
4188c8	movb %cl,%r8b
4188cc	movb %cl,%r12b
4188cf	movb %cl,%r15b
88d0	movb %dl,%al
88d1	movb %dl,%cl
88d2	movb %dl,%dl
88d3	movb %dl,%bl
88d4	movb %dl,%ah
88d5	movb %dl,%ch
88d6	movb %dl,%dh
88d7	movb %dl,%bh
4088d6	movb %dl,%sil
4088d7	movb %dl,%dil
4088d4	movb %dl,%spl
4088d5	movb %dl,%bpl
4188d0	movb %dl,%r8b
4188d4	movb %dl,%r12b
4188d7	movb %dl,%r15b
88d8	movb %bl,%al
88d9	movb %bl,%cl
88da	movb %bl,%dl
88db	movb %bl,%bl
88dc	movb %bl,%ah
88dd	movb %bl,%ch
88de	movb %bl,%dh
88df	movb %bl,%bh
4088de	movb %bl,%sil
4088df	movb %bl,%dil
4088dc	movb %bl,%spl
4088dd	movb %bl,%bpl
4188d8	movb %bl,%r8b
4188dc	movb %bl,%r12b
4188df	movb %bl,%r15b
88e0	movb %ah,%al
88e1	movb %ah,%cl
88e2	movb %ah,%dl
88e3	movb %ah,%bl
88e4	movb %ah,%ah
88e5	movb %ah,%ch
88e6	movb %ah,%dh
88e7	movb %ah,%bh
refused	movb %ah,%sil
refused	movb %ah,%dil
refused	movb %ah,%spl
refused	movb %ah,%bpl
refused	movb %ah,%r8b
refused	movb %ah,%r12b
refused	movb %ah,%r15b
88e8	movb %ch,%al
88e9	movb %ch,%cl
88ea	movb %ch,%dl
88eb	movb %ch,%bl
88ec	movb %ch,%ah
88ed	movb %ch,%ch
88ee	movb %ch,%dh
88ef	movb %ch,%bh
refused	movb %ch,%sil
refused	movb %ch,%dil
refused	movb %ch,%spl
refused	movb %ch,%bpl
refused	movb %ch,%r8b
refused	movb %ch,%r12b
refused	movb %ch,%r15b
88f0	movb %dh,%al
88f1	movb %dh,%cl
88f2	movb %dh,%dl
88f3	movb %dh,%bl
88f4	movb %dh,%ah
88f5	movb %dh,%ch
88f6	movb %dh,%dh
88f7	movb %dh,%bh
refused	movb %dh,%sil
refused	movb %dh,%dil
refused	movb %dh,%spl
refused	movb %dh,%bpl
refused	movb %dh,%r8b
refused	movb %dh,%r12b
refused	movb %dh,%r15b
88f8	movb %bh,%al
88f9	movb %bh,%cl
88fa	movb %bh,%dl
88fb	movb %bh,%bl
88fc	movb %bh,%ah
88fd	movb %bh,%ch
88fe	movb %bh,%dh
88ff	movb %bh,%bh
refused	movb %bh,%sil
refused	movb %bh,%dil
refused	movb %bh,%spl
refused	movb %bh,%bpl
refused	movb %bh,%r8b
refused	movb %bh,%r12b
refused	movb %bh,%r15b
4088f0	movb %sil,%al
4088f1	movb %sil,%cl
4088f2	movb %sil,%dl
4088f3	movb %sil,%bl
refused	movb %sil,%ah
refused	movb %sil,%ch
refused	movb %sil,%dh
refused	movb %sil,%bh
4088f6	movb %sil,%sil
4088f7	movb %sil,%dil
4088f4	movb %sil,%spl
4088f5	movb %sil,%bpl
4188f0	movb %sil,%r8b
4188f4	movb %sil,%r12b
4188f7	movb %sil,%r15b
4088f8	movb %dil,%al
4088f9	movb %dil,%cl
4088fa	movb %dil,%dl
4088fb	movb %dil,%bl
refused	movb %dil,%ah
refused	movb %dil,%ch
refused	movb %dil,%dh
refused	movb %dil,%bh
4088fe	movb %dil,%sil
4088ff	movb %dil,%dil
4088fc	movb %dil,%spl
4088fd	movb %dil,%bpl
4188f8	movb %dil,%r8b
4188fc	movb %dil,%r12b
4188ff	movb %dil,%r15b
4088e0	movb %spl,%al
4088e1	movb %spl,%cl
4088e2	movb %spl,%dl
4088e3	movb %spl,%bl
refused	movb %spl,%ah
refused	movb %spl,%ch
refused	movb %spl,%dh
refused	movb %spl,%bh
4088e6	movb %spl,%sil
4088e7	movb %spl,%dil
4088e4	movb %spl,%spl
4088e5	movb %spl,%bpl
4188e0	movb %spl,%r8b
4188e4	movb %spl,%r12b
4188e7	movb %spl,%r15b
4088e8	movb %bpl,%al
4088e9	movb %bpl,%cl
4088ea	movb %bpl,%dl
4088eb	movb %bpl,%bl
refused	movb %bpl,%ah
refused	movb %bpl,%ch
refused	movb %bpl,%dh
refused	movb %bpl,%bh
4088ee	movb %bpl,%sil
4088ef	movb %bpl,%dil
4088ec	movb %bpl,%spl
4088ed	movb %bpl,%bpl
4188e8	movb %bpl,%r8b
4188ec	movb %bpl,%r12b
4188ef	movb %bpl,%r15b
4488c0	movb %r8b,%al
4488c1	movb %r8b,%cl
4488c2	movb %r8b,%dl
4488c3	movb %r8b,%bl
refused	movb %r8b,%ah
refused	movb %r8b,%ch
refused	movb %r8b,%dh
refused	movb %r8b,%bh
4488c6	movb %r8b,%sil
4488c7	movb %r8b,%dil
4488c4	movb %r8b,%spl
4488c5	movb %r8b,%bpl
4588c0	movb %r8b,%r8b
4588c4	movb %r8b,%r12b
4588c7	movb %r8b,%r15b
4488e0	movb %r12b,%al
4488e1	movb %r12b,%cl
4488e2	movb %r12b,%dl
4488e3	movb %r12b,%bl
refused	movb %r12b,%ah
refused	movb %r12b,%ch
refused	movb %r12b,%dh
refused	movb %r12b,%bh
4488e6	movb %r12b,%sil
4488e7	movb %r12b,%dil
4488e4	movb %r12b,%spl
4488e5	movb %r12b,%bpl
4588e0	movb %r12b,%r8b
4588e4	movb %r12b,%r12b
4588e7	movb %r12b,%r15b
4488f8	movb %r15b,%al
4488f9	movb %r15b,%cl
4488fa	movb %r15b,%dl
4488fb	movb %r15b,%bl
refused	movb %r15b,%ah
refused	movb %r15b,%ch
refused	movb %r15b,%dh
refused	movb %r15b,%bh
4488fe	movb %r15b,%sil
4488ff	movb %r15b,%dil
4488fc	movb %r15b,%spl
4488fd	movb %r15b,%bpl
4588f8	movb %r15b,%r8b
4588fc	movb %r15b,%r12b
4588ff	movb %r15b,%r15b
88c1	mov %al,%cl
b001	movb $1,%al
b001	mov $1,%al
b000 1:14:sym+0	movb $sym,%al
88042500000000 3:11:sym+0	movb %al,sym
8a042500000000 3:11:sym+0	movb sym,%al
b0ff	movb $-1,%al
88c9	mov %cl,%cl
b101	movb $1,%cl
b101	mov $1,%cl
b100 1:14:sym+0	movb $sym,%cl
880c2500000000 3:11:sym+0	movb %cl,sym
8a0c2500000000 3:11:sym+0	movb sym,%cl
b1ff	movb $-1,%cl
88d1	mov %dl,%cl
b201	movb $1,%dl
b201	mov $1,%dl
b200 1:14:sym+0	movb $sym,%dl
88142500000000 3:11:sym+0	movb %dl,sym
8a142500000000 3:11:sym+0	movb sym,%dl
b2ff	movb $-1,%dl
6689c0	movw %ax,%ax
6689c1	movw %ax,%cx
6689c2	movw %ax,%dx
6689c3	movw %ax,%bx
6689c4	movw %ax,%sp
6689c5	movw %ax,%bp
6689c6	movw %ax,%si
6689c7	movw %ax,%di
664189c0	movw %ax,%r8w
664189c5	movw %ax,%r13w
6689c8	movw %cx,%ax
6689c9	movw %cx,%cx
6689ca	movw %cx,%dx
6689cb	movw %cx,%bx
6689cc	movw %cx,%sp
6689cd	movw %cx,%bp
6689ce	movw %cx,%si
6689cf	movw %cx,%di
664189c8	movw %cx,%r8w
664189cd	movw %cx,%r13w
6689d0	movw %dx,%ax
6689d1	movw %dx,%cx
6689d2	movw %dx,%dx
6689d3	movw %dx,%bx
6689d4	movw %dx,%sp
6689d5	movw %dx,%bp
6689d6	movw %dx,%si
6689d7	movw %dx,%di
664189d0	movw %dx,%r8w
664189d5	movw %dx,%r13w
6689d8	movw %bx,%ax
6689d9	movw %bx,%cx
6689da	movw %bx,%dx
6689db	movw %bx,%bx
6689dc	movw %bx,%sp
6689dd	movw %bx,%bp
6689de	movw %bx,%si
6689df	movw %bx,%di
664189d8	movw %bx,%r8w
664189dd	movw %bx,%r13w
6689e0	movw %sp,%ax
6689e1	movw %sp,%cx
6689e2	movw %sp,%dx
6689e3	movw %sp,%bx
6689e4	movw %sp,%sp
6689e5	movw %sp,%bp
6689e6	movw %sp,%si
6689e7	movw %sp,%di
664189e0	movw %sp,%r8w
664189e5	movw %sp,%r13w
6689e8	movw %bp,%ax
6689e9	movw %bp,%cx
6689ea	movw %bp,%dx
6689eb	movw %bp,%bx
6689ec	movw %bp,%sp
6689ed	movw %bp,%bp
6689ee	movw %bp,%si
6689ef	movw %bp,%di
664189e8	movw %bp,%r8w
664189ed	movw %bp,%r13w
6689f0	movw %si,%ax
6689f1	movw %si,%cx
6689f2	movw %si,%dx
6689f3	movw %si,%bx
6689f4	movw %si,%sp
6689f5	movw %si,%bp
6689f6	movw %si,%si
6689f7	movw %si,%di
664189f0	movw %si,%r8w
664189f5	movw %si,%r13w
6689f8	movw %di,%ax
6689f9	movw %di,%cx
6689fa	movw %di,%dx
6689fb	movw %di,%bx
6689fc	movw %di,%sp
6689fd	movw %di,%bp
6689fe	movw %di,%si
6689ff	movw %di,%di
664189f8	movw %di,%r8w
664189fd	movw %di,%r13w
664489c0	movw %r8w,%ax
664489c1	movw %r8w,%cx
664489c2	movw %r8w,%dx
664489c3	movw %r8w,%bx
664489c4	movw %r8w,%sp
664489c5	movw %r8w,%bp
664489c6	movw %r8w,%si
664489c7	movw %r8w,%di
664589c0	movw %r8w,%r8w
664589c5	movw %r8w,%r13w
664489e8	movw %r13w,%ax
664489e9	movw %r13w,%cx
664489ea	movw %r13w,%dx
664489eb	movw %r13w,%bx
664489ec	movw %r13w,%sp
664489ed	movw %r13w,%bp
664489ee	movw %r13w,%si
664489ef	movw %r13w,%di
664589e8	movw %r13w,%r8w
664589ed	movw %r13w,%r13w
6689c1	mov %ax,%cx
66b80100	movw $1,%ax
66b80100	mov $1,%ax
66b80000 2:12:sym+0	movw $sym,%ax
6689042500000000 4:11:sym+0	movw %ax,sym
668b042500000000 4:11:sym+0	movw sym,%ax
66b8ffff	movw $-1,%ax
6689c9	mov %cx,%cx
66b90100	movw $1,%cx
66b90100	mov $1,%cx
66b90000 2:12:sym+0	movw $sym,%cx
66890c2500000000 4:11:sym+0	movw %cx,sym
668b0c2500000000 4:11:sym+0	movw sym,%cx
66b9ffff	movw $-1,%cx
6689d1	mov %dx,%cx
66ba0100	movw $1,%dx
66ba0100	mov $1,%dx
66ba0000 2:12:sym+0	movw $sym,%dx
6689142500000000 4:11:sym+0	movw %dx,sym
668b142500000000 4:11:sym+0	movw sym,%dx
66baffff	movw $-1,%dx
89c0	movl %eax,%eax
89c1	movl %eax,%ecx
89c2	movl %eax,%edx
89c3	movl %eax,%ebx
89c4	movl %eax,%esp
89c5	movl %eax,%ebp
89c6	movl %eax,%esi
89c7	movl %eax,%edi
4189c0	movl %eax,%r8d
4189c4	movl %eax,%r12d
4189c5	movl %eax,%r13d
4189c7	movl %eax,%r15d
89c8	movl %ecx,%eax
89c9	movl %ecx,%ecx
89ca	movl %ecx,%edx
89cb	movl %ecx,%ebx
89cc	movl %ecx,%esp
89cd	movl %ecx,%ebp
89ce	movl %ecx,%esi
89cf	movl %ecx,%edi
4189c8	movl %ecx,%r8d
4189cc	movl %ecx,%r12d
4189cd	movl %ecx,%r13d
4189cf	movl %ecx,%r15d
89d0	movl %edx,%eax
89d1	movl %edx,%ecx
89d2	movl %edx,%edx
89d3	movl %edx,%ebx
89d4	movl %edx,%esp
89d5	movl %edx,%ebp
89d6	movl %edx,%esi
89d7	movl %edx,%edi
4189d0	movl %edx,%r8d
4189d4	movl %edx,%r12d
4189d5	movl %edx,%r13d
4189d7	movl %edx,%r15d
89d8	movl %ebx,%eax
89d9	movl %ebx,%ecx
89da	movl %ebx,%edx
89db	movl %ebx,%ebx
89dc	movl %ebx,%esp
89dd	movl %ebx,%ebp
89de	movl %ebx,%esi
89df	movl %ebx,%edi
4189d8	movl %ebx,%r8d
4189dc	movl %ebx,%r12d
4189dd	movl %ebx,%r13d
4189df	movl %ebx,%r15d
89e0	movl %esp,%eax
89e1	movl %esp,%ecx
89e2	movl %esp,%edx
89e3	movl %esp,%ebx
89e4	movl %esp,%esp
89e5	movl %esp,%ebp
89e6	movl %esp,%esi
89e7	movl %esp,%edi
4189e0	movl %esp,%r8d
4189e4	movl %esp,%r12d
4189e5	movl %esp,%r13d
4189e7	movl %esp,%r15d
89e8	movl %ebp,%eax
89e9	movl %ebp,%ecx
89ea	movl %ebp,%edx
89eb	movl %ebp,%ebx
89ec	movl %ebp,%esp
89ed	movl %ebp,%ebp
89ee	movl %ebp,%esi
89ef	movl %ebp,%edi
4189e8	movl %ebp,%r8d
4189ec	movl %ebp,%r12d
4189ed	movl %ebp,%r13d
4189ef	movl %ebp,%r15d
89f0	movl %esi,%eax
89f1	movl %esi,%ecx
89f2	movl %esi,%edx
89f3	movl %esi,%ebx
89f4	movl %esi,%esp
89f5	movl %esi,%ebp
89f6	movl %esi,%esi
89f7	movl %esi,%edi
4189f0	movl %esi,%r8d
4189f4	movl %esi,%r12d
4189f5	movl %esi,%r13d
4189f7	movl %esi,%r15d
89f8	movl %edi,%eax
89f9	movl %edi,%ecx
89fa	movl %edi,%edx
89fb	movl %edi,%ebx
89fc	movl %edi,%esp
89fd	movl %edi,%ebp
89fe	movl %edi,%esi
89ff	movl %edi,%edi
4189f8	movl %edi,%r8d
4189fc	movl %edi,%r12d
4189fd	movl %edi,%r13d
4189ff	movl %edi,%r15d
4489c0	movl %r8d,%eax
4489c1	movl %r8d,%ecx
4489c2	movl %r8d,%edx
4489c3	movl %r8d,%ebx
4489c4	movl %r8d,%esp
4489c5	movl %r8d,%ebp
4489c6	movl %r8d,%esi
4489c7	movl %r8d,%edi
4589c0	movl %r8d,%r8d
4589c4	movl %r8d,%r12d
4589c5	movl %r8d,%r13d
4589c7	movl %r8d,%r15d
4489e0	movl %r12d,%eax
4489e1	movl %r12d,%ecx
4489e2	movl %r12d,%edx
4489e3	movl %r12d,%ebx
4489e4	movl %r12d,%esp
4489e5	movl %r12d,%ebp
4489e6	movl %r12d,%esi
4489e7	movl %r12d,%edi
4589e0	movl %r12d,%r8d
4589e4	movl %r12d,%r12d
4589e5	movl %r12d,%r13d
4589e7	movl %r12d,%r15d
4489e8	movl %r13d,%eax
4489e9	movl %r13d,%ecx
4489ea	movl %r13d,%edx
4489eb	movl %r13d,%ebx
4489ec	movl %r13d,%esp
4489ed	movl %r13d,%ebp
4489ee	movl %r13d,%esi
4489ef	movl %r13d,%edi
4589e8	movl %r13d,%r8d
4589ec	movl %r13d,%r12d
4589ed	movl %r13d,%r13d
4589ef	movl %r13d,%r15d
4489f8	movl %r15d,%eax
4489f9	movl %r15d,%ecx
4489fa	movl %r15d,%edx
4489fb	movl %r15d,%ebx
4489fc	movl %r15d,%esp
4489fd	movl %r15d,%ebp
4489fe	movl %r15d,%esi
4489ff	movl %r15d,%edi
4589f8	movl %r15d,%r8d
4589fc	movl %r15d,%r12d
4589fd	movl %r15d,%r13d
4589ff	movl %r15d,%r15d
89c1	mov %eax,%ecx
b801000000	movl $1,%eax
b801000000	mov $1,%eax
b800000000 1:10:sym+0	movl $sym,%eax
8b042500000000 3:11:sym+0	movl sym,%eax
b8ffffffff	movl $-1,%eax
89c9	mov %ecx,%ecx
b901000000	movl $1,%ecx
b901000000	mov $1,%ecx
b900000000 1:10:sym+0	movl $sym,%ecx
890c2500000000 3:11:sym+0	movl %ecx,sym
b9ffffffff	movl $-1,%ecx
89d1	mov %edx,%ecx
ba01000000	movl $1,%edx
ba01000000	mov $1,%edx
ba00000000 1:10:sym+0	movl $sym,%edx
89142500000000 3:11:sym+0	movl %edx,sym
8b142500000000 3:11:sym+0	movl sym,%edx
baffffffff	movl $-1,%edx
4889c0	movq %rax,%rax
4889c1	movq %rax,%rcx
4889c2	movq %rax,%rdx
4889c3	movq %rax,%rbx
4889c4	movq %rax,%rsp
4889c5	movq %rax,%rbp
4889c6	movq %rax,%rsi
4889c7	movq %rax,%rdi
4989c0	movq %rax,%r8
4989c1	movq %rax,%r9
4989c2	movq %rax,%r10
4989c3	movq %rax,%r11
4989c4	movq %rax,%r12
4989c5	movq %rax,%r13
4989c6	movq %rax,%r14
4989c7	movq %rax,%r15
4889c8	movq %rcx,%rax
4889c9	movq %rcx,%rcx
4889ca	movq %rcx,%rdx
4889cb	movq %rcx,%rbx
4889cc	movq %rcx,%rsp
4889cd	movq %rcx,%rbp
4889ce	movq %rcx,%rsi
4889cf	movq %rcx,%rdi
4989c8	movq %rcx,%r8
4989c9	movq %rcx,%r9
4989ca	movq %rcx,%r10
4989cb	movq %rcx,%r11
4989cc	movq %rcx,%r12
4989cd	movq %rcx,%r13
4989ce	movq %rcx,%r14
4989cf	movq %rcx,%r15
4889d0	movq %rdx,%rax
4889d1	movq %rdx,%rcx
4889d2	movq %rdx,%rdx
4889d3	movq %rdx,%rbx
4889d4	movq %rdx,%rsp
4889d5	movq %rdx,%rbp
4889d6	movq %rdx,%rsi
4889d7	movq %rdx,%rdi
4989d0	movq %rdx,%r8
4989d1	movq %rdx,%r9
4989d2	movq %rdx,%r10
4989d3	movq %rdx,%r11
4989d4	movq %rdx,%r12
4989d5	movq %rdx,%r13
4989d6	movq %rdx,%r14
4989d7	movq %rdx,%r15
4889d8	movq %rbx,%rax
4889d9	movq %rbx,%rcx
4889da	movq %rbx,%rdx
4889db	movq %rbx,%rbx
4889dc	movq %rbx,%rsp
4889dd	movq %rbx,%rbp
4889de	movq %rbx,%rsi
4889df	movq %rbx,%rdi
4989d8	movq %rbx,%r8
4989d9	movq %rbx,%r9
4989da	movq %rbx,%r10
4989db	movq %rbx,%r11
4989dc	movq %rbx,%r12
4989dd	movq %rbx,%r13
4989de	movq %rbx,%r14
4989df	movq %rbx,%r15
4889e0	movq %rsp,%rax
4889e1	movq %rsp,%rcx
4889e2	movq %rsp,%rdx
4889e3	movq %rsp,%rbx
4889e4	movq %rsp,%rsp
4889e5	movq %rsp,%rbp
4889e6	movq %rsp,%rsi
4889e7	movq %rsp,%rdi
4989e0	movq %rsp,%r8
4989e1	movq %rsp,%r9
4989e2	movq %rsp,%r10
4989e3	movq %rsp,%r11
4989e4	movq %rsp,%r12
4989e5	movq %rsp,%r13
4989e6	movq %rsp,%r14
4989e7	movq %rsp,%r15
4889e8	movq %rbp,%rax
4889e9	movq %rbp,%rcx
4889ea	movq %rbp,%rdx
4889eb	movq %rbp,%rbx
4889ec	movq %rbp,%rsp
4889ed	movq %rbp,%rbp
4889ee	movq %rbp,%rsi
4889ef	movq %rbp,%rdi
4989e8	movq %rbp,%r8
4989e9	movq %rbp,%r9
4989ea	movq %rbp,%r10
4989eb	movq %rbp,%r11
4989ec	movq %rbp,%r12
4989ed	movq %rbp,%r13
4989ee	movq %rbp,%r14
4989ef	movq %rbp,%r15
4889f0	movq %rsi,%rax
4889f1	movq %rsi,%rcx
4889f2	movq %rsi,%rdx
4889f3	movq %rsi,%rbx
4889f4	movq %rsi,%rsp
4889f5	movq %rsi,%rbp
4889f6	movq %rsi,%rsi
4889f7	movq %rsi,%rdi
4989f0	movq %rsi,%r8
4989f1	movq %rsi,%r9
4989f2	movq %rsi,%r10
4989f3	movq %rsi,%r11
4989f4	movq %rsi,%r12
4989f5	movq %rsi,%r13
4989f6	movq %rsi,%r14
4989f7	movq %rsi,%r15
4889f8	movq %rdi,%rax
4889f9	movq %rdi,%rcx
4889fa	movq %rdi,%rdx
4889fb	movq %rdi,%rbx
4889fc	movq %rdi,%rsp
4889fd	movq %rdi,%rbp
4889fe	movq %rdi,%rsi
4889ff	movq %rdi,%rdi
4989f8	movq %rdi,%r8
4989f9	movq %rdi,%r9
4989fa	movq %rdi,%r10
4989fb	movq %rdi,%r11
4989fc	movq %rdi,%r12
4989fd	movq %rdi,%r13
4989fe	movq %rdi,%r14
4989ff	movq %rdi,%r15
4c89c0	movq %r8,%rax
4c89c1	movq %r8,%rcx
4c89c2	movq %r8,%rdx
4c89c3	movq %r8,%rbx
4c89c4	movq %r8,%rsp
4c89c5	movq %r8,%rbp
4c89c6	movq %r8,%rsi
4c89c7	movq %r8,%rdi
4d89c0	movq %r8,%r8
4d89c1	movq %r8,%r9
4d89c2	movq %r8,%r10
4d89c3	movq %r8,%r11
4d89c4	movq %r8,%r12
4d89c5	movq %r8,%r13
4d89c6	movq %r8,%r14
4d89c7	movq %r8,%r15
4c89c8	movq %r9,%rax
4c89c9	movq %r9,%rcx
4c89ca	movq %r9,%rdx
4c89cb	movq %r9,%rbx
4c89cc	movq %r9,%rsp
4c89cd	movq %r9,%rbp
4c89ce	movq %r9,%rsi
4c89cf	movq %r9,%rdi
4d89c8	movq %r9,%r8
4d89c9	movq %r9,%r9
4d89ca	movq %r9,%r10
4d89cb	movq %r9,%r11
4d89cc	movq %r9,%r12
4d89cd	movq %r9,%r13
4d89ce	movq %r9,%r14
4d89cf	movq %r9,%r15
4c89d0	movq %r10,%rax
4c89d1	movq %r10,%rcx
4c89d2	movq %r10,%rdx
4c89d3	movq %r10,%rbx
4c89d4	movq %r10,%rsp
4c89d5	movq %r10,%rbp
4c89d6	movq %r10,%rsi
4c89d7	movq %r10,%rdi
4d89d0	movq %r10,%r8
4d89d1	movq %r10,%r9
4d89d2	movq %r10,%r10
4d89d3	movq %r10,%r11
4d89d4	movq %r10,%r12
4d89d5	movq %r10,%r13
4d89d6	movq %r10,%r14
4d89d7	movq %r10,%r15
4c89d8	movq %r11,%rax
4c89d9	movq %r11,%rcx
4c89da	movq %r11,%rdx
4c89db	movq %r11,%rbx
4c89dc	movq %r11,%rsp
4c89dd	movq %r11,%rbp
4c89de	movq %r11,%rsi
4c89df	movq %r11,%rdi
4d89d8	movq %r11,%r8
4d89d9	movq %r11,%r9
4d89da	movq %r11,%r10
4d89db	movq %r11,%r11
4d89dc	movq %r11,%r12
4d89dd	movq %r11,%r13
4d89de	movq %r11,%r14
4d89df	movq %r11,%r15
4c89e0	movq %r12,%rax
4c89e1	movq %r12,%rcx
4c89e2	movq %r12,%rdx
4c89e3	movq %r12,%rbx
4c89e4	movq %r12,%rsp
4c89e5	movq %r12,%rbp
4c89e6	movq %r12,%rsi
4c89e7	movq %r12,%rdi
4d89e0	movq %r12,%r8
4d89e1	movq %r12,%r9
4d89e2	movq %r12,%r10
4d89e3	movq %r12,%r11
4d89e4	movq %r12,%r12
4d89e5	movq %r12,%r13
4d89e6	movq %r12,%r14
4d89e7	movq %r12,%r15
4c89e8	movq %r13,%rax
4c89e9	movq %r13,%rcx
4c89ea	movq %r13,%rdx
4c89eb	movq %r13,%rbx
4c89ec	movq %r13,%rsp
4c89ed	movq %r13,%rbp
4c89ee	movq %r13,%rsi
4c89ef	movq %r13,%rdi
4d89e8	movq %r13,%r8
4d89e9	movq %r13,%r9
4d89ea	movq %r13,%r10
4d89eb	movq %r13,%r11
4d89ec	movq %r13,%r12
4d89ed	movq %r13,%r13
4d89ee	movq %r13,%r14
4d89ef	movq %r13,%r15
4c89f0	movq %r14,%rax
4c89f1	movq %r14,%rcx
4c89f2	movq %r14,%rdx
4c89f3	movq %r14,%rbx
4c89f4	movq %r14,%rsp
4c89f5	movq %r14,%rbp
4c89f6	movq %r14,%rsi
4c89f7	movq %r14,%rdi
4d89f0	movq %r14,%r8
4d89f1	movq %r14,%r9
4d89f2	movq %r14,%r10
4d89f3	movq %r14,%r11
4d89f4	movq %r14,%r12
4d89f5	movq %r14,%r13
4d89f6	movq %r14,%r14
4d89f7	movq %r14,%r15
4c89f8	movq %r15,%rax
4c89f9	movq %r15,%rcx
4c89fa	movq %r15,%rdx
4c89fb	movq %r15,%rbx
4c89fc	movq %r15,%rsp
4c89fd	movq %r15,%rbp
4c89fe	movq %r15,%rsi
4c89ff	movq %r15,%rdi
4d89f8	movq %r15,%r8
4d89f9	movq %r15,%r9
4d89fa	movq %r15,%r10
4d89fb	movq %r15,%r11
4d89fc	movq %r15,%r12
4d89fd	movq %r15,%r13
4d89fe	movq %r15,%r14
4d89ff	movq %r15,%r15
4889c1	mov %rax,%rcx
48c7c001000000	movq $1,%rax
48c7c001000000	mov $1,%rax
48c7c000000000 3:11:sym+0	movq $sym,%rax
4889042500000000 4:11:sym+0	movq %rax,sym
488b042500000000 4:11:sym+0	movq sym,%rax
48c7c0ffffffff	movq $-1,%rax
4889c9	mov %rcx,%rcx
48c7c101000000	movq $1,%rcx
48c7c101000000	mov $1,%rcx
48c7c100000000 3:11:sym+0	movq $sym,%rcx
48890c2500000000 4:11:sym+0	movq %rcx,sym
488b0c2500000000 4:11:sym+0	movq sym,%rcx
48c7c1ffffffff	movq $-1,%rcx
4889d1	mov %rdx,%rcx
48c7c201000000	movq $1,%rdx
48c7c201000000	mov $1,%rdx
48c7c200000000 3:11:sym+0	movq $sym,%rdx
4889142500000000 4:11:sym+0	movq %rdx,sym
488b142500000000 4:11:sym+0	movq sym,%rdx
48c7c2ffffffff	movq $-1,%rdx
refused	movl %ax,%ebx
refused	movl %eax,%bx
refused	movw %eax,%ebx
refused	movb %ax,%bl
refused	movb %al,%bx
refused	mov %al,%ebx
refused	mov %eax,%bl
refused	addl %ax,%ebx
refused	addw %al,%bx
c70001000000	mov $1,(%rax)
8304250000000001 3:11:sym+0	add $1,sym
ff042500000000 3:11:sym+0	inc sym
ff342500000000 3:11:sym+0	push sym
f718	neg (%rax)
d1242500000000 3:11:sym+0	shl $1,sym
refused	movl %eax
refused	movl
refused	mov %eax,%ebx,%ecx
refused	movl $1,$2
refused	movl %eax,$1
refused	addl %eax,$1
refused	movl (%rax),(%rbx)
refused	addl sym,sym2
b000	movb $256,%al
b07f	movb $-129,%al
b0ff	movb $255,%al
b080	movb $-128,%al
66b80000	movw $65536,%ax
66b8ff7f	movw $-32769,%ax
66b8ffff	movw $65535,%ax
b800000000	movl $0x100000000,%eax
b8ffffff7f	movl $-0x80000001,%eax
b8ffffffff	movl $0xffffffff,%eax
0400	addb $256,%al
6681c34523	addw $0x12345,%bx
81c389674523	addl $0x123456789,%ebx
66684523	pushw $0x12345
refused	int $256
refused	int $-1
cc	int $3
cc	int3
refused	into
refused	ret $0x10000
c2ffff	ret $65535
refused	enter $0x10000,$0
refused	enter $8,$256
c8080001	enter $8,$1
c1e020	shll $32,%eax
refused	shll $256,%eax
c1e0ff	shll $255,%eax
refused	shll %dl,%eax
refused	shll %cx,%eax
e400	in $256,%al
e600	out %al,$256
refused	inb %cx,%al
refused	movb %ah,(%r8)
refused	movb (%rax,%r9),%bh
refused	addb %ah,%dil
refused	movzbl %ah,%r8d
refused	movzbq %ah,%rax
refused	movsbq %bh,%rax
refused	xchgb %ah,%r9b
8820	movb %ah,(%rax)
refused	cmpb %dh,%spl
refused	testb %ch,%bpl
refused	pushl %eax
refused	popl %eax
refused	pushl $1
refused	push %eax
refused	pop %ebx
refused	pushl (%rax)
refused	popl sym
refused	aaa
refused	aas
refused	aad
refused	aam
refused	daa
refused	das
refused	pusha
refused	popa
refused	pushal
refused	popal
refused	pushaw
refused	bound %eax,(%rax)
refused	lds (%rax),%eax
refused	les (%rax),%eax
refused	ljmp $1,$2
refused	lcall $1,$2
ffc0	incl %eax
ffc8	decl %eax
ffc0	inc %eax
refused	push %cs
refused	push %ds
refused	push %es
refused	push %ss
refused	pop %ds
refused	pop %es
refused	pop %ss
0fa0	push %fs
0fa8	push %gs
0fa1	pop %fs
0fa9	pop %gs
660fa0	pushw %fs
0fa0	pushq %fs
refused	salc
refused	arpl %ax,%bx
refused	jcxz 1f | 1:
67e300	jecxz 1f | 1:
e300	jrcxz 1f | 1:
0f34	sysenter
0f35	sysexit
0f05	syscall
0f07	sysret
480f07	sysretq
0f07	sysretl
0f01f8	swapgs
4898	cdqe
4898	cltq
4899	cqo
4899	cqto
4863d8	movslq %eax,%rbx
4863d8	movsxd %eax,%rbx
480fbed8	movsbq %al,%rbx
480fbfd8	movswq %ax,%rbx
480fb7d8	movzwq %ax,%rbx
480fb6d8	movzbq %al,%rbx
refused	movzlq %eax,%rbx
480fc708	cmpxchg16b (%rax)
9c	pushfq
9d	popfq
9c	pushf
9d	popf
refused	pushfl
refused	popfl
669c	pushfw
48cf	iretq
cf	iret
cf	iretl
66cf	iretw
c9	leave
c9	leaveq
refused	leavel
66c9	leavew
c3	retq
c3	ret
refused	retl
66c3	retw
48cb	lretq
cb	lret
cb	lretl
ca0800	lret $8
c20800	retq $8
ffd0	callq *%rax
ffd0	call *%rax
refused	call *%eax
refused	jmp *%eax
ffe0	jmpq *%rax
ff20	jmp *(%rax)
refused	jmpl *(%rax)
refused	calll *(%rax)
66ffd0	callw *%ax
66ffe0	jmpw *%ax
ff28	ljmp *(%rax)
ff18	lcall *(%rax)
refused	ljmpq *(%rax)
refused	lcallq *(%rax)
ff28	ljmpl *(%rax)
66ff28	ljmpw *(%rax)
48c7c000000000	movq $0,%rax
49c7c300000000	movq $0,%r11
48c7c000000000	mov $0,%rax
48b80000000000000000	movabs $0,%rax
48b90000000000000000	movabsq $0,%rcx
4883c000	addq $0,%rax
4883c300	addq $0,%rbx
6a00	pushq $0
6a00	push $0
48c70000000000	movq $0,(%rax)
48a900000000	testq $0,%rax
48f7c200000000	testq $0,%rdx
486bd800	imulq $0,%rax,%rbx
48833b00	cmpq $0,(%rbx)
4883e400	andq $0,%rsp
b800000000	movl $0,%eax
83c000	addl $0,%eax
83c300	addl $0,%ebx
refused	movq $0,%mm0
832000	andl $0,(%rax)
c70000000000	movl $0,(%rax)
4883f000	xorq $0,%rax
4883ec00	subq $0,%rsp
4983c800	orq $0,%r8
666a00	pushw $0
486bc000	imul $0,%rax
66b80000	movw $0,%ax
b000	movb $0,%al
6683c000	addw $0,%ax
0400	addb $0,%al
6683c300	addw $0,%bx
80c300	addb $0,%bl
49c7c301000000	movq $1,%r11
48b80100000000000000	movabs $1,%rax
48b90100000000000000	movabsq $1,%rcx
4883c001	addq $1,%rax
4883c301	addq $1,%rbx
6a01	pushq $1
6a01	push $1
48a901000000	testq $1,%rax
48f7c201000000	testq $1,%rdx
486bd801	imulq $1,%rax,%rbx
48833b01	cmpq $1,(%rbx)
4883e401	andq $1,%rsp
83c001	addl $1,%eax
83c301	addl $1,%ebx
refused	movq $1,%mm0
832001	andl $1,(%rax)
c70001000000	movl $1,(%rax)
4883f001	xorq $1,%rax
4883ec01	subq $1,%rsp
4983c801	orq $1,%r8
666a01	pushw $1
486bc001	imul $1,%rax
6683c001	addw $1,%ax
0401	addb $1,%al
6683c301	addw $1,%bx
80c301	addb $1,%bl
49c7c3ffffffff	movq $-1,%r11
48c7c0ffffffff	mov $-1,%rax
48b8ffffffffffffffff	movabs $-1,%rax
48b9ffffffffffffffff	movabsq $-1,%rcx
4883c0ff	addq $-1,%rax
4883c3ff	addq $-1,%rbx
6aff	pushq $-1
6aff	push $-1
48c700ffffffff	movq $-1,(%rax)
48a9ffffffff	testq $-1,%rax
48f7c2ffffffff	testq $-1,%rdx
486bd8ff	imulq $-1,%rax,%rbx
48833bff	cmpq $-1,(%rbx)
4883e4ff	andq $-1,%rsp
83c0ff	addl $-1,%eax
83c3ff	addl $-1,%ebx
refused	movq $-1,%mm0
8320ff	andl $-1,(%rax)
c700ffffffff	movl $-1,(%rax)
4883f0ff	xorq $-1,%rax
4883ecff	subq $-1,%rsp
4983c8ff	orq $-1,%r8
666aff	pushw $-1
486bc0ff	imul $-1,%rax
6683c0ff	addw $-1,%ax
04ff	addb $-1,%al
6683c3ff	addw $-1,%bx
80c3ff	addb $-1,%bl
48c7c07f000000	movq $0x7f,%rax
49c7c37f000000	movq $0x7f,%r11
48c7c07f000000	mov $0x7f,%rax
48b87f00000000000000	movabs $0x7f,%rax
48b97f00000000000000	movabsq $0x7f,%rcx
4883c07f	addq $0x7f,%rax
4883c37f	addq $0x7f,%rbx
6a7f	pushq $0x7f
6a7f	push $0x7f
48c7007f000000	movq $0x7f,(%rax)
48a97f000000	testq $0x7f,%rax
48f7c27f000000	testq $0x7f,%rdx
486bd87f	imulq $0x7f,%rax,%rbx
48833b7f	cmpq $0x7f,(%rbx)
4883e47f	andq $0x7f,%rsp
b87f000000	movl $0x7f,%eax
83c07f	addl $0x7f,%eax
83c37f	addl $0x7f,%ebx
refused	movq $0x7f,%mm0
83207f	andl $0x7f,(%rax)
c7007f000000	movl $0x7f,(%rax)
4883f07f	xorq $0x7f,%rax
4883ec7f	subq $0x7f,%rsp
4983c87f	orq $0x7f,%r8
666a7f	pushw $0x7f
486bc07f	imul $0x7f,%rax
66b87f00	movw $0x7f,%ax
b07f	movb $0x7f,%al
6683c07f	addw $0x7f,%ax
047f	addb $0x7f,%al
6683c37f	addw $0x7f,%bx
80c37f	addb $0x7f,%bl
48c7c080000000	movq $0x80,%rax
49c7c380000000	movq $0x80,%r11
48c7c080000000	mov $0x80,%rax
48b88000000000000000	movabs $0x80,%rax
48b98000000000000000	movabsq $0x80,%rcx
480580000000	addq $0x80,%rax
4881c380000000	addq $0x80,%rbx
6880000000	pushq $0x80
6880000000	push $0x80
48c70080000000	movq $0x80,(%rax)
48a980000000	testq $0x80,%rax
48f7c280000000	testq $0x80,%rdx
4869d880000000	imulq $0x80,%rax,%rbx
48813b80000000	cmpq $0x80,(%rbx)
4881e480000000	andq $0x80,%rsp
b880000000	movl $0x80,%eax
0580000000	addl $0x80,%eax
81c380000000	addl $0x80,%ebx
refused	movq $0x80,%mm0
812080000000	andl $0x80,(%rax)
c70080000000	movl $0x80,(%rax)
483580000000	xorq $0x80,%rax
4881ec80000000	subq $0x80,%rsp
4981c880000000	orq $0x80,%r8
66688000	pushw $0x80
4869c080000000	imul $0x80,%rax
66b88000	movw $0x80,%ax
b080	movb $0x80,%al
66058000	addw $0x80,%ax
0480	addb $0x80,%al
6681c38000	addw $0x80,%bx
80c380	addb $0x80,%bl
48c7c080ffffff	movq $-0x80,%rax
49c7c380ffffff	movq $-0x80,%r11
48c7c080ffffff	mov $-0x80,%rax
48b880ffffffffffffff	movabs $-0x80,%rax
48b980ffffffffffffff	movabsq $-0x80,%rcx
4883c080	addq $-0x80,%rax
4883c380	addq $-0x80,%rbx
6a80	pushq $-0x80
6a80	push $-0x80
48c70080ffffff	movq $-0x80,(%rax)
48a980ffffff	testq $-0x80,%rax
48f7c280ffffff	testq $-0x80,%rdx
486bd880	imulq $-0x80,%rax,%rbx
48833b80	cmpq $-0x80,(%rbx)
4883e480	andq $-0x80,%rsp
b880ffffff	movl $-0x80,%eax
83c080	addl $-0x80,%eax
83c380	addl $-0x80,%ebx
refused	movq $-0x80,%mm0
832080	andl $-0x80,(%rax)
c70080ffffff	movl $-0x80,(%rax)
4883f080	xorq $-0x80,%rax
4883ec80	subq $-0x80,%rsp
4983c880	orq $-0x80,%r8
666a80	pushw $-0x80
486bc080	imul $-0x80,%rax
66b880ff	movw $-0x80,%ax
b080	movb $-0x80,%al
6683c080	addw $-0x80,%ax
0480	addb $-0x80,%al
6683c380	addw $-0x80,%bx
80c380	addb $-0x80,%bl
48c7c07fffffff	movq $-0x81,%rax
49c7c37fffffff	movq $-0x81,%r11
48c7c07fffffff	mov $-0x81,%rax
48b87fffffffffffffff	movabs $-0x81,%rax
48b97fffffffffffffff	movabsq $-0x81,%rcx
48057fffffff	addq $-0x81,%rax
4881c37fffffff	addq $-0x81,%rbx
687fffffff	pushq $-0x81
687fffffff	push $-0x81
48c7007fffffff	movq $-0x81,(%rax)
48a97fffffff	testq $-0x81,%rax
48f7c27fffffff	testq $-0x81,%rdx
4869d87fffffff	imulq $-0x81,%rax,%rbx
48813b7fffffff	cmpq $-0x81,(%rbx)
4881e47fffffff	andq $-0x81,%rsp
b87fffffff	movl $-0x81,%eax
057fffffff	addl $-0x81,%eax
81c37fffffff	addl $-0x81,%ebx
refused	movq $-0x81,%mm0
81207fffffff	andl $-0x81,(%rax)
c7007fffffff	movl $-0x81,(%rax)
48357fffffff	xorq $-0x81,%rax
4881ec7fffffff	subq $-0x81,%rsp
4981c87fffffff	orq $-0x81,%r8
66687fff	pushw $-0x81
4869c07fffffff	imul $-0x81,%rax
66b87fff	movw $-0x81,%ax
b07f	movb $-0x81,%al
66057fff	addw $-0x81,%ax
047f	addb $-0x81,%al
6681c37fff	addw $-0x81,%bx
80c37f	addb $-0x81,%bl
48c7c0ffffff7f	movq $0x7fffffff,%rax
49c7c3ffffff7f	movq $0x7fffffff,%r11
48c7c0ffffff7f	mov $0x7fffffff,%rax
48b8ffffff7f00000000	movabs $0x7fffffff,%rax
48b9ffffff7f00000000	movabsq $0x7fffffff,%rcx
4805ffffff7f	addq $0x7fffffff,%rax
4881c3ffffff7f	addq $0x7fffffff,%rbx
68ffffff7f	pushq $0x7fffffff
68ffffff7f	push $0x7fffffff
48c700ffffff7f	movq $0x7fffffff,(%rax)
48a9ffffff7f	testq $0x7fffffff,%rax
48f7c2ffffff7f	testq $0x7fffffff,%rdx
4869d8ffffff7f	imulq $0x7fffffff,%rax,%rbx
48813bffffff7f	cmpq $0x7fffffff,(%rbx)
4881e4ffffff7f	andq $0x7fffffff,%rsp
b8ffffff7f	movl $0x7fffffff,%eax
05ffffff7f	addl $0x7fffffff,%eax
81c3ffffff7f	addl $0x7fffffff,%ebx
refused	movq $0x7fffffff,%mm0
8120ffffff7f	andl $0x7fffffff,(%rax)
c700ffffff7f	movl $0x7fffffff,(%rax)
4835ffffff7f	xorq $0x7fffffff,%rax
4881ecffffff7f	subq $0x7fffffff,%rsp
4981c8ffffff7f	orq $0x7fffffff,%r8
6668ffff	pushw $0x7fffffff
4869c0ffffff7f	imul $0x7fffffff,%rax
66b8ffff	movw $0x7fffffff,%ax
b0ff	movb $0x7fffffff,%al
6605ffff	addw $0x7fffffff,%ax
04ff	addb $0x7fffffff,%al
6681c3ffff	addw $0x7fffffff,%bx
80c3ff	addb $0x7fffffff,%bl
48b80000008000000000	movq $0x80000000,%rax
49bb0000008000000000	movq $0x80000000,%r11
48b80000008000000000	mov $0x80000000,%rax
48b80000008000000000	movabs $0x80000000,%rax
48b90000008000000000	movabsq $0x80000000,%rcx
refused	addq $0x80000000,%rax
refused	addq $0x80000000,%rbx
refused	pushq $0x80000000
refused	push $0x80000000
refused	movq $0x80000000,(%rax)
refused	testq $0x80000000,%rax
refused	testq $0x80000000,%rdx
refused	imulq $0x80000000,%rax,%rbx
refused	cmpq $0x80000000,(%rbx)
refused	andq $0x80000000,%rsp
b800000080	movl $0x80000000,%eax
0500000080	addl $0x80000000,%eax
81c300000080	addl $0x80000000,%ebx
refused	movq $0x80000000,%mm0
812000000080	andl $0x80000000,(%rax)
c70000000080	movl $0x80000000,(%rax)
refused	xorq $0x80000000,%rax
refused	subq $0x80000000,%rsp
refused	orq $0x80000000,%r8
66680000	pushw $0x80000000
refused	imul $0x80000000,%rax
66b80000	movw $0x80000000,%ax
b000	movb $0x80000000,%al
66050000	addw $0x80000000,%ax
0400	addb $0x80000000,%al
6681c30000	addw $0x80000000,%bx
80c300	addb $0x80000000,%bl
48b8ffffffff00000000	movq $0xffffffff,%rax
49bbffffffff00000000	movq $0xffffffff,%r11
48b8ffffffff00000000	mov $0xffffffff,%rax
48b8ffffffff00000000	movabs $0xffffffff,%rax
48b9ffffffff00000000	movabsq $0xffffffff,%rcx
refused	addq $0xffffffff,%rax
refused	addq $0xffffffff,%rbx
refused	pushq $0xffffffff
refused	push $0xffffffff
refused	movq $0xffffffff,(%rax)
refused	testq $0xffffffff,%rax
refused	testq $0xffffffff,%rdx
refused	imulq $0xffffffff,%rax,%rbx
refused	cmpq $0xffffffff,(%rbx)
refused	andq $0xffffffff,%rsp
83c0ff	addl $0xffffffff,%eax
83c3ff	addl $0xffffffff,%ebx
refused	movq $0xffffffff,%mm0
8320ff	andl $0xffffffff,(%rax)
c700ffffffff	movl $0xffffffff,(%rax)
refused	xorq $0xffffffff,%rax
refused	subq $0xffffffff,%rsp
refused	orq $0xffffffff,%r8
666aff	pushw $0xffffffff
refused	imul $0xffffffff,%rax
66b8ffff	movw $0xffffffff,%ax
b0ff	movb $0xffffffff,%al
6683c0ff	addw $0xffffffff,%ax
04ff	addb $0xffffffff,%al
6683c3ff	addw $0xffffffff,%bx
80c3ff	addb $0xffffffff,%bl
48c7c000000080	movq $-0x80000000,%rax
49c7c300000080	movq $-0x80000000,%r11
48c7c000000080	mov $-0x80000000,%rax
48b800000080ffffffff	movabs $-0x80000000,%rax
48b900000080ffffffff	movabsq $-0x80000000,%rcx
480500000080	addq $-0x80000000,%rax
4881c300000080	addq $-0x80000000,%rbx
6800000080	pushq $-0x80000000
6800000080	push $-0x80000000
48c70000000080	movq $-0x80000000,(%rax)
48a900000080	testq $-0x80000000,%rax
48f7c200000080	testq $-0x80000000,%rdx
4869d800000080	imulq $-0x80000000,%rax,%rbx
48813b00000080	cmpq $-0x80000000,(%rbx)
4881e400000080	andq $-0x80000000,%rsp
b800000080	movl $-0x80000000,%eax
0500000080	addl $-0x80000000,%eax
81c300000080	addl $-0x80000000,%ebx
refused	movq $-0x80000000,%mm0
812000000080	andl $-0x80000000,(%rax)
c70000000080	movl $-0x80000000,(%rax)
483500000080	xorq $-0x80000000,%rax
4881ec00000080	subq $-0x80000000,%rsp
4981c800000080	orq $-0x80000000,%r8
66680000	pushw $-0x80000000
4869c000000080	imul $-0x80000000,%rax
66b80000	movw $-0x80000000,%ax
b000	movb $-0x80000000,%al
66050000	addw $-0x80000000,%ax
0400	addb $-0x80000000,%al
6681c30000	addw $-0x80000000,%bx
80c300	addb $-0x80000000,%bl
48b8ffffff7fffffffff	movq $-0x80000001,%rax
49bbffffff7fffffffff	movq $-0x80000001,%r11
48b8ffffff7fffffffff	mov $-0x80000001,%rax
48b8ffffff7fffffffff	movabs $-0x80000001,%rax
48b9ffffff7fffffffff	movabsq $-0x80000001,%rcx
refused	addq $-0x80000001,%rax
refused	addq $-0x80000001,%rbx
refused	pushq $-0x80000001
refused	push $-0x80000001
refused	movq $-0x80000001,(%rax)
refused	testq $-0x80000001,%rax
refused	testq $-0x80000001,%rdx
refused	imulq $-0x80000001,%rax,%rbx
refused	cmpq $-0x80000001,(%rbx)
refused	andq $-0x80000001,%rsp
05ffffff7f	addl $-0x80000001,%eax
81c3ffffff7f	addl $-0x80000001,%ebx
refused	movq $-0x80000001,%mm0
8120ffffff7f	andl $-0x80000001,(%rax)
c700ffffff7f	movl $-0x80000001,(%rax)
refused	xorq $-0x80000001,%rax
refused	subq $-0x80000001,%rsp
refused	orq $-0x80000001,%r8
6668ffff	pushw $-0x80000001
refused	imul $-0x80000001,%rax
66b8ffff	movw $-0x80000001,%ax
b0ff	movb $-0x80000001,%al
6605ffff	addw $-0x80000001,%ax
04ff	addb $-0x80000001,%al
6681c3ffff	addw $-0x80000001,%bx
80c3ff	addb $-0x80000001,%bl
48b80000000001000000	movq $0x100000000,%rax
49bb0000000001000000	movq $0x100000000,%r11
48b80000000001000000	mov $0x100000000,%rax
48b80000000001000000	movabs $0x100000000,%rax
48b90000000001000000	movabsq $0x100000000,%rcx
refused	addq $0x100000000,%rax
refused	addq $0x100000000,%rbx
refused	pushq $0x100000000
refused	push $0x100000000
refused	movq $0x100000000,(%rax)
refused	testq $0x100000000,%rax
refused	testq $0x100000000,%rdx
refused	imulq $0x100000000,%rax,%rbx
refused	cmpq $0x100000000,(%rbx)
refused	andq $0x100000000,%rsp
0500000000	addl $0x100000000,%eax
81c300000000	addl $0x100000000,%ebx
refused	movq $0x100000000,%mm0
812000000000	andl $0x100000000,(%rax)
c70000000000	movl $0x100000000,(%rax)
refused	xorq $0x100000000,%rax
refused	subq $0x100000000,%rsp
refused	orq $0x100000000,%r8
66680000	pushw $0x100000000
refused	imul $0x100000000,%rax
66b80000	movw $0x100000000,%ax
b000	movb $0x100000000,%al
66050000	addw $0x100000000,%ax
0400	addb $0x100000000,%al
6681c30000	addw $0x100000000,%bx
80c300	addb $0x100000000,%bl
48b8ffffffffffffff7f	movq $0x7fffffffffffffff,%rax
49bbffffffffffffff7f	movq $0x7fffffffffffffff,%r11
48b8ffffffffffffff7f	mov $0x7fffffffffffffff,%rax
48b8ffffffffffffff7f	movabs $0x7fffffffffffffff,%rax
48b9ffffffffffffff7f	movabsq $0x7fffffffffffffff,%rcx
refused	addq $0x7fffffffffffffff,%rax
refused	addq $0x7fffffffffffffff,%rbx
refused	pushq $0x7fffffffffffffff
refused	push $0x7fffffffffffffff
refused	movq $0x7fffffffffffffff,(%rax)
refused	testq $0x7fffffffffffffff,%rax
refused	testq $0x7fffffffffffffff,%rdx
refused	imulq $0x7fffffffffffffff,%rax,%rbx
refused	cmpq $0x7fffffffffffffff,(%rbx)
refused	andq $0x7fffffffffffffff,%rsp
b8ffffffff	movl $0x7fffffffffffffff,%eax
05ffffffff	addl $0x7fffffffffffffff,%eax
81c3ffffffff	addl $0x7fffffffffffffff,%ebx
refused	movq $0x7fffffffffffffff,%mm0
8120ffffffff	andl $0x7fffffffffffffff,(%rax)
c700ffffffff	movl $0x7fffffffffffffff,(%rax)
refused	xorq $0x7fffffffffffffff,%rax
refused	subq $0x7fffffffffffffff,%rsp
refused	orq $0x7fffffffffffffff,%r8
6668ffff	pushw $0x7fffffffffffffff
refused	imul $0x7fffffffffffffff,%rax
66b8ffff	movw $0x7fffffffffffffff,%ax
b0ff	movb $0x7fffffffffffffff,%al
6605ffff	addw $0x7fffffffffffffff,%ax
04ff	addb $0x7fffffffffffffff,%al
6681c3ffff	addw $0x7fffffffffffffff,%bx
80c3ff	addb $0x7fffffffffffffff,%bl
48c7c0ffffffff	movq $0xffffffffffffffff,%rax
49c7c3ffffffff	movq $0xffffffffffffffff,%r11
48c7c0ffffffff	mov $0xffffffffffffffff,%rax
48b8ffffffffffffffff	movabs $0xffffffffffffffff,%rax
48b9ffffffffffffffff	movabsq $0xffffffffffffffff,%rcx
4883c0ff	addq $0xffffffffffffffff,%rax
4883c3ff	addq $0xffffffffffffffff,%rbx
6aff	pushq $0xffffffffffffffff
6aff	push $0xffffffffffffffff
48c700ffffffff	movq $0xffffffffffffffff,(%rax)
48a9ffffffff	testq $0xffffffffffffffff,%rax
48f7c2ffffffff	testq $0xffffffffffffffff,%rdx
486bd8ff	imulq $0xffffffffffffffff,%rax,%rbx
48833bff	cmpq $0xffffffffffffffff,(%rbx)
4883e4ff	andq $0xffffffffffffffff,%rsp
b8ffffffff	movl $0xffffffffffffffff,%eax
83c0ff	addl $0xffffffffffffffff,%eax
83c3ff	addl $0xffffffffffffffff,%ebx
refused	movq $0xffffffffffffffff,%mm0
8320ff	andl $0xffffffffffffffff,(%rax)
c700ffffffff	movl $0xffffffffffffffff,(%rax)
4883f0ff	xorq $0xffffffffffffffff,%rax
4883ecff	subq $0xffffffffffffffff,%rsp
4983c8ff	orq $0xffffffffffffffff,%r8
666aff	pushw $0xffffffffffffffff
486bc0ff	imul $0xffffffffffffffff,%rax
66b8ffff	movw $0xffffffffffffffff,%ax
b0ff	movb $0xffffffffffffffff,%al
6683c0ff	addw $0xffffffffffffffff,%ax
04ff	addb $0xffffffffffffffff,%al
6683c3ff	addw $0xffffffffffffffff,%bx
80c3ff	addb $0xffffffffffffffff,%bl
48b80000000000000080	movq $-0x8000000000000000,%rax
49bb0000000000000080	movq $-0x8000000000000000,%r11
48b80000000000000080	mov $-0x8000000000000000,%rax
48b80000000000000080	movabs $-0x8000000000000000,%rax
48b90000000000000080	movabsq $-0x8000000000000000,%rcx
refused	addq $-0x8000000000000000,%rax
refused	addq $-0x8000000000000000,%rbx
refused	pushq $-0x8000000000000000
refused	push $-0x8000000000000000
refused	movq $-0x8000000000000000,(%rax)
refused	testq $-0x8000000000000000,%rax
refused	testq $-0x8000000000000000,%rdx
refused	imulq $-0x8000000000000000,%rax,%rbx
refused	cmpq $-0x8000000000000000,(%rbx)
refused	andq $-0x8000000000000000,%rsp
b800000000	movl $-0x8000000000000000,%eax
0500000000	addl $-0x8000000000000000,%eax
81c300000000	addl $-0x8000000000000000,%ebx
refused	movq $-0x8000000000000000,%mm0
812000000000	andl $-0x8000000000000000,(%rax)
c70000000000	movl $-0x8000000000000000,(%rax)
refused	xorq $-0x8000000000000000,%rax
refused	subq $-0x8000000000000000,%rsp
refused	orq $-0x8000000000000000,%r8
66680000	pushw $-0x8000000000000000
refused	imul $-0x8000000000000000,%rax
66b80000	movw $-0x8000000000000000,%ax
b000	movb $-0x8000000000000000,%al
66050000	addw $-0x8000000000000000,%ax
0400	addb $-0x8000000000000000,%al
6681c30000	addw $-0x8000000000000000,%bx
80c300	addb $-0x8000000000000000,%bl
48c7c000000080	movq $0xffffffff80000000,%rax
49c7c300000080	movq $0xffffffff80000000,%r11
48c7c000000080	mov $0xffffffff80000000,%rax
48b800000080ffffffff	movabs $0xffffffff80000000,%rax
48b900000080ffffffff	movabsq $0xffffffff80000000,%rcx
480500000080	addq $0xffffffff80000000,%rax
4881c300000080	addq $0xffffffff80000000,%rbx
6800000080	pushq $0xffffffff80000000
6800000080	push $0xffffffff80000000
48c70000000080	movq $0xffffffff80000000,(%rax)
48a900000080	testq $0xffffffff80000000,%rax
48f7c200000080	testq $0xffffffff80000000,%rdx
4869d800000080	imulq $0xffffffff80000000,%rax,%rbx
48813b00000080	cmpq $0xffffffff80000000,(%rbx)
4881e400000080	andq $0xffffffff80000000,%rsp
b800000080	movl $0xffffffff80000000,%eax
0500000080	addl $0xffffffff80000000,%eax
81c300000080	addl $0xffffffff80000000,%ebx
refused	movq $0xffffffff80000000,%mm0
812000000080	andl $0xffffffff80000000,(%rax)
c70000000080	movl $0xffffffff80000000,(%rax)
483500000080	xorq $0xffffffff80000000,%rax
4881ec00000080	subq $0xffffffff80000000,%rsp
4981c800000080	orq $0xffffffff80000000,%r8
66680000	pushw $0xffffffff80000000
4869c000000080	imul $0xffffffff80000000,%rax
66b80000	movw $0xffffffff80000000,%ax
b000	movb $0xffffffff80000000,%al
66050000	addw $0xffffffff80000000,%ax
0400	addb $0xffffffff80000000,%al
6681c30000	addw $0xffffffff80000000,%bx
80c300	addb $0xffffffff80000000,%bl
48b8ffffff7fffffffff	movq $0xffffffff7fffffff,%rax
49bbffffff7fffffffff	movq $0xffffffff7fffffff,%r11
48b8ffffff7fffffffff	mov $0xffffffff7fffffff,%rax
48b8ffffff7fffffffff	movabs $0xffffffff7fffffff,%rax
48b9ffffff7fffffffff	movabsq $0xffffffff7fffffff,%rcx
refused	addq $0xffffffff7fffffff,%rax
refused	addq $0xffffffff7fffffff,%rbx
refused	pushq $0xffffffff7fffffff
refused	push $0xffffffff7fffffff
refused	movq $0xffffffff7fffffff,(%rax)
refused	testq $0xffffffff7fffffff,%rax
refused	testq $0xffffffff7fffffff,%rdx
refused	imulq $0xffffffff7fffffff,%rax,%rbx
refused	cmpq $0xffffffff7fffffff,(%rbx)
refused	andq $0xffffffff7fffffff,%rsp
b8ffffff7f	movl $0xffffffff7fffffff,%eax
05ffffff7f	addl $0xffffffff7fffffff,%eax
81c3ffffff7f	addl $0xffffffff7fffffff,%ebx
refused	movq $0xffffffff7fffffff,%mm0
8120ffffff7f	andl $0xffffffff7fffffff,(%rax)
c700ffffff7f	movl $0xffffffff7fffffff,(%rax)
refused	xorq $0xffffffff7fffffff,%rax
refused	subq $0xffffffff7fffffff,%rsp
refused	orq $0xffffffff7fffffff,%r8
6668ffff	pushw $0xffffffff7fffffff
refused	imul $0xffffffff7fffffff,%rax
66b8ffff	movw $0xffffffff7fffffff,%ax
b0ff	movb $0xffffffff7fffffff,%al
6605ffff	addw $0xffffffff7fffffff,%ax
04ff	addb $0xffffffff7fffffff,%al
6681c3ffff	addw $0xffffffff7fffffff,%bx
80c3ff	addb $0xffffffff7fffffff,%bl
48b8f0debc9a78563412	movq $0x123456789abcdef0,%rax
49bbf0debc9a78563412	movq $0x123456789abcdef0,%r11
48b8f0debc9a78563412	mov $0x123456789abcdef0,%rax
48b8f0debc9a78563412	movabs $0x123456789abcdef0,%rax
48b9f0debc9a78563412	movabsq $0x123456789abcdef0,%rcx
refused	addq $0x123456789abcdef0,%rax
refused	addq $0x123456789abcdef0,%rbx
refused	pushq $0x123456789abcdef0
refused	push $0x123456789abcdef0
refused	movq $0x123456789abcdef0,(%rax)
refused	testq $0x123456789abcdef0,%rax
refused	testq $0x123456789abcdef0,%rdx
refused	imulq $0x123456789abcdef0,%rax,%rbx
refused	cmpq $0x123456789abcdef0,(%rbx)
refused	andq $0x123456789abcdef0,%rsp
b8f0debc9a	movl $0x123456789abcdef0,%eax
05f0debc9a	addl $0x123456789abcdef0,%eax
81c3f0debc9a	addl $0x123456789abcdef0,%ebx
refused	movq $0x123456789abcdef0,%mm0
8120f0debc9a	andl $0x123456789abcdef0,(%rax)
c700f0debc9a	movl $0x123456789abcdef0,(%rax)
refused	xorq $0x123456789abcdef0,%rax
refused	subq $0x123456789abcdef0,%rsp
refused	orq $0x123456789abcdef0,%r8
6668f0de	pushw $0x123456789abcdef0
refused	imul $0x123456789abcdef0,%rax
66b8f0de	movw $0x123456789abcdef0,%ax
b0f0	movb $0x123456789abcdef0,%al
6605f0de	addw $0x123456789abcdef0,%ax
04f0	addb $0x123456789abcdef0,%al
6681c3f0de	addw $0x123456789abcdef0,%bx
80c3f0	addb $0x123456789abcdef0,%bl
refused	movq $0x1ffffffffffffffff,%rax
refused	movq $0x1ffffffffffffffff,%r11
refused	mov $0x1ffffffffffffffff,%rax
refused	movabs $0x1ffffffffffffffff,%rax
refused	movabsq $0x1ffffffffffffffff,%rcx
refused	addq $0x1ffffffffffffffff,%rax
refused	addq $0x1ffffffffffffffff,%rbx
refused	pushq $0x1ffffffffffffffff
refused	push $0x1ffffffffffffffff
refused	movq $0x1ffffffffffffffff,(%rax)
refused	testq $0x1ffffffffffffffff,%rax
refused	testq $0x1ffffffffffffffff,%rdx
refused	imulq $0x1ffffffffffffffff,%rax,%rbx
refused	cmpq $0x1ffffffffffffffff,(%rbx)
refused	andq $0x1ffffffffffffffff,%rsp
refused	movl $0x1ffffffffffffffff,%eax
refused	addl $0x1ffffffffffffffff,%eax
refused	addl $0x1ffffffffffffffff,%ebx
refused	movq $0x1ffffffffffffffff,%mm0
refused	andl $0x1ffffffffffffffff,(%rax)
refused	movl $0x1ffffffffffffffff,(%rax)
refused	xorq $0x1ffffffffffffffff,%rax
refused	subq $0x1ffffffffffffffff,%rsp
refused	orq $0x1ffffffffffffffff,%r8
refused	pushw $0x1ffffffffffffffff
refused	imul $0x1ffffffffffffffff,%rax
refused	movw $0x1ffffffffffffffff,%ax
refused	movb $0x1ffffffffffffffff,%al
refused	addw $0x1ffffffffffffffff,%ax
refused	addb $0x1ffffffffffffffff,%al
refused	addw $0x1ffffffffffffffff,%bx
refused	addb $0x1ffffffffffffffff,%bl
48a10000000000000000 2:1:sym+0	movabs sym,%rax
48a30000000000000000 2:1:sym+0	movabs %rax,sym
a00000000000000000 1:1:sym+0	movabs sym,%al
66a30000000000000000 2:1:sym+0	movabs %ax,sym
a10000000000000000 1:1:sym+0	movabs sym,%eax
48b80000000000000000 2:1:sym+0	movabsq $sym,%rax
48b80000000000000000 2:1:sym+0	movabs $sym,%rax
49bf0000000000000000 2:1:sym+8	movabs $sym+8,%r15
48c70000000000 3:11:sym+0	movq $sym,(%rax)
48c7c000000000 3:11:sym+0	mov $sym,%rax
b800000000 1:10:sym+0	mov $sym,%eax
48a18877665544332211	movabs 0x1122334455667788,%rax
a28877665544332211	movabs %al,0x1122334455667788
a18877665544332211	movabsl 0x1122334455667788,%eax
66a38877665544332211	movabsw %ax,0x1122334455667788
a01000000000000000	movabsb 0x10,%al
488b042534120000	movq 0x1234,%rax
8a042500000000 3:11:sym+0	mov sym,%al
48c7c000000000 3:9:sym+0	movq $sym@GOTPCREL,%rax
6800000000 1:11:sym+0	pushq $sym
6800000000 1:11:sym+0	push $sym
e800000000 1:4:sym-4	call sym
e800000000 1:4:sym-4	call sym@PLT
e900000000 1:4:sym-4	jmp sym
e900000000 1:4:sym-4	jmp sym@PLT
e900000000 1:2:sym+0	jmp sym+4
e800000000 1:2:sym-8	call sym-4
0f8400000000 2:4:sym-4	je sym
0f8500000000 2:4:sym-4	jne sym@PLT
ff242500000000 3:11:sym+0	jmp *sym
ff142500000000 3:11:sym+0	call *sym
ff2500000000 2:2:sym-4	jmp *sym(%rip)
ff1500000000 2:41:sym-4	call *sym@GOTPCREL(%rip)
ff2500000000 2:41:sym-4	jmp *sym@GOTPCREL(%rip)
488d3d00000000 3:19:sym-4	leaq sym@TLSGD(%rip),%rdi
488d3d00000000 3:20:sym-4	leaq sym@TLSLD(%rip),%rdi
488d9000000000 3:21:sym+0	leaq sym@DTPOFF(%rax),%rdx
488b0500000000 3:22:sym-4	movq sym@GOTTPOFF(%rip),%rax
4c032500000000 3:22:sym-4	addq sym@GOTTPOFF(%rip),%r12
64488b042500000000 5:23:sym+0	movq %fs:sym@TPOFF,%rax
48c7c000000000 3:23:sym+0	movq $sym@TPOFF,%rax
488d8000000000 3:23:sym+0	leaq sym@TPOFF(%rax),%rax
64488b042500000000	movq %fs:0,%rax
488d0500000000 3:34:sym-4	leaq sym@TLSDESC(%rip),%rax
ff10 0:35:sym+0	call *sym@TLSCALL(%rax)
488b0500000000 3:42:sym-4	movq sym@GOTPCREL(%rip),%rax
4c8b3d00000000 3:42:sym-4	movq sym@GOTPCREL(%rip),%r15
8b0500000000 2:41:sym-4	movl sym@GOTPCREL(%rip),%eax
48030500000000 3:42:sym-4	addq sym@GOTPCREL(%rip),%rax
483b0500000000 3:42:sym-4	cmpq sym@GOTPCREL(%rip),%rax
48850500000000 3:42:sym-4	testq %rax,sym@GOTPCREL(%rip)
ff3500000000 2:41:sym-4	pushq sym@GOTPCREL(%rip)
488b9800000000 3:3:sym+0	movq sym@GOT(%rax),%rbx
48b80000000000000000 2:27:sym+0	movabs $sym@GOT,%rax
48b80000000000000000 2:25:sym+0	movabs $sym@GOTOFF,%rax
48b80000000000000000 2:29:_GLOBAL_OFFSET_TABLE_+2	movabs $_GLOBAL_OFFSET_TABLE_,%rax
488d0500000000 3:26:_GLOBAL_OFFSET_TABLE_-4	leaq _GLOBAL_OFFSET_TABLE_(%rip),%rax
48b80000000000000000 2:31:sym+0	movabs $sym@PLTOFF,%rax
c7050000000001000000 2:2:sym-8	movl $1,sym(%rip)
66c705000000000100 3:2:sym-6	movw $1,sym(%rip)
813d0000000000100000 2:2:sym-8	cmpl $0x1000,sym(%rip)
66813d000000000010 3:2:sym-6	cmpw $0x1000,sym(%rip)
4883050000000001 3:2:sym-5	addq $1,sym(%rip)
4881050000000000100000 3:2:sym-8	addq $0x1000,sym(%rip)
f6050000000001 2:2:sym-5	testb $1,sym(%rip)
f7050000000001000000 2:2:sym+0	testl $1,sym+8(%rip)
c1250000000002 2:2:sym-5	shll $2,sym(%rip)
6b050000000005 2:2:sym-5	imull $5,sym(%rip),%eax
48690500000000f4010000 3:2:sym-8	imulq $500,sym(%rip),%rax
66690500000000f401 3:2:sym-6	imulw $500,sym(%rip),%ax
0fa4050000000003 3:2:sym-5	shldl $3,%eax,sym(%rip)
0fba250000000003 3:2:sym-5	btl $3,sym(%rip)
c0050000000003 2:2:sym-5	rolb $3,sym(%rip)
c7050000000001000000 2:9:sym-8	movl $1,sym@GOTPCREL(%rip)
488b05f9ffffff	.L1: | movq .L1(%rip),%rax
488b0500000000	movq 1f(%rip),%rax | 1:
8b0500000000 2:2:.data-4	.data | lv: .long 0 | .text | movl lv(%rip),%eax
c7050000000005000000 2:2:.data-4	.data | .long 0 | lv: .long 0 | .text | movl $5,lv(%rip)
48c7c000000000 3:11:.data+4	.data | .long 0 | lv: .long 0 | .text | movq $lv,%rax
48b80000000000000000 2:1:.data+4	.data | .long 0 | lv: .long 0 | .text | movabs $lv,%rax
e800000000 1:2:.data+0	.data | .long 0 | lv: .long 0 | .text | call lv
8b0500000000 2:2:gv-4	.globl gv | .data | .long 0 | gv: .long 0 | .text | movl gv(%rip),%eax
488d0500000000 3:2:.rodata+4	.section .rodata | .long 0 | lv: .long 0 | .text | leaq lv+4(%rip),%rax
e800000000 1:4:wk-4	.weak wk | call wk
e800000000 1:4:hd-4	.hidden hd | call hd
90e8faffffff	lf: nop | call lf
90e800000000 2:4:gf-4	.globl gf | gf: nop | call gf
90ebfd	.globl gf | gf: nop | jmp gf
9074fd	.globl gf | gf: nop | je gf
90e800000000 2:4:gf-4	.globl gf | gf: nop | call gf@PLT
90488d0500000000 4:2:gf-4	.globl gf | gf: nop | leaq gf(%rip),%rax
668cc0	movw %es,%ax
8cc0	movl %es,%eax
8cc0	mov %es,%eax
668cc0	mov %es,%ax
8ec0	movw %ax,%es
8ec0	movl %eax,%es
8ec0	mov %eax,%es
8ec0	mov %ax,%es
8c00	movw %es,(%rax)
8c03	mov %es,(%rbx)
8e00	movw (%rax),%es
8e042500000000 3:11:sym+0	mov sym,%es
8c042500000000 3:11:sym+0	mov %es,sym
refused	movl %es,(%rax)
refused	movl (%rax),%es
8cc0	mov %es,%rax
8cc0	movq %es,%rax
8ec0	mov %rax,%es
refused	movb %al,%es
refused	mov %es,%al
refused	mov %es,%ds
refused	mov $1,%es
668cc8	movw %cs,%ax
8cc8	movl %cs,%eax
8cc8	mov %cs,%eax
668cc8	mov %cs,%ax
8ec8	movw %ax,%cs
8ec8	movl %eax,%cs
8ec8	mov %eax,%cs
8ec8	mov %ax,%cs
8c08	movw %cs,(%rax)
8c0b	mov %cs,(%rbx)
8e08	movw (%rax),%cs
8e0c2500000000 3:11:sym+0	mov sym,%cs
8c0c2500000000 3:11:sym+0	mov %cs,sym
refused	movl %cs,(%rax)
refused	movl (%rax),%cs
8cc8	mov %cs,%rax
8cc8	movq %cs,%rax
8ec8	mov %rax,%cs
refused	movb %al,%cs
refused	mov %cs,%al
refused	mov %cs,%ds
refused	mov $1,%cs
668cd0	movw %ss,%ax
8cd0	movl %ss,%eax
8cd0	mov %ss,%eax
668cd0	mov %ss,%ax
8ed0	movw %ax,%ss
8ed0	movl %eax,%ss
8ed0	mov %eax,%ss
8ed0	mov %ax,%ss
8c10	movw %ss,(%rax)
8c13	mov %ss,(%rbx)
8e10	movw (%rax),%ss
8e142500000000 3:11:sym+0	mov sym,%ss
8c142500000000 3:11:sym+0	mov %ss,sym
refused	movl %ss,(%rax)
refused	movl (%rax),%ss
8cd0	mov %ss,%rax
8cd0	movq %ss,%rax
8ed0	mov %rax,%ss
refused	movb %al,%ss
refused	mov %ss,%al
refused	mov %ss,%ds
refused	mov $1,%ss
668cd8	movw %ds,%ax
8cd8	movl %ds,%eax
8cd8	mov %ds,%eax
668cd8	mov %ds,%ax
8ed8	movw %ax,%ds
8ed8	movl %eax,%ds
8ed8	mov %eax,%ds
8ed8	mov %ax,%ds
8c18	movw %ds,(%rax)
8c1b	mov %ds,(%rbx)
8e18	movw (%rax),%ds
8e1c2500000000 3:11:sym+0	mov sym,%ds
8c1c2500000000 3:11:sym+0	mov %ds,sym
refused	movl %ds,(%rax)
refused	movl (%rax),%ds
8cd8	mov %ds,%rax
8cd8	movq %ds,%rax
8ed8	mov %rax,%ds
refused	movb %al,%ds
refused	mov %ds,%al
refused	mov %ds,%ds
refused	mov $1,%ds
668ce0	movw %fs,%ax
8ce0	movl %fs,%eax
8ce0	mov %fs,%eax
668ce0	mov %fs,%ax
8ee0	movw %ax,%fs
8ee0	movl %eax,%fs
8ee0	mov %eax,%fs
8ee0	mov %ax,%fs
8c20	movw %fs,(%rax)
8c23	mov %fs,(%rbx)
8e20	movw (%rax),%fs
8e242500000000 3:11:sym+0	mov sym,%fs
8c242500000000 3:11:sym+0	mov %fs,sym
refused	movl %fs,(%rax)
refused	movl (%rax),%fs
8ce0	mov %fs,%rax
8ce0	movq %fs,%rax
8ee0	mov %rax,%fs
refused	movb %al,%fs
refused	mov %fs,%al
refused	mov %fs,%ds
refused	mov $1,%fs
668ce8	movw %gs,%ax
8ce8	movl %gs,%eax
8ce8	mov %gs,%eax
668ce8	mov %gs,%ax
8ee8	movw %ax,%gs
8ee8	movl %eax,%gs
8ee8	mov %eax,%gs
8ee8	mov %ax,%gs
8c28	movw %gs,(%rax)
8c2b	mov %gs,(%rbx)
8e28	movw (%rax),%gs
8e2c2500000000 3:11:sym+0	mov sym,%gs
8c2c2500000000 3:11:sym+0	mov %gs,sym
refused	movl %gs,(%rax)
refused	movl (%rax),%gs
8ce8	mov %gs,%rax
8ce8	movq %gs,%rax
8ee8	mov %rax,%gs
refused	movb %al,%gs
refused	mov %gs,%al
refused	mov %gs,%ds
refused	mov $1,%gs
0f20c0	mov %cr0,%rax
0f22c0	mov %rax,%cr0
0f20c3	movq %cr0,%rbx
0f22c7	movq %rdi,%cr0
refused	mov %cr0,%eax
refused	mov (%rax),%cr0
0f20d0	mov %cr2,%rax
0f22d0	mov %rax,%cr2
0f20d3	movq %cr2,%rbx
0f22d7	movq %rdi,%cr2
refused	mov %cr2,%eax
refused	mov (%rax),%cr2
0f20d8	mov %cr3,%rax
0f22d8	mov %rax,%cr3
0f20db	movq %cr3,%rbx
0f22df	movq %rdi,%cr3
refused	mov %cr3,%eax
refused	mov (%rax),%cr3
0f20e0	mov %cr4,%rax
0f22e0	mov %rax,%cr4
0f20e3	movq %cr4,%rbx
0f22e7	movq %rdi,%cr4
refused	mov %cr4,%eax
refused	mov (%rax),%cr4
440f20c0	mov %cr8,%rax
440f22c0	mov %rax,%cr8
440f20c3	movq %cr8,%rbx
440f22c7	movq %rdi,%cr8
refused	mov %cr8,%eax
refused	mov (%rax),%cr8
0f20c8	mov %cr1,%rax
0f22c8	mov %rax,%cr1
0f20cb	movq %cr1,%rbx
0f22cf	movq %rdi,%cr1
refused	mov %cr1,%eax
refused	mov (%rax),%cr1
0f20e8	mov %cr5,%rax
0f22e8	mov %rax,%cr5
0f20eb	movq %cr5,%rbx
0f22ef	movq %rdi,%cr5
refused	mov %cr5,%eax
refused	mov (%rax),%cr5
0f21c0	mov %dr0,%rax
0f23c0	mov %rax,%dr0
0f21c3	movq %dr0,%rbx
0f23c7	movq %rdi,%dr0
refused	mov %dr0,%eax
refused	mov (%rax),%dr0
0f21c8	mov %dr1,%rax
0f23c8	mov %rax,%dr1
0f21cb	movq %dr1,%rbx
0f23cf	movq %rdi,%dr1
refused	mov %dr1,%eax
refused	mov (%rax),%dr1
0f21d0	mov %dr2,%rax
0f23d0	mov %rax,%dr2
0f21d3	movq %dr2,%rbx
0f23d7	movq %rdi,%dr2
refused	mov %dr2,%eax
refused	mov (%rax),%dr2
0f21d8	mov %dr3,%rax
0f23d8	mov %rax,%dr3
0f21db	movq %dr3,%rbx
0f23df	movq %rdi,%dr3
refused	mov %dr3,%eax
refused	mov (%rax),%dr3
0f21f0	mov %dr6,%rax
0f23f0	mov %rax,%dr6
0f21f3	movq %dr6,%rbx
0f23f7	movq %rdi,%dr6
refused	mov %dr6,%eax
refused	mov (%rax),%dr6
0f21f8	mov %dr7,%rax
0f23f8	mov %rax,%dr7
0f21fb	movq %dr7,%rbx
0f23ff	movq %rdi,%dr7
refused	mov %dr7,%eax
refused	mov (%rax),%dr7
0f21c0	mov %db0,%rax
0f23c0	mov %rax,%db0
0f21c3	movq %db0,%rbx
0f23c7	movq %rdi,%db0
refused	mov %db0,%eax
refused	mov (%rax),%db0
0f21f8	mov %db7,%rax
0f23f8	mov %rax,%db7
0f21fb	movq %db7,%rbx
0f23ff	movq %rdi,%db7
refused	mov %db7,%eax
refused	mov (%rax),%db7
0f21e0	mov %dr4,%rax
0f23e0	mov %rax,%dr4
0f21e3	movq %dr4,%rbx
0f23e7	movq %rdi,%dr4
refused	mov %dr4,%eax
refused	mov (%rax),%dr4
refused	mov %tr6,%rax
refused	mov %rax,%tr6
refused	movq %tr6,%rbx
refused	movq %rdi,%tr6
refused	mov %tr6,%eax
refused	mov (%rax),%tr6
refused	mov %tr7,%rax
refused	mov %rax,%tr7
refused	movq %tr7,%rbx
refused	movq %rdi,%tr7
refused	mov %tr7,%eax
refused	mov (%rax),%tr7
440f20c8	mov %cr9,%rax
440f22c8	mov %rax,%cr9
440f20cb	movq %cr9,%rbx
440f22cf	movq %rdi,%cr9
refused	mov %cr9,%eax
refused	mov (%rax),%cr9
410f20c0	mov %cr0,%r8
410f22d9	mov %r9,%cr3
450f22c7	mov %r15,%cr8
410f21fc	mov %dr7,%r12
410f23c2	mov %r10,%dr0
8b042534120000	movl 0x1234,%eax
648a042500000000 4:11:sym+0	movb %fs:sym,%al
6589042510000000	movl %eax,%gs:0x10
658b042500000000	movl %gs:0,%eax
648b042500000000	movl %fs:0,%eax
8b042500000000 3:11:sym+0	mov sym,%eax
6689042500000000 4:11:sym+0	mov %ax,sym
8b1c2500000000 3:11:sym+0	movl sym,%ebx
660fbed8	movsbw %al,%bx
0fbed8	movsbl %al,%ebx
0fbfd8	movswl %ax,%ebx
660fbe18	movsbw (%rax),%bx
0fbe1c2500000000 4:11:sym+0	movsbl sym,%ebx
0fbf5d08	movswl 8(%rbp),%ebx
660fbed8	movsx %al,%bx
0fbed8	movsx %al,%ebx
0fbfd8	movsx %ax,%ebx
0fbed8	movsxb %al,%ebx
0fbfd8	movsxw %ax,%ebx
0fbed8	movsb %al,%ebx
0fbfd8	movsw %ax,%ebx
0fbe18	movsx (%rax),%ebx
refused	movsbl %ax,%ebx
refused	movswl %al,%ebx
refused	movsbl %al,%bx
refused	movsbw %al,%ebx
refused	movswl %ax,%bx
0fbede	movsbl %dh,%ebx
refused	movsbl %eax,%ebx
660fbe18	movsxb (%rax),%bx
0fbf18	movsxw (%rax),%ebx
refused	movsbl %al,(%rax)
refused	movsww %ax,%bx
63d8	movsx %eax,%ebx
refused	movsxb %al,%bl
4c0fbe08	movsbq (%rax),%r9
4c0fbf0d00000000 4:2:sym-4	movswq sym(%rip),%r9
480fbed8	movsx %al,%rbx
480fbfd8	movsx %ax,%rbx
480fbede	movsbq %sil,%rbx
440fbece	movsbl %sil,%r9d
66450fbed1	movsbw %r9b,%r10w
400fbec4	movsbl %spl,%eax
480fbed8	movsxb %al,%rbx
480fbfd8	movsxw %ax,%rbx
4863d8	movsx %eax,%rbx
4863d8	movsxl %eax,%rbx
486318	movslq (%rax),%rbx
486318	movsxd (%rax),%rbx
63d8	movsxd %eax,%ebx
refused	movsxd %ax,%bx
refused	movsbq %rax,%rbx
refused	movslq %rax,%rbx
refused	movslq %eax,%ebx
660fb6d8	movzbw %al,%bx
0fb6d8	movzbl %al,%ebx
0fb7d8	movzwl %ax,%ebx
660fb618	movzbw (%rax),%bx
0fb61c2500000000 4:11:sym+0	movzbl sym,%ebx
0fb75d08	movzwl 8(%rbp),%ebx
660fb6d8	movzx %al,%bx
0fb6d8	movzx %al,%ebx
0fb7d8	movzx %ax,%ebx
0fb6d8	movzxb %al,%ebx
0fb7d8	movzxw %ax,%ebx
0fb6d8	movzb %al,%ebx
0fb7d8	movzw %ax,%ebx
0fb618	movzx (%rax),%ebx
refused	movzbl %ax,%ebx
refused	movzwl %al,%ebx
refused	movzbl %al,%bx
refused	movzbw %al,%ebx
refused	movzwl %ax,%bx
0fb6de	movzbl %dh,%ebx
refused	movzbl %eax,%ebx
660fb618	movzxb (%rax),%bx
0fb718	movzxw (%rax),%ebx
refused	movzbl %al,(%rax)
refused	movzww %ax,%bx
refused	movzx %eax,%ebx
refused	movzxb %al,%bl
4c0fb608	movzbq (%rax),%r9
4c0fb70d00000000 4:2:sym-4	movzwq sym(%rip),%r9
480fb6d8	movzx %al,%rbx
480fb7d8	movzx %ax,%rbx
480fb6de	movzbq %sil,%rbx
440fb6ce	movzbl %sil,%r9d
66450fb6d1	movzbw %r9b,%r10w
400fb6c4	movzbl %spl,%eax
480fb6d8	movzxb %al,%rbx
480fb7d8	movzxw %ax,%rbx
refused	movzx %eax,%rbx
refused	movzxl %eax,%rbx
refused	movzlq (%rax),%rbx
refused	movzxd %eax,%rbx
refused	movzxd (%rax),%rbx
refused	movzxd %eax,%ebx
refused	movzxd %ax,%bx
refused	movzbq %rax,%rbx
refused	movzlq %rax,%rbx
refused	movzlq %eax,%ebx
00c0	addb %al,%al
0003	addb %al,(%rbx)
0203	addb (%rbx),%al
00042500000000 3:11:sym+0	addb %al,sym
02042500000000 3:11:sym+0	addb sym,%al
0003	add %al,(%rbx)
refused	add 4((%rbx,%rsi,2),%al
047f	addb $127,%al
0480	addb $128,%al
0480	addb $-128,%al
047f	addb $-129,%al
04ff	addb $255,%al
04ff	addb $0x7fff,%al
0400	addb $0x8000,%al
0480	addb $0xff80,%al
04ff	addb $0xffff,%al
0400	addb $0x10000,%al
0480	addb $0xffffff80,%al
0400 1:14:sym+0	addb $sym,%al
00c8	addb %cl,%al
00c1	addb %al,%cl
000b	addb %cl,(%rbx)
020b	addb (%rbx),%cl
000c2500000000 3:11:sym+0	addb %cl,sym
020c2500000000 3:11:sym+0	addb sym,%cl
000b	add %cl,(%rbx)
refused	add 4((%rbx,%rsi,2),%cl
80c100	addb $0,%cl
80c101	addb $1,%cl
80c1ff	addb $-1,%cl
80c17f	addb $127,%cl
80c180	addb $128,%cl
80c180	addb $-128,%cl
80c17f	addb $-129,%cl
80c1ff	addb $255,%cl
80c100	addb $256,%cl
80c1ff	addb $0x7fff,%cl
80c100	addb $0x8000,%cl
80c180	addb $0xff80,%cl
80c1ff	addb $0xffff,%cl
80c100	addb $0x10000,%cl
80c1ff	addb $0x7fffffff,%cl
80c100	addb $0x80000000,%cl
80c180	addb $0xffffff80,%cl
80c1ff	addb $0xffffffff,%cl
80c100 2:14:sym+0	addb $sym,%cl
00f8	addb %bh,%al
00c7	addb %al,%bh
003b	addb %bh,(%rbx)
023b	addb (%rbx),%bh
003c2500000000 3:11:sym+0	addb %bh,sym
023c2500000000 3:11:sym+0	addb sym,%bh
003b	add %bh,(%rbx)
refused	add 4((%rbx,%rsi,2),%bh
80c700	addb $0,%bh
80c701	addb $1,%bh
80c7ff	addb $-1,%bh
80c77f	addb $127,%bh
80c780	addb $128,%bh
80c780	addb $-128,%bh
80c77f	addb $-129,%bh
80c7ff	addb $255,%bh
80c700	addb $256,%bh
80c7ff	addb $0x7fff,%bh
80c700	addb $0x8000,%bh
80c780	addb $0xff80,%bh
80c7ff	addb $0xffff,%bh
80c700	addb $0x10000,%bh
80c7ff	addb $0x7fffffff,%bh
80c700	addb $0x80000000,%bh
80c780	addb $0xffffff80,%bh
80c7ff	addb $0xffffffff,%bh
80c700 2:14:sym+0	addb $sym,%bh
4000f0	addb %sil,%al
4000c6	addb %al,%sil
400033	addb %sil,(%rbx)
400233	addb (%rbx),%sil
4000342500000000 4:11:sym+0	addb %sil,sym
4002342500000000 4:11:sym+0	addb sym,%sil
400033	add %sil,(%rbx)
refused	add 4((%rbx,%rsi,2),%sil
4080c600	addb $0,%sil
4080c601	addb $1,%sil
4080c6ff	addb $-1,%sil
4080c67f	addb $127,%sil
4080c680	addb $128,%sil
4080c680	addb $-128,%sil
4080c67f	addb $-129,%sil
4080c6ff	addb $255,%sil
4080c600	addb $256,%sil
4080c6ff	addb $0x7fff,%sil
4080c600	addb $0x8000,%sil
4080c680	addb $0xff80,%sil
4080c6ff	addb $0xffff,%sil
4080c600	addb $0x10000,%sil
4080c6ff	addb $0x7fffffff,%sil
4080c600	addb $0x80000000,%sil
4080c680	addb $0xffffff80,%sil
4080c6ff	addb $0xffffffff,%sil
4080c600 3:14:sym+0	addb $sym,%sil
4400c8	addb %r9b,%al
4100c1	addb %al,%r9b
44000b	addb %r9b,(%rbx)
44020b	addb (%rbx),%r9b
44000c2500000000 4:11:sym+0	addb %r9b,sym
44020c2500000000 4:11:sym+0	addb sym,%r9b
44000b	add %r9b,(%rbx)
refused	add 4((%rbx,%rsi,2),%r9b
4180c100	addb $0,%r9b
4180c101	addb $1,%r9b
4180c1ff	addb $-1,%r9b
4180c17f	addb $127,%r9b
4180c180	addb $128,%r9b
4180c180	addb $-128,%r9b
4180c17f	addb $-129,%r9b
4180c1ff	addb $255,%r9b
4180c100	addb $256,%r9b
4180c1ff	addb $0x7fff,%r9b
4180c100	addb $0x8000,%r9b
4180c180	addb $0xff80,%r9b
4180c1ff	addb $0xffff,%r9b
4180c100	addb $0x10000,%r9b
4180c1ff	addb $0x7fffffff,%r9b
4180c100	addb $0x80000000,%r9b
4180c180	addb $0xffffff80,%r9b
4180c1ff	addb $0xffffffff,%r9b
4180c100 3:14:sym+0	addb $sym,%r9b
800300	addb $0,(%rbx)
8004250000000000 3:11:sym+0	addb $0,sym
80448dfc00	addb $0,-4(%rbp,%rcx,4)
800301	addb $1,(%rbx)
8004250000000001 3:11:sym+0	addb $1,sym
80448dfc01	addb $1,-4(%rbp,%rcx,4)
8003ff	addb $-1,(%rbx)
80042500000000ff 3:11:sym+0	addb $-1,sym
80448dfcff	addb $-1,-4(%rbp,%rcx,4)
80037f	addb $127,(%rbx)
800425000000007f 3:11:sym+0	addb $127,sym
80448dfc7f	addb $127,-4(%rbp,%rcx,4)
800380	addb $128,(%rbx)
8004250000000080 3:11:sym+0	addb $128,sym
80448dfc80	addb $128,-4(%rbp,%rcx,4)
800380	addb $-128,(%rbx)
8004250000000080 3:11:sym+0	addb $-128,sym
80448dfc80	addb $-128,-4(%rbp,%rcx,4)
80037f	addb $-129,(%rbx)
800425000000007f 3:11:sym+0	addb $-129,sym
80448dfc7f	addb $-129,-4(%rbp,%rcx,4)
8003ff	addb $255,(%rbx)
80042500000000ff 3:11:sym+0	addb $255,sym
80448dfcff	addb $255,-4(%rbp,%rcx,4)
800300	addb $256,(%rbx)
8004250000000000 3:11:sym+0	addb $256,sym
80448dfc00	addb $256,-4(%rbp,%rcx,4)
8003ff	addb $0x7fff,(%rbx)
80042500000000ff 3:11:sym+0	addb $0x7fff,sym
80448dfcff	addb $0x7fff,-4(%rbp,%rcx,4)
800300	addb $0x8000,(%rbx)
8004250000000000 3:11:sym+0	addb $0x8000,sym
80448dfc00	addb $0x8000,-4(%rbp,%rcx,4)
800380	addb $0xff80,(%rbx)
8004250000000080 3:11:sym+0	addb $0xff80,sym
80448dfc80	addb $0xff80,-4(%rbp,%rcx,4)
8003ff	addb $0xffff,(%rbx)
80042500000000ff 3:11:sym+0	addb $0xffff,sym
80448dfcff	addb $0xffff,-4(%rbp,%rcx,4)
800300	addb $0x10000,(%rbx)
8004250000000000 3:11:sym+0	addb $0x10000,sym
80448dfc00	addb $0x10000,-4(%rbp,%rcx,4)
8003ff	addb $0x7fffffff,(%rbx)
80042500000000ff 3:11:sym+0	addb $0x7fffffff,sym
80448dfcff	addb $0x7fffffff,-4(%rbp,%rcx,4)
800300	addb $0x80000000,(%rbx)
8004250000000000 3:11:sym+0	addb $0x80000000,sym
80448dfc00	addb $0x80000000,-4(%rbp,%rcx,4)
800380	addb $0xffffff80,(%rbx)
8004250000000080 3:11:sym+0	addb $0xffffff80,sym
80448dfc80	addb $0xffffff80,-4(%rbp,%rcx,4)
8003ff	addb $0xffffffff,(%rbx)
80042500000000ff 3:11:sym+0	addb $0xffffffff,sym
80448dfcff	addb $0xffffffff,-4(%rbp,%rcx,4)
800300 2:14:sym+0	addb $sym,(%rbx)
8004250000000000 3:11:sym+0 7:14:sym+0	addb $sym,sym
80448dfc00 4:14:sym+0	addb $sym,-4(%rbp,%rcx,4)
80c101	add $1,%cl
830301	add $1,(%rbx)
refused	addb $1,$2
refused	addb %al,$2
refused	addb (%rbx),(%rbx)
f0000b	lock addb %cl,(%rbx)
f0800301	lock addb $1,(%rbx)
refused	lock addb (%rbx),%cl
refused	lock addb %cl,%al
6601c0	addw %ax,%ax
660103	addw %ax,(%rbx)
660303	addw (%rbx),%ax
6601042500000000 4:11:sym+0	addw %ax,sym
6603042500000000 4:11:sym+0	addw sym,%ax
660103	add %ax,(%rbx)
refused	add 4((%rbx,%rsi,2),%ax
6683c07f	addw $127,%ax
66058000	addw $128,%ax
6683c080	addw $-128,%ax
66057fff	addw $-129,%ax
6605ff00	addw $255,%ax
66050001	addw $256,%ax
6605ff7f	addw $0x7fff,%ax
66050080	addw $0x8000,%ax
6683c080	addw $0xff80,%ax
6683c0ff	addw $0xffff,%ax
66050000	addw $0x10000,%ax
6683c080	addw $0xffffff80,%ax
66050000 2:12:sym+0	addw $sym,%ax
6601d8	addw %bx,%ax
6601c3	addw %ax,%bx
66011b	addw %bx,(%rbx)
66031b	addw (%rbx),%bx
66011c2500000000 4:11:sym+0	addw %bx,sym
66031c2500000000 4:11:sym+0	addw sym,%bx
66011b	add %bx,(%rbx)
refused	add 4((%rbx,%rsi,2),%bx
6683c37f	addw $127,%bx
6681c38000	addw $128,%bx
6683c380	addw $-128,%bx
6681c37fff	addw $-129,%bx
6681c3ff00	addw $255,%bx
6681c30001	addw $256,%bx
6681c3ff7f	addw $0x7fff,%bx
6681c30080	addw $0x8000,%bx
6683c380	addw $0xff80,%bx
6683c3ff	addw $0xffff,%bx
6681c30000	addw $0x10000,%bx
6683c380	addw $0xffffff80,%bx
6681c30000 3:12:sym+0	addw $sym,%bx
6601f0	addw %si,%ax
6601c6	addw %ax,%si
660133	addw %si,(%rbx)
660333	addw (%rbx),%si
6601342500000000 4:11:sym+0	addw %si,sym
6603342500000000 4:11:sym+0	addw sym,%si
660133	add %si,(%rbx)
refused	add 4((%rbx,%rsi,2),%si
6683c600	addw $0,%si
6683c601	addw $1,%si
6683c6ff	addw $-1,%si
6683c67f	addw $127,%si
6681c68000	addw $128,%si
6683c680	addw $-128,%si
6681c67fff	addw $-129,%si
6681c6ff00	addw $255,%si
6681c60001	addw $256,%si
6681c6ff7f	addw $0x7fff,%si
6681c60080	addw $0x8000,%si
6683c680	addw $0xff80,%si
6683c6ff	addw $0xffff,%si
6681c60000	addw $0x10000,%si
6681c6ffff	addw $0x7fffffff,%si
6681c60000	addw $0x80000000,%si
6683c680	addw $0xffffff80,%si
6683c6ff	addw $0xffffffff,%si
6681c60000 3:12:sym+0	addw $sym,%si
664401c8	addw %r9w,%ax
664101c1	addw %ax,%r9w
6644010b	addw %r9w,(%rbx)
6644030b	addw (%rbx),%r9w
6644010c2500000000 5:11:sym+0	addw %r9w,sym
6644030c2500000000 5:11:sym+0	addw sym,%r9w
6644010b	add %r9w,(%rbx)
refused	add 4((%rbx,%rsi,2),%r9w
664183c100	addw $0,%r9w
664183c101	addw $1,%r9w
664183c1ff	addw $-1,%r9w
664183c17f	addw $127,%r9w
664181c18000	addw $128,%r9w
664183c180	addw $-128,%r9w
664181c17fff	addw $-129,%r9w
664181c1ff00	addw $255,%r9w
664181c10001	addw $256,%r9w
664181c1ff7f	addw $0x7fff,%r9w
664181c10080	addw $0x8000,%r9w
664183c180	addw $0xff80,%r9w
664183c1ff	addw $0xffff,%r9w
664181c10000	addw $0x10000,%r9w
664181c1ffff	addw $0x7fffffff,%r9w
664181c10000	addw $0x80000000,%r9w
664183c180	addw $0xffffff80,%r9w
664183c1ff	addw $0xffffffff,%r9w
664181c10000 4:12:sym+0	addw $sym,%r9w
66830300	addw $0,(%rbx)
668304250000000000 4:11:sym+0	addw $0,sym
6683448dfc00	addw $0,-4(%rbp,%rcx,4)
66830301	addw $1,(%rbx)
668304250000000001 4:11:sym+0	addw $1,sym
6683448dfc01	addw $1,-4(%rbp,%rcx,4)
668303ff	addw $-1,(%rbx)
6683042500000000ff 4:11:sym+0	addw $-1,sym
6683448dfcff	addw $-1,-4(%rbp,%rcx,4)
6683037f	addw $127,(%rbx)
66830425000000007f 4:11:sym+0	addw $127,sym
6683448dfc7f	addw $127,-4(%rbp,%rcx,4)
6681038000	addw $128,(%rbx)
66810425000000008000 4:11:sym+0	addw $128,sym
6681448dfc8000	addw $128,-4(%rbp,%rcx,4)
66830380	addw $-128,(%rbx)
668304250000000080 4:11:sym+0	addw $-128,sym
6683448dfc80	addw $-128,-4(%rbp,%rcx,4)
6681037fff	addw $-129,(%rbx)
66810425000000007fff 4:11:sym+0	addw $-129,sym
6681448dfc7fff	addw $-129,-4(%rbp,%rcx,4)
668103ff00	addw $255,(%rbx)
6681042500000000ff00 4:11:sym+0	addw $255,sym
6681448dfcff00	addw $255,-4(%rbp,%rcx,4)
6681030001	addw $256,(%rbx)
66810425000000000001 4:11:sym+0	addw $256,sym
6681448dfc0001	addw $256,-4(%rbp,%rcx,4)
668103ff7f	addw $0x7fff,(%rbx)
6681042500000000ff7f 4:11:sym+0	addw $0x7fff,sym
6681448dfcff7f	addw $0x7fff,-4(%rbp,%rcx,4)
6681030080	addw $0x8000,(%rbx)
66810425000000000080 4:11:sym+0	addw $0x8000,sym
6681448dfc0080	addw $0x8000,-4(%rbp,%rcx,4)
66830380	addw $0xff80,(%rbx)
668304250000000080 4:11:sym+0	addw $0xff80,sym
6683448dfc80	addw $0xff80,-4(%rbp,%rcx,4)
668303ff	addw $0xffff,(%rbx)
6683042500000000ff 4:11:sym+0	addw $0xffff,sym
6683448dfcff	addw $0xffff,-4(%rbp,%rcx,4)
6681030000	addw $0x10000,(%rbx)
66810425000000000000 4:11:sym+0	addw $0x10000,sym
6681448dfc0000	addw $0x10000,-4(%rbp,%rcx,4)
668103ffff	addw $0x7fffffff,(%rbx)
6681042500000000ffff 4:11:sym+0	addw $0x7fffffff,sym
6681448dfcffff	addw $0x7fffffff,-4(%rbp,%rcx,4)
6681030000	addw $0x80000000,(%rbx)
66810425000000000000 4:11:sym+0	addw $0x80000000,sym
6681448dfc0000	addw $0x80000000,-4(%rbp,%rcx,4)
66830380	addw $0xffffff80,(%rbx)
668304250000000080 4:11:sym+0	addw $0xffffff80,sym
6683448dfc80	addw $0xffffff80,-4(%rbp,%rcx,4)
668303ff	addw $0xffffffff,(%rbx)
6683042500000000ff 4:11:sym+0	addw $0xffffffff,sym
6683448dfcff	addw $0xffffffff,-4(%rbp,%rcx,4)
6681030000 3:12:sym+0	addw $sym,(%rbx)
66810425000000000000 4:11:sym+0 8:12:sym+0	addw $sym,sym
6681448dfc0000 5:12:sym+0	addw $sym,-4(%rbp,%rcx,4)
6683c301	add $1,%bx
refused	addw $1,$2
refused	addw %ax,$2
refused	addw (%rbx),(%rbx)
66f0011b	lock addw %bx,(%rbx)
66f0830301	lock addw $1,(%rbx)
refused	lock addw (%rbx),%bx
refused	lock addw %bx,%ax
01c0	addl %eax,%eax
0103	addl %eax,(%rbx)
0303	addl (%rbx),%eax
01042500000000 3:11:sym+0	addl %eax,sym
03042500000000 3:11:sym+0	addl sym,%eax
0103	add %eax,(%rbx)
refused	add 4((%rbx,%rsi,2),%eax
83c07f	addl $127,%eax
0580000000	addl $128,%eax
83c080	addl $-128,%eax
057fffffff	addl $-129,%eax
05ff000000	addl $255,%eax
0500010000	addl $256,%eax
05ff7f0000	addl $0x7fff,%eax
0500800000	addl $0x8000,%eax
0580ff0000	addl $0xff80,%eax
05ffff0000	addl $0xffff,%eax
0500000100	addl $0x10000,%eax
83c080	addl $0xffffff80,%eax
0500000000 1:10:sym+0	addl $sym,%eax
01d8	addl %ebx,%eax
01c3	addl %eax,%ebx
011b	addl %ebx,(%rbx)
031b	addl (%rbx),%ebx
011c2500000000 3:11:sym+0	addl %ebx,sym
031c2500000000 3:11:sym+0	addl sym,%ebx
011b	add %ebx,(%rbx)
refused	add 4((%rbx,%rsi,2),%ebx
83c37f	addl $127,%ebx
81c380000000	addl $128,%ebx
83c380	addl $-128,%ebx
81c37fffffff	addl $-129,%ebx
81c3ff000000	addl $255,%ebx
81c300010000	addl $256,%ebx
81c3ff7f0000	addl $0x7fff,%ebx
81c300800000	addl $0x8000,%ebx
81c380ff0000	addl $0xff80,%ebx
81c3ffff0000	addl $0xffff,%ebx
81c300000100	addl $0x10000,%ebx
83c380	addl $0xffffff80,%ebx
81c300000000 2:10:sym+0	addl $sym,%ebx
01e0	addl %esp,%eax
01c4	addl %eax,%esp
0123	addl %esp,(%rbx)
0323	addl (%rbx),%esp
01242500000000 3:11:sym+0	addl %esp,sym
03242500000000 3:11:sym+0	addl sym,%esp
0123	add %esp,(%rbx)
refused	add 4((%rbx,%rsi,2),%esp
83c400	addl $0,%esp
83c401	addl $1,%esp
83c4ff	addl $-1,%esp
83c47f	addl $127,%esp
81c480000000	addl $128,%esp
83c480	addl $-128,%esp
81c47fffffff	addl $-129,%esp
81c4ff000000	addl $255,%esp
81c400010000	addl $256,%esp
81c4ff7f0000	addl $0x7fff,%esp
81c400800000	addl $0x8000,%esp
81c480ff0000	addl $0xff80,%esp
81c4ffff0000	addl $0xffff,%esp
81c400000100	addl $0x10000,%esp
81c4ffffff7f	addl $0x7fffffff,%esp
81c400000080	addl $0x80000000,%esp
83c480	addl $0xffffff80,%esp
83c4ff	addl $0xffffffff,%esp
81c400000000 2:10:sym+0	addl $sym,%esp
01e8	addl %ebp,%eax
01c5	addl %eax,%ebp
012b	addl %ebp,(%rbx)
032b	addl (%rbx),%ebp
012c2500000000 3:11:sym+0	addl %ebp,sym
032c2500000000 3:11:sym+0	addl sym,%ebp
012b	add %ebp,(%rbx)
refused	add 4((%rbx,%rsi,2),%ebp
83c500	addl $0,%ebp
83c501	addl $1,%ebp
83c5ff	addl $-1,%ebp
83c57f	addl $127,%ebp
81c580000000	addl $128,%ebp
83c580	addl $-128,%ebp
81c57fffffff	addl $-129,%ebp
81c5ff000000	addl $255,%ebp
81c500010000	addl $256,%ebp
81c5ff7f0000	addl $0x7fff,%ebp
81c500800000	addl $0x8000,%ebp
81c580ff0000	addl $0xff80,%ebp
81c5ffff0000	addl $0xffff,%ebp
81c500000100	addl $0x10000,%ebp
81c5ffffff7f	addl $0x7fffffff,%ebp
81c500000080	addl $0x80000000,%ebp
83c580	addl $0xffffff80,%ebp
83c5ff	addl $0xffffffff,%ebp
81c500000000 2:10:sym+0	addl $sym,%ebp
4401c8	addl %r9d,%eax
4101c1	addl %eax,%r9d
44010b	addl %r9d,(%rbx)
44030b	addl (%rbx),%r9d
44010c2500000000 4:11:sym+0	addl %r9d,sym
44030c2500000000 4:11:sym+0	addl sym,%r9d
44010b	add %r9d,(%rbx)
refused	add 4((%rbx,%rsi,2),%r9d
4183c100	addl $0,%r9d
4183c101	addl $1,%r9d
4183c1ff	addl $-1,%r9d
4183c17f	addl $127,%r9d
4181c180000000	addl $128,%r9d
4183c180	addl $-128,%r9d
4181c17fffffff	addl $-129,%r9d
4181c1ff000000	addl $255,%r9d
4181c100010000	addl $256,%r9d
4181c1ff7f0000	addl $0x7fff,%r9d
4181c100800000	addl $0x8000,%r9d
4181c180ff0000	addl $0xff80,%r9d
4181c1ffff0000	addl $0xffff,%r9d
4181c100000100	addl $0x10000,%r9d
4181c1ffffff7f	addl $0x7fffffff,%r9d
4181c100000080	addl $0x80000000,%r9d
4183c180	addl $0xffffff80,%r9d
4183c1ff	addl $0xffffffff,%r9d
4181c100000000 3:10:sym+0	addl $sym,%r9d
830300	addl $0,(%rbx)
8304250000000000 3:11:sym+0	addl $0,sym
83448dfc00	addl $0,-4(%rbp,%rcx,4)
830301	addl $1,(%rbx)
8304250000000001 3:11:sym+0	addl $1,sym
83448dfc01	addl $1,-4(%rbp,%rcx,4)
8303ff	addl $-1,(%rbx)
83042500000000ff 3:11:sym+0	addl $-1,sym
83448dfcff	addl $-1,-4(%rbp,%rcx,4)
83037f	addl $127,(%rbx)
830425000000007f 3:11:sym+0	addl $127,sym
83448dfc7f	addl $127,-4(%rbp,%rcx,4)
810380000000	addl $128,(%rbx)
8104250000000080000000 3:11:sym+0	addl $128,sym
81448dfc80000000	addl $128,-4(%rbp,%rcx,4)
830380	addl $-128,(%rbx)
8304250000000080 3:11:sym+0	addl $-128,sym
83448dfc80	addl $-128,-4(%rbp,%rcx,4)
81037fffffff	addl $-129,(%rbx)
810425000000007fffffff 3:11:sym+0	addl $-129,sym
81448dfc7fffffff	addl $-129,-4(%rbp,%rcx,4)
8103ff000000	addl $255,(%rbx)
81042500000000ff000000 3:11:sym+0	addl $255,sym
81448dfcff000000	addl $255,-4(%rbp,%rcx,4)
810300010000	addl $256,(%rbx)
8104250000000000010000 3:11:sym+0	addl $256,sym
81448dfc00010000	addl $256,-4(%rbp,%rcx,4)
8103ff7f0000	addl $0x7fff,(%rbx)
81042500000000ff7f0000 3:11:sym+0	addl $0x7fff,sym
81448dfcff7f0000	addl $0x7fff,-4(%rbp,%rcx,4)
810300800000	addl $0x8000,(%rbx)
8104250000000000800000 3:11:sym+0	addl $0x8000,sym
81448dfc00800000	addl $0x8000,-4(%rbp,%rcx,4)
810380ff0000	addl $0xff80,(%rbx)
8104250000000080ff0000 3:11:sym+0	addl $0xff80,sym
81448dfc80ff0000	addl $0xff80,-4(%rbp,%rcx,4)
8103ffff0000	addl $0xffff,(%rbx)
81042500000000ffff0000 3:11:sym+0	addl $0xffff,sym
81448dfcffff0000	addl $0xffff,-4(%rbp,%rcx,4)
810300000100	addl $0x10000,(%rbx)
8104250000000000000100 3:11:sym+0	addl $0x10000,sym
81448dfc00000100	addl $0x10000,-4(%rbp,%rcx,4)
8103ffffff7f	addl $0x7fffffff,(%rbx)
81042500000000ffffff7f 3:11:sym+0	addl $0x7fffffff,sym
81448dfcffffff7f	addl $0x7fffffff,-4(%rbp,%rcx,4)
810300000080	addl $0x80000000,(%rbx)
8104250000000000000080 3:11:sym+0	addl $0x80000000,sym
81448dfc00000080	addl $0x80000000,-4(%rbp,%rcx,4)
830380	addl $0xffffff80,(%rbx)
8304250000000080 3:11:sym+0	addl $0xffffff80,sym
83448dfc80	addl $0xffffff80,-4(%rbp,%rcx,4)
8303ff	addl $0xffffffff,(%rbx)
83042500000000ff 3:11:sym+0	addl $0xffffffff,sym
83448dfcff	addl $0xffffffff,-4(%rbp,%rcx,4)
810300000000 2:10:sym+0	addl $sym,(%rbx)
8104250000000000000000 3:11:sym+0 7:10:sym+0	addl $sym,sym
81448dfc00000000 4:10:sym+0	addl $sym,-4(%rbp,%rcx,4)
83c301	add $1,%ebx
refused	addl $1,$2
refused	addl %eax,$2
refused	addl (%rbx),(%rbx)
f0011b	lock addl %ebx,(%rbx)
f0830301	lock addl $1,(%rbx)
refused	lock addl (%rbx),%ebx
refused	lock addl %ebx,%eax
4801c0	addq %rax,%rax
480103	addq %rax,(%rbx)
480303	addq (%rbx),%rax
4801042500000000 4:11:sym+0	addq %rax,sym
4803042500000000 4:11:sym+0	addq sym,%rax
480103	add %rax,(%rbx)
refused	add 4((%rbx,%rsi,2),%rax
4883c07f	addq $127,%rax
480580000000	addq $128,%rax
4883c080	addq $-128,%rax
48057fffffff	addq $-129,%rax
4805ff000000	addq $255,%rax
480500010000	addq $256,%rax
4805ff7f0000	addq $0x7fff,%rax
480500800000	addq $0x8000,%rax
480580ff0000	addq $0xff80,%rax
4805ffff0000	addq $0xffff,%rax
480500000100	addq $0x10000,%rax
refused	addq $0xffffff80,%rax
480500000000 2:11:sym+0	addq $sym,%rax
4801d8	addq %rbx,%rax
4801c3	addq %rax,%rbx
48011b	addq %rbx,(%rbx)
48031b	addq (%rbx),%rbx
48011c2500000000 4:11:sym+0	addq %rbx,sym
48031c2500000000 4:11:sym+0	addq sym,%rbx
48011b	add %rbx,(%rbx)
refused	add 4((%rbx,%rsi,2),%rbx
4883c37f	addq $127,%rbx
4881c380000000	addq $128,%rbx
4883c380	addq $-128,%rbx
4881c37fffffff	addq $-129,%rbx
4881c3ff000000	addq $255,%rbx
4881c300010000	addq $256,%rbx
4881c3ff7f0000	addq $0x7fff,%rbx
4881c300800000	addq $0x8000,%rbx
4881c380ff0000	addq $0xff80,%rbx
4881c3ffff0000	addq $0xffff,%rbx
4881c300000100	addq $0x10000,%rbx
refused	addq $0xffffff80,%rbx
4881c300000000 3:11:sym+0	addq $sym,%rbx
4801e0	addq %rsp,%rax
4801c4	addq %rax,%rsp
480123	addq %rsp,(%rbx)
480323	addq (%rbx),%rsp
4801242500000000 4:11:sym+0	addq %rsp,sym
4803242500000000 4:11:sym+0	addq sym,%rsp
480123	add %rsp,(%rbx)
refused	add 4((%rbx,%rsi,2),%rsp
4883c400	addq $0,%rsp
4883c401	addq $1,%rsp
4883c4ff	addq $-1,%rsp
4883c47f	addq $127,%rsp
4881c480000000	addq $128,%rsp
4883c480	addq $-128,%rsp
4881c47fffffff	addq $-129,%rsp
4881c4ff000000	addq $255,%rsp
4881c400010000	addq $256,%rsp
4881c4ff7f0000	addq $0x7fff,%rsp
4881c400800000	addq $0x8000,%rsp
4881c480ff0000	addq $0xff80,%rsp
4881c4ffff0000	addq $0xffff,%rsp
4881c400000100	addq $0x10000,%rsp
4881c4ffffff7f	addq $0x7fffffff,%rsp
refused	addq $0x80000000,%rsp
refused	addq $0xffffff80,%rsp
refused	addq $0xffffffff,%rsp
4881c400000000 3:11:sym+0	addq $sym,%rsp
4c01e0	addq %r12,%rax
4901c4	addq %rax,%r12
4c0123	addq %r12,(%rbx)
4c0323	addq (%rbx),%r12
4c01242500000000 4:11:sym+0	addq %r12,sym
4c03242500000000 4:11:sym+0	addq sym,%r12
4c0123	add %r12,(%rbx)
refused	add 4((%rbx,%rsi,2),%r12
4983c400	addq $0,%r12
4983c401	addq $1,%r12
4983c4ff	addq $-1,%r12
4983c47f	addq $127,%r12
4981c480000000	addq $128,%r12
4983c480	addq $-128,%r12
4981c47fffffff	addq $-129,%r12
4981c4ff000000	addq $255,%r12
4981c400010000	addq $256,%r12
4981c4ff7f0000	addq $0x7fff,%r12
4981c400800000	addq $0x8000,%r12
4981c480ff0000	addq $0xff80,%r12
4981c4ffff0000	addq $0xffff,%r12
4981c400000100	addq $0x10000,%r12
4981c4ffffff7f	addq $0x7fffffff,%r12
refused	addq $0x80000000,%r12
refused	addq $0xffffff80,%r12
refused	addq $0xffffffff,%r12
4981c400000000 3:11:sym+0	addq $sym,%r12
4c01e8	addq %r13,%rax
4901c5	addq %rax,%r13
4c012b	addq %r13,(%rbx)
4c032b	addq (%rbx),%r13
4c012c2500000000 4:11:sym+0	addq %r13,sym
4c032c2500000000 4:11:sym+0	addq sym,%r13
4c012b	add %r13,(%rbx)
refused	add 4((%rbx,%rsi,2),%r13
4983c500	addq $0,%r13
4983c501	addq $1,%r13
4983c5ff	addq $-1,%r13
4983c57f	addq $127,%r13
4981c580000000	addq $128,%r13
4983c580	addq $-128,%r13
4981c57fffffff	addq $-129,%r13
4981c5ff000000	addq $255,%r13
4981c500010000	addq $256,%r13
4981c5ff7f0000	addq $0x7fff,%r13
4981c500800000	addq $0x8000,%r13
4981c580ff0000	addq $0xff80,%r13
4981c5ffff0000	addq $0xffff,%r13
4981c500000100	addq $0x10000,%r13
4981c5ffffff7f	addq $0x7fffffff,%r13
refused	addq $0x80000000,%r13
refused	addq $0xffffff80,%r13
refused	addq $0xffffffff,%r13
4981c500000000 3:11:sym+0	addq $sym,%r13
4c01c8	addq %r9,%rax
4901c1	addq %rax,%r9
4c010b	addq %r9,(%rbx)
4c030b	addq (%rbx),%r9
4c010c2500000000 4:11:sym+0	addq %r9,sym
4c030c2500000000 4:11:sym+0	addq sym,%r9
4c010b	add %r9,(%rbx)
refused	add 4((%rbx,%rsi,2),%r9
4983c100	addq $0,%r9
4983c101	addq $1,%r9
4983c1ff	addq $-1,%r9
4983c17f	addq $127,%r9
4981c180000000	addq $128,%r9
4983c180	addq $-128,%r9
4981c17fffffff	addq $-129,%r9
4981c1ff000000	addq $255,%r9
4981c100010000	addq $256,%r9
4981c1ff7f0000	addq $0x7fff,%r9
4981c100800000	addq $0x8000,%r9
4981c180ff0000	addq $0xff80,%r9
4981c1ffff0000	addq $0xffff,%r9
4981c100000100	addq $0x10000,%r9
4981c1ffffff7f	addq $0x7fffffff,%r9
refused	addq $0x80000000,%r9
refused	addq $0xffffff80,%r9
refused	addq $0xffffffff,%r9
4981c100000000 3:11:sym+0	addq $sym,%r9
48830300	addq $0,(%rbx)
488304250000000000 4:11:sym+0	addq $0,sym
4883448dfc00	addq $0,-4(%rbp,%rcx,4)
48830301	addq $1,(%rbx)
488304250000000001 4:11:sym+0	addq $1,sym
4883448dfc01	addq $1,-4(%rbp,%rcx,4)
488303ff	addq $-1,(%rbx)
4883042500000000ff 4:11:sym+0	addq $-1,sym
4883448dfcff	addq $-1,-4(%rbp,%rcx,4)
4883037f	addq $127,(%rbx)
48830425000000007f 4:11:sym+0	addq $127,sym
4883448dfc7f	addq $127,-4(%rbp,%rcx,4)
48810380000000	addq $128,(%rbx)
488104250000000080000000 4:11:sym+0	addq $128,sym
4881448dfc80000000	addq $128,-4(%rbp,%rcx,4)
48830380	addq $-128,(%rbx)
488304250000000080 4:11:sym+0	addq $-128,sym
4883448dfc80	addq $-128,-4(%rbp,%rcx,4)
4881037fffffff	addq $-129,(%rbx)
48810425000000007fffffff 4:11:sym+0	addq $-129,sym
4881448dfc7fffffff	addq $-129,-4(%rbp,%rcx,4)
488103ff000000	addq $255,(%rbx)
4881042500000000ff000000 4:11:sym+0	addq $255,sym
4881448dfcff000000	addq $255,-4(%rbp,%rcx,4)
48810300010000	addq $256,(%rbx)
488104250000000000010000 4:11:sym+0	addq $256,sym
4881448dfc00010000	addq $256,-4(%rbp,%rcx,4)
488103ff7f0000	addq $0x7fff,(%rbx)
4881042500000000ff7f0000 4:11:sym+0	addq $0x7fff,sym
4881448dfcff7f0000	addq $0x7fff,-4(%rbp,%rcx,4)
48810300800000	addq $0x8000,(%rbx)
488104250000000000800000 4:11:sym+0	addq $0x8000,sym
4881448dfc00800000	addq $0x8000,-4(%rbp,%rcx,4)
48810380ff0000	addq $0xff80,(%rbx)
488104250000000080ff0000 4:11:sym+0	addq $0xff80,sym
4881448dfc80ff0000	addq $0xff80,-4(%rbp,%rcx,4)
488103ffff0000	addq $0xffff,(%rbx)
4881042500000000ffff0000 4:11:sym+0	addq $0xffff,sym
4881448dfcffff0000	addq $0xffff,-4(%rbp,%rcx,4)
48810300000100	addq $0x10000,(%rbx)
488104250000000000000100 4:11:sym+0	addq $0x10000,sym
4881448dfc00000100	addq $0x10000,-4(%rbp,%rcx,4)
488103ffffff7f	addq $0x7fffffff,(%rbx)
4881042500000000ffffff7f 4:11:sym+0	addq $0x7fffffff,sym
4881448dfcffffff7f	addq $0x7fffffff,-4(%rbp,%rcx,4)
refused	addq $0x80000000,(%rbx)
refused	addq $0x80000000,sym
refused	addq $0x80000000,-4(%rbp,%rcx,4)
refused	addq $0xffffff80,(%rbx)
refused	addq $0xffffff80,sym
refused	addq $0xffffff80,-4(%rbp,%rcx,4)
refused	addq $0xffffffff,(%rbx)
refused	addq $0xffffffff,sym
refused	addq $0xffffffff,-4(%rbp,%rcx,4)
48810300000000 3:11:sym+0	addq $sym,(%rbx)
488104250000000000000000 4:11:sym+0 8:11:sym+0	addq $sym,sym
4881448dfc00000000 5:11:sym+0	addq $sym,-4(%rbp,%rcx,4)
4883c301	add $1,%rbx
refused	addq $1,$2
refused	addq %rax,$2
refused	addq (%rbx),(%rbx)
f048011b	lock addq %rbx,(%rbx)
f048830301	lock addq $1,(%rbx)
refused	lock addq (%rbx),%rbx
refused	lock addq %rbx,%rax
08c0	orb %al,%al
0803	orb %al,(%rbx)
0a03	orb (%rbx),%al
08042500000000 3:11:sym+0	orb %al,sym
0a042500000000 3:11:sym+0	orb sym,%al
0803	or %al,(%rbx)
refused	or 4((%rbx,%rsi,2),%al
0c00	orb $0,%al
0c01	orb $1,%al
0cff	orb $-1,%al
0c7f	orb $127,%al
0c80	orb $128,%al
0c80	orb $-128,%al
0c7f	orb $-129,%al
0cff	orb $255,%al
0c00	orb $256,%al
0cff	orb $0x7fff,%al
0c00	orb $0x8000,%al
0c80	orb $0xff80,%al
0cff	orb $0xffff,%al
0c00	orb $0x10000,%al
0cff	orb $0x7fffffff,%al
0c00	orb $0x80000000,%al
0c80	orb $0xffffff80,%al
0cff	orb $0xffffffff,%al
0c00 1:14:sym+0	orb $sym,%al
08c8	orb %cl,%al
08c1	orb %al,%cl
080b	orb %cl,(%rbx)
0a0b	orb (%rbx),%cl
080c2500000000 3:11:sym+0	orb %cl,sym
0a0c2500000000 3:11:sym+0	orb sym,%cl
080b	or %cl,(%rbx)
refused	or 4((%rbx,%rsi,2),%cl
80c900	orb $0,%cl
80c901	orb $1,%cl
80c9ff	orb $-1,%cl
80c97f	orb $127,%cl
80c980	orb $128,%cl
80c980	orb $-128,%cl
80c97f	orb $-129,%cl
80c9ff	orb $255,%cl
80c900	orb $256,%cl
80c9ff	orb $0x7fff,%cl
80c900	orb $0x8000,%cl
80c980	orb $0xff80,%cl
80c9ff	orb $0xffff,%cl
80c900	orb $0x10000,%cl
80c9ff	orb $0x7fffffff,%cl
80c900	orb $0x80000000,%cl
80c980	orb $0xffffff80,%cl
80c9ff	orb $0xffffffff,%cl
80c900 2:14:sym+0	orb $sym,%cl
08f8	orb %bh,%al
08c7	orb %al,%bh
083b	orb %bh,(%rbx)
0a3b	orb (%rbx),%bh
083c2500000000 3:11:sym+0	orb %bh,sym
0a3c2500000000 3:11:sym+0	orb sym,%bh
083b	or %bh,(%rbx)
refused	or 4((%rbx,%rsi,2),%bh
80cf00	orb $0,%bh
80cf01	orb $1,%bh
80cfff	orb $-1,%bh
80cf7f	orb $127,%bh
80cf80	orb $128,%bh
80cf80	orb $-128,%bh
80cf7f	orb $-129,%bh
80cfff	orb $255,%bh
80cf00	orb $256,%bh
80cfff	orb $0x7fff,%bh
80cf00	orb $0x8000,%bh
80cf80	orb $0xff80,%bh
80cfff	orb $0xffff,%bh
80cf00	orb $0x10000,%bh
80cfff	orb $0x7fffffff,%bh
80cf00	orb $0x80000000,%bh
80cf80	orb $0xffffff80,%bh
80cfff	orb $0xffffffff,%bh
80cf00 2:14:sym+0	orb $sym,%bh
4008f0	orb %sil,%al
4008c6	orb %al,%sil
400833	orb %sil,(%rbx)
400a33	orb (%rbx),%sil
4008342500000000 4:11:sym+0	orb %sil,sym
400a342500000000 4:11:sym+0	orb sym,%sil
400833	or %sil,(%rbx)
refused	or 4((%rbx,%rsi,2),%sil
4080ce00	orb $0,%sil
4080ce01	orb $1,%sil
4080ceff	orb $-1,%sil
4080ce7f	orb $127,%sil
4080ce80	orb $128,%sil
4080ce80	orb $-128,%sil
4080ce7f	orb $-129,%sil
4080ceff	orb $255,%sil
4080ce00	orb $256,%sil
4080ceff	orb $0x7fff,%sil
4080ce00	orb $0x8000,%sil
4080ce80	orb $0xff80,%sil
4080ceff	orb $0xffff,%sil
4080ce00	orb $0x10000,%sil
4080ceff	orb $0x7fffffff,%sil
4080ce00	orb $0x80000000,%sil
4080ce80	orb $0xffffff80,%sil
4080ceff	orb $0xffffffff,%sil
4080ce00 3:14:sym+0	orb $sym,%sil
4408c8	orb %r9b,%al
4108c1	orb %al,%r9b
44080b	orb %r9b,(%rbx)
440a0b	orb (%rbx),%r9b
44080c2500000000 4:11:sym+0	orb %r9b,sym
440a0c2500000000 4:11:sym+0	orb sym,%r9b
44080b	or %r9b,(%rbx)
refused	or 4((%rbx,%rsi,2),%r9b
4180c900	orb $0,%r9b
4180c901	orb $1,%r9b
4180c9ff	orb $-1,%r9b
4180c97f	orb $127,%r9b
4180c980	orb $128,%r9b
4180c980	orb $-128,%r9b
4180c97f	orb $-129,%r9b
4180c9ff	orb $255,%r9b
4180c900	orb $256,%r9b
4180c9ff	orb $0x7fff,%r9b
4180c900	orb $0x8000,%r9b
4180c980	orb $0xff80,%r9b
4180c9ff	orb $0xffff,%r9b
4180c900	orb $0x10000,%r9b
4180c9ff	orb $0x7fffffff,%r9b
4180c900	orb $0x80000000,%r9b
4180c980	orb $0xffffff80,%r9b
4180c9ff	orb $0xffffffff,%r9b
4180c900 3:14:sym+0	orb $sym,%r9b
800b00	orb $0,(%rbx)
800c250000000000 3:11:sym+0	orb $0,sym
804c8dfc00	orb $0,-4(%rbp,%rcx,4)
800b01	orb $1,(%rbx)
800c250000000001 3:11:sym+0	orb $1,sym
804c8dfc01	orb $1,-4(%rbp,%rcx,4)
800bff	orb $-1,(%rbx)
800c2500000000ff 3:11:sym+0	orb $-1,sym
804c8dfcff	orb $-1,-4(%rbp,%rcx,4)
800b7f	orb $127,(%rbx)
800c25000000007f 3:11:sym+0	orb $127,sym
804c8dfc7f	orb $127,-4(%rbp,%rcx,4)
800b80	orb $128,(%rbx)
800c250000000080 3:11:sym+0	orb $128,sym
804c8dfc80	orb $128,-4(%rbp,%rcx,4)
800b80	orb $-128,(%rbx)
800c250000000080 3:11:sym+0	orb $-128,sym
804c8dfc80	orb $-128,-4(%rbp,%rcx,4)
800b7f	orb $-129,(%rbx)
800c25000000007f 3:11:sym+0	orb $-129,sym
804c8dfc7f	orb $-129,-4(%rbp,%rcx,4)
800bff	orb $255,(%rbx)
800c2500000000ff 3:11:sym+0	orb $255,sym
804c8dfcff	orb $255,-4(%rbp,%rcx,4)
800b00	orb $256,(%rbx)
800c250000000000 3:11:sym+0	orb $256,sym
804c8dfc00	orb $256,-4(%rbp,%rcx,4)
800bff	orb $0x7fff,(%rbx)
800c2500000000ff 3:11:sym+0	orb $0x7fff,sym
804c8dfcff	orb $0x7fff,-4(%rbp,%rcx,4)
800b00	orb $0x8000,(%rbx)
800c250000000000 3:11:sym+0	orb $0x8000,sym
804c8dfc00	orb $0x8000,-4(%rbp,%rcx,4)
800b80	orb $0xff80,(%rbx)
800c250000000080 3:11:sym+0	orb $0xff80,sym
804c8dfc80	orb $0xff80,-4(%rbp,%rcx,4)
800bff	orb $0xffff,(%rbx)
800c2500000000ff 3:11:sym+0	orb $0xffff,sym
804c8dfcff	orb $0xffff,-4(%rbp,%rcx,4)
800b00	orb $0x10000,(%rbx)
800c250000000000 3:11:sym+0	orb $0x10000,sym
804c8dfc00	orb $0x10000,-4(%rbp,%rcx,4)
800bff	orb $0x7fffffff,(%rbx)
800c2500000000ff 3:11:sym+0	orb $0x7fffffff,sym
804c8dfcff	orb $0x7fffffff,-4(%rbp,%rcx,4)
800b00	orb $0x80000000,(%rbx)
800c250000000000 3:11:sym+0	orb $0x80000000,sym
804c8dfc00	orb $0x80000000,-4(%rbp,%rcx,4)
800b80	orb $0xffffff80,(%rbx)
800c250000000080 3:11:sym+0	orb $0xffffff80,sym
804c8dfc80	orb $0xffffff80,-4(%rbp,%rcx,4)
800bff	orb $0xffffffff,(%rbx)
800c2500000000ff 3:11:sym+0	orb $0xffffffff,sym
804c8dfcff	orb $0xffffffff,-4(%rbp,%rcx,4)
800b00 2:14:sym+0	orb $sym,(%rbx)
800c250000000000 3:11:sym+0 7:14:sym+0	orb $sym,sym
804c8dfc00 4:14:sym+0	orb $sym,-4(%rbp,%rcx,4)
80c901	or $1,%cl
830b01	or $1,(%rbx)
refused	orb $1,$2
refused	orb %al,$2
refused	orb (%rbx),(%rbx)
f0080b	lock orb %cl,(%rbx)
f0800b01	lock orb $1,(%rbx)
refused	lock orb (%rbx),%cl
refused	lock orb %cl,%al
6609c0	orw %ax,%ax
660903	orw %ax,(%rbx)
660b03	orw (%rbx),%ax
6609042500000000 4:11:sym+0	orw %ax,sym
660b042500000000 4:11:sym+0	orw sym,%ax
660903	or %ax,(%rbx)
refused	or 4((%rbx,%rsi,2),%ax
6683c800	orw $0,%ax
6683c801	orw $1,%ax
6683c8ff	orw $-1,%ax
6683c87f	orw $127,%ax
660d8000	orw $128,%ax
6683c880	orw $-128,%ax
660d7fff	orw $-129,%ax
660dff00	orw $255,%ax
660d0001	orw $256,%ax
660dff7f	orw $0x7fff,%ax
660d0080	orw $0x8000,%ax
6683c880	orw $0xff80,%ax
6683c8ff	orw $0xffff,%ax
660d0000	orw $0x10000,%ax
660dffff	orw $0x7fffffff,%ax
660d0000	orw $0x80000000,%ax
6683c880	orw $0xffffff80,%ax
6683c8ff	orw $0xffffffff,%ax
660d0000 2:12:sym+0	orw $sym,%ax
6609d8	orw %bx,%ax
6609c3	orw %ax,%bx
66091b	orw %bx,(%rbx)
660b1b	orw (%rbx),%bx
66091c2500000000 4:11:sym+0	orw %bx,sym
660b1c2500000000 4:11:sym+0	orw sym,%bx
66091b	or %bx,(%rbx)
refused	or 4((%rbx,%rsi,2),%bx
6683cb00	orw $0,%bx
6683cb01	orw $1,%bx
6683cbff	orw $-1,%bx
6683cb7f	orw $127,%bx
6681cb8000	orw $128,%bx
6683cb80	orw $-128,%bx
6681cb7fff	orw $-129,%bx
6681cbff00	orw $255,%bx
6681cb0001	orw $256,%bx
6681cbff7f	orw $0x7fff,%bx
6681cb0080	orw $0x8000,%bx
6683cb80	orw $0xff80,%bx
6683cbff	orw $0xffff,%bx
6681cb0000	orw $0x10000,%bx
6681cbffff	orw $0x7fffffff,%bx
6681cb0000	orw $0x80000000,%bx
6683cb80	orw $0xffffff80,%bx
6683cbff	orw $0xffffffff,%bx
6681cb0000 3:12:sym+0	orw $sym,%bx
6609f0	orw %si,%ax
6609c6	orw %ax,%si
660933	orw %si,(%rbx)
660b33	orw (%rbx),%si
6609342500000000 4:11:sym+0	orw %si,sym
660b342500000000 4:11:sym+0	orw sym,%si
660933	or %si,(%rbx)
refused	or 4((%rbx,%rsi,2),%si
6683ce00	orw $0,%si
6683ce01	orw $1,%si
6683ceff	orw $-1,%si
6683ce7f	orw $127,%si
6681ce8000	orw $128,%si
6683ce80	orw $-128,%si
6681ce7fff	orw $-129,%si
6681ceff00	orw $255,%si
6681ce0001	orw $256,%si
6681ceff7f	orw $0x7fff,%si
6681ce0080	orw $0x8000,%si
6683ce80	orw $0xff80,%si
6683ceff	orw $0xffff,%si
6681ce0000	orw $0x10000,%si
6681ceffff	orw $0x7fffffff,%si
6681ce0000	orw $0x80000000,%si
6683ce80	orw $0xffffff80,%si
6683ceff	orw $0xffffffff,%si
6681ce0000 3:12:sym+0	orw $sym,%si
664409c8	orw %r9w,%ax
664109c1	orw %ax,%r9w
6644090b	orw %r9w,(%rbx)
66440b0b	orw (%rbx),%r9w
6644090c2500000000 5:11:sym+0	orw %r9w,sym
66440b0c2500000000 5:11:sym+0	orw sym,%r9w
6644090b	or %r9w,(%rbx)
refused	or 4((%rbx,%rsi,2),%r9w
664183c900	orw $0,%r9w
664183c901	orw $1,%r9w
664183c9ff	orw $-1,%r9w
664183c97f	orw $127,%r9w
664181c98000	orw $128,%r9w
664183c980	orw $-128,%r9w
664181c97fff	orw $-129,%r9w
664181c9ff00	orw $255,%r9w
664181c90001	orw $256,%r9w
664181c9ff7f	orw $0x7fff,%r9w
664181c90080	orw $0x8000,%r9w
664183c980	orw $0xff80,%r9w
664183c9ff	orw $0xffff,%r9w
664181c90000	orw $0x10000,%r9w
664181c9ffff	orw $0x7fffffff,%r9w
664181c90000	orw $0x80000000,%r9w
664183c980	orw $0xffffff80,%r9w
664183c9ff	orw $0xffffffff,%r9w
664181c90000 4:12:sym+0	orw $sym,%r9w
66830b00	orw $0,(%rbx)
66830c250000000000 4:11:sym+0	orw $0,sym
66834c8dfc00	orw $0,-4(%rbp,%rcx,4)
66830b01	orw $1,(%rbx)
66830c250000000001 4:11:sym+0	orw $1,sym
66834c8dfc01	orw $1,-4(%rbp,%rcx,4)
66830bff	orw $-1,(%rbx)
66830c2500000000ff 4:11:sym+0	orw $-1,sym
66834c8dfcff	orw $-1,-4(%rbp,%rcx,4)
66830b7f	orw $127,(%rbx)
66830c25000000007f 4:11:sym+0	orw $127,sym
66834c8dfc7f	orw $127,-4(%rbp,%rcx,4)
66810b8000	orw $128,(%rbx)
66810c25000000008000 4:11:sym+0	orw $128,sym
66814c8dfc8000	orw $128,-4(%rbp,%rcx,4)
66830b80	orw $-128,(%rbx)
66830c250000000080 4:11:sym+0	orw $-128,sym
66834c8dfc80	orw $-128,-4(%rbp,%rcx,4)
66810b7fff	orw $-129,(%rbx)
66810c25000000007fff 4:11:sym+0	orw $-129,sym
66814c8dfc7fff	orw $-129,-4(%rbp,%rcx,4)
66810bff00	orw $255,(%rbx)
66810c2500000000ff00 4:11:sym+0	orw $255,sym
66814c8dfcff00	orw $255,-4(%rbp,%rcx,4)
66810b0001	orw $256,(%rbx)
66810c25000000000001 4:11:sym+0	orw $256,sym
66814c8dfc0001	orw $256,-4(%rbp,%rcx,4)
66810bff7f	orw $0x7fff,(%rbx)
66810c2500000000ff7f 4:11:sym+0	orw $0x7fff,sym
66814c8dfcff7f	orw $0x7fff,-4(%rbp,%rcx,4)
66810b0080	orw $0x8000,(%rbx)
66810c25000000000080 4:11:sym+0	orw $0x8000,sym
66814c8dfc0080	orw $0x8000,-4(%rbp,%rcx,4)
66830b80	orw $0xff80,(%rbx)
66830c250000000080 4:11:sym+0	orw $0xff80,sym
66834c8dfc80	orw $0xff80,-4(%rbp,%rcx,4)
66830bff	orw $0xffff,(%rbx)
66830c2500000000ff 4:11:sym+0	orw $0xffff,sym
66834c8dfcff	orw $0xffff,-4(%rbp,%rcx,4)
66810b0000	orw $0x10000,(%rbx)
66810c25000000000000 4:11:sym+0	orw $0x10000,sym
66814c8dfc0000	orw $0x10000,-4(%rbp,%rcx,4)
66810bffff	orw $0x7fffffff,(%rbx)
66810c2500000000ffff 4:11:sym+0	orw $0x7fffffff,sym
66814c8dfcffff	orw $0x7fffffff,-4(%rbp,%rcx,4)
66810b0000	orw $0x80000000,(%rbx)
66810c25000000000000 4:11:sym+0	orw $0x80000000,sym
66814c8dfc0000	orw $0x80000000,-4(%rbp,%rcx,4)
66830b80	orw $0xffffff80,(%rbx)
66830c250000000080 4:11:sym+0	orw $0xffffff80,sym
66834c8dfc80	orw $0xffffff80,-4(%rbp,%rcx,4)
66830bff	orw $0xffffffff,(%rbx)
66830c2500000000ff 4:11:sym+0	orw $0xffffffff,sym
66834c8dfcff	orw $0xffffffff,-4(%rbp,%rcx,4)
66810b0000 3:12:sym+0	orw $sym,(%rbx)
66810c25000000000000 4:11:sym+0 8:12:sym+0	orw $sym,sym
66814c8dfc0000 5:12:sym+0	orw $sym,-4(%rbp,%rcx,4)
6683cb01	or $1,%bx
refused	orw $1,$2
refused	orw %ax,$2
refused	orw (%rbx),(%rbx)
66f0091b	lock orw %bx,(%rbx)
66f0830b01	lock orw $1,(%rbx)
refused	lock orw (%rbx),%bx
refused	lock orw %bx,%ax
09c0	orl %eax,%eax
0903	orl %eax,(%rbx)
0b03	orl (%rbx),%eax
09042500000000 3:11:sym+0	orl %eax,sym
0b042500000000 3:11:sym+0	orl sym,%eax
0903	or %eax,(%rbx)
refused	or 4((%rbx,%rsi,2),%eax
83c800	orl $0,%eax
83c801	orl $1,%eax
83c8ff	orl $-1,%eax
83c87f	orl $127,%eax
0d80000000	orl $128,%eax
83c880	orl $-128,%eax
0d7fffffff	orl $-129,%eax
0dff000000	orl $255,%eax
0d00010000	orl $256,%eax
0dff7f0000	orl $0x7fff,%eax
0d00800000	orl $0x8000,%eax
0d80ff0000	orl $0xff80,%eax
0dffff0000	orl $0xffff,%eax
0d00000100	orl $0x10000,%eax
0dffffff7f	orl $0x7fffffff,%eax
0d00000080	orl $0x80000000,%eax
83c880	orl $0xffffff80,%eax
83c8ff	orl $0xffffffff,%eax
0d00000000 1:10:sym+0	orl $sym,%eax
09d8	orl %ebx,%eax
09c3	orl %eax,%ebx
091b	orl %ebx,(%rbx)
0b1b	orl (%rbx),%ebx
091c2500000000 3:11:sym+0	orl %ebx,sym
0b1c2500000000 3:11:sym+0	orl sym,%ebx
091b	or %ebx,(%rbx)
refused	or 4((%rbx,%rsi,2),%ebx
83cb00	orl $0,%ebx
83cb01	orl $1,%ebx
83cbff	orl $-1,%ebx
83cb7f	orl $127,%ebx
81cb80000000	orl $128,%ebx
83cb80	orl $-128,%ebx
81cb7fffffff	orl $-129,%ebx
81cbff000000	orl $255,%ebx
81cb00010000	orl $256,%ebx
81cbff7f0000	orl $0x7fff,%ebx
81cb00800000	orl $0x8000,%ebx
81cb80ff0000	orl $0xff80,%ebx
81cbffff0000	orl $0xffff,%ebx
81cb00000100	orl $0x10000,%ebx
81cbffffff7f	orl $0x7fffffff,%ebx
81cb00000080	orl $0x80000000,%ebx
83cb80	orl $0xffffff80,%ebx
83cbff	orl $0xffffffff,%ebx
81cb00000000 2:10:sym+0	orl $sym,%ebx
09e0	orl %esp,%eax
09c4	orl %eax,%esp
0923	orl %esp,(%rbx)
0b23	orl (%rbx),%esp
09242500000000 3:11:sym+0	orl %esp,sym
0b242500000000 3:11:sym+0	orl sym,%esp
0923	or %esp,(%rbx)
refused	or 4((%rbx,%rsi,2),%esp
83cc00	orl $0,%esp
83cc01	orl $1,%esp
83ccff	orl $-1,%esp
83cc7f	orl $127,%esp
81cc80000000	orl $128,%esp
83cc80	orl $-128,%esp
81cc7fffffff	orl $-129,%esp
81ccff000000	orl $255,%esp
81cc00010000	orl $256,%esp
81ccff7f0000	orl $0x7fff,%esp
81cc00800000	orl $0x8000,%esp
81cc80ff0000	orl $0xff80,%esp
81ccffff0000	orl $0xffff,%esp
81cc00000100	orl $0x10000,%esp
81ccffffff7f	orl $0x7fffffff,%esp
81cc00000080	orl $0x80000000,%esp
83cc80	orl $0xffffff80,%esp
83ccff	orl $0xffffffff,%esp
81cc00000000 2:10:sym+0	orl $sym,%esp
09e8	orl %ebp,%eax
09c5	orl %eax,%ebp
092b	orl %ebp,(%rbx)
0b2b	orl (%rbx),%ebp
092c2500000000 3:11:sym+0	orl %ebp,sym
0b2c2500000000 3:11:sym+0	orl sym,%ebp
092b	or %ebp,(%rbx)
refused	or 4((%rbx,%rsi,2),%ebp
83cd00	orl $0,%ebp
83cd01	orl $1,%ebp
83cdff	orl $-1,%ebp
83cd7f	orl $127,%ebp
81cd80000000	orl $128,%ebp
83cd80	orl $-128,%ebp
81cd7fffffff	orl $-129,%ebp
81cdff000000	orl $255,%ebp
81cd00010000	orl $256,%ebp
81cdff7f0000	orl $0x7fff,%ebp
81cd00800000	orl $0x8000,%ebp
81cd80ff0000	orl $0xff80,%ebp
81cdffff0000	orl $0xffff,%ebp
81cd00000100	orl $0x10000,%ebp
81cdffffff7f	orl $0x7fffffff,%ebp
81cd00000080	orl $0x80000000,%ebp
83cd80	orl $0xffffff80,%ebp
83cdff	orl $0xffffffff,%ebp
81cd00000000 2:10:sym+0	orl $sym,%ebp
4409c8	orl %r9d,%eax
4109c1	orl %eax,%r9d
44090b	orl %r9d,(%rbx)
440b0b	orl (%rbx),%r9d
44090c2500000000 4:11:sym+0	orl %r9d,sym
440b0c2500000000 4:11:sym+0	orl sym,%r9d
44090b	or %r9d,(%rbx)
refused	or 4((%rbx,%rsi,2),%r9d
4183c900	orl $0,%r9d
4183c901	orl $1,%r9d
4183c9ff	orl $-1,%r9d
4183c97f	orl $127,%r9d
4181c980000000	orl $128,%r9d
4183c980	orl $-128,%r9d
4181c97fffffff	orl $-129,%r9d
4181c9ff000000	orl $255,%r9d
4181c900010000	orl $256,%r9d
4181c9ff7f0000	orl $0x7fff,%r9d
4181c900800000	orl $0x8000,%r9d
4181c980ff0000	orl $0xff80,%r9d
4181c9ffff0000	orl $0xffff,%r9d
4181c900000100	orl $0x10000,%r9d
4181c9ffffff7f	orl $0x7fffffff,%r9d
4181c900000080	orl $0x80000000,%r9d
4183c980	orl $0xffffff80,%r9d
4183c9ff	orl $0xffffffff,%r9d
4181c900000000 3:10:sym+0	orl $sym,%r9d
830b00	orl $0,(%rbx)
830c250000000000 3:11:sym+0	orl $0,sym
834c8dfc00	orl $0,-4(%rbp,%rcx,4)
830b01	orl $1,(%rbx)
830c250000000001 3:11:sym+0	orl $1,sym
834c8dfc01	orl $1,-4(%rbp,%rcx,4)
830bff	orl $-1,(%rbx)
830c2500000000ff 3:11:sym+0	orl $-1,sym
834c8dfcff	orl $-1,-4(%rbp,%rcx,4)
830b7f	orl $127,(%rbx)
830c25000000007f 3:11:sym+0	orl $127,sym
834c8dfc7f	orl $127,-4(%rbp,%rcx,4)
810b80000000	orl $128,(%rbx)
810c250000000080000000 3:11:sym+0	orl $128,sym
814c8dfc80000000	orl $128,-4(%rbp,%rcx,4)
830b80	orl $-128,(%rbx)
830c250000000080 3:11:sym+0	orl $-128,sym
834c8dfc80	orl $-128,-4(%rbp,%rcx,4)
810b7fffffff	orl $-129,(%rbx)
810c25000000007fffffff 3:11:sym+0	orl $-129,sym
814c8dfc7fffffff	orl $-129,-4(%rbp,%rcx,4)
810bff000000	orl $255,(%rbx)
810c2500000000ff000000 3:11:sym+0	orl $255,sym
814c8dfcff000000	orl $255,-4(%rbp,%rcx,4)
810b00010000	orl $256,(%rbx)
810c250000000000010000 3:11:sym+0	orl $256,sym
814c8dfc00010000	orl $256,-4(%rbp,%rcx,4)
810bff7f0000	orl $0x7fff,(%rbx)
810c2500000000ff7f0000 3:11:sym+0	orl $0x7fff,sym
814c8dfcff7f0000	orl $0x7fff,-4(%rbp,%rcx,4)
810b00800000	orl $0x8000,(%rbx)
810c250000000000800000 3:11:sym+0	orl $0x8000,sym
814c8dfc00800000	orl $0x8000,-4(%rbp,%rcx,4)
810b80ff0000	orl $0xff80,(%rbx)
810c250000000080ff0000 3:11:sym+0	orl $0xff80,sym
814c8dfc80ff0000	orl $0xff80,-4(%rbp,%rcx,4)
810bffff0000	orl $0xffff,(%rbx)
810c2500000000ffff0000 3:11:sym+0	orl $0xffff,sym
814c8dfcffff0000	orl $0xffff,-4(%rbp,%rcx,4)
810b00000100	orl $0x10000,(%rbx)
810c250000000000000100 3:11:sym+0	orl $0x10000,sym
814c8dfc00000100	orl $0x10000,-4(%rbp,%rcx,4)
810bffffff7f	orl $0x7fffffff,(%rbx)
810c2500000000ffffff7f 3:11:sym+0	orl $0x7fffffff,sym
814c8dfcffffff7f	orl $0x7fffffff,-4(%rbp,%rcx,4)
810b00000080	orl $0x80000000,(%rbx)
810c250000000000000080 3:11:sym+0	orl $0x80000000,sym
814c8dfc00000080	orl $0x80000000,-4(%rbp,%rcx,4)
830b80	orl $0xffffff80,(%rbx)
830c250000000080 3:11:sym+0	orl $0xffffff80,sym
834c8dfc80	orl $0xffffff80,-4(%rbp,%rcx,4)
830bff	orl $0xffffffff,(%rbx)
830c2500000000ff 3:11:sym+0	orl $0xffffffff,sym
834c8dfcff	orl $0xffffffff,-4(%rbp,%rcx,4)
810b00000000 2:10:sym+0	orl $sym,(%rbx)
810c250000000000000000 3:11:sym+0 7:10:sym+0	orl $sym,sym
814c8dfc00000000 4:10:sym+0	orl $sym,-4(%rbp,%rcx,4)
83cb01	or $1,%ebx
refused	orl $1,$2
refused	orl %eax,$2
refused	orl (%rbx),(%rbx)
f0091b	lock orl %ebx,(%rbx)
f0830b01	lock orl $1,(%rbx)
refused	lock orl (%rbx),%ebx
refused	lock orl %ebx,%eax
4809c0	orq %rax,%rax
480903	orq %rax,(%rbx)
480b03	orq (%rbx),%rax
4809042500000000 4:11:sym+0	orq %rax,sym
480b042500000000 4:11:sym+0	orq sym,%rax
480903	or %rax,(%rbx)
refused	or 4((%rbx,%rsi,2),%rax
4883c800	orq $0,%rax
4883c801	orq $1,%rax
4883c8ff	orq $-1,%rax
4883c87f	orq $127,%rax
480d80000000	orq $128,%rax
4883c880	orq $-128,%rax
480d7fffffff	orq $-129,%rax
480dff000000	orq $255,%rax
480d00010000	orq $256,%rax
480dff7f0000	orq $0x7fff,%rax
480d00800000	orq $0x8000,%rax
480d80ff0000	orq $0xff80,%rax
480dffff0000	orq $0xffff,%rax
480d00000100	orq $0x10000,%rax
480dffffff7f	orq $0x7fffffff,%rax
refused	orq $0x80000000,%rax
refused	orq $0xffffff80,%rax
refused	orq $0xffffffff,%rax
480d00000000 2:11:sym+0	orq $sym,%rax
4809d8	orq %rbx,%rax
4809c3	orq %rax,%rbx
48091b	orq %rbx,(%rbx)
480b1b	orq (%rbx),%rbx
48091c2500000000 4:11:sym+0	orq %rbx,sym
480b1c2500000000 4:11:sym+0	orq sym,%rbx
48091b	or %rbx,(%rbx)
refused	or 4((%rbx,%rsi,2),%rbx
4883cb00	orq $0,%rbx
4883cb01	orq $1,%rbx
4883cbff	orq $-1,%rbx
4883cb7f	orq $127,%rbx
4881cb80000000	orq $128,%rbx
4883cb80	orq $-128,%rbx
4881cb7fffffff	orq $-129,%rbx
4881cbff000000	orq $255,%rbx
4881cb00010000	orq $256,%rbx
4881cbff7f0000	orq $0x7fff,%rbx
4881cb00800000	orq $0x8000,%rbx
4881cb80ff0000	orq $0xff80,%rbx
4881cbffff0000	orq $0xffff,%rbx
4881cb00000100	orq $0x10000,%rbx
4881cbffffff7f	orq $0x7fffffff,%rbx
refused	orq $0x80000000,%rbx
refused	orq $0xffffff80,%rbx
refused	orq $0xffffffff,%rbx
4881cb00000000 3:11:sym+0	orq $sym,%rbx
4809e0	orq %rsp,%rax
4809c4	orq %rax,%rsp
480923	orq %rsp,(%rbx)
480b23	orq (%rbx),%rsp
4809242500000000 4:11:sym+0	orq %rsp,sym
480b242500000000 4:11:sym+0	orq sym,%rsp
480923	or %rsp,(%rbx)
refused	or 4((%rbx,%rsi,2),%rsp
4883cc00	orq $0,%rsp
4883cc01	orq $1,%rsp
4883ccff	orq $-1,%rsp
4883cc7f	orq $127,%rsp
4881cc80000000	orq $128,%rsp
4883cc80	orq $-128,%rsp
4881cc7fffffff	orq $-129,%rsp
4881ccff000000	orq $255,%rsp
4881cc00010000	orq $256,%rsp
4881ccff7f0000	orq $0x7fff,%rsp
4881cc00800000	orq $0x8000,%rsp
4881cc80ff0000	orq $0xff80,%rsp
4881ccffff0000	orq $0xffff,%rsp
4881cc00000100	orq $0x10000,%rsp
4881ccffffff7f	orq $0x7fffffff,%rsp
refused	orq $0x80000000,%rsp
refused	orq $0xffffff80,%rsp
refused	orq $0xffffffff,%rsp
4881cc00000000 3:11:sym+0	orq $sym,%rsp
4c09e0	orq %r12,%rax
4909c4	orq %rax,%r12
4c0923	orq %r12,(%rbx)
4c0b23	orq (%rbx),%r12
4c09242500000000 4:11:sym+0	orq %r12,sym
4c0b242500000000 4:11:sym+0	orq sym,%r12
4c0923	or %r12,(%rbx)
refused	or 4((%rbx,%rsi,2),%r12
4983cc00	orq $0,%r12
4983cc01	orq $1,%r12
4983ccff	orq $-1,%r12
4983cc7f	orq $127,%r12
4981cc80000000	orq $128,%r12
4983cc80	orq $-128,%r12
4981cc7fffffff	orq $-129,%r12
4981ccff000000	orq $255,%r12
4981cc00010000	orq $256,%r12
4981ccff7f0000	orq $0x7fff,%r12
4981cc00800000	orq $0x8000,%r12
4981cc80ff0000	orq $0xff80,%r12
4981ccffff0000	orq $0xffff,%r12
4981cc00000100	orq $0x10000,%r12
4981ccffffff7f	orq $0x7fffffff,%r12
refused	orq $0x80000000,%r12
refused	orq $0xffffff80,%r12
refused	orq $0xffffffff,%r12
4981cc00000000 3:11:sym+0	orq $sym,%r12
4c09e8	orq %r13,%rax
4909c5	orq %rax,%r13
4c092b	orq %r13,(%rbx)
4c0b2b	orq (%rbx),%r13
4c092c2500000000 4:11:sym+0	orq %r13,sym
4c0b2c2500000000 4:11:sym+0	orq sym,%r13
4c092b	or %r13,(%rbx)
refused	or 4((%rbx,%rsi,2),%r13
4983cd00	orq $0,%r13
4983cd01	orq $1,%r13
4983cdff	orq $-1,%r13
4983cd7f	orq $127,%r13
4981cd80000000	orq $128,%r13
4983cd80	orq $-128,%r13
4981cd7fffffff	orq $-129,%r13
4981cdff000000	orq $255,%r13
4981cd00010000	orq $256,%r13
4981cdff7f0000	orq $0x7fff,%r13
4981cd00800000	orq $0x8000,%r13
4981cd80ff0000	orq $0xff80,%r13
4981cdffff0000	orq $0xffff,%r13
4981cd00000100	orq $0x10000,%r13
4981cdffffff7f	orq $0x7fffffff,%r13
refused	orq $0x80000000,%r13
refused	orq $0xffffff80,%r13
refused	orq $0xffffffff,%r13
4981cd00000000 3:11:sym+0	orq $sym,%r13
4c09c8	orq %r9,%rax
4909c1	orq %rax,%r9
4c090b	orq %r9,(%rbx)
4c0b0b	orq (%rbx),%r9
4c090c2500000000 4:11:sym+0	orq %r9,sym
4c0b0c2500000000 4:11:sym+0	orq sym,%r9
4c090b	or %r9,(%rbx)
refused	or 4((%rbx,%rsi,2),%r9
4983c900	orq $0,%r9
4983c901	orq $1,%r9
4983c9ff	orq $-1,%r9
4983c97f	orq $127,%r9
4981c980000000	orq $128,%r9
4983c980	orq $-128,%r9
4981c97fffffff	orq $-129,%r9
4981c9ff000000	orq $255,%r9
4981c900010000	orq $256,%r9
4981c9ff7f0000	orq $0x7fff,%r9
4981c900800000	orq $0x8000,%r9
4981c980ff0000	orq $0xff80,%r9
4981c9ffff0000	orq $0xffff,%r9
4981c900000100	orq $0x10000,%r9
4981c9ffffff7f	orq $0x7fffffff,%r9
refused	orq $0x80000000,%r9
refused	orq $0xffffff80,%r9
refused	orq $0xffffffff,%r9
4981c900000000 3:11:sym+0	orq $sym,%r9
48830b00	orq $0,(%rbx)
48830c250000000000 4:11:sym+0	orq $0,sym
48834c8dfc00	orq $0,-4(%rbp,%rcx,4)
48830b01	orq $1,(%rbx)
48830c250000000001 4:11:sym+0	orq $1,sym
48834c8dfc01	orq $1,-4(%rbp,%rcx,4)
48830bff	orq $-1,(%rbx)
48830c2500000000ff 4:11:sym+0	orq $-1,sym
48834c8dfcff	orq $-1,-4(%rbp,%rcx,4)
48830b7f	orq $127,(%rbx)
48830c25000000007f 4:11:sym+0	orq $127,sym
48834c8dfc7f	orq $127,-4(%rbp,%rcx,4)
48810b80000000	orq $128,(%rbx)
48810c250000000080000000 4:11:sym+0	orq $128,sym
48814c8dfc80000000	orq $128,-4(%rbp,%rcx,4)
48830b80	orq $-128,(%rbx)
48830c250000000080 4:11:sym+0	orq $-128,sym
48834c8dfc80	orq $-128,-4(%rbp,%rcx,4)
48810b7fffffff	orq $-129,(%rbx)
48810c25000000007fffffff 4:11:sym+0	orq $-129,sym
48814c8dfc7fffffff	orq $-129,-4(%rbp,%rcx,4)
48810bff000000	orq $255,(%rbx)
48810c2500000000ff000000 4:11:sym+0	orq $255,sym
48814c8dfcff000000	orq $255,-4(%rbp,%rcx,4)
48810b00010000	orq $256,(%rbx)
48810c250000000000010000 4:11:sym+0	orq $256,sym
48814c8dfc00010000	orq $256,-4(%rbp,%rcx,4)
48810bff7f0000	orq $0x7fff,(%rbx)
48810c2500000000ff7f0000 4:11:sym+0	orq $0x7fff,sym
48814c8dfcff7f0000	orq $0x7fff,-4(%rbp,%rcx,4)
48810b00800000	orq $0x8000,(%rbx)
48810c250000000000800000 4:11:sym+0	orq $0x8000,sym
48814c8dfc00800000	orq $0x8000,-4(%rbp,%rcx,4)
48810b80ff0000	orq $0xff80,(%rbx)
48810c250000000080ff0000 4:11:sym+0	orq $0xff80,sym
48814c8dfc80ff0000	orq $0xff80,-4(%rbp,%rcx,4)
48810bffff0000	orq $0xffff,(%rbx)
48810c2500000000ffff0000 4:11:sym+0	orq $0xffff,sym
48814c8dfcffff0000	orq $0xffff,-4(%rbp,%rcx,4)
48810b00000100	orq $0x10000,(%rbx)
48810c250000000000000100 4:11:sym+0	orq $0x10000,sym
48814c8dfc00000100	orq $0x10000,-4(%rbp,%rcx,4)
48810bffffff7f	orq $0x7fffffff,(%rbx)
48810c2500000000ffffff7f 4:11:sym+0	orq $0x7fffffff,sym
48814c8dfcffffff7f	orq $0x7fffffff,-4(%rbp,%rcx,4)
refused	orq $0x80000000,(%rbx)
refused	orq $0x80000000,sym
refused	orq $0x80000000,-4(%rbp,%rcx,4)
refused	orq $0xffffff80,(%rbx)
refused	orq $0xffffff80,sym
refused	orq $0xffffff80,-4(%rbp,%rcx,4)
refused	orq $0xffffffff,(%rbx)
refused	orq $0xffffffff,sym
refused	orq $0xffffffff,-4(%rbp,%rcx,4)
48810b00000000 3:11:sym+0	orq $sym,(%rbx)
48810c250000000000000000 4:11:sym+0 8:11:sym+0	orq $sym,sym
48814c8dfc00000000 5:11:sym+0	orq $sym,-4(%rbp,%rcx,4)
4883cb01	or $1,%rbx
refused	orq $1,$2
refused	orq %rax,$2
refused	orq (%rbx),(%rbx)
f048091b	lock orq %rbx,(%rbx)
f048830b01	lock orq $1,(%rbx)
refused	lock orq (%rbx),%rbx
refused	lock orq %rbx,%rax
10c0	adcb %al,%al
1003	adcb %al,(%rbx)
1203	adcb (%rbx),%al
10042500000000 3:11:sym+0	adcb %al,sym
12042500000000 3:11:sym+0	adcb sym,%al
1003	adc %al,(%rbx)
refused	adc 4((%rbx,%rsi,2),%al
1400	adcb $0,%al
1401	adcb $1,%al
14ff	adcb $-1,%al
147f	adcb $127,%al
1480	adcb $128,%al
1480	adcb $-128,%al
147f	adcb $-129,%al
14ff	adcb $255,%al
1400	adcb $256,%al
14ff	adcb $0x7fff,%al
1400	adcb $0x8000,%al
1480	adcb $0xff80,%al
14ff	adcb $0xffff,%al
1400	adcb $0x10000,%al
14ff	adcb $0x7fffffff,%al
1400	adcb $0x80000000,%al
1480	adcb $0xffffff80,%al
14ff	adcb $0xffffffff,%al
1400 1:14:sym+0	adcb $sym,%al
10c8	adcb %cl,%al
10c1	adcb %al,%cl
100b	adcb %cl,(%rbx)
120b	adcb (%rbx),%cl
100c2500000000 3:11:sym+0	adcb %cl,sym
120c2500000000 3:11:sym+0	adcb sym,%cl
100b	adc %cl,(%rbx)
refused	adc 4((%rbx,%rsi,2),%cl
80d100	adcb $0,%cl
80d101	adcb $1,%cl
80d1ff	adcb $-1,%cl
80d17f	adcb $127,%cl
80d180	adcb $128,%cl
80d180	adcb $-128,%cl
80d17f	adcb $-129,%cl
80d1ff	adcb $255,%cl
80d100	adcb $256,%cl
80d1ff	adcb $0x7fff,%cl
80d100	adcb $0x8000,%cl
80d180	adcb $0xff80,%cl
80d1ff	adcb $0xffff,%cl
80d100	adcb $0x10000,%cl
80d1ff	adcb $0x7fffffff,%cl
80d100	adcb $0x80000000,%cl
80d180	adcb $0xffffff80,%cl
80d1ff	adcb $0xffffffff,%cl
80d100 2:14:sym+0	adcb $sym,%cl
10f8	adcb %bh,%al
10c7	adcb %al,%bh
103b	adcb %bh,(%rbx)
123b	adcb (%rbx),%bh
103c2500000000 3:11:sym+0	adcb %bh,sym
123c2500000000 3:11:sym+0	adcb sym,%bh
103b	adc %bh,(%rbx)
refused	adc 4((%rbx,%rsi,2),%bh
80d700	adcb $0,%bh
80d701	adcb $1,%bh
80d7ff	adcb $-1,%bh
80d77f	adcb $127,%bh
80d780	adcb $128,%bh
80d780	adcb $-128,%bh
80d77f	adcb $-129,%bh
80d7ff	adcb $255,%bh
80d700	adcb $256,%bh
80d7ff	adcb $0x7fff,%bh
80d700	adcb $0x8000,%bh
80d780	adcb $0xff80,%bh
80d7ff	adcb $0xffff,%bh
80d700	adcb $0x10000,%bh
80d7ff	adcb $0x7fffffff,%bh
80d700	adcb $0x80000000,%bh
80d780	adcb $0xffffff80,%bh
80d7ff	adcb $0xffffffff,%bh
80d700 2:14:sym+0	adcb $sym,%bh
4010f0	adcb %sil,%al
4010c6	adcb %al,%sil
401033	adcb %sil,(%rbx)
401233	adcb (%rbx),%sil
4010342500000000 4:11:sym+0	adcb %sil,sym
4012342500000000 4:11:sym+0	adcb sym,%sil
401033	adc %sil,(%rbx)
refused	adc 4((%rbx,%rsi,2),%sil
4080d600	adcb $0,%sil
4080d601	adcb $1,%sil
4080d6ff	adcb $-1,%sil
4080d67f	adcb $127,%sil
4080d680	adcb $128,%sil
4080d680	adcb $-128,%sil
4080d67f	adcb $-129,%sil
4080d6ff	adcb $255,%sil
4080d600	adcb $256,%sil
4080d6ff	adcb $0x7fff,%sil
4080d600	adcb $0x8000,%sil
4080d680	adcb $0xff80,%sil
4080d6ff	adcb $0xffff,%sil
4080d600	adcb $0x10000,%sil
4080d6ff	adcb $0x7fffffff,%sil
4080d600	adcb $0x80000000,%sil
4080d680	adcb $0xffffff80,%sil
4080d6ff	adcb $0xffffffff,%sil
4080d600 3:14:sym+0	adcb $sym,%sil
4410c8	adcb %r9b,%al
4110c1	adcb %al,%r9b
44100b	adcb %r9b,(%rbx)
44120b	adcb (%rbx),%r9b
44100c2500000000 4:11:sym+0	adcb %r9b,sym
44120c2500000000 4:11:sym+0	adcb sym,%r9b
44100b	adc %r9b,(%rbx)
refused	adc 4((%rbx,%rsi,2),%r9b
4180d100	adcb $0,%r9b
4180d101	adcb $1,%r9b
4180d1ff	adcb $-1,%r9b
4180d17f	adcb $127,%r9b
4180d180	adcb $128,%r9b
4180d180	adcb $-128,%r9b
4180d17f	adcb $-129,%r9b
4180d1ff	adcb $255,%r9b
4180d100	adcb $256,%r9b
4180d1ff	adcb $0x7fff,%r9b
4180d100	adcb $0x8000,%r9b
4180d180	adcb $0xff80,%r9b
4180d1ff	adcb $0xffff,%r9b
4180d100	adcb $0x10000,%r9b
4180d1ff	adcb $0x7fffffff,%r9b
4180d100	adcb $0x80000000,%r9b
4180d180	adcb $0xffffff80,%r9b
4180d1ff	adcb $0xffffffff,%r9b
4180d100 3:14:sym+0	adcb $sym,%r9b
801300	adcb $0,(%rbx)
8014250000000000 3:11:sym+0	adcb $0,sym
80548dfc00	adcb $0,-4(%rbp,%rcx,4)
801301	adcb $1,(%rbx)
8014250000000001 3:11:sym+0	adcb $1,sym
80548dfc01	adcb $1,-4(%rbp,%rcx,4)
8013ff	adcb $-1,(%rbx)
80142500000000ff 3:11:sym+0	adcb $-1,sym
80548dfcff	adcb $-1,-4(%rbp,%rcx,4)
80137f	adcb $127,(%rbx)
801425000000007f 3:11:sym+0	adcb $127,sym
80548dfc7f	adcb $127,-4(%rbp,%rcx,4)
801380	adcb $128,(%rbx)
8014250000000080 3:11:sym+0	adcb $128,sym
80548dfc80	adcb $128,-4(%rbp,%rcx,4)
801380	adcb $-128,(%rbx)
8014250000000080 3:11:sym+0	adcb $-128,sym
80548dfc80	adcb $-128,-4(%rbp,%rcx,4)
80137f	adcb $-129,(%rbx)
801425000000007f 3:11:sym+0	adcb $-129,sym
80548dfc7f	adcb $-129,-4(%rbp,%rcx,4)
8013ff	adcb $255,(%rbx)
80142500000000ff 3:11:sym+0	adcb $255,sym
80548dfcff	adcb $255,-4(%rbp,%rcx,4)
801300	adcb $256,(%rbx)
8014250000000000 3:11:sym+0	adcb $256,sym
80548dfc00	adcb $256,-4(%rbp,%rcx,4)
8013ff	adcb $0x7fff,(%rbx)
80142500000000ff 3:11:sym+0	adcb $0x7fff,sym
80548dfcff	adcb $0x7fff,-4(%rbp,%rcx,4)
801300	adcb $0x8000,(%rbx)
8014250000000000 3:11:sym+0	adcb $0x8000,sym
80548dfc00	adcb $0x8000,-4(%rbp,%rcx,4)
801380	adcb $0xff80,(%rbx)
8014250000000080 3:11:sym+0	adcb $0xff80,sym
80548dfc80	adcb $0xff80,-4(%rbp,%rcx,4)
8013ff	adcb $0xffff,(%rbx)
80142500000000ff 3:11:sym+0	adcb $0xffff,sym
80548dfcff	adcb $0xffff,-4(%rbp,%rcx,4)
801300	adcb $0x10000,(%rbx)
8014250000000000 3:11:sym+0	adcb $0x10000,sym
80548dfc00	adcb $0x10000,-4(%rbp,%rcx,4)
8013ff	adcb $0x7fffffff,(%rbx)
80142500000000ff 3:11:sym+0	adcb $0x7fffffff,sym
80548dfcff	adcb $0x7fffffff,-4(%rbp,%rcx,4)
801300	adcb $0x80000000,(%rbx)
8014250000000000 3:11:sym+0	adcb $0x80000000,sym
80548dfc00	adcb $0x80000000,-4(%rbp,%rcx,4)
801380	adcb $0xffffff80,(%rbx)
8014250000000080 3:11:sym+0	adcb $0xffffff80,sym
80548dfc80	adcb $0xffffff80,-4(%rbp,%rcx,4)
8013ff	adcb $0xffffffff,(%rbx)
80142500000000ff 3:11:sym+0	adcb $0xffffffff,sym
80548dfcff	adcb $0xffffffff,-4(%rbp,%rcx,4)
801300 2:14:sym+0	adcb $sym,(%rbx)
8014250000000000 3:11:sym+0 7:14:sym+0	adcb $sym,sym
80548dfc00 4:14:sym+0	adcb $sym,-4(%rbp,%rcx,4)
80d101	adc $1,%cl
831301	adc $1,(%rbx)
refused	adcb $1,$2
refused	adcb %al,$2
refused	adcb (%rbx),(%rbx)
f0100b	lock adcb %cl,(%rbx)
f0801301	lock adcb $1,(%rbx)
refused	lock adcb (%rbx),%cl
refused	lock adcb %cl,%al
6611c0	adcw %ax,%ax
661103	adcw %ax,(%rbx)
661303	adcw (%rbx),%ax
6611042500000000 4:11:sym+0	adcw %ax,sym
6613042500000000 4:11:sym+0	adcw sym,%ax
661103	adc %ax,(%rbx)
refused	adc 4((%rbx,%rsi,2),%ax
6683d000	adcw $0,%ax
6683d001	adcw $1,%ax
6683d0ff	adcw $-1,%ax
6683d07f	adcw $127,%ax
66158000	adcw $128,%ax
6683d080	adcw $-128,%ax
66157fff	adcw $-129,%ax
6615ff00	adcw $255,%ax
66150001	adcw $256,%ax
6615ff7f	adcw $0x7fff,%ax
66150080	adcw $0x8000,%ax
6683d080	adcw $0xff80,%ax
6683d0ff	adcw $0xffff,%ax
66150000	adcw $0x10000,%ax
6615ffff	adcw $0x7fffffff,%ax
66150000	adcw $0x80000000,%ax
6683d080	adcw $0xffffff80,%ax
6683d0ff	adcw $0xffffffff,%ax
66150000 2:12:sym+0	adcw $sym,%ax
6611d8	adcw %bx,%ax
6611c3	adcw %ax,%bx
66111b	adcw %bx,(%rbx)
66131b	adcw (%rbx),%bx
66111c2500000000 4:11:sym+0	adcw %bx,sym
66131c2500000000 4:11:sym+0	adcw sym,%bx
66111b	adc %bx,(%rbx)
refused	adc 4((%rbx,%rsi,2),%bx
6683d300	adcw $0,%bx
6683d301	adcw $1,%bx
6683d3ff	adcw $-1,%bx
6683d37f	adcw $127,%bx
6681d38000	adcw $128,%bx
6683d380	adcw $-128,%bx
6681d37fff	adcw $-129,%bx
6681d3ff00	adcw $255,%bx
6681d30001	adcw $256,%bx
6681d3ff7f	adcw $0x7fff,%bx
6681d30080	adcw $0x8000,%bx
6683d380	adcw $0xff80,%bx
6683d3ff	adcw $0xffff,%bx
6681d30000	adcw $0x10000,%bx
6681d3ffff	adcw $0x7fffffff,%bx
6681d30000	adcw $0x80000000,%bx
6683d380	adcw $0xffffff80,%bx
6683d3ff	adcw $0xffffffff,%bx
6681d30000 3:12:sym+0	adcw $sym,%bx
6611f0	adcw %si,%ax
6611c6	adcw %ax,%si
661133	adcw %si,(%rbx)
661333	adcw (%rbx),%si
6611342500000000 4:11:sym+0	adcw %si,sym
6613342500000000 4:11:sym+0	adcw sym,%si
661133	adc %si,(%rbx)
refused	adc 4((%rbx,%rsi,2),%si
6683d600	adcw $0,%si
6683d601	adcw $1,%si
6683d6ff	adcw $-1,%si
6683d67f	adcw $127,%si
6681d68000	adcw $128,%si
6683d680	adcw $-128,%si
6681d67fff	adcw $-129,%si
6681d6ff00	adcw $255,%si
6681d60001	adcw $256,%si
6681d6ff7f	adcw $0x7fff,%si
6681d60080	adcw $0x8000,%si
6683d680	adcw $0xff80,%si
6683d6ff	adcw $0xffff,%si
6681d60000	adcw $0x10000,%si
6681d6ffff	adcw $0x7fffffff,%si
6681d60000	adcw $0x80000000,%si
6683d680	adcw $0xffffff80,%si
6683d6ff	adcw $0xffffffff,%si
6681d60000 3:12:sym+0	adcw $sym,%si
664411c8	adcw %r9w,%ax
664111c1	adcw %ax,%r9w
6644110b	adcw %r9w,(%rbx)
6644130b	adcw (%rbx),%r9w
6644110c2500000000 5:11:sym+0	adcw %r9w,sym
6644130c2500000000 5:11:sym+0	adcw sym,%r9w
6644110b	adc %r9w,(%rbx)
refused	adc 4((%rbx,%rsi,2),%r9w
664183d100	adcw $0,%r9w
664183d101	adcw $1,%r9w
664183d1ff	adcw $-1,%r9w
664183d17f	adcw $127,%r9w
664181d18000	adcw $128,%r9w
664183d180	adcw $-128,%r9w
664181d17fff	adcw $-129,%r9w
664181d1ff00	adcw $255,%r9w
664181d10001	adcw $256,%r9w
664181d1ff7f	adcw $0x7fff,%r9w
664181d10080	adcw $0x8000,%r9w
664183d180	adcw $0xff80,%r9w
664183d1ff	adcw $0xffff,%r9w
664181d10000	adcw $0x10000,%r9w
664181d1ffff	adcw $0x7fffffff,%r9w
664181d10000	adcw $0x80000000,%r9w
664183d180	adcw $0xffffff80,%r9w
664183d1ff	adcw $0xffffffff,%r9w
664181d10000 4:12:sym+0	adcw $sym,%r9w
66831300	adcw $0,(%rbx)
668314250000000000 4:11:sym+0	adcw $0,sym
6683548dfc00	adcw $0,-4(%rbp,%rcx,4)
66831301	adcw $1,(%rbx)
668314250000000001 4:11:sym+0	adcw $1,sym
6683548dfc01	adcw $1,-4(%rbp,%rcx,4)
668313ff	adcw $-1,(%rbx)
6683142500000000ff 4:11:sym+0	adcw $-1,sym
6683548dfcff	adcw $-1,-4(%rbp,%rcx,4)
6683137f	adcw $127,(%rbx)
66831425000000007f 4:11:sym+0	adcw $127,sym
6683548dfc7f	adcw $127,-4(%rbp,%rcx,4)
6681138000	adcw $128,(%rbx)
66811425000000008000 4:11:sym+0	adcw $128,sym
6681548dfc8000	adcw $128,-4(%rbp,%rcx,4)
66831380	adcw $-128,(%rbx)
668314250000000080 4:11:sym+0	adcw $-128,sym
6683548dfc80	adcw $-128,-4(%rbp,%rcx,4)
6681137fff	adcw $-129,(%rbx)
66811425000000007fff 4:11:sym+0	adcw $-129,sym
6681548dfc7fff	adcw $-129,-4(%rbp,%rcx,4)
668113ff00	adcw $255,(%rbx)
6681142500000000ff00 4:11:sym+0	adcw $255,sym
6681548dfcff00	adcw $255,-4(%rbp,%rcx,4)
6681130001	adcw $256,(%rbx)
66811425000000000001 4:11:sym+0	adcw $256,sym
6681548dfc0001	adcw $256,-4(%rbp,%rcx,4)
668113ff7f	adcw $0x7fff,(%rbx)
6681142500000000ff7f 4:11:sym+0	adcw $0x7fff,sym
6681548dfcff7f	adcw $0x7fff,-4(%rbp,%rcx,4)
6681130080	adcw $0x8000,(%rbx)
66811425000000000080 4:11:sym+0	adcw $0x8000,sym
6681548dfc0080	adcw $0x8000,-4(%rbp,%rcx,4)
66831380	adcw $0xff80,(%rbx)
668314250000000080 4:11:sym+0	adcw $0xff80,sym
6683548dfc80	adcw $0xff80,-4(%rbp,%rcx,4)
668313ff	adcw $0xffff,(%rbx)
6683142500000000ff 4:11:sym+0	adcw $0xffff,sym
6683548dfcff	adcw $0xffff,-4(%rbp,%rcx,4)
6681130000	adcw $0x10000,(%rbx)
66811425000000000000 4:11:sym+0	adcw $0x10000,sym
6681548dfc0000	adcw $0x10000,-4(%rbp,%rcx,4)
668113ffff	adcw $0x7fffffff,(%rbx)
6681142500000000ffff 4:11:sym+0	adcw $0x7fffffff,sym
6681548dfcffff	adcw $0x7fffffff,-4(%rbp,%rcx,4)
6681130000	adcw $0x80000000,(%rbx)
66811425000000000000 4:11:sym+0	adcw $0x80000000,sym
6681548dfc0000	adcw $0x80000000,-4(%rbp,%rcx,4)
66831380	adcw $0xffffff80,(%rbx)
668314250000000080 4:11:sym+0	adcw $0xffffff80,sym
6683548dfc80	adcw $0xffffff80,-4(%rbp,%rcx,4)
668313ff	adcw $0xffffffff,(%rbx)
6683142500000000ff 4:11:sym+0	adcw $0xffffffff,sym
6683548dfcff	adcw $0xffffffff,-4(%rbp,%rcx,4)
6681130000 3:12:sym+0	adcw $sym,(%rbx)
66811425000000000000 4:11:sym+0 8:12:sym+0	adcw $sym,sym
6681548dfc0000 5:12:sym+0	adcw $sym,-4(%rbp,%rcx,4)
6683d301	adc $1,%bx
refused	adcw $1,$2
refused	adcw %ax,$2
refused	adcw (%rbx),(%rbx)
66f0111b	lock adcw %bx,(%rbx)
66f0831301	lock adcw $1,(%rbx)
refused	lock adcw (%rbx),%bx
refused	lock adcw %bx,%ax
11c0	adcl %eax,%eax
1103	adcl %eax,(%rbx)
1303	adcl (%rbx),%eax
11042500000000 3:11:sym+0	adcl %eax,sym
13042500000000 3:11:sym+0	adcl sym,%eax
1103	adc %eax,(%rbx)
refused	adc 4((%rbx,%rsi,2),%eax
83d000	adcl $0,%eax
83d001	adcl $1,%eax
83d0ff	adcl $-1,%eax
83d07f	adcl $127,%eax
1580000000	adcl $128,%eax
83d080	adcl $-128,%eax
157fffffff	adcl $-129,%eax
15ff000000	adcl $255,%eax
1500010000	adcl $256,%eax
15ff7f0000	adcl $0x7fff,%eax
1500800000	adcl $0x8000,%eax
1580ff0000	adcl $0xff80,%eax
15ffff0000	adcl $0xffff,%eax
1500000100	adcl $0x10000,%eax
15ffffff7f	adcl $0x7fffffff,%eax
1500000080	adcl $0x80000000,%eax
83d080	adcl $0xffffff80,%eax
83d0ff	adcl $0xffffffff,%eax
1500000000 1:10:sym+0	adcl $sym,%eax
11d8	adcl %ebx,%eax
11c3	adcl %eax,%ebx
111b	adcl %ebx,(%rbx)
131b	adcl (%rbx),%ebx
111c2500000000 3:11:sym+0	adcl %ebx,sym
131c2500000000 3:11:sym+0	adcl sym,%ebx
111b	adc %ebx,(%rbx)
refused	adc 4((%rbx,%rsi,2),%ebx
83d300	adcl $0,%ebx
83d301	adcl $1,%ebx
83d3ff	adcl $-1,%ebx
83d37f	adcl $127,%ebx
81d380000000	adcl $128,%ebx
83d380	adcl $-128,%ebx
81d37fffffff	adcl $-129,%ebx
81d3ff000000	adcl $255,%ebx
81d300010000	adcl $256,%ebx
81d3ff7f0000	adcl $0x7fff,%ebx
81d300800000	adcl $0x8000,%ebx
81d380ff0000	adcl $0xff80,%ebx
81d3ffff0000	adcl $0xffff,%ebx
81d300000100	adcl $0x10000,%ebx
81d3ffffff7f	adcl $0x7fffffff,%ebx
81d300000080	adcl $0x80000000,%ebx
83d380	adcl $0xffffff80,%ebx
83d3ff	adcl $0xffffffff,%ebx
81d300000000 2:10:sym+0	adcl $sym,%ebx
11e0	adcl %esp,%eax
11c4	adcl %eax,%esp
1123	adcl %esp,(%rbx)
1323	adcl (%rbx),%esp
11242500000000 3:11:sym+0	adcl %esp,sym
13242500000000 3:11:sym+0	adcl sym,%esp
1123	adc %esp,(%rbx)
refused	adc 4((%rbx,%rsi,2),%esp
83d400	adcl $0,%esp
83d401	adcl $1,%esp
83d4ff	adcl $-1,%esp
83d47f	adcl $127,%esp
81d480000000	adcl $128,%esp
83d480	adcl $-128,%esp
81d47fffffff	adcl $-129,%esp
81d4ff000000	adcl $255,%esp
81d400010000	adcl $256,%esp
81d4ff7f0000	adcl $0x7fff,%esp
81d400800000	adcl $0x8000,%esp
81d480ff0000	adcl $0xff80,%esp
81d4ffff0000	adcl $0xffff,%esp
81d400000100	adcl $0x10000,%esp
81d4ffffff7f	adcl $0x7fffffff,%esp
81d400000080	adcl $0x80000000,%esp
83d480	adcl $0xffffff80,%esp
83d4ff	adcl $0xffffffff,%esp
81d400000000 2:10:sym+0	adcl $sym,%esp
11e8	adcl %ebp,%eax
11c5	adcl %eax,%ebp
112b	adcl %ebp,(%rbx)
132b	adcl (%rbx),%ebp
112c2500000000 3:11:sym+0	adcl %ebp,sym
132c2500000000 3:11:sym+0	adcl sym,%ebp
112b	adc %ebp,(%rbx)
refused	adc 4((%rbx,%rsi,2),%ebp
83d500	adcl $0,%ebp
83d501	adcl $1,%ebp
83d5ff	adcl $-1,%ebp
83d57f	adcl $127,%ebp
81d580000000	adcl $128,%ebp
83d580	adcl $-128,%ebp
81d57fffffff	adcl $-129,%ebp
81d5ff000000	adcl $255,%ebp
81d500010000	adcl $256,%ebp
81d5ff7f0000	adcl $0x7fff,%ebp
81d500800000	adcl $0x8000,%ebp
81d580ff0000	adcl $0xff80,%ebp
81d5ffff0000	adcl $0xffff,%ebp
81d500000100	adcl $0x10000,%ebp
81d5ffffff7f	adcl $0x7fffffff,%ebp
81d500000080	adcl $0x80000000,%ebp
83d580	adcl $0xffffff80,%ebp
83d5ff	adcl $0xffffffff,%ebp
81d500000000 2:10:sym+0	adcl $sym,%ebp
4411c8	adcl %r9d,%eax
4111c1	adcl %eax,%r9d
44110b	adcl %r9d,(%rbx)
44130b	adcl (%rbx),%r9d
44110c2500000000 4:11:sym+0	adcl %r9d,sym
44130c2500000000 4:11:sym+0	adcl sym,%r9d
44110b	adc %r9d,(%rbx)
refused	adc 4((%rbx,%rsi,2),%r9d
4183d100	adcl $0,%r9d
4183d101	adcl $1,%r9d
4183d1ff	adcl $-1,%r9d
4183d17f	adcl $127,%r9d
4181d180000000	adcl $128,%r9d
4183d180	adcl $-128,%r9d
4181d17fffffff	adcl $-129,%r9d
4181d1ff000000	adcl $255,%r9d
4181d100010000	adcl $256,%r9d
4181d1ff7f0000	adcl $0x7fff,%r9d
4181d100800000	adcl $0x8000,%r9d
4181d180ff0000	adcl $0xff80,%r9d
4181d1ffff0000	adcl $0xffff,%r9d
4181d100000100	adcl $0x10000,%r9d
4181d1ffffff7f	adcl $0x7fffffff,%r9d
4181d100000080	adcl $0x80000000,%r9d
4183d180	adcl $0xffffff80,%r9d
4183d1ff	adcl $0xffffffff,%r9d
4181d100000000 3:10:sym+0	adcl $sym,%r9d
831300	adcl $0,(%rbx)
8314250000000000 3:11:sym+0	adcl $0,sym
83548dfc00	adcl $0,-4(%rbp,%rcx,4)
831301	adcl $1,(%rbx)
8314250000000001 3:11:sym+0	adcl $1,sym
83548dfc01	adcl $1,-4(%rbp,%rcx,4)
8313ff	adcl $-1,(%rbx)
83142500000000ff 3:11:sym+0	adcl $-1,sym
83548dfcff	adcl $-1,-4(%rbp,%rcx,4)
83137f	adcl $127,(%rbx)
831425000000007f 3:11:sym+0	adcl $127,sym
83548dfc7f	adcl $127,-4(%rbp,%rcx,4)
811380000000	adcl $128,(%rbx)
8114250000000080000000 3:11:sym+0	adcl $128,sym
81548dfc80000000	adcl $128,-4(%rbp,%rcx,4)
831380	adcl $-128,(%rbx)
8314250000000080 3:11:sym+0	adcl $-128,sym
83548dfc80	adcl $-128,-4(%rbp,%rcx,4)
81137fffffff	adcl $-129,(%rbx)
811425000000007fffffff 3:11:sym+0	adcl $-129,sym
81548dfc7fffffff	adcl $-129,-4(%rbp,%rcx,4)
8113ff000000	adcl $255,(%rbx)
81142500000000ff000000 3:11:sym+0	adcl $255,sym
81548dfcff000000	adcl $255,-4(%rbp,%rcx,4)
811300010000	adcl $256,(%rbx)
8114250000000000010000 3:11:sym+0	adcl $256,sym
81548dfc00010000	adcl $256,-4(%rbp,%rcx,4)
8113ff7f0000	adcl $0x7fff,(%rbx)
81142500000000ff7f0000 3:11:sym+0	adcl $0x7fff,sym
81548dfcff7f0000	adcl $0x7fff,-4(%rbp,%rcx,4)
811300800000	adcl $0x8000,(%rbx)
8114250000000000800000 3:11:sym+0	adcl $0x8000,sym
81548dfc00800000	adcl $0x8000,-4(%rbp,%rcx,4)
811380ff0000	adcl $0xff80,(%rbx)
8114250000000080ff0000 3:11:sym+0	adcl $0xff80,sym
81548dfc80ff0000	adcl $0xff80,-4(%rbp,%rcx,4)
8113ffff0000	adcl $0xffff,(%rbx)
81142500000000ffff0000 3:11:sym+0	adcl $0xffff,sym
81548dfcffff0000	adcl $0xffff,-4(%rbp,%rcx,4)
811300000100	adcl $0x10000,(%rbx)
8114250000000000000100 3:11:sym+0	adcl $0x10000,sym
81548dfc00000100	adcl $0x10000,-4(%rbp,%rcx,4)
8113ffffff7f	adcl $0x7fffffff,(%rbx)
81142500000000ffffff7f 3:11:sym+0	adcl $0x7fffffff,sym
81548dfcffffff7f	adcl $0x7fffffff,-4(%rbp,%rcx,4)
811300000080	adcl $0x80000000,(%rbx)
8114250000000000000080 3:11:sym+0	adcl $0x80000000,sym
81548dfc00000080	adcl $0x80000000,-4(%rbp,%rcx,4)
831380	adcl $0xffffff80,(%rbx)
8314250000000080 3:11:sym+0	adcl $0xffffff80,sym
83548dfc80	adcl $0xffffff80,-4(%rbp,%rcx,4)
8313ff	adcl $0xffffffff,(%rbx)
83142500000000ff 3:11:sym+0	adcl $0xffffffff,sym
83548dfcff	adcl $0xffffffff,-4(%rbp,%rcx,4)
811300000000 2:10:sym+0	adcl $sym,(%rbx)
8114250000000000000000 3:11:sym+0 7:10:sym+0	adcl $sym,sym
81548dfc00000000 4:10:sym+0	adcl $sym,-4(%rbp,%rcx,4)
83d301	adc $1,%ebx
refused	adcl $1,$2
refused	adcl %eax,$2
refused	adcl (%rbx),(%rbx)
f0111b	lock adcl %ebx,(%rbx)
f0831301	lock adcl $1,(%rbx)
refused	lock adcl (%rbx),%ebx
refused	lock adcl %ebx,%eax
4811c0	adcq %rax,%rax
481103	adcq %rax,(%rbx)
481303	adcq (%rbx),%rax
4811042500000000 4:11:sym+0	adcq %rax,sym
4813042500000000 4:11:sym+0	adcq sym,%rax
481103	adc %rax,(%rbx)
refused	adc 4((%rbx,%rsi,2),%rax
4883d000	adcq $0,%rax
4883d001	adcq $1,%rax
4883d0ff	adcq $-1,%rax
4883d07f	adcq $127,%rax
481580000000	adcq $128,%rax
4883d080	adcq $-128,%rax
48157fffffff	adcq $-129,%rax
4815ff000000	adcq $255,%rax
481500010000	adcq $256,%rax
4815ff7f0000	adcq $0x7fff,%rax
481500800000	adcq $0x8000,%rax
481580ff0000	adcq $0xff80,%rax
4815ffff0000	adcq $0xffff,%rax
481500000100	adcq $0x10000,%rax
4815ffffff7f	adcq $0x7fffffff,%rax
refused	adcq $0x80000000,%rax
refused	adcq $0xffffff80,%rax
refused	adcq $0xffffffff,%rax
481500000000 2:11:sym+0	adcq $sym,%rax
4811d8	adcq %rbx,%rax
4811c3	adcq %rax,%rbx
48111b	adcq %rbx,(%rbx)
48131b	adcq (%rbx),%rbx
48111c2500000000 4:11:sym+0	adcq %rbx,sym
48131c2500000000 4:11:sym+0	adcq sym,%rbx
48111b	adc %rbx,(%rbx)
refused	adc 4((%rbx,%rsi,2),%rbx
4883d300	adcq $0,%rbx
4883d301	adcq $1,%rbx
4883d3ff	adcq $-1,%rbx
4883d37f	adcq $127,%rbx
4881d380000000	adcq $128,%rbx
4883d380	adcq $-128,%rbx
4881d37fffffff	adcq $-129,%rbx
4881d3ff000000	adcq $255,%rbx
4881d300010000	adcq $256,%rbx
4881d3ff7f0000	adcq $0x7fff,%rbx
4881d300800000	adcq $0x8000,%rbx
4881d380ff0000	adcq $0xff80,%rbx
4881d3ffff0000	adcq $0xffff,%rbx
4881d300000100	adcq $0x10000,%rbx
4881d3ffffff7f	adcq $0x7fffffff,%rbx
refused	adcq $0x80000000,%rbx
refused	adcq $0xffffff80,%rbx
refused	adcq $0xffffffff,%rbx
4881d300000000 3:11:sym+0	adcq $sym,%rbx
4811e0	adcq %rsp,%rax
4811c4	adcq %rax,%rsp
481123	adcq %rsp,(%rbx)
481323	adcq (%rbx),%rsp
4811242500000000 4:11:sym+0	adcq %rsp,sym
4813242500000000 4:11:sym+0	adcq sym,%rsp
481123	adc %rsp,(%rbx)
refused	adc 4((%rbx,%rsi,2),%rsp
4883d400	adcq $0,%rsp
4883d401	adcq $1,%rsp
4883d4ff	adcq $-1,%rsp
4883d47f	adcq $127,%rsp
4881d480000000	adcq $128,%rsp
4883d480	adcq $-128,%rsp
4881d47fffffff	adcq $-129,%rsp
4881d4ff000000	adcq $255,%rsp
4881d400010000	adcq $256,%rsp
4881d4ff7f0000	adcq $0x7fff,%rsp
4881d400800000	adcq $0x8000,%rsp
4881d480ff0000	adcq $0xff80,%rsp
4881d4ffff0000	adcq $0xffff,%rsp
4881d400000100	adcq $0x10000,%rsp
4881d4ffffff7f	adcq $0x7fffffff,%rsp
refused	adcq $0x80000000,%rsp
refused	adcq $0xffffff80,%rsp
refused	adcq $0xffffffff,%rsp
4881d400000000 3:11:sym+0	adcq $sym,%rsp
4c11e0	adcq %r12,%rax
4911c4	adcq %rax,%r12
4c1123	adcq %r12,(%rbx)
4c1323	adcq (%rbx),%r12
4c11242500000000 4:11:sym+0	adcq %r12,sym
4c13242500000000 4:11:sym+0	adcq sym,%r12
4c1123	adc %r12,(%rbx)
refused	adc 4((%rbx,%rsi,2),%r12
4983d400	adcq $0,%r12
4983d401	adcq $1,%r12
4983d4ff	adcq $-1,%r12
4983d47f	adcq $127,%r12
4981d480000000	adcq $128,%r12
4983d480	adcq $-128,%r12
4981d47fffffff	adcq $-129,%r12
4981d4ff000000	adcq $255,%r12
4981d400010000	adcq $256,%r12
4981d4ff7f0000	adcq $0x7fff,%r12
4981d400800000	adcq $0x8000,%r12
4981d480ff0000	adcq $0xff80,%r12
4981d4ffff0000	adcq $0xffff,%r12
4981d400000100	adcq $0x10000,%r12
4981d4ffffff7f	adcq $0x7fffffff,%r12
refused	adcq $0x80000000,%r12
refused	adcq $0xffffff80,%r12
refused	adcq $0xffffffff,%r12
4981d400000000 3:11:sym+0	adcq $sym,%r12
4c11e8	adcq %r13,%rax
4911c5	adcq %rax,%r13
4c112b	adcq %r13,(%rbx)
4c132b	adcq (%rbx),%r13
4c112c2500000000 4:11:sym+0	adcq %r13,sym
4c132c2500000000 4:11:sym+0	adcq sym,%r13
4c112b	adc %r13,(%rbx)
refused	adc 4((%rbx,%rsi,2),%r13
4983d500	adcq $0,%r13
4983d501	adcq $1,%r13
4983d5ff	adcq $-1,%r13
4983d57f	adcq $127,%r13
4981d580000000	adcq $128,%r13
4983d580	adcq $-128,%r13
4981d57fffffff	adcq $-129,%r13
4981d5ff000000	adcq $255,%r13
4981d500010000	adcq $256,%r13
4981d5ff7f0000	adcq $0x7fff,%r13
4981d500800000	adcq $0x8000,%r13
4981d580ff0000	adcq $0xff80,%r13
4981d5ffff0000	adcq $0xffff,%r13
4981d500000100	adcq $0x10000,%r13
4981d5ffffff7f	adcq $0x7fffffff,%r13
refused	adcq $0x80000000,%r13
refused	adcq $0xffffff80,%r13
refused	adcq $0xffffffff,%r13
4981d500000000 3:11:sym+0	adcq $sym,%r13
4c11c8	adcq %r9,%rax
4911c1	adcq %rax,%r9
4c110b	adcq %r9,(%rbx)
4c130b	adcq (%rbx),%r9
4c110c2500000000 4:11:sym+0	adcq %r9,sym
4c130c2500000000 4:11:sym+0	adcq sym,%r9
4c110b	adc %r9,(%rbx)
refused	adc 4((%rbx,%rsi,2),%r9
4983d100	adcq $0,%r9
4983d101	adcq $1,%r9
4983d1ff	adcq $-1,%r9
4983d17f	adcq $127,%r9
4981d180000000	adcq $128,%r9
4983d180	adcq $-128,%r9
4981d17fffffff	adcq $-129,%r9
4981d1ff000000	adcq $255,%r9
4981d100010000	adcq $256,%r9
4981d1ff7f0000	adcq $0x7fff,%r9
4981d100800000	adcq $0x8000,%r9
4981d180ff0000	adcq $0xff80,%r9
4981d1ffff0000	adcq $0xffff,%r9
4981d100000100	adcq $0x10000,%r9
4981d1ffffff7f	adcq $0x7fffffff,%r9
refused	adcq $0x80000000,%r9
refused	adcq $0xffffff80,%r9
refused	adcq $0xffffffff,%r9
4981d100000000 3:11:sym+0	adcq $sym,%r9
48831300	adcq $0,(%rbx)
488314250000000000 4:11:sym+0	adcq $0,sym
4883548dfc00	adcq $0,-4(%rbp,%rcx,4)
48831301	adcq $1,(%rbx)
488314250000000001 4:11:sym+0	adcq $1,sym
4883548dfc01	adcq $1,-4(%rbp,%rcx,4)
488313ff	adcq $-1,(%rbx)
4883142500000000ff 4:11:sym+0	adcq $-1,sym
4883548dfcff	adcq $-1,-4(%rbp,%rcx,4)
4883137f	adcq $127,(%rbx)
48831425000000007f 4:11:sym+0	adcq $127,sym
4883548dfc7f	adcq $127,-4(%rbp,%rcx,4)
48811380000000	adcq $128,(%rbx)
488114250000000080000000 4:11:sym+0	adcq $128,sym
4881548dfc80000000	adcq $128,-4(%rbp,%rcx,4)
48831380	adcq $-128,(%rbx)
488314250000000080 4:11:sym+0	adcq $-128,sym
4883548dfc80	adcq $-128,-4(%rbp,%rcx,4)
4881137fffffff	adcq $-129,(%rbx)
48811425000000007fffffff 4:11:sym+0	adcq $-129,sym
4881548dfc7fffffff	adcq $-129,-4(%rbp,%rcx,4)
488113ff000000	adcq $255,(%rbx)
4881142500000000ff000000 4:11:sym+0	adcq $255,sym
4881548dfcff000000	adcq $255,-4(%rbp,%rcx,4)
48811300010000	adcq $256,(%rbx)
488114250000000000010000 4:11:sym+0	adcq $256,sym
4881548dfc00010000	adcq $256,-4(%rbp,%rcx,4)
488113ff7f0000	adcq $0x7fff,(%rbx)
4881142500000000ff7f0000 4:11:sym+0	adcq $0x7fff,sym
4881548dfcff7f0000	adcq $0x7fff,-4(%rbp,%rcx,4)
48811300800000	adcq $0x8000,(%rbx)
488114250000000000800000 4:11:sym+0	adcq $0x8000,sym
4881548dfc00800000	adcq $0x8000,-4(%rbp,%rcx,4)
48811380ff0000	adcq $0xff80,(%rbx)
488114250000000080ff0000 4:11:sym+0	adcq $0xff80,sym
4881548dfc80ff0000	adcq $0xff80,-4(%rbp,%rcx,4)
488113ffff0000	adcq $0xffff,(%rbx)
4881142500000000ffff0000 4:11:sym+0	adcq $0xffff,sym
4881548dfcffff0000	adcq $0xffff,-4(%rbp,%rcx,4)
48811300000100	adcq $0x10000,(%rbx)
488114250000000000000100 4:11:sym+0	adcq $0x10000,sym
4881548dfc00000100	adcq $0x10000,-4(%rbp,%rcx,4)
488113ffffff7f	adcq $0x7fffffff,(%rbx)
4881142500000000ffffff7f 4:11:sym+0	adcq $0x7fffffff,sym
4881548dfcffffff7f	adcq $0x7fffffff,-4(%rbp,%rcx,4)
refused	adcq $0x80000000,(%rbx)
refused	adcq $0x80000000,sym
refused	adcq $0x80000000,-4(%rbp,%rcx,4)
refused	adcq $0xffffff80,(%rbx)
refused	adcq $0xffffff80,sym
refused	adcq $0xffffff80,-4(%rbp,%rcx,4)
refused	adcq $0xffffffff,(%rbx)
refused	adcq $0xffffffff,sym
refused	adcq $0xffffffff,-4(%rbp,%rcx,4)
48811300000000 3:11:sym+0	adcq $sym,(%rbx)
488114250000000000000000 4:11:sym+0 8:11:sym+0	adcq $sym,sym
4881548dfc00000000 5:11:sym+0	adcq $sym,-4(%rbp,%rcx,4)
4883d301	adc $1,%rbx
refused	adcq $1,$2
refused	adcq %rax,$2
refused	adcq (%rbx),(%rbx)
f048111b	lock adcq %rbx,(%rbx)
f048831301	lock adcq $1,(%rbx)
refused	lock adcq (%rbx),%rbx
refused	lock adcq %rbx,%rax
18c0	sbbb %al,%al
1803	sbbb %al,(%rbx)
1a03	sbbb (%rbx),%al
18042500000000 3:11:sym+0	sbbb %al,sym
1a042500000000 3:11:sym+0	sbbb sym,%al
1803	sbb %al,(%rbx)
refused	sbb 4((%rbx,%rsi,2),%al
1c00	sbbb $0,%al
1c01	sbbb $1,%al
1cff	sbbb $-1,%al
1c7f	sbbb $127,%al
1c80	sbbb $128,%al
1c80	sbbb $-128,%al
1c7f	sbbb $-129,%al
1cff	sbbb $255,%al
1c00	sbbb $256,%al
1cff	sbbb $0x7fff,%al
1c00	sbbb $0x8000,%al
1c80	sbbb $0xff80,%al
1cff	sbbb $0xffff,%al
1c00	sbbb $0x10000,%al
1cff	sbbb $0x7fffffff,%al
1c00	sbbb $0x80000000,%al
1c80	sbbb $0xffffff80,%al
1cff	sbbb $0xffffffff,%al
1c00 1:14:sym+0	sbbb $sym,%al
18c8	sbbb %cl,%al
18c1	sbbb %al,%cl
180b	sbbb %cl,(%rbx)
1a0b	sbbb (%rbx),%cl
180c2500000000 3:11:sym+0	sbbb %cl,sym
1a0c2500000000 3:11:sym+0	sbbb sym,%cl
180b	sbb %cl,(%rbx)
refused	sbb 4((%rbx,%rsi,2),%cl
80d900	sbbb $0,%cl
80d901	sbbb $1,%cl
80d9ff	sbbb $-1,%cl
80d97f	sbbb $127,%cl
80d980	sbbb $128,%cl
80d980	sbbb $-128,%cl
80d97f	sbbb $-129,%cl
80d9ff	sbbb $255,%cl
80d900	sbbb $256,%cl
80d9ff	sbbb $0x7fff,%cl
80d900	sbbb $0x8000,%cl
80d980	sbbb $0xff80,%cl
80d9ff	sbbb $0xffff,%cl
80d900	sbbb $0x10000,%cl
80d9ff	sbbb $0x7fffffff,%cl
80d900	sbbb $0x80000000,%cl
80d980	sbbb $0xffffff80,%cl
80d9ff	sbbb $0xffffffff,%cl
80d900 2:14:sym+0	sbbb $sym,%cl
18f8	sbbb %bh,%al
18c7	sbbb %al,%bh
183b	sbbb %bh,(%rbx)
1a3b	sbbb (%rbx),%bh
183c2500000000 3:11:sym+0	sbbb %bh,sym
1a3c2500000000 3:11:sym+0	sbbb sym,%bh
183b	sbb %bh,(%rbx)
refused	sbb 4((%rbx,%rsi,2),%bh
80df00	sbbb $0,%bh
80df01	sbbb $1,%bh
80dfff	sbbb $-1,%bh
80df7f	sbbb $127,%bh
80df80	sbbb $128,%bh
80df80	sbbb $-128,%bh
80df7f	sbbb $-129,%bh
80dfff	sbbb $255,%bh
80df00	sbbb $256,%bh
80dfff	sbbb $0x7fff,%bh
80df00	sbbb $0x8000,%bh
80df80	sbbb $0xff80,%bh
80dfff	sbbb $0xffff,%bh
80df00	sbbb $0x10000,%bh
80dfff	sbbb $0x7fffffff,%bh
80df00	sbbb $0x80000000,%bh
80df80	sbbb $0xffffff80,%bh
80dfff	sbbb $0xffffffff,%bh
80df00 2:14:sym+0	sbbb $sym,%bh
4018f0	sbbb %sil,%al
4018c6	sbbb %al,%sil
401833	sbbb %sil,(%rbx)
401a33	sbbb (%rbx),%sil
4018342500000000 4:11:sym+0	sbbb %sil,sym
401a342500000000 4:11:sym+0	sbbb sym,%sil
401833	sbb %sil,(%rbx)
refused	sbb 4((%rbx,%rsi,2),%sil
4080de00	sbbb $0,%sil
4080de01	sbbb $1,%sil
4080deff	sbbb $-1,%sil
4080de7f	sbbb $127,%sil
4080de80	sbbb $128,%sil
4080de80	sbbb $-128,%sil
4080de7f	sbbb $-129,%sil
4080deff	sbbb $255,%sil
4080de00	sbbb $256,%sil
4080deff	sbbb $0x7fff,%sil
4080de00	sbbb $0x8000,%sil
4080de80	sbbb $0xff80,%sil
4080deff	sbbb $0xffff,%sil
4080de00	sbbb $0x10000,%sil
4080deff	sbbb $0x7fffffff,%sil
4080de00	sbbb $0x80000000,%sil
4080de80	sbbb $0xffffff80,%sil
4080deff	sbbb $0xffffffff,%sil
4080de00 3:14:sym+0	sbbb $sym,%sil
4418c8	sbbb %r9b,%al
4118c1	sbbb %al,%r9b
44180b	sbbb %r9b,(%rbx)
441a0b	sbbb (%rbx),%r9b
44180c2500000000 4:11:sym+0	sbbb %r9b,sym
441a0c2500000000 4:11:sym+0	sbbb sym,%r9b
44180b	sbb %r9b,(%rbx)
refused	sbb 4((%rbx,%rsi,2),%r9b
4180d900	sbbb $0,%r9b
4180d901	sbbb $1,%r9b
4180d9ff	sbbb $-1,%r9b
4180d97f	sbbb $127,%r9b
4180d980	sbbb $128,%r9b
4180d980	sbbb $-128,%r9b
4180d97f	sbbb $-129,%r9b
4180d9ff	sbbb $255,%r9b
4180d900	sbbb $256,%r9b
4180d9ff	sbbb $0x7fff,%r9b
4180d900	sbbb $0x8000,%r9b
4180d980	sbbb $0xff80,%r9b
4180d9ff	sbbb $0xffff,%r9b
4180d900	sbbb $0x10000,%r9b
4180d9ff	sbbb $0x7fffffff,%r9b
4180d900	sbbb $0x80000000,%r9b
4180d980	sbbb $0xffffff80,%r9b
4180d9ff	sbbb $0xffffffff,%r9b
4180d900 3:14:sym+0	sbbb $sym,%r9b
801b00	sbbb $0,(%rbx)
801c250000000000 3:11:sym+0	sbbb $0,sym
805c8dfc00	sbbb $0,-4(%rbp,%rcx,4)
801b01	sbbb $1,(%rbx)
801c250000000001 3:11:sym+0	sbbb $1,sym
805c8dfc01	sbbb $1,-4(%rbp,%rcx,4)
801bff	sbbb $-1,(%rbx)
801c2500000000ff 3:11:sym+0	sbbb $-1,sym
805c8dfcff	sbbb $-1,-4(%rbp,%rcx,4)
801b7f	sbbb $127,(%rbx)
801c25000000007f 3:11:sym+0	sbbb $127,sym
805c8dfc7f	sbbb $127,-4(%rbp,%rcx,4)
801b80	sbbb $128,(%rbx)
801c250000000080 3:11:sym+0	sbbb $128,sym
805c8dfc80	sbbb $128,-4(%rbp,%rcx,4)
801b80	sbbb $-128,(%rbx)
801c250000000080 3:11:sym+0	sbbb $-128,sym
805c8dfc80	sbbb $-128,-4(%rbp,%rcx,4)
801b7f	sbbb $-129,(%rbx)
801c25000000007f 3:11:sym+0	sbbb $-129,sym
805c8dfc7f	sbbb $-129,-4(%rbp,%rcx,4)
801bff	sbbb $255,(%rbx)
801c2500000000ff 3:11:sym+0	sbbb $255,sym
805c8dfcff	sbbb $255,-4(%rbp,%rcx,4)
801b00	sbbb $256,(%rbx)
801c250000000000 3:11:sym+0	sbbb $256,sym
805c8dfc00	sbbb $256,-4(%rbp,%rcx,4)
801bff	sbbb $0x7fff,(%rbx)
801c2500000000ff 3:11:sym+0	sbbb $0x7fff,sym
805c8dfcff	sbbb $0x7fff,-4(%rbp,%rcx,4)
801b00	sbbb $0x8000,(%rbx)
801c250000000000 3:11:sym+0	sbbb $0x8000,sym
805c8dfc00	sbbb $0x8000,-4(%rbp,%rcx,4)
801b80	sbbb $0xff80,(%rbx)
801c250000000080 3:11:sym+0	sbbb $0xff80,sym
805c8dfc80	sbbb $0xff80,-4(%rbp,%rcx,4)
801bff	sbbb $0xffff,(%rbx)
801c2500000000ff 3:11:sym+0	sbbb $0xffff,sym
805c8dfcff	sbbb $0xffff,-4(%rbp,%rcx,4)
801b00	sbbb $0x10000,(%rbx)
801c250000000000 3:11:sym+0	sbbb $0x10000,sym
805c8dfc00	sbbb $0x10000,-4(%rbp,%rcx,4)
801bff	sbbb $0x7fffffff,(%rbx)
801c2500000000ff 3:11:sym+0	sbbb $0x7fffffff,sym
805c8dfcff	sbbb $0x7fffffff,-4(%rbp,%rcx,4)
801b00	sbbb $0x80000000,(%rbx)
801c250000000000 3:11:sym+0	sbbb $0x80000000,sym
805c8dfc00	sbbb $0x80000000,-4(%rbp,%rcx,4)
801b80	sbbb $0xffffff80,(%rbx)
801c250000000080 3:11:sym+0	sbbb $0xffffff80,sym
805c8dfc80	sbbb $0xffffff80,-4(%rbp,%rcx,4)
801bff	sbbb $0xffffffff,(%rbx)
801c2500000000ff 3:11:sym+0	sbbb $0xffffffff,sym
805c8dfcff	sbbb $0xffffffff,-4(%rbp,%rcx,4)
801b00 2:14:sym+0	sbbb $sym,(%rbx)
801c250000000000 3:11:sym+0 7:14:sym+0	sbbb $sym,sym
805c8dfc00 4:14:sym+0	sbbb $sym,-4(%rbp,%rcx,4)
80d901	sbb $1,%cl
831b01	sbb $1,(%rbx)
refused	sbbb $1,$2
refused	sbbb %al,$2
refused	sbbb (%rbx),(%rbx)
f0180b	lock sbbb %cl,(%rbx)
f0801b01	lock sbbb $1,(%rbx)
refused	lock sbbb (%rbx),%cl
refused	lock sbbb %cl,%al
6619c0	sbbw %ax,%ax
661903	sbbw %ax,(%rbx)
661b03	sbbw (%rbx),%ax
6619042500000000 4:11:sym+0	sbbw %ax,sym
661b042500000000 4:11:sym+0	sbbw sym,%ax
661903	sbb %ax,(%rbx)
refused	sbb 4((%rbx,%rsi,2),%ax
6683d800	sbbw $0,%ax
6683d801	sbbw $1,%ax
6683d8ff	sbbw $-1,%ax
6683d87f	sbbw $127,%ax
661d8000	sbbw $128,%ax
6683d880	sbbw $-128,%ax
661d7fff	sbbw $-129,%ax
661dff00	sbbw $255,%ax
661d0001	sbbw $256,%ax
661dff7f	sbbw $0x7fff,%ax
661d0080	sbbw $0x8000,%ax
6683d880	sbbw $0xff80,%ax
6683d8ff	sbbw $0xffff,%ax
661d0000	sbbw $0x10000,%ax
661dffff	sbbw $0x7fffffff,%ax
661d0000	sbbw $0x80000000,%ax
6683d880	sbbw $0xffffff80,%ax
6683d8ff	sbbw $0xffffffff,%ax
661d0000 2:12:sym+0	sbbw $sym,%ax
6619d8	sbbw %bx,%ax
6619c3	sbbw %ax,%bx
66191b	sbbw %bx,(%rbx)
661b1b	sbbw (%rbx),%bx
66191c2500000000 4:11:sym+0	sbbw %bx,sym
661b1c2500000000 4:11:sym+0	sbbw sym,%bx
66191b	sbb %bx,(%rbx)
refused	sbb 4((%rbx,%rsi,2),%bx
6683db00	sbbw $0,%bx
6683db01	sbbw $1,%bx
6683dbff	sbbw $-1,%bx
6683db7f	sbbw $127,%bx
6681db8000	sbbw $128,%bx
6683db80	sbbw $-128,%bx
6681db7fff	sbbw $-129,%bx
6681dbff00	sbbw $255,%bx
6681db0001	sbbw $256,%bx
6681dbff7f	sbbw $0x7fff,%bx
6681db0080	sbbw $0x8000,%bx
6683db80	sbbw $0xff80,%bx
6683dbff	sbbw $0xffff,%bx
6681db0000	sbbw $0x10000,%bx
6681dbffff	sbbw $0x7fffffff,%bx
6681db0000	sbbw $0x80000000,%bx
6683db80	sbbw $0xffffff80,%bx
6683dbff	sbbw $0xffffffff,%bx
6681db0000 3:12:sym+0	sbbw $sym,%bx
6619f0	sbbw %si,%ax
6619c6	sbbw %ax,%si
661933	sbbw %si,(%rbx)
661b33	sbbw (%rbx),%si
6619342500000000 4:11:sym+0	sbbw %si,sym
661b342500000000 4:11:sym+0	sbbw sym,%si
661933	sbb %si,(%rbx)
refused	sbb 4((%rbx,%rsi,2),%si
6683de00	sbbw $0,%si
6683de01	sbbw $1,%si
6683deff	sbbw $-1,%si
6683de7f	sbbw $127,%si
6681de8000	sbbw $128,%si
6683de80	sbbw $-128,%si
6681de7fff	sbbw $-129,%si
6681deff00	sbbw $255,%si
6681de0001	sbbw $256,%si
6681deff7f	sbbw $0x7fff,%si
6681de0080	sbbw $0x8000,%si
6683de80	sbbw $0xff80,%si
6683deff	sbbw $0xffff,%si
6681de0000	sbbw $0x10000,%si
6681deffff	sbbw $0x7fffffff,%si
6681de0000	sbbw $0x80000000,%si
6683de80	sbbw $0xffffff80,%si
6683deff	sbbw $0xffffffff,%si
6681de0000 3:12:sym+0	sbbw $sym,%si
664419c8	sbbw %r9w,%ax
664119c1	sbbw %ax,%r9w
6644190b	sbbw %r9w,(%rbx)
66441b0b	sbbw (%rbx),%r9w
6644190c2500000000 5:11:sym+0	sbbw %r9w,sym
66441b0c2500000000 5:11:sym+0	sbbw sym,%r9w
6644190b	sbb %r9w,(%rbx)
refused	sbb 4((%rbx,%rsi,2),%r9w
664183d900	sbbw $0,%r9w
664183d901	sbbw $1,%r9w
664183d9ff	sbbw $-1,%r9w
664183d97f	sbbw $127,%r9w
664181d98000	sbbw $128,%r9w
664183d980	sbbw $-128,%r9w
664181d97fff	sbbw $-129,%r9w
664181d9ff00	sbbw $255,%r9w
664181d90001	sbbw $256,%r9w
664181d9ff7f	sbbw $0x7fff,%r9w
664181d90080	sbbw $0x8000,%r9w
664183d980	sbbw $0xff80,%r9w
664183d9ff	sbbw $0xffff,%r9w
664181d90000	sbbw $0x10000,%r9w
664181d9ffff	sbbw $0x7fffffff,%r9w
664181d90000	sbbw $0x80000000,%r9w
664183d980	sbbw $0xffffff80,%r9w
664183d9ff	sbbw $0xffffffff,%r9w
664181d90000 4:12:sym+0	sbbw $sym,%r9w
66831b00	sbbw $0,(%rbx)
66831c250000000000 4:11:sym+0	sbbw $0,sym
66835c8dfc00	sbbw $0,-4(%rbp,%rcx,4)
66831b01	sbbw $1,(%rbx)
66831c250000000001 4:11:sym+0	sbbw $1,sym
66835c8dfc01	sbbw $1,-4(%rbp,%rcx,4)
66831bff	sbbw $-1,(%rbx)
66831c2500000000ff 4:11:sym+0	sbbw $-1,sym
66835c8dfcff	sbbw $-1,-4(%rbp,%rcx,4)
66831b7f	sbbw $127,(%rbx)
66831c25000000007f 4:11:sym+0	sbbw $127,sym
66835c8dfc7f	sbbw $127,-4(%rbp,%rcx,4)
66811b8000	sbbw $128,(%rbx)
66811c25000000008000 4:11:sym+0	sbbw $128,sym
66815c8dfc8000	sbbw $128,-4(%rbp,%rcx,4)
66831b80	sbbw $-128,(%rbx)
66831c250000000080 4:11:sym+0	sbbw $-128,sym
66835c8dfc80	sbbw $-128,-4(%rbp,%rcx,4)
66811b7fff	sbbw $-129,(%rbx)
66811c25000000007fff 4:11:sym+0	sbbw $-129,sym
66815c8dfc7fff	sbbw $-129,-4(%rbp,%rcx,4)
66811bff00	sbbw $255,(%rbx)
66811c2500000000ff00 4:11:sym+0	sbbw $255,sym
66815c8dfcff00	sbbw $255,-4(%rbp,%rcx,4)
66811b0001	sbbw $256,(%rbx)
66811c25000000000001 4:11:sym+0	sbbw $256,sym
66815c8dfc0001	sbbw $256,-4(%rbp,%rcx,4)
66811bff7f	sbbw $0x7fff,(%rbx)
66811c2500000000ff7f 4:11:sym+0	sbbw $0x7fff,sym
66815c8dfcff7f	sbbw $0x7fff,-4(%rbp,%rcx,4)
66811b0080	sbbw $0x8000,(%rbx)
66811c25000000000080 4:11:sym+0	sbbw $0x8000,sym
66815c8dfc0080	sbbw $0x8000,-4(%rbp,%rcx,4)
66831b80	sbbw $0xff80,(%rbx)
66831c250000000080 4:11:sym+0	sbbw $0xff80,sym
66835c8dfc80	sbbw $0xff80,-4(%rbp,%rcx,4)
66831bff	sbbw $0xffff,(%rbx)
66831c2500000000ff 4:11:sym+0	sbbw $0xffff,sym
66835c8dfcff	sbbw $0xffff,-4(%rbp,%rcx,4)
66811b0000	sbbw $0x10000,(%rbx)
66811c25000000000000 4:11:sym+0	sbbw $0x10000,sym
66815c8dfc0000	sbbw $0x10000,-4(%rbp,%rcx,4)
66811bffff	sbbw $0x7fffffff,(%rbx)
66811c2500000000ffff 4:11:sym+0	sbbw $0x7fffffff,sym
66815c8dfcffff	sbbw $0x7fffffff,-4(%rbp,%rcx,4)
66811b0000	sbbw $0x80000000,(%rbx)
66811c25000000000000 4:11:sym+0	sbbw $0x80000000,sym
66815c8dfc0000	sbbw $0x80000000,-4(%rbp,%rcx,4)
66831b80	sbbw $0xffffff80,(%rbx)
66831c250000000080 4:11:sym+0	sbbw $0xffffff80,sym
66835c8dfc80	sbbw $0xffffff80,-4(%rbp,%rcx,4)
66831bff	sbbw $0xffffffff,(%rbx)
66831c2500000000ff 4:11:sym+0	sbbw $0xffffffff,sym
66835c8dfcff	sbbw $0xffffffff,-4(%rbp,%rcx,4)
66811b0000 3:12:sym+0	sbbw $sym,(%rbx)
66811c25000000000000 4:11:sym+0 8:12:sym+0	sbbw $sym,sym
66815c8dfc0000 5:12:sym+0	sbbw $sym,-4(%rbp,%rcx,4)
6683db01	sbb $1,%bx
refused	sbbw $1,$2
refused	sbbw %ax,$2
refused	sbbw (%rbx),(%rbx)
66f0191b	lock sbbw %bx,(%rbx)
66f0831b01	lock sbbw $1,(%rbx)
refused	lock sbbw (%rbx),%bx
refused	lock sbbw %bx,%ax
19c0	sbbl %eax,%eax
1903	sbbl %eax,(%rbx)
1b03	sbbl (%rbx),%eax
19042500000000 3:11:sym+0	sbbl %eax,sym
1b042500000000 3:11:sym+0	sbbl sym,%eax
1903	sbb %eax,(%rbx)
refused	sbb 4((%rbx,%rsi,2),%eax
83d800	sbbl $0,%eax
83d801	sbbl $1,%eax
83d8ff	sbbl $-1,%eax
83d87f	sbbl $127,%eax
1d80000000	sbbl $128,%eax
83d880	sbbl $-128,%eax
1d7fffffff	sbbl $-129,%eax
1dff000000	sbbl $255,%eax
1d00010000	sbbl $256,%eax
1dff7f0000	sbbl $0x7fff,%eax
1d00800000	sbbl $0x8000,%eax
1d80ff0000	sbbl $0xff80,%eax
1dffff0000	sbbl $0xffff,%eax
1d00000100	sbbl $0x10000,%eax
1dffffff7f	sbbl $0x7fffffff,%eax
1d00000080	sbbl $0x80000000,%eax
83d880	sbbl $0xffffff80,%eax
83d8ff	sbbl $0xffffffff,%eax
1d00000000 1:10:sym+0	sbbl $sym,%eax
19d8	sbbl %ebx,%eax
19c3	sbbl %eax,%ebx
191b	sbbl %ebx,(%rbx)
1b1b	sbbl (%rbx),%ebx
191c2500000000 3:11:sym+0	sbbl %ebx,sym
1b1c2500000000 3:11:sym+0	sbbl sym,%ebx
191b	sbb %ebx,(%rbx)
refused	sbb 4((%rbx,%rsi,2),%ebx
83db00	sbbl $0,%ebx
83db01	sbbl $1,%ebx
83dbff	sbbl $-1,%ebx
83db7f	sbbl $127,%ebx
81db80000000	sbbl $128,%ebx
83db80	sbbl $-128,%ebx
81db7fffffff	sbbl $-129,%ebx
81dbff000000	sbbl $255,%ebx
81db00010000	sbbl $256,%ebx
81dbff7f0000	sbbl $0x7fff,%ebx
81db00800000	sbbl $0x8000,%ebx
81db80ff0000	sbbl $0xff80,%ebx
81dbffff0000	sbbl $0xffff,%ebx
81db00000100	sbbl $0x10000,%ebx
81dbffffff7f	sbbl $0x7fffffff,%ebx
81db00000080	sbbl $0x80000000,%ebx
83db80	sbbl $0xffffff80,%ebx
83dbff	sbbl $0xffffffff,%ebx
81db00000000 2:10:sym+0	sbbl $sym,%ebx
19e0	sbbl %esp,%eax
19c4	sbbl %eax,%esp
1923	sbbl %esp,(%rbx)
1b23	sbbl (%rbx),%esp
19242500000000 3:11:sym+0	sbbl %esp,sym
1b242500000000 3:11:sym+0	sbbl sym,%esp
1923	sbb %esp,(%rbx)
refused	sbb 4((%rbx,%rsi,2),%esp
83dc00	sbbl $0,%esp
83dc01	sbbl $1,%esp
83dcff	sbbl $-1,%esp
83dc7f	sbbl $127,%esp
81dc80000000	sbbl $128,%esp
83dc80	sbbl $-128,%esp
81dc7fffffff	sbbl $-129,%esp
81dcff000000	sbbl $255,%esp
81dc00010000	sbbl $256,%esp
81dcff7f0000	sbbl $0x7fff,%esp
81dc00800000	sbbl $0x8000,%esp
81dc80ff0000	sbbl $0xff80,%esp
81dcffff0000	sbbl $0xffff,%esp
81dc00000100	sbbl $0x10000,%esp
81dcffffff7f	sbbl $0x7fffffff,%esp
81dc00000080	sbbl $0x80000000,%esp
83dc80	sbbl $0xffffff80,%esp
83dcff	sbbl $0xffffffff,%esp
81dc00000000 2:10:sym+0	sbbl $sym,%esp
19e8	sbbl %ebp,%eax
19c5	sbbl %eax,%ebp
192b	sbbl %ebp,(%rbx)
1b2b	sbbl (%rbx),%ebp
192c2500000000 3:11:sym+0	sbbl %ebp,sym
1b2c2500000000 3:11:sym+0	sbbl sym,%ebp
192b	sbb %ebp,(%rbx)
refused	sbb 4((%rbx,%rsi,2),%ebp
83dd00	sbbl $0,%ebp
83dd01	sbbl $1,%ebp
83ddff	sbbl $-1,%ebp
83dd7f	sbbl $127,%ebp
81dd80000000	sbbl $128,%ebp
83dd80	sbbl $-128,%ebp
81dd7fffffff	sbbl $-129,%ebp
81ddff000000	sbbl $255,%ebp
81dd00010000	sbbl $256,%ebp
81ddff7f0000	sbbl $0x7fff,%ebp
81dd00800000	sbbl $0x8000,%ebp
81dd80ff0000	sbbl $0xff80,%ebp
81ddffff0000	sbbl $0xffff,%ebp
81dd00000100	sbbl $0x10000,%ebp
81ddffffff7f	sbbl $0x7fffffff,%ebp
81dd00000080	sbbl $0x80000000,%ebp
83dd80	sbbl $0xffffff80,%ebp
83ddff	sbbl $0xffffffff,%ebp
81dd00000000 2:10:sym+0	sbbl $sym,%ebp
4419c8	sbbl %r9d,%eax
4119c1	sbbl %eax,%r9d
44190b	sbbl %r9d,(%rbx)
441b0b	sbbl (%rbx),%r9d
44190c2500000000 4:11:sym+0	sbbl %r9d,sym
441b0c2500000000 4:11:sym+0	sbbl sym,%r9d
44190b	sbb %r9d,(%rbx)
refused	sbb 4((%rbx,%rsi,2),%r9d
4183d900	sbbl $0,%r9d
4183d901	sbbl $1,%r9d
4183d9ff	sbbl $-1,%r9d
4183d97f	sbbl $127,%r9d
4181d980000000	sbbl $128,%r9d
4183d980	sbbl $-128,%r9d
4181d97fffffff	sbbl $-129,%r9d
4181d9ff000000	sbbl $255,%r9d
4181d900010000	sbbl $256,%r9d
4181d9ff7f0000	sbbl $0x7fff,%r9d
4181d900800000	sbbl $0x8000,%r9d
4181d980ff0000	sbbl $0xff80,%r9d
4181d9ffff0000	sbbl $0xffff,%r9d
4181d900000100	sbbl $0x10000,%r9d
4181d9ffffff7f	sbbl $0x7fffffff,%r9d
4181d900000080	sbbl $0x80000000,%r9d
4183d980	sbbl $0xffffff80,%r9d
4183d9ff	sbbl $0xffffffff,%r9d
4181d900000000 3:10:sym+0	sbbl $sym,%r9d
831b00	sbbl $0,(%rbx)
831c250000000000 3:11:sym+0	sbbl $0,sym
835c8dfc00	sbbl $0,-4(%rbp,%rcx,4)
831b01	sbbl $1,(%rbx)
831c250000000001 3:11:sym+0	sbbl $1,sym
835c8dfc01	sbbl $1,-4(%rbp,%rcx,4)
831bff	sbbl $-1,(%rbx)
831c2500000000ff 3:11:sym+0	sbbl $-1,sym
835c8dfcff	sbbl $-1,-4(%rbp,%rcx,4)
831b7f	sbbl $127,(%rbx)
831c25000000007f 3:11:sym+0	sbbl $127,sym
835c8dfc7f	sbbl $127,-4(%rbp,%rcx,4)
811b80000000	sbbl $128,(%rbx)
811c250000000080000000 3:11:sym+0	sbbl $128,sym
815c8dfc80000000	sbbl $128,-4(%rbp,%rcx,4)
831b80	sbbl $-128,(%rbx)
831c250000000080 3:11:sym+0	sbbl $-128,sym
835c8dfc80	sbbl $-128,-4(%rbp,%rcx,4)
811b7fffffff	sbbl $-129,(%rbx)
811c25000000007fffffff 3:11:sym+0	sbbl $-129,sym
815c8dfc7fffffff	sbbl $-129,-4(%rbp,%rcx,4)
811bff000000	sbbl $255,(%rbx)
811c2500000000ff000000 3:11:sym+0	sbbl $255,sym
815c8dfcff000000	sbbl $255,-4(%rbp,%rcx,4)
811b00010000	sbbl $256,(%rbx)
811c250000000000010000 3:11:sym+0	sbbl $256,sym
815c8dfc00010000	sbbl $256,-4(%rbp,%rcx,4)
811bff7f0000	sbbl $0x7fff,(%rbx)
811c2500000000ff7f0000 3:11:sym+0	sbbl $0x7fff,sym
815c8dfcff7f0000	sbbl $0x7fff,-4(%rbp,%rcx,4)
811b00800000	sbbl $0x8000,(%rbx)
811c250000000000800000 3:11:sym+0	sbbl $0x8000,sym
815c8dfc00800000	sbbl $0x8000,-4(%rbp,%rcx,4)
811b80ff0000	sbbl $0xff80,(%rbx)
811c250000000080ff0000 3:11:sym+0	sbbl $0xff80,sym
815c8dfc80ff0000	sbbl $0xff80,-4(%rbp,%rcx,4)
811bffff0000	sbbl $0xffff,(%rbx)
811c2500000000ffff0000 3:11:sym+0	sbbl $0xffff,sym
815c8dfcffff0000	sbbl $0xffff,-4(%rbp,%rcx,4)
811b00000100	sbbl $0x10000,(%rbx)
811c250000000000000100 3:11:sym+0	sbbl $0x10000,sym
815c8dfc00000100	sbbl $0x10000,-4(%rbp,%rcx,4)
811bffffff7f	sbbl $0x7fffffff,(%rbx)
811c2500000000ffffff7f 3:11:sym+0	sbbl $0x7fffffff,sym
815c8dfcffffff7f	sbbl $0x7fffffff,-4(%rbp,%rcx,4)
811b00000080	sbbl $0x80000000,(%rbx)
811c250000000000000080 3:11:sym+0	sbbl $0x80000000,sym
815c8dfc00000080	sbbl $0x80000000,-4(%rbp,%rcx,4)
831b80	sbbl $0xffffff80,(%rbx)
831c250000000080 3:11:sym+0	sbbl $0xffffff80,sym
835c8dfc80	sbbl $0xffffff80,-4(%rbp,%rcx,4)
831bff	sbbl $0xffffffff,(%rbx)
831c2500000000ff 3:11:sym+0	sbbl $0xffffffff,sym
835c8dfcff	sbbl $0xffffffff,-4(%rbp,%rcx,4)
811b00000000 2:10:sym+0	sbbl $sym,(%rbx)
811c250000000000000000 3:11:sym+0 7:10:sym+0	sbbl $sym,sym
815c8dfc00000000 4:10:sym+0	sbbl $sym,-4(%rbp,%rcx,4)
83db01	sbb $1,%ebx
refused	sbbl $1,$2
refused	sbbl %eax,$2
refused	sbbl (%rbx),(%rbx)
f0191b	lock sbbl %ebx,(%rbx)
f0831b01	lock sbbl $1,(%rbx)
refused	lock sbbl (%rbx),%ebx
refused	lock sbbl %ebx,%eax
4819c0	sbbq %rax,%rax
481903	sbbq %rax,(%rbx)
481b03	sbbq (%rbx),%rax
4819042500000000 4:11:sym+0	sbbq %rax,sym
481b042500000000 4:11:sym+0	sbbq sym,%rax
481903	sbb %rax,(%rbx)
refused	sbb 4((%rbx,%rsi,2),%rax
4883d800	sbbq $0,%rax
4883d801	sbbq $1,%rax
4883d8ff	sbbq $-1,%rax
4883d87f	sbbq $127,%rax
481d80000000	sbbq $128,%rax
4883d880	sbbq $-128,%rax
481d7fffffff	sbbq $-129,%rax
481dff000000	sbbq $255,%rax
481d00010000	sbbq $256,%rax
481dff7f0000	sbbq $0x7fff,%rax
481d00800000	sbbq $0x8000,%rax
481d80ff0000	sbbq $0xff80,%rax
481dffff0000	sbbq $0xffff,%rax
481d00000100	sbbq $0x10000,%rax
481dffffff7f	sbbq $0x7fffffff,%rax
refused	sbbq $0x80000000,%rax
refused	sbbq $0xffffff80,%rax
refused	sbbq $0xffffffff,%rax
481d00000000 2:11:sym+0	sbbq $sym,%rax
4819d8	sbbq %rbx,%rax
4819c3	sbbq %rax,%rbx
48191b	sbbq %rbx,(%rbx)
481b1b	sbbq (%rbx),%rbx
48191c2500000000 4:11:sym+0	sbbq %rbx,sym
481b1c2500000000 4:11:sym+0	sbbq sym,%rbx
48191b	sbb %rbx,(%rbx)
refused	sbb 4((%rbx,%rsi,2),%rbx
4883db00	sbbq $0,%rbx
4883db01	sbbq $1,%rbx
4883dbff	sbbq $-1,%rbx
4883db7f	sbbq $127,%rbx
4881db80000000	sbbq $128,%rbx
4883db80	sbbq $-128,%rbx
4881db7fffffff	sbbq $-129,%rbx
4881dbff000000	sbbq $255,%rbx
4881db00010000	sbbq $256,%rbx
4881dbff7f0000	sbbq $0x7fff,%rbx
4881db00800000	sbbq $0x8000,%rbx
4881db80ff0000	sbbq $0xff80,%rbx
4881dbffff0000	sbbq $0xffff,%rbx
4881db00000100	sbbq $0x10000,%rbx
4881dbffffff7f	sbbq $0x7fffffff,%rbx
refused	sbbq $0x80000000,%rbx
refused	sbbq $0xffffff80,%rbx
refused	sbbq $0xffffffff,%rbx
4881db00000000 3:11:sym+0	sbbq $sym,%rbx
4819e0	sbbq %rsp,%rax
4819c4	sbbq %rax,%rsp
481923	sbbq %rsp,(%rbx)
481b23	sbbq (%rbx),%rsp
4819242500000000 4:11:sym+0	sbbq %rsp,sym
481b242500000000 4:11:sym+0	sbbq sym,%rsp
481923	sbb %rsp,(%rbx)
refused	sbb 4((%rbx,%rsi,2),%rsp
4883dc00	sbbq $0,%rsp
4883dc01	sbbq $1,%rsp
4883dcff	sbbq $-1,%rsp
4883dc7f	sbbq $127,%rsp
4881dc80000000	sbbq $128,%rsp
4883dc80	sbbq $-128,%rsp
4881dc7fffffff	sbbq $-129,%rsp
4881dcff000000	sbbq $255,%rsp
4881dc00010000	sbbq $256,%rsp
4881dcff7f0000	sbbq $0x7fff,%rsp
4881dc00800000	sbbq $0x8000,%rsp
4881dc80ff0000	sbbq $0xff80,%rsp
4881dcffff0000	sbbq $0xffff,%rsp
4881dc00000100	sbbq $0x10000,%rsp
4881dcffffff7f	sbbq $0x7fffffff,%rsp
refused	sbbq $0x80000000,%rsp
refused	sbbq $0xffffff80,%rsp
refused	sbbq $0xffffffff,%rsp
4881dc00000000 3:11:sym+0	sbbq $sym,%rsp
4c19e0	sbbq %r12,%rax
4919c4	sbbq %rax,%r12
4c1923	sbbq %r12,(%rbx)
4c1b23	sbbq (%rbx),%r12
4c19242500000000 4:11:sym+0	sbbq %r12,sym
4c1b242500000000 4:11:sym+0	sbbq sym,%r12
4c1923	sbb %r12,(%rbx)
refused	sbb 4((%rbx,%rsi,2),%r12
4983dc00	sbbq $0,%r12
4983dc01	sbbq $1,%r12
4983dcff	sbbq $-1,%r12
4983dc7f	sbbq $127,%r12
4981dc80000000	sbbq $128,%r12
4983dc80	sbbq $-128,%r12
4981dc7fffffff	sbbq $-129,%r12
4981dcff000000	sbbq $255,%r12
4981dc00010000	sbbq $256,%r12
4981dcff7f0000	sbbq $0x7fff,%r12
4981dc00800000	sbbq $0x8000,%r12
4981dc80ff0000	sbbq $0xff80,%r12
4981dcffff0000	sbbq $0xffff,%r12
4981dc00000100	sbbq $0x10000,%r12
4981dcffffff7f	sbbq $0x7fffffff,%r12
refused	sbbq $0x80000000,%r12
refused	sbbq $0xffffff80,%r12
refused	sbbq $0xffffffff,%r12
4981dc00000000 3:11:sym+0	sbbq $sym,%r12
4c19e8	sbbq %r13,%rax
4919c5	sbbq %rax,%r13
4c192b	sbbq %r13,(%rbx)
4c1b2b	sbbq (%rbx),%r13
4c192c2500000000 4:11:sym+0	sbbq %r13,sym
4c1b2c2500000000 4:11:sym+0	sbbq sym,%r13
4c192b	sbb %r13,(%rbx)
refused	sbb 4((%rbx,%rsi,2),%r13
4983dd00	sbbq $0,%r13
4983dd01	sbbq $1,%r13
4983ddff	sbbq $-1,%r13
4983dd7f	sbbq $127,%r13
4981dd80000000	sbbq $128,%r13
4983dd80	sbbq $-128,%r13
4981dd7fffffff	sbbq $-129,%r13
4981ddff000000	sbbq $255,%r13
4981dd00010000	sbbq $256,%r13
4981ddff7f0000	sbbq $0x7fff,%r13
4981dd00800000	sbbq $0x8000,%r13
4981dd80ff0000	sbbq $0xff80,%r13
4981ddffff0000	sbbq $0xffff,%r13
4981dd00000100	sbbq $0x10000,%r13
4981ddffffff7f	sbbq $0x7fffffff,%r13
refused	sbbq $0x80000000,%r13
refused	sbbq $0xffffff80,%r13
refused	sbbq $0xffffffff,%r13
4981dd00000000 3:11:sym+0	sbbq $sym,%r13
4c19c8	sbbq %r9,%rax
4919c1	sbbq %rax,%r9
4c190b	sbbq %r9,(%rbx)
4c1b0b	sbbq (%rbx),%r9
4c190c2500000000 4:11:sym+0	sbbq %r9,sym
4c1b0c2500000000 4:11:sym+0	sbbq sym,%r9
4c190b	sbb %r9,(%rbx)
refused	sbb 4((%rbx,%rsi,2),%r9
4983d900	sbbq $0,%r9
4983d901	sbbq $1,%r9
4983d9ff	sbbq $-1,%r9
4983d97f	sbbq $127,%r9
4981d980000000	sbbq $128,%r9
4983d980	sbbq $-128,%r9
4981d97fffffff	sbbq $-129,%r9
4981d9ff000000	sbbq $255,%r9
4981d900010000	sbbq $256,%r9
4981d9ff7f0000	sbbq $0x7fff,%r9
4981d900800000	sbbq $0x8000,%r9
4981d980ff0000	sbbq $0xff80,%r9
4981d9ffff0000	sbbq $0xffff,%r9
4981d900000100	sbbq $0x10000,%r9
4981d9ffffff7f	sbbq $0x7fffffff,%r9
refused	sbbq $0x80000000,%r9
refused	sbbq $0xffffff80,%r9
refused	sbbq $0xffffffff,%r9
4981d900000000 3:11:sym+0	sbbq $sym,%r9
48831b00	sbbq $0,(%rbx)
48831c250000000000 4:11:sym+0	sbbq $0,sym
48835c8dfc00	sbbq $0,-4(%rbp,%rcx,4)
48831b01	sbbq $1,(%rbx)
48831c250000000001 4:11:sym+0	sbbq $1,sym
48835c8dfc01	sbbq $1,-4(%rbp,%rcx,4)
48831bff	sbbq $-1,(%rbx)
48831c2500000000ff 4:11:sym+0	sbbq $-1,sym
48835c8dfcff	sbbq $-1,-4(%rbp,%rcx,4)
48831b7f	sbbq $127,(%rbx)
48831c25000000007f 4:11:sym+0	sbbq $127,sym
48835c8dfc7f	sbbq $127,-4(%rbp,%rcx,4)
48811b80000000	sbbq $128,(%rbx)
48811c250000000080000000 4:11:sym+0	sbbq $128,sym
48815c8dfc80000000	sbbq $128,-4(%rbp,%rcx,4)
48831b80	sbbq $-128,(%rbx)
48831c250000000080 4:11:sym+0	sbbq $-128,sym
48835c8dfc80	sbbq $-128,-4(%rbp,%rcx,4)
48811b7fffffff	sbbq $-129,(%rbx)
48811c25000000007fffffff 4:11:sym+0	sbbq $-129,sym
48815c8dfc7fffffff	sbbq $-129,-4(%rbp,%rcx,4)
48811bff000000	sbbq $255,(%rbx)
48811c2500000000ff000000 4:11:sym+0	sbbq $255,sym
48815c8dfcff000000	sbbq $255,-4(%rbp,%rcx,4)
48811b00010000	sbbq $256,(%rbx)
48811c250000000000010000 4:11:sym+0	sbbq $256,sym
48815c8dfc00010000	sbbq $256,-4(%rbp,%rcx,4)
48811bff7f0000	sbbq $0x7fff,(%rbx)
48811c2500000000ff7f0000 4:11:sym+0	sbbq $0x7fff,sym
48815c8dfcff7f0000	sbbq $0x7fff,-4(%rbp,%rcx,4)
48811b00800000	sbbq $0x8000,(%rbx)
48811c250000000000800000 4:11:sym+0	sbbq $0x8000,sym
48815c8dfc00800000	sbbq $0x8000,-4(%rbp,%rcx,4)
48811b80ff0000	sbbq $0xff80,(%rbx)
48811c250000000080ff0000 4:11:sym+0	sbbq $0xff80,sym
48815c8dfc80ff0000	sbbq $0xff80,-4(%rbp,%rcx,4)
48811bffff0000	sbbq $0xffff,(%rbx)
48811c2500000000ffff0000 4:11:sym+0	sbbq $0xffff,sym
48815c8dfcffff0000	sbbq $0xffff,-4(%rbp,%rcx,4)
48811b00000100	sbbq $0x10000,(%rbx)
48811c250000000000000100 4:11:sym+0	sbbq $0x10000,sym
48815c8dfc00000100	sbbq $0x10000,-4(%rbp,%rcx,4)
48811bffffff7f	sbbq $0x7fffffff,(%rbx)
48811c2500000000ffffff7f 4:11:sym+0	sbbq $0x7fffffff,sym
48815c8dfcffffff7f	sbbq $0x7fffffff,-4(%rbp,%rcx,4)
refused	sbbq $0x80000000,(%rbx)
refused	sbbq $0x80000000,sym
refused	sbbq $0x80000000,-4(%rbp,%rcx,4)
refused	sbbq $0xffffff80,(%rbx)
refused	sbbq $0xffffff80,sym
refused	sbbq $0xffffff80,-4(%rbp,%rcx,4)
refused	sbbq $0xffffffff,(%rbx)
refused	sbbq $0xffffffff,sym
refused	sbbq $0xffffffff,-4(%rbp,%rcx,4)
48811b00000000 3:11:sym+0	sbbq $sym,(%rbx)
48811c250000000000000000 4:11:sym+0 8:11:sym+0	sbbq $sym,sym
48815c8dfc00000000 5:11:sym+0	sbbq $sym,-4(%rbp,%rcx,4)
4883db01	sbb $1,%rbx
refused	sbbq $1,$2
refused	sbbq %rax,$2
refused	sbbq (%rbx),(%rbx)
f048191b	lock sbbq %rbx,(%rbx)
f048831b01	lock sbbq $1,(%rbx)
refused	lock sbbq (%rbx),%rbx
refused	lock sbbq %rbx,%rax
20c0	andb %al,%al
2003	andb %al,(%rbx)
2203	andb (%rbx),%al
20042500000000 3:11:sym+0	andb %al,sym
22042500000000 3:11:sym+0	andb sym,%al
2003	and %al,(%rbx)
refused	and 4((%rbx,%rsi,2),%al
2400	andb $0,%al
2401	andb $1,%al
24ff	andb $-1,%al
247f	andb $127,%al
2480	andb $128,%al
2480	andb $-128,%al
247f	andb $-129,%al
24ff	andb $255,%al
2400	andb $256,%al
24ff	andb $0x7fff,%al
2400	andb $0x8000,%al
2480	andb $0xff80,%al
24ff	andb $0xffff,%al
2400	andb $0x10000,%al
24ff	andb $0x7fffffff,%al
2400	andb $0x80000000,%al
2480	andb $0xffffff80,%al
24ff	andb $0xffffffff,%al
2400 1:14:sym+0	andb $sym,%al
20c8	andb %cl,%al
20c1	andb %al,%cl
200b	andb %cl,(%rbx)
220b	andb (%rbx),%cl
200c2500000000 3:11:sym+0	andb %cl,sym
220c2500000000 3:11:sym+0	andb sym,%cl
200b	and %cl,(%rbx)
refused	and 4((%rbx,%rsi,2),%cl
80e100	andb $0,%cl
80e101	andb $1,%cl
80e1ff	andb $-1,%cl
80e17f	andb $127,%cl
80e180	andb $128,%cl
80e180	andb $-128,%cl
80e17f	andb $-129,%cl
80e1ff	andb $255,%cl
80e100	andb $256,%cl
80e1ff	andb $0x7fff,%cl
80e100	andb $0x8000,%cl
80e180	andb $0xff80,%cl
80e1ff	andb $0xffff,%cl
80e100	andb $0x10000,%cl
80e1ff	andb $0x7fffffff,%cl
80e100	andb $0x80000000,%cl
80e180	andb $0xffffff80,%cl
80e1ff	andb $0xffffffff,%cl
80e100 2:14:sym+0	andb $sym,%cl
20f8	andb %bh,%al
20c7	andb %al,%bh
203b	andb %bh,(%rbx)
223b	andb (%rbx),%bh
203c2500000000 3:11:sym+0	andb %bh,sym
223c2500000000 3:11:sym+0	andb sym,%bh
203b	and %bh,(%rbx)
refused	and 4((%rbx,%rsi,2),%bh
80e700	andb $0,%bh
80e701	andb $1,%bh
80e7ff	andb $-1,%bh
80e77f	andb $127,%bh
80e780	andb $128,%bh
80e780	andb $-128,%bh
80e77f	andb $-129,%bh
80e7ff	andb $255,%bh
80e700	andb $256,%bh
80e7ff	andb $0x7fff,%bh
80e700	andb $0x8000,%bh
80e780	andb $0xff80,%bh
80e7ff	andb $0xffff,%bh
80e700	andb $0x10000,%bh
80e7ff	andb $0x7fffffff,%bh
80e700	andb $0x80000000,%bh
80e780	andb $0xffffff80,%bh
80e7ff	andb $0xffffffff,%bh
80e700 2:14:sym+0	andb $sym,%bh
4020f0	andb %sil,%al
4020c6	andb %al,%sil
402033	andb %sil,(%rbx)
402233	andb (%rbx),%sil
4020342500000000 4:11:sym+0	andb %sil,sym
4022342500000000 4:11:sym+0	andb sym,%sil
402033	and %sil,(%rbx)
refused	and 4((%rbx,%rsi,2),%sil
4080e600	andb $0,%sil
4080e601	andb $1,%sil
4080e6ff	andb $-1,%sil
4080e67f	andb $127,%sil
4080e680	andb $128,%sil
4080e680	andb $-128,%sil
4080e67f	andb $-129,%sil
4080e6ff	andb $255,%sil
4080e600	andb $256,%sil
4080e6ff	andb $0x7fff,%sil
4080e600	andb $0x8000,%sil
4080e680	andb $0xff80,%sil
4080e6ff	andb $0xffff,%sil
4080e600	andb $0x10000,%sil
4080e6ff	andb $0x7fffffff,%sil
4080e600	andb $0x80000000,%sil
4080e680	andb $0xffffff80,%sil
4080e6ff	andb $0xffffffff,%sil
4080e600 3:14:sym+0	andb $sym,%sil
4420c8	andb %r9b,%al
4120c1	andb %al,%r9b
44200b	andb %r9b,(%rbx)
44220b	andb (%rbx),%r9b
44200c2500000000 4:11:sym+0	andb %r9b,sym
44220c2500000000 4:11:sym+0	andb sym,%r9b
44200b	and %r9b,(%rbx)
refused	and 4((%rbx,%rsi,2),%r9b
4180e100	andb $0,%r9b
4180e101	andb $1,%r9b
4180e1ff	andb $-1,%r9b
4180e17f	andb $127,%r9b
4180e180	andb $128,%r9b
4180e180	andb $-128,%r9b
4180e17f	andb $-129,%r9b
4180e1ff	andb $255,%r9b
4180e100	andb $256,%r9b
4180e1ff	andb $0x7fff,%r9b
4180e100	andb $0x8000,%r9b
4180e180	andb $0xff80,%r9b
4180e1ff	andb $0xffff,%r9b
4180e100	andb $0x10000,%r9b
4180e1ff	andb $0x7fffffff,%r9b
4180e100	andb $0x80000000,%r9b
4180e180	andb $0xffffff80,%r9b
4180e1ff	andb $0xffffffff,%r9b
4180e100 3:14:sym+0	andb $sym,%r9b
802300	andb $0,(%rbx)
8024250000000000 3:11:sym+0	andb $0,sym
80648dfc00	andb $0,-4(%rbp,%rcx,4)
802301	andb $1,(%rbx)
8024250000000001 3:11:sym+0	andb $1,sym
80648dfc01	andb $1,-4(%rbp,%rcx,4)
8023ff	andb $-1,(%rbx)
80242500000000ff 3:11:sym+0	andb $-1,sym
80648dfcff	andb $-1,-4(%rbp,%rcx,4)
80237f	andb $127,(%rbx)
802425000000007f 3:11:sym+0	andb $127,sym
80648dfc7f	andb $127,-4(%rbp,%rcx,4)
802380	andb $128,(%rbx)
8024250000000080 3:11:sym+0	andb $128,sym
80648dfc80	andb $128,-4(%rbp,%rcx,4)
802380	andb $-128,(%rbx)
8024250000000080 3:11:sym+0	andb $-128,sym
80648dfc80	andb $-128,-4(%rbp,%rcx,4)
80237f	andb $-129,(%rbx)
802425000000007f 3:11:sym+0	andb $-129,sym
80648dfc7f	andb $-129,-4(%rbp,%rcx,4)
8023ff	andb $255,(%rbx)
80242500000000ff 3:11:sym+0	andb $255,sym
80648dfcff	andb $255,-4(%rbp,%rcx,4)
802300	andb $256,(%rbx)
8024250000000000 3:11:sym+0	andb $256,sym
80648dfc00	andb $256,-4(%rbp,%rcx,4)
8023ff	andb $0x7fff,(%rbx)
80242500000000ff 3:11:sym+0	andb $0x7fff,sym
80648dfcff	andb $0x7fff,-4(%rbp,%rcx,4)
802300	andb $0x8000,(%rbx)
8024250000000000 3:11:sym+0	andb $0x8000,sym
80648dfc00	andb $0x8000,-4(%rbp,%rcx,4)
802380	andb $0xff80,(%rbx)
8024250000000080 3:11:sym+0	andb $0xff80,sym
80648dfc80	andb $0xff80,-4(%rbp,%rcx,4)
8023ff	andb $0xffff,(%rbx)
80242500000000ff 3:11:sym+0	andb $0xffff,sym
80648dfcff	andb $0xffff,-4(%rbp,%rcx,4)
802300	andb $0x10000,(%rbx)
8024250000000000 3:11:sym+0	andb $0x10000,sym
80648dfc00	andb $0x10000,-4(%rbp,%rcx,4)
8023ff	andb $0x7fffffff,(%rbx)
80242500000000ff 3:11:sym+0	andb $0x7fffffff,sym
80648dfcff	andb $0x7fffffff,-4(%rbp,%rcx,4)
802300	andb $0x80000000,(%rbx)
8024250000000000 3:11:sym+0	andb $0x80000000,sym
80648dfc00	andb $0x80000000,-4(%rbp,%rcx,4)
802380	andb $0xffffff80,(%rbx)
8024250000000080 3:11:sym+0	andb $0xffffff80,sym
80648dfc80	andb $0xffffff80,-4(%rbp,%rcx,4)
8023ff	andb $0xffffffff,(%rbx)
80242500000000ff 3:11:sym+0	andb $0xffffffff,sym
80648dfcff	andb $0xffffffff,-4(%rbp,%rcx,4)
802300 2:14:sym+0	andb $sym,(%rbx)
8024250000000000 3:11:sym+0 7:14:sym+0	andb $sym,sym
80648dfc00 4:14:sym+0	andb $sym,-4(%rbp,%rcx,4)
80e101	and $1,%cl
832301	and $1,(%rbx)
refused	andb $1,$2
refused	andb %al,$2
refused	andb (%rbx),(%rbx)
f0200b	lock andb %cl,(%rbx)
f0802301	lock andb $1,(%rbx)
refused	lock andb (%rbx),%cl
refused	lock andb %cl,%al
6621c0	andw %ax,%ax
662103	andw %ax,(%rbx)
662303	andw (%rbx),%ax
6621042500000000 4:11:sym+0	andw %ax,sym
6623042500000000 4:11:sym+0	andw sym,%ax
662103	and %ax,(%rbx)
refused	and 4((%rbx,%rsi,2),%ax
6683e000	andw $0,%ax
6683e001	andw $1,%ax
6683e0ff	andw $-1,%ax
6683e07f	andw $127,%ax
66258000	andw $128,%ax
6683e080	andw $-128,%ax
66257fff	andw $-129,%ax
6625ff00	andw $255,%ax
66250001	andw $256,%ax
6625ff7f	andw $0x7fff,%ax
66250080	andw $0x8000,%ax
6683e080	andw $0xff80,%ax
6683e0ff	andw $0xffff,%ax
66250000	andw $0x10000,%ax
6625ffff	andw $0x7fffffff,%ax
66250000	andw $0x80000000,%ax
6683e080	andw $0xffffff80,%ax
6683e0ff	andw $0xffffffff,%ax
66250000 2:12:sym+0	andw $sym,%ax
6621d8	andw %bx,%ax
6621c3	andw %ax,%bx
66211b	andw %bx,(%rbx)
66231b	andw (%rbx),%bx
66211c2500000000 4:11:sym+0	andw %bx,sym
66231c2500000000 4:11:sym+0	andw sym,%bx
66211b	and %bx,(%rbx)
refused	and 4((%rbx,%rsi,2),%bx
6683e300	andw $0,%bx
6683e301	andw $1,%bx
6683e3ff	andw $-1,%bx
6683e37f	andw $127,%bx
6681e38000	andw $128,%bx
6683e380	andw $-128,%bx
6681e37fff	andw $-129,%bx
6681e3ff00	andw $255,%bx
6681e30001	andw $256,%bx
6681e3ff7f	andw $0x7fff,%bx
6681e30080	andw $0x8000,%bx
6683e380	andw $0xff80,%bx
6683e3ff	andw $0xffff,%bx
6681e30000	andw $0x10000,%bx
6681e3ffff	andw $0x7fffffff,%bx
6681e30000	andw $0x80000000,%bx
6683e380	andw $0xffffff80,%bx
6683e3ff	andw $0xffffffff,%bx
6681e30000 3:12:sym+0	andw $sym,%bx
6621f0	andw %si,%ax
6621c6	andw %ax,%si
662133	andw %si,(%rbx)
662333	andw (%rbx),%si
6621342500000000 4:11:sym+0	andw %si,sym
6623342500000000 4:11:sym+0	andw sym,%si
662133	and %si,(%rbx)
refused	and 4((%rbx,%rsi,2),%si
6683e600	andw $0,%si
6683e601	andw $1,%si
6683e6ff	andw $-1,%si
6683e67f	andw $127,%si
6681e68000	andw $128,%si
6683e680	andw $-128,%si
6681e67fff	andw $-129,%si
6681e6ff00	andw $255,%si
6681e60001	andw $256,%si
6681e6ff7f	andw $0x7fff,%si
6681e60080	andw $0x8000,%si
6683e680	andw $0xff80,%si
6683e6ff	andw $0xffff,%si
6681e60000	andw $0x10000,%si
6681e6ffff	andw $0x7fffffff,%si
6681e60000	andw $0x80000000,%si
6683e680	andw $0xffffff80,%si
6683e6ff	andw $0xffffffff,%si
6681e60000 3:12:sym+0	andw $sym,%si
664421c8	andw %r9w,%ax
664121c1	andw %ax,%r9w
6644210b	andw %r9w,(%rbx)
6644230b	andw (%rbx),%r9w
6644210c2500000000 5:11:sym+0	andw %r9w,sym
6644230c2500000000 5:11:sym+0	andw sym,%r9w
6644210b	and %r9w,(%rbx)
refused	and 4((%rbx,%rsi,2),%r9w
664183e100	andw $0,%r9w
664183e101	andw $1,%r9w
664183e1ff	andw $-1,%r9w
664183e17f	andw $127,%r9w
664181e18000	andw $128,%r9w
664183e180	andw $-128,%r9w
664181e17fff	andw $-129,%r9w
664181e1ff00	andw $255,%r9w
664181e10001	andw $256,%r9w
664181e1ff7f	andw $0x7fff,%r9w
664181e10080	andw $0x8000,%r9w
664183e180	andw $0xff80,%r9w
664183e1ff	andw $0xffff,%r9w
664181e10000	andw $0x10000,%r9w
664181e1ffff	andw $0x7fffffff,%r9w
664181e10000	andw $0x80000000,%r9w
664183e180	andw $0xffffff80,%r9w
664183e1ff	andw $0xffffffff,%r9w
664181e10000 4:12:sym+0	andw $sym,%r9w
66832300	andw $0,(%rbx)
668324250000000000 4:11:sym+0	andw $0,sym
6683648dfc00	andw $0,-4(%rbp,%rcx,4)
66832301	andw $1,(%rbx)
668324250000000001 4:11:sym+0	andw $1,sym
6683648dfc01	andw $1,-4(%rbp,%rcx,4)
668323ff	andw $-1,(%rbx)
6683242500000000ff 4:11:sym+0	andw $-1,sym
6683648dfcff	andw $-1,-4(%rbp,%rcx,4)
6683237f	andw $127,(%rbx)
66832425000000007f 4:11:sym+0	andw $127,sym
6683648dfc7f	andw $127,-4(%rbp,%rcx,4)
6681238000	andw $128,(%rbx)
66812425000000008000 4:11:sym+0	andw $128,sym
6681648dfc8000	andw $128,-4(%rbp,%rcx,4)
66832380	andw $-128,(%rbx)
668324250000000080 4:11:sym+0	andw $-128,sym
6683648dfc80	andw $-128,-4(%rbp,%rcx,4)
6681237fff	andw $-129,(%rbx)
66812425000000007fff 4:11:sym+0	andw $-129,sym
6681648dfc7fff	andw $-129,-4(%rbp,%rcx,4)
668123ff00	andw $255,(%rbx)
6681242500000000ff00 4:11:sym+0	andw $255,sym
6681648dfcff00	andw $255,-4(%rbp,%rcx,4)
6681230001	andw $256,(%rbx)
66812425000000000001 4:11:sym+0	andw $256,sym
6681648dfc0001	andw $256,-4(%rbp,%rcx,4)
668123ff7f	andw $0x7fff,(%rbx)
6681242500000000ff7f 4:11:sym+0	andw $0x7fff,sym
6681648dfcff7f	andw $0x7fff,-4(%rbp,%rcx,4)
6681230080	andw $0x8000,(%rbx)
66812425000000000080 4:11:sym+0	andw $0x8000,sym
6681648dfc0080	andw $0x8000,-4(%rbp,%rcx,4)
66832380	andw $0xff80,(%rbx)
668324250000000080 4:11:sym+0	andw $0xff80,sym
6683648dfc80	andw $0xff80,-4(%rbp,%rcx,4)
668323ff	andw $0xffff,(%rbx)
6683242500000000ff 4:11:sym+0	andw $0xffff,sym
6683648dfcff	andw $0xffff,-4(%rbp,%rcx,4)
6681230000	andw $0x10000,(%rbx)
66812425000000000000 4:11:sym+0	andw $0x10000,sym
6681648dfc0000	andw $0x10000,-4(%rbp,%rcx,4)
668123ffff	andw $0x7fffffff,(%rbx)
6681242500000000ffff 4:11:sym+0	andw $0x7fffffff,sym
6681648dfcffff	andw $0x7fffffff,-4(%rbp,%rcx,4)
6681230000	andw $0x80000000,(%rbx)
66812425000000000000 4:11:sym+0	andw $0x80000000,sym
6681648dfc0000	andw $0x80000000,-4(%rbp,%rcx,4)
66832380	andw $0xffffff80,(%rbx)
668324250000000080 4:11:sym+0	andw $0xffffff80,sym
6683648dfc80	andw $0xffffff80,-4(%rbp,%rcx,4)
668323ff	andw $0xffffffff,(%rbx)
6683242500000000ff 4:11:sym+0	andw $0xffffffff,sym
6683648dfcff	andw $0xffffffff,-4(%rbp,%rcx,4)
6681230000 3:12:sym+0	andw $sym,(%rbx)
66812425000000000000 4:11:sym+0 8:12:sym+0	andw $sym,sym
6681648dfc0000 5:12:sym+0	andw $sym,-4(%rbp,%rcx,4)
6683e301	and $1,%bx
refused	andw $1,$2
refused	andw %ax,$2
refused	andw (%rbx),(%rbx)
66f0211b	lock andw %bx,(%rbx)
66f0832301	lock andw $1,(%rbx)
refused	lock andw (%rbx),%bx
refused	lock andw %bx,%ax
21c0	andl %eax,%eax
2103	andl %eax,(%rbx)
2303	andl (%rbx),%eax
21042500000000 3:11:sym+0	andl %eax,sym
23042500000000 3:11:sym+0	andl sym,%eax
2103	and %eax,(%rbx)
refused	and 4((%rbx,%rsi,2),%eax
83e000	andl $0,%eax
83e001	andl $1,%eax
83e0ff	andl $-1,%eax
83e07f	andl $127,%eax
2580000000	andl $128,%eax
83e080	andl $-128,%eax
257fffffff	andl $-129,%eax
25ff000000	andl $255,%eax
2500010000	andl $256,%eax
25ff7f0000	andl $0x7fff,%eax
2500800000	andl $0x8000,%eax
2580ff0000	andl $0xff80,%eax
25ffff0000	andl $0xffff,%eax
2500000100	andl $0x10000,%eax
25ffffff7f	andl $0x7fffffff,%eax
2500000080	andl $0x80000000,%eax
83e080	andl $0xffffff80,%eax
83e0ff	andl $0xffffffff,%eax
2500000000 1:10:sym+0	andl $sym,%eax
21d8	andl %ebx,%eax
21c3	andl %eax,%ebx
211b	andl %ebx,(%rbx)
231b	andl (%rbx),%ebx
211c2500000000 3:11:sym+0	andl %ebx,sym
231c2500000000 3:11:sym+0	andl sym,%ebx
211b	and %ebx,(%rbx)
refused	and 4((%rbx,%rsi,2),%ebx
83e300	andl $0,%ebx
83e301	andl $1,%ebx
83e3ff	andl $-1,%ebx
83e37f	andl $127,%ebx
81e380000000	andl $128,%ebx
83e380	andl $-128,%ebx
81e37fffffff	andl $-129,%ebx
81e3ff000000	andl $255,%ebx
81e300010000	andl $256,%ebx
81e3ff7f0000	andl $0x7fff,%ebx
81e300800000	andl $0x8000,%ebx
81e380ff0000	andl $0xff80,%ebx
81e3ffff0000	andl $0xffff,%ebx
81e300000100	andl $0x10000,%ebx
81e3ffffff7f	andl $0x7fffffff,%ebx
81e300000080	andl $0x80000000,%ebx
83e380	andl $0xffffff80,%ebx
83e3ff	andl $0xffffffff,%ebx
81e300000000 2:10:sym+0	andl $sym,%ebx
21e0	andl %esp,%eax
21c4	andl %eax,%esp
2123	andl %esp,(%rbx)
2323	andl (%rbx),%esp
21242500000000 3:11:sym+0	andl %esp,sym
23242500000000 3:11:sym+0	andl sym,%esp
2123	and %esp,(%rbx)
refused	and 4((%rbx,%rsi,2),%esp
83e400	andl $0,%esp
83e401	andl $1,%esp
83e4ff	andl $-1,%esp
83e47f	andl $127,%esp
81e480000000	andl $128,%esp
83e480	andl $-128,%esp
81e47fffffff	andl $-129,%esp
81e4ff000000	andl $255,%esp
81e400010000	andl $256,%esp
81e4ff7f0000	andl $0x7fff,%esp
81e400800000	andl $0x8000,%esp
81e480ff0000	andl $0xff80,%esp
81e4ffff0000	andl $0xffff,%esp
81e400000100	andl $0x10000,%esp
81e4ffffff7f	andl $0x7fffffff,%esp
81e400000080	andl $0x80000000,%esp
83e480	andl $0xffffff80,%esp
83e4ff	andl $0xffffffff,%esp
81e400000000 2:10:sym+0	andl $sym,%esp
21e8	andl %ebp,%eax
21c5	andl %eax,%ebp
212b	andl %ebp,(%rbx)
232b	andl (%rbx),%ebp
212c2500000000 3:11:sym+0	andl %ebp,sym
232c2500000000 3:11:sym+0	andl sym,%ebp
212b	and %ebp,(%rbx)
refused	and 4((%rbx,%rsi,2),%ebp
83e500	andl $0,%ebp
83e501	andl $1,%ebp
83e5ff	andl $-1,%ebp
83e57f	andl $127,%ebp
81e580000000	andl $128,%ebp
83e580	andl $-128,%ebp
81e57fffffff	andl $-129,%ebp
81e5ff000000	andl $255,%ebp
81e500010000	andl $256,%ebp
81e5ff7f0000	andl $0x7fff,%ebp
81e500800000	andl $0x8000,%ebp
81e580ff0000	andl $0xff80,%ebp
81e5ffff0000	andl $0xffff,%ebp
81e500000100	andl $0x10000,%ebp
81e5ffffff7f	andl $0x7fffffff,%ebp
81e500000080	andl $0x80000000,%ebp
83e580	andl $0xffffff80,%ebp
83e5ff	andl $0xffffffff,%ebp
81e500000000 2:10:sym+0	andl $sym,%ebp
4421c8	andl %r9d,%eax
4121c1	andl %eax,%r9d
44210b	andl %r9d,(%rbx)
44230b	andl (%rbx),%r9d
44210c2500000000 4:11:sym+0	andl %r9d,sym
44230c2500000000 4:11:sym+0	andl sym,%r9d
44210b	and %r9d,(%rbx)
refused	and 4((%rbx,%rsi,2),%r9d
4183e100	andl $0,%r9d
4183e101	andl $1,%r9d
4183e1ff	andl $-1,%r9d
4183e17f	andl $127,%r9d
4181e180000000	andl $128,%r9d
4183e180	andl $-128,%r9d
4181e17fffffff	andl $-129,%r9d
4181e1ff000000	andl $255,%r9d
4181e100010000	andl $256,%r9d
4181e1ff7f0000	andl $0x7fff,%r9d
4181e100800000	andl $0x8000,%r9d
4181e180ff0000	andl $0xff80,%r9d
4181e1ffff0000	andl $0xffff,%r9d
4181e100000100	andl $0x10000,%r9d
4181e1ffffff7f	andl $0x7fffffff,%r9d
4181e100000080	andl $0x80000000,%r9d
4183e180	andl $0xffffff80,%r9d
4183e1ff	andl $0xffffffff,%r9d
4181e100000000 3:10:sym+0	andl $sym,%r9d
832300	andl $0,(%rbx)
8324250000000000 3:11:sym+0	andl $0,sym
83648dfc00	andl $0,-4(%rbp,%rcx,4)
832301	andl $1,(%rbx)
8324250000000001 3:11:sym+0	andl $1,sym
83648dfc01	andl $1,-4(%rbp,%rcx,4)
8323ff	andl $-1,(%rbx)
83242500000000ff 3:11:sym+0	andl $-1,sym
83648dfcff	andl $-1,-4(%rbp,%rcx,4)
83237f	andl $127,(%rbx)
832425000000007f 3:11:sym+0	andl $127,sym
83648dfc7f	andl $127,-4(%rbp,%rcx,4)
812380000000	andl $128,(%rbx)
8124250000000080000000 3:11:sym+0	andl $128,sym
81648dfc80000000	andl $128,-4(%rbp,%rcx,4)
832380	andl $-128,(%rbx)
8324250000000080 3:11:sym+0	andl $-128,sym
83648dfc80	andl $-128,-4(%rbp,%rcx,4)
81237fffffff	andl $-129,(%rbx)
812425000000007fffffff 3:11:sym+0	andl $-129,sym
81648dfc7fffffff	andl $-129,-4(%rbp,%rcx,4)
8123ff000000	andl $255,(%rbx)
81242500000000ff000000 3:11:sym+0	andl $255,sym
81648dfcff000000	andl $255,-4(%rbp,%rcx,4)
812300010000	andl $256,(%rbx)
8124250000000000010000 3:11:sym+0	andl $256,sym
81648dfc00010000	andl $256,-4(%rbp,%rcx,4)
8123ff7f0000	andl $0x7fff,(%rbx)
81242500000000ff7f0000 3:11:sym+0	andl $0x7fff,sym
81648dfcff7f0000	andl $0x7fff,-4(%rbp,%rcx,4)
812300800000	andl $0x8000,(%rbx)
8124250000000000800000 3:11:sym+0	andl $0x8000,sym
81648dfc00800000	andl $0x8000,-4(%rbp,%rcx,4)
812380ff0000	andl $0xff80,(%rbx)
8124250000000080ff0000 3:11:sym+0	andl $0xff80,sym
81648dfc80ff0000	andl $0xff80,-4(%rbp,%rcx,4)
8123ffff0000	andl $0xffff,(%rbx)
81242500000000ffff0000 3:11:sym+0	andl $0xffff,sym
81648dfcffff0000	andl $0xffff,-4(%rbp,%rcx,4)
812300000100	andl $0x10000,(%rbx)
8124250000000000000100 3:11:sym+0	andl $0x10000,sym
81648dfc00000100	andl $0x10000,-4(%rbp,%rcx,4)
8123ffffff7f	andl $0x7fffffff,(%rbx)
81242500000000ffffff7f 3:11:sym+0	andl $0x7fffffff,sym
81648dfcffffff7f	andl $0x7fffffff,-4(%rbp,%rcx,4)
812300000080	andl $0x80000000,(%rbx)
8124250000000000000080 3:11:sym+0	andl $0x80000000,sym
81648dfc00000080	andl $0x80000000,-4(%rbp,%rcx,4)
832380	andl $0xffffff80,(%rbx)
8324250000000080 3:11:sym+0	andl $0xffffff80,sym
83648dfc80	andl $0xffffff80,-4(%rbp,%rcx,4)
8323ff	andl $0xffffffff,(%rbx)
83242500000000ff 3:11:sym+0	andl $0xffffffff,sym
83648dfcff	andl $0xffffffff,-4(%rbp,%rcx,4)
812300000000 2:10:sym+0	andl $sym,(%rbx)
8124250000000000000000 3:11:sym+0 7:10:sym+0	andl $sym,sym
81648dfc00000000 4:10:sym+0	andl $sym,-4(%rbp,%rcx,4)
83e301	and $1,%ebx
refused	andl $1,$2
refused	andl %eax,$2
refused	andl (%rbx),(%rbx)
f0211b	lock andl %ebx,(%rbx)
f0832301	lock andl $1,(%rbx)
refused	lock andl (%rbx),%ebx
refused	lock andl %ebx,%eax
4821c0	andq %rax,%rax
482103	andq %rax,(%rbx)
482303	andq (%rbx),%rax
4821042500000000 4:11:sym+0	andq %rax,sym
4823042500000000 4:11:sym+0	andq sym,%rax
482103	and %rax,(%rbx)
refused	and 4((%rbx,%rsi,2),%rax
4883e000	andq $0,%rax
4883e001	andq $1,%rax
4883e0ff	andq $-1,%rax
4883e07f	andq $127,%rax
482580000000	andq $128,%rax
4883e080	andq $-128,%rax
48257fffffff	andq $-129,%rax
4825ff000000	andq $255,%rax
482500010000	andq $256,%rax
4825ff7f0000	andq $0x7fff,%rax
482500800000	andq $0x8000,%rax
482580ff0000	andq $0xff80,%rax
4825ffff0000	andq $0xffff,%rax
482500000100	andq $0x10000,%rax
4825ffffff7f	andq $0x7fffffff,%rax
refused	andq $0x80000000,%rax
refused	andq $0xffffff80,%rax
refused	andq $0xffffffff,%rax
482500000000 2:11:sym+0	andq $sym,%rax
4821d8	andq %rbx,%rax
4821c3	andq %rax,%rbx
48211b	andq %rbx,(%rbx)
48231b	andq (%rbx),%rbx
48211c2500000000 4:11:sym+0	andq %rbx,sym
48231c2500000000 4:11:sym+0	andq sym,%rbx
48211b	and %rbx,(%rbx)
refused	and 4((%rbx,%rsi,2),%rbx
4883e300	andq $0,%rbx
4883e301	andq $1,%rbx
4883e3ff	andq $-1,%rbx
4883e37f	andq $127,%rbx
4881e380000000	andq $128,%rbx
4883e380	andq $-128,%rbx
4881e37fffffff	andq $-129,%rbx
4881e3ff000000	andq $255,%rbx
4881e300010000	andq $256,%rbx
4881e3ff7f0000	andq $0x7fff,%rbx
4881e300800000	andq $0x8000,%rbx
4881e380ff0000	andq $0xff80,%rbx
4881e3ffff0000	andq $0xffff,%rbx
4881e300000100	andq $0x10000,%rbx
4881e3ffffff7f	andq $0x7fffffff,%rbx
refused	andq $0x80000000,%rbx
refused	andq $0xffffff80,%rbx
refused	andq $0xffffffff,%rbx
4881e300000000 3:11:sym+0	andq $sym,%rbx
4821e0	andq %rsp,%rax
4821c4	andq %rax,%rsp
482123	andq %rsp,(%rbx)
482323	andq (%rbx),%rsp
4821242500000000 4:11:sym+0	andq %rsp,sym
4823242500000000 4:11:sym+0	andq sym,%rsp
482123	and %rsp,(%rbx)
refused	and 4((%rbx,%rsi,2),%rsp
4883e47f	andq $127,%rsp
4881e480000000	andq $128,%rsp
4883e480	andq $-128,%rsp
4881e47fffffff	andq $-129,%rsp
4881e4ff000000	andq $255,%rsp
4881e400010000	andq $256,%rsp
4881e4ff7f0000	andq $0x7fff,%rsp
4881e400800000	andq $0x8000,%rsp
4881e480ff0000	andq $0xff80,%rsp
4881e4ffff0000	andq $0xffff,%rsp
4881e400000100	andq $0x10000,%rsp
refused	andq $0xffffff80,%rsp
4881e400000000 3:11:sym+0	andq $sym,%rsp
4c21e0	andq %r12,%rax
4921c4	andq %rax,%r12
4c2123	andq %r12,(%rbx)
4c2323	andq (%rbx),%r12
4c21242500000000 4:11:sym+0	andq %r12,sym
4c23242500000000 4:11:sym+0	andq sym,%r12
4c2123	and %r12,(%rbx)
refused	and 4((%rbx,%rsi,2),%r12
4983e400	andq $0,%r12
4983e401	andq $1,%r12
4983e4ff	andq $-1,%r12
4983e47f	andq $127,%r12
4981e480000000	andq $128,%r12
4983e480	andq $-128,%r12
4981e47fffffff	andq $-129,%r12
4981e4ff000000	andq $255,%r12
4981e400010000	andq $256,%r12
4981e4ff7f0000	andq $0x7fff,%r12
4981e400800000	andq $0x8000,%r12
4981e480ff0000	andq $0xff80,%r12
4981e4ffff0000	andq $0xffff,%r12
4981e400000100	andq $0x10000,%r12
4981e4ffffff7f	andq $0x7fffffff,%r12
refused	andq $0x80000000,%r12
refused	andq $0xffffff80,%r12
refused	andq $0xffffffff,%r12
4981e400000000 3:11:sym+0	andq $sym,%r12
4c21e8	andq %r13,%rax
4921c5	andq %rax,%r13
4c212b	andq %r13,(%rbx)
4c232b	andq (%rbx),%r13
4c212c2500000000 4:11:sym+0	andq %r13,sym
4c232c2500000000 4:11:sym+0	andq sym,%r13
4c212b	and %r13,(%rbx)
refused	and 4((%rbx,%rsi,2),%r13
4983e500	andq $0,%r13
4983e501	andq $1,%r13
4983e5ff	andq $-1,%r13
4983e57f	andq $127,%r13
4981e580000000	andq $128,%r13
4983e580	andq $-128,%r13
4981e57fffffff	andq $-129,%r13
4981e5ff000000	andq $255,%r13
4981e500010000	andq $256,%r13
4981e5ff7f0000	andq $0x7fff,%r13
4981e500800000	andq $0x8000,%r13
4981e580ff0000	andq $0xff80,%r13
4981e5ffff0000	andq $0xffff,%r13
4981e500000100	andq $0x10000,%r13
4981e5ffffff7f	andq $0x7fffffff,%r13
refused	andq $0x80000000,%r13
refused	andq $0xffffff80,%r13
refused	andq $0xffffffff,%r13
4981e500000000 3:11:sym+0	andq $sym,%r13
4c21c8	andq %r9,%rax
4921c1	andq %rax,%r9
4c210b	andq %r9,(%rbx)
4c230b	andq (%rbx),%r9
4c210c2500000000 4:11:sym+0	andq %r9,sym
4c230c2500000000 4:11:sym+0	andq sym,%r9
4c210b	and %r9,(%rbx)
refused	and 4((%rbx,%rsi,2),%r9
4983e100	andq $0,%r9
4983e101	andq $1,%r9
4983e1ff	andq $-1,%r9
4983e17f	andq $127,%r9
4981e180000000	andq $128,%r9
4983e180	andq $-128,%r9
4981e17fffffff	andq $-129,%r9
4981e1ff000000	andq $255,%r9
4981e100010000	andq $256,%r9
4981e1ff7f0000	andq $0x7fff,%r9
4981e100800000	andq $0x8000,%r9
4981e180ff0000	andq $0xff80,%r9
4981e1ffff0000	andq $0xffff,%r9
4981e100000100	andq $0x10000,%r9
4981e1ffffff7f	andq $0x7fffffff,%r9
refused	andq $0x80000000,%r9
refused	andq $0xffffff80,%r9
refused	andq $0xffffffff,%r9
4981e100000000 3:11:sym+0	andq $sym,%r9
48832300	andq $0,(%rbx)
488324250000000000 4:11:sym+0	andq $0,sym
4883648dfc00	andq $0,-4(%rbp,%rcx,4)
48832301	andq $1,(%rbx)
488324250000000001 4:11:sym+0	andq $1,sym
4883648dfc01	andq $1,-4(%rbp,%rcx,4)
488323ff	andq $-1,(%rbx)
4883242500000000ff 4:11:sym+0	andq $-1,sym
4883648dfcff	andq $-1,-4(%rbp,%rcx,4)
4883237f	andq $127,(%rbx)
48832425000000007f 4:11:sym+0	andq $127,sym
4883648dfc7f	andq $127,-4(%rbp,%rcx,4)
48812380000000	andq $128,(%rbx)
488124250000000080000000 4:11:sym+0	andq $128,sym
4881648dfc80000000	andq $128,-4(%rbp,%rcx,4)
48832380	andq $-128,(%rbx)
488324250000000080 4:11:sym+0	andq $-128,sym
4883648dfc80	andq $-128,-4(%rbp,%rcx,4)
4881237fffffff	andq $-129,(%rbx)
48812425000000007fffffff 4:11:sym+0	andq $-129,sym
4881648dfc7fffffff	andq $-129,-4(%rbp,%rcx,4)
488123ff000000	andq $255,(%rbx)
4881242500000000ff000000 4:11:sym+0	andq $255,sym
4881648dfcff000000	andq $255,-4(%rbp,%rcx,4)
48812300010000	andq $256,(%rbx)
488124250000000000010000 4:11:sym+0	andq $256,sym
4881648dfc00010000	andq $256,-4(%rbp,%rcx,4)
488123ff7f0000	andq $0x7fff,(%rbx)
4881242500000000ff7f0000 4:11:sym+0	andq $0x7fff,sym
4881648dfcff7f0000	andq $0x7fff,-4(%rbp,%rcx,4)
48812300800000	andq $0x8000,(%rbx)
488124250000000000800000 4:11:sym+0	andq $0x8000,sym
4881648dfc00800000	andq $0x8000,-4(%rbp,%rcx,4)
48812380ff0000	andq $0xff80,(%rbx)
488124250000000080ff0000 4:11:sym+0	andq $0xff80,sym
4881648dfc80ff0000	andq $0xff80,-4(%rbp,%rcx,4)
488123ffff0000	andq $0xffff,(%rbx)
4881242500000000ffff0000 4:11:sym+0	andq $0xffff,sym
4881648dfcffff0000	andq $0xffff,-4(%rbp,%rcx,4)
48812300000100	andq $0x10000,(%rbx)
488124250000000000000100 4:11:sym+0	andq $0x10000,sym
4881648dfc00000100	andq $0x10000,-4(%rbp,%rcx,4)
488123ffffff7f	andq $0x7fffffff,(%rbx)
4881242500000000ffffff7f 4:11:sym+0	andq $0x7fffffff,sym
4881648dfcffffff7f	andq $0x7fffffff,-4(%rbp,%rcx,4)
refused	andq $0x80000000,(%rbx)
refused	andq $0x80000000,sym
refused	andq $0x80000000,-4(%rbp,%rcx,4)
refused	andq $0xffffff80,(%rbx)
refused	andq $0xffffff80,sym
refused	andq $0xffffff80,-4(%rbp,%rcx,4)
refused	andq $0xffffffff,(%rbx)
refused	andq $0xffffffff,sym
refused	andq $0xffffffff,-4(%rbp,%rcx,4)
48812300000000 3:11:sym+0	andq $sym,(%rbx)
488124250000000000000000 4:11:sym+0 8:11:sym+0	andq $sym,sym
4881648dfc00000000 5:11:sym+0	andq $sym,-4(%rbp,%rcx,4)
4883e301	and $1,%rbx
refused	andq $1,$2
refused	andq %rax,$2
refused	andq (%rbx),(%rbx)
f048211b	lock andq %rbx,(%rbx)
f048832301	lock andq $1,(%rbx)
refused	lock andq (%rbx),%rbx
refused	lock andq %rbx,%rax
28c0	subb %al,%al
2803	subb %al,(%rbx)
2a03	subb (%rbx),%al
28042500000000 3:11:sym+0	subb %al,sym
2a042500000000 3:11:sym+0	subb sym,%al
2803	sub %al,(%rbx)
refused	sub 4((%rbx,%rsi,2),%al
2c00	subb $0,%al
2c01	subb $1,%al
2cff	subb $-1,%al
2c7f	subb $127,%al
2c80	subb $128,%al
2c80	subb $-128,%al
2c7f	subb $-129,%al
2cff	subb $255,%al
2c00	subb $256,%al
2cff	subb $0x7fff,%al
2c00	subb $0x8000,%al
2c80	subb $0xff80,%al
2cff	subb $0xffff,%al
2c00	subb $0x10000,%al
2cff	subb $0x7fffffff,%al
2c00	subb $0x80000000,%al
2c80	subb $0xffffff80,%al
2cff	subb $0xffffffff,%al
2c00 1:14:sym+0	subb $sym,%al
28c8	subb %cl,%al
28c1	subb %al,%cl
280b	subb %cl,(%rbx)
2a0b	subb (%rbx),%cl
280c2500000000 3:11:sym+0	subb %cl,sym
2a0c2500000000 3:11:sym+0	subb sym,%cl
280b	sub %cl,(%rbx)
refused	sub 4((%rbx,%rsi,2),%cl
80e900	subb $0,%cl
80e901	subb $1,%cl
80e9ff	subb $-1,%cl
80e97f	subb $127,%cl
80e980	subb $128,%cl
80e980	subb $-128,%cl
80e97f	subb $-129,%cl
80e9ff	subb $255,%cl
80e900	subb $256,%cl
80e9ff	subb $0x7fff,%cl
80e900	subb $0x8000,%cl
80e980	subb $0xff80,%cl
80e9ff	subb $0xffff,%cl
80e900	subb $0x10000,%cl
80e9ff	subb $0x7fffffff,%cl
80e900	subb $0x80000000,%cl
80e980	subb $0xffffff80,%cl
80e9ff	subb $0xffffffff,%cl
80e900 2:14:sym+0	subb $sym,%cl
28f8	subb %bh,%al
28c7	subb %al,%bh
283b	subb %bh,(%rbx)
2a3b	subb (%rbx),%bh
283c2500000000 3:11:sym+0	subb %bh,sym
2a3c2500000000 3:11:sym+0	subb sym,%bh
283b	sub %bh,(%rbx)
refused	sub 4((%rbx,%rsi,2),%bh
80ef00	subb $0,%bh
80ef01	subb $1,%bh
80efff	subb $-1,%bh
80ef7f	subb $127,%bh
80ef80	subb $128,%bh
80ef80	subb $-128,%bh
80ef7f	subb $-129,%bh
80efff	subb $255,%bh
80ef00	subb $256,%bh
80efff	subb $0x7fff,%bh
80ef00	subb $0x8000,%bh
80ef80	subb $0xff80,%bh
80efff	subb $0xffff,%bh
80ef00	subb $0x10000,%bh
80efff	subb $0x7fffffff,%bh
80ef00	subb $0x80000000,%bh
80ef80	subb $0xffffff80,%bh
80efff	subb $0xffffffff,%bh
80ef00 2:14:sym+0	subb $sym,%bh
4028f0	subb %sil,%al
4028c6	subb %al,%sil
402833	subb %sil,(%rbx)
402a33	subb (%rbx),%sil
4028342500000000 4:11:sym+0	subb %sil,sym
402a342500000000 4:11:sym+0	subb sym,%sil
402833	sub %sil,(%rbx)
refused	sub 4((%rbx,%rsi,2),%sil
4080ee00	subb $0,%sil
4080ee01	subb $1,%sil
4080eeff	subb $-1,%sil
4080ee7f	subb $127,%sil
4080ee80	subb $128,%sil
4080ee80	subb $-128,%sil
4080ee7f	subb $-129,%sil
4080eeff	subb $255,%sil
4080ee00	subb $256,%sil
4080eeff	subb $0x7fff,%sil
4080ee00	subb $0x8000,%sil
4080ee80	subb $0xff80,%sil
4080eeff	subb $0xffff,%sil
4080ee00	subb $0x10000,%sil
4080eeff	subb $0x7fffffff,%sil
4080ee00	subb $0x80000000,%sil
4080ee80	subb $0xffffff80,%sil
4080eeff	subb $0xffffffff,%sil
4080ee00 3:14:sym+0	subb $sym,%sil
4428c8	subb %r9b,%al
4128c1	subb %al,%r9b
44280b	subb %r9b,(%rbx)
442a0b	subb (%rbx),%r9b
44280c2500000000 4:11:sym+0	subb %r9b,sym
442a0c2500000000 4:11:sym+0	subb sym,%r9b
44280b	sub %r9b,(%rbx)
refused	sub 4((%rbx,%rsi,2),%r9b
4180e900	subb $0,%r9b
4180e901	subb $1,%r9b
4180e9ff	subb $-1,%r9b
4180e97f	subb $127,%r9b
4180e980	subb $128,%r9b
4180e980	subb $-128,%r9b
4180e97f	subb $-129,%r9b
4180e9ff	subb $255,%r9b
4180e900	subb $256,%r9b
4180e9ff	subb $0x7fff,%r9b
4180e900	subb $0x8000,%r9b
4180e980	subb $0xff80,%r9b
4180e9ff	subb $0xffff,%r9b
4180e900	subb $0x10000,%r9b
4180e9ff	subb $0x7fffffff,%r9b
4180e900	subb $0x80000000,%r9b
4180e980	subb $0xffffff80,%r9b
4180e9ff	subb $0xffffffff,%r9b
4180e900 3:14:sym+0	subb $sym,%r9b
802b00	subb $0,(%rbx)
802c250000000000 3:11:sym+0	subb $0,sym
806c8dfc00	subb $0,-4(%rbp,%rcx,4)
802b01	subb $1,(%rbx)
802c250000000001 3:11:sym+0	subb $1,sym
806c8dfc01	subb $1,-4(%rbp,%rcx,4)
802bff	subb $-1,(%rbx)
802c2500000000ff 3:11:sym+0	subb $-1,sym
806c8dfcff	subb $-1,-4(%rbp,%rcx,4)
802b7f	subb $127,(%rbx)
802c25000000007f 3:11:sym+0	subb $127,sym
806c8dfc7f	subb $127,-4(%rbp,%rcx,4)
802b80	subb $128,(%rbx)
802c250000000080 3:11:sym+0	subb $128,sym
806c8dfc80	subb $128,-4(%rbp,%rcx,4)
802b80	subb $-128,(%rbx)
802c250000000080 3:11:sym+0	subb $-128,sym
806c8dfc80	subb $-128,-4(%rbp,%rcx,4)
802b7f	subb $-129,(%rbx)
802c25000000007f 3:11:sym+0	subb $-129,sym
806c8dfc7f	subb $-129,-4(%rbp,%rcx,4)
802bff	subb $255,(%rbx)
802c2500000000ff 3:11:sym+0	subb $255,sym
806c8dfcff	subb $255,-4(%rbp,%rcx,4)
802b00	subb $256,(%rbx)
802c250000000000 3:11:sym+0	subb $256,sym
806c8dfc00	subb $256,-4(%rbp,%rcx,4)
802bff	subb $0x7fff,(%rbx)
802c2500000000ff 3:11:sym+0	subb $0x7fff,sym
806c8dfcff	subb $0x7fff,-4(%rbp,%rcx,4)
802b00	subb $0x8000,(%rbx)
802c250000000000 3:11:sym+0	subb $0x8000,sym
806c8dfc00	subb $0x8000,-4(%rbp,%rcx,4)
802b80	subb $0xff80,(%rbx)
802c250000000080 3:11:sym+0	subb $0xff80,sym
806c8dfc80	subb $0xff80,-4(%rbp,%rcx,4)
802bff	subb $0xffff,(%rbx)
802c2500000000ff 3:11:sym+0	subb $0xffff,sym
806c8dfcff	subb $0xffff,-4(%rbp,%rcx,4)
802b00	subb $0x10000,(%rbx)
802c250000000000 3:11:sym+0	subb $0x10000,sym
806c8dfc00	subb $0x10000,-4(%rbp,%rcx,4)
802bff	subb $0x7fffffff,(%rbx)
802c2500000000ff 3:11:sym+0	subb $0x7fffffff,sym
806c8dfcff	subb $0x7fffffff,-4(%rbp,%rcx,4)
802b00	subb $0x80000000,(%rbx)
802c250000000000 3:11:sym+0	subb $0x80000000,sym
806c8dfc00	subb $0x80000000,-4(%rbp,%rcx,4)
802b80	subb $0xffffff80,(%rbx)
802c250000000080 3:11:sym+0	subb $0xffffff80,sym
806c8dfc80	subb $0xffffff80,-4(%rbp,%rcx,4)
802bff	subb $0xffffffff,(%rbx)
802c2500000000ff 3:11:sym+0	subb $0xffffffff,sym
806c8dfcff	subb $0xffffffff,-4(%rbp,%rcx,4)
802b00 2:14:sym+0	subb $sym,(%rbx)
802c250000000000 3:11:sym+0 7:14:sym+0	subb $sym,sym
806c8dfc00 4:14:sym+0	subb $sym,-4(%rbp,%rcx,4)
80e901	sub $1,%cl
832b01	sub $1,(%rbx)
refused	subb $1,$2
refused	subb %al,$2
refused	subb (%rbx),(%rbx)
f0280b	lock subb %cl,(%rbx)
f0802b01	lock subb $1,(%rbx)
refused	lock subb (%rbx),%cl
refused	lock subb %cl,%al
6629c0	subw %ax,%ax
662903	subw %ax,(%rbx)
662b03	subw (%rbx),%ax
6629042500000000 4:11:sym+0	subw %ax,sym
662b042500000000 4:11:sym+0	subw sym,%ax
662903	sub %ax,(%rbx)
refused	sub 4((%rbx,%rsi,2),%ax
6683e800	subw $0,%ax
6683e801	subw $1,%ax
6683e8ff	subw $-1,%ax
6683e87f	subw $127,%ax
662d8000	subw $128,%ax
6683e880	subw $-128,%ax
662d7fff	subw $-129,%ax
662dff00	subw $255,%ax
662d0001	subw $256,%ax
662dff7f	subw $0x7fff,%ax
662d0080	subw $0x8000,%ax
6683e880	subw $0xff80,%ax
6683e8ff	subw $0xffff,%ax
662d0000	subw $0x10000,%ax
662dffff	subw $0x7fffffff,%ax
662d0000	subw $0x80000000,%ax
6683e880	subw $0xffffff80,%ax
6683e8ff	subw $0xffffffff,%ax
662d0000 2:12:sym+0	subw $sym,%ax
6629d8	subw %bx,%ax
6629c3	subw %ax,%bx
66291b	subw %bx,(%rbx)
662b1b	subw (%rbx),%bx
66291c2500000000 4:11:sym+0	subw %bx,sym
662b1c2500000000 4:11:sym+0	subw sym,%bx
66291b	sub %bx,(%rbx)
refused	sub 4((%rbx,%rsi,2),%bx
6683eb00	subw $0,%bx
6683eb01	subw $1,%bx
6683ebff	subw $-1,%bx
6683eb7f	subw $127,%bx
6681eb8000	subw $128,%bx
6683eb80	subw $-128,%bx
6681eb7fff	subw $-129,%bx
6681ebff00	subw $255,%bx
6681eb0001	subw $256,%bx
6681ebff7f	subw $0x7fff,%bx
6681eb0080	subw $0x8000,%bx
6683eb80	subw $0xff80,%bx
6683ebff	subw $0xffff,%bx
6681eb0000	subw $0x10000,%bx
6681ebffff	subw $0x7fffffff,%bx
6681eb0000	subw $0x80000000,%bx
6683eb80	subw $0xffffff80,%bx
6683ebff	subw $0xffffffff,%bx
6681eb0000 3:12:sym+0	subw $sym,%bx
6629f0	subw %si,%ax
6629c6	subw %ax,%si
662933	subw %si,(%rbx)
662b33	subw (%rbx),%si
6629342500000000 4:11:sym+0	subw %si,sym
662b342500000000 4:11:sym+0	subw sym,%si
662933	sub %si,(%rbx)
refused	sub 4((%rbx,%rsi,2),%si
6683ee00	subw $0,%si
6683ee01	subw $1,%si
6683eeff	subw $-1,%si
6683ee7f	subw $127,%si
6681ee8000	subw $128,%si
6683ee80	subw $-128,%si
6681ee7fff	subw $-129,%si
6681eeff00	subw $255,%si
6681ee0001	subw $256,%si
6681eeff7f	subw $0x7fff,%si
6681ee0080	subw $0x8000,%si
6683ee80	subw $0xff80,%si
6683eeff	subw $0xffff,%si
6681ee0000	subw $0x10000,%si
6681eeffff	subw $0x7fffffff,%si
6681ee0000	subw $0x80000000,%si
6683ee80	subw $0xffffff80,%si
6683eeff	subw $0xffffffff,%si
6681ee0000 3:12:sym+0	subw $sym,%si
664429c8	subw %r9w,%ax
664129c1	subw %ax,%r9w
6644290b	subw %r9w,(%rbx)
66442b0b	subw (%rbx),%r9w
6644290c2500000000 5:11:sym+0	subw %r9w,sym
66442b0c2500000000 5:11:sym+0	subw sym,%r9w
6644290b	sub %r9w,(%rbx)
refused	sub 4((%rbx,%rsi,2),%r9w
664183e900	subw $0,%r9w
664183e901	subw $1,%r9w
664183e9ff	subw $-1,%r9w
664183e97f	subw $127,%r9w
664181e98000	subw $128,%r9w
664183e980	subw $-128,%r9w
664181e97fff	subw $-129,%r9w
664181e9ff00	subw $255,%r9w
664181e90001	subw $256,%r9w
664181e9ff7f	subw $0x7fff,%r9w
664181e90080	subw $0x8000,%r9w
664183e980	subw $0xff80,%r9w
664183e9ff	subw $0xffff,%r9w
664181e90000	subw $0x10000,%r9w
664181e9ffff	subw $0x7fffffff,%r9w
664181e90000	subw $0x80000000,%r9w
664183e980	subw $0xffffff80,%r9w
664183e9ff	subw $0xffffffff,%r9w
664181e90000 4:12:sym+0	subw $sym,%r9w
66832b00	subw $0,(%rbx)
66832c250000000000 4:11:sym+0	subw $0,sym
66836c8dfc00	subw $0,-4(%rbp,%rcx,4)
66832b01	subw $1,(%rbx)
66832c250000000001 4:11:sym+0	subw $1,sym
66836c8dfc01	subw $1,-4(%rbp,%rcx,4)
66832bff	subw $-1,(%rbx)
66832c2500000000ff 4:11:sym+0	subw $-1,sym
66836c8dfcff	subw $-1,-4(%rbp,%rcx,4)
66832b7f	subw $127,(%rbx)
66832c25000000007f 4:11:sym+0	subw $127,sym
66836c8dfc7f	subw $127,-4(%rbp,%rcx,4)
66812b8000	subw $128,(%rbx)
66812c25000000008000 4:11:sym+0	subw $128,sym
66816c8dfc8000	subw $128,-4(%rbp,%rcx,4)
66832b80	subw $-128,(%rbx)
66832c250000000080 4:11:sym+0	subw $-128,sym
66836c8dfc80	subw $-128,-4(%rbp,%rcx,4)
66812b7fff	subw $-129,(%rbx)
66812c25000000007fff 4:11:sym+0	subw $-129,sym
66816c8dfc7fff	subw $-129,-4(%rbp,%rcx,4)
66812bff00	subw $255,(%rbx)
66812c2500000000ff00 4:11:sym+0	subw $255,sym
66816c8dfcff00	subw $255,-4(%rbp,%rcx,4)
66812b0001	subw $256,(%rbx)
66812c25000000000001 4:11:sym+0	subw $256,sym
66816c8dfc0001	subw $256,-4(%rbp,%rcx,4)
66812bff7f	subw $0x7fff,(%rbx)
66812c2500000000ff7f 4:11:sym+0	subw $0x7fff,sym
66816c8dfcff7f	subw $0x7fff,-4(%rbp,%rcx,4)
66812b0080	subw $0x8000,(%rbx)
66812c25000000000080 4:11:sym+0	subw $0x8000,sym
66816c8dfc0080	subw $0x8000,-4(%rbp,%rcx,4)
66832b80	subw $0xff80,(%rbx)
66832c250000000080 4:11:sym+0	subw $0xff80,sym
66836c8dfc80	subw $0xff80,-4(%rbp,%rcx,4)
66832bff	subw $0xffff,(%rbx)
66832c2500000000ff 4:11:sym+0	subw $0xffff,sym
66836c8dfcff	subw $0xffff,-4(%rbp,%rcx,4)
66812b0000	subw $0x10000,(%rbx)
66812c25000000000000 4:11:sym+0	subw $0x10000,sym
66816c8dfc0000	subw $0x10000,-4(%rbp,%rcx,4)
66812bffff	subw $0x7fffffff,(%rbx)
66812c2500000000ffff 4:11:sym+0	subw $0x7fffffff,sym
66816c8dfcffff	subw $0x7fffffff,-4(%rbp,%rcx,4)
66812b0000	subw $0x80000000,(%rbx)
66812c25000000000000 4:11:sym+0	subw $0x80000000,sym
66816c8dfc0000	subw $0x80000000,-4(%rbp,%rcx,4)
66832b80	subw $0xffffff80,(%rbx)
66832c250000000080 4:11:sym+0	subw $0xffffff80,sym
66836c8dfc80	subw $0xffffff80,-4(%rbp,%rcx,4)
66832bff	subw $0xffffffff,(%rbx)
66832c2500000000ff 4:11:sym+0	subw $0xffffffff,sym
66836c8dfcff	subw $0xffffffff,-4(%rbp,%rcx,4)
66812b0000 3:12:sym+0	subw $sym,(%rbx)
66812c25000000000000 4:11:sym+0 8:12:sym+0	subw $sym,sym
66816c8dfc0000 5:12:sym+0	subw $sym,-4(%rbp,%rcx,4)
6683eb01	sub $1,%bx
refused	subw $1,$2
refused	subw %ax,$2
refused	subw (%rbx),(%rbx)
66f0291b	lock subw %bx,(%rbx)
66f0832b01	lock subw $1,(%rbx)
refused	lock subw (%rbx),%bx
refused	lock subw %bx,%ax
29c0	subl %eax,%eax
2903	subl %eax,(%rbx)
2b03	subl (%rbx),%eax
29042500000000 3:11:sym+0	subl %eax,sym
2b042500000000 3:11:sym+0	subl sym,%eax
2903	sub %eax,(%rbx)
refused	sub 4((%rbx,%rsi,2),%eax
83e800	subl $0,%eax
83e801	subl $1,%eax
83e8ff	subl $-1,%eax
83e87f	subl $127,%eax
2d80000000	subl $128,%eax
83e880	subl $-128,%eax
2d7fffffff	subl $-129,%eax
2dff000000	subl $255,%eax
2d00010000	subl $256,%eax
2dff7f0000	subl $0x7fff,%eax
2d00800000	subl $0x8000,%eax
2d80ff0000	subl $0xff80,%eax
2dffff0000	subl $0xffff,%eax
2d00000100	subl $0x10000,%eax
2dffffff7f	subl $0x7fffffff,%eax
2d00000080	subl $0x80000000,%eax
83e880	subl $0xffffff80,%eax
83e8ff	subl $0xffffffff,%eax
2d00000000 1:10:sym+0	subl $sym,%eax
29d8	subl %ebx,%eax
29c3	subl %eax,%ebx
291b	subl %ebx,(%rbx)
2b1b	subl (%rbx),%ebx
291c2500000000 3:11:sym+0	subl %ebx,sym
2b1c2500000000 3:11:sym+0	subl sym,%ebx
291b	sub %ebx,(%rbx)
refused	sub 4((%rbx,%rsi,2),%ebx
83eb00	subl $0,%ebx
83eb01	subl $1,%ebx
83ebff	subl $-1,%ebx
83eb7f	subl $127,%ebx
81eb80000000	subl $128,%ebx
83eb80	subl $-128,%ebx
81eb7fffffff	subl $-129,%ebx
81ebff000000	subl $255,%ebx
81eb00010000	subl $256,%ebx
81ebff7f0000	subl $0x7fff,%ebx
81eb00800000	subl $0x8000,%ebx
81eb80ff0000	subl $0xff80,%ebx
81ebffff0000	subl $0xffff,%ebx
81eb00000100	subl $0x10000,%ebx
81ebffffff7f	subl $0x7fffffff,%ebx
81eb00000080	subl $0x80000000,%ebx
83eb80	subl $0xffffff80,%ebx
83ebff	subl $0xffffffff,%ebx
81eb00000000 2:10:sym+0	subl $sym,%ebx
29e0	subl %esp,%eax
29c4	subl %eax,%esp
2923	subl %esp,(%rbx)
2b23	subl (%rbx),%esp
29242500000000 3:11:sym+0	subl %esp,sym
2b242500000000 3:11:sym+0	subl sym,%esp
2923	sub %esp,(%rbx)
refused	sub 4((%rbx,%rsi,2),%esp
83ec00	subl $0,%esp
83ec01	subl $1,%esp
83ecff	subl $-1,%esp
83ec7f	subl $127,%esp
81ec80000000	subl $128,%esp
83ec80	subl $-128,%esp
81ec7fffffff	subl $-129,%esp
81ecff000000	subl $255,%esp
81ec00010000	subl $256,%esp
81ecff7f0000	subl $0x7fff,%esp
81ec00800000	subl $0x8000,%esp
81ec80ff0000	subl $0xff80,%esp
81ecffff0000	subl $0xffff,%esp
81ec00000100	subl $0x10000,%esp
81ecffffff7f	subl $0x7fffffff,%esp
81ec00000080	subl $0x80000000,%esp
83ec80	subl $0xffffff80,%esp
83ecff	subl $0xffffffff,%esp
81ec00000000 2:10:sym+0	subl $sym,%esp
29e8	subl %ebp,%eax
29c5	subl %eax,%ebp
292b	subl %ebp,(%rbx)
2b2b	subl (%rbx),%ebp
292c2500000000 3:11:sym+0	subl %ebp,sym
2b2c2500000000 3:11:sym+0	subl sym,%ebp
292b	sub %ebp,(%rbx)
refused	sub 4((%rbx,%rsi,2),%ebp
83ed00	subl $0,%ebp
83ed01	subl $1,%ebp
83edff	subl $-1,%ebp
83ed7f	subl $127,%ebp
81ed80000000	subl $128,%ebp
83ed80	subl $-128,%ebp
81ed7fffffff	subl $-129,%ebp
81edff000000	subl $255,%ebp
81ed00010000	subl $256,%ebp
81edff7f0000	subl $0x7fff,%ebp
81ed00800000	subl $0x8000,%ebp
81ed80ff0000	subl $0xff80,%ebp
81edffff0000	subl $0xffff,%ebp
81ed00000100	subl $0x10000,%ebp
81edffffff7f	subl $0x7fffffff,%ebp
81ed00000080	subl $0x80000000,%ebp
83ed80	subl $0xffffff80,%ebp
83edff	subl $0xffffffff,%ebp
81ed00000000 2:10:sym+0	subl $sym,%ebp
4429c8	subl %r9d,%eax
4129c1	subl %eax,%r9d
44290b	subl %r9d,(%rbx)
442b0b	subl (%rbx),%r9d
44290c2500000000 4:11:sym+0	subl %r9d,sym
442b0c2500000000 4:11:sym+0	subl sym,%r9d
44290b	sub %r9d,(%rbx)
refused	sub 4((%rbx,%rsi,2),%r9d
4183e900	subl $0,%r9d
4183e901	subl $1,%r9d
4183e9ff	subl $-1,%r9d
4183e97f	subl $127,%r9d
4181e980000000	subl $128,%r9d
4183e980	subl $-128,%r9d
4181e97fffffff	subl $-129,%r9d
4181e9ff000000	subl $255,%r9d
4181e900010000	subl $256,%r9d
4181e9ff7f0000	subl $0x7fff,%r9d
4181e900800000	subl $0x8000,%r9d
4181e980ff0000	subl $0xff80,%r9d
4181e9ffff0000	subl $0xffff,%r9d
4181e900000100	subl $0x10000,%r9d
4181e9ffffff7f	subl $0x7fffffff,%r9d
4181e900000080	subl $0x80000000,%r9d
4183e980	subl $0xffffff80,%r9d
4183e9ff	subl $0xffffffff,%r9d
4181e900000000 3:10:sym+0	subl $sym,%r9d
832b00	subl $0,(%rbx)
832c250000000000 3:11:sym+0	subl $0,sym
836c8dfc00	subl $0,-4(%rbp,%rcx,4)
832b01	subl $1,(%rbx)
832c250000000001 3:11:sym+0	subl $1,sym
836c8dfc01	subl $1,-4(%rbp,%rcx,4)
832bff	subl $-1,(%rbx)
832c2500000000ff 3:11:sym+0	subl $-1,sym
836c8dfcff	subl $-1,-4(%rbp,%rcx,4)
832b7f	subl $127,(%rbx)
832c25000000007f 3:11:sym+0	subl $127,sym
836c8dfc7f	subl $127,-4(%rbp,%rcx,4)
812b80000000	subl $128,(%rbx)
812c250000000080000000 3:11:sym+0	subl $128,sym
816c8dfc80000000	subl $128,-4(%rbp,%rcx,4)
832b80	subl $-128,(%rbx)
832c250000000080 3:11:sym+0	subl $-128,sym
836c8dfc80	subl $-128,-4(%rbp,%rcx,4)
812b7fffffff	subl $-129,(%rbx)
812c25000000007fffffff 3:11:sym+0	subl $-129,sym
816c8dfc7fffffff	subl $-129,-4(%rbp,%rcx,4)
812bff000000	subl $255,(%rbx)
812c2500000000ff000000 3:11:sym+0	subl $255,sym
816c8dfcff000000	subl $255,-4(%rbp,%rcx,4)
812b00010000	subl $256,(%rbx)
812c250000000000010000 3:11:sym+0	subl $256,sym
816c8dfc00010000	subl $256,-4(%rbp,%rcx,4)
812bff7f0000	subl $0x7fff,(%rbx)
812c2500000000ff7f0000 3:11:sym+0	subl $0x7fff,sym
816c8dfcff7f0000	subl $0x7fff,-4(%rbp,%rcx,4)
812b00800000	subl $0x8000,(%rbx)
812c250000000000800000 3:11:sym+0	subl $0x8000,sym
816c8dfc00800000	subl $0x8000,-4(%rbp,%rcx,4)
812b80ff0000	subl $0xff80,(%rbx)
812c250000000080ff0000 3:11:sym+0	subl $0xff80,sym
816c8dfc80ff0000	subl $0xff80,-4(%rbp,%rcx,4)
812bffff0000	subl $0xffff,(%rbx)
812c2500000000ffff0000 3:11:sym+0	subl $0xffff,sym
816c8dfcffff0000	subl $0xffff,-4(%rbp,%rcx,4)
812b00000100	subl $0x10000,(%rbx)
812c250000000000000100 3:11:sym+0	subl $0x10000,sym
816c8dfc00000100	subl $0x10000,-4(%rbp,%rcx,4)
812bffffff7f	subl $0x7fffffff,(%rbx)
812c2500000000ffffff7f 3:11:sym+0	subl $0x7fffffff,sym
816c8dfcffffff7f	subl $0x7fffffff,-4(%rbp,%rcx,4)
812b00000080	subl $0x80000000,(%rbx)
812c250000000000000080 3:11:sym+0	subl $0x80000000,sym
816c8dfc00000080	subl $0x80000000,-4(%rbp,%rcx,4)
832b80	subl $0xffffff80,(%rbx)
832c250000000080 3:11:sym+0	subl $0xffffff80,sym
836c8dfc80	subl $0xffffff80,-4(%rbp,%rcx,4)
832bff	subl $0xffffffff,(%rbx)
832c2500000000ff 3:11:sym+0	subl $0xffffffff,sym
836c8dfcff	subl $0xffffffff,-4(%rbp,%rcx,4)
812b00000000 2:10:sym+0	subl $sym,(%rbx)
812c250000000000000000 3:11:sym+0 7:10:sym+0	subl $sym,sym
816c8dfc00000000 4:10:sym+0	subl $sym,-4(%rbp,%rcx,4)
83eb01	sub $1,%ebx
refused	subl $1,$2
refused	subl %eax,$2
refused	subl (%rbx),(%rbx)
f0291b	lock subl %ebx,(%rbx)
f0832b01	lock subl $1,(%rbx)
refused	lock subl (%rbx),%ebx
refused	lock subl %ebx,%eax
4829c0	subq %rax,%rax
482903	subq %rax,(%rbx)
482b03	subq (%rbx),%rax
4829042500000000 4:11:sym+0	subq %rax,sym
482b042500000000 4:11:sym+0	subq sym,%rax
482903	sub %rax,(%rbx)
refused	sub 4((%rbx,%rsi,2),%rax
4883e800	subq $0,%rax
4883e801	subq $1,%rax
4883e8ff	subq $-1,%rax
4883e87f	subq $127,%rax
482d80000000	subq $128,%rax
4883e880	subq $-128,%rax
482d7fffffff	subq $-129,%rax
482dff000000	subq $255,%rax
482d00010000	subq $256,%rax
482dff7f0000	subq $0x7fff,%rax
482d00800000	subq $0x8000,%rax
482d80ff0000	subq $0xff80,%rax
482dffff0000	subq $0xffff,%rax
482d00000100	subq $0x10000,%rax
482dffffff7f	subq $0x7fffffff,%rax
refused	subq $0x80000000,%rax
refused	subq $0xffffff80,%rax
refused	subq $0xffffffff,%rax
482d00000000 2:11:sym+0	subq $sym,%rax
4829d8	subq %rbx,%rax
4829c3	subq %rax,%rbx
48291b	subq %rbx,(%rbx)
482b1b	subq (%rbx),%rbx
48291c2500000000 4:11:sym+0	subq %rbx,sym
482b1c2500000000 4:11:sym+0	subq sym,%rbx
48291b	sub %rbx,(%rbx)
refused	sub 4((%rbx,%rsi,2),%rbx
4883eb00	subq $0,%rbx
4883eb01	subq $1,%rbx
4883ebff	subq $-1,%rbx
4883eb7f	subq $127,%rbx
4881eb80000000	subq $128,%rbx
4883eb80	subq $-128,%rbx
4881eb7fffffff	subq $-129,%rbx
4881ebff000000	subq $255,%rbx
4881eb00010000	subq $256,%rbx
4881ebff7f0000	subq $0x7fff,%rbx
4881eb00800000	subq $0x8000,%rbx
4881eb80ff0000	subq $0xff80,%rbx
4881ebffff0000	subq $0xffff,%rbx
4881eb00000100	subq $0x10000,%rbx
4881ebffffff7f	subq $0x7fffffff,%rbx
refused	subq $0x80000000,%rbx
refused	subq $0xffffff80,%rbx
refused	subq $0xffffffff,%rbx
4881eb00000000 3:11:sym+0	subq $sym,%rbx
4829e0	subq %rsp,%rax
4829c4	subq %rax,%rsp
482923	subq %rsp,(%rbx)
482b23	subq (%rbx),%rsp
4829242500000000 4:11:sym+0	subq %rsp,sym
482b242500000000 4:11:sym+0	subq sym,%rsp
482923	sub %rsp,(%rbx)
refused	sub 4((%rbx,%rsi,2),%rsp
4883ec7f	subq $127,%rsp
4881ec80000000	subq $128,%rsp
4883ec80	subq $-128,%rsp
4881ec7fffffff	subq $-129,%rsp
4881ecff000000	subq $255,%rsp
4881ec00010000	subq $256,%rsp
4881ecff7f0000	subq $0x7fff,%rsp
4881ec00800000	subq $0x8000,%rsp
4881ec80ff0000	subq $0xff80,%rsp
4881ecffff0000	subq $0xffff,%rsp
4881ec00000100	subq $0x10000,%rsp
refused	subq $0xffffff80,%rsp
4881ec00000000 3:11:sym+0	subq $sym,%rsp
4c29e0	subq %r12,%rax
4929c4	subq %rax,%r12
4c2923	subq %r12,(%rbx)
4c2b23	subq (%rbx),%r12
4c29242500000000 4:11:sym+0	subq %r12,sym
4c2b242500000000 4:11:sym+0	subq sym,%r12
4c2923	sub %r12,(%rbx)
refused	sub 4((%rbx,%rsi,2),%r12
4983ec00	subq $0,%r12
4983ec01	subq $1,%r12
4983ecff	subq $-1,%r12
4983ec7f	subq $127,%r12
4981ec80000000	subq $128,%r12
4983ec80	subq $-128,%r12
4981ec7fffffff	subq $-129,%r12
4981ecff000000	subq $255,%r12
4981ec00010000	subq $256,%r12
4981ecff7f0000	subq $0x7fff,%r12
4981ec00800000	subq $0x8000,%r12
4981ec80ff0000	subq $0xff80,%r12
4981ecffff0000	subq $0xffff,%r12
4981ec00000100	subq $0x10000,%r12
4981ecffffff7f	subq $0x7fffffff,%r12
refused	subq $0x80000000,%r12
refused	subq $0xffffff80,%r12
refused	subq $0xffffffff,%r12
4981ec00000000 3:11:sym+0	subq $sym,%r12
4c29e8	subq %r13,%rax
4929c5	subq %rax,%r13
4c292b	subq %r13,(%rbx)
4c2b2b	subq (%rbx),%r13
4c292c2500000000 4:11:sym+0	subq %r13,sym
4c2b2c2500000000 4:11:sym+0	subq sym,%r13
4c292b	sub %r13,(%rbx)
refused	sub 4((%rbx,%rsi,2),%r13
4983ed00	subq $0,%r13
4983ed01	subq $1,%r13
4983edff	subq $-1,%r13
4983ed7f	subq $127,%r13
4981ed80000000	subq $128,%r13
4983ed80	subq $-128,%r13
4981ed7fffffff	subq $-129,%r13
4981edff000000	subq $255,%r13
4981ed00010000	subq $256,%r13
4981edff7f0000	subq $0x7fff,%r13
4981ed00800000	subq $0x8000,%r13
4981ed80ff0000	subq $0xff80,%r13
4981edffff0000	subq $0xffff,%r13
4981ed00000100	subq $0x10000,%r13
4981edffffff7f	subq $0x7fffffff,%r13
refused	subq $0x80000000,%r13
refused	subq $0xffffff80,%r13
refused	subq $0xffffffff,%r13
4981ed00000000 3:11:sym+0	subq $sym,%r13
4c29c8	subq %r9,%rax
4929c1	subq %rax,%r9
4c290b	subq %r9,(%rbx)
4c2b0b	subq (%rbx),%r9
4c290c2500000000 4:11:sym+0	subq %r9,sym
4c2b0c2500000000 4:11:sym+0	subq sym,%r9
4c290b	sub %r9,(%rbx)
refused	sub 4((%rbx,%rsi,2),%r9
4983e900	subq $0,%r9
4983e901	subq $1,%r9
4983e9ff	subq $-1,%r9
4983e97f	subq $127,%r9
4981e980000000	subq $128,%r9
4983e980	subq $-128,%r9
4981e97fffffff	subq $-129,%r9
4981e9ff000000	subq $255,%r9
4981e900010000	subq $256,%r9
4981e9ff7f0000	subq $0x7fff,%r9
4981e900800000	subq $0x8000,%r9
4981e980ff0000	subq $0xff80,%r9
4981e9ffff0000	subq $0xffff,%r9
4981e900000100	subq $0x10000,%r9
4981e9ffffff7f	subq $0x7fffffff,%r9
refused	subq $0x80000000,%r9
refused	subq $0xffffff80,%r9
refused	subq $0xffffffff,%r9
4981e900000000 3:11:sym+0	subq $sym,%r9
48832b00	subq $0,(%rbx)
48832c250000000000 4:11:sym+0	subq $0,sym
48836c8dfc00	subq $0,-4(%rbp,%rcx,4)
48832b01	subq $1,(%rbx)
48832c250000000001 4:11:sym+0	subq $1,sym
48836c8dfc01	subq $1,-4(%rbp,%rcx,4)
48832bff	subq $-1,(%rbx)
48832c2500000000ff 4:11:sym+0	subq $-1,sym
48836c8dfcff	subq $-1,-4(%rbp,%rcx,4)
48832b7f	subq $127,(%rbx)
48832c25000000007f 4:11:sym+0	subq $127,sym
48836c8dfc7f	subq $127,-4(%rbp,%rcx,4)
48812b80000000	subq $128,(%rbx)
48812c250000000080000000 4:11:sym+0	subq $128,sym
48816c8dfc80000000	subq $128,-4(%rbp,%rcx,4)
48832b80	subq $-128,(%rbx)
48832c250000000080 4:11:sym+0	subq $-128,sym
48836c8dfc80	subq $-128,-4(%rbp,%rcx,4)
48812b7fffffff	subq $-129,(%rbx)
48812c25000000007fffffff 4:11:sym+0	subq $-129,sym
48816c8dfc7fffffff	subq $-129,-4(%rbp,%rcx,4)
48812bff000000	subq $255,(%rbx)
48812c2500000000ff000000 4:11:sym+0	subq $255,sym
48816c8dfcff000000	subq $255,-4(%rbp,%rcx,4)
48812b00010000	subq $256,(%rbx)
48812c250000000000010000 4:11:sym+0	subq $256,sym
48816c8dfc00010000	subq $256,-4(%rbp,%rcx,4)
48812bff7f0000	subq $0x7fff,(%rbx)
48812c2500000000ff7f0000 4:11:sym+0	subq $0x7fff,sym
48816c8dfcff7f0000	subq $0x7fff,-4(%rbp,%rcx,4)
48812b00800000	subq $0x8000,(%rbx)
48812c250000000000800000 4:11:sym+0	subq $0x8000,sym
48816c8dfc00800000	subq $0x8000,-4(%rbp,%rcx,4)
48812b80ff0000	subq $0xff80,(%rbx)
48812c250000000080ff0000 4:11:sym+0	subq $0xff80,sym
48816c8dfc80ff0000	subq $0xff80,-4(%rbp,%rcx,4)
48812bffff0000	subq $0xffff,(%rbx)
48812c2500000000ffff0000 4:11:sym+0	subq $0xffff,sym
48816c8dfcffff0000	subq $0xffff,-4(%rbp,%rcx,4)
48812b00000100	subq $0x10000,(%rbx)
48812c250000000000000100 4:11:sym+0	subq $0x10000,sym
48816c8dfc00000100	subq $0x10000,-4(%rbp,%rcx,4)
48812bffffff7f	subq $0x7fffffff,(%rbx)
48812c2500000000ffffff7f 4:11:sym+0	subq $0x7fffffff,sym
48816c8dfcffffff7f	subq $0x7fffffff,-4(%rbp,%rcx,4)
refused	subq $0x80000000,(%rbx)
refused	subq $0x80000000,sym
refused	subq $0x80000000,-4(%rbp,%rcx,4)
refused	subq $0xffffff80,(%rbx)
refused	subq $0xffffff80,sym
refused	subq $0xffffff80,-4(%rbp,%rcx,4)
refused	subq $0xffffffff,(%rbx)
refused	subq $0xffffffff,sym
refused	subq $0xffffffff,-4(%rbp,%rcx,4)
48812b00000000 3:11:sym+0	subq $sym,(%rbx)
48812c250000000000000000 4:11:sym+0 8:11:sym+0	subq $sym,sym
48816c8dfc00000000 5:11:sym+0	subq $sym,-4(%rbp,%rcx,4)
4883eb01	sub $1,%rbx
refused	subq $1,$2
refused	subq %rax,$2
refused	subq (%rbx),(%rbx)
f048291b	lock subq %rbx,(%rbx)
f048832b01	lock subq $1,(%rbx)
refused	lock subq (%rbx),%rbx
refused	lock subq %rbx,%rax
30c0	xorb %al,%al
3003	xorb %al,(%rbx)
3203	xorb (%rbx),%al
30042500000000 3:11:sym+0	xorb %al,sym
32042500000000 3:11:sym+0	xorb sym,%al
3003	xor %al,(%rbx)
refused	xor 4((%rbx,%rsi,2),%al
3400	xorb $0,%al
3401	xorb $1,%al
34ff	xorb $-1,%al
347f	xorb $127,%al
3480	xorb $128,%al
3480	xorb $-128,%al
347f	xorb $-129,%al
34ff	xorb $255,%al
3400	xorb $256,%al
34ff	xorb $0x7fff,%al
3400	xorb $0x8000,%al
3480	xorb $0xff80,%al
34ff	xorb $0xffff,%al
3400	xorb $0x10000,%al
34ff	xorb $0x7fffffff,%al
3400	xorb $0x80000000,%al
3480	xorb $0xffffff80,%al
34ff	xorb $0xffffffff,%al
3400 1:14:sym+0	xorb $sym,%al
30c8	xorb %cl,%al
30c1	xorb %al,%cl
300b	xorb %cl,(%rbx)
320b	xorb (%rbx),%cl
300c2500000000 3:11:sym+0	xorb %cl,sym
320c2500000000 3:11:sym+0	xorb sym,%cl
300b	xor %cl,(%rbx)
refused	xor 4((%rbx,%rsi,2),%cl
80f100	xorb $0,%cl
80f101	xorb $1,%cl
80f1ff	xorb $-1,%cl
80f17f	xorb $127,%cl
80f180	xorb $128,%cl
80f180	xorb $-128,%cl
80f17f	xorb $-129,%cl
80f1ff	xorb $255,%cl
80f100	xorb $256,%cl
80f1ff	xorb $0x7fff,%cl
80f100	xorb $0x8000,%cl
80f180	xorb $0xff80,%cl
80f1ff	xorb $0xffff,%cl
80f100	xorb $0x10000,%cl
80f1ff	xorb $0x7fffffff,%cl
80f100	xorb $0x80000000,%cl
80f180	xorb $0xffffff80,%cl
80f1ff	xorb $0xffffffff,%cl
80f100 2:14:sym+0	xorb $sym,%cl
30f8	xorb %bh,%al
30c7	xorb %al,%bh
303b	xorb %bh,(%rbx)
323b	xorb (%rbx),%bh
303c2500000000 3:11:sym+0	xorb %bh,sym
323c2500000000 3:11:sym+0	xorb sym,%bh
303b	xor %bh,(%rbx)
refused	xor 4((%rbx,%rsi,2),%bh
80f700	xorb $0,%bh
80f701	xorb $1,%bh
80f7ff	xorb $-1,%bh
80f77f	xorb $127,%bh
80f780	xorb $128,%bh
80f780	xorb $-128,%bh
80f77f	xorb $-129,%bh
80f7ff	xorb $255,%bh
80f700	xorb $256,%bh
80f7ff	xorb $0x7fff,%bh
80f700	xorb $0x8000,%bh
80f780	xorb $0xff80,%bh
80f7ff	xorb $0xffff,%bh
80f700	xorb $0x10000,%bh
80f7ff	xorb $0x7fffffff,%bh
80f700	xorb $0x80000000,%bh
80f780	xorb $0xffffff80,%bh
80f7ff	xorb $0xffffffff,%bh
80f700 2:14:sym+0	xorb $sym,%bh
4030f0	xorb %sil,%al
4030c6	xorb %al,%sil
403033	xorb %sil,(%rbx)
403233	xorb (%rbx),%sil
4030342500000000 4:11:sym+0	xorb %sil,sym
4032342500000000 4:11:sym+0	xorb sym,%sil
403033	xor %sil,(%rbx)
refused	xor 4((%rbx,%rsi,2),%sil
4080f600	xorb $0,%sil
4080f601	xorb $1,%sil
4080f6ff	xorb $-1,%sil
4080f67f	xorb $127,%sil
4080f680	xorb $128,%sil
4080f680	xorb $-128,%sil
4080f67f	xorb $-129,%sil
4080f6ff	xorb $255,%sil
4080f600	xorb $256,%sil
4080f6ff	xorb $0x7fff,%sil
4080f600	xorb $0x8000,%sil
4080f680	xorb $0xff80,%sil
4080f6ff	xorb $0xffff,%sil
4080f600	xorb $0x10000,%sil
4080f6ff	xorb $0x7fffffff,%sil
4080f600	xorb $0x80000000,%sil
4080f680	xorb $0xffffff80,%sil
4080f6ff	xorb $0xffffffff,%sil
4080f600 3:14:sym+0	xorb $sym,%sil
4430c8	xorb %r9b,%al
4130c1	xorb %al,%r9b
44300b	xorb %r9b,(%rbx)
44320b	xorb (%rbx),%r9b
44300c2500000000 4:11:sym+0	xorb %r9b,sym
44320c2500000000 4:11:sym+0	xorb sym,%r9b
44300b	xor %r9b,(%rbx)
refused	xor 4((%rbx,%rsi,2),%r9b
4180f100	xorb $0,%r9b
4180f101	xorb $1,%r9b
4180f1ff	xorb $-1,%r9b
4180f17f	xorb $127,%r9b
4180f180	xorb $128,%r9b
4180f180	xorb $-128,%r9b
4180f17f	xorb $-129,%r9b
4180f1ff	xorb $255,%r9b
4180f100	xorb $256,%r9b
4180f1ff	xorb $0x7fff,%r9b
4180f100	xorb $0x8000,%r9b
4180f180	xorb $0xff80,%r9b
4180f1ff	xorb $0xffff,%r9b
4180f100	xorb $0x10000,%r9b
4180f1ff	xorb $0x7fffffff,%r9b
4180f100	xorb $0x80000000,%r9b
4180f180	xorb $0xffffff80,%r9b
4180f1ff	xorb $0xffffffff,%r9b
4180f100 3:14:sym+0	xorb $sym,%r9b
803300	xorb $0,(%rbx)
8034250000000000 3:11:sym+0	xorb $0,sym
80748dfc00	xorb $0,-4(%rbp,%rcx,4)
803301	xorb $1,(%rbx)
8034250000000001 3:11:sym+0	xorb $1,sym
80748dfc01	xorb $1,-4(%rbp,%rcx,4)
8033ff	xorb $-1,(%rbx)
80342500000000ff 3:11:sym+0	xorb $-1,sym
80748dfcff	xorb $-1,-4(%rbp,%rcx,4)
80337f	xorb $127,(%rbx)
803425000000007f 3:11:sym+0	xorb $127,sym
80748dfc7f	xorb $127,-4(%rbp,%rcx,4)
803380	xorb $128,(%rbx)
8034250000000080 3:11:sym+0	xorb $128,sym
80748dfc80	xorb $128,-4(%rbp,%rcx,4)
803380	xorb $-128,(%rbx)
8034250000000080 3:11:sym+0	xorb $-128,sym
80748dfc80	xorb $-128,-4(%rbp,%rcx,4)
80337f	xorb $-129,(%rbx)
803425000000007f 3:11:sym+0	xorb $-129,sym
80748dfc7f	xorb $-129,-4(%rbp,%rcx,4)
8033ff	xorb $255,(%rbx)
80342500000000ff 3:11:sym+0	xorb $255,sym
80748dfcff	xorb $255,-4(%rbp,%rcx,4)
803300	xorb $256,(%rbx)
8034250000000000 3:11:sym+0	xorb $256,sym
80748dfc00	xorb $256,-4(%rbp,%rcx,4)
8033ff	xorb $0x7fff,(%rbx)
80342500000000ff 3:11:sym+0	xorb $0x7fff,sym
80748dfcff	xorb $0x7fff,-4(%rbp,%rcx,4)
803300	xorb $0x8000,(%rbx)
8034250000000000 3:11:sym+0	xorb $0x8000,sym
80748dfc00	xorb $0x8000,-4(%rbp,%rcx,4)
803380	xorb $0xff80,(%rbx)
8034250000000080 3:11:sym+0	xorb $0xff80,sym
80748dfc80	xorb $0xff80,-4(%rbp,%rcx,4)
8033ff	xorb $0xffff,(%rbx)
80342500000000ff 3:11:sym+0	xorb $0xffff,sym
80748dfcff	xorb $0xffff,-4(%rbp,%rcx,4)
803300	xorb $0x10000,(%rbx)
8034250000000000 3:11:sym+0	xorb $0x10000,sym
80748dfc00	xorb $0x10000,-4(%rbp,%rcx,4)
8033ff	xorb $0x7fffffff,(%rbx)
80342500000000ff 3:11:sym+0	xorb $0x7fffffff,sym
80748dfcff	xorb $0x7fffffff,-4(%rbp,%rcx,4)
803300	xorb $0x80000000,(%rbx)
8034250000000000 3:11:sym+0	xorb $0x80000000,sym
80748dfc00	xorb $0x80000000,-4(%rbp,%rcx,4)
803380	xorb $0xffffff80,(%rbx)
8034250000000080 3:11:sym+0	xorb $0xffffff80,sym
80748dfc80	xorb $0xffffff80,-4(%rbp,%rcx,4)
8033ff	xorb $0xffffffff,(%rbx)
80342500000000ff 3:11:sym+0	xorb $0xffffffff,sym
80748dfcff	xorb $0xffffffff,-4(%rbp,%rcx,4)
803300 2:14:sym+0	xorb $sym,(%rbx)
8034250000000000 3:11:sym+0 7:14:sym+0	xorb $sym,sym
80748dfc00 4:14:sym+0	xorb $sym,-4(%rbp,%rcx,4)
80f101	xor $1,%cl
833301	xor $1,(%rbx)
refused	xorb $1,$2
refused	xorb %al,$2
refused	xorb (%rbx),(%rbx)
f0300b	lock xorb %cl,(%rbx)
f0803301	lock xorb $1,(%rbx)
refused	lock xorb (%rbx),%cl
refused	lock xorb %cl,%al
6631c0	xorw %ax,%ax
663103	xorw %ax,(%rbx)
663303	xorw (%rbx),%ax
6631042500000000 4:11:sym+0	xorw %ax,sym
6633042500000000 4:11:sym+0	xorw sym,%ax
663103	xor %ax,(%rbx)
refused	xor 4((%rbx,%rsi,2),%ax
6683f000	xorw $0,%ax
6683f001	xorw $1,%ax
6683f0ff	xorw $-1,%ax
6683f07f	xorw $127,%ax
66358000	xorw $128,%ax
6683f080	xorw $-128,%ax
66357fff	xorw $-129,%ax
6635ff00	xorw $255,%ax
66350001	xorw $256,%ax
6635ff7f	xorw $0x7fff,%ax
66350080	xorw $0x8000,%ax
6683f080	xorw $0xff80,%ax
6683f0ff	xorw $0xffff,%ax
66350000	xorw $0x10000,%ax
6635ffff	xorw $0x7fffffff,%ax
66350000	xorw $0x80000000,%ax
6683f080	xorw $0xffffff80,%ax
6683f0ff	xorw $0xffffffff,%ax
66350000 2:12:sym+0	xorw $sym,%ax
6631d8	xorw %bx,%ax
6631c3	xorw %ax,%bx
66311b	xorw %bx,(%rbx)
66331b	xorw (%rbx),%bx
66311c2500000000 4:11:sym+0	xorw %bx,sym
66331c2500000000 4:11:sym+0	xorw sym,%bx
66311b	xor %bx,(%rbx)
refused	xor 4((%rbx,%rsi,2),%bx
6683f300	xorw $0,%bx
6683f301	xorw $1,%bx
6683f3ff	xorw $-1,%bx
6683f37f	xorw $127,%bx
6681f38000	xorw $128,%bx
6683f380	xorw $-128,%bx
6681f37fff	xorw $-129,%bx
6681f3ff00	xorw $255,%bx
6681f30001	xorw $256,%bx
6681f3ff7f	xorw $0x7fff,%bx
6681f30080	xorw $0x8000,%bx
6683f380	xorw $0xff80,%bx
6683f3ff	xorw $0xffff,%bx
6681f30000	xorw $0x10000,%bx
6681f3ffff	xorw $0x7fffffff,%bx
6681f30000	xorw $0x80000000,%bx
6683f380	xorw $0xffffff80,%bx
6683f3ff	xorw $0xffffffff,%bx
6681f30000 3:12:sym+0	xorw $sym,%bx
6631f0	xorw %si,%ax
6631c6	xorw %ax,%si
663133	xorw %si,(%rbx)
663333	xorw (%rbx),%si
6631342500000000 4:11:sym+0	xorw %si,sym
6633342500000000 4:11:sym+0	xorw sym,%si
663133	xor %si,(%rbx)
refused	xor 4((%rbx,%rsi,2),%si
6683f600	xorw $0,%si
6683f601	xorw $1,%si
6683f6ff	xorw $-1,%si
6683f67f	xorw $127,%si
6681f68000	xorw $128,%si
6683f680	xorw $-128,%si
6681f67fff	xorw $-129,%si
6681f6ff00	xorw $255,%si
6681f60001	xorw $256,%si
6681f6ff7f	xorw $0x7fff,%si
6681f60080	xorw $0x8000,%si
6683f680	xorw $0xff80,%si
6683f6ff	xorw $0xffff,%si
6681f60000	xorw $0x10000,%si
6681f6ffff	xorw $0x7fffffff,%si
6681f60000	xorw $0x80000000,%si
6683f680	xorw $0xffffff80,%si
6683f6ff	xorw $0xffffffff,%si
6681f60000 3:12:sym+0	xorw $sym,%si
664431c8	xorw %r9w,%ax
664131c1	xorw %ax,%r9w
6644310b	xorw %r9w,(%rbx)
6644330b	xorw (%rbx),%r9w
6644310c2500000000 5:11:sym+0	xorw %r9w,sym
6644330c2500000000 5:11:sym+0	xorw sym,%r9w
6644310b	xor %r9w,(%rbx)
refused	xor 4((%rbx,%rsi,2),%r9w
664183f100	xorw $0,%r9w
664183f101	xorw $1,%r9w
664183f1ff	xorw $-1,%r9w
664183f17f	xorw $127,%r9w
664181f18000	xorw $128,%r9w
664183f180	xorw $-128,%r9w
664181f17fff	xorw $-129,%r9w
664181f1ff00	xorw $255,%r9w
664181f10001	xorw $256,%r9w
664181f1ff7f	xorw $0x7fff,%r9w
664181f10080	xorw $0x8000,%r9w
664183f180	xorw $0xff80,%r9w
664183f1ff	xorw $0xffff,%r9w
664181f10000	xorw $0x10000,%r9w
664181f1ffff	xorw $0x7fffffff,%r9w
664181f10000	xorw $0x80000000,%r9w
664183f180	xorw $0xffffff80,%r9w
664183f1ff	xorw $0xffffffff,%r9w
664181f10000 4:12:sym+0	xorw $sym,%r9w
66833300	xorw $0,(%rbx)
668334250000000000 4:11:sym+0	xorw $0,sym
6683748dfc00	xorw $0,-4(%rbp,%rcx,4)
66833301	xorw $1,(%rbx)
668334250000000001 4:11:sym+0	xorw $1,sym
6683748dfc01	xorw $1,-4(%rbp,%rcx,4)
668333ff	xorw $-1,(%rbx)
6683342500000000ff 4:11:sym+0	xorw $-1,sym
6683748dfcff	xorw $-1,-4(%rbp,%rcx,4)
6683337f	xorw $127,(%rbx)
66833425000000007f 4:11:sym+0	xorw $127,sym
6683748dfc7f	xorw $127,-4(%rbp,%rcx,4)
6681338000	xorw $128,(%rbx)
66813425000000008000 4:11:sym+0	xorw $128,sym
6681748dfc8000	xorw $128,-4(%rbp,%rcx,4)
66833380	xorw $-128,(%rbx)
668334250000000080 4:11:sym+0	xorw $-128,sym
6683748dfc80	xorw $-128,-4(%rbp,%rcx,4)
6681337fff	xorw $-129,(%rbx)
66813425000000007fff 4:11:sym+0	xorw $-129,sym
6681748dfc7fff	xorw $-129,-4(%rbp,%rcx,4)
668133ff00	xorw $255,(%rbx)
6681342500000000ff00 4:11:sym+0	xorw $255,sym
6681748dfcff00	xorw $255,-4(%rbp,%rcx,4)
6681330001	xorw $256,(%rbx)
66813425000000000001 4:11:sym+0	xorw $256,sym
6681748dfc0001	xorw $256,-4(%rbp,%rcx,4)
668133ff7f	xorw $0x7fff,(%rbx)
6681342500000000ff7f 4:11:sym+0	xorw $0x7fff,sym
6681748dfcff7f	xorw $0x7fff,-4(%rbp,%rcx,4)
6681330080	xorw $0x8000,(%rbx)
66813425000000000080 4:11:sym+0	xorw $0x8000,sym
6681748dfc0080	xorw $0x8000,-4(%rbp,%rcx,4)
66833380	xorw $0xff80,(%rbx)
668334250000000080 4:11:sym+0	xorw $0xff80,sym
6683748dfc80	xorw $0xff80,-4(%rbp,%rcx,4)
668333ff	xorw $0xffff,(%rbx)
6683342500000000ff 4:11:sym+0	xorw $0xffff,sym
6683748dfcff	xorw $0xffff,-4(%rbp,%rcx,4)
6681330000	xorw $0x10000,(%rbx)
66813425000000000000 4:11:sym+0	xorw $0x10000,sym
6681748dfc0000	xorw $0x10000,-4(%rbp,%rcx,4)
668133ffff	xorw $0x7fffffff,(%rbx)
6681342500000000ffff 4:11:sym+0	xorw $0x7fffffff,sym
6681748dfcffff	xorw $0x7fffffff,-4(%rbp,%rcx,4)
6681330000	xorw $0x80000000,(%rbx)
66813425000000000000 4:11:sym+0	xorw $0x80000000,sym
6681748dfc0000	xorw $0x80000000,-4(%rbp,%rcx,4)
66833380	xorw $0xffffff80,(%rbx)
668334250000000080 4:11:sym+0	xorw $0xffffff80,sym
6683748dfc80	xorw $0xffffff80,-4(%rbp,%rcx,4)
668333ff	xorw $0xffffffff,(%rbx)
6683342500000000ff 4:11:sym+0	xorw $0xffffffff,sym
6683748dfcff	xorw $0xffffffff,-4(%rbp,%rcx,4)
6681330000 3:12:sym+0	xorw $sym,(%rbx)
66813425000000000000 4:11:sym+0 8:12:sym+0	xorw $sym,sym
6681748dfc0000 5:12:sym+0	xorw $sym,-4(%rbp,%rcx,4)
6683f301	xor $1,%bx
refused	xorw $1,$2
refused	xorw %ax,$2
refused	xorw (%rbx),(%rbx)
66f0311b	lock xorw %bx,(%rbx)
66f0833301	lock xorw $1,(%rbx)
refused	lock xorw (%rbx),%bx
refused	lock xorw %bx,%ax
31c0	xorl %eax,%eax
3103	xorl %eax,(%rbx)
3303	xorl (%rbx),%eax
31042500000000 3:11:sym+0	xorl %eax,sym
33042500000000 3:11:sym+0	xorl sym,%eax
3103	xor %eax,(%rbx)
refused	xor 4((%rbx,%rsi,2),%eax
83f000	xorl $0,%eax
83f001	xorl $1,%eax
83f0ff	xorl $-1,%eax
83f07f	xorl $127,%eax
3580000000	xorl $128,%eax
83f080	xorl $-128,%eax
357fffffff	xorl $-129,%eax
35ff000000	xorl $255,%eax
3500010000	xorl $256,%eax
35ff7f0000	xorl $0x7fff,%eax
3500800000	xorl $0x8000,%eax
3580ff0000	xorl $0xff80,%eax
35ffff0000	xorl $0xffff,%eax
3500000100	xorl $0x10000,%eax
35ffffff7f	xorl $0x7fffffff,%eax
3500000080	xorl $0x80000000,%eax
83f080	xorl $0xffffff80,%eax
83f0ff	xorl $0xffffffff,%eax
3500000000 1:10:sym+0	xorl $sym,%eax
31d8	xorl %ebx,%eax
31c3	xorl %eax,%ebx
311b	xorl %ebx,(%rbx)
331b	xorl (%rbx),%ebx
311c2500000000 3:11:sym+0	xorl %ebx,sym
331c2500000000 3:11:sym+0	xorl sym,%ebx
311b	xor %ebx,(%rbx)
refused	xor 4((%rbx,%rsi,2),%ebx
83f300	xorl $0,%ebx
83f301	xorl $1,%ebx
83f3ff	xorl $-1,%ebx
83f37f	xorl $127,%ebx
81f380000000	xorl $128,%ebx
83f380	xorl $-128,%ebx
81f37fffffff	xorl $-129,%ebx
81f3ff000000	xorl $255,%ebx
81f300010000	xorl $256,%ebx
81f3ff7f0000	xorl $0x7fff,%ebx
81f300800000	xorl $0x8000,%ebx
81f380ff0000	xorl $0xff80,%ebx
81f3ffff0000	xorl $0xffff,%ebx
81f300000100	xorl $0x10000,%ebx
81f3ffffff7f	xorl $0x7fffffff,%ebx
81f300000080	xorl $0x80000000,%ebx
83f380	xorl $0xffffff80,%ebx
83f3ff	xorl $0xffffffff,%ebx
81f300000000 2:10:sym+0	xorl $sym,%ebx
31e0	xorl %esp,%eax
31c4	xorl %eax,%esp
3123	xorl %esp,(%rbx)
3323	xorl (%rbx),%esp
31242500000000 3:11:sym+0	xorl %esp,sym
33242500000000 3:11:sym+0	xorl sym,%esp
3123	xor %esp,(%rbx)
refused	xor 4((%rbx,%rsi,2),%esp
83f400	xorl $0,%esp
83f401	xorl $1,%esp
83f4ff	xorl $-1,%esp
83f47f	xorl $127,%esp
81f480000000	xorl $128,%esp
83f480	xorl $-128,%esp
81f47fffffff	xorl $-129,%esp
81f4ff000000	xorl $255,%esp
81f400010000	xorl $256,%esp
81f4ff7f0000	xorl $0x7fff,%esp
81f400800000	xorl $0x8000,%esp
81f480ff0000	xorl $0xff80,%esp
81f4ffff0000	xorl $0xffff,%esp
81f400000100	xorl $0x10000,%esp
81f4ffffff7f	xorl $0x7fffffff,%esp
81f400000080	xorl $0x80000000,%esp
83f480	xorl $0xffffff80,%esp
83f4ff	xorl $0xffffffff,%esp
81f400000000 2:10:sym+0	xorl $sym,%esp
31e8	xorl %ebp,%eax
31c5	xorl %eax,%ebp
312b	xorl %ebp,(%rbx)
332b	xorl (%rbx),%ebp
312c2500000000 3:11:sym+0	xorl %ebp,sym
332c2500000000 3:11:sym+0	xorl sym,%ebp
312b	xor %ebp,(%rbx)
refused	xor 4((%rbx,%rsi,2),%ebp
83f500	xorl $0,%ebp
83f501	xorl $1,%ebp
83f5ff	xorl $-1,%ebp
83f57f	xorl $127,%ebp
81f580000000	xorl $128,%ebp
83f580	xorl $-128,%ebp
81f57fffffff	xorl $-129,%ebp
81f5ff000000	xorl $255,%ebp
81f500010000	xorl $256,%ebp
81f5ff7f0000	xorl $0x7fff,%ebp
81f500800000	xorl $0x8000,%ebp
81f580ff0000	xorl $0xff80,%ebp
81f5ffff0000	xorl $0xffff,%ebp
81f500000100	xorl $0x10000,%ebp
81f5ffffff7f	xorl $0x7fffffff,%ebp
81f500000080	xorl $0x80000000,%ebp
83f580	xorl $0xffffff80,%ebp
83f5ff	xorl $0xffffffff,%ebp
81f500000000 2:10:sym+0	xorl $sym,%ebp
4431c8	xorl %r9d,%eax
4131c1	xorl %eax,%r9d
44310b	xorl %r9d,(%rbx)
44330b	xorl (%rbx),%r9d
44310c2500000000 4:11:sym+0	xorl %r9d,sym
44330c2500000000 4:11:sym+0	xorl sym,%r9d
44310b	xor %r9d,(%rbx)
refused	xor 4((%rbx,%rsi,2),%r9d
4183f100	xorl $0,%r9d
4183f101	xorl $1,%r9d
4183f1ff	xorl $-1,%r9d
4183f17f	xorl $127,%r9d
4181f180000000	xorl $128,%r9d
4183f180	xorl $-128,%r9d
4181f17fffffff	xorl $-129,%r9d
4181f1ff000000	xorl $255,%r9d
4181f100010000	xorl $256,%r9d
4181f1ff7f0000	xorl $0x7fff,%r9d
4181f100800000	xorl $0x8000,%r9d
4181f180ff0000	xorl $0xff80,%r9d
4181f1ffff0000	xorl $0xffff,%r9d
4181f100000100	xorl $0x10000,%r9d
4181f1ffffff7f	xorl $0x7fffffff,%r9d
4181f100000080	xorl $0x80000000,%r9d
4183f180	xorl $0xffffff80,%r9d
4183f1ff	xorl $0xffffffff,%r9d
4181f100000000 3:10:sym+0	xorl $sym,%r9d
833300	xorl $0,(%rbx)
8334250000000000 3:11:sym+0	xorl $0,sym
83748dfc00	xorl $0,-4(%rbp,%rcx,4)
833301	xorl $1,(%rbx)
8334250000000001 3:11:sym+0	xorl $1,sym
83748dfc01	xorl $1,-4(%rbp,%rcx,4)
8333ff	xorl $-1,(%rbx)
83342500000000ff 3:11:sym+0	xorl $-1,sym
83748dfcff	xorl $-1,-4(%rbp,%rcx,4)
83337f	xorl $127,(%rbx)
833425000000007f 3:11:sym+0	xorl $127,sym
83748dfc7f	xorl $127,-4(%rbp,%rcx,4)
813380000000	xorl $128,(%rbx)
8134250000000080000000 3:11:sym+0	xorl $128,sym
81748dfc80000000	xorl $128,-4(%rbp,%rcx,4)
833380	xorl $-128,(%rbx)
8334250000000080 3:11:sym+0	xorl $-128,sym
83748dfc80	xorl $-128,-4(%rbp,%rcx,4)
81337fffffff	xorl $-129,(%rbx)
813425000000007fffffff 3:11:sym+0	xorl $-129,sym
81748dfc7fffffff	xorl $-129,-4(%rbp,%rcx,4)
8133ff000000	xorl $255,(%rbx)
81342500000000ff000000 3:11:sym+0	xorl $255,sym
81748dfcff000000	xorl $255,-4(%rbp,%rcx,4)
813300010000	xorl $256,(%rbx)
8134250000000000010000 3:11:sym+0	xorl $256,sym
81748dfc00010000	xorl $256,-4(%rbp,%rcx,4)
8133ff7f0000	xorl $0x7fff,(%rbx)
81342500000000ff7f0000 3:11:sym+0	xorl $0x7fff,sym
81748dfcff7f0000	xorl $0x7fff,-4(%rbp,%rcx,4)
813300800000	xorl $0x8000,(%rbx)
8134250000000000800000 3:11:sym+0	xorl $0x8000,sym
81748dfc00800000	xorl $0x8000,-4(%rbp,%rcx,4)
813380ff0000	xorl $0xff80,(%rbx)
8134250000000080ff0000 3:11:sym+0	xorl $0xff80,sym
81748dfc80ff0000	xorl $0xff80,-4(%rbp,%rcx,4)
8133ffff0000	xorl $0xffff,(%rbx)
81342500000000ffff0000 3:11:sym+0	xorl $0xffff,sym
81748dfcffff0000	xorl $0xffff,-4(%rbp,%rcx,4)
813300000100	xorl $0x10000,(%rbx)
8134250000000000000100 3:11:sym+0	xorl $0x10000,sym
81748dfc00000100	xorl $0x10000,-4(%rbp,%rcx,4)
8133ffffff7f	xorl $0x7fffffff,(%rbx)
81342500000000ffffff7f 3:11:sym+0	xorl $0x7fffffff,sym
81748dfcffffff7f	xorl $0x7fffffff,-4(%rbp,%rcx,4)
813300000080	xorl $0x80000000,(%rbx)
8134250000000000000080 3:11:sym+0	xorl $0x80000000,sym
81748dfc00000080	xorl $0x80000000,-4(%rbp,%rcx,4)
833380	xorl $0xffffff80,(%rbx)
8334250000000080 3:11:sym+0	xorl $0xffffff80,sym
83748dfc80	xorl $0xffffff80,-4(%rbp,%rcx,4)
8333ff	xorl $0xffffffff,(%rbx)
83342500000000ff 3:11:sym+0	xorl $0xffffffff,sym
83748dfcff	xorl $0xffffffff,-4(%rbp,%rcx,4)
813300000000 2:10:sym+0	xorl $sym,(%rbx)
8134250000000000000000 3:11:sym+0 7:10:sym+0	xorl $sym,sym
81748dfc00000000 4:10:sym+0	xorl $sym,-4(%rbp,%rcx,4)
83f301	xor $1,%ebx
refused	xorl $1,$2
refused	xorl %eax,$2
refused	xorl (%rbx),(%rbx)
f0311b	lock xorl %ebx,(%rbx)
f0833301	lock xorl $1,(%rbx)
refused	lock xorl (%rbx),%ebx
refused	lock xorl %ebx,%eax
4831c0	xorq %rax,%rax
483103	xorq %rax,(%rbx)
483303	xorq (%rbx),%rax
4831042500000000 4:11:sym+0	xorq %rax,sym
4833042500000000 4:11:sym+0	xorq sym,%rax
483103	xor %rax,(%rbx)
refused	xor 4((%rbx,%rsi,2),%rax
4883f07f	xorq $127,%rax
483580000000	xorq $128,%rax
4883f080	xorq $-128,%rax
48357fffffff	xorq $-129,%rax
4835ff000000	xorq $255,%rax
483500010000	xorq $256,%rax
4835ff7f0000	xorq $0x7fff,%rax
483500800000	xorq $0x8000,%rax
483580ff0000	xorq $0xff80,%rax
4835ffff0000	xorq $0xffff,%rax
483500000100	xorq $0x10000,%rax
refused	xorq $0xffffff80,%rax
483500000000 2:11:sym+0	xorq $sym,%rax
4831d8	xorq %rbx,%rax
4831c3	xorq %rax,%rbx
48311b	xorq %rbx,(%rbx)
48331b	xorq (%rbx),%rbx
48311c2500000000 4:11:sym+0	xorq %rbx,sym
48331c2500000000 4:11:sym+0	xorq sym,%rbx
48311b	xor %rbx,(%rbx)
refused	xor 4((%rbx,%rsi,2),%rbx
4883f300	xorq $0,%rbx
4883f301	xorq $1,%rbx
4883f3ff	xorq $-1,%rbx
4883f37f	xorq $127,%rbx
4881f380000000	xorq $128,%rbx
4883f380	xorq $-128,%rbx
4881f37fffffff	xorq $-129,%rbx
4881f3ff000000	xorq $255,%rbx
4881f300010000	xorq $256,%rbx
4881f3ff7f0000	xorq $0x7fff,%rbx
4881f300800000	xorq $0x8000,%rbx
4881f380ff0000	xorq $0xff80,%rbx
4881f3ffff0000	xorq $0xffff,%rbx
4881f300000100	xorq $0x10000,%rbx
4881f3ffffff7f	xorq $0x7fffffff,%rbx
refused	xorq $0x80000000,%rbx
refused	xorq $0xffffff80,%rbx
refused	xorq $0xffffffff,%rbx
4881f300000000 3:11:sym+0	xorq $sym,%rbx
4831e0	xorq %rsp,%rax
4831c4	xorq %rax,%rsp
483123	xorq %rsp,(%rbx)
483323	xorq (%rbx),%rsp
4831242500000000 4:11:sym+0	xorq %rsp,sym
4833242500000000 4:11:sym+0	xorq sym,%rsp
483123	xor %rsp,(%rbx)
refused	xor 4((%rbx,%rsi,2),%rsp
4883f400	xorq $0,%rsp
4883f401	xorq $1,%rsp
4883f4ff	xorq $-1,%rsp
4883f47f	xorq $127,%rsp
4881f480000000	xorq $128,%rsp
4883f480	xorq $-128,%rsp
4881f47fffffff	xorq $-129,%rsp
4881f4ff000000	xorq $255,%rsp
4881f400010000	xorq $256,%rsp
4881f4ff7f0000	xorq $0x7fff,%rsp
4881f400800000	xorq $0x8000,%rsp
4881f480ff0000	xorq $0xff80,%rsp
4881f4ffff0000	xorq $0xffff,%rsp
4881f400000100	xorq $0x10000,%rsp
4881f4ffffff7f	xorq $0x7fffffff,%rsp
refused	xorq $0x80000000,%rsp
refused	xorq $0xffffff80,%rsp
refused	xorq $0xffffffff,%rsp
4881f400000000 3:11:sym+0	xorq $sym,%rsp
4c31e0	xorq %r12,%rax
4931c4	xorq %rax,%r12
4c3123	xorq %r12,(%rbx)
4c3323	xorq (%rbx),%r12
4c31242500000000 4:11:sym+0	xorq %r12,sym
4c33242500000000 4:11:sym+0	xorq sym,%r12
4c3123	xor %r12,(%rbx)
refused	xor 4((%rbx,%rsi,2),%r12
4983f400	xorq $0,%r12
4983f401	xorq $1,%r12
4983f4ff	xorq $-1,%r12
4983f47f	xorq $127,%r12
4981f480000000	xorq $128,%r12
4983f480	xorq $-128,%r12
4981f47fffffff	xorq $-129,%r12
4981f4ff000000	xorq $255,%r12
4981f400010000	xorq $256,%r12
4981f4ff7f0000	xorq $0x7fff,%r12
4981f400800000	xorq $0x8000,%r12
4981f480ff0000	xorq $0xff80,%r12
4981f4ffff0000	xorq $0xffff,%r12
4981f400000100	xorq $0x10000,%r12
4981f4ffffff7f	xorq $0x7fffffff,%r12
refused	xorq $0x80000000,%r12
refused	xorq $0xffffff80,%r12
refused	xorq $0xffffffff,%r12
4981f400000000 3:11:sym+0	xorq $sym,%r12
4c31e8	xorq %r13,%rax
4931c5	xorq %rax,%r13
4c312b	xorq %r13,(%rbx)
4c332b	xorq (%rbx),%r13
4c312c2500000000 4:11:sym+0	xorq %r13,sym
4c332c2500000000 4:11:sym+0	xorq sym,%r13
4c312b	xor %r13,(%rbx)
refused	xor 4((%rbx,%rsi,2),%r13
4983f500	xorq $0,%r13
4983f501	xorq $1,%r13
4983f5ff	xorq $-1,%r13
4983f57f	xorq $127,%r13
4981f580000000	xorq $128,%r13
4983f580	xorq $-128,%r13
4981f57fffffff	xorq $-129,%r13
4981f5ff000000	xorq $255,%r13
4981f500010000	xorq $256,%r13
4981f5ff7f0000	xorq $0x7fff,%r13
4981f500800000	xorq $0x8000,%r13
4981f580ff0000	xorq $0xff80,%r13
4981f5ffff0000	xorq $0xffff,%r13
4981f500000100	xorq $0x10000,%r13
4981f5ffffff7f	xorq $0x7fffffff,%r13
refused	xorq $0x80000000,%r13
refused	xorq $0xffffff80,%r13
refused	xorq $0xffffffff,%r13
4981f500000000 3:11:sym+0	xorq $sym,%r13
4c31c8	xorq %r9,%rax
4931c1	xorq %rax,%r9
4c310b	xorq %r9,(%rbx)
4c330b	xorq (%rbx),%r9
4c310c2500000000 4:11:sym+0	xorq %r9,sym
4c330c2500000000 4:11:sym+0	xorq sym,%r9
4c310b	xor %r9,(%rbx)
refused	xor 4((%rbx,%rsi,2),%r9
4983f100	xorq $0,%r9
4983f101	xorq $1,%r9
4983f1ff	xorq $-1,%r9
4983f17f	xorq $127,%r9
4981f180000000	xorq $128,%r9
4983f180	xorq $-128,%r9
4981f17fffffff	xorq $-129,%r9
4981f1ff000000	xorq $255,%r9
4981f100010000	xorq $256,%r9
4981f1ff7f0000	xorq $0x7fff,%r9
4981f100800000	xorq $0x8000,%r9
4981f180ff0000	xorq $0xff80,%r9
4981f1ffff0000	xorq $0xffff,%r9
4981f100000100	xorq $0x10000,%r9
4981f1ffffff7f	xorq $0x7fffffff,%r9
refused	xorq $0x80000000,%r9
refused	xorq $0xffffff80,%r9
refused	xorq $0xffffffff,%r9
4981f100000000 3:11:sym+0	xorq $sym,%r9
48833300	xorq $0,(%rbx)
488334250000000000 4:11:sym+0	xorq $0,sym
4883748dfc00	xorq $0,-4(%rbp,%rcx,4)
48833301	xorq $1,(%rbx)
488334250000000001 4:11:sym+0	xorq $1,sym
4883748dfc01	xorq $1,-4(%rbp,%rcx,4)
488333ff	xorq $-1,(%rbx)
4883342500000000ff 4:11:sym+0	xorq $-1,sym
4883748dfcff	xorq $-1,-4(%rbp,%rcx,4)
4883337f	xorq $127,(%rbx)
48833425000000007f 4:11:sym+0	xorq $127,sym
4883748dfc7f	xorq $127,-4(%rbp,%rcx,4)
48813380000000	xorq $128,(%rbx)
488134250000000080000000 4:11:sym+0	xorq $128,sym
4881748dfc80000000	xorq $128,-4(%rbp,%rcx,4)
48833380	xorq $-128,(%rbx)
488334250000000080 4:11:sym+0	xorq $-128,sym
4883748dfc80	xorq $-128,-4(%rbp,%rcx,4)
4881337fffffff	xorq $-129,(%rbx)
48813425000000007fffffff 4:11:sym+0	xorq $-129,sym
4881748dfc7fffffff	xorq $-129,-4(%rbp,%rcx,4)
488133ff000000	xorq $255,(%rbx)
4881342500000000ff000000 4:11:sym+0	xorq $255,sym
4881748dfcff000000	xorq $255,-4(%rbp,%rcx,4)
48813300010000	xorq $256,(%rbx)
488134250000000000010000 4:11:sym+0	xorq $256,sym
4881748dfc00010000	xorq $256,-4(%rbp,%rcx,4)
488133ff7f0000	xorq $0x7fff,(%rbx)
4881342500000000ff7f0000 4:11:sym+0	xorq $0x7fff,sym
4881748dfcff7f0000	xorq $0x7fff,-4(%rbp,%rcx,4)
48813300800000	xorq $0x8000,(%rbx)
488134250000000000800000 4:11:sym+0	xorq $0x8000,sym
4881748dfc00800000	xorq $0x8000,-4(%rbp,%rcx,4)
48813380ff0000	xorq $0xff80,(%rbx)
488134250000000080ff0000 4:11:sym+0	xorq $0xff80,sym
4881748dfc80ff0000	xorq $0xff80,-4(%rbp,%rcx,4)
488133ffff0000	xorq $0xffff,(%rbx)
4881342500000000ffff0000 4:11:sym+0	xorq $0xffff,sym
4881748dfcffff0000	xorq $0xffff,-4(%rbp,%rcx,4)
48813300000100	xorq $0x10000,(%rbx)
488134250000000000000100 4:11:sym+0	xorq $0x10000,sym
4881748dfc00000100	xorq $0x10000,-4(%rbp,%rcx,4)
488133ffffff7f	xorq $0x7fffffff,(%rbx)
4881342500000000ffffff7f 4:11:sym+0	xorq $0x7fffffff,sym
4881748dfcffffff7f	xorq $0x7fffffff,-4(%rbp,%rcx,4)
refused	xorq $0x80000000,(%rbx)
refused	xorq $0x80000000,sym
refused	xorq $0x80000000,-4(%rbp,%rcx,4)
refused	xorq $0xffffff80,(%rbx)
refused	xorq $0xffffff80,sym
refused	xorq $0xffffff80,-4(%rbp,%rcx,4)
refused	xorq $0xffffffff,(%rbx)
refused	xorq $0xffffffff,sym
refused	xorq $0xffffffff,-4(%rbp,%rcx,4)
48813300000000 3:11:sym+0	xorq $sym,(%rbx)
488134250000000000000000 4:11:sym+0 8:11:sym+0	xorq $sym,sym
4881748dfc00000000 5:11:sym+0	xorq $sym,-4(%rbp,%rcx,4)
4883f301	xor $1,%rbx
refused	xorq $1,$2
refused	xorq %rax,$2
refused	xorq (%rbx),(%rbx)
f048311b	lock xorq %rbx,(%rbx)
f048833301	lock xorq $1,(%rbx)
refused	lock xorq (%rbx),%rbx
refused	lock xorq %rbx,%rax
38c0	cmpb %al,%al
3803	cmpb %al,(%rbx)
3a03	cmpb (%rbx),%al
38042500000000 3:11:sym+0	cmpb %al,sym
3a042500000000 3:11:sym+0	cmpb sym,%al
3803	cmp %al,(%rbx)
refused	cmp 4((%rbx,%rsi,2),%al
3c00	cmpb $0,%al
3c01	cmpb $1,%al
3cff	cmpb $-1,%al
3c7f	cmpb $127,%al
3c80	cmpb $128,%al
3c80	cmpb $-128,%al
3c7f	cmpb $-129,%al
3cff	cmpb $255,%al
3c00	cmpb $256,%al
3cff	cmpb $0x7fff,%al
3c00	cmpb $0x8000,%al
3c80	cmpb $0xff80,%al
3cff	cmpb $0xffff,%al
3c00	cmpb $0x10000,%al
3cff	cmpb $0x7fffffff,%al
3c00	cmpb $0x80000000,%al
3c80	cmpb $0xffffff80,%al
3cff	cmpb $0xffffffff,%al
3c00 1:14:sym+0	cmpb $sym,%al
38c8	cmpb %cl,%al
38c1	cmpb %al,%cl
380b	cmpb %cl,(%rbx)
3a0b	cmpb (%rbx),%cl
380c2500000000 3:11:sym+0	cmpb %cl,sym
3a0c2500000000 3:11:sym+0	cmpb sym,%cl
380b	cmp %cl,(%rbx)
refused	cmp 4((%rbx,%rsi,2),%cl
80f900	cmpb $0,%cl
80f901	cmpb $1,%cl
80f9ff	cmpb $-1,%cl
80f97f	cmpb $127,%cl
80f980	cmpb $128,%cl
80f980	cmpb $-128,%cl
80f97f	cmpb $-129,%cl
80f9ff	cmpb $255,%cl
80f900	cmpb $256,%cl
80f9ff	cmpb $0x7fff,%cl
80f900	cmpb $0x8000,%cl
80f980	cmpb $0xff80,%cl
80f9ff	cmpb $0xffff,%cl
80f900	cmpb $0x10000,%cl
80f9ff	cmpb $0x7fffffff,%cl
80f900	cmpb $0x80000000,%cl
80f980	cmpb $0xffffff80,%cl
80f9ff	cmpb $0xffffffff,%cl
80f900 2:14:sym+0	cmpb $sym,%cl
38f8	cmpb %bh,%al
38c7	cmpb %al,%bh
383b	cmpb %bh,(%rbx)
3a3b	cmpb (%rbx),%bh
383c2500000000 3:11:sym+0	cmpb %bh,sym
3a3c2500000000 3:11:sym+0	cmpb sym,%bh
383b	cmp %bh,(%rbx)
refused	cmp 4((%rbx,%rsi,2),%bh
80ff00	cmpb $0,%bh
80ff01	cmpb $1,%bh
80ffff	cmpb $-1,%bh
80ff7f	cmpb $127,%bh
80ff80	cmpb $128,%bh
80ff80	cmpb $-128,%bh
80ff7f	cmpb $-129,%bh
80ffff	cmpb $255,%bh
80ff00	cmpb $256,%bh
80ffff	cmpb $0x7fff,%bh
80ff00	cmpb $0x8000,%bh
80ff80	cmpb $0xff80,%bh
80ffff	cmpb $0xffff,%bh
80ff00	cmpb $0x10000,%bh
80ffff	cmpb $0x7fffffff,%bh
80ff00	cmpb $0x80000000,%bh
80ff80	cmpb $0xffffff80,%bh
80ffff	cmpb $0xffffffff,%bh
80ff00 2:14:sym+0	cmpb $sym,%bh
4038f0	cmpb %sil,%al
4038c6	cmpb %al,%sil
403833	cmpb %sil,(%rbx)
403a33	cmpb (%rbx),%sil
4038342500000000 4:11:sym+0	cmpb %sil,sym
403a342500000000 4:11:sym+0	cmpb sym,%sil
403833	cmp %sil,(%rbx)
refused	cmp 4((%rbx,%rsi,2),%sil
4080fe00	cmpb $0,%sil
4080fe01	cmpb $1,%sil
4080feff	cmpb $-1,%sil
4080fe7f	cmpb $127,%sil
4080fe80	cmpb $128,%sil
4080fe80	cmpb $-128,%sil
4080fe7f	cmpb $-129,%sil
4080feff	cmpb $255,%sil
4080fe00	cmpb $256,%sil
4080feff	cmpb $0x7fff,%sil
4080fe00	cmpb $0x8000,%sil
4080fe80	cmpb $0xff80,%sil
4080feff	cmpb $0xffff,%sil
4080fe00	cmpb $0x10000,%sil
4080feff	cmpb $0x7fffffff,%sil
4080fe00	cmpb $0x80000000,%sil
4080fe80	cmpb $0xffffff80,%sil
4080feff	cmpb $0xffffffff,%sil
4080fe00 3:14:sym+0	cmpb $sym,%sil
4438c8	cmpb %r9b,%al
4138c1	cmpb %al,%r9b
44380b	cmpb %r9b,(%rbx)
443a0b	cmpb (%rbx),%r9b
44380c2500000000 4:11:sym+0	cmpb %r9b,sym
443a0c2500000000 4:11:sym+0	cmpb sym,%r9b
44380b	cmp %r9b,(%rbx)
refused	cmp 4((%rbx,%rsi,2),%r9b
4180f900	cmpb $0,%r9b
4180f901	cmpb $1,%r9b
4180f9ff	cmpb $-1,%r9b
4180f97f	cmpb $127,%r9b
4180f980	cmpb $128,%r9b
4180f980	cmpb $-128,%r9b
4180f97f	cmpb $-129,%r9b
4180f9ff	cmpb $255,%r9b
4180f900	cmpb $256,%r9b
4180f9ff	cmpb $0x7fff,%r9b
4180f900	cmpb $0x8000,%r9b
4180f980	cmpb $0xff80,%r9b
4180f9ff	cmpb $0xffff,%r9b
4180f900	cmpb $0x10000,%r9b
4180f9ff	cmpb $0x7fffffff,%r9b
4180f900	cmpb $0x80000000,%r9b
4180f980	cmpb $0xffffff80,%r9b
4180f9ff	cmpb $0xffffffff,%r9b
4180f900 3:14:sym+0	cmpb $sym,%r9b
803b00	cmpb $0,(%rbx)
803c250000000000 3:11:sym+0	cmpb $0,sym
807c8dfc00	cmpb $0,-4(%rbp,%rcx,4)
803b01	cmpb $1,(%rbx)
803c250000000001 3:11:sym+0	cmpb $1,sym
807c8dfc01	cmpb $1,-4(%rbp,%rcx,4)
803bff	cmpb $-1,(%rbx)
803c2500000000ff 3:11:sym+0	cmpb $-1,sym
807c8dfcff	cmpb $-1,-4(%rbp,%rcx,4)
803b7f	cmpb $127,(%rbx)
803c25000000007f 3:11:sym+0	cmpb $127,sym
807c8dfc7f	cmpb $127,-4(%rbp,%rcx,4)
803b80	cmpb $128,(%rbx)
803c250000000080 3:11:sym+0	cmpb $128,sym
807c8dfc80	cmpb $128,-4(%rbp,%rcx,4)
803b80	cmpb $-128,(%rbx)
803c250000000080 3:11:sym+0	cmpb $-128,sym
807c8dfc80	cmpb $-128,-4(%rbp,%rcx,4)
803b7f	cmpb $-129,(%rbx)
803c25000000007f 3:11:sym+0	cmpb $-129,sym
807c8dfc7f	cmpb $-129,-4(%rbp,%rcx,4)
803bff	cmpb $255,(%rbx)
803c2500000000ff 3:11:sym+0	cmpb $255,sym
807c8dfcff	cmpb $255,-4(%rbp,%rcx,4)
803b00	cmpb $256,(%rbx)
803c250000000000 3:11:sym+0	cmpb $256,sym
807c8dfc00	cmpb $256,-4(%rbp,%rcx,4)
803bff	cmpb $0x7fff,(%rbx)
803c2500000000ff 3:11:sym+0	cmpb $0x7fff,sym
807c8dfcff	cmpb $0x7fff,-4(%rbp,%rcx,4)
803b00	cmpb $0x8000,(%rbx)
803c250000000000 3:11:sym+0	cmpb $0x8000,sym
807c8dfc00	cmpb $0x8000,-4(%rbp,%rcx,4)
803b80	cmpb $0xff80,(%rbx)
803c250000000080 3:11:sym+0	cmpb $0xff80,sym
807c8dfc80	cmpb $0xff80,-4(%rbp,%rcx,4)
803bff	cmpb $0xffff,(%rbx)
803c2500000000ff 3:11:sym+0	cmpb $0xffff,sym
807c8dfcff	cmpb $0xffff,-4(%rbp,%rcx,4)
803b00	cmpb $0x10000,(%rbx)
803c250000000000 3:11:sym+0	cmpb $0x10000,sym
807c8dfc00	cmpb $0x10000,-4(%rbp,%rcx,4)
803bff	cmpb $0x7fffffff,(%rbx)
803c2500000000ff 3:11:sym+0	cmpb $0x7fffffff,sym
807c8dfcff	cmpb $0x7fffffff,-4(%rbp,%rcx,4)
803b00	cmpb $0x80000000,(%rbx)
803c250000000000 3:11:sym+0	cmpb $0x80000000,sym
807c8dfc00	cmpb $0x80000000,-4(%rbp,%rcx,4)
803b80	cmpb $0xffffff80,(%rbx)
803c250000000080 3:11:sym+0	cmpb $0xffffff80,sym
807c8dfc80	cmpb $0xffffff80,-4(%rbp,%rcx,4)
803bff	cmpb $0xffffffff,(%rbx)
803c2500000000ff 3:11:sym+0	cmpb $0xffffffff,sym
807c8dfcff	cmpb $0xffffffff,-4(%rbp,%rcx,4)
803b00 2:14:sym+0	cmpb $sym,(%rbx)
803c250000000000 3:11:sym+0 7:14:sym+0	cmpb $sym,sym
807c8dfc00 4:14:sym+0	cmpb $sym,-4(%rbp,%rcx,4)
80f901	cmp $1,%cl
833b01	cmp $1,(%rbx)
refused	cmpb $1,$2
refused	cmpb %al,$2
refused	cmpb (%rbx),(%rbx)
refused	lock cmpb %cl,(%rbx)
refused	lock cmpb $1,(%rbx)
refused	lock cmpb (%rbx),%cl
refused	lock cmpb %cl,%al
6639c0	cmpw %ax,%ax
663903	cmpw %ax,(%rbx)
663b03	cmpw (%rbx),%ax
6639042500000000 4:11:sym+0	cmpw %ax,sym
663b042500000000 4:11:sym+0	cmpw sym,%ax
663903	cmp %ax,(%rbx)
refused	cmp 4((%rbx,%rsi,2),%ax
6683f800	cmpw $0,%ax
6683f801	cmpw $1,%ax
6683f8ff	cmpw $-1,%ax
6683f87f	cmpw $127,%ax
663d8000	cmpw $128,%ax
6683f880	cmpw $-128,%ax
663d7fff	cmpw $-129,%ax
663dff00	cmpw $255,%ax
663d0001	cmpw $256,%ax
663dff7f	cmpw $0x7fff,%ax
663d0080	cmpw $0x8000,%ax
6683f880	cmpw $0xff80,%ax
6683f8ff	cmpw $0xffff,%ax
663d0000	cmpw $0x10000,%ax
663dffff	cmpw $0x7fffffff,%ax
663d0000	cmpw $0x80000000,%ax
6683f880	cmpw $0xffffff80,%ax
6683f8ff	cmpw $0xffffffff,%ax
663d0000 2:12:sym+0	cmpw $sym,%ax
6639d8	cmpw %bx,%ax
6639c3	cmpw %ax,%bx
66391b	cmpw %bx,(%rbx)
663b1b	cmpw (%rbx),%bx
66391c2500000000 4:11:sym+0	cmpw %bx,sym
663b1c2500000000 4:11:sym+0	cmpw sym,%bx
66391b	cmp %bx,(%rbx)
refused	cmp 4((%rbx,%rsi,2),%bx
6683fb00	cmpw $0,%bx
6683fb01	cmpw $1,%bx
6683fbff	cmpw $-1,%bx
6683fb7f	cmpw $127,%bx
6681fb8000	cmpw $128,%bx
6683fb80	cmpw $-128,%bx
6681fb7fff	cmpw $-129,%bx
6681fbff00	cmpw $255,%bx
6681fb0001	cmpw $256,%bx
6681fbff7f	cmpw $0x7fff,%bx
6681fb0080	cmpw $0x8000,%bx
6683fb80	cmpw $0xff80,%bx
6683fbff	cmpw $0xffff,%bx
6681fb0000	cmpw $0x10000,%bx
6681fbffff	cmpw $0x7fffffff,%bx
6681fb0000	cmpw $0x80000000,%bx
6683fb80	cmpw $0xffffff80,%bx
6683fbff	cmpw $0xffffffff,%bx
6681fb0000 3:12:sym+0	cmpw $sym,%bx
6639f0	cmpw %si,%ax
6639c6	cmpw %ax,%si
663933	cmpw %si,(%rbx)
663b33	cmpw (%rbx),%si
6639342500000000 4:11:sym+0	cmpw %si,sym
663b342500000000 4:11:sym+0	cmpw sym,%si
663933	cmp %si,(%rbx)
refused	cmp 4((%rbx,%rsi,2),%si
6683fe00	cmpw $0,%si
6683fe01	cmpw $1,%si
6683feff	cmpw $-1,%si
6683fe7f	cmpw $127,%si
6681fe8000	cmpw $128,%si
6683fe80	cmpw $-128,%si
6681fe7fff	cmpw $-129,%si
6681feff00	cmpw $255,%si
6681fe0001	cmpw $256,%si
6681feff7f	cmpw $0x7fff,%si
6681fe0080	cmpw $0x8000,%si
6683fe80	cmpw $0xff80,%si
6683feff	cmpw $0xffff,%si
6681fe0000	cmpw $0x10000,%si
6681feffff	cmpw $0x7fffffff,%si
6681fe0000	cmpw $0x80000000,%si
6683fe80	cmpw $0xffffff80,%si
6683feff	cmpw $0xffffffff,%si
6681fe0000 3:12:sym+0	cmpw $sym,%si
664439c8	cmpw %r9w,%ax
664139c1	cmpw %ax,%r9w
6644390b	cmpw %r9w,(%rbx)
66443b0b	cmpw (%rbx),%r9w
6644390c2500000000 5:11:sym+0	cmpw %r9w,sym
66443b0c2500000000 5:11:sym+0	cmpw sym,%r9w
6644390b	cmp %r9w,(%rbx)
refused	cmp 4((%rbx,%rsi,2),%r9w
664183f900	cmpw $0,%r9w
664183f901	cmpw $1,%r9w
664183f9ff	cmpw $-1,%r9w
664183f97f	cmpw $127,%r9w
664181f98000	cmpw $128,%r9w
664183f980	cmpw $-128,%r9w
664181f97fff	cmpw $-129,%r9w
664181f9ff00	cmpw $255,%r9w
664181f90001	cmpw $256,%r9w
664181f9ff7f	cmpw $0x7fff,%r9w
664181f90080	cmpw $0x8000,%r9w
664183f980	cmpw $0xff80,%r9w
664183f9ff	cmpw $0xffff,%r9w
664181f90000	cmpw $0x10000,%r9w
664181f9ffff	cmpw $0x7fffffff,%r9w
664181f90000	cmpw $0x80000000,%r9w
664183f980	cmpw $0xffffff80,%r9w
664183f9ff	cmpw $0xffffffff,%r9w
664181f90000 4:12:sym+0	cmpw $sym,%r9w
66833b00	cmpw $0,(%rbx)
66833c250000000000 4:11:sym+0	cmpw $0,sym
66837c8dfc00	cmpw $0,-4(%rbp,%rcx,4)
66833b01	cmpw $1,(%rbx)
66833c250000000001 4:11:sym+0	cmpw $1,sym
66837c8dfc01	cmpw $1,-4(%rbp,%rcx,4)
66833bff	cmpw $-1,(%rbx)
66833c2500000000ff 4:11:sym+0	cmpw $-1,sym
66837c8dfcff	cmpw $-1,-4(%rbp,%rcx,4)
66833b7f	cmpw $127,(%rbx)
66833c25000000007f 4:11:sym+0	cmpw $127,sym
66837c8dfc7f	cmpw $127,-4(%rbp,%rcx,4)
66813b8000	cmpw $128,(%rbx)
66813c25000000008000 4:11:sym+0	cmpw $128,sym
66817c8dfc8000	cmpw $128,-4(%rbp,%rcx,4)
66833b80	cmpw $-128,(%rbx)
66833c250000000080 4:11:sym+0	cmpw $-128,sym
66837c8dfc80	cmpw $-128,-4(%rbp,%rcx,4)
66813b7fff	cmpw $-129,(%rbx)
66813c25000000007fff 4:11:sym+0	cmpw $-129,sym
66817c8dfc7fff	cmpw $-129,-4(%rbp,%rcx,4)
66813bff00	cmpw $255,(%rbx)
66813c2500000000ff00 4:11:sym+0	cmpw $255,sym
66817c8dfcff00	cmpw $255,-4(%rbp,%rcx,4)
66813b0001	cmpw $256,(%rbx)
66813c25000000000001 4:11:sym+0	cmpw $256,sym
66817c8dfc0001	cmpw $256,-4(%rbp,%rcx,4)
66813bff7f	cmpw $0x7fff,(%rbx)
66813c2500000000ff7f 4:11:sym+0	cmpw $0x7fff,sym
66817c8dfcff7f	cmpw $0x7fff,-4(%rbp,%rcx,4)
66813b0080	cmpw $0x8000,(%rbx)
66813c25000000000080 4:11:sym+0	cmpw $0x8000,sym
66817c8dfc0080	cmpw $0x8000,-4(%rbp,%rcx,4)
66833b80	cmpw $0xff80,(%rbx)
66833c250000000080 4:11:sym+0	cmpw $0xff80,sym
66837c8dfc80	cmpw $0xff80,-4(%rbp,%rcx,4)
66833bff	cmpw $0xffff,(%rbx)
66833c2500000000ff 4:11:sym+0	cmpw $0xffff,sym
66837c8dfcff	cmpw $0xffff,-4(%rbp,%rcx,4)
66813b0000	cmpw $0x10000,(%rbx)
66813c25000000000000 4:11:sym+0	cmpw $0x10000,sym
66817c8dfc0000	cmpw $0x10000,-4(%rbp,%rcx,4)
66813bffff	cmpw $0x7fffffff,(%rbx)
66813c2500000000ffff 4:11:sym+0	cmpw $0x7fffffff,sym
66817c8dfcffff	cmpw $0x7fffffff,-4(%rbp,%rcx,4)
66813b0000	cmpw $0x80000000,(%rbx)
66813c25000000000000 4:11:sym+0	cmpw $0x80000000,sym
66817c8dfc0000	cmpw $0x80000000,-4(%rbp,%rcx,4)
66833b80	cmpw $0xffffff80,(%rbx)
66833c250000000080 4:11:sym+0	cmpw $0xffffff80,sym
66837c8dfc80	cmpw $0xffffff80,-4(%rbp,%rcx,4)
66833bff	cmpw $0xffffffff,(%rbx)
66833c2500000000ff 4:11:sym+0	cmpw $0xffffffff,sym
66837c8dfcff	cmpw $0xffffffff,-4(%rbp,%rcx,4)
66813b0000 3:12:sym+0	cmpw $sym,(%rbx)
66813c25000000000000 4:11:sym+0 8:12:sym+0	cmpw $sym,sym
66817c8dfc0000 5:12:sym+0	cmpw $sym,-4(%rbp,%rcx,4)
6683fb01	cmp $1,%bx
refused	cmpw $1,$2
refused	cmpw %ax,$2
refused	cmpw (%rbx),(%rbx)
refused	lock cmpw %bx,(%rbx)
refused	lock cmpw $1,(%rbx)
refused	lock cmpw (%rbx),%bx
refused	lock cmpw %bx,%ax
39c0	cmpl %eax,%eax
3903	cmpl %eax,(%rbx)
3b03	cmpl (%rbx),%eax
39042500000000 3:11:sym+0	cmpl %eax,sym
3b042500000000 3:11:sym+0	cmpl sym,%eax
3903	cmp %eax,(%rbx)
refused	cmp 4((%rbx,%rsi,2),%eax
83f800	cmpl $0,%eax
83f801	cmpl $1,%eax
83f8ff	cmpl $-1,%eax
83f87f	cmpl $127,%eax
3d80000000	cmpl $128,%eax
83f880	cmpl $-128,%eax
3d7fffffff	cmpl $-129,%eax
3dff000000	cmpl $255,%eax
3d00010000	cmpl $256,%eax
3dff7f0000	cmpl $0x7fff,%eax
3d00800000	cmpl $0x8000,%eax
3d80ff0000	cmpl $0xff80,%eax
3dffff0000	cmpl $0xffff,%eax
3d00000100	cmpl $0x10000,%eax
3dffffff7f	cmpl $0x7fffffff,%eax
3d00000080	cmpl $0x80000000,%eax
83f880	cmpl $0xffffff80,%eax
83f8ff	cmpl $0xffffffff,%eax
3d00000000 1:10:sym+0	cmpl $sym,%eax
39d8	cmpl %ebx,%eax
39c3	cmpl %eax,%ebx
391b	cmpl %ebx,(%rbx)
3b1b	cmpl (%rbx),%ebx
391c2500000000 3:11:sym+0	cmpl %ebx,sym
3b1c2500000000 3:11:sym+0	cmpl sym,%ebx
391b	cmp %ebx,(%rbx)
refused	cmp 4((%rbx,%rsi,2),%ebx
83fb00	cmpl $0,%ebx
83fb01	cmpl $1,%ebx
83fbff	cmpl $-1,%ebx
83fb7f	cmpl $127,%ebx
81fb80000000	cmpl $128,%ebx
83fb80	cmpl $-128,%ebx
81fb7fffffff	cmpl $-129,%ebx
81fbff000000	cmpl $255,%ebx
81fb00010000	cmpl $256,%ebx
81fbff7f0000	cmpl $0x7fff,%ebx
81fb00800000	cmpl $0x8000,%ebx
81fb80ff0000	cmpl $0xff80,%ebx
81fbffff0000	cmpl $0xffff,%ebx
81fb00000100	cmpl $0x10000,%ebx
81fbffffff7f	cmpl $0x7fffffff,%ebx
81fb00000080	cmpl $0x80000000,%ebx
83fb80	cmpl $0xffffff80,%ebx
83fbff	cmpl $0xffffffff,%ebx
81fb00000000 2:10:sym+0	cmpl $sym,%ebx
39e0	cmpl %esp,%eax
39c4	cmpl %eax,%esp
3923	cmpl %esp,(%rbx)
3b23	cmpl (%rbx),%esp
39242500000000 3:11:sym+0	cmpl %esp,sym
3b242500000000 3:11:sym+0	cmpl sym,%esp
3923	cmp %esp,(%rbx)
refused	cmp 4((%rbx,%rsi,2),%esp
83fc00	cmpl $0,%esp
83fc01	cmpl $1,%esp
83fcff	cmpl $-1,%esp
83fc7f	cmpl $127,%esp
81fc80000000	cmpl $128,%esp
83fc80	cmpl $-128,%esp
81fc7fffffff	cmpl $-129,%esp
81fcff000000	cmpl $255,%esp
81fc00010000	cmpl $256,%esp
81fcff7f0000	cmpl $0x7fff,%esp
81fc00800000	cmpl $0x8000,%esp
81fc80ff0000	cmpl $0xff80,%esp
81fcffff0000	cmpl $0xffff,%esp
81fc00000100	cmpl $0x10000,%esp
81fcffffff7f	cmpl $0x7fffffff,%esp
81fc00000080	cmpl $0x80000000,%esp
83fc80	cmpl $0xffffff80,%esp
83fcff	cmpl $0xffffffff,%esp
81fc00000000 2:10:sym+0	cmpl $sym,%esp
39e8	cmpl %ebp,%eax
39c5	cmpl %eax,%ebp
392b	cmpl %ebp,(%rbx)
3b2b	cmpl (%rbx),%ebp
392c2500000000 3:11:sym+0	cmpl %ebp,sym
3b2c2500000000 3:11:sym+0	cmpl sym,%ebp
392b	cmp %ebp,(%rbx)
refused	cmp 4((%rbx,%rsi,2),%ebp
83fd00	cmpl $0,%ebp
83fd01	cmpl $1,%ebp
83fdff	cmpl $-1,%ebp
83fd7f	cmpl $127,%ebp
81fd80000000	cmpl $128,%ebp
83fd80	cmpl $-128,%ebp
81fd7fffffff	cmpl $-129,%ebp
81fdff000000	cmpl $255,%ebp
81fd00010000	cmpl $256,%ebp
81fdff7f0000	cmpl $0x7fff,%ebp
81fd00800000	cmpl $0x8000,%ebp
81fd80ff0000	cmpl $0xff80,%ebp
81fdffff0000	cmpl $0xffff,%ebp
81fd00000100	cmpl $0x10000,%ebp
81fdffffff7f	cmpl $0x7fffffff,%ebp
81fd00000080	cmpl $0x80000000,%ebp
83fd80	cmpl $0xffffff80,%ebp
83fdff	cmpl $0xffffffff,%ebp
81fd00000000 2:10:sym+0	cmpl $sym,%ebp
4439c8	cmpl %r9d,%eax
4139c1	cmpl %eax,%r9d
44390b	cmpl %r9d,(%rbx)
443b0b	cmpl (%rbx),%r9d
44390c2500000000 4:11:sym+0	cmpl %r9d,sym
443b0c2500000000 4:11:sym+0	cmpl sym,%r9d
44390b	cmp %r9d,(%rbx)
refused	cmp 4((%rbx,%rsi,2),%r9d
4183f900	cmpl $0,%r9d
4183f901	cmpl $1,%r9d
4183f9ff	cmpl $-1,%r9d
4183f97f	cmpl $127,%r9d
4181f980000000	cmpl $128,%r9d
4183f980	cmpl $-128,%r9d
4181f97fffffff	cmpl $-129,%r9d
4181f9ff000000	cmpl $255,%r9d
4181f900010000	cmpl $256,%r9d
4181f9ff7f0000	cmpl $0x7fff,%r9d
4181f900800000	cmpl $0x8000,%r9d
4181f980ff0000	cmpl $0xff80,%r9d
4181f9ffff0000	cmpl $0xffff,%r9d
4181f900000100	cmpl $0x10000,%r9d
4181f9ffffff7f	cmpl $0x7fffffff,%r9d
4181f900000080	cmpl $0x80000000,%r9d
4183f980	cmpl $0xffffff80,%r9d
4183f9ff	cmpl $0xffffffff,%r9d
4181f900000000 3:10:sym+0	cmpl $sym,%r9d
833b00	cmpl $0,(%rbx)
833c250000000000 3:11:sym+0	cmpl $0,sym
837c8dfc00	cmpl $0,-4(%rbp,%rcx,4)
833b01	cmpl $1,(%rbx)
833c250000000001 3:11:sym+0	cmpl $1,sym
837c8dfc01	cmpl $1,-4(%rbp,%rcx,4)
833bff	cmpl $-1,(%rbx)
833c2500000000ff 3:11:sym+0	cmpl $-1,sym
837c8dfcff	cmpl $-1,-4(%rbp,%rcx,4)
833b7f	cmpl $127,(%rbx)
833c25000000007f 3:11:sym+0	cmpl $127,sym
837c8dfc7f	cmpl $127,-4(%rbp,%rcx,4)
813b80000000	cmpl $128,(%rbx)
813c250000000080000000 3:11:sym+0	cmpl $128,sym
817c8dfc80000000	cmpl $128,-4(%rbp,%rcx,4)
833b80	cmpl $-128,(%rbx)
833c250000000080 3:11:sym+0	cmpl $-128,sym
837c8dfc80	cmpl $-128,-4(%rbp,%rcx,4)
813b7fffffff	cmpl $-129,(%rbx)
813c25000000007fffffff 3:11:sym+0	cmpl $-129,sym
817c8dfc7fffffff	cmpl $-129,-4(%rbp,%rcx,4)
813bff000000	cmpl $255,(%rbx)
813c2500000000ff000000 3:11:sym+0	cmpl $255,sym
817c8dfcff000000	cmpl $255,-4(%rbp,%rcx,4)
813b00010000	cmpl $256,(%rbx)
813c250000000000010000 3:11:sym+0	cmpl $256,sym
817c8dfc00010000	cmpl $256,-4(%rbp,%rcx,4)
813bff7f0000	cmpl $0x7fff,(%rbx)
813c2500000000ff7f0000 3:11:sym+0	cmpl $0x7fff,sym
817c8dfcff7f0000	cmpl $0x7fff,-4(%rbp,%rcx,4)
813b00800000	cmpl $0x8000,(%rbx)
813c250000000000800000 3:11:sym+0	cmpl $0x8000,sym
817c8dfc00800000	cmpl $0x8000,-4(%rbp,%rcx,4)
813b80ff0000	cmpl $0xff80,(%rbx)
813c250000000080ff0000 3:11:sym+0	cmpl $0xff80,sym
817c8dfc80ff0000	cmpl $0xff80,-4(%rbp,%rcx,4)
813bffff0000	cmpl $0xffff,(%rbx)
813c2500000000ffff0000 3:11:sym+0	cmpl $0xffff,sym
817c8dfcffff0000	cmpl $0xffff,-4(%rbp,%rcx,4)
813b00000100	cmpl $0x10000,(%rbx)
813c250000000000000100 3:11:sym+0	cmpl $0x10000,sym
817c8dfc00000100	cmpl $0x10000,-4(%rbp,%rcx,4)
813bffffff7f	cmpl $0x7fffffff,(%rbx)
813c2500000000ffffff7f 3:11:sym+0	cmpl $0x7fffffff,sym
817c8dfcffffff7f	cmpl $0x7fffffff,-4(%rbp,%rcx,4)
813b00000080	cmpl $0x80000000,(%rbx)
813c250000000000000080 3:11:sym+0	cmpl $0x80000000,sym
817c8dfc00000080	cmpl $0x80000000,-4(%rbp,%rcx,4)
833b80	cmpl $0xffffff80,(%rbx)
833c250000000080 3:11:sym+0	cmpl $0xffffff80,sym
837c8dfc80	cmpl $0xffffff80,-4(%rbp,%rcx,4)
833bff	cmpl $0xffffffff,(%rbx)
833c2500000000ff 3:11:sym+0	cmpl $0xffffffff,sym
837c8dfcff	cmpl $0xffffffff,-4(%rbp,%rcx,4)
813b00000000 2:10:sym+0	cmpl $sym,(%rbx)
813c250000000000000000 3:11:sym+0 7:10:sym+0	cmpl $sym,sym
817c8dfc00000000 4:10:sym+0	cmpl $sym,-4(%rbp,%rcx,4)
83fb01	cmp $1,%ebx
refused	cmpl $1,$2
refused	cmpl %eax,$2
refused	cmpl (%rbx),(%rbx)
refused	lock cmpl %ebx,(%rbx)
refused	lock cmpl $1,(%rbx)
refused	lock cmpl (%rbx),%ebx
refused	lock cmpl %ebx,%eax
4839c0	cmpq %rax,%rax
483903	cmpq %rax,(%rbx)
483b03	cmpq (%rbx),%rax
4839042500000000 4:11:sym+0	cmpq %rax,sym
483b042500000000 4:11:sym+0	cmpq sym,%rax
483903	cmp %rax,(%rbx)
refused	cmp 4((%rbx,%rsi,2),%rax
4883f800	cmpq $0,%rax
4883f801	cmpq $1,%rax
4883f8ff	cmpq $-1,%rax
4883f87f	cmpq $127,%rax
483d80000000	cmpq $128,%rax
4883f880	cmpq $-128,%rax
483d7fffffff	cmpq $-129,%rax
483dff000000	cmpq $255,%rax
483d00010000	cmpq $256,%rax
483dff7f0000	cmpq $0x7fff,%rax
483d00800000	cmpq $0x8000,%rax
483d80ff0000	cmpq $0xff80,%rax
483dffff0000	cmpq $0xffff,%rax
483d00000100	cmpq $0x10000,%rax
483dffffff7f	cmpq $0x7fffffff,%rax
refused	cmpq $0x80000000,%rax
refused	cmpq $0xffffff80,%rax
refused	cmpq $0xffffffff,%rax
483d00000000 2:11:sym+0	cmpq $sym,%rax
4839d8	cmpq %rbx,%rax
4839c3	cmpq %rax,%rbx
48391b	cmpq %rbx,(%rbx)
483b1b	cmpq (%rbx),%rbx
48391c2500000000 4:11:sym+0	cmpq %rbx,sym
483b1c2500000000 4:11:sym+0	cmpq sym,%rbx
48391b	cmp %rbx,(%rbx)
refused	cmp 4((%rbx,%rsi,2),%rbx
4883fb00	cmpq $0,%rbx
4883fb01	cmpq $1,%rbx
4883fbff	cmpq $-1,%rbx
4883fb7f	cmpq $127,%rbx
4881fb80000000	cmpq $128,%rbx
4883fb80	cmpq $-128,%rbx
4881fb7fffffff	cmpq $-129,%rbx
4881fbff000000	cmpq $255,%rbx
4881fb00010000	cmpq $256,%rbx
4881fbff7f0000	cmpq $0x7fff,%rbx
4881fb00800000	cmpq $0x8000,%rbx
4881fb80ff0000	cmpq $0xff80,%rbx
4881fbffff0000	cmpq $0xffff,%rbx
4881fb00000100	cmpq $0x10000,%rbx
4881fbffffff7f	cmpq $0x7fffffff,%rbx
refused	cmpq $0x80000000,%rbx
refused	cmpq $0xffffff80,%rbx
refused	cmpq $0xffffffff,%rbx
4881fb00000000 3:11:sym+0	cmpq $sym,%rbx
4839e0	cmpq %rsp,%rax
4839c4	cmpq %rax,%rsp
483923	cmpq %rsp,(%rbx)
483b23	cmpq (%rbx),%rsp
4839242500000000 4:11:sym+0	cmpq %rsp,sym
483b242500000000 4:11:sym+0	cmpq sym,%rsp
483923	cmp %rsp,(%rbx)
refused	cmp 4((%rbx,%rsi,2),%rsp
4883fc00	cmpq $0,%rsp
4883fc01	cmpq $1,%rsp
4883fcff	cmpq $-1,%rsp
4883fc7f	cmpq $127,%rsp
4881fc80000000	cmpq $128,%rsp
4883fc80	cmpq $-128,%rsp
4881fc7fffffff	cmpq $-129,%rsp
4881fcff000000	cmpq $255,%rsp
4881fc00010000	cmpq $256,%rsp
4881fcff7f0000	cmpq $0x7fff,%rsp
4881fc00800000	cmpq $0x8000,%rsp
4881fc80ff0000	cmpq $0xff80,%rsp
4881fcffff0000	cmpq $0xffff,%rsp
4881fc00000100	cmpq $0x10000,%rsp
4881fcffffff7f	cmpq $0x7fffffff,%rsp
refused	cmpq $0x80000000,%rsp
refused	cmpq $0xffffff80,%rsp
refused	cmpq $0xffffffff,%rsp
4881fc00000000 3:11:sym+0	cmpq $sym,%rsp
4c39e0	cmpq %r12,%rax
4939c4	cmpq %rax,%r12
4c3923	cmpq %r12,(%rbx)
4c3b23	cmpq (%rbx),%r12
4c39242500000000 4:11:sym+0	cmpq %r12,sym
4c3b242500000000 4:11:sym+0	cmpq sym,%r12
4c3923	cmp %r12,(%rbx)
refused	cmp 4((%rbx,%rsi,2),%r12
4983fc00	cmpq $0,%r12
4983fc01	cmpq $1,%r12
4983fcff	cmpq $-1,%r12
4983fc7f	cmpq $127,%r12
4981fc80000000	cmpq $128,%r12
4983fc80	cmpq $-128,%r12
4981fc7fffffff	cmpq $-129,%r12
4981fcff000000	cmpq $255,%r12
4981fc00010000	cmpq $256,%r12
4981fcff7f0000	cmpq $0x7fff,%r12
4981fc00800000	cmpq $0x8000,%r12
4981fc80ff0000	cmpq $0xff80,%r12
4981fcffff0000	cmpq $0xffff,%r12
4981fc00000100	cmpq $0x10000,%r12
4981fcffffff7f	cmpq $0x7fffffff,%r12
refused	cmpq $0x80000000,%r12
refused	cmpq $0xffffff80,%r12
refused	cmpq $0xffffffff,%r12
4981fc00000000 3:11:sym+0	cmpq $sym,%r12
4c39e8	cmpq %r13,%rax
4939c5	cmpq %rax,%r13
4c392b	cmpq %r13,(%rbx)
4c3b2b	cmpq (%rbx),%r13
4c392c2500000000 4:11:sym+0	cmpq %r13,sym
4c3b2c2500000000 4:11:sym+0	cmpq sym,%r13
4c392b	cmp %r13,(%rbx)
refused	cmp 4((%rbx,%rsi,2),%r13
4983fd00	cmpq $0,%r13
4983fd01	cmpq $1,%r13
4983fdff	cmpq $-1,%r13
4983fd7f	cmpq $127,%r13
4981fd80000000	cmpq $128,%r13
4983fd80	cmpq $-128,%r13
4981fd7fffffff	cmpq $-129,%r13
4981fdff000000	cmpq $255,%r13
4981fd00010000	cmpq $256,%r13
4981fdff7f0000	cmpq $0x7fff,%r13
4981fd00800000	cmpq $0x8000,%r13
4981fd80ff0000	cmpq $0xff80,%r13
4981fdffff0000	cmpq $0xffff,%r13
4981fd00000100	cmpq $0x10000,%r13
4981fdffffff7f	cmpq $0x7fffffff,%r13
refused	cmpq $0x80000000,%r13
refused	cmpq $0xffffff80,%r13
refused	cmpq $0xffffffff,%r13
4981fd00000000 3:11:sym+0	cmpq $sym,%r13
4c39c8	cmpq %r9,%rax
4939c1	cmpq %rax,%r9
4c390b	cmpq %r9,(%rbx)
4c3b0b	cmpq (%rbx),%r9
4c390c2500000000 4:11:sym+0	cmpq %r9,sym
4c3b0c2500000000 4:11:sym+0	cmpq sym,%r9
4c390b	cmp %r9,(%rbx)
refused	cmp 4((%rbx,%rsi,2),%r9
4983f900	cmpq $0,%r9
4983f901	cmpq $1,%r9
4983f9ff	cmpq $-1,%r9
4983f97f	cmpq $127,%r9
4981f980000000	cmpq $128,%r9
4983f980	cmpq $-128,%r9
4981f97fffffff	cmpq $-129,%r9
4981f9ff000000	cmpq $255,%r9
4981f900010000	cmpq $256,%r9
4981f9ff7f0000	cmpq $0x7fff,%r9
4981f900800000	cmpq $0x8000,%r9
4981f980ff0000	cmpq $0xff80,%r9
4981f9ffff0000	cmpq $0xffff,%r9
4981f900000100	cmpq $0x10000,%r9
4981f9ffffff7f	cmpq $0x7fffffff,%r9
refused	cmpq $0x80000000,%r9
refused	cmpq $0xffffff80,%r9
refused	cmpq $0xffffffff,%r9
4981f900000000 3:11:sym+0	cmpq $sym,%r9
48833c250000000000 4:11:sym+0	cmpq $0,sym
48837c8dfc00	cmpq $0,-4(%rbp,%rcx,4)
48833c250000000001 4:11:sym+0	cmpq $1,sym
48837c8dfc01	cmpq $1,-4(%rbp,%rcx,4)
48833c2500000000ff 4:11:sym+0	cmpq $-1,sym
48837c8dfcff	cmpq $-1,-4(%rbp,%rcx,4)
48833b7f	cmpq $127,(%rbx)
48833c25000000007f 4:11:sym+0	cmpq $127,sym
48837c8dfc7f	cmpq $127,-4(%rbp,%rcx,4)
48813b80000000	cmpq $128,(%rbx)
48813c250000000080000000 4:11:sym+0	cmpq $128,sym
48817c8dfc80000000	cmpq $128,-4(%rbp,%rcx,4)
48833b80	cmpq $-128,(%rbx)
48833c250000000080 4:11:sym+0	cmpq $-128,sym
48837c8dfc80	cmpq $-128,-4(%rbp,%rcx,4)
48813b7fffffff	cmpq $-129,(%rbx)
48813c25000000007fffffff 4:11:sym+0	cmpq $-129,sym
48817c8dfc7fffffff	cmpq $-129,-4(%rbp,%rcx,4)
48813bff000000	cmpq $255,(%rbx)
48813c2500000000ff000000 4:11:sym+0	cmpq $255,sym
48817c8dfcff000000	cmpq $255,-4(%rbp,%rcx,4)
48813b00010000	cmpq $256,(%rbx)
48813c250000000000010000 4:11:sym+0	cmpq $256,sym
48817c8dfc00010000	cmpq $256,-4(%rbp,%rcx,4)
48813bff7f0000	cmpq $0x7fff,(%rbx)
48813c2500000000ff7f0000 4:11:sym+0	cmpq $0x7fff,sym
48817c8dfcff7f0000	cmpq $0x7fff,-4(%rbp,%rcx,4)
48813b00800000	cmpq $0x8000,(%rbx)
48813c250000000000800000 4:11:sym+0	cmpq $0x8000,sym
48817c8dfc00800000	cmpq $0x8000,-4(%rbp,%rcx,4)
48813b80ff0000	cmpq $0xff80,(%rbx)
48813c250000000080ff0000 4:11:sym+0	cmpq $0xff80,sym
48817c8dfc80ff0000	cmpq $0xff80,-4(%rbp,%rcx,4)
48813bffff0000	cmpq $0xffff,(%rbx)
48813c2500000000ffff0000 4:11:sym+0	cmpq $0xffff,sym
48817c8dfcffff0000	cmpq $0xffff,-4(%rbp,%rcx,4)
48813b00000100	cmpq $0x10000,(%rbx)
48813c250000000000000100 4:11:sym+0	cmpq $0x10000,sym
48817c8dfc00000100	cmpq $0x10000,-4(%rbp,%rcx,4)
48813c2500000000ffffff7f 4:11:sym+0	cmpq $0x7fffffff,sym
48817c8dfcffffff7f	cmpq $0x7fffffff,-4(%rbp,%rcx,4)
refused	cmpq $0x80000000,sym
refused	cmpq $0x80000000,-4(%rbp,%rcx,4)
refused	cmpq $0xffffff80,(%rbx)
refused	cmpq $0xffffff80,sym
refused	cmpq $0xffffff80,-4(%rbp,%rcx,4)
refused	cmpq $0xffffffff,sym
refused	cmpq $0xffffffff,-4(%rbp,%rcx,4)
48813b00000000 3:11:sym+0	cmpq $sym,(%rbx)
48813c250000000000000000 4:11:sym+0 8:11:sym+0	cmpq $sym,sym
48817c8dfc00000000 5:11:sym+0	cmpq $sym,-4(%rbp,%rcx,4)
4883fb01	cmp $1,%rbx
refused	cmpq $1,$2
refused	cmpq %rax,$2
refused	cmpq (%rbx),(%rbx)
refused	lock cmpq %rbx,(%rbx)
refused	lock cmpq $1,(%rbx)
refused	lock cmpq (%rbx),%rbx
refused	lock cmpq %rbx,%rax
84c0	testb %al,%al
8403	testb %al,(%rbx)
8403	testb (%rbx),%al
84042500000000 3:11:sym+0	testb %al,sym
84042500000000 3:11:sym+0	testb sym,%al
8403	test %al,(%rbx)
refused	test 4((%rbx,%rsi,2),%al
a800	testb $0,%al
a801	testb $1,%al
a8ff	testb $-1,%al
a87f	testb $127,%al
a880	testb $128,%al
a880	testb $-128,%al
a87f	testb $-129,%al
a8ff	testb $255,%al
a800	testb $256,%al
a8ff	testb $0x7fff,%al
a800	testb $0x8000,%al
a880	testb $0xff80,%al
a8ff	testb $0xffff,%al
a800	testb $0x10000,%al
a8ff	testb $0x7fffffff,%al
a800	testb $0x80000000,%al
a880	testb $0xffffff80,%al
a8ff	testb $0xffffffff,%al
a800 1:14:sym+0	testb $sym,%al
84c8	testb %cl,%al
84c1	testb %al,%cl
840b	testb %cl,(%rbx)
840b	testb (%rbx),%cl
840c2500000000 3:11:sym+0	testb %cl,sym
840c2500000000 3:11:sym+0	testb sym,%cl
840b	test %cl,(%rbx)
refused	test 4((%rbx,%rsi,2),%cl
f6c100	testb $0,%cl
f6c101	testb $1,%cl
f6c1ff	testb $-1,%cl
f6c17f	testb $127,%cl
f6c180	testb $128,%cl
f6c180	testb $-128,%cl
f6c17f	testb $-129,%cl
f6c1ff	testb $255,%cl
f6c100	testb $256,%cl
f6c1ff	testb $0x7fff,%cl
f6c100	testb $0x8000,%cl
f6c180	testb $0xff80,%cl
f6c1ff	testb $0xffff,%cl
f6c100	testb $0x10000,%cl
f6c1ff	testb $0x7fffffff,%cl
f6c100	testb $0x80000000,%cl
f6c180	testb $0xffffff80,%cl
f6c1ff	testb $0xffffffff,%cl
f6c100 2:14:sym+0	testb $sym,%cl
84f8	testb %bh,%al
84c7	testb %al,%bh
843b	testb %bh,(%rbx)
843b	testb (%rbx),%bh
843c2500000000 3:11:sym+0	testb %bh,sym
843c2500000000 3:11:sym+0	testb sym,%bh
843b	test %bh,(%rbx)
refused	test 4((%rbx,%rsi,2),%bh
f6c700	testb $0,%bh
f6c701	testb $1,%bh
f6c7ff	testb $-1,%bh
f6c77f	testb $127,%bh
f6c780	testb $128,%bh
f6c780	testb $-128,%bh
f6c77f	testb $-129,%bh
f6c7ff	testb $255,%bh
f6c700	testb $256,%bh
f6c7ff	testb $0x7fff,%bh
f6c700	testb $0x8000,%bh
f6c780	testb $0xff80,%bh
f6c7ff	testb $0xffff,%bh
f6c700	testb $0x10000,%bh
f6c7ff	testb $0x7fffffff,%bh
f6c700	testb $0x80000000,%bh
f6c780	testb $0xffffff80,%bh
f6c7ff	testb $0xffffffff,%bh
f6c700 2:14:sym+0	testb $sym,%bh
4084f0	testb %sil,%al
4084c6	testb %al,%sil
408433	testb %sil,(%rbx)
408433	testb (%rbx),%sil
4084342500000000 4:11:sym+0	testb %sil,sym
4084342500000000 4:11:sym+0	testb sym,%sil
408433	test %sil,(%rbx)
refused	test 4((%rbx,%rsi,2),%sil
40f6c600	testb $0,%sil
40f6c601	testb $1,%sil
40f6c6ff	testb $-1,%sil
40f6c67f	testb $127,%sil
40f6c680	testb $128,%sil
40f6c680	testb $-128,%sil
40f6c67f	testb $-129,%sil
40f6c6ff	testb $255,%sil
40f6c600	testb $256,%sil
40f6c6ff	testb $0x7fff,%sil
40f6c600	testb $0x8000,%sil
40f6c680	testb $0xff80,%sil
40f6c6ff	testb $0xffff,%sil
40f6c600	testb $0x10000,%sil
40f6c6ff	testb $0x7fffffff,%sil
40f6c600	testb $0x80000000,%sil
40f6c680	testb $0xffffff80,%sil
40f6c6ff	testb $0xffffffff,%sil
40f6c600 3:14:sym+0	testb $sym,%sil
4484c8	testb %r9b,%al
4184c1	testb %al,%r9b
44840b	testb %r9b,(%rbx)
44840b	testb (%rbx),%r9b
44840c2500000000 4:11:sym+0	testb %r9b,sym
44840c2500000000 4:11:sym+0	testb sym,%r9b
44840b	test %r9b,(%rbx)
refused	test 4((%rbx,%rsi,2),%r9b
41f6c100	testb $0,%r9b
41f6c101	testb $1,%r9b
41f6c1ff	testb $-1,%r9b
41f6c17f	testb $127,%r9b
41f6c180	testb $128,%r9b
41f6c180	testb $-128,%r9b
41f6c17f	testb $-129,%r9b
41f6c1ff	testb $255,%r9b
41f6c100	testb $256,%r9b
41f6c1ff	testb $0x7fff,%r9b
41f6c100	testb $0x8000,%r9b
41f6c180	testb $0xff80,%r9b
41f6c1ff	testb $0xffff,%r9b
41f6c100	testb $0x10000,%r9b
41f6c1ff	testb $0x7fffffff,%r9b
41f6c100	testb $0x80000000,%r9b
41f6c180	testb $0xffffff80,%r9b
41f6c1ff	testb $0xffffffff,%r9b
41f6c100 3:14:sym+0	testb $sym,%r9b
f60300	testb $0,(%rbx)
f604250000000000 3:11:sym+0	testb $0,sym
f6448dfc00	testb $0,-4(%rbp,%rcx,4)
f60301	testb $1,(%rbx)
f604250000000001 3:11:sym+0	testb $1,sym
f6448dfc01	testb $1,-4(%rbp,%rcx,4)
f603ff	testb $-1,(%rbx)
f6042500000000ff 3:11:sym+0	testb $-1,sym
f6448dfcff	testb $-1,-4(%rbp,%rcx,4)
f6037f	testb $127,(%rbx)
f60425000000007f 3:11:sym+0	testb $127,sym
f6448dfc7f	testb $127,-4(%rbp,%rcx,4)
f60380	testb $128,(%rbx)
f604250000000080 3:11:sym+0	testb $128,sym
f6448dfc80	testb $128,-4(%rbp,%rcx,4)
f60380	testb $-128,(%rbx)
f604250000000080 3:11:sym+0	testb $-128,sym
f6448dfc80	testb $-128,-4(%rbp,%rcx,4)
f6037f	testb $-129,(%rbx)
f60425000000007f 3:11:sym+0	testb $-129,sym
f6448dfc7f	testb $-129,-4(%rbp,%rcx,4)
f603ff	testb $255,(%rbx)
f6042500000000ff 3:11:sym+0	testb $255,sym
f6448dfcff	testb $255,-4(%rbp,%rcx,4)
f60300	testb $256,(%rbx)
f604250000000000 3:11:sym+0	testb $256,sym
f6448dfc00	testb $256,-4(%rbp,%rcx,4)
f603ff	testb $0x7fff,(%rbx)
f6042500000000ff 3:11:sym+0	testb $0x7fff,sym
f6448dfcff	testb $0x7fff,-4(%rbp,%rcx,4)
f60300	testb $0x8000,(%rbx)
f604250000000000 3:11:sym+0	testb $0x8000,sym
f6448dfc00	testb $0x8000,-4(%rbp,%rcx,4)
f60380	testb $0xff80,(%rbx)
f604250000000080 3:11:sym+0	testb $0xff80,sym
f6448dfc80	testb $0xff80,-4(%rbp,%rcx,4)
f603ff	testb $0xffff,(%rbx)
f6042500000000ff 3:11:sym+0	testb $0xffff,sym
f6448dfcff	testb $0xffff,-4(%rbp,%rcx,4)
f60300	testb $0x10000,(%rbx)
f604250000000000 3:11:sym+0	testb $0x10000,sym
f6448dfc00	testb $0x10000,-4(%rbp,%rcx,4)
f603ff	testb $0x7fffffff,(%rbx)
f6042500000000ff 3:11:sym+0	testb $0x7fffffff,sym
f6448dfcff	testb $0x7fffffff,-4(%rbp,%rcx,4)
f60300	testb $0x80000000,(%rbx)
f604250000000000 3:11:sym+0	testb $0x80000000,sym
f6448dfc00	testb $0x80000000,-4(%rbp,%rcx,4)
f60380	testb $0xffffff80,(%rbx)
f604250000000080 3:11:sym+0	testb $0xffffff80,sym
f6448dfc80	testb $0xffffff80,-4(%rbp,%rcx,4)
f603ff	testb $0xffffffff,(%rbx)
f6042500000000ff 3:11:sym+0	testb $0xffffffff,sym
f6448dfcff	testb $0xffffffff,-4(%rbp,%rcx,4)
f60300 2:14:sym+0	testb $sym,(%rbx)
f604250000000000 3:11:sym+0 7:14:sym+0	testb $sym,sym
f6448dfc00 4:14:sym+0	testb $sym,-4(%rbp,%rcx,4)
f6c101	test $1,%cl
f70301000000	test $1,(%rbx)
refused	testb $1,$2
refused	testb %al,$2
refused	testb (%rbx),(%rbx)
refused	lock testb %cl,(%rbx)
refused	lock testb $1,(%rbx)
refused	lock testb (%rbx),%cl
refused	lock testb %cl,%al
6685c0	testw %ax,%ax
668503	testw %ax,(%rbx)
668503	testw (%rbx),%ax
6685042500000000 4:11:sym+0	testw %ax,sym
6685042500000000 4:11:sym+0	testw sym,%ax
668503	test %ax,(%rbx)
refused	test 4((%rbx,%rsi,2),%ax
66a90000	testw $0,%ax
66a90100	testw $1,%ax
66a9ffff	testw $-1,%ax
66a97f00	testw $127,%ax
66a98000	testw $128,%ax
66a980ff	testw $-128,%ax
66a97fff	testw $-129,%ax
66a9ff00	testw $255,%ax
66a90001	testw $256,%ax
66a9ff7f	testw $0x7fff,%ax
66a90080	testw $0x8000,%ax
66a980ff	testw $0xff80,%ax
66a9ffff	testw $0xffff,%ax
66a90000	testw $0x10000,%ax
66a9ffff	testw $0x7fffffff,%ax
66a90000	testw $0x80000000,%ax
66a980ff	testw $0xffffff80,%ax
66a9ffff	testw $0xffffffff,%ax
66a90000 2:12:sym+0	testw $sym,%ax
6685d8	testw %bx,%ax
6685c3	testw %ax,%bx
66851b	testw %bx,(%rbx)
66851b	testw (%rbx),%bx
66851c2500000000 4:11:sym+0	testw %bx,sym
66851c2500000000 4:11:sym+0	testw sym,%bx
66851b	test %bx,(%rbx)
refused	test 4((%rbx,%rsi,2),%bx
66f7c30000	testw $0,%bx
66f7c30100	testw $1,%bx
66f7c3ffff	testw $-1,%bx
66f7c37f00	testw $127,%bx
66f7c38000	testw $128,%bx
66f7c380ff	testw $-128,%bx
66f7c37fff	testw $-129,%bx
66f7c3ff00	testw $255,%bx
66f7c30001	testw $256,%bx
66f7c3ff7f	testw $0x7fff,%bx
66f7c30080	testw $0x8000,%bx
66f7c380ff	testw $0xff80,%bx
66f7c3ffff	testw $0xffff,%bx
66f7c30000	testw $0x10000,%bx
66f7c3ffff	testw $0x7fffffff,%bx
66f7c30000	testw $0x80000000,%bx
66f7c380ff	testw $0xffffff80,%bx
66f7c3ffff	testw $0xffffffff,%bx
66f7c30000 3:12:sym+0	testw $sym,%bx
6685f0	testw %si,%ax
6685c6	testw %ax,%si
668533	testw %si,(%rbx)
668533	testw (%rbx),%si
6685342500000000 4:11:sym+0	testw %si,sym
6685342500000000 4:11:sym+0	testw sym,%si
668533	test %si,(%rbx)
refused	test 4((%rbx,%rsi,2),%si
66f7c60000	testw $0,%si
66f7c60100	testw $1,%si
66f7c6ffff	testw $-1,%si
66f7c67f00	testw $127,%si
66f7c68000	testw $128,%si
66f7c680ff	testw $-128,%si
66f7c67fff	testw $-129,%si
66f7c6ff00	testw $255,%si
66f7c60001	testw $256,%si
66f7c6ff7f	testw $0x7fff,%si
66f7c60080	testw $0x8000,%si
66f7c680ff	testw $0xff80,%si
66f7c6ffff	testw $0xffff,%si
66f7c60000	testw $0x10000,%si
66f7c6ffff	testw $0x7fffffff,%si
66f7c60000	testw $0x80000000,%si
66f7c680ff	testw $0xffffff80,%si
66f7c6ffff	testw $0xffffffff,%si
66f7c60000 3:12:sym+0	testw $sym,%si
664485c8	testw %r9w,%ax
664185c1	testw %ax,%r9w
6644850b	testw %r9w,(%rbx)
6644850b	testw (%rbx),%r9w
6644850c2500000000 5:11:sym+0	testw %r9w,sym
6644850c2500000000 5:11:sym+0	testw sym,%r9w
6644850b	test %r9w,(%rbx)
refused	test 4((%rbx,%rsi,2),%r9w
6641f7c10000	testw $0,%r9w
6641f7c10100	testw $1,%r9w
6641f7c1ffff	testw $-1,%r9w
6641f7c17f00	testw $127,%r9w
6641f7c18000	testw $128,%r9w
6641f7c180ff	testw $-128,%r9w
6641f7c17fff	testw $-129,%r9w
6641f7c1ff00	testw $255,%r9w
6641f7c10001	testw $256,%r9w
6641f7c1ff7f	testw $0x7fff,%r9w
6641f7c10080	testw $0x8000,%r9w
6641f7c180ff	testw $0xff80,%r9w
6641f7c1ffff	testw $0xffff,%r9w
6641f7c10000	testw $0x10000,%r9w
6641f7c1ffff	testw $0x7fffffff,%r9w
6641f7c10000	testw $0x80000000,%r9w
6641f7c180ff	testw $0xffffff80,%r9w
6641f7c1ffff	testw $0xffffffff,%r9w
6641f7c10000 4:12:sym+0	testw $sym,%r9w
66f7030000	testw $0,(%rbx)
66f70425000000000000 4:11:sym+0	testw $0,sym
66f7448dfc0000	testw $0,-4(%rbp,%rcx,4)
66f7030100	testw $1,(%rbx)
66f70425000000000100 4:11:sym+0	testw $1,sym
66f7448dfc0100	testw $1,-4(%rbp,%rcx,4)
66f703ffff	testw $-1,(%rbx)
66f7042500000000ffff 4:11:sym+0	testw $-1,sym
66f7448dfcffff	testw $-1,-4(%rbp,%rcx,4)
66f7037f00	testw $127,(%rbx)
66f70425000000007f00 4:11:sym+0	testw $127,sym
66f7448dfc7f00	testw $127,-4(%rbp,%rcx,4)
66f7038000	testw $128,(%rbx)
66f70425000000008000 4:11:sym+0	testw $128,sym
66f7448dfc8000	testw $128,-4(%rbp,%rcx,4)
66f70380ff	testw $-128,(%rbx)
66f704250000000080ff 4:11:sym+0	testw $-128,sym
66f7448dfc80ff	testw $-128,-4(%rbp,%rcx,4)
66f7037fff	testw $-129,(%rbx)
66f70425000000007fff 4:11:sym+0	testw $-129,sym
66f7448dfc7fff	testw $-129,-4(%rbp,%rcx,4)
66f703ff00	testw $255,(%rbx)
66f7042500000000ff00 4:11:sym+0	testw $255,sym
66f7448dfcff00	testw $255,-4(%rbp,%rcx,4)
66f7030001	testw $256,(%rbx)
66f70425000000000001 4:11:sym+0	testw $256,sym
66f7448dfc0001	testw $256,-4(%rbp,%rcx,4)
66f703ff7f	testw $0x7fff,(%rbx)
66f7042500000000ff7f 4:11:sym+0	testw $0x7fff,sym
66f7448dfcff7f	testw $0x7fff,-4(%rbp,%rcx,4)
66f7030080	testw $0x8000,(%rbx)
66f70425000000000080 4:11:sym+0	testw $0x8000,sym
66f7448dfc0080	testw $0x8000,-4(%rbp,%rcx,4)
66f70380ff	testw $0xff80,(%rbx)
66f704250000000080ff 4:11:sym+0	testw $0xff80,sym
66f7448dfc80ff	testw $0xff80,-4(%rbp,%rcx,4)
66f703ffff	testw $0xffff,(%rbx)
66f7042500000000ffff 4:11:sym+0	testw $0xffff,sym
66f7448dfcffff	testw $0xffff,-4(%rbp,%rcx,4)
66f7030000	testw $0x10000,(%rbx)
66f70425000000000000 4:11:sym+0	testw $0x10000,sym
66f7448dfc0000	testw $0x10000,-4(%rbp,%rcx,4)
66f703ffff	testw $0x7fffffff,(%rbx)
66f7042500000000ffff 4:11:sym+0	testw $0x7fffffff,sym
66f7448dfcffff	testw $0x7fffffff,-4(%rbp,%rcx,4)
66f7030000	testw $0x80000000,(%rbx)
66f70425000000000000 4:11:sym+0	testw $0x80000000,sym
66f7448dfc0000	testw $0x80000000,-4(%rbp,%rcx,4)
66f70380ff	testw $0xffffff80,(%rbx)
66f704250000000080ff 4:11:sym+0	testw $0xffffff80,sym
66f7448dfc80ff	testw $0xffffff80,-4(%rbp,%rcx,4)
66f703ffff	testw $0xffffffff,(%rbx)
66f7042500000000ffff 4:11:sym+0	testw $0xffffffff,sym
66f7448dfcffff	testw $0xffffffff,-4(%rbp,%rcx,4)
66f7030000 3:12:sym+0	testw $sym,(%rbx)
66f70425000000000000 4:11:sym+0 8:12:sym+0	testw $sym,sym
66f7448dfc0000 5:12:sym+0	testw $sym,-4(%rbp,%rcx,4)
66f7c30100	test $1,%bx
refused	testw $1,$2
refused	testw %ax,$2
refused	testw (%rbx),(%rbx)
refused	lock testw %bx,(%rbx)
refused	lock testw $1,(%rbx)
refused	lock testw (%rbx),%bx
refused	lock testw %bx,%ax
85c0	testl %eax,%eax
8503	testl %eax,(%rbx)
8503	testl (%rbx),%eax
85042500000000 3:11:sym+0	testl %eax,sym
85042500000000 3:11:sym+0	testl sym,%eax
8503	test %eax,(%rbx)
refused	test 4((%rbx,%rsi,2),%eax
a900000000	testl $0,%eax
a901000000	testl $1,%eax
a9ffffffff	testl $-1,%eax
a97f000000	testl $127,%eax
a980000000	testl $128,%eax
a980ffffff	testl $-128,%eax
a97fffffff	testl $-129,%eax
a9ff000000	testl $255,%eax
a900010000	testl $256,%eax
a9ff7f0000	testl $0x7fff,%eax
a900800000	testl $0x8000,%eax
a980ff0000	testl $0xff80,%eax
a9ffff0000	testl $0xffff,%eax
a900000100	testl $0x10000,%eax
a9ffffff7f	testl $0x7fffffff,%eax
a900000080	testl $0x80000000,%eax
a980ffffff	testl $0xffffff80,%eax
a9ffffffff	testl $0xffffffff,%eax
a900000000 1:10:sym+0	testl $sym,%eax
85d8	testl %ebx,%eax
85c3	testl %eax,%ebx
851b	testl %ebx,(%rbx)
851b	testl (%rbx),%ebx
851c2500000000 3:11:sym+0	testl %ebx,sym
851c2500000000 3:11:sym+0	testl sym,%ebx
851b	test %ebx,(%rbx)
refused	test 4((%rbx,%rsi,2),%ebx
f7c300000000	testl $0,%ebx
f7c301000000	testl $1,%ebx
f7c3ffffffff	testl $-1,%ebx
f7c37f000000	testl $127,%ebx
f7c380000000	testl $128,%ebx
f7c380ffffff	testl $-128,%ebx
f7c37fffffff	testl $-129,%ebx
f7c3ff000000	testl $255,%ebx
f7c300010000	testl $256,%ebx
f7c3ff7f0000	testl $0x7fff,%ebx
f7c300800000	testl $0x8000,%ebx
f7c380ff0000	testl $0xff80,%ebx
f7c3ffff0000	testl $0xffff,%ebx
f7c300000100	testl $0x10000,%ebx
f7c3ffffff7f	testl $0x7fffffff,%ebx
f7c300000080	testl $0x80000000,%ebx
f7c380ffffff	testl $0xffffff80,%ebx
f7c3ffffffff	testl $0xffffffff,%ebx
f7c300000000 2:10:sym+0	testl $sym,%ebx
85e0	testl %esp,%eax
85c4	testl %eax,%esp
8523	testl %esp,(%rbx)
8523	testl (%rbx),%esp
85242500000000 3:11:sym+0	testl %esp,sym
85242500000000 3:11:sym+0	testl sym,%esp
8523	test %esp,(%rbx)
refused	test 4((%rbx,%rsi,2),%esp
f7c400000000	testl $0,%esp
f7c401000000	testl $1,%esp
f7c4ffffffff	testl $-1,%esp
f7c47f000000	testl $127,%esp
f7c480000000	testl $128,%esp
f7c480ffffff	testl $-128,%esp
f7c47fffffff	testl $-129,%esp
f7c4ff000000	testl $255,%esp
f7c400010000	testl $256,%esp
f7c4ff7f0000	testl $0x7fff,%esp
f7c400800000	testl $0x8000,%esp
f7c480ff0000	testl $0xff80,%esp
f7c4ffff0000	testl $0xffff,%esp
f7c400000100	testl $0x10000,%esp
f7c4ffffff7f	testl $0x7fffffff,%esp
f7c400000080	testl $0x80000000,%esp
f7c480ffffff	testl $0xffffff80,%esp
f7c4ffffffff	testl $0xffffffff,%esp
f7c400000000 2:10:sym+0	testl $sym,%esp
85e8	testl %ebp,%eax
85c5	testl %eax,%ebp
852b	testl %ebp,(%rbx)
852b	testl (%rbx),%ebp
852c2500000000 3:11:sym+0	testl %ebp,sym
852c2500000000 3:11:sym+0	testl sym,%ebp
852b	test %ebp,(%rbx)
refused	test 4((%rbx,%rsi,2),%ebp
f7c500000000	testl $0,%ebp
f7c501000000	testl $1,%ebp
f7c5ffffffff	testl $-1,%ebp
f7c57f000000	testl $127,%ebp
f7c580000000	testl $128,%ebp
f7c580ffffff	testl $-128,%ebp
f7c57fffffff	testl $-129,%ebp
f7c5ff000000	testl $255,%ebp
f7c500010000	testl $256,%ebp
f7c5ff7f0000	testl $0x7fff,%ebp
f7c500800000	testl $0x8000,%ebp
f7c580ff0000	testl $0xff80,%ebp
f7c5ffff0000	testl $0xffff,%ebp
f7c500000100	testl $0x10000,%ebp
f7c5ffffff7f	testl $0x7fffffff,%ebp
f7c500000080	testl $0x80000000,%ebp
f7c580ffffff	testl $0xffffff80,%ebp
f7c5ffffffff	testl $0xffffffff,%ebp
f7c500000000 2:10:sym+0	testl $sym,%ebp
4485c8	testl %r9d,%eax
4185c1	testl %eax,%r9d
44850b	testl %r9d,(%rbx)
44850b	testl (%rbx),%r9d
44850c2500000000 4:11:sym+0	testl %r9d,sym
44850c2500000000 4:11:sym+0	testl sym,%r9d
44850b	test %r9d,(%rbx)
refused	test 4((%rbx,%rsi,2),%r9d
41f7c100000000	testl $0,%r9d
41f7c101000000	testl $1,%r9d
41f7c1ffffffff	testl $-1,%r9d
41f7c17f000000	testl $127,%r9d
41f7c180000000	testl $128,%r9d
41f7c180ffffff	testl $-128,%r9d
41f7c17fffffff	testl $-129,%r9d
41f7c1ff000000	testl $255,%r9d
41f7c100010000	testl $256,%r9d
41f7c1ff7f0000	testl $0x7fff,%r9d
41f7c100800000	testl $0x8000,%r9d
41f7c180ff0000	testl $0xff80,%r9d
41f7c1ffff0000	testl $0xffff,%r9d
41f7c100000100	testl $0x10000,%r9d
41f7c1ffffff7f	testl $0x7fffffff,%r9d
41f7c100000080	testl $0x80000000,%r9d
41f7c180ffffff	testl $0xffffff80,%r9d
41f7c1ffffffff	testl $0xffffffff,%r9d
41f7c100000000 3:10:sym+0	testl $sym,%r9d
f70300000000	testl $0,(%rbx)
f704250000000000000000 3:11:sym+0	testl $0,sym
f7448dfc00000000	testl $0,-4(%rbp,%rcx,4)
f70301000000	testl $1,(%rbx)
f704250000000001000000 3:11:sym+0	testl $1,sym
f7448dfc01000000	testl $1,-4(%rbp,%rcx,4)
f703ffffffff	testl $-1,(%rbx)
f7042500000000ffffffff 3:11:sym+0	testl $-1,sym
f7448dfcffffffff	testl $-1,-4(%rbp,%rcx,4)
f7037f000000	testl $127,(%rbx)
f70425000000007f000000 3:11:sym+0	testl $127,sym
f7448dfc7f000000	testl $127,-4(%rbp,%rcx,4)
f70380000000	testl $128,(%rbx)
f704250000000080000000 3:11:sym+0	testl $128,sym
f7448dfc80000000	testl $128,-4(%rbp,%rcx,4)
f70380ffffff	testl $-128,(%rbx)
f704250000000080ffffff 3:11:sym+0	testl $-128,sym
f7448dfc80ffffff	testl $-128,-4(%rbp,%rcx,4)
f7037fffffff	testl $-129,(%rbx)
f70425000000007fffffff 3:11:sym+0	testl $-129,sym
f7448dfc7fffffff	testl $-129,-4(%rbp,%rcx,4)
f703ff000000	testl $255,(%rbx)
f7042500000000ff000000 3:11:sym+0	testl $255,sym
f7448dfcff000000	testl $255,-4(%rbp,%rcx,4)
f70300010000	testl $256,(%rbx)
f704250000000000010000 3:11:sym+0	testl $256,sym
f7448dfc00010000	testl $256,-4(%rbp,%rcx,4)
f703ff7f0000	testl $0x7fff,(%rbx)
f7042500000000ff7f0000 3:11:sym+0	testl $0x7fff,sym
f7448dfcff7f0000	testl $0x7fff,-4(%rbp,%rcx,4)
f70300800000	testl $0x8000,(%rbx)
f704250000000000800000 3:11:sym+0	testl $0x8000,sym
f7448dfc00800000	testl $0x8000,-4(%rbp,%rcx,4)
f70380ff0000	testl $0xff80,(%rbx)
f704250000000080ff0000 3:11:sym+0	testl $0xff80,sym
f7448dfc80ff0000	testl $0xff80,-4(%rbp,%rcx,4)
f703ffff0000	testl $0xffff,(%rbx)
f7042500000000ffff0000 3:11:sym+0	testl $0xffff,sym
f7448dfcffff0000	testl $0xffff,-4(%rbp,%rcx,4)
f70300000100	testl $0x10000,(%rbx)
f704250000000000000100 3:11:sym+0	testl $0x10000,sym
f7448dfc00000100	testl $0x10000,-4(%rbp,%rcx,4)
f703ffffff7f	testl $0x7fffffff,(%rbx)
f7042500000000ffffff7f 3:11:sym+0	testl $0x7fffffff,sym
f7448dfcffffff7f	testl $0x7fffffff,-4(%rbp,%rcx,4)
f70300000080	testl $0x80000000,(%rbx)
f704250000000000000080 3:11:sym+0	testl $0x80000000,sym
f7448dfc00000080	testl $0x80000000,-4(%rbp,%rcx,4)
f70380ffffff	testl $0xffffff80,(%rbx)
f704250000000080ffffff 3:11:sym+0	testl $0xffffff80,sym
f7448dfc80ffffff	testl $0xffffff80,-4(%rbp,%rcx,4)
f703ffffffff	testl $0xffffffff,(%rbx)
f7042500000000ffffffff 3:11:sym+0	testl $0xffffffff,sym
f7448dfcffffffff	testl $0xffffffff,-4(%rbp,%rcx,4)
f70300000000 2:10:sym+0	testl $sym,(%rbx)
f704250000000000000000 3:11:sym+0 7:10:sym+0	testl $sym,sym
f7448dfc00000000 4:10:sym+0	testl $sym,-4(%rbp,%rcx,4)
f7c301000000	test $1,%ebx
refused	testl $1,$2
refused	testl %eax,$2
refused	testl (%rbx),(%rbx)
refused	lock testl %ebx,(%rbx)
refused	lock testl $1,(%rbx)
refused	lock testl (%rbx),%ebx
refused	lock testl %ebx,%eax
4885c0	testq %rax,%rax
488503	testq %rax,(%rbx)
488503	testq (%rbx),%rax
4885042500000000 4:11:sym+0	testq %rax,sym
4885042500000000 4:11:sym+0	testq sym,%rax
488503	test %rax,(%rbx)
refused	test 4((%rbx,%rsi,2),%rax
48a97f000000	testq $127,%rax
48a980000000	testq $128,%rax
48a980ffffff	testq $-128,%rax
48a97fffffff	testq $-129,%rax
48a9ff000000	testq $255,%rax
48a900010000	testq $256,%rax
48a9ff7f0000	testq $0x7fff,%rax
48a900800000	testq $0x8000,%rax
48a980ff0000	testq $0xff80,%rax
48a9ffff0000	testq $0xffff,%rax
48a900000100	testq $0x10000,%rax
refused	testq $0xffffff80,%rax
48a900000000 2:11:sym+0	testq $sym,%rax
4885d8	testq %rbx,%rax
4885c3	testq %rax,%rbx
48851b	testq %rbx,(%rbx)
48851b	testq (%rbx),%rbx
48851c2500000000 4:11:sym+0	testq %rbx,sym
48851c2500000000 4:11:sym+0	testq sym,%rbx
48851b	test %rbx,(%rbx)
refused	test 4((%rbx,%rsi,2),%rbx
48f7c300000000	testq $0,%rbx
48f7c301000000	testq $1,%rbx
48f7c3ffffffff	testq $-1,%rbx
48f7c37f000000	testq $127,%rbx
48f7c380000000	testq $128,%rbx
48f7c380ffffff	testq $-128,%rbx
48f7c37fffffff	testq $-129,%rbx
48f7c3ff000000	testq $255,%rbx
48f7c300010000	testq $256,%rbx
48f7c3ff7f0000	testq $0x7fff,%rbx
48f7c300800000	testq $0x8000,%rbx
48f7c380ff0000	testq $0xff80,%rbx
48f7c3ffff0000	testq $0xffff,%rbx
48f7c300000100	testq $0x10000,%rbx
48f7c3ffffff7f	testq $0x7fffffff,%rbx
refused	testq $0x80000000,%rbx
refused	testq $0xffffff80,%rbx
refused	testq $0xffffffff,%rbx
48f7c300000000 3:11:sym+0	testq $sym,%rbx
4885e0	testq %rsp,%rax
4885c4	testq %rax,%rsp
488523	testq %rsp,(%rbx)
488523	testq (%rbx),%rsp
4885242500000000 4:11:sym+0	testq %rsp,sym
4885242500000000 4:11:sym+0	testq sym,%rsp
488523	test %rsp,(%rbx)
refused	test 4((%rbx,%rsi,2),%rsp
48f7c400000000	testq $0,%rsp
48f7c401000000	testq $1,%rsp
48f7c4ffffffff	testq $-1,%rsp
48f7c47f000000	testq $127,%rsp
48f7c480000000	testq $128,%rsp
48f7c480ffffff	testq $-128,%rsp
48f7c47fffffff	testq $-129,%rsp
48f7c4ff000000	testq $255,%rsp
48f7c400010000	testq $256,%rsp
48f7c4ff7f0000	testq $0x7fff,%rsp
48f7c400800000	testq $0x8000,%rsp
48f7c480ff0000	testq $0xff80,%rsp
48f7c4ffff0000	testq $0xffff,%rsp
48f7c400000100	testq $0x10000,%rsp
48f7c4ffffff7f	testq $0x7fffffff,%rsp
refused	testq $0x80000000,%rsp
refused	testq $0xffffff80,%rsp
refused	testq $0xffffffff,%rsp
48f7c400000000 3:11:sym+0	testq $sym,%rsp
4c85e0	testq %r12,%rax
4985c4	testq %rax,%r12
4c8523	testq %r12,(%rbx)
4c8523	testq (%rbx),%r12
4c85242500000000 4:11:sym+0	testq %r12,sym
4c85242500000000 4:11:sym+0	testq sym,%r12
4c8523	test %r12,(%rbx)
refused	test 4((%rbx,%rsi,2),%r12
49f7c400000000	testq $0,%r12
49f7c401000000	testq $1,%r12
49f7c4ffffffff	testq $-1,%r12
49f7c47f000000	testq $127,%r12
49f7c480000000	testq $128,%r12
49f7c480ffffff	testq $-128,%r12
49f7c47fffffff	testq $-129,%r12
49f7c4ff000000	testq $255,%r12
49f7c400010000	testq $256,%r12
49f7c4ff7f0000	testq $0x7fff,%r12
49f7c400800000	testq $0x8000,%r12
49f7c480ff0000	testq $0xff80,%r12
49f7c4ffff0000	testq $0xffff,%r12
49f7c400000100	testq $0x10000,%r12
49f7c4ffffff7f	testq $0x7fffffff,%r12
refused	testq $0x80000000,%r12
refused	testq $0xffffff80,%r12
refused	testq $0xffffffff,%r12
49f7c400000000 3:11:sym+0	testq $sym,%r12
4c85e8	testq %r13,%rax
4985c5	testq %rax,%r13
4c852b	testq %r13,(%rbx)
4c852b	testq (%rbx),%r13
4c852c2500000000 4:11:sym+0	testq %r13,sym
4c852c2500000000 4:11:sym+0	testq sym,%r13
4c852b	test %r13,(%rbx)
refused	test 4((%rbx,%rsi,2),%r13
49f7c500000000	testq $0,%r13
49f7c501000000	testq $1,%r13
49f7c5ffffffff	testq $-1,%r13
49f7c57f000000	testq $127,%r13
49f7c580000000	testq $128,%r13
49f7c580ffffff	testq $-128,%r13
49f7c57fffffff	testq $-129,%r13
49f7c5ff000000	testq $255,%r13
49f7c500010000	testq $256,%r13
49f7c5ff7f0000	testq $0x7fff,%r13
49f7c500800000	testq $0x8000,%r13
49f7c580ff0000	testq $0xff80,%r13
49f7c5ffff0000	testq $0xffff,%r13
49f7c500000100	testq $0x10000,%r13
49f7c5ffffff7f	testq $0x7fffffff,%r13
refused	testq $0x80000000,%r13
refused	testq $0xffffff80,%r13
refused	testq $0xffffffff,%r13
49f7c500000000 3:11:sym+0	testq $sym,%r13
4c85c8	testq %r9,%rax
4985c1	testq %rax,%r9
4c850b	testq %r9,(%rbx)
4c850b	testq (%rbx),%r9
4c850c2500000000 4:11:sym+0	testq %r9,sym
4c850c2500000000 4:11:sym+0	testq sym,%r9
4c850b	test %r9,(%rbx)
refused	test 4((%rbx,%rsi,2),%r9
49f7c100000000	testq $0,%r9
49f7c101000000	testq $1,%r9
49f7c1ffffffff	testq $-1,%r9
49f7c17f000000	testq $127,%r9
49f7c180000000	testq $128,%r9
49f7c180ffffff	testq $-128,%r9
49f7c17fffffff	testq $-129,%r9
49f7c1ff000000	testq $255,%r9
49f7c100010000	testq $256,%r9
49f7c1ff7f0000	testq $0x7fff,%r9
49f7c100800000	testq $0x8000,%r9
49f7c180ff0000	testq $0xff80,%r9
49f7c1ffff0000	testq $0xffff,%r9
49f7c100000100	testq $0x10000,%r9
49f7c1ffffff7f	testq $0x7fffffff,%r9
refused	testq $0x80000000,%r9
refused	testq $0xffffff80,%r9
refused	testq $0xffffffff,%r9
49f7c100000000 3:11:sym+0	testq $sym,%r9
48f70300000000	testq $0,(%rbx)
48f704250000000000000000 4:11:sym+0	testq $0,sym
48f7448dfc00000000	testq $0,-4(%rbp,%rcx,4)
48f70301000000	testq $1,(%rbx)
48f704250000000001000000 4:11:sym+0	testq $1,sym
48f7448dfc01000000	testq $1,-4(%rbp,%rcx,4)
48f703ffffffff	testq $-1,(%rbx)
48f7042500000000ffffffff 4:11:sym+0	testq $-1,sym
48f7448dfcffffffff	testq $-1,-4(%rbp,%rcx,4)
48f7037f000000	testq $127,(%rbx)
48f70425000000007f000000 4:11:sym+0	testq $127,sym
48f7448dfc7f000000	testq $127,-4(%rbp,%rcx,4)
48f70380000000	testq $128,(%rbx)
48f704250000000080000000 4:11:sym+0	testq $128,sym
48f7448dfc80000000	testq $128,-4(%rbp,%rcx,4)
48f70380ffffff	testq $-128,(%rbx)
48f704250000000080ffffff 4:11:sym+0	testq $-128,sym
48f7448dfc80ffffff	testq $-128,-4(%rbp,%rcx,4)
48f7037fffffff	testq $-129,(%rbx)
48f70425000000007fffffff 4:11:sym+0	testq $-129,sym
48f7448dfc7fffffff	testq $-129,-4(%rbp,%rcx,4)
48f703ff000000	testq $255,(%rbx)
48f7042500000000ff000000 4:11:sym+0	testq $255,sym
48f7448dfcff000000	testq $255,-4(%rbp,%rcx,4)
48f70300010000	testq $256,(%rbx)
48f704250000000000010000 4:11:sym+0	testq $256,sym
48f7448dfc00010000	testq $256,-4(%rbp,%rcx,4)
48f703ff7f0000	testq $0x7fff,(%rbx)
48f7042500000000ff7f0000 4:11:sym+0	testq $0x7fff,sym
48f7448dfcff7f0000	testq $0x7fff,-4(%rbp,%rcx,4)
48f70300800000	testq $0x8000,(%rbx)
48f704250000000000800000 4:11:sym+0	testq $0x8000,sym
48f7448dfc00800000	testq $0x8000,-4(%rbp,%rcx,4)
48f70380ff0000	testq $0xff80,(%rbx)
48f704250000000080ff0000 4:11:sym+0	testq $0xff80,sym
48f7448dfc80ff0000	testq $0xff80,-4(%rbp,%rcx,4)
48f703ffff0000	testq $0xffff,(%rbx)
48f7042500000000ffff0000 4:11:sym+0	testq $0xffff,sym
48f7448dfcffff0000	testq $0xffff,-4(%rbp,%rcx,4)
48f70300000100	testq $0x10000,(%rbx)
48f704250000000000000100 4:11:sym+0	testq $0x10000,sym
48f7448dfc00000100	testq $0x10000,-4(%rbp,%rcx,4)
48f703ffffff7f	testq $0x7fffffff,(%rbx)
48f7042500000000ffffff7f 4:11:sym+0	testq $0x7fffffff,sym
48f7448dfcffffff7f	testq $0x7fffffff,-4(%rbp,%rcx,4)
refused	testq $0x80000000,(%rbx)
refused	testq $0x80000000,sym
refused	testq $0x80000000,-4(%rbp,%rcx,4)
refused	testq $0xffffff80,(%rbx)
refused	testq $0xffffff80,sym
refused	testq $0xffffff80,-4(%rbp,%rcx,4)
refused	testq $0xffffffff,(%rbx)
refused	testq $0xffffffff,sym
refused	testq $0xffffffff,-4(%rbp,%rcx,4)
48f70300000000 3:11:sym+0	testq $sym,(%rbx)
48f704250000000000000000 4:11:sym+0 8:11:sym+0	testq $sym,sym
48f7448dfc00000000 5:11:sym+0	testq $sym,-4(%rbp,%rcx,4)
48f7c301000000	test $1,%rbx
refused	testq $1,$2
refused	testq %rax,$2
refused	testq (%rbx),(%rbx)
refused	lock testq %rbx,(%rbx)
refused	lock testq $1,(%rbx)
refused	lock testq (%rbx),%rbx
refused	lock testq %rbx,%rax
8803	movb %al,(%rbx)
8a03	movb (%rbx),%al
8803	mov %al,(%rbx)
refused	mov 4((%rbx,%rsi,2),%al
b07f	movb $127,%al
b080	movb $128,%al
b0ff	movb $0x7fff,%al
b000	movb $0x8000,%al
b080	movb $0xff80,%al
b0ff	movb $0xffff,%al
b000	movb $0x10000,%al
b080	movb $0xffffff80,%al
880b	movb %cl,(%rbx)
8a0b	movb (%rbx),%cl
880b	mov %cl,(%rbx)
refused	mov 4((%rbx,%rsi,2),%cl
b100	movb $0,%cl
b17f	movb $127,%cl
b180	movb $128,%cl
b180	movb $-128,%cl
b17f	movb $-129,%cl
b1ff	movb $255,%cl
b100	movb $256,%cl
b1ff	movb $0x7fff,%cl
b100	movb $0x8000,%cl
b180	movb $0xff80,%cl
b1ff	movb $0xffff,%cl
b100	movb $0x10000,%cl
b1ff	movb $0x7fffffff,%cl
b100	movb $0x80000000,%cl
b180	movb $0xffffff80,%cl
b1ff	movb $0xffffffff,%cl
883b	movb %bh,(%rbx)
8a3b	movb (%rbx),%bh
883c2500000000 3:11:sym+0	movb %bh,sym
8a3c2500000000 3:11:sym+0	movb sym,%bh
883b	mov %bh,(%rbx)
refused	mov 4((%rbx,%rsi,2),%bh
b700	movb $0,%bh
b701	movb $1,%bh
b7ff	movb $-1,%bh
b77f	movb $127,%bh
b780	movb $128,%bh
b780	movb $-128,%bh
b77f	movb $-129,%bh
b7ff	movb $255,%bh
b700	movb $256,%bh
b7ff	movb $0x7fff,%bh
b700	movb $0x8000,%bh
b780	movb $0xff80,%bh
b7ff	movb $0xffff,%bh
b700	movb $0x10000,%bh
b7ff	movb $0x7fffffff,%bh
b700	movb $0x80000000,%bh
b780	movb $0xffffff80,%bh
b7ff	movb $0xffffffff,%bh
b700 1:14:sym+0	movb $sym,%bh
408833	movb %sil,(%rbx)
408a33	movb (%rbx),%sil
4088342500000000 4:11:sym+0	movb %sil,sym
408a342500000000 4:11:sym+0	movb sym,%sil
408833	mov %sil,(%rbx)
refused	mov 4((%rbx,%rsi,2),%sil
40b600	movb $0,%sil
40b601	movb $1,%sil
40b6ff	movb $-1,%sil
40b67f	movb $127,%sil
40b680	movb $128,%sil
40b680	movb $-128,%sil
40b67f	movb $-129,%sil
40b6ff	movb $255,%sil
40b600	movb $256,%sil
40b6ff	movb $0x7fff,%sil
40b600	movb $0x8000,%sil
40b680	movb $0xff80,%sil
40b6ff	movb $0xffff,%sil
40b600	movb $0x10000,%sil
40b6ff	movb $0x7fffffff,%sil
40b600	movb $0x80000000,%sil
40b680	movb $0xffffff80,%sil
40b6ff	movb $0xffffffff,%sil
40b600 2:14:sym+0	movb $sym,%sil
4488c8	movb %r9b,%al
4188c1	movb %al,%r9b
44880b	movb %r9b,(%rbx)
448a0b	movb (%rbx),%r9b
44880c2500000000 4:11:sym+0	movb %r9b,sym
448a0c2500000000 4:11:sym+0	movb sym,%r9b
44880b	mov %r9b,(%rbx)
refused	mov 4((%rbx,%rsi,2),%r9b
41b100	movb $0,%r9b
41b101	movb $1,%r9b
41b1ff	movb $-1,%r9b
41b17f	movb $127,%r9b
41b180	movb $128,%r9b
41b180	movb $-128,%r9b
41b17f	movb $-129,%r9b
41b1ff	movb $255,%r9b
41b100	movb $256,%r9b
41b1ff	movb $0x7fff,%r9b
41b100	movb $0x8000,%r9b
41b180	movb $0xff80,%r9b
41b1ff	movb $0xffff,%r9b
41b100	movb $0x10000,%r9b
41b1ff	movb $0x7fffffff,%r9b
41b100	movb $0x80000000,%r9b
41b180	movb $0xffffff80,%r9b
41b1ff	movb $0xffffffff,%r9b
41b100 2:14:sym+0	movb $sym,%r9b
c60300	movb $0,(%rbx)
c604250000000000 3:11:sym+0	movb $0,sym
c6448dfc00	movb $0,-4(%rbp,%rcx,4)
c60301	movb $1,(%rbx)
c604250000000001 3:11:sym+0	movb $1,sym
c6448dfc01	movb $1,-4(%rbp,%rcx,4)
c603ff	movb $-1,(%rbx)
c6042500000000ff 3:11:sym+0	movb $-1,sym
c6448dfcff	movb $-1,-4(%rbp,%rcx,4)
c6037f	movb $127,(%rbx)
c60425000000007f 3:11:sym+0	movb $127,sym
c6448dfc7f	movb $127,-4(%rbp,%rcx,4)
c60380	movb $128,(%rbx)
c604250000000080 3:11:sym+0	movb $128,sym
c6448dfc80	movb $128,-4(%rbp,%rcx,4)
c60380	movb $-128,(%rbx)
c604250000000080 3:11:sym+0	movb $-128,sym
c6448dfc80	movb $-128,-4(%rbp,%rcx,4)
c6037f	movb $-129,(%rbx)
c60425000000007f 3:11:sym+0	movb $-129,sym
c6448dfc7f	movb $-129,-4(%rbp,%rcx,4)
c603ff	movb $255,(%rbx)
c6042500000000ff 3:11:sym+0	movb $255,sym
c6448dfcff	movb $255,-4(%rbp,%rcx,4)
c60300	movb $256,(%rbx)
c604250000000000 3:11:sym+0	movb $256,sym
c6448dfc00	movb $256,-4(%rbp,%rcx,4)
c603ff	movb $0x7fff,(%rbx)
c6042500000000ff 3:11:sym+0	movb $0x7fff,sym
c6448dfcff	movb $0x7fff,-4(%rbp,%rcx,4)
c60300	movb $0x8000,(%rbx)
c604250000000000 3:11:sym+0	movb $0x8000,sym
c6448dfc00	movb $0x8000,-4(%rbp,%rcx,4)
c60380	movb $0xff80,(%rbx)
c604250000000080 3:11:sym+0	movb $0xff80,sym
c6448dfc80	movb $0xff80,-4(%rbp,%rcx,4)
c603ff	movb $0xffff,(%rbx)
c6042500000000ff 3:11:sym+0	movb $0xffff,sym
c6448dfcff	movb $0xffff,-4(%rbp,%rcx,4)
c60300	movb $0x10000,(%rbx)
c604250000000000 3:11:sym+0	movb $0x10000,sym
c6448dfc00	movb $0x10000,-4(%rbp,%rcx,4)
c603ff	movb $0x7fffffff,(%rbx)
c6042500000000ff 3:11:sym+0	movb $0x7fffffff,sym
c6448dfcff	movb $0x7fffffff,-4(%rbp,%rcx,4)
c60300	movb $0x80000000,(%rbx)
c604250000000000 3:11:sym+0	movb $0x80000000,sym
c6448dfc00	movb $0x80000000,-4(%rbp,%rcx,4)
c60380	movb $0xffffff80,(%rbx)
c604250000000080 3:11:sym+0	movb $0xffffff80,sym
c6448dfc80	movb $0xffffff80,-4(%rbp,%rcx,4)
c603ff	movb $0xffffffff,(%rbx)
c6042500000000ff 3:11:sym+0	movb $0xffffffff,sym
c6448dfcff	movb $0xffffffff,-4(%rbp,%rcx,4)
c60300 2:14:sym+0	movb $sym,(%rbx)
c604250000000000 3:11:sym+0 7:14:sym+0	movb $sym,sym
c6448dfc00 4:14:sym+0	movb $sym,-4(%rbp,%rcx,4)
c70301000000	mov $1,(%rbx)
refused	movb $1,$2
refused	movb %al,$2
refused	movb (%rbx),(%rbx)
refused	lock movb %cl,(%rbx)
refused	lock movb $1,(%rbx)
refused	lock movb (%rbx),%cl
refused	lock movb %cl,%al
668903	movw %ax,(%rbx)
668b03	movw (%rbx),%ax
668903	mov %ax,(%rbx)
refused	mov 4((%rbx,%rsi,2),%ax
66b87f00	movw $127,%ax
66b88000	movw $128,%ax
66b880ff	movw $-128,%ax
66b87fff	movw $-129,%ax
66b8ff00	movw $255,%ax
66b80001	movw $256,%ax
66b8ff7f	movw $0x7fff,%ax
66b80080	movw $0x8000,%ax
66b880ff	movw $0xff80,%ax
66b8ffff	movw $0xffff,%ax
66b80000	movw $0x10000,%ax
66b880ff	movw $0xffffff80,%ax
66891b	movw %bx,(%rbx)
668b1b	movw (%rbx),%bx
66891c2500000000 4:11:sym+0	movw %bx,sym
668b1c2500000000 4:11:sym+0	movw sym,%bx
66891b	mov %bx,(%rbx)
refused	mov 4((%rbx,%rsi,2),%bx
66bb0000	movw $0,%bx
66bb0100	movw $1,%bx
66bbffff	movw $-1,%bx
66bb7f00	movw $127,%bx
66bb8000	movw $128,%bx
66bb80ff	movw $-128,%bx
66bb7fff	movw $-129,%bx
66bbff00	movw $255,%bx
66bb0001	movw $256,%bx
66bbff7f	movw $0x7fff,%bx
66bb0080	movw $0x8000,%bx
66bb80ff	movw $0xff80,%bx
66bbffff	movw $0xffff,%bx
66bb0000	movw $0x10000,%bx
66bbffff	movw $0x7fffffff,%bx
66bb0000	movw $0x80000000,%bx
66bb80ff	movw $0xffffff80,%bx
66bbffff	movw $0xffffffff,%bx
66bb0000 2:12:sym+0	movw $sym,%bx
668933	movw %si,(%rbx)
668b33	movw (%rbx),%si
6689342500000000 4:11:sym+0	movw %si,sym
668b342500000000 4:11:sym+0	movw sym,%si
668933	mov %si,(%rbx)
refused	mov 4((%rbx,%rsi,2),%si
66be0000	movw $0,%si
66be0100	movw $1,%si
66beffff	movw $-1,%si
66be7f00	movw $127,%si
66be8000	movw $128,%si
66be80ff	movw $-128,%si
66be7fff	movw $-129,%si
66beff00	movw $255,%si
66be0001	movw $256,%si
66beff7f	movw $0x7fff,%si
66be0080	movw $0x8000,%si
66be80ff	movw $0xff80,%si
66beffff	movw $0xffff,%si
66be0000	movw $0x10000,%si
66beffff	movw $0x7fffffff,%si
66be0000	movw $0x80000000,%si
66be80ff	movw $0xffffff80,%si
66beffff	movw $0xffffffff,%si
66be0000 2:12:sym+0	movw $sym,%si
664489c8	movw %r9w,%ax
664189c1	movw %ax,%r9w
6644890b	movw %r9w,(%rbx)
66448b0b	movw (%rbx),%r9w
6644890c2500000000 5:11:sym+0	movw %r9w,sym
66448b0c2500000000 5:11:sym+0	movw sym,%r9w
6644890b	mov %r9w,(%rbx)
refused	mov 4((%rbx,%rsi,2),%r9w
6641b90000	movw $0,%r9w
6641b90100	movw $1,%r9w
6641b9ffff	movw $-1,%r9w
6641b97f00	movw $127,%r9w
6641b98000	movw $128,%r9w
6641b980ff	movw $-128,%r9w
6641b97fff	movw $-129,%r9w
6641b9ff00	movw $255,%r9w
6641b90001	movw $256,%r9w
6641b9ff7f	movw $0x7fff,%r9w
6641b90080	movw $0x8000,%r9w
6641b980ff	movw $0xff80,%r9w
6641b9ffff	movw $0xffff,%r9w
6641b90000	movw $0x10000,%r9w
6641b9ffff	movw $0x7fffffff,%r9w
6641b90000	movw $0x80000000,%r9w
6641b980ff	movw $0xffffff80,%r9w
6641b9ffff	movw $0xffffffff,%r9w
6641b90000 3:12:sym+0	movw $sym,%r9w
66c7030000	movw $0,(%rbx)
66c70425000000000000 4:11:sym+0	movw $0,sym
66c7448dfc0000	movw $0,-4(%rbp,%rcx,4)
66c7030100	movw $1,(%rbx)
66c70425000000000100 4:11:sym+0	movw $1,sym
66c7448dfc0100	movw $1,-4(%rbp,%rcx,4)
66c703ffff	movw $-1,(%rbx)
66c7042500000000ffff 4:11:sym+0	movw $-1,sym
66c7448dfcffff	movw $-1,-4(%rbp,%rcx,4)
66c7037f00	movw $127,(%rbx)
66c70425000000007f00 4:11:sym+0	movw $127,sym
66c7448dfc7f00	movw $127,-4(%rbp,%rcx,4)
66c7038000	movw $128,(%rbx)
66c70425000000008000 4:11:sym+0	movw $128,sym
66c7448dfc8000	movw $128,-4(%rbp,%rcx,4)
66c70380ff	movw $-128,(%rbx)
66c704250000000080ff 4:11:sym+0	movw $-128,sym
66c7448dfc80ff	movw $-128,-4(%rbp,%rcx,4)
66c7037fff	movw $-129,(%rbx)
66c70425000000007fff 4:11:sym+0	movw $-129,sym
66c7448dfc7fff	movw $-129,-4(%rbp,%rcx,4)
66c703ff00	movw $255,(%rbx)
66c7042500000000ff00 4:11:sym+0	movw $255,sym
66c7448dfcff00	movw $255,-4(%rbp,%rcx,4)
66c7030001	movw $256,(%rbx)
66c70425000000000001 4:11:sym+0	movw $256,sym
66c7448dfc0001	movw $256,-4(%rbp,%rcx,4)
66c703ff7f	movw $0x7fff,(%rbx)
66c7042500000000ff7f 4:11:sym+0	movw $0x7fff,sym
66c7448dfcff7f	movw $0x7fff,-4(%rbp,%rcx,4)
66c7030080	movw $0x8000,(%rbx)
66c70425000000000080 4:11:sym+0	movw $0x8000,sym
66c7448dfc0080	movw $0x8000,-4(%rbp,%rcx,4)
66c70380ff	movw $0xff80,(%rbx)
66c704250000000080ff 4:11:sym+0	movw $0xff80,sym
66c7448dfc80ff	movw $0xff80,-4(%rbp,%rcx,4)
66c703ffff	movw $0xffff,(%rbx)
66c7042500000000ffff 4:11:sym+0	movw $0xffff,sym
66c7448dfcffff	movw $0xffff,-4(%rbp,%rcx,4)
66c7030000	movw $0x10000,(%rbx)
66c70425000000000000 4:11:sym+0	movw $0x10000,sym
66c7448dfc0000	movw $0x10000,-4(%rbp,%rcx,4)
66c703ffff	movw $0x7fffffff,(%rbx)
66c7042500000000ffff 4:11:sym+0	movw $0x7fffffff,sym
66c7448dfcffff	movw $0x7fffffff,-4(%rbp,%rcx,4)
66c7030000	movw $0x80000000,(%rbx)
66c70425000000000000 4:11:sym+0	movw $0x80000000,sym
66c7448dfc0000	movw $0x80000000,-4(%rbp,%rcx,4)
66c70380ff	movw $0xffffff80,(%rbx)
66c704250000000080ff 4:11:sym+0	movw $0xffffff80,sym
66c7448dfc80ff	movw $0xffffff80,-4(%rbp,%rcx,4)
66c703ffff	movw $0xffffffff,(%rbx)
66c7042500000000ffff 4:11:sym+0	movw $0xffffffff,sym
66c7448dfcffff	movw $0xffffffff,-4(%rbp,%rcx,4)
66c7030000 3:12:sym+0	movw $sym,(%rbx)
66c70425000000000000 4:11:sym+0 8:12:sym+0	movw $sym,sym
66c7448dfc0000 5:12:sym+0	movw $sym,-4(%rbp,%rcx,4)
66bb0100	mov $1,%bx
refused	movw $1,$2
refused	movw %ax,$2
refused	movw (%rbx),(%rbx)
refused	lock movw %bx,(%rbx)
refused	lock movw $1,(%rbx)
refused	lock movw (%rbx),%bx
refused	lock movw %bx,%ax
8b03	movl (%rbx),%eax
8903	mov %eax,(%rbx)
refused	mov 4((%rbx,%rsi,2),%eax
b87f000000	movl $127,%eax
b880000000	movl $128,%eax
b880ffffff	movl $-128,%eax
b87fffffff	movl $-129,%eax
b8ff000000	movl $255,%eax
b800010000	movl $256,%eax
b8ff7f0000	movl $0x7fff,%eax
b800800000	movl $0x8000,%eax
b880ff0000	movl $0xff80,%eax
b8ffff0000	movl $0xffff,%eax
b800000100	movl $0x10000,%eax
b880ffffff	movl $0xffffff80,%eax
891b	movl %ebx,(%rbx)
8b1b	movl (%rbx),%ebx
891c2500000000 3:11:sym+0	movl %ebx,sym
891b	mov %ebx,(%rbx)
refused	mov 4((%rbx,%rsi,2),%ebx
bb00000000	movl $0,%ebx
bb01000000	movl $1,%ebx
bbffffffff	movl $-1,%ebx
bb7f000000	movl $127,%ebx
bb80000000	movl $128,%ebx
bb80ffffff	movl $-128,%ebx
bb7fffffff	movl $-129,%ebx
bbff000000	movl $255,%ebx
bb00010000	movl $256,%ebx
bbff7f0000	movl $0x7fff,%ebx
bb00800000	movl $0x8000,%ebx
bb80ff0000	movl $0xff80,%ebx
bbffff0000	movl $0xffff,%ebx
bb00000100	movl $0x10000,%ebx
bbffffff7f	movl $0x7fffffff,%ebx
bb00000080	movl $0x80000000,%ebx
bb80ffffff	movl $0xffffff80,%ebx
bbffffffff	movl $0xffffffff,%ebx
bb00000000 1:10:sym+0	movl $sym,%ebx
8923	movl %esp,(%rbx)
8b23	movl (%rbx),%esp
89242500000000 3:11:sym+0	movl %esp,sym
8b242500000000 3:11:sym+0	movl sym,%esp
8923	mov %esp,(%rbx)
refused	mov 4((%rbx,%rsi,2),%esp
bc00000000	movl $0,%esp
bc01000000	movl $1,%esp
bcffffffff	movl $-1,%esp
bc7f000000	movl $127,%esp
bc80000000	movl $128,%esp
bc80ffffff	movl $-128,%esp
bc7fffffff	movl $-129,%esp
bcff000000	movl $255,%esp
bc00010000	movl $256,%esp
bcff7f0000	movl $0x7fff,%esp
bc00800000	movl $0x8000,%esp
bc80ff0000	movl $0xff80,%esp
bcffff0000	movl $0xffff,%esp
bc00000100	movl $0x10000,%esp
bcffffff7f	movl $0x7fffffff,%esp
bc00000080	movl $0x80000000,%esp
bc80ffffff	movl $0xffffff80,%esp
bcffffffff	movl $0xffffffff,%esp
bc00000000 1:10:sym+0	movl $sym,%esp
892b	movl %ebp,(%rbx)
8b2b	movl (%rbx),%ebp
892c2500000000 3:11:sym+0	movl %ebp,sym
8b2c2500000000 3:11:sym+0	movl sym,%ebp
892b	mov %ebp,(%rbx)
refused	mov 4((%rbx,%rsi,2),%ebp
bd00000000	movl $0,%ebp
bd01000000	movl $1,%ebp
bdffffffff	movl $-1,%ebp
bd7f000000	movl $127,%ebp
bd80000000	movl $128,%ebp
bd80ffffff	movl $-128,%ebp
bd7fffffff	movl $-129,%ebp
bdff000000	movl $255,%ebp
bd00010000	movl $256,%ebp
bdff7f0000	movl $0x7fff,%ebp
bd00800000	movl $0x8000,%ebp
bd80ff0000	movl $0xff80,%ebp
bdffff0000	movl $0xffff,%ebp
bd00000100	movl $0x10000,%ebp
bdffffff7f	movl $0x7fffffff,%ebp
bd00000080	movl $0x80000000,%ebp
bd80ffffff	movl $0xffffff80,%ebp
bdffffffff	movl $0xffffffff,%ebp
bd00000000 1:10:sym+0	movl $sym,%ebp
4489c8	movl %r9d,%eax
4189c1	movl %eax,%r9d
44890b	movl %r9d,(%rbx)
448b0b	movl (%rbx),%r9d
44890c2500000000 4:11:sym+0	movl %r9d,sym
448b0c2500000000 4:11:sym+0	movl sym,%r9d
44890b	mov %r9d,(%rbx)
refused	mov 4((%rbx,%rsi,2),%r9d
41b900000000	movl $0,%r9d
41b901000000	movl $1,%r9d
41b9ffffffff	movl $-1,%r9d
41b97f000000	movl $127,%r9d
41b980000000	movl $128,%r9d
41b980ffffff	movl $-128,%r9d
41b97fffffff	movl $-129,%r9d
41b9ff000000	movl $255,%r9d
41b900010000	movl $256,%r9d
41b9ff7f0000	movl $0x7fff,%r9d
41b900800000	movl $0x8000,%r9d
41b980ff0000	movl $0xff80,%r9d
41b9ffff0000	movl $0xffff,%r9d
41b900000100	movl $0x10000,%r9d
41b9ffffff7f	movl $0x7fffffff,%r9d
41b900000080	movl $0x80000000,%r9d
41b980ffffff	movl $0xffffff80,%r9d
41b9ffffffff	movl $0xffffffff,%r9d
41b900000000 2:10:sym+0	movl $sym,%r9d
c70300000000	movl $0,(%rbx)
c704250000000000000000 3:11:sym+0	movl $0,sym
c7448dfc00000000	movl $0,-4(%rbp,%rcx,4)
c70301000000	movl $1,(%rbx)
c704250000000001000000 3:11:sym+0	movl $1,sym
c7448dfc01000000	movl $1,-4(%rbp,%rcx,4)
c703ffffffff	movl $-1,(%rbx)
c7042500000000ffffffff 3:11:sym+0	movl $-1,sym
c7448dfcffffffff	movl $-1,-4(%rbp,%rcx,4)
c7037f000000	movl $127,(%rbx)
c70425000000007f000000 3:11:sym+0	movl $127,sym
c7448dfc7f000000	movl $127,-4(%rbp,%rcx,4)
c70380000000	movl $128,(%rbx)
c704250000000080000000 3:11:sym+0	movl $128,sym
c7448dfc80000000	movl $128,-4(%rbp,%rcx,4)
c70380ffffff	movl $-128,(%rbx)
c704250000000080ffffff 3:11:sym+0	movl $-128,sym
c7448dfc80ffffff	movl $-128,-4(%rbp,%rcx,4)
c7037fffffff	movl $-129,(%rbx)
c70425000000007fffffff 3:11:sym+0	movl $-129,sym
c7448dfc7fffffff	movl $-129,-4(%rbp,%rcx,4)
c703ff000000	movl $255,(%rbx)
c7042500000000ff000000 3:11:sym+0	movl $255,sym
c7448dfcff000000	movl $255,-4(%rbp,%rcx,4)
c70300010000	movl $256,(%rbx)
c704250000000000010000 3:11:sym+0	movl $256,sym
c7448dfc00010000	movl $256,-4(%rbp,%rcx,4)
c703ff7f0000	movl $0x7fff,(%rbx)
c7042500000000ff7f0000 3:11:sym+0	movl $0x7fff,sym
c7448dfcff7f0000	movl $0x7fff,-4(%rbp,%rcx,4)
c70300800000	movl $0x8000,(%rbx)
c704250000000000800000 3:11:sym+0	movl $0x8000,sym
c7448dfc00800000	movl $0x8000,-4(%rbp,%rcx,4)
c70380ff0000	movl $0xff80,(%rbx)
c704250000000080ff0000 3:11:sym+0	movl $0xff80,sym
c7448dfc80ff0000	movl $0xff80,-4(%rbp,%rcx,4)
c703ffff0000	movl $0xffff,(%rbx)
c7042500000000ffff0000 3:11:sym+0	movl $0xffff,sym
c7448dfcffff0000	movl $0xffff,-4(%rbp,%rcx,4)
c70300000100	movl $0x10000,(%rbx)
c704250000000000000100 3:11:sym+0	movl $0x10000,sym
c7448dfc00000100	movl $0x10000,-4(%rbp,%rcx,4)
c703ffffff7f	movl $0x7fffffff,(%rbx)
c7042500000000ffffff7f 3:11:sym+0	movl $0x7fffffff,sym
c7448dfcffffff7f	movl $0x7fffffff,-4(%rbp,%rcx,4)
c70300000080	movl $0x80000000,(%rbx)
c704250000000000000080 3:11:sym+0	movl $0x80000000,sym
c7448dfc00000080	movl $0x80000000,-4(%rbp,%rcx,4)
c70380ffffff	movl $0xffffff80,(%rbx)
c704250000000080ffffff 3:11:sym+0	movl $0xffffff80,sym
c7448dfc80ffffff	movl $0xffffff80,-4(%rbp,%rcx,4)
c703ffffffff	movl $0xffffffff,(%rbx)
c7042500000000ffffffff 3:11:sym+0	movl $0xffffffff,sym
c7448dfcffffffff	movl $0xffffffff,-4(%rbp,%rcx,4)
c70300000000 2:10:sym+0	movl $sym,(%rbx)
c704250000000000000000 3:11:sym+0 7:10:sym+0	movl $sym,sym
c7448dfc00000000 4:10:sym+0	movl $sym,-4(%rbp,%rcx,4)
bb01000000	mov $1,%ebx
refused	movl %eax,$2
refused	movl (%rbx),(%rbx)
refused	lock movl %ebx,(%rbx)
refused	lock movl $1,(%rbx)
refused	lock movl (%rbx),%ebx
refused	lock movl %ebx,%eax
488903	movq %rax,(%rbx)
488b03	movq (%rbx),%rax
488903	mov %rax,(%rbx)
refused	mov 4((%rbx,%rsi,2),%rax
48c7c07f000000	movq $127,%rax
48c7c080000000	movq $128,%rax
48c7c080ffffff	movq $-128,%rax
48c7c07fffffff	movq $-129,%rax
48c7c0ff000000	movq $255,%rax
48c7c000010000	movq $256,%rax
48c7c0ff7f0000	movq $0x7fff,%rax
48c7c000800000	movq $0x8000,%rax
48c7c080ff0000	movq $0xff80,%rax
48c7c0ffff0000	movq $0xffff,%rax
48c7c000000100	movq $0x10000,%rax
48b880ffffff00000000	movq $0xffffff80,%rax
48891b	movq %rbx,(%rbx)
488b1b	movq (%rbx),%rbx
48891c2500000000 4:11:sym+0	movq %rbx,sym
488b1c2500000000 4:11:sym+0	movq sym,%rbx
48891b	mov %rbx,(%rbx)
refused	mov 4((%rbx,%rsi,2),%rbx
48c7c300000000	movq $0,%rbx
48c7c301000000	movq $1,%rbx
48c7c3ffffffff	movq $-1,%rbx
48c7c37f000000	movq $127,%rbx
48c7c380000000	movq $128,%rbx
48c7c380ffffff	movq $-128,%rbx
48c7c37fffffff	movq $-129,%rbx
48c7c3ff000000	movq $255,%rbx
48c7c300010000	movq $256,%rbx
48c7c3ff7f0000	movq $0x7fff,%rbx
48c7c300800000	movq $0x8000,%rbx
48c7c380ff0000	movq $0xff80,%rbx
48c7c3ffff0000	movq $0xffff,%rbx
48c7c300000100	movq $0x10000,%rbx
48c7c3ffffff7f	movq $0x7fffffff,%rbx
48bb0000008000000000	movq $0x80000000,%rbx
48bb80ffffff00000000	movq $0xffffff80,%rbx
48bbffffffff00000000	movq $0xffffffff,%rbx
48c7c300000000 3:11:sym+0	movq $sym,%rbx
488923	movq %rsp,(%rbx)
488b23	movq (%rbx),%rsp
4889242500000000 4:11:sym+0	movq %rsp,sym
488b242500000000 4:11:sym+0	movq sym,%rsp
488923	mov %rsp,(%rbx)
refused	mov 4((%rbx,%rsi,2),%rsp
48c7c400000000	movq $0,%rsp
48c7c401000000	movq $1,%rsp
48c7c4ffffffff	movq $-1,%rsp
48c7c47f000000	movq $127,%rsp
48c7c480000000	movq $128,%rsp
48c7c480ffffff	movq $-128,%rsp
48c7c47fffffff	movq $-129,%rsp
48c7c4ff000000	movq $255,%rsp
48c7c400010000	movq $256,%rsp
48c7c4ff7f0000	movq $0x7fff,%rsp
48c7c400800000	movq $0x8000,%rsp
48c7c480ff0000	movq $0xff80,%rsp
48c7c4ffff0000	movq $0xffff,%rsp
48c7c400000100	movq $0x10000,%rsp
48c7c4ffffff7f	movq $0x7fffffff,%rsp
48bc0000008000000000	movq $0x80000000,%rsp
48bc80ffffff00000000	movq $0xffffff80,%rsp
48bcffffffff00000000	movq $0xffffffff,%rsp
48c7c400000000 3:11:sym+0	movq $sym,%rsp
4c8923	movq %r12,(%rbx)
4c8b23	movq (%rbx),%r12
4c89242500000000 4:11:sym+0	movq %r12,sym
4c8b242500000000 4:11:sym+0	movq sym,%r12
4c8923	mov %r12,(%rbx)
refused	mov 4((%rbx,%rsi,2),%r12
49c7c400000000	movq $0,%r12
49c7c401000000	movq $1,%r12
49c7c4ffffffff	movq $-1,%r12
49c7c47f000000	movq $127,%r12
49c7c480000000	movq $128,%r12
49c7c480ffffff	movq $-128,%r12
49c7c47fffffff	movq $-129,%r12
49c7c4ff000000	movq $255,%r12
49c7c400010000	movq $256,%r12
49c7c4ff7f0000	movq $0x7fff,%r12
49c7c400800000	movq $0x8000,%r12
49c7c480ff0000	movq $0xff80,%r12
49c7c4ffff0000	movq $0xffff,%r12
49c7c400000100	movq $0x10000,%r12
49c7c4ffffff7f	movq $0x7fffffff,%r12
49bc0000008000000000	movq $0x80000000,%r12
49bc80ffffff00000000	movq $0xffffff80,%r12
49bcffffffff00000000	movq $0xffffffff,%r12
49c7c400000000 3:11:sym+0	movq $sym,%r12
4c892b	movq %r13,(%rbx)
4c8b2b	movq (%rbx),%r13
4c892c2500000000 4:11:sym+0	movq %r13,sym
4c8b2c2500000000 4:11:sym+0	movq sym,%r13
4c892b	mov %r13,(%rbx)
refused	mov 4((%rbx,%rsi,2),%r13
49c7c500000000	movq $0,%r13
49c7c501000000	movq $1,%r13
49c7c5ffffffff	movq $-1,%r13
49c7c57f000000	movq $127,%r13
49c7c580000000	movq $128,%r13
49c7c580ffffff	movq $-128,%r13
49c7c57fffffff	movq $-129,%r13
49c7c5ff000000	movq $255,%r13
49c7c500010000	movq $256,%r13
49c7c5ff7f0000	movq $0x7fff,%r13
49c7c500800000	movq $0x8000,%r13
49c7c580ff0000	movq $0xff80,%r13
49c7c5ffff0000	movq $0xffff,%r13
49c7c500000100	movq $0x10000,%r13
49c7c5ffffff7f	movq $0x7fffffff,%r13
49bd0000008000000000	movq $0x80000000,%r13
49bd80ffffff00000000	movq $0xffffff80,%r13
49bdffffffff00000000	movq $0xffffffff,%r13
49c7c500000000 3:11:sym+0	movq $sym,%r13
4c890b	movq %r9,(%rbx)
4c8b0b	movq (%rbx),%r9
4c890c2500000000 4:11:sym+0	movq %r9,sym
4c8b0c2500000000 4:11:sym+0	movq sym,%r9
4c890b	mov %r9,(%rbx)
refused	mov 4((%rbx,%rsi,2),%r9
49c7c100000000	movq $0,%r9
49c7c101000000	movq $1,%r9
49c7c1ffffffff	movq $-1,%r9
49c7c17f000000	movq $127,%r9
49c7c180000000	movq $128,%r9
49c7c180ffffff	movq $-128,%r9
49c7c17fffffff	movq $-129,%r9
49c7c1ff000000	movq $255,%r9
49c7c100010000	movq $256,%r9
49c7c1ff7f0000	movq $0x7fff,%r9
49c7c100800000	movq $0x8000,%r9
49c7c180ff0000	movq $0xff80,%r9
49c7c1ffff0000	movq $0xffff,%r9
49c7c100000100	movq $0x10000,%r9
49c7c1ffffff7f	movq $0x7fffffff,%r9
49b90000008000000000	movq $0x80000000,%r9
49b980ffffff00000000	movq $0xffffff80,%r9
49b9ffffffff00000000	movq $0xffffffff,%r9
49c7c100000000 3:11:sym+0	movq $sym,%r9
48c70300000000	movq $0,(%rbx)
48c704250000000000000000 4:11:sym+0	movq $0,sym
48c7448dfc00000000	movq $0,-4(%rbp,%rcx,4)
48c70301000000	movq $1,(%rbx)
48c704250000000001000000 4:11:sym+0	movq $1,sym
48c7448dfc01000000	movq $1,-4(%rbp,%rcx,4)
48c703ffffffff	movq $-1,(%rbx)
48c7042500000000ffffffff 4:11:sym+0	movq $-1,sym
48c7448dfcffffffff	movq $-1,-4(%rbp,%rcx,4)
48c7037f000000	movq $127,(%rbx)
48c70425000000007f000000 4:11:sym+0	movq $127,sym
48c7448dfc7f000000	movq $127,-4(%rbp,%rcx,4)
48c70380000000	movq $128,(%rbx)
48c704250000000080000000 4:11:sym+0	movq $128,sym
48c7448dfc80000000	movq $128,-4(%rbp,%rcx,4)
48c70380ffffff	movq $-128,(%rbx)
48c704250000000080ffffff 4:11:sym+0	movq $-128,sym
48c7448dfc80ffffff	movq $-128,-4(%rbp,%rcx,4)
48c7037fffffff	movq $-129,(%rbx)
48c70425000000007fffffff 4:11:sym+0	movq $-129,sym
48c7448dfc7fffffff	movq $-129,-4(%rbp,%rcx,4)
48c703ff000000	movq $255,(%rbx)
48c7042500000000ff000000 4:11:sym+0	movq $255,sym
48c7448dfcff000000	movq $255,-4(%rbp,%rcx,4)
48c70300010000	movq $256,(%rbx)
48c704250000000000010000 4:11:sym+0	movq $256,sym
48c7448dfc00010000	movq $256,-4(%rbp,%rcx,4)
48c703ff7f0000	movq $0x7fff,(%rbx)
48c7042500000000ff7f0000 4:11:sym+0	movq $0x7fff,sym
48c7448dfcff7f0000	movq $0x7fff,-4(%rbp,%rcx,4)
48c70300800000	movq $0x8000,(%rbx)
48c704250000000000800000 4:11:sym+0	movq $0x8000,sym
48c7448dfc00800000	movq $0x8000,-4(%rbp,%rcx,4)
48c70380ff0000	movq $0xff80,(%rbx)
48c704250000000080ff0000 4:11:sym+0	movq $0xff80,sym
48c7448dfc80ff0000	movq $0xff80,-4(%rbp,%rcx,4)
48c703ffff0000	movq $0xffff,(%rbx)
48c7042500000000ffff0000 4:11:sym+0	movq $0xffff,sym
48c7448dfcffff0000	movq $0xffff,-4(%rbp,%rcx,4)
48c70300000100	movq $0x10000,(%rbx)
48c704250000000000000100 4:11:sym+0	movq $0x10000,sym
48c7448dfc00000100	movq $0x10000,-4(%rbp,%rcx,4)
48c703ffffff7f	movq $0x7fffffff,(%rbx)
48c7042500000000ffffff7f 4:11:sym+0	movq $0x7fffffff,sym
48c7448dfcffffff7f	movq $0x7fffffff,-4(%rbp,%rcx,4)
refused	movq $0x80000000,(%rbx)
refused	movq $0x80000000,sym
refused	movq $0x80000000,-4(%rbp,%rcx,4)
refused	movq $0xffffff80,(%rbx)
refused	movq $0xffffff80,sym
refused	movq $0xffffff80,-4(%rbp,%rcx,4)
refused	movq $0xffffffff,(%rbx)
refused	movq $0xffffffff,sym
refused	movq $0xffffffff,-4(%rbp,%rcx,4)
48c70300000000 3:11:sym+0	movq $sym,(%rbx)
48c704250000000000000000 4:11:sym+0 8:11:sym+0	movq $sym,sym
48c7448dfc00000000 5:11:sym+0	movq $sym,-4(%rbp,%rcx,4)
48c7c301000000	mov $1,%rbx
refused	movq $1,$2
refused	movq %rax,$2
refused	movq (%rbx),(%rbx)
refused	lock movq %rbx,(%rbx)
refused	lock movq $1,(%rbx)
refused	lock movq (%rbx),%rbx
refused	lock movq %rbx,%rax
fec0	incb %al
fec0	inc %al
fec1	incb %cl
fec1	inc %cl
fec7	incb %bh
fec7	inc %bh
40fec6	incb %sil
40fec6	inc %sil
41fec1	incb %r9b
41fec1	inc %r9b
fe03	incb (%rbx)
fe042500000000 3:11:sym+0	incb sym
ff03	inc (%rbx)
fe442408	incb 8(%rsp)
f0fe03	lock incb (%rbx)
refused	incb $1
refused	incb
66ffc0	incw %ax
66ffc0	inc %ax
66ffc3	incw %bx
66ffc3	inc %bx
66ffc6	incw %si
66ffc6	inc %si
6641ffc1	incw %r9w
6641ffc1	inc %r9w
66ff03	incw (%rbx)
66ff042500000000 4:11:sym+0	incw sym
66ff442408	incw 8(%rsp)
66f0ff03	lock incw (%rbx)
refused	incw $1
refused	incw
ffc3	incl %ebx
ffc3	inc %ebx
ffc4	incl %esp
ffc4	inc %esp
ffc5	incl %ebp
ffc5	inc %ebp
41ffc1	incl %r9d
41ffc1	inc %r9d
ff03	incl (%rbx)
ff042500000000 3:11:sym+0	incl sym
ff442408	incl 8(%rsp)
f0ff03	lock incl (%rbx)
refused	incl $1
refused	incl
48ffc0	incq %rax
48ffc0	inc %rax
48ffc3	incq %rbx
48ffc3	inc %rbx
48ffc4	incq %rsp
48ffc4	inc %rsp
49ffc4	incq %r12
49ffc4	inc %r12
49ffc5	incq %r13
49ffc5	inc %r13
49ffc1	incq %r9
49ffc1	inc %r9
48ff03	incq (%rbx)
48ff042500000000 4:11:sym+0	incq sym
48ff442408	incq 8(%rsp)
f048ff03	lock incq (%rbx)
refused	incq $1
refused	incq
fec8	decb %al
fec8	dec %al
fec9	decb %cl
fec9	dec %cl
fecf	decb %bh
fecf	dec %bh
40fece	decb %sil
40fece	dec %sil
41fec9	decb %r9b
41fec9	dec %r9b
fe0b	decb (%rbx)
fe0c2500000000 3:11:sym+0	decb sym
ff0b	dec (%rbx)
fe4c2408	decb 8(%rsp)
f0fe0b	lock decb (%rbx)
refused	decb $1
refused	decb
66ffc8	decw %ax
66ffc8	dec %ax
66ffcb	decw %bx
66ffcb	dec %bx
66ffce	decw %si
66ffce	dec %si
6641ffc9	decw %r9w
6641ffc9	dec %r9w
66ff0b	decw (%rbx)
66ff0c2500000000 4:11:sym+0	decw sym
66ff4c2408	decw 8(%rsp)
66f0ff0b	lock decw (%rbx)
refused	decw $1
refused	decw
ffc8	dec %eax
ffcb	decl %ebx
ffcb	dec %ebx
ffcc	decl %esp
ffcc	dec %esp
ffcd	decl %ebp
ffcd	dec %ebp
41ffc9	decl %r9d
41ffc9	dec %r9d
ff0b	decl (%rbx)
ff0c2500000000 3:11:sym+0	decl sym
ff4c2408	decl 8(%rsp)
f0ff0b	lock decl (%rbx)
refused	decl $1
refused	decl
48ffc8	decq %rax
48ffc8	dec %rax
48ffcb	decq %rbx
48ffcb	dec %rbx
48ffcc	decq %rsp
48ffcc	dec %rsp
49ffcc	decq %r12
49ffcc	dec %r12
49ffcd	decq %r13
49ffcd	dec %r13
49ffc9	decq %r9
49ffc9	dec %r9
48ff0b	decq (%rbx)
48ff0c2500000000 4:11:sym+0	decq sym
48ff4c2408	decq 8(%rsp)
f048ff0b	lock decq (%rbx)
refused	decq $1
refused	decq
f6d8	negb %al
f6d8	neg %al
f6d9	negb %cl
f6d9	neg %cl
f6df	negb %bh
f6df	neg %bh
40f6de	negb %sil
40f6de	neg %sil
41f6d9	negb %r9b
41f6d9	neg %r9b
f61b	negb (%rbx)
f61c2500000000 3:11:sym+0	negb sym
f71b	neg (%rbx)
f65c2408	negb 8(%rsp)
f0f61b	lock negb (%rbx)
refused	negb $1
refused	negb
66f7d8	negw %ax
66f7d8	neg %ax
66f7db	negw %bx
66f7db	neg %bx
66f7de	negw %si
66f7de	neg %si
6641f7d9	negw %r9w
6641f7d9	neg %r9w
66f71b	negw (%rbx)
66f71c2500000000 4:11:sym+0	negw sym
66f75c2408	negw 8(%rsp)
66f0f71b	lock negw (%rbx)
refused	negw $1
refused	negw
f7d8	negl %eax
f7d8	neg %eax
f7db	negl %ebx
f7db	neg %ebx
f7dc	negl %esp
f7dc	neg %esp
f7dd	negl %ebp
f7dd	neg %ebp
41f7d9	negl %r9d
41f7d9	neg %r9d
f71b	negl (%rbx)
f71c2500000000 3:11:sym+0	negl sym
f75c2408	negl 8(%rsp)
f0f71b	lock negl (%rbx)
refused	negl $1
refused	negl
48f7d8	negq %rax
48f7d8	neg %rax
48f7db	negq %rbx
48f7db	neg %rbx
48f7dc	negq %rsp
48f7dc	neg %rsp
49f7dc	negq %r12
49f7dc	neg %r12
49f7dd	negq %r13
49f7dd	neg %r13
49f7d9	negq %r9
49f7d9	neg %r9
48f71b	negq (%rbx)
48f71c2500000000 4:11:sym+0	negq sym
48f75c2408	negq 8(%rsp)
f048f71b	lock negq (%rbx)
refused	negq $1
refused	negq
f6d0	notb %al
f6d0	not %al
f6d1	notb %cl
f6d1	not %cl
f6d7	notb %bh
f6d7	not %bh
40f6d6	notb %sil
40f6d6	not %sil
41f6d1	notb %r9b
41f6d1	not %r9b
f613	notb (%rbx)
f6142500000000 3:11:sym+0	notb sym
f713	not (%rbx)
f6542408	notb 8(%rsp)
f0f613	lock notb (%rbx)
refused	notb $1
refused	notb
66f7d0	notw %ax
66f7d0	not %ax
66f7d3	notw %bx
66f7d3	not %bx
66f7d6	notw %si
66f7d6	not %si
6641f7d1	notw %r9w
6641f7d1	not %r9w
66f713	notw (%rbx)
66f7142500000000 4:11:sym+0	notw sym
66f7542408	notw 8(%rsp)
66f0f713	lock notw (%rbx)
refused	notw $1
refused	notw
f7d0	notl %eax
f7d0	not %eax
f7d3	notl %ebx
f7d3	not %ebx
f7d4	notl %esp
f7d4	not %esp
f7d5	notl %ebp
f7d5	not %ebp
41f7d1	notl %r9d
41f7d1	not %r9d
f713	notl (%rbx)
f7142500000000 3:11:sym+0	notl sym
f7542408	notl 8(%rsp)
f0f713	lock notl (%rbx)
refused	notl $1
refused	notl
48f7d0	notq %rax
48f7d0	not %rax
48f7d3	notq %rbx
48f7d3	not %rbx
48f7d4	notq %rsp
48f7d4	not %rsp
49f7d4	notq %r12
49f7d4	not %r12
49f7d5	notq %r13
49f7d5	not %r13
49f7d1	notq %r9
49f7d1	not %r9
48f713	notq (%rbx)
48f7142500000000 4:11:sym+0	notq sym
48f7542408	notq 8(%rsp)
f048f713	lock notq (%rbx)
refused	notq $1
refused	notq
f6e0	mulb %al
f6e0	mul %al
f6e1	mulb %cl
f6e1	mul %cl
f6e7	mulb %bh
f6e7	mul %bh
40f6e6	mulb %sil
40f6e6	mul %sil
41f6e1	mulb %r9b
41f6e1	mul %r9b
f623	mulb (%rbx)
f6242500000000 3:11:sym+0	mulb sym
f723	mul (%rbx)
f6642408	mulb 8(%rsp)
refused	lock mulb (%rbx)
refused	mulb $1
refused	mulb
66f7e0	mulw %ax
66f7e0	mul %ax
66f7e3	mulw %bx
66f7e3	mul %bx
66f7e6	mulw %si
66f7e6	mul %si
6641f7e1	mulw %r9w
6641f7e1	mul %r9w
66f723	mulw (%rbx)
66f7242500000000 4:11:sym+0	mulw sym
66f7642408	mulw 8(%rsp)
refused	lock mulw (%rbx)
refused	mulw $1
refused	mulw
f7e0	mull %eax
f7e0	mul %eax
f7e3	mull %ebx
f7e3	mul %ebx
f7e4	mull %esp
f7e4	mul %esp
f7e5	mull %ebp
f7e5	mul %ebp
41f7e1	mull %r9d
41f7e1	mul %r9d
f723	mull (%rbx)
f7242500000000 3:11:sym+0	mull sym
f7642408	mull 8(%rsp)
refused	lock mull (%rbx)
refused	mull $1
refused	mull
48f7e0	mulq %rax
48f7e0	mul %rax
48f7e3	mulq %rbx
48f7e3	mul %rbx
48f7e4	mulq %rsp
48f7e4	mul %rsp
49f7e4	mulq %r12
49f7e4	mul %r12
49f7e5	mulq %r13
49f7e5	mul %r13
49f7e1	mulq %r9
49f7e1	mul %r9
48f723	mulq (%rbx)
48f7242500000000 4:11:sym+0	mulq sym
48f7642408	mulq 8(%rsp)
refused	lock mulq (%rbx)
refused	mulq $1
refused	mulq
f6e8	imulb %al
f6e8	imul %al
f6e9	imulb %cl
f6e9	imul %cl
f6ef	imulb %bh
f6ef	imul %bh
40f6ee	imulb %sil
40f6ee	imul %sil
41f6e9	imulb %r9b
41f6e9	imul %r9b
f62b	imulb (%rbx)
f62c2500000000 3:11:sym+0	imulb sym
f72b	imul (%rbx)
f66c2408	imulb 8(%rsp)
refused	lock imulb (%rbx)
refused	imulb $1
refused	imulb
66f7e8	imulw %ax
66f7e8	imul %ax
66f7eb	imulw %bx
66f7eb	imul %bx
66f7ee	imulw %si
66f7ee	imul %si
6641f7e9	imulw %r9w
6641f7e9	imul %r9w
66f72b	imulw (%rbx)
66f72c2500000000 4:11:sym+0	imulw sym
66f76c2408	imulw 8(%rsp)
refused	lock imulw (%rbx)
refused	imulw $1
refused	imulw
f7e8	imull %eax
f7e8	imul %eax
f7eb	imull %ebx
f7eb	imul %ebx
f7ec	imull %esp
f7ec	imul %esp
f7ed	imull %ebp
f7ed	imul %ebp
41f7e9	imull %r9d
41f7e9	imul %r9d
f72b	imull (%rbx)
f72c2500000000 3:11:sym+0	imull sym
f76c2408	imull 8(%rsp)
refused	lock imull (%rbx)
refused	imull $1
refused	imull
48f7e8	imulq %rax
48f7e8	imul %rax
48f7eb	imulq %rbx
48f7eb	imul %rbx
48f7ec	imulq %rsp
48f7ec	imul %rsp
49f7ec	imulq %r12
49f7ec	imul %r12
49f7ed	imulq %r13
49f7ed	imul %r13
49f7e9	imulq %r9
49f7e9	imul %r9
48f72b	imulq (%rbx)
48f72c2500000000 4:11:sym+0	imulq sym
48f76c2408	imulq 8(%rsp)
refused	lock imulq (%rbx)
refused	imulq $1
refused	imulq
f6f0	divb %al
f6f0	div %al
f6f1	divb %cl
f6f1	div %cl
f6f7	divb %bh
f6f7	div %bh
40f6f6	divb %sil
40f6f6	div %sil
41f6f1	divb %r9b
41f6f1	div %r9b
f633	divb (%rbx)
f6342500000000 3:11:sym+0	divb sym
f733	div (%rbx)
f6742408	divb 8(%rsp)
refused	lock divb (%rbx)
refused	divb $1
refused	divb
66f7f0	divw %ax
66f7f0	div %ax
66f7f3	divw %bx
66f7f3	div %bx
66f7f6	divw %si
66f7f6	div %si
6641f7f1	divw %r9w
6641f7f1	div %r9w
66f733	divw (%rbx)
66f7342500000000 4:11:sym+0	divw sym
66f7742408	divw 8(%rsp)
refused	lock divw (%rbx)
refused	divw $1
refused	divw
f7f0	divl %eax
f7f0	div %eax
f7f3	divl %ebx
f7f3	div %ebx
f7f4	divl %esp
f7f4	div %esp
f7f5	divl %ebp
f7f5	div %ebp
41f7f1	divl %r9d
41f7f1	div %r9d
f733	divl (%rbx)
f7342500000000 3:11:sym+0	divl sym
f7742408	divl 8(%rsp)
refused	lock divl (%rbx)
refused	divl $1
refused	divl
48f7f0	divq %rax
48f7f0	div %rax
48f7f3	divq %rbx
48f7f3	div %rbx
48f7f4	divq %rsp
48f7f4	div %rsp
49f7f4	divq %r12
49f7f4	div %r12
49f7f5	divq %r13
49f7f5	div %r13
49f7f1	divq %r9
49f7f1	div %r9
48f733	divq (%rbx)
48f7342500000000 4:11:sym+0	divq sym
48f7742408	divq 8(%rsp)
refused	lock divq (%rbx)
refused	divq $1
refused	divq
f6f8	idivb %al
f6f8	idiv %al
f6f9	idivb %cl
f6f9	idiv %cl
f6ff	idivb %bh
f6ff	idiv %bh
40f6fe	idivb %sil
40f6fe	idiv %sil
41f6f9	idivb %r9b
41f6f9	idiv %r9b
f63b	idivb (%rbx)
f63c2500000000 3:11:sym+0	idivb sym
f73b	idiv (%rbx)
f67c2408	idivb 8(%rsp)
refused	lock idivb (%rbx)
refused	idivb $1
refused	idivb
66f7f8	idivw %ax
66f7f8	idiv %ax
66f7fb	idivw %bx
66f7fb	idiv %bx
66f7fe	idivw %si
66f7fe	idiv %si
6641f7f9	idivw %r9w
6641f7f9	idiv %r9w
66f73b	idivw (%rbx)
66f73c2500000000 4:11:sym+0	idivw sym
66f77c2408	idivw 8(%rsp)
refused	lock idivw (%rbx)
refused	idivw $1
refused	idivw
f7f8	idivl %eax
f7f8	idiv %eax
f7fb	idivl %ebx
f7fb	idiv %ebx
f7fc	idivl %esp
f7fc	idiv %esp
f7fd	idivl %ebp
f7fd	idiv %ebp
41f7f9	idivl %r9d
41f7f9	idiv %r9d
f73b	idivl (%rbx)
f73c2500000000 3:11:sym+0	idivl sym
f77c2408	idivl 8(%rsp)
refused	lock idivl (%rbx)
refused	idivl $1
refused	idivl
48f7f8	idivq %rax
48f7f8	idiv %rax
48f7fb	idivq %rbx
48f7fb	idiv %rbx
48f7fc	idivq %rsp
48f7fc	idiv %rsp
49f7fc	idivq %r12
49f7fc	idiv %r12
49f7fd	idivq %r13
49f7fd	idiv %r13
49f7f9	idivq %r9
49f7f9	idiv %r9
48f73b	idivq (%rbx)
48f73c2500000000 4:11:sym+0	idivq sym
48f77c2408	idivq 8(%rsp)
refused	lock idivq (%rbx)
refused	idivq $1
refused	idivq
refused	pushb %al
refused	push %al
refused	pushb %cl
refused	push %cl
refused	pushb %bh
refused	push %bh
refused	pushb %sil
refused	push %sil
refused	pushb %r9b
refused	push %r9b
refused	pushb (%rbx)
refused	pushb sym
ff33	push (%rbx)
refused	pushb 8(%rsp)
refused	lock pushb (%rbx)
refused	pushb $1
refused	pushb
6650	pushw %ax
6650	push %ax
6653	pushw %bx
6653	push %bx
6656	pushw %si
6656	push %si
664151	pushw %r9w
664151	push %r9w
66ff33	pushw (%rbx)
66ff342500000000 4:11:sym+0	pushw sym
66ff742408	pushw 8(%rsp)
refused	lock pushw (%rbx)
refused	pushw
refused	pushl %ebx
refused	push %ebx
refused	pushl %esp
refused	push %esp
refused	pushl %ebp
refused	push %ebp
refused	pushl %r9d
refused	push %r9d
refused	pushl (%rbx)
refused	pushl sym
refused	pushl 8(%rsp)
refused	lock pushl (%rbx)
refused	pushl
50	pushq %rax
50	push %rax
53	pushq %rbx
53	push %rbx
54	pushq %rsp
54	push %rsp
4154	pushq %r12
4154	push %r12
4155	pushq %r13
4155	push %r13
4151	pushq %r9
4151	push %r9
ff33	pushq (%rbx)
ff342500000000 3:11:sym+0	pushq sym
ff742408	pushq 8(%rsp)
refused	lock pushq (%rbx)
refused	pushq
refused	popb %al
refused	pop %al
refused	popb %cl
refused	pop %cl
refused	popb %bh
refused	pop %bh
refused	popb %sil
refused	pop %sil
refused	popb %r9b
refused	pop %r9b
refused	popb (%rbx)
refused	popb sym
8f03	pop (%rbx)
refused	popb 8(%rsp)
refused	lock popb (%rbx)
refused	popb $1
refused	popb
6658	popw %ax
6658	pop %ax
665b	popw %bx
665b	pop %bx
665e	popw %si
665e	pop %si
664159	popw %r9w
664159	pop %r9w
668f03	popw (%rbx)
668f042500000000 4:11:sym+0	popw sym
668f442408	popw 8(%rsp)
refused	lock popw (%rbx)
refused	popw $1
refused	popw
refused	pop %eax
refused	popl %ebx
refused	popl %esp
refused	pop %esp
refused	popl %ebp
refused	pop %ebp
refused	popl %r9d
refused	pop %r9d
refused	popl (%rbx)
refused	popl 8(%rsp)
refused	lock popl (%rbx)
refused	popl $1
refused	popl
58	popq %rax
58	pop %rax
5b	popq %rbx
5b	pop %rbx
5c	popq %rsp
5c	pop %rsp
415c	popq %r12
415c	pop %r12
415d	popq %r13
415d	pop %r13
4159	popq %r9
4159	pop %r9
8f03	popq (%rbx)
8f042500000000 3:11:sym+0	popq sym
8f442408	popq 8(%rsp)
refused	lock popq (%rbx)
refused	popq $1
refused	popq
d0e0	salb %al
d0e0	sal %al
d0e1	salb %cl
d0e1	sal %cl
d0e7	salb %bh
d0e7	sal %bh
40d0e6	salb %sil
40d0e6	sal %sil
41d0e1	salb %r9b
41d0e1	sal %r9b
d023	salb (%rbx)
d0242500000000 3:11:sym+0	salb sym
d123	sal (%rbx)
d0642408	salb 8(%rsp)
refused	lock salb (%rbx)
refused	salb $1
refused	salb
66d1e0	salw %ax
66d1e0	sal %ax
66d1e3	salw %bx
66d1e3	sal %bx
66d1e6	salw %si
66d1e6	sal %si
6641d1e1	salw %r9w
6641d1e1	sal %r9w
66d123	salw (%rbx)
66d1242500000000 4:11:sym+0	salw sym
66d1642408	salw 8(%rsp)
refused	lock salw (%rbx)
refused	salw $1
refused	salw
d1e0	sall %eax
d1e0	sal %eax
d1e3	sall %ebx
d1e3	sal %ebx
d1e4	sall %esp
d1e4	sal %esp
d1e5	sall %ebp
d1e5	sal %ebp
41d1e1	sall %r9d
41d1e1	sal %r9d
d123	sall (%rbx)
d1242500000000 3:11:sym+0	sall sym
d1642408	sall 8(%rsp)
refused	lock sall (%rbx)
refused	sall $1
refused	sall
48d1e0	salq %rax
48d1e0	sal %rax
48d1e3	salq %rbx
48d1e3	sal %rbx
48d1e4	salq %rsp
48d1e4	sal %rsp
49d1e4	salq %r12
49d1e4	sal %r12
49d1e5	salq %r13
49d1e5	sal %r13
49d1e1	salq %r9
49d1e1	sal %r9
48d123	salq (%rbx)
48d1242500000000 4:11:sym+0	salq sym
48d1642408	salq 8(%rsp)
refused	lock salq (%rbx)
refused	salq $1
refused	salq
d0e0	shlb %al
d0e0	shl %al
d0e1	shlb %cl
d0e1	shl %cl
d0e7	shlb %bh
d0e7	shl %bh
40d0e6	shlb %sil
40d0e6	shl %sil
41d0e1	shlb %r9b
41d0e1	shl %r9b
d023	shlb (%rbx)
d0242500000000 3:11:sym+0	shlb sym
d123	shl (%rbx)
d0642408	shlb 8(%rsp)
refused	lock shlb (%rbx)
refused	shlb $1
refused	shlb
66d1e0	shlw %ax
66d1e0	shl %ax
66d1e3	shlw %bx
66d1e3	shl %bx
66d1e6	shlw %si
66d1e6	shl %si
6641d1e1	shlw %r9w
6641d1e1	shl %r9w
66d123	shlw (%rbx)
66d1242500000000 4:11:sym+0	shlw sym
66d1642408	shlw 8(%rsp)
refused	lock shlw (%rbx)
refused	shlw $1
refused	shlw
d1e0	shll %eax
d1e0	shl %eax
d1e3	shll %ebx
d1e3	shl %ebx
d1e4	shll %esp
d1e4	shl %esp
d1e5	shll %ebp
d1e5	shl %ebp
41d1e1	shll %r9d
41d1e1	shl %r9d
d123	shll (%rbx)
d1242500000000 3:11:sym+0	shll sym
d1642408	shll 8(%rsp)
refused	lock shll (%rbx)
refused	shll $1
refused	shll
48d1e0	shlq %rax
48d1e0	shl %rax
48d1e3	shlq %rbx
48d1e3	shl %rbx
48d1e4	shlq %rsp
48d1e4	shl %rsp
49d1e4	shlq %r12
49d1e4	shl %r12
49d1e5	shlq %r13
49d1e5	shl %r13
49d1e1	shlq %r9
49d1e1	shl %r9
48d123	shlq (%rbx)
48d1242500000000 4:11:sym+0	shlq sym
48d1642408	shlq 8(%rsp)
refused	lock shlq (%rbx)
refused	shlq $1
refused	shlq
d0e8	shrb %al
d0e8	shr %al
d0e9	shrb %cl
d0e9	shr %cl
d0ef	shrb %bh
d0ef	shr %bh
40d0ee	shrb %sil
40d0ee	shr %sil
41d0e9	shrb %r9b
41d0e9	shr %r9b
d02b	shrb (%rbx)
d02c2500000000 3:11:sym+0	shrb sym
d12b	shr (%rbx)
d06c2408	shrb 8(%rsp)
refused	lock shrb (%rbx)
refused	shrb $1
refused	shrb
66d1e8	shrw %ax
66d1e8	shr %ax
66d1eb	shrw %bx
66d1eb	shr %bx
66d1ee	shrw %si
66d1ee	shr %si
6641d1e9	shrw %r9w
6641d1e9	shr %r9w
66d12b	shrw (%rbx)
66d12c2500000000 4:11:sym+0	shrw sym
66d16c2408	shrw 8(%rsp)
refused	lock shrw (%rbx)
refused	shrw $1
refused	shrw
d1e8	shrl %eax
d1e8	shr %eax
d1eb	shrl %ebx
d1eb	shr %ebx
d1ec	shrl %esp
d1ec	shr %esp
d1ed	shrl %ebp
d1ed	shr %ebp
41d1e9	shrl %r9d
41d1e9	shr %r9d
d12b	shrl (%rbx)
d12c2500000000 3:11:sym+0	shrl sym
d16c2408	shrl 8(%rsp)
refused	lock shrl (%rbx)
refused	shrl $1
refused	shrl
48d1e8	shrq %rax
48d1e8	shr %rax
48d1eb	shrq %rbx
48d1eb	shr %rbx
48d1ec	shrq %rsp
48d1ec	shr %rsp
49d1ec	shrq %r12
49d1ec	shr %r12
49d1ed	shrq %r13
49d1ed	shr %r13
49d1e9	shrq %r9
49d1e9	shr %r9
48d12b	shrq (%rbx)
48d12c2500000000 4:11:sym+0	shrq sym
48d16c2408	shrq 8(%rsp)
refused	lock shrq (%rbx)
refused	shrq $1
refused	shrq
d0f8	sarb %al
d0f8	sar %al
d0f9	sarb %cl
d0f9	sar %cl
d0ff	sarb %bh
d0ff	sar %bh
40d0fe	sarb %sil
40d0fe	sar %sil
41d0f9	sarb %r9b
41d0f9	sar %r9b
d03b	sarb (%rbx)
d03c2500000000 3:11:sym+0	sarb sym
d13b	sar (%rbx)
d07c2408	sarb 8(%rsp)
refused	lock sarb (%rbx)
refused	sarb $1
refused	sarb
66d1f8	sarw %ax
66d1f8	sar %ax
66d1fb	sarw %bx
66d1fb	sar %bx
66d1fe	sarw %si
66d1fe	sar %si
6641d1f9	sarw %r9w
6641d1f9	sar %r9w
66d13b	sarw (%rbx)
66d13c2500000000 4:11:sym+0	sarw sym
66d17c2408	sarw 8(%rsp)
refused	lock sarw (%rbx)
refused	sarw $1
refused	sarw
d1f8	sarl %eax
d1f8	sar %eax
d1fb	sarl %ebx
d1fb	sar %ebx
d1fc	sarl %esp
d1fc	sar %esp
d1fd	sarl %ebp
d1fd	sar %ebp
41d1f9	sarl %r9d
41d1f9	sar %r9d
d13b	sarl (%rbx)
d13c2500000000 3:11:sym+0	sarl sym
d17c2408	sarl 8(%rsp)
refused	lock sarl (%rbx)
refused	sarl $1
refused	sarl
48d1f8	sarq %rax
48d1f8	sar %rax
48d1fb	sarq %rbx
48d1fb	sar %rbx
48d1fc	sarq %rsp
48d1fc	sar %rsp
49d1fc	sarq %r12
49d1fc	sar %r12
49d1fd	sarq %r13
49d1fd	sar %r13
49d1f9	sarq %r9
49d1f9	sar %r9
48d13b	sarq (%rbx)
48d13c2500000000 4:11:sym+0	sarq sym
48d17c2408	sarq 8(%rsp)
refused	lock sarq (%rbx)
refused	sarq $1
refused	sarq
d0c0	rolb %al
d0c0	rol %al
d0c1	rolb %cl
d0c1	rol %cl
d0c7	rolb %bh
d0c7	rol %bh
40d0c6	rolb %sil
40d0c6	rol %sil
41d0c1	rolb %r9b
41d0c1	rol %r9b
d003	rolb (%rbx)
d0042500000000 3:11:sym+0	rolb sym
d103	rol (%rbx)
d0442408	rolb 8(%rsp)
refused	lock rolb (%rbx)
refused	rolb $1
refused	rolb
66d1c0	rolw %ax
66d1c0	rol %ax
66d1c3	rolw %bx
66d1c3	rol %bx
66d1c6	rolw %si
66d1c6	rol %si
6641d1c1	rolw %r9w
6641d1c1	rol %r9w
66d103	rolw (%rbx)
66d1042500000000 4:11:sym+0	rolw sym
66d1442408	rolw 8(%rsp)
refused	lock rolw (%rbx)
refused	rolw $1
refused	rolw
d1c0	roll %eax
d1c0	rol %eax
d1c3	roll %ebx
d1c3	rol %ebx
d1c4	roll %esp
d1c4	rol %esp
d1c5	roll %ebp
d1c5	rol %ebp
41d1c1	roll %r9d
41d1c1	rol %r9d
d103	roll (%rbx)
d1042500000000 3:11:sym+0	roll sym
d1442408	roll 8(%rsp)
refused	lock roll (%rbx)
refused	roll $1
refused	roll
48d1c0	rolq %rax
48d1c0	rol %rax
48d1c3	rolq %rbx
48d1c3	rol %rbx
48d1c4	rolq %rsp
48d1c4	rol %rsp
49d1c4	rolq %r12
49d1c4	rol %r12
49d1c5	rolq %r13
49d1c5	rol %r13
49d1c1	rolq %r9
49d1c1	rol %r9
48d103	rolq (%rbx)
48d1042500000000 4:11:sym+0	rolq sym
48d1442408	rolq 8(%rsp)
refused	lock rolq (%rbx)
refused	rolq $1
refused	rolq
d0c8	rorb %al
d0c8	ror %al
d0c9	rorb %cl
d0c9	ror %cl
d0cf	rorb %bh
d0cf	ror %bh
40d0ce	rorb %sil
40d0ce	ror %sil
41d0c9	rorb %r9b
41d0c9	ror %r9b
d00b	rorb (%rbx)
d00c2500000000 3:11:sym+0	rorb sym
d10b	ror (%rbx)
d04c2408	rorb 8(%rsp)
refused	lock rorb (%rbx)
refused	rorb $1
refused	rorb
66d1c8	rorw %ax
66d1c8	ror %ax
66d1cb	rorw %bx
66d1cb	ror %bx
66d1ce	rorw %si
66d1ce	ror %si
6641d1c9	rorw %r9w
6641d1c9	ror %r9w
66d10b	rorw (%rbx)
66d10c2500000000 4:11:sym+0	rorw sym
66d14c2408	rorw 8(%rsp)
refused	lock rorw (%rbx)
refused	rorw $1
refused	rorw
d1c8	rorl %eax
d1c8	ror %eax
d1cb	rorl %ebx
d1cb	ror %ebx
d1cc	rorl %esp
d1cc	ror %esp
d1cd	rorl %ebp
d1cd	ror %ebp
41d1c9	rorl %r9d
41d1c9	ror %r9d
d10b	rorl (%rbx)
d10c2500000000 3:11:sym+0	rorl sym
d14c2408	rorl 8(%rsp)
refused	lock rorl (%rbx)
refused	rorl $1
refused	rorl
48d1c8	rorq %rax
48d1c8	ror %rax
48d1cb	rorq %rbx
48d1cb	ror %rbx
48d1cc	rorq %rsp
48d1cc	ror %rsp
49d1cc	rorq %r12
49d1cc	ror %r12
49d1cd	rorq %r13
49d1cd	ror %r13
49d1c9	rorq %r9
49d1c9	ror %r9
48d10b	rorq (%rbx)
48d10c2500000000 4:11:sym+0	rorq sym
48d14c2408	rorq 8(%rsp)
refused	lock rorq (%rbx)
refused	rorq $1
refused	rorq
d0d0	rclb %al
d0d0	rcl %al
d0d1	rclb %cl
d0d1	rcl %cl
d0d7	rclb %bh
d0d7	rcl %bh
40d0d6	rclb %sil
40d0d6	rcl %sil
41d0d1	rclb %r9b
41d0d1	rcl %r9b
d013	rclb (%rbx)
d0142500000000 3:11:sym+0	rclb sym
d113	rcl (%rbx)
d0542408	rclb 8(%rsp)
refused	lock rclb (%rbx)
refused	rclb $1
refused	rclb
66d1d0	rclw %ax
66d1d0	rcl %ax
66d1d3	rclw %bx
66d1d3	rcl %bx
66d1d6	rclw %si
66d1d6	rcl %si
6641d1d1	rclw %r9w
6641d1d1	rcl %r9w
66d113	rclw (%rbx)
66d1142500000000 4:11:sym+0	rclw sym
66d1542408	rclw 8(%rsp)
refused	lock rclw (%rbx)
refused	rclw $1
refused	rclw
d1d0	rcll %eax
d1d0	rcl %eax
d1d3	rcll %ebx
d1d3	rcl %ebx
d1d4	rcll %esp
d1d4	rcl %esp
d1d5	rcll %ebp
d1d5	rcl %ebp
41d1d1	rcll %r9d
41d1d1	rcl %r9d
d113	rcll (%rbx)
d1142500000000 3:11:sym+0	rcll sym
d1542408	rcll 8(%rsp)
refused	lock rcll (%rbx)
refused	rcll $1
refused	rcll
48d1d0	rclq %rax
48d1d0	rcl %rax
48d1d3	rclq %rbx
48d1d3	rcl %rbx
48d1d4	rclq %rsp
48d1d4	rcl %rsp
49d1d4	rclq %r12
49d1d4	rcl %r12
49d1d5	rclq %r13
49d1d5	rcl %r13
49d1d1	rclq %r9
49d1d1	rcl %r9
48d113	rclq (%rbx)
48d1142500000000 4:11:sym+0	rclq sym
48d1542408	rclq 8(%rsp)
refused	lock rclq (%rbx)
refused	rclq $1
refused	rclq
d0d8	rcrb %al
d0d8	rcr %al
d0d9	rcrb %cl
d0d9	rcr %cl
d0df	rcrb %bh
d0df	rcr %bh
40d0de	rcrb %sil
40d0de	rcr %sil
41d0d9	rcrb %r9b
41d0d9	rcr %r9b
d01b	rcrb (%rbx)
d01c2500000000 3:11:sym+0	rcrb sym
d11b	rcr (%rbx)
d05c2408	rcrb 8(%rsp)
refused	lock rcrb (%rbx)
refused	rcrb $1
refused	rcrb
66d1d8	rcrw %ax
66d1d8	rcr %ax
66d1db	rcrw %bx
66d1db	rcr %bx
66d1de	rcrw %si
66d1de	rcr %si
6641d1d9	rcrw %r9w
6641d1d9	rcr %r9w
66d11b	rcrw (%rbx)
66d11c2500000000 4:11:sym+0	rcrw sym
66d15c2408	rcrw 8(%rsp)
refused	lock rcrw (%rbx)
refused	rcrw $1
refused	rcrw
d1d8	rcrl %eax
d1d8	rcr %eax
d1db	rcrl %ebx
d1db	rcr %ebx
d1dc	rcrl %esp
d1dc	rcr %esp
d1dd	rcrl %ebp
d1dd	rcr %ebp
41d1d9	rcrl %r9d
41d1d9	rcr %r9d
d11b	rcrl (%rbx)
d11c2500000000 3:11:sym+0	rcrl sym
d15c2408	rcrl 8(%rsp)
refused	lock rcrl (%rbx)
refused	rcrl $1
refused	rcrl
48d1d8	rcrq %rax
48d1d8	rcr %rax
48d1db	rcrq %rbx
48d1db	rcr %rbx
48d1dc	rcrq %rsp
48d1dc	rcr %rsp
49d1dc	rcrq %r12
49d1dc	rcr %r12
49d1dd	rcrq %r13
49d1dd	rcr %r13
49d1d9	rcrq %r9
49d1d9	rcr %r9
48d11b	rcrq (%rbx)
48d11c2500000000 4:11:sym+0	rcrq sym
48d15c2408	rcrq 8(%rsp)
refused	lock rcrq (%rbx)
refused	rcrq $1
refused	rcrq
d0e1	salb $1,%cl
d023	salb $1,(%rbx)
d0e1	sal $1,%cl
c0e102	salb $2,%cl
c02302	salb $2,(%rbx)
c0e102	sal $2,%cl
c0e11f	salb $31,%cl
c0231f	salb $31,(%rbx)
c0e11f	sal $31,%cl
c0e100	salb $0,%cl
c02300	salb $0,(%rbx)
c0e100	sal $0,%cl
d2e1	salb %cl,%cl
d223	salb %cl,(%rbx)
d2e1	sal %cl,%cl
c0e107	salb $7,%cl
c02307	salb $7,(%rbx)
c0e107	sal $7,%cl
c0e11f	salb $0x1f,%cl
c0231f	salb $0x1f,(%rbx)
c0e11f	sal $0x1f,%cl
c0e13f	salb $63,%cl
c0233f	salb $63,(%rbx)
c0e13f	sal $63,%cl
c0e140	salb $64,%cl
c02340	salb $64,(%rbx)
c0e140	sal $64,%cl
c0e1ff	salb $-1,%cl
c023ff	salb $-1,(%rbx)
c0e1ff	sal $-1,%cl
d2242500000000 3:11:sym+0	salb %cl,sym
d323	sal %cl,(%rbx)
d123	sal $1,(%rbx)
66d1e3	salw $1,%bx
66d123	salw $1,(%rbx)
66d1e3	sal $1,%bx
66c1e302	salw $2,%bx
66c12302	salw $2,(%rbx)
66c1e302	sal $2,%bx
66c1e31f	salw $31,%bx
66c1231f	salw $31,(%rbx)
66c1e31f	sal $31,%bx
66c1e300	salw $0,%bx
66c12300	salw $0,(%rbx)
66c1e300	sal $0,%bx
66d3e3	salw %cl,%bx
66d323	salw %cl,(%rbx)
66d3e3	sal %cl,%bx
66c1e307	salw $7,%bx
66c12307	salw $7,(%rbx)
66c1e307	sal $7,%bx
66c1e31f	salw $0x1f,%bx
66c1231f	salw $0x1f,(%rbx)
66c1e31f	sal $0x1f,%bx
66c1e33f	salw $63,%bx
66c1233f	salw $63,(%rbx)
66c1e33f	sal $63,%bx
66c1e340	salw $64,%bx
66c12340	salw $64,(%rbx)
66c1e340	sal $64,%bx
refused	salw $-1,%bx
refused	salw $-1,(%rbx)
refused	sal $-1,%bx
66d3242500000000 4:11:sym+0	salw %cl,sym
refused	salw %bx,%bx
refused	salw %cl
d1e3	sall $1,%ebx
d123	sall $1,(%rbx)
d1e3	sal $1,%ebx
c1e302	sall $2,%ebx
c12302	sall $2,(%rbx)
c1e302	sal $2,%ebx
c1e31f	sall $31,%ebx
c1231f	sall $31,(%rbx)
c1e31f	sal $31,%ebx
c1e300	sall $0,%ebx
c12300	sall $0,(%rbx)
c1e300	sal $0,%ebx
d3e3	sall %cl,%ebx
d323	sall %cl,(%rbx)
d3e3	sal %cl,%ebx
c1e307	sall $7,%ebx
c12307	sall $7,(%rbx)
c1e307	sal $7,%ebx
c1e31f	sall $0x1f,%ebx
c1231f	sall $0x1f,(%rbx)
c1e31f	sal $0x1f,%ebx
c1e33f	sall $63,%ebx
c1233f	sall $63,(%rbx)
c1e33f	sal $63,%ebx
c1e340	sall $64,%ebx
c12340	sall $64,(%rbx)
c1e340	sal $64,%ebx
refused	sall $-1,%ebx
refused	sall $-1,(%rbx)
refused	sal $-1,%ebx
d3242500000000 3:11:sym+0	sall %cl,sym
refused	sall %ebx,%ebx
refused	sall %cl
48d1e3	salq $1,%rbx
48d123	salq $1,(%rbx)
48d1e3	sal $1,%rbx
48c1e302	salq $2,%rbx
48c12302	salq $2,(%rbx)
48c1e302	sal $2,%rbx
48c1e31f	salq $31,%rbx
48c1231f	salq $31,(%rbx)
48c1e31f	sal $31,%rbx
48c1e300	salq $0,%rbx
48c12300	salq $0,(%rbx)
48c1e300	sal $0,%rbx
48d3e3	salq %cl,%rbx
48d323	salq %cl,(%rbx)
48d3e3	sal %cl,%rbx
48c1e307	salq $7,%rbx
48c12307	salq $7,(%rbx)
48c1e307	sal $7,%rbx
48c1e31f	salq $0x1f,%rbx
48c1231f	salq $0x1f,(%rbx)
48c1e31f	sal $0x1f,%rbx
48c1e33f	salq $63,%rbx
48c1233f	salq $63,(%rbx)
48c1e33f	sal $63,%rbx
48c1e340	salq $64,%rbx
48c12340	salq $64,(%rbx)
48c1e340	sal $64,%rbx
refused	salq $-1,%rbx
refused	salq $-1,(%rbx)
refused	sal $-1,%rbx
48d3242500000000 4:11:sym+0	salq %cl,sym
refused	salq %rbx,%rbx
refused	salq %cl
d0e1	shlb $1,%cl
d023	shlb $1,(%rbx)
d0e1	shl $1,%cl
c0e102	shlb $2,%cl
c02302	shlb $2,(%rbx)
c0e102	shl $2,%cl
c0e11f	shlb $31,%cl
c0231f	shlb $31,(%rbx)
c0e11f	shl $31,%cl
c0e100	shlb $0,%cl
c02300	shlb $0,(%rbx)
c0e100	shl $0,%cl
d2e1	shlb %cl,%cl
d223	shlb %cl,(%rbx)
d2e1	shl %cl,%cl
c0e107	shlb $7,%cl
c02307	shlb $7,(%rbx)
c0e107	shl $7,%cl
c0e11f	shlb $0x1f,%cl
c0231f	shlb $0x1f,(%rbx)
c0e11f	shl $0x1f,%cl
c0e13f	shlb $63,%cl
c0233f	shlb $63,(%rbx)
c0e13f	shl $63,%cl
c0e140	shlb $64,%cl
c02340	shlb $64,(%rbx)
c0e140	shl $64,%cl
c0e1ff	shlb $-1,%cl
c023ff	shlb $-1,(%rbx)
c0e1ff	shl $-1,%cl
d2242500000000 3:11:sym+0	shlb %cl,sym
d323	shl %cl,(%rbx)
d123	shl $1,(%rbx)
66d1e3	shlw $1,%bx
66d123	shlw $1,(%rbx)
66d1e3	shl $1,%bx
66c1e302	shlw $2,%bx
66c12302	shlw $2,(%rbx)
66c1e302	shl $2,%bx
66c1e31f	shlw $31,%bx
66c1231f	shlw $31,(%rbx)
66c1e31f	shl $31,%bx
66c1e300	shlw $0,%bx
66c12300	shlw $0,(%rbx)
66c1e300	shl $0,%bx
66d3e3	shlw %cl,%bx
66d323	shlw %cl,(%rbx)
66d3e3	shl %cl,%bx
66c1e307	shlw $7,%bx
66c12307	shlw $7,(%rbx)
66c1e307	shl $7,%bx
66c1e31f	shlw $0x1f,%bx
66c1231f	shlw $0x1f,(%rbx)
66c1e31f	shl $0x1f,%bx
66c1e33f	shlw $63,%bx
66c1233f	shlw $63,(%rbx)
66c1e33f	shl $63,%bx
66c1e340	shlw $64,%bx
66c12340	shlw $64,(%rbx)
66c1e340	shl $64,%bx
refused	shlw $-1,%bx
refused	shlw $-1,(%rbx)
refused	shl $-1,%bx
66d3242500000000 4:11:sym+0	shlw %cl,sym
refused	shlw %bx,%bx
refused	shlw %cl
d1e3	shll $1,%ebx
d123	shll $1,(%rbx)
d1e3	shl $1,%ebx
c1e302	shll $2,%ebx
c12302	shll $2,(%rbx)
c1e302	shl $2,%ebx
c1e31f	shll $31,%ebx
c1231f	shll $31,(%rbx)
c1e31f	shl $31,%ebx
c1e300	shll $0,%ebx
c12300	shll $0,(%rbx)
c1e300	shl $0,%ebx
d3e3	shll %cl,%ebx
d323	shll %cl,(%rbx)
d3e3	shl %cl,%ebx
c1e307	shll $7,%ebx
c12307	shll $7,(%rbx)
c1e307	shl $7,%ebx
c1e31f	shll $0x1f,%ebx
c1231f	shll $0x1f,(%rbx)
c1e31f	shl $0x1f,%ebx
c1e33f	shll $63,%ebx
c1233f	shll $63,(%rbx)
c1e33f	shl $63,%ebx
c1e340	shll $64,%ebx
c12340	shll $64,(%rbx)
c1e340	shl $64,%ebx
refused	shll $-1,%ebx
refused	shll $-1,(%rbx)
refused	shl $-1,%ebx
d3242500000000 3:11:sym+0	shll %cl,sym
refused	shll %ebx,%ebx
refused	shll %cl
48d1e3	shlq $1,%rbx
48d123	shlq $1,(%rbx)
48d1e3	shl $1,%rbx
48c1e302	shlq $2,%rbx
48c12302	shlq $2,(%rbx)
48c1e302	shl $2,%rbx
48c1e31f	shlq $31,%rbx
48c1231f	shlq $31,(%rbx)
48c1e31f	shl $31,%rbx
48c1e300	shlq $0,%rbx
48c12300	shlq $0,(%rbx)
48c1e300	shl $0,%rbx
48d3e3	shlq %cl,%rbx
48d323	shlq %cl,(%rbx)
48d3e3	shl %cl,%rbx
48c1e307	shlq $7,%rbx
48c12307	shlq $7,(%rbx)
48c1e307	shl $7,%rbx
48c1e31f	shlq $0x1f,%rbx
48c1231f	shlq $0x1f,(%rbx)
48c1e31f	shl $0x1f,%rbx
48c1e33f	shlq $63,%rbx
48c1233f	shlq $63,(%rbx)
48c1e33f	shl $63,%rbx
48c1e340	shlq $64,%rbx
48c12340	shlq $64,(%rbx)
48c1e340	shl $64,%rbx
refused	shlq $-1,%rbx
refused	shlq $-1,(%rbx)
refused	shl $-1,%rbx
48d3242500000000 4:11:sym+0	shlq %cl,sym
refused	shlq %rbx,%rbx
refused	shlq %cl
d0e9	shrb $1,%cl
d02b	shrb $1,(%rbx)
d0e9	shr $1,%cl
c0e902	shrb $2,%cl
c02b02	shrb $2,(%rbx)
c0e902	shr $2,%cl
c0e91f	shrb $31,%cl
c02b1f	shrb $31,(%rbx)
c0e91f	shr $31,%cl
c0e900	shrb $0,%cl
c02b00	shrb $0,(%rbx)
c0e900	shr $0,%cl
d2e9	shrb %cl,%cl
d22b	shrb %cl,(%rbx)
d2e9	shr %cl,%cl
c0e907	shrb $7,%cl
c02b07	shrb $7,(%rbx)
c0e907	shr $7,%cl
c0e91f	shrb $0x1f,%cl
c02b1f	shrb $0x1f,(%rbx)
c0e91f	shr $0x1f,%cl
c0e93f	shrb $63,%cl
c02b3f	shrb $63,(%rbx)
c0e93f	shr $63,%cl
c0e940	shrb $64,%cl
c02b40	shrb $64,(%rbx)
c0e940	shr $64,%cl
c0e9ff	shrb $-1,%cl
c02bff	shrb $-1,(%rbx)
c0e9ff	shr $-1,%cl
d22c2500000000 3:11:sym+0	shrb %cl,sym
d32b	shr %cl,(%rbx)
d12b	shr $1,(%rbx)
66d1eb	shrw $1,%bx
66d12b	shrw $1,(%rbx)
66d1eb	shr $1,%bx
66c1eb02	shrw $2,%bx
66c12b02	shrw $2,(%rbx)
66c1eb02	shr $2,%bx
66c1eb1f	shrw $31,%bx
66c12b1f	shrw $31,(%rbx)
66c1eb1f	shr $31,%bx
66c1eb00	shrw $0,%bx
66c12b00	shrw $0,(%rbx)
66c1eb00	shr $0,%bx
66d3eb	shrw %cl,%bx
66d32b	shrw %cl,(%rbx)
66d3eb	shr %cl,%bx
66c1eb07	shrw $7,%bx
66c12b07	shrw $7,(%rbx)
66c1eb07	shr $7,%bx
66c1eb1f	shrw $0x1f,%bx
66c12b1f	shrw $0x1f,(%rbx)
66c1eb1f	shr $0x1f,%bx
66c1eb3f	shrw $63,%bx
66c12b3f	shrw $63,(%rbx)
66c1eb3f	shr $63,%bx
66c1eb40	shrw $64,%bx
66c12b40	shrw $64,(%rbx)
66c1eb40	shr $64,%bx
refused	shrw $-1,%bx
refused	shrw $-1,(%rbx)
refused	shr $-1,%bx
66d32c2500000000 4:11:sym+0	shrw %cl,sym
refused	shrw %bx,%bx
refused	shrw %cl
d1eb	shrl $1,%ebx
d12b	shrl $1,(%rbx)
d1eb	shr $1,%ebx
c1eb02	shrl $2,%ebx
c12b02	shrl $2,(%rbx)
c1eb02	shr $2,%ebx
c1eb1f	shrl $31,%ebx
c12b1f	shrl $31,(%rbx)
c1eb1f	shr $31,%ebx
c1eb00	shrl $0,%ebx
c12b00	shrl $0,(%rbx)
c1eb00	shr $0,%ebx
d3eb	shrl %cl,%ebx
d32b	shrl %cl,(%rbx)
d3eb	shr %cl,%ebx
c1eb07	shrl $7,%ebx
c12b07	shrl $7,(%rbx)
c1eb07	shr $7,%ebx
c1eb1f	shrl $0x1f,%ebx
c12b1f	shrl $0x1f,(%rbx)
c1eb1f	shr $0x1f,%ebx
c1eb3f	shrl $63,%ebx
c12b3f	shrl $63,(%rbx)
c1eb3f	shr $63,%ebx
c1eb40	shrl $64,%ebx
c12b40	shrl $64,(%rbx)
c1eb40	shr $64,%ebx
refused	shrl $-1,%ebx
refused	shrl $-1,(%rbx)
refused	shr $-1,%ebx
d32c2500000000 3:11:sym+0	shrl %cl,sym
refused	shrl %ebx,%ebx
refused	shrl %cl
48d1eb	shrq $1,%rbx
48d12b	shrq $1,(%rbx)
48d1eb	shr $1,%rbx
48c1eb02	shrq $2,%rbx
48c12b02	shrq $2,(%rbx)
48c1eb02	shr $2,%rbx
48c1eb1f	shrq $31,%rbx
48c12b1f	shrq $31,(%rbx)
48c1eb1f	shr $31,%rbx
48c1eb00	shrq $0,%rbx
48c12b00	shrq $0,(%rbx)
48c1eb00	shr $0,%rbx
48d3eb	shrq %cl,%rbx
48d32b	shrq %cl,(%rbx)
48d3eb	shr %cl,%rbx
48c1eb07	shrq $7,%rbx
48c12b07	shrq $7,(%rbx)
48c1eb07	shr $7,%rbx
48c1eb1f	shrq $0x1f,%rbx
48c12b1f	shrq $0x1f,(%rbx)
48c1eb1f	shr $0x1f,%rbx
48c1eb3f	shrq $63,%rbx
48c12b3f	shrq $63,(%rbx)
48c1eb3f	shr $63,%rbx
48c1eb40	shrq $64,%rbx
48c12b40	shrq $64,(%rbx)
48c1eb40	shr $64,%rbx
refused	shrq $-1,%rbx
refused	shrq $-1,(%rbx)
refused	shr $-1,%rbx
48d32c2500000000 4:11:sym+0	shrq %cl,sym
refused	shrq %rbx,%rbx
refused	shrq %cl
d0f9	sarb $1,%cl
d03b	sarb $1,(%rbx)
d0f9	sar $1,%cl
c0f902	sarb $2,%cl
c03b02	sarb $2,(%rbx)
c0f902	sar $2,%cl
c0f91f	sarb $31,%cl
c03b1f	sarb $31,(%rbx)
c0f91f	sar $31,%cl
c0f900	sarb $0,%cl
c03b00	sarb $0,(%rbx)
c0f900	sar $0,%cl
d2f9	sarb %cl,%cl
d23b	sarb %cl,(%rbx)
d2f9	sar %cl,%cl
c0f907	sarb $7,%cl
c03b07	sarb $7,(%rbx)
c0f907	sar $7,%cl
c0f91f	sarb $0x1f,%cl
c03b1f	sarb $0x1f,(%rbx)
c0f91f	sar $0x1f,%cl
c0f93f	sarb $63,%cl
c03b3f	sarb $63,(%rbx)
c0f93f	sar $63,%cl
c0f940	sarb $64,%cl
c03b40	sarb $64,(%rbx)
c0f940	sar $64,%cl
c0f9ff	sarb $-1,%cl
c03bff	sarb $-1,(%rbx)
c0f9ff	sar $-1,%cl
d23c2500000000 3:11:sym+0	sarb %cl,sym
d33b	sar %cl,(%rbx)
d13b	sar $1,(%rbx)
66d1fb	sarw $1,%bx
66d13b	sarw $1,(%rbx)
66d1fb	sar $1,%bx
66c1fb02	sarw $2,%bx
66c13b02	sarw $2,(%rbx)
66c1fb02	sar $2,%bx
66c1fb1f	sarw $31,%bx
66c13b1f	sarw $31,(%rbx)
66c1fb1f	sar $31,%bx
66c1fb00	sarw $0,%bx
66c13b00	sarw $0,(%rbx)
66c1fb00	sar $0,%bx
66d3fb	sarw %cl,%bx
66d33b	sarw %cl,(%rbx)
66d3fb	sar %cl,%bx
66c1fb07	sarw $7,%bx
66c13b07	sarw $7,(%rbx)
66c1fb07	sar $7,%bx
66c1fb1f	sarw $0x1f,%bx
66c13b1f	sarw $0x1f,(%rbx)
66c1fb1f	sar $0x1f,%bx
66c1fb3f	sarw $63,%bx
66c13b3f	sarw $63,(%rbx)
66c1fb3f	sar $63,%bx
66c1fb40	sarw $64,%bx
66c13b40	sarw $64,(%rbx)
66c1fb40	sar $64,%bx
refused	sarw $-1,%bx
refused	sarw $-1,(%rbx)
refused	sar $-1,%bx
66d33c2500000000 4:11:sym+0	sarw %cl,sym
refused	sarw %bx,%bx
refused	sarw %cl
d1fb	sarl $1,%ebx
d13b	sarl $1,(%rbx)
d1fb	sar $1,%ebx
c1fb02	sarl $2,%ebx
c13b02	sarl $2,(%rbx)
c1fb02	sar $2,%ebx
c1fb1f	sarl $31,%ebx
c13b1f	sarl $31,(%rbx)
c1fb1f	sar $31,%ebx
c1fb00	sarl $0,%ebx
c13b00	sarl $0,(%rbx)
c1fb00	sar $0,%ebx
d3fb	sarl %cl,%ebx
d33b	sarl %cl,(%rbx)
d3fb	sar %cl,%ebx
c1fb07	sarl $7,%ebx
c13b07	sarl $7,(%rbx)
c1fb07	sar $7,%ebx
c1fb1f	sarl $0x1f,%ebx
c13b1f	sarl $0x1f,(%rbx)
c1fb1f	sar $0x1f,%ebx
c1fb3f	sarl $63,%ebx
c13b3f	sarl $63,(%rbx)
c1fb3f	sar $63,%ebx
c1fb40	sarl $64,%ebx
c13b40	sarl $64,(%rbx)
c1fb40	sar $64,%ebx
refused	sarl $-1,%ebx
refused	sarl $-1,(%rbx)
refused	sar $-1,%ebx
d33c2500000000 3:11:sym+0	sarl %cl,sym
refused	sarl %ebx,%ebx
refused	sarl %cl
48d1fb	sarq $1,%rbx
48d13b	sarq $1,(%rbx)
48d1fb	sar $1,%rbx
48c1fb02	sarq $2,%rbx
48c13b02	sarq $2,(%rbx)
48c1fb02	sar $2,%rbx
48c1fb1f	sarq $31,%rbx
48c13b1f	sarq $31,(%rbx)
48c1fb1f	sar $31,%rbx
48c1fb00	sarq $0,%rbx
48c13b00	sarq $0,(%rbx)
48c1fb00	sar $0,%rbx
48d3fb	sarq %cl,%rbx
48d33b	sarq %cl,(%rbx)
48d3fb	sar %cl,%rbx
48c1fb07	sarq $7,%rbx
48c13b07	sarq $7,(%rbx)
48c1fb07	sar $7,%rbx
48c1fb1f	sarq $0x1f,%rbx
48c13b1f	sarq $0x1f,(%rbx)
48c1fb1f	sar $0x1f,%rbx
48c1fb3f	sarq $63,%rbx
48c13b3f	sarq $63,(%rbx)
48c1fb3f	sar $63,%rbx
48c1fb40	sarq $64,%rbx
48c13b40	sarq $64,(%rbx)
48c1fb40	sar $64,%rbx
refused	sarq $-1,%rbx
refused	sarq $-1,(%rbx)
refused	sar $-1,%rbx
48d33c2500000000 4:11:sym+0	sarq %cl,sym
refused	sarq %rbx,%rbx
refused	sarq %cl
d0c1	rolb $1,%cl
d003	rolb $1,(%rbx)
d0c1	rol $1,%cl
c0c102	rolb $2,%cl
c00302	rolb $2,(%rbx)
c0c102	rol $2,%cl
c0c11f	rolb $31,%cl
c0031f	rolb $31,(%rbx)
c0c11f	rol $31,%cl
c0c100	rolb $0,%cl
c00300	rolb $0,(%rbx)
c0c100	rol $0,%cl
d2c1	rolb %cl,%cl
d203	rolb %cl,(%rbx)
d2c1	rol %cl,%cl
c0c107	rolb $7,%cl
c00307	rolb $7,(%rbx)
c0c107	rol $7,%cl
c0c11f	rolb $0x1f,%cl
c0031f	rolb $0x1f,(%rbx)
c0c11f	rol $0x1f,%cl
c0c13f	rolb $63,%cl
c0033f	rolb $63,(%rbx)
c0c13f	rol $63,%cl
c0c140	rolb $64,%cl
c00340	rolb $64,(%rbx)
c0c140	rol $64,%cl
c0c1ff	rolb $-1,%cl
c003ff	rolb $-1,(%rbx)
c0c1ff	rol $-1,%cl
d2042500000000 3:11:sym+0	rolb %cl,sym
d303	rol %cl,(%rbx)
d103	rol $1,(%rbx)
66d1c3	rolw $1,%bx
66d103	rolw $1,(%rbx)
66d1c3	rol $1,%bx
66c1c302	rolw $2,%bx
66c10302	rolw $2,(%rbx)
66c1c302	rol $2,%bx
66c1c31f	rolw $31,%bx
66c1031f	rolw $31,(%rbx)
66c1c31f	rol $31,%bx
66c1c300	rolw $0,%bx
66c10300	rolw $0,(%rbx)
66c1c300	rol $0,%bx
66d3c3	rolw %cl,%bx
66d303	rolw %cl,(%rbx)
66d3c3	rol %cl,%bx
66c1c307	rolw $7,%bx
66c10307	rolw $7,(%rbx)
66c1c307	rol $7,%bx
66c1c31f	rolw $0x1f,%bx
66c1031f	rolw $0x1f,(%rbx)
66c1c31f	rol $0x1f,%bx
66c1c33f	rolw $63,%bx
66c1033f	rolw $63,(%rbx)
66c1c33f	rol $63,%bx
66c1c340	rolw $64,%bx
66c10340	rolw $64,(%rbx)
66c1c340	rol $64,%bx
66c1c3ff	rolw $-1,%bx
66c103ff	rolw $-1,(%rbx)
66c1c3ff	rol $-1,%bx
66d3042500000000 4:11:sym+0	rolw %cl,sym
refused	rolw %bx,%bx
refused	rolw %cl
d1c3	roll $1,%ebx
d103	roll $1,(%rbx)
d1c3	rol $1,%ebx
c1c302	roll $2,%ebx
c10302	roll $2,(%rbx)
c1c302	rol $2,%ebx
c1c31f	roll $31,%ebx
c1031f	roll $31,(%rbx)
c1c31f	rol $31,%ebx
c1c300	roll $0,%ebx
c10300	roll $0,(%rbx)
c1c300	rol $0,%ebx
d3c3	roll %cl,%ebx
d303	roll %cl,(%rbx)
d3c3	rol %cl,%ebx
c1c307	roll $7,%ebx
c10307	roll $7,(%rbx)
c1c307	rol $7,%ebx
c1c31f	roll $0x1f,%ebx
c1031f	roll $0x1f,(%rbx)
c1c31f	rol $0x1f,%ebx
c1c33f	roll $63,%ebx
c1033f	roll $63,(%rbx)
c1c33f	rol $63,%ebx
c1c340	roll $64,%ebx
c10340	roll $64,(%rbx)
c1c340	rol $64,%ebx
c1c3ff	roll $-1,%ebx
c103ff	roll $-1,(%rbx)
c1c3ff	rol $-1,%ebx
d3042500000000 3:11:sym+0	roll %cl,sym
refused	roll %ebx,%ebx
refused	roll %cl
48d1c3	rolq $1,%rbx
48d103	rolq $1,(%rbx)
48d1c3	rol $1,%rbx
48c1c302	rolq $2,%rbx
48c10302	rolq $2,(%rbx)
48c1c302	rol $2,%rbx
48c1c31f	rolq $31,%rbx
48c1031f	rolq $31,(%rbx)
48c1c31f	rol $31,%rbx
48c1c300	rolq $0,%rbx
48c10300	rolq $0,(%rbx)
48c1c300	rol $0,%rbx
48d3c3	rolq %cl,%rbx
48d303	rolq %cl,(%rbx)
48d3c3	rol %cl,%rbx
48c1c307	rolq $7,%rbx
48c10307	rolq $7,(%rbx)
48c1c307	rol $7,%rbx
48c1c31f	rolq $0x1f,%rbx
48c1031f	rolq $0x1f,(%rbx)
48c1c31f	rol $0x1f,%rbx
48c1c33f	rolq $63,%rbx
48c1033f	rolq $63,(%rbx)
48c1c33f	rol $63,%rbx
48c1c340	rolq $64,%rbx
48c10340	rolq $64,(%rbx)
48c1c340	rol $64,%rbx
48c1c3ff	rolq $-1,%rbx
48c103ff	rolq $-1,(%rbx)
48c1c3ff	rol $-1,%rbx
48d3042500000000 4:11:sym+0	rolq %cl,sym
refused	rolq %rbx,%rbx
refused	rolq %cl
d0c9	rorb $1,%cl
d00b	rorb $1,(%rbx)
d0c9	ror $1,%cl
c0c902	rorb $2,%cl
c00b02	rorb $2,(%rbx)
c0c902	ror $2,%cl
c0c91f	rorb $31,%cl
c00b1f	rorb $31,(%rbx)
c0c91f	ror $31,%cl
c0c900	rorb $0,%cl
c00b00	rorb $0,(%rbx)
c0c900	ror $0,%cl
d2c9	rorb %cl,%cl
d20b	rorb %cl,(%rbx)
d2c9	ror %cl,%cl
c0c907	rorb $7,%cl
c00b07	rorb $7,(%rbx)
c0c907	ror $7,%cl
c0c91f	rorb $0x1f,%cl
c00b1f	rorb $0x1f,(%rbx)
c0c91f	ror $0x1f,%cl
c0c93f	rorb $63,%cl
c00b3f	rorb $63,(%rbx)
c0c93f	ror $63,%cl
c0c940	rorb $64,%cl
c00b40	rorb $64,(%rbx)
c0c940	ror $64,%cl
c0c9ff	rorb $-1,%cl
c00bff	rorb $-1,(%rbx)
c0c9ff	ror $-1,%cl
d20c2500000000 3:11:sym+0	rorb %cl,sym
d30b	ror %cl,(%rbx)
d10b	ror $1,(%rbx)
66d1cb	rorw $1,%bx
66d10b	rorw $1,(%rbx)
66d1cb	ror $1,%bx
66c1cb02	rorw $2,%bx
66c10b02	rorw $2,(%rbx)
66c1cb02	ror $2,%bx
66c1cb1f	rorw $31,%bx
66c10b1f	rorw $31,(%rbx)
66c1cb1f	ror $31,%bx
66c1cb00	rorw $0,%bx
66c10b00	rorw $0,(%rbx)
66c1cb00	ror $0,%bx
66d3cb	rorw %cl,%bx
66d30b	rorw %cl,(%rbx)
66d3cb	ror %cl,%bx
66c1cb07	rorw $7,%bx
66c10b07	rorw $7,(%rbx)
66c1cb07	ror $7,%bx
66c1cb1f	rorw $0x1f,%bx
66c10b1f	rorw $0x1f,(%rbx)
66c1cb1f	ror $0x1f,%bx
66c1cb3f	rorw $63,%bx
66c10b3f	rorw $63,(%rbx)
66c1cb3f	ror $63,%bx
66c1cb40	rorw $64,%bx
66c10b40	rorw $64,(%rbx)
66c1cb40	ror $64,%bx
66c1cbff	rorw $-1,%bx
66c10bff	rorw $-1,(%rbx)
66c1cbff	ror $-1,%bx
66d30c2500000000 4:11:sym+0	rorw %cl,sym
refused	rorw %bx,%bx
refused	rorw %cl
d1cb	rorl $1,%ebx
d10b	rorl $1,(%rbx)
d1cb	ror $1,%ebx
c1cb02	rorl $2,%ebx
c10b02	rorl $2,(%rbx)
c1cb02	ror $2,%ebx
c1cb1f	rorl $31,%ebx
c10b1f	rorl $31,(%rbx)
c1cb1f	ror $31,%ebx
c1cb00	rorl $0,%ebx
c10b00	rorl $0,(%rbx)
c1cb00	ror $0,%ebx
d3cb	rorl %cl,%ebx
d30b	rorl %cl,(%rbx)
d3cb	ror %cl,%ebx
c1cb07	rorl $7,%ebx
c10b07	rorl $7,(%rbx)
c1cb07	ror $7,%ebx
c1cb1f	rorl $0x1f,%ebx
c10b1f	rorl $0x1f,(%rbx)
c1cb1f	ror $0x1f,%ebx
c1cb3f	rorl $63,%ebx
c10b3f	rorl $63,(%rbx)
c1cb3f	ror $63,%ebx
c1cb40	rorl $64,%ebx
c10b40	rorl $64,(%rbx)
c1cb40	ror $64,%ebx
c1cbff	rorl $-1,%ebx
c10bff	rorl $-1,(%rbx)
c1cbff	ror $-1,%ebx
d30c2500000000 3:11:sym+0	rorl %cl,sym
refused	rorl %ebx,%ebx
refused	rorl %cl
48d1cb	rorq $1,%rbx
48d10b	rorq $1,(%rbx)
48d1cb	ror $1,%rbx
48c1cb02	rorq $2,%rbx
48c10b02	rorq $2,(%rbx)
48c1cb02	ror $2,%rbx
48c1cb1f	rorq $31,%rbx
48c10b1f	rorq $31,(%rbx)
48c1cb1f	ror $31,%rbx
48c1cb00	rorq $0,%rbx
48c10b00	rorq $0,(%rbx)
48c1cb00	ror $0,%rbx
48d3cb	rorq %cl,%rbx
48d30b	rorq %cl,(%rbx)
48d3cb	ror %cl,%rbx
48c1cb07	rorq $7,%rbx
48c10b07	rorq $7,(%rbx)
48c1cb07	ror $7,%rbx
48c1cb1f	rorq $0x1f,%rbx
48c10b1f	rorq $0x1f,(%rbx)
48c1cb1f	ror $0x1f,%rbx
48c1cb3f	rorq $63,%rbx
48c10b3f	rorq $63,(%rbx)
48c1cb3f	ror $63,%rbx
48c1cb40	rorq $64,%rbx
48c10b40	rorq $64,(%rbx)
48c1cb40	ror $64,%rbx
48c1cbff	rorq $-1,%rbx
48c10bff	rorq $-1,(%rbx)
48c1cbff	ror $-1,%rbx
48d30c2500000000 4:11:sym+0	rorq %cl,sym
refused	rorq %rbx,%rbx
refused	rorq %cl
d0d1	rclb $1,%cl
d013	rclb $1,(%rbx)
d0d1	rcl $1,%cl
c0d102	rclb $2,%cl
c01302	rclb $2,(%rbx)
c0d102	rcl $2,%cl
c0d11f	rclb $31,%cl
c0131f	rclb $31,(%rbx)
c0d11f	rcl $31,%cl
c0d100	rclb $0,%cl
c01300	rclb $0,(%rbx)
c0d100	rcl $0,%cl
d2d1	rclb %cl,%cl
d213	rclb %cl,(%rbx)
d2d1	rcl %cl,%cl
c0d107	rclb $7,%cl
c01307	rclb $7,(%rbx)
c0d107	rcl $7,%cl
c0d11f	rclb $0x1f,%cl
c0131f	rclb $0x1f,(%rbx)
c0d11f	rcl $0x1f,%cl
c0d13f	rclb $63,%cl
c0133f	rclb $63,(%rbx)
c0d13f	rcl $63,%cl
c0d140	rclb $64,%cl
c01340	rclb $64,(%rbx)
c0d140	rcl $64,%cl
c0d1ff	rclb $-1,%cl
c013ff	rclb $-1,(%rbx)
c0d1ff	rcl $-1,%cl
d2142500000000 3:11:sym+0	rclb %cl,sym
d313	rcl %cl,(%rbx)
d113	rcl $1,(%rbx)
66d1d3	rclw $1,%bx
66d113	rclw $1,(%rbx)
66d1d3	rcl $1,%bx
66c1d302	rclw $2,%bx
66c11302	rclw $2,(%rbx)
66c1d302	rcl $2,%bx
66c1d31f	rclw $31,%bx
66c1131f	rclw $31,(%rbx)
66c1d31f	rcl $31,%bx
66c1d300	rclw $0,%bx
66c11300	rclw $0,(%rbx)
66c1d300	rcl $0,%bx
66d3d3	rclw %cl,%bx
66d313	rclw %cl,(%rbx)
66d3d3	rcl %cl,%bx
66c1d307	rclw $7,%bx
66c11307	rclw $7,(%rbx)
66c1d307	rcl $7,%bx
66c1d31f	rclw $0x1f,%bx
66c1131f	rclw $0x1f,(%rbx)
66c1d31f	rcl $0x1f,%bx
66c1d33f	rclw $63,%bx
66c1133f	rclw $63,(%rbx)
66c1d33f	rcl $63,%bx
66c1d340	rclw $64,%bx
66c11340	rclw $64,(%rbx)
66c1d340	rcl $64,%bx
refused	rclw $-1,%bx
refused	rclw $-1,(%rbx)
refused	rcl $-1,%bx
66d3142500000000 4:11:sym+0	rclw %cl,sym
refused	rclw %bx,%bx
refused	rclw %cl
d1d3	rcll $1,%ebx
d113	rcll $1,(%rbx)
d1d3	rcl $1,%ebx
c1d302	rcll $2,%ebx
c11302	rcll $2,(%rbx)
c1d302	rcl $2,%ebx
c1d31f	rcll $31,%ebx
c1131f	rcll $31,(%rbx)
c1d31f	rcl $31,%ebx
c1d300	rcll $0,%ebx
c11300	rcll $0,(%rbx)
c1d300	rcl $0,%ebx
d3d3	rcll %cl,%ebx
d313	rcll %cl,(%rbx)
d3d3	rcl %cl,%ebx
c1d307	rcll $7,%ebx
c11307	rcll $7,(%rbx)
c1d307	rcl $7,%ebx
c1d31f	rcll $0x1f,%ebx
c1131f	rcll $0x1f,(%rbx)
c1d31f	rcl $0x1f,%ebx
c1d33f	rcll $63,%ebx
c1133f	rcll $63,(%rbx)
c1d33f	rcl $63,%ebx
c1d340	rcll $64,%ebx
c11340	rcll $64,(%rbx)
c1d340	rcl $64,%ebx
refused	rcll $-1,%ebx
refused	rcll $-1,(%rbx)
refused	rcl $-1,%ebx
d3142500000000 3:11:sym+0	rcll %cl,sym
refused	rcll %ebx,%ebx
refused	rcll %cl
48d1d3	rclq $1,%rbx
48d113	rclq $1,(%rbx)
48d1d3	rcl $1,%rbx
48c1d302	rclq $2,%rbx
48c11302	rclq $2,(%rbx)
48c1d302	rcl $2,%rbx
48c1d31f	rclq $31,%rbx
48c1131f	rclq $31,(%rbx)
48c1d31f	rcl $31,%rbx
48c1d300	rclq $0,%rbx
48c11300	rclq $0,(%rbx)
48c1d300	rcl $0,%rbx
48d3d3	rclq %cl,%rbx
48d313	rclq %cl,(%rbx)
48d3d3	rcl %cl,%rbx
48c1d307	rclq $7,%rbx
48c11307	rclq $7,(%rbx)
48c1d307	rcl $7,%rbx
48c1d31f	rclq $0x1f,%rbx
48c1131f	rclq $0x1f,(%rbx)
48c1d31f	rcl $0x1f,%rbx
48c1d33f	rclq $63,%rbx
48c1133f	rclq $63,(%rbx)
48c1d33f	rcl $63,%rbx
48c1d340	rclq $64,%rbx
48c11340	rclq $64,(%rbx)
48c1d340	rcl $64,%rbx
refused	rclq $-1,%rbx
refused	rclq $-1,(%rbx)
refused	rcl $-1,%rbx
48d3142500000000 4:11:sym+0	rclq %cl,sym
refused	rclq %rbx,%rbx
refused	rclq %cl
d0d9	rcrb $1,%cl
d01b	rcrb $1,(%rbx)
d0d9	rcr $1,%cl
c0d902	rcrb $2,%cl
c01b02	rcrb $2,(%rbx)
c0d902	rcr $2,%cl
c0d91f	rcrb $31,%cl
c01b1f	rcrb $31,(%rbx)
c0d91f	rcr $31,%cl
c0d900	rcrb $0,%cl
c01b00	rcrb $0,(%rbx)
c0d900	rcr $0,%cl
d2d9	rcrb %cl,%cl
d21b	rcrb %cl,(%rbx)
d2d9	rcr %cl,%cl
c0d907	rcrb $7,%cl
c01b07	rcrb $7,(%rbx)
c0d907	rcr $7,%cl
c0d91f	rcrb $0x1f,%cl
c01b1f	rcrb $0x1f,(%rbx)
c0d91f	rcr $0x1f,%cl
c0d93f	rcrb $63,%cl
c01b3f	rcrb $63,(%rbx)
c0d93f	rcr $63,%cl
c0d940	rcrb $64,%cl
c01b40	rcrb $64,(%rbx)
c0d940	rcr $64,%cl
c0d9ff	rcrb $-1,%cl
c01bff	rcrb $-1,(%rbx)
c0d9ff	rcr $-1,%cl
d21c2500000000 3:11:sym+0	rcrb %cl,sym
d31b	rcr %cl,(%rbx)
d11b	rcr $1,(%rbx)
66d1db	rcrw $1,%bx
66d11b	rcrw $1,(%rbx)
66d1db	rcr $1,%bx
66c1db02	rcrw $2,%bx
66c11b02	rcrw $2,(%rbx)
66c1db02	rcr $2,%bx
66c1db1f	rcrw $31,%bx
66c11b1f	rcrw $31,(%rbx)
66c1db1f	rcr $31,%bx
66c1db00	rcrw $0,%bx
66c11b00	rcrw $0,(%rbx)
66c1db00	rcr $0,%bx
66d3db	rcrw %cl,%bx
66d31b	rcrw %cl,(%rbx)
66d3db	rcr %cl,%bx
66c1db07	rcrw $7,%bx
66c11b07	rcrw $7,(%rbx)
66c1db07	rcr $7,%bx
66c1db1f	rcrw $0x1f,%bx
66c11b1f	rcrw $0x1f,(%rbx)
66c1db1f	rcr $0x1f,%bx
66c1db3f	rcrw $63,%bx
66c11b3f	rcrw $63,(%rbx)
66c1db3f	rcr $63,%bx
66c1db40	rcrw $64,%bx
66c11b40	rcrw $64,(%rbx)
66c1db40	rcr $64,%bx
refused	rcrw $-1,%bx
refused	rcrw $-1,(%rbx)
refused	rcr $-1,%bx
66d31c2500000000 4:11:sym+0	rcrw %cl,sym
refused	rcrw %bx,%bx
refused	rcrw %cl
d1db	rcrl $1,%ebx
d11b	rcrl $1,(%rbx)
d1db	rcr $1,%ebx
c1db02	rcrl $2,%ebx
c11b02	rcrl $2,(%rbx)
c1db02	rcr $2,%ebx
c1db1f	rcrl $31,%ebx
c11b1f	rcrl $31,(%rbx)
c1db1f	rcr $31,%ebx
c1db00	rcrl $0,%ebx
c11b00	rcrl $0,(%rbx)
c1db00	rcr $0,%ebx
d3db	rcrl %cl,%ebx
d31b	rcrl %cl,(%rbx)
d3db	rcr %cl,%ebx
c1db07	rcrl $7,%ebx
c11b07	rcrl $7,(%rbx)
c1db07	rcr $7,%ebx
c1db1f	rcrl $0x1f,%ebx
c11b1f	rcrl $0x1f,(%rbx)
c1db1f	rcr $0x1f,%ebx
c1db3f	rcrl $63,%ebx
c11b3f	rcrl $63,(%rbx)
c1db3f	rcr $63,%ebx
c1db40	rcrl $64,%ebx
c11b40	rcrl $64,(%rbx)
c1db40	rcr $64,%ebx
refused	rcrl $-1,%ebx
refused	rcrl $-1,(%rbx)
refused	rcr $-1,%ebx
d31c2500000000 3:11:sym+0	rcrl %cl,sym
refused	rcrl %ebx,%ebx
refused	rcrl %cl
48d1db	rcrq $1,%rbx
48d11b	rcrq $1,(%rbx)
48d1db	rcr $1,%rbx
48c1db02	rcrq $2,%rbx
48c11b02	rcrq $2,(%rbx)
48c1db02	rcr $2,%rbx
48c1db1f	rcrq $31,%rbx
48c11b1f	rcrq $31,(%rbx)
48c1db1f	rcr $31,%rbx
48c1db00	rcrq $0,%rbx
48c11b00	rcrq $0,(%rbx)
48c1db00	rcr $0,%rbx
48d3db	rcrq %cl,%rbx
48d31b	rcrq %cl,(%rbx)
48d3db	rcr %cl,%rbx
48c1db07	rcrq $7,%rbx
48c11b07	rcrq $7,(%rbx)
48c1db07	rcr $7,%rbx
48c1db1f	rcrq $0x1f,%rbx
48c11b1f	rcrq $0x1f,(%rbx)
48c1db1f	rcr $0x1f,%rbx
48c1db3f	rcrq $63,%rbx
48c11b3f	rcrq $63,(%rbx)
48c1db3f	rcr $63,%rbx
48c1db40	rcrq $64,%rbx
48c11b40	rcrq $64,(%rbx)
48c1db40	rcr $64,%rbx
refused	rcrq $-1,%rbx
refused	rcrq $-1,(%rbx)
refused	rcr $-1,%rbx
48d31c2500000000 4:11:sym+0	rcrq %cl,sym
refused	rcrq %rbx,%rbx
refused	rcrq %cl
660fa4c304	shldw $4,%ax,%bx
660fa5c3	shldw %cl,%ax,%bx
660fa40304	shldw $4,%ax,(%rbx)
660fa503	shldw %cl,%ax,(%rbx)
660fa5c3	shldw %ax,%bx
660fa503	shldw %ax,(%rbx)
660fa4c304	shld $4,%ax,%bx
660fa5042500000000 5:11:sym+0	shld %cl,%ax,sym
refused	shldw $4,(%rbx),%ax
refused	shldw $256,%ax,%bx
refused	shldb $1,%al,%bl
0fa4c304	shldl $4,%eax,%ebx
0fa5c3	shldl %cl,%eax,%ebx
0fa40304	shldl $4,%eax,(%rbx)
0fa503	shldl %cl,%eax,(%rbx)
0fa5c3	shldl %eax,%ebx
0fa503	shldl %eax,(%rbx)
0fa4c304	shld $4,%eax,%ebx
0fa5042500000000 4:11:sym+0	shld %cl,%eax,sym
refused	shldl $4,(%rbx),%eax
refused	shldl $256,%eax,%ebx
480fa4c304	shldq $4,%rax,%rbx
480fa5c3	shldq %cl,%rax,%rbx
480fa40304	shldq $4,%rax,(%rbx)
480fa503	shldq %cl,%rax,(%rbx)
480fa5c3	shldq %rax,%rbx
480fa503	shldq %rax,(%rbx)
480fa4c304	shld $4,%rax,%rbx
480fa5042500000000 5:11:sym+0	shld %cl,%rax,sym
refused	shldq $4,(%rbx),%rax
refused	shldq $256,%rax,%rbx
660facc304	shrdw $4,%ax,%bx
660fadc3	shrdw %cl,%ax,%bx
660fac0304	shrdw $4,%ax,(%rbx)
660fad03	shrdw %cl,%ax,(%rbx)
660fadc3	shrdw %ax,%bx
660fad03	shrdw %ax,(%rbx)
660facc304	shrd $4,%ax,%bx
660fad042500000000 5:11:sym+0	shrd %cl,%ax,sym
refused	shrdw $4,(%rbx),%ax
refused	shrdw $256,%ax,%bx
refused	shrdb $1,%al,%bl
0facc304	shrdl $4,%eax,%ebx
0fadc3	shrdl %cl,%eax,%ebx
0fac0304	shrdl $4,%eax,(%rbx)
0fad03	shrdl %cl,%eax,(%rbx)
0fadc3	shrdl %eax,%ebx
0fad03	shrdl %eax,(%rbx)
0facc304	shrd $4,%eax,%ebx
0fad042500000000 4:11:sym+0	shrd %cl,%eax,sym
refused	shrdl $4,(%rbx),%eax
refused	shrdl $256,%eax,%ebx
480facc304	shrdq $4,%rax,%rbx
480fadc3	shrdq %cl,%rax,%rbx
480fac0304	shrdq $4,%rax,(%rbx)
480fad03	shrdq %cl,%rax,(%rbx)
480fadc3	shrdq %rax,%rbx
480fad03	shrdq %rax,(%rbx)
480facc304	shrd $4,%rax,%rbx
480fad042500000000 5:11:sym+0	shrd %cl,%rax,sym
refused	shrdq $4,(%rbx),%rax
refused	shrdq $256,%rax,%rbx
666bd802	imulw $2,%ax,%bx
666bdb02	imulw $2,%bx
666b1b02	imulw $2,(%rbx),%bx
666bdb02	imul $2,%bx
666b1b02	imul $2,(%rbx),%bx
666bd87f	imulw $127,%ax,%bx
666bdb7f	imulw $127,%bx
666b1b7f	imulw $127,(%rbx),%bx
666bdb7f	imul $127,%bx
666b1b7f	imul $127,(%rbx),%bx
6669d88000	imulw $128,%ax,%bx
6669db8000	imulw $128,%bx
66691b8000	imulw $128,(%rbx),%bx
6669db8000	imul $128,%bx
66691b8000	imul $128,(%rbx),%bx
666bd880	imulw $-128,%ax,%bx
666bdb80	imulw $-128,%bx
666b1b80	imulw $-128,(%rbx),%bx
666bdb80	imul $-128,%bx
666b1b80	imul $-128,(%rbx),%bx
6669d87fff	imulw $-129,%ax,%bx
6669db7fff	imulw $-129,%bx
66691b7fff	imulw $-129,(%rbx),%bx
6669db7fff	imul $-129,%bx
66691b7fff	imul $-129,(%rbx),%bx
6669d80010	imulw $0x1000,%ax,%bx
6669db0010	imulw $0x1000,%bx
66691b0010	imulw $0x1000,(%rbx),%bx
6669db0010	imul $0x1000,%bx
66691b0010	imul $0x1000,(%rbx),%bx
6669d80000 3:12:sym+0	imulw $sym,%ax,%bx
6669db0000 3:12:sym+0	imulw $sym,%bx
66691b0000 3:12:sym+0	imulw $sym,(%rbx),%bx
6669db0000 3:12:sym+0	imul $sym,%bx
66691b0000 3:12:sym+0	imul $sym,(%rbx),%bx
6669d8ffff	imulw $0x7fffffff,%ax,%bx
6669dbffff	imulw $0x7fffffff,%bx
66691bffff	imulw $0x7fffffff,(%rbx),%bx
6669dbffff	imul $0x7fffffff,%bx
66691bffff	imul $0x7fffffff,(%rbx),%bx
6669d87856	imulw $0x12345678,%ax,%bx
6669db7856	imulw $0x12345678,%bx
66691b7856	imulw $0x12345678,(%rbx),%bx
6669db7856	imul $0x12345678,%bx
66691b7856	imul $0x12345678,(%rbx),%bx
660fafd8	imulw %ax,%bx
660faf1b	imulw (%rbx),%bx
660fafd8	imul %ax,%bx
660faf1c2500000000 5:11:sym+0	imul sym,%bx
refused	imulw %ax,(%rbx)
refused	imulw $2,%ax,(%rbx)
refused	imulb $2,%al,%bl
refused	imulb %al,%bl
refused	mulw %ax,%bx
refused	mulw %ax,%ax
66f7f3	divw %bx,%ax
66f73b	idivw (%rbx),%ax
62f46518afc3	imulw %bx,%ax,%bx
660fa3c3	btw %ax,%bx
660fa303	btw %ax,(%rbx)
660fbae303	btw $3,%bx
660fba2303	btw $3,(%rbx)
660fbae303	bt $3,%bx
660fa3042500000000 5:11:sym+0	bt %ax,sym
0fba2303	bt $3,(%rbx)
660fbae3ff	btw $255,%bx
refused	btw $256,%bx
refused	btw (%rbx),%bx
refused	lock btw %ax,(%rbx)
refused	btb $1,%al
660fabc3	btsw %ax,%bx
660fab03	btsw %ax,(%rbx)
660fbaeb03	btsw $3,%bx
660fba2b03	btsw $3,(%rbx)
660fbaeb03	bts $3,%bx
660fab042500000000 5:11:sym+0	bts %ax,sym
0fba2b03	bts $3,(%rbx)
660fbaebff	btsw $255,%bx
refused	btsw $256,%bx
refused	btsw (%rbx),%bx
66f00fab03	lock btsw %ax,(%rbx)
refused	btsb $1,%al
660fb3c3	btrw %ax,%bx
660fb303	btrw %ax,(%rbx)
660fbaf303	btrw $3,%bx
660fba3303	btrw $3,(%rbx)
660fbaf303	btr $3,%bx
660fb3042500000000 5:11:sym+0	btr %ax,sym
0fba3303	btr $3,(%rbx)
660fbaf3ff	btrw $255,%bx
refused	btrw $256,%bx
refused	btrw (%rbx),%bx
66f00fb303	lock btrw %ax,(%rbx)
refused	btrb $1,%al
660fbbc3	btcw %ax,%bx
660fbb03	btcw %ax,(%rbx)
660fbafb03	btcw $3,%bx
660fba3b03	btcw $3,(%rbx)
660fbafb03	btc $3,%bx
660fbb042500000000 5:11:sym+0	btc %ax,sym
0fba3b03	btc $3,(%rbx)
660fbafbff	btcw $255,%bx
refused	btcw $256,%bx
refused	btcw (%rbx),%bx
66f00fbb03	lock btcw %ax,(%rbx)
refused	btcb $1,%al
660fbcd8	bsfw %ax,%bx
660fbc1b	bsfw (%rbx),%bx
660fbcd8	bsf %ax,%bx
660fbc1c2500000000 5:11:sym+0	bsf sym,%bx
refused	bsfw %ax,(%rbx)
refused	bsfw $1,%ax
660fbdd8	bsrw %ax,%bx
660fbd1b	bsrw (%rbx),%bx
660fbdd8	bsr %ax,%bx
660fbd1c2500000000 5:11:sym+0	bsr sym,%bx
refused	bsrw %ax,(%rbx)
refused	bsrw $1,%ax
660f03d8	lslw %ax,%bx
660f031b	lslw (%rbx),%bx
660f03d8	lsl %ax,%bx
660f031c2500000000 5:11:sym+0	lsl sym,%bx
refused	lslw %ax,(%rbx)
refused	lslw $1,%ax
660f02d8	larw %ax,%bx
660f021b	larw (%rbx),%bx
660f02d8	lar %ax,%bx
660f021c2500000000 5:11:sym+0	lar sym,%bx
refused	larw %ax,(%rbx)
refused	larw $1,%ax
660f40d8	cmovow %ax,%bx
660f401b	cmovow (%rbx),%bx
660f40d8	cmovo %ax,%bx
660f401c2500000000 5:11:sym+0	cmovo sym,%bx
refused	cmovow %ax,(%rbx)
refused	cmovow $1,%ax
66f30fb8d8	popcntw %ax,%bx
66f30fb81b	popcntw (%rbx),%bx
66f30fb8d8	popcnt %ax,%bx
66f30fb81c2500000000 6:11:sym+0	popcnt sym,%bx
refused	popcntw %ax,(%rbx)
refused	popcntw $1,%ax
66f30fbdd8	lzcntw %ax,%bx
66f30fbd1b	lzcntw (%rbx),%bx
66f30fbdd8	lzcnt %ax,%bx
66f30fbd1c2500000000 6:11:sym+0	lzcnt sym,%bx
refused	lzcntw %ax,(%rbx)
refused	lzcntw $1,%ax
66f30fbcd8	tzcntw %ax,%bx
66f30fbc1b	tzcntw (%rbx),%bx
66f30fbcd8	tzcnt %ax,%bx
66f30fbc1c2500000000 6:11:sym+0	tzcnt sym,%bx
refused	tzcntw %ax,(%rbx)
refused	tzcntw $1,%ax
6693	xchgw %ax,%bx
6693	xchgw %bx,%ax
66871b	xchgw %bx,(%rbx)
66871b	xchgw (%rbx),%bx
6693	xchg %bx,%ax
6687042500000000 4:11:sym+0	xchg %ax,sym
66f0871b	lock xchgw %bx,(%rbx)
6687db	xchgw %bx,%bx
6690	xchgw %ax,%ax
refused	xchgw $1,%ax
660fb1c3	cmpxchgw %ax,%bx
660fb1d8	cmpxchgw %bx,%ax
660fb11b	cmpxchgw %bx,(%rbx)
refused	cmpxchgw (%rbx),%bx
660fb1d8	cmpxchg %bx,%ax
660fb1042500000000 5:11:sym+0	cmpxchg %ax,sym
66f00fb11b	lock cmpxchgw %bx,(%rbx)
660fb1db	cmpxchgw %bx,%bx
660fb1c0	cmpxchgw %ax,%ax
refused	cmpxchgw $1,%ax
660fc1c3	xaddw %ax,%bx
660fc1d8	xaddw %bx,%ax
660fc11b	xaddw %bx,(%rbx)
refused	xaddw (%rbx),%bx
660fc1d8	xadd %bx,%ax
660fc1042500000000 5:11:sym+0	xadd %ax,sym
66f00fc11b	lock xaddw %bx,(%rbx)
660fc1db	xaddw %bx,%bx
660fc1c0	xaddw %ax,%ax
refused	xaddw $1,%ax
6685d8	test %bx,%ax
6685042500000000 4:11:sym+0	test %ax,sym
6685db	testw %bx,%bx
6bd802	imull $2,%eax,%ebx
6bdb02	imull $2,%ebx
6b1b02	imull $2,(%rbx),%ebx
6bdb02	imul $2,%ebx
6b1b02	imul $2,(%rbx),%ebx
6bd87f	imull $127,%eax,%ebx
6bdb7f	imull $127,%ebx
6b1b7f	imull $127,(%rbx),%ebx
6bdb7f	imul $127,%ebx
6b1b7f	imul $127,(%rbx),%ebx
69d880000000	imull $128,%eax,%ebx
69db80000000	imull $128,%ebx
691b80000000	imull $128,(%rbx),%ebx
69db80000000	imul $128,%ebx
691b80000000	imul $128,(%rbx),%ebx
6bd880	imull $-128,%eax,%ebx
6bdb80	imull $-128,%ebx
6b1b80	imull $-128,(%rbx),%ebx
6bdb80	imul $-128,%ebx
6b1b80	imul $-128,(%rbx),%ebx
69d87fffffff	imull $-129,%eax,%ebx
69db7fffffff	imull $-129,%ebx
691b7fffffff	imull $-129,(%rbx),%ebx
69db7fffffff	imul $-129,%ebx
691b7fffffff	imul $-129,(%rbx),%ebx
69d800100000	imull $0x1000,%eax,%ebx
69db00100000	imull $0x1000,%ebx
691b00100000	imull $0x1000,(%rbx),%ebx
69db00100000	imul $0x1000,%ebx
691b00100000	imul $0x1000,(%rbx),%ebx
69d800000000 2:10:sym+0	imull $sym,%eax,%ebx
69db00000000 2:10:sym+0	imull $sym,%ebx
691b00000000 2:10:sym+0	imull $sym,(%rbx),%ebx
69db00000000 2:10:sym+0	imul $sym,%ebx
691b00000000 2:10:sym+0	imul $sym,(%rbx),%ebx
69d8ffffff7f	imull $0x7fffffff,%eax,%ebx
69dbffffff7f	imull $0x7fffffff,%ebx
691bffffff7f	imull $0x7fffffff,(%rbx),%ebx
69dbffffff7f	imul $0x7fffffff,%ebx
691bffffff7f	imul $0x7fffffff,(%rbx),%ebx
69d878563412	imull $0x12345678,%eax,%ebx
69db78563412	imull $0x12345678,%ebx
691b78563412	imull $0x12345678,(%rbx),%ebx
69db78563412	imul $0x12345678,%ebx
691b78563412	imul $0x12345678,(%rbx),%ebx
0fafd8	imull %eax,%ebx
0faf1b	imull (%rbx),%ebx
0fafd8	imul %eax,%ebx
0faf1c2500000000 4:11:sym+0	imul sym,%ebx
refused	imull %eax,(%rbx)
refused	imull $2,%eax,(%rbx)
refused	mull %eax,%ebx
refused	mull %eax,%eax
f7f3	divl %ebx,%eax
f73b	idivl (%rbx),%eax
62f46418afc3	imull %ebx,%eax,%ebx
0fa3c3	btl %eax,%ebx
0fa303	btl %eax,(%rbx)
0fbae303	btl $3,%ebx
0fba2303	btl $3,(%rbx)
0fbae303	bt $3,%ebx
0fa3042500000000 4:11:sym+0	bt %eax,sym
0fbae3ff	btl $255,%ebx
refused	btl $256,%ebx
refused	btl (%rbx),%ebx
refused	lock btl %eax,(%rbx)
0fabc3	btsl %eax,%ebx
0fab03	btsl %eax,(%rbx)
0fbaeb03	btsl $3,%ebx
0fba2b03	btsl $3,(%rbx)
0fbaeb03	bts $3,%ebx
0fab042500000000 4:11:sym+0	bts %eax,sym
0fbaebff	btsl $255,%ebx
refused	btsl $256,%ebx
refused	btsl (%rbx),%ebx
f00fab03	lock btsl %eax,(%rbx)
0fb3c3	btrl %eax,%ebx
0fb303	btrl %eax,(%rbx)
0fbaf303	btrl $3,%ebx
0fba3303	btrl $3,(%rbx)
0fbaf303	btr $3,%ebx
0fb3042500000000 4:11:sym+0	btr %eax,sym
0fbaf3ff	btrl $255,%ebx
refused	btrl $256,%ebx
refused	btrl (%rbx),%ebx
f00fb303	lock btrl %eax,(%rbx)
0fbbc3	btcl %eax,%ebx
0fbb03	btcl %eax,(%rbx)
0fbafb03	btcl $3,%ebx
0fba3b03	btcl $3,(%rbx)
0fbafb03	btc $3,%ebx
0fbb042500000000 4:11:sym+0	btc %eax,sym
0fbafbff	btcl $255,%ebx
refused	btcl $256,%ebx
refused	btcl (%rbx),%ebx
f00fbb03	lock btcl %eax,(%rbx)
0fbcd8	bsfl %eax,%ebx
0fbc1b	bsfl (%rbx),%ebx
0fbcd8	bsf %eax,%ebx
0fbc1c2500000000 4:11:sym+0	bsf sym,%ebx
refused	bsfl %eax,(%rbx)
refused	bsfl $1,%eax
0fbdd8	bsrl %eax,%ebx
0fbd1b	bsrl (%rbx),%ebx
0fbdd8	bsr %eax,%ebx
0fbd1c2500000000 4:11:sym+0	bsr sym,%ebx
refused	bsrl %eax,(%rbx)
refused	bsrl $1,%eax
0f03d8	lsll %eax,%ebx
0f031b	lsll (%rbx),%ebx
0f03d8	lsl %eax,%ebx
0f031c2500000000 4:11:sym+0	lsl sym,%ebx
refused	lsll %eax,(%rbx)
refused	lsll $1,%eax
0f02d8	larl %eax,%ebx
0f021b	larl (%rbx),%ebx
0f02d8	lar %eax,%ebx
0f021c2500000000 4:11:sym+0	lar sym,%ebx
refused	larl %eax,(%rbx)
refused	larl $1,%eax
0f40d8	cmovol %eax,%ebx
0f401b	cmovol (%rbx),%ebx
0f40d8	cmovo %eax,%ebx
0f401c2500000000 4:11:sym+0	cmovo sym,%ebx
refused	cmovol %eax,(%rbx)
refused	cmovol $1,%eax
f30fb8d8	popcntl %eax,%ebx
f30fb81b	popcntl (%rbx),%ebx
f30fb8d8	popcnt %eax,%ebx
f30fb81c2500000000 5:11:sym+0	popcnt sym,%ebx
refused	popcntl %eax,(%rbx)
refused	popcntl $1,%eax
f30fbdd8	lzcntl %eax,%ebx
f30fbd1b	lzcntl (%rbx),%ebx
f30fbdd8	lzcnt %eax,%ebx
f30fbd1c2500000000 5:11:sym+0	lzcnt sym,%ebx
refused	lzcntl %eax,(%rbx)
refused	lzcntl $1,%eax
f30fbcd8	tzcntl %eax,%ebx
f30fbc1b	tzcntl (%rbx),%ebx
f30fbcd8	tzcnt %eax,%ebx
f30fbc1c2500000000 5:11:sym+0	tzcnt sym,%ebx
refused	tzcntl %eax,(%rbx)
refused	tzcntl $1,%eax
93	xchgl %eax,%ebx
93	xchgl %ebx,%eax
871b	xchgl %ebx,(%rbx)
871b	xchgl (%rbx),%ebx
93	xchg %ebx,%eax
87042500000000 3:11:sym+0	xchg %eax,sym
f0871b	lock xchgl %ebx,(%rbx)
87db	xchgl %ebx,%ebx
87c0	xchgl %eax,%eax
refused	xchgl $1,%eax
0fb1c3	cmpxchgl %eax,%ebx
0fb1d8	cmpxchgl %ebx,%eax
0fb11b	cmpxchgl %ebx,(%rbx)
refused	cmpxchgl (%rbx),%ebx
0fb1d8	cmpxchg %ebx,%eax
0fb1042500000000 4:11:sym+0	cmpxchg %eax,sym
f00fb11b	lock cmpxchgl %ebx,(%rbx)
0fb1db	cmpxchgl %ebx,%ebx
0fb1c0	cmpxchgl %eax,%eax
refused	cmpxchgl $1,%eax
0fc1c3	xaddl %eax,%ebx
0fc1d8	xaddl %ebx,%eax
0fc11b	xaddl %ebx,(%rbx)
refused	xaddl (%rbx),%ebx
0fc1d8	xadd %ebx,%eax
0fc1042500000000 4:11:sym+0	xadd %eax,sym
f00fc11b	lock xaddl %ebx,(%rbx)
0fc1db	xaddl %ebx,%ebx
0fc1c0	xaddl %eax,%eax
refused	xaddl $1,%eax
85d8	test %ebx,%eax
85042500000000 3:11:sym+0	test %eax,sym
85db	testl %ebx,%ebx
486bd802	imulq $2,%rax,%rbx
486bdb02	imulq $2,%rbx
486b1b02	imulq $2,(%rbx),%rbx
486bdb02	imul $2,%rbx
486b1b02	imul $2,(%rbx),%rbx
486bd87f	imulq $127,%rax,%rbx
486bdb7f	imulq $127,%rbx
486b1b7f	imulq $127,(%rbx),%rbx
486bdb7f	imul $127,%rbx
486b1b7f	imul $127,(%rbx),%rbx
4869d880000000	imulq $128,%rax,%rbx
4869db80000000	imulq $128,%rbx
48691b80000000	imulq $128,(%rbx),%rbx
4869db80000000	imul $128,%rbx
48691b80000000	imul $128,(%rbx),%rbx
486bd880	imulq $-128,%rax,%rbx
486bdb80	imulq $-128,%rbx
486b1b80	imulq $-128,(%rbx),%rbx
486bdb80	imul $-128,%rbx
486b1b80	imul $-128,(%rbx),%rbx
4869d87fffffff	imulq $-129,%rax,%rbx
4869db7fffffff	imulq $-129,%rbx
48691b7fffffff	imulq $-129,(%rbx),%rbx
4869db7fffffff	imul $-129,%rbx
48691b7fffffff	imul $-129,(%rbx),%rbx
4869d800100000	imulq $0x1000,%rax,%rbx
4869db00100000	imulq $0x1000,%rbx
48691b00100000	imulq $0x1000,(%rbx),%rbx
4869db00100000	imul $0x1000,%rbx
48691b00100000	imul $0x1000,(%rbx),%rbx
4869d800000000 3:11:sym+0	imulq $sym,%rax,%rbx
4869db00000000 3:11:sym+0	imulq $sym,%rbx
48691b00000000 3:11:sym+0	imulq $sym,(%rbx),%rbx
4869db00000000 3:11:sym+0	imul $sym,%rbx
48691b00000000 3:11:sym+0	imul $sym,(%rbx),%rbx
4869dbffffff7f	imulq $0x7fffffff,%rbx
48691bffffff7f	imulq $0x7fffffff,(%rbx),%rbx
4869dbffffff7f	imul $0x7fffffff,%rbx
48691bffffff7f	imul $0x7fffffff,(%rbx),%rbx
4869d878563412	imulq $0x12345678,%rax,%rbx
4869db78563412	imulq $0x12345678,%rbx
48691b78563412	imulq $0x12345678,(%rbx),%rbx
4869db78563412	imul $0x12345678,%rbx
48691b78563412	imul $0x12345678,(%rbx),%rbx
480fafd8	imulq %rax,%rbx
480faf1b	imulq (%rbx),%rbx
480fafd8	imul %rax,%rbx
480faf1c2500000000 5:11:sym+0	imul sym,%rbx
refused	imulq %rax,(%rbx)
refused	imulq $2,%rax,(%rbx)
refused	mulq %rax,%rbx
refused	mulq %rax,%rax
48f7f3	divq %rbx,%rax
48f73b	idivq (%rbx),%rax
62f4e418afc3	imulq %rbx,%rax,%rbx
480fa3c3	btq %rax,%rbx
480fa303	btq %rax,(%rbx)
480fbae303	btq $3,%rbx
480fba2303	btq $3,(%rbx)
480fbae303	bt $3,%rbx
480fa3042500000000 5:11:sym+0	bt %rax,sym
480fbae3ff	btq $255,%rbx
refused	btq $256,%rbx
refused	btq (%rbx),%rbx
refused	lock btq %rax,(%rbx)
480fabc3	btsq %rax,%rbx
480fab03	btsq %rax,(%rbx)
480fbaeb03	btsq $3,%rbx
480fba2b03	btsq $3,(%rbx)
480fbaeb03	bts $3,%rbx
480fab042500000000 5:11:sym+0	bts %rax,sym
480fbaebff	btsq $255,%rbx
refused	btsq $256,%rbx
refused	btsq (%rbx),%rbx
f0480fab03	lock btsq %rax,(%rbx)
480fb3c3	btrq %rax,%rbx
480fb303	btrq %rax,(%rbx)
480fbaf303	btrq $3,%rbx
480fba3303	btrq $3,(%rbx)
480fbaf303	btr $3,%rbx
480fb3042500000000 5:11:sym+0	btr %rax,sym
480fbaf3ff	btrq $255,%rbx
refused	btrq $256,%rbx
refused	btrq (%rbx),%rbx
f0480fb303	lock btrq %rax,(%rbx)
480fbbc3	btcq %rax,%rbx
480fbb03	btcq %rax,(%rbx)
480fbafb03	btcq $3,%rbx
480fba3b03	btcq $3,(%rbx)
480fbafb03	btc $3,%rbx
480fbb042500000000 5:11:sym+0	btc %rax,sym
480fbafbff	btcq $255,%rbx
refused	btcq $256,%rbx
refused	btcq (%rbx),%rbx
f0480fbb03	lock btcq %rax,(%rbx)
480fbcd8	bsfq %rax,%rbx
480fbc1b	bsfq (%rbx),%rbx
480fbcd8	bsf %rax,%rbx
480fbc1c2500000000 5:11:sym+0	bsf sym,%rbx
refused	bsfq %rax,(%rbx)
refused	bsfq $1,%rax
480fbdd8	bsrq %rax,%rbx
480fbd1b	bsrq (%rbx),%rbx
480fbdd8	bsr %rax,%rbx
480fbd1c2500000000 5:11:sym+0	bsr sym,%rbx
refused	bsrq %rax,(%rbx)
refused	bsrq $1,%rax
0f03d8	lslq %rax,%rbx
0f031b	lslq (%rbx),%rbx
0f03d8	lsl %rax,%rbx
0f031c2500000000 4:11:sym+0	lsl sym,%rbx
refused	lslq %rax,(%rbx)
refused	lslq $1,%rax
0f02d8	larq %rax,%rbx
0f021b	larq (%rbx),%rbx
0f02d8	lar %rax,%rbx
0f021c2500000000 4:11:sym+0	lar sym,%rbx
refused	larq %rax,(%rbx)
refused	larq $1,%rax
480f40d8	cmovoq %rax,%rbx
480f401b	cmovoq (%rbx),%rbx
480f40d8	cmovo %rax,%rbx
480f401c2500000000 5:11:sym+0	cmovo sym,%rbx
refused	cmovoq %rax,(%rbx)
refused	cmovoq $1,%rax
f3480fb8d8	popcntq %rax,%rbx
f3480fb81b	popcntq (%rbx),%rbx
f3480fb8d8	popcnt %rax,%rbx
f3480fb81c2500000000 6:11:sym+0	popcnt sym,%rbx
refused	popcntq %rax,(%rbx)
refused	popcntq $1,%rax
f3480fbdd8	lzcntq %rax,%rbx
f3480fbd1b	lzcntq (%rbx),%rbx
f3480fbdd8	lzcnt %rax,%rbx
f3480fbd1c2500000000 6:11:sym+0	lzcnt sym,%rbx
refused	lzcntq %rax,(%rbx)
refused	lzcntq $1,%rax
f3480fbcd8	tzcntq %rax,%rbx
f3480fbc1b	tzcntq (%rbx),%rbx
f3480fbcd8	tzcnt %rax,%rbx
f3480fbc1c2500000000 6:11:sym+0	tzcnt sym,%rbx
refused	tzcntq %rax,(%rbx)
refused	tzcntq $1,%rax
4893	xchgq %rax,%rbx
4893	xchgq %rbx,%rax
48871b	xchgq %rbx,(%rbx)
48871b	xchgq (%rbx),%rbx
4893	xchg %rbx,%rax
4887042500000000 4:11:sym+0	xchg %rax,sym
f048871b	lock xchgq %rbx,(%rbx)
4887db	xchgq %rbx,%rbx
90	xchgq %rax,%rax
refused	xchgq $1,%rax
480fb1c3	cmpxchgq %rax,%rbx
480fb1d8	cmpxchgq %rbx,%rax
480fb11b	cmpxchgq %rbx,(%rbx)
refused	cmpxchgq (%rbx),%rbx
480fb1d8	cmpxchg %rbx,%rax
480fb1042500000000 5:11:sym+0	cmpxchg %rax,sym
f0480fb11b	lock cmpxchgq %rbx,(%rbx)
480fb1db	cmpxchgq %rbx,%rbx
480fb1c0	cmpxchgq %rax,%rax
refused	cmpxchgq $1,%rax
480fc1c3	xaddq %rax,%rbx
480fc1d8	xaddq %rbx,%rax
480fc11b	xaddq %rbx,(%rbx)
refused	xaddq (%rbx),%rbx
480fc1d8	xadd %rbx,%rax
480fc1042500000000 5:11:sym+0	xadd %rax,sym
f0480fc11b	lock xaddq %rbx,(%rbx)
480fc1db	xaddq %rbx,%rbx
480fc1c0	xaddq %rax,%rax
refused	xaddq $1,%rax
4885d8	test %rbx,%rax
4885042500000000 4:11:sym+0	test %rax,sym
4885db	testq %rbx,%rbx
86c3	xchgb %al,%bl
8603	xchgb %al,(%rbx)
860b	xchgb (%rbx),%cl
0fb00b	cmpxchgb %cl,(%rbx)
0fc0ca	xaddb %cl,%dl
0fc00c2500000000 4:11:sym+0	xaddb %cl,sym
0fc70b	cmpxchg8b (%rbx)
0fc70c2500000000 4:11:sym+0	cmpxchg8b sym
f00fc70b	lock cmpxchg8b (%rbx)
refused	cmpxchg8b %eax
87c0	xchg %eax,%eax
90	nop
660f1f00	nopw (%rax)
0f1f00	nopl (%rax)
0f1f0400	nopl 0(%rax,%rax,1)
660f1f0400	nopw 0(%rax,%rax,1)
0f1f00	nopl 0x0(%rax)
0f1f8000020000	nopl 0x200(%rax)
0f1fc0	nop %eax
660f1fc0	nopw %ax
f390	pause
6693	xchg %ax,%bx
91	xchg %eax,%ecx
91	xchg %ecx,%eax
refused	lock nop
refused	lock movl %eax,(%rbx)
refused	lock incl %eax
f0	lock
f3	rep
f2	repne
f0ff00	lock; incl (%rax)
f390	rep; nop
f390	rep nop
f3a4	rep movsb
f3a5	rep movsl
f3ab	rep stosl
f3a6	repe cmpsb
f3a6	repz cmpsb
f2ae	repne scasb
f2ae	repnz scasb
f3c3	rep ret
f3c3	rep; ret
f3c3	repz ret
f3ac	rep lodsb
f36c	rep insb
f36e	rep outsb
refused	rep addl %eax,%ebx
f3a7	repe cmpsl
66f2af	repne scasw
66f3ab	rep stosw
66f3a5	rep movsw
refused	lock rep movsb
refused	rep lock addl %eax,(%rbx)
f2f00103	xacquire lock addl %eax,(%rbx)
f38903	xrelease movl %eax,(%rbx)
3effe0	notrack jmp *%rax
refused	bnd jmp sym
e800000000 1:4:sym-4	ds call sym
2e90	cs nop
refused	es movsb
6690	data16 nop
6689c3	data16 movl %eax,%ebx
refused	addr32 movl %eax,(%rax)
6790	addr32 nop
refused	addr16 nop
refused	data32 nop
66	data16
67	addr32
666a01	data16 pushq $1
66e90000 2:13:sym-2	data16 jmp sym
refused	addr32 jecxz 1f | 1:
660f0110	data16 lgdt (%rax)
66f3a4	data16 rep movsb
2e90	cs; nop
3e8903	ds; movl %eax,(%rbx)
refused	es
64	fs
65	gs
refused	ss
4890	rex.W nop
4889c3	rex64 movl %eax,%ebx
4088c3	rex movb %al,%bl
41ffc0	rex.B incl %eax
4050	rex pushq %rax
4958	rex.WB popq %rax
664890	data16 rex.W nop
9b	wait
9b	fwait
f3a4	rep; movsb
f3a6	repe; cmpsb
f2ae	repne; scasb
f00fb10b	lock; cmpxchgl %ecx,(%rbx)
f08703	lock xchgl %eax,(%rbx)
f08703	lock xchgl (%rbx),%eax
f00fba2b01	lock btsl $1,(%rbx)
f00fc103	lock xaddl %eax,(%rbx)
refused	lock cmpl %eax,(%rbx)
refused	lock testl %eax,(%rbx)
refused	lock addl %eax,%ebx
0f90c0	seto %al
0f90c7	seto %bh
0f9003	seto (%rbx)
0f90042500000000 4:11:sym+0	seto sym
0f400b	cmovol (%rbx),%ecx
660f40142500000000 5:11:sym+0	cmovo sym,%dx
700190	jo 1f | nop | 1:
9070fd	1: nop | jo 1b
0f8000000000 2:4:sym-4	jo sym
0f80c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jo 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8032ffffff	1: .skip 200 | jo 1b
4c0f40c8	cmovoq %rax,%r9
490f4001	cmovo (%r9),%rax
400f90c6	seto %sil
410f90c2	seto %r10b
410f900424	seto (%r12)
0f91c0	setno %al
0f91c7	setno %bh
0f9103	setno (%rbx)
0f91042500000000 4:11:sym+0	setno sym
0f41d8	cmovno %eax,%ebx
0f410b	cmovnol (%rbx),%ecx
660f41d8	cmovnow %ax,%bx
660f41142500000000 5:11:sym+0	cmovno sym,%dx
710190	jno 1f | nop | 1:
9071fd	1: nop | jno 1b
0f8100000000 2:4:sym-4	jno sym
0f81c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jno 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8132ffffff	1: .skip 200 | jno 1b
4c0f41c8	cmovnoq %rax,%r9
490f4101	cmovno (%r9),%rax
400f91c6	setno %sil
410f91c2	setno %r10b
410f910424	setno (%r12)
0f92c0	setb %al
0f92c7	setb %bh
0f9203	setb (%rbx)
0f92042500000000 4:11:sym+0	setb sym
0f42d8	cmovb %eax,%ebx
0f420b	cmovbl (%rbx),%ecx
660f42d8	cmovbw %ax,%bx
660f42142500000000 5:11:sym+0	cmovb sym,%dx
720190	jb 1f | nop | 1:
9072fd	1: nop | jb 1b
0f8200000000 2:4:sym-4	jb sym
0f82c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jb 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8232ffffff	1: .skip 200 | jb 1b
4c0f42c8	cmovbq %rax,%r9
490f4201	cmovb (%r9),%rax
400f92c6	setb %sil
410f92c2	setb %r10b
410f920424	setb (%r12)
0f92c0	setc %al
0f92c7	setc %bh
0f9203	setc (%rbx)
0f92042500000000 4:11:sym+0	setc sym
0f42d8	cmovc %eax,%ebx
0f420b	cmovcl (%rbx),%ecx
660f42d8	cmovcw %ax,%bx
660f42142500000000 5:11:sym+0	cmovc sym,%dx
720190	jc 1f | nop | 1:
9072fd	1: nop | jc 1b
0f8200000000 2:4:sym-4	jc sym
0f82c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jc 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8232ffffff	1: .skip 200 | jc 1b
4c0f42c8	cmovcq %rax,%r9
490f4201	cmovc (%r9),%rax
400f92c6	setc %sil
410f92c2	setc %r10b
410f920424	setc (%r12)
0f92c0	setnae %al
0f92c7	setnae %bh
0f9203	setnae (%rbx)
0f92042500000000 4:11:sym+0	setnae sym
0f42d8	cmovnae %eax,%ebx
0f420b	cmovnael (%rbx),%ecx
660f42d8	cmovnaew %ax,%bx
660f42142500000000 5:11:sym+0	cmovnae sym,%dx
720190	jnae 1f | nop | 1:
9072fd	1: nop | jnae 1b
0f8200000000 2:4:sym-4	jnae sym
0f82c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jnae 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8232ffffff	1: .skip 200 | jnae 1b
4c0f42c8	cmovnaeq %rax,%r9
490f4201	cmovnae (%r9),%rax
400f92c6	setnae %sil
410f92c2	setnae %r10b
410f920424	setnae (%r12)
0f93c0	setae %al
0f93c7	setae %bh
0f9303	setae (%rbx)
0f93042500000000 4:11:sym+0	setae sym
0f43d8	cmovae %eax,%ebx
0f430b	cmovael (%rbx),%ecx
660f43d8	cmovaew %ax,%bx
660f43142500000000 5:11:sym+0	cmovae sym,%dx
730190	jae 1f | nop | 1:
9073fd	1: nop | jae 1b
0f8300000000 2:4:sym-4	jae sym
0f83c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jae 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8332ffffff	1: .skip 200 | jae 1b
4c0f43c8	cmovaeq %rax,%r9
490f4301	cmovae (%r9),%rax
400f93c6	setae %sil
410f93c2	setae %r10b
410f930424	setae (%r12)
0f93c0	setnb %al
0f93c7	setnb %bh
0f9303	setnb (%rbx)
0f93042500000000 4:11:sym+0	setnb sym
0f43d8	cmovnb %eax,%ebx
0f430b	cmovnbl (%rbx),%ecx
660f43d8	cmovnbw %ax,%bx
660f43142500000000 5:11:sym+0	cmovnb sym,%dx
730190	jnb 1f | nop | 1:
9073fd	1: nop | jnb 1b
0f8300000000 2:4:sym-4	jnb sym
0f83c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jnb 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8332ffffff	1: .skip 200 | jnb 1b
4c0f43c8	cmovnbq %rax,%r9
490f4301	cmovnb (%r9),%rax
400f93c6	setnb %sil
410f93c2	setnb %r10b
410f930424	setnb (%r12)
0f93c0	setnc %al
0f93c7	setnc %bh
0f9303	setnc (%rbx)
0f93042500000000 4:11:sym+0	setnc sym
0f43d8	cmovnc %eax,%ebx
0f430b	cmovncl (%rbx),%ecx
660f43d8	cmovncw %ax,%bx
660f43142500000000 5:11:sym+0	cmovnc sym,%dx
730190	jnc 1f | nop | 1:
9073fd	1: nop | jnc 1b
0f8300000000 2:4:sym-4	jnc sym
0f83c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jnc 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8332ffffff	1: .skip 200 | jnc 1b
4c0f43c8	cmovncq %rax,%r9
490f4301	cmovnc (%r9),%rax
400f93c6	setnc %sil
410f93c2	setnc %r10b
410f930424	setnc (%r12)
0f94c0	sete %al
0f94c7	sete %bh
0f9403	sete (%rbx)
0f94042500000000 4:11:sym+0	sete sym
0f44d8	cmove %eax,%ebx
0f440b	cmovel (%rbx),%ecx
660f44d8	cmovew %ax,%bx
660f44142500000000 5:11:sym+0	cmove sym,%dx
740190	je 1f | nop | 1:
9074fd	1: nop | je 1b
0f84c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	je 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8432ffffff	1: .skip 200 | je 1b
4c0f44c8	cmoveq %rax,%r9
490f4401	cmove (%r9),%rax
400f94c6	sete %sil
410f94c2	sete %r10b
410f940424	sete (%r12)
0f94c0	setz %al
0f94c7	setz %bh
0f9403	setz (%rbx)
0f94042500000000 4:11:sym+0	setz sym
0f44d8	cmovz %eax,%ebx
0f440b	cmovzl (%rbx),%ecx
660f44d8	cmovzw %ax,%bx
660f44142500000000 5:11:sym+0	cmovz sym,%dx
740190	jz 1f | nop | 1:
9074fd	1: nop | jz 1b
0f8400000000 2:4:sym-4	jz sym
0f84c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jz 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8432ffffff	1: .skip 200 | jz 1b
4c0f44c8	cmovzq %rax,%r9
490f4401	cmovz (%r9),%rax
400f94c6	setz %sil
410f94c2	setz %r10b
410f940424	setz (%r12)
0f95c0	setne %al
0f95c7	setne %bh
0f9503	setne (%rbx)
0f95042500000000 4:11:sym+0	setne sym
0f45d8	cmovne %eax,%ebx
0f450b	cmovnel (%rbx),%ecx
660f45d8	cmovnew %ax,%bx
660f45142500000000 5:11:sym+0	cmovne sym,%dx
750190	jne 1f | nop | 1:
9075fd	1: nop | jne 1b
0f8500000000 2:4:sym-4	jne sym
0f85c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jne 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8532ffffff	1: .skip 200 | jne 1b
4c0f45c8	cmovneq %rax,%r9
490f4501	cmovne (%r9),%rax
400f95c6	setne %sil
410f95c2	setne %r10b
410f950424	setne (%r12)
0f95c0	setnz %al
0f95c7	setnz %bh
0f9503	setnz (%rbx)
0f95042500000000 4:11:sym+0	setnz sym
0f45d8	cmovnz %eax,%ebx
0f450b	cmovnzl (%rbx),%ecx
660f45d8	cmovnzw %ax,%bx
660f45142500000000 5:11:sym+0	cmovnz sym,%dx
750190	jnz 1f | nop | 1:
9075fd	1: nop | jnz 1b
0f8500000000 2:4:sym-4	jnz sym
0f85c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jnz 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8532ffffff	1: .skip 200 | jnz 1b
4c0f45c8	cmovnzq %rax,%r9
490f4501	cmovnz (%r9),%rax
400f95c6	setnz %sil
410f95c2	setnz %r10b
410f950424	setnz (%r12)
0f96c0	setbe %al
0f96c7	setbe %bh
0f9603	setbe (%rbx)
0f96042500000000 4:11:sym+0	setbe sym
0f46d8	cmovbe %eax,%ebx
0f460b	cmovbel (%rbx),%ecx
660f46d8	cmovbew %ax,%bx
660f46142500000000 5:11:sym+0	cmovbe sym,%dx
760190	jbe 1f | nop | 1:
9076fd	1: nop | jbe 1b
0f8600000000 2:4:sym-4	jbe sym
0f86c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jbe 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8632ffffff	1: .skip 200 | jbe 1b
4c0f46c8	cmovbeq %rax,%r9
490f4601	cmovbe (%r9),%rax
400f96c6	setbe %sil
410f96c2	setbe %r10b
410f960424	setbe (%r12)
0f96c0	setna %al
0f96c7	setna %bh
0f9603	setna (%rbx)
0f96042500000000 4:11:sym+0	setna sym
0f46d8	cmovna %eax,%ebx
0f460b	cmovnal (%rbx),%ecx
660f46d8	cmovnaw %ax,%bx
660f46142500000000 5:11:sym+0	cmovna sym,%dx
760190	jna 1f | nop | 1:
9076fd	1: nop | jna 1b
0f8600000000 2:4:sym-4	jna sym
0f86c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jna 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8632ffffff	1: .skip 200 | jna 1b
4c0f46c8	cmovnaq %rax,%r9
490f4601	cmovna (%r9),%rax
400f96c6	setna %sil
410f96c2	setna %r10b
410f960424	setna (%r12)
0f97c0	seta %al
0f97c7	seta %bh
0f9703	seta (%rbx)
0f97042500000000 4:11:sym+0	seta sym
0f47d8	cmova %eax,%ebx
0f470b	cmoval (%rbx),%ecx
660f47d8	cmovaw %ax,%bx
660f47142500000000 5:11:sym+0	cmova sym,%dx
770190	ja 1f | nop | 1:
9077fd	1: nop | ja 1b
0f8700000000 2:4:sym-4	ja sym
0f87c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	ja 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8732ffffff	1: .skip 200 | ja 1b
4c0f47c8	cmovaq %rax,%r9
490f4701	cmova (%r9),%rax
400f97c6	seta %sil
410f97c2	seta %r10b
410f970424	seta (%r12)
0f97c0	setnbe %al
0f97c7	setnbe %bh
0f9703	setnbe (%rbx)
0f97042500000000 4:11:sym+0	setnbe sym
0f47d8	cmovnbe %eax,%ebx
0f470b	cmovnbel (%rbx),%ecx
660f47d8	cmovnbew %ax,%bx
660f47142500000000 5:11:sym+0	cmovnbe sym,%dx
770190	jnbe 1f | nop | 1:
9077fd	1: nop | jnbe 1b
0f8700000000 2:4:sym-4	jnbe sym
0f87c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jnbe 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8732ffffff	1: .skip 200 | jnbe 1b
4c0f47c8	cmovnbeq %rax,%r9
490f4701	cmovnbe (%r9),%rax
400f97c6	setnbe %sil
410f97c2	setnbe %r10b
410f970424	setnbe (%r12)
0f98c0	sets %al
0f98c7	sets %bh
0f9803	sets (%rbx)
0f98042500000000 4:11:sym+0	sets sym
0f48d8	cmovs %eax,%ebx
0f480b	cmovsl (%rbx),%ecx
660f48d8	cmovsw %ax,%bx
660f48142500000000 5:11:sym+0	cmovs sym,%dx
780190	js 1f | nop | 1:
9078fd	1: nop | js 1b
0f8800000000 2:4:sym-4	js sym
0f88c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	js 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8832ffffff	1: .skip 200 | js 1b
4c0f48c8	cmovsq %rax,%r9
490f4801	cmovs (%r9),%rax
400f98c6	sets %sil
410f98c2	sets %r10b
410f980424	sets (%r12)
0f99c0	setns %al
0f99c7	setns %bh
0f9903	setns (%rbx)
0f99042500000000 4:11:sym+0	setns sym
0f49d8	cmovns %eax,%ebx
0f490b	cmovnsl (%rbx),%ecx
660f49d8	cmovnsw %ax,%bx
660f49142500000000 5:11:sym+0	cmovns sym,%dx
790190	jns 1f | nop | 1:
9079fd	1: nop | jns 1b
0f8900000000 2:4:sym-4	jns sym
0f89c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jns 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8932ffffff	1: .skip 200 | jns 1b
4c0f49c8	cmovnsq %rax,%r9
490f4901	cmovns (%r9),%rax
400f99c6	setns %sil
410f99c2	setns %r10b
410f990424	setns (%r12)
0f9ac0	setp %al
0f9ac7	setp %bh
0f9a03	setp (%rbx)
0f9a042500000000 4:11:sym+0	setp sym
0f4ad8	cmovp %eax,%ebx
0f4a0b	cmovpl (%rbx),%ecx
660f4ad8	cmovpw %ax,%bx
660f4a142500000000 5:11:sym+0	cmovp sym,%dx
7a0190	jp 1f | nop | 1:
907afd	1: nop | jp 1b
0f8a00000000 2:4:sym-4	jp sym
0f8ac80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jp 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8a32ffffff	1: .skip 200 | jp 1b
4c0f4ac8	cmovpq %rax,%r9
490f4a01	cmovp (%r9),%rax
400f9ac6	setp %sil
410f9ac2	setp %r10b
410f9a0424	setp (%r12)
0f9ac0	setpe %al
0f9ac7	setpe %bh
0f9a03	setpe (%rbx)
0f9a042500000000 4:11:sym+0	setpe sym
0f4ad8	cmovpe %eax,%ebx
0f4a0b	cmovpel (%rbx),%ecx
660f4ad8	cmovpew %ax,%bx
660f4a142500000000 5:11:sym+0	cmovpe sym,%dx
7a0190	jpe 1f | nop | 1:
907afd	1: nop | jpe 1b
0f8a00000000 2:4:sym-4	jpe sym
0f8ac80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jpe 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8a32ffffff	1: .skip 200 | jpe 1b
4c0f4ac8	cmovpeq %rax,%r9
490f4a01	cmovpe (%r9),%rax
400f9ac6	setpe %sil
410f9ac2	setpe %r10b
410f9a0424	setpe (%r12)
0f9bc0	setnp %al
0f9bc7	setnp %bh
0f9b03	setnp (%rbx)
0f9b042500000000 4:11:sym+0	setnp sym
0f4bd8	cmovnp %eax,%ebx
0f4b0b	cmovnpl (%rbx),%ecx
660f4bd8	cmovnpw %ax,%bx
660f4b142500000000 5:11:sym+0	cmovnp sym,%dx
7b0190	jnp 1f | nop | 1:
907bfd	1: nop | jnp 1b
0f8b00000000 2:4:sym-4	jnp sym
0f8bc80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jnp 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8b32ffffff	1: .skip 200 | jnp 1b
4c0f4bc8	cmovnpq %rax,%r9
490f4b01	cmovnp (%r9),%rax
400f9bc6	setnp %sil
410f9bc2	setnp %r10b
410f9b0424	setnp (%r12)
0f9bc0	setpo %al
0f9bc7	setpo %bh
0f9b03	setpo (%rbx)
0f9b042500000000 4:11:sym+0	setpo sym
0f4bd8	cmovpo %eax,%ebx
0f4b0b	cmovpol (%rbx),%ecx
660f4bd8	cmovpow %ax,%bx
660f4b142500000000 5:11:sym+0	cmovpo sym,%dx
7b0190	jpo 1f | nop | 1:
907bfd	1: nop | jpo 1b
0f8b00000000 2:4:sym-4	jpo sym
0f8bc80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jpo 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8b32ffffff	1: .skip 200 | jpo 1b
4c0f4bc8	cmovpoq %rax,%r9
490f4b01	cmovpo (%r9),%rax
400f9bc6	setpo %sil
410f9bc2	setpo %r10b
410f9b0424	setpo (%r12)
0f9cc0	setl %al
0f9cc7	setl %bh
0f9c03	setl (%rbx)
0f9c042500000000 4:11:sym+0	setl sym
0f4cd8	cmovl %eax,%ebx
0f4c0b	cmovll (%rbx),%ecx
660f4cd8	cmovlw %ax,%bx
660f4c142500000000 5:11:sym+0	cmovl sym,%dx
7c0190	jl 1f | nop | 1:
907cfd	1: nop | jl 1b
0f8c00000000 2:4:sym-4	jl sym
0f8cc80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jl 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8c32ffffff	1: .skip 200 | jl 1b
4c0f4cc8	cmovlq %rax,%r9
490f4c01	cmovl (%r9),%rax
400f9cc6	setl %sil
410f9cc2	setl %r10b
410f9c0424	setl (%r12)
0f9cc0	setnge %al
0f9cc7	setnge %bh
0f9c03	setnge (%rbx)
0f9c042500000000 4:11:sym+0	setnge sym
0f4cd8	cmovnge %eax,%ebx
0f4c0b	cmovngel (%rbx),%ecx
660f4cd8	cmovngew %ax,%bx
660f4c142500000000 5:11:sym+0	cmovnge sym,%dx
7c0190	jnge 1f | nop | 1:
907cfd	1: nop | jnge 1b
0f8c00000000 2:4:sym-4	jnge sym
0f8cc80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jnge 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8c32ffffff	1: .skip 200 | jnge 1b
4c0f4cc8	cmovngeq %rax,%r9
490f4c01	cmovnge (%r9),%rax
400f9cc6	setnge %sil
410f9cc2	setnge %r10b
410f9c0424	setnge (%r12)
0f9dc0	setge %al
0f9dc7	setge %bh
0f9d03	setge (%rbx)
0f9d042500000000 4:11:sym+0	setge sym
0f4dd8	cmovge %eax,%ebx
0f4d0b	cmovgel (%rbx),%ecx
660f4dd8	cmovgew %ax,%bx
660f4d142500000000 5:11:sym+0	cmovge sym,%dx
7d0190	jge 1f | nop | 1:
907dfd	1: nop | jge 1b
0f8d00000000 2:4:sym-4	jge sym
0f8dc80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jge 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8d32ffffff	1: .skip 200 | jge 1b
4c0f4dc8	cmovgeq %rax,%r9
490f4d01	cmovge (%r9),%rax
400f9dc6	setge %sil
410f9dc2	setge %r10b
410f9d0424	setge (%r12)
0f9dc0	setnl %al
0f9dc7	setnl %bh
0f9d03	setnl (%rbx)
0f9d042500000000 4:11:sym+0	setnl sym
0f4dd8	cmovnl %eax,%ebx
0f4d0b	cmovnll (%rbx),%ecx
660f4dd8	cmovnlw %ax,%bx
660f4d142500000000 5:11:sym+0	cmovnl sym,%dx
7d0190	jnl 1f | nop | 1:
907dfd	1: nop | jnl 1b
0f8d00000000 2:4:sym-4	jnl sym
0f8dc80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jnl 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8d32ffffff	1: .skip 200 | jnl 1b
4c0f4dc8	cmovnlq %rax,%r9
490f4d01	cmovnl (%r9),%rax
400f9dc6	setnl %sil
410f9dc2	setnl %r10b
410f9d0424	setnl (%r12)
0f9ec0	setle %al
0f9ec7	setle %bh
0f9e03	setle (%rbx)
0f9e042500000000 4:11:sym+0	setle sym
0f4ed8	cmovle %eax,%ebx
0f4e0b	cmovlel (%rbx),%ecx
660f4ed8	cmovlew %ax,%bx
660f4e142500000000 5:11:sym+0	cmovle sym,%dx
7e0190	jle 1f | nop | 1:
907efd	1: nop | jle 1b
0f8e00000000 2:4:sym-4	jle sym
0f8ec80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jle 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8e32ffffff	1: .skip 200 | jle 1b
4c0f4ec8	cmovleq %rax,%r9
490f4e01	cmovle (%r9),%rax
400f9ec6	setle %sil
410f9ec2	setle %r10b
410f9e0424	setle (%r12)
0f9ec0	setng %al
0f9ec7	setng %bh
0f9e03	setng (%rbx)
0f9e042500000000 4:11:sym+0	setng sym
0f4ed8	cmovng %eax,%ebx
0f4e0b	cmovngl (%rbx),%ecx
660f4ed8	cmovngw %ax,%bx
660f4e142500000000 5:11:sym+0	cmovng sym,%dx
7e0190	jng 1f | nop | 1:
907efd	1: nop | jng 1b
0f8e00000000 2:4:sym-4	jng sym
0f8ec80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jng 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8e32ffffff	1: .skip 200 | jng 1b
4c0f4ec8	cmovngq %rax,%r9
490f4e01	cmovng (%r9),%rax
400f9ec6	setng %sil
410f9ec2	setng %r10b
410f9e0424	setng (%r12)
0f9fc0	setg %al
0f9fc7	setg %bh
0f9f03	setg (%rbx)
0f9f042500000000 4:11:sym+0	setg sym
0f4fd8	cmovg %eax,%ebx
0f4f0b	cmovgl (%rbx),%ecx
660f4fd8	cmovgw %ax,%bx
660f4f142500000000 5:11:sym+0	cmovg sym,%dx
7f0190	jg 1f | nop | 1:
907ffd	1: nop | jg 1b
0f8f00000000 2:4:sym-4	jg sym
0f8fc80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jg 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8f32ffffff	1: .skip 200 | jg 1b
4c0f4fc8	cmovgq %rax,%r9
490f4f01	cmovg (%r9),%rax
400f9fc6	setg %sil
410f9fc2	setg %r10b
410f9f0424	setg (%r12)
0f9fc0	setnle %al
0f9fc7	setnle %bh
0f9f03	setnle (%rbx)
0f9f042500000000 4:11:sym+0	setnle sym
0f4fd8	cmovnle %eax,%ebx
0f4f0b	cmovnlel (%rbx),%ecx
660f4fd8	cmovnlew %ax,%bx
660f4f142500000000 5:11:sym+0	cmovnle sym,%dx
7f0190	jnle 1f | nop | 1:
907ffd	1: nop | jnle 1b
0f8f00000000 2:4:sym-4	jnle sym
0f8fc80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jnle 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8f32ffffff	1: .skip 200 | jnle 1b
4c0f4fc8	cmovnleq %rax,%r9
490f4f01	cmovnle (%r9),%rax
400f9fc6	setnle %sil
410f9fc2	setnle %r10b
410f9f0424	setnle (%r12)
refused	seta %ax
62f47f1847c0	seta %eax
0f97c0	setab %al
refused	cmovab %al,%bl
refused	cmova %eax,(%rbx)
refused	cmova $1,%eax
refused	ja %eax
refused	ja *%eax
refused	ja $1
eb00	jmp 1f | 1:
ebfe	1: jmp 1b
eb7f00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jmp 1f | .skip 127 | 1:
e9800000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jmp 1f | .skip 128 | 1:
000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000eb80	1: .skip 126 | jmp 1b
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000e97cffffff	1: .skip 127 | jmp 1b
747f00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	je 1f | .skip 127 | 1:
0f84800000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	je 1f | .skip 128 | 1:
0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000007480	1: .skip 126 | je 1b
000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f847bffffff	1: .skip 127 | je 1b
ebfe	jmp .
eb00	jmp .+2
e9fb000000	jmp .+0x100
e8fbffffff	call .
e800000000	call .+5
74fe	je .
7400	je .+2
eb0190	jmp 1f+1 | 1: nop
e200	loop 1f | 1:
e2fe	1: loop 1b
e100	loope 1f | 1:
e000	loopne 1f | 1:
e100	loopz 1f | 1:
e000	loopnz 1f | 1:
refused	loop 1f | .skip 200 | 1:
refused	jecxz 1f | .skip 200 | 1:
67e200	loopl 1f | 1:
refused	loopw 1f | 1:
e900000000e900000000 1:4:sym-4 6:4:sym-4	jmp sym | jmp sym
e800000000	call 1f | 1:
e82c010000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	call 1f | .skip 300 | 1:
000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000e8cffeffff	1: .skip 300 | call 1b
eb7a7478000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jmp 1f | je 1f | .skip 120 | 1:
e9e200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000eb650000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000090	jmp 2f | 1: .skip 124 | jmp 1f | .skip 100 | 2: nop | 1:
6a7f	push $127
6880000000	push $128
6a80	push $-128
687fffffff	push $-129
6800100000	push $0x1000
66680010	pushw $0x1000
666aff	pushw $0xffff
66688000	pushw $128
refused	pushl $0x12345678
66ff30	pushw (%rax)
668f00	popw (%rax)
ff30	push (%rax)
8f00	pop (%rax)
refused	pop $1
8f042500000000 3:11:sym+0	pop sym
8f442408	pop 8(%rsp)
refused	pushq %rax,%rbx
51	pushq %rcx
52	pushq %rdx
55	pushq %rbp
56	pushq %rsi
57	pushq %rdi
4150	pushq %r8
4152	pushq %r10
4153	pushq %r11
4156	pushq %r14
4157	pushq %r15
59	popq %rcx
5a	popq %rdx
5d	popq %rbp
5e	popq %rsi
5f	popq %rdi
4158	popq %r8
415a	popq %r10
415b	popq %r11
415e	popq %r14
415f	popq %r15
51	push %rcx
52	push %rdx
55	push %rbp
56	push %rsi
57	push %rdi
4150	push %r8
4152	push %r10
4153	push %r11
4156	push %r14
4157	push %r15
41ff31	pushq (%r9)
418f4500	popq (%r13)
c20800	ret $8
c20000	ret $0
ca0400	lret $4
c8080000	enter $8,$0
c8001003	enter $0x1000,$3
refused	enter $8
66c8080000	enterw $8,$0
c8080000	enterq $8,$0
f4	hlt
fa	cli
fb	sti
fc	cld
fd	std
f8	clc
f9	stc
f5	cmc
0fa2	cpuid
0f31	rdtsc
0f01f9	rdtscp
0f32	rdmsr
0f30	wrmsr
0f33	rdpmc
cd80	int $0x80
cd00	int $0
cdff	int $255
f1	int1
refused	icebp
0f0b	ud2
0f0b	ud2a
0fb9	ud2b
0fb9d8	ud1 %eax,%ebx
0fffd8	ud0 %eax,%ebx
9f	lahf
9e	sahf
6698	cbw
98	cwde
6699	cwd
99	cdq
6698	cbtw
98	cwtl
6699	cwtd
99	cltd
d7	xlat
d7	xlatb
d7	xlat (%rbx)
0f06	clts
0f08	invd
0f09	wbinvd
0faef8	sfence
0faee8	lfence
0faef0	mfence
0f77	emms
0f0e	femms
0f01c8	monitor
0f01c9	mwait
0f01d0	xgetbv
0f01d1	xsetbv
0f01c1	vmcall
0f01c2	vmlaunch
0f01c3	vmresume
0f01c4	vmxoff
0f01cb	stac
0f01ca	clac
0faa	rsm
0fc7f0	rdrand %eax
0fc7f8	rdseed %eax
refused	rdrandw %ax
refused	rdrandl %ebx
0fc8	bswap %eax
0fcb	bswapl %ebx
refused	bswap %ax
refused	bswapw %ax
refused	bswap (%rax)
f30f1efb	endbr32
f30f1efa	endbr64
0f01e8	serialize
0f0110	lgdt (%rax)
0f0118	lidt (%rax)
0f0100	sgdt (%rax)
0f0108	sidt (%rax)
0f01142500000000 4:11:sym+0	lgdt sym
0f011c2500000000 4:11:sym+0	lidt sym
0f01042500000000 4:11:sym+0	sgdt sym
0f010c2500000000 4:11:sym+0	sidt sym
refused	lgdtl (%rax)
refused	lgdtw (%rax)
0f0110	lgdtq (%rax)
refused	sgdtl (%rax)
refused	sidtw (%rax)
refused	lgdt %eax
0f00d0	lldt %ax
660f00c0	sldt %ax
0f00c0	sldt %eax
0f0000	sldt (%rax)
0f0000	sldtw (%rax)
0f0010	lldt (%rax)
0f00142500000000 4:11:sym+0	lldtw sym
0f00d8	ltr %ax
0f0018	ltr (%rax)
0f001c2500000000 4:11:sym+0	ltrw sym
660f00c8	str %ax
0f00c8	str %eax
0f0008	str (%rax)
0f000c2500000000 4:11:sym+0	strw sym
0f01f0	lmsw %ax
0f0130	lmsw (%rax)
660f01e0	smsw %ax
0f01e0	smsw %eax
0f0120	smsw (%rax)
0f00e0	verr %ax
0f00e8	verw %ax
0f0020	verr (%rax)
0f002c2500000000 4:11:sym+0	verw sym
0f0138	invlpg (%rax)
0f013c2500000000 4:11:sym+0	invlpg sym
refused	invlpg %eax
0f00d0	lldt %eax
0f00d8	ltr %eax
refused	lmsw %eax
0f00e0	verr %eax
0fae38	clflush (%rax)
660fae38	clflushopt (%rax)
0f1800	prefetchnta (%rax)
0f1808	prefetcht0 (%rax)
0f18142500000000 4:11:sym+0	prefetcht1 sym
0f1818	prefetcht2 (%rax)
0f0d00	prefetch (%rax)
0f0d08	prefetchw (%rax)
0fc303	movnti %eax,(%rbx)
0f38f103	movbe %eax,(%rbx)
0f38f003	movbe (%rbx),%eax
f20f38f0d8	crc32b %al,%ebx
f20f38f1d8	crc32l %eax,%ebx
0fae00	fxsave (%rax)
0fae08	fxrstor (%rax)
0fae20	xsave (%rax)
0fae28	xrstor (%rax)
0fae30	xsaveopt (%rax)
0fae10	ldmxcsr (%rax)
0fae18	stmxcsr (%rax)
f30faec0	rdfsbase %eax
f30faed8	wrgsbase %eax
660f388218	invpcid (%rax),%rbx
f30fc7f8	rdpid %rax
refused	ret %eax
refused	hlt %eax
refused	cli $1
refused	nop $1
refused	cpuid %eax
refused	leave $1
refused	int
refused	int %eax
cd00 1:14:sym+0	int $sym
c20000 1:12:sym+0	ret $sym
c8000001 1:12:sym+0	enter $sym,$1
refused	lea %eax,%ebx
668d18	lea (%rax),%bx
668d18	leaw (%rax),%bx
8d042500000000 3:10:sym+0	leal sym,%eax
8d042500000000 3:10:sym+0	lea sym,%eax
refused	leab (%rax),%al
refused	lea (%rax),%al
refused	leal $1,%eax
refused	leal (%rax),sym
refused	lea (%rax),%ds
488d18	leaq (%rax),%rbx
8d18	leal (%rax),%ebx
67488d18	leaq (%eax),%rbx
4c8d08	lea (%rax),%r9
488d3d00000000 3:2:sym-4	lea sym(%rip),%rdi
488d042500000000 4:11:sym+0	leaq sym,%rax
488d14c500000000	leaq 0(,%rax,8),%rdx
488d0440	leaq (%rax,%rax,2),%rax
488d6424f8	leaq -8(%rsp),%rsp
4d8d6d00	leaq (%r13),%r13
4d8d2424	leaq (%r12),%r12
4f8d742500	leaq (%r13,%r12),%r14
4f8d342c	leaq (%r12,%r13),%r14
4e8d742d00	leaq (%rbp,%r13),%r14
4a8d046500000000	leaq (,%r12,2),%rax
480fc8	bswap %rax
490fc9	bswapq %r9
410fca	bswap %r10d
480fc7f0	rdrand %rax
490fc7f1	rdrand %r9
f3480faec0	rdfsbase %rax
f3490faedb	wrgsbase %r11
480fc303	movnti %rax,(%rbx)
480f38f103	movbe %rax,(%rbx)
f2480f38f1d8	crc32q %rax,%rbx
f2400f38f0c6	crc32b %sil,%eax
f2480f38f0d8	crc32b %al,%rbx
410f0111	lgdt (%r9)
410f013c24	invlpg (%r12)
410f017d00	invlpg (%r13)
0f00c0	sldt %rax
410f00c1	sldt %r9
0f00c8	str %rax
480f01e0	smsw %rax
410f01e1	smsw %r9d
410f00d1	lldt %r9w
410f00d9	ltr %r9w
90	xchg %rax,%rax
4990	xchg %rax,%r8
4990	xchg %r8,%rax
4190	xchg %eax,%r8d
4190	xchg %r8d,%eax
664190	xchg %ax,%r8w
4891	xchgq %rax,%rcx
4d87c1	xchg %r8,%r9
490fc709	cmpxchg16b (%r9)
f0480fc70d00000000 5:2:sym-4	lock cmpxchg16b sym(%rip)
480fae00	fxsave64 (%rax)
480fae08	fxrstor64 (%rax)
480fae20	xsave64 (%rax)
480f35	sysexitq
48a5	movsq
48ab	stosq
48ad	lodsq
48af	scasq
48a7	cmpsq
f348a5	rep movsq
f348ab	rep stosq
f348a7	repe cmpsq
480fb10b	cmpxchgq %rcx,(%rbx)
490fc1c9	xaddq %rcx,%r9
498501	testq %rax,(%r9)
4084f7	testb %sil,%dil
40f6c401	testb $1,%spl
45840c24	testb %r9b,(%r12)
49ff4d00	decq (%r13)
48f71500000000 3:2:sym-4	notq sym(%rip)
4d0fafd1	imulq %r9,%r10
4d0faf1424	imulq (%r12),%r10
49f77500	divq (%r13)
49d3e1	shlq %cl,%r9
49c1f93f	sarq $63,%r9
49d12c24	shrq (%r12)
48d1e0	shlq $1,%rax
480fbae03f	btq $63,%rax
4d0fa30c24	btq %r9,(%r12)
4d0fbcd1	bsfq %r9,%r10
4d0fbd5500	bsrq (%r13),%r10
f34d0fb8d1	popcntq %r9,%r10
440f22c0	movq %rax,%cr8
480fc708	rex.W cmpxchg8b (%rax)
a4	movsb
66a5	movsw
a5	movsl
a5	movs
a6	cmpsb
66a7	cmpsw
a7	cmpsl
a7	cmps
ae	scasb
66af	scasw
af	scasl
af	scas
aa	stosb
66ab	stosw
ab	stosl
ab	stos
ac	lodsb
66ad	lodsw
ad	lodsl
ad	lods
6c	insb
666d	insw
6d	insl
6d	ins
refused	insq
6e	outsb
666f	outsw
6f	outsl
6f	outs
refused	outsq
a4	movsb (%rsi),%es:(%rdi)
a5	movsl (%rsi),%es:(%rdi)
a5	movsl %ds:(%rsi),%es:(%rdi)
64a5	movsl %fs:(%rsi),%es:(%rdi)
2e66a5	movsw %cs:(%rsi),%es:(%rdi)
a5	movs (%rsi),%es:(%rdi)
refused	movsb (%si),%es:(%di)
67a5	movsl (%esi),%es:(%edi)
a6	cmpsb %es:(%rdi),(%rsi)
65a7	cmpsl %es:(%rdi),%gs:(%rsi)
ae	scasb %es:(%rdi),%al
af	scasl %es:(%rdi),%eax
66af	scas %es:(%rdi),%ax
aa	stosb %al,%es:(%rdi)
ab	stosl %eax,%es:(%rdi)
66ab	stos %ax,%es:(%rdi)
ab	stos %eax,(%rdi)
ac	lodsb (%rsi),%al
64ad	lodsl %fs:(%rsi),%eax
66ad	lods (%rsi),%ax
ad	lods (%rsi)
6c	insb (%dx),%es:(%rdi)
6d	insl (%dx),%es:(%rdi)
6e	outsb (%rsi),(%dx)
656f	outsl %gs:(%rsi),(%dx)
a5	movsl (%rdi),%es:(%rsi)
refused	movsl (%rsi),%ds:(%rdi)
refused	stosl %eax,%ds:(%rdi)
67a5	movsl (%eax),%es:(%ebx)
refused	stosb %bl,%es:(%rdi)
refused	lodsl (%rsi),%ebx
refused	movsbl (%rsi),%es:(%rdi)
a5	smovl
af	ssca
ad	slod
ab	ssto
a7	scmp
67ae	scasb (%edi),%al
67ac	lodsb (%esi)
67ab	stosl %eax,(%edi)
a5	movsd
a7	cmpsd
refused	movsl %eax,%ebx
refused	movsw %ax,%bx
e460	inb $0x60,%al
66e560	inw $0x60,%ax
e560	inl $0x60,%eax
e460	in $0x60,%al
66e560	in $0x60,%ax
e560	in $0x60,%eax
ec	inb %dx,%al
66ed	inw %dx,%ax
ed	inl %dx,%eax
ec	in %dx,%al
ed	in %dx,%eax
ec	inb (%dx),%al
ed	in (%dx),%eax
ec	inb %dx
e401	inb $1
ed	inl %dx
ed	in %dx
refused	inb
e680	outb %al,$0x80
66e780	outw %ax,$0x80
e780	outl %eax,$0x80
e680	out %al,$0x80
e780	out %eax,$0x80
ee	outb %al,%dx
66ef	outw %ax,%dx
ef	outl %eax,%dx
ee	out %al,%dx
66ef	out %ax,%dx
ee	outb %al,(%dx)
ef	out %eax,(%dx)
ee	outb %dx
e680	outb $0x80
refused	outb
refused	outb %bl,%dx
refused	inb %dx,%bl
refused	inb $0x60,%bl
refused	outl %eax,%cx
refused	in $0x60,%rax
refused	inb %dx,%ax
refused	outb %ax,%dx
refused	outb $1,$0x80
e400 1:14:sym+0	in $sym,%al
e600 1:14:sym+0	out %al,$sym
d903	flds (%rbx)
dd03	fldl (%rbx)
db2b	fldt (%rbx)
d9c1	fld %st(1)
d9c0	fld %st
d9c7	fld %st(7)
d903	fld (%rbx)
d9042500000000 3:11:sym+0	flds sym
dd442408	fldl 8(%rsp)
db2c2500000000 3:11:sym+0	fldt sym
refused	fld %st(8)
refused	fld
refused	fldq (%rbx)
refused	fld %eax
d913	fsts (%rbx)
dd13	fstl (%rbx)
refused	fstt (%rbx)
ddd1	fst %st(1)
ddd0	fst %st
ddd7	fst %st(7)
d913	fst (%rbx)
d9142500000000 3:11:sym+0	fsts sym
dd542408	fstl 8(%rsp)
refused	fstt sym
refused	fst %st(8)
refused	fst
refused	fstq (%rbx)
refused	fst %eax
d91b	fstps (%rbx)
dd1b	fstpl (%rbx)
db3b	fstpt (%rbx)
ddd9	fstp %st(1)
ddd8	fstp %st
dddf	fstp %st(7)
d91b	fstp (%rbx)
d91c2500000000 3:11:sym+0	fstps sym
dd5c2408	fstpl 8(%rsp)
db3c2500000000 3:11:sym+0	fstpt sym
refused	fstp %st(8)
refused	fstp
refused	fstpq (%rbx)
refused	fstp %eax
df03	filds (%rbx)
db03	fildl (%rbx)
df2b	fildq (%rbx)
df2b	fildll (%rbx)
df03	fild (%rbx)
df042500000000 3:11:sym+0	filds sym
df2c2500000000 3:11:sym+0	fildll sym
refused	fildw (%rbx)
refused	fild %st(1)
df13	fists (%rbx)
db13	fistl (%rbx)
refused	fistq (%rbx)
refused	fistll (%rbx)
df13	fist (%rbx)
df142500000000 3:11:sym+0	fists sym
refused	fistll sym
refused	fistw (%rbx)
refused	fist %st(1)
df1b	fistps (%rbx)
db1b	fistpl (%rbx)
df3b	fistpq (%rbx)
df3b	fistpll (%rbx)
df1b	fistp (%rbx)
df1c2500000000 3:11:sym+0	fistps sym
df3c2500000000 3:11:sym+0	fistpll sym
refused	fistpw (%rbx)
refused	fistp %st(1)
df0b	fisttps (%rbx)
db0b	fisttpl (%rbx)
dd0b	fisttpq (%rbx)
dd0b	fisttpll (%rbx)
df0b	fisttp (%rbx)
df0c2500000000 3:11:sym+0	fisttps sym
dd0c2500000000 3:11:sym+0	fisttpll sym
refused	fisttpw (%rbx)
refused	fisttp %st(1)
d803	fadds (%rbx)
dc03	faddl (%rbx)
d803	fadd (%rbx)
d8c1	fadd %st(1)
d8c1	fadd %st(1),%st
dcc1	fadd %st,%st(1)
d8c3	fadd %st(3),%st(0)
dcc3	fadd %st(0),%st(3)
d8c0	fadd %st,%st
dec1	fadd
dec1	faddp
dec1	faddp %st(1)
dec1	faddp %st,%st(1)
dec3	faddp %st,%st(3)
dec1	faddp %st(1),%st
refused	fadd %st(1),%st(2)
refused	faddt (%rbx)
d8042500000000 3:11:sym+0	fadds sym
dc042500000000 3:11:sym+0	faddl sym
d8c0	fadd %st(0)
refused	faddp %st(2),%st(1)
d823	fsubs (%rbx)
dc23	fsubl (%rbx)
d823	fsub (%rbx)
d8e1	fsub %st(1)
d8e1	fsub %st(1),%st
dce1	fsub %st,%st(1)
d8e3	fsub %st(3),%st(0)
dce3	fsub %st(0),%st(3)
d8e0	fsub %st,%st
dee1	fsub
dee1	fsubp
dee1	fsubp %st(1)
dee1	fsubp %st,%st(1)
dee3	fsubp %st,%st(3)
refused	fsubp %st(1),%st
refused	fsub %st(1),%st(2)
refused	fsubt (%rbx)
d8242500000000 3:11:sym+0	fsubs sym
dc242500000000 3:11:sym+0	fsubl sym
d8e0	fsub %st(0)
refused	fsubp %st(2),%st(1)
d82b	fsubrs (%rbx)
dc2b	fsubrl (%rbx)
d82b	fsubr (%rbx)
d8e9	fsubr %st(1)
d8e9	fsubr %st(1),%st
dce9	fsubr %st,%st(1)
d8eb	fsubr %st(3),%st(0)
dceb	fsubr %st(0),%st(3)
d8e8	fsubr %st,%st
dee9	fsubr
dee9	fsubrp
dee9	fsubrp %st(1)
dee9	fsubrp %st,%st(1)
deeb	fsubrp %st,%st(3)
refused	fsubrp %st(1),%st
refused	fsubr %st(1),%st(2)
refused	fsubrt (%rbx)
d82c2500000000 3:11:sym+0	fsubrs sym
dc2c2500000000 3:11:sym+0	fsubrl sym
d8e8	fsubr %st(0)
refused	fsubrp %st(2),%st(1)
d80b	fmuls (%rbx)
dc0b	fmull (%rbx)
d80b	fmul (%rbx)
d8c9	fmul %st(1)
d8c9	fmul %st(1),%st
dcc9	fmul %st,%st(1)
d8cb	fmul %st(3),%st(0)
dccb	fmul %st(0),%st(3)
d8c8	fmul %st,%st
dec9	fmul
dec9	fmulp
dec9	fmulp %st(1)
dec9	fmulp %st,%st(1)
decb	fmulp %st,%st(3)
dec9	fmulp %st(1),%st
refused	fmul %st(1),%st(2)
refused	fmult (%rbx)
d80c2500000000 3:11:sym+0	fmuls sym
dc0c2500000000 3:11:sym+0	fmull sym
d8c8	fmul %st(0)
refused	fmulp %st(2),%st(1)
d833	fdivs (%rbx)
dc33	fdivl (%rbx)
d833	fdiv (%rbx)
d8f1	fdiv %st(1)
d8f1	fdiv %st(1),%st
dcf1	fdiv %st,%st(1)
d8f3	fdiv %st(3),%st(0)
dcf3	fdiv %st(0),%st(3)
d8f0	fdiv %st,%st
def1	fdiv
def1	fdivp
def1	fdivp %st(1)
def1	fdivp %st,%st(1)
def3	fdivp %st,%st(3)
refused	fdivp %st(1),%st
refused	fdiv %st(1),%st(2)
refused	fdivt (%rbx)
d8342500000000 3:11:sym+0	fdivs sym
dc342500000000 3:11:sym+0	fdivl sym
d8f0	fdiv %st(0)
refused	fdivp %st(2),%st(1)
d83b	fdivrs (%rbx)
dc3b	fdivrl (%rbx)
d83b	fdivr (%rbx)
d8f9	fdivr %st(1)
d8f9	fdivr %st(1),%st
dcf9	fdivr %st,%st(1)
d8fb	fdivr %st(3),%st(0)
dcfb	fdivr %st(0),%st(3)
d8f8	fdivr %st,%st
def9	fdivr
def9	fdivrp
def9	fdivrp %st(1)
def9	fdivrp %st,%st(1)
defb	fdivrp %st,%st(3)
refused	fdivrp %st(1),%st
refused	fdivr %st(1),%st(2)
refused	fdivrt (%rbx)
d83c2500000000 3:11:sym+0	fdivrs sym
dc3c2500000000 3:11:sym+0	fdivrl sym
d8f8	fdivr %st(0)
refused	fdivrp %st(2),%st(1)
d813	fcoms (%rbx)
dc13	fcoml (%rbx)
d813	fcom (%rbx)
d8d1	fcom %st(1)
refused	fcom %st(1),%st
refused	fcom %st,%st(1)
refused	fcom %st(3),%st(0)
refused	fcom %st(0),%st(3)
refused	fcom %st,%st
d8d1	fcom
d8d9	fcomp
d8d9	fcomp %st(1)
refused	fcomp %st,%st(1)
refused	fcomp %st,%st(3)
refused	fcomp %st(1),%st
refused	fcom %st(1),%st(2)
refused	fcomt (%rbx)
d8142500000000 3:11:sym+0	fcoms sym
dc142500000000 3:11:sym+0	fcoml sym
d8d0	fcom %st(0)
refused	fcomp %st(2),%st(1)
d81b	fcomps (%rbx)
dc1b	fcompl (%rbx)
d81b	fcomp (%rbx)
refused	fcomp %st(3),%st(0)
refused	fcomp %st(0),%st(3)
refused	fcomp %st,%st
ded9	fcompp
refused	fcompp %st(1)
refused	fcompp %st,%st(1)
refused	fcompp %st,%st(3)
refused	fcompp %st(1),%st
refused	fcomp %st(1),%st(2)
refused	fcompt (%rbx)
d81c2500000000 3:11:sym+0	fcomps sym
dc1c2500000000 3:11:sym+0	fcompl sym
d8d8	fcomp %st(0)
refused	fcompp %st(2),%st(1)
de03	fiadds (%rbx)
da03	fiaddl (%rbx)
de03	fiadd (%rbx)
refused	fiaddq (%rbx)
de042500000000 3:11:sym+0	fiadds sym
refused	fiaddw (%rbx)
de23	fisubs (%rbx)
da23	fisubl (%rbx)
de23	fisub (%rbx)
refused	fisubq (%rbx)
de242500000000 3:11:sym+0	fisubs sym
refused	fisubw (%rbx)
de2b	fisubrs (%rbx)
da2b	fisubrl (%rbx)
de2b	fisubr (%rbx)
refused	fisubrq (%rbx)
de2c2500000000 3:11:sym+0	fisubrs sym
refused	fisubrw (%rbx)
de0b	fimuls (%rbx)
da0b	fimull (%rbx)
de0b	fimul (%rbx)
refused	fimulq (%rbx)
de0c2500000000 3:11:sym+0	fimuls sym
refused	fimulw (%rbx)
de33	fidivs (%rbx)
da33	fidivl (%rbx)
de33	fidiv (%rbx)
refused	fidivq (%rbx)
de342500000000 3:11:sym+0	fidivs sym
refused	fidivw (%rbx)
de3b	fidivrs (%rbx)
da3b	fidivrl (%rbx)
de3b	fidivr (%rbx)
refused	fidivrq (%rbx)
de3c2500000000 3:11:sym+0	fidivrs sym
refused	fidivrw (%rbx)
de13	ficoms (%rbx)
da13	ficoml (%rbx)
de13	ficom (%rbx)
refused	ficomq (%rbx)
de142500000000 3:11:sym+0	ficoms sym
refused	ficomw (%rbx)
de1b	ficomps (%rbx)
da1b	ficompl (%rbx)
de1b	ficomp (%rbx)
refused	ficompq (%rbx)
de1c2500000000 3:11:sym+0	ficomps sym
refused	ficompw (%rbx)
d9c9	fxch
d9c9	fxch %st(1)
d9cb	fxch %st(3)
refused	fxch %st,%st(1)
refused	fxch %st(1),%st
d9e0	fchs
d9e1	fabs
d9e4	ftst
d9e5	fxam
d9e8	fld1
d9e9	fldl2t
d9ea	fldl2e
d9eb	fldpi
d9ec	fldlg2
d9ed	fldln2
d9ee	fldz
d9f0	f2xm1
d9f1	fyl2x
d9f2	fptan
d9f3	fpatan
d9f4	fxtract
d9f5	fprem1
d9f6	fdecstp
d9f7	fincstp
d9f8	fprem
d9f9	fyl2xp1
d9fa	fsqrt
d9fb	fsincos
d9fc	frndint
d9fd	fscale
d9fe	fsin
d9ff	fcos
d9d0	fnop
dae9	fucompp
dde1	fucom
dde2	fucom %st(2)
dde9	fucomp
ddea	fucomp %st(2)
dbe9	fucomi %st(1),%st
dbe9	fucomi %st(1)
dfe9	fucomip %st(1),%st
dfe9	fucomip
dbf2	fcomi %st(2),%st
dbf2	fcomi %st(2)
dff2	fcomip %st(2),%st
dbf1	fcomi
dbe9	fucomi
dac1	fcmovb %st(1),%st
daca	fcmove %st(2),%st
dad1	fcmovbe %st(1),%st
dad9	fcmovu %st(1),%st
dbc1	fcmovnb %st(1),%st
dbc9	fcmovne %st(1),%st
dbd1	fcmovnbe %st(1),%st
dbdf	fcmovnu %st(7),%st
refused	fcmovb %st(1)
ddc1	ffree %st(1)
ddc0	ffree %st
dfc1	ffreep %st(1)
dfe0	fnstsw %ax
dfe0	fnstsw
9bdfe0	fstsw %ax
9bdfe0	fstsw
dd38	fnstsw (%rax)
9bdd38	fstsw (%rax)
dd3c2500000000 3:11:sym+0	fnstsw sym
refused	fnstsw %eax
refused	fnstsw %bx
d928	fldcw (%rax)
d938	fnstcw (%rax)
9bd938	fstcw (%rax)
d92c2500000000 3:11:sym+0	fldcw sym
d93c2500000000 3:11:sym+0	fnstcw sym
9bd93c2500000000 4:11:sym+0	fstcw sym
refused	fldcw %ax
d920	fldenv (%rax)
d930	fnstenv (%rax)
9bd930	fstenv (%rax)
dd20	frstor (%rax)
dd30	fnsave (%rax)
9bdd30	fsave (%rax)
dbe3	fninit
9bdbe3	finit
dbe2	fnclex
9bdbe2	fclex
df20	fbld (%rax)
df30	fbstp (%rax)
66d920	fldenvs (%rax)
d930	fnstenvl (%rax)
9bdbe4	fsetpm
9bdbe1	fdisi
9bdbe0	feni
dbe0	fneni
dbe1	fndisi
refused	fstp1 %st(1)
da1c2500000000 3:11:sym+0	ficompl sym
refused	fucom %st(1),%st
refused	fcom %st(1),%st(1)
refused	fstpt %st(1)
refused	flds %st(1)
refused	fld %st(1),%st
dee2	fsubp %st,%st(2)
deea	fsubrp %st,%st(2)
def2	fdivp %st,%st(2)
defa	fdivrp %st,%st(2)
dce2	fsub %st,%st(2)
dcea	fsubr %st,%st(2)
dcf2	fdiv %st,%st(2)
dcfa	fdivr %st,%st(2)
d8e2	fsub %st(2),%st
d8ea	fsubr %st(2),%st
d8f2	fdiv %st(2),%st
d8fa	fdivr %st(2),%st
dec2	faddp %st(2)
deca	fmulp %st(2)
d8c2	fadd %st(2)
d8ca	fmul %st(2)
d8e2	fsub %st(2)
d8ea	fsubr %st(2)
d8f2	fdiv %st(2)
d8fa	fdivr %st(2)
dee2	fsubp %st(2)
deea	fsubrp %st(2)
def2	fdivp %st(2)
defa	fdivrp %st(2)
89c3	.code32 | movl %eax,%ebx
50	.code32 | pushl %eax
40	.code32 | incl %eax
60	.code32 | pusha
a100000000 1:10:sym+0	.code32 | movl sym,%eax
8b18	.code32 | movl (%eax),%ebx
e800000000 1:2:sym-4	.code32 | call sym
ffe0	.code32 | jmp *%eax
refused	.code32 | movq %rax,%rbx
89c34889c3	.code32 | movl %eax,%ebx | .code64 | movq %rax,%rbx
89c3	.code16 | movw %ax,%bx
8b07	.code16 | movw (%bx),%ax
e90000 1:13:sym-2	.code16 | jmp sym
50	.code16 | pushw %ax
6689c3	.code16 | movl %eax,%ebx
0f22c066ea00000000080089c3ea0000000008004889c3 5:10:.text+11 e:10:.text+20	.code16 | movl %eax,%cr0 | ljmpl $8,$1f | .code32 | 1: movl %eax,%ebx | ljmp $8,$2f | .code64 | 2: movq %rax,%rbx
ea000000000800 1:10:sym+0	.code32 | ljmp $8,$sym
37	.code32 | aaa
6800000000 1:10:sym+0	.code32 | pushl $sym
b800000000 1:10:sym+0	.code32 | movl $sym,%eax
678b07	.code32 | addr16 movl (%bx),%eax
40ffc0	.code32 | inc %eax | .code64 | inc %eax
