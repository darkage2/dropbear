#include "includes.h"
#include "dbutil.h"
#include "buffer.h"
#include "ecdsa.h"
#include "genrsa.h"
#include "gendss.h"
#include "gened25519.h"
#include "signkey.h"
#include "dbrandom.h"

/* Returns DROPBEAR_SUCCESS or DROPBEAR_FAILURE */
static int buf_writefile(buffer * buf, const char * filename, int skip_exist) {
	int ret = DROPBEAR_FAILURE;
	int fd = -1;

	fd = open(filename, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		/* If generating keys on connection (skip_exist) it's OK to get EEXIST
		- we probably just lost a race with another connection to generate the key */
		if (skip_exist && errno == EEXIST) {
			ret = DROPBEAR_SUCCESS;
		} else {
			dropbear_log(LOG_ERR, "Couldn't create new file %s: %s",
				filename, strerror(errno));
		}

		goto out;
	}

	/* write the file now */
	while (buf->pos != buf->len) {
		int len = write(fd, buf_getptr(buf, buf->len - buf->pos),
				buf->len - buf->pos);
		if (len == -1 && errno == EINTR) {
			continue;
		}
		if (len <= 0) {
			dropbear_log(LOG_ERR, "Failed writing file %s: %s",
				filename, strerror(errno));
			goto out;
		}
		buf_incrpos(buf, len);
	}

	ret = DROPBEAR_SUCCESS;

out:
	if (fd >= 0) {
		if (fsync(fd) != 0) {
			dropbear_log(LOG_ERR, "fsync of %s failed: %s", filename, strerror(errno));
		}
		m_close(fd);
	}
	return ret;
}

/* returns 0 on failure */
static int get_default_bits(enum signkey_type keytype)
{
	switch (keytype) {
#if DROPBEAR_RSA
		case DROPBEAR_SIGNKEY_RSA:
			return DROPBEAR_DEFAULT_RSA_SIZE;
#endif
#if DROPBEAR_DSS
		case DROPBEAR_SIGNKEY_DSS:
			/* DSS for SSH only defines 1024 bits */
			return 1024;
#endif
#if DROPBEAR_ECDSA
		case DROPBEAR_SIGNKEY_ECDSA_KEYGEN:
			return ECDSA_DEFAULT_SIZE;
		case DROPBEAR_SIGNKEY_ECDSA_NISTP521:
			return 521;
		case DROPBEAR_SIGNKEY_ECDSA_NISTP384:
			return 384;
		case DROPBEAR_SIGNKEY_ECDSA_NISTP256:
			return 256;
#endif
#if DROPBEAR_ED25519
		case DROPBEAR_SIGNKEY_ED25519:
			return 256;
#endif
		default:
			return 0;
	}
}

int signkey_generate_get_bits(enum signkey_type keytype, int bits) {
	if (bits == 0)
	{
		bits = get_default_bits(keytype);
	}
	return bits;
}

/* if skip_exist is set it will silently return if the key file exists */
int signkey_generate(enum signkey_type keytype, int bits, const char* filename, int skip_exist)
{
	sign_key * key = NULL;
	buffer *buf = NULL;
	char *fn_temp = NULL;
	int ret = DROPBEAR_FAILURE;
	bits = signkey_generate_get_bits(keytype, bits);

	/* now we can generate the key */
	key = new_sign_key();

	seedrandom();

	switch(keytype) {
#if DROPBEAR_RSA
		case DROPBEAR_SIGNKEY_RSA:
			key->rsakey = gen_rsa_priv_key(bits);
			break;
#endif
#if DROPBEAR_DSS
		case DROPBEAR_SIGNKEY_DSS:
			key->dsskey = gen_dss_priv_key(bits);
			break;
#endif
#if DROPBEAR_ECDSA
		case DROPBEAR_SIGNKEY_ECDSA_KEYGEN:
		case DROPBEAR_SIGNKEY_ECDSA_NISTP521:
		case DROPBEAR_SIGNKEY_ECDSA_NISTP384:
		case DROPBEAR_SIGNKEY_ECDSA_NISTP256:
			{
				ecc_key *ecckey = gen_ecdsa_priv_key(bits);
				keytype = ecdsa_signkey_type(ecckey);
				*signkey_key_ptr(key, keytype) = ecckey;
			}
			break;
#endif
#if DROPBEAR_ED25519
		case DROPBEAR_SIGNKEY_ED25519:
			key->ed25519key = gen_ed25519_priv_key(bits);
			break;
#endif
		default:
			dropbear_exit("Internal error");
	}

	seedrandom();

	buf = buf_new(MAX_PRIVKEY_SIZE);

	buf_put_priv_key(buf, key, keytype);
	sign_key_free(key);
	key = NULL;
	buf_setpos(buf, 0);

	fn_temp = m_malloc(strlen(filename) + 30);
	snprintf(fn_temp, strlen(filename)+30, "%s.tmp%d", filename, getpid());
	ret = buf_writefile(buf, fn_temp, 0);

	if (ret == DROPBEAR_FAILURE) {
		goto out;
	}

	if (link(fn_temp, filename) < 0) {
		/* If generating keys on connection (skipexist) it's OK to get EEXIST
		- we probably just lost a race with another connection to generate the key */
		if (!(skip_exist && errno == EEXIST)) {
			if (errno == EPERM || errno == EACCES || errno == ENOSYS) {
				/* Non-atomic fallback when hard-links not allowed or unsupported */
				buf_setpos(buf, 0);
				ret = buf_writefile(buf, filename, skip_exist);
			} else {
				dropbear_log(LOG_ERR, "Failed moving key file to %s: %s", filename,
					strerror(errno));
				ret = DROPBEAR_FAILURE;
			}

			goto out;
		}
	}

	/* ensure directory update is flushed to disk, otherwise we can end up
	with zero-byte hostkey files if the power goes off */
	fsync_parent_dir(filename);

out:
	if (buf) {
		buf_burn_free(buf);
	}
	
	if (fn_temp) {
		unlink(fn_temp);
		m_free(fn_temp);
	}

	return ret;
}
