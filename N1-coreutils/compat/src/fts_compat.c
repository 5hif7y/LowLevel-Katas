#include <fts.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

typedef struct FTS_IMPL {
    FTS pub;
    FTSENT *roots;
    FTSENT *curr;
    FTSENT *stack;
    int (*compar)(const FTSENT **, const FTSENT **);
    int options;
} FTS_IMPL;

static FTSENT *
alloc_ftsent(const char *path, const char *name, int level, FTSENT *parent)
{
    size_t nlen = strlen(name);
    size_t plen = strlen(path);
    size_t statoff, pathoff;
    FTSENT *e;

    /*
     * fts_name[1] es el ULTIMO miembro de FTSENT, asi que el nombre vive
     * DENTRO del struct a partir de offsetof(fts_name) == sizeof(FTSENT) - 1
     * y necesita nlen bytes adicionales mas alla de sizeof(FTSENT).
     *
     * El struct stat va DESPUES del nombre, alineado. Colocarlo en
     * sizeof(FTSENT) —como se hacia antes— lo ponia a arrancar en fts_name[8]:
     * stat(2) escribia sus 96 bytes encima del nombre desde el noveno byte, de
     * modo que los nombres de 8 caracteres perdian el NUL terminador y los mas
     * largos quedaban truncados a 8. Medido en tests/probes/probe_fts_name.c:
     *
     *	namelen=8   bytes: 61 62 63 64 2e 74 78 74 02      <- st_dev pisa el NUL
     *	namelen=11  bytes: 61 62 63 64 65 66 67 2e 02 00   <- truncado a "abcdefg."
     */
    statoff = sizeof(FTSENT) + nlen;
    statoff = (statoff + _Alignof(struct stat) - 1) & ~(_Alignof(struct stat) - 1);
    pathoff = statoff + sizeof(struct stat);

    e = (FTSENT *)calloc(1, pathoff + plen + 1);
    if (e == NULL)
        return NULL;

    e->fts_statp = (struct stat *)((char *)e + statoff);
    e->fts_path = (char *)e + pathoff;
    memcpy(e->fts_path, path, plen + 1);
    e->fts_accpath = e->fts_path;
    e->fts_pathlen = plen;

    memcpy(e->fts_name, name, nlen + 1);
    e->fts_namelen = nlen;
    e->fts_level = level;
    e->fts_parent = parent;

    if (stat(e->fts_accpath, e->fts_statp) == 0) {
        if (S_ISDIR(e->fts_statp->st_mode))
            e->fts_info = FTS_D;
        else
            e->fts_info = FTS_F;
        e->fts_ino = e->fts_statp->st_ino;
        e->fts_dev = e->fts_statp->st_dev;
        e->fts_nlink = (nlink_t)e->fts_statp->st_nlink;
    } else {
        e->fts_info = FTS_NS;
        e->fts_errno = errno;
    }
    return e;
}

static void
free_ftsent(FTSENT *e)
{
    while (e != NULL) {
        FTSENT *next = e->fts_link;
        free(e);
        e = next;
    }
}

static int
qsort_wrapper(const void *a, const void *b)
{
    const FTSENT **ea = (const FTSENT **)a;
    const FTSENT **eb = (const FTSENT **)b;
    return strcmp((*ea)->fts_name, (*eb)->fts_name);
}

static FTSENT *
sort_list(FTSENT *head, int (*compar)(const FTSENT **, const FTSENT **))
{
    if (head == NULL || head->fts_link == NULL)
        return head;

    int count = 0;
    for (FTSENT *p = head; p != NULL; p = p->fts_link)
        count++;

    FTSENT **arr = (FTSENT **)malloc(count * sizeof(FTSENT *));
    if (arr == NULL)
        return head;

    int i = 0;
    for (FTSENT *p = head; p != NULL; p = p->fts_link)
        arr[i++] = p;

    if (compar != NULL)
        qsort(arr, count, sizeof(FTSENT *), (int (*)(const void *, const void *))compar);
    else
        qsort(arr, count, sizeof(FTSENT *), qsort_wrapper);

    head = arr[0];
    FTSENT *cur = head;
    for (i = 1; i < count; i++) {
        cur->fts_link = arr[i];
        cur = arr[i];
    }
    cur->fts_link = NULL;
    free(arr);
    return head;
}

FTS *
fts_open(char * const *argv, int options, int (*compar)(const FTSENT **, const FTSENT **))
{
    FTS_IMPL *impl = (FTS_IMPL *)calloc(1, sizeof(FTS_IMPL));
    if (impl == NULL)
        return NULL;

    impl->options = options;
    impl->compar = compar;

    FTSENT *head = NULL, *tail = NULL;
    char * const *p = argv;
    if (p != NULL && *p != NULL) {
        while (*p != NULL) {
            const char *arg = *p++;
            const char *slash = strrchr(arg, '/');
            const char *bslash = strrchr(arg, '\\');
            if (bslash > slash)
                slash = bslash;
            const char *name = (slash != NULL) ? slash + 1 : arg;
            if (*name == '\0')
                name = arg;

            FTSENT *e = alloc_ftsent(arg, name, FTS_ROOTLEVEL, NULL);
            if (e != NULL) {
                if (head == NULL)
                    head = e;
                else
                    tail->fts_link = e;
                tail = e;
            }
        }
    } else {
        head = alloc_ftsent(".", ".", FTS_ROOTLEVEL, NULL);
    }

    if (head != NULL && compar != NULL)
        head = sort_list(head, compar);

    impl->roots = head;
    impl->curr = NULL;
    impl->pub.fts_options = options;
    return (FTS *)impl;
}

FTSENT *
fts_children(FTS *sp, int instr)
{
    FTS_IMPL *impl = (FTS_IMPL *)sp;
    (void)instr;

    FTSENT *parent = impl->pub.fts_cur;
    if (parent == NULL)
        return impl->roots;

    if (parent->fts_info != FTS_D && parent->fts_info != FTS_DEFAULT)
        return NULL;

    DIR *dir = opendir(parent->fts_accpath);
    if (dir == NULL)
        return NULL;

    FTSENT *head = NULL, *tail = NULL;
    struct dirent *de;
    char pathbuf[4096];

    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            if (!(impl->options & FTS_SEEDOT))
                continue;
        }

        size_t plen = strlen(parent->fts_accpath);
        int has_sep = (plen > 0 && (parent->fts_accpath[plen - 1] == '/' || parent->fts_accpath[plen - 1] == '\\'));
        if (has_sep)
            snprintf(pathbuf, sizeof(pathbuf), "%s%s", parent->fts_accpath, de->d_name);
        else
            snprintf(pathbuf, sizeof(pathbuf), "%s/%s", parent->fts_accpath, de->d_name);

        FTSENT *e = alloc_ftsent(pathbuf, de->d_name, parent->fts_level + 1, parent);
        if (e != NULL) {
            if (head == NULL)
                head = e;
            else
                tail->fts_link = e;
            tail = e;
        }
    }
    closedir(dir);

    if (head != NULL && impl->compar != NULL)
        head = sort_list(head, impl->compar);

    impl->pub.fts_child = head;
    return head;
}

FTSENT *
fts_read(FTS *sp)
{
    FTS_IMPL *impl = (FTS_IMPL *)sp;

    if (impl->curr == NULL) {
        impl->curr = impl->roots;
        impl->pub.fts_cur = impl->curr;
        return impl->curr;
    }

    if (impl->curr->fts_info == FTS_D && impl->curr->fts_instr != FTS_SKIP) {
        FTSENT *ch = fts_children(sp, 0);
        if (ch != NULL) {
            impl->curr = ch;
            impl->pub.fts_cur = impl->curr;
            return impl->curr;
        }
    }

    if (impl->curr->fts_link != NULL) {
        impl->curr = impl->curr->fts_link;
        impl->pub.fts_cur = impl->curr;
        return impl->curr;
    }

    if (impl->curr->fts_parent != NULL) {
        impl->curr = impl->curr->fts_parent;
        impl->curr->fts_info = FTS_DP;
        impl->pub.fts_cur = impl->curr;
        return impl->curr;
    }

    impl->pub.fts_cur = NULL;
    return NULL;
}

int
fts_set(FTS *sp, FTSENT *p, int instr)
{
    (void)sp;
    if (p != NULL)
        p->fts_instr = (unsigned short)instr;
    return 0;
}

int
fts_close(FTS *sp)
{
    FTS_IMPL *impl = (FTS_IMPL *)sp;
    if (impl != NULL) {
        free_ftsent(impl->roots);
        free(impl);
    }
    return 0;
}
