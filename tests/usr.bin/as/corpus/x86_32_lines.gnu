# What GNU as makes of each line of x86_32_lines.txt: its result, a tab, the line.
# Written by gnu-diff.py --write-gnu-results with GNU assembler (GNU Binutils) 2.47
8900	movl %eax,(%eax)
8b08	movl (%eax),%ecx
8d10	leal (%eax),%edx
894008	movl %eax,8(%eax)
8b4808	movl 8(%eax),%ecx
8d5008	leal 8(%eax),%edx
8940f8	movl %eax,-8(%eax)
8b48f8	movl -8(%eax),%ecx
8d50f8	leal -8(%eax),%edx
89407f	movl %eax,127(%eax)
8b487f	movl 127(%eax),%ecx
8d507f	leal 127(%eax),%edx
898080000000	movl %eax,128(%eax)
8b8880000000	movl 128(%eax),%ecx
8d9080000000	leal 128(%eax),%edx
894080	movl %eax,-128(%eax)
8b4880	movl -128(%eax),%ecx
8d5080	leal -128(%eax),%edx
89807fffffff	movl %eax,-129(%eax)
8b887fffffff	movl -129(%eax),%ecx
8d907fffffff	leal -129(%eax),%edx
898045230100	movl %eax,0x12345(%eax)
8b8845230100	movl 0x12345(%eax),%ecx
8d9045230100	leal 0x12345(%eax),%edx
8900	movl %eax,0(%eax)
8b08	movl 0(%eax),%ecx
8d10	leal 0(%eax),%edx
898000000000 2:1:sym+0	movl %eax,sym(%eax)
8b8800000000 2:1:sym+0	movl sym(%eax),%ecx
8d9000000000 2:1:sym+0	leal sym(%eax),%edx
898004000000 2:1:sym+0	movl %eax,sym+4(%eax)
8b8804000000 2:1:sym+0	movl sym+4(%eax),%ecx
8d9004000000 2:1:sym+0	leal sym+4(%eax),%edx
8901	movl %eax,(%ecx)
8b09	movl (%ecx),%ecx
8d11	leal (%ecx),%edx
894108	movl %eax,8(%ecx)
8b4908	movl 8(%ecx),%ecx
8d5108	leal 8(%ecx),%edx
8941f8	movl %eax,-8(%ecx)
8b49f8	movl -8(%ecx),%ecx
8d51f8	leal -8(%ecx),%edx
89417f	movl %eax,127(%ecx)
8b497f	movl 127(%ecx),%ecx
8d517f	leal 127(%ecx),%edx
898180000000	movl %eax,128(%ecx)
8b8980000000	movl 128(%ecx),%ecx
8d9180000000	leal 128(%ecx),%edx
894180	movl %eax,-128(%ecx)
8b4980	movl -128(%ecx),%ecx
8d5180	leal -128(%ecx),%edx
89817fffffff	movl %eax,-129(%ecx)
8b897fffffff	movl -129(%ecx),%ecx
8d917fffffff	leal -129(%ecx),%edx
898145230100	movl %eax,0x12345(%ecx)
8b8945230100	movl 0x12345(%ecx),%ecx
8d9145230100	leal 0x12345(%ecx),%edx
8901	movl %eax,0(%ecx)
8b09	movl 0(%ecx),%ecx
8d11	leal 0(%ecx),%edx
898100000000 2:1:sym+0	movl %eax,sym(%ecx)
8b8900000000 2:1:sym+0	movl sym(%ecx),%ecx
8d9100000000 2:1:sym+0	leal sym(%ecx),%edx
898104000000 2:1:sym+0	movl %eax,sym+4(%ecx)
8b8904000000 2:1:sym+0	movl sym+4(%ecx),%ecx
8d9104000000 2:1:sym+0	leal sym+4(%ecx),%edx
8902	movl %eax,(%edx)
8b0a	movl (%edx),%ecx
8d12	leal (%edx),%edx
894208	movl %eax,8(%edx)
8b4a08	movl 8(%edx),%ecx
8d5208	leal 8(%edx),%edx
8942f8	movl %eax,-8(%edx)
8b4af8	movl -8(%edx),%ecx
8d52f8	leal -8(%edx),%edx
89427f	movl %eax,127(%edx)
8b4a7f	movl 127(%edx),%ecx
8d527f	leal 127(%edx),%edx
898280000000	movl %eax,128(%edx)
8b8a80000000	movl 128(%edx),%ecx
8d9280000000	leal 128(%edx),%edx
894280	movl %eax,-128(%edx)
8b4a80	movl -128(%edx),%ecx
8d5280	leal -128(%edx),%edx
89827fffffff	movl %eax,-129(%edx)
8b8a7fffffff	movl -129(%edx),%ecx
8d927fffffff	leal -129(%edx),%edx
898245230100	movl %eax,0x12345(%edx)
8b8a45230100	movl 0x12345(%edx),%ecx
8d9245230100	leal 0x12345(%edx),%edx
8902	movl %eax,0(%edx)
8b0a	movl 0(%edx),%ecx
8d12	leal 0(%edx),%edx
898200000000 2:1:sym+0	movl %eax,sym(%edx)
8b8a00000000 2:1:sym+0	movl sym(%edx),%ecx
8d9200000000 2:1:sym+0	leal sym(%edx),%edx
898204000000 2:1:sym+0	movl %eax,sym+4(%edx)
8b8a04000000 2:1:sym+0	movl sym+4(%edx),%ecx
8d9204000000 2:1:sym+0	leal sym+4(%edx),%edx
8903	movl %eax,(%ebx)
8b0b	movl (%ebx),%ecx
8d13	leal (%ebx),%edx
894308	movl %eax,8(%ebx)
8b4b08	movl 8(%ebx),%ecx
8d5308	leal 8(%ebx),%edx
8943f8	movl %eax,-8(%ebx)
8b4bf8	movl -8(%ebx),%ecx
8d53f8	leal -8(%ebx),%edx
89437f	movl %eax,127(%ebx)
8b4b7f	movl 127(%ebx),%ecx
8d537f	leal 127(%ebx),%edx
898380000000	movl %eax,128(%ebx)
8b8b80000000	movl 128(%ebx),%ecx
8d9380000000	leal 128(%ebx),%edx
894380	movl %eax,-128(%ebx)
8b4b80	movl -128(%ebx),%ecx
8d5380	leal -128(%ebx),%edx
89837fffffff	movl %eax,-129(%ebx)
8b8b7fffffff	movl -129(%ebx),%ecx
8d937fffffff	leal -129(%ebx),%edx
898345230100	movl %eax,0x12345(%ebx)
8b8b45230100	movl 0x12345(%ebx),%ecx
8d9345230100	leal 0x12345(%ebx),%edx
8903	movl %eax,0(%ebx)
8b0b	movl 0(%ebx),%ecx
8d13	leal 0(%ebx),%edx
898300000000 2:1:sym+0	movl %eax,sym(%ebx)
8b8b00000000 2:1:sym+0	movl sym(%ebx),%ecx
8d9300000000 2:1:sym+0	leal sym(%ebx),%edx
898304000000 2:1:sym+0	movl %eax,sym+4(%ebx)
8b8b04000000 2:1:sym+0	movl sym+4(%ebx),%ecx
8d9304000000 2:1:sym+0	leal sym+4(%ebx),%edx
890424	movl %eax,(%esp)
8b0c24	movl (%esp),%ecx
8d1424	leal (%esp),%edx
89442408	movl %eax,8(%esp)
8b4c2408	movl 8(%esp),%ecx
8d542408	leal 8(%esp),%edx
894424f8	movl %eax,-8(%esp)
8b4c24f8	movl -8(%esp),%ecx
8d5424f8	leal -8(%esp),%edx
8944247f	movl %eax,127(%esp)
8b4c247f	movl 127(%esp),%ecx
8d54247f	leal 127(%esp),%edx
89842480000000	movl %eax,128(%esp)
8b8c2480000000	movl 128(%esp),%ecx
8d942480000000	leal 128(%esp),%edx
89442480	movl %eax,-128(%esp)
8b4c2480	movl -128(%esp),%ecx
8d542480	leal -128(%esp),%edx
8984247fffffff	movl %eax,-129(%esp)
8b8c247fffffff	movl -129(%esp),%ecx
8d94247fffffff	leal -129(%esp),%edx
89842445230100	movl %eax,0x12345(%esp)
8b8c2445230100	movl 0x12345(%esp),%ecx
8d942445230100	leal 0x12345(%esp),%edx
890424	movl %eax,0(%esp)
8b0c24	movl 0(%esp),%ecx
8d1424	leal 0(%esp),%edx
89842400000000 3:1:sym+0	movl %eax,sym(%esp)
8b8c2400000000 3:1:sym+0	movl sym(%esp),%ecx
8d942400000000 3:1:sym+0	leal sym(%esp),%edx
89842404000000 3:1:sym+0	movl %eax,sym+4(%esp)
8b8c2404000000 3:1:sym+0	movl sym+4(%esp),%ecx
8d942404000000 3:1:sym+0	leal sym+4(%esp),%edx
894500	movl %eax,(%ebp)
8b4d00	movl (%ebp),%ecx
8d5500	leal (%ebp),%edx
894508	movl %eax,8(%ebp)
8b4d08	movl 8(%ebp),%ecx
8d5508	leal 8(%ebp),%edx
8945f8	movl %eax,-8(%ebp)
8b4df8	movl -8(%ebp),%ecx
8d55f8	leal -8(%ebp),%edx
89457f	movl %eax,127(%ebp)
8b4d7f	movl 127(%ebp),%ecx
8d557f	leal 127(%ebp),%edx
898580000000	movl %eax,128(%ebp)
8b8d80000000	movl 128(%ebp),%ecx
8d9580000000	leal 128(%ebp),%edx
894580	movl %eax,-128(%ebp)
8b4d80	movl -128(%ebp),%ecx
8d5580	leal -128(%ebp),%edx
89857fffffff	movl %eax,-129(%ebp)
8b8d7fffffff	movl -129(%ebp),%ecx
8d957fffffff	leal -129(%ebp),%edx
898545230100	movl %eax,0x12345(%ebp)
8b8d45230100	movl 0x12345(%ebp),%ecx
8d9545230100	leal 0x12345(%ebp),%edx
894500	movl %eax,0(%ebp)
8b4d00	movl 0(%ebp),%ecx
8d5500	leal 0(%ebp),%edx
898500000000 2:1:sym+0	movl %eax,sym(%ebp)
8b8d00000000 2:1:sym+0	movl sym(%ebp),%ecx
8d9500000000 2:1:sym+0	leal sym(%ebp),%edx
898504000000 2:1:sym+0	movl %eax,sym+4(%ebp)
8b8d04000000 2:1:sym+0	movl sym+4(%ebp),%ecx
8d9504000000 2:1:sym+0	leal sym+4(%ebp),%edx
8906	movl %eax,(%esi)
8b0e	movl (%esi),%ecx
8d16	leal (%esi),%edx
894608	movl %eax,8(%esi)
8b4e08	movl 8(%esi),%ecx
8d5608	leal 8(%esi),%edx
8946f8	movl %eax,-8(%esi)
8b4ef8	movl -8(%esi),%ecx
8d56f8	leal -8(%esi),%edx
89467f	movl %eax,127(%esi)
8b4e7f	movl 127(%esi),%ecx
8d567f	leal 127(%esi),%edx
898680000000	movl %eax,128(%esi)
8b8e80000000	movl 128(%esi),%ecx
8d9680000000	leal 128(%esi),%edx
894680	movl %eax,-128(%esi)
8b4e80	movl -128(%esi),%ecx
8d5680	leal -128(%esi),%edx
89867fffffff	movl %eax,-129(%esi)
8b8e7fffffff	movl -129(%esi),%ecx
8d967fffffff	leal -129(%esi),%edx
898645230100	movl %eax,0x12345(%esi)
8b8e45230100	movl 0x12345(%esi),%ecx
8d9645230100	leal 0x12345(%esi),%edx
8906	movl %eax,0(%esi)
8b0e	movl 0(%esi),%ecx
8d16	leal 0(%esi),%edx
898600000000 2:1:sym+0	movl %eax,sym(%esi)
8b8e00000000 2:1:sym+0	movl sym(%esi),%ecx
8d9600000000 2:1:sym+0	leal sym(%esi),%edx
898604000000 2:1:sym+0	movl %eax,sym+4(%esi)
8b8e04000000 2:1:sym+0	movl sym+4(%esi),%ecx
8d9604000000 2:1:sym+0	leal sym+4(%esi),%edx
8907	movl %eax,(%edi)
8b0f	movl (%edi),%ecx
8d17	leal (%edi),%edx
894708	movl %eax,8(%edi)
8b4f08	movl 8(%edi),%ecx
8d5708	leal 8(%edi),%edx
8947f8	movl %eax,-8(%edi)
8b4ff8	movl -8(%edi),%ecx
8d57f8	leal -8(%edi),%edx
89477f	movl %eax,127(%edi)
8b4f7f	movl 127(%edi),%ecx
8d577f	leal 127(%edi),%edx
898780000000	movl %eax,128(%edi)
8b8f80000000	movl 128(%edi),%ecx
8d9780000000	leal 128(%edi),%edx
894780	movl %eax,-128(%edi)
8b4f80	movl -128(%edi),%ecx
8d5780	leal -128(%edi),%edx
89877fffffff	movl %eax,-129(%edi)
8b8f7fffffff	movl -129(%edi),%ecx
8d977fffffff	leal -129(%edi),%edx
898745230100	movl %eax,0x12345(%edi)
8b8f45230100	movl 0x12345(%edi),%ecx
8d9745230100	leal 0x12345(%edi),%edx
8907	movl %eax,0(%edi)
8b0f	movl 0(%edi),%ecx
8d17	leal 0(%edi),%edx
898700000000 2:1:sym+0	movl %eax,sym(%edi)
8b8f00000000 2:1:sym+0	movl sym(%edi),%ecx
8d9700000000 2:1:sym+0	leal sym(%edi),%edx
898704000000 2:1:sym+0	movl %eax,sym+4(%edi)
8b8f04000000 2:1:sym+0	movl sym+4(%edi),%ecx
8d9704000000 2:1:sym+0	leal sym+4(%edi),%edx
890400	movl %eax,(%eax,%eax)
8b0c00	movl (%eax,%eax),%ecx
8d1400	leal (%eax,%eax),%edx
890400	movl %eax,(%eax,%eax,1)
8b0c00	movl (%eax,%eax,1),%ecx
8d1400	leal (%eax,%eax,1),%edx
890440	movl %eax,(%eax,%eax,2)
8b0c40	movl (%eax,%eax,2),%ecx
8d1440	leal (%eax,%eax,2),%edx
89448004	movl %eax,4(%eax,%eax,4)
8b4c8004	movl 4(%eax,%eax,4),%ecx
8d548004	leal 4(%eax,%eax,4),%edx
8984c000100000	movl %eax,0x1000(%eax,%eax,8)
8b8cc000100000	movl 0x1000(%eax,%eax,8),%ecx
8d94c000100000	leal 0x1000(%eax,%eax,8),%edx
8984c000000000 3:1:sym+0	movl %eax,sym(%eax,%eax,8)
8b8cc000000000 3:1:sym+0	movl sym(%eax,%eax,8),%ecx
8d94c000000000 3:1:sym+0	leal sym(%eax,%eax,8),%edx
890408	movl %eax,(%eax,%ecx)
8b0c08	movl (%eax,%ecx),%ecx
8d1408	leal (%eax,%ecx),%edx
890408	movl %eax,(%eax,%ecx,1)
8b0c08	movl (%eax,%ecx,1),%ecx
8d1408	leal (%eax,%ecx,1),%edx
890448	movl %eax,(%eax,%ecx,2)
8b0c48	movl (%eax,%ecx,2),%ecx
8d1448	leal (%eax,%ecx,2),%edx
89448804	movl %eax,4(%eax,%ecx,4)
8b4c8804	movl 4(%eax,%ecx,4),%ecx
8d548804	leal 4(%eax,%ecx,4),%edx
8984c800100000	movl %eax,0x1000(%eax,%ecx,8)
8b8cc800100000	movl 0x1000(%eax,%ecx,8),%ecx
8d94c800100000	leal 0x1000(%eax,%ecx,8),%edx
8984c800000000 3:1:sym+0	movl %eax,sym(%eax,%ecx,8)
8b8cc800000000 3:1:sym+0	movl sym(%eax,%ecx,8),%ecx
8d94c800000000 3:1:sym+0	leal sym(%eax,%ecx,8),%edx
890410	movl %eax,(%eax,%edx)
8b0c10	movl (%eax,%edx),%ecx
8d1410	leal (%eax,%edx),%edx
890410	movl %eax,(%eax,%edx,1)
8b0c10	movl (%eax,%edx,1),%ecx
8d1410	leal (%eax,%edx,1),%edx
890450	movl %eax,(%eax,%edx,2)
8b0c50	movl (%eax,%edx,2),%ecx
8d1450	leal (%eax,%edx,2),%edx
89449004	movl %eax,4(%eax,%edx,4)
8b4c9004	movl 4(%eax,%edx,4),%ecx
8d549004	leal 4(%eax,%edx,4),%edx
8984d000100000	movl %eax,0x1000(%eax,%edx,8)
8b8cd000100000	movl 0x1000(%eax,%edx,8),%ecx
8d94d000100000	leal 0x1000(%eax,%edx,8),%edx
8984d000000000 3:1:sym+0	movl %eax,sym(%eax,%edx,8)
8b8cd000000000 3:1:sym+0	movl sym(%eax,%edx,8),%ecx
8d94d000000000 3:1:sym+0	leal sym(%eax,%edx,8),%edx
890418	movl %eax,(%eax,%ebx)
8b0c18	movl (%eax,%ebx),%ecx
8d1418	leal (%eax,%ebx),%edx
890418	movl %eax,(%eax,%ebx,1)
8b0c18	movl (%eax,%ebx,1),%ecx
8d1418	leal (%eax,%ebx,1),%edx
890458	movl %eax,(%eax,%ebx,2)
8b0c58	movl (%eax,%ebx,2),%ecx
8d1458	leal (%eax,%ebx,2),%edx
89449804	movl %eax,4(%eax,%ebx,4)
8b4c9804	movl 4(%eax,%ebx,4),%ecx
8d549804	leal 4(%eax,%ebx,4),%edx
8984d800100000	movl %eax,0x1000(%eax,%ebx,8)
8b8cd800100000	movl 0x1000(%eax,%ebx,8),%ecx
8d94d800100000	leal 0x1000(%eax,%ebx,8),%edx
8984d800000000 3:1:sym+0	movl %eax,sym(%eax,%ebx,8)
8b8cd800000000 3:1:sym+0	movl sym(%eax,%ebx,8),%ecx
8d94d800000000 3:1:sym+0	leal sym(%eax,%ebx,8),%edx
890428	movl %eax,(%eax,%ebp)
8b0c28	movl (%eax,%ebp),%ecx
8d1428	leal (%eax,%ebp),%edx
890428	movl %eax,(%eax,%ebp,1)
8b0c28	movl (%eax,%ebp,1),%ecx
8d1428	leal (%eax,%ebp,1),%edx
890468	movl %eax,(%eax,%ebp,2)
8b0c68	movl (%eax,%ebp,2),%ecx
8d1468	leal (%eax,%ebp,2),%edx
8944a804	movl %eax,4(%eax,%ebp,4)
8b4ca804	movl 4(%eax,%ebp,4),%ecx
8d54a804	leal 4(%eax,%ebp,4),%edx
8984e800100000	movl %eax,0x1000(%eax,%ebp,8)
8b8ce800100000	movl 0x1000(%eax,%ebp,8),%ecx
8d94e800100000	leal 0x1000(%eax,%ebp,8),%edx
8984e800000000 3:1:sym+0	movl %eax,sym(%eax,%ebp,8)
8b8ce800000000 3:1:sym+0	movl sym(%eax,%ebp,8),%ecx
8d94e800000000 3:1:sym+0	leal sym(%eax,%ebp,8),%edx
890430	movl %eax,(%eax,%esi)
8b0c30	movl (%eax,%esi),%ecx
8d1430	leal (%eax,%esi),%edx
890430	movl %eax,(%eax,%esi,1)
8b0c30	movl (%eax,%esi,1),%ecx
8d1430	leal (%eax,%esi,1),%edx
890470	movl %eax,(%eax,%esi,2)
8b0c70	movl (%eax,%esi,2),%ecx
8d1470	leal (%eax,%esi,2),%edx
8944b004	movl %eax,4(%eax,%esi,4)
8b4cb004	movl 4(%eax,%esi,4),%ecx
8d54b004	leal 4(%eax,%esi,4),%edx
8984f000100000	movl %eax,0x1000(%eax,%esi,8)
8b8cf000100000	movl 0x1000(%eax,%esi,8),%ecx
8d94f000100000	leal 0x1000(%eax,%esi,8),%edx
8984f000000000 3:1:sym+0	movl %eax,sym(%eax,%esi,8)
8b8cf000000000 3:1:sym+0	movl sym(%eax,%esi,8),%ecx
8d94f000000000 3:1:sym+0	leal sym(%eax,%esi,8),%edx
890438	movl %eax,(%eax,%edi)
8b0c38	movl (%eax,%edi),%ecx
8d1438	leal (%eax,%edi),%edx
890438	movl %eax,(%eax,%edi,1)
8b0c38	movl (%eax,%edi,1),%ecx
8d1438	leal (%eax,%edi,1),%edx
890478	movl %eax,(%eax,%edi,2)
8b0c78	movl (%eax,%edi,2),%ecx
8d1478	leal (%eax,%edi,2),%edx
8944b804	movl %eax,4(%eax,%edi,4)
8b4cb804	movl 4(%eax,%edi,4),%ecx
8d54b804	leal 4(%eax,%edi,4),%edx
8984f800100000	movl %eax,0x1000(%eax,%edi,8)
8b8cf800100000	movl 0x1000(%eax,%edi,8),%ecx
8d94f800100000	leal 0x1000(%eax,%edi,8),%edx
8984f800000000 3:1:sym+0	movl %eax,sym(%eax,%edi,8)
8b8cf800000000 3:1:sym+0	movl sym(%eax,%edi,8),%ecx
8d94f800000000 3:1:sym+0	leal sym(%eax,%edi,8),%edx
890401	movl %eax,(%ecx,%eax)
8b0c01	movl (%ecx,%eax),%ecx
8d1401	leal (%ecx,%eax),%edx
890401	movl %eax,(%ecx,%eax,1)
8b0c01	movl (%ecx,%eax,1),%ecx
8d1401	leal (%ecx,%eax,1),%edx
890441	movl %eax,(%ecx,%eax,2)
8b0c41	movl (%ecx,%eax,2),%ecx
8d1441	leal (%ecx,%eax,2),%edx
89448104	movl %eax,4(%ecx,%eax,4)
8b4c8104	movl 4(%ecx,%eax,4),%ecx
8d548104	leal 4(%ecx,%eax,4),%edx
8984c100100000	movl %eax,0x1000(%ecx,%eax,8)
8b8cc100100000	movl 0x1000(%ecx,%eax,8),%ecx
8d94c100100000	leal 0x1000(%ecx,%eax,8),%edx
8984c100000000 3:1:sym+0	movl %eax,sym(%ecx,%eax,8)
8b8cc100000000 3:1:sym+0	movl sym(%ecx,%eax,8),%ecx
8d94c100000000 3:1:sym+0	leal sym(%ecx,%eax,8),%edx
890409	movl %eax,(%ecx,%ecx)
8b0c09	movl (%ecx,%ecx),%ecx
8d1409	leal (%ecx,%ecx),%edx
890409	movl %eax,(%ecx,%ecx,1)
8b0c09	movl (%ecx,%ecx,1),%ecx
8d1409	leal (%ecx,%ecx,1),%edx
890449	movl %eax,(%ecx,%ecx,2)
8b0c49	movl (%ecx,%ecx,2),%ecx
8d1449	leal (%ecx,%ecx,2),%edx
89448904	movl %eax,4(%ecx,%ecx,4)
8b4c8904	movl 4(%ecx,%ecx,4),%ecx
8d548904	leal 4(%ecx,%ecx,4),%edx
8984c900100000	movl %eax,0x1000(%ecx,%ecx,8)
8b8cc900100000	movl 0x1000(%ecx,%ecx,8),%ecx
8d94c900100000	leal 0x1000(%ecx,%ecx,8),%edx
8984c900000000 3:1:sym+0	movl %eax,sym(%ecx,%ecx,8)
8b8cc900000000 3:1:sym+0	movl sym(%ecx,%ecx,8),%ecx
8d94c900000000 3:1:sym+0	leal sym(%ecx,%ecx,8),%edx
890411	movl %eax,(%ecx,%edx)
8b0c11	movl (%ecx,%edx),%ecx
8d1411	leal (%ecx,%edx),%edx
890411	movl %eax,(%ecx,%edx,1)
8b0c11	movl (%ecx,%edx,1),%ecx
8d1411	leal (%ecx,%edx,1),%edx
890451	movl %eax,(%ecx,%edx,2)
8b0c51	movl (%ecx,%edx,2),%ecx
8d1451	leal (%ecx,%edx,2),%edx
89449104	movl %eax,4(%ecx,%edx,4)
8b4c9104	movl 4(%ecx,%edx,4),%ecx
8d549104	leal 4(%ecx,%edx,4),%edx
8984d100100000	movl %eax,0x1000(%ecx,%edx,8)
8b8cd100100000	movl 0x1000(%ecx,%edx,8),%ecx
8d94d100100000	leal 0x1000(%ecx,%edx,8),%edx
8984d100000000 3:1:sym+0	movl %eax,sym(%ecx,%edx,8)
8b8cd100000000 3:1:sym+0	movl sym(%ecx,%edx,8),%ecx
8d94d100000000 3:1:sym+0	leal sym(%ecx,%edx,8),%edx
890419	movl %eax,(%ecx,%ebx)
8b0c19	movl (%ecx,%ebx),%ecx
8d1419	leal (%ecx,%ebx),%edx
890419	movl %eax,(%ecx,%ebx,1)
8b0c19	movl (%ecx,%ebx,1),%ecx
8d1419	leal (%ecx,%ebx,1),%edx
890459	movl %eax,(%ecx,%ebx,2)
8b0c59	movl (%ecx,%ebx,2),%ecx
8d1459	leal (%ecx,%ebx,2),%edx
89449904	movl %eax,4(%ecx,%ebx,4)
8b4c9904	movl 4(%ecx,%ebx,4),%ecx
8d549904	leal 4(%ecx,%ebx,4),%edx
8984d900100000	movl %eax,0x1000(%ecx,%ebx,8)
8b8cd900100000	movl 0x1000(%ecx,%ebx,8),%ecx
8d94d900100000	leal 0x1000(%ecx,%ebx,8),%edx
8984d900000000 3:1:sym+0	movl %eax,sym(%ecx,%ebx,8)
8b8cd900000000 3:1:sym+0	movl sym(%ecx,%ebx,8),%ecx
8d94d900000000 3:1:sym+0	leal sym(%ecx,%ebx,8),%edx
890429	movl %eax,(%ecx,%ebp)
8b0c29	movl (%ecx,%ebp),%ecx
8d1429	leal (%ecx,%ebp),%edx
890429	movl %eax,(%ecx,%ebp,1)
8b0c29	movl (%ecx,%ebp,1),%ecx
8d1429	leal (%ecx,%ebp,1),%edx
890469	movl %eax,(%ecx,%ebp,2)
8b0c69	movl (%ecx,%ebp,2),%ecx
8d1469	leal (%ecx,%ebp,2),%edx
8944a904	movl %eax,4(%ecx,%ebp,4)
8b4ca904	movl 4(%ecx,%ebp,4),%ecx
8d54a904	leal 4(%ecx,%ebp,4),%edx
8984e900100000	movl %eax,0x1000(%ecx,%ebp,8)
8b8ce900100000	movl 0x1000(%ecx,%ebp,8),%ecx
8d94e900100000	leal 0x1000(%ecx,%ebp,8),%edx
8984e900000000 3:1:sym+0	movl %eax,sym(%ecx,%ebp,8)
8b8ce900000000 3:1:sym+0	movl sym(%ecx,%ebp,8),%ecx
8d94e900000000 3:1:sym+0	leal sym(%ecx,%ebp,8),%edx
890431	movl %eax,(%ecx,%esi)
8b0c31	movl (%ecx,%esi),%ecx
8d1431	leal (%ecx,%esi),%edx
890431	movl %eax,(%ecx,%esi,1)
8b0c31	movl (%ecx,%esi,1),%ecx
8d1431	leal (%ecx,%esi,1),%edx
890471	movl %eax,(%ecx,%esi,2)
8b0c71	movl (%ecx,%esi,2),%ecx
8d1471	leal (%ecx,%esi,2),%edx
8944b104	movl %eax,4(%ecx,%esi,4)
8b4cb104	movl 4(%ecx,%esi,4),%ecx
8d54b104	leal 4(%ecx,%esi,4),%edx
8984f100100000	movl %eax,0x1000(%ecx,%esi,8)
8b8cf100100000	movl 0x1000(%ecx,%esi,8),%ecx
8d94f100100000	leal 0x1000(%ecx,%esi,8),%edx
8984f100000000 3:1:sym+0	movl %eax,sym(%ecx,%esi,8)
8b8cf100000000 3:1:sym+0	movl sym(%ecx,%esi,8),%ecx
8d94f100000000 3:1:sym+0	leal sym(%ecx,%esi,8),%edx
890439	movl %eax,(%ecx,%edi)
8b0c39	movl (%ecx,%edi),%ecx
8d1439	leal (%ecx,%edi),%edx
890439	movl %eax,(%ecx,%edi,1)
8b0c39	movl (%ecx,%edi,1),%ecx
8d1439	leal (%ecx,%edi,1),%edx
890479	movl %eax,(%ecx,%edi,2)
8b0c79	movl (%ecx,%edi,2),%ecx
8d1479	leal (%ecx,%edi,2),%edx
8944b904	movl %eax,4(%ecx,%edi,4)
8b4cb904	movl 4(%ecx,%edi,4),%ecx
8d54b904	leal 4(%ecx,%edi,4),%edx
8984f900100000	movl %eax,0x1000(%ecx,%edi,8)
8b8cf900100000	movl 0x1000(%ecx,%edi,8),%ecx
8d94f900100000	leal 0x1000(%ecx,%edi,8),%edx
8984f900000000 3:1:sym+0	movl %eax,sym(%ecx,%edi,8)
8b8cf900000000 3:1:sym+0	movl sym(%ecx,%edi,8),%ecx
8d94f900000000 3:1:sym+0	leal sym(%ecx,%edi,8),%edx
890402	movl %eax,(%edx,%eax)
8b0c02	movl (%edx,%eax),%ecx
8d1402	leal (%edx,%eax),%edx
890402	movl %eax,(%edx,%eax,1)
8b0c02	movl (%edx,%eax,1),%ecx
8d1402	leal (%edx,%eax,1),%edx
890442	movl %eax,(%edx,%eax,2)
8b0c42	movl (%edx,%eax,2),%ecx
8d1442	leal (%edx,%eax,2),%edx
89448204	movl %eax,4(%edx,%eax,4)
8b4c8204	movl 4(%edx,%eax,4),%ecx
8d548204	leal 4(%edx,%eax,4),%edx
8984c200100000	movl %eax,0x1000(%edx,%eax,8)
8b8cc200100000	movl 0x1000(%edx,%eax,8),%ecx
8d94c200100000	leal 0x1000(%edx,%eax,8),%edx
8984c200000000 3:1:sym+0	movl %eax,sym(%edx,%eax,8)
8b8cc200000000 3:1:sym+0	movl sym(%edx,%eax,8),%ecx
8d94c200000000 3:1:sym+0	leal sym(%edx,%eax,8),%edx
89040a	movl %eax,(%edx,%ecx)
8b0c0a	movl (%edx,%ecx),%ecx
8d140a	leal (%edx,%ecx),%edx
89040a	movl %eax,(%edx,%ecx,1)
8b0c0a	movl (%edx,%ecx,1),%ecx
8d140a	leal (%edx,%ecx,1),%edx
89044a	movl %eax,(%edx,%ecx,2)
8b0c4a	movl (%edx,%ecx,2),%ecx
8d144a	leal (%edx,%ecx,2),%edx
89448a04	movl %eax,4(%edx,%ecx,4)
8b4c8a04	movl 4(%edx,%ecx,4),%ecx
8d548a04	leal 4(%edx,%ecx,4),%edx
8984ca00100000	movl %eax,0x1000(%edx,%ecx,8)
8b8cca00100000	movl 0x1000(%edx,%ecx,8),%ecx
8d94ca00100000	leal 0x1000(%edx,%ecx,8),%edx
8984ca00000000 3:1:sym+0	movl %eax,sym(%edx,%ecx,8)
8b8cca00000000 3:1:sym+0	movl sym(%edx,%ecx,8),%ecx
8d94ca00000000 3:1:sym+0	leal sym(%edx,%ecx,8),%edx
890412	movl %eax,(%edx,%edx)
8b0c12	movl (%edx,%edx),%ecx
8d1412	leal (%edx,%edx),%edx
890412	movl %eax,(%edx,%edx,1)
8b0c12	movl (%edx,%edx,1),%ecx
8d1412	leal (%edx,%edx,1),%edx
890452	movl %eax,(%edx,%edx,2)
8b0c52	movl (%edx,%edx,2),%ecx
8d1452	leal (%edx,%edx,2),%edx
89449204	movl %eax,4(%edx,%edx,4)
8b4c9204	movl 4(%edx,%edx,4),%ecx
8d549204	leal 4(%edx,%edx,4),%edx
8984d200100000	movl %eax,0x1000(%edx,%edx,8)
8b8cd200100000	movl 0x1000(%edx,%edx,8),%ecx
8d94d200100000	leal 0x1000(%edx,%edx,8),%edx
8984d200000000 3:1:sym+0	movl %eax,sym(%edx,%edx,8)
8b8cd200000000 3:1:sym+0	movl sym(%edx,%edx,8),%ecx
8d94d200000000 3:1:sym+0	leal sym(%edx,%edx,8),%edx
89041a	movl %eax,(%edx,%ebx)
8b0c1a	movl (%edx,%ebx),%ecx
8d141a	leal (%edx,%ebx),%edx
89041a	movl %eax,(%edx,%ebx,1)
8b0c1a	movl (%edx,%ebx,1),%ecx
8d141a	leal (%edx,%ebx,1),%edx
89045a	movl %eax,(%edx,%ebx,2)
8b0c5a	movl (%edx,%ebx,2),%ecx
8d145a	leal (%edx,%ebx,2),%edx
89449a04	movl %eax,4(%edx,%ebx,4)
8b4c9a04	movl 4(%edx,%ebx,4),%ecx
8d549a04	leal 4(%edx,%ebx,4),%edx
8984da00100000	movl %eax,0x1000(%edx,%ebx,8)
8b8cda00100000	movl 0x1000(%edx,%ebx,8),%ecx
8d94da00100000	leal 0x1000(%edx,%ebx,8),%edx
8984da00000000 3:1:sym+0	movl %eax,sym(%edx,%ebx,8)
8b8cda00000000 3:1:sym+0	movl sym(%edx,%ebx,8),%ecx
8d94da00000000 3:1:sym+0	leal sym(%edx,%ebx,8),%edx
89042a	movl %eax,(%edx,%ebp)
8b0c2a	movl (%edx,%ebp),%ecx
8d142a	leal (%edx,%ebp),%edx
89042a	movl %eax,(%edx,%ebp,1)
8b0c2a	movl (%edx,%ebp,1),%ecx
8d142a	leal (%edx,%ebp,1),%edx
89046a	movl %eax,(%edx,%ebp,2)
8b0c6a	movl (%edx,%ebp,2),%ecx
8d146a	leal (%edx,%ebp,2),%edx
8944aa04	movl %eax,4(%edx,%ebp,4)
8b4caa04	movl 4(%edx,%ebp,4),%ecx
8d54aa04	leal 4(%edx,%ebp,4),%edx
8984ea00100000	movl %eax,0x1000(%edx,%ebp,8)
8b8cea00100000	movl 0x1000(%edx,%ebp,8),%ecx
8d94ea00100000	leal 0x1000(%edx,%ebp,8),%edx
8984ea00000000 3:1:sym+0	movl %eax,sym(%edx,%ebp,8)
8b8cea00000000 3:1:sym+0	movl sym(%edx,%ebp,8),%ecx
8d94ea00000000 3:1:sym+0	leal sym(%edx,%ebp,8),%edx
890432	movl %eax,(%edx,%esi)
8b0c32	movl (%edx,%esi),%ecx
8d1432	leal (%edx,%esi),%edx
890432	movl %eax,(%edx,%esi,1)
8b0c32	movl (%edx,%esi,1),%ecx
8d1432	leal (%edx,%esi,1),%edx
890472	movl %eax,(%edx,%esi,2)
8b0c72	movl (%edx,%esi,2),%ecx
8d1472	leal (%edx,%esi,2),%edx
8944b204	movl %eax,4(%edx,%esi,4)
8b4cb204	movl 4(%edx,%esi,4),%ecx
8d54b204	leal 4(%edx,%esi,4),%edx
8984f200100000	movl %eax,0x1000(%edx,%esi,8)
8b8cf200100000	movl 0x1000(%edx,%esi,8),%ecx
8d94f200100000	leal 0x1000(%edx,%esi,8),%edx
8984f200000000 3:1:sym+0	movl %eax,sym(%edx,%esi,8)
8b8cf200000000 3:1:sym+0	movl sym(%edx,%esi,8),%ecx
8d94f200000000 3:1:sym+0	leal sym(%edx,%esi,8),%edx
89043a	movl %eax,(%edx,%edi)
8b0c3a	movl (%edx,%edi),%ecx
8d143a	leal (%edx,%edi),%edx
89043a	movl %eax,(%edx,%edi,1)
8b0c3a	movl (%edx,%edi,1),%ecx
8d143a	leal (%edx,%edi,1),%edx
89047a	movl %eax,(%edx,%edi,2)
8b0c7a	movl (%edx,%edi,2),%ecx
8d147a	leal (%edx,%edi,2),%edx
8944ba04	movl %eax,4(%edx,%edi,4)
8b4cba04	movl 4(%edx,%edi,4),%ecx
8d54ba04	leal 4(%edx,%edi,4),%edx
8984fa00100000	movl %eax,0x1000(%edx,%edi,8)
8b8cfa00100000	movl 0x1000(%edx,%edi,8),%ecx
8d94fa00100000	leal 0x1000(%edx,%edi,8),%edx
8984fa00000000 3:1:sym+0	movl %eax,sym(%edx,%edi,8)
8b8cfa00000000 3:1:sym+0	movl sym(%edx,%edi,8),%ecx
8d94fa00000000 3:1:sym+0	leal sym(%edx,%edi,8),%edx
890403	movl %eax,(%ebx,%eax)
8b0c03	movl (%ebx,%eax),%ecx
8d1403	leal (%ebx,%eax),%edx
890403	movl %eax,(%ebx,%eax,1)
8b0c03	movl (%ebx,%eax,1),%ecx
8d1403	leal (%ebx,%eax,1),%edx
890443	movl %eax,(%ebx,%eax,2)
8b0c43	movl (%ebx,%eax,2),%ecx
8d1443	leal (%ebx,%eax,2),%edx
89448304	movl %eax,4(%ebx,%eax,4)
8b4c8304	movl 4(%ebx,%eax,4),%ecx
8d548304	leal 4(%ebx,%eax,4),%edx
8984c300100000	movl %eax,0x1000(%ebx,%eax,8)
8b8cc300100000	movl 0x1000(%ebx,%eax,8),%ecx
8d94c300100000	leal 0x1000(%ebx,%eax,8),%edx
8984c300000000 3:1:sym+0	movl %eax,sym(%ebx,%eax,8)
8b8cc300000000 3:1:sym+0	movl sym(%ebx,%eax,8),%ecx
8d94c300000000 3:1:sym+0	leal sym(%ebx,%eax,8),%edx
89040b	movl %eax,(%ebx,%ecx)
8b0c0b	movl (%ebx,%ecx),%ecx
8d140b	leal (%ebx,%ecx),%edx
89040b	movl %eax,(%ebx,%ecx,1)
8b0c0b	movl (%ebx,%ecx,1),%ecx
8d140b	leal (%ebx,%ecx,1),%edx
89044b	movl %eax,(%ebx,%ecx,2)
8b0c4b	movl (%ebx,%ecx,2),%ecx
8d144b	leal (%ebx,%ecx,2),%edx
89448b04	movl %eax,4(%ebx,%ecx,4)
8b4c8b04	movl 4(%ebx,%ecx,4),%ecx
8d548b04	leal 4(%ebx,%ecx,4),%edx
8984cb00100000	movl %eax,0x1000(%ebx,%ecx,8)
8b8ccb00100000	movl 0x1000(%ebx,%ecx,8),%ecx
8d94cb00100000	leal 0x1000(%ebx,%ecx,8),%edx
8984cb00000000 3:1:sym+0	movl %eax,sym(%ebx,%ecx,8)
8b8ccb00000000 3:1:sym+0	movl sym(%ebx,%ecx,8),%ecx
8d94cb00000000 3:1:sym+0	leal sym(%ebx,%ecx,8),%edx
890413	movl %eax,(%ebx,%edx)
8b0c13	movl (%ebx,%edx),%ecx
8d1413	leal (%ebx,%edx),%edx
890413	movl %eax,(%ebx,%edx,1)
8b0c13	movl (%ebx,%edx,1),%ecx
8d1413	leal (%ebx,%edx,1),%edx
890453	movl %eax,(%ebx,%edx,2)
8b0c53	movl (%ebx,%edx,2),%ecx
8d1453	leal (%ebx,%edx,2),%edx
89449304	movl %eax,4(%ebx,%edx,4)
8b4c9304	movl 4(%ebx,%edx,4),%ecx
8d549304	leal 4(%ebx,%edx,4),%edx
8984d300100000	movl %eax,0x1000(%ebx,%edx,8)
8b8cd300100000	movl 0x1000(%ebx,%edx,8),%ecx
8d94d300100000	leal 0x1000(%ebx,%edx,8),%edx
8984d300000000 3:1:sym+0	movl %eax,sym(%ebx,%edx,8)
8b8cd300000000 3:1:sym+0	movl sym(%ebx,%edx,8),%ecx
8d94d300000000 3:1:sym+0	leal sym(%ebx,%edx,8),%edx
89041b	movl %eax,(%ebx,%ebx)
8b0c1b	movl (%ebx,%ebx),%ecx
8d141b	leal (%ebx,%ebx),%edx
89041b	movl %eax,(%ebx,%ebx,1)
8b0c1b	movl (%ebx,%ebx,1),%ecx
8d141b	leal (%ebx,%ebx,1),%edx
89045b	movl %eax,(%ebx,%ebx,2)
8b0c5b	movl (%ebx,%ebx,2),%ecx
8d145b	leal (%ebx,%ebx,2),%edx
89449b04	movl %eax,4(%ebx,%ebx,4)
8b4c9b04	movl 4(%ebx,%ebx,4),%ecx
8d549b04	leal 4(%ebx,%ebx,4),%edx
8984db00100000	movl %eax,0x1000(%ebx,%ebx,8)
8b8cdb00100000	movl 0x1000(%ebx,%ebx,8),%ecx
8d94db00100000	leal 0x1000(%ebx,%ebx,8),%edx
8984db00000000 3:1:sym+0	movl %eax,sym(%ebx,%ebx,8)
8b8cdb00000000 3:1:sym+0	movl sym(%ebx,%ebx,8),%ecx
8d94db00000000 3:1:sym+0	leal sym(%ebx,%ebx,8),%edx
89042b	movl %eax,(%ebx,%ebp)
8b0c2b	movl (%ebx,%ebp),%ecx
8d142b	leal (%ebx,%ebp),%edx
89042b	movl %eax,(%ebx,%ebp,1)
8b0c2b	movl (%ebx,%ebp,1),%ecx
8d142b	leal (%ebx,%ebp,1),%edx
89046b	movl %eax,(%ebx,%ebp,2)
8b0c6b	movl (%ebx,%ebp,2),%ecx
8d146b	leal (%ebx,%ebp,2),%edx
8944ab04	movl %eax,4(%ebx,%ebp,4)
8b4cab04	movl 4(%ebx,%ebp,4),%ecx
8d54ab04	leal 4(%ebx,%ebp,4),%edx
8984eb00100000	movl %eax,0x1000(%ebx,%ebp,8)
8b8ceb00100000	movl 0x1000(%ebx,%ebp,8),%ecx
8d94eb00100000	leal 0x1000(%ebx,%ebp,8),%edx
8984eb00000000 3:1:sym+0	movl %eax,sym(%ebx,%ebp,8)
8b8ceb00000000 3:1:sym+0	movl sym(%ebx,%ebp,8),%ecx
8d94eb00000000 3:1:sym+0	leal sym(%ebx,%ebp,8),%edx
890433	movl %eax,(%ebx,%esi)
8b0c33	movl (%ebx,%esi),%ecx
8d1433	leal (%ebx,%esi),%edx
890433	movl %eax,(%ebx,%esi,1)
8b0c33	movl (%ebx,%esi,1),%ecx
8d1433	leal (%ebx,%esi,1),%edx
890473	movl %eax,(%ebx,%esi,2)
8b0c73	movl (%ebx,%esi,2),%ecx
8d1473	leal (%ebx,%esi,2),%edx
8944b304	movl %eax,4(%ebx,%esi,4)
8b4cb304	movl 4(%ebx,%esi,4),%ecx
8d54b304	leal 4(%ebx,%esi,4),%edx
8984f300100000	movl %eax,0x1000(%ebx,%esi,8)
8b8cf300100000	movl 0x1000(%ebx,%esi,8),%ecx
8d94f300100000	leal 0x1000(%ebx,%esi,8),%edx
8984f300000000 3:1:sym+0	movl %eax,sym(%ebx,%esi,8)
8b8cf300000000 3:1:sym+0	movl sym(%ebx,%esi,8),%ecx
8d94f300000000 3:1:sym+0	leal sym(%ebx,%esi,8),%edx
89043b	movl %eax,(%ebx,%edi)
8b0c3b	movl (%ebx,%edi),%ecx
8d143b	leal (%ebx,%edi),%edx
89043b	movl %eax,(%ebx,%edi,1)
8b0c3b	movl (%ebx,%edi,1),%ecx
8d143b	leal (%ebx,%edi,1),%edx
89047b	movl %eax,(%ebx,%edi,2)
8b0c7b	movl (%ebx,%edi,2),%ecx
8d147b	leal (%ebx,%edi,2),%edx
8944bb04	movl %eax,4(%ebx,%edi,4)
8b4cbb04	movl 4(%ebx,%edi,4),%ecx
8d54bb04	leal 4(%ebx,%edi,4),%edx
8984fb00100000	movl %eax,0x1000(%ebx,%edi,8)
8b8cfb00100000	movl 0x1000(%ebx,%edi,8),%ecx
8d94fb00100000	leal 0x1000(%ebx,%edi,8),%edx
8984fb00000000 3:1:sym+0	movl %eax,sym(%ebx,%edi,8)
8b8cfb00000000 3:1:sym+0	movl sym(%ebx,%edi,8),%ecx
8d94fb00000000 3:1:sym+0	leal sym(%ebx,%edi,8),%edx
890404	movl %eax,(%esp,%eax)
8b0c04	movl (%esp,%eax),%ecx
8d1404	leal (%esp,%eax),%edx
890404	movl %eax,(%esp,%eax,1)
8b0c04	movl (%esp,%eax,1),%ecx
8d1404	leal (%esp,%eax,1),%edx
890444	movl %eax,(%esp,%eax,2)
8b0c44	movl (%esp,%eax,2),%ecx
8d1444	leal (%esp,%eax,2),%edx
89448404	movl %eax,4(%esp,%eax,4)
8b4c8404	movl 4(%esp,%eax,4),%ecx
8d548404	leal 4(%esp,%eax,4),%edx
8984c400100000	movl %eax,0x1000(%esp,%eax,8)
8b8cc400100000	movl 0x1000(%esp,%eax,8),%ecx
8d94c400100000	leal 0x1000(%esp,%eax,8),%edx
8984c400000000 3:1:sym+0	movl %eax,sym(%esp,%eax,8)
8b8cc400000000 3:1:sym+0	movl sym(%esp,%eax,8),%ecx
8d94c400000000 3:1:sym+0	leal sym(%esp,%eax,8),%edx
89040c	movl %eax,(%esp,%ecx)
8b0c0c	movl (%esp,%ecx),%ecx
8d140c	leal (%esp,%ecx),%edx
89040c	movl %eax,(%esp,%ecx,1)
8b0c0c	movl (%esp,%ecx,1),%ecx
8d140c	leal (%esp,%ecx,1),%edx
89044c	movl %eax,(%esp,%ecx,2)
8b0c4c	movl (%esp,%ecx,2),%ecx
8d144c	leal (%esp,%ecx,2),%edx
89448c04	movl %eax,4(%esp,%ecx,4)
8b4c8c04	movl 4(%esp,%ecx,4),%ecx
8d548c04	leal 4(%esp,%ecx,4),%edx
8984cc00100000	movl %eax,0x1000(%esp,%ecx,8)
8b8ccc00100000	movl 0x1000(%esp,%ecx,8),%ecx
8d94cc00100000	leal 0x1000(%esp,%ecx,8),%edx
8984cc00000000 3:1:sym+0	movl %eax,sym(%esp,%ecx,8)
8b8ccc00000000 3:1:sym+0	movl sym(%esp,%ecx,8),%ecx
8d94cc00000000 3:1:sym+0	leal sym(%esp,%ecx,8),%edx
890414	movl %eax,(%esp,%edx)
8b0c14	movl (%esp,%edx),%ecx
8d1414	leal (%esp,%edx),%edx
890414	movl %eax,(%esp,%edx,1)
8b0c14	movl (%esp,%edx,1),%ecx
8d1414	leal (%esp,%edx,1),%edx
890454	movl %eax,(%esp,%edx,2)
8b0c54	movl (%esp,%edx,2),%ecx
8d1454	leal (%esp,%edx,2),%edx
89449404	movl %eax,4(%esp,%edx,4)
8b4c9404	movl 4(%esp,%edx,4),%ecx
8d549404	leal 4(%esp,%edx,4),%edx
8984d400100000	movl %eax,0x1000(%esp,%edx,8)
8b8cd400100000	movl 0x1000(%esp,%edx,8),%ecx
8d94d400100000	leal 0x1000(%esp,%edx,8),%edx
8984d400000000 3:1:sym+0	movl %eax,sym(%esp,%edx,8)
8b8cd400000000 3:1:sym+0	movl sym(%esp,%edx,8),%ecx
8d94d400000000 3:1:sym+0	leal sym(%esp,%edx,8),%edx
89041c	movl %eax,(%esp,%ebx)
8b0c1c	movl (%esp,%ebx),%ecx
8d141c	leal (%esp,%ebx),%edx
89041c	movl %eax,(%esp,%ebx,1)
8b0c1c	movl (%esp,%ebx,1),%ecx
8d141c	leal (%esp,%ebx,1),%edx
89045c	movl %eax,(%esp,%ebx,2)
8b0c5c	movl (%esp,%ebx,2),%ecx
8d145c	leal (%esp,%ebx,2),%edx
89449c04	movl %eax,4(%esp,%ebx,4)
8b4c9c04	movl 4(%esp,%ebx,4),%ecx
8d549c04	leal 4(%esp,%ebx,4),%edx
8984dc00100000	movl %eax,0x1000(%esp,%ebx,8)
8b8cdc00100000	movl 0x1000(%esp,%ebx,8),%ecx
8d94dc00100000	leal 0x1000(%esp,%ebx,8),%edx
8984dc00000000 3:1:sym+0	movl %eax,sym(%esp,%ebx,8)
8b8cdc00000000 3:1:sym+0	movl sym(%esp,%ebx,8),%ecx
8d94dc00000000 3:1:sym+0	leal sym(%esp,%ebx,8),%edx
89042c	movl %eax,(%esp,%ebp)
8b0c2c	movl (%esp,%ebp),%ecx
8d142c	leal (%esp,%ebp),%edx
89042c	movl %eax,(%esp,%ebp,1)
8b0c2c	movl (%esp,%ebp,1),%ecx
8d142c	leal (%esp,%ebp,1),%edx
89046c	movl %eax,(%esp,%ebp,2)
8b0c6c	movl (%esp,%ebp,2),%ecx
8d146c	leal (%esp,%ebp,2),%edx
8944ac04	movl %eax,4(%esp,%ebp,4)
8b4cac04	movl 4(%esp,%ebp,4),%ecx
8d54ac04	leal 4(%esp,%ebp,4),%edx
8984ec00100000	movl %eax,0x1000(%esp,%ebp,8)
8b8cec00100000	movl 0x1000(%esp,%ebp,8),%ecx
8d94ec00100000	leal 0x1000(%esp,%ebp,8),%edx
8984ec00000000 3:1:sym+0	movl %eax,sym(%esp,%ebp,8)
8b8cec00000000 3:1:sym+0	movl sym(%esp,%ebp,8),%ecx
8d94ec00000000 3:1:sym+0	leal sym(%esp,%ebp,8),%edx
890434	movl %eax,(%esp,%esi)
8b0c34	movl (%esp,%esi),%ecx
8d1434	leal (%esp,%esi),%edx
890434	movl %eax,(%esp,%esi,1)
8b0c34	movl (%esp,%esi,1),%ecx
8d1434	leal (%esp,%esi,1),%edx
890474	movl %eax,(%esp,%esi,2)
8b0c74	movl (%esp,%esi,2),%ecx
8d1474	leal (%esp,%esi,2),%edx
8944b404	movl %eax,4(%esp,%esi,4)
8b4cb404	movl 4(%esp,%esi,4),%ecx
8d54b404	leal 4(%esp,%esi,4),%edx
8984f400100000	movl %eax,0x1000(%esp,%esi,8)
8b8cf400100000	movl 0x1000(%esp,%esi,8),%ecx
8d94f400100000	leal 0x1000(%esp,%esi,8),%edx
8984f400000000 3:1:sym+0	movl %eax,sym(%esp,%esi,8)
8b8cf400000000 3:1:sym+0	movl sym(%esp,%esi,8),%ecx
8d94f400000000 3:1:sym+0	leal sym(%esp,%esi,8),%edx
89043c	movl %eax,(%esp,%edi)
8b0c3c	movl (%esp,%edi),%ecx
8d143c	leal (%esp,%edi),%edx
89043c	movl %eax,(%esp,%edi,1)
8b0c3c	movl (%esp,%edi,1),%ecx
8d143c	leal (%esp,%edi,1),%edx
89047c	movl %eax,(%esp,%edi,2)
8b0c7c	movl (%esp,%edi,2),%ecx
8d147c	leal (%esp,%edi,2),%edx
8944bc04	movl %eax,4(%esp,%edi,4)
8b4cbc04	movl 4(%esp,%edi,4),%ecx
8d54bc04	leal 4(%esp,%edi,4),%edx
8984fc00100000	movl %eax,0x1000(%esp,%edi,8)
8b8cfc00100000	movl 0x1000(%esp,%edi,8),%ecx
8d94fc00100000	leal 0x1000(%esp,%edi,8),%edx
8984fc00000000 3:1:sym+0	movl %eax,sym(%esp,%edi,8)
8b8cfc00000000 3:1:sym+0	movl sym(%esp,%edi,8),%ecx
8d94fc00000000 3:1:sym+0	leal sym(%esp,%edi,8),%edx
89440500	movl %eax,(%ebp,%eax)
8b4c0500	movl (%ebp,%eax),%ecx
8d540500	leal (%ebp,%eax),%edx
89440500	movl %eax,(%ebp,%eax,1)
8b4c0500	movl (%ebp,%eax,1),%ecx
8d540500	leal (%ebp,%eax,1),%edx
89444500	movl %eax,(%ebp,%eax,2)
8b4c4500	movl (%ebp,%eax,2),%ecx
8d544500	leal (%ebp,%eax,2),%edx
89448504	movl %eax,4(%ebp,%eax,4)
8b4c8504	movl 4(%ebp,%eax,4),%ecx
8d548504	leal 4(%ebp,%eax,4),%edx
8984c500100000	movl %eax,0x1000(%ebp,%eax,8)
8b8cc500100000	movl 0x1000(%ebp,%eax,8),%ecx
8d94c500100000	leal 0x1000(%ebp,%eax,8),%edx
8984c500000000 3:1:sym+0	movl %eax,sym(%ebp,%eax,8)
8b8cc500000000 3:1:sym+0	movl sym(%ebp,%eax,8),%ecx
8d94c500000000 3:1:sym+0	leal sym(%ebp,%eax,8),%edx
89440d00	movl %eax,(%ebp,%ecx)
8b4c0d00	movl (%ebp,%ecx),%ecx
8d540d00	leal (%ebp,%ecx),%edx
89440d00	movl %eax,(%ebp,%ecx,1)
8b4c0d00	movl (%ebp,%ecx,1),%ecx
8d540d00	leal (%ebp,%ecx,1),%edx
89444d00	movl %eax,(%ebp,%ecx,2)
8b4c4d00	movl (%ebp,%ecx,2),%ecx
8d544d00	leal (%ebp,%ecx,2),%edx
89448d04	movl %eax,4(%ebp,%ecx,4)
8b4c8d04	movl 4(%ebp,%ecx,4),%ecx
8d548d04	leal 4(%ebp,%ecx,4),%edx
8984cd00100000	movl %eax,0x1000(%ebp,%ecx,8)
8b8ccd00100000	movl 0x1000(%ebp,%ecx,8),%ecx
8d94cd00100000	leal 0x1000(%ebp,%ecx,8),%edx
8984cd00000000 3:1:sym+0	movl %eax,sym(%ebp,%ecx,8)
8b8ccd00000000 3:1:sym+0	movl sym(%ebp,%ecx,8),%ecx
8d94cd00000000 3:1:sym+0	leal sym(%ebp,%ecx,8),%edx
89441500	movl %eax,(%ebp,%edx)
8b4c1500	movl (%ebp,%edx),%ecx
8d541500	leal (%ebp,%edx),%edx
89441500	movl %eax,(%ebp,%edx,1)
8b4c1500	movl (%ebp,%edx,1),%ecx
8d541500	leal (%ebp,%edx,1),%edx
89445500	movl %eax,(%ebp,%edx,2)
8b4c5500	movl (%ebp,%edx,2),%ecx
8d545500	leal (%ebp,%edx,2),%edx
89449504	movl %eax,4(%ebp,%edx,4)
8b4c9504	movl 4(%ebp,%edx,4),%ecx
8d549504	leal 4(%ebp,%edx,4),%edx
8984d500100000	movl %eax,0x1000(%ebp,%edx,8)
8b8cd500100000	movl 0x1000(%ebp,%edx,8),%ecx
8d94d500100000	leal 0x1000(%ebp,%edx,8),%edx
8984d500000000 3:1:sym+0	movl %eax,sym(%ebp,%edx,8)
8b8cd500000000 3:1:sym+0	movl sym(%ebp,%edx,8),%ecx
8d94d500000000 3:1:sym+0	leal sym(%ebp,%edx,8),%edx
89441d00	movl %eax,(%ebp,%ebx)
8b4c1d00	movl (%ebp,%ebx),%ecx
8d541d00	leal (%ebp,%ebx),%edx
89441d00	movl %eax,(%ebp,%ebx,1)
8b4c1d00	movl (%ebp,%ebx,1),%ecx
8d541d00	leal (%ebp,%ebx,1),%edx
89445d00	movl %eax,(%ebp,%ebx,2)
8b4c5d00	movl (%ebp,%ebx,2),%ecx
8d545d00	leal (%ebp,%ebx,2),%edx
89449d04	movl %eax,4(%ebp,%ebx,4)
8b4c9d04	movl 4(%ebp,%ebx,4),%ecx
8d549d04	leal 4(%ebp,%ebx,4),%edx
8984dd00100000	movl %eax,0x1000(%ebp,%ebx,8)
8b8cdd00100000	movl 0x1000(%ebp,%ebx,8),%ecx
8d94dd00100000	leal 0x1000(%ebp,%ebx,8),%edx
8984dd00000000 3:1:sym+0	movl %eax,sym(%ebp,%ebx,8)
8b8cdd00000000 3:1:sym+0	movl sym(%ebp,%ebx,8),%ecx
8d94dd00000000 3:1:sym+0	leal sym(%ebp,%ebx,8),%edx
89442d00	movl %eax,(%ebp,%ebp)
8b4c2d00	movl (%ebp,%ebp),%ecx
8d542d00	leal (%ebp,%ebp),%edx
89442d00	movl %eax,(%ebp,%ebp,1)
8b4c2d00	movl (%ebp,%ebp,1),%ecx
8d542d00	leal (%ebp,%ebp,1),%edx
89446d00	movl %eax,(%ebp,%ebp,2)
8b4c6d00	movl (%ebp,%ebp,2),%ecx
8d546d00	leal (%ebp,%ebp,2),%edx
8944ad04	movl %eax,4(%ebp,%ebp,4)
8b4cad04	movl 4(%ebp,%ebp,4),%ecx
8d54ad04	leal 4(%ebp,%ebp,4),%edx
8984ed00100000	movl %eax,0x1000(%ebp,%ebp,8)
8b8ced00100000	movl 0x1000(%ebp,%ebp,8),%ecx
8d94ed00100000	leal 0x1000(%ebp,%ebp,8),%edx
8984ed00000000 3:1:sym+0	movl %eax,sym(%ebp,%ebp,8)
8b8ced00000000 3:1:sym+0	movl sym(%ebp,%ebp,8),%ecx
8d94ed00000000 3:1:sym+0	leal sym(%ebp,%ebp,8),%edx
89443500	movl %eax,(%ebp,%esi)
8b4c3500	movl (%ebp,%esi),%ecx
8d543500	leal (%ebp,%esi),%edx
89443500	movl %eax,(%ebp,%esi,1)
8b4c3500	movl (%ebp,%esi,1),%ecx
8d543500	leal (%ebp,%esi,1),%edx
89447500	movl %eax,(%ebp,%esi,2)
8b4c7500	movl (%ebp,%esi,2),%ecx
8d547500	leal (%ebp,%esi,2),%edx
8944b504	movl %eax,4(%ebp,%esi,4)
8b4cb504	movl 4(%ebp,%esi,4),%ecx
8d54b504	leal 4(%ebp,%esi,4),%edx
8984f500100000	movl %eax,0x1000(%ebp,%esi,8)
8b8cf500100000	movl 0x1000(%ebp,%esi,8),%ecx
8d94f500100000	leal 0x1000(%ebp,%esi,8),%edx
8984f500000000 3:1:sym+0	movl %eax,sym(%ebp,%esi,8)
8b8cf500000000 3:1:sym+0	movl sym(%ebp,%esi,8),%ecx
8d94f500000000 3:1:sym+0	leal sym(%ebp,%esi,8),%edx
89443d00	movl %eax,(%ebp,%edi)
8b4c3d00	movl (%ebp,%edi),%ecx
8d543d00	leal (%ebp,%edi),%edx
89443d00	movl %eax,(%ebp,%edi,1)
8b4c3d00	movl (%ebp,%edi,1),%ecx
8d543d00	leal (%ebp,%edi,1),%edx
89447d00	movl %eax,(%ebp,%edi,2)
8b4c7d00	movl (%ebp,%edi,2),%ecx
8d547d00	leal (%ebp,%edi,2),%edx
8944bd04	movl %eax,4(%ebp,%edi,4)
8b4cbd04	movl 4(%ebp,%edi,4),%ecx
8d54bd04	leal 4(%ebp,%edi,4),%edx
8984fd00100000	movl %eax,0x1000(%ebp,%edi,8)
8b8cfd00100000	movl 0x1000(%ebp,%edi,8),%ecx
8d94fd00100000	leal 0x1000(%ebp,%edi,8),%edx
8984fd00000000 3:1:sym+0	movl %eax,sym(%ebp,%edi,8)
8b8cfd00000000 3:1:sym+0	movl sym(%ebp,%edi,8),%ecx
8d94fd00000000 3:1:sym+0	leal sym(%ebp,%edi,8),%edx
890406	movl %eax,(%esi,%eax)
8b0c06	movl (%esi,%eax),%ecx
8d1406	leal (%esi,%eax),%edx
890406	movl %eax,(%esi,%eax,1)
8b0c06	movl (%esi,%eax,1),%ecx
8d1406	leal (%esi,%eax,1),%edx
890446	movl %eax,(%esi,%eax,2)
8b0c46	movl (%esi,%eax,2),%ecx
8d1446	leal (%esi,%eax,2),%edx
89448604	movl %eax,4(%esi,%eax,4)
8b4c8604	movl 4(%esi,%eax,4),%ecx
8d548604	leal 4(%esi,%eax,4),%edx
8984c600100000	movl %eax,0x1000(%esi,%eax,8)
8b8cc600100000	movl 0x1000(%esi,%eax,8),%ecx
8d94c600100000	leal 0x1000(%esi,%eax,8),%edx
8984c600000000 3:1:sym+0	movl %eax,sym(%esi,%eax,8)
8b8cc600000000 3:1:sym+0	movl sym(%esi,%eax,8),%ecx
8d94c600000000 3:1:sym+0	leal sym(%esi,%eax,8),%edx
89040e	movl %eax,(%esi,%ecx)
8b0c0e	movl (%esi,%ecx),%ecx
8d140e	leal (%esi,%ecx),%edx
89040e	movl %eax,(%esi,%ecx,1)
8b0c0e	movl (%esi,%ecx,1),%ecx
8d140e	leal (%esi,%ecx,1),%edx
89044e	movl %eax,(%esi,%ecx,2)
8b0c4e	movl (%esi,%ecx,2),%ecx
8d144e	leal (%esi,%ecx,2),%edx
89448e04	movl %eax,4(%esi,%ecx,4)
8b4c8e04	movl 4(%esi,%ecx,4),%ecx
8d548e04	leal 4(%esi,%ecx,4),%edx
8984ce00100000	movl %eax,0x1000(%esi,%ecx,8)
8b8cce00100000	movl 0x1000(%esi,%ecx,8),%ecx
8d94ce00100000	leal 0x1000(%esi,%ecx,8),%edx
8984ce00000000 3:1:sym+0	movl %eax,sym(%esi,%ecx,8)
8b8cce00000000 3:1:sym+0	movl sym(%esi,%ecx,8),%ecx
8d94ce00000000 3:1:sym+0	leal sym(%esi,%ecx,8),%edx
890416	movl %eax,(%esi,%edx)
8b0c16	movl (%esi,%edx),%ecx
8d1416	leal (%esi,%edx),%edx
890416	movl %eax,(%esi,%edx,1)
8b0c16	movl (%esi,%edx,1),%ecx
8d1416	leal (%esi,%edx,1),%edx
890456	movl %eax,(%esi,%edx,2)
8b0c56	movl (%esi,%edx,2),%ecx
8d1456	leal (%esi,%edx,2),%edx
89449604	movl %eax,4(%esi,%edx,4)
8b4c9604	movl 4(%esi,%edx,4),%ecx
8d549604	leal 4(%esi,%edx,4),%edx
8984d600100000	movl %eax,0x1000(%esi,%edx,8)
8b8cd600100000	movl 0x1000(%esi,%edx,8),%ecx
8d94d600100000	leal 0x1000(%esi,%edx,8),%edx
8984d600000000 3:1:sym+0	movl %eax,sym(%esi,%edx,8)
8b8cd600000000 3:1:sym+0	movl sym(%esi,%edx,8),%ecx
8d94d600000000 3:1:sym+0	leal sym(%esi,%edx,8),%edx
89041e	movl %eax,(%esi,%ebx)
8b0c1e	movl (%esi,%ebx),%ecx
8d141e	leal (%esi,%ebx),%edx
89041e	movl %eax,(%esi,%ebx,1)
8b0c1e	movl (%esi,%ebx,1),%ecx
8d141e	leal (%esi,%ebx,1),%edx
89045e	movl %eax,(%esi,%ebx,2)
8b0c5e	movl (%esi,%ebx,2),%ecx
8d145e	leal (%esi,%ebx,2),%edx
89449e04	movl %eax,4(%esi,%ebx,4)
8b4c9e04	movl 4(%esi,%ebx,4),%ecx
8d549e04	leal 4(%esi,%ebx,4),%edx
8984de00100000	movl %eax,0x1000(%esi,%ebx,8)
8b8cde00100000	movl 0x1000(%esi,%ebx,8),%ecx
8d94de00100000	leal 0x1000(%esi,%ebx,8),%edx
8984de00000000 3:1:sym+0	movl %eax,sym(%esi,%ebx,8)
8b8cde00000000 3:1:sym+0	movl sym(%esi,%ebx,8),%ecx
8d94de00000000 3:1:sym+0	leal sym(%esi,%ebx,8),%edx
89042e	movl %eax,(%esi,%ebp)
8b0c2e	movl (%esi,%ebp),%ecx
8d142e	leal (%esi,%ebp),%edx
89042e	movl %eax,(%esi,%ebp,1)
8b0c2e	movl (%esi,%ebp,1),%ecx
8d142e	leal (%esi,%ebp,1),%edx
89046e	movl %eax,(%esi,%ebp,2)
8b0c6e	movl (%esi,%ebp,2),%ecx
8d146e	leal (%esi,%ebp,2),%edx
8944ae04	movl %eax,4(%esi,%ebp,4)
8b4cae04	movl 4(%esi,%ebp,4),%ecx
8d54ae04	leal 4(%esi,%ebp,4),%edx
8984ee00100000	movl %eax,0x1000(%esi,%ebp,8)
8b8cee00100000	movl 0x1000(%esi,%ebp,8),%ecx
8d94ee00100000	leal 0x1000(%esi,%ebp,8),%edx
8984ee00000000 3:1:sym+0	movl %eax,sym(%esi,%ebp,8)
8b8cee00000000 3:1:sym+0	movl sym(%esi,%ebp,8),%ecx
8d94ee00000000 3:1:sym+0	leal sym(%esi,%ebp,8),%edx
890436	movl %eax,(%esi,%esi)
8b0c36	movl (%esi,%esi),%ecx
8d1436	leal (%esi,%esi),%edx
890436	movl %eax,(%esi,%esi,1)
8b0c36	movl (%esi,%esi,1),%ecx
8d1436	leal (%esi,%esi,1),%edx
890476	movl %eax,(%esi,%esi,2)
8b0c76	movl (%esi,%esi,2),%ecx
8d1476	leal (%esi,%esi,2),%edx
8944b604	movl %eax,4(%esi,%esi,4)
8b4cb604	movl 4(%esi,%esi,4),%ecx
8d54b604	leal 4(%esi,%esi,4),%edx
8984f600100000	movl %eax,0x1000(%esi,%esi,8)
8b8cf600100000	movl 0x1000(%esi,%esi,8),%ecx
8d94f600100000	leal 0x1000(%esi,%esi,8),%edx
8984f600000000 3:1:sym+0	movl %eax,sym(%esi,%esi,8)
8b8cf600000000 3:1:sym+0	movl sym(%esi,%esi,8),%ecx
8d94f600000000 3:1:sym+0	leal sym(%esi,%esi,8),%edx
89043e	movl %eax,(%esi,%edi)
8b0c3e	movl (%esi,%edi),%ecx
8d143e	leal (%esi,%edi),%edx
89043e	movl %eax,(%esi,%edi,1)
8b0c3e	movl (%esi,%edi,1),%ecx
8d143e	leal (%esi,%edi,1),%edx
89047e	movl %eax,(%esi,%edi,2)
8b0c7e	movl (%esi,%edi,2),%ecx
8d147e	leal (%esi,%edi,2),%edx
8944be04	movl %eax,4(%esi,%edi,4)
8b4cbe04	movl 4(%esi,%edi,4),%ecx
8d54be04	leal 4(%esi,%edi,4),%edx
8984fe00100000	movl %eax,0x1000(%esi,%edi,8)
8b8cfe00100000	movl 0x1000(%esi,%edi,8),%ecx
8d94fe00100000	leal 0x1000(%esi,%edi,8),%edx
8984fe00000000 3:1:sym+0	movl %eax,sym(%esi,%edi,8)
8b8cfe00000000 3:1:sym+0	movl sym(%esi,%edi,8),%ecx
8d94fe00000000 3:1:sym+0	leal sym(%esi,%edi,8),%edx
890407	movl %eax,(%edi,%eax)
8b0c07	movl (%edi,%eax),%ecx
8d1407	leal (%edi,%eax),%edx
890407	movl %eax,(%edi,%eax,1)
8b0c07	movl (%edi,%eax,1),%ecx
8d1407	leal (%edi,%eax,1),%edx
890447	movl %eax,(%edi,%eax,2)
8b0c47	movl (%edi,%eax,2),%ecx
8d1447	leal (%edi,%eax,2),%edx
89448704	movl %eax,4(%edi,%eax,4)
8b4c8704	movl 4(%edi,%eax,4),%ecx
8d548704	leal 4(%edi,%eax,4),%edx
8984c700100000	movl %eax,0x1000(%edi,%eax,8)
8b8cc700100000	movl 0x1000(%edi,%eax,8),%ecx
8d94c700100000	leal 0x1000(%edi,%eax,8),%edx
8984c700000000 3:1:sym+0	movl %eax,sym(%edi,%eax,8)
8b8cc700000000 3:1:sym+0	movl sym(%edi,%eax,8),%ecx
8d94c700000000 3:1:sym+0	leal sym(%edi,%eax,8),%edx
89040f	movl %eax,(%edi,%ecx)
8b0c0f	movl (%edi,%ecx),%ecx
8d140f	leal (%edi,%ecx),%edx
89040f	movl %eax,(%edi,%ecx,1)
8b0c0f	movl (%edi,%ecx,1),%ecx
8d140f	leal (%edi,%ecx,1),%edx
89044f	movl %eax,(%edi,%ecx,2)
8b0c4f	movl (%edi,%ecx,2),%ecx
8d144f	leal (%edi,%ecx,2),%edx
89448f04	movl %eax,4(%edi,%ecx,4)
8b4c8f04	movl 4(%edi,%ecx,4),%ecx
8d548f04	leal 4(%edi,%ecx,4),%edx
8984cf00100000	movl %eax,0x1000(%edi,%ecx,8)
8b8ccf00100000	movl 0x1000(%edi,%ecx,8),%ecx
8d94cf00100000	leal 0x1000(%edi,%ecx,8),%edx
8984cf00000000 3:1:sym+0	movl %eax,sym(%edi,%ecx,8)
8b8ccf00000000 3:1:sym+0	movl sym(%edi,%ecx,8),%ecx
8d94cf00000000 3:1:sym+0	leal sym(%edi,%ecx,8),%edx
890417	movl %eax,(%edi,%edx)
8b0c17	movl (%edi,%edx),%ecx
8d1417	leal (%edi,%edx),%edx
890417	movl %eax,(%edi,%edx,1)
8b0c17	movl (%edi,%edx,1),%ecx
8d1417	leal (%edi,%edx,1),%edx
890457	movl %eax,(%edi,%edx,2)
8b0c57	movl (%edi,%edx,2),%ecx
8d1457	leal (%edi,%edx,2),%edx
89449704	movl %eax,4(%edi,%edx,4)
8b4c9704	movl 4(%edi,%edx,4),%ecx
8d549704	leal 4(%edi,%edx,4),%edx
8984d700100000	movl %eax,0x1000(%edi,%edx,8)
8b8cd700100000	movl 0x1000(%edi,%edx,8),%ecx
8d94d700100000	leal 0x1000(%edi,%edx,8),%edx
8984d700000000 3:1:sym+0	movl %eax,sym(%edi,%edx,8)
8b8cd700000000 3:1:sym+0	movl sym(%edi,%edx,8),%ecx
8d94d700000000 3:1:sym+0	leal sym(%edi,%edx,8),%edx
89041f	movl %eax,(%edi,%ebx)
8b0c1f	movl (%edi,%ebx),%ecx
8d141f	leal (%edi,%ebx),%edx
89041f	movl %eax,(%edi,%ebx,1)
8b0c1f	movl (%edi,%ebx,1),%ecx
8d141f	leal (%edi,%ebx,1),%edx
89045f	movl %eax,(%edi,%ebx,2)
8b0c5f	movl (%edi,%ebx,2),%ecx
8d145f	leal (%edi,%ebx,2),%edx
89449f04	movl %eax,4(%edi,%ebx,4)
8b4c9f04	movl 4(%edi,%ebx,4),%ecx
8d549f04	leal 4(%edi,%ebx,4),%edx
8984df00100000	movl %eax,0x1000(%edi,%ebx,8)
8b8cdf00100000	movl 0x1000(%edi,%ebx,8),%ecx
8d94df00100000	leal 0x1000(%edi,%ebx,8),%edx
8984df00000000 3:1:sym+0	movl %eax,sym(%edi,%ebx,8)
8b8cdf00000000 3:1:sym+0	movl sym(%edi,%ebx,8),%ecx
8d94df00000000 3:1:sym+0	leal sym(%edi,%ebx,8),%edx
89042f	movl %eax,(%edi,%ebp)
8b0c2f	movl (%edi,%ebp),%ecx
8d142f	leal (%edi,%ebp),%edx
89042f	movl %eax,(%edi,%ebp,1)
8b0c2f	movl (%edi,%ebp,1),%ecx
8d142f	leal (%edi,%ebp,1),%edx
89046f	movl %eax,(%edi,%ebp,2)
8b0c6f	movl (%edi,%ebp,2),%ecx
8d146f	leal (%edi,%ebp,2),%edx
8944af04	movl %eax,4(%edi,%ebp,4)
8b4caf04	movl 4(%edi,%ebp,4),%ecx
8d54af04	leal 4(%edi,%ebp,4),%edx
8984ef00100000	movl %eax,0x1000(%edi,%ebp,8)
8b8cef00100000	movl 0x1000(%edi,%ebp,8),%ecx
8d94ef00100000	leal 0x1000(%edi,%ebp,8),%edx
8984ef00000000 3:1:sym+0	movl %eax,sym(%edi,%ebp,8)
8b8cef00000000 3:1:sym+0	movl sym(%edi,%ebp,8),%ecx
8d94ef00000000 3:1:sym+0	leal sym(%edi,%ebp,8),%edx
890437	movl %eax,(%edi,%esi)
8b0c37	movl (%edi,%esi),%ecx
8d1437	leal (%edi,%esi),%edx
890437	movl %eax,(%edi,%esi,1)
8b0c37	movl (%edi,%esi,1),%ecx
8d1437	leal (%edi,%esi,1),%edx
890477	movl %eax,(%edi,%esi,2)
8b0c77	movl (%edi,%esi,2),%ecx
8d1477	leal (%edi,%esi,2),%edx
8944b704	movl %eax,4(%edi,%esi,4)
8b4cb704	movl 4(%edi,%esi,4),%ecx
8d54b704	leal 4(%edi,%esi,4),%edx
8984f700100000	movl %eax,0x1000(%edi,%esi,8)
8b8cf700100000	movl 0x1000(%edi,%esi,8),%ecx
8d94f700100000	leal 0x1000(%edi,%esi,8),%edx
8984f700000000 3:1:sym+0	movl %eax,sym(%edi,%esi,8)
8b8cf700000000 3:1:sym+0	movl sym(%edi,%esi,8),%ecx
8d94f700000000 3:1:sym+0	leal sym(%edi,%esi,8),%edx
89043f	movl %eax,(%edi,%edi)
8b0c3f	movl (%edi,%edi),%ecx
8d143f	leal (%edi,%edi),%edx
89043f	movl %eax,(%edi,%edi,1)
8b0c3f	movl (%edi,%edi,1),%ecx
8d143f	leal (%edi,%edi,1),%edx
89047f	movl %eax,(%edi,%edi,2)
8b0c7f	movl (%edi,%edi,2),%ecx
8d147f	leal (%edi,%edi,2),%edx
8944bf04	movl %eax,4(%edi,%edi,4)
8b4cbf04	movl 4(%edi,%edi,4),%ecx
8d54bf04	leal 4(%edi,%edi,4),%edx
8984ff00100000	movl %eax,0x1000(%edi,%edi,8)
8b8cff00100000	movl 0x1000(%edi,%edi,8),%ecx
8d94ff00100000	leal 0x1000(%edi,%edi,8),%edx
8984ff00000000 3:1:sym+0	movl %eax,sym(%edi,%edi,8)
8b8cff00000000 3:1:sym+0	movl sym(%edi,%edi,8),%ecx
8d94ff00000000 3:1:sym+0	leal sym(%edi,%edi,8),%edx
89048500000000	movl %eax,(,%eax,4)
8b0c8500000000	movl (,%eax,4),%ecx
8d148500000000	leal (,%eax,4),%edx
89044508000000	movl %eax,8(,%eax,2)
8b0c4508000000	movl 8(,%eax,2),%ecx
8d144508000000	leal 8(,%eax,2),%edx
8904c500000000 3:1:sym+0	movl %eax,sym(,%eax,8)
8b0cc500000000 3:1:sym+0	movl sym(,%eax,8),%ecx
8d14c500000000 3:1:sym+0	leal sym(,%eax,8),%edx
89040500000000	movl %eax,(,%eax,1)
8b0c0500000000	movl (,%eax,1),%ecx
8d140500000000	leal (,%eax,1),%edx
89040500000000	movl %eax,(,%eax)
8b0c0500000000	movl (,%eax),%ecx
8d140500000000	leal (,%eax),%edx
89048d00000000	movl %eax,(,%ecx,4)
8b0c8d00000000	movl (,%ecx,4),%ecx
8d148d00000000	leal (,%ecx,4),%edx
89044d08000000	movl %eax,8(,%ecx,2)
8b0c4d08000000	movl 8(,%ecx,2),%ecx
8d144d08000000	leal 8(,%ecx,2),%edx
8904cd00000000 3:1:sym+0	movl %eax,sym(,%ecx,8)
8b0ccd00000000 3:1:sym+0	movl sym(,%ecx,8),%ecx
8d14cd00000000 3:1:sym+0	leal sym(,%ecx,8),%edx
89040d00000000	movl %eax,(,%ecx,1)
8b0c0d00000000	movl (,%ecx,1),%ecx
8d140d00000000	leal (,%ecx,1),%edx
89040d00000000	movl %eax,(,%ecx)
8b0c0d00000000	movl (,%ecx),%ecx
8d140d00000000	leal (,%ecx),%edx
89049500000000	movl %eax,(,%edx,4)
8b0c9500000000	movl (,%edx,4),%ecx
8d149500000000	leal (,%edx,4),%edx
89045508000000	movl %eax,8(,%edx,2)
8b0c5508000000	movl 8(,%edx,2),%ecx
8d145508000000	leal 8(,%edx,2),%edx
8904d500000000 3:1:sym+0	movl %eax,sym(,%edx,8)
8b0cd500000000 3:1:sym+0	movl sym(,%edx,8),%ecx
8d14d500000000 3:1:sym+0	leal sym(,%edx,8),%edx
89041500000000	movl %eax,(,%edx,1)
8b0c1500000000	movl (,%edx,1),%ecx
8d141500000000	leal (,%edx,1),%edx
89041500000000	movl %eax,(,%edx)
8b0c1500000000	movl (,%edx),%ecx
8d141500000000	leal (,%edx),%edx
89049d00000000	movl %eax,(,%ebx,4)
8b0c9d00000000	movl (,%ebx,4),%ecx
8d149d00000000	leal (,%ebx,4),%edx
89045d08000000	movl %eax,8(,%ebx,2)
8b0c5d08000000	movl 8(,%ebx,2),%ecx
8d145d08000000	leal 8(,%ebx,2),%edx
8904dd00000000 3:1:sym+0	movl %eax,sym(,%ebx,8)
8b0cdd00000000 3:1:sym+0	movl sym(,%ebx,8),%ecx
8d14dd00000000 3:1:sym+0	leal sym(,%ebx,8),%edx
89041d00000000	movl %eax,(,%ebx,1)
8b0c1d00000000	movl (,%ebx,1),%ecx
8d141d00000000	leal (,%ebx,1),%edx
89041d00000000	movl %eax,(,%ebx)
8b0c1d00000000	movl (,%ebx),%ecx
8d141d00000000	leal (,%ebx),%edx
8904ad00000000	movl %eax,(,%ebp,4)
8b0cad00000000	movl (,%ebp,4),%ecx
8d14ad00000000	leal (,%ebp,4),%edx
89046d08000000	movl %eax,8(,%ebp,2)
8b0c6d08000000	movl 8(,%ebp,2),%ecx
8d146d08000000	leal 8(,%ebp,2),%edx
8904ed00000000 3:1:sym+0	movl %eax,sym(,%ebp,8)
8b0ced00000000 3:1:sym+0	movl sym(,%ebp,8),%ecx
8d14ed00000000 3:1:sym+0	leal sym(,%ebp,8),%edx
89042d00000000	movl %eax,(,%ebp,1)
8b0c2d00000000	movl (,%ebp,1),%ecx
8d142d00000000	leal (,%ebp,1),%edx
89042d00000000	movl %eax,(,%ebp)
8b0c2d00000000	movl (,%ebp),%ecx
8d142d00000000	leal (,%ebp),%edx
8904b500000000	movl %eax,(,%esi,4)
8b0cb500000000	movl (,%esi,4),%ecx
8d14b500000000	leal (,%esi,4),%edx
89047508000000	movl %eax,8(,%esi,2)
8b0c7508000000	movl 8(,%esi,2),%ecx
8d147508000000	leal 8(,%esi,2),%edx
8904f500000000 3:1:sym+0	movl %eax,sym(,%esi,8)
8b0cf500000000 3:1:sym+0	movl sym(,%esi,8),%ecx
8d14f500000000 3:1:sym+0	leal sym(,%esi,8),%edx
89043500000000	movl %eax,(,%esi,1)
8b0c3500000000	movl (,%esi,1),%ecx
8d143500000000	leal (,%esi,1),%edx
89043500000000	movl %eax,(,%esi)
8b0c3500000000	movl (,%esi),%ecx
8d143500000000	leal (,%esi),%edx
8904bd00000000	movl %eax,(,%edi,4)
8b0cbd00000000	movl (,%edi,4),%ecx
8d14bd00000000	leal (,%edi,4),%edx
89047d08000000	movl %eax,8(,%edi,2)
8b0c7d08000000	movl 8(,%edi,2),%ecx
8d147d08000000	leal 8(,%edi,2),%edx
8904fd00000000 3:1:sym+0	movl %eax,sym(,%edi,8)
8b0cfd00000000 3:1:sym+0	movl sym(,%edi,8),%ecx
8d14fd00000000 3:1:sym+0	leal sym(,%edi,8),%edx
89043d00000000	movl %eax,(,%edi,1)
8b0c3d00000000	movl (,%edi,1),%ecx
8d143d00000000	leal (,%edi,1),%edx
89043d00000000	movl %eax,(,%edi)
8b0c3d00000000	movl (,%edi),%ecx
8d143d00000000	leal (,%edi),%edx
a300000000 1:1:sym+0	movl %eax,sym
8b0d00000000 2:1:sym+0	movl sym,%ecx
8d1500000000 2:1:sym+0	leal sym,%edx
a304000000 1:1:sym+0	movl %eax,sym+4
8b0d04000000 2:1:sym+0	movl sym+4,%ecx
8d1504000000 2:1:sym+0	leal sym+4,%edx
a3fcffffff 1:1:sym+0	movl %eax,sym-4
8b0dfcffffff 2:1:sym+0	movl sym-4,%ecx
8d15fcffffff 2:1:sym+0	leal sym-4,%edx
a334120000	movl %eax,0x1234
8b0d34120000	movl 0x1234,%ecx
8d1534120000	leal 0x1234,%edx
a300000000	movl %eax,0
8b0d00000000	movl 0,%ecx
8d1500000000	leal 0,%edx
a3fcffffff	movl %eax,-4
8b0dfcffffff	movl -4,%ecx
8d15fcffffff	leal -4,%edx
64a300000000	movl %eax,%fs:0
648b0d00000000	movl %fs:0,%ecx
648d1500000000	leal %fs:0,%edx
65a300000000	movl %eax,%gs:0
658b0d00000000	movl %gs:0,%ecx
658d1500000000	leal %gs:0,%edx
65a314000000	movl %eax,%gs:20
658b0d14000000	movl %gs:20,%ecx
658d1514000000	leal %gs:20,%edx
648900	movl %eax,%fs:(%eax)
648b08	movl %fs:(%eax),%ecx
648d10	leal %fs:(%eax),%edx
26894308	movl %eax,%es:8(%ebx)
268b4b08	movl %es:8(%ebx),%ecx
268d5308	leal %es:8(%ebx),%edx
2ea300000000 2:1:sym+0	movl %eax,%cs:sym
2e8b0d00000000 3:1:sym+0	movl %cs:sym,%ecx
2e8d1500000000 3:1:sym+0	leal %cs:sym,%edx
a300000000 1:1:sym+0	movl %eax,%ds:sym
8b0d00000000 2:1:sym+0	movl %ds:sym,%ecx
8d1500000000 2:1:sym+0	leal %ds:sym,%edx
894500	movl %eax,%ss:(%ebp)
8b4d00	movl %ss:(%ebp),%ecx
8d5500	leal %ss:(%ebp),%edx
65a300000000 2:17:sym+0	movl %eax,%gs:sym@NTPOFF
658b0d00000000 3:17:sym+0	movl %gs:sym@NTPOFF,%ecx
658d1500000000 3:17:sym+0	leal %gs:sym@NTPOFF,%edx
678907	movl %eax,(%bx)
678b0f	movl (%bx),%ecx
678d17	leal (%bx),%edx
678900	movl %eax,(%bx,%si)
678b08	movl (%bx,%si),%ecx
678d10	leal (%bx,%si),%edx
678903	movl %eax,(%bp,%di)
678b0b	movl (%bp,%di),%ecx
678d13	leal (%bp,%di),%edx
67894608	movl %eax,8(%bp)
678b4e08	movl 8(%bp),%ecx
678d5608	leal 8(%bp),%edx
678904	movl %eax,(%si)
678b0c	movl (%si),%ecx
678d14	leal (%si),%edx
678905	movl %eax,(%di)
678b0d	movl (%di),%ecx
678d15	leal (%di),%edx
6789873412	movl %eax,0x1234(%bx)
678b8f3412	movl 0x1234(%bx),%ecx
678d973412	leal 0x1234(%bx),%edx
67894600	movl %eax,(%bp)
678b4e00	movl (%bp),%ecx
678d5600	leal (%bp),%edx
678901	movl %eax,(%bx,%di)
678b09	movl (%bx,%di),%ecx
678d11	leal (%bx,%di),%edx
678902	movl %eax,(%bp,%si)
678b0a	movl (%bp,%si),%ecx
678d12	leal (%bp,%si),%edx
678942fe	movl %eax,-2(%bp,%si)
678b4afe	movl -2(%bp,%si),%ecx
678d52fe	leal -2(%bp,%si),%edx
6789870000 3:20:sym+0	movl %eax,sym(%bx)
678b8f0000 3:20:sym+0	movl sym(%bx),%ecx
678d970000 3:20:sym+0	leal sym(%bx),%edx
898300000000 2:3:sym+0	movl %eax,sym@GOT(%ebx)
8b8b00000000 2:43:sym+0	movl sym@GOT(%ebx),%ecx
8d9300000000 2:3:sym+0	leal sym@GOT(%ebx),%edx
898300000000 2:9:sym+0	movl %eax,sym@GOTOFF(%ebx)
8b8b00000000 2:9:sym+0	movl sym@GOTOFF(%ebx),%ecx
8d9300000000 2:9:sym+0	leal sym@GOTOFF(%ebx),%edx
a300000000 1:4:sym+0	movl %eax,sym@PLT
8b0d00000000 2:4:sym+0	movl sym@PLT,%ecx
8d1500000000 2:4:sym+0	leal sym@PLT,%edx
refused	movl %eax,sym@TLSGD(,%ebx,1)
refused	movl sym@TLSGD(,%ebx,1),%ecx
refused	leal sym@TLSGD(,%ebx,1),%edx
refused	movl %eax,sym@GOTTPOFF(%ebx)
8b8b00000000 2:33:sym+0	movl sym@GOTTPOFF(%ebx),%ecx
refused	leal sym@GOTTPOFF(%ebx),%edx
898000000000 2:17:sym+0	movl %eax,sym@NTPOFF(%eax)
8b8800000000 2:17:sym+0	movl sym@NTPOFF(%eax),%ecx
8d9000000000 2:17:sym+0	leal sym@NTPOFF(%eax),%edx
refused	movl %eax,sym@INDNTPOFF
8b0d00000000 2:15:sym+0	movl sym@INDNTPOFF,%ecx
refused	leal sym@INDNTPOFF,%edx
898000000000 2:34:sym+0	movl %eax,sym@TPOFF(%eax)
8b8800000000 2:34:sym+0	movl sym@TPOFF(%eax),%ecx
8d9000000000 2:34:sym+0	leal sym@TPOFF(%eax),%edx
refused	movl %eax,sym@GOTNTPOFF(%ebx)
8b8b00000000 2:16:sym+0	movl sym@GOTNTPOFF(%ebx),%ecx
refused	leal sym@GOTNTPOFF(%ebx),%edx
898000000000 2:32:sym+0	movl %eax,sym@DTPOFF(%eax)
8b8800000000 2:32:sym+0	movl sym@DTPOFF(%eax),%ecx
8d9000000000 2:32:sym+0	leal sym@DTPOFF(%eax),%edx
refused	movl %eax,sym@TLSLDM(%ebx)
refused	movl sym@TLSLDM(%ebx),%ecx
refused	leal sym@TLSLDM(%ebx),%edx
a300000000 1:3:sym+0	movl %eax,sym@GOT
8b0d00000000 2:43:sym+0	movl sym@GOT,%ecx
8d1500000000 2:3:sym+0	leal sym@GOT,%edx
a300000000 1:9:sym+0	movl %eax,sym@GOTOFF
8b0d00000000 2:9:sym+0	movl sym@GOTOFF,%ecx
8d1500000000 2:9:sym+0	leal sym@GOTOFF,%edx
898302000000 2:10:_GLOBAL_OFFSET_TABLE_+0	movl %eax,_GLOBAL_OFFSET_TABLE_(%ebx)
8b8b02000000 2:10:_GLOBAL_OFFSET_TABLE_+0	movl _GLOBAL_OFFSET_TABLE_(%ebx),%ecx
8d9302000000 2:10:_GLOBAL_OFFSET_TABLE_+0	leal _GLOBAL_OFFSET_TABLE_(%ebx),%edx
refused	movl %eax,sym@TLSGD(%ebx)
refused	movl sym@TLSGD(%ebx),%ecx
refused	leal sym@TLSGD(%ebx),%edx
refused	movl %eax,sym@TLSDESC(%ebx)
refused	movl sym@TLSDESC(%ebx),%ecx
8d9300000000 2:39:sym+0	leal sym@TLSDESC(%ebx),%edx
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
refused	movl %eax,(%rax)
refused	movl (%rax),%ecx
refused	leal (%rax),%edx
refused	movl %eax,(%r8d)
refused	movl (%r8d),%ecx
refused	leal (%r8d),%edx
refused	movl %eax,sym(%rip)
refused	movl sym(%rip),%ecx
refused	leal sym(%rip),%edx
refused	movl %eax,(%eax,%r8d)
refused	movl (%eax,%r8d),%ecx
refused	leal (%eax,%r8d),%edx
898089674523	movl %eax,0x123456789(%eax)
8b8889674523	movl 0x123456789(%eax),%ecx
8d9089674523	leal 0x123456789(%eax),%edx
c60001	movb $1,(%eax)
66c7003412	movw $0x1234,(%eax)
c70078563412	movl $0x12345678,(%eax)
830004	addl $4,(%eax)
803800	cmpb $0,(%eax)
ff00	incl (%eax)
ff30	pushl (%eax)
ff10	call *(%eax)
c70000000000 2:1:sym2+0	movl $sym2,(%eax)
f70001000000	testl $1,(%eax)
c12003	shll $3,(%eax)
6b0064	imull $100,(%eax),%eax
dd00	fldl (%eax)
8800	movb %al,(%eax)
668b18	movw (%eax),%bx
c6804523010001	movb $1,0x12345(%eax)
66c780452301003412	movw $0x1234,0x12345(%eax)
c7804523010078563412	movl $0x12345678,0x12345(%eax)
83804523010004	addl $4,0x12345(%eax)
80b84523010000	cmpb $0,0x12345(%eax)
ff8045230100	incl 0x12345(%eax)
ffb045230100	pushl 0x12345(%eax)
ff9045230100	call *0x12345(%eax)
c7804523010000000000 6:1:sym2+0	movl $sym2,0x12345(%eax)
f7804523010001000000	testl $1,0x12345(%eax)
c1a04523010003	shll $3,0x12345(%eax)
6b804523010064	imull $100,0x12345(%eax),%eax
dd8045230100	fldl 0x12345(%eax)
888045230100	movb %al,0x12345(%eax)
668b9845230100	movw 0x12345(%eax),%bx
c6417f01	movb $1,127(%ecx)
66c7417f3412	movw $0x1234,127(%ecx)
c7417f78563412	movl $0x12345678,127(%ecx)
83417f04	addl $4,127(%ecx)
80797f00	cmpb $0,127(%ecx)
ff417f	incl 127(%ecx)
ff717f	pushl 127(%ecx)
ff517f	call *127(%ecx)
c7417f00000000 3:1:sym2+0	movl $sym2,127(%ecx)
f7417f01000000	testl $1,127(%ecx)
c1617f03	shll $3,127(%ecx)
6b417f64	imull $100,127(%ecx),%eax
dd417f	fldl 127(%ecx)
88417f	movb %al,127(%ecx)
668b597f	movw 127(%ecx),%bx
c6810400000001 2:1:sym+0	movb $1,sym+4(%ecx)
66c781040000003412 3:1:sym+0	movw $0x1234,sym+4(%ecx)
c7810400000078563412 2:1:sym+0	movl $0x12345678,sym+4(%ecx)
83810400000004 2:1:sym+0	addl $4,sym+4(%ecx)
80b90400000000 2:1:sym+0	cmpb $0,sym+4(%ecx)
ff8104000000 2:1:sym+0	incl sym+4(%ecx)
ffb104000000 2:1:sym+0	pushl sym+4(%ecx)
ff9104000000 2:1:sym+0	call *sym+4(%ecx)
c7810400000000000000 2:1:sym+0 6:1:sym2+0	movl $sym2,sym+4(%ecx)
f7810400000001000000 2:1:sym+0	testl $1,sym+4(%ecx)
c1a10400000003 2:1:sym+0	shll $3,sym+4(%ecx)
6b810400000064 2:1:sym+0	imull $100,sym+4(%ecx),%eax
dd8104000000 2:1:sym+0	fldl sym+4(%ecx)
888104000000 2:1:sym+0	movb %al,sym+4(%ecx)
668b9904000000 3:1:sym+0	movw sym+4(%ecx),%bx
c6827fffffff01	movb $1,-129(%edx)
66c7827fffffff3412	movw $0x1234,-129(%edx)
c7827fffffff78563412	movl $0x12345678,-129(%edx)
83827fffffff04	addl $4,-129(%edx)
80ba7fffffff00	cmpb $0,-129(%edx)
ff827fffffff	incl -129(%edx)
ffb27fffffff	pushl -129(%edx)
ff927fffffff	call *-129(%edx)
c7827fffffff00000000 6:1:sym2+0	movl $sym2,-129(%edx)
f7827fffffff01000000	testl $1,-129(%edx)
c1a27fffffff03	shll $3,-129(%edx)
6b827fffffff64	imull $100,-129(%edx),%eax
dd827fffffff	fldl -129(%edx)
88827fffffff	movb %al,-129(%edx)
668b9a7fffffff	movw -129(%edx),%bx
c643f801	movb $1,-8(%ebx)
66c743f83412	movw $0x1234,-8(%ebx)
c743f878563412	movl $0x12345678,-8(%ebx)
8343f804	addl $4,-8(%ebx)
807bf800	cmpb $0,-8(%ebx)
ff43f8	incl -8(%ebx)
ff73f8	pushl -8(%ebx)
ff53f8	call *-8(%ebx)
c743f800000000 3:1:sym2+0	movl $sym2,-8(%ebx)
f743f801000000	testl $1,-8(%ebx)
c163f803	shll $3,-8(%ebx)
6b43f864	imull $100,-8(%ebx),%eax
dd43f8	fldl -8(%ebx)
8843f8	movb %al,-8(%ebx)
668b5bf8	movw -8(%ebx),%bx
c6830000000001 2:1:sym+0	movb $1,sym(%ebx)
66c783000000003412 3:1:sym+0	movw $0x1234,sym(%ebx)
c7830000000078563412 2:1:sym+0	movl $0x12345678,sym(%ebx)
83830000000004 2:1:sym+0	addl $4,sym(%ebx)
80bb0000000000 2:1:sym+0	cmpb $0,sym(%ebx)
ff8300000000 2:1:sym+0	incl sym(%ebx)
ffb300000000 2:1:sym+0	pushl sym(%ebx)
ff9300000000 2:1:sym+0	call *sym(%ebx)
c7830000000000000000 2:1:sym+0 6:1:sym2+0	movl $sym2,sym(%ebx)
f7830000000001000000 2:1:sym+0	testl $1,sym(%ebx)
c1a30000000003 2:1:sym+0	shll $3,sym(%ebx)
6b830000000064 2:1:sym+0	imull $100,sym(%ebx),%eax
dd8300000000 2:1:sym+0	fldl sym(%ebx)
888300000000 2:1:sym+0	movb %al,sym(%ebx)
668b9b00000000 3:1:sym+0	movw sym(%ebx),%bx
c644248001	movb $1,-128(%esp)
66c74424803412	movw $0x1234,-128(%esp)
c744248078563412	movl $0x12345678,-128(%esp)
8344248004	addl $4,-128(%esp)
807c248000	cmpb $0,-128(%esp)
ff442480	incl -128(%esp)
ff742480	pushl -128(%esp)
ff542480	call *-128(%esp)
c744248000000000 4:1:sym2+0	movl $sym2,-128(%esp)
f744248001000000	testl $1,-128(%esp)
c164248003	shll $3,-128(%esp)
6b44248064	imull $100,-128(%esp),%eax
dd442480	fldl -128(%esp)
88442480	movb %al,-128(%esp)
668b5c2480	movw -128(%esp),%bx
c6450801	movb $1,8(%ebp)
66c745083412	movw $0x1234,8(%ebp)
c7450878563412	movl $0x12345678,8(%ebp)
83450804	addl $4,8(%ebp)
807d0800	cmpb $0,8(%ebp)
ff4508	incl 8(%ebp)
ff7508	pushl 8(%ebp)
ff5508	call *8(%ebp)
c7450800000000 3:1:sym2+0	movl $sym2,8(%ebp)
f7450801000000	testl $1,8(%ebp)
c1650803	shll $3,8(%ebp)
6b450864	imull $100,8(%ebp),%eax
dd4508	fldl 8(%ebp)
884508	movb %al,8(%ebp)
668b5d08	movw 8(%ebp),%bx
c6450001	movb $1,0(%ebp)
66c745003412	movw $0x1234,0(%ebp)
c7450078563412	movl $0x12345678,0(%ebp)
83450004	addl $4,0(%ebp)
807d0000	cmpb $0,0(%ebp)
ff4500	incl 0(%ebp)
ff7500	pushl 0(%ebp)
ff5500	call *0(%ebp)
c7450000000000 3:1:sym2+0	movl $sym2,0(%ebp)
f7450001000000	testl $1,0(%ebp)
c1650003	shll $3,0(%ebp)
6b450064	imull $100,0(%ebp),%eax
dd4500	fldl 0(%ebp)
884500	movb %al,0(%ebp)
668b5d00	movw 0(%ebp),%bx
c6868000000001	movb $1,128(%esi)
66c786800000003412	movw $0x1234,128(%esi)
c7868000000078563412	movl $0x12345678,128(%esi)
83868000000004	addl $4,128(%esi)
80be8000000000	cmpb $0,128(%esi)
ff8680000000	incl 128(%esi)
ffb680000000	pushl 128(%esi)
ff9680000000	call *128(%esi)
c7868000000000000000 6:1:sym2+0	movl $sym2,128(%esi)
f7868000000001000000	testl $1,128(%esi)
c1a68000000003	shll $3,128(%esi)
6b868000000064	imull $100,128(%esi),%eax
dd8680000000	fldl 128(%esi)
888680000000	movb %al,128(%esi)
668b9e80000000	movw 128(%esi),%bx
c60701	movb $1,(%edi)
66c7073412	movw $0x1234,(%edi)
c70778563412	movl $0x12345678,(%edi)
830704	addl $4,(%edi)
803f00	cmpb $0,(%edi)
ff07	incl (%edi)
ff37	pushl (%edi)
ff17	call *(%edi)
c70700000000 2:1:sym2+0	movl $sym2,(%edi)
f70701000000	testl $1,(%edi)
c12703	shll $3,(%edi)
6b0764	imull $100,(%edi),%eax
dd07	fldl (%edi)
8807	movb %al,(%edi)
668b1f	movw (%edi),%bx
c6874523010001	movb $1,0x12345(%edi)
66c787452301003412	movw $0x1234,0x12345(%edi)
c7874523010078563412	movl $0x12345678,0x12345(%edi)
83874523010004	addl $4,0x12345(%edi)
80bf4523010000	cmpb $0,0x12345(%edi)
ff8745230100	incl 0x12345(%edi)
ffb745230100	pushl 0x12345(%edi)
ff9745230100	call *0x12345(%edi)
c7874523010000000000 6:1:sym2+0	movl $sym2,0x12345(%edi)
f7874523010001000000	testl $1,0x12345(%edi)
c1a74523010003	shll $3,0x12345(%edi)
6b874523010064	imull $100,0x12345(%edi),%eax
dd8745230100	fldl 0x12345(%edi)
888745230100	movb %al,0x12345(%edi)
668b9f45230100	movw 0x12345(%edi),%bx
c644800401	movb $1,4(%eax,%eax,4)
66c74480043412	movw $0x1234,4(%eax,%eax,4)
c744800478563412	movl $0x12345678,4(%eax,%eax,4)
8344800404	addl $4,4(%eax,%eax,4)
807c800400	cmpb $0,4(%eax,%eax,4)
ff448004	incl 4(%eax,%eax,4)
ff748004	pushl 4(%eax,%eax,4)
ff548004	call *4(%eax,%eax,4)
c744800400000000 4:1:sym2+0	movl $sym2,4(%eax,%eax,4)
f744800401000000	testl $1,4(%eax,%eax,4)
c164800403	shll $3,4(%eax,%eax,4)
6b44800464	imull $100,4(%eax,%eax,4),%eax
dd448004	fldl 4(%eax,%eax,4)
88448004	movb %al,4(%eax,%eax,4)
668b5c8004	movw 4(%eax,%eax,4),%bx
c684c80010000001	movb $1,0x1000(%eax,%ecx,8)
66c784c8001000003412	movw $0x1234,0x1000(%eax,%ecx,8)
c784c80010000078563412	movl $0x12345678,0x1000(%eax,%ecx,8)
8384c80010000004	addl $4,0x1000(%eax,%ecx,8)
80bcc80010000000	cmpb $0,0x1000(%eax,%ecx,8)
ff84c800100000	incl 0x1000(%eax,%ecx,8)
ffb4c800100000	pushl 0x1000(%eax,%ecx,8)
ff94c800100000	call *0x1000(%eax,%ecx,8)
c784c80010000000000000 7:1:sym2+0	movl $sym2,0x1000(%eax,%ecx,8)
f784c80010000001000000	testl $1,0x1000(%eax,%ecx,8)
c1a4c80010000003	shll $3,0x1000(%eax,%ecx,8)
6b84c80010000064	imull $100,0x1000(%eax,%ecx,8),%eax
dd84c800100000	fldl 0x1000(%eax,%ecx,8)
8884c800100000	movb %al,0x1000(%eax,%ecx,8)
668b9cc800100000	movw 0x1000(%eax,%ecx,8),%bx
c684d00000000001 3:1:sym+0	movb $1,sym(%eax,%edx,8)
66c784d0000000003412 4:1:sym+0	movw $0x1234,sym(%eax,%edx,8)
c784d00000000078563412 3:1:sym+0	movl $0x12345678,sym(%eax,%edx,8)
8384d00000000004 3:1:sym+0	addl $4,sym(%eax,%edx,8)
80bcd00000000000 3:1:sym+0	cmpb $0,sym(%eax,%edx,8)
ff84d000000000 3:1:sym+0	incl sym(%eax,%edx,8)
ffb4d000000000 3:1:sym+0	pushl sym(%eax,%edx,8)
ff94d000000000 3:1:sym+0	call *sym(%eax,%edx,8)
c784d00000000000000000 3:1:sym+0 7:1:sym2+0	movl $sym2,sym(%eax,%edx,8)
f784d00000000001000000 3:1:sym+0	testl $1,sym(%eax,%edx,8)
c1a4d00000000003 3:1:sym+0	shll $3,sym(%eax,%edx,8)
6b84d00000000064 3:1:sym+0	imull $100,sym(%eax,%edx,8),%eax
dd84d000000000 3:1:sym+0	fldl sym(%eax,%edx,8)
8884d000000000 3:1:sym+0	movb %al,sym(%eax,%edx,8)
668b9cd000000000 4:1:sym+0	movw sym(%eax,%edx,8),%bx
c6042801	movb $1,(%eax,%ebp)
66c704283412	movw $0x1234,(%eax,%ebp)
c7042878563412	movl $0x12345678,(%eax,%ebp)
83042804	addl $4,(%eax,%ebp)
803c2800	cmpb $0,(%eax,%ebp)
ff0428	incl (%eax,%ebp)
ff3428	pushl (%eax,%ebp)
ff1428	call *(%eax,%ebp)
c7042800000000 3:1:sym2+0	movl $sym2,(%eax,%ebp)
f7042801000000	testl $1,(%eax,%ebp)
c1242803	shll $3,(%eax,%ebp)
6b042864	imull $100,(%eax,%ebp),%eax
dd0428	fldl (%eax,%ebp)
880428	movb %al,(%eax,%ebp)
668b1c28	movw (%eax,%ebp),%bx
c6043001	movb $1,(%eax,%esi,1)
66c704303412	movw $0x1234,(%eax,%esi,1)
c7043078563412	movl $0x12345678,(%eax,%esi,1)
83043004	addl $4,(%eax,%esi,1)
803c3000	cmpb $0,(%eax,%esi,1)
ff0430	incl (%eax,%esi,1)
ff3430	pushl (%eax,%esi,1)
ff1430	call *(%eax,%esi,1)
c7043000000000 3:1:sym2+0	movl $sym2,(%eax,%esi,1)
f7043001000000	testl $1,(%eax,%esi,1)
c1243003	shll $3,(%eax,%esi,1)
6b043064	imull $100,(%eax,%esi,1),%eax
dd0430	fldl (%eax,%esi,1)
880430	movb %al,(%eax,%esi,1)
668b1c30	movw (%eax,%esi,1),%bx
c6047801	movb $1,(%eax,%edi,2)
66c704783412	movw $0x1234,(%eax,%edi,2)
c7047878563412	movl $0x12345678,(%eax,%edi,2)
83047804	addl $4,(%eax,%edi,2)
803c7800	cmpb $0,(%eax,%edi,2)
ff0478	incl (%eax,%edi,2)
ff3478	pushl (%eax,%edi,2)
ff1478	call *(%eax,%edi,2)
c7047800000000 3:1:sym2+0	movl $sym2,(%eax,%edi,2)
f7047801000000	testl $1,(%eax,%edi,2)
c1247803	shll $3,(%eax,%edi,2)
6b047864	imull $100,(%eax,%edi,2),%eax
dd0478	fldl (%eax,%edi,2)
880478	movb %al,(%eax,%edi,2)
668b1c78	movw (%eax,%edi,2),%bx
c644810401	movb $1,4(%ecx,%eax,4)
66c74481043412	movw $0x1234,4(%ecx,%eax,4)
c744810478563412	movl $0x12345678,4(%ecx,%eax,4)
8344810404	addl $4,4(%ecx,%eax,4)
807c810400	cmpb $0,4(%ecx,%eax,4)
ff448104	incl 4(%ecx,%eax,4)
ff748104	pushl 4(%ecx,%eax,4)
ff548104	call *4(%ecx,%eax,4)
c744810400000000 4:1:sym2+0	movl $sym2,4(%ecx,%eax,4)
f744810401000000	testl $1,4(%ecx,%eax,4)
c164810403	shll $3,4(%ecx,%eax,4)
6b44810464	imull $100,4(%ecx,%eax,4),%eax
dd448104	fldl 4(%ecx,%eax,4)
88448104	movb %al,4(%ecx,%eax,4)
668b5c8104	movw 4(%ecx,%eax,4),%bx
c684c90010000001	movb $1,0x1000(%ecx,%ecx,8)
66c784c9001000003412	movw $0x1234,0x1000(%ecx,%ecx,8)
c784c90010000078563412	movl $0x12345678,0x1000(%ecx,%ecx,8)
8384c90010000004	addl $4,0x1000(%ecx,%ecx,8)
80bcc90010000000	cmpb $0,0x1000(%ecx,%ecx,8)
ff84c900100000	incl 0x1000(%ecx,%ecx,8)
ffb4c900100000	pushl 0x1000(%ecx,%ecx,8)
ff94c900100000	call *0x1000(%ecx,%ecx,8)
c784c90010000000000000 7:1:sym2+0	movl $sym2,0x1000(%ecx,%ecx,8)
f784c90010000001000000	testl $1,0x1000(%ecx,%ecx,8)
c1a4c90010000003	shll $3,0x1000(%ecx,%ecx,8)
6b84c90010000064	imull $100,0x1000(%ecx,%ecx,8),%eax
dd84c900100000	fldl 0x1000(%ecx,%ecx,8)
8884c900100000	movb %al,0x1000(%ecx,%ecx,8)
668b9cc900100000	movw 0x1000(%ecx,%ecx,8),%bx
c684d10000000001 3:1:sym+0	movb $1,sym(%ecx,%edx,8)
66c784d1000000003412 4:1:sym+0	movw $0x1234,sym(%ecx,%edx,8)
c784d10000000078563412 3:1:sym+0	movl $0x12345678,sym(%ecx,%edx,8)
8384d10000000004 3:1:sym+0	addl $4,sym(%ecx,%edx,8)
80bcd10000000000 3:1:sym+0	cmpb $0,sym(%ecx,%edx,8)
ff84d100000000 3:1:sym+0	incl sym(%ecx,%edx,8)
ffb4d100000000 3:1:sym+0	pushl sym(%ecx,%edx,8)
ff94d100000000 3:1:sym+0	call *sym(%ecx,%edx,8)
c784d10000000000000000 3:1:sym+0 7:1:sym2+0	movl $sym2,sym(%ecx,%edx,8)
f784d10000000001000000 3:1:sym+0	testl $1,sym(%ecx,%edx,8)
c1a4d10000000003 3:1:sym+0	shll $3,sym(%ecx,%edx,8)
6b84d10000000064 3:1:sym+0	imull $100,sym(%ecx,%edx,8),%eax
dd84d100000000 3:1:sym+0	fldl sym(%ecx,%edx,8)
8884d100000000 3:1:sym+0	movb %al,sym(%ecx,%edx,8)
668b9cd100000000 4:1:sym+0	movw sym(%ecx,%edx,8),%bx
c6042901	movb $1,(%ecx,%ebp)
66c704293412	movw $0x1234,(%ecx,%ebp)
c7042978563412	movl $0x12345678,(%ecx,%ebp)
83042904	addl $4,(%ecx,%ebp)
803c2900	cmpb $0,(%ecx,%ebp)
ff0429	incl (%ecx,%ebp)
ff3429	pushl (%ecx,%ebp)
ff1429	call *(%ecx,%ebp)
c7042900000000 3:1:sym2+0	movl $sym2,(%ecx,%ebp)
f7042901000000	testl $1,(%ecx,%ebp)
c1242903	shll $3,(%ecx,%ebp)
6b042964	imull $100,(%ecx,%ebp),%eax
dd0429	fldl (%ecx,%ebp)
880429	movb %al,(%ecx,%ebp)
668b1c29	movw (%ecx,%ebp),%bx
c6043101	movb $1,(%ecx,%esi,1)
66c704313412	movw $0x1234,(%ecx,%esi,1)
c7043178563412	movl $0x12345678,(%ecx,%esi,1)
83043104	addl $4,(%ecx,%esi,1)
803c3100	cmpb $0,(%ecx,%esi,1)
ff0431	incl (%ecx,%esi,1)
ff3431	pushl (%ecx,%esi,1)
ff1431	call *(%ecx,%esi,1)
c7043100000000 3:1:sym2+0	movl $sym2,(%ecx,%esi,1)
f7043101000000	testl $1,(%ecx,%esi,1)
c1243103	shll $3,(%ecx,%esi,1)
6b043164	imull $100,(%ecx,%esi,1),%eax
dd0431	fldl (%ecx,%esi,1)
880431	movb %al,(%ecx,%esi,1)
668b1c31	movw (%ecx,%esi,1),%bx
c6047901	movb $1,(%ecx,%edi,2)
66c704793412	movw $0x1234,(%ecx,%edi,2)
c7047978563412	movl $0x12345678,(%ecx,%edi,2)
83047904	addl $4,(%ecx,%edi,2)
803c7900	cmpb $0,(%ecx,%edi,2)
ff0479	incl (%ecx,%edi,2)
ff3479	pushl (%ecx,%edi,2)
ff1479	call *(%ecx,%edi,2)
c7047900000000 3:1:sym2+0	movl $sym2,(%ecx,%edi,2)
f7047901000000	testl $1,(%ecx,%edi,2)
c1247903	shll $3,(%ecx,%edi,2)
6b047964	imull $100,(%ecx,%edi,2),%eax
dd0479	fldl (%ecx,%edi,2)
880479	movb %al,(%ecx,%edi,2)
668b1c79	movw (%ecx,%edi,2),%bx
c644820401	movb $1,4(%edx,%eax,4)
66c74482043412	movw $0x1234,4(%edx,%eax,4)
c744820478563412	movl $0x12345678,4(%edx,%eax,4)
8344820404	addl $4,4(%edx,%eax,4)
807c820400	cmpb $0,4(%edx,%eax,4)
ff448204	incl 4(%edx,%eax,4)
ff748204	pushl 4(%edx,%eax,4)
ff548204	call *4(%edx,%eax,4)
c744820400000000 4:1:sym2+0	movl $sym2,4(%edx,%eax,4)
f744820401000000	testl $1,4(%edx,%eax,4)
c164820403	shll $3,4(%edx,%eax,4)
6b44820464	imull $100,4(%edx,%eax,4),%eax
dd448204	fldl 4(%edx,%eax,4)
88448204	movb %al,4(%edx,%eax,4)
668b5c8204	movw 4(%edx,%eax,4),%bx
c684ca0010000001	movb $1,0x1000(%edx,%ecx,8)
66c784ca001000003412	movw $0x1234,0x1000(%edx,%ecx,8)
c784ca0010000078563412	movl $0x12345678,0x1000(%edx,%ecx,8)
8384ca0010000004	addl $4,0x1000(%edx,%ecx,8)
80bcca0010000000	cmpb $0,0x1000(%edx,%ecx,8)
ff84ca00100000	incl 0x1000(%edx,%ecx,8)
ffb4ca00100000	pushl 0x1000(%edx,%ecx,8)
ff94ca00100000	call *0x1000(%edx,%ecx,8)
c784ca0010000000000000 7:1:sym2+0	movl $sym2,0x1000(%edx,%ecx,8)
f784ca0010000001000000	testl $1,0x1000(%edx,%ecx,8)
c1a4ca0010000003	shll $3,0x1000(%edx,%ecx,8)
6b84ca0010000064	imull $100,0x1000(%edx,%ecx,8),%eax
dd84ca00100000	fldl 0x1000(%edx,%ecx,8)
8884ca00100000	movb %al,0x1000(%edx,%ecx,8)
668b9cca00100000	movw 0x1000(%edx,%ecx,8),%bx
c684d20000000001 3:1:sym+0	movb $1,sym(%edx,%edx,8)
66c784d2000000003412 4:1:sym+0	movw $0x1234,sym(%edx,%edx,8)
c784d20000000078563412 3:1:sym+0	movl $0x12345678,sym(%edx,%edx,8)
8384d20000000004 3:1:sym+0	addl $4,sym(%edx,%edx,8)
80bcd20000000000 3:1:sym+0	cmpb $0,sym(%edx,%edx,8)
ff84d200000000 3:1:sym+0	incl sym(%edx,%edx,8)
ffb4d200000000 3:1:sym+0	pushl sym(%edx,%edx,8)
ff94d200000000 3:1:sym+0	call *sym(%edx,%edx,8)
c784d20000000000000000 3:1:sym+0 7:1:sym2+0	movl $sym2,sym(%edx,%edx,8)
f784d20000000001000000 3:1:sym+0	testl $1,sym(%edx,%edx,8)
c1a4d20000000003 3:1:sym+0	shll $3,sym(%edx,%edx,8)
6b84d20000000064 3:1:sym+0	imull $100,sym(%edx,%edx,8),%eax
dd84d200000000 3:1:sym+0	fldl sym(%edx,%edx,8)
8884d200000000 3:1:sym+0	movb %al,sym(%edx,%edx,8)
668b9cd200000000 4:1:sym+0	movw sym(%edx,%edx,8),%bx
c6042a01	movb $1,(%edx,%ebp)
66c7042a3412	movw $0x1234,(%edx,%ebp)
c7042a78563412	movl $0x12345678,(%edx,%ebp)
83042a04	addl $4,(%edx,%ebp)
803c2a00	cmpb $0,(%edx,%ebp)
ff042a	incl (%edx,%ebp)
ff342a	pushl (%edx,%ebp)
ff142a	call *(%edx,%ebp)
c7042a00000000 3:1:sym2+0	movl $sym2,(%edx,%ebp)
f7042a01000000	testl $1,(%edx,%ebp)
c1242a03	shll $3,(%edx,%ebp)
6b042a64	imull $100,(%edx,%ebp),%eax
dd042a	fldl (%edx,%ebp)
88042a	movb %al,(%edx,%ebp)
668b1c2a	movw (%edx,%ebp),%bx
c6043201	movb $1,(%edx,%esi,1)
66c704323412	movw $0x1234,(%edx,%esi,1)
c7043278563412	movl $0x12345678,(%edx,%esi,1)
83043204	addl $4,(%edx,%esi,1)
803c3200	cmpb $0,(%edx,%esi,1)
ff0432	incl (%edx,%esi,1)
ff3432	pushl (%edx,%esi,1)
ff1432	call *(%edx,%esi,1)
c7043200000000 3:1:sym2+0	movl $sym2,(%edx,%esi,1)
f7043201000000	testl $1,(%edx,%esi,1)
c1243203	shll $3,(%edx,%esi,1)
6b043264	imull $100,(%edx,%esi,1),%eax
dd0432	fldl (%edx,%esi,1)
880432	movb %al,(%edx,%esi,1)
668b1c32	movw (%edx,%esi,1),%bx
c6047a01	movb $1,(%edx,%edi,2)
66c7047a3412	movw $0x1234,(%edx,%edi,2)
c7047a78563412	movl $0x12345678,(%edx,%edi,2)
83047a04	addl $4,(%edx,%edi,2)
803c7a00	cmpb $0,(%edx,%edi,2)
ff047a	incl (%edx,%edi,2)
ff347a	pushl (%edx,%edi,2)
ff147a	call *(%edx,%edi,2)
c7047a00000000 3:1:sym2+0	movl $sym2,(%edx,%edi,2)
f7047a01000000	testl $1,(%edx,%edi,2)
c1247a03	shll $3,(%edx,%edi,2)
6b047a64	imull $100,(%edx,%edi,2),%eax
dd047a	fldl (%edx,%edi,2)
88047a	movb %al,(%edx,%edi,2)
668b1c7a	movw (%edx,%edi,2),%bx
c644830401	movb $1,4(%ebx,%eax,4)
66c74483043412	movw $0x1234,4(%ebx,%eax,4)
c744830478563412	movl $0x12345678,4(%ebx,%eax,4)
8344830404	addl $4,4(%ebx,%eax,4)
807c830400	cmpb $0,4(%ebx,%eax,4)
ff448304	incl 4(%ebx,%eax,4)
ff748304	pushl 4(%ebx,%eax,4)
ff548304	call *4(%ebx,%eax,4)
c744830400000000 4:1:sym2+0	movl $sym2,4(%ebx,%eax,4)
f744830401000000	testl $1,4(%ebx,%eax,4)
c164830403	shll $3,4(%ebx,%eax,4)
6b44830464	imull $100,4(%ebx,%eax,4),%eax
dd448304	fldl 4(%ebx,%eax,4)
88448304	movb %al,4(%ebx,%eax,4)
668b5c8304	movw 4(%ebx,%eax,4),%bx
c684cb0010000001	movb $1,0x1000(%ebx,%ecx,8)
66c784cb001000003412	movw $0x1234,0x1000(%ebx,%ecx,8)
c784cb0010000078563412	movl $0x12345678,0x1000(%ebx,%ecx,8)
8384cb0010000004	addl $4,0x1000(%ebx,%ecx,8)
80bccb0010000000	cmpb $0,0x1000(%ebx,%ecx,8)
ff84cb00100000	incl 0x1000(%ebx,%ecx,8)
ffb4cb00100000	pushl 0x1000(%ebx,%ecx,8)
ff94cb00100000	call *0x1000(%ebx,%ecx,8)
c784cb0010000000000000 7:1:sym2+0	movl $sym2,0x1000(%ebx,%ecx,8)
f784cb0010000001000000	testl $1,0x1000(%ebx,%ecx,8)
c1a4cb0010000003	shll $3,0x1000(%ebx,%ecx,8)
6b84cb0010000064	imull $100,0x1000(%ebx,%ecx,8),%eax
dd84cb00100000	fldl 0x1000(%ebx,%ecx,8)
8884cb00100000	movb %al,0x1000(%ebx,%ecx,8)
668b9ccb00100000	movw 0x1000(%ebx,%ecx,8),%bx
c684d30000000001 3:1:sym+0	movb $1,sym(%ebx,%edx,8)
66c784d3000000003412 4:1:sym+0	movw $0x1234,sym(%ebx,%edx,8)
c784d30000000078563412 3:1:sym+0	movl $0x12345678,sym(%ebx,%edx,8)
8384d30000000004 3:1:sym+0	addl $4,sym(%ebx,%edx,8)
80bcd30000000000 3:1:sym+0	cmpb $0,sym(%ebx,%edx,8)
ff84d300000000 3:1:sym+0	incl sym(%ebx,%edx,8)
ffb4d300000000 3:1:sym+0	pushl sym(%ebx,%edx,8)
ff94d300000000 3:1:sym+0	call *sym(%ebx,%edx,8)
c784d30000000000000000 3:1:sym+0 7:1:sym2+0	movl $sym2,sym(%ebx,%edx,8)
f784d30000000001000000 3:1:sym+0	testl $1,sym(%ebx,%edx,8)
c1a4d30000000003 3:1:sym+0	shll $3,sym(%ebx,%edx,8)
6b84d30000000064 3:1:sym+0	imull $100,sym(%ebx,%edx,8),%eax
dd84d300000000 3:1:sym+0	fldl sym(%ebx,%edx,8)
8884d300000000 3:1:sym+0	movb %al,sym(%ebx,%edx,8)
668b9cd300000000 4:1:sym+0	movw sym(%ebx,%edx,8),%bx
c6042b01	movb $1,(%ebx,%ebp)
66c7042b3412	movw $0x1234,(%ebx,%ebp)
c7042b78563412	movl $0x12345678,(%ebx,%ebp)
83042b04	addl $4,(%ebx,%ebp)
803c2b00	cmpb $0,(%ebx,%ebp)
ff042b	incl (%ebx,%ebp)
ff342b	pushl (%ebx,%ebp)
ff142b	call *(%ebx,%ebp)
c7042b00000000 3:1:sym2+0	movl $sym2,(%ebx,%ebp)
f7042b01000000	testl $1,(%ebx,%ebp)
c1242b03	shll $3,(%ebx,%ebp)
6b042b64	imull $100,(%ebx,%ebp),%eax
dd042b	fldl (%ebx,%ebp)
88042b	movb %al,(%ebx,%ebp)
668b1c2b	movw (%ebx,%ebp),%bx
c6043301	movb $1,(%ebx,%esi,1)
66c704333412	movw $0x1234,(%ebx,%esi,1)
c7043378563412	movl $0x12345678,(%ebx,%esi,1)
83043304	addl $4,(%ebx,%esi,1)
803c3300	cmpb $0,(%ebx,%esi,1)
ff0433	incl (%ebx,%esi,1)
ff3433	pushl (%ebx,%esi,1)
ff1433	call *(%ebx,%esi,1)
c7043300000000 3:1:sym2+0	movl $sym2,(%ebx,%esi,1)
f7043301000000	testl $1,(%ebx,%esi,1)
c1243303	shll $3,(%ebx,%esi,1)
6b043364	imull $100,(%ebx,%esi,1),%eax
dd0433	fldl (%ebx,%esi,1)
880433	movb %al,(%ebx,%esi,1)
668b1c33	movw (%ebx,%esi,1),%bx
c6047b01	movb $1,(%ebx,%edi,2)
66c7047b3412	movw $0x1234,(%ebx,%edi,2)
c7047b78563412	movl $0x12345678,(%ebx,%edi,2)
83047b04	addl $4,(%ebx,%edi,2)
803c7b00	cmpb $0,(%ebx,%edi,2)
ff047b	incl (%ebx,%edi,2)
ff347b	pushl (%ebx,%edi,2)
ff147b	call *(%ebx,%edi,2)
c7047b00000000 3:1:sym2+0	movl $sym2,(%ebx,%edi,2)
f7047b01000000	testl $1,(%ebx,%edi,2)
c1247b03	shll $3,(%ebx,%edi,2)
6b047b64	imull $100,(%ebx,%edi,2),%eax
dd047b	fldl (%ebx,%edi,2)
88047b	movb %al,(%ebx,%edi,2)
668b1c7b	movw (%ebx,%edi,2),%bx
c644840401	movb $1,4(%esp,%eax,4)
66c74484043412	movw $0x1234,4(%esp,%eax,4)
c744840478563412	movl $0x12345678,4(%esp,%eax,4)
8344840404	addl $4,4(%esp,%eax,4)
807c840400	cmpb $0,4(%esp,%eax,4)
ff448404	incl 4(%esp,%eax,4)
ff748404	pushl 4(%esp,%eax,4)
ff548404	call *4(%esp,%eax,4)
c744840400000000 4:1:sym2+0	movl $sym2,4(%esp,%eax,4)
f744840401000000	testl $1,4(%esp,%eax,4)
c164840403	shll $3,4(%esp,%eax,4)
6b44840464	imull $100,4(%esp,%eax,4),%eax
dd448404	fldl 4(%esp,%eax,4)
88448404	movb %al,4(%esp,%eax,4)
668b5c8404	movw 4(%esp,%eax,4),%bx
c684cc0010000001	movb $1,0x1000(%esp,%ecx,8)
66c784cc001000003412	movw $0x1234,0x1000(%esp,%ecx,8)
c784cc0010000078563412	movl $0x12345678,0x1000(%esp,%ecx,8)
8384cc0010000004	addl $4,0x1000(%esp,%ecx,8)
80bccc0010000000	cmpb $0,0x1000(%esp,%ecx,8)
ff84cc00100000	incl 0x1000(%esp,%ecx,8)
ffb4cc00100000	pushl 0x1000(%esp,%ecx,8)
ff94cc00100000	call *0x1000(%esp,%ecx,8)
c784cc0010000000000000 7:1:sym2+0	movl $sym2,0x1000(%esp,%ecx,8)
f784cc0010000001000000	testl $1,0x1000(%esp,%ecx,8)
c1a4cc0010000003	shll $3,0x1000(%esp,%ecx,8)
6b84cc0010000064	imull $100,0x1000(%esp,%ecx,8),%eax
dd84cc00100000	fldl 0x1000(%esp,%ecx,8)
8884cc00100000	movb %al,0x1000(%esp,%ecx,8)
668b9ccc00100000	movw 0x1000(%esp,%ecx,8),%bx
c684d40000000001 3:1:sym+0	movb $1,sym(%esp,%edx,8)
66c784d4000000003412 4:1:sym+0	movw $0x1234,sym(%esp,%edx,8)
c784d40000000078563412 3:1:sym+0	movl $0x12345678,sym(%esp,%edx,8)
8384d40000000004 3:1:sym+0	addl $4,sym(%esp,%edx,8)
80bcd40000000000 3:1:sym+0	cmpb $0,sym(%esp,%edx,8)
ff84d400000000 3:1:sym+0	incl sym(%esp,%edx,8)
ffb4d400000000 3:1:sym+0	pushl sym(%esp,%edx,8)
ff94d400000000 3:1:sym+0	call *sym(%esp,%edx,8)
c784d40000000000000000 3:1:sym+0 7:1:sym2+0	movl $sym2,sym(%esp,%edx,8)
f784d40000000001000000 3:1:sym+0	testl $1,sym(%esp,%edx,8)
c1a4d40000000003 3:1:sym+0	shll $3,sym(%esp,%edx,8)
6b84d40000000064 3:1:sym+0	imull $100,sym(%esp,%edx,8),%eax
dd84d400000000 3:1:sym+0	fldl sym(%esp,%edx,8)
8884d400000000 3:1:sym+0	movb %al,sym(%esp,%edx,8)
668b9cd400000000 4:1:sym+0	movw sym(%esp,%edx,8),%bx
c6042c01	movb $1,(%esp,%ebp)
66c7042c3412	movw $0x1234,(%esp,%ebp)
c7042c78563412	movl $0x12345678,(%esp,%ebp)
83042c04	addl $4,(%esp,%ebp)
803c2c00	cmpb $0,(%esp,%ebp)
ff042c	incl (%esp,%ebp)
ff342c	pushl (%esp,%ebp)
ff142c	call *(%esp,%ebp)
c7042c00000000 3:1:sym2+0	movl $sym2,(%esp,%ebp)
f7042c01000000	testl $1,(%esp,%ebp)
c1242c03	shll $3,(%esp,%ebp)
6b042c64	imull $100,(%esp,%ebp),%eax
dd042c	fldl (%esp,%ebp)
88042c	movb %al,(%esp,%ebp)
668b1c2c	movw (%esp,%ebp),%bx
c6043401	movb $1,(%esp,%esi,1)
66c704343412	movw $0x1234,(%esp,%esi,1)
c7043478563412	movl $0x12345678,(%esp,%esi,1)
83043404	addl $4,(%esp,%esi,1)
803c3400	cmpb $0,(%esp,%esi,1)
ff0434	incl (%esp,%esi,1)
ff3434	pushl (%esp,%esi,1)
ff1434	call *(%esp,%esi,1)
c7043400000000 3:1:sym2+0	movl $sym2,(%esp,%esi,1)
f7043401000000	testl $1,(%esp,%esi,1)
c1243403	shll $3,(%esp,%esi,1)
6b043464	imull $100,(%esp,%esi,1),%eax
dd0434	fldl (%esp,%esi,1)
880434	movb %al,(%esp,%esi,1)
668b1c34	movw (%esp,%esi,1),%bx
c6047c01	movb $1,(%esp,%edi,2)
66c7047c3412	movw $0x1234,(%esp,%edi,2)
c7047c78563412	movl $0x12345678,(%esp,%edi,2)
83047c04	addl $4,(%esp,%edi,2)
803c7c00	cmpb $0,(%esp,%edi,2)
ff047c	incl (%esp,%edi,2)
ff347c	pushl (%esp,%edi,2)
ff147c	call *(%esp,%edi,2)
c7047c00000000 3:1:sym2+0	movl $sym2,(%esp,%edi,2)
f7047c01000000	testl $1,(%esp,%edi,2)
c1247c03	shll $3,(%esp,%edi,2)
6b047c64	imull $100,(%esp,%edi,2),%eax
dd047c	fldl (%esp,%edi,2)
88047c	movb %al,(%esp,%edi,2)
668b1c7c	movw (%esp,%edi,2),%bx
c644850401	movb $1,4(%ebp,%eax,4)
66c74485043412	movw $0x1234,4(%ebp,%eax,4)
c744850478563412	movl $0x12345678,4(%ebp,%eax,4)
8344850404	addl $4,4(%ebp,%eax,4)
807c850400	cmpb $0,4(%ebp,%eax,4)
ff448504	incl 4(%ebp,%eax,4)
ff748504	pushl 4(%ebp,%eax,4)
ff548504	call *4(%ebp,%eax,4)
c744850400000000 4:1:sym2+0	movl $sym2,4(%ebp,%eax,4)
f744850401000000	testl $1,4(%ebp,%eax,4)
c164850403	shll $3,4(%ebp,%eax,4)
6b44850464	imull $100,4(%ebp,%eax,4),%eax
dd448504	fldl 4(%ebp,%eax,4)
88448504	movb %al,4(%ebp,%eax,4)
668b5c8504	movw 4(%ebp,%eax,4),%bx
c684cd0010000001	movb $1,0x1000(%ebp,%ecx,8)
66c784cd001000003412	movw $0x1234,0x1000(%ebp,%ecx,8)
c784cd0010000078563412	movl $0x12345678,0x1000(%ebp,%ecx,8)
8384cd0010000004	addl $4,0x1000(%ebp,%ecx,8)
80bccd0010000000	cmpb $0,0x1000(%ebp,%ecx,8)
ff84cd00100000	incl 0x1000(%ebp,%ecx,8)
ffb4cd00100000	pushl 0x1000(%ebp,%ecx,8)
ff94cd00100000	call *0x1000(%ebp,%ecx,8)
c784cd0010000000000000 7:1:sym2+0	movl $sym2,0x1000(%ebp,%ecx,8)
f784cd0010000001000000	testl $1,0x1000(%ebp,%ecx,8)
c1a4cd0010000003	shll $3,0x1000(%ebp,%ecx,8)
6b84cd0010000064	imull $100,0x1000(%ebp,%ecx,8),%eax
dd84cd00100000	fldl 0x1000(%ebp,%ecx,8)
8884cd00100000	movb %al,0x1000(%ebp,%ecx,8)
668b9ccd00100000	movw 0x1000(%ebp,%ecx,8),%bx
c684d50000000001 3:1:sym+0	movb $1,sym(%ebp,%edx,8)
66c784d5000000003412 4:1:sym+0	movw $0x1234,sym(%ebp,%edx,8)
c784d50000000078563412 3:1:sym+0	movl $0x12345678,sym(%ebp,%edx,8)
8384d50000000004 3:1:sym+0	addl $4,sym(%ebp,%edx,8)
80bcd50000000000 3:1:sym+0	cmpb $0,sym(%ebp,%edx,8)
ff84d500000000 3:1:sym+0	incl sym(%ebp,%edx,8)
ffb4d500000000 3:1:sym+0	pushl sym(%ebp,%edx,8)
ff94d500000000 3:1:sym+0	call *sym(%ebp,%edx,8)
c784d50000000000000000 3:1:sym+0 7:1:sym2+0	movl $sym2,sym(%ebp,%edx,8)
f784d50000000001000000 3:1:sym+0	testl $1,sym(%ebp,%edx,8)
c1a4d50000000003 3:1:sym+0	shll $3,sym(%ebp,%edx,8)
6b84d50000000064 3:1:sym+0	imull $100,sym(%ebp,%edx,8),%eax
dd84d500000000 3:1:sym+0	fldl sym(%ebp,%edx,8)
8884d500000000 3:1:sym+0	movb %al,sym(%ebp,%edx,8)
668b9cd500000000 4:1:sym+0	movw sym(%ebp,%edx,8),%bx
c6442d0001	movb $1,(%ebp,%ebp)
66c7442d003412	movw $0x1234,(%ebp,%ebp)
c7442d0078563412	movl $0x12345678,(%ebp,%ebp)
83442d0004	addl $4,(%ebp,%ebp)
807c2d0000	cmpb $0,(%ebp,%ebp)
ff442d00	incl (%ebp,%ebp)
ff742d00	pushl (%ebp,%ebp)
ff542d00	call *(%ebp,%ebp)
c7442d0000000000 4:1:sym2+0	movl $sym2,(%ebp,%ebp)
f7442d0001000000	testl $1,(%ebp,%ebp)
c1642d0003	shll $3,(%ebp,%ebp)
6b442d0064	imull $100,(%ebp,%ebp),%eax
dd442d00	fldl (%ebp,%ebp)
88442d00	movb %al,(%ebp,%ebp)
668b5c2d00	movw (%ebp,%ebp),%bx
c644350001	movb $1,(%ebp,%esi,1)
66c74435003412	movw $0x1234,(%ebp,%esi,1)
c744350078563412	movl $0x12345678,(%ebp,%esi,1)
8344350004	addl $4,(%ebp,%esi,1)
807c350000	cmpb $0,(%ebp,%esi,1)
ff443500	incl (%ebp,%esi,1)
ff743500	pushl (%ebp,%esi,1)
ff543500	call *(%ebp,%esi,1)
c744350000000000 4:1:sym2+0	movl $sym2,(%ebp,%esi,1)
f744350001000000	testl $1,(%ebp,%esi,1)
c164350003	shll $3,(%ebp,%esi,1)
6b44350064	imull $100,(%ebp,%esi,1),%eax
dd443500	fldl (%ebp,%esi,1)
88443500	movb %al,(%ebp,%esi,1)
668b5c3500	movw (%ebp,%esi,1),%bx
c6447d0001	movb $1,(%ebp,%edi,2)
66c7447d003412	movw $0x1234,(%ebp,%edi,2)
c7447d0078563412	movl $0x12345678,(%ebp,%edi,2)
83447d0004	addl $4,(%ebp,%edi,2)
807c7d0000	cmpb $0,(%ebp,%edi,2)
ff447d00	incl (%ebp,%edi,2)
ff747d00	pushl (%ebp,%edi,2)
ff547d00	call *(%ebp,%edi,2)
c7447d0000000000 4:1:sym2+0	movl $sym2,(%ebp,%edi,2)
f7447d0001000000	testl $1,(%ebp,%edi,2)
c1647d0003	shll $3,(%ebp,%edi,2)
6b447d0064	imull $100,(%ebp,%edi,2),%eax
dd447d00	fldl (%ebp,%edi,2)
88447d00	movb %al,(%ebp,%edi,2)
668b5c7d00	movw (%ebp,%edi,2),%bx
c644860401	movb $1,4(%esi,%eax,4)
66c74486043412	movw $0x1234,4(%esi,%eax,4)
c744860478563412	movl $0x12345678,4(%esi,%eax,4)
8344860404	addl $4,4(%esi,%eax,4)
807c860400	cmpb $0,4(%esi,%eax,4)
ff448604	incl 4(%esi,%eax,4)
ff748604	pushl 4(%esi,%eax,4)
ff548604	call *4(%esi,%eax,4)
c744860400000000 4:1:sym2+0	movl $sym2,4(%esi,%eax,4)
f744860401000000	testl $1,4(%esi,%eax,4)
c164860403	shll $3,4(%esi,%eax,4)
6b44860464	imull $100,4(%esi,%eax,4),%eax
dd448604	fldl 4(%esi,%eax,4)
88448604	movb %al,4(%esi,%eax,4)
668b5c8604	movw 4(%esi,%eax,4),%bx
c684ce0010000001	movb $1,0x1000(%esi,%ecx,8)
66c784ce001000003412	movw $0x1234,0x1000(%esi,%ecx,8)
c784ce0010000078563412	movl $0x12345678,0x1000(%esi,%ecx,8)
8384ce0010000004	addl $4,0x1000(%esi,%ecx,8)
80bcce0010000000	cmpb $0,0x1000(%esi,%ecx,8)
ff84ce00100000	incl 0x1000(%esi,%ecx,8)
ffb4ce00100000	pushl 0x1000(%esi,%ecx,8)
ff94ce00100000	call *0x1000(%esi,%ecx,8)
c784ce0010000000000000 7:1:sym2+0	movl $sym2,0x1000(%esi,%ecx,8)
f784ce0010000001000000	testl $1,0x1000(%esi,%ecx,8)
c1a4ce0010000003	shll $3,0x1000(%esi,%ecx,8)
6b84ce0010000064	imull $100,0x1000(%esi,%ecx,8),%eax
dd84ce00100000	fldl 0x1000(%esi,%ecx,8)
8884ce00100000	movb %al,0x1000(%esi,%ecx,8)
668b9cce00100000	movw 0x1000(%esi,%ecx,8),%bx
c684d60000000001 3:1:sym+0	movb $1,sym(%esi,%edx,8)
66c784d6000000003412 4:1:sym+0	movw $0x1234,sym(%esi,%edx,8)
c784d60000000078563412 3:1:sym+0	movl $0x12345678,sym(%esi,%edx,8)
8384d60000000004 3:1:sym+0	addl $4,sym(%esi,%edx,8)
80bcd60000000000 3:1:sym+0	cmpb $0,sym(%esi,%edx,8)
ff84d600000000 3:1:sym+0	incl sym(%esi,%edx,8)
ffb4d600000000 3:1:sym+0	pushl sym(%esi,%edx,8)
ff94d600000000 3:1:sym+0	call *sym(%esi,%edx,8)
c784d60000000000000000 3:1:sym+0 7:1:sym2+0	movl $sym2,sym(%esi,%edx,8)
f784d60000000001000000 3:1:sym+0	testl $1,sym(%esi,%edx,8)
c1a4d60000000003 3:1:sym+0	shll $3,sym(%esi,%edx,8)
6b84d60000000064 3:1:sym+0	imull $100,sym(%esi,%edx,8),%eax
dd84d600000000 3:1:sym+0	fldl sym(%esi,%edx,8)
8884d600000000 3:1:sym+0	movb %al,sym(%esi,%edx,8)
668b9cd600000000 4:1:sym+0	movw sym(%esi,%edx,8),%bx
c6042e01	movb $1,(%esi,%ebp)
66c7042e3412	movw $0x1234,(%esi,%ebp)
c7042e78563412	movl $0x12345678,(%esi,%ebp)
83042e04	addl $4,(%esi,%ebp)
803c2e00	cmpb $0,(%esi,%ebp)
ff042e	incl (%esi,%ebp)
ff342e	pushl (%esi,%ebp)
ff142e	call *(%esi,%ebp)
c7042e00000000 3:1:sym2+0	movl $sym2,(%esi,%ebp)
f7042e01000000	testl $1,(%esi,%ebp)
c1242e03	shll $3,(%esi,%ebp)
6b042e64	imull $100,(%esi,%ebp),%eax
dd042e	fldl (%esi,%ebp)
88042e	movb %al,(%esi,%ebp)
668b1c2e	movw (%esi,%ebp),%bx
c6043601	movb $1,(%esi,%esi,1)
66c704363412	movw $0x1234,(%esi,%esi,1)
c7043678563412	movl $0x12345678,(%esi,%esi,1)
83043604	addl $4,(%esi,%esi,1)
803c3600	cmpb $0,(%esi,%esi,1)
ff0436	incl (%esi,%esi,1)
ff3436	pushl (%esi,%esi,1)
ff1436	call *(%esi,%esi,1)
c7043600000000 3:1:sym2+0	movl $sym2,(%esi,%esi,1)
f7043601000000	testl $1,(%esi,%esi,1)
c1243603	shll $3,(%esi,%esi,1)
6b043664	imull $100,(%esi,%esi,1),%eax
dd0436	fldl (%esi,%esi,1)
880436	movb %al,(%esi,%esi,1)
668b1c36	movw (%esi,%esi,1),%bx
c6047e01	movb $1,(%esi,%edi,2)
66c7047e3412	movw $0x1234,(%esi,%edi,2)
c7047e78563412	movl $0x12345678,(%esi,%edi,2)
83047e04	addl $4,(%esi,%edi,2)
803c7e00	cmpb $0,(%esi,%edi,2)
ff047e	incl (%esi,%edi,2)
ff347e	pushl (%esi,%edi,2)
ff147e	call *(%esi,%edi,2)
c7047e00000000 3:1:sym2+0	movl $sym2,(%esi,%edi,2)
f7047e01000000	testl $1,(%esi,%edi,2)
c1247e03	shll $3,(%esi,%edi,2)
6b047e64	imull $100,(%esi,%edi,2),%eax
dd047e	fldl (%esi,%edi,2)
88047e	movb %al,(%esi,%edi,2)
668b1c7e	movw (%esi,%edi,2),%bx
c644870401	movb $1,4(%edi,%eax,4)
66c74487043412	movw $0x1234,4(%edi,%eax,4)
c744870478563412	movl $0x12345678,4(%edi,%eax,4)
8344870404	addl $4,4(%edi,%eax,4)
807c870400	cmpb $0,4(%edi,%eax,4)
ff448704	incl 4(%edi,%eax,4)
ff748704	pushl 4(%edi,%eax,4)
ff548704	call *4(%edi,%eax,4)
c744870400000000 4:1:sym2+0	movl $sym2,4(%edi,%eax,4)
f744870401000000	testl $1,4(%edi,%eax,4)
c164870403	shll $3,4(%edi,%eax,4)
6b44870464	imull $100,4(%edi,%eax,4),%eax
dd448704	fldl 4(%edi,%eax,4)
88448704	movb %al,4(%edi,%eax,4)
668b5c8704	movw 4(%edi,%eax,4),%bx
c684cf0010000001	movb $1,0x1000(%edi,%ecx,8)
66c784cf001000003412	movw $0x1234,0x1000(%edi,%ecx,8)
c784cf0010000078563412	movl $0x12345678,0x1000(%edi,%ecx,8)
8384cf0010000004	addl $4,0x1000(%edi,%ecx,8)
80bccf0010000000	cmpb $0,0x1000(%edi,%ecx,8)
ff84cf00100000	incl 0x1000(%edi,%ecx,8)
ffb4cf00100000	pushl 0x1000(%edi,%ecx,8)
ff94cf00100000	call *0x1000(%edi,%ecx,8)
c784cf0010000000000000 7:1:sym2+0	movl $sym2,0x1000(%edi,%ecx,8)
f784cf0010000001000000	testl $1,0x1000(%edi,%ecx,8)
c1a4cf0010000003	shll $3,0x1000(%edi,%ecx,8)
6b84cf0010000064	imull $100,0x1000(%edi,%ecx,8),%eax
dd84cf00100000	fldl 0x1000(%edi,%ecx,8)
8884cf00100000	movb %al,0x1000(%edi,%ecx,8)
668b9ccf00100000	movw 0x1000(%edi,%ecx,8),%bx
c684d70000000001 3:1:sym+0	movb $1,sym(%edi,%edx,8)
66c784d7000000003412 4:1:sym+0	movw $0x1234,sym(%edi,%edx,8)
c784d70000000078563412 3:1:sym+0	movl $0x12345678,sym(%edi,%edx,8)
8384d70000000004 3:1:sym+0	addl $4,sym(%edi,%edx,8)
80bcd70000000000 3:1:sym+0	cmpb $0,sym(%edi,%edx,8)
ff84d700000000 3:1:sym+0	incl sym(%edi,%edx,8)
ffb4d700000000 3:1:sym+0	pushl sym(%edi,%edx,8)
ff94d700000000 3:1:sym+0	call *sym(%edi,%edx,8)
c784d70000000000000000 3:1:sym+0 7:1:sym2+0	movl $sym2,sym(%edi,%edx,8)
f784d70000000001000000 3:1:sym+0	testl $1,sym(%edi,%edx,8)
c1a4d70000000003 3:1:sym+0	shll $3,sym(%edi,%edx,8)
6b84d70000000064 3:1:sym+0	imull $100,sym(%edi,%edx,8),%eax
dd84d700000000 3:1:sym+0	fldl sym(%edi,%edx,8)
8884d700000000 3:1:sym+0	movb %al,sym(%edi,%edx,8)
668b9cd700000000 4:1:sym+0	movw sym(%edi,%edx,8),%bx
c6042f01	movb $1,(%edi,%ebp)
66c7042f3412	movw $0x1234,(%edi,%ebp)
c7042f78563412	movl $0x12345678,(%edi,%ebp)
83042f04	addl $4,(%edi,%ebp)
803c2f00	cmpb $0,(%edi,%ebp)
ff042f	incl (%edi,%ebp)
ff342f	pushl (%edi,%ebp)
ff142f	call *(%edi,%ebp)
c7042f00000000 3:1:sym2+0	movl $sym2,(%edi,%ebp)
f7042f01000000	testl $1,(%edi,%ebp)
c1242f03	shll $3,(%edi,%ebp)
6b042f64	imull $100,(%edi,%ebp),%eax
dd042f	fldl (%edi,%ebp)
88042f	movb %al,(%edi,%ebp)
668b1c2f	movw (%edi,%ebp),%bx
c6043701	movb $1,(%edi,%esi,1)
66c704373412	movw $0x1234,(%edi,%esi,1)
c7043778563412	movl $0x12345678,(%edi,%esi,1)
83043704	addl $4,(%edi,%esi,1)
803c3700	cmpb $0,(%edi,%esi,1)
ff0437	incl (%edi,%esi,1)
ff3437	pushl (%edi,%esi,1)
ff1437	call *(%edi,%esi,1)
c7043700000000 3:1:sym2+0	movl $sym2,(%edi,%esi,1)
f7043701000000	testl $1,(%edi,%esi,1)
c1243703	shll $3,(%edi,%esi,1)
6b043764	imull $100,(%edi,%esi,1),%eax
dd0437	fldl (%edi,%esi,1)
880437	movb %al,(%edi,%esi,1)
668b1c37	movw (%edi,%esi,1),%bx
c6047f01	movb $1,(%edi,%edi,2)
66c7047f3412	movw $0x1234,(%edi,%edi,2)
c7047f78563412	movl $0x12345678,(%edi,%edi,2)
83047f04	addl $4,(%edi,%edi,2)
803c7f00	cmpb $0,(%edi,%edi,2)
ff047f	incl (%edi,%edi,2)
ff347f	pushl (%edi,%edi,2)
ff147f	call *(%edi,%edi,2)
c7047f00000000 3:1:sym2+0	movl $sym2,(%edi,%edi,2)
f7047f01000000	testl $1,(%edi,%edi,2)
c1247f03	shll $3,(%edi,%edi,2)
6b047f64	imull $100,(%edi,%edi,2),%eax
dd047f	fldl (%edi,%edi,2)
88047f	movb %al,(%edi,%edi,2)
668b1c7f	movw (%edi,%edi,2),%bx
c604050000000001	movb $1,(,%eax,1)
66c70405000000003412	movw $0x1234,(,%eax,1)
c704050000000078563412	movl $0x12345678,(,%eax,1)
8304050000000004	addl $4,(,%eax,1)
803c050000000000	cmpb $0,(,%eax,1)
ff040500000000	incl (,%eax,1)
ff340500000000	pushl (,%eax,1)
ff140500000000	call *(,%eax,1)
c704050000000000000000 7:1:sym2+0	movl $sym2,(,%eax,1)
f704050000000001000000	testl $1,(,%eax,1)
c124050000000003	shll $3,(,%eax,1)
6b04050000000064	imull $100,(,%eax,1),%eax
dd040500000000	fldl (,%eax,1)
88040500000000	movb %al,(,%eax,1)
668b1c0500000000	movw (,%eax,1),%bx
c604950000000001	movb $1,(,%edx,4)
66c70495000000003412	movw $0x1234,(,%edx,4)
c704950000000078563412	movl $0x12345678,(,%edx,4)
8304950000000004	addl $4,(,%edx,4)
803c950000000000	cmpb $0,(,%edx,4)
ff049500000000	incl (,%edx,4)
ff349500000000	pushl (,%edx,4)
ff149500000000	call *(,%edx,4)
c704950000000000000000 7:1:sym2+0	movl $sym2,(,%edx,4)
f704950000000001000000	testl $1,(,%edx,4)
c124950000000003	shll $3,(,%edx,4)
6b04950000000064	imull $100,(,%edx,4),%eax
dd049500000000	fldl (,%edx,4)
88049500000000	movb %al,(,%edx,4)
668b1c9500000000	movw (,%edx,4),%bx
c604dd0000000001 3:1:sym+0	movb $1,sym(,%ebx,8)
66c704dd000000003412 4:1:sym+0	movw $0x1234,sym(,%ebx,8)
c704dd0000000078563412 3:1:sym+0	movl $0x12345678,sym(,%ebx,8)
8304dd0000000004 3:1:sym+0	addl $4,sym(,%ebx,8)
803cdd0000000000 3:1:sym+0	cmpb $0,sym(,%ebx,8)
ff04dd00000000 3:1:sym+0	incl sym(,%ebx,8)
ff34dd00000000 3:1:sym+0	pushl sym(,%ebx,8)
ff14dd00000000 3:1:sym+0	call *sym(,%ebx,8)
c704dd0000000000000000 3:1:sym+0 7:1:sym2+0	movl $sym2,sym(,%ebx,8)
f704dd0000000001000000 3:1:sym+0	testl $1,sym(,%ebx,8)
c124dd0000000003 3:1:sym+0	shll $3,sym(,%ebx,8)
6b04dd0000000064 3:1:sym+0	imull $100,sym(,%ebx,8),%eax
dd04dd00000000 3:1:sym+0	fldl sym(,%ebx,8)
8804dd00000000 3:1:sym+0	movb %al,sym(,%ebx,8)
668b1cdd00000000 4:1:sym+0	movw sym(,%ebx,8),%bx
c6042d0000000001	movb $1,(,%ebp)
66c7042d000000003412	movw $0x1234,(,%ebp)
c7042d0000000078563412	movl $0x12345678,(,%ebp)
83042d0000000004	addl $4,(,%ebp)
803c2d0000000000	cmpb $0,(,%ebp)
ff042d00000000	incl (,%ebp)
ff342d00000000	pushl (,%ebp)
ff142d00000000	call *(,%ebp)
c7042d0000000000000000 7:1:sym2+0	movl $sym2,(,%ebp)
f7042d0000000001000000	testl $1,(,%ebp)
c1242d0000000003	shll $3,(,%ebp)
6b042d0000000064	imull $100,(,%ebp),%eax
dd042d00000000	fldl (,%ebp)
88042d00000000	movb %al,(,%ebp)
668b1c2d00000000	movw (,%ebp),%bx
c6047d0800000001	movb $1,8(,%edi,2)
66c7047d080000003412	movw $0x1234,8(,%edi,2)
c7047d0800000078563412	movl $0x12345678,8(,%edi,2)
83047d0800000004	addl $4,8(,%edi,2)
803c7d0800000000	cmpb $0,8(,%edi,2)
ff047d08000000	incl 8(,%edi,2)
ff347d08000000	pushl 8(,%edi,2)
ff147d08000000	call *8(,%edi,2)
c7047d0800000000000000 7:1:sym2+0	movl $sym2,8(,%edi,2)
f7047d0800000001000000	testl $1,8(,%edi,2)
c1247d0800000003	shll $3,8(,%edi,2)
6b047d0800000064	imull $100,8(,%edi,2),%eax
dd047d08000000	fldl 8(,%edi,2)
88047d08000000	movb %al,8(,%edi,2)
668b1c7d08000000	movw 8(,%edi,2),%bx
c6053412000001	movb $1,0x1234
66c705341200003412	movw $0x1234,0x1234
c7053412000078563412	movl $0x12345678,0x1234
83053412000004	addl $4,0x1234
803d3412000000	cmpb $0,0x1234
ff0534120000	incl 0x1234
ff3534120000	pushl 0x1234
ff1534120000	call *0x1234
c7053412000000000000 6:1:sym2+0	movl $sym2,0x1234
f7053412000001000000	testl $1,0x1234
c1253412000003	shll $3,0x1234
6b053412000064	imull $100,0x1234,%eax
dd0534120000	fldl 0x1234
a234120000	movb %al,0x1234
668b1d34120000	movw 0x1234,%bx
26c6430801	movb $1,%es:8(%ebx)
2666c743083412	movw $0x1234,%es:8(%ebx)
26c7430878563412	movl $0x12345678,%es:8(%ebx)
2683430804	addl $4,%es:8(%ebx)
26807b0800	cmpb $0,%es:8(%ebx)
26ff4308	incl %es:8(%ebx)
26ff7308	pushl %es:8(%ebx)
26ff5308	call *%es:8(%ebx)
26c7430800000000 4:1:sym2+0	movl $sym2,%es:8(%ebx)
26f7430801000000	testl $1,%es:8(%ebx)
26c1630803	shll $3,%es:8(%ebx)
266b430864	imull $100,%es:8(%ebx),%eax
26dd4308	fldl %es:8(%ebx)
26884308	movb %al,%es:8(%ebx)
26668b5b08	movw %es:8(%ebx),%bx
67c60301	movb $1,(%bp,%di)
6766c7033412	movw $0x1234,(%bp,%di)
67c70378563412	movl $0x12345678,(%bp,%di)
67830304	addl $4,(%bp,%di)
67803b00	cmpb $0,(%bp,%di)
67ff03	incl (%bp,%di)
67ff33	pushl (%bp,%di)
67ff13	call *(%bp,%di)
67c70300000000 3:1:sym2+0	movl $sym2,(%bp,%di)
67f70301000000	testl $1,(%bp,%di)
67c12303	shll $3,(%bp,%di)
676b0364	imull $100,(%bp,%di),%eax
67dd03	fldl (%bp,%di)
678803	movb %al,(%bp,%di)
67668b1b	movw (%bp,%di),%bx
67c60201	movb $1,(%bp,%si)
6766c7023412	movw $0x1234,(%bp,%si)
67c70278563412	movl $0x12345678,(%bp,%si)
67830204	addl $4,(%bp,%si)
67803a00	cmpb $0,(%bp,%si)
67ff02	incl (%bp,%si)
67ff32	pushl (%bp,%si)
67ff12	call *(%bp,%si)
67c70200000000 3:1:sym2+0	movl $sym2,(%bp,%si)
67f70201000000	testl $1,(%bp,%si)
67c12203	shll $3,(%bp,%si)
676b0264	imull $100,(%bp,%si),%eax
67dd02	fldl (%bp,%si)
678802	movb %al,(%bp,%si)
67668b1a	movw (%bp,%si),%bx
refused	movb $1,sym@GOTTPOFF(%ebx)
refused	movw $0x1234,sym@GOTTPOFF(%ebx)
refused	movl $0x12345678,sym@GOTTPOFF(%ebx)
refused	addl $4,sym@GOTTPOFF(%ebx)
refused	cmpb $0,sym@GOTTPOFF(%ebx)
refused	incl sym@GOTTPOFF(%ebx)
refused	pushl sym@GOTTPOFF(%ebx)
refused	call *sym@GOTTPOFF(%ebx)
refused	movl $sym2,sym@GOTTPOFF(%ebx)
refused	testl $1,sym@GOTTPOFF(%ebx)
refused	shll $3,sym@GOTTPOFF(%ebx)
refused	imull $100,sym@GOTTPOFF(%ebx),%eax
refused	fldl sym@GOTTPOFF(%ebx)
refused	movb %al,sym@GOTTPOFF(%ebx)
refused	movw sym@GOTTPOFF(%ebx),%bx
c6050000000001 2:3:sym+0	movb $1,sym@GOT
66c705000000003412 3:3:sym+0	movw $0x1234,sym@GOT
c7050000000078563412 2:3:sym+0	movl $0x12345678,sym@GOT
83050000000004 2:3:sym+0	addl $4,sym@GOT
803d0000000000 2:3:sym+0	cmpb $0,sym@GOT
ff0500000000 2:3:sym+0	incl sym@GOT
ff3500000000 2:43:sym+0	pushl sym@GOT
ff1500000000 2:43:sym+0	call *sym@GOT
c7050000000000000000 2:3:sym+0 6:1:sym2+0	movl $sym2,sym@GOT
f7050000000001000000 2:3:sym+0	testl $1,sym@GOT
c1250000000003 2:3:sym+0	shll $3,sym@GOT
6b050000000064 2:3:sym+0	imull $100,sym@GOT,%eax
dd0500000000 2:3:sym+0	fldl sym@GOT
a200000000 1:3:sym+0	movb %al,sym@GOT
668b1d00000000 3:3:sym+0	movw sym@GOT,%bx
refused	movb $1,(%esp,%esp)
refused	movw $0x1234,(%esp,%esp)
refused	movl $0x12345678,(%esp,%esp)
refused	addl $4,(%esp,%esp)
refused	cmpb $0,(%esp,%esp)
refused	incl (%esp,%esp)
refused	pushl (%esp,%esp)
refused	call *(%esp,%esp)
refused	movl $sym2,(%esp,%esp)
refused	testl $1,(%esp,%esp)
refused	shll $3,(%esp,%esp)
refused	imull $100,(%esp,%esp),%eax
refused	fldl (%esp,%esp)
refused	movb %al,(%esp,%esp)
refused	movw (%esp,%esp),%bx
refused	movb $1,(%eax,%esp)
refused	movw $0x1234,(%eax,%esp)
refused	movl $0x12345678,(%eax,%esp)
refused	addl $4,(%eax,%esp)
refused	cmpb $0,(%eax,%esp)
refused	incl (%eax,%esp)
refused	pushl (%eax,%esp)
refused	call *(%eax,%esp)
refused	movl $sym2,(%eax,%esp)
refused	testl $1,(%eax,%esp)
refused	shll $3,(%eax,%esp)
refused	imull $100,(%eax,%esp),%eax
refused	fldl (%eax,%esp)
refused	movb %al,(%eax,%esp)
refused	movw (%eax,%esp),%bx
refused	movb $1,(%eax,%ebx,3)
refused	movw $0x1234,(%eax,%ebx,3)
refused	movl $0x12345678,(%eax,%ebx,3)
refused	addl $4,(%eax,%ebx,3)
refused	cmpb $0,(%eax,%ebx,3)
refused	incl (%eax,%ebx,3)
refused	pushl (%eax,%ebx,3)
refused	call *(%eax,%ebx,3)
refused	movl $sym2,(%eax,%ebx,3)
refused	testl $1,(%eax,%ebx,3)
refused	shll $3,(%eax,%ebx,3)
refused	imull $100,(%eax,%ebx,3),%eax
refused	fldl (%eax,%ebx,3)
refused	movb %al,(%eax,%ebx,3)
refused	movw (%eax,%ebx,3),%bx
refused	movb $1,(%eax,%ebx,16)
refused	movw $0x1234,(%eax,%ebx,16)
refused	movl $0x12345678,(%eax,%ebx,16)
refused	addl $4,(%eax,%ebx,16)
refused	cmpb $0,(%eax,%ebx,16)
refused	incl (%eax,%ebx,16)
refused	pushl (%eax,%ebx,16)
refused	call *(%eax,%ebx,16)
refused	movl $sym2,(%eax,%ebx,16)
refused	testl $1,(%eax,%ebx,16)
refused	shll $3,(%eax,%ebx,16)
refused	imull $100,(%eax,%ebx,16),%eax
refused	fldl (%eax,%ebx,16)
refused	movb %al,(%eax,%ebx,16)
refused	movw (%eax,%ebx,16),%bx
refused	movb $1,(%ax,%bx)
refused	movw $0x1234,(%ax,%bx)
refused	movl $0x12345678,(%ax,%bx)
refused	addl $4,(%ax,%bx)
refused	cmpb $0,(%ax,%bx)
refused	incl (%ax,%bx)
refused	pushl (%ax,%bx)
refused	call *(%ax,%bx)
refused	movl $sym2,(%ax,%bx)
refused	testl $1,(%ax,%bx)
refused	shll $3,(%ax,%bx)
refused	imull $100,(%ax,%bx),%eax
refused	fldl (%ax,%bx)
refused	movb %al,(%ax,%bx)
refused	movw (%ax,%bx),%bx
refused	movb $1,(%eax,%bx)
refused	movw $0x1234,(%eax,%bx)
refused	movl $0x12345678,(%eax,%bx)
refused	addl $4,(%eax,%bx)
refused	cmpb $0,(%eax,%bx)
refused	incl (%eax,%bx)
refused	pushl (%eax,%bx)
refused	call *(%eax,%bx)
refused	movl $sym2,(%eax,%bx)
refused	testl $1,(%eax,%bx)
refused	shll $3,(%eax,%bx)
refused	imull $100,(%eax,%bx),%eax
refused	fldl (%eax,%bx)
refused	movb %al,(%eax,%bx)
refused	movw (%eax,%bx),%bx
refused	movb $1,(%al)
refused	movw $0x1234,(%al)
refused	movl $0x12345678,(%al)
refused	addl $4,(%al)
refused	cmpb $0,(%al)
refused	incl (%al)
refused	pushl (%al)
refused	call *(%al)
refused	movl $sym2,(%al)
refused	testl $1,(%al)
refused	shll $3,(%al)
refused	imull $100,(%al),%eax
refused	fldl (%al)
refused	movb %al,(%al)
refused	movw (%al),%bx
refused	movb $1,(%eax,%eax,0)
refused	movw $0x1234,(%eax,%eax,0)
refused	movl $0x12345678,(%eax,%eax,0)
refused	addl $4,(%eax,%eax,0)
refused	cmpb $0,(%eax,%eax,0)
refused	incl (%eax,%eax,0)
refused	pushl (%eax,%eax,0)
refused	call *(%eax,%eax,0)
refused	movl $sym2,(%eax,%eax,0)
refused	testl $1,(%eax,%eax,0)
refused	shll $3,(%eax,%eax,0)
refused	imull $100,(%eax,%eax,0),%eax
refused	fldl (%eax,%eax,0)
refused	movb %al,(%eax,%eax,0)
refused	movw (%eax,%eax,0),%bx
refused	movb $1,(%bx,%bp)
refused	movw $0x1234,(%bx,%bp)
refused	movl $0x12345678,(%bx,%bp)
refused	addl $4,(%bx,%bp)
refused	cmpb $0,(%bx,%bp)
refused	incl (%bx,%bp)
refused	pushl (%bx,%bp)
refused	call *(%bx,%bp)
refused	movl $sym2,(%bx,%bp)
refused	testl $1,(%bx,%bp)
refused	shll $3,(%bx,%bp)
refused	imull $100,(%bx,%bp),%eax
refused	fldl (%bx,%bp)
refused	movb %al,(%bx,%bp)
refused	movw (%bx,%bp),%bx
refused	movb $1,(%si,%di)
refused	movw $0x1234,(%si,%di)
refused	movl $0x12345678,(%si,%di)
refused	addl $4,(%si,%di)
refused	cmpb $0,(%si,%di)
refused	incl (%si,%di)
refused	pushl (%si,%di)
refused	call *(%si,%di)
refused	movl $sym2,(%si,%di)
refused	testl $1,(%si,%di)
refused	shll $3,(%si,%di)
refused	imull $100,(%si,%di),%eax
refused	fldl (%si,%di)
refused	movb %al,(%si,%di)
refused	movw (%si,%di),%bx
refused	movb $1,(%cx)
refused	movw $0x1234,(%cx)
refused	movl $0x12345678,(%cx)
refused	addl $4,(%cx)
refused	cmpb $0,(%cx)
refused	incl (%cx)
refused	pushl (%cx)
refused	call *(%cx)
refused	movl $sym2,(%cx)
refused	testl $1,(%cx)
refused	shll $3,(%cx)
refused	imull $100,(%cx),%eax
refused	fldl (%cx)
refused	movb %al,(%cx)
refused	movw (%cx),%bx
refused	movb $1,(%eax,%ebx,4,5)
refused	movw $0x1234,(%eax,%ebx,4,5)
refused	movl $0x12345678,(%eax,%ebx,4,5)
refused	addl $4,(%eax,%ebx,4,5)
refused	cmpb $0,(%eax,%ebx,4,5)
refused	incl (%eax,%ebx,4,5)
refused	pushl (%eax,%ebx,4,5)
refused	call *(%eax,%ebx,4,5)
refused	movl $sym2,(%eax,%ebx,4,5)
refused	testl $1,(%eax,%ebx,4,5)
refused	shll $3,(%eax,%ebx,4,5)
refused	imull $100,(%eax,%ebx,4,5),%eax
refused	fldl (%eax,%ebx,4,5)
refused	movb %al,(%eax,%ebx,4,5)
refused	movw (%eax,%ebx,4,5),%bx
refused	movb $1,(%cr0)
refused	movw $0x1234,(%cr0)
refused	movl $0x12345678,(%cr0)
refused	addl $4,(%cr0)
refused	cmpb $0,(%cr0)
refused	incl (%cr0)
refused	pushl (%cr0)
refused	call *(%cr0)
refused	movl $sym2,(%cr0)
refused	testl $1,(%cr0)
refused	shll $3,(%cr0)
refused	imull $100,(%cr0),%eax
refused	fldl (%cr0)
refused	movb %al,(%cr0)
refused	movw (%cr0),%bx
refused	movb $1,(%eax,%xmm0)
refused	movw $0x1234,(%eax,%xmm0)
refused	movl $0x12345678,(%eax,%xmm0)
refused	addl $4,(%eax,%xmm0)
refused	cmpb $0,(%eax,%xmm0)
refused	incl (%eax,%xmm0)
refused	pushl (%eax,%xmm0)
refused	call *(%eax,%xmm0)
refused	movl $sym2,(%eax,%xmm0)
refused	testl $1,(%eax,%xmm0)
refused	shll $3,(%eax,%xmm0)
refused	imull $100,(%eax,%xmm0),%eax
refused	fldl (%eax,%xmm0)
refused	movb %al,(%eax,%xmm0)
refused	movw (%eax,%xmm0),%bx
refused	movb $1,(%rax)
refused	movw $0x1234,(%rax)
refused	movl $0x12345678,(%rax)
refused	addl $4,(%rax)
refused	cmpb $0,(%rax)
refused	incl (%rax)
refused	pushl (%rax)
refused	call *(%rax)
refused	movl $sym2,(%rax)
refused	testl $1,(%rax)
refused	shll $3,(%rax)
refused	imull $100,(%rax),%eax
refused	fldl (%rax)
refused	movb %al,(%rax)
refused	movw (%rax),%bx
refused	movb $1,(%r8d)
refused	movw $0x1234,(%r8d)
refused	movl $0x12345678,(%r8d)
refused	addl $4,(%r8d)
refused	cmpb $0,(%r8d)
refused	incl (%r8d)
refused	pushl (%r8d)
refused	call *(%r8d)
refused	movl $sym2,(%r8d)
refused	testl $1,(%r8d)
refused	shll $3,(%r8d)
refused	imull $100,(%r8d),%eax
refused	fldl (%r8d)
refused	movb %al,(%r8d)
refused	movw (%r8d),%bx
refused	movb $1,sym(%rip)
refused	movw $0x1234,sym(%rip)
refused	movl $0x12345678,sym(%rip)
refused	addl $4,sym(%rip)
refused	cmpb $0,sym(%rip)
refused	incl sym(%rip)
refused	pushl sym(%rip)
refused	call *sym(%rip)
refused	movl $sym2,sym(%rip)
refused	testl $1,sym(%rip)
refused	shll $3,sym(%rip)
refused	imull $100,sym(%rip),%eax
refused	fldl sym(%rip)
refused	movb %al,sym(%rip)
refused	movw sym(%rip),%bx
refused	movb $1,(%eax,%r8d)
refused	movw $0x1234,(%eax,%r8d)
refused	movl $0x12345678,(%eax,%r8d)
refused	addl $4,(%eax,%r8d)
refused	cmpb $0,(%eax,%r8d)
refused	incl (%eax,%r8d)
refused	pushl (%eax,%r8d)
refused	call *(%eax,%r8d)
refused	movl $sym2,(%eax,%r8d)
refused	testl $1,(%eax,%r8d)
refused	shll $3,(%eax,%r8d)
refused	imull $100,(%eax,%r8d),%eax
refused	fldl (%eax,%r8d)
refused	movb %al,(%eax,%r8d)
refused	movw (%eax,%r8d),%bx
c6808967452301	movb $1,0x123456789(%eax)
66c780896745233412	movw $0x1234,0x123456789(%eax)
c7808967452378563412	movl $0x12345678,0x123456789(%eax)
83808967452304	addl $4,0x123456789(%eax)
80b88967452300	cmpb $0,0x123456789(%eax)
ff8089674523	incl 0x123456789(%eax)
ffb089674523	pushl 0x123456789(%eax)
ff9089674523	call *0x123456789(%eax)
c7808967452300000000 6:1:sym2+0	movl $sym2,0x123456789(%eax)
f7808967452301000000	testl $1,0x123456789(%eax)
c1a08967452303	shll $3,0x123456789(%eax)
6b808967452364	imull $100,0x123456789(%eax),%eax
dd8089674523	fldl 0x123456789(%eax)
888089674523	movb %al,0x123456789(%eax)
668b9889674523	movw 0x123456789(%eax),%bx
88c0	movb %al,%al
88c1	movb %al,%cl
88c2	movb %al,%dl
88c3	movb %al,%bl
88c4	movb %al,%ah
88c5	movb %al,%ch
88c6	movb %al,%dh
88c7	movb %al,%bh
88c8	movb %cl,%al
88c9	movb %cl,%cl
88ca	movb %cl,%dl
88cb	movb %cl,%bl
88cc	movb %cl,%ah
88cd	movb %cl,%ch
88ce	movb %cl,%dh
88cf	movb %cl,%bh
88d0	movb %dl,%al
88d1	movb %dl,%cl
88d2	movb %dl,%dl
88d3	movb %dl,%bl
88d4	movb %dl,%ah
88d5	movb %dl,%ch
88d6	movb %dl,%dh
88d7	movb %dl,%bh
88d8	movb %bl,%al
88d9	movb %bl,%cl
88da	movb %bl,%dl
88db	movb %bl,%bl
88dc	movb %bl,%ah
88dd	movb %bl,%ch
88de	movb %bl,%dh
88df	movb %bl,%bh
88e0	movb %ah,%al
88e1	movb %ah,%cl
88e2	movb %ah,%dl
88e3	movb %ah,%bl
88e4	movb %ah,%ah
88e5	movb %ah,%ch
88e6	movb %ah,%dh
88e7	movb %ah,%bh
88e8	movb %ch,%al
88e9	movb %ch,%cl
88ea	movb %ch,%dl
88eb	movb %ch,%bl
88ec	movb %ch,%ah
88ed	movb %ch,%ch
88ee	movb %ch,%dh
88ef	movb %ch,%bh
88f0	movb %dh,%al
88f1	movb %dh,%cl
88f2	movb %dh,%dl
88f3	movb %dh,%bl
88f4	movb %dh,%ah
88f5	movb %dh,%ch
88f6	movb %dh,%dh
88f7	movb %dh,%bh
88f8	movb %bh,%al
88f9	movb %bh,%cl
88fa	movb %bh,%dl
88fb	movb %bh,%bl
88fc	movb %bh,%ah
88fd	movb %bh,%ch
88fe	movb %bh,%dh
88ff	movb %bh,%bh
88c1	mov %al,%cl
b001	movb $1,%al
b001	mov $1,%al
b000 1:22:sym+0	movb $sym,%al
a200000000 1:1:sym+0	movb %al,sym
a000000000 1:1:sym+0	movb sym,%al
b0ff	movb $-1,%al
88c9	mov %cl,%cl
b101	movb $1,%cl
b101	mov $1,%cl
b100 1:22:sym+0	movb $sym,%cl
880d00000000 2:1:sym+0	movb %cl,sym
8a0d00000000 2:1:sym+0	movb sym,%cl
b1ff	movb $-1,%cl
88d1	mov %dl,%cl
b201	movb $1,%dl
b201	mov $1,%dl
b200 1:22:sym+0	movb $sym,%dl
881500000000 2:1:sym+0	movb %dl,sym
8a1500000000 2:1:sym+0	movb sym,%dl
b2ff	movb $-1,%dl
6689c0	movw %ax,%ax
6689c1	movw %ax,%cx
6689c2	movw %ax,%dx
6689c3	movw %ax,%bx
6689c4	movw %ax,%sp
6689c5	movw %ax,%bp
6689c6	movw %ax,%si
6689c7	movw %ax,%di
6689c8	movw %cx,%ax
6689c9	movw %cx,%cx
6689ca	movw %cx,%dx
6689cb	movw %cx,%bx
6689cc	movw %cx,%sp
6689cd	movw %cx,%bp
6689ce	movw %cx,%si
6689cf	movw %cx,%di
6689d0	movw %dx,%ax
6689d1	movw %dx,%cx
6689d2	movw %dx,%dx
6689d3	movw %dx,%bx
6689d4	movw %dx,%sp
6689d5	movw %dx,%bp
6689d6	movw %dx,%si
6689d7	movw %dx,%di
6689d8	movw %bx,%ax
6689d9	movw %bx,%cx
6689da	movw %bx,%dx
6689db	movw %bx,%bx
6689dc	movw %bx,%sp
6689dd	movw %bx,%bp
6689de	movw %bx,%si
6689df	movw %bx,%di
6689e0	movw %sp,%ax
6689e1	movw %sp,%cx
6689e2	movw %sp,%dx
6689e3	movw %sp,%bx
6689e4	movw %sp,%sp
6689e5	movw %sp,%bp
6689e6	movw %sp,%si
6689e7	movw %sp,%di
6689e8	movw %bp,%ax
6689e9	movw %bp,%cx
6689ea	movw %bp,%dx
6689eb	movw %bp,%bx
6689ec	movw %bp,%sp
6689ed	movw %bp,%bp
6689ee	movw %bp,%si
6689ef	movw %bp,%di
6689f0	movw %si,%ax
6689f1	movw %si,%cx
6689f2	movw %si,%dx
6689f3	movw %si,%bx
6689f4	movw %si,%sp
6689f5	movw %si,%bp
6689f6	movw %si,%si
6689f7	movw %si,%di
6689f8	movw %di,%ax
6689f9	movw %di,%cx
6689fa	movw %di,%dx
6689fb	movw %di,%bx
6689fc	movw %di,%sp
6689fd	movw %di,%bp
6689fe	movw %di,%si
6689ff	movw %di,%di
6689c1	mov %ax,%cx
66b80100	movw $1,%ax
66b80100	mov $1,%ax
66b80000 2:20:sym+0	movw $sym,%ax
66a300000000 2:1:sym+0	movw %ax,sym
66a100000000 2:1:sym+0	movw sym,%ax
66b8ffff	movw $-1,%ax
6689c9	mov %cx,%cx
66b90100	movw $1,%cx
66b90100	mov $1,%cx
66b90000 2:20:sym+0	movw $sym,%cx
66890d00000000 3:1:sym+0	movw %cx,sym
668b0d00000000 3:1:sym+0	movw sym,%cx
66b9ffff	movw $-1,%cx
6689d1	mov %dx,%cx
66ba0100	movw $1,%dx
66ba0100	mov $1,%dx
66ba0000 2:20:sym+0	movw $sym,%dx
66891500000000 3:1:sym+0	movw %dx,sym
668b1500000000 3:1:sym+0	movw sym,%dx
66baffff	movw $-1,%dx
89c0	movl %eax,%eax
89c1	movl %eax,%ecx
89c2	movl %eax,%edx
89c3	movl %eax,%ebx
89c4	movl %eax,%esp
89c5	movl %eax,%ebp
89c6	movl %eax,%esi
89c7	movl %eax,%edi
89c8	movl %ecx,%eax
89c9	movl %ecx,%ecx
89ca	movl %ecx,%edx
89cb	movl %ecx,%ebx
89cc	movl %ecx,%esp
89cd	movl %ecx,%ebp
89ce	movl %ecx,%esi
89cf	movl %ecx,%edi
89d0	movl %edx,%eax
89d1	movl %edx,%ecx
89d2	movl %edx,%edx
89d3	movl %edx,%ebx
89d4	movl %edx,%esp
89d5	movl %edx,%ebp
89d6	movl %edx,%esi
89d7	movl %edx,%edi
89d8	movl %ebx,%eax
89d9	movl %ebx,%ecx
89da	movl %ebx,%edx
89db	movl %ebx,%ebx
89dc	movl %ebx,%esp
89dd	movl %ebx,%ebp
89de	movl %ebx,%esi
89df	movl %ebx,%edi
89e0	movl %esp,%eax
89e1	movl %esp,%ecx
89e2	movl %esp,%edx
89e3	movl %esp,%ebx
89e4	movl %esp,%esp
89e5	movl %esp,%ebp
89e6	movl %esp,%esi
89e7	movl %esp,%edi
89e8	movl %ebp,%eax
89e9	movl %ebp,%ecx
89ea	movl %ebp,%edx
89eb	movl %ebp,%ebx
89ec	movl %ebp,%esp
89ed	movl %ebp,%ebp
89ee	movl %ebp,%esi
89ef	movl %ebp,%edi
89f0	movl %esi,%eax
89f1	movl %esi,%ecx
89f2	movl %esi,%edx
89f3	movl %esi,%ebx
89f4	movl %esi,%esp
89f5	movl %esi,%ebp
89f6	movl %esi,%esi
89f7	movl %esi,%edi
89f8	movl %edi,%eax
89f9	movl %edi,%ecx
89fa	movl %edi,%edx
89fb	movl %edi,%ebx
89fc	movl %edi,%esp
89fd	movl %edi,%ebp
89fe	movl %edi,%esi
89ff	movl %edi,%edi
89c1	mov %eax,%ecx
b801000000	movl $1,%eax
b801000000	mov $1,%eax
b800000000 1:1:sym+0	movl $sym,%eax
a100000000 1:1:sym+0	movl sym,%eax
b8ffffffff	movl $-1,%eax
89c9	mov %ecx,%ecx
b901000000	movl $1,%ecx
b901000000	mov $1,%ecx
b900000000 1:1:sym+0	movl $sym,%ecx
890d00000000 2:1:sym+0	movl %ecx,sym
b9ffffffff	movl $-1,%ecx
89d1	mov %edx,%ecx
ba01000000	movl $1,%edx
ba01000000	mov $1,%edx
ba00000000 1:1:sym+0	movl $sym,%edx
891500000000 2:1:sym+0	movl %edx,sym
8b1500000000 2:1:sym+0	movl sym,%edx
baffffffff	movl $-1,%edx
refused	movl %ax,%ebx
refused	movl %eax,%bx
refused	movw %eax,%ebx
refused	movb %ax,%bl
refused	movb %al,%bx
refused	mov %al,%ebx
refused	mov %eax,%bl
refused	addl %ax,%ebx
refused	addw %al,%bx
c70001000000	mov $1,(%eax)
83050000000001 2:1:sym+0	add $1,sym
ff0500000000 2:1:sym+0	inc sym
ff3500000000 2:1:sym+0	push sym
f718	neg (%eax)
d12500000000 2:1:sym+0	shl $1,sym
refused	movl %eax
refused	movl
refused	mov %eax,%ebx,%ecx
refused	movl $1,$2
refused	movl %eax,$1
refused	addl %eax,$1
refused	movl (%eax),(%ebx)
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
ce	into
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
refused	movl %eax,%r8d
refused	movq %rax,%rbx
refused	movb %al,%sil
refused	movb %spl,%al
refused	movw %r8w,%ax
refused	pushq %rax
refused	push %rax
refused	movabs $1,%rax
refused	cdqe
refused	cltq
refused	cqo
refused	cqto
refused	swapgs
0f05	syscall
0f07	sysret
0f34	sysenter
0f35	sysexit
refused	movslq %eax,%rbx
refused	movq $1,%rax
refused	addq $1,(%eax)
refused	incq (%eax)
refused	pushq $1
refused	popq %rax
refused	retq
refused	leaveq
refused	callq sym
refused	jmpq *%eax
refused	iretq
refused	pushfq
refused	popfq
refused	cmpxchg16b (%eax)
refused	movsq
refused	stosq
refused	lodsq
refused	scasq
refused	cmpsq
refused	rex
refused	rex64 nop
f00f20c0	movl %cr8,%eax
f00f22c0	movl %eax,%cr8
refused	jrcxz 1f | 1:
37	aaa
3f	aas
d50a	aad
d40a	aam
d50a	aad $10
d410	aam $16
27	daa
2f	das
60	pusha
61	popa
60	pushal
61	popal
6660	pushaw
6661	popaw
refused	pushad
6203	bound %eax,(%ebx)
666203	boundw %ax,(%ebx)
c518	lds (%eax),%ebx
c418	les (%eax),%ebx
0fb418	lfs (%eax),%ebx
0fb518	lgs (%eax),%ebx
0fb220	lss (%eax),%esp
66c518	ldsw (%eax),%bx
ea001000001000	ljmp $0x10,$0x1000
9a001000001000	lcall $0x10,$0x1000
ea000000001000 1:1:sym+0	ljmp $0x10,$sym
9a000000000000 1:1:sym2+0 5:20:sym+0	lcall $sym,$sym2
66ea00101000	ljmpw $0x10,$0x1000
ea001000001000	ljmpl $0x10,$0x1000
ff28	ljmp *(%eax)
ff18	lcall *(%eax)
ff2d00000000 2:1:sym+0	ljmp *sym
66ff28	ljmpw *(%eax)
66ff18	lcallw *(%eax)
ff28	ljmpl *(%eax)
refused	ljmp $0x10000,$0
ea001000001000	jmp $0x10,$0x1000
9a001000001000	call $0x10,$0x1000
d6	salc
63c3	arpl %ax,%bx
6303	arpl %ax,(%ebx)
67e300	jcxz 1f | 1:
e300	jecxz 1f | 1:
67e3ff 2:23:sym+0	jcxz sym
e3ff 1:23:sym+0	jecxz sym
e2ff 1:23:sym+0	loop sym
0e	push %cs
1e	push %ds
06	push %es
16	push %ss
0fa0	push %fs
0fa8	push %gs
refused	pop %cs
1f	pop %ds
07	pop %es
17	pop %ss
0fa1	pop %fs
0fa9	pop %gs
0e	pushl %cs
660e	pushw %cs
0fa0	pushl %fs
660fa0	pushw %fs
1f	popl %ds
661f	popw %ds
0fa9	popl %gs
660fa9	popw %gs
9c	pushf
9d	popf
9c	pushfl
9d	popfl
669c	pushfw
669d	popfw
cf	iret
cf	iretl
66cf	iretw
c9	leave
c9	leavel
66c9	leavew
c3	ret
c3	retl
66c3	retw
c20800	ret $8
66c20800	retw $8
cb	lret
cb	lretl
66cb	lretw
ca0800	lret $8
66ca0800	lretw $8
ffd0	call *%eax
ffd0	calll *%eax
66ffd0	callw *%ax
66ffd0	call *%ax
ffe0	jmp *%eax
ffe0	jmpl *%eax
66ffe0	jmpw *%ax
66ffe0	jmp *%ax
ff20	jmp *(%eax)
ff20	jmpl *(%eax)
66ff20	jmpw *(%eax)
ff1500000000 2:1:sym+0	call *sym
ff2500000000 2:1:sym+0	jmp *sym
ff148500000000 3:1:sym+0	call *sym(,%eax,4)
refused	jmp *%al
ffd0	call %eax
ffe0	jmp %eax
ff20	jmp (%eax)
ff10	call (%eax)
refused	jmp *$1
refused	call *%cs
e8fcffffff 1:2:sym+0	call sym
e8fcffffff 1:4:sym+0	call sym@PLT
e9fcffffff 1:2:sym+0	jmp sym
e9fcffffff 1:4:sym+0	jmp sym@PLT
e900000000 1:2:sym+0	jmp sym+4
e8f8ffffff 1:2:sym+0	call sym-4
0f84fcffffff 2:2:sym+0	je sym
0f85fcffffff 2:4:sym+0	jne sym@PLT
e8fc0f0000 1:2:+0	call 0x1000
e9fc0f0000 1:2:+0	jmp 0x1000
e9fcffffff 1:2:+0	jmp 0
0f84fc0f0000 2:2:+0	je 0x1000
66e8feff 2:21:sym+0	callw sym
refused	jmpw sym
e8fcffffff 1:2:sym+0	calll sym
refused	jmpl sym
b804000000 1:1:sym+0	movl $sym+4,%eax
b8fcffffff 1:1:sym+0	movl $sym-4,%eax
b800000000 1:3:sym+0	movl $sym@GOT,%eax
b800000000 1:9:sym+0	movl $sym@GOTOFF,%eax
b800000000 1:17:sym+0	movl $sym@NTPOFF,%eax
b800000000 1:34:sym+0	movl $sym@TPOFF,%eax
b800000000 1:32:sym+0	movl $sym@DTPOFF,%eax
b800000000 1:4:sym+0	movl $sym@PLT,%eax
refused	movl $sym@INDNTPOFF,%eax
bb01000000 1:10:_GLOBAL_OFFSET_TABLE_+0	movl $_GLOBAL_OFFSET_TABLE_,%ebx
81c302000000 2:10:_GLOBAL_OFFSET_TABLE_+0	addl $_GLOBAL_OFFSET_TABLE_,%ebx
refused	addl $_GLOBAL_OFFSET_TABLE_+[.-1b],%ebx
81c302000000 2:10:_GLOBAL_OFFSET_TABLE_+0	1: | addl $_GLOBAL_OFFSET_TABLE_+[.-1b],%ebx
e8000000005b81c303000000 8:10:_GLOBAL_OFFSET_TABLE_+0	call 1f | 1: popl %ebx | addl $_GLOBAL_OFFSET_TABLE_+[.-1b],%ebx
0501000000 1:10:_GLOBAL_OFFSET_TABLE_+0	addl $_GLOBAL_OFFSET_TABLE_,%eax
8d8302000000 2:10:_GLOBAL_OFFSET_TABLE_+0	leal _GLOBAL_OFFSET_TABLE_(%ebx),%eax
8b8300000000 2:43:sym+0	movl sym@GOT(%ebx),%eax
8d8300000000 2:9:sym+0	leal sym@GOTOFF(%ebx),%eax
8d8304000000 2:9:sym+0	leal sym@GOTOFF+4(%ebx),%eax
8b848b00000000 3:9:sym+0	movl sym@GOTOFF(%ebx,%ecx,4),%eax
ff9300000000 2:43:sym+0	call *sym@GOT(%ebx)
8d041d00000000 3:18:sym+0	leal sym@TLSGD(,%ebx,1),%eax
8d8300000000 2:19:sym+0	leal sym@TLSLDM(%ebx),%eax
8b8300000000 2:16:sym+0	movl sym@GOTNTPOFF(%ebx),%eax
a100000000 1:15:sym+0	movl sym@INDNTPOFF,%eax
030500000000 2:15:sym+0	addl sym@INDNTPOFF,%eax
65a100000000 2:17:sym+0	movl %gs:sym@NTPOFF,%eax
8b8300000000 2:33:sym+0	movl sym@GOTTPOFF(%ebx),%eax
2d00000000 1:34:sym+0	subl $sym@TPOFF,%eax
8d8000000000 2:17:sym+0	leal sym@NTPOFF(%eax),%eax
6800000000 1:1:sym+0	pushl $sym
6800000000 1:1:sym+0	push $sym
6800000000 1:9:sym+0	pushl $sym@GOTOFF
0500000000 1:1:sym+0	addl $sym,%eax
81c300000000 2:1:sym+0	addl $sym,%ebx
81f900000000 2:1:sym+0	cmpl $sym,%ecx
813d0000000000000000 2:1:sym2+0 6:1:sym+0	cmpl $sym,sym2
c7050000000000000000 2:1:sym2+0 6:1:sym+0	movl $sym,sym2
c744240800000000 4:1:sym+0	movl $sym,8(%esp)
6681c30000 3:20:sym+0	addw $sym,%bx
80c300 2:22:sym+0	addb $sym,%bl
66680000 2:20:sym+0	pushw $sym
69d800000000 2:1:sym+0	imull $sym,%eax,%ebx
a900000000 1:1:sym+0	testl $sym,%eax
f7c300000000 2:1:sym+0	testl $sym,%ebx
812000000000 2:1:sym+0	andl $sym,(%eax)
refused	movl $sym2-sym,%eax
a104000000 1:1:.data+0	.data | .long 0 | lv: .long 0 | .text | movl lv,%eax
b804000000 1:1:.data+0	.data | .long 0 | lv: .long 0 | .text | movl $lv,%eax
c74424040c000000 4:1:.data+0	.data | .long 0 | lv: .long 0 | .text | movl $lv+8,4(%esp)
e800000000 1:2:.data+0	.data | .long 0 | lv: .long 0 | .text | call lv
8d8304000000 2:9:.data+0	.data | .long 0 | lv: .long 0 | .text | leal lv@GOTOFF(%ebx),%eax
a100000000 1:1:gv+0	.globl gv | .data | .long 0 | gv: .long 0 | .text | movl gv,%eax
e8fcffffff 1:2:wk+0	.weak wk | call wk
90e8faffffff	lf: nop | call lf
90e8fcffffff 2:2:gf+0	.globl gf | gf: nop | call gf
90ebfd	.globl gf | gf: nop | jmp gf
9074fd	.globl gf | gf: nop | je gf
90e8fcffffff 2:4:gf+0	.globl gf | gf: nop | call gf@PLT
90b800000000 2:1:gf+0	.globl gf | gf: nop | movl $gf,%eax
9090b801000000 3:1:.text+0	nop | lf: nop | movl $lf,%eax
90906801000000 3:1:.text+0	nop | lf: nop | pushl $lf
9090ff2501000000 4:1:.text+0	nop | lf: nop | jmp *lf
90908b0c8501000000 5:1:.text+0	nop | lf: nop | movl lf(,%eax,4),%ecx
9090b801000000 3:1:.text+0	nop | 1: nop | movl $1b,%eax
b805000000 1:1:.text+0	movl $1f,%eax | 1:
b805000000	movl $1f-.,%eax | 1:
e8fcffffff 1:2:.foo+0	.section .foo,"ax" | lf: nop | .text | call lf
e9fcffffff 1:2:.foo+0	.section .foo,"ax" | lf: nop | .text | jmp lf
0f84fcffffff 2:2:.foo+0	.section .foo,"ax" | lf: nop | .text | je lf
b800000000 1:1:.foo+0	.section .foo,"ax" | lf: nop | .text | movl $lf,%eax
668cc0	movw %es,%ax
8cc0	movl %es,%eax
8cc0	mov %es,%eax
668cc0	mov %es,%ax
8ec0	movw %ax,%es
8ec0	movl %eax,%es
8ec0	mov %eax,%es
8ec0	mov %ax,%es
8c00	movw %es,(%eax)
8c03	mov %es,(%ebx)
8e00	movw (%eax),%es
8e0500000000 2:1:sym+0	mov sym,%es
8c0500000000 2:1:sym+0	mov %es,sym
refused	movl %es,(%eax)
refused	movl (%eax),%es
668cc3	mov %es,%bx
668cc1	movw %es,%cx
8ec1	mov %cx,%es
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
8c08	movw %cs,(%eax)
8c0b	mov %cs,(%ebx)
8e08	movw (%eax),%cs
8e0d00000000 2:1:sym+0	mov sym,%cs
8c0d00000000 2:1:sym+0	mov %cs,sym
refused	movl %cs,(%eax)
refused	movl (%eax),%cs
668ccb	mov %cs,%bx
668cc9	movw %cs,%cx
8ec9	mov %cx,%cs
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
8c10	movw %ss,(%eax)
8c13	mov %ss,(%ebx)
8e10	movw (%eax),%ss
8e1500000000 2:1:sym+0	mov sym,%ss
8c1500000000 2:1:sym+0	mov %ss,sym
refused	movl %ss,(%eax)
refused	movl (%eax),%ss
668cd3	mov %ss,%bx
668cd1	movw %ss,%cx
8ed1	mov %cx,%ss
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
8c18	movw %ds,(%eax)
8c1b	mov %ds,(%ebx)
8e18	movw (%eax),%ds
8e1d00000000 2:1:sym+0	mov sym,%ds
8c1d00000000 2:1:sym+0	mov %ds,sym
refused	movl %ds,(%eax)
refused	movl (%eax),%ds
668cdb	mov %ds,%bx
668cd9	movw %ds,%cx
8ed9	mov %cx,%ds
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
8c20	movw %fs,(%eax)
8c23	mov %fs,(%ebx)
8e20	movw (%eax),%fs
8e2500000000 2:1:sym+0	mov sym,%fs
8c2500000000 2:1:sym+0	mov %fs,sym
refused	movl %fs,(%eax)
refused	movl (%eax),%fs
668ce3	mov %fs,%bx
668ce1	movw %fs,%cx
8ee1	mov %cx,%fs
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
8c28	movw %gs,(%eax)
8c2b	mov %gs,(%ebx)
8e28	movw (%eax),%gs
8e2d00000000 2:1:sym+0	mov sym,%gs
8c2d00000000 2:1:sym+0	mov %gs,sym
refused	movl %gs,(%eax)
refused	movl (%eax),%gs
668ceb	mov %gs,%bx
668ce9	movw %gs,%cx
8ee9	mov %cx,%gs
refused	movb %al,%gs
refused	mov %gs,%al
refused	mov %gs,%ds
refused	mov $1,%gs
0f20c0	mov %cr0,%eax
0f22c0	mov %eax,%cr0
0f20c3	movl %cr0,%ebx
0f22c7	movl %edi,%cr0
refused	mov %cr0,%ax
refused	mov (%eax),%cr0
0f20d0	mov %cr2,%eax
0f22d0	mov %eax,%cr2
0f20d3	movl %cr2,%ebx
0f22d7	movl %edi,%cr2
refused	mov %cr2,%ax
refused	mov (%eax),%cr2
0f20d8	mov %cr3,%eax
0f22d8	mov %eax,%cr3
0f20db	movl %cr3,%ebx
0f22df	movl %edi,%cr3
refused	mov %cr3,%ax
refused	mov (%eax),%cr3
0f20e0	mov %cr4,%eax
0f22e0	mov %eax,%cr4
0f20e3	movl %cr4,%ebx
0f22e7	movl %edi,%cr4
refused	mov %cr4,%ax
refused	mov (%eax),%cr4
f00f20c0	mov %cr8,%eax
f00f22c0	mov %eax,%cr8
f00f20c3	movl %cr8,%ebx
f00f22c7	movl %edi,%cr8
refused	mov %cr8,%ax
refused	mov (%eax),%cr8
0f20c8	mov %cr1,%eax
0f22c8	mov %eax,%cr1
0f20cb	movl %cr1,%ebx
0f22cf	movl %edi,%cr1
refused	mov %cr1,%ax
refused	mov (%eax),%cr1
0f20e8	mov %cr5,%eax
0f22e8	mov %eax,%cr5
0f20eb	movl %cr5,%ebx
0f22ef	movl %edi,%cr5
refused	mov %cr5,%ax
refused	mov (%eax),%cr5
0f21c0	mov %dr0,%eax
0f23c0	mov %eax,%dr0
0f21c3	movl %dr0,%ebx
0f23c7	movl %edi,%dr0
refused	mov %dr0,%ax
refused	mov (%eax),%dr0
0f21c8	mov %dr1,%eax
0f23c8	mov %eax,%dr1
0f21cb	movl %dr1,%ebx
0f23cf	movl %edi,%dr1
refused	mov %dr1,%ax
refused	mov (%eax),%dr1
0f21d0	mov %dr2,%eax
0f23d0	mov %eax,%dr2
0f21d3	movl %dr2,%ebx
0f23d7	movl %edi,%dr2
refused	mov %dr2,%ax
refused	mov (%eax),%dr2
0f21d8	mov %dr3,%eax
0f23d8	mov %eax,%dr3
0f21db	movl %dr3,%ebx
0f23df	movl %edi,%dr3
refused	mov %dr3,%ax
refused	mov (%eax),%dr3
0f21f0	mov %dr6,%eax
0f23f0	mov %eax,%dr6
0f21f3	movl %dr6,%ebx
0f23f7	movl %edi,%dr6
refused	mov %dr6,%ax
refused	mov (%eax),%dr6
0f21f8	mov %dr7,%eax
0f23f8	mov %eax,%dr7
0f21fb	movl %dr7,%ebx
0f23ff	movl %edi,%dr7
refused	mov %dr7,%ax
refused	mov (%eax),%dr7
0f21c0	mov %db0,%eax
0f23c0	mov %eax,%db0
0f21c3	movl %db0,%ebx
0f23c7	movl %edi,%db0
refused	mov %db0,%ax
refused	mov (%eax),%db0
0f21f8	mov %db7,%eax
0f23f8	mov %eax,%db7
0f21fb	movl %db7,%ebx
0f23ff	movl %edi,%db7
refused	mov %db7,%ax
refused	mov (%eax),%db7
0f21e0	mov %dr4,%eax
0f23e0	mov %eax,%dr4
0f21e3	movl %dr4,%ebx
0f23e7	movl %edi,%dr4
refused	mov %dr4,%ax
refused	mov (%eax),%dr4
0f24f0	mov %tr6,%eax
0f26f0	mov %eax,%tr6
0f24f3	movl %tr6,%ebx
0f26f7	movl %edi,%tr6
refused	mov %tr6,%ax
refused	mov (%eax),%tr6
0f24f8	mov %tr7,%eax
0f26f8	mov %eax,%tr7
0f24fb	movl %tr7,%ebx
0f26ff	movl %edi,%tr7
refused	mov %tr7,%ax
refused	mov (%eax),%tr7
f00f20c8	mov %cr9,%eax
f00f22c8	mov %eax,%cr9
f00f20cb	movl %cr9,%ebx
f00f22cf	movl %edi,%cr9
refused	mov %cr9,%ax
refused	mov (%eax),%cr9
a134120000	movl 0x1234,%eax
64a000000000 2:1:sym+0	movb %fs:sym,%al
65a310000000	movl %eax,%gs:0x10
65a100000000	movl %gs:0,%eax
64a100000000	movl %fs:0,%eax
a000000000 1:1:sym+0	mov sym,%al
a100000000 1:1:sym+0	mov sym,%eax
66a300000000 2:1:sym+0	mov %ax,sym
8b1d00000000 2:1:sym+0	movl sym,%ebx
660fbed8	movsbw %al,%bx
0fbed8	movsbl %al,%ebx
0fbfd8	movswl %ax,%ebx
660fbe18	movsbw (%eax),%bx
0fbe1d00000000 3:1:sym+0	movsbl sym,%ebx
0fbf5d08	movswl 8(%ebp),%ebx
660fbed8	movsx %al,%bx
0fbed8	movsx %al,%ebx
0fbfd8	movsx %ax,%ebx
0fbed8	movsxb %al,%ebx
0fbfd8	movsxw %ax,%ebx
0fbed8	movsb %al,%ebx
0fbfd8	movsw %ax,%ebx
0fbe18	movsx (%eax),%ebx
refused	movsbl %ax,%ebx
refused	movswl %al,%ebx
refused	movsbl %al,%bx
refused	movsbw %al,%ebx
refused	movswl %ax,%bx
0fbede	movsbl %dh,%ebx
refused	movsbl %eax,%ebx
660fbe18	movsxb (%eax),%bx
0fbf18	movsxw (%eax),%ebx
refused	movsbl %al,(%eax)
refused	movsww %ax,%bx
refused	movsx %eax,%ebx
refused	movsxb %al,%bl
660fb6d8	movzbw %al,%bx
0fb6d8	movzbl %al,%ebx
0fb7d8	movzwl %ax,%ebx
660fb618	movzbw (%eax),%bx
0fb61d00000000 3:1:sym+0	movzbl sym,%ebx
0fb75d08	movzwl 8(%ebp),%ebx
660fb6d8	movzx %al,%bx
0fb6d8	movzx %al,%ebx
0fb7d8	movzx %ax,%ebx
0fb6d8	movzxb %al,%ebx
0fb7d8	movzxw %ax,%ebx
0fb6d8	movzb %al,%ebx
0fb7d8	movzw %ax,%ebx
0fb618	movzx (%eax),%ebx
refused	movzbl %ax,%ebx
refused	movzwl %al,%ebx
refused	movzbl %al,%bx
refused	movzbw %al,%ebx
refused	movzwl %ax,%bx
0fb6de	movzbl %dh,%ebx
refused	movzbl %eax,%ebx
660fb618	movzxb (%eax),%bx
0fb718	movzxw (%eax),%ebx
refused	movzbl %al,(%eax)
refused	movzww %ax,%bx
refused	movzx %eax,%ebx
refused	movzxb %al,%bl
00c0	addb %al,%al
0003	addb %al,(%ebx)
0203	addb (%ebx),%al
000500000000 2:1:sym+0	addb %al,sym
020500000000 2:1:sym+0	addb sym,%al
0003	add %al,(%ebx)
refused	add 4((%ebx,%esi,2),%al
0400	addb $0,%al
0401	addb $1,%al
04ff	addb $-1,%al
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
04ff	addb $0x7fffffff,%al
0400	addb $0x80000000,%al
0480	addb $0xffffff80,%al
04ff	addb $0xffffffff,%al
0400 1:22:sym+0	addb $sym,%al
00c8	addb %cl,%al
00c1	addb %al,%cl
000b	addb %cl,(%ebx)
020b	addb (%ebx),%cl
000d00000000 2:1:sym+0	addb %cl,sym
020d00000000 2:1:sym+0	addb sym,%cl
000b	add %cl,(%ebx)
refused	add 4((%ebx,%esi,2),%cl
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
80c100 2:22:sym+0	addb $sym,%cl
00f8	addb %bh,%al
00c7	addb %al,%bh
003b	addb %bh,(%ebx)
023b	addb (%ebx),%bh
003d00000000 2:1:sym+0	addb %bh,sym
023d00000000 2:1:sym+0	addb sym,%bh
003b	add %bh,(%ebx)
refused	add 4((%ebx,%esi,2),%bh
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
80c700 2:22:sym+0	addb $sym,%bh
800300	addb $0,(%ebx)
80050000000000 2:1:sym+0	addb $0,sym
80448dfc00	addb $0,-4(%ebp,%ecx,4)
800301	addb $1,(%ebx)
80050000000001 2:1:sym+0	addb $1,sym
80448dfc01	addb $1,-4(%ebp,%ecx,4)
8003ff	addb $-1,(%ebx)
800500000000ff 2:1:sym+0	addb $-1,sym
80448dfcff	addb $-1,-4(%ebp,%ecx,4)
80037f	addb $127,(%ebx)
8005000000007f 2:1:sym+0	addb $127,sym
80448dfc7f	addb $127,-4(%ebp,%ecx,4)
800380	addb $128,(%ebx)
80050000000080 2:1:sym+0	addb $128,sym
80448dfc80	addb $128,-4(%ebp,%ecx,4)
800380	addb $-128,(%ebx)
80050000000080 2:1:sym+0	addb $-128,sym
80448dfc80	addb $-128,-4(%ebp,%ecx,4)
80037f	addb $-129,(%ebx)
8005000000007f 2:1:sym+0	addb $-129,sym
80448dfc7f	addb $-129,-4(%ebp,%ecx,4)
8003ff	addb $255,(%ebx)
800500000000ff 2:1:sym+0	addb $255,sym
80448dfcff	addb $255,-4(%ebp,%ecx,4)
800300	addb $256,(%ebx)
80050000000000 2:1:sym+0	addb $256,sym
80448dfc00	addb $256,-4(%ebp,%ecx,4)
8003ff	addb $0x7fff,(%ebx)
800500000000ff 2:1:sym+0	addb $0x7fff,sym
80448dfcff	addb $0x7fff,-4(%ebp,%ecx,4)
800300	addb $0x8000,(%ebx)
80050000000000 2:1:sym+0	addb $0x8000,sym
80448dfc00	addb $0x8000,-4(%ebp,%ecx,4)
800380	addb $0xff80,(%ebx)
80050000000080 2:1:sym+0	addb $0xff80,sym
80448dfc80	addb $0xff80,-4(%ebp,%ecx,4)
8003ff	addb $0xffff,(%ebx)
800500000000ff 2:1:sym+0	addb $0xffff,sym
80448dfcff	addb $0xffff,-4(%ebp,%ecx,4)
800300	addb $0x10000,(%ebx)
80050000000000 2:1:sym+0	addb $0x10000,sym
80448dfc00	addb $0x10000,-4(%ebp,%ecx,4)
8003ff	addb $0x7fffffff,(%ebx)
800500000000ff 2:1:sym+0	addb $0x7fffffff,sym
80448dfcff	addb $0x7fffffff,-4(%ebp,%ecx,4)
800300	addb $0x80000000,(%ebx)
80050000000000 2:1:sym+0	addb $0x80000000,sym
80448dfc00	addb $0x80000000,-4(%ebp,%ecx,4)
800380	addb $0xffffff80,(%ebx)
80050000000080 2:1:sym+0	addb $0xffffff80,sym
80448dfc80	addb $0xffffff80,-4(%ebp,%ecx,4)
8003ff	addb $0xffffffff,(%ebx)
800500000000ff 2:1:sym+0	addb $0xffffffff,sym
80448dfcff	addb $0xffffffff,-4(%ebp,%ecx,4)
800300 2:22:sym+0	addb $sym,(%ebx)
80050000000000 2:1:sym+0 6:22:sym+0	addb $sym,sym
80448dfc00 4:22:sym+0	addb $sym,-4(%ebp,%ecx,4)
80c101	add $1,%cl
830301	add $1,(%ebx)
refused	addb $1,$2
refused	addb %al,$2
refused	addb (%ebx),(%ebx)
f0000b	lock addb %cl,(%ebx)
f0800301	lock addb $1,(%ebx)
refused	lock addb (%ebx),%cl
refused	lock addb %cl,%al
6601c0	addw %ax,%ax
660103	addw %ax,(%ebx)
660303	addw (%ebx),%ax
66010500000000 3:1:sym+0	addw %ax,sym
66030500000000 3:1:sym+0	addw sym,%ax
660103	add %ax,(%ebx)
refused	add 4((%ebx,%esi,2),%ax
6683c000	addw $0,%ax
6683c001	addw $1,%ax
6683c0ff	addw $-1,%ax
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
6605ffff	addw $0x7fffffff,%ax
66050000	addw $0x80000000,%ax
6683c080	addw $0xffffff80,%ax
6683c0ff	addw $0xffffffff,%ax
66050000 2:20:sym+0	addw $sym,%ax
6601d8	addw %bx,%ax
6601c3	addw %ax,%bx
66011b	addw %bx,(%ebx)
66031b	addw (%ebx),%bx
66011d00000000 3:1:sym+0	addw %bx,sym
66031d00000000 3:1:sym+0	addw sym,%bx
66011b	add %bx,(%ebx)
refused	add 4((%ebx,%esi,2),%bx
6683c300	addw $0,%bx
6683c301	addw $1,%bx
6683c3ff	addw $-1,%bx
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
6681c3ffff	addw $0x7fffffff,%bx
6681c30000	addw $0x80000000,%bx
6683c380	addw $0xffffff80,%bx
6683c3ff	addw $0xffffffff,%bx
6601f0	addw %si,%ax
6601c6	addw %ax,%si
660133	addw %si,(%ebx)
660333	addw (%ebx),%si
66013500000000 3:1:sym+0	addw %si,sym
66033500000000 3:1:sym+0	addw sym,%si
660133	add %si,(%ebx)
refused	add 4((%ebx,%esi,2),%si
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
6681c60000 3:20:sym+0	addw $sym,%si
66830300	addw $0,(%ebx)
6683050000000000 3:1:sym+0	addw $0,sym
6683448dfc00	addw $0,-4(%ebp,%ecx,4)
66830301	addw $1,(%ebx)
6683050000000001 3:1:sym+0	addw $1,sym
6683448dfc01	addw $1,-4(%ebp,%ecx,4)
668303ff	addw $-1,(%ebx)
66830500000000ff 3:1:sym+0	addw $-1,sym
6683448dfcff	addw $-1,-4(%ebp,%ecx,4)
6683037f	addw $127,(%ebx)
668305000000007f 3:1:sym+0	addw $127,sym
6683448dfc7f	addw $127,-4(%ebp,%ecx,4)
6681038000	addw $128,(%ebx)
668105000000008000 3:1:sym+0	addw $128,sym
6681448dfc8000	addw $128,-4(%ebp,%ecx,4)
66830380	addw $-128,(%ebx)
6683050000000080 3:1:sym+0	addw $-128,sym
6683448dfc80	addw $-128,-4(%ebp,%ecx,4)
6681037fff	addw $-129,(%ebx)
668105000000007fff 3:1:sym+0	addw $-129,sym
6681448dfc7fff	addw $-129,-4(%ebp,%ecx,4)
668103ff00	addw $255,(%ebx)
66810500000000ff00 3:1:sym+0	addw $255,sym
6681448dfcff00	addw $255,-4(%ebp,%ecx,4)
6681030001	addw $256,(%ebx)
668105000000000001 3:1:sym+0	addw $256,sym
6681448dfc0001	addw $256,-4(%ebp,%ecx,4)
668103ff7f	addw $0x7fff,(%ebx)
66810500000000ff7f 3:1:sym+0	addw $0x7fff,sym
6681448dfcff7f	addw $0x7fff,-4(%ebp,%ecx,4)
6681030080	addw $0x8000,(%ebx)
668105000000000080 3:1:sym+0	addw $0x8000,sym
6681448dfc0080	addw $0x8000,-4(%ebp,%ecx,4)
66830380	addw $0xff80,(%ebx)
6683050000000080 3:1:sym+0	addw $0xff80,sym
6683448dfc80	addw $0xff80,-4(%ebp,%ecx,4)
668303ff	addw $0xffff,(%ebx)
66830500000000ff 3:1:sym+0	addw $0xffff,sym
6683448dfcff	addw $0xffff,-4(%ebp,%ecx,4)
6681030000	addw $0x10000,(%ebx)
668105000000000000 3:1:sym+0	addw $0x10000,sym
6681448dfc0000	addw $0x10000,-4(%ebp,%ecx,4)
668103ffff	addw $0x7fffffff,(%ebx)
66810500000000ffff 3:1:sym+0	addw $0x7fffffff,sym
6681448dfcffff	addw $0x7fffffff,-4(%ebp,%ecx,4)
6681030000	addw $0x80000000,(%ebx)
668105000000000000 3:1:sym+0	addw $0x80000000,sym
6681448dfc0000	addw $0x80000000,-4(%ebp,%ecx,4)
66830380	addw $0xffffff80,(%ebx)
6683050000000080 3:1:sym+0	addw $0xffffff80,sym
6683448dfc80	addw $0xffffff80,-4(%ebp,%ecx,4)
668303ff	addw $0xffffffff,(%ebx)
66830500000000ff 3:1:sym+0	addw $0xffffffff,sym
6683448dfcff	addw $0xffffffff,-4(%ebp,%ecx,4)
6681030000 3:20:sym+0	addw $sym,(%ebx)
668105000000000000 3:1:sym+0 7:20:sym+0	addw $sym,sym
6681448dfc0000 5:20:sym+0	addw $sym,-4(%ebp,%ecx,4)
6683c301	add $1,%bx
refused	addw $1,$2
refused	addw %ax,$2
refused	addw (%ebx),(%ebx)
66f0011b	lock addw %bx,(%ebx)
66f0830301	lock addw $1,(%ebx)
refused	lock addw (%ebx),%bx
refused	lock addw %bx,%ax
01c0	addl %eax,%eax
0103	addl %eax,(%ebx)
0303	addl (%ebx),%eax
010500000000 2:1:sym+0	addl %eax,sym
030500000000 2:1:sym+0	addl sym,%eax
0103	add %eax,(%ebx)
refused	add 4((%ebx,%esi,2),%eax
83c000	addl $0,%eax
83c001	addl $1,%eax
83c0ff	addl $-1,%eax
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
05ffffff7f	addl $0x7fffffff,%eax
0500000080	addl $0x80000000,%eax
83c080	addl $0xffffff80,%eax
83c0ff	addl $0xffffffff,%eax
01d8	addl %ebx,%eax
01c3	addl %eax,%ebx
011b	addl %ebx,(%ebx)
031b	addl (%ebx),%ebx
011d00000000 2:1:sym+0	addl %ebx,sym
031d00000000 2:1:sym+0	addl sym,%ebx
011b	add %ebx,(%ebx)
refused	add 4((%ebx,%esi,2),%ebx
83c300	addl $0,%ebx
83c301	addl $1,%ebx
83c3ff	addl $-1,%ebx
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
81c3ffffff7f	addl $0x7fffffff,%ebx
81c300000080	addl $0x80000000,%ebx
83c380	addl $0xffffff80,%ebx
83c3ff	addl $0xffffffff,%ebx
01e0	addl %esp,%eax
01c4	addl %eax,%esp
0123	addl %esp,(%ebx)
0323	addl (%ebx),%esp
012500000000 2:1:sym+0	addl %esp,sym
032500000000 2:1:sym+0	addl sym,%esp
0123	add %esp,(%ebx)
refused	add 4((%ebx,%esi,2),%esp
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
81c400000000 2:1:sym+0	addl $sym,%esp
01e8	addl %ebp,%eax
01c5	addl %eax,%ebp
012b	addl %ebp,(%ebx)
032b	addl (%ebx),%ebp
012d00000000 2:1:sym+0	addl %ebp,sym
032d00000000 2:1:sym+0	addl sym,%ebp
012b	add %ebp,(%ebx)
refused	add 4((%ebx,%esi,2),%ebp
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
81c500000000 2:1:sym+0	addl $sym,%ebp
830300	addl $0,(%ebx)
83050000000000 2:1:sym+0	addl $0,sym
83448dfc00	addl $0,-4(%ebp,%ecx,4)
830301	addl $1,(%ebx)
83050000000001 2:1:sym+0	addl $1,sym
83448dfc01	addl $1,-4(%ebp,%ecx,4)
8303ff	addl $-1,(%ebx)
830500000000ff 2:1:sym+0	addl $-1,sym
83448dfcff	addl $-1,-4(%ebp,%ecx,4)
83037f	addl $127,(%ebx)
8305000000007f 2:1:sym+0	addl $127,sym
83448dfc7f	addl $127,-4(%ebp,%ecx,4)
810380000000	addl $128,(%ebx)
81050000000080000000 2:1:sym+0	addl $128,sym
81448dfc80000000	addl $128,-4(%ebp,%ecx,4)
830380	addl $-128,(%ebx)
83050000000080 2:1:sym+0	addl $-128,sym
83448dfc80	addl $-128,-4(%ebp,%ecx,4)
81037fffffff	addl $-129,(%ebx)
8105000000007fffffff 2:1:sym+0	addl $-129,sym
81448dfc7fffffff	addl $-129,-4(%ebp,%ecx,4)
8103ff000000	addl $255,(%ebx)
810500000000ff000000 2:1:sym+0	addl $255,sym
81448dfcff000000	addl $255,-4(%ebp,%ecx,4)
810300010000	addl $256,(%ebx)
81050000000000010000 2:1:sym+0	addl $256,sym
81448dfc00010000	addl $256,-4(%ebp,%ecx,4)
8103ff7f0000	addl $0x7fff,(%ebx)
810500000000ff7f0000 2:1:sym+0	addl $0x7fff,sym
81448dfcff7f0000	addl $0x7fff,-4(%ebp,%ecx,4)
810300800000	addl $0x8000,(%ebx)
81050000000000800000 2:1:sym+0	addl $0x8000,sym
81448dfc00800000	addl $0x8000,-4(%ebp,%ecx,4)
810380ff0000	addl $0xff80,(%ebx)
81050000000080ff0000 2:1:sym+0	addl $0xff80,sym
81448dfc80ff0000	addl $0xff80,-4(%ebp,%ecx,4)
8103ffff0000	addl $0xffff,(%ebx)
810500000000ffff0000 2:1:sym+0	addl $0xffff,sym
81448dfcffff0000	addl $0xffff,-4(%ebp,%ecx,4)
810300000100	addl $0x10000,(%ebx)
81050000000000000100 2:1:sym+0	addl $0x10000,sym
81448dfc00000100	addl $0x10000,-4(%ebp,%ecx,4)
8103ffffff7f	addl $0x7fffffff,(%ebx)
810500000000ffffff7f 2:1:sym+0	addl $0x7fffffff,sym
81448dfcffffff7f	addl $0x7fffffff,-4(%ebp,%ecx,4)
810300000080	addl $0x80000000,(%ebx)
81050000000000000080 2:1:sym+0	addl $0x80000000,sym
81448dfc00000080	addl $0x80000000,-4(%ebp,%ecx,4)
830380	addl $0xffffff80,(%ebx)
83050000000080 2:1:sym+0	addl $0xffffff80,sym
83448dfc80	addl $0xffffff80,-4(%ebp,%ecx,4)
8303ff	addl $0xffffffff,(%ebx)
830500000000ff 2:1:sym+0	addl $0xffffffff,sym
83448dfcff	addl $0xffffffff,-4(%ebp,%ecx,4)
810300000000 2:1:sym+0	addl $sym,(%ebx)
81050000000000000000 2:1:sym+0 6:1:sym+0	addl $sym,sym
81448dfc00000000 4:1:sym+0	addl $sym,-4(%ebp,%ecx,4)
83c301	add $1,%ebx
refused	addl $1,$2
refused	addl %eax,$2
refused	addl (%ebx),(%ebx)
f0011b	lock addl %ebx,(%ebx)
f0830301	lock addl $1,(%ebx)
refused	lock addl (%ebx),%ebx
refused	lock addl %ebx,%eax
08c0	orb %al,%al
0803	orb %al,(%ebx)
0a03	orb (%ebx),%al
080500000000 2:1:sym+0	orb %al,sym
0a0500000000 2:1:sym+0	orb sym,%al
0803	or %al,(%ebx)
refused	or 4((%ebx,%esi,2),%al
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
0c00 1:22:sym+0	orb $sym,%al
08c8	orb %cl,%al
08c1	orb %al,%cl
080b	orb %cl,(%ebx)
0a0b	orb (%ebx),%cl
080d00000000 2:1:sym+0	orb %cl,sym
0a0d00000000 2:1:sym+0	orb sym,%cl
080b	or %cl,(%ebx)
refused	or 4((%ebx,%esi,2),%cl
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
80c900 2:22:sym+0	orb $sym,%cl
08f8	orb %bh,%al
08c7	orb %al,%bh
083b	orb %bh,(%ebx)
0a3b	orb (%ebx),%bh
083d00000000 2:1:sym+0	orb %bh,sym
0a3d00000000 2:1:sym+0	orb sym,%bh
083b	or %bh,(%ebx)
refused	or 4((%ebx,%esi,2),%bh
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
80cf00 2:22:sym+0	orb $sym,%bh
800b00	orb $0,(%ebx)
800d0000000000 2:1:sym+0	orb $0,sym
804c8dfc00	orb $0,-4(%ebp,%ecx,4)
800b01	orb $1,(%ebx)
800d0000000001 2:1:sym+0	orb $1,sym
804c8dfc01	orb $1,-4(%ebp,%ecx,4)
800bff	orb $-1,(%ebx)
800d00000000ff 2:1:sym+0	orb $-1,sym
804c8dfcff	orb $-1,-4(%ebp,%ecx,4)
800b7f	orb $127,(%ebx)
800d000000007f 2:1:sym+0	orb $127,sym
804c8dfc7f	orb $127,-4(%ebp,%ecx,4)
800b80	orb $128,(%ebx)
800d0000000080 2:1:sym+0	orb $128,sym
804c8dfc80	orb $128,-4(%ebp,%ecx,4)
800b80	orb $-128,(%ebx)
800d0000000080 2:1:sym+0	orb $-128,sym
804c8dfc80	orb $-128,-4(%ebp,%ecx,4)
800b7f	orb $-129,(%ebx)
800d000000007f 2:1:sym+0	orb $-129,sym
804c8dfc7f	orb $-129,-4(%ebp,%ecx,4)
800bff	orb $255,(%ebx)
800d00000000ff 2:1:sym+0	orb $255,sym
804c8dfcff	orb $255,-4(%ebp,%ecx,4)
800b00	orb $256,(%ebx)
800d0000000000 2:1:sym+0	orb $256,sym
804c8dfc00	orb $256,-4(%ebp,%ecx,4)
800bff	orb $0x7fff,(%ebx)
800d00000000ff 2:1:sym+0	orb $0x7fff,sym
804c8dfcff	orb $0x7fff,-4(%ebp,%ecx,4)
800b00	orb $0x8000,(%ebx)
800d0000000000 2:1:sym+0	orb $0x8000,sym
804c8dfc00	orb $0x8000,-4(%ebp,%ecx,4)
800b80	orb $0xff80,(%ebx)
800d0000000080 2:1:sym+0	orb $0xff80,sym
804c8dfc80	orb $0xff80,-4(%ebp,%ecx,4)
800bff	orb $0xffff,(%ebx)
800d00000000ff 2:1:sym+0	orb $0xffff,sym
804c8dfcff	orb $0xffff,-4(%ebp,%ecx,4)
800b00	orb $0x10000,(%ebx)
800d0000000000 2:1:sym+0	orb $0x10000,sym
804c8dfc00	orb $0x10000,-4(%ebp,%ecx,4)
800bff	orb $0x7fffffff,(%ebx)
800d00000000ff 2:1:sym+0	orb $0x7fffffff,sym
804c8dfcff	orb $0x7fffffff,-4(%ebp,%ecx,4)
800b00	orb $0x80000000,(%ebx)
800d0000000000 2:1:sym+0	orb $0x80000000,sym
804c8dfc00	orb $0x80000000,-4(%ebp,%ecx,4)
800b80	orb $0xffffff80,(%ebx)
800d0000000080 2:1:sym+0	orb $0xffffff80,sym
804c8dfc80	orb $0xffffff80,-4(%ebp,%ecx,4)
800bff	orb $0xffffffff,(%ebx)
800d00000000ff 2:1:sym+0	orb $0xffffffff,sym
804c8dfcff	orb $0xffffffff,-4(%ebp,%ecx,4)
800b00 2:22:sym+0	orb $sym,(%ebx)
800d0000000000 2:1:sym+0 6:22:sym+0	orb $sym,sym
804c8dfc00 4:22:sym+0	orb $sym,-4(%ebp,%ecx,4)
80c901	or $1,%cl
830b01	or $1,(%ebx)
refused	orb $1,$2
refused	orb %al,$2
refused	orb (%ebx),(%ebx)
f0080b	lock orb %cl,(%ebx)
f0800b01	lock orb $1,(%ebx)
refused	lock orb (%ebx),%cl
refused	lock orb %cl,%al
6609c0	orw %ax,%ax
660903	orw %ax,(%ebx)
660b03	orw (%ebx),%ax
66090500000000 3:1:sym+0	orw %ax,sym
660b0500000000 3:1:sym+0	orw sym,%ax
660903	or %ax,(%ebx)
refused	or 4((%ebx,%esi,2),%ax
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
660d0000 2:20:sym+0	orw $sym,%ax
6609d8	orw %bx,%ax
6609c3	orw %ax,%bx
66091b	orw %bx,(%ebx)
660b1b	orw (%ebx),%bx
66091d00000000 3:1:sym+0	orw %bx,sym
660b1d00000000 3:1:sym+0	orw sym,%bx
66091b	or %bx,(%ebx)
refused	or 4((%ebx,%esi,2),%bx
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
6681cb0000 3:20:sym+0	orw $sym,%bx
6609f0	orw %si,%ax
6609c6	orw %ax,%si
660933	orw %si,(%ebx)
660b33	orw (%ebx),%si
66093500000000 3:1:sym+0	orw %si,sym
660b3500000000 3:1:sym+0	orw sym,%si
660933	or %si,(%ebx)
refused	or 4((%ebx,%esi,2),%si
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
6681ce0000 3:20:sym+0	orw $sym,%si
66830b00	orw $0,(%ebx)
66830d0000000000 3:1:sym+0	orw $0,sym
66834c8dfc00	orw $0,-4(%ebp,%ecx,4)
66830b01	orw $1,(%ebx)
66830d0000000001 3:1:sym+0	orw $1,sym
66834c8dfc01	orw $1,-4(%ebp,%ecx,4)
66830bff	orw $-1,(%ebx)
66830d00000000ff 3:1:sym+0	orw $-1,sym
66834c8dfcff	orw $-1,-4(%ebp,%ecx,4)
66830b7f	orw $127,(%ebx)
66830d000000007f 3:1:sym+0	orw $127,sym
66834c8dfc7f	orw $127,-4(%ebp,%ecx,4)
66810b8000	orw $128,(%ebx)
66810d000000008000 3:1:sym+0	orw $128,sym
66814c8dfc8000	orw $128,-4(%ebp,%ecx,4)
66830b80	orw $-128,(%ebx)
66830d0000000080 3:1:sym+0	orw $-128,sym
66834c8dfc80	orw $-128,-4(%ebp,%ecx,4)
66810b7fff	orw $-129,(%ebx)
66810d000000007fff 3:1:sym+0	orw $-129,sym
66814c8dfc7fff	orw $-129,-4(%ebp,%ecx,4)
66810bff00	orw $255,(%ebx)
66810d00000000ff00 3:1:sym+0	orw $255,sym
66814c8dfcff00	orw $255,-4(%ebp,%ecx,4)
66810b0001	orw $256,(%ebx)
66810d000000000001 3:1:sym+0	orw $256,sym
66814c8dfc0001	orw $256,-4(%ebp,%ecx,4)
66810bff7f	orw $0x7fff,(%ebx)
66810d00000000ff7f 3:1:sym+0	orw $0x7fff,sym
66814c8dfcff7f	orw $0x7fff,-4(%ebp,%ecx,4)
66810b0080	orw $0x8000,(%ebx)
66810d000000000080 3:1:sym+0	orw $0x8000,sym
66814c8dfc0080	orw $0x8000,-4(%ebp,%ecx,4)
66830b80	orw $0xff80,(%ebx)
66830d0000000080 3:1:sym+0	orw $0xff80,sym
66834c8dfc80	orw $0xff80,-4(%ebp,%ecx,4)
66830bff	orw $0xffff,(%ebx)
66830d00000000ff 3:1:sym+0	orw $0xffff,sym
66834c8dfcff	orw $0xffff,-4(%ebp,%ecx,4)
66810b0000	orw $0x10000,(%ebx)
66810d000000000000 3:1:sym+0	orw $0x10000,sym
66814c8dfc0000	orw $0x10000,-4(%ebp,%ecx,4)
66810bffff	orw $0x7fffffff,(%ebx)
66810d00000000ffff 3:1:sym+0	orw $0x7fffffff,sym
66814c8dfcffff	orw $0x7fffffff,-4(%ebp,%ecx,4)
66810b0000	orw $0x80000000,(%ebx)
66810d000000000000 3:1:sym+0	orw $0x80000000,sym
66814c8dfc0000	orw $0x80000000,-4(%ebp,%ecx,4)
66830b80	orw $0xffffff80,(%ebx)
66830d0000000080 3:1:sym+0	orw $0xffffff80,sym
66834c8dfc80	orw $0xffffff80,-4(%ebp,%ecx,4)
66830bff	orw $0xffffffff,(%ebx)
66830d00000000ff 3:1:sym+0	orw $0xffffffff,sym
66834c8dfcff	orw $0xffffffff,-4(%ebp,%ecx,4)
66810b0000 3:20:sym+0	orw $sym,(%ebx)
66810d000000000000 3:1:sym+0 7:20:sym+0	orw $sym,sym
66814c8dfc0000 5:20:sym+0	orw $sym,-4(%ebp,%ecx,4)
6683cb01	or $1,%bx
refused	orw $1,$2
refused	orw %ax,$2
refused	orw (%ebx),(%ebx)
66f0091b	lock orw %bx,(%ebx)
66f0830b01	lock orw $1,(%ebx)
refused	lock orw (%ebx),%bx
refused	lock orw %bx,%ax
09c0	orl %eax,%eax
0903	orl %eax,(%ebx)
0b03	orl (%ebx),%eax
090500000000 2:1:sym+0	orl %eax,sym
0b0500000000 2:1:sym+0	orl sym,%eax
0903	or %eax,(%ebx)
refused	or 4((%ebx,%esi,2),%eax
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
0d00000000 1:1:sym+0	orl $sym,%eax
09d8	orl %ebx,%eax
09c3	orl %eax,%ebx
091b	orl %ebx,(%ebx)
0b1b	orl (%ebx),%ebx
091d00000000 2:1:sym+0	orl %ebx,sym
0b1d00000000 2:1:sym+0	orl sym,%ebx
091b	or %ebx,(%ebx)
refused	or 4((%ebx,%esi,2),%ebx
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
81cb00000000 2:1:sym+0	orl $sym,%ebx
09e0	orl %esp,%eax
09c4	orl %eax,%esp
0923	orl %esp,(%ebx)
0b23	orl (%ebx),%esp
092500000000 2:1:sym+0	orl %esp,sym
0b2500000000 2:1:sym+0	orl sym,%esp
0923	or %esp,(%ebx)
refused	or 4((%ebx,%esi,2),%esp
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
81cc00000000 2:1:sym+0	orl $sym,%esp
09e8	orl %ebp,%eax
09c5	orl %eax,%ebp
092b	orl %ebp,(%ebx)
0b2b	orl (%ebx),%ebp
092d00000000 2:1:sym+0	orl %ebp,sym
0b2d00000000 2:1:sym+0	orl sym,%ebp
092b	or %ebp,(%ebx)
refused	or 4((%ebx,%esi,2),%ebp
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
81cd00000000 2:1:sym+0	orl $sym,%ebp
830b00	orl $0,(%ebx)
830d0000000000 2:1:sym+0	orl $0,sym
834c8dfc00	orl $0,-4(%ebp,%ecx,4)
830b01	orl $1,(%ebx)
830d0000000001 2:1:sym+0	orl $1,sym
834c8dfc01	orl $1,-4(%ebp,%ecx,4)
830bff	orl $-1,(%ebx)
830d00000000ff 2:1:sym+0	orl $-1,sym
834c8dfcff	orl $-1,-4(%ebp,%ecx,4)
830b7f	orl $127,(%ebx)
830d000000007f 2:1:sym+0	orl $127,sym
834c8dfc7f	orl $127,-4(%ebp,%ecx,4)
810b80000000	orl $128,(%ebx)
810d0000000080000000 2:1:sym+0	orl $128,sym
814c8dfc80000000	orl $128,-4(%ebp,%ecx,4)
830b80	orl $-128,(%ebx)
830d0000000080 2:1:sym+0	orl $-128,sym
834c8dfc80	orl $-128,-4(%ebp,%ecx,4)
810b7fffffff	orl $-129,(%ebx)
810d000000007fffffff 2:1:sym+0	orl $-129,sym
814c8dfc7fffffff	orl $-129,-4(%ebp,%ecx,4)
810bff000000	orl $255,(%ebx)
810d00000000ff000000 2:1:sym+0	orl $255,sym
814c8dfcff000000	orl $255,-4(%ebp,%ecx,4)
810b00010000	orl $256,(%ebx)
810d0000000000010000 2:1:sym+0	orl $256,sym
814c8dfc00010000	orl $256,-4(%ebp,%ecx,4)
810bff7f0000	orl $0x7fff,(%ebx)
810d00000000ff7f0000 2:1:sym+0	orl $0x7fff,sym
814c8dfcff7f0000	orl $0x7fff,-4(%ebp,%ecx,4)
810b00800000	orl $0x8000,(%ebx)
810d0000000000800000 2:1:sym+0	orl $0x8000,sym
814c8dfc00800000	orl $0x8000,-4(%ebp,%ecx,4)
810b80ff0000	orl $0xff80,(%ebx)
810d0000000080ff0000 2:1:sym+0	orl $0xff80,sym
814c8dfc80ff0000	orl $0xff80,-4(%ebp,%ecx,4)
810bffff0000	orl $0xffff,(%ebx)
810d00000000ffff0000 2:1:sym+0	orl $0xffff,sym
814c8dfcffff0000	orl $0xffff,-4(%ebp,%ecx,4)
810b00000100	orl $0x10000,(%ebx)
810d0000000000000100 2:1:sym+0	orl $0x10000,sym
814c8dfc00000100	orl $0x10000,-4(%ebp,%ecx,4)
810bffffff7f	orl $0x7fffffff,(%ebx)
810d00000000ffffff7f 2:1:sym+0	orl $0x7fffffff,sym
814c8dfcffffff7f	orl $0x7fffffff,-4(%ebp,%ecx,4)
810b00000080	orl $0x80000000,(%ebx)
810d0000000000000080 2:1:sym+0	orl $0x80000000,sym
814c8dfc00000080	orl $0x80000000,-4(%ebp,%ecx,4)
830b80	orl $0xffffff80,(%ebx)
830d0000000080 2:1:sym+0	orl $0xffffff80,sym
834c8dfc80	orl $0xffffff80,-4(%ebp,%ecx,4)
830bff	orl $0xffffffff,(%ebx)
830d00000000ff 2:1:sym+0	orl $0xffffffff,sym
834c8dfcff	orl $0xffffffff,-4(%ebp,%ecx,4)
810b00000000 2:1:sym+0	orl $sym,(%ebx)
810d0000000000000000 2:1:sym+0 6:1:sym+0	orl $sym,sym
814c8dfc00000000 4:1:sym+0	orl $sym,-4(%ebp,%ecx,4)
83cb01	or $1,%ebx
refused	orl $1,$2
refused	orl %eax,$2
refused	orl (%ebx),(%ebx)
f0091b	lock orl %ebx,(%ebx)
f0830b01	lock orl $1,(%ebx)
refused	lock orl (%ebx),%ebx
refused	lock orl %ebx,%eax
10c0	adcb %al,%al
1003	adcb %al,(%ebx)
1203	adcb (%ebx),%al
100500000000 2:1:sym+0	adcb %al,sym
120500000000 2:1:sym+0	adcb sym,%al
1003	adc %al,(%ebx)
refused	adc 4((%ebx,%esi,2),%al
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
1400 1:22:sym+0	adcb $sym,%al
10c8	adcb %cl,%al
10c1	adcb %al,%cl
100b	adcb %cl,(%ebx)
120b	adcb (%ebx),%cl
100d00000000 2:1:sym+0	adcb %cl,sym
120d00000000 2:1:sym+0	adcb sym,%cl
100b	adc %cl,(%ebx)
refused	adc 4((%ebx,%esi,2),%cl
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
80d100 2:22:sym+0	adcb $sym,%cl
10f8	adcb %bh,%al
10c7	adcb %al,%bh
103b	adcb %bh,(%ebx)
123b	adcb (%ebx),%bh
103d00000000 2:1:sym+0	adcb %bh,sym
123d00000000 2:1:sym+0	adcb sym,%bh
103b	adc %bh,(%ebx)
refused	adc 4((%ebx,%esi,2),%bh
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
80d700 2:22:sym+0	adcb $sym,%bh
801300	adcb $0,(%ebx)
80150000000000 2:1:sym+0	adcb $0,sym
80548dfc00	adcb $0,-4(%ebp,%ecx,4)
801301	adcb $1,(%ebx)
80150000000001 2:1:sym+0	adcb $1,sym
80548dfc01	adcb $1,-4(%ebp,%ecx,4)
8013ff	adcb $-1,(%ebx)
801500000000ff 2:1:sym+0	adcb $-1,sym
80548dfcff	adcb $-1,-4(%ebp,%ecx,4)
80137f	adcb $127,(%ebx)
8015000000007f 2:1:sym+0	adcb $127,sym
80548dfc7f	adcb $127,-4(%ebp,%ecx,4)
801380	adcb $128,(%ebx)
80150000000080 2:1:sym+0	adcb $128,sym
80548dfc80	adcb $128,-4(%ebp,%ecx,4)
801380	adcb $-128,(%ebx)
80150000000080 2:1:sym+0	adcb $-128,sym
80548dfc80	adcb $-128,-4(%ebp,%ecx,4)
80137f	adcb $-129,(%ebx)
8015000000007f 2:1:sym+0	adcb $-129,sym
80548dfc7f	adcb $-129,-4(%ebp,%ecx,4)
8013ff	adcb $255,(%ebx)
801500000000ff 2:1:sym+0	adcb $255,sym
80548dfcff	adcb $255,-4(%ebp,%ecx,4)
801300	adcb $256,(%ebx)
80150000000000 2:1:sym+0	adcb $256,sym
80548dfc00	adcb $256,-4(%ebp,%ecx,4)
8013ff	adcb $0x7fff,(%ebx)
801500000000ff 2:1:sym+0	adcb $0x7fff,sym
80548dfcff	adcb $0x7fff,-4(%ebp,%ecx,4)
801300	adcb $0x8000,(%ebx)
80150000000000 2:1:sym+0	adcb $0x8000,sym
80548dfc00	adcb $0x8000,-4(%ebp,%ecx,4)
801380	adcb $0xff80,(%ebx)
80150000000080 2:1:sym+0	adcb $0xff80,sym
80548dfc80	adcb $0xff80,-4(%ebp,%ecx,4)
8013ff	adcb $0xffff,(%ebx)
801500000000ff 2:1:sym+0	adcb $0xffff,sym
80548dfcff	adcb $0xffff,-4(%ebp,%ecx,4)
801300	adcb $0x10000,(%ebx)
80150000000000 2:1:sym+0	adcb $0x10000,sym
80548dfc00	adcb $0x10000,-4(%ebp,%ecx,4)
8013ff	adcb $0x7fffffff,(%ebx)
801500000000ff 2:1:sym+0	adcb $0x7fffffff,sym
80548dfcff	adcb $0x7fffffff,-4(%ebp,%ecx,4)
801300	adcb $0x80000000,(%ebx)
80150000000000 2:1:sym+0	adcb $0x80000000,sym
80548dfc00	adcb $0x80000000,-4(%ebp,%ecx,4)
801380	adcb $0xffffff80,(%ebx)
80150000000080 2:1:sym+0	adcb $0xffffff80,sym
80548dfc80	adcb $0xffffff80,-4(%ebp,%ecx,4)
8013ff	adcb $0xffffffff,(%ebx)
801500000000ff 2:1:sym+0	adcb $0xffffffff,sym
80548dfcff	adcb $0xffffffff,-4(%ebp,%ecx,4)
801300 2:22:sym+0	adcb $sym,(%ebx)
80150000000000 2:1:sym+0 6:22:sym+0	adcb $sym,sym
80548dfc00 4:22:sym+0	adcb $sym,-4(%ebp,%ecx,4)
80d101	adc $1,%cl
831301	adc $1,(%ebx)
refused	adcb $1,$2
refused	adcb %al,$2
refused	adcb (%ebx),(%ebx)
f0100b	lock adcb %cl,(%ebx)
f0801301	lock adcb $1,(%ebx)
refused	lock adcb (%ebx),%cl
refused	lock adcb %cl,%al
6611c0	adcw %ax,%ax
661103	adcw %ax,(%ebx)
661303	adcw (%ebx),%ax
66110500000000 3:1:sym+0	adcw %ax,sym
66130500000000 3:1:sym+0	adcw sym,%ax
661103	adc %ax,(%ebx)
refused	adc 4((%ebx,%esi,2),%ax
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
66150000 2:20:sym+0	adcw $sym,%ax
6611d8	adcw %bx,%ax
6611c3	adcw %ax,%bx
66111b	adcw %bx,(%ebx)
66131b	adcw (%ebx),%bx
66111d00000000 3:1:sym+0	adcw %bx,sym
66131d00000000 3:1:sym+0	adcw sym,%bx
66111b	adc %bx,(%ebx)
refused	adc 4((%ebx,%esi,2),%bx
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
6681d30000 3:20:sym+0	adcw $sym,%bx
6611f0	adcw %si,%ax
6611c6	adcw %ax,%si
661133	adcw %si,(%ebx)
661333	adcw (%ebx),%si
66113500000000 3:1:sym+0	adcw %si,sym
66133500000000 3:1:sym+0	adcw sym,%si
661133	adc %si,(%ebx)
refused	adc 4((%ebx,%esi,2),%si
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
6681d60000 3:20:sym+0	adcw $sym,%si
66831300	adcw $0,(%ebx)
6683150000000000 3:1:sym+0	adcw $0,sym
6683548dfc00	adcw $0,-4(%ebp,%ecx,4)
66831301	adcw $1,(%ebx)
6683150000000001 3:1:sym+0	adcw $1,sym
6683548dfc01	adcw $1,-4(%ebp,%ecx,4)
668313ff	adcw $-1,(%ebx)
66831500000000ff 3:1:sym+0	adcw $-1,sym
6683548dfcff	adcw $-1,-4(%ebp,%ecx,4)
6683137f	adcw $127,(%ebx)
668315000000007f 3:1:sym+0	adcw $127,sym
6683548dfc7f	adcw $127,-4(%ebp,%ecx,4)
6681138000	adcw $128,(%ebx)
668115000000008000 3:1:sym+0	adcw $128,sym
6681548dfc8000	adcw $128,-4(%ebp,%ecx,4)
66831380	adcw $-128,(%ebx)
6683150000000080 3:1:sym+0	adcw $-128,sym
6683548dfc80	adcw $-128,-4(%ebp,%ecx,4)
6681137fff	adcw $-129,(%ebx)
668115000000007fff 3:1:sym+0	adcw $-129,sym
6681548dfc7fff	adcw $-129,-4(%ebp,%ecx,4)
668113ff00	adcw $255,(%ebx)
66811500000000ff00 3:1:sym+0	adcw $255,sym
6681548dfcff00	adcw $255,-4(%ebp,%ecx,4)
6681130001	adcw $256,(%ebx)
668115000000000001 3:1:sym+0	adcw $256,sym
6681548dfc0001	adcw $256,-4(%ebp,%ecx,4)
668113ff7f	adcw $0x7fff,(%ebx)
66811500000000ff7f 3:1:sym+0	adcw $0x7fff,sym
6681548dfcff7f	adcw $0x7fff,-4(%ebp,%ecx,4)
6681130080	adcw $0x8000,(%ebx)
668115000000000080 3:1:sym+0	adcw $0x8000,sym
6681548dfc0080	adcw $0x8000,-4(%ebp,%ecx,4)
66831380	adcw $0xff80,(%ebx)
6683150000000080 3:1:sym+0	adcw $0xff80,sym
6683548dfc80	adcw $0xff80,-4(%ebp,%ecx,4)
668313ff	adcw $0xffff,(%ebx)
66831500000000ff 3:1:sym+0	adcw $0xffff,sym
6683548dfcff	adcw $0xffff,-4(%ebp,%ecx,4)
6681130000	adcw $0x10000,(%ebx)
668115000000000000 3:1:sym+0	adcw $0x10000,sym
6681548dfc0000	adcw $0x10000,-4(%ebp,%ecx,4)
668113ffff	adcw $0x7fffffff,(%ebx)
66811500000000ffff 3:1:sym+0	adcw $0x7fffffff,sym
6681548dfcffff	adcw $0x7fffffff,-4(%ebp,%ecx,4)
6681130000	adcw $0x80000000,(%ebx)
668115000000000000 3:1:sym+0	adcw $0x80000000,sym
6681548dfc0000	adcw $0x80000000,-4(%ebp,%ecx,4)
66831380	adcw $0xffffff80,(%ebx)
6683150000000080 3:1:sym+0	adcw $0xffffff80,sym
6683548dfc80	adcw $0xffffff80,-4(%ebp,%ecx,4)
668313ff	adcw $0xffffffff,(%ebx)
66831500000000ff 3:1:sym+0	adcw $0xffffffff,sym
6683548dfcff	adcw $0xffffffff,-4(%ebp,%ecx,4)
6681130000 3:20:sym+0	adcw $sym,(%ebx)
668115000000000000 3:1:sym+0 7:20:sym+0	adcw $sym,sym
6681548dfc0000 5:20:sym+0	adcw $sym,-4(%ebp,%ecx,4)
6683d301	adc $1,%bx
refused	adcw $1,$2
refused	adcw %ax,$2
refused	adcw (%ebx),(%ebx)
66f0111b	lock adcw %bx,(%ebx)
66f0831301	lock adcw $1,(%ebx)
refused	lock adcw (%ebx),%bx
refused	lock adcw %bx,%ax
11c0	adcl %eax,%eax
1103	adcl %eax,(%ebx)
1303	adcl (%ebx),%eax
110500000000 2:1:sym+0	adcl %eax,sym
130500000000 2:1:sym+0	adcl sym,%eax
1103	adc %eax,(%ebx)
refused	adc 4((%ebx,%esi,2),%eax
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
1500000000 1:1:sym+0	adcl $sym,%eax
11d8	adcl %ebx,%eax
11c3	adcl %eax,%ebx
111b	adcl %ebx,(%ebx)
131b	adcl (%ebx),%ebx
111d00000000 2:1:sym+0	adcl %ebx,sym
131d00000000 2:1:sym+0	adcl sym,%ebx
111b	adc %ebx,(%ebx)
refused	adc 4((%ebx,%esi,2),%ebx
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
81d300000000 2:1:sym+0	adcl $sym,%ebx
11e0	adcl %esp,%eax
11c4	adcl %eax,%esp
1123	adcl %esp,(%ebx)
1323	adcl (%ebx),%esp
112500000000 2:1:sym+0	adcl %esp,sym
132500000000 2:1:sym+0	adcl sym,%esp
1123	adc %esp,(%ebx)
refused	adc 4((%ebx,%esi,2),%esp
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
81d400000000 2:1:sym+0	adcl $sym,%esp
11e8	adcl %ebp,%eax
11c5	adcl %eax,%ebp
112b	adcl %ebp,(%ebx)
132b	adcl (%ebx),%ebp
112d00000000 2:1:sym+0	adcl %ebp,sym
132d00000000 2:1:sym+0	adcl sym,%ebp
112b	adc %ebp,(%ebx)
refused	adc 4((%ebx,%esi,2),%ebp
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
81d500000000 2:1:sym+0	adcl $sym,%ebp
831300	adcl $0,(%ebx)
83150000000000 2:1:sym+0	adcl $0,sym
83548dfc00	adcl $0,-4(%ebp,%ecx,4)
831301	adcl $1,(%ebx)
83150000000001 2:1:sym+0	adcl $1,sym
83548dfc01	adcl $1,-4(%ebp,%ecx,4)
8313ff	adcl $-1,(%ebx)
831500000000ff 2:1:sym+0	adcl $-1,sym
83548dfcff	adcl $-1,-4(%ebp,%ecx,4)
83137f	adcl $127,(%ebx)
8315000000007f 2:1:sym+0	adcl $127,sym
83548dfc7f	adcl $127,-4(%ebp,%ecx,4)
811380000000	adcl $128,(%ebx)
81150000000080000000 2:1:sym+0	adcl $128,sym
81548dfc80000000	adcl $128,-4(%ebp,%ecx,4)
831380	adcl $-128,(%ebx)
83150000000080 2:1:sym+0	adcl $-128,sym
83548dfc80	adcl $-128,-4(%ebp,%ecx,4)
81137fffffff	adcl $-129,(%ebx)
8115000000007fffffff 2:1:sym+0	adcl $-129,sym
81548dfc7fffffff	adcl $-129,-4(%ebp,%ecx,4)
8113ff000000	adcl $255,(%ebx)
811500000000ff000000 2:1:sym+0	adcl $255,sym
81548dfcff000000	adcl $255,-4(%ebp,%ecx,4)
811300010000	adcl $256,(%ebx)
81150000000000010000 2:1:sym+0	adcl $256,sym
81548dfc00010000	adcl $256,-4(%ebp,%ecx,4)
8113ff7f0000	adcl $0x7fff,(%ebx)
811500000000ff7f0000 2:1:sym+0	adcl $0x7fff,sym
81548dfcff7f0000	adcl $0x7fff,-4(%ebp,%ecx,4)
811300800000	adcl $0x8000,(%ebx)
81150000000000800000 2:1:sym+0	adcl $0x8000,sym
81548dfc00800000	adcl $0x8000,-4(%ebp,%ecx,4)
811380ff0000	adcl $0xff80,(%ebx)
81150000000080ff0000 2:1:sym+0	adcl $0xff80,sym
81548dfc80ff0000	adcl $0xff80,-4(%ebp,%ecx,4)
8113ffff0000	adcl $0xffff,(%ebx)
811500000000ffff0000 2:1:sym+0	adcl $0xffff,sym
81548dfcffff0000	adcl $0xffff,-4(%ebp,%ecx,4)
811300000100	adcl $0x10000,(%ebx)
81150000000000000100 2:1:sym+0	adcl $0x10000,sym
81548dfc00000100	adcl $0x10000,-4(%ebp,%ecx,4)
8113ffffff7f	adcl $0x7fffffff,(%ebx)
811500000000ffffff7f 2:1:sym+0	adcl $0x7fffffff,sym
81548dfcffffff7f	adcl $0x7fffffff,-4(%ebp,%ecx,4)
811300000080	adcl $0x80000000,(%ebx)
81150000000000000080 2:1:sym+0	adcl $0x80000000,sym
81548dfc00000080	adcl $0x80000000,-4(%ebp,%ecx,4)
831380	adcl $0xffffff80,(%ebx)
83150000000080 2:1:sym+0	adcl $0xffffff80,sym
83548dfc80	adcl $0xffffff80,-4(%ebp,%ecx,4)
8313ff	adcl $0xffffffff,(%ebx)
831500000000ff 2:1:sym+0	adcl $0xffffffff,sym
83548dfcff	adcl $0xffffffff,-4(%ebp,%ecx,4)
811300000000 2:1:sym+0	adcl $sym,(%ebx)
81150000000000000000 2:1:sym+0 6:1:sym+0	adcl $sym,sym
81548dfc00000000 4:1:sym+0	adcl $sym,-4(%ebp,%ecx,4)
83d301	adc $1,%ebx
refused	adcl $1,$2
refused	adcl %eax,$2
refused	adcl (%ebx),(%ebx)
f0111b	lock adcl %ebx,(%ebx)
f0831301	lock adcl $1,(%ebx)
refused	lock adcl (%ebx),%ebx
refused	lock adcl %ebx,%eax
18c0	sbbb %al,%al
1803	sbbb %al,(%ebx)
1a03	sbbb (%ebx),%al
180500000000 2:1:sym+0	sbbb %al,sym
1a0500000000 2:1:sym+0	sbbb sym,%al
1803	sbb %al,(%ebx)
refused	sbb 4((%ebx,%esi,2),%al
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
1c00 1:22:sym+0	sbbb $sym,%al
18c8	sbbb %cl,%al
18c1	sbbb %al,%cl
180b	sbbb %cl,(%ebx)
1a0b	sbbb (%ebx),%cl
180d00000000 2:1:sym+0	sbbb %cl,sym
1a0d00000000 2:1:sym+0	sbbb sym,%cl
180b	sbb %cl,(%ebx)
refused	sbb 4((%ebx,%esi,2),%cl
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
80d900 2:22:sym+0	sbbb $sym,%cl
18f8	sbbb %bh,%al
18c7	sbbb %al,%bh
183b	sbbb %bh,(%ebx)
1a3b	sbbb (%ebx),%bh
183d00000000 2:1:sym+0	sbbb %bh,sym
1a3d00000000 2:1:sym+0	sbbb sym,%bh
183b	sbb %bh,(%ebx)
refused	sbb 4((%ebx,%esi,2),%bh
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
80df00 2:22:sym+0	sbbb $sym,%bh
801b00	sbbb $0,(%ebx)
801d0000000000 2:1:sym+0	sbbb $0,sym
805c8dfc00	sbbb $0,-4(%ebp,%ecx,4)
801b01	sbbb $1,(%ebx)
801d0000000001 2:1:sym+0	sbbb $1,sym
805c8dfc01	sbbb $1,-4(%ebp,%ecx,4)
801bff	sbbb $-1,(%ebx)
801d00000000ff 2:1:sym+0	sbbb $-1,sym
805c8dfcff	sbbb $-1,-4(%ebp,%ecx,4)
801b7f	sbbb $127,(%ebx)
801d000000007f 2:1:sym+0	sbbb $127,sym
805c8dfc7f	sbbb $127,-4(%ebp,%ecx,4)
801b80	sbbb $128,(%ebx)
801d0000000080 2:1:sym+0	sbbb $128,sym
805c8dfc80	sbbb $128,-4(%ebp,%ecx,4)
801b80	sbbb $-128,(%ebx)
801d0000000080 2:1:sym+0	sbbb $-128,sym
805c8dfc80	sbbb $-128,-4(%ebp,%ecx,4)
801b7f	sbbb $-129,(%ebx)
801d000000007f 2:1:sym+0	sbbb $-129,sym
805c8dfc7f	sbbb $-129,-4(%ebp,%ecx,4)
801bff	sbbb $255,(%ebx)
801d00000000ff 2:1:sym+0	sbbb $255,sym
805c8dfcff	sbbb $255,-4(%ebp,%ecx,4)
801b00	sbbb $256,(%ebx)
801d0000000000 2:1:sym+0	sbbb $256,sym
805c8dfc00	sbbb $256,-4(%ebp,%ecx,4)
801bff	sbbb $0x7fff,(%ebx)
801d00000000ff 2:1:sym+0	sbbb $0x7fff,sym
805c8dfcff	sbbb $0x7fff,-4(%ebp,%ecx,4)
801b00	sbbb $0x8000,(%ebx)
801d0000000000 2:1:sym+0	sbbb $0x8000,sym
805c8dfc00	sbbb $0x8000,-4(%ebp,%ecx,4)
801b80	sbbb $0xff80,(%ebx)
801d0000000080 2:1:sym+0	sbbb $0xff80,sym
805c8dfc80	sbbb $0xff80,-4(%ebp,%ecx,4)
801bff	sbbb $0xffff,(%ebx)
801d00000000ff 2:1:sym+0	sbbb $0xffff,sym
805c8dfcff	sbbb $0xffff,-4(%ebp,%ecx,4)
801b00	sbbb $0x10000,(%ebx)
801d0000000000 2:1:sym+0	sbbb $0x10000,sym
805c8dfc00	sbbb $0x10000,-4(%ebp,%ecx,4)
801bff	sbbb $0x7fffffff,(%ebx)
801d00000000ff 2:1:sym+0	sbbb $0x7fffffff,sym
805c8dfcff	sbbb $0x7fffffff,-4(%ebp,%ecx,4)
801b00	sbbb $0x80000000,(%ebx)
801d0000000000 2:1:sym+0	sbbb $0x80000000,sym
805c8dfc00	sbbb $0x80000000,-4(%ebp,%ecx,4)
801b80	sbbb $0xffffff80,(%ebx)
801d0000000080 2:1:sym+0	sbbb $0xffffff80,sym
805c8dfc80	sbbb $0xffffff80,-4(%ebp,%ecx,4)
801bff	sbbb $0xffffffff,(%ebx)
801d00000000ff 2:1:sym+0	sbbb $0xffffffff,sym
805c8dfcff	sbbb $0xffffffff,-4(%ebp,%ecx,4)
801b00 2:22:sym+0	sbbb $sym,(%ebx)
801d0000000000 2:1:sym+0 6:22:sym+0	sbbb $sym,sym
805c8dfc00 4:22:sym+0	sbbb $sym,-4(%ebp,%ecx,4)
80d901	sbb $1,%cl
831b01	sbb $1,(%ebx)
refused	sbbb $1,$2
refused	sbbb %al,$2
refused	sbbb (%ebx),(%ebx)
f0180b	lock sbbb %cl,(%ebx)
f0801b01	lock sbbb $1,(%ebx)
refused	lock sbbb (%ebx),%cl
refused	lock sbbb %cl,%al
6619c0	sbbw %ax,%ax
661903	sbbw %ax,(%ebx)
661b03	sbbw (%ebx),%ax
66190500000000 3:1:sym+0	sbbw %ax,sym
661b0500000000 3:1:sym+0	sbbw sym,%ax
661903	sbb %ax,(%ebx)
refused	sbb 4((%ebx,%esi,2),%ax
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
661d0000 2:20:sym+0	sbbw $sym,%ax
6619d8	sbbw %bx,%ax
6619c3	sbbw %ax,%bx
66191b	sbbw %bx,(%ebx)
661b1b	sbbw (%ebx),%bx
66191d00000000 3:1:sym+0	sbbw %bx,sym
661b1d00000000 3:1:sym+0	sbbw sym,%bx
66191b	sbb %bx,(%ebx)
refused	sbb 4((%ebx,%esi,2),%bx
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
6681db0000 3:20:sym+0	sbbw $sym,%bx
6619f0	sbbw %si,%ax
6619c6	sbbw %ax,%si
661933	sbbw %si,(%ebx)
661b33	sbbw (%ebx),%si
66193500000000 3:1:sym+0	sbbw %si,sym
661b3500000000 3:1:sym+0	sbbw sym,%si
661933	sbb %si,(%ebx)
refused	sbb 4((%ebx,%esi,2),%si
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
6681de0000 3:20:sym+0	sbbw $sym,%si
66831b00	sbbw $0,(%ebx)
66831d0000000000 3:1:sym+0	sbbw $0,sym
66835c8dfc00	sbbw $0,-4(%ebp,%ecx,4)
66831b01	sbbw $1,(%ebx)
66831d0000000001 3:1:sym+0	sbbw $1,sym
66835c8dfc01	sbbw $1,-4(%ebp,%ecx,4)
66831bff	sbbw $-1,(%ebx)
66831d00000000ff 3:1:sym+0	sbbw $-1,sym
66835c8dfcff	sbbw $-1,-4(%ebp,%ecx,4)
66831b7f	sbbw $127,(%ebx)
66831d000000007f 3:1:sym+0	sbbw $127,sym
66835c8dfc7f	sbbw $127,-4(%ebp,%ecx,4)
66811b8000	sbbw $128,(%ebx)
66811d000000008000 3:1:sym+0	sbbw $128,sym
66815c8dfc8000	sbbw $128,-4(%ebp,%ecx,4)
66831b80	sbbw $-128,(%ebx)
66831d0000000080 3:1:sym+0	sbbw $-128,sym
66835c8dfc80	sbbw $-128,-4(%ebp,%ecx,4)
66811b7fff	sbbw $-129,(%ebx)
66811d000000007fff 3:1:sym+0	sbbw $-129,sym
66815c8dfc7fff	sbbw $-129,-4(%ebp,%ecx,4)
66811bff00	sbbw $255,(%ebx)
66811d00000000ff00 3:1:sym+0	sbbw $255,sym
66815c8dfcff00	sbbw $255,-4(%ebp,%ecx,4)
66811b0001	sbbw $256,(%ebx)
66811d000000000001 3:1:sym+0	sbbw $256,sym
66815c8dfc0001	sbbw $256,-4(%ebp,%ecx,4)
66811bff7f	sbbw $0x7fff,(%ebx)
66811d00000000ff7f 3:1:sym+0	sbbw $0x7fff,sym
66815c8dfcff7f	sbbw $0x7fff,-4(%ebp,%ecx,4)
66811b0080	sbbw $0x8000,(%ebx)
66811d000000000080 3:1:sym+0	sbbw $0x8000,sym
66815c8dfc0080	sbbw $0x8000,-4(%ebp,%ecx,4)
66831b80	sbbw $0xff80,(%ebx)
66831d0000000080 3:1:sym+0	sbbw $0xff80,sym
66835c8dfc80	sbbw $0xff80,-4(%ebp,%ecx,4)
66831bff	sbbw $0xffff,(%ebx)
66831d00000000ff 3:1:sym+0	sbbw $0xffff,sym
66835c8dfcff	sbbw $0xffff,-4(%ebp,%ecx,4)
66811b0000	sbbw $0x10000,(%ebx)
66811d000000000000 3:1:sym+0	sbbw $0x10000,sym
66815c8dfc0000	sbbw $0x10000,-4(%ebp,%ecx,4)
66811bffff	sbbw $0x7fffffff,(%ebx)
66811d00000000ffff 3:1:sym+0	sbbw $0x7fffffff,sym
66815c8dfcffff	sbbw $0x7fffffff,-4(%ebp,%ecx,4)
66811b0000	sbbw $0x80000000,(%ebx)
66811d000000000000 3:1:sym+0	sbbw $0x80000000,sym
66815c8dfc0000	sbbw $0x80000000,-4(%ebp,%ecx,4)
66831b80	sbbw $0xffffff80,(%ebx)
66831d0000000080 3:1:sym+0	sbbw $0xffffff80,sym
66835c8dfc80	sbbw $0xffffff80,-4(%ebp,%ecx,4)
66831bff	sbbw $0xffffffff,(%ebx)
66831d00000000ff 3:1:sym+0	sbbw $0xffffffff,sym
66835c8dfcff	sbbw $0xffffffff,-4(%ebp,%ecx,4)
66811b0000 3:20:sym+0	sbbw $sym,(%ebx)
66811d000000000000 3:1:sym+0 7:20:sym+0	sbbw $sym,sym
66815c8dfc0000 5:20:sym+0	sbbw $sym,-4(%ebp,%ecx,4)
6683db01	sbb $1,%bx
refused	sbbw $1,$2
refused	sbbw %ax,$2
refused	sbbw (%ebx),(%ebx)
66f0191b	lock sbbw %bx,(%ebx)
66f0831b01	lock sbbw $1,(%ebx)
refused	lock sbbw (%ebx),%bx
refused	lock sbbw %bx,%ax
19c0	sbbl %eax,%eax
1903	sbbl %eax,(%ebx)
1b03	sbbl (%ebx),%eax
190500000000 2:1:sym+0	sbbl %eax,sym
1b0500000000 2:1:sym+0	sbbl sym,%eax
1903	sbb %eax,(%ebx)
refused	sbb 4((%ebx,%esi,2),%eax
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
1d00000000 1:1:sym+0	sbbl $sym,%eax
19d8	sbbl %ebx,%eax
19c3	sbbl %eax,%ebx
191b	sbbl %ebx,(%ebx)
1b1b	sbbl (%ebx),%ebx
191d00000000 2:1:sym+0	sbbl %ebx,sym
1b1d00000000 2:1:sym+0	sbbl sym,%ebx
191b	sbb %ebx,(%ebx)
refused	sbb 4((%ebx,%esi,2),%ebx
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
81db00000000 2:1:sym+0	sbbl $sym,%ebx
19e0	sbbl %esp,%eax
19c4	sbbl %eax,%esp
1923	sbbl %esp,(%ebx)
1b23	sbbl (%ebx),%esp
192500000000 2:1:sym+0	sbbl %esp,sym
1b2500000000 2:1:sym+0	sbbl sym,%esp
1923	sbb %esp,(%ebx)
refused	sbb 4((%ebx,%esi,2),%esp
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
81dc00000000 2:1:sym+0	sbbl $sym,%esp
19e8	sbbl %ebp,%eax
19c5	sbbl %eax,%ebp
192b	sbbl %ebp,(%ebx)
1b2b	sbbl (%ebx),%ebp
192d00000000 2:1:sym+0	sbbl %ebp,sym
1b2d00000000 2:1:sym+0	sbbl sym,%ebp
192b	sbb %ebp,(%ebx)
refused	sbb 4((%ebx,%esi,2),%ebp
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
81dd00000000 2:1:sym+0	sbbl $sym,%ebp
831b00	sbbl $0,(%ebx)
831d0000000000 2:1:sym+0	sbbl $0,sym
835c8dfc00	sbbl $0,-4(%ebp,%ecx,4)
831b01	sbbl $1,(%ebx)
831d0000000001 2:1:sym+0	sbbl $1,sym
835c8dfc01	sbbl $1,-4(%ebp,%ecx,4)
831bff	sbbl $-1,(%ebx)
831d00000000ff 2:1:sym+0	sbbl $-1,sym
835c8dfcff	sbbl $-1,-4(%ebp,%ecx,4)
831b7f	sbbl $127,(%ebx)
831d000000007f 2:1:sym+0	sbbl $127,sym
835c8dfc7f	sbbl $127,-4(%ebp,%ecx,4)
811b80000000	sbbl $128,(%ebx)
811d0000000080000000 2:1:sym+0	sbbl $128,sym
815c8dfc80000000	sbbl $128,-4(%ebp,%ecx,4)
831b80	sbbl $-128,(%ebx)
831d0000000080 2:1:sym+0	sbbl $-128,sym
835c8dfc80	sbbl $-128,-4(%ebp,%ecx,4)
811b7fffffff	sbbl $-129,(%ebx)
811d000000007fffffff 2:1:sym+0	sbbl $-129,sym
815c8dfc7fffffff	sbbl $-129,-4(%ebp,%ecx,4)
811bff000000	sbbl $255,(%ebx)
811d00000000ff000000 2:1:sym+0	sbbl $255,sym
815c8dfcff000000	sbbl $255,-4(%ebp,%ecx,4)
811b00010000	sbbl $256,(%ebx)
811d0000000000010000 2:1:sym+0	sbbl $256,sym
815c8dfc00010000	sbbl $256,-4(%ebp,%ecx,4)
811bff7f0000	sbbl $0x7fff,(%ebx)
811d00000000ff7f0000 2:1:sym+0	sbbl $0x7fff,sym
815c8dfcff7f0000	sbbl $0x7fff,-4(%ebp,%ecx,4)
811b00800000	sbbl $0x8000,(%ebx)
811d0000000000800000 2:1:sym+0	sbbl $0x8000,sym
815c8dfc00800000	sbbl $0x8000,-4(%ebp,%ecx,4)
811b80ff0000	sbbl $0xff80,(%ebx)
811d0000000080ff0000 2:1:sym+0	sbbl $0xff80,sym
815c8dfc80ff0000	sbbl $0xff80,-4(%ebp,%ecx,4)
811bffff0000	sbbl $0xffff,(%ebx)
811d00000000ffff0000 2:1:sym+0	sbbl $0xffff,sym
815c8dfcffff0000	sbbl $0xffff,-4(%ebp,%ecx,4)
811b00000100	sbbl $0x10000,(%ebx)
811d0000000000000100 2:1:sym+0	sbbl $0x10000,sym
815c8dfc00000100	sbbl $0x10000,-4(%ebp,%ecx,4)
811bffffff7f	sbbl $0x7fffffff,(%ebx)
811d00000000ffffff7f 2:1:sym+0	sbbl $0x7fffffff,sym
815c8dfcffffff7f	sbbl $0x7fffffff,-4(%ebp,%ecx,4)
811b00000080	sbbl $0x80000000,(%ebx)
811d0000000000000080 2:1:sym+0	sbbl $0x80000000,sym
815c8dfc00000080	sbbl $0x80000000,-4(%ebp,%ecx,4)
831b80	sbbl $0xffffff80,(%ebx)
831d0000000080 2:1:sym+0	sbbl $0xffffff80,sym
835c8dfc80	sbbl $0xffffff80,-4(%ebp,%ecx,4)
831bff	sbbl $0xffffffff,(%ebx)
831d00000000ff 2:1:sym+0	sbbl $0xffffffff,sym
835c8dfcff	sbbl $0xffffffff,-4(%ebp,%ecx,4)
811b00000000 2:1:sym+0	sbbl $sym,(%ebx)
811d0000000000000000 2:1:sym+0 6:1:sym+0	sbbl $sym,sym
815c8dfc00000000 4:1:sym+0	sbbl $sym,-4(%ebp,%ecx,4)
83db01	sbb $1,%ebx
refused	sbbl $1,$2
refused	sbbl %eax,$2
refused	sbbl (%ebx),(%ebx)
f0191b	lock sbbl %ebx,(%ebx)
f0831b01	lock sbbl $1,(%ebx)
refused	lock sbbl (%ebx),%ebx
refused	lock sbbl %ebx,%eax
20c0	andb %al,%al
2003	andb %al,(%ebx)
2203	andb (%ebx),%al
200500000000 2:1:sym+0	andb %al,sym
220500000000 2:1:sym+0	andb sym,%al
2003	and %al,(%ebx)
refused	and 4((%ebx,%esi,2),%al
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
2400 1:22:sym+0	andb $sym,%al
20c8	andb %cl,%al
20c1	andb %al,%cl
200b	andb %cl,(%ebx)
220b	andb (%ebx),%cl
200d00000000 2:1:sym+0	andb %cl,sym
220d00000000 2:1:sym+0	andb sym,%cl
200b	and %cl,(%ebx)
refused	and 4((%ebx,%esi,2),%cl
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
80e100 2:22:sym+0	andb $sym,%cl
20f8	andb %bh,%al
20c7	andb %al,%bh
203b	andb %bh,(%ebx)
223b	andb (%ebx),%bh
203d00000000 2:1:sym+0	andb %bh,sym
223d00000000 2:1:sym+0	andb sym,%bh
203b	and %bh,(%ebx)
refused	and 4((%ebx,%esi,2),%bh
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
80e700 2:22:sym+0	andb $sym,%bh
802300	andb $0,(%ebx)
80250000000000 2:1:sym+0	andb $0,sym
80648dfc00	andb $0,-4(%ebp,%ecx,4)
802301	andb $1,(%ebx)
80250000000001 2:1:sym+0	andb $1,sym
80648dfc01	andb $1,-4(%ebp,%ecx,4)
8023ff	andb $-1,(%ebx)
802500000000ff 2:1:sym+0	andb $-1,sym
80648dfcff	andb $-1,-4(%ebp,%ecx,4)
80237f	andb $127,(%ebx)
8025000000007f 2:1:sym+0	andb $127,sym
80648dfc7f	andb $127,-4(%ebp,%ecx,4)
802380	andb $128,(%ebx)
80250000000080 2:1:sym+0	andb $128,sym
80648dfc80	andb $128,-4(%ebp,%ecx,4)
802380	andb $-128,(%ebx)
80250000000080 2:1:sym+0	andb $-128,sym
80648dfc80	andb $-128,-4(%ebp,%ecx,4)
80237f	andb $-129,(%ebx)
8025000000007f 2:1:sym+0	andb $-129,sym
80648dfc7f	andb $-129,-4(%ebp,%ecx,4)
8023ff	andb $255,(%ebx)
802500000000ff 2:1:sym+0	andb $255,sym
80648dfcff	andb $255,-4(%ebp,%ecx,4)
802300	andb $256,(%ebx)
80250000000000 2:1:sym+0	andb $256,sym
80648dfc00	andb $256,-4(%ebp,%ecx,4)
8023ff	andb $0x7fff,(%ebx)
802500000000ff 2:1:sym+0	andb $0x7fff,sym
80648dfcff	andb $0x7fff,-4(%ebp,%ecx,4)
802300	andb $0x8000,(%ebx)
80250000000000 2:1:sym+0	andb $0x8000,sym
80648dfc00	andb $0x8000,-4(%ebp,%ecx,4)
802380	andb $0xff80,(%ebx)
80250000000080 2:1:sym+0	andb $0xff80,sym
80648dfc80	andb $0xff80,-4(%ebp,%ecx,4)
8023ff	andb $0xffff,(%ebx)
802500000000ff 2:1:sym+0	andb $0xffff,sym
80648dfcff	andb $0xffff,-4(%ebp,%ecx,4)
802300	andb $0x10000,(%ebx)
80250000000000 2:1:sym+0	andb $0x10000,sym
80648dfc00	andb $0x10000,-4(%ebp,%ecx,4)
8023ff	andb $0x7fffffff,(%ebx)
802500000000ff 2:1:sym+0	andb $0x7fffffff,sym
80648dfcff	andb $0x7fffffff,-4(%ebp,%ecx,4)
802300	andb $0x80000000,(%ebx)
80250000000000 2:1:sym+0	andb $0x80000000,sym
80648dfc00	andb $0x80000000,-4(%ebp,%ecx,4)
802380	andb $0xffffff80,(%ebx)
80250000000080 2:1:sym+0	andb $0xffffff80,sym
80648dfc80	andb $0xffffff80,-4(%ebp,%ecx,4)
8023ff	andb $0xffffffff,(%ebx)
802500000000ff 2:1:sym+0	andb $0xffffffff,sym
80648dfcff	andb $0xffffffff,-4(%ebp,%ecx,4)
802300 2:22:sym+0	andb $sym,(%ebx)
80250000000000 2:1:sym+0 6:22:sym+0	andb $sym,sym
80648dfc00 4:22:sym+0	andb $sym,-4(%ebp,%ecx,4)
80e101	and $1,%cl
832301	and $1,(%ebx)
refused	andb $1,$2
refused	andb %al,$2
refused	andb (%ebx),(%ebx)
f0200b	lock andb %cl,(%ebx)
f0802301	lock andb $1,(%ebx)
refused	lock andb (%ebx),%cl
refused	lock andb %cl,%al
6621c0	andw %ax,%ax
662103	andw %ax,(%ebx)
662303	andw (%ebx),%ax
66210500000000 3:1:sym+0	andw %ax,sym
66230500000000 3:1:sym+0	andw sym,%ax
662103	and %ax,(%ebx)
refused	and 4((%ebx,%esi,2),%ax
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
66250000 2:20:sym+0	andw $sym,%ax
6621d8	andw %bx,%ax
6621c3	andw %ax,%bx
66211b	andw %bx,(%ebx)
66231b	andw (%ebx),%bx
66211d00000000 3:1:sym+0	andw %bx,sym
66231d00000000 3:1:sym+0	andw sym,%bx
66211b	and %bx,(%ebx)
refused	and 4((%ebx,%esi,2),%bx
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
6681e30000 3:20:sym+0	andw $sym,%bx
6621f0	andw %si,%ax
6621c6	andw %ax,%si
662133	andw %si,(%ebx)
662333	andw (%ebx),%si
66213500000000 3:1:sym+0	andw %si,sym
66233500000000 3:1:sym+0	andw sym,%si
662133	and %si,(%ebx)
refused	and 4((%ebx,%esi,2),%si
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
6681e60000 3:20:sym+0	andw $sym,%si
66832300	andw $0,(%ebx)
6683250000000000 3:1:sym+0	andw $0,sym
6683648dfc00	andw $0,-4(%ebp,%ecx,4)
66832301	andw $1,(%ebx)
6683250000000001 3:1:sym+0	andw $1,sym
6683648dfc01	andw $1,-4(%ebp,%ecx,4)
668323ff	andw $-1,(%ebx)
66832500000000ff 3:1:sym+0	andw $-1,sym
6683648dfcff	andw $-1,-4(%ebp,%ecx,4)
6683237f	andw $127,(%ebx)
668325000000007f 3:1:sym+0	andw $127,sym
6683648dfc7f	andw $127,-4(%ebp,%ecx,4)
6681238000	andw $128,(%ebx)
668125000000008000 3:1:sym+0	andw $128,sym
6681648dfc8000	andw $128,-4(%ebp,%ecx,4)
66832380	andw $-128,(%ebx)
6683250000000080 3:1:sym+0	andw $-128,sym
6683648dfc80	andw $-128,-4(%ebp,%ecx,4)
6681237fff	andw $-129,(%ebx)
668125000000007fff 3:1:sym+0	andw $-129,sym
6681648dfc7fff	andw $-129,-4(%ebp,%ecx,4)
668123ff00	andw $255,(%ebx)
66812500000000ff00 3:1:sym+0	andw $255,sym
6681648dfcff00	andw $255,-4(%ebp,%ecx,4)
6681230001	andw $256,(%ebx)
668125000000000001 3:1:sym+0	andw $256,sym
6681648dfc0001	andw $256,-4(%ebp,%ecx,4)
668123ff7f	andw $0x7fff,(%ebx)
66812500000000ff7f 3:1:sym+0	andw $0x7fff,sym
6681648dfcff7f	andw $0x7fff,-4(%ebp,%ecx,4)
6681230080	andw $0x8000,(%ebx)
668125000000000080 3:1:sym+0	andw $0x8000,sym
6681648dfc0080	andw $0x8000,-4(%ebp,%ecx,4)
66832380	andw $0xff80,(%ebx)
6683250000000080 3:1:sym+0	andw $0xff80,sym
6683648dfc80	andw $0xff80,-4(%ebp,%ecx,4)
668323ff	andw $0xffff,(%ebx)
66832500000000ff 3:1:sym+0	andw $0xffff,sym
6683648dfcff	andw $0xffff,-4(%ebp,%ecx,4)
6681230000	andw $0x10000,(%ebx)
668125000000000000 3:1:sym+0	andw $0x10000,sym
6681648dfc0000	andw $0x10000,-4(%ebp,%ecx,4)
668123ffff	andw $0x7fffffff,(%ebx)
66812500000000ffff 3:1:sym+0	andw $0x7fffffff,sym
6681648dfcffff	andw $0x7fffffff,-4(%ebp,%ecx,4)
6681230000	andw $0x80000000,(%ebx)
668125000000000000 3:1:sym+0	andw $0x80000000,sym
6681648dfc0000	andw $0x80000000,-4(%ebp,%ecx,4)
66832380	andw $0xffffff80,(%ebx)
6683250000000080 3:1:sym+0	andw $0xffffff80,sym
6683648dfc80	andw $0xffffff80,-4(%ebp,%ecx,4)
668323ff	andw $0xffffffff,(%ebx)
66832500000000ff 3:1:sym+0	andw $0xffffffff,sym
6683648dfcff	andw $0xffffffff,-4(%ebp,%ecx,4)
6681230000 3:20:sym+0	andw $sym,(%ebx)
668125000000000000 3:1:sym+0 7:20:sym+0	andw $sym,sym
6681648dfc0000 5:20:sym+0	andw $sym,-4(%ebp,%ecx,4)
6683e301	and $1,%bx
refused	andw $1,$2
refused	andw %ax,$2
refused	andw (%ebx),(%ebx)
66f0211b	lock andw %bx,(%ebx)
66f0832301	lock andw $1,(%ebx)
refused	lock andw (%ebx),%bx
refused	lock andw %bx,%ax
21c0	andl %eax,%eax
2103	andl %eax,(%ebx)
2303	andl (%ebx),%eax
210500000000 2:1:sym+0	andl %eax,sym
230500000000 2:1:sym+0	andl sym,%eax
2103	and %eax,(%ebx)
refused	and 4((%ebx,%esi,2),%eax
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
2500000000 1:1:sym+0	andl $sym,%eax
21d8	andl %ebx,%eax
21c3	andl %eax,%ebx
211b	andl %ebx,(%ebx)
231b	andl (%ebx),%ebx
211d00000000 2:1:sym+0	andl %ebx,sym
231d00000000 2:1:sym+0	andl sym,%ebx
211b	and %ebx,(%ebx)
refused	and 4((%ebx,%esi,2),%ebx
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
81e300000000 2:1:sym+0	andl $sym,%ebx
21e0	andl %esp,%eax
21c4	andl %eax,%esp
2123	andl %esp,(%ebx)
2323	andl (%ebx),%esp
212500000000 2:1:sym+0	andl %esp,sym
232500000000 2:1:sym+0	andl sym,%esp
2123	and %esp,(%ebx)
refused	and 4((%ebx,%esi,2),%esp
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
81e400000000 2:1:sym+0	andl $sym,%esp
21e8	andl %ebp,%eax
21c5	andl %eax,%ebp
212b	andl %ebp,(%ebx)
232b	andl (%ebx),%ebp
212d00000000 2:1:sym+0	andl %ebp,sym
232d00000000 2:1:sym+0	andl sym,%ebp
212b	and %ebp,(%ebx)
refused	and 4((%ebx,%esi,2),%ebp
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
81e500000000 2:1:sym+0	andl $sym,%ebp
832300	andl $0,(%ebx)
83250000000000 2:1:sym+0	andl $0,sym
83648dfc00	andl $0,-4(%ebp,%ecx,4)
832301	andl $1,(%ebx)
83250000000001 2:1:sym+0	andl $1,sym
83648dfc01	andl $1,-4(%ebp,%ecx,4)
8323ff	andl $-1,(%ebx)
832500000000ff 2:1:sym+0	andl $-1,sym
83648dfcff	andl $-1,-4(%ebp,%ecx,4)
83237f	andl $127,(%ebx)
8325000000007f 2:1:sym+0	andl $127,sym
83648dfc7f	andl $127,-4(%ebp,%ecx,4)
812380000000	andl $128,(%ebx)
81250000000080000000 2:1:sym+0	andl $128,sym
81648dfc80000000	andl $128,-4(%ebp,%ecx,4)
832380	andl $-128,(%ebx)
83250000000080 2:1:sym+0	andl $-128,sym
83648dfc80	andl $-128,-4(%ebp,%ecx,4)
81237fffffff	andl $-129,(%ebx)
8125000000007fffffff 2:1:sym+0	andl $-129,sym
81648dfc7fffffff	andl $-129,-4(%ebp,%ecx,4)
8123ff000000	andl $255,(%ebx)
812500000000ff000000 2:1:sym+0	andl $255,sym
81648dfcff000000	andl $255,-4(%ebp,%ecx,4)
812300010000	andl $256,(%ebx)
81250000000000010000 2:1:sym+0	andl $256,sym
81648dfc00010000	andl $256,-4(%ebp,%ecx,4)
8123ff7f0000	andl $0x7fff,(%ebx)
812500000000ff7f0000 2:1:sym+0	andl $0x7fff,sym
81648dfcff7f0000	andl $0x7fff,-4(%ebp,%ecx,4)
812300800000	andl $0x8000,(%ebx)
81250000000000800000 2:1:sym+0	andl $0x8000,sym
81648dfc00800000	andl $0x8000,-4(%ebp,%ecx,4)
812380ff0000	andl $0xff80,(%ebx)
81250000000080ff0000 2:1:sym+0	andl $0xff80,sym
81648dfc80ff0000	andl $0xff80,-4(%ebp,%ecx,4)
8123ffff0000	andl $0xffff,(%ebx)
812500000000ffff0000 2:1:sym+0	andl $0xffff,sym
81648dfcffff0000	andl $0xffff,-4(%ebp,%ecx,4)
812300000100	andl $0x10000,(%ebx)
81250000000000000100 2:1:sym+0	andl $0x10000,sym
81648dfc00000100	andl $0x10000,-4(%ebp,%ecx,4)
8123ffffff7f	andl $0x7fffffff,(%ebx)
812500000000ffffff7f 2:1:sym+0	andl $0x7fffffff,sym
81648dfcffffff7f	andl $0x7fffffff,-4(%ebp,%ecx,4)
812300000080	andl $0x80000000,(%ebx)
81250000000000000080 2:1:sym+0	andl $0x80000000,sym
81648dfc00000080	andl $0x80000000,-4(%ebp,%ecx,4)
832380	andl $0xffffff80,(%ebx)
83250000000080 2:1:sym+0	andl $0xffffff80,sym
83648dfc80	andl $0xffffff80,-4(%ebp,%ecx,4)
8323ff	andl $0xffffffff,(%ebx)
832500000000ff 2:1:sym+0	andl $0xffffffff,sym
83648dfcff	andl $0xffffffff,-4(%ebp,%ecx,4)
812300000000 2:1:sym+0	andl $sym,(%ebx)
81250000000000000000 2:1:sym+0 6:1:sym+0	andl $sym,sym
81648dfc00000000 4:1:sym+0	andl $sym,-4(%ebp,%ecx,4)
83e301	and $1,%ebx
refused	andl $1,$2
refused	andl %eax,$2
refused	andl (%ebx),(%ebx)
f0211b	lock andl %ebx,(%ebx)
f0832301	lock andl $1,(%ebx)
refused	lock andl (%ebx),%ebx
refused	lock andl %ebx,%eax
28c0	subb %al,%al
2803	subb %al,(%ebx)
2a03	subb (%ebx),%al
280500000000 2:1:sym+0	subb %al,sym
2a0500000000 2:1:sym+0	subb sym,%al
2803	sub %al,(%ebx)
refused	sub 4((%ebx,%esi,2),%al
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
2c00 1:22:sym+0	subb $sym,%al
28c8	subb %cl,%al
28c1	subb %al,%cl
280b	subb %cl,(%ebx)
2a0b	subb (%ebx),%cl
280d00000000 2:1:sym+0	subb %cl,sym
2a0d00000000 2:1:sym+0	subb sym,%cl
280b	sub %cl,(%ebx)
refused	sub 4((%ebx,%esi,2),%cl
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
80e900 2:22:sym+0	subb $sym,%cl
28f8	subb %bh,%al
28c7	subb %al,%bh
283b	subb %bh,(%ebx)
2a3b	subb (%ebx),%bh
283d00000000 2:1:sym+0	subb %bh,sym
2a3d00000000 2:1:sym+0	subb sym,%bh
283b	sub %bh,(%ebx)
refused	sub 4((%ebx,%esi,2),%bh
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
80ef00 2:22:sym+0	subb $sym,%bh
802b00	subb $0,(%ebx)
802d0000000000 2:1:sym+0	subb $0,sym
806c8dfc00	subb $0,-4(%ebp,%ecx,4)
802b01	subb $1,(%ebx)
802d0000000001 2:1:sym+0	subb $1,sym
806c8dfc01	subb $1,-4(%ebp,%ecx,4)
802bff	subb $-1,(%ebx)
802d00000000ff 2:1:sym+0	subb $-1,sym
806c8dfcff	subb $-1,-4(%ebp,%ecx,4)
802b7f	subb $127,(%ebx)
802d000000007f 2:1:sym+0	subb $127,sym
806c8dfc7f	subb $127,-4(%ebp,%ecx,4)
802b80	subb $128,(%ebx)
802d0000000080 2:1:sym+0	subb $128,sym
806c8dfc80	subb $128,-4(%ebp,%ecx,4)
802b80	subb $-128,(%ebx)
802d0000000080 2:1:sym+0	subb $-128,sym
806c8dfc80	subb $-128,-4(%ebp,%ecx,4)
802b7f	subb $-129,(%ebx)
802d000000007f 2:1:sym+0	subb $-129,sym
806c8dfc7f	subb $-129,-4(%ebp,%ecx,4)
802bff	subb $255,(%ebx)
802d00000000ff 2:1:sym+0	subb $255,sym
806c8dfcff	subb $255,-4(%ebp,%ecx,4)
802b00	subb $256,(%ebx)
802d0000000000 2:1:sym+0	subb $256,sym
806c8dfc00	subb $256,-4(%ebp,%ecx,4)
802bff	subb $0x7fff,(%ebx)
802d00000000ff 2:1:sym+0	subb $0x7fff,sym
806c8dfcff	subb $0x7fff,-4(%ebp,%ecx,4)
802b00	subb $0x8000,(%ebx)
802d0000000000 2:1:sym+0	subb $0x8000,sym
806c8dfc00	subb $0x8000,-4(%ebp,%ecx,4)
802b80	subb $0xff80,(%ebx)
802d0000000080 2:1:sym+0	subb $0xff80,sym
806c8dfc80	subb $0xff80,-4(%ebp,%ecx,4)
802bff	subb $0xffff,(%ebx)
802d00000000ff 2:1:sym+0	subb $0xffff,sym
806c8dfcff	subb $0xffff,-4(%ebp,%ecx,4)
802b00	subb $0x10000,(%ebx)
802d0000000000 2:1:sym+0	subb $0x10000,sym
806c8dfc00	subb $0x10000,-4(%ebp,%ecx,4)
802bff	subb $0x7fffffff,(%ebx)
802d00000000ff 2:1:sym+0	subb $0x7fffffff,sym
806c8dfcff	subb $0x7fffffff,-4(%ebp,%ecx,4)
802b00	subb $0x80000000,(%ebx)
802d0000000000 2:1:sym+0	subb $0x80000000,sym
806c8dfc00	subb $0x80000000,-4(%ebp,%ecx,4)
802b80	subb $0xffffff80,(%ebx)
802d0000000080 2:1:sym+0	subb $0xffffff80,sym
806c8dfc80	subb $0xffffff80,-4(%ebp,%ecx,4)
802bff	subb $0xffffffff,(%ebx)
802d00000000ff 2:1:sym+0	subb $0xffffffff,sym
806c8dfcff	subb $0xffffffff,-4(%ebp,%ecx,4)
802b00 2:22:sym+0	subb $sym,(%ebx)
802d0000000000 2:1:sym+0 6:22:sym+0	subb $sym,sym
806c8dfc00 4:22:sym+0	subb $sym,-4(%ebp,%ecx,4)
80e901	sub $1,%cl
832b01	sub $1,(%ebx)
refused	subb $1,$2
refused	subb %al,$2
refused	subb (%ebx),(%ebx)
f0280b	lock subb %cl,(%ebx)
f0802b01	lock subb $1,(%ebx)
refused	lock subb (%ebx),%cl
refused	lock subb %cl,%al
6629c0	subw %ax,%ax
662903	subw %ax,(%ebx)
662b03	subw (%ebx),%ax
66290500000000 3:1:sym+0	subw %ax,sym
662b0500000000 3:1:sym+0	subw sym,%ax
662903	sub %ax,(%ebx)
refused	sub 4((%ebx,%esi,2),%ax
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
662d0000 2:20:sym+0	subw $sym,%ax
6629d8	subw %bx,%ax
6629c3	subw %ax,%bx
66291b	subw %bx,(%ebx)
662b1b	subw (%ebx),%bx
66291d00000000 3:1:sym+0	subw %bx,sym
662b1d00000000 3:1:sym+0	subw sym,%bx
66291b	sub %bx,(%ebx)
refused	sub 4((%ebx,%esi,2),%bx
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
6681eb0000 3:20:sym+0	subw $sym,%bx
6629f0	subw %si,%ax
6629c6	subw %ax,%si
662933	subw %si,(%ebx)
662b33	subw (%ebx),%si
66293500000000 3:1:sym+0	subw %si,sym
662b3500000000 3:1:sym+0	subw sym,%si
662933	sub %si,(%ebx)
refused	sub 4((%ebx,%esi,2),%si
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
6681ee0000 3:20:sym+0	subw $sym,%si
66832b00	subw $0,(%ebx)
66832d0000000000 3:1:sym+0	subw $0,sym
66836c8dfc00	subw $0,-4(%ebp,%ecx,4)
66832b01	subw $1,(%ebx)
66832d0000000001 3:1:sym+0	subw $1,sym
66836c8dfc01	subw $1,-4(%ebp,%ecx,4)
66832bff	subw $-1,(%ebx)
66832d00000000ff 3:1:sym+0	subw $-1,sym
66836c8dfcff	subw $-1,-4(%ebp,%ecx,4)
66832b7f	subw $127,(%ebx)
66832d000000007f 3:1:sym+0	subw $127,sym
66836c8dfc7f	subw $127,-4(%ebp,%ecx,4)
66812b8000	subw $128,(%ebx)
66812d000000008000 3:1:sym+0	subw $128,sym
66816c8dfc8000	subw $128,-4(%ebp,%ecx,4)
66832b80	subw $-128,(%ebx)
66832d0000000080 3:1:sym+0	subw $-128,sym
66836c8dfc80	subw $-128,-4(%ebp,%ecx,4)
66812b7fff	subw $-129,(%ebx)
66812d000000007fff 3:1:sym+0	subw $-129,sym
66816c8dfc7fff	subw $-129,-4(%ebp,%ecx,4)
66812bff00	subw $255,(%ebx)
66812d00000000ff00 3:1:sym+0	subw $255,sym
66816c8dfcff00	subw $255,-4(%ebp,%ecx,4)
66812b0001	subw $256,(%ebx)
66812d000000000001 3:1:sym+0	subw $256,sym
66816c8dfc0001	subw $256,-4(%ebp,%ecx,4)
66812bff7f	subw $0x7fff,(%ebx)
66812d00000000ff7f 3:1:sym+0	subw $0x7fff,sym
66816c8dfcff7f	subw $0x7fff,-4(%ebp,%ecx,4)
66812b0080	subw $0x8000,(%ebx)
66812d000000000080 3:1:sym+0	subw $0x8000,sym
66816c8dfc0080	subw $0x8000,-4(%ebp,%ecx,4)
66832b80	subw $0xff80,(%ebx)
66832d0000000080 3:1:sym+0	subw $0xff80,sym
66836c8dfc80	subw $0xff80,-4(%ebp,%ecx,4)
66832bff	subw $0xffff,(%ebx)
66832d00000000ff 3:1:sym+0	subw $0xffff,sym
66836c8dfcff	subw $0xffff,-4(%ebp,%ecx,4)
66812b0000	subw $0x10000,(%ebx)
66812d000000000000 3:1:sym+0	subw $0x10000,sym
66816c8dfc0000	subw $0x10000,-4(%ebp,%ecx,4)
66812bffff	subw $0x7fffffff,(%ebx)
66812d00000000ffff 3:1:sym+0	subw $0x7fffffff,sym
66816c8dfcffff	subw $0x7fffffff,-4(%ebp,%ecx,4)
66812b0000	subw $0x80000000,(%ebx)
66812d000000000000 3:1:sym+0	subw $0x80000000,sym
66816c8dfc0000	subw $0x80000000,-4(%ebp,%ecx,4)
66832b80	subw $0xffffff80,(%ebx)
66832d0000000080 3:1:sym+0	subw $0xffffff80,sym
66836c8dfc80	subw $0xffffff80,-4(%ebp,%ecx,4)
66832bff	subw $0xffffffff,(%ebx)
66832d00000000ff 3:1:sym+0	subw $0xffffffff,sym
66836c8dfcff	subw $0xffffffff,-4(%ebp,%ecx,4)
66812b0000 3:20:sym+0	subw $sym,(%ebx)
66812d000000000000 3:1:sym+0 7:20:sym+0	subw $sym,sym
66816c8dfc0000 5:20:sym+0	subw $sym,-4(%ebp,%ecx,4)
6683eb01	sub $1,%bx
refused	subw $1,$2
refused	subw %ax,$2
refused	subw (%ebx),(%ebx)
66f0291b	lock subw %bx,(%ebx)
66f0832b01	lock subw $1,(%ebx)
refused	lock subw (%ebx),%bx
refused	lock subw %bx,%ax
29c0	subl %eax,%eax
2903	subl %eax,(%ebx)
2b03	subl (%ebx),%eax
290500000000 2:1:sym+0	subl %eax,sym
2b0500000000 2:1:sym+0	subl sym,%eax
2903	sub %eax,(%ebx)
refused	sub 4((%ebx,%esi,2),%eax
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
2d00000000 1:1:sym+0	subl $sym,%eax
29d8	subl %ebx,%eax
29c3	subl %eax,%ebx
291b	subl %ebx,(%ebx)
2b1b	subl (%ebx),%ebx
291d00000000 2:1:sym+0	subl %ebx,sym
2b1d00000000 2:1:sym+0	subl sym,%ebx
291b	sub %ebx,(%ebx)
refused	sub 4((%ebx,%esi,2),%ebx
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
81eb00000000 2:1:sym+0	subl $sym,%ebx
29e0	subl %esp,%eax
29c4	subl %eax,%esp
2923	subl %esp,(%ebx)
2b23	subl (%ebx),%esp
292500000000 2:1:sym+0	subl %esp,sym
2b2500000000 2:1:sym+0	subl sym,%esp
2923	sub %esp,(%ebx)
refused	sub 4((%ebx,%esi,2),%esp
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
81ec00000000 2:1:sym+0	subl $sym,%esp
29e8	subl %ebp,%eax
29c5	subl %eax,%ebp
292b	subl %ebp,(%ebx)
2b2b	subl (%ebx),%ebp
292d00000000 2:1:sym+0	subl %ebp,sym
2b2d00000000 2:1:sym+0	subl sym,%ebp
292b	sub %ebp,(%ebx)
refused	sub 4((%ebx,%esi,2),%ebp
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
81ed00000000 2:1:sym+0	subl $sym,%ebp
832b00	subl $0,(%ebx)
832d0000000000 2:1:sym+0	subl $0,sym
836c8dfc00	subl $0,-4(%ebp,%ecx,4)
832b01	subl $1,(%ebx)
832d0000000001 2:1:sym+0	subl $1,sym
836c8dfc01	subl $1,-4(%ebp,%ecx,4)
832bff	subl $-1,(%ebx)
832d00000000ff 2:1:sym+0	subl $-1,sym
836c8dfcff	subl $-1,-4(%ebp,%ecx,4)
832b7f	subl $127,(%ebx)
832d000000007f 2:1:sym+0	subl $127,sym
836c8dfc7f	subl $127,-4(%ebp,%ecx,4)
812b80000000	subl $128,(%ebx)
812d0000000080000000 2:1:sym+0	subl $128,sym
816c8dfc80000000	subl $128,-4(%ebp,%ecx,4)
832b80	subl $-128,(%ebx)
832d0000000080 2:1:sym+0	subl $-128,sym
836c8dfc80	subl $-128,-4(%ebp,%ecx,4)
812b7fffffff	subl $-129,(%ebx)
812d000000007fffffff 2:1:sym+0	subl $-129,sym
816c8dfc7fffffff	subl $-129,-4(%ebp,%ecx,4)
812bff000000	subl $255,(%ebx)
812d00000000ff000000 2:1:sym+0	subl $255,sym
816c8dfcff000000	subl $255,-4(%ebp,%ecx,4)
812b00010000	subl $256,(%ebx)
812d0000000000010000 2:1:sym+0	subl $256,sym
816c8dfc00010000	subl $256,-4(%ebp,%ecx,4)
812bff7f0000	subl $0x7fff,(%ebx)
812d00000000ff7f0000 2:1:sym+0	subl $0x7fff,sym
816c8dfcff7f0000	subl $0x7fff,-4(%ebp,%ecx,4)
812b00800000	subl $0x8000,(%ebx)
812d0000000000800000 2:1:sym+0	subl $0x8000,sym
816c8dfc00800000	subl $0x8000,-4(%ebp,%ecx,4)
812b80ff0000	subl $0xff80,(%ebx)
812d0000000080ff0000 2:1:sym+0	subl $0xff80,sym
816c8dfc80ff0000	subl $0xff80,-4(%ebp,%ecx,4)
812bffff0000	subl $0xffff,(%ebx)
812d00000000ffff0000 2:1:sym+0	subl $0xffff,sym
816c8dfcffff0000	subl $0xffff,-4(%ebp,%ecx,4)
812b00000100	subl $0x10000,(%ebx)
812d0000000000000100 2:1:sym+0	subl $0x10000,sym
816c8dfc00000100	subl $0x10000,-4(%ebp,%ecx,4)
812bffffff7f	subl $0x7fffffff,(%ebx)
812d00000000ffffff7f 2:1:sym+0	subl $0x7fffffff,sym
816c8dfcffffff7f	subl $0x7fffffff,-4(%ebp,%ecx,4)
812b00000080	subl $0x80000000,(%ebx)
812d0000000000000080 2:1:sym+0	subl $0x80000000,sym
816c8dfc00000080	subl $0x80000000,-4(%ebp,%ecx,4)
832b80	subl $0xffffff80,(%ebx)
832d0000000080 2:1:sym+0	subl $0xffffff80,sym
836c8dfc80	subl $0xffffff80,-4(%ebp,%ecx,4)
832bff	subl $0xffffffff,(%ebx)
832d00000000ff 2:1:sym+0	subl $0xffffffff,sym
836c8dfcff	subl $0xffffffff,-4(%ebp,%ecx,4)
812b00000000 2:1:sym+0	subl $sym,(%ebx)
812d0000000000000000 2:1:sym+0 6:1:sym+0	subl $sym,sym
816c8dfc00000000 4:1:sym+0	subl $sym,-4(%ebp,%ecx,4)
83eb01	sub $1,%ebx
refused	subl $1,$2
refused	subl %eax,$2
refused	subl (%ebx),(%ebx)
f0291b	lock subl %ebx,(%ebx)
f0832b01	lock subl $1,(%ebx)
refused	lock subl (%ebx),%ebx
refused	lock subl %ebx,%eax
30c0	xorb %al,%al
3003	xorb %al,(%ebx)
3203	xorb (%ebx),%al
300500000000 2:1:sym+0	xorb %al,sym
320500000000 2:1:sym+0	xorb sym,%al
3003	xor %al,(%ebx)
refused	xor 4((%ebx,%esi,2),%al
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
3400 1:22:sym+0	xorb $sym,%al
30c8	xorb %cl,%al
30c1	xorb %al,%cl
300b	xorb %cl,(%ebx)
320b	xorb (%ebx),%cl
300d00000000 2:1:sym+0	xorb %cl,sym
320d00000000 2:1:sym+0	xorb sym,%cl
300b	xor %cl,(%ebx)
refused	xor 4((%ebx,%esi,2),%cl
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
80f100 2:22:sym+0	xorb $sym,%cl
30f8	xorb %bh,%al
30c7	xorb %al,%bh
303b	xorb %bh,(%ebx)
323b	xorb (%ebx),%bh
303d00000000 2:1:sym+0	xorb %bh,sym
323d00000000 2:1:sym+0	xorb sym,%bh
303b	xor %bh,(%ebx)
refused	xor 4((%ebx,%esi,2),%bh
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
80f700 2:22:sym+0	xorb $sym,%bh
803300	xorb $0,(%ebx)
80350000000000 2:1:sym+0	xorb $0,sym
80748dfc00	xorb $0,-4(%ebp,%ecx,4)
803301	xorb $1,(%ebx)
80350000000001 2:1:sym+0	xorb $1,sym
80748dfc01	xorb $1,-4(%ebp,%ecx,4)
8033ff	xorb $-1,(%ebx)
803500000000ff 2:1:sym+0	xorb $-1,sym
80748dfcff	xorb $-1,-4(%ebp,%ecx,4)
80337f	xorb $127,(%ebx)
8035000000007f 2:1:sym+0	xorb $127,sym
80748dfc7f	xorb $127,-4(%ebp,%ecx,4)
803380	xorb $128,(%ebx)
80350000000080 2:1:sym+0	xorb $128,sym
80748dfc80	xorb $128,-4(%ebp,%ecx,4)
803380	xorb $-128,(%ebx)
80350000000080 2:1:sym+0	xorb $-128,sym
80748dfc80	xorb $-128,-4(%ebp,%ecx,4)
80337f	xorb $-129,(%ebx)
8035000000007f 2:1:sym+0	xorb $-129,sym
80748dfc7f	xorb $-129,-4(%ebp,%ecx,4)
8033ff	xorb $255,(%ebx)
803500000000ff 2:1:sym+0	xorb $255,sym
80748dfcff	xorb $255,-4(%ebp,%ecx,4)
803300	xorb $256,(%ebx)
80350000000000 2:1:sym+0	xorb $256,sym
80748dfc00	xorb $256,-4(%ebp,%ecx,4)
8033ff	xorb $0x7fff,(%ebx)
803500000000ff 2:1:sym+0	xorb $0x7fff,sym
80748dfcff	xorb $0x7fff,-4(%ebp,%ecx,4)
803300	xorb $0x8000,(%ebx)
80350000000000 2:1:sym+0	xorb $0x8000,sym
80748dfc00	xorb $0x8000,-4(%ebp,%ecx,4)
803380	xorb $0xff80,(%ebx)
80350000000080 2:1:sym+0	xorb $0xff80,sym
80748dfc80	xorb $0xff80,-4(%ebp,%ecx,4)
8033ff	xorb $0xffff,(%ebx)
803500000000ff 2:1:sym+0	xorb $0xffff,sym
80748dfcff	xorb $0xffff,-4(%ebp,%ecx,4)
803300	xorb $0x10000,(%ebx)
80350000000000 2:1:sym+0	xorb $0x10000,sym
80748dfc00	xorb $0x10000,-4(%ebp,%ecx,4)
8033ff	xorb $0x7fffffff,(%ebx)
803500000000ff 2:1:sym+0	xorb $0x7fffffff,sym
80748dfcff	xorb $0x7fffffff,-4(%ebp,%ecx,4)
803300	xorb $0x80000000,(%ebx)
80350000000000 2:1:sym+0	xorb $0x80000000,sym
80748dfc00	xorb $0x80000000,-4(%ebp,%ecx,4)
803380	xorb $0xffffff80,(%ebx)
80350000000080 2:1:sym+0	xorb $0xffffff80,sym
80748dfc80	xorb $0xffffff80,-4(%ebp,%ecx,4)
8033ff	xorb $0xffffffff,(%ebx)
803500000000ff 2:1:sym+0	xorb $0xffffffff,sym
80748dfcff	xorb $0xffffffff,-4(%ebp,%ecx,4)
803300 2:22:sym+0	xorb $sym,(%ebx)
80350000000000 2:1:sym+0 6:22:sym+0	xorb $sym,sym
80748dfc00 4:22:sym+0	xorb $sym,-4(%ebp,%ecx,4)
80f101	xor $1,%cl
833301	xor $1,(%ebx)
refused	xorb $1,$2
refused	xorb %al,$2
refused	xorb (%ebx),(%ebx)
f0300b	lock xorb %cl,(%ebx)
f0803301	lock xorb $1,(%ebx)
refused	lock xorb (%ebx),%cl
refused	lock xorb %cl,%al
6631c0	xorw %ax,%ax
663103	xorw %ax,(%ebx)
663303	xorw (%ebx),%ax
66310500000000 3:1:sym+0	xorw %ax,sym
66330500000000 3:1:sym+0	xorw sym,%ax
663103	xor %ax,(%ebx)
refused	xor 4((%ebx,%esi,2),%ax
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
66350000 2:20:sym+0	xorw $sym,%ax
6631d8	xorw %bx,%ax
6631c3	xorw %ax,%bx
66311b	xorw %bx,(%ebx)
66331b	xorw (%ebx),%bx
66311d00000000 3:1:sym+0	xorw %bx,sym
66331d00000000 3:1:sym+0	xorw sym,%bx
66311b	xor %bx,(%ebx)
refused	xor 4((%ebx,%esi,2),%bx
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
6681f30000 3:20:sym+0	xorw $sym,%bx
6631f0	xorw %si,%ax
6631c6	xorw %ax,%si
663133	xorw %si,(%ebx)
663333	xorw (%ebx),%si
66313500000000 3:1:sym+0	xorw %si,sym
66333500000000 3:1:sym+0	xorw sym,%si
663133	xor %si,(%ebx)
refused	xor 4((%ebx,%esi,2),%si
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
6681f60000 3:20:sym+0	xorw $sym,%si
66833300	xorw $0,(%ebx)
6683350000000000 3:1:sym+0	xorw $0,sym
6683748dfc00	xorw $0,-4(%ebp,%ecx,4)
66833301	xorw $1,(%ebx)
6683350000000001 3:1:sym+0	xorw $1,sym
6683748dfc01	xorw $1,-4(%ebp,%ecx,4)
668333ff	xorw $-1,(%ebx)
66833500000000ff 3:1:sym+0	xorw $-1,sym
6683748dfcff	xorw $-1,-4(%ebp,%ecx,4)
6683337f	xorw $127,(%ebx)
668335000000007f 3:1:sym+0	xorw $127,sym
6683748dfc7f	xorw $127,-4(%ebp,%ecx,4)
6681338000	xorw $128,(%ebx)
668135000000008000 3:1:sym+0	xorw $128,sym
6681748dfc8000	xorw $128,-4(%ebp,%ecx,4)
66833380	xorw $-128,(%ebx)
6683350000000080 3:1:sym+0	xorw $-128,sym
6683748dfc80	xorw $-128,-4(%ebp,%ecx,4)
6681337fff	xorw $-129,(%ebx)
668135000000007fff 3:1:sym+0	xorw $-129,sym
6681748dfc7fff	xorw $-129,-4(%ebp,%ecx,4)
668133ff00	xorw $255,(%ebx)
66813500000000ff00 3:1:sym+0	xorw $255,sym
6681748dfcff00	xorw $255,-4(%ebp,%ecx,4)
6681330001	xorw $256,(%ebx)
668135000000000001 3:1:sym+0	xorw $256,sym
6681748dfc0001	xorw $256,-4(%ebp,%ecx,4)
668133ff7f	xorw $0x7fff,(%ebx)
66813500000000ff7f 3:1:sym+0	xorw $0x7fff,sym
6681748dfcff7f	xorw $0x7fff,-4(%ebp,%ecx,4)
6681330080	xorw $0x8000,(%ebx)
668135000000000080 3:1:sym+0	xorw $0x8000,sym
6681748dfc0080	xorw $0x8000,-4(%ebp,%ecx,4)
66833380	xorw $0xff80,(%ebx)
6683350000000080 3:1:sym+0	xorw $0xff80,sym
6683748dfc80	xorw $0xff80,-4(%ebp,%ecx,4)
668333ff	xorw $0xffff,(%ebx)
66833500000000ff 3:1:sym+0	xorw $0xffff,sym
6683748dfcff	xorw $0xffff,-4(%ebp,%ecx,4)
6681330000	xorw $0x10000,(%ebx)
668135000000000000 3:1:sym+0	xorw $0x10000,sym
6681748dfc0000	xorw $0x10000,-4(%ebp,%ecx,4)
668133ffff	xorw $0x7fffffff,(%ebx)
66813500000000ffff 3:1:sym+0	xorw $0x7fffffff,sym
6681748dfcffff	xorw $0x7fffffff,-4(%ebp,%ecx,4)
6681330000	xorw $0x80000000,(%ebx)
668135000000000000 3:1:sym+0	xorw $0x80000000,sym
6681748dfc0000	xorw $0x80000000,-4(%ebp,%ecx,4)
66833380	xorw $0xffffff80,(%ebx)
6683350000000080 3:1:sym+0	xorw $0xffffff80,sym
6683748dfc80	xorw $0xffffff80,-4(%ebp,%ecx,4)
668333ff	xorw $0xffffffff,(%ebx)
66833500000000ff 3:1:sym+0	xorw $0xffffffff,sym
6683748dfcff	xorw $0xffffffff,-4(%ebp,%ecx,4)
6681330000 3:20:sym+0	xorw $sym,(%ebx)
668135000000000000 3:1:sym+0 7:20:sym+0	xorw $sym,sym
6681748dfc0000 5:20:sym+0	xorw $sym,-4(%ebp,%ecx,4)
6683f301	xor $1,%bx
refused	xorw $1,$2
refused	xorw %ax,$2
refused	xorw (%ebx),(%ebx)
66f0311b	lock xorw %bx,(%ebx)
66f0833301	lock xorw $1,(%ebx)
refused	lock xorw (%ebx),%bx
refused	lock xorw %bx,%ax
31c0	xorl %eax,%eax
3103	xorl %eax,(%ebx)
3303	xorl (%ebx),%eax
310500000000 2:1:sym+0	xorl %eax,sym
330500000000 2:1:sym+0	xorl sym,%eax
3103	xor %eax,(%ebx)
refused	xor 4((%ebx,%esi,2),%eax
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
3500000000 1:1:sym+0	xorl $sym,%eax
31d8	xorl %ebx,%eax
31c3	xorl %eax,%ebx
311b	xorl %ebx,(%ebx)
331b	xorl (%ebx),%ebx
311d00000000 2:1:sym+0	xorl %ebx,sym
331d00000000 2:1:sym+0	xorl sym,%ebx
311b	xor %ebx,(%ebx)
refused	xor 4((%ebx,%esi,2),%ebx
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
81f300000000 2:1:sym+0	xorl $sym,%ebx
31e0	xorl %esp,%eax
31c4	xorl %eax,%esp
3123	xorl %esp,(%ebx)
3323	xorl (%ebx),%esp
312500000000 2:1:sym+0	xorl %esp,sym
332500000000 2:1:sym+0	xorl sym,%esp
3123	xor %esp,(%ebx)
refused	xor 4((%ebx,%esi,2),%esp
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
81f400000000 2:1:sym+0	xorl $sym,%esp
31e8	xorl %ebp,%eax
31c5	xorl %eax,%ebp
312b	xorl %ebp,(%ebx)
332b	xorl (%ebx),%ebp
312d00000000 2:1:sym+0	xorl %ebp,sym
332d00000000 2:1:sym+0	xorl sym,%ebp
312b	xor %ebp,(%ebx)
refused	xor 4((%ebx,%esi,2),%ebp
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
81f500000000 2:1:sym+0	xorl $sym,%ebp
833300	xorl $0,(%ebx)
83350000000000 2:1:sym+0	xorl $0,sym
83748dfc00	xorl $0,-4(%ebp,%ecx,4)
833301	xorl $1,(%ebx)
83350000000001 2:1:sym+0	xorl $1,sym
83748dfc01	xorl $1,-4(%ebp,%ecx,4)
8333ff	xorl $-1,(%ebx)
833500000000ff 2:1:sym+0	xorl $-1,sym
83748dfcff	xorl $-1,-4(%ebp,%ecx,4)
83337f	xorl $127,(%ebx)
8335000000007f 2:1:sym+0	xorl $127,sym
83748dfc7f	xorl $127,-4(%ebp,%ecx,4)
813380000000	xorl $128,(%ebx)
81350000000080000000 2:1:sym+0	xorl $128,sym
81748dfc80000000	xorl $128,-4(%ebp,%ecx,4)
833380	xorl $-128,(%ebx)
83350000000080 2:1:sym+0	xorl $-128,sym
83748dfc80	xorl $-128,-4(%ebp,%ecx,4)
81337fffffff	xorl $-129,(%ebx)
8135000000007fffffff 2:1:sym+0	xorl $-129,sym
81748dfc7fffffff	xorl $-129,-4(%ebp,%ecx,4)
8133ff000000	xorl $255,(%ebx)
813500000000ff000000 2:1:sym+0	xorl $255,sym
81748dfcff000000	xorl $255,-4(%ebp,%ecx,4)
813300010000	xorl $256,(%ebx)
81350000000000010000 2:1:sym+0	xorl $256,sym
81748dfc00010000	xorl $256,-4(%ebp,%ecx,4)
8133ff7f0000	xorl $0x7fff,(%ebx)
813500000000ff7f0000 2:1:sym+0	xorl $0x7fff,sym
81748dfcff7f0000	xorl $0x7fff,-4(%ebp,%ecx,4)
813300800000	xorl $0x8000,(%ebx)
81350000000000800000 2:1:sym+0	xorl $0x8000,sym
81748dfc00800000	xorl $0x8000,-4(%ebp,%ecx,4)
813380ff0000	xorl $0xff80,(%ebx)
81350000000080ff0000 2:1:sym+0	xorl $0xff80,sym
81748dfc80ff0000	xorl $0xff80,-4(%ebp,%ecx,4)
8133ffff0000	xorl $0xffff,(%ebx)
813500000000ffff0000 2:1:sym+0	xorl $0xffff,sym
81748dfcffff0000	xorl $0xffff,-4(%ebp,%ecx,4)
813300000100	xorl $0x10000,(%ebx)
81350000000000000100 2:1:sym+0	xorl $0x10000,sym
81748dfc00000100	xorl $0x10000,-4(%ebp,%ecx,4)
8133ffffff7f	xorl $0x7fffffff,(%ebx)
813500000000ffffff7f 2:1:sym+0	xorl $0x7fffffff,sym
81748dfcffffff7f	xorl $0x7fffffff,-4(%ebp,%ecx,4)
813300000080	xorl $0x80000000,(%ebx)
81350000000000000080 2:1:sym+0	xorl $0x80000000,sym
81748dfc00000080	xorl $0x80000000,-4(%ebp,%ecx,4)
833380	xorl $0xffffff80,(%ebx)
83350000000080 2:1:sym+0	xorl $0xffffff80,sym
83748dfc80	xorl $0xffffff80,-4(%ebp,%ecx,4)
8333ff	xorl $0xffffffff,(%ebx)
833500000000ff 2:1:sym+0	xorl $0xffffffff,sym
83748dfcff	xorl $0xffffffff,-4(%ebp,%ecx,4)
813300000000 2:1:sym+0	xorl $sym,(%ebx)
81350000000000000000 2:1:sym+0 6:1:sym+0	xorl $sym,sym
81748dfc00000000 4:1:sym+0	xorl $sym,-4(%ebp,%ecx,4)
83f301	xor $1,%ebx
refused	xorl $1,$2
refused	xorl %eax,$2
refused	xorl (%ebx),(%ebx)
f0311b	lock xorl %ebx,(%ebx)
f0833301	lock xorl $1,(%ebx)
refused	lock xorl (%ebx),%ebx
refused	lock xorl %ebx,%eax
38c0	cmpb %al,%al
3803	cmpb %al,(%ebx)
3a03	cmpb (%ebx),%al
380500000000 2:1:sym+0	cmpb %al,sym
3a0500000000 2:1:sym+0	cmpb sym,%al
3803	cmp %al,(%ebx)
refused	cmp 4((%ebx,%esi,2),%al
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
3c00 1:22:sym+0	cmpb $sym,%al
38c8	cmpb %cl,%al
38c1	cmpb %al,%cl
380b	cmpb %cl,(%ebx)
3a0b	cmpb (%ebx),%cl
380d00000000 2:1:sym+0	cmpb %cl,sym
3a0d00000000 2:1:sym+0	cmpb sym,%cl
380b	cmp %cl,(%ebx)
refused	cmp 4((%ebx,%esi,2),%cl
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
80f900 2:22:sym+0	cmpb $sym,%cl
38f8	cmpb %bh,%al
38c7	cmpb %al,%bh
383b	cmpb %bh,(%ebx)
3a3b	cmpb (%ebx),%bh
383d00000000 2:1:sym+0	cmpb %bh,sym
3a3d00000000 2:1:sym+0	cmpb sym,%bh
383b	cmp %bh,(%ebx)
refused	cmp 4((%ebx,%esi,2),%bh
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
80ff00 2:22:sym+0	cmpb $sym,%bh
803b00	cmpb $0,(%ebx)
803d0000000000 2:1:sym+0	cmpb $0,sym
807c8dfc00	cmpb $0,-4(%ebp,%ecx,4)
803b01	cmpb $1,(%ebx)
803d0000000001 2:1:sym+0	cmpb $1,sym
807c8dfc01	cmpb $1,-4(%ebp,%ecx,4)
803bff	cmpb $-1,(%ebx)
803d00000000ff 2:1:sym+0	cmpb $-1,sym
807c8dfcff	cmpb $-1,-4(%ebp,%ecx,4)
803b7f	cmpb $127,(%ebx)
803d000000007f 2:1:sym+0	cmpb $127,sym
807c8dfc7f	cmpb $127,-4(%ebp,%ecx,4)
803b80	cmpb $128,(%ebx)
803d0000000080 2:1:sym+0	cmpb $128,sym
807c8dfc80	cmpb $128,-4(%ebp,%ecx,4)
803b80	cmpb $-128,(%ebx)
803d0000000080 2:1:sym+0	cmpb $-128,sym
807c8dfc80	cmpb $-128,-4(%ebp,%ecx,4)
803b7f	cmpb $-129,(%ebx)
803d000000007f 2:1:sym+0	cmpb $-129,sym
807c8dfc7f	cmpb $-129,-4(%ebp,%ecx,4)
803bff	cmpb $255,(%ebx)
803d00000000ff 2:1:sym+0	cmpb $255,sym
807c8dfcff	cmpb $255,-4(%ebp,%ecx,4)
803b00	cmpb $256,(%ebx)
803d0000000000 2:1:sym+0	cmpb $256,sym
807c8dfc00	cmpb $256,-4(%ebp,%ecx,4)
803bff	cmpb $0x7fff,(%ebx)
803d00000000ff 2:1:sym+0	cmpb $0x7fff,sym
807c8dfcff	cmpb $0x7fff,-4(%ebp,%ecx,4)
803b00	cmpb $0x8000,(%ebx)
803d0000000000 2:1:sym+0	cmpb $0x8000,sym
807c8dfc00	cmpb $0x8000,-4(%ebp,%ecx,4)
803b80	cmpb $0xff80,(%ebx)
803d0000000080 2:1:sym+0	cmpb $0xff80,sym
807c8dfc80	cmpb $0xff80,-4(%ebp,%ecx,4)
803bff	cmpb $0xffff,(%ebx)
803d00000000ff 2:1:sym+0	cmpb $0xffff,sym
807c8dfcff	cmpb $0xffff,-4(%ebp,%ecx,4)
803b00	cmpb $0x10000,(%ebx)
803d0000000000 2:1:sym+0	cmpb $0x10000,sym
807c8dfc00	cmpb $0x10000,-4(%ebp,%ecx,4)
803bff	cmpb $0x7fffffff,(%ebx)
803d00000000ff 2:1:sym+0	cmpb $0x7fffffff,sym
807c8dfcff	cmpb $0x7fffffff,-4(%ebp,%ecx,4)
803b00	cmpb $0x80000000,(%ebx)
803d0000000000 2:1:sym+0	cmpb $0x80000000,sym
807c8dfc00	cmpb $0x80000000,-4(%ebp,%ecx,4)
803b80	cmpb $0xffffff80,(%ebx)
803d0000000080 2:1:sym+0	cmpb $0xffffff80,sym
807c8dfc80	cmpb $0xffffff80,-4(%ebp,%ecx,4)
803bff	cmpb $0xffffffff,(%ebx)
803d00000000ff 2:1:sym+0	cmpb $0xffffffff,sym
807c8dfcff	cmpb $0xffffffff,-4(%ebp,%ecx,4)
803b00 2:22:sym+0	cmpb $sym,(%ebx)
803d0000000000 2:1:sym+0 6:22:sym+0	cmpb $sym,sym
807c8dfc00 4:22:sym+0	cmpb $sym,-4(%ebp,%ecx,4)
80f901	cmp $1,%cl
833b01	cmp $1,(%ebx)
refused	cmpb $1,$2
refused	cmpb %al,$2
refused	cmpb (%ebx),(%ebx)
refused	lock cmpb %cl,(%ebx)
refused	lock cmpb $1,(%ebx)
refused	lock cmpb (%ebx),%cl
refused	lock cmpb %cl,%al
6639c0	cmpw %ax,%ax
663903	cmpw %ax,(%ebx)
663b03	cmpw (%ebx),%ax
66390500000000 3:1:sym+0	cmpw %ax,sym
663b0500000000 3:1:sym+0	cmpw sym,%ax
663903	cmp %ax,(%ebx)
refused	cmp 4((%ebx,%esi,2),%ax
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
663d0000 2:20:sym+0	cmpw $sym,%ax
6639d8	cmpw %bx,%ax
6639c3	cmpw %ax,%bx
66391b	cmpw %bx,(%ebx)
663b1b	cmpw (%ebx),%bx
66391d00000000 3:1:sym+0	cmpw %bx,sym
663b1d00000000 3:1:sym+0	cmpw sym,%bx
66391b	cmp %bx,(%ebx)
refused	cmp 4((%ebx,%esi,2),%bx
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
6681fb0000 3:20:sym+0	cmpw $sym,%bx
6639f0	cmpw %si,%ax
6639c6	cmpw %ax,%si
663933	cmpw %si,(%ebx)
663b33	cmpw (%ebx),%si
66393500000000 3:1:sym+0	cmpw %si,sym
663b3500000000 3:1:sym+0	cmpw sym,%si
663933	cmp %si,(%ebx)
refused	cmp 4((%ebx,%esi,2),%si
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
6681fe0000 3:20:sym+0	cmpw $sym,%si
66833b00	cmpw $0,(%ebx)
66833d0000000000 3:1:sym+0	cmpw $0,sym
66837c8dfc00	cmpw $0,-4(%ebp,%ecx,4)
66833b01	cmpw $1,(%ebx)
66833d0000000001 3:1:sym+0	cmpw $1,sym
66837c8dfc01	cmpw $1,-4(%ebp,%ecx,4)
66833bff	cmpw $-1,(%ebx)
66833d00000000ff 3:1:sym+0	cmpw $-1,sym
66837c8dfcff	cmpw $-1,-4(%ebp,%ecx,4)
66833b7f	cmpw $127,(%ebx)
66833d000000007f 3:1:sym+0	cmpw $127,sym
66837c8dfc7f	cmpw $127,-4(%ebp,%ecx,4)
66813b8000	cmpw $128,(%ebx)
66813d000000008000 3:1:sym+0	cmpw $128,sym
66817c8dfc8000	cmpw $128,-4(%ebp,%ecx,4)
66833b80	cmpw $-128,(%ebx)
66833d0000000080 3:1:sym+0	cmpw $-128,sym
66837c8dfc80	cmpw $-128,-4(%ebp,%ecx,4)
66813b7fff	cmpw $-129,(%ebx)
66813d000000007fff 3:1:sym+0	cmpw $-129,sym
66817c8dfc7fff	cmpw $-129,-4(%ebp,%ecx,4)
66813bff00	cmpw $255,(%ebx)
66813d00000000ff00 3:1:sym+0	cmpw $255,sym
66817c8dfcff00	cmpw $255,-4(%ebp,%ecx,4)
66813b0001	cmpw $256,(%ebx)
66813d000000000001 3:1:sym+0	cmpw $256,sym
66817c8dfc0001	cmpw $256,-4(%ebp,%ecx,4)
66813bff7f	cmpw $0x7fff,(%ebx)
66813d00000000ff7f 3:1:sym+0	cmpw $0x7fff,sym
66817c8dfcff7f	cmpw $0x7fff,-4(%ebp,%ecx,4)
66813b0080	cmpw $0x8000,(%ebx)
66813d000000000080 3:1:sym+0	cmpw $0x8000,sym
66817c8dfc0080	cmpw $0x8000,-4(%ebp,%ecx,4)
66833b80	cmpw $0xff80,(%ebx)
66833d0000000080 3:1:sym+0	cmpw $0xff80,sym
66837c8dfc80	cmpw $0xff80,-4(%ebp,%ecx,4)
66833bff	cmpw $0xffff,(%ebx)
66833d00000000ff 3:1:sym+0	cmpw $0xffff,sym
66837c8dfcff	cmpw $0xffff,-4(%ebp,%ecx,4)
66813b0000	cmpw $0x10000,(%ebx)
66813d000000000000 3:1:sym+0	cmpw $0x10000,sym
66817c8dfc0000	cmpw $0x10000,-4(%ebp,%ecx,4)
66813bffff	cmpw $0x7fffffff,(%ebx)
66813d00000000ffff 3:1:sym+0	cmpw $0x7fffffff,sym
66817c8dfcffff	cmpw $0x7fffffff,-4(%ebp,%ecx,4)
66813b0000	cmpw $0x80000000,(%ebx)
66813d000000000000 3:1:sym+0	cmpw $0x80000000,sym
66817c8dfc0000	cmpw $0x80000000,-4(%ebp,%ecx,4)
66833b80	cmpw $0xffffff80,(%ebx)
66833d0000000080 3:1:sym+0	cmpw $0xffffff80,sym
66837c8dfc80	cmpw $0xffffff80,-4(%ebp,%ecx,4)
66833bff	cmpw $0xffffffff,(%ebx)
66833d00000000ff 3:1:sym+0	cmpw $0xffffffff,sym
66837c8dfcff	cmpw $0xffffffff,-4(%ebp,%ecx,4)
66813b0000 3:20:sym+0	cmpw $sym,(%ebx)
66813d000000000000 3:1:sym+0 7:20:sym+0	cmpw $sym,sym
66817c8dfc0000 5:20:sym+0	cmpw $sym,-4(%ebp,%ecx,4)
6683fb01	cmp $1,%bx
refused	cmpw $1,$2
refused	cmpw %ax,$2
refused	cmpw (%ebx),(%ebx)
refused	lock cmpw %bx,(%ebx)
refused	lock cmpw $1,(%ebx)
refused	lock cmpw (%ebx),%bx
refused	lock cmpw %bx,%ax
39c0	cmpl %eax,%eax
3903	cmpl %eax,(%ebx)
3b03	cmpl (%ebx),%eax
390500000000 2:1:sym+0	cmpl %eax,sym
3b0500000000 2:1:sym+0	cmpl sym,%eax
3903	cmp %eax,(%ebx)
refused	cmp 4((%ebx,%esi,2),%eax
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
3d00000000 1:1:sym+0	cmpl $sym,%eax
39d8	cmpl %ebx,%eax
39c3	cmpl %eax,%ebx
391b	cmpl %ebx,(%ebx)
3b1b	cmpl (%ebx),%ebx
391d00000000 2:1:sym+0	cmpl %ebx,sym
3b1d00000000 2:1:sym+0	cmpl sym,%ebx
391b	cmp %ebx,(%ebx)
refused	cmp 4((%ebx,%esi,2),%ebx
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
81fb00000000 2:1:sym+0	cmpl $sym,%ebx
39e0	cmpl %esp,%eax
39c4	cmpl %eax,%esp
3923	cmpl %esp,(%ebx)
3b23	cmpl (%ebx),%esp
392500000000 2:1:sym+0	cmpl %esp,sym
3b2500000000 2:1:sym+0	cmpl sym,%esp
3923	cmp %esp,(%ebx)
refused	cmp 4((%ebx,%esi,2),%esp
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
81fc00000000 2:1:sym+0	cmpl $sym,%esp
39e8	cmpl %ebp,%eax
39c5	cmpl %eax,%ebp
392b	cmpl %ebp,(%ebx)
3b2b	cmpl (%ebx),%ebp
392d00000000 2:1:sym+0	cmpl %ebp,sym
3b2d00000000 2:1:sym+0	cmpl sym,%ebp
392b	cmp %ebp,(%ebx)
refused	cmp 4((%ebx,%esi,2),%ebp
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
81fd00000000 2:1:sym+0	cmpl $sym,%ebp
833b00	cmpl $0,(%ebx)
833d0000000000 2:1:sym+0	cmpl $0,sym
837c8dfc00	cmpl $0,-4(%ebp,%ecx,4)
833b01	cmpl $1,(%ebx)
833d0000000001 2:1:sym+0	cmpl $1,sym
837c8dfc01	cmpl $1,-4(%ebp,%ecx,4)
833bff	cmpl $-1,(%ebx)
833d00000000ff 2:1:sym+0	cmpl $-1,sym
837c8dfcff	cmpl $-1,-4(%ebp,%ecx,4)
833b7f	cmpl $127,(%ebx)
833d000000007f 2:1:sym+0	cmpl $127,sym
837c8dfc7f	cmpl $127,-4(%ebp,%ecx,4)
813b80000000	cmpl $128,(%ebx)
813d0000000080000000 2:1:sym+0	cmpl $128,sym
817c8dfc80000000	cmpl $128,-4(%ebp,%ecx,4)
833b80	cmpl $-128,(%ebx)
833d0000000080 2:1:sym+0	cmpl $-128,sym
837c8dfc80	cmpl $-128,-4(%ebp,%ecx,4)
813b7fffffff	cmpl $-129,(%ebx)
813d000000007fffffff 2:1:sym+0	cmpl $-129,sym
817c8dfc7fffffff	cmpl $-129,-4(%ebp,%ecx,4)
813bff000000	cmpl $255,(%ebx)
813d00000000ff000000 2:1:sym+0	cmpl $255,sym
817c8dfcff000000	cmpl $255,-4(%ebp,%ecx,4)
813b00010000	cmpl $256,(%ebx)
813d0000000000010000 2:1:sym+0	cmpl $256,sym
817c8dfc00010000	cmpl $256,-4(%ebp,%ecx,4)
813bff7f0000	cmpl $0x7fff,(%ebx)
813d00000000ff7f0000 2:1:sym+0	cmpl $0x7fff,sym
817c8dfcff7f0000	cmpl $0x7fff,-4(%ebp,%ecx,4)
813b00800000	cmpl $0x8000,(%ebx)
813d0000000000800000 2:1:sym+0	cmpl $0x8000,sym
817c8dfc00800000	cmpl $0x8000,-4(%ebp,%ecx,4)
813b80ff0000	cmpl $0xff80,(%ebx)
813d0000000080ff0000 2:1:sym+0	cmpl $0xff80,sym
817c8dfc80ff0000	cmpl $0xff80,-4(%ebp,%ecx,4)
813bffff0000	cmpl $0xffff,(%ebx)
813d00000000ffff0000 2:1:sym+0	cmpl $0xffff,sym
817c8dfcffff0000	cmpl $0xffff,-4(%ebp,%ecx,4)
813b00000100	cmpl $0x10000,(%ebx)
813d0000000000000100 2:1:sym+0	cmpl $0x10000,sym
817c8dfc00000100	cmpl $0x10000,-4(%ebp,%ecx,4)
813bffffff7f	cmpl $0x7fffffff,(%ebx)
813d00000000ffffff7f 2:1:sym+0	cmpl $0x7fffffff,sym
817c8dfcffffff7f	cmpl $0x7fffffff,-4(%ebp,%ecx,4)
813b00000080	cmpl $0x80000000,(%ebx)
813d0000000000000080 2:1:sym+0	cmpl $0x80000000,sym
817c8dfc00000080	cmpl $0x80000000,-4(%ebp,%ecx,4)
833b80	cmpl $0xffffff80,(%ebx)
833d0000000080 2:1:sym+0	cmpl $0xffffff80,sym
837c8dfc80	cmpl $0xffffff80,-4(%ebp,%ecx,4)
833bff	cmpl $0xffffffff,(%ebx)
833d00000000ff 2:1:sym+0	cmpl $0xffffffff,sym
837c8dfcff	cmpl $0xffffffff,-4(%ebp,%ecx,4)
813b00000000 2:1:sym+0	cmpl $sym,(%ebx)
813d0000000000000000 2:1:sym+0 6:1:sym+0	cmpl $sym,sym
817c8dfc00000000 4:1:sym+0	cmpl $sym,-4(%ebp,%ecx,4)
83fb01	cmp $1,%ebx
refused	cmpl $1,$2
refused	cmpl %eax,$2
refused	cmpl (%ebx),(%ebx)
refused	lock cmpl %ebx,(%ebx)
refused	lock cmpl $1,(%ebx)
refused	lock cmpl (%ebx),%ebx
refused	lock cmpl %ebx,%eax
84c0	testb %al,%al
8403	testb %al,(%ebx)
8403	testb (%ebx),%al
840500000000 2:1:sym+0	testb %al,sym
840500000000 2:1:sym+0	testb sym,%al
8403	test %al,(%ebx)
refused	test 4((%ebx,%esi,2),%al
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
a800 1:22:sym+0	testb $sym,%al
84c8	testb %cl,%al
84c1	testb %al,%cl
840b	testb %cl,(%ebx)
840b	testb (%ebx),%cl
840d00000000 2:1:sym+0	testb %cl,sym
840d00000000 2:1:sym+0	testb sym,%cl
840b	test %cl,(%ebx)
refused	test 4((%ebx,%esi,2),%cl
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
f6c100 2:22:sym+0	testb $sym,%cl
84f8	testb %bh,%al
84c7	testb %al,%bh
843b	testb %bh,(%ebx)
843b	testb (%ebx),%bh
843d00000000 2:1:sym+0	testb %bh,sym
843d00000000 2:1:sym+0	testb sym,%bh
843b	test %bh,(%ebx)
refused	test 4((%ebx,%esi,2),%bh
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
f6c700 2:22:sym+0	testb $sym,%bh
f60300	testb $0,(%ebx)
f6050000000000 2:1:sym+0	testb $0,sym
f6448dfc00	testb $0,-4(%ebp,%ecx,4)
f60301	testb $1,(%ebx)
f6050000000001 2:1:sym+0	testb $1,sym
f6448dfc01	testb $1,-4(%ebp,%ecx,4)
f603ff	testb $-1,(%ebx)
f60500000000ff 2:1:sym+0	testb $-1,sym
f6448dfcff	testb $-1,-4(%ebp,%ecx,4)
f6037f	testb $127,(%ebx)
f605000000007f 2:1:sym+0	testb $127,sym
f6448dfc7f	testb $127,-4(%ebp,%ecx,4)
f60380	testb $128,(%ebx)
f6050000000080 2:1:sym+0	testb $128,sym
f6448dfc80	testb $128,-4(%ebp,%ecx,4)
f60380	testb $-128,(%ebx)
f6050000000080 2:1:sym+0	testb $-128,sym
f6448dfc80	testb $-128,-4(%ebp,%ecx,4)
f6037f	testb $-129,(%ebx)
f605000000007f 2:1:sym+0	testb $-129,sym
f6448dfc7f	testb $-129,-4(%ebp,%ecx,4)
f603ff	testb $255,(%ebx)
f60500000000ff 2:1:sym+0	testb $255,sym
f6448dfcff	testb $255,-4(%ebp,%ecx,4)
f60300	testb $256,(%ebx)
f6050000000000 2:1:sym+0	testb $256,sym
f6448dfc00	testb $256,-4(%ebp,%ecx,4)
f603ff	testb $0x7fff,(%ebx)
f60500000000ff 2:1:sym+0	testb $0x7fff,sym
f6448dfcff	testb $0x7fff,-4(%ebp,%ecx,4)
f60300	testb $0x8000,(%ebx)
f6050000000000 2:1:sym+0	testb $0x8000,sym
f6448dfc00	testb $0x8000,-4(%ebp,%ecx,4)
f60380	testb $0xff80,(%ebx)
f6050000000080 2:1:sym+0	testb $0xff80,sym
f6448dfc80	testb $0xff80,-4(%ebp,%ecx,4)
f603ff	testb $0xffff,(%ebx)
f60500000000ff 2:1:sym+0	testb $0xffff,sym
f6448dfcff	testb $0xffff,-4(%ebp,%ecx,4)
f60300	testb $0x10000,(%ebx)
f6050000000000 2:1:sym+0	testb $0x10000,sym
f6448dfc00	testb $0x10000,-4(%ebp,%ecx,4)
f603ff	testb $0x7fffffff,(%ebx)
f60500000000ff 2:1:sym+0	testb $0x7fffffff,sym
f6448dfcff	testb $0x7fffffff,-4(%ebp,%ecx,4)
f60300	testb $0x80000000,(%ebx)
f6050000000000 2:1:sym+0	testb $0x80000000,sym
f6448dfc00	testb $0x80000000,-4(%ebp,%ecx,4)
f60380	testb $0xffffff80,(%ebx)
f6050000000080 2:1:sym+0	testb $0xffffff80,sym
f6448dfc80	testb $0xffffff80,-4(%ebp,%ecx,4)
f603ff	testb $0xffffffff,(%ebx)
f60500000000ff 2:1:sym+0	testb $0xffffffff,sym
f6448dfcff	testb $0xffffffff,-4(%ebp,%ecx,4)
f60300 2:22:sym+0	testb $sym,(%ebx)
f6050000000000 2:1:sym+0 6:22:sym+0	testb $sym,sym
f6448dfc00 4:22:sym+0	testb $sym,-4(%ebp,%ecx,4)
f6c101	test $1,%cl
f70301000000	test $1,(%ebx)
refused	testb $1,$2
refused	testb %al,$2
refused	testb (%ebx),(%ebx)
refused	lock testb %cl,(%ebx)
refused	lock testb $1,(%ebx)
refused	lock testb (%ebx),%cl
refused	lock testb %cl,%al
6685c0	testw %ax,%ax
668503	testw %ax,(%ebx)
668503	testw (%ebx),%ax
66850500000000 3:1:sym+0	testw %ax,sym
66850500000000 3:1:sym+0	testw sym,%ax
668503	test %ax,(%ebx)
refused	test 4((%ebx,%esi,2),%ax
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
66a90000 2:20:sym+0	testw $sym,%ax
6685d8	testw %bx,%ax
6685c3	testw %ax,%bx
66851b	testw %bx,(%ebx)
66851b	testw (%ebx),%bx
66851d00000000 3:1:sym+0	testw %bx,sym
66851d00000000 3:1:sym+0	testw sym,%bx
66851b	test %bx,(%ebx)
refused	test 4((%ebx,%esi,2),%bx
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
66f7c30000 3:20:sym+0	testw $sym,%bx
6685f0	testw %si,%ax
6685c6	testw %ax,%si
668533	testw %si,(%ebx)
668533	testw (%ebx),%si
66853500000000 3:1:sym+0	testw %si,sym
66853500000000 3:1:sym+0	testw sym,%si
668533	test %si,(%ebx)
refused	test 4((%ebx,%esi,2),%si
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
66f7c60000 3:20:sym+0	testw $sym,%si
66f7030000	testw $0,(%ebx)
66f705000000000000 3:1:sym+0	testw $0,sym
66f7448dfc0000	testw $0,-4(%ebp,%ecx,4)
66f7030100	testw $1,(%ebx)
66f705000000000100 3:1:sym+0	testw $1,sym
66f7448dfc0100	testw $1,-4(%ebp,%ecx,4)
66f703ffff	testw $-1,(%ebx)
66f70500000000ffff 3:1:sym+0	testw $-1,sym
66f7448dfcffff	testw $-1,-4(%ebp,%ecx,4)
66f7037f00	testw $127,(%ebx)
66f705000000007f00 3:1:sym+0	testw $127,sym
66f7448dfc7f00	testw $127,-4(%ebp,%ecx,4)
66f7038000	testw $128,(%ebx)
66f705000000008000 3:1:sym+0	testw $128,sym
66f7448dfc8000	testw $128,-4(%ebp,%ecx,4)
66f70380ff	testw $-128,(%ebx)
66f7050000000080ff 3:1:sym+0	testw $-128,sym
66f7448dfc80ff	testw $-128,-4(%ebp,%ecx,4)
66f7037fff	testw $-129,(%ebx)
66f705000000007fff 3:1:sym+0	testw $-129,sym
66f7448dfc7fff	testw $-129,-4(%ebp,%ecx,4)
66f703ff00	testw $255,(%ebx)
66f70500000000ff00 3:1:sym+0	testw $255,sym
66f7448dfcff00	testw $255,-4(%ebp,%ecx,4)
66f7030001	testw $256,(%ebx)
66f705000000000001 3:1:sym+0	testw $256,sym
66f7448dfc0001	testw $256,-4(%ebp,%ecx,4)
66f703ff7f	testw $0x7fff,(%ebx)
66f70500000000ff7f 3:1:sym+0	testw $0x7fff,sym
66f7448dfcff7f	testw $0x7fff,-4(%ebp,%ecx,4)
66f7030080	testw $0x8000,(%ebx)
66f705000000000080 3:1:sym+0	testw $0x8000,sym
66f7448dfc0080	testw $0x8000,-4(%ebp,%ecx,4)
66f70380ff	testw $0xff80,(%ebx)
66f7050000000080ff 3:1:sym+0	testw $0xff80,sym
66f7448dfc80ff	testw $0xff80,-4(%ebp,%ecx,4)
66f703ffff	testw $0xffff,(%ebx)
66f70500000000ffff 3:1:sym+0	testw $0xffff,sym
66f7448dfcffff	testw $0xffff,-4(%ebp,%ecx,4)
66f7030000	testw $0x10000,(%ebx)
66f705000000000000 3:1:sym+0	testw $0x10000,sym
66f7448dfc0000	testw $0x10000,-4(%ebp,%ecx,4)
66f703ffff	testw $0x7fffffff,(%ebx)
66f70500000000ffff 3:1:sym+0	testw $0x7fffffff,sym
66f7448dfcffff	testw $0x7fffffff,-4(%ebp,%ecx,4)
66f7030000	testw $0x80000000,(%ebx)
66f705000000000000 3:1:sym+0	testw $0x80000000,sym
66f7448dfc0000	testw $0x80000000,-4(%ebp,%ecx,4)
66f70380ff	testw $0xffffff80,(%ebx)
66f7050000000080ff 3:1:sym+0	testw $0xffffff80,sym
66f7448dfc80ff	testw $0xffffff80,-4(%ebp,%ecx,4)
66f703ffff	testw $0xffffffff,(%ebx)
66f70500000000ffff 3:1:sym+0	testw $0xffffffff,sym
66f7448dfcffff	testw $0xffffffff,-4(%ebp,%ecx,4)
66f7030000 3:20:sym+0	testw $sym,(%ebx)
66f705000000000000 3:1:sym+0 7:20:sym+0	testw $sym,sym
66f7448dfc0000 5:20:sym+0	testw $sym,-4(%ebp,%ecx,4)
66f7c30100	test $1,%bx
refused	testw $1,$2
refused	testw %ax,$2
refused	testw (%ebx),(%ebx)
refused	lock testw %bx,(%ebx)
refused	lock testw $1,(%ebx)
refused	lock testw (%ebx),%bx
refused	lock testw %bx,%ax
85c0	testl %eax,%eax
8503	testl %eax,(%ebx)
8503	testl (%ebx),%eax
850500000000 2:1:sym+0	testl %eax,sym
850500000000 2:1:sym+0	testl sym,%eax
8503	test %eax,(%ebx)
refused	test 4((%ebx,%esi,2),%eax
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
85d8	testl %ebx,%eax
85c3	testl %eax,%ebx
851b	testl %ebx,(%ebx)
851b	testl (%ebx),%ebx
851d00000000 2:1:sym+0	testl %ebx,sym
851d00000000 2:1:sym+0	testl sym,%ebx
851b	test %ebx,(%ebx)
refused	test 4((%ebx,%esi,2),%ebx
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
85e0	testl %esp,%eax
85c4	testl %eax,%esp
8523	testl %esp,(%ebx)
8523	testl (%ebx),%esp
852500000000 2:1:sym+0	testl %esp,sym
852500000000 2:1:sym+0	testl sym,%esp
8523	test %esp,(%ebx)
refused	test 4((%ebx,%esi,2),%esp
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
f7c400000000 2:1:sym+0	testl $sym,%esp
85e8	testl %ebp,%eax
85c5	testl %eax,%ebp
852b	testl %ebp,(%ebx)
852b	testl (%ebx),%ebp
852d00000000 2:1:sym+0	testl %ebp,sym
852d00000000 2:1:sym+0	testl sym,%ebp
852b	test %ebp,(%ebx)
refused	test 4((%ebx,%esi,2),%ebp
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
f7c500000000 2:1:sym+0	testl $sym,%ebp
f70300000000	testl $0,(%ebx)
f7050000000000000000 2:1:sym+0	testl $0,sym
f7448dfc00000000	testl $0,-4(%ebp,%ecx,4)
f70301000000	testl $1,(%ebx)
f7050000000001000000 2:1:sym+0	testl $1,sym
f7448dfc01000000	testl $1,-4(%ebp,%ecx,4)
f703ffffffff	testl $-1,(%ebx)
f70500000000ffffffff 2:1:sym+0	testl $-1,sym
f7448dfcffffffff	testl $-1,-4(%ebp,%ecx,4)
f7037f000000	testl $127,(%ebx)
f705000000007f000000 2:1:sym+0	testl $127,sym
f7448dfc7f000000	testl $127,-4(%ebp,%ecx,4)
f70380000000	testl $128,(%ebx)
f7050000000080000000 2:1:sym+0	testl $128,sym
f7448dfc80000000	testl $128,-4(%ebp,%ecx,4)
f70380ffffff	testl $-128,(%ebx)
f7050000000080ffffff 2:1:sym+0	testl $-128,sym
f7448dfc80ffffff	testl $-128,-4(%ebp,%ecx,4)
f7037fffffff	testl $-129,(%ebx)
f705000000007fffffff 2:1:sym+0	testl $-129,sym
f7448dfc7fffffff	testl $-129,-4(%ebp,%ecx,4)
f703ff000000	testl $255,(%ebx)
f70500000000ff000000 2:1:sym+0	testl $255,sym
f7448dfcff000000	testl $255,-4(%ebp,%ecx,4)
f70300010000	testl $256,(%ebx)
f7050000000000010000 2:1:sym+0	testl $256,sym
f7448dfc00010000	testl $256,-4(%ebp,%ecx,4)
f703ff7f0000	testl $0x7fff,(%ebx)
f70500000000ff7f0000 2:1:sym+0	testl $0x7fff,sym
f7448dfcff7f0000	testl $0x7fff,-4(%ebp,%ecx,4)
f70300800000	testl $0x8000,(%ebx)
f7050000000000800000 2:1:sym+0	testl $0x8000,sym
f7448dfc00800000	testl $0x8000,-4(%ebp,%ecx,4)
f70380ff0000	testl $0xff80,(%ebx)
f7050000000080ff0000 2:1:sym+0	testl $0xff80,sym
f7448dfc80ff0000	testl $0xff80,-4(%ebp,%ecx,4)
f703ffff0000	testl $0xffff,(%ebx)
f70500000000ffff0000 2:1:sym+0	testl $0xffff,sym
f7448dfcffff0000	testl $0xffff,-4(%ebp,%ecx,4)
f70300000100	testl $0x10000,(%ebx)
f7050000000000000100 2:1:sym+0	testl $0x10000,sym
f7448dfc00000100	testl $0x10000,-4(%ebp,%ecx,4)
f703ffffff7f	testl $0x7fffffff,(%ebx)
f70500000000ffffff7f 2:1:sym+0	testl $0x7fffffff,sym
f7448dfcffffff7f	testl $0x7fffffff,-4(%ebp,%ecx,4)
f70300000080	testl $0x80000000,(%ebx)
f7050000000000000080 2:1:sym+0	testl $0x80000000,sym
f7448dfc00000080	testl $0x80000000,-4(%ebp,%ecx,4)
f70380ffffff	testl $0xffffff80,(%ebx)
f7050000000080ffffff 2:1:sym+0	testl $0xffffff80,sym
f7448dfc80ffffff	testl $0xffffff80,-4(%ebp,%ecx,4)
f703ffffffff	testl $0xffffffff,(%ebx)
f70500000000ffffffff 2:1:sym+0	testl $0xffffffff,sym
f7448dfcffffffff	testl $0xffffffff,-4(%ebp,%ecx,4)
f70300000000 2:1:sym+0	testl $sym,(%ebx)
f7050000000000000000 2:1:sym+0 6:1:sym+0	testl $sym,sym
f7448dfc00000000 4:1:sym+0	testl $sym,-4(%ebp,%ecx,4)
f7c301000000	test $1,%ebx
refused	testl $1,$2
refused	testl %eax,$2
refused	testl (%ebx),(%ebx)
refused	lock testl %ebx,(%ebx)
refused	lock testl $1,(%ebx)
refused	lock testl (%ebx),%ebx
refused	lock testl %ebx,%eax
8803	movb %al,(%ebx)
8a03	movb (%ebx),%al
8803	mov %al,(%ebx)
refused	mov 4((%ebx,%esi,2),%al
b000	movb $0,%al
b07f	movb $127,%al
b080	movb $128,%al
b0ff	movb $0x7fff,%al
b000	movb $0x8000,%al
b080	movb $0xff80,%al
b0ff	movb $0xffff,%al
b000	movb $0x10000,%al
b0ff	movb $0x7fffffff,%al
b000	movb $0x80000000,%al
b080	movb $0xffffff80,%al
b0ff	movb $0xffffffff,%al
880b	movb %cl,(%ebx)
8a0b	movb (%ebx),%cl
880b	mov %cl,(%ebx)
refused	mov 4((%ebx,%esi,2),%cl
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
883b	movb %bh,(%ebx)
8a3b	movb (%ebx),%bh
883d00000000 2:1:sym+0	movb %bh,sym
8a3d00000000 2:1:sym+0	movb sym,%bh
883b	mov %bh,(%ebx)
refused	mov 4((%ebx,%esi,2),%bh
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
b700 1:22:sym+0	movb $sym,%bh
c60300	movb $0,(%ebx)
c6050000000000 2:1:sym+0	movb $0,sym
c6448dfc00	movb $0,-4(%ebp,%ecx,4)
c60301	movb $1,(%ebx)
c6050000000001 2:1:sym+0	movb $1,sym
c6448dfc01	movb $1,-4(%ebp,%ecx,4)
c603ff	movb $-1,(%ebx)
c60500000000ff 2:1:sym+0	movb $-1,sym
c6448dfcff	movb $-1,-4(%ebp,%ecx,4)
c6037f	movb $127,(%ebx)
c605000000007f 2:1:sym+0	movb $127,sym
c6448dfc7f	movb $127,-4(%ebp,%ecx,4)
c60380	movb $128,(%ebx)
c6050000000080 2:1:sym+0	movb $128,sym
c6448dfc80	movb $128,-4(%ebp,%ecx,4)
c60380	movb $-128,(%ebx)
c6050000000080 2:1:sym+0	movb $-128,sym
c6448dfc80	movb $-128,-4(%ebp,%ecx,4)
c6037f	movb $-129,(%ebx)
c605000000007f 2:1:sym+0	movb $-129,sym
c6448dfc7f	movb $-129,-4(%ebp,%ecx,4)
c603ff	movb $255,(%ebx)
c60500000000ff 2:1:sym+0	movb $255,sym
c6448dfcff	movb $255,-4(%ebp,%ecx,4)
c60300	movb $256,(%ebx)
c6050000000000 2:1:sym+0	movb $256,sym
c6448dfc00	movb $256,-4(%ebp,%ecx,4)
c603ff	movb $0x7fff,(%ebx)
c60500000000ff 2:1:sym+0	movb $0x7fff,sym
c6448dfcff	movb $0x7fff,-4(%ebp,%ecx,4)
c60300	movb $0x8000,(%ebx)
c6050000000000 2:1:sym+0	movb $0x8000,sym
c6448dfc00	movb $0x8000,-4(%ebp,%ecx,4)
c60380	movb $0xff80,(%ebx)
c6050000000080 2:1:sym+0	movb $0xff80,sym
c6448dfc80	movb $0xff80,-4(%ebp,%ecx,4)
c603ff	movb $0xffff,(%ebx)
c60500000000ff 2:1:sym+0	movb $0xffff,sym
c6448dfcff	movb $0xffff,-4(%ebp,%ecx,4)
c60300	movb $0x10000,(%ebx)
c6050000000000 2:1:sym+0	movb $0x10000,sym
c6448dfc00	movb $0x10000,-4(%ebp,%ecx,4)
c603ff	movb $0x7fffffff,(%ebx)
c60500000000ff 2:1:sym+0	movb $0x7fffffff,sym
c6448dfcff	movb $0x7fffffff,-4(%ebp,%ecx,4)
c60300	movb $0x80000000,(%ebx)
c6050000000000 2:1:sym+0	movb $0x80000000,sym
c6448dfc00	movb $0x80000000,-4(%ebp,%ecx,4)
c60380	movb $0xffffff80,(%ebx)
c6050000000080 2:1:sym+0	movb $0xffffff80,sym
c6448dfc80	movb $0xffffff80,-4(%ebp,%ecx,4)
c603ff	movb $0xffffffff,(%ebx)
c60500000000ff 2:1:sym+0	movb $0xffffffff,sym
c6448dfcff	movb $0xffffffff,-4(%ebp,%ecx,4)
c60300 2:22:sym+0	movb $sym,(%ebx)
c6050000000000 2:1:sym+0 6:22:sym+0	movb $sym,sym
c6448dfc00 4:22:sym+0	movb $sym,-4(%ebp,%ecx,4)
c70301000000	mov $1,(%ebx)
refused	movb $1,$2
refused	movb %al,$2
refused	movb (%ebx),(%ebx)
refused	lock movb %cl,(%ebx)
refused	lock movb $1,(%ebx)
refused	lock movb (%ebx),%cl
refused	lock movb %cl,%al
668903	movw %ax,(%ebx)
668b03	movw (%ebx),%ax
668903	mov %ax,(%ebx)
refused	mov 4((%ebx,%esi,2),%ax
66b80000	movw $0,%ax
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
66b8ffff	movw $0x7fffffff,%ax
66b80000	movw $0x80000000,%ax
66b880ff	movw $0xffffff80,%ax
66b8ffff	movw $0xffffffff,%ax
66891b	movw %bx,(%ebx)
668b1b	movw (%ebx),%bx
66891d00000000 3:1:sym+0	movw %bx,sym
668b1d00000000 3:1:sym+0	movw sym,%bx
66891b	mov %bx,(%ebx)
refused	mov 4((%ebx,%esi,2),%bx
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
66bb0000 2:20:sym+0	movw $sym,%bx
668933	movw %si,(%ebx)
668b33	movw (%ebx),%si
66893500000000 3:1:sym+0	movw %si,sym
668b3500000000 3:1:sym+0	movw sym,%si
668933	mov %si,(%ebx)
refused	mov 4((%ebx,%esi,2),%si
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
66be0000 2:20:sym+0	movw $sym,%si
66c7030000	movw $0,(%ebx)
66c705000000000000 3:1:sym+0	movw $0,sym
66c7448dfc0000	movw $0,-4(%ebp,%ecx,4)
66c7030100	movw $1,(%ebx)
66c705000000000100 3:1:sym+0	movw $1,sym
66c7448dfc0100	movw $1,-4(%ebp,%ecx,4)
66c703ffff	movw $-1,(%ebx)
66c70500000000ffff 3:1:sym+0	movw $-1,sym
66c7448dfcffff	movw $-1,-4(%ebp,%ecx,4)
66c7037f00	movw $127,(%ebx)
66c705000000007f00 3:1:sym+0	movw $127,sym
66c7448dfc7f00	movw $127,-4(%ebp,%ecx,4)
66c7038000	movw $128,(%ebx)
66c705000000008000 3:1:sym+0	movw $128,sym
66c7448dfc8000	movw $128,-4(%ebp,%ecx,4)
66c70380ff	movw $-128,(%ebx)
66c7050000000080ff 3:1:sym+0	movw $-128,sym
66c7448dfc80ff	movw $-128,-4(%ebp,%ecx,4)
66c7037fff	movw $-129,(%ebx)
66c705000000007fff 3:1:sym+0	movw $-129,sym
66c7448dfc7fff	movw $-129,-4(%ebp,%ecx,4)
66c703ff00	movw $255,(%ebx)
66c70500000000ff00 3:1:sym+0	movw $255,sym
66c7448dfcff00	movw $255,-4(%ebp,%ecx,4)
66c7030001	movw $256,(%ebx)
66c705000000000001 3:1:sym+0	movw $256,sym
66c7448dfc0001	movw $256,-4(%ebp,%ecx,4)
66c703ff7f	movw $0x7fff,(%ebx)
66c70500000000ff7f 3:1:sym+0	movw $0x7fff,sym
66c7448dfcff7f	movw $0x7fff,-4(%ebp,%ecx,4)
66c7030080	movw $0x8000,(%ebx)
66c705000000000080 3:1:sym+0	movw $0x8000,sym
66c7448dfc0080	movw $0x8000,-4(%ebp,%ecx,4)
66c70380ff	movw $0xff80,(%ebx)
66c7050000000080ff 3:1:sym+0	movw $0xff80,sym
66c7448dfc80ff	movw $0xff80,-4(%ebp,%ecx,4)
66c703ffff	movw $0xffff,(%ebx)
66c70500000000ffff 3:1:sym+0	movw $0xffff,sym
66c7448dfcffff	movw $0xffff,-4(%ebp,%ecx,4)
66c7030000	movw $0x10000,(%ebx)
66c705000000000000 3:1:sym+0	movw $0x10000,sym
66c7448dfc0000	movw $0x10000,-4(%ebp,%ecx,4)
66c703ffff	movw $0x7fffffff,(%ebx)
66c70500000000ffff 3:1:sym+0	movw $0x7fffffff,sym
66c7448dfcffff	movw $0x7fffffff,-4(%ebp,%ecx,4)
66c7030000	movw $0x80000000,(%ebx)
66c705000000000000 3:1:sym+0	movw $0x80000000,sym
66c7448dfc0000	movw $0x80000000,-4(%ebp,%ecx,4)
66c70380ff	movw $0xffffff80,(%ebx)
66c7050000000080ff 3:1:sym+0	movw $0xffffff80,sym
66c7448dfc80ff	movw $0xffffff80,-4(%ebp,%ecx,4)
66c703ffff	movw $0xffffffff,(%ebx)
66c70500000000ffff 3:1:sym+0	movw $0xffffffff,sym
66c7448dfcffff	movw $0xffffffff,-4(%ebp,%ecx,4)
66c7030000 3:20:sym+0	movw $sym,(%ebx)
66c705000000000000 3:1:sym+0 7:20:sym+0	movw $sym,sym
66c7448dfc0000 5:20:sym+0	movw $sym,-4(%ebp,%ecx,4)
66bb0100	mov $1,%bx
refused	movw $1,$2
refused	movw %ax,$2
refused	movw (%ebx),(%ebx)
refused	lock movw %bx,(%ebx)
refused	lock movw $1,(%ebx)
refused	lock movw (%ebx),%bx
refused	lock movw %bx,%ax
8b03	movl (%ebx),%eax
8903	mov %eax,(%ebx)
refused	mov 4((%ebx,%esi,2),%eax
b800000000	movl $0,%eax
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
b8ffffff7f	movl $0x7fffffff,%eax
b800000080	movl $0x80000000,%eax
b880ffffff	movl $0xffffff80,%eax
891b	movl %ebx,(%ebx)
8b1b	movl (%ebx),%ebx
891d00000000 2:1:sym+0	movl %ebx,sym
891b	mov %ebx,(%ebx)
refused	mov 4((%ebx,%esi,2),%ebx
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
bb00000000 1:1:sym+0	movl $sym,%ebx
8923	movl %esp,(%ebx)
8b23	movl (%ebx),%esp
892500000000 2:1:sym+0	movl %esp,sym
8b2500000000 2:1:sym+0	movl sym,%esp
8923	mov %esp,(%ebx)
refused	mov 4((%ebx,%esi,2),%esp
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
bc00000000 1:1:sym+0	movl $sym,%esp
892b	movl %ebp,(%ebx)
8b2b	movl (%ebx),%ebp
892d00000000 2:1:sym+0	movl %ebp,sym
8b2d00000000 2:1:sym+0	movl sym,%ebp
892b	mov %ebp,(%ebx)
refused	mov 4((%ebx,%esi,2),%ebp
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
bd00000000 1:1:sym+0	movl $sym,%ebp
c70300000000	movl $0,(%ebx)
c7050000000000000000 2:1:sym+0	movl $0,sym
c7448dfc00000000	movl $0,-4(%ebp,%ecx,4)
c70301000000	movl $1,(%ebx)
c7050000000001000000 2:1:sym+0	movl $1,sym
c7448dfc01000000	movl $1,-4(%ebp,%ecx,4)
c703ffffffff	movl $-1,(%ebx)
c70500000000ffffffff 2:1:sym+0	movl $-1,sym
c7448dfcffffffff	movl $-1,-4(%ebp,%ecx,4)
c7037f000000	movl $127,(%ebx)
c705000000007f000000 2:1:sym+0	movl $127,sym
c7448dfc7f000000	movl $127,-4(%ebp,%ecx,4)
c70380000000	movl $128,(%ebx)
c7050000000080000000 2:1:sym+0	movl $128,sym
c7448dfc80000000	movl $128,-4(%ebp,%ecx,4)
c70380ffffff	movl $-128,(%ebx)
c7050000000080ffffff 2:1:sym+0	movl $-128,sym
c7448dfc80ffffff	movl $-128,-4(%ebp,%ecx,4)
c7037fffffff	movl $-129,(%ebx)
c705000000007fffffff 2:1:sym+0	movl $-129,sym
c7448dfc7fffffff	movl $-129,-4(%ebp,%ecx,4)
c703ff000000	movl $255,(%ebx)
c70500000000ff000000 2:1:sym+0	movl $255,sym
c7448dfcff000000	movl $255,-4(%ebp,%ecx,4)
c70300010000	movl $256,(%ebx)
c7050000000000010000 2:1:sym+0	movl $256,sym
c7448dfc00010000	movl $256,-4(%ebp,%ecx,4)
c703ff7f0000	movl $0x7fff,(%ebx)
c70500000000ff7f0000 2:1:sym+0	movl $0x7fff,sym
c7448dfcff7f0000	movl $0x7fff,-4(%ebp,%ecx,4)
c70300800000	movl $0x8000,(%ebx)
c7050000000000800000 2:1:sym+0	movl $0x8000,sym
c7448dfc00800000	movl $0x8000,-4(%ebp,%ecx,4)
c70380ff0000	movl $0xff80,(%ebx)
c7050000000080ff0000 2:1:sym+0	movl $0xff80,sym
c7448dfc80ff0000	movl $0xff80,-4(%ebp,%ecx,4)
c703ffff0000	movl $0xffff,(%ebx)
c70500000000ffff0000 2:1:sym+0	movl $0xffff,sym
c7448dfcffff0000	movl $0xffff,-4(%ebp,%ecx,4)
c70300000100	movl $0x10000,(%ebx)
c7050000000000000100 2:1:sym+0	movl $0x10000,sym
c7448dfc00000100	movl $0x10000,-4(%ebp,%ecx,4)
c703ffffff7f	movl $0x7fffffff,(%ebx)
c70500000000ffffff7f 2:1:sym+0	movl $0x7fffffff,sym
c7448dfcffffff7f	movl $0x7fffffff,-4(%ebp,%ecx,4)
c70300000080	movl $0x80000000,(%ebx)
c7050000000000000080 2:1:sym+0	movl $0x80000000,sym
c7448dfc00000080	movl $0x80000000,-4(%ebp,%ecx,4)
c70380ffffff	movl $0xffffff80,(%ebx)
c7050000000080ffffff 2:1:sym+0	movl $0xffffff80,sym
c7448dfc80ffffff	movl $0xffffff80,-4(%ebp,%ecx,4)
c703ffffffff	movl $0xffffffff,(%ebx)
c70500000000ffffffff 2:1:sym+0	movl $0xffffffff,sym
c7448dfcffffffff	movl $0xffffffff,-4(%ebp,%ecx,4)
c70300000000 2:1:sym+0	movl $sym,(%ebx)
c7050000000000000000 2:1:sym+0 6:1:sym+0	movl $sym,sym
c7448dfc00000000 4:1:sym+0	movl $sym,-4(%ebp,%ecx,4)
bb01000000	mov $1,%ebx
refused	movl %eax,$2
refused	movl (%ebx),(%ebx)
refused	lock movl %ebx,(%ebx)
refused	lock movl $1,(%ebx)
refused	lock movl (%ebx),%ebx
refused	lock movl %ebx,%eax
fec0	incb %al
fec0	inc %al
fec1	incb %cl
fec1	inc %cl
fec7	incb %bh
fec7	inc %bh
fe03	incb (%ebx)
fe0500000000 2:1:sym+0	incb sym
ff03	inc (%ebx)
fe442408	incb 8(%esp)
f0fe03	lock incb (%ebx)
refused	incb $1
refused	incb
6640	incw %ax
6640	inc %ax
6643	incw %bx
6643	inc %bx
6646	incw %si
6646	inc %si
66ff03	incw (%ebx)
66ff0500000000 3:1:sym+0	incw sym
66ff442408	incw 8(%esp)
66f0ff03	lock incw (%ebx)
refused	incw $1
refused	incw
40	incl %eax
40	inc %eax
43	incl %ebx
43	inc %ebx
44	incl %esp
44	inc %esp
45	incl %ebp
45	inc %ebp
ff03	incl (%ebx)
ff0500000000 2:1:sym+0	incl sym
ff442408	incl 8(%esp)
f0ff03	lock incl (%ebx)
refused	incl $1
refused	incl
fec8	decb %al
fec8	dec %al
fec9	decb %cl
fec9	dec %cl
fecf	decb %bh
fecf	dec %bh
fe0b	decb (%ebx)
fe0d00000000 2:1:sym+0	decb sym
ff0b	dec (%ebx)
fe4c2408	decb 8(%esp)
f0fe0b	lock decb (%ebx)
refused	decb $1
refused	decb
6648	decw %ax
6648	dec %ax
664b	decw %bx
664b	dec %bx
664e	decw %si
664e	dec %si
66ff0b	decw (%ebx)
66ff0d00000000 3:1:sym+0	decw sym
66ff4c2408	decw 8(%esp)
66f0ff0b	lock decw (%ebx)
refused	decw $1
refused	decw
48	decl %eax
48	dec %eax
4b	decl %ebx
4b	dec %ebx
4c	decl %esp
4c	dec %esp
4d	decl %ebp
4d	dec %ebp
ff0b	decl (%ebx)
ff0d00000000 2:1:sym+0	decl sym
ff4c2408	decl 8(%esp)
f0ff0b	lock decl (%ebx)
refused	decl $1
refused	decl
f6d8	negb %al
f6d8	neg %al
f6d9	negb %cl
f6d9	neg %cl
f6df	negb %bh
f6df	neg %bh
f61b	negb (%ebx)
f61d00000000 2:1:sym+0	negb sym
f71b	neg (%ebx)
f65c2408	negb 8(%esp)
f0f61b	lock negb (%ebx)
refused	negb $1
refused	negb
66f7d8	negw %ax
66f7d8	neg %ax
66f7db	negw %bx
66f7db	neg %bx
66f7de	negw %si
66f7de	neg %si
66f71b	negw (%ebx)
66f71d00000000 3:1:sym+0	negw sym
66f75c2408	negw 8(%esp)
66f0f71b	lock negw (%ebx)
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
f71b	negl (%ebx)
f71d00000000 2:1:sym+0	negl sym
f75c2408	negl 8(%esp)
f0f71b	lock negl (%ebx)
refused	negl $1
refused	negl
f6d0	notb %al
f6d0	not %al
f6d1	notb %cl
f6d1	not %cl
f6d7	notb %bh
f6d7	not %bh
f613	notb (%ebx)
f61500000000 2:1:sym+0	notb sym
f713	not (%ebx)
f6542408	notb 8(%esp)
f0f613	lock notb (%ebx)
refused	notb $1
refused	notb
66f7d0	notw %ax
66f7d0	not %ax
66f7d3	notw %bx
66f7d3	not %bx
66f7d6	notw %si
66f7d6	not %si
66f713	notw (%ebx)
66f71500000000 3:1:sym+0	notw sym
66f7542408	notw 8(%esp)
66f0f713	lock notw (%ebx)
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
f713	notl (%ebx)
f71500000000 2:1:sym+0	notl sym
f7542408	notl 8(%esp)
f0f713	lock notl (%ebx)
refused	notl $1
refused	notl
f6e0	mulb %al
f6e0	mul %al
f6e1	mulb %cl
f6e1	mul %cl
f6e7	mulb %bh
f6e7	mul %bh
f623	mulb (%ebx)
f62500000000 2:1:sym+0	mulb sym
f723	mul (%ebx)
f6642408	mulb 8(%esp)
refused	lock mulb (%ebx)
refused	mulb $1
refused	mulb
66f7e0	mulw %ax
66f7e0	mul %ax
66f7e3	mulw %bx
66f7e3	mul %bx
66f7e6	mulw %si
66f7e6	mul %si
66f723	mulw (%ebx)
66f72500000000 3:1:sym+0	mulw sym
66f7642408	mulw 8(%esp)
refused	lock mulw (%ebx)
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
f723	mull (%ebx)
f72500000000 2:1:sym+0	mull sym
f7642408	mull 8(%esp)
refused	lock mull (%ebx)
refused	mull $1
refused	mull
f6e8	imulb %al
f6e8	imul %al
f6e9	imulb %cl
f6e9	imul %cl
f6ef	imulb %bh
f6ef	imul %bh
f62b	imulb (%ebx)
f62d00000000 2:1:sym+0	imulb sym
f72b	imul (%ebx)
f66c2408	imulb 8(%esp)
refused	lock imulb (%ebx)
refused	imulb $1
refused	imulb
66f7e8	imulw %ax
66f7e8	imul %ax
66f7eb	imulw %bx
66f7eb	imul %bx
66f7ee	imulw %si
66f7ee	imul %si
66f72b	imulw (%ebx)
66f72d00000000 3:1:sym+0	imulw sym
66f76c2408	imulw 8(%esp)
refused	lock imulw (%ebx)
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
f72b	imull (%ebx)
f72d00000000 2:1:sym+0	imull sym
f76c2408	imull 8(%esp)
refused	lock imull (%ebx)
refused	imull $1
refused	imull
f6f0	divb %al
f6f0	div %al
f6f1	divb %cl
f6f1	div %cl
f6f7	divb %bh
f6f7	div %bh
f633	divb (%ebx)
f63500000000 2:1:sym+0	divb sym
f733	div (%ebx)
f6742408	divb 8(%esp)
refused	lock divb (%ebx)
refused	divb $1
refused	divb
66f7f0	divw %ax
66f7f0	div %ax
66f7f3	divw %bx
66f7f3	div %bx
66f7f6	divw %si
66f7f6	div %si
66f733	divw (%ebx)
66f73500000000 3:1:sym+0	divw sym
66f7742408	divw 8(%esp)
refused	lock divw (%ebx)
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
f733	divl (%ebx)
f73500000000 2:1:sym+0	divl sym
f7742408	divl 8(%esp)
refused	lock divl (%ebx)
refused	divl $1
refused	divl
f6f8	idivb %al
f6f8	idiv %al
f6f9	idivb %cl
f6f9	idiv %cl
f6ff	idivb %bh
f6ff	idiv %bh
f63b	idivb (%ebx)
f63d00000000 2:1:sym+0	idivb sym
f73b	idiv (%ebx)
f67c2408	idivb 8(%esp)
refused	lock idivb (%ebx)
refused	idivb $1
refused	idivb
66f7f8	idivw %ax
66f7f8	idiv %ax
66f7fb	idivw %bx
66f7fb	idiv %bx
66f7fe	idivw %si
66f7fe	idiv %si
66f73b	idivw (%ebx)
66f73d00000000 3:1:sym+0	idivw sym
66f77c2408	idivw 8(%esp)
refused	lock idivw (%ebx)
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
f73b	idivl (%ebx)
f73d00000000 2:1:sym+0	idivl sym
f77c2408	idivl 8(%esp)
refused	lock idivl (%ebx)
refused	idivl $1
refused	idivl
refused	pushb %al
refused	push %al
refused	pushb %cl
refused	push %cl
refused	pushb %bh
refused	push %bh
refused	pushb (%ebx)
refused	pushb sym
ff33	push (%ebx)
refused	pushb 8(%esp)
refused	lock pushb (%ebx)
refused	pushb $1
refused	pushb
6650	pushw %ax
6650	push %ax
6653	pushw %bx
6653	push %bx
6656	pushw %si
6656	push %si
66ff33	pushw (%ebx)
66ff3500000000 3:1:sym+0	pushw sym
66ff742408	pushw 8(%esp)
refused	lock pushw (%ebx)
666a01	pushw $1
refused	pushw
50	pushl %eax
50	push %eax
53	pushl %ebx
53	push %ebx
54	pushl %esp
54	push %esp
55	pushl %ebp
55	push %ebp
ff33	pushl (%ebx)
ff3500000000 2:1:sym+0	pushl sym
ff742408	pushl 8(%esp)
refused	lock pushl (%ebx)
6a01	pushl $1
refused	pushl
refused	popb %al
refused	pop %al
refused	popb %cl
refused	pop %cl
refused	popb %bh
refused	pop %bh
refused	popb (%ebx)
refused	popb sym
8f03	pop (%ebx)
refused	popb 8(%esp)
refused	lock popb (%ebx)
refused	popb $1
refused	popb
6658	popw %ax
6658	pop %ax
665b	popw %bx
665b	pop %bx
665e	popw %si
665e	pop %si
668f03	popw (%ebx)
668f0500000000 3:1:sym+0	popw sym
668f442408	popw 8(%esp)
refused	lock popw (%ebx)
refused	popw $1
refused	popw
58	popl %eax
58	pop %eax
5b	popl %ebx
5b	pop %ebx
5c	popl %esp
5c	pop %esp
5d	popl %ebp
5d	pop %ebp
8f03	popl (%ebx)
8f0500000000 2:1:sym+0	popl sym
8f442408	popl 8(%esp)
refused	lock popl (%ebx)
refused	popl $1
refused	popl
d0e0	salb %al
d0e0	sal %al
d0e1	salb %cl
d0e1	sal %cl
d0e7	salb %bh
d0e7	sal %bh
d023	salb (%ebx)
d02500000000 2:1:sym+0	salb sym
d123	sal (%ebx)
d0642408	salb 8(%esp)
refused	lock salb (%ebx)
refused	salb $1
refused	salb
66d1e0	salw %ax
66d1e0	sal %ax
66d1e3	salw %bx
66d1e3	sal %bx
66d1e6	salw %si
66d1e6	sal %si
66d123	salw (%ebx)
66d12500000000 3:1:sym+0	salw sym
66d1642408	salw 8(%esp)
refused	lock salw (%ebx)
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
d123	sall (%ebx)
d12500000000 2:1:sym+0	sall sym
d1642408	sall 8(%esp)
refused	lock sall (%ebx)
refused	sall $1
refused	sall
d0e0	shlb %al
d0e0	shl %al
d0e1	shlb %cl
d0e1	shl %cl
d0e7	shlb %bh
d0e7	shl %bh
d023	shlb (%ebx)
d02500000000 2:1:sym+0	shlb sym
d123	shl (%ebx)
d0642408	shlb 8(%esp)
refused	lock shlb (%ebx)
refused	shlb $1
refused	shlb
66d1e0	shlw %ax
66d1e0	shl %ax
66d1e3	shlw %bx
66d1e3	shl %bx
66d1e6	shlw %si
66d1e6	shl %si
66d123	shlw (%ebx)
66d12500000000 3:1:sym+0	shlw sym
66d1642408	shlw 8(%esp)
refused	lock shlw (%ebx)
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
d123	shll (%ebx)
d12500000000 2:1:sym+0	shll sym
d1642408	shll 8(%esp)
refused	lock shll (%ebx)
refused	shll $1
refused	shll
d0e8	shrb %al
d0e8	shr %al
d0e9	shrb %cl
d0e9	shr %cl
d0ef	shrb %bh
d0ef	shr %bh
d02b	shrb (%ebx)
d02d00000000 2:1:sym+0	shrb sym
d12b	shr (%ebx)
d06c2408	shrb 8(%esp)
refused	lock shrb (%ebx)
refused	shrb $1
refused	shrb
66d1e8	shrw %ax
66d1e8	shr %ax
66d1eb	shrw %bx
66d1eb	shr %bx
66d1ee	shrw %si
66d1ee	shr %si
66d12b	shrw (%ebx)
66d12d00000000 3:1:sym+0	shrw sym
66d16c2408	shrw 8(%esp)
refused	lock shrw (%ebx)
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
d12b	shrl (%ebx)
d12d00000000 2:1:sym+0	shrl sym
d16c2408	shrl 8(%esp)
refused	lock shrl (%ebx)
refused	shrl $1
refused	shrl
d0f8	sarb %al
d0f8	sar %al
d0f9	sarb %cl
d0f9	sar %cl
d0ff	sarb %bh
d0ff	sar %bh
d03b	sarb (%ebx)
d03d00000000 2:1:sym+0	sarb sym
d13b	sar (%ebx)
d07c2408	sarb 8(%esp)
refused	lock sarb (%ebx)
refused	sarb $1
refused	sarb
66d1f8	sarw %ax
66d1f8	sar %ax
66d1fb	sarw %bx
66d1fb	sar %bx
66d1fe	sarw %si
66d1fe	sar %si
66d13b	sarw (%ebx)
66d13d00000000 3:1:sym+0	sarw sym
66d17c2408	sarw 8(%esp)
refused	lock sarw (%ebx)
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
d13b	sarl (%ebx)
d13d00000000 2:1:sym+0	sarl sym
d17c2408	sarl 8(%esp)
refused	lock sarl (%ebx)
refused	sarl $1
refused	sarl
d0c0	rolb %al
d0c0	rol %al
d0c1	rolb %cl
d0c1	rol %cl
d0c7	rolb %bh
d0c7	rol %bh
d003	rolb (%ebx)
d00500000000 2:1:sym+0	rolb sym
d103	rol (%ebx)
d0442408	rolb 8(%esp)
refused	lock rolb (%ebx)
refused	rolb $1
refused	rolb
66d1c0	rolw %ax
66d1c0	rol %ax
66d1c3	rolw %bx
66d1c3	rol %bx
66d1c6	rolw %si
66d1c6	rol %si
66d103	rolw (%ebx)
66d10500000000 3:1:sym+0	rolw sym
66d1442408	rolw 8(%esp)
refused	lock rolw (%ebx)
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
d103	roll (%ebx)
d10500000000 2:1:sym+0	roll sym
d1442408	roll 8(%esp)
refused	lock roll (%ebx)
refused	roll $1
refused	roll
d0c8	rorb %al
d0c8	ror %al
d0c9	rorb %cl
d0c9	ror %cl
d0cf	rorb %bh
d0cf	ror %bh
d00b	rorb (%ebx)
d00d00000000 2:1:sym+0	rorb sym
d10b	ror (%ebx)
d04c2408	rorb 8(%esp)
refused	lock rorb (%ebx)
refused	rorb $1
refused	rorb
66d1c8	rorw %ax
66d1c8	ror %ax
66d1cb	rorw %bx
66d1cb	ror %bx
66d1ce	rorw %si
66d1ce	ror %si
66d10b	rorw (%ebx)
66d10d00000000 3:1:sym+0	rorw sym
66d14c2408	rorw 8(%esp)
refused	lock rorw (%ebx)
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
d10b	rorl (%ebx)
d10d00000000 2:1:sym+0	rorl sym
d14c2408	rorl 8(%esp)
refused	lock rorl (%ebx)
refused	rorl $1
refused	rorl
d0d0	rclb %al
d0d0	rcl %al
d0d1	rclb %cl
d0d1	rcl %cl
d0d7	rclb %bh
d0d7	rcl %bh
d013	rclb (%ebx)
d01500000000 2:1:sym+0	rclb sym
d113	rcl (%ebx)
d0542408	rclb 8(%esp)
refused	lock rclb (%ebx)
refused	rclb $1
refused	rclb
66d1d0	rclw %ax
66d1d0	rcl %ax
66d1d3	rclw %bx
66d1d3	rcl %bx
66d1d6	rclw %si
66d1d6	rcl %si
66d113	rclw (%ebx)
66d11500000000 3:1:sym+0	rclw sym
66d1542408	rclw 8(%esp)
refused	lock rclw (%ebx)
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
d113	rcll (%ebx)
d11500000000 2:1:sym+0	rcll sym
d1542408	rcll 8(%esp)
refused	lock rcll (%ebx)
refused	rcll $1
refused	rcll
d0d8	rcrb %al
d0d8	rcr %al
d0d9	rcrb %cl
d0d9	rcr %cl
d0df	rcrb %bh
d0df	rcr %bh
d01b	rcrb (%ebx)
d01d00000000 2:1:sym+0	rcrb sym
d11b	rcr (%ebx)
d05c2408	rcrb 8(%esp)
refused	lock rcrb (%ebx)
refused	rcrb $1
refused	rcrb
66d1d8	rcrw %ax
66d1d8	rcr %ax
66d1db	rcrw %bx
66d1db	rcr %bx
66d1de	rcrw %si
66d1de	rcr %si
66d11b	rcrw (%ebx)
66d11d00000000 3:1:sym+0	rcrw sym
66d15c2408	rcrw 8(%esp)
refused	lock rcrw (%ebx)
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
d11b	rcrl (%ebx)
d11d00000000 2:1:sym+0	rcrl sym
d15c2408	rcrl 8(%esp)
refused	lock rcrl (%ebx)
refused	rcrl $1
refused	rcrl
d0e1	salb $1,%cl
d023	salb $1,(%ebx)
d0e1	sal $1,%cl
c0e102	salb $2,%cl
c02302	salb $2,(%ebx)
c0e102	sal $2,%cl
c0e11f	salb $31,%cl
c0231f	salb $31,(%ebx)
c0e11f	sal $31,%cl
c0e100	salb $0,%cl
c02300	salb $0,(%ebx)
c0e100	sal $0,%cl
d2e1	salb %cl,%cl
d223	salb %cl,(%ebx)
d2e1	sal %cl,%cl
c0e107	salb $7,%cl
c02307	salb $7,(%ebx)
c0e107	sal $7,%cl
c0e11f	salb $0x1f,%cl
c0231f	salb $0x1f,(%ebx)
c0e11f	sal $0x1f,%cl
c0e13f	salb $63,%cl
c0233f	salb $63,(%ebx)
c0e13f	sal $63,%cl
c0e140	salb $64,%cl
c02340	salb $64,(%ebx)
c0e140	sal $64,%cl
c0e1ff	salb $-1,%cl
c023ff	salb $-1,(%ebx)
c0e1ff	sal $-1,%cl
d22500000000 2:1:sym+0	salb %cl,sym
d323	sal %cl,(%ebx)
d123	sal $1,(%ebx)
66d1e3	salw $1,%bx
66d123	salw $1,(%ebx)
66d1e3	sal $1,%bx
66c1e302	salw $2,%bx
66c12302	salw $2,(%ebx)
66c1e302	sal $2,%bx
66c1e31f	salw $31,%bx
66c1231f	salw $31,(%ebx)
66c1e31f	sal $31,%bx
66c1e300	salw $0,%bx
66c12300	salw $0,(%ebx)
66c1e300	sal $0,%bx
66d3e3	salw %cl,%bx
66d323	salw %cl,(%ebx)
66d3e3	sal %cl,%bx
66c1e307	salw $7,%bx
66c12307	salw $7,(%ebx)
66c1e307	sal $7,%bx
66c1e31f	salw $0x1f,%bx
66c1231f	salw $0x1f,(%ebx)
66c1e31f	sal $0x1f,%bx
66c1e33f	salw $63,%bx
66c1233f	salw $63,(%ebx)
66c1e33f	sal $63,%bx
66c1e340	salw $64,%bx
66c12340	salw $64,(%ebx)
66c1e340	sal $64,%bx
refused	salw $-1,%bx
refused	salw $-1,(%ebx)
refused	sal $-1,%bx
66d32500000000 3:1:sym+0	salw %cl,sym
refused	salw %bx,%bx
refused	salw %cl
d1e3	sall $1,%ebx
d123	sall $1,(%ebx)
d1e3	sal $1,%ebx
c1e302	sall $2,%ebx
c12302	sall $2,(%ebx)
c1e302	sal $2,%ebx
c1e31f	sall $31,%ebx
c1231f	sall $31,(%ebx)
c1e31f	sal $31,%ebx
c1e300	sall $0,%ebx
c12300	sall $0,(%ebx)
c1e300	sal $0,%ebx
d3e3	sall %cl,%ebx
d323	sall %cl,(%ebx)
d3e3	sal %cl,%ebx
c1e307	sall $7,%ebx
c12307	sall $7,(%ebx)
c1e307	sal $7,%ebx
c1e31f	sall $0x1f,%ebx
c1231f	sall $0x1f,(%ebx)
c1e31f	sal $0x1f,%ebx
c1e33f	sall $63,%ebx
c1233f	sall $63,(%ebx)
c1e33f	sal $63,%ebx
c1e340	sall $64,%ebx
c12340	sall $64,(%ebx)
c1e340	sal $64,%ebx
refused	sall $-1,%ebx
refused	sall $-1,(%ebx)
refused	sal $-1,%ebx
d32500000000 2:1:sym+0	sall %cl,sym
refused	sall %ebx,%ebx
refused	sall %cl
d0e1	shlb $1,%cl
d023	shlb $1,(%ebx)
d0e1	shl $1,%cl
c0e102	shlb $2,%cl
c02302	shlb $2,(%ebx)
c0e102	shl $2,%cl
c0e11f	shlb $31,%cl
c0231f	shlb $31,(%ebx)
c0e11f	shl $31,%cl
c0e100	shlb $0,%cl
c02300	shlb $0,(%ebx)
c0e100	shl $0,%cl
d2e1	shlb %cl,%cl
d223	shlb %cl,(%ebx)
d2e1	shl %cl,%cl
c0e107	shlb $7,%cl
c02307	shlb $7,(%ebx)
c0e107	shl $7,%cl
c0e11f	shlb $0x1f,%cl
c0231f	shlb $0x1f,(%ebx)
c0e11f	shl $0x1f,%cl
c0e13f	shlb $63,%cl
c0233f	shlb $63,(%ebx)
c0e13f	shl $63,%cl
c0e140	shlb $64,%cl
c02340	shlb $64,(%ebx)
c0e140	shl $64,%cl
c0e1ff	shlb $-1,%cl
c023ff	shlb $-1,(%ebx)
c0e1ff	shl $-1,%cl
d22500000000 2:1:sym+0	shlb %cl,sym
d323	shl %cl,(%ebx)
d123	shl $1,(%ebx)
66d1e3	shlw $1,%bx
66d123	shlw $1,(%ebx)
66d1e3	shl $1,%bx
66c1e302	shlw $2,%bx
66c12302	shlw $2,(%ebx)
66c1e302	shl $2,%bx
66c1e31f	shlw $31,%bx
66c1231f	shlw $31,(%ebx)
66c1e31f	shl $31,%bx
66c1e300	shlw $0,%bx
66c12300	shlw $0,(%ebx)
66c1e300	shl $0,%bx
66d3e3	shlw %cl,%bx
66d323	shlw %cl,(%ebx)
66d3e3	shl %cl,%bx
66c1e307	shlw $7,%bx
66c12307	shlw $7,(%ebx)
66c1e307	shl $7,%bx
66c1e31f	shlw $0x1f,%bx
66c1231f	shlw $0x1f,(%ebx)
66c1e31f	shl $0x1f,%bx
66c1e33f	shlw $63,%bx
66c1233f	shlw $63,(%ebx)
66c1e33f	shl $63,%bx
66c1e340	shlw $64,%bx
66c12340	shlw $64,(%ebx)
66c1e340	shl $64,%bx
refused	shlw $-1,%bx
refused	shlw $-1,(%ebx)
refused	shl $-1,%bx
66d32500000000 3:1:sym+0	shlw %cl,sym
refused	shlw %bx,%bx
refused	shlw %cl
d1e3	shll $1,%ebx
d123	shll $1,(%ebx)
d1e3	shl $1,%ebx
c1e302	shll $2,%ebx
c12302	shll $2,(%ebx)
c1e302	shl $2,%ebx
c1e31f	shll $31,%ebx
c1231f	shll $31,(%ebx)
c1e31f	shl $31,%ebx
c1e300	shll $0,%ebx
c12300	shll $0,(%ebx)
c1e300	shl $0,%ebx
d3e3	shll %cl,%ebx
d323	shll %cl,(%ebx)
d3e3	shl %cl,%ebx
c1e307	shll $7,%ebx
c12307	shll $7,(%ebx)
c1e307	shl $7,%ebx
c1e31f	shll $0x1f,%ebx
c1231f	shll $0x1f,(%ebx)
c1e31f	shl $0x1f,%ebx
c1e33f	shll $63,%ebx
c1233f	shll $63,(%ebx)
c1e33f	shl $63,%ebx
c1e340	shll $64,%ebx
c12340	shll $64,(%ebx)
c1e340	shl $64,%ebx
refused	shll $-1,%ebx
refused	shll $-1,(%ebx)
refused	shl $-1,%ebx
d32500000000 2:1:sym+0	shll %cl,sym
refused	shll %ebx,%ebx
refused	shll %cl
d0e9	shrb $1,%cl
d02b	shrb $1,(%ebx)
d0e9	shr $1,%cl
c0e902	shrb $2,%cl
c02b02	shrb $2,(%ebx)
c0e902	shr $2,%cl
c0e91f	shrb $31,%cl
c02b1f	shrb $31,(%ebx)
c0e91f	shr $31,%cl
c0e900	shrb $0,%cl
c02b00	shrb $0,(%ebx)
c0e900	shr $0,%cl
d2e9	shrb %cl,%cl
d22b	shrb %cl,(%ebx)
d2e9	shr %cl,%cl
c0e907	shrb $7,%cl
c02b07	shrb $7,(%ebx)
c0e907	shr $7,%cl
c0e91f	shrb $0x1f,%cl
c02b1f	shrb $0x1f,(%ebx)
c0e91f	shr $0x1f,%cl
c0e93f	shrb $63,%cl
c02b3f	shrb $63,(%ebx)
c0e93f	shr $63,%cl
c0e940	shrb $64,%cl
c02b40	shrb $64,(%ebx)
c0e940	shr $64,%cl
c0e9ff	shrb $-1,%cl
c02bff	shrb $-1,(%ebx)
c0e9ff	shr $-1,%cl
d22d00000000 2:1:sym+0	shrb %cl,sym
d32b	shr %cl,(%ebx)
d12b	shr $1,(%ebx)
66d1eb	shrw $1,%bx
66d12b	shrw $1,(%ebx)
66d1eb	shr $1,%bx
66c1eb02	shrw $2,%bx
66c12b02	shrw $2,(%ebx)
66c1eb02	shr $2,%bx
66c1eb1f	shrw $31,%bx
66c12b1f	shrw $31,(%ebx)
66c1eb1f	shr $31,%bx
66c1eb00	shrw $0,%bx
66c12b00	shrw $0,(%ebx)
66c1eb00	shr $0,%bx
66d3eb	shrw %cl,%bx
66d32b	shrw %cl,(%ebx)
66d3eb	shr %cl,%bx
66c1eb07	shrw $7,%bx
66c12b07	shrw $7,(%ebx)
66c1eb07	shr $7,%bx
66c1eb1f	shrw $0x1f,%bx
66c12b1f	shrw $0x1f,(%ebx)
66c1eb1f	shr $0x1f,%bx
66c1eb3f	shrw $63,%bx
66c12b3f	shrw $63,(%ebx)
66c1eb3f	shr $63,%bx
66c1eb40	shrw $64,%bx
66c12b40	shrw $64,(%ebx)
66c1eb40	shr $64,%bx
refused	shrw $-1,%bx
refused	shrw $-1,(%ebx)
refused	shr $-1,%bx
66d32d00000000 3:1:sym+0	shrw %cl,sym
refused	shrw %bx,%bx
refused	shrw %cl
d1eb	shrl $1,%ebx
d12b	shrl $1,(%ebx)
d1eb	shr $1,%ebx
c1eb02	shrl $2,%ebx
c12b02	shrl $2,(%ebx)
c1eb02	shr $2,%ebx
c1eb1f	shrl $31,%ebx
c12b1f	shrl $31,(%ebx)
c1eb1f	shr $31,%ebx
c1eb00	shrl $0,%ebx
c12b00	shrl $0,(%ebx)
c1eb00	shr $0,%ebx
d3eb	shrl %cl,%ebx
d32b	shrl %cl,(%ebx)
d3eb	shr %cl,%ebx
c1eb07	shrl $7,%ebx
c12b07	shrl $7,(%ebx)
c1eb07	shr $7,%ebx
c1eb1f	shrl $0x1f,%ebx
c12b1f	shrl $0x1f,(%ebx)
c1eb1f	shr $0x1f,%ebx
c1eb3f	shrl $63,%ebx
c12b3f	shrl $63,(%ebx)
c1eb3f	shr $63,%ebx
c1eb40	shrl $64,%ebx
c12b40	shrl $64,(%ebx)
c1eb40	shr $64,%ebx
refused	shrl $-1,%ebx
refused	shrl $-1,(%ebx)
refused	shr $-1,%ebx
d32d00000000 2:1:sym+0	shrl %cl,sym
refused	shrl %ebx,%ebx
refused	shrl %cl
d0f9	sarb $1,%cl
d03b	sarb $1,(%ebx)
d0f9	sar $1,%cl
c0f902	sarb $2,%cl
c03b02	sarb $2,(%ebx)
c0f902	sar $2,%cl
c0f91f	sarb $31,%cl
c03b1f	sarb $31,(%ebx)
c0f91f	sar $31,%cl
c0f900	sarb $0,%cl
c03b00	sarb $0,(%ebx)
c0f900	sar $0,%cl
d2f9	sarb %cl,%cl
d23b	sarb %cl,(%ebx)
d2f9	sar %cl,%cl
c0f907	sarb $7,%cl
c03b07	sarb $7,(%ebx)
c0f907	sar $7,%cl
c0f91f	sarb $0x1f,%cl
c03b1f	sarb $0x1f,(%ebx)
c0f91f	sar $0x1f,%cl
c0f93f	sarb $63,%cl
c03b3f	sarb $63,(%ebx)
c0f93f	sar $63,%cl
c0f940	sarb $64,%cl
c03b40	sarb $64,(%ebx)
c0f940	sar $64,%cl
c0f9ff	sarb $-1,%cl
c03bff	sarb $-1,(%ebx)
c0f9ff	sar $-1,%cl
d23d00000000 2:1:sym+0	sarb %cl,sym
d33b	sar %cl,(%ebx)
d13b	sar $1,(%ebx)
66d1fb	sarw $1,%bx
66d13b	sarw $1,(%ebx)
66d1fb	sar $1,%bx
66c1fb02	sarw $2,%bx
66c13b02	sarw $2,(%ebx)
66c1fb02	sar $2,%bx
66c1fb1f	sarw $31,%bx
66c13b1f	sarw $31,(%ebx)
66c1fb1f	sar $31,%bx
66c1fb00	sarw $0,%bx
66c13b00	sarw $0,(%ebx)
66c1fb00	sar $0,%bx
66d3fb	sarw %cl,%bx
66d33b	sarw %cl,(%ebx)
66d3fb	sar %cl,%bx
66c1fb07	sarw $7,%bx
66c13b07	sarw $7,(%ebx)
66c1fb07	sar $7,%bx
66c1fb1f	sarw $0x1f,%bx
66c13b1f	sarw $0x1f,(%ebx)
66c1fb1f	sar $0x1f,%bx
66c1fb3f	sarw $63,%bx
66c13b3f	sarw $63,(%ebx)
66c1fb3f	sar $63,%bx
66c1fb40	sarw $64,%bx
66c13b40	sarw $64,(%ebx)
66c1fb40	sar $64,%bx
refused	sarw $-1,%bx
refused	sarw $-1,(%ebx)
refused	sar $-1,%bx
66d33d00000000 3:1:sym+0	sarw %cl,sym
refused	sarw %bx,%bx
refused	sarw %cl
d1fb	sarl $1,%ebx
d13b	sarl $1,(%ebx)
d1fb	sar $1,%ebx
c1fb02	sarl $2,%ebx
c13b02	sarl $2,(%ebx)
c1fb02	sar $2,%ebx
c1fb1f	sarl $31,%ebx
c13b1f	sarl $31,(%ebx)
c1fb1f	sar $31,%ebx
c1fb00	sarl $0,%ebx
c13b00	sarl $0,(%ebx)
c1fb00	sar $0,%ebx
d3fb	sarl %cl,%ebx
d33b	sarl %cl,(%ebx)
d3fb	sar %cl,%ebx
c1fb07	sarl $7,%ebx
c13b07	sarl $7,(%ebx)
c1fb07	sar $7,%ebx
c1fb1f	sarl $0x1f,%ebx
c13b1f	sarl $0x1f,(%ebx)
c1fb1f	sar $0x1f,%ebx
c1fb3f	sarl $63,%ebx
c13b3f	sarl $63,(%ebx)
c1fb3f	sar $63,%ebx
c1fb40	sarl $64,%ebx
c13b40	sarl $64,(%ebx)
c1fb40	sar $64,%ebx
refused	sarl $-1,%ebx
refused	sarl $-1,(%ebx)
refused	sar $-1,%ebx
d33d00000000 2:1:sym+0	sarl %cl,sym
refused	sarl %ebx,%ebx
refused	sarl %cl
d0c1	rolb $1,%cl
d003	rolb $1,(%ebx)
d0c1	rol $1,%cl
c0c102	rolb $2,%cl
c00302	rolb $2,(%ebx)
c0c102	rol $2,%cl
c0c11f	rolb $31,%cl
c0031f	rolb $31,(%ebx)
c0c11f	rol $31,%cl
c0c100	rolb $0,%cl
c00300	rolb $0,(%ebx)
c0c100	rol $0,%cl
d2c1	rolb %cl,%cl
d203	rolb %cl,(%ebx)
d2c1	rol %cl,%cl
c0c107	rolb $7,%cl
c00307	rolb $7,(%ebx)
c0c107	rol $7,%cl
c0c11f	rolb $0x1f,%cl
c0031f	rolb $0x1f,(%ebx)
c0c11f	rol $0x1f,%cl
c0c13f	rolb $63,%cl
c0033f	rolb $63,(%ebx)
c0c13f	rol $63,%cl
c0c140	rolb $64,%cl
c00340	rolb $64,(%ebx)
c0c140	rol $64,%cl
c0c1ff	rolb $-1,%cl
c003ff	rolb $-1,(%ebx)
c0c1ff	rol $-1,%cl
d20500000000 2:1:sym+0	rolb %cl,sym
d303	rol %cl,(%ebx)
d103	rol $1,(%ebx)
66d1c3	rolw $1,%bx
66d103	rolw $1,(%ebx)
66d1c3	rol $1,%bx
66c1c302	rolw $2,%bx
66c10302	rolw $2,(%ebx)
66c1c302	rol $2,%bx
66c1c31f	rolw $31,%bx
66c1031f	rolw $31,(%ebx)
66c1c31f	rol $31,%bx
66c1c300	rolw $0,%bx
66c10300	rolw $0,(%ebx)
66c1c300	rol $0,%bx
66d3c3	rolw %cl,%bx
66d303	rolw %cl,(%ebx)
66d3c3	rol %cl,%bx
66c1c307	rolw $7,%bx
66c10307	rolw $7,(%ebx)
66c1c307	rol $7,%bx
66c1c31f	rolw $0x1f,%bx
66c1031f	rolw $0x1f,(%ebx)
66c1c31f	rol $0x1f,%bx
66c1c33f	rolw $63,%bx
66c1033f	rolw $63,(%ebx)
66c1c33f	rol $63,%bx
66c1c340	rolw $64,%bx
66c10340	rolw $64,(%ebx)
66c1c340	rol $64,%bx
66c1c3ff	rolw $-1,%bx
66c103ff	rolw $-1,(%ebx)
66c1c3ff	rol $-1,%bx
66d30500000000 3:1:sym+0	rolw %cl,sym
refused	rolw %bx,%bx
refused	rolw %cl
d1c3	roll $1,%ebx
d103	roll $1,(%ebx)
d1c3	rol $1,%ebx
c1c302	roll $2,%ebx
c10302	roll $2,(%ebx)
c1c302	rol $2,%ebx
c1c31f	roll $31,%ebx
c1031f	roll $31,(%ebx)
c1c31f	rol $31,%ebx
c1c300	roll $0,%ebx
c10300	roll $0,(%ebx)
c1c300	rol $0,%ebx
d3c3	roll %cl,%ebx
d303	roll %cl,(%ebx)
d3c3	rol %cl,%ebx
c1c307	roll $7,%ebx
c10307	roll $7,(%ebx)
c1c307	rol $7,%ebx
c1c31f	roll $0x1f,%ebx
c1031f	roll $0x1f,(%ebx)
c1c31f	rol $0x1f,%ebx
c1c33f	roll $63,%ebx
c1033f	roll $63,(%ebx)
c1c33f	rol $63,%ebx
c1c340	roll $64,%ebx
c10340	roll $64,(%ebx)
c1c340	rol $64,%ebx
c1c3ff	roll $-1,%ebx
c103ff	roll $-1,(%ebx)
c1c3ff	rol $-1,%ebx
d30500000000 2:1:sym+0	roll %cl,sym
refused	roll %ebx,%ebx
refused	roll %cl
d0c9	rorb $1,%cl
d00b	rorb $1,(%ebx)
d0c9	ror $1,%cl
c0c902	rorb $2,%cl
c00b02	rorb $2,(%ebx)
c0c902	ror $2,%cl
c0c91f	rorb $31,%cl
c00b1f	rorb $31,(%ebx)
c0c91f	ror $31,%cl
c0c900	rorb $0,%cl
c00b00	rorb $0,(%ebx)
c0c900	ror $0,%cl
d2c9	rorb %cl,%cl
d20b	rorb %cl,(%ebx)
d2c9	ror %cl,%cl
c0c907	rorb $7,%cl
c00b07	rorb $7,(%ebx)
c0c907	ror $7,%cl
c0c91f	rorb $0x1f,%cl
c00b1f	rorb $0x1f,(%ebx)
c0c91f	ror $0x1f,%cl
c0c93f	rorb $63,%cl
c00b3f	rorb $63,(%ebx)
c0c93f	ror $63,%cl
c0c940	rorb $64,%cl
c00b40	rorb $64,(%ebx)
c0c940	ror $64,%cl
c0c9ff	rorb $-1,%cl
c00bff	rorb $-1,(%ebx)
c0c9ff	ror $-1,%cl
d20d00000000 2:1:sym+0	rorb %cl,sym
d30b	ror %cl,(%ebx)
d10b	ror $1,(%ebx)
66d1cb	rorw $1,%bx
66d10b	rorw $1,(%ebx)
66d1cb	ror $1,%bx
66c1cb02	rorw $2,%bx
66c10b02	rorw $2,(%ebx)
66c1cb02	ror $2,%bx
66c1cb1f	rorw $31,%bx
66c10b1f	rorw $31,(%ebx)
66c1cb1f	ror $31,%bx
66c1cb00	rorw $0,%bx
66c10b00	rorw $0,(%ebx)
66c1cb00	ror $0,%bx
66d3cb	rorw %cl,%bx
66d30b	rorw %cl,(%ebx)
66d3cb	ror %cl,%bx
66c1cb07	rorw $7,%bx
66c10b07	rorw $7,(%ebx)
66c1cb07	ror $7,%bx
66c1cb1f	rorw $0x1f,%bx
66c10b1f	rorw $0x1f,(%ebx)
66c1cb1f	ror $0x1f,%bx
66c1cb3f	rorw $63,%bx
66c10b3f	rorw $63,(%ebx)
66c1cb3f	ror $63,%bx
66c1cb40	rorw $64,%bx
66c10b40	rorw $64,(%ebx)
66c1cb40	ror $64,%bx
66c1cbff	rorw $-1,%bx
66c10bff	rorw $-1,(%ebx)
66c1cbff	ror $-1,%bx
66d30d00000000 3:1:sym+0	rorw %cl,sym
refused	rorw %bx,%bx
refused	rorw %cl
d1cb	rorl $1,%ebx
d10b	rorl $1,(%ebx)
d1cb	ror $1,%ebx
c1cb02	rorl $2,%ebx
c10b02	rorl $2,(%ebx)
c1cb02	ror $2,%ebx
c1cb1f	rorl $31,%ebx
c10b1f	rorl $31,(%ebx)
c1cb1f	ror $31,%ebx
c1cb00	rorl $0,%ebx
c10b00	rorl $0,(%ebx)
c1cb00	ror $0,%ebx
d3cb	rorl %cl,%ebx
d30b	rorl %cl,(%ebx)
d3cb	ror %cl,%ebx
c1cb07	rorl $7,%ebx
c10b07	rorl $7,(%ebx)
c1cb07	ror $7,%ebx
c1cb1f	rorl $0x1f,%ebx
c10b1f	rorl $0x1f,(%ebx)
c1cb1f	ror $0x1f,%ebx
c1cb3f	rorl $63,%ebx
c10b3f	rorl $63,(%ebx)
c1cb3f	ror $63,%ebx
c1cb40	rorl $64,%ebx
c10b40	rorl $64,(%ebx)
c1cb40	ror $64,%ebx
c1cbff	rorl $-1,%ebx
c10bff	rorl $-1,(%ebx)
c1cbff	ror $-1,%ebx
d30d00000000 2:1:sym+0	rorl %cl,sym
refused	rorl %ebx,%ebx
refused	rorl %cl
d0d1	rclb $1,%cl
d013	rclb $1,(%ebx)
d0d1	rcl $1,%cl
c0d102	rclb $2,%cl
c01302	rclb $2,(%ebx)
c0d102	rcl $2,%cl
c0d11f	rclb $31,%cl
c0131f	rclb $31,(%ebx)
c0d11f	rcl $31,%cl
c0d100	rclb $0,%cl
c01300	rclb $0,(%ebx)
c0d100	rcl $0,%cl
d2d1	rclb %cl,%cl
d213	rclb %cl,(%ebx)
d2d1	rcl %cl,%cl
c0d107	rclb $7,%cl
c01307	rclb $7,(%ebx)
c0d107	rcl $7,%cl
c0d11f	rclb $0x1f,%cl
c0131f	rclb $0x1f,(%ebx)
c0d11f	rcl $0x1f,%cl
c0d13f	rclb $63,%cl
c0133f	rclb $63,(%ebx)
c0d13f	rcl $63,%cl
c0d140	rclb $64,%cl
c01340	rclb $64,(%ebx)
c0d140	rcl $64,%cl
c0d1ff	rclb $-1,%cl
c013ff	rclb $-1,(%ebx)
c0d1ff	rcl $-1,%cl
d21500000000 2:1:sym+0	rclb %cl,sym
d313	rcl %cl,(%ebx)
d113	rcl $1,(%ebx)
66d1d3	rclw $1,%bx
66d113	rclw $1,(%ebx)
66d1d3	rcl $1,%bx
66c1d302	rclw $2,%bx
66c11302	rclw $2,(%ebx)
66c1d302	rcl $2,%bx
66c1d31f	rclw $31,%bx
66c1131f	rclw $31,(%ebx)
66c1d31f	rcl $31,%bx
66c1d300	rclw $0,%bx
66c11300	rclw $0,(%ebx)
66c1d300	rcl $0,%bx
66d3d3	rclw %cl,%bx
66d313	rclw %cl,(%ebx)
66d3d3	rcl %cl,%bx
66c1d307	rclw $7,%bx
66c11307	rclw $7,(%ebx)
66c1d307	rcl $7,%bx
66c1d31f	rclw $0x1f,%bx
66c1131f	rclw $0x1f,(%ebx)
66c1d31f	rcl $0x1f,%bx
66c1d33f	rclw $63,%bx
66c1133f	rclw $63,(%ebx)
66c1d33f	rcl $63,%bx
66c1d340	rclw $64,%bx
66c11340	rclw $64,(%ebx)
66c1d340	rcl $64,%bx
refused	rclw $-1,%bx
refused	rclw $-1,(%ebx)
refused	rcl $-1,%bx
66d31500000000 3:1:sym+0	rclw %cl,sym
refused	rclw %bx,%bx
refused	rclw %cl
d1d3	rcll $1,%ebx
d113	rcll $1,(%ebx)
d1d3	rcl $1,%ebx
c1d302	rcll $2,%ebx
c11302	rcll $2,(%ebx)
c1d302	rcl $2,%ebx
c1d31f	rcll $31,%ebx
c1131f	rcll $31,(%ebx)
c1d31f	rcl $31,%ebx
c1d300	rcll $0,%ebx
c11300	rcll $0,(%ebx)
c1d300	rcl $0,%ebx
d3d3	rcll %cl,%ebx
d313	rcll %cl,(%ebx)
d3d3	rcl %cl,%ebx
c1d307	rcll $7,%ebx
c11307	rcll $7,(%ebx)
c1d307	rcl $7,%ebx
c1d31f	rcll $0x1f,%ebx
c1131f	rcll $0x1f,(%ebx)
c1d31f	rcl $0x1f,%ebx
c1d33f	rcll $63,%ebx
c1133f	rcll $63,(%ebx)
c1d33f	rcl $63,%ebx
c1d340	rcll $64,%ebx
c11340	rcll $64,(%ebx)
c1d340	rcl $64,%ebx
refused	rcll $-1,%ebx
refused	rcll $-1,(%ebx)
refused	rcl $-1,%ebx
d31500000000 2:1:sym+0	rcll %cl,sym
refused	rcll %ebx,%ebx
refused	rcll %cl
d0d9	rcrb $1,%cl
d01b	rcrb $1,(%ebx)
d0d9	rcr $1,%cl
c0d902	rcrb $2,%cl
c01b02	rcrb $2,(%ebx)
c0d902	rcr $2,%cl
c0d91f	rcrb $31,%cl
c01b1f	rcrb $31,(%ebx)
c0d91f	rcr $31,%cl
c0d900	rcrb $0,%cl
c01b00	rcrb $0,(%ebx)
c0d900	rcr $0,%cl
d2d9	rcrb %cl,%cl
d21b	rcrb %cl,(%ebx)
d2d9	rcr %cl,%cl
c0d907	rcrb $7,%cl
c01b07	rcrb $7,(%ebx)
c0d907	rcr $7,%cl
c0d91f	rcrb $0x1f,%cl
c01b1f	rcrb $0x1f,(%ebx)
c0d91f	rcr $0x1f,%cl
c0d93f	rcrb $63,%cl
c01b3f	rcrb $63,(%ebx)
c0d93f	rcr $63,%cl
c0d940	rcrb $64,%cl
c01b40	rcrb $64,(%ebx)
c0d940	rcr $64,%cl
c0d9ff	rcrb $-1,%cl
c01bff	rcrb $-1,(%ebx)
c0d9ff	rcr $-1,%cl
d21d00000000 2:1:sym+0	rcrb %cl,sym
d31b	rcr %cl,(%ebx)
d11b	rcr $1,(%ebx)
66d1db	rcrw $1,%bx
66d11b	rcrw $1,(%ebx)
66d1db	rcr $1,%bx
66c1db02	rcrw $2,%bx
66c11b02	rcrw $2,(%ebx)
66c1db02	rcr $2,%bx
66c1db1f	rcrw $31,%bx
66c11b1f	rcrw $31,(%ebx)
66c1db1f	rcr $31,%bx
66c1db00	rcrw $0,%bx
66c11b00	rcrw $0,(%ebx)
66c1db00	rcr $0,%bx
66d3db	rcrw %cl,%bx
66d31b	rcrw %cl,(%ebx)
66d3db	rcr %cl,%bx
66c1db07	rcrw $7,%bx
66c11b07	rcrw $7,(%ebx)
66c1db07	rcr $7,%bx
66c1db1f	rcrw $0x1f,%bx
66c11b1f	rcrw $0x1f,(%ebx)
66c1db1f	rcr $0x1f,%bx
66c1db3f	rcrw $63,%bx
66c11b3f	rcrw $63,(%ebx)
66c1db3f	rcr $63,%bx
66c1db40	rcrw $64,%bx
66c11b40	rcrw $64,(%ebx)
66c1db40	rcr $64,%bx
refused	rcrw $-1,%bx
refused	rcrw $-1,(%ebx)
refused	rcr $-1,%bx
66d31d00000000 3:1:sym+0	rcrw %cl,sym
refused	rcrw %bx,%bx
refused	rcrw %cl
d1db	rcrl $1,%ebx
d11b	rcrl $1,(%ebx)
d1db	rcr $1,%ebx
c1db02	rcrl $2,%ebx
c11b02	rcrl $2,(%ebx)
c1db02	rcr $2,%ebx
c1db1f	rcrl $31,%ebx
c11b1f	rcrl $31,(%ebx)
c1db1f	rcr $31,%ebx
c1db00	rcrl $0,%ebx
c11b00	rcrl $0,(%ebx)
c1db00	rcr $0,%ebx
d3db	rcrl %cl,%ebx
d31b	rcrl %cl,(%ebx)
d3db	rcr %cl,%ebx
c1db07	rcrl $7,%ebx
c11b07	rcrl $7,(%ebx)
c1db07	rcr $7,%ebx
c1db1f	rcrl $0x1f,%ebx
c11b1f	rcrl $0x1f,(%ebx)
c1db1f	rcr $0x1f,%ebx
c1db3f	rcrl $63,%ebx
c11b3f	rcrl $63,(%ebx)
c1db3f	rcr $63,%ebx
c1db40	rcrl $64,%ebx
c11b40	rcrl $64,(%ebx)
c1db40	rcr $64,%ebx
refused	rcrl $-1,%ebx
refused	rcrl $-1,(%ebx)
refused	rcr $-1,%ebx
d31d00000000 2:1:sym+0	rcrl %cl,sym
refused	rcrl %ebx,%ebx
refused	rcrl %cl
660fa4c304	shldw $4,%ax,%bx
660fa5c3	shldw %cl,%ax,%bx
660fa40304	shldw $4,%ax,(%ebx)
660fa503	shldw %cl,%ax,(%ebx)
660fa5c3	shldw %ax,%bx
660fa503	shldw %ax,(%ebx)
660fa4c304	shld $4,%ax,%bx
660fa50500000000 4:1:sym+0	shld %cl,%ax,sym
refused	shldw $4,(%ebx),%ax
refused	shldw $256,%ax,%bx
refused	shldb $1,%al,%bl
0fa4c304	shldl $4,%eax,%ebx
0fa5c3	shldl %cl,%eax,%ebx
0fa40304	shldl $4,%eax,(%ebx)
0fa503	shldl %cl,%eax,(%ebx)
0fa5c3	shldl %eax,%ebx
0fa503	shldl %eax,(%ebx)
0fa4c304	shld $4,%eax,%ebx
0fa50500000000 3:1:sym+0	shld %cl,%eax,sym
refused	shldl $4,(%ebx),%eax
refused	shldl $256,%eax,%ebx
660facc304	shrdw $4,%ax,%bx
660fadc3	shrdw %cl,%ax,%bx
660fac0304	shrdw $4,%ax,(%ebx)
660fad03	shrdw %cl,%ax,(%ebx)
660fadc3	shrdw %ax,%bx
660fad03	shrdw %ax,(%ebx)
660facc304	shrd $4,%ax,%bx
660fad0500000000 4:1:sym+0	shrd %cl,%ax,sym
refused	shrdw $4,(%ebx),%ax
refused	shrdw $256,%ax,%bx
refused	shrdb $1,%al,%bl
0facc304	shrdl $4,%eax,%ebx
0fadc3	shrdl %cl,%eax,%ebx
0fac0304	shrdl $4,%eax,(%ebx)
0fad03	shrdl %cl,%eax,(%ebx)
0fadc3	shrdl %eax,%ebx
0fad03	shrdl %eax,(%ebx)
0facc304	shrd $4,%eax,%ebx
0fad0500000000 3:1:sym+0	shrd %cl,%eax,sym
refused	shrdl $4,(%ebx),%eax
refused	shrdl $256,%eax,%ebx
666bd802	imulw $2,%ax,%bx
666bdb02	imulw $2,%bx
666b1b02	imulw $2,(%ebx),%bx
666bdb02	imul $2,%bx
666b1b02	imul $2,(%ebx),%bx
666bd87f	imulw $127,%ax,%bx
666bdb7f	imulw $127,%bx
666b1b7f	imulw $127,(%ebx),%bx
666bdb7f	imul $127,%bx
666b1b7f	imul $127,(%ebx),%bx
6669d88000	imulw $128,%ax,%bx
6669db8000	imulw $128,%bx
66691b8000	imulw $128,(%ebx),%bx
6669db8000	imul $128,%bx
66691b8000	imul $128,(%ebx),%bx
666bd880	imulw $-128,%ax,%bx
666bdb80	imulw $-128,%bx
666b1b80	imulw $-128,(%ebx),%bx
666bdb80	imul $-128,%bx
666b1b80	imul $-128,(%ebx),%bx
6669d87fff	imulw $-129,%ax,%bx
6669db7fff	imulw $-129,%bx
66691b7fff	imulw $-129,(%ebx),%bx
6669db7fff	imul $-129,%bx
66691b7fff	imul $-129,(%ebx),%bx
6669d80010	imulw $0x1000,%ax,%bx
6669db0010	imulw $0x1000,%bx
66691b0010	imulw $0x1000,(%ebx),%bx
6669db0010	imul $0x1000,%bx
66691b0010	imul $0x1000,(%ebx),%bx
6669d80000 3:20:sym+0	imulw $sym,%ax,%bx
6669db0000 3:20:sym+0	imulw $sym,%bx
66691b0000 3:20:sym+0	imulw $sym,(%ebx),%bx
6669db0000 3:20:sym+0	imul $sym,%bx
66691b0000 3:20:sym+0	imul $sym,(%ebx),%bx
6669d8ffff	imulw $0x7fffffff,%ax,%bx
6669dbffff	imulw $0x7fffffff,%bx
66691bffff	imulw $0x7fffffff,(%ebx),%bx
6669dbffff	imul $0x7fffffff,%bx
66691bffff	imul $0x7fffffff,(%ebx),%bx
6669d87856	imulw $0x12345678,%ax,%bx
6669db7856	imulw $0x12345678,%bx
66691b7856	imulw $0x12345678,(%ebx),%bx
6669db7856	imul $0x12345678,%bx
66691b7856	imul $0x12345678,(%ebx),%bx
660fafd8	imulw %ax,%bx
660faf1b	imulw (%ebx),%bx
660fafd8	imul %ax,%bx
660faf1d00000000 4:1:sym+0	imul sym,%bx
refused	imulw %ax,(%ebx)
refused	imulw $2,%ax,(%ebx)
refused	imulb $2,%al,%bl
refused	imulb %al,%bl
refused	mulw %ax,%bx
refused	mulw %ax,%ax
66f7f3	divw %bx,%ax
66f73b	idivw (%ebx),%ax
refused	imulw %bx,%ax,%bx
660fa3c3	btw %ax,%bx
660fa303	btw %ax,(%ebx)
660fbae303	btw $3,%bx
660fba2303	btw $3,(%ebx)
660fbae303	bt $3,%bx
660fa30500000000 4:1:sym+0	bt %ax,sym
0fba2303	bt $3,(%ebx)
660fbae3ff	btw $255,%bx
refused	btw $256,%bx
refused	btw (%ebx),%bx
refused	lock btw %ax,(%ebx)
refused	btb $1,%al
660fabc3	btsw %ax,%bx
660fab03	btsw %ax,(%ebx)
660fbaeb03	btsw $3,%bx
660fba2b03	btsw $3,(%ebx)
660fbaeb03	bts $3,%bx
660fab0500000000 4:1:sym+0	bts %ax,sym
0fba2b03	bts $3,(%ebx)
660fbaebff	btsw $255,%bx
refused	btsw $256,%bx
refused	btsw (%ebx),%bx
66f00fab03	lock btsw %ax,(%ebx)
refused	btsb $1,%al
660fb3c3	btrw %ax,%bx
660fb303	btrw %ax,(%ebx)
660fbaf303	btrw $3,%bx
660fba3303	btrw $3,(%ebx)
660fbaf303	btr $3,%bx
660fb30500000000 4:1:sym+0	btr %ax,sym
0fba3303	btr $3,(%ebx)
660fbaf3ff	btrw $255,%bx
refused	btrw $256,%bx
refused	btrw (%ebx),%bx
66f00fb303	lock btrw %ax,(%ebx)
refused	btrb $1,%al
660fbbc3	btcw %ax,%bx
660fbb03	btcw %ax,(%ebx)
660fbafb03	btcw $3,%bx
660fba3b03	btcw $3,(%ebx)
660fbafb03	btc $3,%bx
660fbb0500000000 4:1:sym+0	btc %ax,sym
0fba3b03	btc $3,(%ebx)
660fbafbff	btcw $255,%bx
refused	btcw $256,%bx
refused	btcw (%ebx),%bx
66f00fbb03	lock btcw %ax,(%ebx)
refused	btcb $1,%al
660fbcd8	bsfw %ax,%bx
660fbc1b	bsfw (%ebx),%bx
660fbcd8	bsf %ax,%bx
660fbc1d00000000 4:1:sym+0	bsf sym,%bx
refused	bsfw %ax,(%ebx)
refused	bsfw $1,%ax
660fbdd8	bsrw %ax,%bx
660fbd1b	bsrw (%ebx),%bx
660fbdd8	bsr %ax,%bx
660fbd1d00000000 4:1:sym+0	bsr sym,%bx
refused	bsrw %ax,(%ebx)
refused	bsrw $1,%ax
660f03d8	lslw %ax,%bx
660f031b	lslw (%ebx),%bx
660f03d8	lsl %ax,%bx
660f031d00000000 4:1:sym+0	lsl sym,%bx
refused	lslw %ax,(%ebx)
refused	lslw $1,%ax
660f02d8	larw %ax,%bx
660f021b	larw (%ebx),%bx
660f02d8	lar %ax,%bx
660f021d00000000 4:1:sym+0	lar sym,%bx
refused	larw %ax,(%ebx)
refused	larw $1,%ax
660f40d8	cmovow %ax,%bx
660f401b	cmovow (%ebx),%bx
660f40d8	cmovo %ax,%bx
660f401d00000000 4:1:sym+0	cmovo sym,%bx
refused	cmovow %ax,(%ebx)
refused	cmovow $1,%ax
66f30fb8d8	popcntw %ax,%bx
66f30fb81b	popcntw (%ebx),%bx
66f30fb8d8	popcnt %ax,%bx
66f30fb81d00000000 5:1:sym+0	popcnt sym,%bx
refused	popcntw %ax,(%ebx)
refused	popcntw $1,%ax
66f30fbdd8	lzcntw %ax,%bx
66f30fbd1b	lzcntw (%ebx),%bx
66f30fbdd8	lzcnt %ax,%bx
66f30fbd1d00000000 5:1:sym+0	lzcnt sym,%bx
refused	lzcntw %ax,(%ebx)
refused	lzcntw $1,%ax
66f30fbcd8	tzcntw %ax,%bx
66f30fbc1b	tzcntw (%ebx),%bx
66f30fbcd8	tzcnt %ax,%bx
66f30fbc1d00000000 5:1:sym+0	tzcnt sym,%bx
refused	tzcntw %ax,(%ebx)
refused	tzcntw $1,%ax
6693	xchgw %ax,%bx
6693	xchgw %bx,%ax
66871b	xchgw %bx,(%ebx)
66871b	xchgw (%ebx),%bx
6693	xchg %bx,%ax
66870500000000 3:1:sym+0	xchg %ax,sym
66f0871b	lock xchgw %bx,(%ebx)
6687db	xchgw %bx,%bx
6690	xchgw %ax,%ax
refused	xchgw $1,%ax
660fb1c3	cmpxchgw %ax,%bx
660fb1d8	cmpxchgw %bx,%ax
660fb11b	cmpxchgw %bx,(%ebx)
refused	cmpxchgw (%ebx),%bx
660fb1d8	cmpxchg %bx,%ax
660fb10500000000 4:1:sym+0	cmpxchg %ax,sym
66f00fb11b	lock cmpxchgw %bx,(%ebx)
660fb1db	cmpxchgw %bx,%bx
660fb1c0	cmpxchgw %ax,%ax
refused	cmpxchgw $1,%ax
660fc1c3	xaddw %ax,%bx
660fc1d8	xaddw %bx,%ax
660fc11b	xaddw %bx,(%ebx)
refused	xaddw (%ebx),%bx
660fc1d8	xadd %bx,%ax
660fc10500000000 4:1:sym+0	xadd %ax,sym
66f00fc11b	lock xaddw %bx,(%ebx)
660fc1db	xaddw %bx,%bx
660fc1c0	xaddw %ax,%ax
refused	xaddw $1,%ax
6685d8	test %bx,%ax
66850500000000 3:1:sym+0	test %ax,sym
6685db	testw %bx,%bx
6bd802	imull $2,%eax,%ebx
6bdb02	imull $2,%ebx
6b1b02	imull $2,(%ebx),%ebx
6bdb02	imul $2,%ebx
6b1b02	imul $2,(%ebx),%ebx
6bd87f	imull $127,%eax,%ebx
6bdb7f	imull $127,%ebx
6b1b7f	imull $127,(%ebx),%ebx
6bdb7f	imul $127,%ebx
6b1b7f	imul $127,(%ebx),%ebx
69d880000000	imull $128,%eax,%ebx
69db80000000	imull $128,%ebx
691b80000000	imull $128,(%ebx),%ebx
69db80000000	imul $128,%ebx
691b80000000	imul $128,(%ebx),%ebx
6bd880	imull $-128,%eax,%ebx
6bdb80	imull $-128,%ebx
6b1b80	imull $-128,(%ebx),%ebx
6bdb80	imul $-128,%ebx
6b1b80	imul $-128,(%ebx),%ebx
69d87fffffff	imull $-129,%eax,%ebx
69db7fffffff	imull $-129,%ebx
691b7fffffff	imull $-129,(%ebx),%ebx
69db7fffffff	imul $-129,%ebx
691b7fffffff	imul $-129,(%ebx),%ebx
69d800100000	imull $0x1000,%eax,%ebx
69db00100000	imull $0x1000,%ebx
691b00100000	imull $0x1000,(%ebx),%ebx
69db00100000	imul $0x1000,%ebx
691b00100000	imul $0x1000,(%ebx),%ebx
69db00000000 2:1:sym+0	imull $sym,%ebx
691b00000000 2:1:sym+0	imull $sym,(%ebx),%ebx
69db00000000 2:1:sym+0	imul $sym,%ebx
691b00000000 2:1:sym+0	imul $sym,(%ebx),%ebx
69d8ffffff7f	imull $0x7fffffff,%eax,%ebx
69dbffffff7f	imull $0x7fffffff,%ebx
691bffffff7f	imull $0x7fffffff,(%ebx),%ebx
69dbffffff7f	imul $0x7fffffff,%ebx
691bffffff7f	imul $0x7fffffff,(%ebx),%ebx
69d878563412	imull $0x12345678,%eax,%ebx
69db78563412	imull $0x12345678,%ebx
691b78563412	imull $0x12345678,(%ebx),%ebx
69db78563412	imul $0x12345678,%ebx
691b78563412	imul $0x12345678,(%ebx),%ebx
0fafd8	imull %eax,%ebx
0faf1b	imull (%ebx),%ebx
0fafd8	imul %eax,%ebx
0faf1d00000000 3:1:sym+0	imul sym,%ebx
refused	imull %eax,(%ebx)
refused	imull $2,%eax,(%ebx)
refused	mull %eax,%ebx
refused	mull %eax,%eax
f7f3	divl %ebx,%eax
f73b	idivl (%ebx),%eax
refused	imull %ebx,%eax,%ebx
0fa3c3	btl %eax,%ebx
0fa303	btl %eax,(%ebx)
0fbae303	btl $3,%ebx
0fba2303	btl $3,(%ebx)
0fbae303	bt $3,%ebx
0fa30500000000 3:1:sym+0	bt %eax,sym
0fbae3ff	btl $255,%ebx
refused	btl $256,%ebx
refused	btl (%ebx),%ebx
refused	lock btl %eax,(%ebx)
0fabc3	btsl %eax,%ebx
0fab03	btsl %eax,(%ebx)
0fbaeb03	btsl $3,%ebx
0fba2b03	btsl $3,(%ebx)
0fbaeb03	bts $3,%ebx
0fab0500000000 3:1:sym+0	bts %eax,sym
0fbaebff	btsl $255,%ebx
refused	btsl $256,%ebx
refused	btsl (%ebx),%ebx
f00fab03	lock btsl %eax,(%ebx)
0fb3c3	btrl %eax,%ebx
0fb303	btrl %eax,(%ebx)
0fbaf303	btrl $3,%ebx
0fba3303	btrl $3,(%ebx)
0fbaf303	btr $3,%ebx
0fb30500000000 3:1:sym+0	btr %eax,sym
0fbaf3ff	btrl $255,%ebx
refused	btrl $256,%ebx
refused	btrl (%ebx),%ebx
f00fb303	lock btrl %eax,(%ebx)
0fbbc3	btcl %eax,%ebx
0fbb03	btcl %eax,(%ebx)
0fbafb03	btcl $3,%ebx
0fba3b03	btcl $3,(%ebx)
0fbafb03	btc $3,%ebx
0fbb0500000000 3:1:sym+0	btc %eax,sym
0fbafbff	btcl $255,%ebx
refused	btcl $256,%ebx
refused	btcl (%ebx),%ebx
f00fbb03	lock btcl %eax,(%ebx)
0fbcd8	bsfl %eax,%ebx
0fbc1b	bsfl (%ebx),%ebx
0fbcd8	bsf %eax,%ebx
0fbc1d00000000 3:1:sym+0	bsf sym,%ebx
refused	bsfl %eax,(%ebx)
refused	bsfl $1,%eax
0fbdd8	bsrl %eax,%ebx
0fbd1b	bsrl (%ebx),%ebx
0fbdd8	bsr %eax,%ebx
0fbd1d00000000 3:1:sym+0	bsr sym,%ebx
refused	bsrl %eax,(%ebx)
refused	bsrl $1,%eax
0f03d8	lsll %eax,%ebx
0f031b	lsll (%ebx),%ebx
0f03d8	lsl %eax,%ebx
0f031d00000000 3:1:sym+0	lsl sym,%ebx
refused	lsll %eax,(%ebx)
refused	lsll $1,%eax
0f02d8	larl %eax,%ebx
0f021b	larl (%ebx),%ebx
0f02d8	lar %eax,%ebx
0f021d00000000 3:1:sym+0	lar sym,%ebx
refused	larl %eax,(%ebx)
refused	larl $1,%eax
0f40d8	cmovol %eax,%ebx
0f401b	cmovol (%ebx),%ebx
0f40d8	cmovo %eax,%ebx
0f401d00000000 3:1:sym+0	cmovo sym,%ebx
refused	cmovol %eax,(%ebx)
refused	cmovol $1,%eax
f30fb8d8	popcntl %eax,%ebx
f30fb81b	popcntl (%ebx),%ebx
f30fb8d8	popcnt %eax,%ebx
f30fb81d00000000 4:1:sym+0	popcnt sym,%ebx
refused	popcntl %eax,(%ebx)
refused	popcntl $1,%eax
f30fbdd8	lzcntl %eax,%ebx
f30fbd1b	lzcntl (%ebx),%ebx
f30fbdd8	lzcnt %eax,%ebx
f30fbd1d00000000 4:1:sym+0	lzcnt sym,%ebx
refused	lzcntl %eax,(%ebx)
refused	lzcntl $1,%eax
f30fbcd8	tzcntl %eax,%ebx
f30fbc1b	tzcntl (%ebx),%ebx
f30fbcd8	tzcnt %eax,%ebx
f30fbc1d00000000 4:1:sym+0	tzcnt sym,%ebx
refused	tzcntl %eax,(%ebx)
refused	tzcntl $1,%eax
93	xchgl %eax,%ebx
93	xchgl %ebx,%eax
871b	xchgl %ebx,(%ebx)
871b	xchgl (%ebx),%ebx
93	xchg %ebx,%eax
870500000000 2:1:sym+0	xchg %eax,sym
f0871b	lock xchgl %ebx,(%ebx)
87db	xchgl %ebx,%ebx
90	xchgl %eax,%eax
refused	xchgl $1,%eax
0fb1c3	cmpxchgl %eax,%ebx
0fb1d8	cmpxchgl %ebx,%eax
0fb11b	cmpxchgl %ebx,(%ebx)
refused	cmpxchgl (%ebx),%ebx
0fb1d8	cmpxchg %ebx,%eax
0fb10500000000 3:1:sym+0	cmpxchg %eax,sym
f00fb11b	lock cmpxchgl %ebx,(%ebx)
0fb1db	cmpxchgl %ebx,%ebx
0fb1c0	cmpxchgl %eax,%eax
refused	cmpxchgl $1,%eax
0fc1c3	xaddl %eax,%ebx
0fc1d8	xaddl %ebx,%eax
0fc11b	xaddl %ebx,(%ebx)
refused	xaddl (%ebx),%ebx
0fc1d8	xadd %ebx,%eax
0fc10500000000 3:1:sym+0	xadd %eax,sym
f00fc11b	lock xaddl %ebx,(%ebx)
0fc1db	xaddl %ebx,%ebx
0fc1c0	xaddl %eax,%eax
refused	xaddl $1,%eax
85d8	test %ebx,%eax
850500000000 2:1:sym+0	test %eax,sym
85db	testl %ebx,%ebx
86c3	xchgb %al,%bl
8603	xchgb %al,(%ebx)
860b	xchgb (%ebx),%cl
0fb00b	cmpxchgb %cl,(%ebx)
0fc0ca	xaddb %cl,%dl
0fc00d00000000 3:1:sym+0	xaddb %cl,sym
0fc70b	cmpxchg8b (%ebx)
0fc70d00000000 3:1:sym+0	cmpxchg8b sym
f00fc70b	lock cmpxchg8b (%ebx)
refused	cmpxchg8b %eax
90	xchg %eax,%eax
90	nop
660f1f00	nopw (%eax)
0f1f00	nopl (%eax)
0f1f0400	nopl 0(%eax,%eax,1)
660f1f0400	nopw 0(%eax,%eax,1)
0f1f00	nopl 0x0(%eax)
0f1f8000020000	nopl 0x200(%eax)
0f1fc0	nop %eax
660f1fc0	nopw %ax
f390	pause
6693	xchg %ax,%bx
91	xchg %eax,%ecx
91	xchg %ecx,%eax
refused	lock nop
refused	lock movl %eax,(%ebx)
refused	lock incl %eax
f0	lock
f3	rep
f2	repne
f0ff00	lock; incl (%eax)
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
refused	rep lock addl %eax,(%ebx)
f2f00103	xacquire lock addl %eax,(%ebx)
f38903	xrelease movl %eax,(%ebx)
3effe0	notrack jmp *%eax
refused	bnd jmp sym
e8fcffffff 1:2:sym+0	ds call sym
2e90	cs nop
26a4	es movsb
6690	data16 nop
6689c3	data16 movl %eax,%ebx
refused	addr16 movl %eax,(%eax)
refused	addr32 nop
6790	addr16 nop
refused	data32 nop
66	data16
67	addr32
666a01	data16 pushl $1
66e9feff 2:21:sym+0	data16 jmp sym
refused	addr16 jcxz 1f | 1:
660f0110	data16 lgdt (%eax)
66f3a4	data16 rep movsb
2e90	cs; nop
3e8903	ds; movl %eax,(%ebx)
26	es
64	fs
65	gs
36	ss
refused	rex.W nop
refused	rex64 movl %eax,%ebx
refused	rex movb %al,%bl
refused	rex.B incl %eax
refused	rex pushq %rax
refused	rex.WB popq %rax
refused	data16 rex.W nop
9b	wait
9b	fwait
f3a4	rep; movsb
f3a6	repe; cmpsb
f2ae	repne; scasb
f00fb10b	lock; cmpxchgl %ecx,(%ebx)
f08703	lock xchgl %eax,(%ebx)
f08703	lock xchgl (%ebx),%eax
f00fba2b01	lock btsl $1,(%ebx)
f00fc103	lock xaddl %eax,(%ebx)
refused	lock cmpl %eax,(%ebx)
refused	lock testl %eax,(%ebx)
refused	lock addl %eax,%ebx
0f90c0	seto %al
0f90c7	seto %bh
0f9003	seto (%ebx)
0f900500000000 3:1:sym+0	seto sym
0f400b	cmovol (%ebx),%ecx
660f401500000000 4:1:sym+0	cmovo sym,%dx
700190	jo 1f | nop | 1:
9070fd	1: nop | jo 1b
0f80fcffffff 2:2:sym+0	jo sym
0f80c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jo 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8032ffffff	1: .skip 200 | jo 1b
0f91c0	setno %al
0f91c7	setno %bh
0f9103	setno (%ebx)
0f910500000000 3:1:sym+0	setno sym
0f41d8	cmovno %eax,%ebx
0f410b	cmovnol (%ebx),%ecx
660f41d8	cmovnow %ax,%bx
660f411500000000 4:1:sym+0	cmovno sym,%dx
710190	jno 1f | nop | 1:
9071fd	1: nop | jno 1b
0f81fcffffff 2:2:sym+0	jno sym
0f81c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jno 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8132ffffff	1: .skip 200 | jno 1b
0f92c0	setb %al
0f92c7	setb %bh
0f9203	setb (%ebx)
0f920500000000 3:1:sym+0	setb sym
0f42d8	cmovb %eax,%ebx
0f420b	cmovbl (%ebx),%ecx
660f42d8	cmovbw %ax,%bx
660f421500000000 4:1:sym+0	cmovb sym,%dx
720190	jb 1f | nop | 1:
9072fd	1: nop | jb 1b
0f82fcffffff 2:2:sym+0	jb sym
0f82c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jb 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8232ffffff	1: .skip 200 | jb 1b
0f92c0	setc %al
0f92c7	setc %bh
0f9203	setc (%ebx)
0f920500000000 3:1:sym+0	setc sym
0f42d8	cmovc %eax,%ebx
0f420b	cmovcl (%ebx),%ecx
660f42d8	cmovcw %ax,%bx
660f421500000000 4:1:sym+0	cmovc sym,%dx
720190	jc 1f | nop | 1:
9072fd	1: nop | jc 1b
0f82fcffffff 2:2:sym+0	jc sym
0f82c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jc 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8232ffffff	1: .skip 200 | jc 1b
0f92c0	setnae %al
0f92c7	setnae %bh
0f9203	setnae (%ebx)
0f920500000000 3:1:sym+0	setnae sym
0f42d8	cmovnae %eax,%ebx
0f420b	cmovnael (%ebx),%ecx
660f42d8	cmovnaew %ax,%bx
660f421500000000 4:1:sym+0	cmovnae sym,%dx
720190	jnae 1f | nop | 1:
9072fd	1: nop | jnae 1b
0f82fcffffff 2:2:sym+0	jnae sym
0f82c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jnae 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8232ffffff	1: .skip 200 | jnae 1b
0f93c0	setae %al
0f93c7	setae %bh
0f9303	setae (%ebx)
0f930500000000 3:1:sym+0	setae sym
0f43d8	cmovae %eax,%ebx
0f430b	cmovael (%ebx),%ecx
660f43d8	cmovaew %ax,%bx
660f431500000000 4:1:sym+0	cmovae sym,%dx
730190	jae 1f | nop | 1:
9073fd	1: nop | jae 1b
0f83fcffffff 2:2:sym+0	jae sym
0f83c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jae 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8332ffffff	1: .skip 200 | jae 1b
0f93c0	setnb %al
0f93c7	setnb %bh
0f9303	setnb (%ebx)
0f930500000000 3:1:sym+0	setnb sym
0f43d8	cmovnb %eax,%ebx
0f430b	cmovnbl (%ebx),%ecx
660f43d8	cmovnbw %ax,%bx
660f431500000000 4:1:sym+0	cmovnb sym,%dx
730190	jnb 1f | nop | 1:
9073fd	1: nop | jnb 1b
0f83fcffffff 2:2:sym+0	jnb sym
0f83c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jnb 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8332ffffff	1: .skip 200 | jnb 1b
0f93c0	setnc %al
0f93c7	setnc %bh
0f9303	setnc (%ebx)
0f930500000000 3:1:sym+0	setnc sym
0f43d8	cmovnc %eax,%ebx
0f430b	cmovncl (%ebx),%ecx
660f43d8	cmovncw %ax,%bx
660f431500000000 4:1:sym+0	cmovnc sym,%dx
730190	jnc 1f | nop | 1:
9073fd	1: nop | jnc 1b
0f83fcffffff 2:2:sym+0	jnc sym
0f83c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jnc 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8332ffffff	1: .skip 200 | jnc 1b
0f94c0	sete %al
0f94c7	sete %bh
0f9403	sete (%ebx)
0f940500000000 3:1:sym+0	sete sym
0f44d8	cmove %eax,%ebx
0f440b	cmovel (%ebx),%ecx
660f44d8	cmovew %ax,%bx
660f441500000000 4:1:sym+0	cmove sym,%dx
740190	je 1f | nop | 1:
9074fd	1: nop | je 1b
0f84c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	je 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8432ffffff	1: .skip 200 | je 1b
0f94c0	setz %al
0f94c7	setz %bh
0f9403	setz (%ebx)
0f940500000000 3:1:sym+0	setz sym
0f44d8	cmovz %eax,%ebx
0f440b	cmovzl (%ebx),%ecx
660f44d8	cmovzw %ax,%bx
660f441500000000 4:1:sym+0	cmovz sym,%dx
740190	jz 1f | nop | 1:
9074fd	1: nop | jz 1b
0f84fcffffff 2:2:sym+0	jz sym
0f84c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jz 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8432ffffff	1: .skip 200 | jz 1b
0f95c0	setne %al
0f95c7	setne %bh
0f9503	setne (%ebx)
0f950500000000 3:1:sym+0	setne sym
0f45d8	cmovne %eax,%ebx
0f450b	cmovnel (%ebx),%ecx
660f45d8	cmovnew %ax,%bx
660f451500000000 4:1:sym+0	cmovne sym,%dx
750190	jne 1f | nop | 1:
9075fd	1: nop | jne 1b
0f85fcffffff 2:2:sym+0	jne sym
0f85c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jne 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8532ffffff	1: .skip 200 | jne 1b
0f95c0	setnz %al
0f95c7	setnz %bh
0f9503	setnz (%ebx)
0f950500000000 3:1:sym+0	setnz sym
0f45d8	cmovnz %eax,%ebx
0f450b	cmovnzl (%ebx),%ecx
660f45d8	cmovnzw %ax,%bx
660f451500000000 4:1:sym+0	cmovnz sym,%dx
750190	jnz 1f | nop | 1:
9075fd	1: nop | jnz 1b
0f85fcffffff 2:2:sym+0	jnz sym
0f85c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jnz 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8532ffffff	1: .skip 200 | jnz 1b
0f96c0	setbe %al
0f96c7	setbe %bh
0f9603	setbe (%ebx)
0f960500000000 3:1:sym+0	setbe sym
0f46d8	cmovbe %eax,%ebx
0f460b	cmovbel (%ebx),%ecx
660f46d8	cmovbew %ax,%bx
660f461500000000 4:1:sym+0	cmovbe sym,%dx
760190	jbe 1f | nop | 1:
9076fd	1: nop | jbe 1b
0f86fcffffff 2:2:sym+0	jbe sym
0f86c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jbe 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8632ffffff	1: .skip 200 | jbe 1b
0f96c0	setna %al
0f96c7	setna %bh
0f9603	setna (%ebx)
0f960500000000 3:1:sym+0	setna sym
0f46d8	cmovna %eax,%ebx
0f460b	cmovnal (%ebx),%ecx
660f46d8	cmovnaw %ax,%bx
660f461500000000 4:1:sym+0	cmovna sym,%dx
760190	jna 1f | nop | 1:
9076fd	1: nop | jna 1b
0f86fcffffff 2:2:sym+0	jna sym
0f86c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jna 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8632ffffff	1: .skip 200 | jna 1b
0f97c0	seta %al
0f97c7	seta %bh
0f9703	seta (%ebx)
0f970500000000 3:1:sym+0	seta sym
0f47d8	cmova %eax,%ebx
0f470b	cmoval (%ebx),%ecx
660f47d8	cmovaw %ax,%bx
660f471500000000 4:1:sym+0	cmova sym,%dx
770190	ja 1f | nop | 1:
9077fd	1: nop | ja 1b
0f87fcffffff 2:2:sym+0	ja sym
0f87c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	ja 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8732ffffff	1: .skip 200 | ja 1b
0f97c0	setnbe %al
0f97c7	setnbe %bh
0f9703	setnbe (%ebx)
0f970500000000 3:1:sym+0	setnbe sym
0f47d8	cmovnbe %eax,%ebx
0f470b	cmovnbel (%ebx),%ecx
660f47d8	cmovnbew %ax,%bx
660f471500000000 4:1:sym+0	cmovnbe sym,%dx
770190	jnbe 1f | nop | 1:
9077fd	1: nop | jnbe 1b
0f87fcffffff 2:2:sym+0	jnbe sym
0f87c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jnbe 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8732ffffff	1: .skip 200 | jnbe 1b
0f98c0	sets %al
0f98c7	sets %bh
0f9803	sets (%ebx)
0f980500000000 3:1:sym+0	sets sym
0f48d8	cmovs %eax,%ebx
0f480b	cmovsl (%ebx),%ecx
660f48d8	cmovsw %ax,%bx
660f481500000000 4:1:sym+0	cmovs sym,%dx
780190	js 1f | nop | 1:
9078fd	1: nop | js 1b
0f88fcffffff 2:2:sym+0	js sym
0f88c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	js 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8832ffffff	1: .skip 200 | js 1b
0f99c0	setns %al
0f99c7	setns %bh
0f9903	setns (%ebx)
0f990500000000 3:1:sym+0	setns sym
0f49d8	cmovns %eax,%ebx
0f490b	cmovnsl (%ebx),%ecx
660f49d8	cmovnsw %ax,%bx
660f491500000000 4:1:sym+0	cmovns sym,%dx
790190	jns 1f | nop | 1:
9079fd	1: nop | jns 1b
0f89fcffffff 2:2:sym+0	jns sym
0f89c80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jns 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8932ffffff	1: .skip 200 | jns 1b
0f9ac0	setp %al
0f9ac7	setp %bh
0f9a03	setp (%ebx)
0f9a0500000000 3:1:sym+0	setp sym
0f4ad8	cmovp %eax,%ebx
0f4a0b	cmovpl (%ebx),%ecx
660f4ad8	cmovpw %ax,%bx
660f4a1500000000 4:1:sym+0	cmovp sym,%dx
7a0190	jp 1f | nop | 1:
907afd	1: nop | jp 1b
0f8afcffffff 2:2:sym+0	jp sym
0f8ac80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jp 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8a32ffffff	1: .skip 200 | jp 1b
0f9ac0	setpe %al
0f9ac7	setpe %bh
0f9a03	setpe (%ebx)
0f9a0500000000 3:1:sym+0	setpe sym
0f4ad8	cmovpe %eax,%ebx
0f4a0b	cmovpel (%ebx),%ecx
660f4ad8	cmovpew %ax,%bx
660f4a1500000000 4:1:sym+0	cmovpe sym,%dx
7a0190	jpe 1f | nop | 1:
907afd	1: nop | jpe 1b
0f8afcffffff 2:2:sym+0	jpe sym
0f8ac80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jpe 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8a32ffffff	1: .skip 200 | jpe 1b
0f9bc0	setnp %al
0f9bc7	setnp %bh
0f9b03	setnp (%ebx)
0f9b0500000000 3:1:sym+0	setnp sym
0f4bd8	cmovnp %eax,%ebx
0f4b0b	cmovnpl (%ebx),%ecx
660f4bd8	cmovnpw %ax,%bx
660f4b1500000000 4:1:sym+0	cmovnp sym,%dx
7b0190	jnp 1f | nop | 1:
907bfd	1: nop | jnp 1b
0f8bfcffffff 2:2:sym+0	jnp sym
0f8bc80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jnp 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8b32ffffff	1: .skip 200 | jnp 1b
0f9bc0	setpo %al
0f9bc7	setpo %bh
0f9b03	setpo (%ebx)
0f9b0500000000 3:1:sym+0	setpo sym
0f4bd8	cmovpo %eax,%ebx
0f4b0b	cmovpol (%ebx),%ecx
660f4bd8	cmovpow %ax,%bx
660f4b1500000000 4:1:sym+0	cmovpo sym,%dx
7b0190	jpo 1f | nop | 1:
907bfd	1: nop | jpo 1b
0f8bfcffffff 2:2:sym+0	jpo sym
0f8bc80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jpo 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8b32ffffff	1: .skip 200 | jpo 1b
0f9cc0	setl %al
0f9cc7	setl %bh
0f9c03	setl (%ebx)
0f9c0500000000 3:1:sym+0	setl sym
0f4cd8	cmovl %eax,%ebx
0f4c0b	cmovll (%ebx),%ecx
660f4cd8	cmovlw %ax,%bx
660f4c1500000000 4:1:sym+0	cmovl sym,%dx
7c0190	jl 1f | nop | 1:
907cfd	1: nop | jl 1b
0f8cfcffffff 2:2:sym+0	jl sym
0f8cc80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jl 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8c32ffffff	1: .skip 200 | jl 1b
0f9cc0	setnge %al
0f9cc7	setnge %bh
0f9c03	setnge (%ebx)
0f9c0500000000 3:1:sym+0	setnge sym
0f4cd8	cmovnge %eax,%ebx
0f4c0b	cmovngel (%ebx),%ecx
660f4cd8	cmovngew %ax,%bx
660f4c1500000000 4:1:sym+0	cmovnge sym,%dx
7c0190	jnge 1f | nop | 1:
907cfd	1: nop | jnge 1b
0f8cfcffffff 2:2:sym+0	jnge sym
0f8cc80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jnge 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8c32ffffff	1: .skip 200 | jnge 1b
0f9dc0	setge %al
0f9dc7	setge %bh
0f9d03	setge (%ebx)
0f9d0500000000 3:1:sym+0	setge sym
0f4dd8	cmovge %eax,%ebx
0f4d0b	cmovgel (%ebx),%ecx
660f4dd8	cmovgew %ax,%bx
660f4d1500000000 4:1:sym+0	cmovge sym,%dx
7d0190	jge 1f | nop | 1:
907dfd	1: nop | jge 1b
0f8dfcffffff 2:2:sym+0	jge sym
0f8dc80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jge 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8d32ffffff	1: .skip 200 | jge 1b
0f9dc0	setnl %al
0f9dc7	setnl %bh
0f9d03	setnl (%ebx)
0f9d0500000000 3:1:sym+0	setnl sym
0f4dd8	cmovnl %eax,%ebx
0f4d0b	cmovnll (%ebx),%ecx
660f4dd8	cmovnlw %ax,%bx
660f4d1500000000 4:1:sym+0	cmovnl sym,%dx
7d0190	jnl 1f | nop | 1:
907dfd	1: nop | jnl 1b
0f8dfcffffff 2:2:sym+0	jnl sym
0f8dc80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jnl 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8d32ffffff	1: .skip 200 | jnl 1b
0f9ec0	setle %al
0f9ec7	setle %bh
0f9e03	setle (%ebx)
0f9e0500000000 3:1:sym+0	setle sym
0f4ed8	cmovle %eax,%ebx
0f4e0b	cmovlel (%ebx),%ecx
660f4ed8	cmovlew %ax,%bx
660f4e1500000000 4:1:sym+0	cmovle sym,%dx
7e0190	jle 1f | nop | 1:
907efd	1: nop | jle 1b
0f8efcffffff 2:2:sym+0	jle sym
0f8ec80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jle 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8e32ffffff	1: .skip 200 | jle 1b
0f9ec0	setng %al
0f9ec7	setng %bh
0f9e03	setng (%ebx)
0f9e0500000000 3:1:sym+0	setng sym
0f4ed8	cmovng %eax,%ebx
0f4e0b	cmovngl (%ebx),%ecx
660f4ed8	cmovngw %ax,%bx
660f4e1500000000 4:1:sym+0	cmovng sym,%dx
7e0190	jng 1f | nop | 1:
907efd	1: nop | jng 1b
0f8efcffffff 2:2:sym+0	jng sym
0f8ec80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jng 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8e32ffffff	1: .skip 200 | jng 1b
0f9fc0	setg %al
0f9fc7	setg %bh
0f9f03	setg (%ebx)
0f9f0500000000 3:1:sym+0	setg sym
0f4fd8	cmovg %eax,%ebx
0f4f0b	cmovgl (%ebx),%ecx
660f4fd8	cmovgw %ax,%bx
660f4f1500000000 4:1:sym+0	cmovg sym,%dx
7f0190	jg 1f | nop | 1:
907ffd	1: nop | jg 1b
0f8ffcffffff 2:2:sym+0	jg sym
0f8fc80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jg 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8f32ffffff	1: .skip 200 | jg 1b
0f9fc0	setnle %al
0f9fc7	setnle %bh
0f9f03	setnle (%ebx)
0f9f0500000000 3:1:sym+0	setnle sym
0f4fd8	cmovnle %eax,%ebx
0f4f0b	cmovnlel (%ebx),%ecx
660f4fd8	cmovnlew %ax,%bx
660f4f1500000000 4:1:sym+0	cmovnle sym,%dx
7f0190	jnle 1f | nop | 1:
907ffd	1: nop | jnle 1b
0f8ffcffffff 2:2:sym+0	jnle sym
0f8fc80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jnle 1f | .skip 200 | 1:
00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f8f32ffffff	1: .skip 200 | jnle 1b
refused	seta %ax
refused	seta %eax
0f97c0	setab %al
refused	cmovab %al,%bl
refused	cmova %eax,(%ebx)
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
e200	loopl 1f | 1:
67e200	loopw 1f | 1:
e9fcffffffe9fcffffff 1:2:sym+0 6:2:sym+0	jmp sym | jmp sym
e800000000	call 1f | 1:
e82c010000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	call 1f | .skip 300 | 1:
000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000e8cffeffff	1: .skip 300 | call 1b
eb7a7478000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	jmp 1f | je 1f | .skip 120 | 1:
e9e200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000eb650000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000090	jmp 2f | 1: .skip 124 | jmp 1f | .skip 100 | 2: nop | 1:
6a01	push $1
6a7f	push $127
6880000000	push $128
6a80	push $-128
687fffffff	push $-129
6800100000	push $0x1000
6aff	push $0xffffffff
6aff	push $-1
66680010	pushw $0x1000
666aff	pushw $-1
666aff	pushw $0xffff
66688000	pushw $128
6878563412	pushl $0x12345678
66ff30	pushw (%eax)
668f00	popw (%eax)
ff30	push (%eax)
8f00	pop (%eax)
refused	pop $1
8f0500000000 2:1:sym+0	pop sym
8f442408	pop 8(%esp)
refused	pushl %eax,%ebx
51	pushl %ecx
52	pushl %edx
56	pushl %esi
57	pushl %edi
59	popl %ecx
5a	popl %edx
5e	popl %esi
5f	popl %edi
51	push %ecx
52	push %edx
56	push %esi
57	push %edi
6651	pushw %cx
6652	pushw %dx
6654	pushw %sp
6655	pushw %bp
6657	pushw %di
c20000	ret $0
ca0400	lret $4
c8080000	enter $8,$0
c8001003	enter $0x1000,$3
refused	enter $8
66c8080000	enterw $8,$0
c8080000	enterl $8,$0
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
d7	xlat (%ebx)
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
refused	bswap (%eax)
f30f1efb	endbr32
f30f1efa	endbr64
0f01e8	serialize
0f0110	lgdt (%eax)
0f0118	lidt (%eax)
0f0100	sgdt (%eax)
0f0108	sidt (%eax)
0f011500000000 3:1:sym+0	lgdt sym
0f011d00000000 3:1:sym+0	lidt sym
0f010500000000 3:1:sym+0	sgdt sym
0f010d00000000 3:1:sym+0	sidt sym
0f0110	lgdtl (%eax)
660f0110	lgdtw (%eax)
refused	lgdtq (%eax)
0f0100	sgdtl (%eax)
660f0108	sidtw (%eax)
refused	lgdt %eax
0f00d0	lldt %ax
660f00c0	sldt %ax
0f00c0	sldt %eax
0f0000	sldt (%eax)
0f0000	sldtw (%eax)
0f0010	lldt (%eax)
0f001500000000 3:1:sym+0	lldtw sym
0f00d8	ltr %ax
0f0018	ltr (%eax)
0f001d00000000 3:1:sym+0	ltrw sym
660f00c8	str %ax
0f00c8	str %eax
0f0008	str (%eax)
0f000d00000000 3:1:sym+0	strw sym
0f01f0	lmsw %ax
0f0130	lmsw (%eax)
660f01e0	smsw %ax
0f01e0	smsw %eax
0f0120	smsw (%eax)
0f00e0	verr %ax
0f00e8	verw %ax
0f0020	verr (%eax)
0f002d00000000 3:1:sym+0	verw sym
0f0138	invlpg (%eax)
0f013d00000000 3:1:sym+0	invlpg sym
refused	invlpg %eax
0f00d0	lldt %eax
0f00d8	ltr %eax
refused	lmsw %eax
0f00e0	verr %eax
0fae38	clflush (%eax)
660fae38	clflushopt (%eax)
0f1800	prefetchnta (%eax)
0f1808	prefetcht0 (%eax)
0f181500000000 3:1:sym+0	prefetcht1 sym
0f1818	prefetcht2 (%eax)
0f0d00	prefetch (%eax)
0f0d08	prefetchw (%eax)
0fc303	movnti %eax,(%ebx)
0f38f103	movbe %eax,(%ebx)
0f38f003	movbe (%ebx),%eax
f20f38f0d8	crc32b %al,%ebx
f20f38f1d8	crc32l %eax,%ebx
0fae00	fxsave (%eax)
0fae08	fxrstor (%eax)
0fae20	xsave (%eax)
0fae28	xrstor (%eax)
0fae30	xsaveopt (%eax)
0fae10	ldmxcsr (%eax)
0fae18	stmxcsr (%eax)
refused	rdfsbase %eax
refused	wrgsbase %eax
660f388218	invpcid (%eax),%ebx
f30fc7f8	rdpid %eax
refused	ret %eax
refused	hlt %eax
refused	cli $1
refused	nop $1
refused	cpuid %eax
refused	leave $1
refused	int
refused	int %eax
cd00 1:22:sym+0	int $sym
c20000 1:20:sym+0	ret $sym
c8000001 1:20:sym+0	enter $sym,$1
refused	lea %eax,%ebx
668d18	lea (%eax),%bx
668d18	leaw (%eax),%bx
8d0500000000 2:1:sym+0	leal sym,%eax
8d0500000000 2:1:sym+0	lea sym,%eax
refused	leab (%eax),%al
refused	lea (%eax),%al
refused	leal $1,%eax
refused	leal (%eax),sym
refused	lea (%eax),%ds
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
6e	outsb
666f	outsw
6f	outsl
6f	outs
a4	movsb (%esi),%es:(%edi)
a5	movsl (%esi),%es:(%edi)
a5	movsl %ds:(%esi),%es:(%edi)
64a5	movsl %fs:(%esi),%es:(%edi)
2e66a5	movsw %cs:(%esi),%es:(%edi)
a5	movs (%esi),%es:(%edi)
67a4	movsb (%si),%es:(%di)
a6	cmpsb %es:(%edi),(%esi)
65a7	cmpsl %es:(%edi),%gs:(%esi)
ae	scasb %es:(%edi),%al
af	scasl %es:(%edi),%eax
66af	scas %es:(%edi),%ax
aa	stosb %al,%es:(%edi)
ab	stosl %eax,%es:(%edi)
66ab	stos %ax,%es:(%edi)
ab	stos %eax,(%edi)
ac	lodsb (%esi),%al
64ad	lodsl %fs:(%esi),%eax
66ad	lods (%esi),%ax
ad	lods (%esi)
6c	insb (%dx),%es:(%edi)
6d	insl (%dx),%es:(%edi)
6e	outsb (%esi),(%dx)
656f	outsl %gs:(%esi),(%dx)
a5	movsl (%edi),%es:(%esi)
refused	movsl (%esi),%ds:(%edi)
refused	stosl %eax,%ds:(%edi)
a5	movsl (%eax),%es:(%ebx)
refused	stosb %bl,%es:(%edi)
refused	lodsl (%esi),%ebx
refused	movsbl (%esi),%es:(%edi)
a5	smovl
af	ssca
ad	slod
ab	ssto
a7	scmp
67ae	scasb (%di),%al
67ac	lodsb (%si)
67ab	stosl %eax,(%di)
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
e400 1:22:sym+0	in $sym,%al
e600 1:22:sym+0	out %al,$sym
d903	flds (%ebx)
dd03	fldl (%ebx)
db2b	fldt (%ebx)
d9c1	fld %st(1)
d9c0	fld %st
d9c7	fld %st(7)
d903	fld (%ebx)
d90500000000 2:1:sym+0	flds sym
dd442408	fldl 8(%esp)
db2d00000000 2:1:sym+0	fldt sym
refused	fld %st(8)
refused	fld
refused	fldq (%ebx)
refused	fld %eax
d913	fsts (%ebx)
dd13	fstl (%ebx)
refused	fstt (%ebx)
ddd1	fst %st(1)
ddd0	fst %st
ddd7	fst %st(7)
d913	fst (%ebx)
d91500000000 2:1:sym+0	fsts sym
dd542408	fstl 8(%esp)
refused	fstt sym
refused	fst %st(8)
refused	fst
refused	fstq (%ebx)
refused	fst %eax
d91b	fstps (%ebx)
dd1b	fstpl (%ebx)
db3b	fstpt (%ebx)
ddd9	fstp %st(1)
ddd8	fstp %st
dddf	fstp %st(7)
d91b	fstp (%ebx)
d91d00000000 2:1:sym+0	fstps sym
dd5c2408	fstpl 8(%esp)
db3d00000000 2:1:sym+0	fstpt sym
refused	fstp %st(8)
refused	fstp
refused	fstpq (%ebx)
refused	fstp %eax
df03	filds (%ebx)
db03	fildl (%ebx)
df2b	fildq (%ebx)
df2b	fildll (%ebx)
df03	fild (%ebx)
df0500000000 2:1:sym+0	filds sym
df2d00000000 2:1:sym+0	fildll sym
refused	fildw (%ebx)
refused	fild %st(1)
df13	fists (%ebx)
db13	fistl (%ebx)
refused	fistq (%ebx)
refused	fistll (%ebx)
df13	fist (%ebx)
df1500000000 2:1:sym+0	fists sym
refused	fistll sym
refused	fistw (%ebx)
refused	fist %st(1)
df1b	fistps (%ebx)
db1b	fistpl (%ebx)
df3b	fistpq (%ebx)
df3b	fistpll (%ebx)
df1b	fistp (%ebx)
df1d00000000 2:1:sym+0	fistps sym
df3d00000000 2:1:sym+0	fistpll sym
refused	fistpw (%ebx)
refused	fistp %st(1)
df0b	fisttps (%ebx)
db0b	fisttpl (%ebx)
dd0b	fisttpq (%ebx)
dd0b	fisttpll (%ebx)
df0b	fisttp (%ebx)
df0d00000000 2:1:sym+0	fisttps sym
dd0d00000000 2:1:sym+0	fisttpll sym
refused	fisttpw (%ebx)
refused	fisttp %st(1)
d803	fadds (%ebx)
dc03	faddl (%ebx)
d803	fadd (%ebx)
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
refused	faddt (%ebx)
d80500000000 2:1:sym+0	fadds sym
dc0500000000 2:1:sym+0	faddl sym
d8c0	fadd %st(0)
refused	faddp %st(2),%st(1)
d823	fsubs (%ebx)
dc23	fsubl (%ebx)
d823	fsub (%ebx)
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
refused	fsubt (%ebx)
d82500000000 2:1:sym+0	fsubs sym
dc2500000000 2:1:sym+0	fsubl sym
d8e0	fsub %st(0)
refused	fsubp %st(2),%st(1)
d82b	fsubrs (%ebx)
dc2b	fsubrl (%ebx)
d82b	fsubr (%ebx)
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
refused	fsubrt (%ebx)
d82d00000000 2:1:sym+0	fsubrs sym
dc2d00000000 2:1:sym+0	fsubrl sym
d8e8	fsubr %st(0)
refused	fsubrp %st(2),%st(1)
d80b	fmuls (%ebx)
dc0b	fmull (%ebx)
d80b	fmul (%ebx)
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
refused	fmult (%ebx)
d80d00000000 2:1:sym+0	fmuls sym
dc0d00000000 2:1:sym+0	fmull sym
d8c8	fmul %st(0)
refused	fmulp %st(2),%st(1)
d833	fdivs (%ebx)
dc33	fdivl (%ebx)
d833	fdiv (%ebx)
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
refused	fdivt (%ebx)
d83500000000 2:1:sym+0	fdivs sym
dc3500000000 2:1:sym+0	fdivl sym
d8f0	fdiv %st(0)
refused	fdivp %st(2),%st(1)
d83b	fdivrs (%ebx)
dc3b	fdivrl (%ebx)
d83b	fdivr (%ebx)
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
refused	fdivrt (%ebx)
d83d00000000 2:1:sym+0	fdivrs sym
dc3d00000000 2:1:sym+0	fdivrl sym
d8f8	fdivr %st(0)
refused	fdivrp %st(2),%st(1)
d813	fcoms (%ebx)
dc13	fcoml (%ebx)
d813	fcom (%ebx)
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
refused	fcomt (%ebx)
d81500000000 2:1:sym+0	fcoms sym
dc1500000000 2:1:sym+0	fcoml sym
d8d0	fcom %st(0)
refused	fcomp %st(2),%st(1)
d81b	fcomps (%ebx)
dc1b	fcompl (%ebx)
d81b	fcomp (%ebx)
refused	fcomp %st(3),%st(0)
refused	fcomp %st(0),%st(3)
refused	fcomp %st,%st
ded9	fcompp
refused	fcompp %st(1)
refused	fcompp %st,%st(1)
refused	fcompp %st,%st(3)
refused	fcompp %st(1),%st
refused	fcomp %st(1),%st(2)
refused	fcompt (%ebx)
d81d00000000 2:1:sym+0	fcomps sym
dc1d00000000 2:1:sym+0	fcompl sym
d8d8	fcomp %st(0)
refused	fcompp %st(2),%st(1)
de03	fiadds (%ebx)
da03	fiaddl (%ebx)
de03	fiadd (%ebx)
refused	fiaddq (%ebx)
de0500000000 2:1:sym+0	fiadds sym
refused	fiaddw (%ebx)
de23	fisubs (%ebx)
da23	fisubl (%ebx)
de23	fisub (%ebx)
refused	fisubq (%ebx)
de2500000000 2:1:sym+0	fisubs sym
refused	fisubw (%ebx)
de2b	fisubrs (%ebx)
da2b	fisubrl (%ebx)
de2b	fisubr (%ebx)
refused	fisubrq (%ebx)
de2d00000000 2:1:sym+0	fisubrs sym
refused	fisubrw (%ebx)
de0b	fimuls (%ebx)
da0b	fimull (%ebx)
de0b	fimul (%ebx)
refused	fimulq (%ebx)
de0d00000000 2:1:sym+0	fimuls sym
refused	fimulw (%ebx)
de33	fidivs (%ebx)
da33	fidivl (%ebx)
de33	fidiv (%ebx)
refused	fidivq (%ebx)
de3500000000 2:1:sym+0	fidivs sym
refused	fidivw (%ebx)
de3b	fidivrs (%ebx)
da3b	fidivrl (%ebx)
de3b	fidivr (%ebx)
refused	fidivrq (%ebx)
de3d00000000 2:1:sym+0	fidivrs sym
refused	fidivrw (%ebx)
de13	ficoms (%ebx)
da13	ficoml (%ebx)
de13	ficom (%ebx)
refused	ficomq (%ebx)
de1500000000 2:1:sym+0	ficoms sym
refused	ficomw (%ebx)
de1b	ficomps (%ebx)
da1b	ficompl (%ebx)
de1b	ficomp (%ebx)
refused	ficompq (%ebx)
de1d00000000 2:1:sym+0	ficomps sym
refused	ficompw (%ebx)
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
dd38	fnstsw (%eax)
9bdd38	fstsw (%eax)
dd3d00000000 2:1:sym+0	fnstsw sym
refused	fnstsw %eax
refused	fnstsw %bx
d928	fldcw (%eax)
d938	fnstcw (%eax)
9bd938	fstcw (%eax)
d92d00000000 2:1:sym+0	fldcw sym
d93d00000000 2:1:sym+0	fnstcw sym
9bd93d00000000 3:1:sym+0	fstcw sym
refused	fldcw %ax
d920	fldenv (%eax)
d930	fnstenv (%eax)
9bd930	fstenv (%eax)
dd20	frstor (%eax)
dd30	fnsave (%eax)
9bdd30	fsave (%eax)
dbe3	fninit
9bdbe3	finit
dbe2	fnclex
9bdbe2	fclex
df20	fbld (%eax)
df30	fbstp (%eax)
66d920	fldenvs (%eax)
d930	fnstenvl (%eax)
9bdbe4	fsetpm
9bdbe1	fdisi
9bdbe0	feni
dbe0	fneni
dbe1	fndisi
refused	fstp1 %st(1)
da1d00000000 2:1:sym+0	ficompl sym
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
89c3	.code16 | movw %ax,%bx
6689c3	.code16 | movl %eax,%ebx
8b07	.code16 | movw (%bx),%ax
678b00	.code16 | movw (%eax),%ax
66b801000000	.code16 | movl $1,%eax
b80100	.code16 | movw $1,%ax
50	.code16 | pushw %ax
6650	.code16 | pushl %eax
50	.code16 | push %ax
6650	.code16 | push %eax
eb00	.code16 | jmp 1f | 1:
e9c8000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	.code16 | jmp 1f | .skip 200 | 1:
e8feff 1:21:sym+0	.code16 | call sym
0f84c8000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000	.code16 | je 1f | .skip 200 | 1:
ea00100800	.code16 | ljmp $0x8,$0x1000
66ea001000000800	.code16 | ljmpl $0x8,$0x1000
ea00000800 1:20:sym+0	.code16 | ljmpw $0x8,$sym
66ea000000000800 2:1:sym+0	.code16 | ljmpl $0x8,$sym
0f01160000 3:20:sym+0	.code16 | lgdt sym
660f01160000 4:20:sym+0	.code16 | lgdtl sym
0f011e0000 3:20:sym+0	.code16 | lidtw sym
a10000 1:20:sym+0	.code16 | movw sym,%ax
66a10000 2:20:sym+0	.code16 | movl sym,%eax
a30000 1:20:sym+0	.code16 | movw %ax,sym
a00000 1:20:sym+0	.code16 | movb sym,%al
0f20c0	.code16 | movl %cr0,%eax
0f22c0	.code16 | movl %eax,%cr0
c3	.code16 | ret
c3	.code16 | retw
66c3	.code16 | retl
cb	.code16 | lret
cf	.code16 | iret
cd10	.code16 | int $0x10
83c001	.code16 | addw $1,%ax
050010	.code16 | addw $0x1000,%ax
660500100000	.code16 | addl $0x1000,%eax
66830701	.code16 | addl $1,(%bx)
830001	.code16 | addw $1,(%bx,%si)
8b4608	.code16 | movw 8(%bp),%ax
8b870000 2:20:sym+0	.code16 | movw sym(%bx),%ax
8d7c04	.code16 | leaw 4(%si),%di
67668d7e04	.code16 | leal 4(%esi),%edi
678d3c58	.code16 | leaw (%eax,%ebx,2),%di
8ed8	.code16 | movw %ax,%ds
8cd8	.code16 | movw %ds,%ax
b80000 1:20:sym+0	.code16 | movw $sym,%ax
66b800000000 2:1:sym+0	.code16 | movl $sym,%eax
6a01	.code16 | pushw $1
666a01	.code16 | pushl $1
6a01	.code16 | push $1
680010	.code16 | push $0x1000
60	.code16 | pusha
60	.code16 | pushaw
6660	.code16 | pushal
61	.code16 | popa
9c	.code16 | pushf
669c	.code16 | pushfl
9d	.code16 | popf
c9	.code16 | leave
c8080000	.code16 | enter $8,$0
ffd0	.code16 | call *%ax
66ffd0	.code16 | call *%eax
ffe3	.code16 | jmp *%bx
ff27	.code16 | jmp *(%bx)
66e8fcffffff 2:2:sym+0	.code16 | calll sym
refused	.code16 | jmpl sym
66e9fcffffff 2:2:sym+0	.code16 | data32 jmp sym
678b00	.code16 | addr32 movw (%eax),%ax
a4	.code16 | movsb
a5	.code16 | movsw
66a5	.code16 | movsl
f3ab	.code16 | rep stosw
66f3ab	.code16 | rep stosl
aa	.code16 | stosb
ad	.code16 | lodsw
e460	.code16 | inb $0x60,%al
ed	.code16 | inw %dx,%ax
66ed	.code16 | inl %dx,%eax
ef	.code16 | outw %ax,%dx
31c0	.code16 | xorw %ax,%ax
6631c0	.code16 | xorl %eax,%eax
31c0	.code16 | xor %ax,%ax
6631c0	.code16 | xor %eax,%eax
40	.code16 | incw %ax
6640	.code16 | incl %eax
4b	.code16 | decw %bx
fa	.code16 | cli
f4	.code16 | hlt
98	.code16 | cbw
6698	.code16 | cwde
99	.code16 | cwd
6699	.code16 | cdq
98	.code16 | cbtw
6698	.code16 | cwtl
99	.code16 | cwtd
6699	.code16 | cltd
0fb6d8	.code16 | movzbw %al,%bx
660fb6d8	.code16 | movzbl %al,%ebx
660fb7d8	.code16 | movzwl %ax,%ebx
c1e004	.code16 | shlw $4,%ax
66c1e004	.code16 | shll $4,%eax
6bd80a	.code16 | imulw $10,%ax,%bx
6669d8e8030000	.code16 | imull $1000,%eax,%ebx
a90001	.code16 | testw $0x100,%ax
66a900010000	.code16 | testl $0x100,%eax
813f0010	.code16 | cmpw $0x1000,(%bx)
66813f00100000	.code16 | cmpl $0x1000,(%bx)
66c70778563412	.code16 | movl $0x12345678,(%bx)
c70600003412 2:20:sym+0	.code16 | movw $0x1234,sym
268b05	.code16 | movw %es:(%di),%ax
64a10000 2:20:sym+0	.code16 | movw %fs:sym,%ax
e200	.code16 | loop 1f | 1:
e300	.code16 | jcxz 1f | 1:
67e300	.code16 | jecxz 1f | 1:
9a00100800	.code16 | lcall $0x8,$0x1000
ff1f	.code16 | lcall *(%bx)
ff2f	.code16 | ljmp *(%bx)
66ff2f	.code16 | ljmpl *(%bx)
660fc8	.code16 | bswap %eax
660f44d8	.code16 | cmovel %eax,%ebx
0f44d8	.code16 | cmovew %ax,%bx
0f94c0	.code16 | setz %al
93	.code16 | xchgw %ax,%bx
6693	.code16 | xchgl %eax,%ebx
90	.code16 | nop
0f0117	.code16 | lgdt (%bx)
0f01e0	.code16 | smsw %ax
0f01f0	.code16 | lmsw %ax
0f0104	.code16 | sgdt (%si)
d907	.code16 | flds (%bx)
dfe0	.code16 | fnstsw %ax
8cc8	.code16 | mov %cs,%ax
89e5	.code16 | movw %sp,%bp
6689e5	.code16 | movl %esp,%ebp
0f32	.code16 | rdmsr
0f30	.code16 | wrmsr
0fa2	.code16 | cpuid
89c3	.code16 | .code32 | movl %eax,%ebx
90a100000000a10000 2:1:sym+0 7:20:sym+0	.code16 | nop | .code32 | movl sym,%eax | .code16 | movw sym,%ax
b8010066b80100	.code16 | mov $1,%ax | .code32 | mov $1,%ax
678b0424	.code16 | movw (%esp),%ax
678b444404	.code16 | movw 4(%esp,%eax,2),%ax
67668b9800000000 4:1:sym+0	.code16 | movl sym(%eax),%ebx
666800000000 2:1:sym+0	.code16 | pushl $sym
680000 1:20:sym+0	.code16 | pushw $sym
e9feff 1:21:sym+0	.code16 | jmp sym
0f84feff 2:21:sym+0	.code16 | je sym
b80000	.code16 | movw $0x10000,%ax
8b870000	.code16 | movw 0x10000(%bx),%ax
8b47ff	.code16 | movw 0xffff(%bx),%ax
8b47ff	.code16 | movw -1(%bx),%ax
8b477f	.code16 | movw 0x7f(%bx),%ax
8b878000	.code16 | movw 0x80(%bx),%ax
6650	.code16gcc | pushl %eax
66e8fcffffff 2:2:sym+0	.code16gcc | call sym
66c3	.code16gcc | ret
666a01	.code16gcc | push $1
66c9	.code16gcc | leave
4889c3	.code64 | movq %rax,%rbx
90	.code64 | nop
89c3	.code32 | movl %eax,%ebx
67668b07	movw (%bx),%ax
67668900	movw %ax,(%bx,%si)
678b07	movl (%bx),%eax
678b07	addr16 movl (%bx),%eax
678b4308	movl 8(%bp,%di),%eax
678d00	leal (%bx,%si),%eax
67668d07	leaw (%bx),%ax
678b840000 3:20:sym+0	movl sym(%si),%eax
678b870000	movl 0x10000(%bx),%eax
678b46ff	movl -1(%bp),%eax
67a10000 2:20:sym+0	addr16 movl sym,%eax
67a30000 2:20:sym+0	addr16 movl %eax,sym
67a4	addr16 movsb
67f3ab	addr16 rep stosl
67e200	addr16 loop 1f | 1:
66a4	data16 movsb
67884600	movb %al,(%bp)
67ff27	jmp *(%bx)
6766ff34	pushw (%si)
67fe05	incb (%di)
67dd07	fldl (%bx)
670f0117	lgdt (%bx)
6789407f	movl %eax,0x7f(%bx,%si)
6789808000	movl %eax,0x80(%bx,%si)
67894080	movl %eax,-0x80(%bx,%si)
6789807fff	movl %eax,-0x81(%bx,%si)
