/*
   Unix SMB/CIFS implementation.
   async implementation of WINBINDD_GETPWSID
   Copyright (C) Volker Lendecke 2009

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "includes.h"
#include "winbindd.h"
#include "../libcli/security/security.h"

struct winbindd_getpwsid_state {
	struct dom_sid sid;
	struct winbindd_pw pw;
};

static void winbindd_getpwsid_done(struct tevent_req *subreq);

struct tevent_req *winbindd_getpwsid_send(TALLOC_CTX *mem_ctx,
					  struct tevent_context *ev,
					  struct winbindd_cli_state *cli,
					  struct winbindd_request *request)
{
	struct tevent_req *req, *subreq;
	struct winbindd_getpwsid_state *state;

	req = tevent_req_create(mem_ctx, &state,
				struct winbindd_getpwsid_state);
	if (req == NULL) {
		return NULL;
	}

	/* Ensure null termination */
	request->data.sid[sizeof(request->data.sid)-1]='\0';

	D_NOTICE("[%s (%u)] Winbind external command GETPWSID start.\n"
		 "sid=%s\n",
		 cli->client_name,
		 (unsigned int)cli->pid,
		 request->data.sid);

	if (!string_to_sid(&state->sid, request->data.sid)) {
		D_WARNING("Could not get convert sid %s from string\n",
			  request->data.sid);
		tevent_req_nterror(req, NT_STATUS_INVALID_PARAMETER);
		return tevent_req_post(req, ev);
	}

	subreq = wb_getpwsid_send(state, ev, &state->sid, &state->pw);
	if (tevent_req_nomem(subreq, req)) {
		return tevent_req_post(req, ev);
	}
	tevent_req_set_callback(subreq, winbindd_getpwsid_done, req);
	return req;
}

static void winbindd_getpwsid_done(struct tevent_req *subreq)
{
	struct tevent_req *req = tevent_req_callback_data(
		subreq, struct tevent_req);
	NTSTATUS status;

	status = wb_getpwsid_recv(subreq);
	TALLOC_FREE(subreq);
	if (tevent_req_nterror(req, status)) {
		return;
	}
	tevent_req_done(req);
}

NTSTATUS winbindd_getpwsid_recv(struct tevent_req *req,
				struct winbindd_response *response)
{
	struct winbindd_getpwsid_state *state = tevent_req_data(
		req, struct winbindd_getpwsid_state);
	NTSTATUS status;

	if (tevent_req_is_nterror(req, &status)) {
		 D_WARNING("Failed with %s.\n", nt_errstr(status));
		return status;
	}
	response->data.pw = state->pw;

	D_NOTICE("Winbind external command GETPWSID end.\n"
		 "(name:passwd:uid:gid:gecos:dir:shell)\n"
		 "%s:%s:%u:%u:%s:%s:%s\n",
		 state->pw.pw_name,
		 state->pw.pw_passwd,
		 (unsigned int)state->pw.pw_uid,
		 (unsigned int)state->pw.pw_gid,
		 state->pw.pw_gecos,
		 state->pw.pw_dir,
		 state->pw.pw_shell
		 );

	return NT_STATUS_OK;
}
