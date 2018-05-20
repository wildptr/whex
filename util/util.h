#define TODO()\
	do {\
		fprintf(stderr, "%s:%d: TODO\n", __FILE__, __LINE__);\
		abort();\
	} while (0)

#define NELEM(x) (sizeof(x)/sizeof(*(x)))
