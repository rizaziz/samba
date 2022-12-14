/*
   Unix SMB/CIFS implementation.
   async implementation of WINBINDD_GETUSERSIDS
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

struct winbindd_getusersids_state {
	struct dom_sid sid;
	uint32_t num_sids;
	struct dom_sid *sids;
};

static void winbindd_getusersids_done(struct tevent_req *subreq);

struct tevent_req *winbindd_getusersids_send(TALLOC_CTX *mem_ctx,
					     struct tevent_context *ev,
					     struct winbindd_cli_state *cli,
					     struct winbindd_request *request)
{
	struct tevent_req *req, *subreq;
	struct winbindd_getusersids_state *state;

	req = tevent_req_create(mem_ctx, &state,
				struct winbindd_getusersids_state);
	if (req == NULL) {
		return NULL;
	}

	/* Ensure null termination */
	request->data.sid[sizeof(request->data.sid)-1]='\0';

	D_NOTICE("[%s (%u)] Winbind external command GETUSERSIDS start.\n"
		 "sid=%s\n",
		 cli->client_name,
		 (unsigned int)cli->pid,
		 request->data.sid);

	if (!string_to_sid(&state->sid, request->data.sid)) {
		D_WARNING("Returning NT_STATUS_INVALID_PARAMETER.\n"
			  "Could not get convert sid %s from string\n",
			  request->data.sid);
		tevent_req_nterror(req, NT_STATUS_INVALID_PARAMETER);
		return tevent_req_post(req, ev);
	}

	subreq = wb_gettoken_send(state, ev, &state->sid, true);
	if (tevent_req_nomem(subreq, req)) {
		return tevent_req_post(req, ev);
	}
	tevent_req_set_callback(subreq, winbindd_getusersids_done, req);
	return req;
}

static void winbindd_getusersids_done(struct tevent_req *subreq)
{
	struct tevent_req *req = tevent_req_callback_data(
		subreq, struct tevent_req);
	struct winbindd_getusersids_state *state = tevent_req_data(
		req, struct winbindd_getusersids_state);
	NTSTATUS status;

	status = wb_gettoken_recv(subreq, state, &state->num_sids,
				  &state->sids);
	TALLOC_FREE(subreq);
	if (tevent_req_nterror(req, status)) {
		D_WARNING("wb_gettoken_recv failed with %s.\n",
			  nt_errstr(status));
		return;
	}
	tevent_req_done(req);
}

NTSTATUS winbindd_getusersids_recv(struct tevent_req *req,
				   struct winbindd_response *response)
{
	struct winbindd_getusersids_state *state = tevent_req_data(
		req, struct winbindd_getusersids_state);
	struct dom_sid_buf sidbuf;
	NTSTATUS status;
	uint32_t i;
	char *result;

	if (tevent_req_is_nterror(req, &status)) {
		D_WARNING("Could not convert sid %s: %s\n",
			  dom_sid_str_buf(&state->sid, &sidbuf),
			  nt_errstr(status));
		return status;
	}

	result = talloc_strdup(response, "");
	if (result == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	D_NOTICE("Winbind external command GETUSERSIDS end.\n"
		 "Got %"PRIu32" SID(s).\n", state->num_sids);
	for (i=0; i<state->num_sids; i++) {
		D_NOTICE("%"PRIu32": %s\n",
			 i,
			 dom_sid_str_buf(&state->sids[i], &sidbuf));
		talloc_asprintf_addbuf(
			&result,
			"%s\n",
			dom_sid_str_buf(&state->sids[i], &sidbuf));
	}

	response->data.num_entries = state->num_sids;
	response->extra_data.data = result;
	response->length += talloc_get_size(result);
	return NT_STATUS_OK;
}
