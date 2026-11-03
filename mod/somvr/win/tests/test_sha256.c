/* test_sha256.c - win/sha256.c against FIPS 180-4's vectors and sha256sum of a file (Linux, build.sh --check) */
#include "../sha256.h"
#include <stdio.h>
#include <string.h>

static int one(const char *msg, size_t reps, const char *want)
{
	somvr_sha256 s;
	char hex[65];
	somvr_sha256_init(&s);
	for (size_t i = 0; i < reps; i++)
		somvr_sha256_add(&s, msg, strlen(msg));
	somvr_sha256_hex(&s, hex);
	if (strcmp(hex, want)) {
		printf("FAIL sha256(%.20s x%zu) = %s, want %s\n", msg, reps, hex, want);
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	int f = 0;
	f += one("", 1, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	f += one("abc", 1, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	f += one("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 1,
		 "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	f += one("a", 1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
	f += one("0123456701234567012345670123456701234567012345670123456701234567", 10,
		 "594847328451bdfa85056225462cc1d867d877fb388df0ce35f25ab5562bfbb5");
	if (argc == 3) { /* a file and its sha256sum */
		FILE *fp = fopen(argv[1], "rb");
		static unsigned char b[1 << 16];
		somvr_sha256 s;
		char hex[65];
		size_t n;
		somvr_sha256_init(&s);
		while (fp && (n = fread(b, 1, sizeof b, fp)) > 0)
			somvr_sha256_add(&s, b, n);
		if (fp)
			fclose(fp);
		somvr_sha256_hex(&s, hex);
		if (strcmp(hex, argv[2]))
			printf("FAIL %s: %s, want %s\n", argv[1], hex, argv[2]), f++;
	}
	printf("test_sha256: %s\n", f ? "FAIL" : "ok (FIPS 180-4 vectors + the Steam-Windows exe)");
	return f != 0;
}
