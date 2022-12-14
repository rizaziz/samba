/*
   Unix SMB/CIFS implementation.
   async implementation of WINBINDD_GETUSERDOMGROUPS
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

struct winbindd_getuserdomgroups_state {
	struct dom_sid sid;
	uint32_t num_sids;
	struct dom_sid *sids;
};

static void winbindd_getuserdomgroups_done(struct tevent_req *subreq);

struct tevent_req *winbindd_getuserdomgroups_send(TALLOC_CTX *mem_ctx,
						  struct tevent_context *ev,
						  struct winbindd_cli_state *cli,
						  struct winbindd_request *request)
{
	struct tevent_req *req, *subreq;
	struct winbindd_getuserdomgroups_state *state;

	req = tevent_req_create(mem_ctx, &state,
				struct winbindd_getuserdomgroups_state);
	if (req == NULL) {
		return NULL;
	}

	/* Ensure null termination */
	request->data.sid[sizeof(request->data.sid)-1]='\0';

	D_NOTICE("[%s (%u)] Winbind external command GETUSERDOMGROUPS start.\n"
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

	subreq = wb_gettoken_send(state, ev, &state->sid, false);
	if (tevent_req_nomem(subreq, req)) {
		return tevent_req_post(req, ev);
	}
	tevent_req_set_callback(subreq, winbindd_getuserdomgroups_done, req);
	return req;
}

static void winbindd_getuserdomgroups_done(struct tevent_req *subreq)
{
	struct tevent_req *req = tevent_req_callback_data(
		subreq, struct tevent_req);
	struct winbindd_getuserdomgroups_state *state = tevent_req_data(
		req, struct winbindd_getuserdomgroups_state);
	NTSTATUS status;

	status = wb_gettoken_recv(subreq, state, &state->num_sids,
				  &state->sids);
	TALLOC_FREE(subreq);
	if (tevent_req_nterror(req, status)) {
		return;
	}
	tevent_req_done(req);
}

NTSTATUS winbindd_getuserdomgroups_recv(struct tevent_req *req,
					struct winbindd_response *response)
{
	struct winbindd_getuserdomgroups_state *state = tevent_req_data(
		req, struct winbindd_getuserdomgroups_state);
	NTSTATUS status;
	uint32_t i;
	char *sidlist;

	if (tevent_req_is_nterror(req, &status)) {
		D_WARNING("Failed with %s.\n", nt_errstr(status));
		return status;
	}

	sidlist = talloc_strdup(response, "");
	if (sidlist == NULL) {
		return NT_STATUS_NO_MEMORY;
	}
	D_NOTICE("Winbind external command GETUSERDOMGROUPS end.\n"
		 "Received %"PRIu32" entries.\n",
		 state->num_sids);
	for (i=0; i<state->num_sids; i++) {
		struct dom_sid_buf tmp;
		sidlist = talloc_asprintf_append_buffer(
			sidlist, "%s\n",
			dom_sid_str_buf(&state->sids[i], &tmp));
		if (sidlist == NULL) {
			return NT_STATUS_NO_MEMORY;
		}
		D_NOTICE("%"PRIu32": %s\n",
			 i, dom_sid_str_buf(&state->sids[i], &tmp));
	}
	response->extra_data.data = sidlist;
	response->length += talloc_get_size(sidlist);
	response->data.num_entries = state->num_sids;
	return NT_STATUS_OK;
}
