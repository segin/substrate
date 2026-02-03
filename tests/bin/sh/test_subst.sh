echo $(echo hello)
export x=$(echo world)
echo $x
echo "start $(echo middle) end"
echo $(echo "line1
line2")
echo $(echo $(echo nested))
