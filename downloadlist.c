
struct downloaditem {
	struct downloaditem *nextinorigin;
	// todo: what is best, some tree, balanced tree, linear?
	struct downloaditem *next,*prev;
	char *destination;
	char *origin;
	/* if NULL, is it index-file */
	char *md5sum;
};

struct origin {
	struct downloadlist *list;
	struct origin *next;
	char *method;
	char *config;
};
struct downloadlist {
	struct origin *origins;
};


/* Initialize a new download session */
retvalue downloadlist_initialize(struct downloadlist **download);

/* free all memory, cancel all queued downloads */
retvalue downloadlist_done(struct downloadlist *downloadlist);

/* try to fetch and register all queued files */
retvalue downloadlist_run(struct downloadlist *downloadlist);

/* add a new origin to download files from,
retvalue downloadlist_neworigin(struct downloadlist *download,
		const char *method,const char *config,struct origin **origin);
		
/* queue a new file to be downloaded, dest relative to listdir */
retvalue downloadlist_addindex(struct origin *origin,const char *orig,const char *dest);

/* queue a new file to be downloaded: 
 * results in RET_ERROR_WRONG_MD5, if someone else already asked
 * for the same destination with other md5sum created. */
retvalue downloadlist_add(struct origin *origin,const char *orig,const char *dest,const char *md5sum);
