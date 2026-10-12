/*
 * Sonda: ¿qué contiene realmente fts_name después de alloc_ftsent()?
 *
 * La sospecha es que en compat/src/fts_compat.c el bloque de memoria se reparte
 * así:
 *
 *     calloc(1, sizeof(FTSENT) + nlen+1 + plen+1 + sizeof(struct stat))
 *     fts_statp = (char *)e + sizeof(FTSENT)      <-- ¿pisa fts_name?
 *     fts_path  = fts_statp + sizeof(struct stat)
 *
 * y como fts_name[1] es el ÚLTIMO miembro de FTSENT, arranca en
 * offsetof(fts_name) == sizeof(FTSENT) - 1, es decir un byte ANTES de fts_statp.
 *
 * Esta sonda imprime los offsets reales y el contenido de fts_name tal como lo
 * ve un consumidor (ls), para confirmar o refutar el solapamiento.
 *
 * Uso:
 *   probe_fts_name.exe <directorio>
 *
 * Compilar (MSVC), desde la raíz del kata:
 *   cl /nologo /W3 /I compat\include /FI compat\include\compat.h ^
 *      /Fe:tests\probes\probe_fts_name.exe tests\probes\probe_fts_name.c ^
 *      build\lib\Release\bsd_compat.lib
 */
#include <stdio.h>
#include <stddef.h>
#include <fts.h>

int
main(int argc, char *argv[])
{
	FTS	*ftsp;
	FTSENT	*p;
	char	*paths[2];

	if (argc < 2) {
		fprintf(stderr, "uso: %s <directorio>\n", argv[0]);
		return 2;
	}

	printf("--- layout ---\n");
	printf("sizeof(FTSENT)            = %zu\n", sizeof(FTSENT));
	printf("offsetof(fts_name)        = %zu\n", offsetof(FTSENT, fts_name));
	printf("offsetof(fts_statp)       = %zu\n", offsetof(FTSENT, fts_statp));
	printf("sizeof(struct stat)       = %zu\n", sizeof(struct stat));
	printf("=> fts_statp arranca en   = %zu  (fts_name + %zu)\n",
	    sizeof(FTSENT), sizeof(FTSENT) - offsetof(FTSENT, fts_name));
	printf("=> un struct stat pisaria fts_name[%zu .. %zu]\n",
	    sizeof(FTSENT) - offsetof(FTSENT, fts_name),
	    sizeof(FTSENT) + sizeof(struct stat) - offsetof(FTSENT, fts_name) - 1);

	printf("\n--- entradas ---\n");
	paths[0] = argv[1];
	paths[1] = NULL;

	if ((ftsp = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, NULL)) == NULL) {
		perror("fts_open");
		return 1;
	}
	while ((p = fts_read(ftsp)) != NULL) {
		printf("lvl=%d  namelen=%zu  name=\"%s\"",
		    p->fts_level, p->fts_namelen, p->fts_name);

		/* Los bytes crudos del nombre, por si hay basura embebida. */
		if (p->fts_namelen > 0) {
			size_t	i;
			printf("  bytes:");
			for (i = 0; i <= p->fts_namelen && i < 32; i++)
				printf(" %02x", (unsigned char)p->fts_name[i]);
		}
		printf("\n");
	}
	fts_close(ftsp);
	return 0;
}
