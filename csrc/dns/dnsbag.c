#include <runtime.h>

typedef struct dns_bag {
    struct bag b;
} *dns_bag;


CONTINUATION_1_5(bagbag_scan, evaluation, int, listener, value, value, value);
void dns_scan(evaluation ev, int sig, listener out, value e, value a, value v)
{
    if (sig & e_sig) {
    }

    if (sig & a_sig) {
        
    }
    if (sig & v_sig) {
    }
}

bag create_dns_bag(estring resolver)
{
}

