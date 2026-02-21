static const char *months[] = {"Jan", "Feb", "Mar"};
static int nums[5] = {3, 1, 4};

int main(void) {
    if (months[0][0] != 'J' || months[1][1] != 'e' || months[2][2] != 'r') {
        return 1;
    }
    if (nums[0] != 3 || nums[1] != 1 || nums[2] != 4) {
        return 2;
    }
    if (nums[3] != 0 || nums[4] != 0) {
        return 3;
    }
    return 0;
}
