int main(void) {
    double a = 0.5;
    double b = 0.0;
    double c = -0.0;
    int score = 0;

    if (a) {
        score += 1;
    }
    if (b) {
        score += 100;
    }
    if (c) {
        score += 100;
    }

    if (a > 0.25) {
        score += 1;
    }
    if (a >= 0.5) {
        score += 1;
    }
    if (a < 1.0) {
        score += 1;
    }
    if (a <= 0.5) {
        score += 1;
    }
    if (a == 0.5) {
        score += 1;
    }
    if (a != 0.25) {
        score += 1;
    }

    if ((a && 2.0) && (0.0 || 3.0)) {
        score += 1;
    }
    if ((0.0 && 1.0) || 4.0) {
        score += 1;
    }
    if (1 && 2 | 0) {
        score += 1;
    }

    return score == 10 ? 0 : score;
}
