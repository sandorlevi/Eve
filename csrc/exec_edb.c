#include <runtime.h>
#include <exec.h>


static vector uuid_set(block bk, vector scopes)
{
    if (vector_length(scopes) == 0) return 0;
    vector out = allocate_vector(bk->h, vector_length(scopes));
    vector_foreach(scopes, i) {
        // we're going to soft create these scopes, but the uuids
        // remain unreferrable to the outside world
        uuid u = table_find(bk->ev->scopes, i);
        if (!u) {
            uuid lost = generate_uuid();
            prf("Unable to find context: %v. New id: %v\n", i, lost);
            table_set(bk->ev->scopes, i, lost);
        }
        vector_insert(out, u);
    }
    return out;
}


static CONTINUATION_7_5(scan_listener,
                        execf, heap, value *, perf, value, value, value,
                        value, value, value, multiplicity, uuid);
static void scan_listener(execf n, heap h, value *r, perf p,
                          value er, value ar, value vr,
                          value e, value a, value v, multiplicity m, uuid block_id)
{
    if (m > 0) {
        store(r, er, e);
        store(r, ar, a);
        store(r, vr, v);
        apply(n, h, p, r);
    }
}

#define sigbit(__sig, __p, __r) ((sig&(1<<__p))? register_ignore: __r)

static CONTINUATION_8_3(do_scan, block, perf, execf,
                        vector, int, value, value, value,
                        heap, perf, value *);
static void do_scan(block bk, perf p, execf n,
                    vector scopes, int sig, value e, value a, value v,
                    heap h, perf pp, value *r)
{
    start_perf(p);

    merge_scan(bk->ev, scopes, sig,
               cont(h, scan_listener, n, h, r, p,
                    sigbit(sig, 2, e), sigbit(sig, 1, a), sigbit(sig, 0, v)),
               lookup(r, e), lookup(r, a), lookup(r, v));

    stop_perf(p, pp);
}

static inline boolean is_cap(unsigned char x) {return (x >= 'A') && (x <= 'Z');}

static execf build_scan(block bk, bag b, uuid n, execf *e, flushf *f)
{
    estring description = blookupv(b, n, sym(sig));
    int sig = 0;
    for (int i=0; i< 3; i++) {
        sig <<= 1;
        sig |= is_cap(description->body[i]);
    }
    vector name_scopes = blookup_vector(bk->h, b, n, sym(scopes));

    *e = cont(bk->h, do_scan, bk,
              register_perf(bk->ev, n),
              cfg_next(bk, b, n),
              vector_length(name_scopes)?uuid_set(bk, name_scopes):bk->ev->default_scan_scopes,
              sig,
              blookupv(b, n, sym(e)),
              blookupv(b, n, sym(a)),
              blookupv(b, n, sym(v)));
}


typedef struct insert  {
    block bk;
    perf p;
    execf n;
    multibag *target;
    heap *h;
    vector scopes;
    int deltam;
    value e, a, v;
    boolean is_t;
} *insert;

static CONTINUATION_1_3(do_insert, insert, heap, perf, value *);

static void do_insert(insert ins,
                      heap h, perf pp, value *r)
{
    start_perf(ins->p);

    vector_foreach(ins->scopes, u)
        multibag_insert(ins->target, *ins->h, u,
                        lookup(r, ins->e), 
                        lookup(r, ins->a),
                        lookup(r, ins->v),
                        ins->deltam, ins->bk->name);

    apply(ins->n, h, ins->p, r);
    stop_perf(ins->p, pp);
}


static void build_mutation(block bk, bag b, uuid n, execf *e, flushf *f, int deltam)
{
    value mt = blookupv(b, n, sym(mutateType));
    insert ins = allocate(bk->h, sizeof(struct insert));

    if (mt == sym(bind)) {
        ins->h = &bk->ev->working;
        ins->is_t = false;
        ins->target = &bk->ev->f_solution;
    } else if (mt == sym(commit)) {
        ins->h = &bk->h;
        ins->is_t = true;
        ins->target = &bk->ev->t_solution_for_f;
    } else {
        prf("unknown mutation scope: %v\n", mt);
    }

    vector name_scopes = blookup_vector(bk->h, b, n, sym(scopes));
    ins->bk = bk;
    ins->p = register_perf(bk->ev, n);
    ins->n =  cfg_next(bk, b, n);
    ins->e = blookupv(b, n, sym(e));
    ins->a = blookupv(b, n, sym(a));
    ins->v = blookupv(b, n, sym(v));
    ins->deltam = deltam;
    ins->scopes = vector_length(name_scopes)?uuid_set(bk, name_scopes):bk->ev->default_insert_scopes;
    *e = cont(bk->h, do_insert, ins);
}


// merge these two
static void build_insert(block bk, bag b, uuid n, execf *e, flushf *f)
{
    build_mutation(bk, b, n, e, f, 1);
}

static void build_remove(block bk, bag b, uuid n, execf *e, flushf *f)
{
    build_mutation(bk, b, n, e, f, -1);
}

static CONTINUATION_4_5(each_t_solution_remove,
                        evaluation, heap, uuid, multibag *,
                        value, value, value, multiplicity, uuid);
static void each_t_solution_remove(evaluation ev, heap h, uuid u, multibag *target,
                                   value e, value a, value v, multiplicity m, uuid block_id)
{
    if (m >0)
        multibag_insert(target, h, u, e, a, v, -1, block_id);
}

static CONTINUATION_4_5(each_t_remove,
                        evaluation, heap, uuid, multibag *,
                        value, value, value, multiplicity, uuid);
static void each_t_remove(evaluation ev, heap h, uuid u, multibag *target,
                          value e, value a, value v, multiplicity m, uuid block_id)
{
    if ((m >0) && ev->t_input){
        edb base = table_find(ev->t_input, u);
        if (base && (count_of(base, e, a, v) > 0)) {
            if (ev->t_solution) {
                edb t_shadow = table_find(ev->t_solution, u);
                if (t_shadow && (count_of(t_shadow, e, a, v) == -1)) return;
            }
            multibag_insert(target, h, u, e, a, v, -1, block_id);
        }
    }
}

static CONTINUATION_8_3(do_set, block, perf, execf,
                        vector, value, value, value, value,
                        heap, perf, value *);
static void do_set(block bk, perf p, execf n,
                   vector scopes, value mt,
                   value e, value a, value v,
                   heap h, perf pp, value *r)
{
    start_perf(p);

    value ev = lookup(r, e);
    value av=  lookup(r, a);
    value vv=  lookup(r, v);
    boolean should_insert = true;
    bag b;
    multibag *target;
    
    if (mt == sym(bind)) {
        target = &bk->ev->block_f_solution;
    } else {
        target = &bk->ev->block_t_solution;
    }
    
    vector_foreach(scopes, u) {
        if (vv != register_ignore)
            multibag_insert(target, bk->ev->h, u, ev, av, vv, 1, bk->name);
        
        if ((b = table_find(bk->ev->t_input, u))) {
            apply(b->scan, s_EAv,
                  cont(h, each_t_remove, bk->ev, bk->ev->working, u, target),
                  ev, av, 0);
        }
        if (bk->ev->t_solution && (b = table_find(bk->ev->t_solution, u))) {
            apply(b->scan, s_EAv,
                  cont(h, each_t_solution_remove, bk->ev, bk->ev->working, u, target),
                  ev, av, 0);
        }
    }
    apply(n, h, p, r);
}

static execf build_set(block bk, bag b, uuid n, execf *e, flushf *f)
{
    vector name_scopes = blookupv(b, n,sym(scopes));

    *e = cont(bk->h,
              do_set, 
              bk,
              register_perf(bk->ev, n),
              cfg_next(bk, b, n),
              vector_length(name_scopes)?uuid_set(bk, name_scopes):bk->ev->default_insert_scopes,
              blookupv(b, n,sym(mutateType)),
              blookupv(b, n,sym(e)),
              blookupv(b, n,sym(a)),
              blookupv(b, n,sym(v)));
}

static CONTINUATION_6_3(do_erase, perf, execf, block,
                        vector, value, value,
                        heap, perf, value *);
static void do_erase(perf p, execf n, block bk, vector scopes, value mt, value e,
                   heap h, perf pp, value *r)
{
    start_perf(p);
    bag b;
    multibag *target;
    value ev = lookup(r, e);
    if (mt == sym(bind)) {
        target = &bk->ev->block_f_solution;
    } else {
        target = &bk->ev->block_t_solution;
    }
    
    vector_foreach(scopes, u) {
        // xxx - this can be done in constant time rather than
        // the size of the object. the attribute tables are also
        // being left behind below, which will confuse generic join
        if ((b = table_find(bk->ev->t_input, u))) {
            apply(b->scan, s_Eav,
                  cont(h, each_t_remove, bk->ev, bk->ev->working, u, target),
                  ev, 0, 0);
        }
        if (bk->ev->t_solution && (b = table_find(bk->ev->t_solution, u))) {
            apply(b->scan, s_Eav,
                  cont(h, each_t_solution_remove, bk->ev, bk->ev->working, u, target),
                  ev, 0, 0);
        }
    }
    apply(n, h, p, r);
    stop_perf(p, pp);
}

static execf build_erase(block bk, bag b, uuid n, execf *e, flushf *f)
{
    vector name_scopes = blookup_vector(bk->h, b, n,sym(scopes));

    *e = cont(bk->h, do_erase,
              register_perf(bk->ev, n),
              cfg_next(bk, b, n),
              bk,
              vector_length(name_scopes)?uuid_set(bk, name_scopes):bk->ev->default_insert_scopes,
              blookupv(b, n,sym(mutateType)),
              blookupv(b, n,sym(e)));
}

extern void register_edb_builders(table builders)
{
    table_set(builders, intern_cstring("insert"), build_insert);
    table_set(builders, intern_cstring("remove"), build_remove);
    table_set(builders, intern_cstring("set"), build_set);
    table_set(builders, intern_cstring("scan"), build_scan);
    table_set(builders, intern_cstring("erase"), build_erase);
}
