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

#ifndef DROPBEAR_KEX_H_
#define DROPBEAR_KEX_H_

#include "includes.h"
#include "algo.h"
#include "signkey.h"

void send_msg_kexinit(void);
void recv_msg_kexinit(void);
void send_msg_newkeys(void);
void recv_msg_newkeys(void);
void kexfirstinitialise(void);
void finish_kexhashbuf(void);

#if DROPBEAR_NORMAL_DH
struct kex_dh_param *gen_kexdh_param(void);
void free_kexdh_param(struct kex_dh_param *param);
void kexdh_comb_key(struct kex_dh_param *param, mp_int *dh_pub_them,
		sign_key *hostkey);
#endif

#if DROPBEAR_ECDH
struct kex_ecdh_param *gen_kexecdh_param(void);
void free_kexecdh_param(struct kex_ecdh_param *param);
void kexecdh_comb_key(struct kex_ecdh_param *param, buffer *pub_them,
		sign_key *hostkey);
#endif

#if DROPBEAR_CURVE25519_DEP
struct kex_curve25519_param *gen_kexcurve25519_param(void);
void free_kexcurve25519_param(struct kex_curve25519_param *param);
void kexcurve25519_derive(const struct kex_curve25519_param *param, const buffer *buf_pub_them,
    unsigned char *out);
#endif
#if DROPBEAR_CURVE25519
void kexcurve25519_comb_key(const struct kex_curve25519_param *param, const buffer *pub_them,
		sign_key *hostkey);
#endif

#if DROPBEAR_PQHYBRID
struct kex_pqhybrid_param *gen_kexpqhybrid_param(void);
void free_kexpqhybrid_param(struct kex_pqhybrid_param *param);
void kexpqhybrid_comb_key(struct kex_pqhybrid_param *param,
    buffer *buf_pub, sign_key *hostkey);
#endif

#ifndef DISABLE_ZLIB
int is_compress_trans(void);
int is_compress_recv(void);
#endif

void recv_msg_kexdh_init(void); /* server */

void send_msg_kexdh_init(void); /* client */
void recv_msg_kexdh_reply(void); /* client */

void recv_msg_ext_info(void);

struct KEXState {

	unsigned sentkexinit : 1; /*set when we've sent/recv kexinit packet */
	unsigned recvkexinit : 1;
	unsigned them_firstfollows : 1; /* true when first_kex_packet_follows is set */
	unsigned sentnewkeys : 1; /* set once we've send MSG_NEWKEYS (will be cleared once we have also received */
	unsigned recvnewkeys : 1; /* set once we've received MSG_NEWKEYS (cleared once we have also sent */

	unsigned int donefirstkex; /* Set to 1 after the first kex has completed,
								  ie the transport layer has been set up */
	unsigned int donesecondkex; /* Set to 1 after the second kex has completed */
	unsigned int recvfirstnewkeys; /* Set to 1 after the first valid newkeys has been received */

	unsigned our_first_follows_matches : 1;

	/* Boolean indicating that strict kex mode is in use */
	unsigned int strict_kex;

	time_t lastkextime; /* time of the last kex */
	unsigned int needrekey; /* manually trigger a rekey */
	unsigned int datatrans; /* data transmitted since last kex */
	unsigned int datarecv; /* data received since last kex */

};

#if DROPBEAR_NORMAL_DH
struct kex_dh_param {
	mp_int pub; /* e */
	mp_int priv; /* x */
};
#endif

#if DROPBEAR_ECDH
struct kex_ecdh_param {
	ecc_key key;
};
#endif

#if DROPBEAR_CURVE25519_DEP
#define CURVE25519_LEN 32
struct kex_curve25519_param {
	unsigned char priv[CURVE25519_LEN];
	unsigned char pub[CURVE25519_LEN];
};
#endif

#if DROPBEAR_PQHYBRID
struct kex_pqhybrid_param {
	struct kex_curve25519_param *curve25519;

	/* The public part sent, concatenated PQ and EC parts.
      Client sets it in gen_kexpqybrid_param().
        C_INIT = C_PK2 || C_PK1
      Server sets it in kexpqhybrid_comb().
        S_REPLY = S_CT2 || S_PK1
	*/
	buffer *concat_public;
	/* pq secret, only used by the client */
	buffer *kem_cli_secret;
};
#endif

#endif /* DROPBEAR_KEX_H_ */
