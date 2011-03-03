#ifndef REPREPRO_SOURCEEXTRACTION_H
#define REPREPRO_SOURCEEXTRACTION_H

struct sourceextraction;

/*@NULL@*/struct sourceextraction *sourceextraction_init(/*@null@*/char **section_p, /*@null@*/char **priority_p);

void sourceextraction_abort(/*@only@*/struct sourceextraction *);

/* register a file part of this source */
void sourceextraction_setpart(struct sourceextraction *, int , const char *);

/* return the next needed file */
bool sourceextraction_needs(struct sourceextraction *, /*@out@*/int *);

/* full file name of requested files ready to analyse */
retvalue sourceextraction_analyse(struct sourceextraction *, const char *);

retvalue sourceextraction_finish(/*@only@*/struct sourceextraction *);

#endif
