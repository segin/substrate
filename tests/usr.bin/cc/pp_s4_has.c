#if __has_include("pp_s3_quote.h")
int hi = 1;
#else
int hi = 0;
#endif

#if __has_embed("pp_s3_quote.h")
int he = 1;
#else
int he = 0;
#endif
