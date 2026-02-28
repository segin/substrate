#include "../job.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void test_job_list(void) {
    job_t *j1 = job_new();
    j1->pgid = 100;
    j1->command = strdup("ls");
    
    job_t *j2 = job_new();
    j2->pgid = 200;
    j2->command = strdup("cat");
    
    job_t *found = find_job(100);
    assert(found == j1);
    
    found = find_job(200);
    assert(found == j2);
    
    found = find_job(300);
    assert(found == NULL);
    
    // We don't have job_free exposed in job.h currently, 
    // but in a real test we would clean up.
    printf("PASS: test_job_list\n");
}

pid_t shell_pgid = 0;
int shell_is_interactive = 0;

int main(void) {
    test_job_list();
    return 0;
}
