#ifndef FS_CEPH_FRAG_H
#define FS_CEPH_FRAG_H

/*
 * "Frags" are a way to describe a subset of a 32-bit number space,
 * using a mask and a value to match against that mask.  Any given frag
 * (subset of the number space) can be partitioned into 2^n sub-frags.
 *
 * Frags are encoded into a 32-bit word:
 *   8 upper bits = "bits"
 *  24 lower bits = "value"
 * (We could go to 5+27 bits, but who cares.)
 *
 * We use the _most_ significant bits of the 24 bit value.  This makes
 * values logically sort.
 *
 * Unfortunately, because the "bits" field is still in the high bits, we
 * can't sort encoded frags numerically.  However, it does allow you
 * to feed encoded frags as values into frag_contains_value.
 */
static inline __u32 ceph_frag_make(__u32 b, __u32 v)
{
	return (b << 24) |
		(v & (0xffffffu << (24-b)) & 0xffffffu);
}
static inline __u32 ceph_frag_bits(__u32 f)
{
	return f >> 24;
}
static inline __u32 ceph_frag_value(__u32 f)
{
	return f & 0xffffffu;
}
static inline __u32 ceph_frag_mask(__u32 f)
{
	return (0xffffffu << (24-ceph_frag_bits(f))) & 0xffffffu;
}
static inline __u32 ceph_frag_mask_shift(__u32 f)
{
	return 24 - ceph_frag_bits(f);
}

static inline int ceph_frag_contains_value(__u32 f, __u32 v)
{
	return (v & ceph_frag_mask(f)) == ceph_frag_value(f);
}
static inline int ceph_frag_contains_frag(__u32 f, __u32 sub)
{
	/* is sub as specific as us, and contained by us? */
	return ceph_frag_bits(sub) >= ceph_frag_bits(f) &&
	       (ceph_frag_value(sub) & ceph_frag_mask(f)) == ceph_frag_value(f);
}

static inline __u32 ceph_frag_parent(__u32 f)
{
	return ceph_frag_make(ceph_frag_bits(f) - 1,
			 ceph_frag_value(f) & (ceph_frag_mask(f) << 1));
}
static inline int ceph_frag_is_left_child(__u32 f)
{
	return ceph_frag_bits(f) > 0 &&
		(ceph_frag_value(f) & (0x1000000 >> ceph_frag_bits(f))) == 0;
}
static inline int ceph_frag_is_right_child(__u32 f)
{
	return ceph_frag_bits(f) > 0 &&
		(ceph_frag_value(f) & (0x1000000 >> ceph_frag_bits(f))) == 1;
}
static inline __u32 ceph_frag_sibling(__u32 f)
{
	return ceph_frag_make(ceph_frag_bits(f),
		      ceph_frag_value(f) ^ (0x1000000 >> ceph_frag_bits(f)));
}
static inline __u32 ceph_frag_left_child(__u32 f)
{
	return ceph_frag_make(ceph_frag_bits(f)+1, ceph_frag_value(f));
}
static inline __u32 ceph_frag_right_child(__u32 f)
{
	return ceph_frag_make(ceph_frag_bits(f)+1,
	      ceph_frag_value(f) | (0x1000000 >> (1+ceph_frag_bits(f))));
}
/*
f: _encoded frag, 高8位 + 低24位，高8位表示低24上实际使用的位数
by: 增加位数
i: 第几个子节点
返回值：子节点的编码值
举例：
f: 0x1000000
by: 1
i: 0，返回值：0x1000000
i: 1，返回值：0x1100000
*/
static inline __u32 ceph_frag_make_child(__u32 f, int by, int i)
{
	/* ceph_frag_bits 提取片段编码值的高8位，表示片段的位数，加上by得到新的位数 */
	int newbits = ceph_frag_bits(f) + by;
	/* ceph_frag_value 提取低24位的值 */
	/* i << (24 - newbits) 表示将i左移24 - newbits位，得到一个子节点新的值 */
	/* ceph_frag_value(f) | (i << (24 - newbits)) 表示将新的值与ceph_frag_value(f)进行按位或操作 */
	/* ceph_frag_make 将新的位数和新的值进行组合，得到子节点的编码值 */
	return ceph_frag_make(newbits,
			 ceph_frag_value(f) | (i << (24 - newbits)));
}
static inline int ceph_frag_is_leftmost(__u32 f)
{
	return ceph_frag_value(f) == 0;
}
static inline int ceph_frag_is_rightmost(__u32 f)
{
	return ceph_frag_value(f) == ceph_frag_mask(f);
}
static inline __u32 ceph_frag_next(__u32 f)
{
	return ceph_frag_make(ceph_frag_bits(f),
			 ceph_frag_value(f) + (0x1000000 >> ceph_frag_bits(f)));
}

/*
 * comparator to sort frags logically, as when traversing the
 * number space in ascending order...
 */
int ceph_frag_compare(__u32 a, __u32 b);

#endif
