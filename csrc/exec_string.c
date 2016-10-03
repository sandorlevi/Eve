#include <runtime.h>
#include <exec.h>


static CONTINUATION_4_3(do_concat, perf, execf, value, vector, heap, perf, value *);
static void do_concat(perf p, execf n, value dest, vector terms, heap h, perf pp, value *r)
{
    start_perf(p);
    buffer b = allocate_string(h);

    vector_foreach(terms, i)
        print_value_raw(b, lookup(r, i));

    store(r, dest, intern_string(bref(b, 0), buffer_length(b)));
    apply(n, h, p, r);
    stop_perf(p, pp);
}


static void build_concat(block bk, bag b, uuid n, execf *e, flushf *f)
{
    *e = cont(bk->h, do_concat,
              register_perf(bk->ev, n),
              cfg_next(bk, b, n),
              blookupv(b, n, sym(return)),
              blookupv(b, n, sym(variadic)));
}


static inline void output_split(execf n, buffer out, int ind,
                                heap h, perf p, value *r, value token, value index,
                                boolean bound_index, boolean bound_token)
{
    estring k = intern_buffer(out);
    if ((!bound_index || (ind == *(double *)lookup(r, index))) &&
        (!bound_token || (k == lookup(r, token)))){
        store(r, token, k) ;
        store(r, index, box_float(ind));
        apply(n, h, p, r);
    }
}

static CONTINUATION_8_3(do_split, perf, execf,
                        value, value, value, value,
                        boolean, boolean,
                        heap, perf, value *);
static void do_split(perf p, execf n,
                     value token, value text, value index, value by,
                     boolean bound_index, boolean bound_token,
                     heap h, perf pp, value *r)
{
    start_perf(p);
    buffer out = 0;
    int j = 0;
    int ind = 0;
    estring s = lookup(r, text);
    estring k = lookup(r, by);
    // utf8
    for (int i = 0; i < s->length; i++) {
        character si = s->body[i];
        character ki = k->body[j];

        if (!out) out = allocate_string(h);
        if (si == ki) {
            j++;
        } else {
            for (int z = 0; z < j; z++)
                string_insert(out, k->body[z]);
            j = 0;
            string_insert(out, si);
        }
        if (j == k->length) {
            j = 0;
            output_split(n, out, ++ind, h, p, r, token, index, bound_index, bound_token);
            buffer_clear(out);
        }
    }
    if (out && buffer_length(out))
        output_split(n, out, ++ind, h, p, r, token, index, bound_index, bound_token);
    stop_perf(p, pp);
}


// xxx - bound index and bound filter are just split
static void build_split_bound_index(block bk, bag b, uuid n, execf *e, flushf *f)
{
    *e = cont(bk->h, do_split,
              register_perf(bk->ev, n),
              cfg_next(bk, b, n),
              blookupv(b, n, sym(token)),
              blookupv(b, n, sym(text)),
              blookupv(b, n, sym(index)),
              blookupv(b, n, sym(by)),
              true, false);
}

static void build_split_filter(block bk, bag b, uuid n, execf *e, flushf *f)
{
    *e = cont(bk->h, do_split,
              register_perf(bk->ev, n),
              cfg_next(bk, b, n),
              blookupv(b, n, sym(token)),
              blookupv(b, n, sym(text)),
              blookupv(b, n, sym(index)),
              blookupv(b, n, sym(by)),
              true, true);
}

static void build_split(block bk, bag b, uuid n, execf *e, flushf *f)
{
    *e = cont(bk->h, do_split,
              register_perf(bk->ev, n),
              cfg_next(bk, b, n),
              blookupv(b, n, sym(token)),
              blookupv(b, n, sym(text)),
              blookupv(b, n, sym(index)),
              blookupv(b, n, sym(by)),
              false, false);
}


static CONTINUATION_5_3(do_length, block, perf, execf, value,  value, heap, perf, value *);
static void do_length(block bk, perf p, execf n, value dest, value src, heap h, perf pp, value *r)
{
    start_perf(p);
    value str = lookup(r, src);
    // this probably needs implicit coersion because
    if((type_of(str) == estring_space)) {
        store(r, dest, box_float(((estring)str)->length));
        apply(n, h, p, r);
    } else {
        exec_error(bk->ev, "Attempt to get length of non-string", str); \
    }
    apply(n, h, p, r);
    stop_perf(p, pp);
}


static void build_length(block bk, bag b, uuid n, execf *e, flushf *f)
{
    *e = cont(bk->h, do_length,
              bk,
              register_perf(bk->ev, n),
              cfg_next(bk, b, n),
              blookupv(b, n, sym(return)),
              blookupv(b, n, sym(string)));
}

void register_string_builders(table builders)
{
    table_set(builders, intern_cstring("concat"), build_concat);
    table_set(builders, intern_cstring("split"), build_split);
    table_set(builders, intern_cstring("split-filter"), build_split_filter);
    table_set(builders, intern_cstring("split-bound-index"), build_split_bound_index);
    table_set(builders, intern_cstring("length"), build_length);
}
