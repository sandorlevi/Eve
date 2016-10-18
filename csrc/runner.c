#include <runtime.h>

// debuggin
static estring bagname(evaluation e, uuid u)
{

    estring bagname = efalse;
    table_foreach(e->scopes, n, u2) if (u2 ==u) return(n);
    return(intern_cstring("missing bag?"));
}

static uuid bag_bag_id;


void merge_scan(evaluation ev, vector scopes, int sig, listener result, value e, value a, value v)
{
#if 0
    vector_foreach(scopes, u) {
        bag b = table_find(ev->t_input, u);
        if(!b) continue;
        apply(b->scan, sig,
              cont(ev->working, shadow_p_by_t_and_f, ev, result),
              e, a, v);
    }

    multibag_foreach(ev->t_solution, u, b)
        apply(((bag)b)->scan, sig,
              cont(ev->working, shadow_t_by_f, ev, result),
              e, a, v);

    multibag_foreach(ev->last_f_solution, u, b)
        apply(((bag)b)->scan, sig,
              cont(ev->working, shadow_f_by_p_and_t, ev, result),
              e, a, v);
#endif
}

static CONTINUATION_1_0(evaluation_complete, evaluation);
static void evaluation_complete(evaluation s)
{
    s->non_empty = true;
}


static void merge_multibag_bag(evaluation ev, table *d, uuid u, bag s)
{
    bag bd;
    if (!*d) {
        *d = create_value_table(ev->working);
    }

    if (!(bd = table_find(*d, u))) {
        table_set(*d, u, s);
    } else {
        edb_foreach((edb)s, e, a, v, bku) {
            edb_insert(bd, e, a, v, bku);
        }
    }
}

static void run_block(evaluation ev)
{
    heap bh = allocate_rolling(pages, sstring("block run"));
    ev->block_t_solution = 0;
    ev->block_f_solution = 0;
    ev->non_empty = false;
    ticks start = rdtsc();
    value *r = allocate(ev->working, (ev->regs + 1)* sizeof(value));

    apply(ev->head, bh, 0, r);
    // arrange for flush
    ev->cycle_time += rdtsc() - start;

    if (ev->non_empty) {
        multibag_foreach(ev->block_f_solution, u, b)
            merge_multibag_bag(ev, &ev->f_solution, u, b);
        // is this really merge_multibag_bag?
        multibag_foreach(ev->block_t_solution, u, b)
            merge_multibag_bag(ev, &ev->t_solution_for_f, u, b);
    }

    destroy(bh);
}

const int MAX_F_ITERATIONS = 250;
const int MAX_T_ITERATIONS = 50;

static void fixedpoint_error(evaluation ev, vector diffs, char * message) {
    prf("ERROR: %s\n", message);

    uuid error_data_id = generate_uuid();
    bag edata = (bag)create_edb(ev->working, 0);
    uuid error_diffs_id = generate_uuid();
    edb_insert(edata, error_diffs_id, sym(tag), sym(array), 0);

    table eavs = create_value_table(ev->working);
    int diff_ix = 1;
    vector_foreach(diffs, diff) {
        uuid diff_id = generate_uuid();
        apply(edata->insert, error_diffs_id, box_float((float)(diff_ix++)), diff_id, 1, 0);

        edb_foreach((edb)diff, e, a, v, bku) {
            value key = box_float(value_as_key(e) ^ value_as_key(a) ^ value_as_key(v));
            uuid eav_id = table_find(eavs, key);
            if(!eav_id) {
                eav_id = generate_uuid();
                edb_insert(edata, eav_id, sym(entity), e, bku);
                edb_insert(edata, eav_id, sym(attribute), a, bku);
                edb_insert(edata, eav_id, sym(value), v, bku);
                table_set(eavs, key, eav_id);
            }

            if(c > 0) {
                apply(edata->insert, diff_id, sym(insert), eav_id, 1, bku);
            } else {
                apply(edata->insert, diff_id, sym(remove), eav_id, 1, bku);
            }
        }
    }

    apply(ev->error, message, edata, error_diffs_id);
    destroy(ev->working);
}

extern string print_dot(heap h, block bk, table counters);

void simple_commit_handler(multibag backing, multibag m, closure(done,boolean))
{
    // xxx - clear out the new bags before anything else
    // this is so process will have them?
    if (m) {
        bag bdelta = table_find(m, bag_bag_id);
        bag b = table_find(backing, bag_bag_id);
        if (b && bdelta) apply(b->commit, (edb)bdelta);
    }

    multibag_foreach(m, u, b) {
        bag bd;
        if (u != bag_bag_id) {
            // xxx - we were soft creating implicitly
            // defined bags in the persistent state..but
            // we'll just going to throw them away
            if ((bd = table_find(backing, u)))
                apply(bd->commit, b);
        }
    }

    // this should be part of the commit above, but we want all the
    // targets to be consistent
    multibag_foreach(m, u, b){
        table_foreach(((bag)b)->listeners, t, _) {
            apply((bag_handler)t, b);
        }
    }
    apply(done, true);
}

static CONTINUATION_3_1(fp_complete, evaluation, vector, ticks, boolean);
static void fp_complete(evaluation ev, vector counts, ticks start_time, boolean status)
{
    ticks end_time = now();

    ticks handler_time = end_time;
    // counters? reflection? enable them
    apply(ev->complete, ev->t_solution, ev->last_f_solution, true);

    prf ("fixedpoint %v in %t seconds, %V iterations, %d changes to global, %d maintains, %t seconds handler\n",
         ev->name,
         end_time-start_time, 
         counts,
         multibag_count(ev->t_solution),
         multibag_count(ev->last_f_solution),
         now() - end_time);

    destroy(ev->working);
}

static void setup_evaluation(evaluation ev)
{
    ev->working = allocate_rolling(pages, sstring("working"));
    ev->f_solution = 0;
    ev->t = now();
}


// change evaluation to be per-block-scope
void close_evaluation(evaluation ev)
{
    // really deregister 
    table_foreach(ev->t_input, uuid, b) 
        table_set(((bag)b)->listeners, ev->run, 0);

    destroy(ev->h);
}

evaluation build_evaluation(heap h,
                            bag source,
                            bag block_root)
{
    evaluation ev = allocate(h, sizeof(struct evaluation));
    ev->h = h;
    // ev->scopes = scopes;  - resolve w/ a lookup
    // ev->t_input = t_input; - same
    ev->counters =  allocate_table(h, key_from_pointer, compare_pointer);
    ev->cycle_time = 0;
    ev->terminal = cont(ev->h, evaluation_complete, ev);
    //    ev->run = cont(h, run_solver, ev);
    ev->default_scan_scopes = allocate_vector(h, 5);
    ev->default_insert_scopes = allocate_vector(h, 5);
    ev->commit = simple_commit_handler;

    // xxx - this should be on a per-block basis
    table_foreach(ev->t_input, uuid, z) {
        bag b = z;
        table_set(b->listeners, ev->run, (void *)1);
    }

    ev->bag_bag = init_bag_bag(ev);

    if (!bag_bag_id)
        bag_bag_id = generate_uuid();
    table_set(ev->t_input, bag_bag_id, ev->bag_bag);
    table_set(ev->scopes, sym(bag), bag_bag_id);

    uuid debug_bag_id = generate_uuid();
    table_set(ev->scopes, sym(debug), debug_bag_id);
    table_set(ev->t_input, debug_bag_id, init_debug_bag(ev));

    table_foreach(ev->scopes, name, id) {
        bag input_bag = table_find(ev->t_input, id);
        if(!input_bag) continue;
        //        vector_foreach(input_bag->blocks, b) {
        //  vector_insert(ev->blocks, build(ev, b));
        //        }
    }

    return ev;
}
