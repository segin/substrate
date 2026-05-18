#!/bin/sh

echo "--- Testing Redirection with Variable Expansion ---"
LOGFILE=/tmp/test_redir_out.txt
echo "hello from variable" > $LOGFILE
cat $LOGFILE
rm $LOGFILE

echo "--- Testing 2>&1 Ordering ---"
# First redirect stdout to file, then stderr to stdout (which is now the file)
( ls /nonexistent_dir_12345 > /tmp/out.txt 2>&1 )
echo "Combined output:"
cat /tmp/out.txt
rm /tmp/out.txt

echo "--- Testing Tilde in Redirection ---"
echo "tilde test" > ~/test_tilde_redir.txt
cat ~/test_tilde_redir.txt
rm ~/test_tilde_redir.txt

echo "--- All redirection tests passed ---"
