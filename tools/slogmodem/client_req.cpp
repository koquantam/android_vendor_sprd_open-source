/*
 *  client_req.cpp - Client request parsing functions.
 *
 *  Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 *  History:
 *  2015-2-16 Zhang Ziyi
 *  Initial version.
 */
#include <cstddef>
#include <cstring>

#include "client_req.h"
#include "parse_utils.h"

int send_response(int conn, ResponseErrorCode err)
{
	char resp[32];
	size_t len;
	int ret = -1;

	if (REC_SUCCESS == err) {
		memcpy(resp, "OK\n", 3);
		len = 3;
	} else {
		len = snprintf(resp, 32, "ERROR %d\n", err);
	}

	ssize_t n = write(conn, resp, len);
	if (static_cast<size_t>(n) == len) {
		ret = 0;
	}

	return ret;
}

static CpType get_cp_type(const uint8_t* cp, size_t len)
{
	CpType t = CT_UNKNOWN;

	switch (len) {
	case 2:
		if (!memcmp(cp, "TD", 2)) {
			t = CT_TD;
		}
		break;
	case 3:
		if (!memcmp(cp, "WCN", 3)) {
			t = CT_WCN;
		}
		break;
	case 5:
		if (!memcmp(cp, "WCDMA", 5)) {
			t = CT_WCDMA;
		} else if (!memcmp(cp, "5MODE", 5)) {
			t = CT_5MODE;
		}
		break;
	case 7:
		if (!memcmp(cp, "TDD-LTE", 7)) {
			t = CT_3MODE;
		} else if (!memcmp(cp, "FDD-LTE", 7)) {
			t = CT_4MODE;
		}
		break;
	default:
		break;
	}

	return t;
}

int parse_modem_set(const uint8_t* req, size_t len, ModemSet& ms)
{
	size_t tok_len;
	const uint8_t* endp = req + len;
	const uint8_t* token;
	int err = 0;
	int n = 0;

	while (req < endp) {
		token = get_token(req, len, tok_len);
		if (!token) {
			break;
		}
		if (n >= MAX_MODEM_NUM) {
			err = -1;
			break;
		}
		CpType t = get_cp_type(token, tok_len);
		if (CT_UNKNOWN == t) {
			err = -1;
			break;
		}
		// Duplicate CP?
		int i;
		for (i = 0; i < n; ++i) {
			if (ms.modems[i] == t) {
				break;
			}
		}
		if (i < n) {
			err = -1;
			break;
		}
		ms.modems[n] = t;
		++n;
		req = token + tok_len;
	}
	ms.num = n;

	return err;
}
