#if (defined alignas || __alignas_is_defined \
     || 202311 <= __STDC_VERSION__ || 201103 <= __cplusplus)
int ok;
#else
int bad;
#endif
