# The code in fat.c currently handles `!fs` properly and also does disk read when `fs->fat_table` is NULL.
# I will slightly modify test_null_fs to be safer if `fat_get_next_cluster` changes.
