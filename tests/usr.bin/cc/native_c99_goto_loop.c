int main(void) {
    int i = 0;
    int sum = 0;

top:
    if (i >= 5) {
        goto end;
    }
    sum += i;
    i++;
    goto top;

end:
    return sum - 10;
}
