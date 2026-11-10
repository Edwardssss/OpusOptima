/* Standalone independent oracle; compile against the existing local libverify. */
#include "../../src/Verification/libverify.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: verifier PUZZLE SOLUTION CYCLE_LIMIT\n");
        return 2;
    }
    char* end;
    long limit = strtol(argv[3], &end, 10);
    if (!*argv[3] || *end || limit <= 0 || limit > INT_MAX) return 2;
    void* v = verifier_create(argv[1], argv[2]);
    if (!v) return 2;
    verifier_set_cycle_limit(v, (int)limit);
    const char* names[] = {"cycles", "cost", "area", "instructions"};
    int metrics[4];
    for (int i = 0; i < 4; ++i) {
        metrics[i] = verifier_evaluate_metric(v, names[i]);
        const char* error = verifier_error(v);
        if (error || metrics[i] < 0) {
            fprintf(stderr, "%s\n", error ? error : "Negative metric");
            verifier_destroy(v);
            return 1;
        }
    }
    printf("{\"valid\":true,\"metrics\":{\"cycles\":%d,\"cost\":%d,\"area\":%d,\"instructions\":%d}"
           "}\n",
           metrics[0], metrics[1], metrics[2], metrics[3]);
    verifier_destroy(v);
    return 0;
}
