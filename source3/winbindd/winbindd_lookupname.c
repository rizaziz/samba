/*
   Unix SMB/CIFS implementation.
   async implementation of WINBINDD_LOOKUPNAME
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
#include "libcli/security/dom_sid.h"

struct winbindd_lookupname_state {
	struct tevent_context *ev;
	struct dom_sid sid;
	enum lsa_SidType type;
};

static void winbindd_lookupname_done(struct tevent_req *subreq);

struct tevent_req *winbindd_lookupname_send(TALLOC_CTX *mem_ctx,
					    struct tevent_context *ev,
					    struct winbindd_cli_state *cli,
					    struct winbindd_request *request)
{
	struct tevent_req *req, *subreq;
	struct winbindd_lookupname_state *state;
	char *p = NULL;
	const char *domname = NULL;
	const char *name = NULL;
	const char *namespace = NULL;

	req = tevent_req_create(mem_ctx, &state,
				struct winbindd_lookupname_state);
	if (req == NULL) {
		return NULL;
	}

	D_NOTICE("[%s (%u)] Winbind external command LOOKUPNAME start.\n",
		 cli->client_name,
		 (unsigned int)cli->pid);

	state->ev = ev;

	/* Ensure null termination */
	request->data.name.dom_name[
		sizeof(request->data.name.dom_name)-1]='\0';
	request->data.name.name[sizeof(request->data.name.name)-1]='\0';

	if (strlen(request->data.name.dom_name) == 0) {
		/* cope with the name being a fully qualified name */
		p = strstr(request->data.name.name, lp_winbind_separator());
		if (p != NULL) {
			*p = '\0';
			domname = request->data.name.name;
			namespace = domname;
			name = p + 1;
		} else {
			p = strchr(request->data.name.name, '@');
			if (p != NULL) {
				/* upn */
				namespace = p + 1;
			} else {
				namespace = "";
			}
			domname = "";
			name = request->data.name.name;
		}
	} else {
		domname = request->data.name.dom_name;
		namespace = domname;
		name = request->data.name.name;
	}

	D_NOTICE("lookupname %s%s%s\n", domname, lp_winbind_separator(), name);

	subreq = wb_lookupname_send(state, ev, namespace, domname, name, 0);
	if (tevent_req_nomem(subreq, req)) {
		return tevent_req_post(req, ev);
	}
	tevent_req_set_callback(subreq, winbindd_lookupname_done, req);
	return req;
}

static void winbindd_lookupname_done(struct tevent_req *subreq)
{
	struct tevent_req *req = tevent_req_callback_data(
		subreq, struct tevent_req);
	struct winbindd_lookupname_state *state = tevent_req_data(
		req, struct winbindd_lookupname_state);
	NTSTATUS status;

	status = wb_lookupname_recv(subreq, &state->sid, &state->type);
	TALLOC_FREE(subreq);
	if (tevent_req_nterror(req, status)) {
		return;
	}
	tevent_req_done(req);
}

NTSTATUS winbindd_lookupname_recv(struct tevent_req *req,
				  struct winbindd_response *response)
{
	struct winbindd_lookupname_state *state = tevent_req_data(
		req, struct winbindd_lookupname_state);
	NTSTATUS status;

	D_NOTICE("Winbind external command LOOKUPNAME end.\n");
	if (tevent_req_is_nterror(req, &status)) {
		struct dom_sid_buf buf;
		D_WARNING("Could not convert SID %s, error is %s\n",
			  dom_sid_str_buf(&state->sid, &buf),
			  nt_errstr(status));
		return status;
	}
	sid_to_fstring(response->data.sid.sid, &state->sid);
	response->data.sid.type = state->type;
	return NT_STATUS_OK;
}
