/* vim: set sw=8 ts=8 sts=8 noet: */

#ifndef CAPN_H
#define CAPN_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAPN_SEGID_LOCAL 0xFFFFFFFF

/* struct capn is a common structure shared between segments in the same
 * session/context so that far pointers between the segments will be created.
 *
 * lookup is used to lookup segments by id when derefencing a far pointer
 *
 * create is used to create or lookup an alternate segment that has at least
 * sz available (ie returned seg->len + sz <= seg->cap)
 *
 * Allocated segments must be zero initialized.
 *
 * create and lookup can be NULL if you don't need multiple segments and don't
 * want to support copying
 *
 * create is also used to allocate room for the copy tree with id ==
 * CAPN_SEGID_LOCAL. This data should be allocated in the local memory space
 *
 * seglist and copylist are linked lists which can be used to free up segments
 * on cleanup
 *
 * lookup, create, and user can be set by the user. Other values should be
 * zero initialized.
 */
struct capn {
	struct capn_segment *(*lookup)(void* /*user*/, uint32_t /*id */);
	struct capn_segment *(*create)(void* /*user*/, uint32_t /*id */, int /*sz*/);
	void *user;
	uint32_t segnum;
	struct capn_tree *copy;
	struct capn_tree *segtree;
	struct capn_segment *seglist, *lastseg;
	struct capn_segment *copylist;
};

struct capn_tree {
	struct capn_tree *parent, *link[2];
	unsigned int red : 1;
};

/* struct capn_segment contains the information about a single segment.
 * capn should point to a struct capn that is shared between segments in the
 * same session
 * id specifies the segment id, used for far pointers
 * data specifies the segment data. This should not move after creation.
 * len specifies the current segment length. This should be 0 for a blank
 * segment.
 * cap specifies the segment capacity.
 * When creating new structures len will be incremented until it reaces cap,
 * at which point a new segment will be requested via capn->create.
 *
 * data, len, and cap must all by 8 byte aligned
 *
 * data, len, cap should all set by the user. Other values should be zero
 * initialized.
 */
struct capn_segment {
	struct capn_tree hdr;
	struct capn_segment *next;
	struct capn *capn;
	uint32_t id;
	char *data;
	int len, cap;
};

enum CAPN_TYPE {
	CAPN_NULL = 0,
	CAPN_STRUCT = 1,
	CAPN_LIST = 2,
	CAPN_PTR_LIST = 3,
	CAPN_BIT_LIST = 4
};

struct capn_ptr {
	enum CAPN_TYPE type : 3;
	unsigned int is_list_member : 1;
	unsigned int has_ptr_tag : 1;
	unsigned int has_composite_tag : 1;
	unsigned int datasz : 19;
	unsigned int ptrsz : 19;
	int size;
	char *data;
	struct capn_segment *seg;
};

struct capn_text {
	int size;
	const char *str;
	struct capn_segment *seg;
};

struct capn_data {
	int size;
	const uint8_t *data;
	struct capn_segment *seg;
};

typedef struct capn_ptr capn_ptr;
typedef struct capn_text capn_text;
typedef struct capn_data capn_data;

/* capn_append_segment appends a segment to a session */
void capn_append_segment(struct capn*, struct capn_segment*);

capn_ptr capn_get_root(struct capn*);

/* capn_getp|setp functions get/set ptrs in list/structs
 * off is the list index or pointer index in a struct
 * capn_set_ptr will copy the data, create far pointers, etc if the target
 * is in a different segment/context.
 * Both of these will use/return inner pointers for composite lists.
 */
capn_ptr capn_getp(capn_ptr p, int off);
int capn_setp(capn_ptr p, int off, capn_ptr tgt);

capn_text capn_get_text(capn_ptr p, int off);
capn_data capn_get_data(capn_ptr p, int off);
int capn_set_text(capn_ptr p, int off, capn_text tgt);
int capn_set_data(capn_ptr p, int off, capn_data tgt);

/* capn_get_* functions get data from a list
 * The length of the list is given by p->size
 * off specifies how far into the list to start
 * sz indicates the number of elements to get
 * The function returns the number of elements read or -1 on an error.
 * off must be byte aligned for capn_get1v
 */
int capn_get1(capn_ptr p, int off);
uint8_t capn_get8(capn_ptr p, int off);
uint16_t capn_get16(capn_ptr p, int off);
uint32_t capn_get32(capn_ptr p, int off);
uint64_t capn_get64(capn_ptr p, int off);
int capn_getv1(capn_ptr p, int off, uint8_t *data, int sz);
int capn_getv8(capn_ptr p, int off, uint8_t *data, int sz);
int capn_getv16(capn_ptr p, int off, uint16_t *data, int sz);
int capn_getv32(capn_ptr p, int off, uint32_t *data, int sz);
int capn_getv64(capn_ptr p, int off, uint64_t *data, int sz);

/* capn_set_* function set data in a list
 * off specifies how far into the list to start
 * sz indicates the number of elements to write
 * The function returns the number of elemnts written or -1 on an error.
 * off must be byte aligned for capn_set1v
 */
int capn_set1(capn_ptr p, int off, int v);
int capn_set8(capn_ptr p, int off, uint8_t v);
int capn_set16(capn_ptr p, int off, uint16_t v);
int capn_set32(capn_ptr p, int off, uint32_t v);
int capn_set64(capn_ptr p, int off, uint64_t v);
int capn_setv1(capn_ptr p, int off, const uint8_t *data, int sz);
int capn_setv8(capn_ptr p, int off, const uint8_t *data, int sz);
int capn_setv16(capn_ptr p, int off, const uint16_t *data, int sz);
int capn_setv32(capn_ptr p, int off, const uint32_t *data, int sz);
int capn_setv64(capn_ptr p, int off, const uint64_t *data, int sz);

/* capn_new_* functions create a new object
 * datasz is in bytes, ptrs is # of pointers, sz is # of elements in the list
 * If capn_new_string sz < 0, strlen is used to compute the string length
 * On an error a CAPN_NULL pointer is returned
 */
capn_ptr capn_new_struct(struct capn_segment *seg, int datasz, int ptrs);
capn_ptr capn_new_list(struct capn_segment *seg, int sz, int datasz, int ptrs);
capn_ptr capn_new_bit_list(struct capn_segment *seg, int sz);
capn_ptr capn_new_ptr_list(struct capn_segment *seg, int sz);
capn_ptr capn_new_string(struct capn_segment *seg, const char *str, int sz);
capn_ptr capn_new_root(struct capn*);

#if defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L)
#define CAPN_INLINE static inline
#else
#define CAPN_INLINE static
#endif

/* capn_read|write_* functions read/write struct values
 * off is the offset into the structure in bytes
 * Rarely should these be called directly, instead use the generated code.
 * Data must be xored with the default value
 * These are inlined
 */
CAPN_INLINE uint8_t capn_read8(capn_ptr p, int off);
CAPN_INLINE uint16_t capn_read16(capn_ptr p, int off);
CAPN_INLINE uint32_t capn_read32(capn_ptr p, int off);
CAPN_INLINE uint64_t capn_read64(capn_ptr p, int off);
CAPN_INLINE int capn_write8(capn_ptr p, int off, uint8_t val);
CAPN_INLINE int capn_write16(capn_ptr p, int off, uint16_t val);
CAPN_INLINE int capn_write32(capn_ptr p, int off, uint32_t val);
CAPN_INLINE int capn_write64(capn_ptr p, int off, uint64_t val);


/* capn_init_malloc inits the capn struct with a create function which
 * allocates segments on the heap using malloc
 *
 * capn_free_all frees all the segment headers and data created by the create
 * function setup by capn_init_malloc
 */
void capn_init_malloc(struct capn *c);
void capn_free_malloc(struct capn *c);

int capn_init_fp(struct capn *c, FILE *f, int packed);
void capn_free_fp(struct capn *c);

int capn_init_mem(struct capn *c, const uint8_t *p, size_t sz, int packed);
void capn_free_mem(struct capn *c);

/* capn_stream encapsulates the needed fields for capn_(deflate|inflate) in a
 * similar manner to z_stream from zlib
 *
 * The user should set next_in, avail_in, next_out, avail_out to the
 * available in/out buffers before calling capn_(deflate|inflate).
 *
 * Other fields should be zero initialized.
 */
struct capn_stream {
	const uint8_t *next_in;
	int avail_in;
	uint8_t *next_out;
	int avail_out;
	int zeros, raw;
};

#define CAPN_MISALIGNED -1
#define CAPN_NEED_MORE -2

/* capn_deflate deflates a stream to the packed format
 * capn_inflate inflates a stream from the packed format
 *
 * Returns:
 * CAPN_MISALIGNED - if the unpacked data is not 8 byte aligned
 * CAPN_NEED_MORE - more packed data/room is required (out for inflate, in for
 * deflate)
 * 0 - success, all output for inflate, all input for deflate processed
 */
int capn_deflate(struct capn_stream*);
int capn_inflate(struct capn_stream*);

/* Inline functions */


#define T(IDX) s.v[IDX] = (uint8_t) (v >> (8*IDX))
CAPN_INLINE uint8_t capn_flip8(uint8_t v) {
	return v;
}
CAPN_INLINE uint16_t capn_flip16(uint16_t v) {
	union { uint16_t u; uint8_t v[2]; } s;
	T(0); T(1);
	return s.u;
}
CAPN_INLINE uint32_t capn_flip32(uint32_t v) {
	union { uint32_t u; uint8_t v[4]; } s;
	T(0); T(1); T(2); T(3);
	return s.u;
}
CAPN_INLINE uint64_t capn_flip64(uint64_t v) {
	union { uint64_t u; uint8_t v[8]; } s;
	T(0); T(1); T(2); T(3); T(4); T(5); T(6); T(7);
	return s.u;
}
#undef T

CAPN_INLINE uint8_t capn_read8(capn_ptr p, int off) {
	return off+1 <= p.datasz ? capn_flip8(*(uint8_t*) (p.data+off)) : 0;
}
CAPN_INLINE int capn_write8(capn_ptr p, int off, uint8_t val) {
	if (off+1 <= p.datasz) {
		*(uint8_t*) (p.data+off) = capn_flip8(val);
		return 0;
	} else {
		return -1;
	}
}

CAPN_INLINE uint16_t capn_read16(capn_ptr p, int off) {
	return off+2 <= p.datasz ? capn_flip16(*(uint16_t*) (p.data+off)) : 0;
}
CAPN_INLINE int capn_write16(capn_ptr p, int off, uint16_t val) {
	if (off+2 <= p.datasz) {
		*(uint16_t*) (p.data+off) = capn_flip16(val);
		return 0;
	} else {
		return -1;
	}
}

CAPN_INLINE uint32_t capn_read32(capn_ptr p, int off) {
	return off+4 <= p.datasz ? capn_flip32(*(uint32_t*) (p.data+off)) : 0;
}
CAPN_INLINE int capn_write32(capn_ptr p, int off, uint32_t val) {
	if (off+4 <= p.datasz) {
		*(uint32_t*) (p.data+off) = capn_flip32(val);
		return 0;
	} else {
		return -1;
	}
}

CAPN_INLINE uint64_t capn_read64(capn_ptr p, int off) {
	return off+8 <= p.datasz ? capn_flip64(*(uint64_t*) (p.data+off)) : 0;
}
CAPN_INLINE int capn_write64(capn_ptr p, int off, uint64_t val) {
	if (off+8 <= p.datasz) {
		*(uint64_t*) (p.data+off) = capn_flip64(val);
		return 0;
	} else {
		return -1;
	}
}

CAPN_INLINE float capn_read_float(capn_ptr p, int off, float def) {
	union { float f; uint32_t u;} u;
	u.f = def;
	u.u ^= capn_read32(p, off);
	return u.f;
}
CAPN_INLINE int capn_write_float(capn_ptr p, int off, float f, float def) {
	union { float f; uint32_t u;} u;
	union { float f; uint32_t u;} d;
	u.f = f;
	d.f = def;
	return capn_write32(p, off, u.u ^ d.u);
}

CAPN_INLINE double capn_read_double(capn_ptr p, int off, double def) {
	union { double f; uint64_t u;} u;
	u.f = def;
	u.u ^= capn_read64(p, off);
	return u.f;
}
CAPN_INLINE int capn_write_double(capn_ptr p, int off, double f, double def) {
	union { double f; uint64_t u;} u;
	union { double f; uint64_t u;} d;
	d.f = f;
	u.f = f;
	return capn_write64(p, off, u.u ^ d.u);
}

#ifdef __cplusplus
}
#endif

#endif
