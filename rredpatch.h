struct rred_patch;
struct modification;
retvalue patch_load(const char *, off_t, /*@out@*/struct rred_patch **);
void patch_free(/*@only@*/struct rred_patch *);
/*@only@*//*@null@*/struct modification *patch_getmodifications(struct rred_patch *);
void modification_freelist(/*@only@*/struct modification *);
retvalue combine_patches(/*@out@*/struct modification **, /*@only@*/struct modification *, /*@only@*/struct modification *);
void modification_printaspatch(const struct modification *);
