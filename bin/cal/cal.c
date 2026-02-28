#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h> // We need time.h

// Helper to determine day of week for 1st of month (0=Sun)
// Zeller's congruence
int get_day_of_week(int d, int m, int y) {
    if (m < 3) {
        m += 12;
        y -= 1;
    }
    int k = y % 100;
    int j = y / 100;
    int day = (d + 13*(m+1)/5 + k + k/4 + j/4 + 5*j) % 7;
    // Zeller returns 0=Sat, 1=Sun... 6=Fri
    // Convert to 0=Sun, 1=Mon...
    return (day + 6) % 7; // Wait, standard is: 0=Sat, 1=Sun...
    // Let's verify:
    // (h + 5) % 7? No.
    // Result: 0 = Saturday, 1 = Sunday, 2 = Monday, ..., 6 = Friday
    // We want 0 = Sunday. So (day + 1) % 7.
}

int is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

int days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
char *months[] = {"", "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

void print_cal(int m, int y) {
    printf("     %s %d\n", months[m], y);
    printf("Su Mo Tu We Th Fr Sa\n");
    
    int days = days_in_month[m];
    if (m == 2 && is_leap(y)) days = 29;
    
    // Zeller: m=13,14 for Jan/Feb of prev year
    int z_m = m;
    int z_y = y;
    if (z_m < 3) { z_m += 12; z_y--; }
    int q = 1;
    int K = z_y % 100;
    int J = z_y / 100;
    int h = (q + 13*(z_m+1)/5 + K + K/4 + J/4 + 5*J) % 7;
    // h: 0=Sat, 1=Sun ...
    // Map to 0=Sun (offset 3 spaces per day)
    // 0(Sat) -> 6
    // 1(Sun) -> 0
    int start_day = (h + 6) % 7; // 0=Sun..6=Sat
    
    // Print padding
    for (int i = 0; i < start_day; i++) printf("   ");
    
    for (int d = 1; d <= days; d++) {
        printf("%2d ", d);
        if ((start_day + d) % 7 == 0) printf("\n");
    }
    printf("\n");
}

int main(int argc, char *argv[]) {
    // Default to current date? We don't have time() syscall yet really.
    // Mock current date: Dec 2025
    int m = 12, y = 2025;
    
    if (argc == 3) {
        m = atoi(argv[1]);
        y = atoi(argv[2]);
    } else if (argc == 2) {
        y = atoi(argv[1]);
        // Print whole year? Just do current month of that year for now or error
        printf("Usage: cal [month] year\n");
        return 0;
    }
    
    if (m < 1 || m > 12) {
        printf("cal: illegal month value: use 1-12\n");
        return 1;
    }
    
    print_cal(m, y);
    return 0;
}