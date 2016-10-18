typedef void (*buildf)(block, bag, uuid, execf *, flushf *);

static void exec_error(evaluation e, char *format, ...)
{
    prf ("error %s\n", format);
}

// xxx - ok, arms are currently ordered...break that?
static inline execf resolve_cfg(block bk, uuid n)
{
    return table_find(bk->nmap, n);
}

static boolean isreg(value k)
{
    if ((type_of(k) == register_space) && (k != etrue) && (k != efalse)) return true;
    return false;
}

static inline value lookup(value *r, value k)
{
    if (isreg(k)) {
        // good look keeping your sanity if this is a non-register value in this space
        return(r[toreg(k)]);
    }
    return k;
}

// xxx - should really actually coerce to a string
static inline estring lookup_string(value *r, value k)
{
    value f = lookup(r, k);
    if (type_of(f) == estring_space) return f;
    buffer out = allocate_buffer(transient, 10);
    print_value(out, r);
    // xxx - leave in a non-comparable space and promote only on demand
    return intern_buffer(out);
}


static perf register_perf(evaluation e, uuid n)
{
    perf p = allocate(e->h, sizeof(struct perf));
    p->time = 0;
    p->count = 0;
    p->trig = 0;
    table_set(e->counters, n, p);
    return p;
}

static inline void extract(vector dest, vector keys, value *r)
{
    for (int i = 0; i< vector_length(keys); i ++) {
        vector_set(dest, i, lookup(r, vector_get(keys, i)));
    }
}


static inline void store(value *r, value reg, value v)
{
    if (reg != register_ignore)
        r[toreg(reg)] = v;
}


static inline void copyout(value *r, vector keys, vector source)
{
    for (int i = 0; i< vector_length(keys); i++) {
        store(r, vector_get(keys, i), vector_get(source, i));
    }
}

static inline void copyto(value *d, value *s, vector keys)
{
    for (int i = 0; i< vector_length(keys); i++) {
        value k =  vector_get(keys, i);
        store(d, k, lookup(s, k));
    }
}


// should try to throw an error here for writing into a non-reg
static inline int reg(value n)
{
    return ((unsigned long) n - register_base);
}




static inline void start_perf(perf p)
{
    p->count++;
    p->start = rdtsc();
}

static inline void stop_perf(perf p, perf pp)
{
    ticks delta = rdtsc() - p->start;
    if (pp)
        pp->time -= delta;
    p->time += delta;
}

// xxx - there is probably a better way to wire this
static execf cfg_next(evaluation bk, bag g, uuid n)
{
    return(table_find(bk->nmap, blookupv(g, n, sym(next))));
}


