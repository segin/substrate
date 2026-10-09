#include "as_a64_v81.h"

#include "as_a64_encode.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *mnemonic;
    uint32_t word;
} a64_v81_desc_t;

static int streq_ci(const char *a, const char *b) {
    size_t i;

    if (a == NULL || b == NULL) {
        return 0;
    }

    for (i = 0; a[i] != '\0' && b[i] != '\0'; ++i) {
        char ca = a[i];
        char cb = b[i];

        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca + ('a' - 'A'));
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb + ('a' - 'A'));
        }
        if (ca != cb) {
            return 0;
        }
    }

    return a[i] == '\0' && b[i] == '\0';
}

static int set_err(char *errbuf, size_t errbuf_sz, const char *fmt, ...) {
    va_list ap;

    if (errbuf == NULL || errbuf_sz == 0) {
        return -1;
    }

    va_start(ap, fmt);
    vsnprintf(errbuf, errbuf_sz, fmt, ap);
    va_end(ap);
    return -1;
}

static const a64_v81_desc_t k_desc[] = {
{"ldaddb w0, w1, [x2]", 0x38200041u},
{"ldaddab w3, w4, [x5]", 0x38a300a4u},
{"ldaddlb w6, w7, [x8]", 0x38660107u},
{"ldaddalb w9, w10, [x11]", 0x38e9016au},
{"ldaddh w12, w13, [x14]", 0x782c01cdu},
{"ldaddah w15, w16, [x17]", 0x78af0230u},
{"ldaddlh w18, w19, [x20]", 0x78720293u},
{"ldaddalh w21, w22, [x23]", 0x78f502f6u},
{"ldadd w24, w25, [x26]", 0xb8380359u},
{"ldadda w0, w2, [x3]", 0xb8a00062u},
{"ldaddl w4, w5, [x6]", 0xb86400c5u},
{"ldaddal w7, w8, [x9]", 0xb8e70128u},
{"ldadd x10, x11, [x12]", 0xf82a018bu},
{"ldadda x13, x14, [x15]", 0xf8ad01eeu},
{"ldaddl x16, x17, [x18]", 0xf8700251u},
{"ldaddal x19, x20, [x21]", 0xf8f302b4u},
{"ldclrb w0, w1, [x2]", 0x38201041u},
{"ldclrab w3, w4, [x5]", 0x38a310a4u},
{"ldclrlb w6, w7, [x8]", 0x38661107u},
{"ldclralb w9, w10, [x11]", 0x38e9116au},
{"ldclrh w12, w13, [x14]", 0x782c11cdu},
{"ldclrah w15, w16, [x17]", 0x78af1230u},
{"ldclrlh w18, w19, [x20]", 0x78721293u},
{"ldclralh w21, w22, [x23]", 0x78f512f6u},
{"ldclr w24, w25, [x26]", 0xb8381359u},
{"ldclra w0, w2, [x3]", 0xb8a01062u},
{"ldclrl w4, w5, [x6]", 0xb86410c5u},
{"ldclral w7, w8, [x9]", 0xb8e71128u},
{"ldclr x10, x11, [x12]", 0xf82a118bu},
{"ldclra x13, x14, [x15]", 0xf8ad11eeu},
{"ldclrl x16, x17, [x18]", 0xf8701251u},
{"ldclral x19, x20, [x21]", 0xf8f312b4u},
{"ldeorb w0, w1, [x2]", 0x38202041u},
{"ldeorab w3, w4, [x5]", 0x38a320a4u},
{"ldeorlb w6, w7, [x8]", 0x38662107u},
{"ldeoralb w9, w10, [x11]", 0x38e9216au},
{"ldeorh w12, w13, [x14]", 0x782c21cdu},
{"ldeorah w15, w16, [x17]", 0x78af2230u},
{"ldeorlh w18, w19, [x20]", 0x78722293u},
{"ldeoralh w21, w22, [x23]", 0x78f522f6u},
{"ldeor w24, w25, [x26]", 0xb8382359u},
{"ldeora w0, w2, [x3]", 0xb8a02062u},
{"ldeorl w4, w5, [x6]", 0xb86420c5u},
{"ldeoral w7, w8, [x9]", 0xb8e72128u},
{"ldeor x10, x11, [x12]", 0xf82a218bu},
{"ldeora x13, x14, [x15]", 0xf8ad21eeu},
{"ldeorl x16, x17, [x18]", 0xf8702251u},
{"ldeoral x19, x20, [x21]", 0xf8f322b4u},
{"ldsetb w0, w1, [x2]", 0x38203041u},
{"ldsetab w3, w4, [x5]", 0x38a330a4u},
{"ldsetlb w6, w7, [x8]", 0x38663107u},
{"ldsetalb w9, w10, [x11]", 0x38e9316au},
{"ldseth w12, w13, [x14]", 0x782c31cdu},
{"ldsetah w15, w16, [x17]", 0x78af3230u},
{"ldsetlh w18, w19, [x20]", 0x78723293u},
{"ldsetalh w21, w22, [x23]", 0x78f532f6u},
{"ldset w24, w25, [x26]", 0xb8383359u},
{"ldseta w0, w2, [x3]", 0xb8a03062u},
{"ldsetl w4, w5, [x6]", 0xb86430c5u},
{"ldsetal w7, w8, [x9]", 0xb8e73128u},
{"ldset x10, x11, [x12]", 0xf82a318bu},
{"ldseta x13, x14, [x15]", 0xf8ad31eeu},
{"ldsetl x16, x17, [x18]", 0xf8703251u},
{"ldsetal x19, x20, [x21]", 0xf8f332b4u},
{"ldsmaxb w0, w1, [x2]", 0x38204041u},
{"ldsmaxab w3, w4, [x5]", 0x38a340a4u},
{"ldsmaxlb w6, w7, [x8]", 0x38664107u},
{"ldsmaxalb w9, w10, [x11]", 0x38e9416au},
{"ldsmaxh w12, w13, [x14]", 0x782c41cdu},
{"ldsmaxah w15, w16, [x17]", 0x78af4230u},
{"ldsmaxlh w18, w19, [x20]", 0x78724293u},
{"ldsmaxalh w21, w22, [x23]", 0x78f542f6u},
{"ldsmax w24, w25, [x26]", 0xb8384359u},
{"ldsmaxa w0, w2, [x3]", 0xb8a04062u},
{"ldsmaxl w4, w5, [x6]", 0xb86440c5u},
{"ldsmaxal w7, w8, [x9]", 0xb8e74128u},
{"ldsmax x10, x11, [x12]", 0xf82a418bu},
{"ldsmaxa x13, x14, [x15]", 0xf8ad41eeu},
{"ldsmaxl x16, x17, [x18]", 0xf8704251u},
{"ldsmaxal x19, x20, [x21]", 0xf8f342b4u},
{"ldsminb w0, w1, [x2]", 0x38205041u},
{"lduminb w3, w4, [x5]", 0x382370a4u},
{"ldumaxb w6, w7, [x8]", 0x38266107u},
{"ldsminal x9, x10, [x11]", 0xf8e9516au},
{"lduminal x12, x13, [x14]", 0xf8ec71cdu},
{"ldumaxal x15, x16, [x17]", 0xf8ef6230u},
{"swpb w0, w1, [x2]", 0x38208041u},
{"swpab w3, w4, [x5]", 0x38a380a4u},
{"swplb w6, w7, [x8]", 0x38668107u},
{"swpalb w9, w10, [x11]", 0x38e9816au},
{"swph w12, w13, [x14]", 0x782c81cdu},
{"swpah w15, w16, [x17]", 0x78af8230u},
{"swplh w18, w19, [x20]", 0x78728293u},
{"swpalh w21, w22, [x23]", 0x78f582f6u},
{"swp w24, w25, [x26]", 0xb8388359u},
{"swpa w0, w2, [x3]", 0xb8a08062u},
{"swpl w4, w5, [x6]", 0xb86480c5u},
{"swpal w7, w8, [x9]", 0xb8e78128u},
{"swp x10, x11, [x12]", 0xf82a818bu},
{"swpa x13, x14, [x15]", 0xf8ad81eeu},
{"swpl x16, x17, [x18]", 0xf8708251u},
{"swpal x19, x20, [x21]", 0xf8f382b4u},
{"casb w0, w1, [x2]", 0x08a07c41u},
{"casab w3, w4, [x5]", 0x08e37ca4u},
{"caslb w6, w7, [x8]", 0x08a6fd07u},
{"casalb w9, w10, [x11]", 0x08e9fd6au},
{"cash w12, w13, [x14]", 0x48ac7dcdu},
{"casah w15, w16, [x17]", 0x48ef7e30u},
{"caslh w18, w19, [x20]", 0x48b2fe93u},
{"casalh w21, w22, [x23]", 0x48f5fef6u},
{"cas w24, w25, [x26]", 0x88b87f59u},
{"casa w0, w2, [x3]", 0x88e07c62u},
{"casl w4, w5, [x6]", 0x88a4fcc5u},
{"casal w7, w8, [x9]", 0x88e7fd28u},
{"cas x10, x11, [x12]", 0xc8aa7d8bu},
{"casa x13, x14, [x15]", 0xc8ed7deeu},
{"casl x16, x17, [x18]", 0xc8b0fe51u},
{"casal x19, x20, [x21]", 0xc8f3feb4u},
{"casp x0, x1, x2, x3, [x4]", 0x48207c82u},
{"caspa x6, x7, x8, x9, [x10]", 0x48667d48u},
{"caspl x12, x13, x14, x15, [x16]", 0x482cfe0eu},
{"caspal x18, x19, x20, x21, [x22]", 0x4872fed4u},
{"staddb w0, [x1]", 0x3820003fu},
{"staddlb w2, [x3]", 0x3862007fu},
{"staddh w4, [x5]", 0x782400bfu},
{"staddlh w6, [x7]", 0x786600ffu},
{"stadd w8, [x9]", 0xb828013fu},
{"staddl w10, [x11]", 0xb86a017fu},
{"stadd x12, [x13]", 0xf82c01bfu},
{"staddl x14, [x15]", 0xf86e01ffu},
{"stclrb w16, [x17]", 0x3830123fu},
{"stclrlb w18, [x19]", 0x3872127fu},
{"stclrh w20, [x21]", 0x783412bfu},
{"stclrlh w22, [x23]", 0x787612ffu},
{"stclr w24, [x25]", 0xb838133fu},
{"stclrl w26, [x27]", 0xb87a137fu},
{"stclr x28, [x29]", 0xf83c13bfu},
{"stclrl x0, [x1]", 0xf860103fu},
{"steorb w2, [x3]", 0x3822207fu},
{"steorlb w4, [x5]", 0x386420bfu},
{"steorh w6, [x7]", 0x782620ffu},
{"steorlh w8, [x9]", 0x7868213fu},
{"steor w10, [x11]", 0xb82a217fu},
{"steorl w12, [x13]", 0xb86c21bfu},
{"steor x14, [x15]", 0xf82e21ffu},
{"steorl x16, [x17]", 0xf870223fu},
{"stsetb w18, [x19]", 0x3832327fu},
{"stsetlb w20, [x21]", 0x387432bfu},
{"stseth w22, [x23]", 0x783632ffu},
{"stsetlh w24, [x25]", 0x7878333fu},
{"stset w26, [x27]", 0xb83a337fu},
{"stsetl w28, [x29]", 0xb87c33bfu},
{"stset x0, [x1]", 0xf820303fu},
{"stsetl x2, [x3]", 0xf862307fu},
{"stsmaxb w4, [x5]", 0x382440bfu},
{"stsmaxlb w6, [x7]", 0x386640ffu},
{"stsmaxh w8, [x9]", 0x7828413fu},
{"stsmaxlh w10, [x11]", 0x786a417fu},
{"stsmax w12, [x13]", 0xb82c41bfu},
{"stsmaxl w14, [x15]", 0xb86e41ffu},
{"stsmax x16, [x17]", 0xf830423fu},
{"stsmaxl x18, [x19]", 0xf872427fu},
{"stsminb w20, [x21]", 0x383452bfu},
{"stsminlb w22, [x23]", 0x387652ffu},
{"stsminh w24, [x25]", 0x7838533fu},
{"stsminlh w26, [x27]", 0x787a537fu},
{"stsmin w28, [x29]", 0xb83c53bfu},
{"stsminl w0, [x1]", 0xb860503fu},
{"stsmin x2, [x3]", 0xf822507fu},
{"stsminl x4, [x5]", 0xf86450bfu},
{"stumaxb w6, [x7]", 0x382660ffu},
{"stumaxlb w8, [x9]", 0x3868613fu},
{"stumaxh w10, [x11]", 0x782a617fu},
{"stumaxlh w12, [x13]", 0x786c61bfu},
{"stumax w14, [x15]", 0xb82e61ffu},
{"stumaxl w16, [x17]", 0xb870623fu},
{"stumax x18, [x19]", 0xf832627fu},
{"stumaxl x20, [x21]", 0xf87462bfu},
{"stuminb w22, [x23]", 0x383672ffu},
{"stuminlb w24, [x25]", 0x3878733fu},
{"stuminh w26, [x27]", 0x783a737fu},
{"stuminlh w28, [x29]", 0x787c73bfu},
{"stumin w0, [x1]", 0xb820703fu},
{"stuminl w2, [x3]", 0xb862707fu},
{"stumin x4, [x5]", 0xf82470bfu},
{"stuminl x6, [x7]", 0xf86670ffu},
{"sqrdmlah v0.4s, v1.4s, v2.4s", 0x6e828420u},
{"sqrdmlah v3.8h, v4.8h, v5.8h", 0x6e458483u},
{"sqrdmlah v6.4s, v7.4s, v8.s[1]", 0x6fa8d0e6u},
{"sqrdmlsh v9.4s, v10.4s, v11.4s", 0x6e8b8d49u},
{"sqrdmlsh v12.8h, v13.8h, v14.8h", 0x6e4e8dacu},
{"sqrdmlsh v15.4s, v16.4s, v17.s[2]", 0x6f91fa0fu},
{"ldlarb w0, [x1]", 0x08df7c20u},
{"ldlarh w2, [x3]", 0x48df7c62u},
{"ldlar w4, [x5]", 0x88df7ca4u},
{"ldlar x6, [x7]", 0xc8df7ce6u},
{"stllrb w8, [x9]", 0x089f7d28u},
{"stllrh w10, [x11]", 0x489f7d6au},
{"stllr w12, [x13]", 0x889f7dacu},
{"stllr x14, [x15]", 0xc89f7deeu},
{"mrs x16, sctlr_el2", 0xd53c1010u},
{"msr sctlr_el2, x17", 0xd51c1011u},
{"mrs x18, hcr_el2", 0xd53c1112u},
{"msr hcr_el2, x19", 0xd51c1113u},
{"ldtr w20, [x21]", 0xb8400ab4u},
{"sttr w22, [x23]", 0xb8000af6u},
{"ldtrb w24, [x25]", 0x38400b38u},
{"sttrb w26, [x27]", 0x38000b7au},
{"ldtrh w28, [x29]", 0x78400bbcu},
{"sttrh w0, [x1]", 0x78000820u},
{"ldtrsw x2, [x3]", 0xb8800862u},
{"mrs x4, tcr_el1", 0xd5382044u},
{"msr tcr_el1, x5", 0xd5182045u},
};

int as_a64_encode_v81(const as_a64_v81_insn_t *insn, uint8_t *out, size_t out_cap,
                      size_t *out_len, char *errbuf, size_t errbuf_sz) {
    size_t i;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    if (insn == NULL || insn->mnemonic == NULL || out == NULL || out_cap < 4) {
        return set_err(errbuf, errbuf_sz, "invalid A64 v8.1 encode inputs");
    }

    for (i = 0; i < sizeof(k_desc) / sizeof(k_desc[0]); ++i) {
        if (streq_ci(insn->mnemonic, k_desc[i].mnemonic)) {
            as_a64_put32le(out, k_desc[i].word);
            if (out_len != NULL) {
                *out_len = 4;
            }
            return 0;
        }
    }

    return set_err(errbuf, errbuf_sz, "unsupported A64 v8.1 mnemonic: %s", insn->mnemonic);
}
