sed -i 's/size_t arg_count;/size_t arg_count;\n    size_t arg_cap;/g' usr.bin/cc/include/cc_frontend.h
sed -i 's/size_t block_count;/size_t block_count;\n    size_t block_cap;/g' usr.bin/cc/include/cc_frontend.h
sed -i 's/size_t param_count;/size_t param_count;\n    size_t param_cap;/g' usr.bin/cc/include/cc_frontend.h
sed -i 's/size_t stmt_count;/size_t stmt_count;\n    size_t stmt_cap;/g' usr.bin/cc/include/cc_frontend.h
sed -i 's/size_t func_count;/size_t func_count;\n    size_t func_cap;/g' usr.bin/cc/include/cc_frontend.h
sed -i 's/size_t global_count;/size_t global_count;\n    size_t global_cap;/g' usr.bin/cc/include/cc_frontend.h
