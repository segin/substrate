int sv = __STDC_VERSION__;

#ifdef __STDC_UTF_16__
int u16 = __STDC_UTF_16__;
#else
int u16 = 0;
#endif

#ifdef __STDC_UTF_32__
int u32 = __STDC_UTF_32__;
#else
int u32 = 0;
#endif

#ifdef __STDC_EMBED_FOUND__
int ef = __STDC_EMBED_FOUND__;
#else
int ef = -1;
#endif
