/*
  This file is part of the SC Library.
  The SC Library provides support for parallel scientific applications.

  Copyright (C) 2010 The University of Texas System

  The SC Library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  The SC Library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with the SC Library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
  02110-1301, USA.
*/

#include <sc_object.h>
#include <sc_keyvalue.h>

const char         *sc_object_type = "sc_object";

static unsigned
sc_object_entry_hash (const void *v, const void *u)
{
  uint32_t            a, b, c;
  const sc_object_entry_t *e = (const sc_object_entry_t *) v;
  const unsigned long l = (unsigned long) e->key;
#if SC_SIZEOF_UNSIGNED_LONG > 4
  const unsigned long m = ((1UL << 32) - 1);

  a = (uint32_t) (l & m);
  b = (uint32_t) ((l >> 32) & m);
#else
  a = (uint32_t) l;
  b = 0xb0defe1dU;
#endif
  c = 0xdeadbeefU;
  sc_hash_final (a, b, c);

  return (unsigned) c;
}

static int
sc_object_entry_equal (const void *v1, const void *v2, const void *u)
{
  const sc_object_entry_t *e1 = (const sc_object_entry_t *) v1;
  const sc_object_entry_t *e2 = (const sc_object_entry_t *) v2;

  return e1->key == e2->key;
}

static unsigned
sc_object_hash (const void *v, const void *u)
{
  uint32_t            a, b, c;
  const unsigned long l = (unsigned long) v;
#if SC_SIZEOF_UNSIGNED_LONG > 4
  const unsigned long m = ((1UL << 32) - 1);

  a = (uint32_t) (l & m);
  b = (uint32_t) ((l >> 32) & m);
#else
  a = (uint32_t) l;
  b = 0xb0defe1dU;
#endif
  c = 0xdeadbeefU;
  sc_hash_final (a, b, c);

  return (unsigned) c;
}

static int
sc_object_equal (const void *v1, const void *v2, const void *u)
{
  return v1 == v2;
}

static int
sc_object_entry_free (void **v, const void *u)
{
  /* *INDENT-OFF* HORRIBLE indent bug */
  sc_object_entry_t  *e = (sc_object_entry_t *) * v;
  /* *INDENT-ON* */

  SC_ASSERT (e->oinmi == NULL || e->odata == NULL);

  SC_FREE (e->odata);           /* legal to be NULL */
  SC_FREE (e);

  return 1;
}

static int
is_type_fn (sc_object_t * o, sc_object_t * m, const char *type)
{
  return !strcmp (type, sc_object_type);
}

static void
finalize_fn (sc_object_t * o, sc_object_t * m)
{
  SC_ASSERT (sc_object_is_type (o, sc_object_type));

  sc_object_delegate_pop_all (o);

  if (o->table != NULL) {
    sc_hash_foreach (o->table, sc_object_entry_free);
    sc_hash_destroy (o->table);
    o->table = NULL;
  }

  SC_FREE (o);
}

static void
write_fn (sc_object_t * o, sc_object_t * m, FILE * out)
{
  SC_ASSERT (sc_object_is_type (o, sc_object_type));

  fprintf (out, "sc_object_t refs %d data %p\n", o->num_refs, o->data);
}

void
sc_object_ref (sc_object_t * o)
{
  SC_ASSERT (o->num_refs > 0);

  ++o->num_refs;
}

void
sc_object_unref (sc_object_t * o)
{
  SC_ASSERT (o->num_refs > 0);

  if (--o->num_refs == 0) {
    sc_object_finalize (o);
  }
}

sc_object_t        *
sc_object_dup (sc_object_t * o)
{
  sc_object_ref (o);

  return o;
}

void
sc_object_delegate_push (sc_object_t * o, sc_object_t * d)
{
  void               *v;

  sc_object_ref (d);
  v = sc_array_push (&o->delegates);
  *((sc_object_t **) v) = d;
}

void
sc_object_delegate_pop (sc_object_t * o)
{
  sc_object_t        *d;
  void               *v;

  v = sc_array_pop (&o->delegates);
  d = *((sc_object_t **) v);
  sc_object_unref (d);
}

void
sc_object_delegate_pop_all (sc_object_t * o)
{
  sc_object_t        *d;
  size_t              zz;

  for (zz = o->delegates.elem_count; zz > 0; --zz) {
    d = sc_object_delegate_index (o, zz - 1);
    sc_object_unref (d);
  }

  sc_array_reset (&o->delegates);
}

sc_object_t        *
sc_object_delegate_index (sc_object_t * o, size_t iz)
{
  void               *v = sc_array_index (&o->delegates, iz);

  return *((sc_object_t **) v);
}

sc_object_entry_t  *
sc_object_entry_lookup (sc_object_t * o, sc_object_method_t ifm)
{
  void              **lookup;
  sc_object_entry_t   se, *e = &se;

  if (o->table == NULL)
    return NULL;

  e->key = ifm;
  if (sc_hash_lookup (o->table, e, &lookup)) {
    /* *INDENT-OFF* HORRIBLE indent bug */
    return (sc_object_entry_t *) *lookup;
    /* *INDENT-ON* */
  }
  else
    return NULL;
}

void
sc_object_entry_search_init (sc_object_search_context_t * rc,
                             sc_object_method_t ifm, int allow_oinmi,
                             int allow_odata, sc_array_t * found)
{
  rc->visited = NULL;
  rc->lookup = ifm;

  if (found != NULL) {
    sc_array_init (found, sizeof (sc_object_entry_match_t));
    rc->found = found;
  }
  else {
    rc->found = NULL;
  }

  rc->skip_top = 0;
  rc->accept_self = 0;
  rc->accept_delegate = 0;
  rc->allow_oinmi = allow_oinmi;
  rc->allow_odata = allow_odata;
  rc->call_fn = NULL;
  rc->user_data = NULL;
  rc->last_match = NULL;
}

int
sc_object_entry_search (sc_object_t * o, sc_object_search_context_t * rc)
{
  int                 toplevel, added, answered;
  int                 found_self, found_delegate;
  size_t              zz;
  sc_object_entry_t  *e;
  sc_object_entry_match_t *match;
  sc_object_t        *d;

  SC_ASSERT (rc->lookup != NULL);
  SC_ASSERT (rc->found == NULL ||
             rc->found->elem_size == sizeof (sc_object_entry_match_t));

  if (rc->visited == NULL) {
    toplevel = 1;
    rc->visited = sc_hash_new (sc_object_hash, sc_object_equal, NULL, NULL);
  }
  else {
    toplevel = 0;
  }

  answered = 0;
  found_self = found_delegate = 0;
  added = sc_hash_insert_unique (rc->visited, o, NULL);

  if (added) {
    if (!toplevel || !rc->skip_top) {
      e = sc_object_entry_lookup (o, rc->lookup);
      if (e != NULL) {
        SC_ASSERT (e->key == rc->lookup);
        SC_ASSERT (e->oinmi == NULL || rc->allow_oinmi);
        SC_ASSERT (e->odata == NULL || rc->allow_odata);
        if (rc->found != NULL) {
          match = (sc_object_entry_match_t *) sc_array_push (rc->found);
          match->match = o;
          match->entry = e;
          found_self = 1;
        }
        if (rc->call_fn != NULL) {
          answered = rc->call_fn (o, e, rc->user_data);
        }
        rc->last_match = o;
      }
    }

    if (!answered && !(found_self && rc->accept_self)) {
      for (zz = o->delegates.elem_count; zz > 0; --zz) {
        d = sc_object_delegate_index (o, zz - 1);
        answered = sc_object_entry_search (d, rc);
        if (answered) {
          found_delegate = 1;
          if (rc->call_fn != NULL || rc->accept_delegate) {
            break;
          }
        }
      }
    }
  }
  else {
    SC_LDEBUG ("Avoiding double recursion\n");
  }

  if (toplevel) {
    sc_hash_destroy (rc->visited);
    rc->visited = NULL;
  }

  return rc->call_fn != NULL ? answered : found_self || found_delegate;
}

int
sc_object_method_register (sc_object_t * o,
                           sc_object_method_t ifm, sc_object_method_t oinmi)
{
  int                 added, first;
  void              **lookup;
  sc_object_entry_t   se, *e = &se;

  if (o->table == NULL) {
    o->table =
      sc_hash_new (sc_object_entry_hash, sc_object_entry_equal, NULL, NULL);
    first = 1;
  }
  else {
    first = 0;
  }

  e->key = ifm;
  added = sc_hash_insert_unique (o->table, e, &lookup);

  if (!added) {
    SC_ASSERT (!first);

    /* replace existing method */
    /* *INDENT-OFF* HORRIBLE indent bug */
    e = (sc_object_entry_t *) *lookup;
    /* *INDENT-ON* */
    SC_ASSERT (e->key == ifm && e->odata == NULL);
  }
  else {
    e = SC_ALLOC (sc_object_entry_t, 1);
    e->key = ifm;
    e->odata = NULL;
    *lookup = e;
  }

  e->oinmi = oinmi;

  return added;
}

void
sc_object_method_unregister (sc_object_t * o, sc_object_method_t ifm)
{
  int                 found;
  void               *lookup;
  sc_object_entry_t   se, *e = &se;

  SC_ASSERT (o->table != NULL);

  e->key = ifm;
  found = sc_hash_remove (o->table, e, &lookup);
  SC_ASSERT (found);

  e = (sc_object_entry_t *) lookup;
  SC_ASSERT (e->oinmi != NULL && e->odata == NULL);

  SC_FREE (e);
}

sc_object_method_t
sc_object_method_lookup (sc_object_t * o, sc_object_method_t ifm)
{
  sc_object_entry_t  *e;

  e = sc_object_entry_lookup (o, ifm);
  if (e != NULL) {
    SC_ASSERT (e->key == ifm && e->oinmi != NULL && e->odata == NULL);

    return e->oinmi;
  }
  else
    return NULL;
}

typedef struct sc_object_method_search_data
{
  sc_object_method_t  oinmi;
}
sc_object_method_search_data_t;

static int
method_search_fn (sc_object_t * o, sc_object_entry_t * e, void *user_data)
{
  ((sc_object_method_search_data_t *) user_data)->oinmi = e->oinmi;

  return 1;
}

sc_object_method_t
sc_object_method_search (sc_object_t * o, sc_object_method_t ifm,
                         int skip_top, sc_object_t ** m)
{
  sc_object_search_context_t src, *rc = &src;
  sc_object_method_search_data_t smsd, *msd = &smsd;

  msd->oinmi = NULL;

  sc_object_entry_search_init (rc, ifm, 1, 0, NULL);
  rc->skip_top = skip_top;
  rc->call_fn = method_search_fn;
  rc->user_data = msd;

  if (sc_object_entry_search (o, rc)) {
    SC_ASSERT (rc->last_match != NULL);
    if (m != NULL)
      *m = rc->last_match;
  }

  return msd->oinmi;
}

void               *
sc_object_data_register (sc_object_t * o, sc_object_method_t ifm, size_t s)
{
  int                 added;
  void              **lookup;
  sc_object_entry_t   se, *e = &se;

  if (o->table == NULL)
    o->table =
      sc_hash_new (sc_object_entry_hash, sc_object_entry_equal, NULL, NULL);

  e->key = ifm;
  added = sc_hash_insert_unique (o->table, e, &lookup);
  SC_ASSERT (added);

  e = SC_ALLOC (sc_object_entry_t, 1);
  e->key = ifm;
  e->oinmi = NULL;
  e->odata = sc_calloc (sc_package_id, 1, s);
  *lookup = e;

  return e->odata;
}

void               *
sc_object_data_lookup (sc_object_t * o, sc_object_method_t ifm)
{
  sc_object_entry_t  *e;

  e = sc_object_entry_lookup (o, ifm);
  SC_ASSERT (e != NULL);
  SC_ASSERT (e->key == ifm && e->oinmi == NULL);

  return e->odata;
}

static int
data_search_fn (sc_object_t * o, sc_object_entry_t * e, void *user_data)
{
  *((void **) user_data) = e->odata;

  return 1;
}

void               *
sc_object_data_search (sc_object_t * o, sc_object_method_t ifm,
                       int skip_top, sc_object_t ** m)
{
  sc_object_search_context_t src, *rc = &src;
  int                 found;
  void               *odata;

  sc_object_entry_search_init (rc, ifm, 0, 1, NULL);
  rc->skip_top = skip_top;
  rc->call_fn = data_search_fn;
  rc->user_data = &odata;

  found = sc_object_entry_search (o, rc);
  SC_ASSERT (found);
  SC_ASSERT (rc->last_match != NULL);
  if (m != NULL)
    *m = rc->last_match;

  return odata;
}

sc_object_t        *
sc_object_alloc (void)
{
  sc_object_t        *o;

  o = SC_ALLOC (sc_object_t, 1);
  o->num_refs = 1;
  sc_array_init (&o->delegates, sizeof (sc_object_t *));
  o->table = NULL;
  o->data = NULL;

  return o;
}

sc_object_t        *
sc_object_klass_new (void)
{
  int                 a1, a2, a3;
  sc_object_t        *o;

  o = sc_object_alloc ();

  a1 = sc_object_method_register (o, (sc_object_method_t) sc_object_is_type,
                                  (sc_object_method_t) is_type_fn);
  a2 = sc_object_method_register (o, (sc_object_method_t) sc_object_finalize,
                                  (sc_object_method_t) finalize_fn);
  a3 = sc_object_method_register (o, (sc_object_method_t) sc_object_write,
                                  (sc_object_method_t) write_fn);
  SC_ASSERT (a1 && a2 && a3);

  sc_object_initialize (o, NULL);

  return o;
}

sc_object_t        *
sc_object_new_from_klass (sc_object_t * d, sc_keyvalue_t * args)
{
  sc_object_t        *o;

  SC_ASSERT (d != NULL);

  o = sc_object_alloc ();
  sc_object_delegate_push (o, d);
  sc_object_initialize (o, args);

  return o;
}

sc_object_t        *
sc_object_new_from_klassf (sc_object_t * d, ...)
{
  va_list             ap;
  sc_object_t        *o;

  va_start (ap, d);
  o = sc_object_new_from_klassv (d, ap);
  va_end (ap);

  return o;
}

sc_object_t        *
sc_object_new_from_klassv (sc_object_t * d, va_list ap)
{
  sc_object_t        *o;
  sc_keyvalue_t      *args;

  args = sc_keyvalue_newv (ap);
  o = sc_object_new_from_klass (d, args);
  sc_keyvalue_destroy (args);

  return o;
}

typedef struct sc_object_is_type_data
{
  const char         *type;
  sc_object_t        *o;        /* the original toplevel object */
}
sc_object_is_type_data_t;

static int
is_type_call_fn (sc_object_t * o, sc_object_entry_t * e, void *user_data)
{
  sc_object_is_type_data_t *itd = (sc_object_is_type_data_t *) user_data;

  return ((int (*)(sc_object_t *, sc_object_t *, const char *)) e->oinmi)
    (itd->o, o, itd->type);
}

int
sc_object_is_type (sc_object_t * o, const char *type)
{
  sc_object_search_context_t src, *rc = &src;
  sc_object_is_type_data_t sitd, *itd = &sitd;

  itd->type = type;
  itd->o = o;

  sc_object_entry_search_init (rc, (sc_object_method_t) sc_object_is_type,
                               1, 0, NULL);
  rc->call_fn = is_type_call_fn;
  rc->user_data = itd;

  return sc_object_entry_search (o, rc);
}

sc_object_t        *
sc_object_copy (sc_object_t * o)
{
  size_t              zz;
  sc_array_t          sfound, *found = &sfound;
  sc_object_search_context_t src, *rc = &src;
  sc_object_entry_match_t *match;
  sc_object_method_t  oinmi;
  sc_object_t        *c;

  SC_ASSERT (sc_object_is_type (o, sc_object_type));

  /* allocate copy */
  c = sc_object_alloc ();

  /* copy all delegate pointers from original */
  for (zz = 0; zz < o->delegates.elem_count; ++zz) {
    sc_object_delegate_push (c, sc_object_delegate_index (o, zz));
  }

  /* prepare recursion */
  sc_object_entry_search_init (rc, (sc_object_method_t) sc_object_copy,
                               1, 0, found);

  /* post-order */
  if (sc_object_entry_search (o, rc)) {
    for (zz = found->elem_count; zz > 0; --zz) {
      match = (sc_object_entry_match_t *) sc_array_index (found, zz - 1);
      oinmi = match->entry->oinmi;
      SC_ASSERT (oinmi != NULL);

      ((void (*)(sc_object_t *, sc_object_t *, sc_object_t *)) oinmi)
        (o, match->match, c);
    }
  }

  sc_array_reset (found);

  return c;
}

void
sc_object_initialize (sc_object_t * o, sc_keyvalue_t * args)
{
  size_t              zz;
  sc_array_t          sfound, *found = &sfound;
  sc_object_search_context_t src, *rc = &src;
  sc_object_entry_match_t *match;
  sc_object_method_t  oinmi;

  SC_ASSERT (sc_object_is_type (o, sc_object_type));

  sc_object_entry_search_init (rc, (sc_object_method_t) sc_object_initialize,
                               1, 0, found);

  /* post-order */
  if (sc_object_entry_search (o, rc)) {
    for (zz = found->elem_count; zz > 0; --zz) {
      match = (sc_object_entry_match_t *) sc_array_index (found, zz - 1);
      oinmi = match->entry->oinmi;
      SC_ASSERT (oinmi != NULL);

      ((void (*)(sc_object_t *, sc_object_t *, sc_keyvalue_t *)) oinmi)
        (o, match->match, args);
    }
  }

  sc_array_reset (found);
}

void
sc_object_finalize (sc_object_t * o)
{
  size_t              zz;
  sc_array_t          sfound, *found = &sfound;
  sc_object_search_context_t src, *rc = &src;
  sc_object_entry_match_t *match;
  sc_object_method_t  oinmi;

  SC_ASSERT (sc_object_is_type (o, sc_object_type));

  sc_object_entry_search_init (rc, (sc_object_method_t) sc_object_finalize,
                               1, 0, found);

  /* pre-order */
  if (sc_object_entry_search (o, rc)) {
    for (zz = 0; zz < found->elem_count; ++zz) {
      match = (sc_object_entry_match_t *) sc_array_index (found, zz);
      oinmi = match->entry->oinmi;
      SC_ASSERT (oinmi != NULL);

      ((void (*)(sc_object_t *, sc_object_t *)) oinmi) (o, match->match);
    }
  }

  sc_array_reset (found);
}

void
sc_object_write (sc_object_t * o, FILE * out)
{
  sc_object_method_t  oinmi;
  sc_object_t        *m = NULL;

  SC_ASSERT (sc_object_is_type (o, sc_object_type));

  oinmi = sc_object_method_search (o, (sc_object_method_t) sc_object_write,
                                   0, &m);
  if (oinmi != NULL) {
    ((void (*)(sc_object_t *, sc_object_t *, FILE *)) oinmi) (o, m, out);
  }
}
