/*
 * Dropbear - a SSH2 server
 *
 * Copyright (c) 2002,2003 Matt Johnston
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. */

#include "includes.h"
#include "buffer.h"
#include "dbutil.h"
#include "bignum.h"
#include "dbrandom.h"
#include "runopts.h"

/* this is used to generate unique output from the same hashpool */
static uint32_t counter = 0;
/* the max value for the counter, so it won't integer overflow */
#define MAX_COUNTER (1<<30)

static unsigned char hashpool[SHA256_HASH_SIZE] = {0};
static int donerandinit = 0;

#define INIT_SEED_SIZE 32 /* 256 bits */

/* The basic setup is we read some data from /dev/(u)random or prngd and hash it
 * into hashpool. To read data, we hash together current hashpool contents,
 * and a counter. We feed more data in by hashing the current pool and new
 * data into the pool.
 *
 * It is important to ensure that counter doesn't wrap around before we
 * feed in new entropy.
 *
 */

/* Pass wantlen=0 to hash an entire file */
static int
process_file(hash_state *hs, const char *filename,
		unsigned int wantlen, int prngd) {
	int readfd = -1;
	unsigned int readcount;
	int ret = DROPBEAR_FAILURE;

	if (prngd) {
#if DROPBEAR_USE_PRNGD
		readfd = connect_unix(filename);
#endif
	} else {
		readfd = open(filename, O_RDONLY);
	}

	if (readfd < 0) {
		goto out;
	}

	readcount = 0;
	while (wantlen == 0 || readcount < wantlen) {
		int readlen, wantread;
		unsigned char readbuf[4096];
		if (wantlen == 0) {
			wantread = sizeof(readbuf);
		} else {
			wantread = MIN(sizeof(readbuf), wantlen-readcount);
		}

#if DROPBEAR_USE_PRNGD
		if (prngd) {
			char egdcmd[2];
			egdcmd[0] = 0x02;	/* blocking read */
			egdcmd[1] = (unsigned char)wantread;
			if (write(readfd, egdcmd, 2) < 0) {
				dropbear_exit("Can't send command to egd");
			}
		}
#endif
		readlen = read(readfd, readbuf, wantread);
		if (readlen <= 0) {
			if (readlen < 0 && errno == EINTR) {
				continue;
			}
			if (readlen == 0 && wantlen == 0) {
				/* whole file was read as requested */
				break;
			}
			goto out;
		}
		sha256_process(hs, readbuf, readlen);
		readcount += readlen;
	}
	ret = DROPBEAR_SUCCESS;
out:
	close(readfd);
	return ret;
}

void addrandom(const unsigned char * buf, unsigned int len)
{
	hash_state hs;

#if DROPBEAR_FUZZ
	if (fuzz.fuzzing) {
		return;
	}
#endif

	/* hash in the new seed data */
	sha256_init(&hs);
	/* existing state (zeroes on startup) */
	sha256_process(&hs, (void*)hashpool, sizeof(hashpool));

	/* new */
	sha256_process(&hs, buf, len);
	sha256_done(&hs, hashpool);
}

static void write_urandom()
{
#if DROPBEAR_FUZZ
	if (fuzz.fuzzing) {
		return;
	}
#endif
#if !DROPBEAR_USE_PRNGD
	/* This is opportunistic, don't worry about failure */
	unsigned char buf[INIT_SEED_SIZE];
	FILE *f = fopen(DROPBEAR_URANDOM_DEV, "w");
	if (!f) {
		return;
	}
	genrandom(buf, sizeof(buf));
	fwrite(buf, sizeof(buf), 1, f);
	fclose(f);
#endif
}

#if DROPBEAR_FUZZ
void fuzz_seed(const unsigned char* dat, unsigned int len) {
	hash_state hs;
	sha256_init(&hs);
	sha256_process(&hs, "fuzzfuzzfuzz", strlen("fuzzfuzzfuzz"));
	sha256_process(&hs, dat, len);
	sha256_done(&hs, hashpool);
	counter = 0;
	donerandinit = 1;
}
#endif


#ifdef HAVE_GETRANDOM
/* Reads entropy seed with getrandom().
 * May block if the kernel isn't ready.
 * Return DROPBEAR_SUCCESS or DROPBEAR_FAILURE */
static int process_getrandom(hash_state *hs) {
	char buf[INIT_SEED_SIZE];
	ssize_t ret;

	/* First try non-blocking so that we can warn about waiting */
	ret = getrandom(buf, sizeof(buf), GRND_NONBLOCK);
	if (ret == -1) {
		if (errno == ENOSYS) {
			/* Old kernel */
			return DROPBEAR_FAILURE;
		}
		/* Other errors fall through to blocking getrandom() */
		TRACE(("first getrandom() failed: %d %s", errno, strerror(errno)))
		if (errno == EAGAIN) {
			dropbear_log(LOG_WARNING, "Waiting for kernel randomness to be initialised...");
		}
	}

	/* Wait blocking if needed. Loop in case we get EINTR */
	while (ret != sizeof(buf)) {
		ret = getrandom(buf, sizeof(buf), 0);

		if (ret == sizeof(buf)) {
			/* Success */
			break;
		}
		if (ret == -1 && errno == EINTR) {
			/* Try again. */
			continue;
		}
		if (ret >= 0) {
			TRACE(("Short read %zd from getrandom() shouldn't happen", ret))
			/* Try again? */
			continue;
		}

		/* Unexpected problem, fall back to /dev/urandom */
		TRACE(("2nd getrandom() failed: %d %s", errno, strerror(errno)))
		break;
	}

	if (ret == sizeof(buf)) {
		/* Success, stir in the entropy */
		sha256_process(hs, (void*)buf, sizeof(buf));
		return DROPBEAR_SUCCESS;
	}

	return DROPBEAR_FAILURE;

}
#endif /* HAVE_GETRANDOM */

/* Initialise the prng from /dev/urandom or prngd. This function can
 * be called multiple times */
void seedrandom() {
	hash_state hs;

	pid_t pid;
	struct timeval tv;
	clock_t clockval;
	int urandom_seeded = 0;

#if DROPBEAR_FUZZ
	if (fuzz.fuzzing) {
		return;
	}
#endif

	/* hash in the new seed data */
	sha256_init(&hs);

	/* existing state */
	sha256_process(&hs, (void*)hashpool, sizeof(hashpool));

#ifdef HAVE_GETRANDOM
	if (process_getrandom(&hs) == DROPBEAR_SUCCESS) {
		urandom_seeded = 1;
	}
#endif

	if (!urandom_seeded) {
#if DROPBEAR_USE_PRNGD
		if (process_file(&hs, DROPBEAR_PRNGD_SOCKET, INIT_SEED_SIZE, 1)
				!= DROPBEAR_SUCCESS) {
			dropbear_exit("Failure reading random device %s",
					DROPBEAR_PRNGD_SOCKET);
			urandom_seeded = 1;
		}
#else
		/* non-blocking random source (probably /dev/urandom) */
		if (process_file(&hs, DROPBEAR_URANDOM_DEV, INIT_SEED_SIZE, 0)
				!= DROPBEAR_SUCCESS) {
			dropbear_exit("Failure reading random device %s",
					DROPBEAR_URANDOM_DEV);
			urandom_seeded = 1;
		}
#endif
	} /* urandom_seeded */

	/* A few other sources to fall back on.
	 * Add more here for other platforms */
#ifdef __linux__
	/* Might help on systems with wireless */
	process_file(&hs, "/proc/interrupts", 0, 0);

	process_file(&hs, "/proc/loadavg", 0, 0);
	process_file(&hs, "/proc/sys/kernel/random/entropy_avail", 0, 0);

	/* Mostly network visible but useful in some situations.
	 * Limit size to avoid slowdowns on systems with lots of routes */
	process_file(&hs, "/proc/net/netstat", 4096, 0);
	process_file(&hs, "/proc/net/dev", 4096, 0);
	process_file(&hs, "/proc/net/tcp", 4096, 0);
	/* Also includes interface lo */
	process_file(&hs, "/proc/net/rt_cache", 4096, 0);
	process_file(&hs, "/proc/vmstat", 0, 0);
#endif

	pid = getpid();
	sha256_process(&hs, (void*)&pid, sizeof(pid));

	/* gettimeofday() doesn't completely fill out struct timeval on
	   OS X (10.8.3), avoid valgrind warnings by clearing it first */
	memset(&tv, 0x0, sizeof(tv));
	gettimeofday(&tv, NULL);
	sha256_process(&hs, (void*)&tv, sizeof(tv));

	clockval = clock();
	sha256_process(&hs, (void*)&clockval, sizeof(clockval));

	/* When a private key is read by the client or server it will
	 * be added to the hashpool - see runopts.c */

	sha256_done(&hs, hashpool);

	counter = 0;
	donerandinit = 1;

	/* Feed it all back into /dev/urandom - this might help if Dropbear
	 * is running from inetd and gets new state each time */
	write_urandom();
}

/* return len bytes of pseudo-random data */
void genrandom(unsigned char* buf, unsigned int len) {

	hash_state hs;
	unsigned char hash[SHA256_HASH_SIZE];
	unsigned int copylen;

	if (!donerandinit) {
		dropbear_exit("seedrandom not done");
	}

	while (len > 0) {
		sha256_init(&hs);
		sha256_process(&hs, (void*)hashpool, sizeof(hashpool));
		sha256_process(&hs, (void*)&counter, sizeof(counter));
		sha256_done(&hs, hash);

		counter++;
		if (counter > MAX_COUNTER) {
			seedrandom();
		}

		copylen = MIN(len, SHA256_HASH_SIZE);
		memcpy(buf, hash, copylen);
		len -= copylen;
		buf += copylen;
	}
	m_burn(hash, sizeof(hash));
}

/* Generates a random mp_int.
 * max is a *mp_int specifying an upper bound.
 * rand must be an initialised *mp_int for the result.
 * the result rand satisfies:  0 < rand < max
 * */
void gen_random_mpint(const mp_int *max, mp_int *rand) {

	unsigned char *randbuf = NULL;
	unsigned int len = 0;
	const unsigned char masks[] = {0xff, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f};

	const int size_bits = mp_count_bits(max);

	len = size_bits / 8;
	if ((size_bits % 8) != 0) {
		len += 1;
	}

	randbuf = (unsigned char*)m_malloc(len);
	do {
		genrandom(randbuf, len);
		/* Mask out the unrequired bits - mp_read_unsigned_bin expects
		 * MSB first.*/
		randbuf[0] &= masks[size_bits % 8];

		bytes_to_mp(rand, randbuf, len);

		/* keep regenerating until we get one satisfying
		 * 0 < rand < max    */
	} while (!(mp_cmp(rand, max) == MP_LT && mp_cmp_d(rand, 0) == MP_GT));
	m_burn(randbuf, len);
	m_free(randbuf);
}
