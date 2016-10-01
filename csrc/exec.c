#include <runtime.h>
#include <exec.h>

static CONTINUATION_3_3(do_sub_tail, perf, value, vector, heap, perf, value *);
static void do_sub_tail(perf p,
                        value resreg,
                        vector outputs,
                        heap h, perf pp, value *r)
{
    // just drop flush and remove on the floor
    start_perf(p);
    table results = lookup(r, resreg);
    vector result = allocate_vector(results->h, vector_length(outputs));
    extract(result, outputs, r);
    table_set(results, result, etrue);
    stop_perf(p, pp);
}

static void build_sub_tail(block bk, bag b, uuid n, execf *e, flushf *f)
{
    *e = cont(bk->h,
              do_sub_tail,
              register_perf(bk->ev, n),
              blookupv(b, n, sym(pass)),
              blookupv(b, n, sym(provides)));
}


typedef struct sub {
    value id;
    vector v;
    vector projection;
    vector outputs;
    vector ids;
    table ids_cache; //these persist for all time
    table results;
    execf leg, next;
    value resreg;
    heap resh;
    heap h;
    boolean id_collapse;
} *sub;


static void set_ids(sub s, vector key, value *r)
{
    vector k;

    if (!(k = table_find(s->ids_cache, key))) {
        int len = vector_length(s->ids);
        k = allocate_vector(s->h, len);
        for (int i= 0; i < len; i++)
            vector_set(k, i, generate_uuid());
        table_set(s->ids_cache, key, k);
    }
    copyout(r, s->ids, k);
}

static void subflush(sub s, flushf n)
{
    if (s->results){
        s->results = 0;
        destroy(s->resh);
    }
    apply(n);
    return;
}

static CONTINUATION_2_3(do_sub, perf, sub, heap, perf, value *);
static void do_sub(perf p, sub s, heap h, perf pp, value *r)
{
    start_perf(p);

    table res;
    extract(s->v, s->projection, r);
    vector key;

    if (!s->results) {
        s->resh = allocate_rolling(pages, sstring("sub-results"));
        s->results = create_value_vector_table(s->resh);
    }

    if (!(res = table_find(s->results, s->v))){
        res = create_value_vector_table(s->h);
        key = allocate_vector(s->h, vector_length(s->projection));
        extract(key, s->projection, r);
        store(r, s->resreg, res);
        if (s->id_collapse) {
            set_ids(s, key, r);
        } else{
            vector_foreach(s->ids, i)
                store(r, i, generate_uuid());
        }
        apply(s->leg, h, p, r);
        table_set(s->results, key, res);
    }

    // cross
    table_foreach(res, n, _) {
        copyout(r, s->outputs, n);
        apply(s->next, h, p, r);
    }
    stop_perf(p, pp);
}


static void build_sub(block bk, bag b, uuid n, execf *e, flushf *f)
{
    sub s = allocate(bk->h, sizeof(struct sub));
    s->h = bk->h;
    s->results = 0;
    s->ids_cache = create_value_vector_table(s->h);
    s->projection = blookup_vector(bk->h, b, n, sym(projection));
    s->v = allocate_vector(s->h, vector_length(s->projection));
    // xxx - fix build
    value leg = blookupv(b, n, sym(leg));
    s->leg = resolve_cfg(bk, blookupv(b, n, sym(arm)));
    s->outputs = blookup_vector(bk->h, b, n, sym(provides));
    s->resreg =  blookupv(b, n, sym(pass));
    s->ids = blookup_vector(bk->h, b, n, sym(ids));
    s->next = *e;
    s->id_collapse = (blookupv(b, n, sym(id_collapse))==etrue)?true:false;
    *e = cont(s->h,
              do_sub,
              register_perf(bk->ev, n),
              s);

}

static CONTINUATION_3_3(do_choose_tail, perf, execf, value, heap, perf, value *);
static void do_choose_tail(perf p, execf next, value flag, heap h, perf pp, value *r)
{
    start_perf(p);
    // terminate flush and close along this leg, the head will inject it into the
    // tail
    // explicit flush gets rid of this stupid bool and the synchronous assumption
    boolean *x = lookup(r, flag);
    *x = true;
    stop_perf(p, pp);
    if (next) apply(next, h, p, r);
    stop_perf(p, pp);
}

static void build_choose_tail(block bk, bag b, uuid n, execf *e, flushf *f)
{
    vector arms = blookup_vector(bk->h, b, n, sym(arm));
    *e = cont(bk->h,
              do_choose_tail,
              register_perf(bk->ev, n),
              // xxx - fix
              (vector_length(arms) > 0)? cfg_next(bk, b, n):0,
              blookupv(b, n, sym(pass)));
}

static CONTINUATION_5_3(do_choose, perf, execf, vector, value, boolean *,
                        heap, perf, value *);
static void do_choose(perf p, execf n, vector legs, value flag, boolean *flagstore,
                      heap h, perf pp, value *r)
{
    flushf f;
    start_perf(p);
    *flagstore = false;
    store(r, flag, flagstore);
    vector_foreach (legs, i){
        apply((execf) i, h, p, r);
        apply(f);
        if (*flagstore) {
            stop_perf(p, pp);
            return;
        }
    }
    stop_perf(p, pp);
}


static void build_choose(block bk, bag b, uuid n, execf *e, flushf *f)
{
    vector arms = blookup_vector(bk->h, b, n, sym(arm));
    int narms = vector_length(arms);
    vector v = allocate_vector(bk->h, narms);
    // ooh - take out the damn index!
    vector_foreach(arms, i) 
        vector_insert(v, resolve_cfg(bk, i));

    *e = cont(bk->h,
              do_choose,
              register_perf(bk->ev, n),
              cfg_next(bk, b, n),
              v,
              blookupv(b, n, sym(pass)),
              allocate(bk->h, sizeof(boolean)));
}


static CONTINUATION_5_3(do_not,
                        perf, execf, execf, value, boolean *,
                        heap, perf, value *);
static void do_not(perf p, execf next, execf leg, value flag, boolean *flagstore,
                   heap h, perf pp, value *r)
{
    start_perf(p);
    *flagstore = false;
    store(r, flag, flagstore);

    apply(leg, h, p, r);

    if (!*flagstore)
        apply(next, h, p, r);
    stop_perf(p, pp);
}


static void build_not(block bk, bag b, uuid n, execf *e, flushf *f)
{
    *e = cont(bk->h,
              do_not,
              register_perf(bk->ev, n),
              cfg_next(bk, b, n),
              resolve_cfg(bk, blookupv(b, n, sym(arm))),
              blookupv(b, n, sym(pass)),
              allocate(bk->h, sizeof(boolean)));
}


static CONTINUATION_4_3(do_move, perf, execf, value,  value, heap, perf, value *);
static void do_move(perf p, execf n, value dest, value src, heap h, perf pp, value *r)
{
    start_perf(p);
    store(r, dest, lookup(r, src));
    apply(n, h, p, r);
    stop_perf(p, pp);
}


static void build_move(block bk, bag b, uuid n, execf *e, flushf *f)
{
    *e = cont(bk->h, do_move,
              register_perf(bk->ev, n),
              cfg_next(bk, b, n),
              // nicer names would be nice
              blookupv(b, n, sym(a)),
              blookupv(b, n, sym(b)));
}


static CONTINUATION_3_0(do_merge_flush, flushf, int, u32 *);
static void do_merge_flush(flushf n, int count, u32 *total)
{
    *total = *total +1;
    if (*total == count) {
        apply(n);
        *total = 0;
    } 
}

static execf build_merge(block bk, bag b, uuid n, execf *e, flushf *f)
{
    u32 *c = allocate(bk->h, sizeof(u32));
    *c = 0;
    *f = cont(bk->h,
              do_merge_flush, 
              *f,
              (int)*(double *)blookupv(b, n, sym(arm)),
              c);
}

// this is flushy, not inserty
static CONTINUATION_1_3(do_terminal, block, heap, perf, value *);
static void do_terminal(block bk, heap h, perf pp, value *r)
{
    apply(bk->ev->terminal);
}

static void build_terminal(block bk, bag b, uuid n, execf *e, flushf *f)
{
    *e = cont(bk->h, do_terminal, bk);
}


CONTINUATION_1_0(remove_timer, timer);

static CONTINUATION_8_3(do_time,
                        block, perf, execf, value, value, value, value, timer,
                        heap, perf, value *);
static void do_time(block bk, perf p, execf n, value hour, value minute, value second, value frame, timer t, 
                    heap h, perf pp, value *r)
{
    start_perf(p);

    unsigned int seconds, minutes,  hours;
    clocktime(bk->ev->t, &hours, &minutes, &seconds);
    value sv = box_float((double)seconds);
    value mv = box_float((double)minutes);
    value hv = box_float((double)hours);
    u64 ms = ((((u64)bk->ev->t)*1000ull)>>32) % 1000;
    value fv = box_float((double)ms);
    store(r, frame, fv);
    store(r, second, sv);
    store(r, minute, mv);
    store(r, hour, hv);
    apply(n, h, p, r);
    stop_perf(p, pp);
}

static CONTINUATION_1_0(time_expire, block);
static void time_expire(block bk)
{
    run_solver(bk->ev);
}

// xxx  - handle the bound case
static void build_time(block bk, bag b, uuid n, execf *e, flushf *f)
{
    value hour = blookupv(b, n, sym(hours));
    value minute = blookupv(b, n, sym(minutes));
    value second = blookupv(b, n, sym(seconds));
    value frame = blookupv(b, n, sym(frames));
    ticks interval = seconds(60 * 60);
    if(frame != 0) interval = milliseconds(1000 / 60);
    else if(second != 0) interval = seconds(1);
    else if(minute != 0) interval = seconds(60);
    // xxx - this shoud be only one of these guys at the finest resolution
    // requested for by the block..this should really just be 
    // 'ask the commit time', with a different thing for
    // 'create an object for each time'
    timer t = register_periodic_timer(tcontext()->t, interval, cont(bk->h, time_expire, bk));
    vector_insert(bk->cleanup, cont(bk->h, remove_timer, t));
    *e =  cont(bk->h,
               do_time,
               bk,
               register_perf(bk->ev, n),
               cfg_next(bk, b, n),
               hour,
               minute,
               second,
               frame,
               t);
}

static CONTINUATION_5_3(do_random,
                        block, perf, execf, value, value, 
                        heap, perf, value *);
static void do_random(block bk, perf p, execf n, value dest, value seed, heap h, perf pp, value *r)
{
    start_perf(p);

    // This is all very scientific.
    u64 ub = value_as_key(lookup(r, seed));
    u32 tb = (u64)bk->ev->t & (0x200000 - 1); // The 21 bottom tick bits are pretty random
    
    // Fold the tick bits down into a u8
    u8 ts = (tb ^ (tb >> 7)
             ^ (tb >> 14)) & (0x80 - 1);
    
    // Fold the user seed bits down into a u8
    u8 us = (ub ^ (ub >> 7)
             ^ (ub >> 14)
             ^ (ub >> 21)
             ^ (ub >> 28)
             ^ (ub >> 35)
             ^ (ub >> 42)
             ^ (ub >> 49)
             ^ (ub >> 56)
             ^ (ub >> 63)) & (0x80 - 1);
    
    // We fold down to 7 bits to gain some semblance of actual entropy. This means the RNG only has 128 outputs for now.
    u8 true_seed = us ^ ts;
    
    // No actual rng for now.
    store(r, dest, box_float(((double)true_seed)/128.0));
    apply(n, h, p, r);
    stop_perf(p, pp);
}

static execf build_random(block bk, bag b, uuid n, execf *e, flushf *f)
{
    value dest = blookupv(b, n, sym(return));
    value seed = blookupv(b, n, sym(seed));
    ticks interval = milliseconds(1000 / 60);
    *e = cont(bk->h,
              do_random,
              bk,
              register_perf(bk->ev, n),
              cfg_next(bk, b, n),
              dest,
              seed);
}

static CONTINUATION_2_3(do_fork, 
                        perf, vector,
                        heap, perf, value *);
static void do_fork(perf p, vector legs, heap h, perf pp, value *r)
{
    start_perf(p);
    vector_foreach(legs, i)
        apply((execf)i, h, p, r);
    stop_perf(p, pp);
}

static void build_fork(block bk, bag b, uuid n, execf *e, flushf *f)
{
    vector arms = blookup_vector(bk->h, b, n, sym(arm));
    int count = vector_length(arms);
    vector a = allocate_vector(bk->h, count);
    
    vector_foreach(arms, i)
        vector_insert(a, resolve_cfg(bk, n));
    *e = cont(bk->h, do_fork, register_perf(bk->ev, n), a);
}

static table builders;

extern void register_exec_expression(table builders);
extern void register_string_builders(table builders);
extern void register_aggregate_builders(table builders);
extern void register_edb_builders(table builders);


table builders_table()
{
    if (!builders) {
        builders = allocate_table(init, key_from_pointer, compare_pointer);
        table_set(builders, intern_cstring("fork"), build_fork);
        table_set(builders, intern_cstring("sub"), build_sub);
        table_set(builders, intern_cstring("subtail"), build_sub_tail);
        table_set(builders, intern_cstring("terminal"), build_terminal);
        table_set(builders, intern_cstring("choose"), build_choose);
        table_set(builders, intern_cstring("choosetail"), build_choose_tail);
        table_set(builders, intern_cstring("move"), build_move);
        table_set(builders, intern_cstring("not"), build_not);
        table_set(builders, intern_cstring("time"), build_time);
        table_set(builders, intern_cstring("merge"), build_merge);
        table_set(builders, intern_cstring("random"), build_random);

        register_exec_expression(builders);
        register_string_builders(builders);
        register_aggregate_builders(builders);
        register_edb_builders(builders);
    }
    return builders;
}

static void print_dot_internal(buffer dest, table visited, table counters, bag b, uuid n)
{
    if (!table_find(visited, n)) {
        vector arms = blookup_vector(transient, b, n, sym(arm));
        buffer description = allocate_string(dest->h);
        estring sig = blookupv(b, n, sym(sig));
        estring e = blookupv(b, n, sym(e));
        estring a = blookupv(b, n, sym(a));
        estring v = blookupv(b, n, sym(v));

        if(sig != 0) {
            bprintf(description, "sig: %r, e: %r, a: %r, v: %r\n", sig, e, a, v);
        }

        perf p = table_find(counters, n);
        bprintf(dest, "%v [label=\"%r:%v:%d:%b\"];\n",
                n, blookupv(b, n, sym(type)),
                n, p?p->count:0, description);
        table_set(visited, n, (void *)1);
        vector_foreach(arms, i) {
            bprintf(dest, "%v -> %v;\n", n, i);
            print_dot_internal(dest, visited, counters, b, i);
        }
    }
}

string print_dot(heap h, bag b, uuid i, table counters)
{
    table visited = allocate_table(h, key_from_pointer, compare_pointer);
    string dest = allocate_string(h);
    bprintf(dest, "digraph %v {\n", blookupv(b, i, sym(name)));
    print_dot_internal(dest, visited, counters, b, i);
    bprintf(dest, "}\n");
    return dest;
}

static void force_node(block bk, bag b, uuid n)
{
    if (!table_find(bk->nmap, n)){
        execf *x = allocate(bk->h, sizeof(execf *));
        table_set(bk->nmap, n, x);

        // this is* guarenteed to be ordered - lets use that please - pass next and f through e if its available
        // what if its not

        vector_foreach(blookup_vector(transient, b, n, sym(arm)), i)
            force_node(bk, b, i);

        uuid z;
        if (z = blookupv( b, n, sym(next))) 
            force_node(bk, b, z);
        execf e;
        flushf f;
        buildf bf = table_find(builders_table(), blookupv(b, n, sym(type)));
        bf(bk, b, n, &e, &f);
    }
}

void block_close(block bk)
{
    // who calls this and for what purpose? do they want a flush? 
    destroy(bk->h);
}

block build(evaluation ev, bag b, uuid root)
{
    heap h = allocate_rolling(pages, sstring("build"));
    block bk = allocate(h, sizeof(struct block));
    bk->ev = ev;
    bk->regs = (int)*(double *)blookupv(b, root, sym(regs));
    bk->h = h;
    bk->name = blookupv(b, root, sym(name));
    // this is only used during building
    bk->nmap = allocate_table(bk->h, key_from_pointer, compare_pointer);
    bk->start = blookupv(b, root, sym(start));
    force_node(bk, b, bk->start);
    bk->head = *(execf *)table_find(bk->nmap, bk->start);
    return bk;
}
